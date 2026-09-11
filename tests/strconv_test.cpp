#include "tos/strconv.h"

#include <gtest/gtest.h>
#include <string>

namespace tos {
namespace {

TEST(StrconvTest, ConvertsUtf8AndWideTextWithoutUsingTheProcessLocale) {
    std::string utf8 = u8"hello 世界 😀";
    utf8.insert(5, 1, '\0');
    auto wide = Utf8ToWide(utf8);
    ASSERT_TRUE(wide);
    auto round_trip = WideToUtf8(wide.value());
    ASSERT_TRUE(round_trip);
    EXPECT_EQ(round_trip.value(), utf8);

    const std::string malformed("\xf0\x80\x80\x80", 4);
    const Result<std::wstring> invalid_utf8 = Utf8ToWide(malformed);
    EXPECT_FALSE(invalid_utf8);
    EXPECT_EQ(invalid_utf8.status().code(), StatusCode::kInvalidArgument);

    const std::wstring invalid_wide(1, static_cast<wchar_t>(0xd800));
    const Result<std::string> invalid_scalar = WideToUtf8(invalid_wide);
    EXPECT_FALSE(invalid_scalar);
    EXPECT_EQ(invalid_scalar.status().code(), StatusCode::kInvalidArgument);
}

TEST(StrconvTest, ReportsWindowsAnsiConversionAvailability) {
#ifdef _WIN32
    const std::string ansi("A\0B", 3);
    auto wide = AnsiToWide(ansi);
    ASSERT_TRUE(wide);
    auto round_trip = WideToAnsi(wide.value());
    ASSERT_TRUE(round_trip);
    EXPECT_EQ(round_trip.value(), ansi);
#else
    const Result<std::wstring> wide = AnsiToWide("text");
    EXPECT_FALSE(wide);
    EXPECT_EQ(wide.status().code(), StatusCode::kUnimplemented);
    const Result<std::string> ansi = WideToAnsi(L"text");
    EXPECT_FALSE(ansi);
    EXPECT_EQ(ansi.status().code(), StatusCode::kUnimplemented);
#endif
}

}  // namespace
}  // namespace tos
