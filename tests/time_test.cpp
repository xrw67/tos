#include "tos/base/time.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace tos {
namespace {

TEST(DurationTest, ConvertsAndFormatsGoStyleUnits) {
    const Duration duration = Duration::FromNanoseconds(3723400500678LL);

    EXPECT_EQ(duration.Nanoseconds(), 3723400500678LL);
    EXPECT_EQ(Duration::FromNanoseconds(1500).Microseconds(), 1);
    EXPECT_EQ(Duration::FromNanoseconds(-1500).Microseconds(), -1);
    EXPECT_EQ(Duration::FromNanoseconds(999999).Milliseconds(), 0);
    EXPECT_DOUBLE_EQ(duration.Seconds(), 3723.400500678);
    EXPECT_DOUBLE_EQ(duration.Minutes(), 62.0566750113);
    EXPECT_EQ(duration.ToString(), "1h2m3.400500678s");
    EXPECT_EQ(Duration::FromNanoseconds(-250000000).ToString(), "-250ms");
    EXPECT_EQ(Duration::FromNanoseconds(250500000).ToString(), "250.5ms");
    EXPECT_EQ(Duration::FromNanoseconds(1500).ToString(), "1.5us");
    EXPECT_EQ(Duration::FromNanoseconds(0).ToString(), "0s");
}

TEST(DurationTest, ParsesCompoundFractionsAndSigns) {
    const auto compound = ParseDuration("1h2m3.400500678s");
    ASSERT_TRUE(compound);
    EXPECT_EQ(compound.value(), Duration::FromNanoseconds(3723400500678LL));

    const auto fractional = ParseDuration("-.5s");
    ASSERT_TRUE(fractional);
    EXPECT_EQ(fractional.value(), Duration::FromNanoseconds(-500000000));

    const auto subnanosecond = ParseDuration("1.9ns");
    ASSERT_TRUE(subnanosecond);
    EXPECT_EQ(subnanosecond.value(), Nanosecond);

    const auto submillisecond = ParseDuration("1.5ms");
    ASSERT_TRUE(submillisecond);
    EXPECT_EQ(submillisecond.value(), Duration::FromNanoseconds(1500000));
}

TEST(DurationTest, RejectsMalformedAndOverflowingValues) {
    for (const std::string text : {"", "+", "3", "ms", "1x", "1..2s", "9223372036854775808ns"}) {
        SCOPED_TRACE(text);
        const auto result = ParseDuration(text);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
    }

    const auto minimum = ParseDuration("-9223372036854775808ns");
    ASSERT_TRUE(minimum);
    EXPECT_EQ(minimum.value().Nanoseconds(), std::numeric_limits<std::int64_t>::min());
}

TEST(TimeTest, NormalizesUnixNanosecondsLikeGo) {
    const auto normalized = Time::FromUnix(1, -1);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(normalized.value().Unix(), 0);
    EXPECT_EQ(normalized.value().Nanosecond(), 999999999);
    EXPECT_EQ(normalized.value().UnixNanoseconds(), 999999999);
    EXPECT_EQ(normalized.value().UnixNano(), 999999999);

    const auto before_epoch = Time::FromUnix(-1, 999999999);
    ASSERT_TRUE(before_epoch);
    EXPECT_EQ(before_epoch.value().Unix(), -1);
    EXPECT_EQ(before_epoch.value().UnixMilliseconds(), -1);
    EXPECT_EQ(before_epoch.value().UnixMicroseconds(), -1);
    EXPECT_EQ(before_epoch.value().UnixMilli(), -1);
    EXPECT_EQ(before_epoch.value().UnixMicro(), -1);
}

TEST(TimeTest, FormatsAndParsesUtcRfc3339) {
    const auto source = Time::FromUnix(0, 123400000);
    ASSERT_TRUE(source);
    EXPECT_EQ(source.value().FormatRfc3339(), "1970-01-01T00:00:00.1234Z");

    const auto parsed = ParseRfc3339("1970-01-01T01:30:00.123400000+01:30");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value(), source.value());

    const auto leap_day = ParseRfc3339("2000-02-29T23:59:59Z");
    ASSERT_TRUE(leap_day);
    EXPECT_EQ(leap_day.value().FormatRfc3339(), "2000-02-29T23:59:59Z");
}

TEST(TimeTest, RejectsInvalidRfc3339AndOutOfRangeUnixValues) {
    for (const std::string text : {"1970-01-01", "1970-13-01T00:00:00Z", "2001-02-29T00:00:00Z",
                                   "1970-01-01T00:00:00", "1970-01-01T00:00:00.1234567890Z"}) {
        SCOPED_TRACE(text);
        const auto result = ParseRfc3339(text);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
    }

    const auto out_of_range = Time::FromUnix(std::numeric_limits<std::int64_t>::max());
    EXPECT_FALSE(out_of_range);
    EXPECT_EQ(out_of_range.status().code(), StatusCode::kInvalidArgument);
}

TEST(TimeTest, ReportsArithmeticOverflowInsteadOfWrapping) {
    const Time maximum = Time::FromUnixNanoseconds(std::numeric_limits<std::int64_t>::max());
    const auto addition = maximum.Add(Nanosecond);
    EXPECT_FALSE(addition);
    EXPECT_EQ(addition.status().code(), StatusCode::kInvalidArgument);

    const Time minimum = Time::FromUnixNanoseconds(std::numeric_limits<std::int64_t>::min());
    const auto subtraction = minimum.Sub(maximum);
    EXPECT_FALSE(subtraction);
    EXPECT_EQ(subtraction.status().code(), StatusCode::kInvalidArgument);
}

TEST(TimeTest, ManualClockIsDeterministicAndImplementsIClock) {
    ManualClock clock(Time::FromUnixNanoseconds(100));
    const IClock& interface = clock;
    EXPECT_EQ(interface.Now().UnixNanoseconds(), 100);

    const Status advanced = clock.Advance(Duration::FromNanoseconds(25));
    ASSERT_TRUE(advanced);
    EXPECT_EQ(clock.Now().UnixNanoseconds(), 125);

    clock.Set(Time::FromUnixNanoseconds(std::numeric_limits<std::int64_t>::max()));
    const Status overflow = clock.Advance(Nanosecond);
    EXPECT_FALSE(overflow);
    EXPECT_EQ(overflow.code(), StatusCode::kInvalidArgument);
}

TEST(TimeTest, MonotonicClockReportsNondecreasingElapsedDuration) {
    const MonotonicClock clock;
    const Duration earlier = clock.Elapsed();
    const Duration later = clock.Elapsed();
    EXPECT_GE(later, earlier);
}

}  // namespace
}  // namespace tos
