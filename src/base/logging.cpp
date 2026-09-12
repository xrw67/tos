#include "tos/base/logging.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <system_error>
#include <vector>

#include "tos/base/json.h"

namespace tos {
namespace {

bool IsKnownLevel(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kTrace:
        case LogLevel::kDebug:
        case LogLevel::kInfo:
        case LogLevel::kWarning:
        case LogLevel::kError:
        case LogLevel::kCritical:
        case LogLevel::kOff:
            return true;
    }
    return false;
}

bool IsEnabled(LogLevel threshold, LogLevel level) noexcept {
    return threshold != LogLevel::kOff && level != LogLevel::kOff &&
           static_cast<int>(level) >= static_cast<int>(threshold);
}

const char* LevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarning:
            return "WARNING";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kCritical:
            return "CRITICAL";
        case LogLevel::kOff:
            return "OFF";
    }
    return "UNKNOWN";
}

Status FileError(const std::error_code& error, std::string_view action, const Path& path) {
    StatusCode code = StatusCode::kUnavailable;
    if (error == std::errc::permission_denied) {
        code = StatusCode::kPermissionDenied;
    } else if (error == std::errc::no_such_file_or_directory) {
        code = StatusCode::kNotFound;
    } else if (error == std::errc::no_space_on_device) {
        code = StatusCode::kResourceExhausted;
    }
    return Status(code, std::string(action) + " '" + path.utf8() + "': " + error.message());
}

Status StreamError(std::string_view action, const Path& path) {
    return Status(StatusCode::kUnavailable, std::string(action) + " '" + path.utf8() + "'");
}

std::filesystem::path NativeLogPath(const Path& path) {
    return std::filesystem::u8path(path.utf8());
}

bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        unsigned char minimum_second = 0x80;
        unsigned char maximum_second = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
        } else if (first == 0xe0) {
            length = 3;
            minimum_second = 0xa0;
        } else if (first >= 0xe1 && first <= 0xec) {
            length = 3;
        } else if (first == 0xed) {
            length = 3;
            maximum_second = 0x9f;
        } else if (first >= 0xee && first <= 0xef) {
            length = 3;
        } else if (first == 0xf0) {
            length = 4;
            minimum_second = 0x90;
        } else if (first >= 0xf1 && first <= 0xf3) {
            length = 4;
        } else if (first == 0xf4) {
            length = 4;
            maximum_second = 0x8f;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if (second < minimum_second || second > maximum_second) {
            return false;
        }
        for (std::size_t continuation = 2; continuation < length; ++continuation) {
            const unsigned char byte = static_cast<unsigned char>(text[index + continuation]);
            if (byte < 0x80 || byte > 0xbf) {
                return false;
            }
        }
        index += length;
    }
    return true;
}

Status ValidateRecord(std::string_view logger_name, std::string_view message,
                      const LogFields& fields) {
    if (!IsValidUtf8(logger_name) || !IsValidUtf8(message)) {
        return Status(StatusCode::kInvalidArgument, "log names and messages must be valid UTF-8");
    }
    for (const auto& field : fields) {
        if (field.first.empty() || !IsValidUtf8(field.first)) {
            return Status(StatusCode::kInvalidArgument,
                          "log field names must be nonempty valid UTF-8");
        }
        if (const auto* value = std::get_if<std::string>(&field.second)) {
            if (!IsValidUtf8(*value)) {
                return Status(StatusCode::kInvalidArgument,
                              "log string field values must be valid UTF-8");
            }
        } else if (const auto* value = std::get_if<double>(&field.second)) {
            if (!std::isfinite(*value)) {
                return Status(StatusCode::kInvalidArgument,
                              "log double field values must be finite");
            }
        }
    }
    return Status::Ok();
}

json JsonValue(const LogValue& value) {
    return std::visit([](const auto& item) { return json(item); }, value);
}

json JsonFields(const LogFields& fields) {
    json encoded = json::object();
    for (const auto& field : fields) {
        encoded[field.first] = JsonValue(field.second);
    }
    return encoded;
}

std::string ConsoleLine(Time timestamp, LogLevel level, std::string_view logger_name,
                        std::string_view message, const LogFields& fields) {
    std::string line = timestamp.FormatRfc3339();
    line.append(" [");
    line.append(LevelName(level));
    line.append("] [");
    line.append(logger_name);
    line.append("] ");
    line.append(message);
    for (const auto& field : fields) {
        line.push_back(' ');
        line.append(field.first);
        line.push_back('=');
        line.append(JsonValue(field.second).dump());
    }
    line.push_back('\n');
    return line;
}

