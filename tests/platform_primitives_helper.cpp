#include <chrono>
#include <string>

#include "tos/base/file_lock.h"
#include "tos/base/termination.h"

#ifndef _WIN32
#include <signal.h>
#endif

namespace {
#ifndef _WIN32
volatile sig_atomic_t restored = 0;
void PreviousHandler(int) { restored = 1; }
#endif
}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "lock") {
        auto path = tos::Path::Parse(argv[2]);
        if (!path) return 2;
        auto lock = tos::FileLock::TryAcquire(path.value());
        const bool expected = std::string(argv[3]) == "available";
        return lock ? (expected ? 0 : 3)
                    : (!expected && lock.status().code() == tos::StatusCode::kUnavailable ? 0 : 4);
    }
    if (argc != 2 || std::string(argv[1]) != "signal") return 2;
#ifdef _WIN32
    FreeConsole();
    if (!AllocConsole()) return 5;
#else
    struct sigaction original = {};
    struct sigaction previous = {};
    previous.sa_handler = PreviousHandler;
    sigemptyset(&previous.sa_mask);
    if (sigaction(SIGINT, &previous, &original) != 0) return 5;
#endif
    tos::TerminationController controller;
    if (!controller.Install()) return 6;
#ifdef _WIN32
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0)) return 7;
#else
    if (raise(SIGINT) != 0) return 7;
#endif
    auto interrupt = controller.Wait(std::chrono::seconds(5));
    if (!interrupt || *interrupt != tos::TerminationReason::kInterrupt) return 8;
    controller.Uninstall();
#ifndef _WIN32
    if (raise(SIGINT) != 0 || restored != 1) return 9;
    if (sigaction(SIGINT, &original, nullptr) != 0) return 10;
    if (!controller.Install()) return 11;
    if (raise(SIGTERM) != 0) return 12;
    auto terminate = controller.Wait(std::chrono::seconds(5));
    if (!terminate || *terminate != tos::TerminationReason::kTerminate) return 13;
#endif
    return 0;
}
