#include "tos/process.h"

#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tos {
namespace {

Path HelperPath() {
    auto path = Path::Parse(TOS_PROCESS_TEST_HELPER);
    EXPECT_TRUE(path);
    return std::move(path).value();
}

ProcessOptions Helper(std::vector<std::string> arguments) {
    return ProcessOptions{HelperPath(), std::move(arguments), std::nullopt, {}};
}

TEST(ProcessTest, RunsExactArgumentsAndCapturesSeparateStreams) {
    ProcessOptions options = Helper(
        {"emit", "text with spaces \"and quotes\" \xe4\xbd\xa0\xe5\xa5\xbd", "stderr text", "17"});
    auto result = RunCommand(options);
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->exit.exit_code);
    EXPECT_EQ(*result->exit.exit_code, 17U);
    EXPECT_FALSE(result->exit.terminating_signal);
    EXPECT_EQ(result->stdout_output, "text with spaces \"and quotes\" \xe4\xbd\xa0\xe5\xa5\xbd");
    EXPECT_EQ(result->stderr_output, "stderr text");
}

TEST(ProcessTest, StartsWaitsAndTerminatesOwnedChildren) {
    auto process = Process::Start(Helper({"sleep", "5", "9"}));
    ASSERT_TRUE(process);
    EXPECT_NE(process->id(), 0U);
    auto first_exit = process->Wait();
    ASSERT_TRUE(first_exit);
    ASSERT_TRUE(first_exit->exit_code);
    EXPECT_EQ(*first_exit->exit_code, 9U);
    auto second_exit = process->Wait();
    ASSERT_TRUE(second_exit);
    EXPECT_EQ(second_exit->exit_code, first_exit->exit_code);

    auto running = Process::Start(Helper({"sleep", "1000", "0"}));
    ASSERT_TRUE(running);
    EXPECT_TRUE(running->Terminate());
    EXPECT_TRUE(running->Terminate());
    auto terminated = running->Wait();
    ASSERT_TRUE(terminated);
#ifdef _WIN32
    EXPECT_TRUE(terminated->exit_code);
#else
    EXPECT_TRUE(terminated->terminating_signal);
#endif
}

TEST(ProcessTest, AppliesWorkingDirectoryAndEnvironmentOverrides) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "tos-process-working-directory";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    ASSERT_FALSE(error);
    auto working_directory = Path::Parse(directory.u8string());
    ASSERT_TRUE(working_directory);

    ProcessOptions cwd = Helper({"cwd"});
    cwd.working_directory = std::move(working_directory).value();
    auto cwd_result = RunCommand(cwd);
    ASSERT_TRUE(cwd_result);
    const std::filesystem::path child_directory =
        std::filesystem::u8path(cwd_result->stdout_output);
    EXPECT_TRUE(std::filesystem::equivalent(child_directory, directory, error));
    EXPECT_FALSE(error);

    ProcessOptions replacement = Helper({"env", "TOS_PROCESS_TEST_VALUE"});
    replacement.environment_overrides["TOS_PROCESS_TEST_VALUE"] = "overridden";
    auto replacement_result = RunCommand(replacement);
    ASSERT_TRUE(replacement_result);
    EXPECT_EQ(replacement_result->stdout_output, "overridden");

    const char* inherited_value = std::getenv("TOS_PROCESS_TEST_VALUE");
    const std::optional<std::string> previous_value =
        inherited_value == nullptr ? std::nullopt : std::optional<std::string>(inherited_value);
#ifdef _WIN32
    ASSERT_EQ(_putenv_s("TOS_PROCESS_TEST_VALUE", "inherited"), 0);
#else
    ASSERT_EQ(setenv("TOS_PROCESS_TEST_VALUE", "inherited", 1), 0);
#endif
    ProcessOptions removed = Helper({"env", "TOS_PROCESS_TEST_VALUE"});
    removed.environment_overrides["TOS_PROCESS_TEST_VALUE"] = std::nullopt;
    auto removed_result = RunCommand(removed);
    ASSERT_TRUE(removed_result);
    EXPECT_EQ(removed_result->stdout_output, "<missing>");
#ifdef _WIN32
    ASSERT_EQ(_putenv_s("TOS_PROCESS_TEST_VALUE", previous_value ? previous_value->c_str() : ""),
              0);
#else
    if (previous_value) {
        ASSERT_EQ(setenv("TOS_PROCESS_TEST_VALUE", previous_value->c_str(), 1), 0);
    } else {
        ASSERT_EQ(unsetenv("TOS_PROCESS_TEST_VALUE"), 0);
    }
#endif

    std::filesystem::remove_all(directory, error);
}

TEST(ProcessTest, RejectsInvalidInputsAndReportsMissingExecutables) {
    const Path helper = HelperPath();
    ProcessOptions invalid{helper, {}, std::nullopt, {{"", "value"}}};
    const Result<CommandResult> invalid_result = RunCommand(invalid);
    EXPECT_FALSE(invalid_result);
    EXPECT_EQ(invalid_result.status().code(), StatusCode::kInvalidArgument);

    RunCommandOptions invalid_timeout;
    invalid_timeout.timeout = Duration::FromNanoseconds(0);
    const Result<CommandResult> timeout_result =
        RunCommand(Helper({"sleep", "1", "0"}), invalid_timeout);
    EXPECT_FALSE(timeout_result);
    EXPECT_EQ(timeout_result.status().code(), StatusCode::kInvalidArgument);

    auto missing_path = Path::Parse("tos-process-missing-executable");
    ASSERT_TRUE(missing_path);
    ProcessOptions missing{std::move(missing_path).value(), {}, std::nullopt, {}};
    const Result<Process> missing_result = Process::Start(missing);
    EXPECT_FALSE(missing_result);
    EXPECT_EQ(missing_result.status().code(), StatusCode::kNotFound);
}

TEST(ProcessTest, EnforcesTimeoutAndDrainsLargeOutputWithoutBlocking) {
    RunCommandOptions timeout;
    timeout.timeout = Millisecond;
    const Result<CommandResult> timed_out = RunCommand(Helper({"sleep", "1000", "0"}), timeout);
    EXPECT_FALSE(timed_out);
    EXPECT_EQ(timed_out.status().code(), StatusCode::kTimeout);

    RunCommandOptions limited;
    limited.max_output_bytes_per_stream = 1024;
    const Result<CommandResult> too_large =
        RunCommand(Helper({"large", "65536", "65536"}), limited);
    EXPECT_FALSE(too_large);
    EXPECT_EQ(too_large.status().code(), StatusCode::kResourceExhausted);
}

}  // namespace
}  // namespace tos
