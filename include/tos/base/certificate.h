#ifndef TOS_BASE_CERTIFICATE_H_
#define TOS_BASE_CERTIFICATE_H_

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include "tos/base/filesystem.h"
#include "tos/base/result.h"

namespace tos {

/// Metadata from a client certificate that has been verified against its private key.
struct ClientCertificateInfo final {
    std::chrono::system_clock::time_point not_after;
    std::string sha256_fingerprint;
};

/// Loads or creates an RSA private key and returns a PEM-encoded certificate signing request.
///
/// If private_key_path does not exist, a new unencrypted PKCS#8 RSA private key is generated with
/// rsa_key_bits bits, written atomically to that path, and restricted to owner read/write access
/// on POSIX. Existing keys are reused and are never overwritten. The CSR uses a SHA-256
/// signature and a subject common name supplied by the caller. Filesystem and PEM failures are
/// returned as structured Status values; OpenSSL diagnostics are not exposed.
[[nodiscard]] Result<std::string> CreateCertificateSigningRequest(const Path& private_key_path,
                                                                  std::string_view common_name,
                                                                  std::size_t rsa_key_bits = 3072);

/// Verifies PEM-encoded client certificate material before it is made active locally.
///
/// The certificate must be currently valid, have the TLS client-authentication extended usage, and
/// match private_key_path. The Gateway TLS connection authenticates the issuer; this helper checks
/// the locally actionable properties of the returned credential.
[[nodiscard]] Result<ClientCertificateInfo> ValidateClientCertificate(
    std::string_view certificate_pem, const Path& private_key_path);

/// Reports whether a PEM certificate expires at or before the current time plus renewal_window.
/// Missing, malformed, and unreadable certificates return structured Status values. A negative
/// renewal window is rejected with kInvalidArgument.
[[nodiscard]] Result<bool> CertificateNeedsRenewal(const Path& certificate_path,
                                                   std::chrono::hours renewal_window);

}  // namespace tos

#endif  // TOS_BASE_CERTIFICATE_H_