std::string JsonLine(Time timestamp, LogLevel level, std::string_view logger_name,
                     std::string_view message, const LogFields& fields) {
    json record = json::object();
    record["timestamp"] = timestamp.FormatRfc3339();
    record["level"] = LevelName(level);
    record["logger"] = logger_name;
    record["message"] = message;
    record["fields"] = JsonFields(fields);
    std::string line = record.dump();
    line.push_back('\n');
    return line;
}

class RotatingFileSink {
   public:
    explicit RotatingFileSink(RotatingFileOptions options) : options_(std::move(options)) {}

    Status Open() {
        stream_.open(NativeLogPath(options_.path), std::ios::binary | std::ios::app);
        if (!stream_.is_open()) {
            const std::error_code error = std::make_error_code(std::errc::io_error);
            return FileError(error, "could not open log file", options_.path);
        }

        std::error_code error;
        current_size_ = std::filesystem::file_size(NativeLogPath(options_.path), error);
        if (error) {
            stream_.close();
            return FileError(error, "could not inspect log file", options_.path);
        }
        return Status::Ok();
    }

    Status Write(std::string_view line) {
        const std::size_t line_size = line.size();
        if (current_size_ != 0 &&
            (line_size > options_.max_bytes || current_size_ > options_.max_bytes - line_size)) {
            Status rotated = Rotate();
            if (!rotated) {
                return rotated;
            }
        }

        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        if (!stream_) {
            return StreamError("could not write log file", options_.path);
        }
        current_size_ += line_size;
        return Status::Ok();
    }

    Status Flush() {
        stream_.flush();
        if (!stream_) {
            return StreamError("could not flush log file", options_.path);
        }
        return Status::Ok();
    }

   private:
    Path ArchivePath(std::size_t index) const {
        return std::move(Path::Parse(options_.path.utf8() + "." + std::to_string(index))).value();
    }

    Status Rotate() {
        Status flushed = Flush();
        if (!flushed) {
            return flushed;
        }
        stream_.close();

        std::error_code error;
        const Path oldest = ArchivePath(options_.max_files);
        const bool oldest_exists = std::filesystem::exists(NativeLogPath(oldest), error);
        if (error) {
            return FileError(error, "could not inspect log archive", oldest);
        }
        if (oldest_exists && !std::filesystem::remove(NativeLogPath(oldest), error)) {
            if (error) {
                return FileError(error, "could not remove log archive", oldest);
            }
            return Status(StatusCode::kUnavailable,
                          "could not remove log archive '" + oldest.utf8() + "'");
        }
        if (error) {
            return FileError(error, "could not remove log archive", oldest);
        }

        for (std::size_t index = options_.max_files; index > 1; --index) {
            const Path source = ArchivePath(index - 1);
            const bool source_exists = std::filesystem::exists(NativeLogPath(source), error);
            if (error) {
                return FileError(error, "could not inspect log archive", source);
            }
            if (source_exists) {
                const Path destination = ArchivePath(index);
                std::filesystem::rename(NativeLogPath(source), NativeLogPath(destination), error);
                if (error) {
                    return FileError(error, "could not rotate log archive", source);
                }
            }
        }

        const Path newest = ArchivePath(1);
        std::filesystem::rename(NativeLogPath(options_.path), NativeLogPath(newest), error);
        if (error) {
            return FileError(error, "could not rotate log file", options_.path);
        }

        stream_.open(NativeLogPath(options_.path), std::ios::binary | std::ios::trunc);
        if (!stream_.is_open()) {
            const std::error_code io_error = std::make_error_code(std::errc::io_error);
            return FileError(io_error, "could not reopen log file", options_.path);
        }
        current_size_ = 0;
        return Status::Ok();
    }

    RotatingFileOptions options_;
    std::ofstream stream_;
    std::uintmax_t current_size_ = 0;
};

void RecordFirstFailure(Status candidate, Status* first_failure) {
    if (first_failure->ok() && !candidate.ok()) {
        *first_failure = std::move(candidate);
    }
}

}  // namespace

class Logger::Impl {
   public:
    explicit Impl(LoggerOptions options)
        : name_(std::move(options.name)),
          level_(options.level),
          console_enabled_(options.console),
          clock_(options.clock ? std::move(options.clock) : std::make_shared<SystemClock>()) {}

