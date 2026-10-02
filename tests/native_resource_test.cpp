#include "native_resource.h"

#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <utility>

#include "tos/base/process.h"

#ifndef _WIN32
#include <fcntl.h>
#endif

namespace tos {
namespace {
#ifdef _WIN32
using Resource = detail::UniqueHandle;
Resource OpenResource() { return Resource(CreateEventW(nullptr, TRUE, FALSE, nullptr)); }
bool IsOpen(HANDLE value) {
    DWORD flags = 0;
    return GetHandleInformation(value, &flags) != FALSE;
}
#else
using Resource = detail::UniqueFd;
Resource OpenResource() { return Resource(open("/dev/null", O_RDONLY | O_CLOEXEC)); }
bool IsOpen(int value) { return fcntl(value, F_GETFD) != -1; }
#endif
static_assert(!std::is_copy_constructible_v<Resource>);
static_assert(!std::is_copy_assignable_v<Resource>);
static_assert(std::is_nothrow_move_constructible_v<Resource>);
static_assert(std::is_nothrow_move_assignable_v<Resource>);
static_assert(std::is_nothrow_destructible_v<Resource>);

TEST(NativeResourceTest, EmptyAndExplicitCloseAreIdempotent) {
    Resource empty;
    EXPECT_FALSE(empty);
    EXPECT_EQ(empty.Close(), 0U);
#ifdef _WIN32
    Resource invalid(INVALID_HANDLE_VALUE);
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.Close(), ERROR_SUCCESS);
#endif
    Resource owned = OpenResource();
    ASSERT_TRUE(owned);
    const auto value = owned.Get();
    EXPECT_TRUE(IsOpen(value));
    EXPECT_EQ(owned.Close(), 0U);
    EXPECT_FALSE(owned);
    EXPECT_FALSE(IsOpen(value));
    EXPECT_EQ(owned.Close(), 0U);
}

TEST(NativeResourceTest, MovesReleaseResetAndDestructionCloseExactlyOnce) {
    Resource first = OpenResource();
    Resource second = OpenResource();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto original = first.Get();
    const auto replaced = second.Get();
    {
        Resource moved(std::move(first));
        EXPECT_FALSE(first);
        second = std::move(moved);
        EXPECT_FALSE(moved);
        EXPECT_FALSE(IsOpen(replaced));
        EXPECT_TRUE(IsOpen(original));
        Resource* self = &second;
        second = std::move(*self);
        second.Reset(second.Get());
        EXPECT_TRUE(IsOpen(original));
        const auto released = second.Release();
        EXPECT_FALSE(second);
        EXPECT_TRUE(IsOpen(released));
        first.Reset(released);
    }
    first.Reset();
    EXPECT_FALSE(IsOpen(original));
    const auto value = [&] {
        auto resource = OpenResource();
        return resource.Get();
    }();
    EXPECT_FALSE(IsOpen(value));
}

TEST(NativeResourceTest, BestEffortCleanupPreservesAmbientError) {
    auto resource = OpenResource();
    ASSERT_TRUE(resource);
#ifdef _WIN32
    SetLastError(ERROR_BAD_ARGUMENTS);
    resource.Reset();
    EXPECT_EQ(GetLastError(), ERROR_BAD_ARGUMENTS);
#else
    errno = EDOM;
    resource.Reset();
    EXPECT_EQ(errno, EDOM);
    // A fabricated, never-owned descriptor exercises error reporting without double closing.
    detail::UniqueFd invalid(std::numeric_limits<int>::max());
    EXPECT_EQ(invalid.Close(), EBADF);
    EXPECT_FALSE(invalid);
#endif
}

TEST(NativeResourceTest, ResetReplacementAndMoveAssignmentPreserveErrors) {
    auto first = OpenResource();
    auto second = OpenResource();
    auto replacement = OpenResource();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(replacement);
    const auto first_value = first.Get();
    const auto second_value = second.Get();
#ifdef _WIN32
    SetLastError(ERROR_BAD_ARGUMENTS);
#else
    errno = EDOM;
#endif
    first.Reset(replacement.Release());
    second = std::move(first);
#ifdef _WIN32
    EXPECT_EQ(GetLastError(), ERROR_BAD_ARGUMENTS);
#else
    EXPECT_EQ(errno, EDOM);
#endif
    EXPECT_FALSE(IsOpen(first_value));
    EXPECT_FALSE(IsOpen(second_value));
    const auto value = second.Get();
    {
        Resource moved(std::move(second));
#ifdef _WIN32
        SetLastError(ERROR_BAD_ARGUMENTS);
#else
        errno = EDOM;
#endif
    }
#ifdef _WIN32
    EXPECT_EQ(GetLastError(), ERROR_BAD_ARGUMENTS);
#else
    EXPECT_EQ(errno, EDOM);
#endif
    EXPECT_FALSE(IsOpen(value));
}

#ifndef _WIN32
TEST(NativeResourceTest, DescriptorZeroIsOwnedInIsolatedProcess) {
    auto path = Path::Parse(TOS_RESOURCE_TEST_HELPER);
    ASSERT_TRUE(path);
    auto result = RunCommand(ProcessOptions{std::move(path).value(), {"zero"}, std::nullopt, {}});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->exit.exit_code, 0U);
}
#endif

}  // namespace
}  // namespace tos
