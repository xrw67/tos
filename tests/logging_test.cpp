#include "tos/base/logging.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tos/base/json.h"

namespace tos {
namespace {

class TemporaryDirectory {
   public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("tos-logging-test-" + std::to_string(next_id_.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

   private:
    static std::atomic<std::uint64_t> next_id_;
    std::filesystem::path path_;
};

std::atomic<std::uint64_t> TemporaryDirectory::next_id_{0};

Path LogPath(const std::filesystem::path& path) {
    auto parsed = Path::Parse(path.u8string());
    if (!parsed) {
        throw std::runtime_error(parsed.status().ToString());
    }
    return std::move(parsed).value();
}

std::vector<json> ReadJsonLines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<json> records;
    std::string line;
    while (std::getline(input, line)) {
        records.push_back(json::parse(line));
    }
    return records;
}

LoggerOptions FileLoggerOptions(std::shared_ptr<const IClock> clock) {
    LoggerOptions options;
    options.name = "unit";
    options.console = false;
    options.clock = std::move(clock);
    return options;
}

TEST(LoggerTest, FiltersLevelsAndFormatsConsoleRecords) {
    auto clock = std::make_shared<ManualClock>(Time::FromUnixNanoseconds(0));
    LoggerOptions options;
    options.name = "console";
    options.level = LogLevel::kInfo;
    options.clock = clock;

    std::ostringstream captured_stdout;
    std::ostringstream captured_stderr;
    std::streambuf* const original_stdout = std::cout.rdbuf(captured_stdout.rdbuf());
    std::streambuf* const original_stderr = std::cerr.rdbuf(captured_stderr.rdbuf());
    {
        Logger logger(std::move(options));
        EXPECT_TRUE(logger.Debug("filtered"));
        EXPECT_TRUE(logger.Info({{"request_id", std::string("r-1")}, {"attempt", std::int64_t(2)}},
                                "processed {}", 2));
        EXPECT_TRUE(logger.Warning("attention"));
        EXPECT_TRUE(logger.Flush());
    }
    std::cout.rdbuf(original_stdout);
    std::cerr.rdbuf(original_stderr);

    EXPECT_EQ(captured_stdout.str(),
              "1970-01-01T00:00:00Z [INFO] [console] processed 2 attempt=2 request_id=\"r-1\"\n");
    EXPECT_EQ(captured_stderr.str(), "1970-01-01T00:00:00Z [WARNING] [console] attention\n");
}

TEST(LoggerTest, WritesTypedJsonFieldsWithoutChangingValues) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "typed.jsonl";
    auto clock = std::make_shared<ManualClock>(Time::FromUnixNanoseconds(123400000));
    Logger logger(FileLoggerOptions(clock));
    ASSERT_TRUE(logger.AddRotatingFileSink({LogPath(path), 4096, 2}));

    ASSERT_TRUE(logger.Info({{"enabled", true},
                             {"count", std::int64_t(7)},
                             {"ratio", 1.5},
                             {"label", std::string("line\nbreak")},
                             {"access_token", std::string("do-not-write")}},
                            "completed"));
    ASSERT_TRUE(logger.Shutdown());

    const std::vector<json> records = ReadJsonLines(path);
    ASSERT_EQ(records.size(), 1U);
    const json& record = records.front();
    EXPECT_EQ(record.at("timestamp"), "1970-01-01T00:00:00.1234Z");
    EXPECT_EQ(record.at("level"), "INFO");
    EXPECT_EQ(record.at("logger"), "unit");
    EXPECT_EQ(record.at("message"), "completed");
    EXPECT_TRUE(record.at("fields").at("enabled").is_boolean());
    EXPECT_TRUE(record.at("fields").at("count").is_number_integer());
    EXPECT_TRUE(record.at("fields").at("ratio").is_number_float());
    EXPECT_EQ(record.at("fields").at("label"), "line\nbreak");
    EXPECT_EQ(record.at("fields").at("access_token"), "do-not-write");
}

TEST(LoggerTest, RotatesByProspectiveSizeAndRetainsConfiguredArchives) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "rotate.jsonl";
    Logger logger(FileLoggerOptions(std::make_shared<ManualClock>()));
    ASSERT_TRUE(logger.AddRotatingFileSink({LogPath(path), 150, 2}));

    for (int index = 0; index < 8; ++index) {
        ASSERT_TRUE(logger.Info("record-{}-with-a-long-message", index));
    }
    ASSERT_TRUE(logger.Shutdown());

