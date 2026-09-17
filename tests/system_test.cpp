#include "tos/base/system.h"

#include <chrono>
#include <string_view>

#include <gtest/gtest.h>

namespace tos {
namespace {

TEST(SystemTest, ReportsTheBuildTargetAndCurrentIdentifiers) {
#ifdef _WIN32
    EXPECT_EQ(System::OperatingSystem(), "windows");
#elif defined(__APPLE__)
    EXPECT_EQ(System::OperatingSystem(), "macos");
#elif defined(__linux__)
    EXPECT_EQ(System::OperatingSystem(), "linux");
#else
    EXPECT_EQ(System::OperatingSystem(), "unknown");
#endif
    EXPECT_NE(System::CurrentProcessId(), 0U);
    EXPECT_NE(System::CurrentThreadId(), 0U);
}

TEST(SystemTest, ReportsTheBuildTargetArchitecture) {
#if defined(_M_IX86) || defined(__i386__)
    EXPECT_EQ(System::Architecture(), "x86");
#elif defined(_M_X64) || defined(__x86_64__)
    EXPECT_EQ(System::Architecture(), "x64");
#elif defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
    EXPECT_EQ(System::Architecture(), "arm64");
#else
    EXPECT_EQ(System::Architecture(), "unknown");
#endif
}

TEST(SystemTest, ReportsCurrentHostName) {
    auto host_name = System::GetHostName();
    ASSERT_TRUE(host_name) << host_name.status().ToString();
    EXPECT_FALSE(host_name->empty());
}

TEST(SystemTest, ReportsExecutablePathAndDirectory) {
    auto executable = System::CurrentProcessPath();
    ASSERT_TRUE(executable) << executable.status().ToString();
    EXPECT_TRUE(executable->is_absolute());
    EXPECT_FALSE(executable->empty());

    auto directory = System::CurrentProcessDirectory();
    ASSERT_TRUE(directory) << directory.status().ToString();
    EXPECT_TRUE(directory->is_absolute());
    EXPECT_EQ(directory.value(), executable->parent_path());
}

TEST(SystemTest, ReportsCurrentWorkingDirectory) {
    auto directory = System::CurrentWorkingDirectory();
    ASSERT_TRUE(directory) << directory.status().ToString();
    EXPECT_TRUE(directory->is_absolute());
    EXPECT_FALSE(directory->empty());
}

TEST(SystemTest, ReportsAtLeastOneAvailableCpu) { EXPECT_GE(System::NumberOfProcessors(), 1U); }

TEST(SystemTest, SleepsForRequestedMilliseconds) {
    const auto started = std::chrono::steady_clock::now();
    System::Sleep(1);
    EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(1));
    System::Sleep(0);
}

}  // namespace
}  // namespace tos
