#include "tos/app/debug.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tos/app/app.h"
#include "tos/base/filesystem.h"

namespace {

using namespace std::chrono_literals;

tos::AppOptions QuietOptions() {
    tos::AppOptions options;
    options.log.console = false;
    options.thread_pool_worker_count = 1;
    options.thread_pool_queue_capacity = 3;
    return options;
}

tos::Path ParsePathOrThrow(const std::filesystem::path& path) {
    auto parsed = tos::Path::Parse(path.u8string());
    if (!parsed) {
        throw std::runtime_error(parsed.status().ToString());
    }
    return std::move(parsed).value();
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string ExpectedStatus(std::string_view app_state, std::string_view log_level) {
    return "app_state=" + std::string(app_state) + "\n" + "log_level=" + std::string(log_level) +
           "\n" +
           "executor.worker_count=1\n"
           "executor.queue_capacity=3\n"
           "executor.queued=0\n"
           "executor.running=0\n"
           "executor.accepted=0\n"
           "executor.rejected=0\n"
           "executor.completed=0\n";
}

std::string ExpectedError(std::string_view detail) {
    return "error=INVALID_ARGUMENT: invalid debug command: " + std::string(detail) + "\n";
}

TEST(DebugControllerTest, ReturnsStableStatusTextAndChangesLogLevel) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::ostringstream output;

    ASSERT_TRUE(debug.Execute(" \tstatus\r\n", output));
    EXPECT_EQ(output.str(), ExpectedStatus("created", "info"));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level trace", output));
    EXPECT_EQ(output.str(), ExpectedStatus("created", "trace"));
    EXPECT_EQ(app.logger().level(), tos::LogLevel::kTrace);

    ASSERT_TRUE(app.Start());
    ASSERT_TRUE(app.Stop());
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("status", output));
    EXPECT_EQ(output.str(), ExpectedStatus("stopped", "trace"));
}

TEST(DebugControllerTest, RejectsMalformedAndInvalidAppCommands) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::ostringstream output;

    EXPECT_EQ(debug.Execute("", output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(debug.Execute(" \t\n", output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(debug.Execute("missing", output).code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(output.str(), "");

    ASSERT_TRUE(debug.Execute("status extra", output));
    EXPECT_EQ(output.str(), ExpectedError("status does not accept arguments"));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level", output));
    EXPECT_EQ(output.str(), ExpectedError("log-level requires exactly one argument"));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level trace extra", output));
    EXPECT_EQ(output.str(), ExpectedError("log-level requires exactly one argument"));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level TRACE", output));
    EXPECT_EQ(output.str(), ExpectedError("log level is not recognized"));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level \"trace\"", output));
    EXPECT_EQ(output.str(), ExpectedError("log level is not recognized"));

    const std::string with_nul("status\0tail", 11);
    output.str("");
    output.clear();
    EXPECT_EQ(debug.Execute(with_nul, output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
}

TEST(DebugControllerTest, ChangesLoggerFilteringThroughTextCommand) {
    static std::atomic<std::uint64_t> next_id{0};
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("tos-debug-test-" + std::to_string(next_id.fetch_add(1)) + ".jsonl");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    tos::App app(QuietOptions());
    ASSERT_TRUE(app.logger().AddRotatingFileSink({ParsePathOrThrow(path), 4096, 1}));
    tos::DebugController& debug = app.debug();
    std::ostringstream output;
    ASSERT_TRUE(debug.Execute("log-level off", output));
    ASSERT_TRUE(app.logger().Info("filtered-by-debug-controller"));
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level trace", output));
    ASSERT_TRUE(app.logger().Debug("written-by-debug-controller"));
    ASSERT_TRUE(app.logger().Flush());

    const std::string records = ReadFile(path);
    EXPECT_EQ(records.find("filtered-by-debug-controller"), std::string::npos);
    EXPECT_NE(records.find("written-by-debug-controller"), std::string::npos);
    ASSERT_TRUE(app.Stop());
    ASSERT_TRUE(app.logger().Shutdown());
    std::filesystem::remove(path, ignored);
}

TEST(DebugControllerTest, WritesLoggerFailuresToOutput) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::ostringstream output;

    ASSERT_TRUE(app.logger().Shutdown());
    ASSERT_TRUE(debug.Execute("log-level info", output));
    EXPECT_EQ(output.str(), "error=FAILED_PRECONDITION: logger has been shut down\n");
}

TEST(DebugControllerTest, RegistersCustomHandlersAndAllowsReplacement) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::vector<std::string> observed;
    std::ostringstream output;

    ASSERT_TRUE(debug.RegisterHandler(
        "echo", [&observed](const tos::span<std::string>& args, std::ostream& output) {
            observed.assign(args.begin(), args.end());
            output << "echo-result";
        }));
    ASSERT_TRUE(debug.Execute("echo first second", output));
    EXPECT_EQ(output.str(), "echo-result");
    EXPECT_EQ(observed, (std::vector<std::string>{"first", "second"}));

    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("echo \"two words\"", output));
    EXPECT_EQ(output.str(), "echo-result");
    EXPECT_EQ(observed, (std::vector<std::string>{"\"two", "words\""}));

    EXPECT_EQ(
        debug.RegisterHandler("echo", [](const tos::span<std::string>&, std::ostream&) {}).code(),
        tos::StatusCode::kAlreadyExists);
    EXPECT_EQ(
        debug.RegisterHandler("status", [](const tos::span<std::string>&, std::ostream&) {}).code(),
        tos::StatusCode::kAlreadyExists);
    ASSERT_TRUE(debug.UnregisterHandler("status"));
    ASSERT_TRUE(debug.RegisterHandler(
        "status",
        [](const tos::span<std::string>&, std::ostream& output) { output << "replacement"; }));
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("status", output));
    EXPECT_EQ(output.str(), "replacement");

    ASSERT_TRUE(debug.UnregisterHandler("log-level"));
    ASSERT_TRUE(debug.RegisterHandler(
        "log-level",
        [](const tos::span<std::string>&, std::ostream& output) { output << "replacement"; }));
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("log-level trace", output));
    EXPECT_EQ(output.str(), "replacement");

    EXPECT_EQ(debug.UnregisterHandler("missing").code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(debug.RegisterHandler("", [](const tos::span<std::string>&, std::ostream&) {}).code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        debug.RegisterHandler("two words", [](const tos::span<std::string>&, std::ostream&) {})
            .code(),
        tos::StatusCode::kInvalidArgument);
    const std::string nul_name("nul\0name", 8);
    EXPECT_EQ(
        debug.RegisterHandler(nul_name, [](const tos::span<std::string>&, std::ostream&) {}).code(),
        tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug.RegisterHandler("empty", tos::DebugHandler{}).code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug.UnregisterHandler("two words").code(), tos::StatusCode::kInvalidArgument);

    ASSERT_TRUE(debug.UnregisterHandler("echo"));
    EXPECT_EQ(debug.Execute("echo", output).code(), tos::StatusCode::kNotFound);
}

