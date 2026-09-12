#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "tos/base/strconv.h"
#endif

namespace {

int ParseNumber(const std::string& text) { return std::atoi(text.c_str()); }

int Run(const std::vector<std::string>& arguments) {
    if (arguments.size() < 2) {
        return 64;
    }
    const std::string& mode = arguments[1];
    if (mode == "emit" && arguments.size() == 5) {
        std::cout << arguments[2];
        std::cerr << arguments[3];
        return ParseNumber(arguments[4]);
    }
    if (mode == "sleep" && arguments.size() == 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ParseNumber(arguments[2])));
        return ParseNumber(arguments[3]);
    }
    if (mode == "large" && arguments.size() == 4) {
        const int stdout_size = ParseNumber(arguments[2]);
        const int stderr_size = ParseNumber(arguments[3]);
        for (int index = 0; index < stdout_size; ++index) {
            std::cout.put('o');
        }
        for (int index = 0; index < stderr_size; ++index) {
            std::cerr.put('e');
        }
        return 0;
    }
    if (mode == "cwd" && arguments.size() == 2) {
        std::cout << std::filesystem::current_path().u8string();
        return 0;
    }
    if (mode == "env" && arguments.size() == 3) {
        const char* value = std::getenv(arguments[2].c_str());
        std::cout << (value == nullptr ? "<missing>" : value);
        return 0;
    }
    return 64;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        auto utf8 = tos::WideToUtf8(argv[index]);
        if (!utf8) {
            return 65;
        }
        arguments.push_back(std::move(utf8).value());
    }
    return Run(arguments);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return Run(arguments);
}
#endif
