#ifndef TOS_TEST_UTIL_H_
#define TOS_TEST_UTIL_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "tos/base/scope_exit.h"
#include "tos/base/uuid.h"

namespace tos {
namespace test {

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
        // Independent CTest processes and concurrently running build trees must not share paths.
        auto id = GenerateUuidV4();
        if (!id) throw std::runtime_error(std::string(id.status().message()));
        path_ = std::filesystem::temp_directory_path() / (std::move(name_prefix) + id.value());
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
    std::filesystem::path path_;
};

}  // namespace test
}  // namespace tos

#endif  // TOS_TEST_UTIL_H_
