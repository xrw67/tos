#ifndef TOS_BASE_CRYPTO_H_
#define TOS_BASE_CRYPTO_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "tos/base/result.h"
#include "tos/base/span.h"

namespace tos {

namespace detail {
struct CryptoKey;
struct CryptoAccess;
}  // namespace detail

/// Computes lowercase hexadecimal MD5 hashes through OpenSSL's default provider.
/// MD5 is only for legacy protocols; use SHA-2 for new security designs. Md5File streams its
/// borrowed filename: missing files return kNotFound, non-regular files kFailedPrecondition,
/// unavailable OpenSSL support kUnimplemented, other provider failures kInternal, and I/O failures
/// a classified Status. Md5Data and Md5String borrow input and return an empty string only for a
/// null nonempty buffer or provider failure. Path and allocation exceptions propagate. Thread-safe.
[[nodiscard]] Result<std::string> Md5File(const std::string& filename);
[[nodiscard]] std::string Md5Data(const char* data, std::size_t len);
[[nodiscard]] std::string Md5String(std::string_view data);

/// Computes SHA-1 hashes with Md5File, Md5Data, and Md5String semantics.
/// SHA-1 is only for legacy protocols; use SHA-2 for new security designs.
[[nodiscard]] Result<std::string> Sha1File(const std::string& filename);
[[nodiscard]] std::string Sha1Data(const char* data, std::size_t len);
[[nodiscard]] std::string Sha1String(std::string_view data);

/// Computes SHA-256 hashes with Md5File, Md5Data, and Md5String semantics.
[[nodiscard]] Result<std::string> Sha256File(const std::string& filename);
[[nodiscard]] std::string Sha256Data(const char* data, std::size_t len);
[[nodiscard]] std::string Sha256String(std::string_view data);

/// Computes SHA-384 hashes with Md5File, Md5Data, and Md5String semantics.
[[nodiscard]] Result<std::string> Sha384File(const std::string& filename);
[[nodiscard]] std::string Sha384Data(const char* data, std::size_t len);
[[nodiscard]] std::string Sha384String(std::string_view data);

/// Computes SHA-512 hashes with Md5File, Md5Data, and Md5String semantics.
[[nodiscard]] Result<std::string> Sha512File(const std::string& filename);
[[nodiscard]] std::string Sha512Data(const char* data, std::size_t len);
[[nodiscard]] std::string Sha512String(std::string_view data);

/// Move-only RSA private key owning an OpenSSL EVP_PKEY.
/// It accepts unencrypted PEM only and does not erase caller-owned text. Const operations are
/// concurrent-safe; mutation and destruction need caller synchronization. Native failures return
/// Result errors; standard-library allocation exceptions propagate.
class RsaPrivateKey {
   public:
    RsaPrivateKey(const RsaPrivateKey&) = delete;
    RsaPrivateKey& operator=(const RsaPrivateKey&) = delete;
    RsaPrivateKey(RsaPrivateKey&&) noexcept;
    RsaPrivateKey& operator=(RsaPrivateKey&&) noexcept;
    ~RsaPrivateKey();

   private:
    explicit RsaPrivateKey(std::unique_ptr<detail::CryptoKey> key) noexcept;

    std::unique_ptr<detail::CryptoKey> key_;

    friend struct detail::CryptoAccess;
};

/// Move-only RSA public key owning an OpenSSL EVP_PKEY.
/// Const operations are concurrent-safe; mutation and destruction need caller synchronization.
/// Native allocation failures return Result errors.
class RsaPublicKey {
   public:
    RsaPublicKey(const RsaPublicKey&) = delete;
    RsaPublicKey& operator=(const RsaPublicKey&) = delete;
    RsaPublicKey(RsaPublicKey&&) noexcept;
    RsaPublicKey& operator=(RsaPublicKey&&) noexcept;
    ~RsaPublicKey();

   private:
    explicit RsaPublicKey(std::unique_ptr<detail::CryptoKey> key) noexcept;

    std::unique_ptr<detail::CryptoKey> key_;

    friend struct detail::CryptoAccess;
};

/// Move-only Ed25519 private key owning an OpenSSL EVP_PKEY.
/// Const operations are concurrent-safe; mutation and destruction need caller synchronization.
/// It has the same PEM and sensitive-memory limitations as RsaPrivateKey.
class Ed25519PrivateKey {
   public:
    Ed25519PrivateKey(const Ed25519PrivateKey&) = delete;
    Ed25519PrivateKey& operator=(const Ed25519PrivateKey&) = delete;
    Ed25519PrivateKey(Ed25519PrivateKey&&) noexcept;
    Ed25519PrivateKey& operator=(Ed25519PrivateKey&&) noexcept;
    ~Ed25519PrivateKey();

   private:
    explicit Ed25519PrivateKey(std::unique_ptr<detail::CryptoKey> key) noexcept;

    std::unique_ptr<detail::CryptoKey> key_;

