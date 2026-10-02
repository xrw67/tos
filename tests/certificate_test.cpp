#include "tos/base/certificate.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "test_util.h"

namespace tos {
namespace {
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using RequestPtr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>;

std::string CertificatePem(std::string_view key_pem, const char* before, const char* after,
                           const char* eku = "clientAuth", const char* usage = nullptr,
                           bool corrupt_signature = false) {
    BioPtr input(BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size())), BIO_free);
    PkeyPtr key(PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    X509Ptr cert(X509_new(), X509_free);
    BioPtr output(BIO_new(BIO_s_mem()), BIO_free);
    if (!key || !cert || !output || X509_set_version(cert.get(), 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) != 1 ||
        ASN1_TIME_set_string(X509_getm_notBefore(cert.get()), before) != 1 ||
        ASN1_TIME_set_string(X509_getm_notAfter(cert.get()), after) != 1 ||
        X509_set_pubkey(cert.get(), key.get()) != 1)
        throw std::runtime_error("certificate fixture initialization failed");
    auto* subject = X509_get_subject_name(cert.get());
    if (!subject ||
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("fixture"), -1, -1,
                                   0) != 1 ||
        X509_set_issuer_name(cert.get(), subject) != 1)
        throw std::runtime_error("certificate fixture subject failed");
    for (const auto& extension :
         {std::make_pair(NID_ext_key_usage, eku), std::make_pair(NID_key_usage, usage)}) {
        if (!extension.second) continue;
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> value(
            X509V3_EXT_conf_nid(nullptr, nullptr, extension.first,
                                const_cast<char*>(extension.second)),
            X509_EXTENSION_free);
        if (!value || X509_add_ext(cert.get(), value.get(), -1) != 1)
            throw std::runtime_error("certificate fixture extension failed");
    }
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0)
        throw std::runtime_error("certificate fixture signing failed");
    if (corrupt_signature) {
        const ASN1_BIT_STRING* signature = nullptr;
        X509_get0_signature(&signature, nullptr, cert.get());
        auto* bytes = const_cast<unsigned char*>(ASN1_STRING_get0_data(signature));
        bytes[ASN1_STRING_length(signature) - 1] ^= 1;
    }
    if (PEM_write_bio_X509(output.get(), cert.get()) != 1)
        throw std::runtime_error("certificate fixture serialization failed");
    char* data = nullptr;
    const long length = BIO_get_mem_data(output.get(), &data);
    return std::string(data, static_cast<std::size_t>(length));
}

const char* kBefore = "20240101000000Z";
const char* kAfter = "20300101000000Z";

struct Material {
    RsaPrivateKey key;
    std::string pem;
};
Material MakeMaterial() {
    auto key = GenerateRsaPrivateKey(2048);
    if (!key) throw std::runtime_error("key fixture failed");
    auto pem = ExportRsaPrivateKeyPem(key.value());
    if (!pem) throw std::runtime_error("key fixture export failed");
    return Material{std::move(key).value(), std::move(pem).value()};
}
static_assert(!std::is_copy_constructible_v<Certificate>);
static_assert(!std::is_copy_assignable_v<Certificate>);
static_assert(std::is_nothrow_move_constructible_v<Certificate>);
static_assert(std::is_nothrow_move_assignable_v<Certificate>);
static_assert(std::is_nothrow_destructible_v<Certificate>);

