#include "tos/base/span.h"

#include <array>
#include <gtest/gtest.h>
#include <type_traits>
#include <vector>

namespace tos {
namespace {

TEST(SpanTest, ViewsArrayWithoutTakingOwnership) {
    int values[] = {3, 5, 7};
    span<int> view(values);

    static_assert(std::is_same_v<decltype(view.data()), int*>);
    EXPECT_EQ(view.size(), 3U);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.front(), 3);
    EXPECT_EQ(view.back(), 7);

    view[1] = 11;
    EXPECT_EQ(values[1], 11);
}

TEST(SpanTest, SupportsStaticAndDynamicExtents) {
    std::array<int, 4> values = {2, 4, 6, 8};
    span<int, 4> fixed(values);
    const auto middle = fixed.subspan(1, 2);

    static_assert(decltype(fixed)::extent == 4);
    static_assert(decltype(middle)::extent == dynamic_extent);
    EXPECT_EQ(middle.size(), 2U);
    EXPECT_EQ(middle[0], 4);
    EXPECT_EQ(middle[1], 6);
}

TEST(SpanTest, AcceptsMutableAndConstContainers) {
    std::vector<int> mutable_values = {1, 2, 3};
    const std::vector<int> const_values = {4, 5};

    span<int> mutable_view(mutable_values);
    span<const int> const_view(const_values);

    mutable_view[0] = 9;
    EXPECT_EQ(mutable_values[0], 9);
    EXPECT_EQ(const_view.size(), 2U);
    EXPECT_EQ(const_view[0], 4);
}

}  // namespace
}  // namespace tos
