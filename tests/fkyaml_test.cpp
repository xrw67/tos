#include <gtest/gtest.h>

#include "tos/base/yaml.h"

namespace tos {
namespace {

TEST(FkYamlTest, ParsesAndReadsAMapping) {
    const auto document = fkyaml::node::deserialize("enabled: true\nretries: 3\n");

    EXPECT_TRUE(document.at("enabled").get_value<bool>());
    EXPECT_EQ(document.at("retries").get_value<int>(), 3);
}

}  // namespace
}  // namespace tos
