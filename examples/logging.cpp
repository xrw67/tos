#include "tos/logging.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "tos-logging-example.jsonl";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(std::filesystem::path(path.string() + ".1"), ignored);

    tos::LoggerOptions options;
    options.name = "example";
    tos::Logger logger(options);

    auto utf8_path = tos::Path::Parse(path.u8string());
    if (!utf8_path) {
        std::cerr << utf8_path.status().ToString() << '\n';
        return 1;
    }
    const tos::Status configured =
        logger.AddRotatingFileSink({std::move(utf8_path).value(), 1024 * 1024, 2});
    if (!configured) {
        std::cerr << configured.ToString() << '\n';
        return 1;
    }
    const tos::Status logged =
        logger.Info({{"request_id", std::string("r-42")}, {"attempt", std::int64_t(1)}},
                    "processed request {}", "r-42");
    if (!logged) {
        std::cerr << logged.ToString() << '\n';
        return 1;
    }
    const tos::Status stopped = logger.Shutdown();
    if (!stopped) {
        std::cerr << stopped.ToString() << '\n';
        return 1;
    }

    std::filesystem::remove(path, ignored);
    std::filesystem::remove(std::filesystem::path(path.string() + ".1"), ignored);
    return 0;
}
