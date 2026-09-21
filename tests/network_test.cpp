#include "tos/base/network.h"

#include <cstdint>
#include <gtest/gtest.h>

namespace tos {
namespace {

TEST(NetworkTest, ConvertsIntegerByteOrder) {
    EXPECT_EQ(NetworkToHost16(HostToNetwork16(0x1234)), 0x1234);
    EXPECT_EQ(NetworkToHost32(HostToNetwork32(0x12345678)), 0x12345678U);
    EXPECT_EQ(NetworkToHost64(HostToNetwork64(0x0123456789abcdefULL)), 0x0123456789abcdefULL);
}

TEST(NetworkTest, ConvertsIPv4BetweenBytesAndText) {
    const auto parsed = ParseIPv4("192.0.2.42");
    ASSERT_TRUE(parsed) << parsed.status().ToString();
    EXPECT_EQ(parsed.value(), (IPv4Address{192, 0, 2, 42}));
    EXPECT_EQ(FormatIPv4(parsed.value()), "192.0.2.42");
}

TEST(NetworkTest, ConvertsIPv6BetweenBytesAndCanonicalText) {
    const auto parsed = ParseIPv6("2001:db8::1");
    ASSERT_TRUE(parsed) << parsed.status().ToString();
    EXPECT_EQ(FormatIPv6(parsed.value()), "2001:db8::1");
    EXPECT_EQ(ParseIPv6(FormatIPv6(parsed.value())).value(), parsed.value());
}

TEST(NetworkTest, RejectsInvalidAddressText) {
    EXPECT_EQ(ParseIPv4("192.0.2.999").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseIPv4("2001:db8::1").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseIPv6("192.0.2.42").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseIPv6("").status().code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace tos
