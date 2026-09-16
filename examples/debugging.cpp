#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tos/app/app.h"
#include "tos/app/debug.h"

int main() {
    tos::AppOptions options;
    options.log.console = false;
    tos::App app(std::move(options));
    tos::DebugController& debug = app.debug();
    if (!app.Start() || !debug.RegisterHandler(
                            "echo", [](const tos::span<std::string>& args, std::ostream& output) {
                                output << (args.empty() ? std::string("empty") : args.front());
                            })) {
        return 1;
    }

    std::ostringstream status;
    std::ostringstream echoed;
    if (!debug.Execute("status", status) || !debug.Execute("echo debug controls ready", echoed) ||
        echoed.str() != "debug") {
        return 1;
    }
    std::cout << status.str();
    return app.Stop() ? 0 : 1;
}
