#include "tos/base/crypto.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>
#include <utility>
#include <vector>

namespace tos {
namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()),
            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size()};
}

std::vector<std::uint8_t> Hex(std::string_view text) {
    EXPECT_EQ(text.size() % 2, 0U);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const auto value = [](char character) -> std::uint8_t {
            if (character >= '0' && character <= '9') {
                return static_cast<std::uint8_t>(character - '0');
            }
            if (character >= 'a' && character <= 'f') {
                return static_cast<std::uint8_t>(character - 'a' + 10);
            }
            return static_cast<std::uint8_t>(character - 'A' + 10);
        };
        bytes.push_back(
            static_cast<std::uint8_t>((value(text[index]) << 4) | value(text[index + 1])));
    }
    return bytes;
}

span<const std::uint8_t> View(const std::vector<std::uint8_t>& bytes) {
    return {bytes.data(), bytes.size()};
}

TEST(CryptoHashTest, MatchesKnownDigestVectors) {
    const std::vector<std::uint8_t> input = Bytes("abc");
    const struct {
        HashAlgorithm algorithm;
        std::string_view expected;
    } cases[] = {
        {HashAlgorithm::kMd5, "900150983cd24fb0d6963f7d28e17f72"},
        {HashAlgorithm::kSha1, "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {HashAlgorithm::kSha256,
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {HashAlgorithm::kSha384,
         "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086"
         "072ba1e7cc2358baeca134c825a7"},
        {HashAlgorithm::kSha512,
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39"
         "a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    };

    for (const auto& test_case : cases) {
        const auto digest = Hash(test_case.algorithm, View(input));
        ASSERT_TRUE(digest) << static_cast<int>(test_case.algorithm);
        EXPECT_EQ(digest.value(), Hex(test_case.expected));

        const auto hex = HashHex(test_case.algorithm, View(input));
        ASSERT_TRUE(hex) << static_cast<int>(test_case.algorithm);
        EXPECT_EQ(hex.value(), test_case.expected);
    }

    const auto unknown = Hash(static_cast<HashAlgorithm>(999), View(input));
    EXPECT_FALSE(unknown);
    EXPECT_EQ(unknown.status().code(), StatusCode::kInvalidArgument);

    const auto unknown_hex = HashHex(static_cast<HashAlgorithm>(999), View(input));
    EXPECT_FALSE(unknown_hex);
    EXPECT_EQ(unknown_hex.status().code(), StatusCode::kInvalidArgument);
}

TEST(CryptoRsaTest, GeneratesExportsAndUsesModernRsaOperations) {
    const auto generated = GenerateRsaPrivateKey(2048);
    ASSERT_TRUE(generated);
    const auto pem = ExportRsaPrivateKeyPem(generated.value());
    ASSERT_TRUE(pem);
    EXPECT_NE(pem.value().find("-----BEGIN PRIVATE KEY-----"), std::string::npos);

    const auto parsed = ParseRsaPrivateKeyPem(pem.value());
    ASSERT_TRUE(parsed);
    const auto public_key = DeriveRsaPublicKey(parsed.value());
    ASSERT_TRUE(public_key);
    const auto public_pem = ExportRsaPublicKeyPem(public_key.value());
    ASSERT_TRUE(public_pem);
    EXPECT_NE(public_pem.value().find("-----BEGIN PUBLIC KEY-----"), std::string::npos);

    const std::vector<std::uint8_t> message = Bytes("signed and encrypted message");
    const auto signature = RsaPssSign(parsed.value(), View(message));
    ASSERT_TRUE(signature);
    EXPECT_TRUE(RsaPssVerify(public_key.value(), View(message), View(signature.value())));

    std::vector<std::uint8_t> tampered_signature = signature.value();
    tampered_signature[0] ^= 1;
    const Status invalid_signature =
        RsaPssVerify(public_key.value(), View(message), View(tampered_signature));
    EXPECT_FALSE(invalid_signature);
    EXPECT_EQ(invalid_signature.code(), StatusCode::kUnauthenticated);

    const auto ciphertext = RsaOaepEncrypt(public_key.value(), View(message));
    ASSERT_TRUE(ciphertext);
    const auto plaintext = RsaOaepDecrypt(parsed.value(), View(ciphertext.value()));
    ASSERT_TRUE(plaintext);
    EXPECT_EQ(plaintext.value(), message);

    std::vector<std::uint8_t> tampered_ciphertext = ciphertext.value();
    tampered_ciphertext[0] ^= 1;
    const auto invalid_ciphertext = RsaOaepDecrypt(parsed.value(), View(tampered_ciphertext));
    EXPECT_FALSE(invalid_ciphertext);
    EXPECT_EQ(invalid_ciphertext.status().code(), StatusCode::kUnauthenticated);
}

TEST(CryptoRsaTest, RejectsWeakKeysOversizedPlaintextAndEncryptedPem) {
    const auto weak = GenerateRsaPrivateKey(1024);
    EXPECT_FALSE(weak);
    EXPECT_EQ(weak.status().code(), StatusCode::kInvalidArgument);

    const auto private_key = GenerateRsaPrivateKey(2048);
    ASSERT_TRUE(private_key);
    const auto public_key = DeriveRsaPublicKey(private_key.value());
    ASSERT_TRUE(public_key);
    const std::vector<std::uint8_t> too_long(191, 0);
    const auto ciphertext = RsaOaepEncrypt(public_key.value(), View(too_long));
    EXPECT_FALSE(ciphertext);
    EXPECT_EQ(ciphertext.status().code(), StatusCode::kInvalidArgument);

    const auto encrypted =
        ParseRsaPrivateKeyPem("-----BEGIN ENCRYPTED PRIVATE KEY-----\ninvalid\n");
    EXPECT_FALSE(encrypted);
    EXPECT_EQ(encrypted.status().code(), StatusCode::kUnimplemented);

    const auto wrong_type = ParseRsaPrivateKeyPem(
        "-----BEGIN PRIVATE KEY-----\n"
        "MC4CAQAwBQYDK2VwBCIEIJ1hsZ3v/VpguoRK9JLsLMREScVpezJpGXA7rAMcrn9g\n"
        "-----END PRIVATE KEY-----\n");
    EXPECT_FALSE(wrong_type);
    EXPECT_EQ(wrong_type.status().code(), StatusCode::kInvalidArgument);
}

TEST(CryptoEd25519Test, MatchesRfc8032VectorAndSupportsPemRoundTrip) {
    constexpr std::string_view kPrivatePem =
        "-----BEGIN PRIVATE KEY-----\n"
        "MC4CAQAwBQYDK2VwBCIEIJ1hsZ3v/VpguoRK9JLsLMREScVpezJpGXA7rAMcrn9g\n"
        "-----END PRIVATE KEY-----\n";
    constexpr std::string_view kPublicPem =
        "-----BEGIN PUBLIC KEY-----\n"
        "MCowBQYDK2VwAyEA11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=\n"
        "-----END PUBLIC KEY-----\n";
    const std::vector<std::uint8_t> empty;
    const auto private_key = ParseEd25519PrivateKeyPem(kPrivatePem);
    ASSERT_TRUE(private_key);
    const auto signature = Ed25519Sign(private_key.value(), View(empty));
    ASSERT_TRUE(signature);
    EXPECT_EQ(signature.value(),
              Hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                  "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));

    const auto public_key = ParseEd25519PublicKeyPem(kPublicPem);
    ASSERT_TRUE(public_key);
    EXPECT_TRUE(Ed25519Verify(public_key.value(), View(empty), View(signature.value())));

    const auto exported = ExportEd25519PrivateKeyPem(private_key.value());
    ASSERT_TRUE(exported);
    const auto reparsed = ParseEd25519PrivateKeyPem(exported.value());
    ASSERT_TRUE(reparsed);
    const auto derived_public = DeriveEd25519PublicKey(reparsed.value());
    ASSERT_TRUE(derived_public);

    std::vector<std::uint8_t> tampered = signature.value();
    tampered.back() ^= 1;
    const Status invalid = Ed25519Verify(derived_public.value(), View(empty), View(tampered));
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.code(), StatusCode::kUnauthenticated);

    auto generated = GenerateEd25519PrivateKey();
    ASSERT_TRUE(generated);
    Ed25519PrivateKey moved_key = std::move(generated).value();
    const auto moved_from_export = ExportEd25519PrivateKeyPem(generated.value());
    EXPECT_FALSE(moved_from_export);
    EXPECT_EQ(moved_from_export.status().code(), StatusCode::kFailedPrecondition);

    const auto generated_public = DeriveEd25519PublicKey(moved_key);
    ASSERT_TRUE(generated_public);
    const std::vector<std::uint8_t> generated_message = Bytes("generated key");
    const auto generated_signature = Ed25519Sign(moved_key, View(generated_message));
    ASSERT_TRUE(generated_signature);
    EXPECT_TRUE(Ed25519Verify(generated_public.value(), View(generated_message),
                              View(generated_signature.value())));
}

}  // namespace
}  // namespace tos
