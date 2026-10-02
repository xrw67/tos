#include "tos/base/certificate.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string>
#include <utility>

#include "crypto_internal.h"
#include "tos/base/strconv.h"

namespace tos {
namespace {

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using RequestPtr = std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)>;
using CertificatePtr = std::unique_ptr<X509, decltype(&X509_free)>;

Status InvalidArgument(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kInvalidArgument, message};
}

Status InternalError(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kInternal, message};
}

Result<BioPtr> InputBio(std::string_view pem) {
    if (pem.empty()) return InvalidArgument("PEM input is empty");
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

Result<std::string> CreateRequestPem(EVP_PKEY* key, std::string_view common_name) {
    if (common_name.size() > static_cast<std::size_t>(INT_MAX)) {
        return Status(StatusCode::kOutOfRange, "certificate common name is too long");
    }
    if (common_name.empty() || common_name.find('\0') != std::string_view::npos ||
        !strconv_detail::IsValidUtf8(common_name)) {
        return InvalidArgument("certificate common name must be nonempty UTF-8 without NUL");
    }
    RequestPtr request(X509_REQ_new(), X509_REQ_free);
    auto output = OutputBio();
    if (!output) return std::move(output).status();
    if (!request || X509_REQ_set_version(request.get(), 0L) != 1 ||
        X509_REQ_set_pubkey(request.get(), key) != 1) {
        return InternalError("OpenSSL could not initialize certificate signing request");
    }
    X509_NAME* subject = X509_REQ_get_subject_name(request.get());
    if (subject == nullptr) return InternalError("OpenSSL could not initialize CSR subject");
    if (X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>(common_name.data()),
                                   static_cast<int>(common_name.size()), -1, 0) != 1) {
        return InvalidArgument("certificate common name is not a valid X.509 common name");
    }
    if (X509_REQ_sign(request.get(), key, EVP_sha256()) <= 0 ||
        PEM_write_bio_X509_REQ(output.value().get(), request.get()) != 1) {
        return InternalError("OpenSSL could not create certificate signing request");
    }
    return BioString(output.value().get());
}

Result<CertificatePtr> ParseCertificate(std::string_view certificate_pem) {
    auto input = InputBio(certificate_pem);
    if (!input) {
        return std::move(input).status();
    }
    ERR_clear_error();
    CertificatePtr certificate(PEM_read_bio_X509(input.value().get(), nullptr, nullptr, nullptr),
                               X509_free);
    if (!certificate) {
        return InvalidArgument("PEM input is not a valid X.509 certificate");
    }
    return certificate;
}

Result<std::string> CertificateFingerprint(X509* certificate) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digest_size = 0;
    ERR_clear_error();
    if (X509_digest(certificate, EVP_sha256(), digest, &digest_size) != 1) {
        return InternalError("OpenSSL could not fingerprint certificate");
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string fingerprint;
    fingerprint.reserve(static_cast<std::size_t>(digest_size) * 2);
    for (unsigned int index = 0; index < digest_size; ++index) {
        fingerprint.push_back(kHex[(digest[index] >> 4U) & 0x0fU]);
        fingerprint.push_back(kHex[digest[index] & 0x0fU]);
    }
    return fingerprint;
}

Result<std::chrono::system_clock::time_point> ReadTime(const ASN1_TIME* value) {
    if (!value || ASN1_TIME_check(value) != 1)
        return InvalidArgument("certificate contains an invalid validity time");
    using TimePtr = std::unique_ptr<ASN1_TIME, decltype(&ASN1_TIME_free)>;
    TimePtr epoch(ASN1_TIME_new(), ASN1_TIME_free);
    if (!epoch || ASN1_TIME_set_string_X509(epoch.get(), "19700101000000Z") != 1)
        return InternalError("OpenSSL could not initialize certificate time conversion");
    int days = 0;
    int seconds = 0;
    if (ASN1_TIME_diff(&days, &seconds, epoch.get(), value) != 1)
        return InvalidArgument("certificate contains an invalid validity time");
    const std::int64_t total = static_cast<std::int64_t>(days) * 86400 + seconds;
    using Clock = std::chrono::system_clock;
    // Whole-second bounds are rounded toward zero: the accepted conversion cannot overflow
    // the platform clock's duration, even when its native precision is nanoseconds.
    const auto minimum = std::chrono::duration_cast<std::chrono::seconds>(Clock::duration::min());
    const auto maximum = std::chrono::duration_cast<std::chrono::seconds>(Clock::duration::max());
    if (total < minimum.count() || total > maximum.count())
        return Status(StatusCode::kOutOfRange, "certificate time is outside system clock range");
    return Clock::time_point(
        std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(total)));
}

