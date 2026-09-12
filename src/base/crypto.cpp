#include "tos/base/crypto.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <utility>

namespace tos {
namespace {

using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using MdPtr = std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

constexpr std::uint8_t kEmptyByte = 0;

const unsigned char* DataPointer(span<const std::uint8_t> input) noexcept {
    return input.empty() ? &kEmptyByte : input.data();
}

Status InvalidArgument(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kInvalidArgument, message};
}

Status OutOfRange(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kOutOfRange, message};
}

Status Unimplemented(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kUnimplemented, message};
}

Status InternalError(std::string_view operation) {
    ERR_clear_error();
    return {StatusCode::kInternal, operation};
}

Status Unauthenticated(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kUnauthenticated, message};
}

Status FailedPrecondition(std::string_view message) {
    ERR_clear_error();
    return {StatusCode::kFailedPrecondition, message};
}

const char* DigestName(HashAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case HashAlgorithm::kMd5:
            return "MD5";
        case HashAlgorithm::kSha1:
            return "SHA1";
        case HashAlgorithm::kSha256:
            return "SHA256";
        case HashAlgorithm::kSha384:
            return "SHA384";
        case HashAlgorithm::kSha512:
            return "SHA512";
    }
    return nullptr;
}

Result<MdPtr> FetchDigest(const char* name) {
    ERR_clear_error();
    EVP_MD* digest = EVP_MD_fetch(nullptr, name, nullptr);
    if (digest == nullptr) {
        return Unimplemented("OpenSSL digest is unavailable from the active provider");
    }
    return MdPtr(digest, EVP_MD_free);
}

bool IsBase64Character(char character) noexcept {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '+' || character == '/';
}

bool IsBase64UrlCharacter(char character) noexcept {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '_';
}

int Base64Value(char character) noexcept {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+' || character == '-') {
        return 62;
    }
    if (character == '/' || character == '_') {
        return 63;
    }
    return -1;
}

Status ValidateStandardBase64(std::string_view encoded) {
    if (encoded.size() % 4 != 0) {
        return InvalidArgument("Base64 input must have required padding");
    }

    std::size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') {
        padding = 1;
        if (encoded.size() >= 2 && encoded[encoded.size() - 2] == '=') {
            padding = 2;
        }
    }
    if (padding > 2 || (padding != 0 && encoded.size() < 4)) {
        return InvalidArgument("Base64 input has invalid padding");
    }

    const std::size_t content_size = encoded.size() - padding;
    for (std::size_t index = 0; index < content_size; ++index) {
        if (!IsBase64Character(encoded[index])) {
            return InvalidArgument("Base64 input contains an invalid character");
        }
    }
    for (std::size_t index = content_size; index < encoded.size(); ++index) {
        if (encoded[index] != '=') {
            return InvalidArgument("Base64 padding must appear only at the end");
        }
    }
    if (padding == 1 && (Base64Value(encoded[content_size - 1]) & 0x03) != 0) {
        return InvalidArgument("Base64 input has non-canonical pad bits");
    }
    if (padding == 2 && (Base64Value(encoded[content_size - 1]) & 0x0f) != 0) {
        return InvalidArgument("Base64 input has non-canonical pad bits");
    }
    return Status::Ok();
}

Result<std::vector<std::uint8_t>> DecodeStandardBase64(std::string_view encoded) {
    Status validation = ValidateStandardBase64(encoded);
    if (!validation) {
        return validation;
    }
    if (encoded.empty()) {
        return std::vector<std::uint8_t>{};
    }
    if (encoded.size() > static_cast<std::size_t>(INT_MAX)) {
        return OutOfRange("Base64 input exceeds OpenSSL size limits");
    }

    std::vector<std::uint8_t> decoded((encoded.size() / 4) * 3);
    ERR_clear_error();
    const int decoded_size =
        EVP_DecodeBlock(decoded.data(), reinterpret_cast<const unsigned char*>(encoded.data()),
                        static_cast<int>(encoded.size()));
    if (decoded_size < 0) {
        return InternalError("OpenSSL Base64 decode failed");
    }
    const std::size_t padding = !encoded.empty() && encoded.back() == '='
                                    ? (encoded[encoded.size() - 2] == '=' ? 2U : 1U)
                                    : 0U;
    decoded.resize(static_cast<std::size_t>(decoded_size) - padding);
    return decoded;
}

