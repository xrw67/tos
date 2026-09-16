#ifndef TOS_TEST_UTIL_H_
#define TOS_TEST_UTIL_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace tos {
namespace test {

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
