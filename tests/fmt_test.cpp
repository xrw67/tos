#include <gtest/gtest.h>

#include "tos/format.h"

namespace tos {
namespace {

TEST(FmtTest, FormatsValuesThroughPublicHeader) {
    EXPECT_EQ(tos::format("{} + {} = {}", 2, 3, 5), "2 + 3 = 5");
}

}  // namespace
}  // namespace tos
