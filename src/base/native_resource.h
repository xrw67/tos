#ifndef TOS_BASE_NATIVE_RESOURCE_H_
#define TOS_BASE_NATIVE_RESOURCE_H_

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>

#include <cerrno>
#endif

namespace tos {
namespace detail {

// Exclusive native ownership, no allocation and no exceptions. Get borrows; Release
// transfers ownership. Close relinquishes ownership even on error and returns a native
// error code (zero on success). Best-effort cleanup preserves the ambient native error.
// Callers must synchronize access and only adopt resources belonging to this closer.
#ifdef _WIN32
class UniqueHandle final {
   public:
    explicit UniqueHandle(HANDLE value = nullptr) noexcept : value_(Normalize(value)) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) Reset(other.Release());
        return *this;
    }
    ~UniqueHandle() noexcept { Reset(); }

    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    [[nodiscard]] HANDLE Release() noexcept { return std::exchange(value_, nullptr); }
    [[nodiscard]] DWORD Close() noexcept {
        HANDLE value = Release();
        return !value || CloseHandle(value) ? ERROR_SUCCESS : GetLastError();
    }
    void Reset(HANDLE value = nullptr) noexcept {
        value = Normalize(value);
        if (value == value_) return;
        const DWORD error = GetLastError();
        static_cast<void>(Close());
        value_ = value;
        SetLastError(error);
    }

   private:
    static HANDLE Normalize(HANDLE value) noexcept {
        return value == INVALID_HANDLE_VALUE ? nullptr : value;
    }
    HANDLE value_;
};
#else
class UniqueFd final {
   public:
    explicit UniqueFd(int value = -1) noexcept : value_(value < 0 ? -1 : value) {}
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : value_(other.Release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) Reset(other.Release());
        return *this;
    }
    ~UniqueFd() noexcept { Reset(); }

    [[nodiscard]] int Get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ >= 0; }
    [[nodiscard]] int Release() noexcept { return std::exchange(value_, -1); }
    [[nodiscard]] int Close() noexcept {
        const int value = Release();
        // Do not retry EINTR: the descriptor may already have been released and reused.
        return value < 0 || close(value) == 0 ? 0 : errno;
    }
    void Reset(int value = -1) noexcept {
        if (value < 0) value = -1;
        if (value == value_) return;
        const int error = errno;
        static_cast<void>(Close());
        value_ = value;
        errno = error;
    }

   private:
    int value_;
};
#endif

}  // namespace detail
}  // namespace tos

#endif  // TOS_BASE_NATIVE_RESOURCE_H_
