#ifndef TOS_BASE_COMMAND_LINE_H_
#define TOS_BASE_COMMAND_LINE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tos/base/result.h"
#include "tos/base/span.h"

namespace tos {

/// Whether an option accepts no value or exactly one value.
enum class CommandLineOptionValueMode { kNone, kRequired };

/// One declared command-line option.
///
/// Each string owns its text. A short_name of '\0' means no short spelling. Required values use
/// value_name only when formatting help text; it must be nonempty. Constructing or assigning the
/// strings can throw. This value type has identical behavior on Linux, macOS, and Windows.
struct CommandLineOption {
    std::string long_name;
    char short_name = '\0';
    CommandLineOptionValueMode value_mode = CommandLineOptionValueMode::kNone;
    bool repeatable = false;
    std::string value_name;
    std::string description;
};

/// Metadata and declared options for one command-line interface.
///
/// program_name and version must be nonempty. Options retain declaration order for help output.
/// Each field owns its text; constructing or modifying it can throw. This value type does not
/// synchronize concurrent mutation. It has identical behavior on Linux, macOS, and Windows.
struct CommandLineSpec {
    std::string program_name;
    std::string version;
    std::string description;
    std::vector<CommandLineOption> options;
};

/// The action selected by command-line parsing.
enum class CommandLineAction { kRun, kHelp, kVersion };

/// One parsed option occurrence, normalized to its long name.
///
/// value is empty for a flag and independently owns a required option value otherwise. Constructing
/// or modifying the strings can throw. This value type has identical behavior on Linux, macOS, and
/// Windows.
struct CommandLineOccurrence {
    std::string long_name;
    std::optional<std::string> value;
};

class CommandLine;

[[nodiscard]] Result<CommandLine> ParseCommandLine(span<const std::string_view> arguments,
                                                   const CommandLineSpec& spec);

/// Move-only result of parsing one command-line argument list.
///
/// The result owns all option names, values, and positional arguments. Const operations are safe
/// concurrently; moving or destroying the same object requires caller synchronization.
class [[nodiscard]] CommandLine final {
   public:
    CommandLine(const CommandLine&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&) noexcept = default;
    CommandLine& operator=(CommandLine&&) noexcept = default;
    ~CommandLine() = default;

    /// Returns the selected run, help, or version action without throwing.
    [[nodiscard]] CommandLineAction action() const noexcept { return action_; }

    /// Returns parsed options in encounter order. The view remains valid while this result lives.
    [[nodiscard]] span<const CommandLineOccurrence> occurrences() const noexcept {
        return occurrences_;
    }

    /// Returns positional arguments in encounter order. The view remains valid while this result
    /// lives.
    [[nodiscard]] span<const std::string> positionals() const noexcept { return positionals_; }

    /// Returns whether long_name appeared at least once. This method does not throw.
    [[nodiscard]] bool Has(std::string_view long_name) const noexcept {
        return Count(long_name) != 0;
    }

    /// Returns the number of occurrences of long_name. This method does not throw.
    [[nodiscard]] std::size_t Count(std::string_view long_name) const noexcept {
        std::size_t count = 0;
        for (const CommandLineOccurrence& occurrence : occurrences_) {
            if (occurrence.long_name == long_name) {
                ++count;
            }
        }
        return count;
    }

    /// Returns the required value for one indexed occurrence of long_name.
    ///
    /// std::nullopt means that the occurrence is absent or is a flag. The returned view remains
    /// valid while this result lives and this method does not throw.
    [[nodiscard]] std::optional<std::string_view> Value(
        std::string_view long_name, std::size_t occurrence_index = 0) const noexcept {
        std::size_t matched = 0;
        for (const CommandLineOccurrence& occurrence : occurrences_) {
            if (occurrence.long_name != long_name) {
                continue;
            }
            if (matched++ == occurrence_index) {
                if (!occurrence.value) {
                    return std::nullopt;
                }
                return *occurrence.value;
            }
        }
        return std::nullopt;
    }

