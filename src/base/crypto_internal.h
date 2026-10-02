#ifndef TOS_BASE_CRYPTO_INTERNAL_H_
#define TOS_BASE_CRYPTO_INTERNAL_H_

#include <memory>
#include <openssl/evp.h>
#include <utility>

#include "tos/base/crypto.h"

namespace tos {
namespace detail {

// Native access is confined to the crypto implementation; these headers are not installed.
using NativeKeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

struct CryptoKey {
    explicit CryptoKey(NativeKeyPtr value) : value(std::move(value)) {}

    NativeKeyPtr value;
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

    static RsaPrivateKey MakeRsaPrivate(NativeKeyPtr key) {
        return RsaPrivateKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static RsaPublicKey MakeRsaPublic(NativeKeyPtr key) {
        return RsaPublicKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static Ed25519PrivateKey MakeEd25519Private(NativeKeyPtr key) {
        return Ed25519PrivateKey(std::make_unique<CryptoKey>(std::move(key)));
    }
    static Ed25519PublicKey MakeEd25519Public(NativeKeyPtr key) {
        return Ed25519PublicKey(std::make_unique<CryptoKey>(std::move(key)));
    }
};

}  // namespace detail

}  // namespace tos

#endif  // TOS_BASE_CRYPTO_INTERNAL_H_
