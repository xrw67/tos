#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>
#include <type_traits>
#include <vector>

#include "tos/base/base64.h"

namespace tos {
namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()),
            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size()};
}

span<const std::uint8_t> View(const std::vector<std::uint8_t>& bytes) {
    return {bytes.data(), bytes.size()};
}

TEST(Base64Test, EncodesAndStrictlyDecodesStandardBase64) {
    const struct {
        std::string_view plain;
        std::string_view encoded;
    } cases[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };

    for (const auto& test_case : cases) {
        const auto input = Bytes(test_case.plain);
        const auto encoded = Base64Encode(View(input));
        static_assert(std::is_same_v<decltype(encoded), const std::string>);
        EXPECT_EQ(encoded, test_case.encoded);

        const auto decoded = Base64Decode(encoded);
        ASSERT_TRUE(decoded) << test_case.encoded;
        EXPECT_EQ(decoded.value(), input);
    }

    for (const std::string_view invalid : {"Zg", "Zg=", "Zg==\n", "Zh==", "Zg=a", "-g=="}) {
        const auto result = Base64Decode(invalid);
        EXPECT_FALSE(result) << invalid;
        EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument) << invalid;
    }
}

TEST(Base64Test, EncodesAndStrictlyDecodesUnpaddedBase64Url) {
    const struct {
        std::vector<std::uint8_t> input;
        std::string_view encoded;
    } cases[] = {
        {{}, ""},
        {{'f'}, "Zg"},
        {{'f', 'o'}, "Zm8"},
        {{'f', 'o', 'o'}, "Zm9v"},
        {{0xfb, 0xff, 0xff}, "-___"},
    };

    for (const auto& test_case : cases) {
        const auto encoded = Base64UrlEncode(View(test_case.input));
        static_assert(std::is_same_v<decltype(encoded), const std::string>);
        EXPECT_EQ(encoded, test_case.encoded);

        const auto decoded = Base64UrlDecode(encoded);
        ASSERT_TRUE(decoded) << test_case.encoded;
        EXPECT_EQ(decoded.value(), test_case.input);
    }

    for (const std::string_view invalid : {"+___", "-___=", "A", "Zh", "aGVs\n"}) {
        const auto result = Base64UrlDecode(invalid);
        EXPECT_FALSE(result) << invalid;
        EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument) << invalid;
    }
}

}  // namespace
}  // namespace tos
