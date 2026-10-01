#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>

namespace {
// Volatile values prevent optimization from removing the intentional faults.
volatile int overflow_operand = std::numeric_limits<int>::max();
volatile int shared_value = 0;
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "address") == 0) {
        auto bytes = std::make_unique<volatile char[]>(8);
        volatile int offset = argc + 6;
        bytes[offset] = 1;
        return 0;
    }
    if (std::strcmp(argv[1], "undefined") == 0) {
        volatile int result = overflow_operand + 1;
        (void)result;
        return 0;
    }
    if (std::strcmp(argv[1], "thread") == 0) {
        std::atomic<int> ready{0};
        auto write = [&] {
            ready.fetch_add(1);
            while (ready.load() != 2) std::this_thread::yield();
            for (int index = 0; index < 1000; ++index) shared_value = index;
        };
        std::thread first(write), second(write);
        first.join();
        second.join();
        return 0;
    }
    return 2;
}