    friend struct detail::CryptoAccess;
};

/// Move-only Ed25519 public key owning an OpenSSL EVP_PKEY.
/// Const operations are concurrent-safe; mutation and destruction need caller synchronization.
/// Native allocation failures return Result errors.
class Ed25519PublicKey {
   public:
    Ed25519PublicKey(const Ed25519PublicKey&) = delete;
    Ed25519PublicKey& operator=(const Ed25519PublicKey&) = delete;
    Ed25519PublicKey(Ed25519PublicKey&&) noexcept;
    Ed25519PublicKey& operator=(Ed25519PublicKey&&) noexcept;
    ~Ed25519PublicKey();

   private:
    explicit Ed25519PublicKey(std::unique_ptr<detail::CryptoKey> key) noexcept;

    std::unique_ptr<detail::CryptoKey> key_;

    friend struct detail::CryptoAccess;
};

/// Parses an unencrypted PEM RSA private key with a modulus of at least 2048 bits.
/// Malformed PEM and non-RSA keys return kInvalidArgument; encrypted PEM returns
/// kUnimplemented. The caller retains and is responsible for the PEM buffer.
Result<RsaPrivateKey> ParseRsaPrivateKeyPem(std::string_view pem);
/// Parses PEM SubjectPublicKeyInfo containing an RSA public key of at least 2048 bits.
Result<RsaPublicKey> ParseRsaPublicKeyPem(std::string_view pem);
/// Generates an RSA private key. bits must be at least 2048; the default is 3072.
Result<RsaPrivateKey> GenerateRsaPrivateKey(std::size_t bits = 3072);
/// Exports an unencrypted PKCS#8 PEM private key. Output allocation exceptions propagate.
Result<std::string> ExportRsaPrivateKeyPem(const RsaPrivateKey& key);
/// Exports a SubjectPublicKeyInfo PEM public key. Output allocation exceptions propagate.
Result<std::string> ExportRsaPublicKeyPem(const RsaPublicKey& key);
/// Derives a public-only RSA key from a private key.
Result<RsaPublicKey> DeriveRsaPublicKey(const RsaPrivateKey& key);

/// Signs data using RSA-PSS with SHA-256, MGF1-SHA-256, and digest-length salt.
Result<std::vector<std::uint8_t>> RsaPssSign(const RsaPrivateKey& key,
                                             span<const std::uint8_t> message);
/// Verifies an RSA-PSS SHA-256 signature. A mismatched signature returns kUnauthenticated.
Status RsaPssVerify(const RsaPublicKey& key, span<const std::uint8_t> message,
                    span<const std::uint8_t> signature);
/// Encrypts data using RSA-OAEP-SHA-256 with MGF1-SHA-256 and an empty label.
Result<std::vector<std::uint8_t>> RsaOaepEncrypt(const RsaPublicKey& key,
                                                 span<const std::uint8_t> plaintext);
/// Decrypts RSA-OAEP-SHA-256 ciphertext. Invalid ciphertext returns kUnauthenticated.
Result<std::vector<std::uint8_t>> RsaOaepDecrypt(const RsaPrivateKey& key,
                                                 span<const std::uint8_t> ciphertext);

/// Parses an unencrypted PEM Ed25519 private key. Malformed PEM and non-Ed25519
/// keys return kInvalidArgument; encrypted PEM returns kUnimplemented.
Result<Ed25519PrivateKey> ParseEd25519PrivateKeyPem(std::string_view pem);
/// Parses PEM SubjectPublicKeyInfo containing an Ed25519 public key.
Result<Ed25519PublicKey> ParseEd25519PublicKeyPem(std::string_view pem);
/// Generates a standard Ed25519 private key using OpenSSL's configured RNG.
Result<Ed25519PrivateKey> GenerateEd25519PrivateKey();
/// Exports an unencrypted PKCS#8 PEM private key. Output allocation exceptions propagate.
Result<std::string> ExportEd25519PrivateKeyPem(const Ed25519PrivateKey& key);
/// Exports a SubjectPublicKeyInfo PEM public key. Output allocation exceptions propagate.
Result<std::string> ExportEd25519PublicKeyPem(const Ed25519PublicKey& key);
/// Derives a public-only Ed25519 key from a private key.
Result<Ed25519PublicKey> DeriveEd25519PublicKey(const Ed25519PrivateKey& key);

/// Signs data with standard pure Ed25519; it does not pre-hash the message.
Result<std::vector<std::uint8_t>> Ed25519Sign(const Ed25519PrivateKey& key,
                                              span<const std::uint8_t> message);
/// Verifies a standard pure Ed25519 signature. A mismatch returns kUnauthenticated.
Status Ed25519Verify(const Ed25519PublicKey& key, span<const std::uint8_t> message,
                     span<const std::uint8_t> signature);

}  // namespace tos

#endif  // TOS_BASE_CRYPTO_H_