Result<std::string> EncodeStandardBase64(span<const std::uint8_t> input) {
    if (input.size() > static_cast<std::size_t>(INT_MAX) ||
        input.size() > (std::numeric_limits<std::size_t>::max() - 2) / 3) {
        return OutOfRange("Base64 input exceeds OpenSSL size limits");
    }

    const std::size_t output_size = ((input.size() + 2) / 3) * 4;
    if (output_size > static_cast<std::size_t>(INT_MAX)) {
        return OutOfRange("Base64 output exceeds OpenSSL size limits");
    }
    std::string encoded(output_size, '\0');
    if (input.empty()) {
        return encoded;
    }
    ERR_clear_error();
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]),
                                        DataPointer(input), static_cast<int>(input.size()));
    if (written < 0 || static_cast<std::size_t>(written) != output_size) {
        return InternalError("OpenSSL Base64 encode failed");
    }
    return encoded;
}

bool IsEncryptedPem(std::string_view pem) noexcept {
    return pem.find("-----BEGIN ENCRYPTED PRIVATE KEY-----") != std::string_view::npos ||
           pem.find("Proc-Type: 4,ENCRYPTED") != std::string_view::npos;
}

Result<BioPtr> InputBio(std::string_view pem) {
    if (pem.size() > static_cast<std::size_t>(INT_MAX)) {
        return OutOfRange("PEM input exceeds OpenSSL size limits");
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
    BUF_MEM* contents = nullptr;
    BIO_get_mem_ptr(bio, &contents);
    if (contents == nullptr || contents->data == nullptr) {
        return InternalError("OpenSSL could not read a PEM output buffer");
    }
    return std::string(contents->data, contents->length);
}

Result<PkeyPtr> ReadPrivateKey(std::string_view pem, const char* expected_type,
                               bool requires_rsa_size) {
    if (IsEncryptedPem(pem)) {
        return Unimplemented("encrypted PEM private keys are not supported");
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
    if (EVP_PKEY_is_a(key, expected_type) != 1) {
        EVP_PKEY_free(key);
        return InvalidArgument("PEM private key has an unexpected algorithm");
    }
    if (requires_rsa_size && EVP_PKEY_get_bits(key) < 2048) {
        EVP_PKEY_free(key);
        return InvalidArgument("RSA private key must use at least 2048 bits");
    }
    return PkeyPtr(key, EVP_PKEY_free);
}

Result<PkeyPtr> ReadPublicKey(std::string_view pem, const char* expected_type,
                              bool requires_rsa_size) {
    auto input = InputBio(pem);
    if (!input) {
        return std::move(input).status();
    }
    ERR_clear_error();
    EVP_PKEY* key = PEM_read_bio_PUBKEY(input.value().get(), nullptr, nullptr, nullptr);
    if (key == nullptr) {
        return InvalidArgument("PEM input is not a valid public key");
    }
    if (EVP_PKEY_is_a(key, expected_type) != 1) {
        EVP_PKEY_free(key);
        return InvalidArgument("PEM public key has an unexpected algorithm");
    }
    if (requires_rsa_size && EVP_PKEY_get_bits(key) < 2048) {
        EVP_PKEY_free(key);
        return InvalidArgument("RSA public key must use at least 2048 bits");
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

Result<std::string> ExportPublicKey(EVP_PKEY* key) {
    auto output = OutputBio();
    if (!output) {
        return std::move(output).status();
    }
    ERR_clear_error();
    if (PEM_write_bio_PUBKEY(output.value().get(), key) != 1) {
        return InternalError("OpenSSL could not export a public key as PEM");
    }
    return BioString(output.value().get());
}

Result<MdPtr> FetchSha256() { return FetchDigest("SHA256"); }

Status ConfigureRsaPss(EVP_PKEY_CTX* context, const EVP_MD* sha256) {
    if (EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PSS_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context, sha256) <= 0 ||
        EVP_PKEY_CTX_set_rsa_pss_saltlen(context, RSA_PSS_SALTLEN_DIGEST) <= 0) {
        return InternalError("OpenSSL could not configure RSA-PSS");
    }
    return Status::Ok();
}

Status ConfigureRsaOaep(EVP_PKEY_CTX* context, const EVP_MD* sha256) {
    if (EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_OAEP_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_oaep_md(context, sha256) <= 0 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context, sha256) <= 0) {
        return InternalError("OpenSSL could not configure RSA-OAEP");
    }
    return Status::Ok();
}

}  // namespace

namespace detail {

struct CryptoKey {
    explicit CryptoKey(PkeyPtr value) : value(std::move(value)) {}

    PkeyPtr value;
};

struct CryptoAccess {
    static EVP_PKEY* Get(const RsaPrivateKey& key) noexcept {
        return key.key_ ? key.key_->value.get() : nullptr;
    }
    static EVP_PKEY* Get(const RsaPublicKey& key) noexcept {
        return key.key_ ? key.key_->value.get() : nullptr;
    }
    static EVP_PKEY* Get(const Ed25519PrivateKey& key) noexcept {
        return key.key_ ? key.key_->value.get() : nullptr;
    }
    static EVP_PKEY* Get(const Ed25519PublicKey& key) noexcept {
        return key.key_ ? key.key_->value.get() : nullptr;
    }

    static RsaPrivateKey MakeRsaPrivate(PkeyPtr key) {
        return RsaPrivateKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static RsaPublicKey MakeRsaPublic(PkeyPtr key) {
        return RsaPublicKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static Ed25519PrivateKey MakeEd25519Private(PkeyPtr key) {
        return Ed25519PrivateKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static Ed25519PublicKey MakeEd25519Public(PkeyPtr key) {
        return Ed25519PublicKey(std::make_unique<CryptoKey>(std::move(key)));
    }
};

}  // namespace detail

RsaPrivateKey::RsaPrivateKey(std::unique_ptr<detail::CryptoKey> key) noexcept
    : key_(std::move(key)) {}
RsaPrivateKey::RsaPrivateKey(RsaPrivateKey&&) noexcept = default;
RsaPrivateKey& RsaPrivateKey::operator=(RsaPrivateKey&&) noexcept = default;
RsaPrivateKey::~RsaPrivateKey() = default;

RsaPublicKey::RsaPublicKey(std::unique_ptr<detail::CryptoKey> key) noexcept
    : key_(std::move(key)) {}
RsaPublicKey::RsaPublicKey(RsaPublicKey&&) noexcept = default;
RsaPublicKey& RsaPublicKey::operator=(RsaPublicKey&&) noexcept = default;
RsaPublicKey::~RsaPublicKey() = default;

Ed25519PrivateKey::Ed25519PrivateKey(std::unique_ptr<detail::CryptoKey> key) noexcept
    : key_(std::move(key)) {}
Ed25519PrivateKey::Ed25519PrivateKey(Ed25519PrivateKey&&) noexcept = default;
Ed25519PrivateKey& Ed25519PrivateKey::operator=(Ed25519PrivateKey&&) noexcept = default;
Ed25519PrivateKey::~Ed25519PrivateKey() = default;

Ed25519PublicKey::Ed25519PublicKey(std::unique_ptr<detail::CryptoKey> key) noexcept
    : key_(std::move(key)) {}
Ed25519PublicKey::Ed25519PublicKey(Ed25519PublicKey&&) noexcept = default;
Ed25519PublicKey& Ed25519PublicKey::operator=(Ed25519PublicKey&&) noexcept = default;
Ed25519PublicKey::~Ed25519PublicKey() = default;

namespace {

Status RequireKey(EVP_PKEY* key) {
    return key == nullptr ? FailedPrecondition("key has been moved from") : Status::Ok();
}

}  // namespace

Result<std::vector<std::uint8_t>> Hash(HashAlgorithm algorithm, span<const std::uint8_t> input) {
    const char* name = DigestName(algorithm);
    if (name == nullptr) {
        return InvalidArgument("unknown hash algorithm");
    }
    auto digest = FetchDigest(name);
    if (!digest) {
        return std::move(digest).status();
    }
    std::vector<std::uint8_t> output(EVP_MD_get_size(digest.value().get()));
    std::size_t output_size = output.size();
    ERR_clear_error();
    if (EVP_Q_digest(nullptr, name, nullptr, DataPointer(input), input.size(), output.data(),
                     &output_size) != 1) {
        return InternalError("OpenSSL digest operation failed");
    }
    output.resize(output_size);
    return output;
}

Result<std::string> HashHex(HashAlgorithm algorithm, span<const std::uint8_t> input) {
    auto digest = Hash(algorithm, input);
    if (!digest) {
        return std::move(digest).status();
    }

    constexpr char kHexDigits[] = "0123456789abcdef";
    const auto& bytes = digest.value();
    std::string text(bytes.size() * 2, '\0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        text[index * 2] = kHexDigits[bytes[index] >> 4];
        text[index * 2 + 1] = kHexDigits[bytes[index] & 0x0f];
    }
    return text;
}

Result<std::string> Base64Encode(span<const std::uint8_t> input) {
    return EncodeStandardBase64(input);
}

Result<std::vector<std::uint8_t>> Base64Decode(std::string_view encoded) {
    return DecodeStandardBase64(encoded);
}

Result<std::string> Base64UrlEncode(span<const std::uint8_t> input) {
    auto encoded = EncodeStandardBase64(input);
    if (!encoded) {
        return std::move(encoded).status();
    }
    std::string output = std::move(encoded).value();
    std::replace(output.begin(), output.end(), '+', '-');
    std::replace(output.begin(), output.end(), '/', '_');
    while (!output.empty() && output.back() == '=') {
        output.pop_back();
    }
    return output;
}

Result<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view encoded) {
    if (encoded.size() > static_cast<std::size_t>(INT_MAX) - 3) {
        return OutOfRange("Base64url input exceeds OpenSSL size limits");
    }
    if (encoded.find('=') != std::string_view::npos || encoded.size() % 4 == 1) {
        return InvalidArgument("Base64url input has invalid padding");
    }
    for (const char character : encoded) {
        if (!IsBase64UrlCharacter(character)) {
            return InvalidArgument("Base64url input contains an invalid character");
        }
    }
    std::string standard(encoded);
    std::replace(standard.begin(), standard.end(), '-', '+');
    std::replace(standard.begin(), standard.end(), '_', '/');
    standard.append((4 - standard.size() % 4) % 4, '=');
    return DecodeStandardBase64(standard);
}

Result<RsaPrivateKey> ParseRsaPrivateKeyPem(std::string_view pem) {
    auto key = ReadPrivateKey(pem, "RSA", true);
    if (!key) {
        return std::move(key).status();
    }
    return detail::CryptoAccess::MakeRsaPrivate(std::move(key).value());
}

Result<RsaPublicKey> ParseRsaPublicKeyPem(std::string_view pem) {
    auto key = ReadPublicKey(pem, "RSA", true);
    if (!key) {
        return std::move(key).status();
    }
    return detail::CryptoAccess::MakeRsaPublic(std::move(key).value());
}

Result<RsaPrivateKey> GenerateRsaPrivateKey(std::size_t bits) {
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
    return detail::CryptoAccess::MakeRsaPrivate(PkeyPtr(key, EVP_PKEY_free));
}

Result<std::string> ExportRsaPrivateKeyPem(const RsaPrivateKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    return ExportPrivateKey(native);
}

Result<std::string> ExportRsaPublicKeyPem(const RsaPublicKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    return ExportPublicKey(native);
}

Result<RsaPublicKey> DeriveRsaPublicKey(const RsaPrivateKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto pem = ExportPublicKey(native);
    if (!pem) {
        return std::move(pem).status();
    }
    return ParseRsaPublicKeyPem(pem.value());
}

Result<std::vector<std::uint8_t>> RsaPssSign(const RsaPrivateKey& key,
                                             span<const std::uint8_t> message) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto digest = FetchSha256();
    if (!digest) {
        return std::move(digest).status();
    }
    ERR_clear_error();
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* key_context = nullptr;
    if (!context || EVP_DigestSignInit(context.get(), &key_context, digest.value().get(), nullptr,
                                       native) <= 0) {
        return InternalError("OpenSSL could not initialize RSA-PSS signing");
    }
    Status configuration = ConfigureRsaPss(key_context, digest.value().get());
    if (!configuration) {
        return configuration;
    }
    if (EVP_DigestSignUpdate(context.get(), DataPointer(message), message.size()) <= 0) {
        return InternalError("OpenSSL RSA-PSS signing failed");
    }
    std::size_t signature_size = 0;
    if (EVP_DigestSignFinal(context.get(), nullptr, &signature_size) <= 0) {
        return InternalError("OpenSSL could not size an RSA-PSS signature");
    }
    std::vector<std::uint8_t> signature(signature_size);
    if (EVP_DigestSignFinal(context.get(), signature.data(), &signature_size) <= 0) {
        return InternalError("OpenSSL RSA-PSS signing failed");
    }
    signature.resize(signature_size);
    return signature;
}

Status RsaPssVerify(const RsaPublicKey& key, span<const std::uint8_t> message,
                    span<const std::uint8_t> signature) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto digest = FetchSha256();
    if (!digest) {
        return std::move(digest).status();
    }
    ERR_clear_error();
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* key_context = nullptr;
    if (!context || EVP_DigestVerifyInit(context.get(), &key_context, digest.value().get(), nullptr,
                                         native) <= 0) {
        return InternalError("OpenSSL could not initialize RSA-PSS verification");
    }
    Status configuration = ConfigureRsaPss(key_context, digest.value().get());
    if (!configuration) {
        return configuration;
    }
    if (EVP_DigestVerifyUpdate(context.get(), DataPointer(message), message.size()) <= 0) {
        return InternalError("OpenSSL RSA-PSS verification failed");
    }
    const int verified =
        EVP_DigestVerifyFinal(context.get(), DataPointer(signature), signature.size());
    if (verified == 1) {
        ERR_clear_error();
        return Status::Ok();
    }
    if (verified == 0) {
        return Unauthenticated("RSA-PSS signature verification failed");
    }
    return InternalError("OpenSSL RSA-PSS verification failed");
}

Result<std::vector<std::uint8_t>> RsaOaepEncrypt(const RsaPublicKey& key,
                                                 span<const std::uint8_t> plaintext) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto digest = FetchSha256();
    if (!digest) {
        return std::move(digest).status();
    }
    const int key_size = EVP_PKEY_get_size(native);
    constexpr std::size_t kSha256OaepOverhead = 66;
    if (key_size <= static_cast<int>(kSha256OaepOverhead) ||
        plaintext.size() > static_cast<std::size_t>(key_size) - kSha256OaepOverhead) {
        return InvalidArgument("RSA-OAEP plaintext exceeds the key's maximum size");
    }
    ERR_clear_error();
    PkeyCtxPtr context(EVP_PKEY_CTX_new(native, nullptr), EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0) {
        return InternalError("OpenSSL could not initialize RSA-OAEP encryption");
    }
    Status configuration = ConfigureRsaOaep(context.get(), digest.value().get());
    if (!configuration) {
        return configuration;
    }
    std::size_t output_size = 0;
    if (EVP_PKEY_encrypt(context.get(), nullptr, &output_size, DataPointer(plaintext),
                         plaintext.size()) <= 0) {
        return InternalError("OpenSSL could not size RSA-OAEP ciphertext");
    }
    std::vector<std::uint8_t> ciphertext(output_size);
    if (EVP_PKEY_encrypt(context.get(), ciphertext.data(), &output_size, DataPointer(plaintext),
                         plaintext.size()) <= 0) {
        return InternalError("OpenSSL RSA-OAEP encryption failed");
    }
    ciphertext.resize(output_size);
    return ciphertext;
}