    const std::filesystem::path first = std::filesystem::path(path.string() + ".1");
    const std::filesystem::path second = std::filesystem::path(path.string() + ".2");
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(first));
    EXPECT_TRUE(std::filesystem::exists(second));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path.string() + ".3")));

    for (const std::filesystem::path& candidate : {path, first, second}) {
        const std::vector<json> records = ReadJsonLines(candidate);
        ASSERT_FALSE(records.empty()) << candidate;
        for (const json& record : records) {
            EXPECT_EQ(record.at("logger"), "unit");
            EXPECT_TRUE(record.at("message").is_string());
        }
    }
}

TEST(LoggerTest, RejectsInvalidFileOptionsAndReportsFileOpenFailures) {
    TemporaryDirectory directory;
    Logger logger(FileLoggerOptions(std::make_shared<ManualClock>()));

    const Status invalid =
        logger.AddRotatingFileSink({LogPath(directory.path() / "invalid.jsonl"), 0, 1});
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.code(), StatusCode::kInvalidArgument);

    const Status directory_file = logger.AddRotatingFileSink({LogPath(directory.path()), 128, 1});
    EXPECT_FALSE(directory_file);
    EXPECT_NE(directory_file.code(), StatusCode::kOk);
}

TEST(LoggerTest, SerializesConcurrentRecordsWithoutCorruptingJsonLines) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "concurrent.jsonl";
    Logger logger(FileLoggerOptions(std::make_shared<ManualClock>()));
    ASSERT_TRUE(logger.AddRotatingFileSink({LogPath(path), 1024 * 1024, 2}));

    constexpr int kThreadCount = 8;
    constexpr int kRecordsPerThread = 100;
    std::vector<std::thread> writers;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        writers.emplace_back([&, thread] {
            for (int index = 0; index < kRecordsPerThread; ++index) {
                const Status status = logger.Info(
                    {{"thread", std::int64_t(thread)}, {"index", std::int64_t(index)}}, "record");
                ASSERT_TRUE(status);
            }
        });
    }
    for (std::thread& writer : writers) {
        writer.join();
    }
    ASSERT_TRUE(logger.Shutdown());

    const std::vector<json> records = ReadJsonLines(path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(kThreadCount * kRecordsPerThread));
    for (const json& record : records) {
        EXPECT_EQ(record.at("message"), "record");
        EXPECT_TRUE(record.at("fields").at("thread").is_number_integer());
        EXPECT_TRUE(record.at("fields").at("index").is_number_integer());
    }
}

TEST(LoggerTest, FlushesOnShutdownRejectsLaterWritesAndDestructorFlushes) {
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path() / "shutdown.jsonl";
    Logger logger(FileLoggerOptions(std::make_shared<ManualClock>()));
    ASSERT_TRUE(logger.AddRotatingFileSink({LogPath(path), 4096, 1}));
    ASSERT_TRUE(logger.Info("before shutdown"));
    ASSERT_TRUE(logger.Shutdown());
    EXPECT_TRUE(logger.Shutdown());

    const Status after_shutdown = logger.Info("after shutdown");
    EXPECT_FALSE(after_shutdown);
    EXPECT_EQ(after_shutdown.code(), StatusCode::kFailedPrecondition);
    const Status flushed_after_shutdown = logger.Flush();
    EXPECT_FALSE(flushed_after_shutdown);
    EXPECT_EQ(flushed_after_shutdown.code(), StatusCode::kFailedPrecondition);
    EXPECT_EQ(ReadJsonLines(path).size(), 1U);

    const std::filesystem::path destructor_path = directory.path() / "destructor.jsonl";
    {
        Logger destructor_logger(FileLoggerOptions(std::make_shared<ManualClock>()));
        ASSERT_TRUE(destructor_logger.AddRotatingFileSink({LogPath(destructor_path), 4096, 1}));
        ASSERT_TRUE(destructor_logger.Info("destructor flush"));
    }
    EXPECT_EQ(ReadJsonLines(destructor_path).size(), 1U);
}

TEST(LoggerTest, ValidatesLevelsAndStructuredValues) {
    Logger logger(FileLoggerOptions(std::make_shared<ManualClock>()));
    const Status level = logger.SetLevel(static_cast<LogLevel>(99));
    EXPECT_FALSE(level);
    EXPECT_EQ(level.code(), StatusCode::kInvalidArgument);

    const Status empty_field = logger.Info({{"", std::string("value")}}, "invalid");
    EXPECT_FALSE(empty_field);
    EXPECT_EQ(empty_field.code(), StatusCode::kInvalidArgument);

    const Status infinite =
        logger.Info({{"value", std::numeric_limits<double>::infinity()}}, "invalid");
    EXPECT_FALSE(infinite);
    EXPECT_EQ(infinite.code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace tos