    Status AddConsoleSink() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return ClosedStatus();
        }
        console_enabled_ = true;
        return Status::Ok();
    }

    Status AddRotatingFileSink(RotatingFileOptions options) {
        if (options.path.empty() || options.max_bytes == 0 || options.max_files == 0) {
            return Status(StatusCode::kInvalidArgument,
                          "log file path, max_bytes, and max_files must be nonzero");
        }
        auto sink = std::make_unique<RotatingFileSink>(std::move(options));
        Status opened = sink->Open();
        if (!opened) {
            return opened;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return ClosedStatus();
        }
        files_.push_back(std::move(sink));
        return Status::Ok();
    }

    Status SetLevel(LogLevel level) {
        if (!IsKnownLevel(level)) {
            return Status(StatusCode::kInvalidArgument, "unknown log level");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return ClosedStatus();
        }
        level_ = level;
        return Status::Ok();
    }

    LogLevel level() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

    Status Log(LogLevel level, const LogFields& fields, std::string_view message) {
        if (!IsKnownLevel(level)) {
            return Status(StatusCode::kInvalidArgument, "unknown log level");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return ClosedStatus();
        }
        if (!IsEnabled(level_, level)) {
            return Status::Ok();
        }

        Status valid = ValidateRecord(name_, message, fields);
        if (!valid) {
            return valid;
        }
        const Time timestamp = clock_->Now();
        const std::string console_line = ConsoleLine(timestamp, level, name_, message, fields);
        const std::string json_line = JsonLine(timestamp, level, name_, message, fields);

        Status first_failure = Status::Ok();
        if (console_enabled_) {
            std::ostream& output = level <= LogLevel::kInfo ? std::cout : std::cerr;
            try {
                output.write(console_line.data(),
                             static_cast<std::streamsize>(console_line.size()));
            } catch (const std::ios_base::failure&) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not write console log"),
                                   &first_failure);
            }
            if (!output) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not write console log"),
                                   &first_failure);
            }
        }
        for (const std::unique_ptr<RotatingFileSink>& file : files_) {
            RecordFirstFailure(file->Write(json_line), &first_failure);
        }
        return first_failure;
    }

    Status Flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return ClosedStatus();
        }
        return FlushLocked();
    }

    Status Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) {
            return Status::Ok();
        }
        Status flushed = FlushLocked();
        shutdown_ = true;
        return flushed;
    }

   private:
    static Status ClosedStatus() {
        return Status(StatusCode::kFailedPrecondition, "logger has been shut down");
    }

    Status FlushLocked() {
        Status first_failure = Status::Ok();
        if (console_enabled_) {
            try {
                std::cout.flush();
            } catch (const std::ios_base::failure&) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not flush stdout log"),
                                   &first_failure);
            }
            if (!std::cout) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not flush stdout log"),
                                   &first_failure);
            }
            try {
                std::cerr.flush();
            } catch (const std::ios_base::failure&) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not flush stderr log"),
                                   &first_failure);
            }
            if (!std::cerr) {
                RecordFirstFailure(Status(StatusCode::kUnavailable, "could not flush stderr log"),
                                   &first_failure);
            }
        }
        for (const std::unique_ptr<RotatingFileSink>& file : files_) {
            RecordFirstFailure(file->Flush(), &first_failure);
        }
        return first_failure;
    }

    mutable std::mutex mutex_;
    std::string name_;
    LogLevel level_;
    bool console_enabled_;
    bool shutdown_ = false;
    std::shared_ptr<const IClock> clock_;
    std::vector<std::unique_ptr<RotatingFileSink>> files_;
};

Logger::Logger() : Logger(LoggerOptions{}) {}

Logger::Logger(LoggerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

Logger::~Logger() noexcept {
    try {
        (void)impl_->Shutdown();
    } catch (...) {
    }
}

Status Logger::AddConsoleSink() { return impl_->AddConsoleSink(); }

Status Logger::AddRotatingFileSink(RotatingFileOptions options) {
    return impl_->AddRotatingFileSink(std::move(options));
}

Status Logger::SetLevel(LogLevel level) { return impl_->SetLevel(level); }

LogLevel Logger::level() const { return impl_->level(); }

Status Logger::Log(LogLevel level, const LogFields& fields, std::string_view message) {
    return impl_->Log(level, fields, message);
}

Status Logger::Flush() { return impl_->Flush(); }

Status Logger::Shutdown() { return impl_->Shutdown(); }

}  // namespace tos
