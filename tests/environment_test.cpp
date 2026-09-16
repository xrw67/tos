#include "tos/base/environment.h"

#include <atomic>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tos {
namespace {

std::atomic<unsigned int> next_variable_id{0};

std::string TestVariableName() {
    return "TOS_ENVIRONMENT_TEST_" + std::to_string(next_variable_id.fetch_add(1));
}

class EnvironmentRestore {
   public:
    explicit EnvironmentRestore(std::string name) : name_(std::move(name)) {
        auto original = Environment::GetVar(name_);
        if (original) {
            original_ = std::move(original).value();
        } else if (original.status().code() != StatusCode::kNotFound) {
            throw std::runtime_error(original.status().ToString());
        }
    }

    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(const EnvironmentRestore&) = delete;

    ~EnvironmentRestore() {
        if (original_) {
            static_cast<void>(Environment::SetVar(name_, *original_));
        } else {
            static_cast<void>(Environment::UnsetVar(name_));
        }
    }

   private:
    std::string name_;
    std::optional<std::string> original_;
};

TEST(EnvironmentTest, ReadsEmptyValuesDefaultsAndPresence) {
    const std::string name = TestVariableName();
    EnvironmentRestore restore(name);

    ASSERT_TRUE(Environment::SetVar(name, ""));
    auto empty_value = Environment::GetVar(name);
    ASSERT_TRUE(empty_value);
    EXPECT_TRUE(empty_value->empty());
    EXPECT_TRUE(Environment::HasVar(name));
    EXPECT_EQ(Environment::GetVarOr(name, "fallback"), "");

    ASSERT_TRUE(Environment::UnsetVar(name));
    const auto missing = Environment::GetVar(name);
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.status().code(), StatusCode::kNotFound);
    EXPECT_FALSE(Environment::HasVar(name));
    EXPECT_EQ(Environment::GetVarOr(name, "fallback"), "fallback");
    EXPECT_EQ(Environment::GetVarOr("invalid=name", "fallback"), "fallback");
    EXPECT_TRUE(Environment::UnsetVar(name));
}

TEST(EnvironmentTest, ValidatesNamesAndValues) {
    const std::string embedded_nul("name\0suffix", 11);
    EXPECT_EQ(Environment::GetVar("").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(Environment::GetVar("invalid=name").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(Environment::GetVar(embedded_nul).status().code(), StatusCode::kInvalidArgument);
    EXPECT_FALSE(Environment::HasVar(""));
    EXPECT_EQ(Environment::UnsetVar("invalid=name").code(), StatusCode::kInvalidArgument);

    const std::string value_with_nul("value\0suffix", 12);
    EXPECT_EQ(Environment::SetVar("TOS_ENVIRONMENT_TEST_VALUE", value_with_nul).code(),
              StatusCode::kInvalidArgument);
}

TEST(EnvironmentTest, SplitsPathAndPreservesEmptyFields) {
    EnvironmentRestore restore("PATH");
#ifdef _WIN32
    ASSERT_TRUE(Environment::SetVar("PATH", "alpha;;omega;"));
#else
    ASSERT_TRUE(Environment::SetVar("PATH", "alpha::omega:"));
#endif
    auto path = Environment::GetPathVar();
    EXPECT_EQ(path, (std::vector<std::string>{"alpha", "", "omega", ""}));

    ASSERT_TRUE(Environment::UnsetVar("PATH"));
    auto missing_path = Environment::GetPathVar();
    EXPECT_TRUE(missing_path.empty());
}

TEST(EnvironmentTest, UsesNativeNameCaseRules) {
    const std::string upper = TestVariableName() + "_UPPER";
    std::string lower = upper;
    for (char& character : lower) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    EnvironmentRestore restore_upper(upper);
    EnvironmentRestore restore_lower(lower);

    ASSERT_TRUE(Environment::SetVar(upper, "value"));
    auto lower_value = Environment::GetVar(lower);
#ifdef _WIN32
    ASSERT_TRUE(lower_value);
    EXPECT_EQ(lower_value.value(), "value");
#else
    EXPECT_FALSE(lower_value);
    EXPECT_EQ(lower_value.status().code(), StatusCode::kNotFound);
#endif
}

TEST(EnvironmentTest, SerializesCallsMadeThroughItsApi) {
    const std::string name = TestVariableName();
    EnvironmentRestore restore(name);
    ASSERT_TRUE(Environment::SetVar(name, "initial"));

    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int thread_index = 0; thread_index != 4; ++thread_index) {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration != 100; ++iteration) {
                if (!Environment::SetVar(name, iteration % 2 == 0 ? "first" : "second")) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
                auto value = Environment::GetVar(name);
                const bool has_value = Environment::HasVar(name);
                if (!value || (value.value() != "first" && value.value() != "second") ||
                    !has_value) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_FALSE(failed.load(std::memory_order_acquire));
}

}  // namespace
}  // namespace tos
