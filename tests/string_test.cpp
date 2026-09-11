#include "tos/string.h"

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

namespace tos {
namespace {

TEST(StringTest, TrimsOnlyFixedAsciiWhitespace) {
    EXPECT_EQ(StrTrimLeft(" \t\r\nvalue\f\v"), "value\f\v");
    EXPECT_EQ(StrTrimRight(" \t\r\nvalue\f\v"), " \t\r\nvalue");
    EXPECT_EQ(StrTrim(" \t\r\nvalue\f\v"), "value");
    EXPECT_EQ(StrTrim("\xc2\xa0value\xc2\xa0"), "\xc2\xa0value\xc2\xa0");
    EXPECT_EQ(StrTrim(""), "");
}

TEST(StringTest, ChangesOnlyAsciiCaseAndPreservesEmbeddedNullBytes) {
    const std::string input = std::string("\xc3\x84") + "Ab" + std::string("\xc3\x9f");
    EXPECT_EQ(StrToLower(input), std::string("\xc3\x84") + "ab" + std::string("\xc3\x9f"));
    EXPECT_EQ(StrToUpper(input), std::string("\xc3\x84") + "AB" + std::string("\xc3\x9f"));

    const std::string null_text("A\0b", 3);
    EXPECT_EQ(StrToLower(null_text), std::string("a\0b", 3));
    EXPECT_EQ(StrToUpper(null_text), std::string("A\0B", 3));
}

TEST(StringTest, ChecksPrefixesSuffixesAndContainsByExactBytes) {
    const std::string text("ab\0cd", 5);
    EXPECT_TRUE(StrStartsWith(text, std::string_view("ab\0", 3)));
    EXPECT_TRUE(StrEndsWith(text, "cd"));
    EXPECT_TRUE(StrContains(text, std::string_view("\0c", 2)));
    EXPECT_FALSE(StrStartsWith(text, "Ab"));
    EXPECT_FALSE(StrEndsWith(text, "CD"));
    EXPECT_FALSE(StrContains(text, "missing"));
}

TEST(StringTest, ContainsIgnoreCaseFoldsOnlyAsciiBytes) {
    EXPECT_TRUE(StrContainsIgnoreCase("PrefixVALUEsuffix", "value"));
    EXPECT_TRUE(StrContainsIgnoreCase("PrefixVALUEsuffix", "VaLuE"));
    EXPECT_TRUE(StrContainsIgnoreCase(std::string_view("a\0Bc", 4), std::string_view("\0b", 2)));
    EXPECT_TRUE(StrContainsIgnoreCase("value", ""));
    EXPECT_FALSE(StrContainsIgnoreCase("value", "values"));
    EXPECT_FALSE(StrContainsIgnoreCase("\xc3\x84", "\xc3\xa4"));
}

TEST(StringTest, SplitsTextDelimitersAndPreservesEmptyFields) {
    EXPECT_EQ(StrSplit("a,,b,", ","), (std::vector<std::string>{"a", "", "b", ""}));
    EXPECT_EQ(StrSplit("::a::::b::", "::"), (std::vector<std::string>{"", "a", "", "b", ""}));
    EXPECT_EQ(StrSplit("", ","), (std::vector<std::string>{""}));
    EXPECT_EQ(StrSplit("value", ""), (std::vector<std::string>{"value"}));
}

TEST(StringTest, JoinsOwningBorrowedAndInitializerListValues) {
    const std::vector<std::string> owning = {"one", "two", "three"};
    const std::vector<std::string_view> borrowed = {"one", "two", "three"};
    EXPECT_EQ(StrJoin(owning, ","), "one,two,three");
    EXPECT_EQ(StrJoin(borrowed, "::"), "one::two::three");
    EXPECT_EQ(StrJoin({"one", "two", "three"}, "/"), "one/two/three");
    EXPECT_EQ(StrJoin({}, ","), "");
    EXPECT_EQ(
        StrJoin(std::vector<std::string_view>{std::string_view{}, "value", std::string_view{}},
                ","),
        ",value,");
}

TEST(StringTest, ReplacesNonOverlappingMatchesAndHandlesEmptyPatterns) {
    EXPECT_EQ(StrReplaceAll("one two one", "one", "1"), "1 two 1");
    EXPECT_EQ(StrReplaceAll("aaaa", "aa", "b"), "bb");
    EXPECT_EQ(StrReplaceAll("value", "", "-"), "value");
    EXPECT_EQ(StrReplaceAll(std::string_view{}, "a", "b"), "");
    EXPECT_EQ(StrReplaceAll(std::string_view("a\0a", 3), "a", "b"), std::string("b\0b", 3));
}

}  // namespace
}  // namespace tos
