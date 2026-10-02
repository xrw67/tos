#include "tos/base/scope_exit.h"

#include <memory>
#include <utility>

int main() {
    int result = 0;
    {
        auto cleanup =
            tos::MakeScopeExit([owned = std::make_unique<int>(7), &result] { result += *owned; });
        auto owner = std::move(cleanup);
        tos::ScopeExit cancelled([&] { result = -1; });
        cancelled.Release();
    }
    return result == 7 ? 0 : 1;
}
