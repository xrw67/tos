#include "tos/app/debug.h"

#include <atomic>
#include <chrono>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

TEST(DebugControllerTest, BuiltInHelpListsDescriptionsForCurrentCommands) {
    tos::DebugController debug;
    std::ostringstream output;

    ASSERT_TRUE(debug.Execute("help", output));
    EXPECT_EQ(output.str(), "help: List registered debug commands.\n");

    ASSERT_TRUE(debug.RegisterHandler(
        "echo", "Print the first argument.",
        [](const tos::span<std::string>& args, std::ostream& command_output) {
            command_output << (args.empty() ? std::string("empty") : args.front());
        }));
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("help", output));
    EXPECT_EQ(output.str(),
              "echo: Print the first argument.\n"
              "help: List registered debug commands.\n");

    ASSERT_TRUE(debug.UnregisterHandler("echo"));
    output.str("");
    output.clear();
    ASSERT_TRUE(debug.Execute("help", output));
    EXPECT_EQ(output.str(), "help: List registered debug commands.\n");
}

TEST(DebugControllerTest, RejectsMalformedAndUnknownCommands) {
    tos::DebugController debug;
    std::ostringstream output;

    EXPECT_EQ(debug.Execute("", output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(debug.Execute(" \t\n", output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(debug.Execute("missing", output).code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(output.str(), "");

    ASSERT_TRUE(debug.Execute("help extra", output));
    EXPECT_EQ(output.str(),
              "error=INVALID_ARGUMENT: invalid debug command: help does not accept arguments\n");

    const std::string with_nul("status\0tail", 11);
    output.str("");
    output.clear();
    EXPECT_EQ(debug.Execute(with_nul, output).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(output.str(), "");
}

TEST(DebugControllerTest, RegistersCustomHandlersAndAllowsReplacement) {
    tos::DebugController debug;
    std::vector<std::string> observed;
    std::ostringstream output;

    ASSERT_TRUE(debug.RegisterHandler(
        "echo", "Write the received arguments.",
        [&observed](const tos::span<std::string>& args, std::ostream& output) {
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

    EXPECT_EQ(debug
                  .RegisterHandler("echo", "Duplicate command",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kAlreadyExists);
    EXPECT_EQ(debug
                  .RegisterHandler("help", "Replace built-in help.",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kAlreadyExists);

    EXPECT_EQ(debug.UnregisterHandler("missing").code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(
        debug
            .RegisterHandler("", "Description", [](const tos::span<std::string>&, std::ostream&) {})
            .code(),
        tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug
                  .RegisterHandler("two words", "Description",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kInvalidArgument);
    const std::string nul_name("nul\0name", 8);
    EXPECT_EQ(debug
                  .RegisterHandler(nul_name, "Description",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug.RegisterHandler("empty", "Description", tos::DebugHandler{}).code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug
                  .RegisterHandler("empty-description", "",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug
                  .RegisterHandler("multiline-description", "first\nsecond",
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kInvalidArgument);
    const std::string nul_description("before\0after", 12);
    EXPECT_EQ(debug
                  .RegisterHandler("nul-description", nul_description,
                                   [](const tos::span<std::string>&, std::ostream&) {})
                  .code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(debug.UnregisterHandler("help").code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_EQ(debug.UnregisterHandler("two words").code(), tos::StatusCode::kInvalidArgument);

    ASSERT_TRUE(debug.UnregisterHandler("echo"));
    EXPECT_EQ(debug.Execute("echo", output).code(), tos::StatusCode::kNotFound);
}

TEST(DebugControllerTest, WritesCustomHandlerResultsAndPropagatesExceptions) {
    tos::DebugController debug;
    std::ostringstream output;
    EXPECT_EQ(debug.Execute("status", output).code(), tos::StatusCode::kNotFound);

    ASSERT_TRUE(debug.RegisterHandler("fail", "Write a failure result.",
                                      [](const tos::span<std::string>&, std::ostream& output) {
                                          output << "error=UNAVAILABLE: try later\n";
                                      }));
    ASSERT_TRUE(debug.Execute("fail", output));
    EXPECT_EQ(output.str(), "error=UNAVAILABLE: try later\n");

    ASSERT_TRUE(debug.RegisterHandler("throw", "Throw an exception.",
                                      [](const tos::span<std::string>&, std::ostream&) {
                                          throw std::runtime_error("handler failure");
                                      }));
    EXPECT_THROW(static_cast<void>(debug.Execute("throw", output)), std::runtime_error);
}

TEST(DebugControllerTest, RejectsFailedOutputStreamsBeforeAndAfterHandlerExecution) {
    tos::DebugController debug;
    std::atomic<bool> invoked{false};
    ASSERT_TRUE(debug.RegisterHandler(
        "write", "Write output.", [&invoked](const tos::span<std::string>&, std::ostream& output) {
            invoked.store(true);
            output << "written";
        }));

    std::ostream unavailable(nullptr);
    EXPECT_EQ(debug.Execute("write", unavailable).code(), tos::StatusCode::kUnavailable);
    EXPECT_FALSE(invoked.load());

    ASSERT_TRUE(debug.RegisterHandler("fail-stream", "Fail the output stream.",
                                      [](const tos::span<std::string>&, std::ostream& output) {
                                          output.setstate(std::ios_base::badbit);
                                      }));
    std::ostringstream output;
    EXPECT_EQ(debug.Execute("fail-stream", output).code(), tos::StatusCode::kUnavailable);
}

TEST(DebugControllerTest, UnregisterDoesNotWaitForInFlightHandlerAndAllowsReplacement) {
    tos::DebugController debug;
    std::promise<void> entered;
    std::future<void> entered_future = entered.get_future();
    std::promise<void> release;
    std::future<void> release_future = release.get_future();
    std::atomic<bool> completed{false};
    std::string first_result;

    ASSERT_TRUE(debug.RegisterHandler(
        "block", "Block until released.",
        [&entered, &release_future](const tos::span<std::string>&, std::ostream& output) {
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
        "block", "Write the replacement result.",
        [](const tos::span<std::string>&, std::ostream& output) { output << "replacement"; }));
    ASSERT_TRUE(debug.Execute("block", output));
    EXPECT_EQ(output.str(), "replacement");

    release.set_value();
    caller.join();
    EXPECT_TRUE(completed.load());
    EXPECT_EQ(first_result, "first");
}

TEST(DebugControllerTest, SupportsConcurrentExecutionRegistrationAndRemoval) {
    tos::DebugController debug;
    ASSERT_TRUE(debug.RegisterHandler(
        "ready", "Write a ready result.",
        [](const tos::span<std::string>&, std::ostream& output) { output << "ready"; }));
    std::atomic<bool> succeeded{true};
    constexpr int kThreadCount = 8;
    constexpr int kOperationsPerThread = 50;
    std::vector<std::thread> callers;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        callers.emplace_back(
            [&debug, &succeeded, operations_per_thread = kOperationsPerThread, thread] {
                const std::string command = "custom-" + std::to_string(thread);
                for (int index = 0; index < operations_per_thread; ++index) {
                    std::ostringstream status_output;
                    if (!debug.Execute("ready", status_output) || status_output.str() != "ready" ||
                        !debug.RegisterHandler(command, "Write a success result.",
                                               [](const tos::span<std::string>&,
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