   private:
    friend Result<CommandLine> ParseCommandLine(span<const std::string_view> arguments,
                                                const CommandLineSpec& spec);

    CommandLine(CommandLineAction action, std::vector<CommandLineOccurrence> occurrences,
                std::vector<std::string> positionals) noexcept
        : action_(action),
          occurrences_(std::move(occurrences)),
          positionals_(std::move(positionals)) {}

    CommandLineAction action_ = CommandLineAction::kRun;
    std::vector<CommandLineOccurrence> occurrences_;
    std::vector<std::string> positionals_;
};

namespace command_line_detail {

inline bool ContainsNul(std::string_view text) noexcept {
    return text.find('\0') != std::string_view::npos;
}

inline bool IsUtf8ContinuationByte(unsigned char byte) noexcept {
    return byte >= 0x80U && byte <= 0xBFU;
}

inline bool IsValidUtf8(std::string_view text) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = bytes[index];
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU && index + 1 < text.size() &&
            IsUtf8ContinuationByte(bytes[index + 1])) {
            index += 2;
            continue;
        }
        if (first == 0xE0U && index + 2 < text.size() && bytes[index + 1] >= 0xA0U &&
            bytes[index + 1] <= 0xBFU && IsUtf8ContinuationByte(bytes[index + 2])) {
            index += 3;
            continue;
        }
        if (((first >= 0xE1U && first <= 0xECU) || (first >= 0xEEU && first <= 0xEFU)) &&
            index + 2 < text.size() && IsUtf8ContinuationByte(bytes[index + 1]) &&
            IsUtf8ContinuationByte(bytes[index + 2])) {
            index += 3;
            continue;
        }
        if (first == 0xEDU && index + 2 < text.size() && bytes[index + 1] >= 0x80U &&
            bytes[index + 1] <= 0x9FU && IsUtf8ContinuationByte(bytes[index + 2])) {
            index += 3;
            continue;
        }
        if (first == 0xF0U && index + 3 < text.size() && bytes[index + 1] >= 0x90U &&
            bytes[index + 1] <= 0xBFU && IsUtf8ContinuationByte(bytes[index + 2]) &&
            IsUtf8ContinuationByte(bytes[index + 3])) {
            index += 4;
            continue;
        }
        if (first >= 0xF1U && first <= 0xF3U && index + 3 < text.size() &&
            IsUtf8ContinuationByte(bytes[index + 1]) && IsUtf8ContinuationByte(bytes[index + 2]) &&
            IsUtf8ContinuationByte(bytes[index + 3])) {
            index += 4;
            continue;
        }
        if (first == 0xF4U && index + 3 < text.size() && bytes[index + 1] >= 0x80U &&
            bytes[index + 1] <= 0x8FU && IsUtf8ContinuationByte(bytes[index + 2]) &&
            IsUtf8ContinuationByte(bytes[index + 3])) {
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

inline bool IsAsciiAlpha(char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

inline bool IsAsciiAlphaNumeric(char character) noexcept {
    return IsAsciiAlpha(character) || (character >= '0' && character <= '9');
}

inline bool IsLongName(std::string_view name) noexcept {
    if (name.empty() || !IsAsciiAlpha(name.front())) {
        return false;
    }
    for (const char character : name.substr(1)) {
        if (!IsAsciiAlphaNumeric(character) && character != '-') {
            return false;
        }
    }
    return true;
}

inline bool IsShortName(char name) noexcept { return IsAsciiAlphaNumeric(name); }

inline Status InvalidSpec(std::string_view detail) {
    return Status(StatusCode::kInvalidArgument,
                  "invalid command-line specification: " + std::string(detail));
}

inline Status InvalidArgument(std::string_view detail) {
    return Status(StatusCode::kInvalidArgument,
                  "invalid command-line argument: " + std::string(detail));
}

inline Status ValidateSpec(const CommandLineSpec& spec) {
    if (spec.program_name.empty() || ContainsNul(spec.program_name)) {
        return InvalidSpec("program name must be nonempty and NUL-free");
    }
    if (spec.version.empty() || ContainsNul(spec.version)) {
        return InvalidSpec("version must be nonempty and NUL-free");
    }
    if (ContainsNul(spec.description)) {
        return InvalidSpec("description must be NUL-free");
    }

    for (std::size_t index = 0; index < spec.options.size(); ++index) {
        const CommandLineOption& option = spec.options[index];
        if (!IsLongName(option.long_name) || option.long_name == "help" ||
            option.long_name == "version") {
            return InvalidSpec("option long name is invalid or reserved");
        }
        if (option.short_name != '\0' && (!IsShortName(option.short_name) ||
                                          option.short_name == 'h' || option.short_name == 'V')) {
            return InvalidSpec("option short name is invalid or reserved");
        }
        if (ContainsNul(option.value_name) || ContainsNul(option.description)) {
            return InvalidSpec("option value name and description must be NUL-free");
        }
        if (option.value_mode != CommandLineOptionValueMode::kNone &&
            option.value_mode != CommandLineOptionValueMode::kRequired) {
            return InvalidSpec("option value mode is invalid");
        }
        if (option.value_mode == CommandLineOptionValueMode::kRequired &&
            option.value_name.empty()) {
            return InvalidSpec("required option value name must be nonempty");
        }
        if (option.value_mode == CommandLineOptionValueMode::kNone && !option.value_name.empty()) {
            return InvalidSpec("flag option value name must be empty");
        }

        for (std::size_t previous = 0; previous < index; ++previous) {
            const CommandLineOption& earlier = spec.options[previous];
            if (option.long_name == earlier.long_name) {
                return InvalidSpec("duplicate option long name");
            }
            if (option.short_name != '\0' && option.short_name == earlier.short_name) {
                return InvalidSpec("duplicate option short name");
            }
        }
    }
    return Status::Ok();
}

inline const CommandLineOption* FindLongOption(const CommandLineSpec& spec,
                                               std::string_view name) noexcept {
    for (const CommandLineOption& option : spec.options) {
        if (option.long_name == name) {
            return &option;
        }
    }
    return nullptr;
}

inline const CommandLineOption* FindShortOption(const CommandLineSpec& spec, char name) noexcept {
    for (const CommandLineOption& option : spec.options) {
        if (option.short_name == name) {
            return &option;
        }
    }
    return nullptr;
}

inline bool HasOccurrence(const std::vector<CommandLineOccurrence>& occurrences,
                          std::string_view long_name) noexcept {
    for (const CommandLineOccurrence& occurrence : occurrences) {
        if (occurrence.long_name == long_name) {
            return true;
        }
    }
    return false;
}

inline Status AddOccurrence(const CommandLineOption& option, std::optional<std::string> value,
                            std::vector<CommandLineOccurrence>* occurrences) {
    if (!option.repeatable && HasOccurrence(*occurrences, option.long_name)) {
        return Status(StatusCode::kAlreadyExists,
                      "command-line option '--" + option.long_name + "' was repeated");
    }
    occurrences->push_back(CommandLineOccurrence{option.long_name, std::move(value)});
    return Status::Ok();
}

inline std::string OptionSynopsis(const CommandLineOption& option) {
    std::string synopsis;
    if (option.short_name != '\0') {
        synopsis = "-";
        synopsis.push_back(option.short_name);
        synopsis.append(", --");
    } else {
        synopsis = "    --";
    }
    synopsis.append(option.long_name);
    if (option.value_mode == CommandLineOptionValueMode::kRequired) {
        synopsis.push_back(' ');
        synopsis.append(option.value_name);
    }
    return synopsis;
}

inline void AppendHelpOption(std::string* output, std::string_view synopsis,
                             std::string_view description, std::size_t width) {
    output->append("  ");
    output->append(synopsis.data(), synopsis.size());
    output->append(width - synopsis.size() + 2, ' ');
    output->append(description.data(), description.size());
    output->push_back('\n');
}

}  // namespace command_line_detail

/// Parses UTF-8 arguments that exclude argv[0] according to spec.
///
/// The parser accepts --name=value, --name value, -n value, flag-only short-option clusters, and
/// -- as an option terminator. Every examined argument must be well-formed UTF-8 and NUL-free; a
/// violation, invalid specification, or invalid syntax returns kInvalidArgument. Repeated
/// non-repeatable options return kAlreadyExists. It owns all returned text and allocation
/// exceptions propagate. This function is safe to call concurrently with independent arguments and
/// specs.
[[nodiscard]] inline Result<CommandLine> ParseCommandLine(span<const std::string_view> arguments,
                                                          const CommandLineSpec& spec) {
    Status valid_spec = command_line_detail::ValidateSpec(spec);
    if (!valid_spec) {
        return valid_spec;
    }

    std::vector<CommandLineOccurrence> occurrences;
    std::vector<std::string> positionals;
    bool options_ended = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (command_line_detail::ContainsNul(argument)) {
            return command_line_detail::InvalidArgument("argument contains NUL");
        }
        if (!command_line_detail::IsValidUtf8(argument)) {
            return command_line_detail::InvalidArgument("argument is not valid UTF-8");
        }
        if (options_ended) {
            positionals.emplace_back(argument);
            continue;
        }
        if (argument == "--") {
            options_ended = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            return CommandLine(CommandLineAction::kHelp, std::move(occurrences),
                               std::move(positionals));
        }
        if (argument == "--version" || argument == "-V") {
            return CommandLine(CommandLineAction::kVersion, std::move(occurrences),
                               std::move(positionals));
        }
        if (argument.size() > 2 && argument[0] == '-' && argument[1] == '-') {
            const std::string_view body = argument.substr(2);
            const std::size_t equal = body.find('=');
            const std::string_view name = body.substr(0, equal);
            const std::optional<std::string_view> inline_value =
                equal == std::string_view::npos
                    ? std::nullopt
                    : std::optional<std::string_view>(body.substr(equal + 1));
            if (name == "help" || name == "version") {
                return command_line_detail::InvalidArgument(
                    "built-in option does not accept a value");
            }
            const CommandLineOption* option = command_line_detail::FindLongOption(spec, name);
            if (option == nullptr) {
                return command_line_detail::InvalidArgument("unknown option '--" +
                                                            std::string(name) + "'");
            }
            if (option->value_mode == CommandLineOptionValueMode::kNone) {
                if (inline_value) {
                    return command_line_detail::InvalidArgument(
                        "flag option '--" + option->long_name + "' does not accept a value");
                }
                Status added =
                    command_line_detail::AddOccurrence(*option, std::nullopt, &occurrences);
                if (!added) {
                    return added;
                }
                continue;
            }

            std::string value;
            if (inline_value) {
                value.assign(inline_value->data(), inline_value->size());
            } else {
                if (++index == arguments.size()) {
                    return command_line_detail::InvalidArgument("option '--" + option->long_name +
                                                                "' requires a value");
                }
                const std::string_view next = arguments[index];
                if (command_line_detail::ContainsNul(next)) {
                    return command_line_detail::InvalidArgument("argument contains NUL");
                }
                if (!command_line_detail::IsValidUtf8(next)) {
                    return command_line_detail::InvalidArgument("argument is not valid UTF-8");
                }
                value.assign(next.data(), next.size());
            }
            Status added =
                command_line_detail::AddOccurrence(*option, std::move(value), &occurrences);
            if (!added) {
                return added;
            }
            continue;
        }
        if (argument.size() > 1 && argument.front() == '-') {
            for (std::size_t short_index = 1; short_index < argument.size(); ++short_index) {
                const char name = argument[short_index];
                if (name == 'h') {
                    if (short_index + 1 < argument.size() && argument[short_index + 1] == '=') {
                        return command_line_detail::InvalidArgument(
                            "built-in option '-h' does not accept a value");
                    }
                    return CommandLine(CommandLineAction::kHelp, std::move(occurrences),
                                       std::move(positionals));
                }
                if (name == 'V') {
                    if (short_index + 1 < argument.size() && argument[short_index + 1] == '=') {
                        return command_line_detail::InvalidArgument(
                            "built-in option '-V' does not accept a value");
                    }
                    return CommandLine(CommandLineAction::kVersion, std::move(occurrences),
                                       std::move(positionals));
                }
                const CommandLineOption* option = command_line_detail::FindShortOption(spec, name);
                if (option == nullptr) {
                    return command_line_detail::InvalidArgument("unknown option '-" +
                                                                std::string(1, name) + "'");
                }
                if (option->value_mode == CommandLineOptionValueMode::kNone) {
                    Status added =
                        command_line_detail::AddOccurrence(*option, std::nullopt, &occurrences);
                    if (!added) {
                        return added;
                    }
                    continue;
                }
                if (short_index + 1 != argument.size()) {
                    return command_line_detail::InvalidArgument(
                        "value option '-" + option->long_name +
                        "' cannot be in a short option cluster");
                }
                if (++index == arguments.size()) {
                    return command_line_detail::InvalidArgument("option '-" + std::string(1, name) +
                                                                "' requires a value");
                }
                const std::string_view value = arguments[index];
                if (command_line_detail::ContainsNul(value)) {
                    return command_line_detail::InvalidArgument("argument contains NUL");
                }
                if (!command_line_detail::IsValidUtf8(value)) {
                    return command_line_detail::InvalidArgument("argument is not valid UTF-8");
                }
                Status added = command_line_detail::AddOccurrence(
                    *option, std::string(value.data(), value.size()), &occurrences);
                if (!added) {
                    return added;
                }
            }
            continue;
        }
        positionals.emplace_back(argument);
    }
    return CommandLine(CommandLineAction::kRun, std::move(occurrences), std::move(positionals));
}

/// Formats deterministic plain-text help for spec without inspecting the terminal.
///
/// The output lists built-in help and version options before declared options. Allocation
/// exceptions propagate. The function does not validate spec and is safe to call concurrently.
[[nodiscard]] inline std::string FormatCommandLineHelp(const CommandLineSpec& spec) {
    struct HelpOption {
        std::string synopsis;
        std::string_view description;
    };

    std::vector<HelpOption> options;
    options.push_back(HelpOption{"-h, --help", "Show this help text."});
    options.push_back(HelpOption{"-V, --version", "Show version information."});
    for (const CommandLineOption& option : spec.options) {
        options.push_back(
            HelpOption{command_line_detail::OptionSynopsis(option), option.description});
    }

    std::size_t width = 0;
    for (const HelpOption& option : options) {
        if (option.synopsis.size() > width) {
            width = option.synopsis.size();
        }
    }

    std::string output = "Usage: ";
    output.append(spec.program_name);
    output.append(" [options] [--] [arguments...]\n");
    if (!spec.description.empty()) {
        output.push_back('\n');
        output.append(spec.description);
        output.push_back('\n');
    }
    output.append("\nOptions:\n");
    for (const HelpOption& option : options) {
        command_line_detail::AppendHelpOption(&output, option.synopsis, option.description, width);
    }
    return output;
}

/// Formats the deterministic version line for spec.
///
/// The returned text is "<program_name> <version>\\n". Allocation exceptions propagate and this
/// function is safe to call concurrently.
[[nodiscard]] inline std::string FormatCommandLineVersion(const CommandLineSpec& spec) {
    return spec.program_name + " " + spec.version + "\n";
}

}  // namespace tos

#endif  // TOS_BASE_COMMAND_LINE_H_
