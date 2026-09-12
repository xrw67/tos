#include "tos/base/registry.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tos {
namespace {

#ifdef _WIN32

class RegistryTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = "Software\\tos-registry-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
                std::to_string(next_id_++);
        CleanUp();
    }

    void TearDown() override { CleanUp(); }

    std::string Child(std::string_view child) const { return root_ + "\\" + std::string(child); }

    std::string root_;
    static std::uint64_t next_id_;

   private:
    void CleanUp() {
        for (const RegistryView view :
             {RegistryView::kNative, RegistryView::k32Bit, RegistryView::k64Bit}) {
            const Status cleanup =
                RegistryKey::DeleteKey(RegistryHive::kCurrentUser, root_, true, view);
            EXPECT_TRUE(cleanup || cleanup.code() == StatusCode::kNotFound);
        }
    }
};

std::uint64_t RegistryTest::next_id_ = 0;

TEST_F(RegistryTest, CreatesAndReadsAllSupportedValueTypes) {
    auto key = RegistryKey::Create(RegistryHive::kCurrentUser, Child("你好"));
    ASSERT_TRUE(key);
    ASSERT_TRUE(key->SetValue("text", std::string("hello 你好")));
    ASSERT_TRUE(key->SetValue("expand", RegistryExpandString{"%USERPROFILE%\\raw"}));
    ASSERT_TRUE(key->SetValue("dword", std::uint32_t(42)));
    ASSERT_TRUE(key->SetValue("qword", std::uint64_t(1) << 40U));
    ASSERT_TRUE(key->SetValue("binary", std::vector<std::uint8_t>{0, 1, 255}));
    ASSERT_TRUE(key->SetValue("multi", std::vector<std::string>{"first", "你好", "third"}));
    ASSERT_TRUE(key->SetValue("", std::string("default")));

    auto text = key->GetValue("text");
    ASSERT_TRUE(text);
    EXPECT_EQ(std::get<std::string>(text.value()), "hello 你好");
    auto expand = key->GetValue("expand");
    ASSERT_TRUE(expand);
    EXPECT_EQ(std::get<RegistryExpandString>(expand.value()).value, "%USERPROFILE%\\raw");
    auto dword = key->GetValue("dword");
    ASSERT_TRUE(dword);
    EXPECT_EQ(std::get<std::uint32_t>(dword.value()), 42U);
    auto qword = key->GetValue("qword");
    ASSERT_TRUE(qword);
    EXPECT_EQ(std::get<std::uint64_t>(qword.value()), std::uint64_t(1) << 40U);
    auto binary = key->GetValue("binary");
    ASSERT_TRUE(binary);
    EXPECT_EQ(std::get<std::vector<std::uint8_t>>(binary.value()),
              (std::vector<std::uint8_t>{0, 1, 255}));
    auto multi = key->GetValue("multi");
    ASSERT_TRUE(multi);
    EXPECT_EQ(std::get<std::vector<std::string>>(multi.value()),
              (std::vector<std::string>{"first", "你好", "third"}));
    auto default_value = key->GetValue("");
    ASSERT_TRUE(default_value);
    EXPECT_EQ(std::get<std::string>(default_value.value()), "default");
}

TEST_F(RegistryTest, EnumeratesSortedNamesAndHonorsAccess) {
    auto key = RegistryKey::Create(RegistryHive::kCurrentUser, Child("values"));
    ASSERT_TRUE(key);
    ASSERT_TRUE(key->SetValue("z", std::string("z")));
    ASSERT_TRUE(key->SetValue("a", std::string("a")));
    ASSERT_TRUE(RegistryKey::Create(RegistryHive::kCurrentUser, Child("values\\z")));
    ASSERT_TRUE(RegistryKey::Create(RegistryHive::kCurrentUser, Child("values\\a")));

    auto values = key->ListValueNames();
    ASSERT_TRUE(values);
    EXPECT_EQ(values.value(), (std::vector<std::string>{"a", "z"}));
    auto subkeys = key->ListSubkeys();
    ASSERT_TRUE(subkeys);
    EXPECT_EQ(subkeys.value(), (std::vector<std::string>{"a", "z"}));

    auto read_only = RegistryKey::Open(RegistryHive::kCurrentUser, Child("values"));
    ASSERT_TRUE(read_only);
    const Status write = read_only->SetValue("denied", std::string("value"));
    EXPECT_FALSE(write);
    EXPECT_EQ(write.code(), StatusCode::kPermissionDenied);
}