TEST(CertificateTest, CreatesVerifiableCsrWithoutChangingPrivateKey) {
    auto material = MakeMaterial();
    const auto csr = CreateCertificateSigningRequest(material.key, "example.你好");
    ASSERT_TRUE(csr) << csr.status().ToString();
    BioPtr input(BIO_new_mem_buf(csr->data(), static_cast<int>(csr->size())), BIO_free);
    RequestPtr request(PEM_read_bio_X509_REQ(input.get(), nullptr, nullptr, nullptr),
                       X509_REQ_free);
    ASSERT_TRUE(request);
    PkeyPtr public_key(X509_REQ_get_pubkey(request.get()), EVP_PKEY_free);
    ASSERT_TRUE(public_key);
    EXPECT_EQ(X509_REQ_verify(request.get(), public_key.get()), 1);
    EXPECT_EQ(X509_REQ_get_signature_nid(request.get()), NID_sha256WithRSAEncryption);
    EXPECT_EQ(EVP_PKEY_get_bits(public_key.get()), 2048);
    auto* subject = X509_REQ_get_subject_name(request.get());
    EXPECT_EQ(X509_NAME_entry_count(subject), 1);
    const auto* entry = X509_NAME_get_entry(subject, 0);
    auto* value = X509_NAME_ENTRY_get_data(entry);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(value)),
                          static_cast<std::size_t>(ASN1_STRING_length(value))),
              "example.你好");
    const auto exported = ExportRsaPrivateKeyPem(material.key);
    ASSERT_TRUE(exported);
    EXPECT_EQ(*exported, material.pem);
    BioPtr key_input(BIO_new_mem_buf(material.pem.data(), static_cast<int>(material.pem.size())),
                     BIO_free);
    PkeyPtr private_key(PEM_read_bio_PrivateKey(key_input.get(), nullptr, nullptr, nullptr),
                        EVP_PKEY_free);
    ASSERT_TRUE(private_key);
    EXPECT_EQ(EVP_PKEY_eq(public_key.get(), private_key.get()), 1);
    const auto leaf = ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter));
    ASSERT_TRUE(leaf);
    EXPECT_TRUE(leaf->CheckPrivateKey(material.key));
    const auto repeated = CreateCertificateSigningRequest(material.key, "example.你好");
    ASSERT_TRUE(repeated);
    EXPECT_EQ(*repeated, *csr);
}

TEST(CertificateTest, ReportsInvalidCsrNamesAndMovedKeys) {
    auto material = MakeMaterial();
    for (const auto& name : {std::string(), std::string("bad\0name", 8), std::string("\xff", 1),
                             std::string(100, 'x')}) {
        auto result = CreateCertificateSigningRequest(material.key, name);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
    }
    auto owner = std::move(material.key);
    auto moved = CreateCertificateSigningRequest(material.key, "name");
    ASSERT_FALSE(moved);
    EXPECT_EQ(moved.status().code(), StatusCode::kFailedPrecondition);
    EXPECT_TRUE(CreateCertificateSigningRequest(owner, "name"));
}

TEST(CertificateTest, OwnsMetadataAndFingerprintsDerRatherThanPemFormatting) {
    auto material = MakeMaterial();
    auto pem = CertificatePem(material.pem, kBefore, kAfter);
    const auto certificate = ParseCertificatePem(pem);
    ASSERT_TRUE(certificate);
    const auto info = certificate->GetInfo();
    ASSERT_TRUE(info);
    EXPECT_EQ(info->not_before,
              std::chrono::system_clock::time_point(std::chrono::seconds(1704067200)));
    EXPECT_EQ(info->not_after,
              std::chrono::system_clock::time_point(std::chrono::seconds(1893456000)));
    EXPECT_EQ(info->sha256_fingerprint.size(), 64U);
    EXPECT_EQ(info->sha256_fingerprint.find_first_not_of("0123456789abcdef"), std::string::npos);
    BioPtr input(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    X509Ptr native(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    ASSERT_TRUE(native);
    const int length = i2d_X509(native.get(), nullptr);
    ASSERT_GT(length, 0);
    std::string der(static_cast<std::size_t>(length), '\0');
    auto* bytes = reinterpret_cast<unsigned char*>(der.data());
    ASSERT_EQ(i2d_X509(native.get(), &bytes), length);
    EXPECT_EQ(info->sha256_fingerprint, Sha256String(der));
    const auto trailing =
        ParseCertificatePem(pem + CertificatePem(material.pem, kBefore, "20310101000000Z"));
    ASSERT_TRUE(trailing);
    auto trailing_info = trailing->GetInfo();
    ASSERT_TRUE(trailing_info);
    EXPECT_EQ(trailing_info->sha256_fingerprint, info->sha256_fingerprint);
    pem.clear();
    EXPECT_TRUE(certificate->CheckPrivateKey(material.key));
}

TEST(CertificateTest, ChecksExactValidityBoundariesWithoutReadingClock) {
    auto material = MakeMaterial();
    auto certificate = ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter));
    ASSERT_TRUE(certificate);
    auto info = certificate->GetInfo();
    ASSERT_TRUE(info);
    const auto tick = std::chrono::system_clock::duration(1);
    EXPECT_FALSE(certificate->CheckValidityAt(info->not_before - tick));
    EXPECT_TRUE(certificate->CheckValidityAt(info->not_before));
    EXPECT_TRUE(certificate->CheckValidityAt(info->not_after - tick));
    EXPECT_FALSE(certificate->CheckValidityAt(info->not_after));
    EXPECT_FALSE(certificate->CheckValidityAt(std::chrono::system_clock::time_point::min()));
    EXPECT_FALSE(certificate->CheckValidityAt(std::chrono::system_clock::time_point::max()));
    EXPECT_TRUE(
        ParseCertificatePem(CertificatePem(material.pem, "20000101000000Z", "20010101000000Z")));
    EXPECT_TRUE(
        ParseCertificatePem(CertificatePem(material.pem, "21000101000000Z", "21010101000000Z")));
}

