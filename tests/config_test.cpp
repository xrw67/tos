#include "tos/base/config.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace tos {
namespace {

Config ParseOrFail(std::string_view text, ConfigFormat format, std::string_view source = "test") {
    auto config = Config::Parse(text, format, source);
    EXPECT_TRUE(config) << config.status().ToString();
    return std::move(config).value();
}

LayeredConfig LayersOrFail(std::vector<ConfigLayer> layers) {
    auto layered = LayeredConfig::Create(std::move(layers));
    EXPECT_TRUE(layered) << layered.status().ToString();
    return std::move(layered).value();
}

TEST(ConfigTest, ParsesEquivalentJsonAndYamlAndReadsBasicTypes) {
    const Config json = ParseOrFail(
        R"({"enabled":false,"retries":3,"ratio":1.5,"name":"","servers":[{"port":8080}],"logging.json":{"level":"info"}})",
        ConfigFormat::kJson, "settings.json");
    const Config yaml = ParseOrFail(
        "enabled: false\nretries: 3\nratio: 1.5\nname: ''\nservers:\n  - port: "
        "8080\nlogging.json:\n  level: info\n",
        ConfigFormat::kYaml, "settings.yaml");

    for (const Config* config : {&json, &yaml}) {
        const auto enabled = config->GetBool("enabled");
        ASSERT_TRUE(enabled);
        EXPECT_FALSE(enabled.value());
        EXPECT_EQ(config->GetInt64("retries").value(), 3);
        EXPECT_EQ(config->GetUint64("retries").value(), 3u);
        EXPECT_DOUBLE_EQ(config->GetDouble("ratio").value(), 1.5);
        EXPECT_EQ(config->GetString("name").value(), "");
        EXPECT_EQ(config->GetInt64("servers.0.port").value(), 8080);
        EXPECT_EQ(config->GetString("logging\\.json.level").value(), "info");
    }
}

TEST(ConfigTest, DistinguishesMissingValuesTypesAndInvalidPaths) {
    const Config config =
        ParseOrFail(R"({"enabled":false,"count":0,"name":""})", ConfigFormat::kJson);

    EXPECT_TRUE(config.Has("enabled"));
    EXPECT_FALSE(config.Has("missing"));

    const auto wrong_type = config.GetString("enabled");
    EXPECT_FALSE(wrong_type);
    EXPECT_EQ(wrong_type.status().code(), StatusCode::kInvalidArgument);
    EXPECT_NE(wrong_type.status().message().find("enabled"), std::string::npos);
    const auto missing = config.GetBool("missing");
    EXPECT_EQ(missing.status().code(), StatusCode::kNotFound);
    EXPECT_NE(missing.status().message().find("missing"), std::string::npos);
    EXPECT_EQ(config.GetBool("count").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(config.GetDouble("count").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(config.GetString("name").value(), "");
    EXPECT_FALSE(config.Has("enabled..value"));
    EXPECT_FALSE(config.Has("enabled\\x"));
    EXPECT_FALSE(config.Has("enabled\\"));
}

TEST(ConfigTest, ReportsFormatAndYamlConversionFailures) {
    const auto invalid_json = Config::Parse("{", ConfigFormat::kJson, "broken.json");
    EXPECT_FALSE(invalid_json);
    EXPECT_EQ(invalid_json.status().code(), StatusCode::kInvalidArgument);
    EXPECT_NE(invalid_json.status().message().find("broken.json"), std::string::npos);

    const auto invalid_yaml =
        Config::Parse("key: \"unterminated", ConfigFormat::kYaml, "broken.yaml");
    EXPECT_FALSE(invalid_yaml);
    EXPECT_EQ(invalid_yaml.status().code(), StatusCode::kInvalidArgument);
    EXPECT_NE(invalid_yaml.status().message().find("broken.yaml"), std::string::npos);

    const auto non_string_key = Config::Parse("1: value\n", ConfigFormat::kYaml, "keys.yaml");
    EXPECT_FALSE(non_string_key);
    EXPECT_EQ(non_string_key.status().code(), StatusCode::kInvalidArgument);
    EXPECT_NE(non_string_key.status().message().find("keys.yaml"), std::string::npos);

    const auto tagged_value =
        Config::Parse("key: !custom value\n", ConfigFormat::kYaml, "tagged.yaml");
    EXPECT_FALSE(tagged_value);
    EXPECT_EQ(tagged_value.status().code(), StatusCode::kInvalidArgument);
    EXPECT_NE(tagged_value.status().message().find("tagged.yaml"), std::string::npos);

    const auto scalar_root = Config::Parse("[]", ConfigFormat::kJson, "array.json");
    EXPECT_FALSE(scalar_root);
    EXPECT_EQ(scalar_root.status().code(), StatusCode::kInvalidArgument);
}

TEST(ConfigTest, MergesObjectsAndReplacesOtherValues) {
    const Config base = ParseOrFail(
        R"({"service":{"host":"localhost","port":80,"headers":["old"]},"mode":"old","nullable":"value"})",
        ConfigFormat::kJson);
    const Config overlay = ParseOrFail(
        R"({"service":{"port":443,"headers":["new"]},"mode":{"enabled":true},"nullable":null})",
        ConfigFormat::kJson);
    const Config merged = base.Merge(overlay);

    EXPECT_EQ(merged.GetString("service.host").value(), "localhost");
    EXPECT_EQ(merged.GetInt64("service.port").value(), 443);
    EXPECT_EQ(merged.GetString("service.headers.0").value(), "new");
    EXPECT_EQ(merged.GetString("service.headers.1").status().code(), StatusCode::kNotFound);
    EXPECT_TRUE(merged.Has("mode.enabled"));
    EXPECT_TRUE(merged.GetBool("mode.enabled").value());
    EXPECT_TRUE(merged.Has("nullable"));
    EXPECT_EQ(merged.GetString("nullable").status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(base.GetInt64("service.port").value(), 80);
}

TEST(ConfigTest, LayeredConfigMergesByPriorityAndReportsSources) {
    const Config defaults = ParseOrFail(
        R"({"service":{"host":"localhost","port":80,"headers":["default"]},"mode":"default","retries":1})",
        ConfigFormat::kJson, "defaults.json");
    const Config file = ParseOrFail(R"({"service":{"port":443},"mode":{"file":true},"retries":2})",
                                    ConfigFormat::kJson, "app.json");
    const Config environment =
        ParseOrFail(R"({"service":{"headers":["environment"]},"logging":{"level":"debug"}})",
                    ConfigFormat::kJson, "environment");
    const Config command_line =
        ParseOrFail(R"({"service":{"port":8443}})", ConfigFormat::kJson, "command-line");

    const LayeredConfig layered = LayersOrFail({
        {"defaults", 100, defaults},
        {"file", 10, file},
        {"environment", 0, environment},
        {"command-line", 0, command_line},
    });
    const Config snapshot = layered.Snapshot();

    EXPECT_EQ(snapshot.GetString("service.host").value(), "localhost");
    EXPECT_EQ(snapshot.GetInt64("service.port").value(), 8443);
    EXPECT_EQ(snapshot.GetString("service.headers.0").value(), "environment");
    EXPECT_EQ(snapshot.GetString("service.headers.1").status().code(), StatusCode::kNotFound);
    EXPECT_EQ(snapshot.GetInt64("retries").value(), 2);
    EXPECT_TRUE(snapshot.GetBool("mode.file").value());
    EXPECT_EQ(snapshot.GetString("logging.level").value(), "debug");

    EXPECT_EQ(layered.SourceOf("service.host").value(), "defaults");
    EXPECT_EQ(layered.SourceOf("service.port").value(), "command-line");
    EXPECT_EQ(layered.SourceOf("service.headers.0").value(), "environment");
    EXPECT_EQ(layered.SourceOf("retries").value(), "file");
    EXPECT_EQ(layered.SourceOf("service").value(), "command-line");
    EXPECT_EQ(layered.SourceOf("missing").status().code(), StatusCode::kNotFound);
    EXPECT_EQ(layered.SourceOf("service..port").status().code(), StatusCode::kInvalidArgument);
}

TEST(ConfigTest, LayeredConfigAcceptsEmptyLayersAndRejectsInvalidLabels) {
    const LayeredConfig empty = LayersOrFail({});
    EXPECT_FALSE(empty.Snapshot().Has("anything"));
    EXPECT_EQ(empty.SourceOf("anything").status().code(), StatusCode::kNotFound);

    const Config config = ParseOrFail(R"({"value":1})", ConfigFormat::kJson);
    const auto empty_label = LayeredConfig::Create({{"", 0, config}});
    EXPECT_FALSE(empty_label);
    EXPECT_EQ(empty_label.status().code(), StatusCode::kInvalidArgument);

    const auto duplicate_label = LayeredConfig::Create({{"same", 0, config}, {"same", 1, config}});
    EXPECT_FALSE(duplicate_label);
    EXPECT_EQ(duplicate_label.status().code(), StatusCode::kInvalidArgument);
}

TEST(ConfigTest, LayeredConfigCopiesAndReadsConcurrently) {
    const LayeredConfig original = LayersOrFail({
        {"defaults", 1, ParseOrFail(R"({"generation":1,"name":"default"})", ConfigFormat::kJson)},
        {"override", 0, ParseOrFail(R"({"generation":2})", ConfigFormat::kJson)},
    });
    const LayeredConfig copy = original;
    LayeredConfig moved_from = original;
    const LayeredConfig moved = std::move(moved_from);
    const Config retained_snapshot = original.Snapshot();
    EXPECT_EQ(retained_snapshot.GetInt64("generation").value(), 2);
    EXPECT_EQ(copy.SourceOf("name").value(), "defaults");
    EXPECT_EQ(moved.SourceOf("generation").value(), "override");
    EXPECT_EQ(moved_from.SourceOf("generation").value(), "override");

    std::atomic<bool> saw_invalid_value{false};
    std::vector<std::thread> readers;
    for (int reader = 0; reader != 4; ++reader) {
        readers.emplace_back([&] {
            for (int iteration = 0; iteration != 500; ++iteration) {
                const Config snapshot = copy.Snapshot();
                const auto generation = snapshot.GetInt64("generation");
                const auto source = copy.SourceOf("generation");
                if (!generation || generation.value() != 2 || !source ||
                    source.value() != "override") {
                    saw_invalid_value.store(true, std::memory_order_release);
                }
            }
        });
    }
    for (std::thread& reader : readers) {
        reader.join();
    }
    EXPECT_FALSE(saw_invalid_value.load(std::memory_order_acquire));
    EXPECT_EQ(retained_snapshot.GetString("name").value(), "default");
}

TEST(ConfigTest, ReloadPublishesOnlyCompleteSnapshots) {
    const Config first = ParseOrFail(R"({"generation":1,"value":"one"})", ConfigFormat::kJson);
    ConfigStore store(first);

    EXPECT_TRUE(
        store.Reload(R"({"generation":2,"value":"two"})", ConfigFormat::kJson, "second.json"));
    const Config second = store.Snapshot();
    EXPECT_EQ(second.GetInt64("generation").value(), 2);
    EXPECT_EQ(second.GetString("value").value(), "two");

    EXPECT_FALSE(store.Reload("{", ConfigFormat::kJson, "invalid.json"));
    EXPECT_EQ(store.Snapshot().GetInt64("generation").value(), 2);

    std::atomic<bool> running{true};
    std::atomic<bool> saw_invalid_pair{false};
    std::vector<std::thread> readers;
    for (int reader = 0; reader != 4; ++reader) {
        readers.emplace_back([&] {
            while (running.load(std::memory_order_acquire)) {
                const Config snapshot = store.Snapshot();
                const auto generation = snapshot.GetInt64("generation");
                const auto value = snapshot.GetString("value");
                if (!generation || !value || (generation.value() == 1 && value.value() != "one") ||
                    (generation.value() == 2 && value.value() != "two")) {
                    saw_invalid_pair.store(true, std::memory_order_release);
                }
            }
        });
    }
    for (int iteration = 0; iteration != 200; ++iteration) {
        const bool first_generation = iteration % 2 == 0;
        ASSERT_TRUE(store.Reload(first_generation ? R"({"generation":1,"value":"one"})"
                                                  : R"({"generation":2,"value":"two"})",
                                 ConfigFormat::kJson));
    }
    running.store(false, std::memory_order_release);
    for (std::thread& reader : readers) {
        reader.join();
    }
    EXPECT_FALSE(saw_invalid_pair.load(std::memory_order_acquire));
}

}  // namespace
}  // namespace tos