TEST_F(RegistryTest, ReportsMissingItemsAndDeletesOnlyWhenRequested) {
    auto missing = RegistryKey::Open(RegistryHive::kCurrentUser, Child("missing"));
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.status().code(), StatusCode::kNotFound);

    auto key = RegistryKey::Create(RegistryHive::kCurrentUser, Child("parent"));
    ASSERT_TRUE(key);
    ASSERT_TRUE(
        RegistryKey::Create(RegistryHive::kCurrentUser, Child("parent\\child\\grandchild")));
    const Status nonrecursive = key->DeleteSubkey("child");
    EXPECT_FALSE(nonrecursive);
    EXPECT_EQ(nonrecursive.code(), StatusCode::kFailedPrecondition);
    EXPECT_TRUE(key->DeleteSubkey("child", true));
    const auto no_value = key->GetValue("missing");
    EXPECT_FALSE(no_value);
    EXPECT_EQ(no_value.status().code(), StatusCode::kNotFound);
    EXPECT_TRUE(key->DeleteValue("missing").code() == StatusCode::kNotFound);
}

TEST_F(RegistryTest, AcceptsEachRegistryViewAndMoveTransfersOwnership) {
    for (const RegistryView view :
         {RegistryView::kNative, RegistryView::k32Bit, RegistryView::k64Bit}) {
        auto key =
            RegistryKey::Create(RegistryHive::kCurrentUser,
                                Child("view-" + std::to_string(static_cast<int>(view))), view);
        ASSERT_TRUE(key);
        RegistryKey moved = std::move(key).value();
        EXPECT_TRUE(moved.SetValue("value", std::string("ok")));
        const auto moved_from = key->GetValue("value");
        EXPECT_FALSE(moved_from);
        EXPECT_EQ(moved_from.status().code(), StatusCode::kFailedPrecondition);
    }
}

TEST_F(RegistryTest, ValidatesDangerousAndMalformedInputs) {
    const Status create_root = RegistryKey::DeleteKey(RegistryHive::kCurrentUser, "");
    EXPECT_FALSE(create_root);
    EXPECT_EQ(create_root.code(), StatusCode::kInvalidArgument);
    auto key = RegistryKey::Create(RegistryHive::kCurrentUser, Child("validation"));
    ASSERT_TRUE(key);
    EXPECT_EQ(key->DeleteSubkey("").code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(key->SetValue(std::string_view("bad\0name", 8), std::string("value")).code(),
              StatusCode::kInvalidArgument);
    EXPECT_EQ(key->SetValue("bad", std::string("value\0text", 10)).code(),
              StatusCode::kInvalidArgument);
}

#else

TEST(RegistryTest, IsExplicitlyUnsupportedOutsideWindows) {
    const auto opened = RegistryKey::Open(RegistryHive::kCurrentUser, "Software");
    EXPECT_FALSE(opened);
    EXPECT_EQ(opened.status().code(), StatusCode::kUnimplemented);

    const auto created = RegistryKey::Create(RegistryHive::kCurrentUser, "Software");
    EXPECT_FALSE(created);
    EXPECT_EQ(created.status().code(), StatusCode::kUnimplemented);

    const Status deleted = RegistryKey::DeleteKey(RegistryHive::kCurrentUser, "Software", true);
    EXPECT_FALSE(deleted);
    EXPECT_EQ(deleted.code(), StatusCode::kUnimplemented);
}

#endif

}  // namespace
}  // namespace tos
