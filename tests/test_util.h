#ifndef TOS_TEST_UTIL_H_
#define TOS_TEST_UTIL_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace tos {
namespace test {

// Test-owned threads must be released and joined even after a fatal assertion.
template <typename F>
class ScopeExit {
   public:
    explicit ScopeExit(F cleanup) : cleanup_(std::move(cleanup)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() { cleanup_(); }

   private:
    F cleanup_;
};

class Gate {
   public:
    void Open() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        ready_.notify_all();
    }
    void Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return open_; });
    }

   private:
    std::mutex mutex_;
    std::condition_variable ready_;
    bool open_{false};
};

class TemporaryDirectory {
   public:
    explicit TemporaryDirectory(std::string name_prefix) {
        path_ = std::filesystem::temp_directory_path() /
                (std::move(name_prefix) + std::to_string(next_id_.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

   private:
    inline static std::atomic<std::uint64_t> next_id_{0};
    std::filesystem::path path_;
};

}  // namespace test
}  // namespace tos

#endif  // TOS_TEST_UTIL_H_
