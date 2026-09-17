#include "tos/base/command_line.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

static_assert(!std::is_copy_constructible_v<tos::CommandLine>);
static_assert(!std::is_copy_assignable_v<tos::CommandLine>);
static_assert(std::is_move_constructible_v<tos::CommandLine>);
static_assert(std::is_move_assignable_v<tos::CommandLine>);

tos::CommandLineSpec TestSpec() {
    return {
        "tos-tool",
        "1.2.3",
        "Run a test command.",
        {
            {"verbose", 'v', tos::CommandLineOptionValueMode::kNone, false, "",
             "Enable verbose logging."},
            {"quiet", 'q', tos::CommandLineOptionValueMode::kNone, true, "", "Reduce output."},
            {"port", 'p', tos::CommandLineOptionValueMode::kRequired, false, "PORT",
             "Listen on a port."},
            {"threshold", 't', tos::CommandLineOptionValueMode::kRequired, false, "NUMBER",
             "Set a numeric threshold."},
            {"config", '\0', tos::CommandLineOptionValueMode::kRequired, false, "FILE",
             "Read configuration."},
        },
    };
}

tos::CommandLine ParseOrFail(const tos::CommandLineSpec& spec,
                             std::vector<std::string_view> arguments) {
    auto parsed = tos::ParseCommandLine(arguments, spec);
    EXPECT_TRUE(parsed) << parsed.status().ToString();
    return std::move(parsed).value();
}

TEST(CommandLineTest, ParsesOptionsValuesClustersAndPositionals) {
    const tos::CommandLineSpec spec = TestSpec();
    const tos::CommandLine parsed = ParseOrFail(spec, {"serve", "--port=8080", "-v", "-qq", "-t",
                                                       "-5", "tail", "--", "--not-an-option", ""});

    EXPECT_EQ(parsed.action(), tos::CommandLineAction::kRun);
    ASSERT_EQ(parsed.occurrences().size(), 5U);
    EXPECT_EQ(parsed.occurrences()[0].long_name, "port");
    ASSERT_TRUE(parsed.occurrences()[0].value);
    EXPECT_EQ(*parsed.occurrences()[0].value, "8080");
    EXPECT_EQ(parsed.occurrences()[1].long_name, "verbose");
    EXPECT_FALSE(parsed.occurrences()[1].value);
    EXPECT_EQ(parsed.occurrences()[2].long_name, "quiet");
    EXPECT_EQ(parsed.occurrences()[3].long_name, "quiet");
    EXPECT_EQ(parsed.occurrences()[4].long_name, "threshold");
    ASSERT_TRUE(parsed.occurrences()[4].value);
    EXPECT_EQ(*parsed.occurrences()[4].value, "-5");
    EXPECT_TRUE(parsed.Has("verbose"));
    EXPECT_FALSE(parsed.Has("missing"));
    EXPECT_EQ(parsed.Count("quiet"), 2U);
    EXPECT_EQ(parsed.Value("port"), "8080");
    EXPECT_EQ(parsed.Value("threshold"), "-5");
    EXPECT_FALSE(parsed.Value("verbose"));
    EXPECT_FALSE(parsed.Value("missing"));

    ASSERT_EQ(parsed.positionals().size(), 4U);
    EXPECT_EQ(parsed.positionals()[0], "serve");
    EXPECT_EQ(parsed.positionals()[1], "tail");
    EXPECT_EQ(parsed.positionals()[2], "--not-an-option");
    EXPECT_EQ(parsed.positionals()[3], "");
}

TEST(CommandLineTest, ConsumesRequiredShortAndLongValues) {
    const tos::CommandLineSpec spec = TestSpec();
    const tos::CommandLine parsed = ParseOrFail(spec, {"--config", "settings.yaml", "-p", "9000"});

    EXPECT_EQ(parsed.Value("config"), "settings.yaml");
    EXPECT_EQ(parsed.Value("port"), "9000");
}

TEST(CommandLineTest, ReturnsBuiltinActionsAndIgnoresFollowingArguments) {
    const tos::CommandLineSpec spec = TestSpec();
    const tos::CommandLine help = ParseOrFail(spec, {"-v", "--help", "--unknown"});
    EXPECT_EQ(help.action(), tos::CommandLineAction::kHelp);
    EXPECT_TRUE(help.Has("verbose"));
    EXPECT_TRUE(help.positionals().empty());

    const tos::CommandLine version = ParseOrFail(spec, {"--version", "--unknown"});
    EXPECT_EQ(version.action(), tos::CommandLineAction::kVersion);

    const tos::CommandLine clustered_help = ParseOrFail(spec, {"-vh", "--unknown"});
    EXPECT_EQ(clustered_help.action(), tos::CommandLineAction::kHelp);
    EXPECT_TRUE(clustered_help.Has("verbose"));

    const tos::CommandLine positional_help = ParseOrFail(spec, {"--", "--help"});
    EXPECT_EQ(positional_help.action(), tos::CommandLineAction::kRun);
    ASSERT_EQ(positional_help.positionals().size(), 1U);
    EXPECT_EQ(positional_help.positionals()[0], "--help");
}

