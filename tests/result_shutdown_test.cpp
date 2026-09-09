#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "tos/result.h"

namespace {

struct ThrowOnMove {
    explicit ThrowOnMove(int) {}
    ThrowOnMove(ThrowOnMove&&) { throw std::runtime_error("move failed"); }
    ThrowOnMove& operator=(ThrowOnMove&&) = default;
};

struct ShutdownCheck {
    tos::Result<int> moved = tos::Status(tos::StatusCode::kNotFound, "original");
    tos::Result<ThrowOnMove> valueless = tos::Status(tos::StatusCode::kNotFound, "original");
    std::string_view mode;

    bool verify() const {
        const auto& error = mode == "moved" ? moved.status() : valueless.status();
        const auto expected = mode == "moved" ? "Result error has been moved"
                                              : "Result has no value after an exception";
        return !(mode == "moved" ? moved.ok() : valueless.ok()) &&
               error.code() == tos::StatusCode::kInternal && error.message() == expected;
    }

    ~ShutdownCheck() {
        // This global predates fallback initialization in main, so its destructor
        // runs after ordinary function-local static Status destructors would run.
        if (!verify()) {
            std::fputs("Result status changed during global destruction\n", stderr);
            std::abort();
        }
    }
};

ShutdownCheck check;

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::abort();
    }
    check.mode = argv[1];
    if (check.mode == "moved") {
        const auto extracted = std::move(check.moved).status();
    } else if (check.mode == "valueless") {
        tos::Result<ThrowOnMove> source = 7;
        try {
            check.valueless = std::move(source);
            std::abort();
        } catch (const std::runtime_error&) {
        }
    } else {
        std::abort();
    }
    return check.verify() ? 0 : 1;
}
