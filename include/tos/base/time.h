#ifndef TOS_BASE_TIME_H_
#define TOS_BASE_TIME_H_

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "tos/base/result.h"

namespace tos {

/// A signed elapsed duration with nanosecond precision, modelled after Go's time.Duration.
/// It owns no resources and is safe to copy and read concurrently. Arithmetic which can
/// overflow is exposed through Time::Add and Time::Sub and returns Result rather than wrapping.
class Duration {
   public:
    /// Creates a zero duration without throwing.
    constexpr Duration() noexcept = default;

    /// Creates a duration from an exact signed nanosecond count without throwing.
    static constexpr Duration FromNanoseconds(std::int64_t nanoseconds) noexcept {
        return Duration(nanoseconds);
    }

    /// Returns the exact signed nanosecond count without throwing.
    constexpr std::int64_t Nanoseconds() const noexcept { return nanoseconds_; }

    /// Returns the whole microseconds, truncating toward zero like Go's Duration.Microseconds().
    constexpr std::int64_t Microseconds() const noexcept {
        return nanoseconds_ / static_cast<std::int64_t>(kNanosecondsPerMicrosecond);
    }

    /// Returns the whole milliseconds, truncating toward zero like Go's Duration.Milliseconds().
    constexpr std::int64_t Milliseconds() const noexcept {
        return nanoseconds_ / static_cast<std::int64_t>(kNanosecondsPerMillisecond);
    }

    /// Returns the duration converted to a floating-point count of seconds.
    constexpr double Seconds() const noexcept {
        return static_cast<double>(nanoseconds_) / static_cast<double>(kNanosecondsPerSecond);
    }

    /// Returns the duration converted to a floating-point count of minutes.
    constexpr double Minutes() const noexcept { return Seconds() / 60.0; }

    /// Returns the duration converted to a floating-point count of hours.
    constexpr double Hours() const noexcept { return Minutes() / 60.0; }

    /// Formats the duration using Go-style units, such as "1h2m3.4s" or "250ms".
    /// String allocation exceptions propagate to the caller.
    std::string ToString() const {
        if (nanoseconds_ == 0) {
            return "0s";
        }

        const std::uint64_t magnitude = Absolute(nanoseconds_);
        std::string text;
        if (nanoseconds_ < 0) {
            text.push_back('-');
        }

        std::uint64_t remainder = magnitude;
        const std::uint64_t hours = remainder / kNanosecondsPerHour;
        remainder %= kNanosecondsPerHour;
        const std::uint64_t minutes = remainder / kNanosecondsPerMinute;
        remainder %= kNanosecondsPerMinute;

        if (hours != 0) {
            text.append(std::to_string(hours));
            text.push_back('h');
        }
        if (hours != 0 || minutes != 0) {
            text.append(std::to_string(minutes));
            text.push_back('m');
        }
        if (hours != 0 || minutes != 0 || remainder >= kNanosecondsPerSecond) {
            AppendSeconds(text, remainder);
            return text;
        }
        if (remainder >= kNanosecondsPerMillisecond) {
            AppendFraction(text, remainder / kNanosecondsPerMillisecond,
                           remainder % kNanosecondsPerMillisecond, kNanosecondsPerMillisecond,
                           "ms");
            return text;
        }
        if (remainder >= kNanosecondsPerMicrosecond) {
            AppendFraction(text, remainder / kNanosecondsPerMicrosecond,
                           remainder % kNanosecondsPerMicrosecond, kNanosecondsPerMicrosecond,
                           "us");
            return text;
        }
        text.append(std::to_string(remainder));
        return text.append("ns");
    }

