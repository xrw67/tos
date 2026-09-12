#include <gtest/gtest.h>

#include "tos/base/json.h"

namespace tos {
namespace {

TEST(NlohmannJsonTest, ParsesAndReadsAnObject) {
    const tos::json document = tos::json::parse(R"({"enabled":true,"retries":3})");

    EXPECT_TRUE(document.at("enabled").get<bool>());
    EXPECT_EQ(document.at("retries").get<int>(), 3);
}

}  // namespace
}  // namespace tos
