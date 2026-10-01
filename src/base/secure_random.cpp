#include "tos/base/secure_random.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#endif
#endif

namespace tos {
namespace {

#if !defined(_WIN32) && !defined(__APPLE__)
Status RandomError(std::string_view operation) {
    return Status(StatusCode::kUnavailable,
                  std::string(operation) + ": errno " + std::to_string(errno));
}
#endif

}  // namespace

Result<std::string> SecureRandomBytes(std::size_t size) {
    if (size > std::string{}.max_size()) {
        return Status(StatusCode::kOutOfRange, "requested random byte count is too large");
    }
    std::string bytes(size, '\0');
    if (size == 0) {
        return bytes;
    }

#ifdef _WIN32
    std::size_t offset = 0;
    while (offset < size) {
        const auto chunk = static_cast<ULONG>(std::min<std::size_t>(
            size - offset, static_cast<std::size_t>(std::numeric_limits<ULONG>::max())));
        const NTSTATUS result =
            BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data() + offset), chunk,
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (result < 0) {
            return Status(StatusCode::kUnavailable, "BCryptGenRandom failed");
        }
        offset += chunk;
    }
    return bytes;
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
    return bytes;
#else
    std::size_t offset = 0;
#if defined(__linux__)
    while (offset < size) {
        const ssize_t count = getrandom(bytes.data() + offset, size - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno != ENOSYS && errno != EPERM) {
            return RandomError("generate secure random bytes");
        }
        break;
    }
#endif
    if (offset == size) {
        return bytes;
    }

    const int descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return RandomError("open secure random source");
    }
    while (offset < size) {
        const ssize_t count = read(descriptor, bytes.data() + offset, size - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        const int error = count < 0 ? errno : EIO;
        close(descriptor);
        errno = error;
        return RandomError("read secure random source");
    }
    if (close(descriptor) != 0) {
        return RandomError("close secure random source");
    }
    return bytes;
#endif
}

}  // namespace tos