Result<std::vector<std::uint8_t>> RsaOaepDecrypt(const RsaPrivateKey& key,
                                                 span<const std::uint8_t> ciphertext) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto digest = FetchSha256();
    if (!digest) {
        return std::move(digest).status();
    }
    const int key_size = EVP_PKEY_get_size(native);
    if (key_size <= 0 || ciphertext.size() != static_cast<std::size_t>(key_size)) {
        return Unauthenticated("RSA-OAEP ciphertext is invalid");
    }
    ERR_clear_error();
    PkeyCtxPtr context(EVP_PKEY_CTX_new(native, nullptr), EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_decrypt_init(context.get()) <= 0) {
        return InternalError("OpenSSL could not initialize RSA-OAEP decryption");
    }
    Status configuration = ConfigureRsaOaep(context.get(), digest.value().get());
    if (!configuration) {
        return configuration;
    }
    std::size_t output_size = 0;
    if (EVP_PKEY_decrypt(context.get(), nullptr, &output_size, DataPointer(ciphertext),
                         ciphertext.size()) <= 0) {
        return Unauthenticated("RSA-OAEP ciphertext is invalid");
    }
    std::vector<std::uint8_t> plaintext(output_size);
    if (EVP_PKEY_decrypt(context.get(), plaintext.data(), &output_size, DataPointer(ciphertext),
                         ciphertext.size()) <= 0) {
        return Unauthenticated("RSA-OAEP ciphertext is invalid");
    }
    plaintext.resize(output_size);
    return plaintext;
}