TEST(CommandLineTest, RejectsInvalidSpecifications) {
    tos::CommandLineSpec empty_program = TestSpec();
    empty_program.program_name.clear();
    EXPECT_EQ(tos::ParseCommandLine({}, empty_program).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec reserved_long = TestSpec();
    reserved_long.options[0].long_name = "help";
    EXPECT_EQ(tos::ParseCommandLine({}, reserved_long).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec reserved_short = TestSpec();
    reserved_short.options[0].short_name = 'h';
    EXPECT_EQ(tos::ParseCommandLine({}, reserved_short).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec reserved_version_long = TestSpec();
    reserved_version_long.options[0].long_name = "version";
    EXPECT_EQ(tos::ParseCommandLine({}, reserved_version_long).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec reserved_version_short = TestSpec();
    reserved_version_short.options[0].short_name = 'V';
    EXPECT_EQ(tos::ParseCommandLine({}, reserved_version_short).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec duplicate = TestSpec();
    duplicate.options[1].long_name = "verbose";
    EXPECT_EQ(tos::ParseCommandLine({}, duplicate).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec missing_value_name = TestSpec();
    missing_value_name.options[2].value_name.clear();
    EXPECT_EQ(tos::ParseCommandLine({}, missing_value_name).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec invalid_long_name = TestSpec();
    invalid_long_name.options[0].long_name = "not_valid";
    EXPECT_EQ(tos::ParseCommandLine({}, invalid_long_name).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec duplicate_short = TestSpec();
    duplicate_short.options[1].short_name = 'v';
    EXPECT_EQ(tos::ParseCommandLine({}, duplicate_short).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec flag_value_name = TestSpec();
    flag_value_name.options[0].value_name = "UNUSED";
    EXPECT_EQ(tos::ParseCommandLine({}, flag_value_name).status().code(),
              tos::StatusCode::kInvalidArgument);

    tos::CommandLineSpec invalid_value_mode = TestSpec();
    invalid_value_mode.options[0].value_mode = static_cast<tos::CommandLineOptionValueMode>(42);
    EXPECT_EQ(tos::ParseCommandLine({}, invalid_value_mode).status().code(),
              tos::StatusCode::kInvalidArgument);
}

TEST(CommandLineTest, RejectsInvalidArguments) {
    const tos::CommandLineSpec spec = TestSpec();
    const auto expect_invalid = [&spec](std::vector<std::string_view> arguments) {
        auto parsed = tos::ParseCommandLine(arguments, spec);
        EXPECT_FALSE(parsed);
        EXPECT_EQ(parsed.status().code(), tos::StatusCode::kInvalidArgument);
    };

    expect_invalid({"--unknown"});
    expect_invalid({"--verbose=true"});
    expect_invalid({"--port"});
    expect_invalid({"-p9000"});
    expect_invalid({"-vp"});
    expect_invalid({"--help=value"});
    expect_invalid({"-h=value"});
    const std::string embedded_nul("bad\0argument", 12);
    expect_invalid({embedded_nul});
    const std::string invalid_utf8("\xC3\x28", 2);
    expect_invalid({invalid_utf8});
}

TEST(CommandLineTest, RejectsRepeatedNonRepeatableOptions) {
    const tos::CommandLineSpec spec = TestSpec();
    auto parsed = tos::ParseCommandLine(std::vector<std::string_view>{"--verbose", "-v"}, spec);
    EXPECT_FALSE(parsed);
    EXPECT_EQ(parsed.status().code(), tos::StatusCode::kAlreadyExists);
}

TEST(CommandLineTest, OwnsParsedInputAndSupportsMove) {
    tos::CommandLineSpec spec = TestSpec();
    std::string port = "8080";
    std::vector<std::string_view> arguments{"--port", port, "serve"};
    tos::CommandLine parsed = ParseOrFail(spec, arguments);
    port = "changed";
    arguments[2] = "changed";
    spec.options[2].long_name = "changed";

    tos::CommandLine moved = std::move(parsed);
    EXPECT_EQ(moved.Value("port"), "8080");
    ASSERT_EQ(moved.positionals().size(), 1U);
    EXPECT_EQ(moved.positionals()[0], "serve");
}

TEST(CommandLineTest, FormatsDeterministicHelpAndVersion) {
    tos::CommandLineSpec spec{
        "tos-tool",
        "1.2.3",
        "Run a test command.",
        {
            {"verbose", 'v', tos::CommandLineOptionValueMode::kNone, false, "",
             "Enable verbose logging."},
            {"config", '\0', tos::CommandLineOptionValueMode::kRequired, false, "FILE",
             "Read configuration."},
        },
    };

    EXPECT_EQ(tos::FormatCommandLineHelp(spec),
              "Usage: tos-tool [options] [--] [arguments...]\n"
              "\n"
              "Run a test command.\n"
              "\n"
              "Options:\n"
              "  -h, --help         Show this help text.\n"
              "  -V, --version      Show version information.\n"
              "  -v, --verbose      Enable verbose logging.\n"
              "      --config FILE  Read configuration.\n");
    EXPECT_EQ(tos::FormatCommandLineVersion(spec), "tos-tool 1.2.3\n");
}

TEST(CommandLineTest, FormatsHelpWithoutDescription) {
    tos::CommandLineSpec spec{"tos-tool", "1.2.3", "", {}};
    EXPECT_EQ(tos::FormatCommandLineHelp(spec),
              "Usage: tos-tool [options] [--] [arguments...]\n"
              "\n"
              "Options:\n"
              "  -h, --help     Show this help text.\n"
              "  -V, --version  Show version information.\n");
}

TEST(CommandLineTest, FormatsHelpWithLongSynopsis) {
    tos::CommandLineSpec spec{
        "tos-tool",
        "1.2.3",
        "",
        {{"long-option-name", '\0', tos::CommandLineOptionValueMode::kRequired, false,
          "A_VERY_LONG_VALUE_NAME", "Long option."}},
    };

    EXPECT_EQ(tos::FormatCommandLineHelp(spec),
              "Usage: tos-tool [options] [--] [arguments...]\n"
              "\n"
              "Options:\n"
              "  -h, --help                                     Show this help text.\n"
              "  -V, --version                                  Show version information.\n"
              "      --long-option-name A_VERY_LONG_VALUE_NAME  Long option.\n");
}

}  // namespace
