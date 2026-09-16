#include "tos/base/crypto.h"

#include <array>
#include <climits>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
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

constexpr unsigned char kEmptyByte = 0;

const void* DataPointer(const char* data, std::size_t len) noexcept {
    return len == 0 ? static_cast<const void*>(&kEmptyByte) : static_cast<const void*>(data);
}

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

Result<MdPtr> FetchDigest(const char* name) {
    ERR_clear_error();
    EVP_MD* digest = EVP_MD_fetch(nullptr, name, nullptr);
    if (digest == nullptr) {
        return Unimplemented("OpenSSL digest is unavailable from the active provider");
    }
    return MdPtr(digest, EVP_MD_free);
}

Result<MdCtxPtr> NewDigestContext(const EVP_MD* digest) {
    ERR_clear_error();
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        return InternalError("OpenSSL could not allocate a digest context");
    }
    MdCtxPtr result(context, EVP_MD_CTX_free);
    if (EVP_DigestInit_ex2(result.get(), digest, nullptr) != 1) {
        return InternalError("OpenSSL could not initialize a digest operation");
    }
    return result;
}

Status UpdateDigest(EVP_MD_CTX* context, const char* data, std::size_t len) {
    if (len == 0) {
        return Status::Ok();
    }
    ERR_clear_error();
    if (EVP_DigestUpdate(context, DataPointer(data, len), len) != 1) {
        return InternalError("OpenSSL digest operation failed");
    }
    return Status::Ok();
}

Result<std::string> FinishDigest(EVP_MD_CTX* context) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned int size = 0;
    ERR_clear_error();
    if (EVP_DigestFinal_ex(context, bytes.data(), &size) != 1) {
        return InternalError("OpenSSL could not finalize a digest operation");
    }

    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string text(static_cast<std::size_t>(size) * 2, '\0');
    for (std::size_t index = 0; index < size; ++index) {
        text[index * 2] = kHexDigits[bytes[index] >> 4];
        text[index * 2 + 1] = kHexDigits[bytes[index] & 0x0f];
    }
    return text;
}

Result<std::string> DigestData(const char* name, const char* data, std::size_t len) {
    if (data == nullptr && len != 0) {
        return InvalidArgument("data must not be null when len is nonzero");
    }
    auto digest = FetchDigest(name);
    if (!digest) {
        return std::move(digest).status();
    }
    auto context = NewDigestContext(digest.value().get());
    if (!context) {
        return std::move(context).status();
    }
    Status updated = UpdateDigest(context.value().get(), data, len);
    if (!updated) {
        return updated;
    }
    return FinishDigest(context.value().get());
}

Status FileError(const std::error_code& error, std::string_view action,
                 const std::string& filename) {
    StatusCode code = StatusCode::kUnavailable;
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) {
        code = StatusCode::kPermissionDenied;
    } else if (error == std::errc::no_such_file_or_directory) {
        code = StatusCode::kNotFound;
    } else if (error == std::errc::no_space_on_device) {
        code = StatusCode::kResourceExhausted;
    } else if (error == std::errc::is_a_directory || error == std::errc::not_a_directory) {
        code = StatusCode::kFailedPrecondition;
    }
    return Status(code, std::string(action) + " '" + filename + "': " + error.message());
}

Result<std::string> DigestFile(const char* name, const std::string& filename) {
    const std::filesystem::path path = std::filesystem::u8path(filename);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (error) {
        return FileError(error, "could not inspect file", filename);
    }
    if (!std::filesystem::exists(status)) {
        return Status(StatusCode::kNotFound, "file does not exist '" + filename + "'");
    }
    if (!std::filesystem::is_regular_file(status)) {
        return Status(StatusCode::kFailedPrecondition,
                      "path is not a regular file '" + filename + "'");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return Status(StatusCode::kUnavailable, "could not open file '" + filename + "'");
    }
    auto digest = FetchDigest(name);
    if (!digest) {
        return std::move(digest).status();
    }
    auto context = NewDigestContext(digest.value().get());
    if (!context) {
        return std::move(context).status();
    }

    std::array<char, 16 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            Status updated =
                UpdateDigest(context.value().get(), buffer.data(), static_cast<std::size_t>(count));
            if (!updated) {
                return updated;
            }
        }
    }
    if (!input.eof()) {
        return Status(StatusCode::kUnavailable, "could not read file '" + filename + "'");
    }
    return FinishDigest(context.value().get());
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

Result<std::string> Md5File(const std::string& filename) { return DigestFile("MD5", filename); }

std::string Md5Data(const char* data, std::size_t len) {
    auto result = DigestData("MD5", data, len);
    return result ? std::move(result).value() : std::string();
}

std::string Md5String(std::string_view data) { return Md5Data(data.data(), data.size()); }

Result<std::string> Sha1File(const std::string& filename) { return DigestFile("SHA1", filename); }

std::string Sha1Data(const char* data, std::size_t len) {
    auto result = DigestData("SHA1", data, len);
    return result ? std::move(result).value() : std::string();
}

std::string Sha1String(std::string_view data) { return Sha1Data(data.data(), data.size()); }

Result<std::string> Sha256File(const std::string& filename) {
    return DigestFile("SHA256", filename);
}

std::string Sha256Data(const char* data, std::size_t len) {
    auto result = DigestData("SHA256", data, len);
    return result ? std::move(result).value() : std::string();
}

std::string Sha256String(std::string_view data) { return Sha256Data(data.data(), data.size()); }

Result<std::string> Sha384File(const std::string& filename) {
    return DigestFile("SHA384", filename);
}

std::string Sha384Data(const char* data, std::size_t len) {
    auto result = DigestData("SHA384", data, len);
    return result ? std::move(result).value() : std::string();
}

std::string Sha384String(std::string_view data) { return Sha384Data(data.data(), data.size()); }

Result<std::string> Sha512File(const std::string& filename) {
    return DigestFile("SHA512", filename);
}

std::string Sha512Data(const char* data, std::size_t len) {
    auto result = DigestData("SHA512", data, len);
    return result ? std::move(result).value() : std::string();
}

std::string Sha512String(std::string_view data) { return Sha512Data(data.data(), data.size()); }

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
