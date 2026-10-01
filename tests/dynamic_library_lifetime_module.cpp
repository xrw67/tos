#include <atomic>

#ifdef _WIN32
#define TOS_TEST_EXPORT __declspec(dllexport)
#else
#define TOS_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace {
struct UnloadObserver {
    std::atomic<int>* counter{nullptr};
    ~UnloadObserver() {
        if (counter) counter->fetch_add(1);
    }
} observer;
}  // namespace

extern "C" TOS_TEST_EXPORT void TosObserveUnload(std::atomic<int>* counter) {
    observer.counter = counter;
}

extern "C" TOS_TEST_EXPORT int TosLifetimeIncrement(int value) { return value + 1; }