TEST(CertificateTest, ChecksPurposesIndependentlyAndAllowsUnrestrictedEku) {
    auto material = MakeMaterial();
    for (const char* eku : std::initializer_list<const char*>{"clientAuth", "serverAuth",
                                                              "clientAuth,serverAuth", nullptr}) {
        auto certificate = ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter, eku));
        ASSERT_TRUE(certificate);
        EXPECT_EQ(certificate->CheckPurpose(CertificatePurpose::kTlsClient).ok(),
                  !eku || std::string_view(eku).find("clientAuth") != std::string_view::npos);
        EXPECT_EQ(certificate->CheckPurpose(CertificatePurpose::kTlsServer).ok(),
                  !eku || std::string_view(eku).find("serverAuth") != std::string_view::npos);
        EXPECT_EQ(certificate->CheckPurpose(static_cast<CertificatePurpose>(99)).code(),
                  StatusCode::kInvalidArgument);
    }
    auto wrong_usage = ParseCertificatePem(
        CertificatePem(material.pem, kBefore, kAfter, "clientAuth", "keyCertSign"));
    ASSERT_TRUE(wrong_usage);
    EXPECT_FALSE(wrong_usage->CheckPurpose(CertificatePurpose::kTlsClient));
}

TEST(CertificateTest, MatchesBorrowedKeyAndRejectsMismatchAndMovedKey) {
    auto material = MakeMaterial();
    auto other = MakeMaterial();
    auto certificate = ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter));
    ASSERT_TRUE(certificate);
    EXPECT_TRUE(certificate->CheckPrivateKey(material.key));
    EXPECT_EQ(certificate->CheckPrivateKey(other.key).code(), StatusCode::kInvalidArgument);
    auto owner = std::move(material.key);
    EXPECT_EQ(certificate->CheckPrivateKey(material.key).code(), StatusCode::kFailedPrecondition);
    EXPECT_TRUE(certificate->CheckPrivateKey(owner));
}

