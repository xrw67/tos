#include "tos/base/file_lock.h"

#include <cerrno>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace tos {

struct FileLock::Impl final {
    ~Impl() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (descriptor >= 0) close(descriptor);
#endif
    }
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int descriptor{-1};
#endif
};

FileLock::FileLock(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
FileLock::FileLock() = default;
FileLock::FileLock(FileLock&&) noexcept = default;
FileLock& FileLock::operator=(FileLock&&) noexcept = default;

FileLock::~FileLock() = default;

Result<FileLock> FileLock::TryAcquire(const Path& path) {
    if (path.empty()) {
        return Status(StatusCode::kInvalidArgument, "lock path must not be empty");
    }
    auto impl = std::make_unique<Impl>();
#ifdef _WIN32
    auto wide = Utf8ToWide(path.utf8());
    if (!wide) {
        return std::move(wide).status();
    }
    impl->handle = CreateFileW(wide.value().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl->handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            return Status(StatusCode::kUnavailable, "lock file is already in use");
        }
        return WindowsError(error, "open lock file");
    }
#else
    impl->descriptor = open(path.utf8().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (impl->descriptor < 0) {
        return ErrnoError(errno, "open lock file");
    }
    int result;
    do {
        result = flock(impl->descriptor, LOCK_EX | LOCK_NB);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        const int error = errno;
        if (error == EACCES || error == EAGAIN) {
            return Status(StatusCode::kUnavailable, "lock file is already in use");
        }
        return ErrnoError(error, "acquire lock file");
    }
#endif
    return FileLock(std::move(impl));
}

}  // namespace tos
