#include "tos/base/process.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--child") {
        std::cout << "process example child";
        return 3;
    }

    const std::filesystem::path executable = std::filesystem::absolute(argv[0]);
    auto path = tos::Path::Parse(executable.u8string());
    if (!path) {
        return 1;
    }
    tos::ProcessOptions options{std::move(path).value(), {"--child"}, std::nullopt, {}};
    auto result = tos::RunCommand(options);
    if (!result || !result->exit.exit_code || *result->exit.exit_code != 3 ||
        result->stdout_output != "process example child") {
        return 1;
    }
    return 0;
}