    friend constexpr bool operator==(Duration lhs, Duration rhs) noexcept {
        return lhs.nanoseconds_ == rhs.nanoseconds_;
    }
    friend constexpr bool operator!=(Duration lhs, Duration rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<(Duration lhs, Duration rhs) noexcept {
        return lhs.nanoseconds_ < rhs.nanoseconds_;
    }
    friend constexpr bool operator<=(Duration lhs, Duration rhs) noexcept { return !(rhs < lhs); }
    friend constexpr bool operator>(Duration lhs, Duration rhs) noexcept { return rhs < lhs; }
    friend constexpr bool operator>=(Duration lhs, Duration rhs) noexcept { return !(lhs < rhs); }

   private:
    friend Result<Duration> ParseDuration(std::string_view text);
    friend class Time;

    static constexpr std::uint64_t kNanosecondsPerMicrosecond = 1000;
    static constexpr std::uint64_t kNanosecondsPerMillisecond = 1000 * kNanosecondsPerMicrosecond;
    static constexpr std::uint64_t kNanosecondsPerSecond = 1000 * kNanosecondsPerMillisecond;
    static constexpr std::uint64_t kNanosecondsPerMinute = 60 * kNanosecondsPerSecond;
    static constexpr std::uint64_t kNanosecondsPerHour = 60 * kNanosecondsPerMinute;

    explicit constexpr Duration(std::int64_t nanoseconds) noexcept : nanoseconds_(nanoseconds) {}

    static constexpr std::uint64_t Absolute(std::int64_t value) noexcept {
        return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                         : static_cast<std::uint64_t>(value);
    }

    static void AppendSeconds(std::string& text, std::uint64_t nanoseconds) {
        AppendFraction(text, nanoseconds / kNanosecondsPerSecond,
                       nanoseconds % kNanosecondsPerSecond, kNanosecondsPerSecond, "s");
    }

    static void AppendFraction(std::string& text, std::uint64_t whole, std::uint64_t fraction,
                               std::uint64_t scale, const char* suffix) {
        text.append(std::to_string(whole));
        if (fraction != 0) {
            std::string digits = std::to_string(scale + fraction).substr(1);
            while (digits.back() == '0') {
                digits.pop_back();
            }
            text.push_back('.');
            text.append(digits);
        }
        text.append(suffix);
    }

    std::int64_t nanoseconds_ = 0;
};

/// One nanosecond.
constexpr Duration Nanosecond = Duration::FromNanoseconds(1);
/// One microsecond.
constexpr Duration Microsecond = Duration::FromNanoseconds(1000);
/// One millisecond.
constexpr Duration Millisecond = Duration::FromNanoseconds(1000 * 1000);
/// One second.
constexpr Duration Second = Duration::FromNanoseconds(1000 * 1000 * 1000);
/// One minute.
constexpr Duration Minute = Duration::FromNanoseconds(60 * 1000 * 1000 * 1000LL);
/// One hour.
constexpr Duration Hour = Duration::FromNanoseconds(60LL * 60 * 1000 * 1000 * 1000);

/// Parses a Go-style duration containing one or more decimal values and ns, us, ms, s, m, or h.
/// A leading sign is accepted. Malformed input and values outside Duration's range return
/// kInvalidArgument; constructing that Status may throw std::bad_alloc.
inline Result<Duration> ParseDuration(std::string_view text) {
    if (text.empty()) {
        return Status(StatusCode::kInvalidArgument, "duration must not be empty");
    }

    std::size_t position = 0;
    bool negative = false;
    if (text[position] == '+' || text[position] == '-') {
        negative = text[position] == '-';
        ++position;
    }
    if (position == text.size()) {
        return Status(StatusCode::kInvalidArgument, "duration is missing a value");
    }

    const std::uint64_t maximum =
        negative ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1
                 : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::uint64_t total = 0;
    while (position != text.size()) {
        const std::size_t number_start = position;
        std::uint64_t whole = 0;
        while (position != text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position]))) {
            const unsigned digit = static_cast<unsigned>(text[position] - '0');
            if (whole > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                return Status(StatusCode::kInvalidArgument,
                              "duration is outside the supported range");
            }
            whole = whole * 10 + digit;
            ++position;
        }

