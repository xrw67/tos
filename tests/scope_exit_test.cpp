#include "tos/base/scope_exit.h"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "tos/base/process.h"

namespace tos {
namespace {
struct Callback {
    int* count;
    void operator()() { ++*count; }
};
static_assert(!std::is_copy_constructible_v<ScopeExit<Callback>>);
static_assert(!std::is_copy_assignable_v<ScopeExit<Callback>>);
static_assert(std::is_nothrow_move_constructible_v<ScopeExit<Callback>>);
static_assert(!std::is_move_assignable_v<ScopeExit<Callback>>);
static_assert(std::is_nothrow_destructible_v<ScopeExit<Callback>>);

TEST(ScopeExitTest, NormalExitEarlyReturnAndUnwindingRunOnce) {
    int count = 0;
    { ScopeExit guard(Callback{&count}); }
    EXPECT_EQ(count, 1);
    auto early_return = [&] {
        auto guard = MakeScopeExit([&] { ++count; });
        return;
    };
    early_return();
    EXPECT_EQ(count, 2);
    EXPECT_THROW(
        {
            auto guard = MakeScopeExit([&] { ++count; });
            throw std::runtime_error("unwind");
        },
        std::runtime_error);
    EXPECT_EQ(count, 3);
}

TEST(ScopeExitTest, ReleaseIsIdempotentAndMoveTransfersOnlyActiveCleanup) {
    int count = 0;
    {
        ScopeExit first(Callback{&count});
        ScopeExit second(std::move(first));
        ScopeExit third(std::move(second));
    }
    EXPECT_EQ(count, 1);
    {
        auto guard = MakeScopeExit(Callback{&count});
        guard.Release();
        guard.Release();
        auto moved = std::move(guard);
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeExitTest, OwnsMoveOnlyCallbacksAndCopiesLvalueCallbacks) {
    int count = 0;
    {
        auto guard = MakeScopeExit([owned = std::make_unique<int>(7), &count] { count += *owned; });
        auto moved = std::move(guard);
    }
    EXPECT_EQ(count, 7);
    Callback callback{&count};
    { auto guard = MakeScopeExit(callback); }
    EXPECT_EQ(count, 8);
}

struct ThrowingCopy {
    ThrowingCopy() = default;
    ThrowingCopy(const ThrowingCopy&) { throw std::runtime_error("copy"); }
    ThrowingCopy(ThrowingCopy&&) noexcept = default;
    void operator()() noexcept {}
};
TEST(ScopeExitTest, CallbackCopyExceptionsPropagate) {
    ThrowingCopy callback;
    EXPECT_THROW(static_cast<void>(MakeScopeExit(callback)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(ScopeExit(callback)), std::runtime_error);
}

TEST(ScopeExitTest, ThrowingCleanupTerminatesInIsolatedProcess) {
    auto path = Path::Parse(TOS_RESOURCE_TEST_HELPER);
    ASSERT_TRUE(path);
    auto result = RunCommand(ProcessOptions{std::move(path).value(), {"throw"}, std::nullopt, {}});
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->exit.exit_code);
    EXPECT_EQ(*result->exit.exit_code, 73U);
    EXPECT_EQ(result->stdout_output, "terminate\n");
}
}  // namespace
}  // namespace tos