Result<Ed25519PrivateKey> ParseEd25519PrivateKeyPem(std::string_view pem) {
    auto key = ReadPrivateKey(pem, "ED25519", false);
    if (!key) {
        return std::move(key).status();
    }
    return detail::CryptoAccess::MakeEd25519Private(std::move(key).value());
}

Result<Ed25519PublicKey> ParseEd25519PublicKeyPem(std::string_view pem) {
    auto key = ReadPublicKey(pem, "ED25519", false);
    if (!key) {
        return std::move(key).status();
    }
    return detail::CryptoAccess::MakeEd25519Public(std::move(key).value());
}

Result<Ed25519PrivateKey> GenerateEd25519PrivateKey() {
    ERR_clear_error();
    PkeyCtxPtr context(EVP_PKEY_CTX_new_from_name(nullptr, "ED25519", nullptr), EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_keygen_init(context.get()) <= 0) {
        return InternalError("OpenSSL could not initialize Ed25519 key generation");
    }
    EVP_PKEY* key = nullptr;
    if (EVP_PKEY_generate(context.get(), &key) <= 0 || key == nullptr) {
        return InternalError("OpenSSL Ed25519 key generation failed");
    }
    return detail::CryptoAccess::MakeEd25519Private(PkeyPtr(key, EVP_PKEY_free));
}