        std::uint64_t fractional_digits = 0;
        std::size_t fractional_count = 0;
        if (position != text.size() && text[position] == '.') {
            ++position;
            while (position != text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[position]))) {
                if (fractional_count < 9) {
                    fractional_digits =
                        fractional_digits * 10 + static_cast<unsigned>(text[position] - '0');
                }
                ++fractional_count;
                ++position;
            }
        }
        if (number_start == position || (text[number_start] == '.' && fractional_count == 0) ||
            (number_start + 1 == position && text[number_start] == '.')) {
            return Status(StatusCode::kInvalidArgument, "duration contains an invalid number");
        }
        if (position == text.size()) {
            return Status(StatusCode::kInvalidArgument, "duration is missing a unit");
        }

        std::uint64_t unit = 0;
        if (text.compare(position, 2, "ns") == 0) {
            unit = 1;
            position += 2;
        } else if (text.compare(position, 2, "us") == 0) {
            unit = 1000;
            position += 2;
        } else if (text.compare(position, 2, "ms") == 0) {
            unit = 1000 * 1000;
            position += 2;
        } else if (text[position] == 's') {
            unit = 1000 * 1000 * 1000;
            ++position;
        } else if (text[position] == 'm') {
            unit = 60 * 1000 * 1000 * 1000ULL;
            ++position;
        } else if (text[position] == 'h') {
            unit = 60ULL * 60 * 1000 * 1000 * 1000;
            ++position;
        } else {
            return Status(StatusCode::kInvalidArgument, "duration has an unsupported unit");
        }

        if (whole > maximum / unit) {
            return Status(StatusCode::kInvalidArgument, "duration is outside the supported range");
        }
        std::uint64_t component = whole * unit;
        if (fractional_count != 0) {
            while (fractional_count < 9) {
                fractional_digits *= 10;
                ++fractional_count;
            }
            const std::uint64_t fraction =
                unit >= 1000 * 1000 * 1000ULL
                    ? fractional_digits * (unit / (1000 * 1000 * 1000ULL))
                    : fractional_digits / ((1000 * 1000 * 1000ULL) / unit);
            if (component > maximum - fraction) {
                return Status(StatusCode::kInvalidArgument,
                              "duration is outside the supported range");
            }
            component += fraction;
        }
        if (total > maximum - component) {
            return Status(StatusCode::kInvalidArgument, "duration is outside the supported range");
        }
        total += component;
    }

    if (negative &&
        total == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1) {
        return Duration::FromNanoseconds(std::numeric_limits<std::int64_t>::min());
    }
    return Duration::FromNanoseconds(negative ? -static_cast<std::int64_t>(total)
                                              : static_cast<std::int64_t>(total));
}

/// An instant on the UTC timeline with nanosecond precision, modelled after Go's time.Time.
/// It owns no resources and is safe to copy and read concurrently. This C++17 implementation
/// deliberately has no mutable location or hidden monotonic reading: values compare by UTC instant.
class Time {
   public:
    /// Creates the Unix epoch without throwing.
    constexpr Time() noexcept = default;

    /// Creates a Time from an exact signed Unix nanosecond count without throwing.
    static constexpr Time FromUnixNanoseconds(std::int64_t nanoseconds) noexcept {
        return Time(nanoseconds);
    }

    /// Creates a UTC instant from Unix seconds and nanoseconds, normalizing nanoseconds like Go.
    /// Values outside this implementation's int64 nanosecond range return kInvalidArgument; error
    /// Status allocation may throw std::bad_alloc.
    static Result<Time> FromUnix(std::int64_t seconds, std::int64_t nanoseconds = 0) {
        const std::int64_t extra_seconds = FloorDivide(nanoseconds, kNanosecondsPerSecond);
        const std::int64_t normalized_nanoseconds = FloorModulo(nanoseconds, kNanosecondsPerSecond);
        std::int64_t normalized_seconds = 0;
        std::int64_t epoch_nanoseconds = 0;
        if (!CheckedAdd(seconds, extra_seconds, &normalized_seconds) ||
            !CheckedMultiply(normalized_seconds, kNanosecondsPerSecond, &epoch_nanoseconds) ||
            !CheckedAdd(epoch_nanoseconds, normalized_nanoseconds, &epoch_nanoseconds)) {
            return Status(StatusCode::kInvalidArgument, "Unix time is outside the supported range");
        }
        return Time(epoch_nanoseconds);
    }

