#include "tos/base/certificate.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <string>
#include <string_view>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "test_util.h"
#include "tos/base/crypto.h"

namespace tos {
namespace {

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using CertificatePtr = std::unique_ptr<X509, decltype(&X509_free)>;
using RequestPtr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>;

Path TestPath(const std::filesystem::path& path) {
    auto parsed = Path::Parse(path.string());
    EXPECT_TRUE(parsed) << parsed.status().ToString();
    return std::move(parsed).value();
}

std::string SelfSignedCertificate(int not_after_seconds) {
    PkeyCtxPtr key_context(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    if (!key_context || EVP_PKEY_keygen_init(key_context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) <= 0) {
        return {};
    }
    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_generate(key_context.get(), &raw_key) <= 0 || raw_key == nullptr) {
        return {};
    }
    PkeyPtr key(raw_key, EVP_PKEY_free);

    CertificatePtr certificate(X509_new(), X509_free);
    BioPtr output(BIO_new(BIO_s_mem()), BIO_free);
    if (!certificate || !output || X509_set_version(certificate.get(), 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) != 1 ||
        X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
        X509_gmtime_adj(X509_getm_notAfter(certificate.get()), not_after_seconds) == nullptr ||
        X509_set_pubkey(certificate.get(), key.get()) != 1) {
        return {};
    }
    X509_NAME* subject = X509_get_subject_name(certificate.get());
    if (subject == nullptr ||
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("test"), -1, -1,
                                   0) != 1 ||
        X509_set_issuer_name(certificate.get(), subject) != 1 ||
        X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0 ||
        PEM_write_bio_X509(output.get(), certificate.get()) != 1) {
        return {};
    }
    char* data = nullptr;
    const long length = BIO_get_mem_data(output.get(), &data);
    return length > 0 && data != nullptr ? std::string(data, static_cast<std::size_t>(length))
                                         : std::string();
}

TEST(CertificateTest, CreatesCsrAndPersistsReusablePrivateKey) {
    test::TemporaryDirectory directory("tos-certificate-test-");
    const auto key_path = TestPath(directory.path() / "client-key.pem");

    const auto first_request = CreateCertificateSigningRequest(key_path, "agent.example");
    ASSERT_TRUE(first_request) << first_request.status().ToString();
    ASSERT_NE(first_request.value().find("-----BEGIN CERTIFICATE REQUEST-----"), std::string::npos);
    const auto first_key = ReadTextFile(key_path);
    ASSERT_TRUE(first_key) << first_key.status().ToString();
    const auto parsed_key = ParseRsaPrivateKeyPem(first_key.value());
    ASSERT_TRUE(parsed_key) << parsed_key.status().ToString();

    BioPtr request_input(BIO_new_mem_buf(first_request.value().data(),
                                         static_cast<int>(first_request.value().size())),
                         BIO_free);
    RequestPtr request(PEM_read_bio_X509_REQ(request_input.get(), nullptr, nullptr, nullptr),
                       X509_REQ_free);
    ASSERT_TRUE(request);
    char common_name[128] = {};
    ASSERT_GT(X509_NAME_get_text_by_NID(X509_REQ_get_subject_name(request.get()), NID_commonName,
                                        common_name, sizeof(common_name)),
              0);
    EXPECT_STREQ(common_name, "agent.example");

    const auto second_request = CreateCertificateSigningRequest(key_path, "agent.example");
    ASSERT_TRUE(second_request) << second_request.status().ToString();
    const auto second_key = ReadTextFile(key_path);
    ASSERT_TRUE(second_key) << second_key.status().ToString();
    EXPECT_EQ(second_key.value(), first_key.value());

#if !defined(_WIN32)
    struct stat metadata{};
    ASSERT_EQ(::stat(key_path.utf8().c_str(), &metadata), 0);
    EXPECT_EQ(metadata.st_mode & 0777, 0600);
#endif
}

TEST(CertificateTest, ReportsStructuredPrivateKeyErrors) {
    test::TemporaryDirectory directory("tos-certificate-error-test-");
    const auto key_path = TestPath(directory.path() / "client-key.pem");
    ASSERT_TRUE(WriteTextFileAtomic(key_path, "not a private key"));

    const auto malformed = CreateCertificateSigningRequest(key_path, "agent");
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.status().code(), StatusCode::kInvalidArgument);

    ASSERT_TRUE(WriteTextFileAtomic(key_path, "-----BEGIN ENCRYPTED PRIVATE KEY-----\ninvalid\n"));
    const auto encrypted = CreateCertificateSigningRequest(key_path, "agent");
    ASSERT_FALSE(encrypted);
    EXPECT_EQ(encrypted.status().code(), StatusCode::kUnimplemented);

    const auto missing_parent = TestPath(directory.path() / "missing" / "key.pem");
    const auto write_failure = CreateCertificateSigningRequest(missing_parent, "agent");
    ASSERT_FALSE(write_failure);
    EXPECT_EQ(write_failure.status().code(), StatusCode::kNotFound);
}

TEST(CertificateTest, ChecksRenewalWindowAndCertificateErrors) {
    test::TemporaryDirectory directory("tos-certificate-renewal-test-");
    const auto soon_path = TestPath(directory.path() / "soon.pem");
    const auto long_path = TestPath(directory.path() / "long.pem");
    const auto expired_path = TestPath(directory.path() / "expired.pem");

    ASSERT_TRUE(WriteTextFileAtomic(soon_path, SelfSignedCertificate(24 * 60 * 60)));
    ASSERT_TRUE(WriteTextFileAtomic(long_path, SelfSignedCertificate(30 * 24 * 60 * 60)));
    ASSERT_TRUE(WriteTextFileAtomic(expired_path, SelfSignedCertificate(-60)));

    const auto soon = CertificateNeedsRenewal(soon_path, std::chrono::hours(24 * 7));
    ASSERT_TRUE(soon) << soon.status().ToString();
    EXPECT_TRUE(soon.value());
    const auto long_lived = CertificateNeedsRenewal(long_path, std::chrono::hours(24 * 7));
    ASSERT_TRUE(long_lived) << long_lived.status().ToString();
    EXPECT_FALSE(long_lived.value());
    const auto expired = CertificateNeedsRenewal(expired_path, std::chrono::hours(24 * 7));
    ASSERT_TRUE(expired) << expired.status().ToString();
    EXPECT_TRUE(expired.value());

    const auto missing = CertificateNeedsRenewal(TestPath(directory.path() / "missing.pem"),
                                                 std::chrono::hours(24 * 7));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.status().code(), StatusCode::kNotFound);

    ASSERT_TRUE(WriteTextFileAtomic(long_path, "not a certificate"));
    const auto malformed = CertificateNeedsRenewal(long_path, std::chrono::hours(24 * 7));
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.status().code(), StatusCode::kInvalidArgument);

    const auto negative_window = CertificateNeedsRenewal(soon_path, std::chrono::hours(-1));
    ASSERT_FALSE(negative_window);
    EXPECT_EQ(negative_window.status().code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace tos