Result<std::string> ExportEd25519PrivateKeyPem(const Ed25519PrivateKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    return ExportPrivateKey(native);
}

Result<std::string> ExportEd25519PublicKeyPem(const Ed25519PublicKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    return ExportPublicKey(native);
}

Result<Ed25519PublicKey> DeriveEd25519PublicKey(const Ed25519PrivateKey& key) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    auto pem = ExportPublicKey(native);
    if (!pem) {
        return std::move(pem).status();
    }
    return ParseEd25519PublicKeyPem(pem.value());
}

Result<std::vector<std::uint8_t>> Ed25519Sign(const Ed25519PrivateKey& key,
                                              span<const std::uint8_t> message) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    ERR_clear_error();
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, native) <= 0) {
        return InternalError("OpenSSL could not initialize Ed25519 signing");
    }
    std::size_t signature_size = 0;
    if (EVP_DigestSign(context.get(), nullptr, &signature_size, DataPointer(message),
                       message.size()) <= 0) {
        return InternalError("OpenSSL could not size an Ed25519 signature");
    }
    std::vector<std::uint8_t> signature(signature_size);
    if (EVP_DigestSign(context.get(), signature.data(), &signature_size, DataPointer(message),
                       message.size()) <= 0) {
        return InternalError("OpenSSL Ed25519 signing failed");
    }
    signature.resize(signature_size);
    return signature;
}

Status Ed25519Verify(const Ed25519PublicKey& key, span<const std::uint8_t> message,
                     span<const std::uint8_t> signature) {
    EVP_PKEY* native = detail::CryptoAccess::Get(key);
    Status available = RequireKey(native);
    if (!available) {
        return available;
    }
    ERR_clear_error();
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, native) <= 0) {
        return InternalError("OpenSSL could not initialize Ed25519 verification");
    }
    const int verified = EVP_DigestVerify(context.get(), DataPointer(signature), signature.size(),
                                          DataPointer(message), message.size());
    if (verified == 1) {
        ERR_clear_error();
        return Status::Ok();
    }
    if (verified == 0) {
        return Unauthenticated("Ed25519 signature verification failed");
    }
    return InternalError("OpenSSL Ed25519 verification failed");
}

}  // namespace tos