    /// Returns the current system UTC time. It is not monotonic: system-clock adjustments can move
    /// consecutive results backwards. This operation does not allocate or throw.
    static Time Now() noexcept {
        const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
        return Time(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

    /// Returns Unix seconds rounded down for instants before the epoch.
    constexpr std::int64_t Unix() const noexcept {
        return FloorDivide(unix_nanoseconds_, kNanosecondsPerSecond);
    }

    /// Returns Unix milliseconds rounded down for instants before the epoch.
    constexpr std::int64_t UnixMilliseconds() const noexcept {
        return FloorDivide(unix_nanoseconds_, kNanosecondsPerMillisecond);
    }

    /// Go-compatible alias for UnixMilliseconds().
    constexpr std::int64_t UnixMilli() const noexcept { return UnixMilliseconds(); }

    /// Returns Unix microseconds rounded down for instants before the epoch.
    constexpr std::int64_t UnixMicroseconds() const noexcept {
        return FloorDivide(unix_nanoseconds_, kNanosecondsPerMicrosecond);
    }

    /// Go-compatible alias for UnixMicroseconds().
    constexpr std::int64_t UnixMicro() const noexcept { return UnixMicroseconds(); }

    /// Returns the exact signed Unix nanosecond count.
    constexpr std::int64_t UnixNanoseconds() const noexcept { return unix_nanoseconds_; }

    /// Go-compatible alias for UnixNanoseconds().
    constexpr std::int64_t UnixNano() const noexcept { return UnixNanoseconds(); }

    /// Returns the nanosecond offset within the UTC second in the range [0, 1,000,000,000).
    constexpr std::int32_t Nanosecond() const noexcept {
        return static_cast<std::int32_t>(FloorModulo(unix_nanoseconds_, kNanosecondsPerSecond));
    }

    /// Adds an elapsed duration. Overflow returns kInvalidArgument rather than wrapping; error
    /// Status allocation may throw std::bad_alloc.
    Result<Time> Add(Duration duration) const {
        std::int64_t value = 0;
        if (!CheckedAdd(unix_nanoseconds_, duration.nanoseconds_, &value)) {
            return Status(StatusCode::kInvalidArgument,
                          "time addition is outside the supported range");
        }
        return Time(value);
    }

    /// Returns this instant minus other as an elapsed duration. Overflow returns kInvalidArgument;
    /// error Status allocation may throw std::bad_alloc.
    Result<Duration> Sub(Time other) const {
        std::int64_t value = 0;
        if (!CheckedSubtract(unix_nanoseconds_, other.unix_nanoseconds_, &value)) {
            return Status(StatusCode::kInvalidArgument,
                          "time difference is outside the supported range");
        }
        return Duration::FromNanoseconds(value);
    }

    /// Returns RFC3339Nano-like UTC text, for example "1970-01-01T00:00:00Z".
    /// It always uses UTC and propagates string allocation exceptions.
    std::string FormatRfc3339() const {
        const std::int64_t seconds = Unix();
        const CivilDate date = CivilFromDays(FloorDivide(seconds, 86400));
        const std::int64_t day_seconds = FloorModulo(seconds, 86400);
        std::string text;
        AppendPadded(text, date.year, 4);
        text.push_back('-');
        AppendPadded(text, date.month, 2);
        text.push_back('-');
        AppendPadded(text, date.day, 2);
        text.push_back('T');
        AppendPadded(text, day_seconds / 3600, 2);
        text.push_back(':');
        AppendPadded(text, (day_seconds % 3600) / 60, 2);
        text.push_back(':');
        AppendPadded(text, day_seconds % 60, 2);
        const std::int64_t fraction = FloorModulo(unix_nanoseconds_, kNanosecondsPerSecond);
        if (fraction != 0) {
            std::string digits = std::to_string(kNanosecondsPerSecond + fraction).substr(1);
            while (digits.back() == '0') {
                digits.pop_back();
            }
            text.push_back('.');
            text.append(digits);
        }
        text.push_back('Z');
        return text;
    }

    friend constexpr bool operator==(Time lhs, Time rhs) noexcept {
        return lhs.unix_nanoseconds_ == rhs.unix_nanoseconds_;
    }
    friend constexpr bool operator!=(Time lhs, Time rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<(Time lhs, Time rhs) noexcept {
        return lhs.unix_nanoseconds_ < rhs.unix_nanoseconds_;
    }
    friend constexpr bool operator<=(Time lhs, Time rhs) noexcept { return !(rhs < lhs); }
    friend constexpr bool operator>(Time lhs, Time rhs) noexcept { return rhs < lhs; }
    friend constexpr bool operator>=(Time lhs, Time rhs) noexcept { return !(lhs < rhs); }

   private:
    friend Result<Time> ParseRfc3339(std::string_view text);
    friend class ManualClock;

    struct CivilDate {
        std::int64_t year;
        std::int64_t month;
        std::int64_t day;
    };

    static constexpr std::int64_t kNanosecondsPerMicrosecond = 1000;
    static constexpr std::int64_t kNanosecondsPerMillisecond = 1000 * kNanosecondsPerMicrosecond;
    static constexpr std::int64_t kNanosecondsPerSecond = 1000 * kNanosecondsPerMillisecond;

    explicit constexpr Time(std::int64_t unix_nanoseconds) noexcept
        : unix_nanoseconds_(unix_nanoseconds) {}

    static constexpr std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept {
        const std::int64_t quotient = value / divisor;
        const std::int64_t remainder = value % divisor;
        return remainder < 0 ? quotient - 1 : quotient;
    }

    static constexpr std::int64_t FloorModulo(std::int64_t value, std::int64_t divisor) noexcept {
        const std::int64_t remainder = value % divisor;
        return remainder < 0 ? remainder + divisor : remainder;
    }

    static bool CheckedAdd(std::int64_t lhs, std::int64_t rhs, std::int64_t* output) noexcept {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
            return false;
        }
        *output = lhs + rhs;
        return true;
    }

    static bool CheckedSubtract(std::int64_t lhs, std::int64_t rhs, std::int64_t* output) noexcept {
        if ((rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs) ||
            (rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs)) {
            return false;
        }
        *output = lhs - rhs;
        return true;
    }

    static bool CheckedMultiply(std::int64_t lhs, std::int64_t rhs, std::int64_t* output) noexcept {
        if (lhs == 0 || rhs == 0) {
            *output = 0;
            return true;
        }
        if (lhs == -1 && rhs == std::numeric_limits<std::int64_t>::min()) {
            return false;
        }
        if (rhs == -1 && lhs == std::numeric_limits<std::int64_t>::min()) {
            return false;
        }
        if (lhs > 0
                ? (rhs > 0 ? lhs > std::numeric_limits<std::int64_t>::max() / rhs
                           : rhs < std::numeric_limits<std::int64_t>::min() / lhs)
                : (rhs > 0 ? lhs < std::numeric_limits<std::int64_t>::min() / rhs
                           : lhs != 0 && rhs < std::numeric_limits<std::int64_t>::max() / lhs)) {
            return false;
        }
        *output = lhs * rhs;
        return true;
    }

    static constexpr bool IsLeapYear(std::int64_t year) noexcept {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    static constexpr int DaysInMonth(std::int64_t year, std::int64_t month) noexcept {
        constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return month == 2 && IsLeapYear(year) ? 29 : kDays[month - 1];
    }

    static constexpr std::int64_t DaysFromCivil(std::int64_t year, std::int64_t month,
                                                std::int64_t day) noexcept {
        year -= month <= 2;
        const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
        const std::int64_t year_of_era = year - era * 400;
        const std::int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const std::int64_t day_of_era =
            year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
        return era * 146097 + day_of_era - 719468;
    }

    static constexpr CivilDate CivilFromDays(std::int64_t days) noexcept {
        const std::int64_t z = days + 719468;
        const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const std::int64_t day_of_era = z - era * 146097;
        const std::int64_t year_of_era =
            (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
        const std::int64_t year = year_of_era + era * 400;
        const std::int64_t day_of_year =
            day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        const std::int64_t month_prime = (5 * day_of_year + 2) / 153;
        return {year + (month_prime >= 10), month_prime + (month_prime < 10 ? 3 : -9),
                day_of_year - (153 * month_prime + 2) / 5 + 1};
    }

    static void AppendPadded(std::string& text, std::int64_t value, int width) {
        const std::string digits = std::to_string(value);
        for (int count = static_cast<int>(digits.size()); count < width; ++count) {
            text.push_back('0');
        }
        text.append(digits);
    }

    std::int64_t unix_nanoseconds_ = 0;
};

/// Parses an RFC3339 UTC or numeric-offset instant into a UTC Time.
/// Invalid dates, offsets, fractions, or values outside Time's range return kInvalidArgument;
/// constructing the error Status may throw std::bad_alloc.
inline Result<Time> ParseRfc3339(std::string_view text) {
    const auto invalid = [] {
        return Result<Time>(Status(StatusCode::kInvalidArgument, "invalid RFC3339 time"));
    };
    const auto decimal = [&text](std::size_t position, std::size_t width, int* value) {
        if (position + width > text.size()) {
            return false;
        }
        int parsed = 0;
        for (std::size_t index = 0; index < width; ++index) {
            const char character = text[position + index];
            if (character < '0' || character > '9') {
                return false;
            }
            parsed = parsed * 10 + character - '0';
        }
        *value = parsed;
        return true;
    };

    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return invalid();
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!decimal(0, 4, &year) || !decimal(5, 2, &month) || !decimal(8, 2, &day) ||
        !decimal(11, 2, &hour) || !decimal(14, 2, &minute) || !decimal(17, 2, &second) ||
        month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59 ||
        day > Time::DaysInMonth(year, month)) {
        return invalid();
    }

    std::size_t position = 19;
    std::int64_t fraction = 0;
    if (position != text.size() && text[position] == '.') {
        ++position;
        const std::size_t start = position;
        std::size_t digits = 0;
        while (position != text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position]))) {
            if (digits >= 9) {
                return invalid();
            }
            fraction = fraction * 10 + text[position] - '0';
            ++digits;
            ++position;
        }
        if (position == start) {
            return invalid();
        }
        while (digits++ < 9) {
            fraction *= 10;
        }
    }

