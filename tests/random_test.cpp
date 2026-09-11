#include "tos/random.h"

#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace tos {
namespace {

bool ContainsByte(std::string_view alphabet, char byte) {
    return alphabet.find(byte) != std::string_view::npos;
}

TEST(RandomTest, ExposesStandardAsciiAlphabets) {
    EXPECT_EQ(kRandomDigits, "0123456789");
    EXPECT_EQ(kRandomLowercaseLetters, "abcdefghijklmnopqrstuvwxyz");
    EXPECT_EQ(kRandomUppercaseLetters, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    EXPECT_EQ(kRandomLetters, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    EXPECT_EQ(kRandomAlphaNumeric,
              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
}

TEST(RandomTest, GeneratesDefaultAndCustomAlphabetStrings) {
    auto default_value = RandomString(256);
    ASSERT_TRUE(default_value);
    EXPECT_EQ(default_value->size(), 256U);
    for (const char byte : default_value.value()) {
        EXPECT_TRUE(ContainsByte(kRandomAlphaNumeric, byte));
    }

    constexpr std::string_view alphabet = "ab-";
    auto custom_value = RandomString(256, alphabet);
    ASSERT_TRUE(custom_value);
    EXPECT_EQ(custom_value->size(), 256U);
    for (const char byte : custom_value.value()) {
        EXPECT_TRUE(ContainsByte(alphabet, byte));
    }
}

TEST(RandomTest, HandlesEmptyAndByteOrientedAlphabets) {
    auto empty_value = RandomString(0, {});
    ASSERT_TRUE(empty_value);
    EXPECT_TRUE(empty_value->empty());

    const Result<std::string> missing_alphabet = RandomString(1, {});
    EXPECT_FALSE(missing_alphabet);
    EXPECT_EQ(missing_alphabet.status().code(), StatusCode::kInvalidArgument);

    auto single_byte = RandomString(64, "x");
    ASSERT_TRUE(single_byte);
    EXPECT_EQ(single_byte.value(), std::string(64, 'x'));

    constexpr char kNullAlphabet[] = {'\0'};
    auto null_bytes = RandomString(64, std::string_view(kNullAlphabet, sizeof(kNullAlphabet)));
    ASSERT_TRUE(null_bytes);
    EXPECT_EQ(null_bytes.value(), std::string(64, '\0'));

    constexpr char kEmbeddedNullAlphabet[] = {'a', '\0', 'b'};
    auto embedded_null_bytes =
        RandomString(256, std::string_view(kEmbeddedNullAlphabet, sizeof(kEmbeddedNullAlphabet)));
    ASSERT_TRUE(embedded_null_bytes);
    for (const char byte : embedded_null_bytes.value()) {
        EXPECT_TRUE(ContainsByte(
            std::string_view(kEmbeddedNullAlphabet, sizeof(kEmbeddedNullAlphabet)), byte));
    }

    constexpr char kRepeatedAlphabet[] = {'x', 'x'};
    auto repeated_bytes =
        RandomString(64, std::string_view(kRepeatedAlphabet, sizeof(kRepeatedAlphabet)));
    ASSERT_TRUE(repeated_bytes);
    EXPECT_EQ(repeated_bytes.value(), std::string(64, 'x'));
}

TEST(RandomTest, GeneratesConcurrentlyWithoutCrossThreadState) {
    constexpr std::size_t kThreadCount = 8;
    constexpr std::size_t kRecordsPerThread = 100;
    constexpr std::size_t kLength = 64;
    constexpr std::string_view kAlphabet = "abc";
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (std::size_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        threads.emplace_back([&] {
            for (std::size_t record_index = 0; record_index < kRecordsPerThread; ++record_index) {
                auto value = RandomString(kLength, kAlphabet);
                if (!value || value->size() != kLength) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                for (const char byte : value.value()) {
                    if (!ContainsByte(kAlphabet, byte)) {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_FALSE(failed.load(std::memory_order_relaxed));
}

}  // namespace
}  // namespace tos
