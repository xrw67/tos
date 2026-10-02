#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>

#include "native_resource.h"
#include "tos/base/scope_exit.h"

#ifndef _WIN32
#include <fcntl.h>
#endif

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::string_view scenario(argv[1]);
    if (scenario == "throw") {
        std::set_terminate([] {
            std::fputs("terminate\n", stdout);
            std::fflush(stdout);
            std::_Exit(73);
        });
        auto guard = tos::MakeScopeExit([] { throw 1; });
        return 2;
    }
    if (scenario == "zero") {
#ifndef _WIN32
        // This process may close stdin without affecting the test runner.
        tos::detail::UniqueFd input(STDIN_FILENO);
        if (input.Close()) return 3;
        {
            tos::detail::UniqueFd zero(open("/dev/null", O_RDONLY));
            if (!zero || zero.Get() != 0) return 4;
        }
        if (fcntl(0, F_GETFD) != -1 || errno != EBADF) return 5;
#endif
        return 0;
    }
    return 6;
}