    std::int64_t offset = 0;
    if (position != text.size() && text[position] == 'Z') {
        ++position;
    } else if (position + 6 == text.size() && (text[position] == '+' || text[position] == '-') &&
               text[position + 3] == ':') {
        int offset_hour = 0;
        int offset_minute = 0;
        if (!decimal(position + 1, 2, &offset_hour) || !decimal(position + 4, 2, &offset_minute) ||
            offset_hour > 23 || offset_minute > 59) {
            return invalid();
        }
        offset = offset_hour * 3600 + offset_minute * 60;
        if (text[position] == '-') {
            offset = -offset;
        }
        position += 6;
    } else {
        return invalid();
    }
    if (position != text.size()) {
        return invalid();
    }

    const std::int64_t days = Time::DaysFromCivil(year, month, day);
    std::int64_t local_seconds = 0;
    std::int64_t seconds = 0;
    std::int64_t nanoseconds = 0;
    if (!Time::CheckedMultiply(days, 86400, &local_seconds) ||
        !Time::CheckedAdd(local_seconds, hour * 3600 + minute * 60 + second, &local_seconds) ||
        !Time::CheckedSubtract(local_seconds, offset, &seconds) ||
        !Time::CheckedMultiply(seconds, Time::kNanosecondsPerSecond, &nanoseconds) ||
        !Time::CheckedAdd(nanoseconds, fraction, &nanoseconds)) {
        return Status(StatusCode::kInvalidArgument, "RFC3339 time is outside the supported range");
    }
    return Time::FromUnixNanoseconds(nanoseconds);
}