TEST(DebugControllerTest, WritesCustomHandlerResultsAndPropagatesExceptions) {
    tos::DebugController debug;
    std::ostringstream output;
    EXPECT_EQ(debug.Execute("status", output).code(), tos::StatusCode::kNotFound);

    ASSERT_TRUE(
        debug.RegisterHandler("fail", [](const tos::span<std::string>&, std::ostream& output) {
            output << "error=UNAVAILABLE: try later\n";
        }));
    ASSERT_TRUE(debug.Execute("fail", output));
    EXPECT_EQ(output.str(), "error=UNAVAILABLE: try later\n");

    ASSERT_TRUE(debug.RegisterHandler("throw", [](const tos::span<std::string>&, std::ostream&) {
        throw std::runtime_error("handler failure");
    }));
    EXPECT_THROW(static_cast<void>(debug.Execute("throw", output)), std::runtime_error);
}

TEST(DebugControllerTest, RejectsFailedOutputStreamsBeforeAndAfterHandlerExecution) {
    tos::DebugController debug;
    std::atomic<bool> invoked{false};
    ASSERT_TRUE(debug.RegisterHandler(
        "write", [&invoked](const tos::span<std::string>&, std::ostream& output) {
            invoked.store(true);
            output << "written";
        }));

    std::ostream unavailable(nullptr);
    EXPECT_EQ(debug.Execute("write", unavailable).code(), tos::StatusCode::kUnavailable);
    EXPECT_FALSE(invoked.load());

    ASSERT_TRUE(debug.RegisterHandler("fail-stream",
                                      [](const tos::span<std::string>&, std::ostream& output) {
                                          output.setstate(std::ios_base::badbit);
                                      }));
    std::ostringstream output;
    EXPECT_EQ(debug.Execute("fail-stream", output).code(), tos::StatusCode::kUnavailable);
}

TEST(DebugControllerTest, UnregisterDoesNotWaitForInFlightHandlerAndAllowsReplacement) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::promise<void> entered;
    std::future<void> entered_future = entered.get_future();
    std::promise<void> release;
    std::future<void> release_future = release.get_future();
    std::atomic<bool> completed{false};
    std::string first_result;

    ASSERT_TRUE(debug.RegisterHandler(
        "block", [&entered, &release_future](const tos::span<std::string>&, std::ostream& output) {
            entered.set_value();
            release_future.wait();
            output << "first";
        }));
    std::thread caller([&] {
        std::ostringstream output;
        if (debug.Execute("block", output)) {
            first_result = output.str();
            completed.store(true);
        }
    });
    ASSERT_EQ(entered_future.wait_for(1s), std::future_status::ready);

    ASSERT_TRUE(debug.UnregisterHandler("block"));
    std::ostringstream output;
    EXPECT_EQ(debug.Execute("block", output).code(), tos::StatusCode::kNotFound);
    ASSERT_TRUE(debug.RegisterHandler(
        "block",
        [](const tos::span<std::string>&, std::ostream& output) { output << "replacement"; }));
    ASSERT_TRUE(debug.Execute("block", output));
    EXPECT_EQ(output.str(), "replacement");

    release.set_value();
    caller.join();
    EXPECT_TRUE(completed.load());
    EXPECT_EQ(first_result, "first");
}

TEST(DebugControllerTest, SupportsConcurrentExecutionRegistrationAndRemoval) {
    tos::App app(QuietOptions());
    tos::DebugController& debug = app.debug();
    std::atomic<bool> succeeded{true};
    constexpr int kThreadCount = 8;
    constexpr int kOperationsPerThread = 50;
    std::vector<std::thread> callers;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        callers.emplace_back([&debug, &succeeded, operations_per_thread = kOperationsPerThread,
                              thread] {
            const std::string command = "custom-" + std::to_string(thread);
            for (int index = 0; index < operations_per_thread; ++index) {
                std::ostringstream status_output;
                if (!debug.Execute("status", status_output) ||
                    !debug.RegisterHandler(command, [](const tos::span<std::string>&,
                                                       std::ostream& output) { output << "ok"; })) {
                    succeeded.store(false);
                    return;
                }
                std::ostringstream custom_output;
                if (!debug.Execute(command, custom_output) || custom_output.str() != "ok" ||
                    !debug.UnregisterHandler(command)) {
                    succeeded.store(false);
                    return;
                }
            }
        });
    }
    for (std::thread& caller : callers) {
        caller.join();
    }
    EXPECT_TRUE(succeeded.load());
}

}  // namespace
