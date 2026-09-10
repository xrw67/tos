#ifndef TOS_CRYPTO_H_
#define TOS_CRYPTO_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "tos/result.h"
#include "tos/span.h"

namespace tos {

namespace detail {
struct CryptoKey;
struct CryptoAccess;
}  // namespace detail

/// Digest algorithms provided through the OpenSSL 3 default provider.
/// MD5 and SHA-1 are retained only to verify or interoperate with legacy
/// protocols. New security designs must use SHA-256, SHA-384, or SHA-512.
enum class HashAlgorithm {
    kMd5,
    kSha1,
    kSha256,
    kSha384,
    kSha512,
};

/// Computes a one-shot digest of arbitrary binary input.
///
/// The returned bytes are in the standard algorithm-defined order. An
/// unsupported OpenSSL provider algorithm returns kUnimplemented; other
/// OpenSSL failures return kInternal. Output allocation exceptions propagate.
/// This function does not retain input and is safe to call concurrently.
Result<std::vector<std::uint8_t>> Hash(HashAlgorithm algorithm, span<const std::uint8_t> input);

/// Computes a one-shot digest and encodes it as lowercase hexadecimal ASCII.
///
/// The result has no prefix or separators. Its error behavior matches Hash():
/// an unsupported provider algorithm returns kUnimplemented, an unknown enum
/// value returns kInvalidArgument, and other OpenSSL failures return kInternal.
/// Output allocation exceptions propagate. This function does not retain input
/// and is safe to call concurrently.
Result<std::string> HashHex(HashAlgorithm algorithm, span<const std::uint8_t> input);

/// Encodes binary input with RFC 4648 standard Base64 and required '=' padding.
/// It returns kOutOfRange if the input cannot be represented by OpenSSL's API.
/// Output allocation exceptions propagate. The function does not retain input
/// and is safe to call concurrently.
Result<std::string> Base64Encode(span<const std::uint8_t> input);

/// Strictly decodes RFC 4648 standard Base64.
/// Whitespace, URL-safe characters, missing padding, non-canonical pad bits,
/// and malformed input return kInvalidArgument. Output allocation exceptions
/// propagate. The function does not retain input and is safe to call concurrently.
Result<std::vector<std::uint8_t>> Base64Decode(std::string_view encoded);

/// Encodes binary input with RFC 4648 Base64url without '=' padding.
/// It returns kOutOfRange if the input cannot be represented by OpenSSL's API.
/// Output allocation exceptions propagate. The function does not retain input
/// and is safe to call concurrently.
Result<std::string> Base64UrlEncode(span<const std::uint8_t> input);

/// Strictly decodes unpadded RFC 4648 Base64url.
/// Standard Base64 characters, '=', whitespace, non-canonical pad bits, and
/// malformed input return kInvalidArgument. Output allocation exceptions
/// propagate. The function does not retain input and is safe to call concurrently.
Result<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view encoded);

/// Move-only RSA private key backed by an OpenSSL EVP_PKEY.
///
/// The key owns its native representation. It accepts only unencrypted PEM
/// input and does not erase caller-owned PEM text. Const crypto operations may
/// run concurrently; moving, assigning, or destroying the same object requires
/// caller synchronization. Construction and native allocation failures return
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

/// Move-only RSA public key backed by an OpenSSL EVP_PKEY.
/// It owns its native representation. Const crypto operations may run
/// concurrently; moving, assigning, or destroying the same object requires
/// caller synchronization. Native allocation failures return Result errors.
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

/// Move-only Ed25519 private key backed by an OpenSSL EVP_PKEY.
/// It owns its native representation. Const crypto operations may run
/// concurrently; moving, assigning, or destroying the same object requires
/// caller synchronization. The same PEM and sensitive-memory limitations as
/// RsaPrivateKey apply.
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

/// Move-only Ed25519 public key backed by an OpenSSL EVP_PKEY.
/// It owns its native representation. Const crypto operations may run
/// concurrently; moving, assigning, or destroying the same object requires
/// caller synchronization. Native allocation failures return Result errors.
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

#endif  // TOS_CRYPTO_H_