/// Read-only source of UTC Time values. Implementations own no caller resources; concurrent calls
/// are safe when the concrete implementation documents them. Now never throws.
class IClock {
   public:
    virtual ~IClock() = default;
    virtual Time Now() const noexcept = 0;
};

/// IClock backed by the system clock. Concurrent calls are safe; wall-clock adjustments may move
/// values backwards. It owns no resources and Now does not throw.
class SystemClock final : public IClock {
   public:
    Time Now() const noexcept override { return Time::Now(); }
};

/// Deterministic, thread-safe clock for tests and schedulers. Set and Now do not throw. Advance
/// returns kInvalidArgument if it would exceed Time's representable range; Status allocation may
/// throw.
class ManualClock final : public IClock {
   public:
    explicit ManualClock(Time initial = Time()) noexcept
        : nanoseconds_(initial.UnixNanoseconds()) {}

    Time Now() const noexcept override { return Time::FromUnixNanoseconds(nanoseconds_.load()); }

    void Set(Time value) noexcept { nanoseconds_.store(value.UnixNanoseconds()); }

    Status Advance(Duration duration) {
        std::int64_t observed = nanoseconds_.load();
        while (true) {
            std::int64_t next = 0;
            if (!Time::CheckedAdd(observed, duration.Nanoseconds(), &next)) {
                return Status(StatusCode::kInvalidArgument,
                              "manual clock advance is outside the supported range");
            }
            if (nanoseconds_.compare_exchange_weak(observed, next)) {
                return Status::Ok();
            }
        }
    }

   private:
    std::atomic<std::int64_t> nanoseconds_;
};

/// Measures elapsed time with std::chrono::steady_clock. The origin is private and has no UTC
/// meaning. It owns no resources, is safe to read concurrently after construction, and never
/// throws.
class MonotonicClock final {
   public:
    MonotonicClock() noexcept : started_(std::chrono::steady_clock::now()) {}

    /// Returns a nondecreasing elapsed duration while the platform steady clock remains available.
    Duration Elapsed() const noexcept {
        return Duration::FromNanoseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::steady_clock::now() - started_)
                                             .count());
    }

   private:
    std::chrono::steady_clock::time_point started_;
};

}  // namespace tos

#endif  // TOS_BASE_TIME_H_
