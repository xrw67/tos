#ifndef TOS_LOGGING_H_
#define TOS_LOGGING_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "tos/format.h"
#include "tos/status.h"
#include "tos/time.h"

namespace tos {

/// Severity of a log record. kOff suppresses every record.
enum class LogLevel { kTrace, kDebug, kInfo, kWarning, kError, kCritical, kOff };

/// Scalar value retained by a structured log field.
using LogValue = std::variant<bool, std::int64_t, double, std::string>;

/// Deterministically ordered, owning fields attached to one log record.
using LogFields = std::map<std::string, LogValue>;

/// Options for a size-rotating JSON Lines file sink.
struct RotatingFileOptions {
    /// Active log file. Its parent directory must already exist.
    std::filesystem::path path;
    /// Maximum active-file size in bytes before the next record triggers rotation.
    std::size_t max_bytes = 0;
    /// Number of archive files retained, excluding the active file.
    std::size_t max_files = 0;
};

/// Options used when a Logger is constructed.
struct LoggerOptions {
    /// Name written into every record. The default identifies this library.
    std::string name = "tos";
    /// Minimum severity written to configured sinks.
    LogLevel level = LogLevel::kInfo;
    /// Whether to install the default console sink.
    bool console = true;
    /// Optional shared UTC clock. A null pointer selects an owned SystemClock.
    std::shared_ptr<const IClock> clock;
};

/// Synchronous structured logger with console and rotating-file sinks.
///
/// Logger owns all configured sinks and is neither copyable nor movable. All public member
/// functions are safe to call concurrently. A supplied clock is shared and must itself support
/// concurrent Now calls. Logger serializes its own writes, but cannot make writes from other
/// processes or direct writers to the same stream or file atomic with its records.
/// Destroying a Logger while another thread calls one of its members requires caller
/// synchronization.
///
/// Operational failures from sink configuration, logging, flushing, and shutdown are returned as
/// Status. Formatting, string, path, and allocation exceptions propagate. The destructor makes a
/// best-effort flush and suppresses all failures because it has no return channel.
class Logger {
   public:
    /// Constructs a logger named "tos" with Info filtering and the default console sink.
    /// Allocation exceptions from implementation state creation propagate.
    Logger();

    /// Constructs a logger from options. A null options.clock creates an owned SystemClock.
    /// Allocation exceptions from options copies or implementation state creation propagate.
    explicit Logger(LoggerOptions options);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /// Performs a best-effort flush and never throws.
    ~Logger() noexcept;

    /// Enables the default console sink if it is not already enabled.
    /// Returns kFailedPrecondition after Shutdown. Allocation exceptions while constructing an
    /// error Status propagate.
    [[nodiscard]] Status AddConsoleSink();

    /// Opens and owns an additional JSON Lines rolling-file sink.
    /// max_bytes and max_files must both be nonzero. File-system failures return Status and path
    /// or allocation exceptions propagate. Returns kFailedPrecondition after Shutdown.
    [[nodiscard]] Status AddRotatingFileSink(RotatingFileOptions options);

    /// Changes the minimum enabled severity. Unknown enum values return kInvalidArgument.
    /// Returns kFailedPrecondition after Shutdown. Allocation exceptions while constructing an
    /// error Status propagate.
    [[nodiscard]] Status SetLevel(LogLevel level);

    /// Returns the currently configured minimum severity without modifying the logger.
    /// Mutex locking exceptions propagate.
    [[nodiscard]] LogLevel level() const;

    /// Writes a preformatted message with fields. Invalid UTF-8 field data, empty field names,
    /// non-finite doubles, unknown levels, and writes after Shutdown return Status. A successful
    /// return means all configured sinks accepted the record; partial delivery is possible after
    /// a sink I/O failure. Formatting, JSON encoding, and allocation exceptions propagate.
    [[nodiscard]] Status Log(LogLevel level, const LogFields& fields, std::string_view message);

    /// Writes a preformatted message without fields.
    [[nodiscard]] Status Log(LogLevel level, std::string_view message) {
        return Log(level, LogFields{}, message);
    }

    /// Formats and writes a message with fields. fmt formatting and allocation exceptions
    /// propagate; operational sink failures are returned as Status.
    template <typename... Args>
    [[nodiscard]] Status Log(LogLevel level, const LogFields& fields,
                             fmt::format_string<Args...> format, Args&&... args) {
        return Log(level, fields, fmt::format(format, std::forward<Args>(args)...));
    }

    /// Formats and writes a message without fields.
    template <typename... Args>
    [[nodiscard]] Status Log(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
        return Log(level, LogFields{}, format, std::forward<Args>(args)...);
    }

#define TOS_DECLARE_LOG_LEVEL_METHODS(method_name, level_value)                                   \
    [[nodiscard]] Status method_name(const LogFields& fields, std::string_view message) {         \
        return Log(level_value, fields, message);                                                 \
    }                                                                                             \
    [[nodiscard]] Status method_name(std::string_view message) {                                  \
        return Log(level_value, message);                                                         \
    }                                                                                             \
    template <typename... Args>                                                                   \
    [[nodiscard]] Status method_name(const LogFields& fields, fmt::format_string<Args...> format, \
                                     Args&&... args) {                                            \
        return Log(level_value, fields, format, std::forward<Args>(args)...);                     \
    }                                                                                             \
    template <typename... Args>                                                                   \
    [[nodiscard]] Status method_name(fmt::format_string<Args...> format, Args&&... args) {        \
        return Log(level_value, format, std::forward<Args>(args)...);                             \
    }

    TOS_DECLARE_LOG_LEVEL_METHODS(Trace, LogLevel::kTrace)
    TOS_DECLARE_LOG_LEVEL_METHODS(Debug, LogLevel::kDebug)
    TOS_DECLARE_LOG_LEVEL_METHODS(Info, LogLevel::kInfo)
    TOS_DECLARE_LOG_LEVEL_METHODS(Warning, LogLevel::kWarning)
    TOS_DECLARE_LOG_LEVEL_METHODS(Error, LogLevel::kError)
    TOS_DECLARE_LOG_LEVEL_METHODS(Critical, LogLevel::kCritical)

#undef TOS_DECLARE_LOG_LEVEL_METHODS

    /// Flushes every enabled sink. Returns kFailedPrecondition after Shutdown.
    /// String allocation exceptions while constructing a failure Status propagate.
    [[nodiscard]] Status Flush();

    /// Flushes and closes every sink. It is idempotent; repeated calls return success.
    /// String allocation exceptions while constructing a failure Status propagate.
    [[nodiscard]] Status Shutdown();

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_LOGGING_H_
