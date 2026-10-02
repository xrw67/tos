#ifndef TOS_BASE_CERTIFICATE_H_
#define TOS_BASE_CERTIFICATE_H_

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "tos/base/crypto.h"
#include "tos/base/result.h"

namespace tos {

/// Owned metadata from a parsed certificate, without validity, identity or trust assurances.
/// The fingerprint is SHA-256 of the DER certificate, encoded as 64 lowercase hex characters.
struct CertificateInfo final {
    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point not_after;
    std::string sha256_fingerprint;
};

enum class CertificatePurpose { kTlsClient, kTlsServer };

/// Owns a parsed X.509 certificate. No file I/O, clock reads, key generation, issuer trust,
/// signature-chain validation, hostname checking or renewal policy is performed.
/// Const operations are concurrent-safe; move and destruction require external synchronization.
/// Native failures return Status/Result without OpenSSL diagnostics. Standard-library allocation
/// exceptions propagate. Moved-from operations return kFailedPrecondition; destruction never
/// throws.
class [[nodiscard]] Certificate final {
   public:
    Certificate(const Certificate&) = delete;
    Certificate& operator=(const Certificate&) = delete;
    Certificate(Certificate&&) noexcept;
    Certificate& operator=(Certificate&&) noexcept;
    ~Certificate();

    /// Copies metadata; its lifetime is independent of this object. Copy allocation may throw.
    [[nodiscard]] Result<CertificateInfo> GetInfo() const;
    /// Checks not_before <= at < not_after; an invalid interval/time returns kInvalidArgument.
    [[nodiscard]] Status CheckValidityAt(std::chrono::system_clock::time_point at) const;
    /// Applies OpenSSL TLS leaf-purpose rules, including key usage/basic constraints/EKU.
    /// Missing EKU may permit a purpose; this does not require an explicit EKU OID.
    /// A disallowed or unknown purpose returns kInvalidArgument, without checking time or trust.
    [[nodiscard]] Status CheckPurpose(CertificatePurpose purpose) const;
    /// Checks the leaf public key against a borrowed RSA private key. A mismatch returns
    /// kInvalidArgument; a moved-from private key returns kFailedPrecondition.
    [[nodiscard]] Status CheckPrivateKey(const RsaPrivateKey& key) const;

   private:
    struct Impl;
    explicit Certificate(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
    friend Result<Certificate> ParseCertificatePem(std::string_view pem);
};

/// Parses and owns the first PEM X.509 certificate; trailing material is ignored and is not
/// verified as a chain. Input is borrowed only during this call. Expired/future certificates can
/// be parsed. Malformed PEM/dates return kInvalidArgument; size or clock range errors return
/// kOutOfRange. Standard-library allocation exceptions propagate. Concurrent calls are safe.
[[nodiscard]] Result<Certificate> ParseCertificatePem(std::string_view pem);

/// Creates a CN-only, SHA-256 signed PEM CSR using a borrowed RSA private key, without generating
/// a key or touching files. Common name must be valid nonempty UTF-8 without NUL and satisfy
/// OpenSSL's X.509 name constraints. Invalid names return kInvalidArgument, excessive input sizes
/// kOutOfRange, and a moved-from key kFailedPrecondition. Allocation exceptions propagate.
/// Concurrent calls are safe while the key remains alive and is not moved or mutated.
[[nodiscard]] Result<std::string> CreateCertificateSigningRequest(const RsaPrivateKey& key,
                                                                  std::string_view common_name);

}  // namespace tos

#endif  // TOS_BASE_CERTIFICATE_H_