Status MovedCertificate() {
    return Status(StatusCode::kFailedPrecondition, "certificate has been moved from");
}

}  // namespace

struct Certificate::Impl {
    Impl(CertificatePtr certificate, CertificateInfo metadata)
        : certificate(std::move(certificate)), info(std::move(metadata)) {}
    CertificatePtr certificate;
    const CertificateInfo info;
    // OpenSSL purpose checks may lazily populate X.509 caches.
    mutable std::mutex mutex;
};

Certificate::Certificate(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Certificate::Certificate(Certificate&&) noexcept = default;
Certificate& Certificate::operator=(Certificate&&) noexcept = default;
Certificate::~Certificate() = default;

Result<Certificate> ParseCertificatePem(std::string_view pem) {
    auto certificate = ParseCertificate(pem);
    if (!certificate) return std::move(certificate).status();
    auto before = ReadTime(X509_get0_notBefore(certificate->get()));
    if (!before) return std::move(before).status();
    auto after = ReadTime(X509_get0_notAfter(certificate->get()));
    if (!after) return std::move(after).status();
    auto fingerprint = CertificateFingerprint(certificate->get());
    if (!fingerprint) return std::move(fingerprint).status();
    CertificateInfo info{*before, *after, std::move(fingerprint).value()};
    return Certificate(
        std::make_unique<Certificate::Impl>(std::move(certificate).value(), std::move(info)));
}

Result<CertificateInfo> Certificate::GetInfo() const {
    if (!impl_) return MovedCertificate();
    return impl_->info;
}

Status Certificate::CheckValidityAt(std::chrono::system_clock::time_point at) const {
    if (!impl_) return MovedCertificate();
    if (at < impl_->info.not_before || at >= impl_->info.not_after)
        return InvalidArgument("certificate is not valid at the supplied time");
    return Status::Ok();
}

Status Certificate::CheckPurpose(CertificatePurpose purpose) const {
    if (!impl_) return MovedCertificate();
    int native_purpose;
    switch (purpose) {
        case CertificatePurpose::kTlsClient:
            native_purpose = X509_PURPOSE_SSL_CLIENT;
            break;
        case CertificatePurpose::kTlsServer:
            native_purpose = X509_PURPOSE_SSL_SERVER;
            break;
        default:
            return InvalidArgument("unknown certificate purpose");
    }
    std::lock_guard<std::mutex> guard(impl_->mutex);
    ERR_clear_error();
    if (X509_check_purpose(impl_->certificate.get(), native_purpose, 0) != 1)
        return InvalidArgument("certificate does not support the requested purpose");
    return Status::Ok();
}

Status Certificate::CheckPrivateKey(const RsaPrivateKey& key) const {
    if (!impl_) return MovedCertificate();
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    if (!native) return Status(StatusCode::kFailedPrecondition, "key has been moved from");
    std::lock_guard<std::mutex> guard(impl_->mutex);
    ERR_clear_error();
    if (X509_check_private_key(impl_->certificate.get(), native) != 1)
        return InvalidArgument("certificate does not match private key");
    return Status::Ok();
}

Result<std::string> CreateCertificateSigningRequest(const RsaPrivateKey& key,
                                                    std::string_view common_name) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    if (!native) return Status(StatusCode::kFailedPrecondition, "key has been moved from");
    ERR_clear_error();
    return CreateRequestPem(native, common_name);
}

}  // namespace tos
