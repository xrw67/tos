#include "tos/base/dynamic_library.h"

#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace tos {
namespace {

Path TestModulePath() { return std::move(Path::Parse(TOS_DYNAMIC_LIBRARY_TEST_MODULE)).value(); }

DynamicLibrary LoadTestModule() {
    auto library = DynamicLibrary::Load(TestModulePath());
    EXPECT_TRUE(library) << library.status().ToString();
    return std::move(library).value();
}

TEST(DynamicLibraryTest, IsMoveOnly) {
    static_assert(!std::is_copy_constructible_v<DynamicLibrary>);
    static_assert(!std::is_copy_assignable_v<DynamicLibrary>);
    static_assert(std::is_move_constructible_v<DynamicLibrary>);
    static_assert(std::is_move_assignable_v<DynamicLibrary>);
}

TEST(DynamicLibraryTest, RejectsInvalidPathsAndReportsMissingLibraries) {
    const Path empty = std::move(Path::Parse("")).value();
    auto empty_library = DynamicLibrary::Load(empty);
    ASSERT_FALSE(empty_library);
    EXPECT_EQ(empty_library.status().code(), StatusCode::kInvalidArgument);

    const Path relative = std::move(Path::Parse("test-module")).value();
    auto relative_library = DynamicLibrary::Load(relative);
    ASSERT_FALSE(relative_library);
    EXPECT_EQ(relative_library.status().code(), StatusCode::kInvalidArgument);

    const auto missing_path = Path::Parse(TestModulePath().utf8() + ".missing");
    ASSERT_TRUE(missing_path) << missing_path.status().ToString();
    auto missing_library = DynamicLibrary::Load(missing_path.value());
    EXPECT_FALSE(missing_library);
}

TEST(DynamicLibraryTest, ResolvesFunctionAndDataSymbols) {
    DynamicLibrary library = LoadTestModule();
    ASSERT_TRUE(library.loaded());

    auto increment = library.GetSymbol<int (*)(int)>("TosDynamicLibraryIncrement");
    ASSERT_TRUE(increment) << increment.status().ToString();
    EXPECT_EQ(increment.value()(41), 42);

    auto value = library.GetSymbol<int*>("tos_dynamic_library_test_value");
    ASSERT_TRUE(value) << value.status().ToString();
    EXPECT_EQ(*value.value(), 42);
    *value.value() = 99;
    EXPECT_EQ(*value.value(), 99);

    auto missing = library.GetSymbol<void (*)()>("TosDynamicLibraryMissing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.status().code(), StatusCode::kNotFound);
}

TEST(DynamicLibraryTest, RejectsInvalidNamesAndOperationsAfterUnload) {
    DynamicLibrary library = LoadTestModule();

    auto empty_name = library.GetSymbol<void (*)()>("");
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.status().code(), StatusCode::kInvalidArgument);

    const std::string nul_name("symbol\0suffix", 13);
    auto nul_symbol = library.GetSymbol<void (*)()>(nul_name);
    ASSERT_FALSE(nul_symbol);
    EXPECT_EQ(nul_symbol.status().code(), StatusCode::kInvalidArgument);

    EXPECT_TRUE(library.Unload());
    EXPECT_FALSE(library.loaded());
    EXPECT_TRUE(library.Unload());

    auto unloaded = library.GetSymbol<void (*)()>("TosDynamicLibraryIncrement");
    ASSERT_FALSE(unloaded);
    EXPECT_EQ(unloaded.status().code(), StatusCode::kFailedPrecondition);
}

TEST(DynamicLibraryTest, TransfersOwnershipOnMove) {
    DynamicLibrary original = LoadTestModule();
    DynamicLibrary moved(std::move(original));
    EXPECT_FALSE(original.loaded());
    EXPECT_TRUE(moved.loaded());

    auto increment = moved.GetSymbol<int (*)(int)>("TosDynamicLibraryIncrement");
    ASSERT_TRUE(increment) << increment.status().ToString();
    EXPECT_EQ(increment.value()(9), 10);

    DynamicLibrary assigned = LoadTestModule();
    assigned = std::move(moved);
    EXPECT_FALSE(moved.loaded());
    EXPECT_TRUE(assigned.loaded());
    EXPECT_TRUE(assigned.Unload());
}

TEST(DynamicLibraryTest, DetachRelinquishesTheHandle) {
    DynamicLibrary library = LoadTestModule();
    ASSERT_TRUE(library.loaded());

    library.Detach();
    EXPECT_FALSE(library.loaded());
    EXPECT_TRUE(library.Unload());

    auto symbol = library.GetSymbol<void (*)()>("TosDynamicLibraryIncrement");
    ASSERT_FALSE(symbol);
    EXPECT_EQ(symbol.status().code(), StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace tos
