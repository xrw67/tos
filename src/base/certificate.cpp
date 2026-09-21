#include "tos/base/certificate.h"

#include <climits>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

namespace tos {
namespace {

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using RequestPtr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>;
using CertificatePtr = std::unique_ptr<X509, decltype(&X509_free)>;

#if !defined(_WIN32)
constexpr int kPrivateKeyFileMode = S_IRUSR | S_IWUSR;
#endif

Status InvalidArgument(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kInvalidArgument, message};
}

Status InternalError(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kInternal, message};
}

Status PermissionError(const Path& path) {
    ERR_clear_error();
    return {StatusCode::kPermissionDenied,
            "could not restrict private key permissions '" + path.utf8() + "'"};
}

Result<BioPtr> InputBio(std::string_view pem) {
    if (pem.size() > static_cast<std::size_t>(INT_MAX)) {
        return Status(StatusCode::kOutOfRange, "PEM input exceeds OpenSSL size limits");
    }
    ERR_clear_error();
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return InternalError("OpenSSL could not allocate a PEM input buffer");
    }
    return BioPtr(bio, BIO_free);
}

Result<BioPtr> OutputBio() {
    ERR_clear_error();
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return InternalError("OpenSSL could not allocate a PEM output buffer");
    }
    return BioPtr(bio, BIO_free);
}

Result<std::string> BioString(BIO* bio) {
    char* data = nullptr;
    const long length = BIO_get_mem_data(bio, &data);
    if (length < 0 || data == nullptr) {
        return InternalError("OpenSSL could not read a PEM output buffer");
    }
    return std::string(data, static_cast<std::size_t>(length));
}

Result<PkeyPtr> ReadPrivateKey(std::string_view pem) {
    if (pem.find("-----BEGIN ENCRYPTED PRIVATE KEY-----") != std::string_view::npos ||
        pem.find("Proc-Type: 4,ENCRYPTED") != std::string_view::npos) {
        return Status(StatusCode::kUnimplemented, "encrypted PEM private keys are not supported");
    }
    auto input = InputBio(pem);
    if (!input) {
        return std::move(input).status();
    }
    ERR_clear_error();
    EVP_PKEY* key = PEM_read_bio_PrivateKey(input.value().get(), nullptr, nullptr, nullptr);
    if (key == nullptr) {
        return InvalidArgument("PEM input is not a valid unencrypted private key");
    }
    return PkeyPtr(key, EVP_PKEY_free);
}

Result<PkeyPtr> GeneratePrivateKey(std::size_t bits) {
    if (bits < 2048 || bits > static_cast<std::size_t>(INT_MAX)) {
        return InvalidArgument("RSA key size must be between 2048 and INT_MAX bits");
    }
    ERR_clear_error();
    PkeyCtxPtr context(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_keygen_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), static_cast<int>(bits)) <= 0) {
        return InternalError("OpenSSL could not initialize RSA key generation");
    }
    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_generate(context.get(), &key) <= 0 || key == nullptr) {
        return InternalError("OpenSSL RSA key generation failed");
    }
    return PkeyPtr(key, EVP_PKEY_free);
}

Result<std::string> ExportPrivateKey(EVP_PKEY* key) {
    auto output = OutputBio();
    if (!output) {
        return std::move(output).status();
    }
    ERR_clear_error();
    if (PEM_write_bio_PrivateKey(output.value().get(), key, nullptr, nullptr, 0, nullptr,
                                 nullptr) != 1) {
        return InternalError("OpenSSL could not export a private key as PEM");
    }
    return BioString(output.value().get());
}

Result<std::string> CreateRequestPem(EVP_PKEY* key, std::string_view common_name) {
    if (common_name.size() > static_cast<std::size_t>(INT_MAX)) {
        return Status(StatusCode::kOutOfRange, "certificate common name is too long");
    }
    RequestPtr request(X509_REQ_new(), X509_REQ_free);
    auto output = OutputBio();
    if (!request || !output || X509_REQ_set_version(request.get(), 0L) != 1 ||
        X509_REQ_set_pubkey(request.get(), key) != 1) {
        return InternalError("OpenSSL could not initialize certificate signing request");
    }
    X509_NAME* subject = X509_REQ_get_subject_name(request.get());
    if (subject == nullptr ||
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>(common_name.data()),
                                   static_cast<int>(common_name.size()), -1, 0) != 1 ||
        X509_REQ_sign(request.get(), key, EVP_sha256()) <= 0 ||
        PEM_write_bio_X509_REQ(output.value().get(), request.get()) != 1) {
        return InternalError("OpenSSL could not create certificate signing request");
    }
    return BioString(output.value().get());
}

}  // namespace

Result<std::string> CreateCertificateSigningRequest(const Path& private_key_path,
                                                    std::string_view common_name,
                                                    std::size_t rsa_key_bits) {
    auto existing_key = ReadTextFile(private_key_path);
    PkeyPtr key(nullptr, EVP_PKEY_free);
    if (existing_key) {
        auto parsed = ReadPrivateKey(existing_key.value());
        if (!parsed) {
            return std::move(parsed).status();
        }
        key = std::move(parsed).value();
    } else if (existing_key.status().code() == StatusCode::kNotFound) {
        auto generated = GeneratePrivateKey(rsa_key_bits);
        if (!generated) {
            return std::move(generated).status();
        }
        auto private_key_pem = ExportPrivateKey(generated.value().get());
        if (!private_key_pem) {
            return std::move(private_key_pem).status();
        }
        Status written = WriteTextFileAtomic(private_key_path, private_key_pem.value());
        if (!written) {
            return std::move(written);
        }
#if !defined(_WIN32)
        if (::chmod(private_key_path.utf8().c_str(), kPrivateKeyFileMode) != 0) {
            return PermissionError(private_key_path);
        }
#endif
        key = std::move(generated).value();
    } else {
        return std::move(existing_key).status();
    }
    return CreateRequestPem(key.get(), common_name);
}

Result<bool> CertificateNeedsRenewal(const Path& certificate_path,
                                     std::chrono::hours renewal_window) {
    if (renewal_window.count() < 0) {
        return InvalidArgument("certificate renewal window must not be negative");
    }
    auto certificate_text = ReadTextFile(certificate_path);
    if (!certificate_text) {
        return std::move(certificate_text).status();
    }
    auto input = InputBio(certificate_text.value());
    if (!input) {
        return std::move(input).status();
    }
    ERR_clear_error();
    CertificatePtr certificate(PEM_read_bio_X509(input.value().get(), nullptr, nullptr, nullptr),
                               X509_free);
    if (!certificate) {
        return InvalidArgument("PEM input is not a valid X.509 certificate");
    }
    auto renewal_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + renewal_window);
    const int comparison = X509_cmp_time(X509_get0_notAfter(certificate.get()), &renewal_time);
    if (comparison == -2) {
        return InvalidArgument("certificate has an invalid expiration time");
    }
    return comparison <= 0;
}

}  // namespace tos