TEST(CertificateTest, DoesNotAuthenticateSignatureOrCombinePropertyChecks) {
    auto material = MakeMaterial();
    const auto pem = CertificatePem(material.pem, "20000101000000Z", "20010101000000Z",
                                    "clientAuth", nullptr, true);
    BioPtr input(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    X509Ptr native(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    ASSERT_TRUE(native);
    PkeyPtr public_key(X509_get_pubkey(native.get()), EVP_PKEY_free);
    ASSERT_TRUE(public_key);
    EXPECT_EQ(X509_verify(native.get(), public_key.get()), 0);
    auto certificate = ParseCertificatePem(pem);
    ASSERT_TRUE(certificate);
    EXPECT_TRUE(certificate->GetInfo());
    EXPECT_TRUE(certificate->CheckPrivateKey(material.key));
    EXPECT_TRUE(certificate->CheckPurpose(CertificatePurpose::kTlsClient));
    EXPECT_FALSE(certificate->CheckValidityAt(std::chrono::system_clock::now()));
}

TEST(CertificateTest, SupportsMoveAssignmentAndIndependentMetadataLifetime) {
    auto material = MakeMaterial();
    auto first = ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter));
    auto second = ParseCertificatePem(CertificatePem(material.pem, kBefore, "20310101000000Z"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto info = first->GetInfo();
    ASSERT_TRUE(info);
    Certificate owner(std::move(first).value());
    EXPECT_EQ(first->GetInfo().status().code(), StatusCode::kFailedPrecondition);
    EXPECT_EQ(first->CheckPurpose(CertificatePurpose::kTlsClient).code(),
              StatusCode::kFailedPrecondition);
    EXPECT_EQ(first->CheckValidityAt(info->not_before).code(), StatusCode::kFailedPrecondition);
    EXPECT_EQ(first->CheckPrivateKey(material.key).code(), StatusCode::kFailedPrecondition);
    *second = std::move(owner);
    EXPECT_EQ(owner.GetInfo().status().code(), StatusCode::kFailedPrecondition);
    Certificate* self = &*second;
    *second = std::move(*self);
    auto copied = second->GetInfo();
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied->sha256_fingerprint, info->sha256_fingerprint);
}

TEST(CertificateTest, ParsesPreEpochDatesAndRejectsMalformedOrUnrepresentableDates) {
    EXPECT_EQ(ParseCertificatePem("").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseCertificatePem("not PEM").status().code(), StatusCode::kInvalidArgument);
    auto material = MakeMaterial();
    auto old =
        ParseCertificatePem(CertificatePem(material.pem, "19691230000000Z", "19691231235959Z"));
    ASSERT_TRUE(old);
    auto info = old->GetInfo();
    ASSERT_TRUE(info);
    EXPECT_EQ(info->not_after, std::chrono::system_clock::time_point(std::chrono::seconds(-1)));
    auto far = ParseCertificatePem(CertificatePem(material.pem, kBefore, "99991231235959Z"));
    const auto max_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max())
            .count();
    if (max_seconds < 253402300799LL) {
        ASSERT_FALSE(far);
        EXPECT_EQ(far.status().code(), StatusCode::kOutOfRange);
    } else {
        EXPECT_TRUE(far);
    }
    const auto valid_pem = CertificatePem(material.pem, kBefore, kAfter);
    BioPtr input(BIO_new_mem_buf(valid_pem.data(), static_cast<int>(valid_pem.size())), BIO_free);
    X509Ptr native(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    ASSERT_TRUE(native);
    ASSERT_EQ(ASN1_STRING_set(X509_getm_notAfter(native.get()), "20309901000000Z", 15), 1);
    // Re-encode the modified TBS; parsed X.509 objects otherwise retain the original DER cache.
    ASSERT_GT(i2d_re_X509_tbs(native.get(), nullptr), 0);
    BioPtr output(BIO_new(BIO_s_mem()), BIO_free);
    ASSERT_EQ(PEM_write_bio_X509(output.get(), native.get()), 1);
    char* data = nullptr;
    const long length = BIO_get_mem_data(output.get(), &data);
    auto malformed = ParseCertificatePem(std::string_view(data, static_cast<std::size_t>(length)));
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.status().code(), StatusCode::kInvalidArgument);
}

TEST(CertificateTest, ConcurrentConstChecksAreSafe) {
    auto material = MakeMaterial();
    auto certificate =
        ParseCertificatePem(CertificatePem(material.pem, kBefore, kAfter, "clientAuth,serverAuth"));
    ASSERT_TRUE(certificate);
    auto info = certificate->GetInfo();
    ASSERT_TRUE(info);
    test::Gate gate;
    std::atomic<bool> okay{true};
    std::vector<std::thread> threads;
    auto cleanup = MakeScopeExit([&] {
        gate.Open();
        for (auto& thread : threads)
            if (thread.joinable()) thread.join();
    });
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            gate.Wait();
            if (!CreateCertificateSigningRequest(material.key, "concurrent")) okay = false;
            for (int n = 0; n < 50; ++n) {
                if (!certificate->GetInfo() || !certificate->CheckValidityAt(info->not_before) ||
                    !certificate->CheckPurpose(CertificatePurpose::kTlsClient) ||
                    !certificate->CheckPurpose(CertificatePurpose::kTlsServer) ||
                    !certificate->CheckPrivateKey(material.key))
                    okay = false;
            }
        });
    }
    gate.Open();
    for (auto& thread : threads) thread.join();
    EXPECT_TRUE(okay.load());
}

}  // namespace
}  // namespace tos
