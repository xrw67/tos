#ifndef TOS_BASE_SECURE_RANDOM_H_
#define TOS_BASE_SECURE_RANDOM_H_

#include <cstddef>
#include <string>

#include "tos/base/result.h"

namespace tos {

/// Returns cryptographically secure random bytes in an owning byte string.
/// The string may contain NUL bytes. The operating system random source is used directly;
/// failures return kUnavailable and allocation exceptions propagate. Zero length succeeds without
/// accessing the source; an unrepresentable size returns kOutOfRange. Concurrent calls are safe.
/// Output buffers are not erased by the library; callers own sensitive-memory handling.
[[nodiscard]] Result<std::string> SecureRandomBytes(std::size_t size);

}  // namespace tos

#endif  // TOS_BASE_SECURE_RANDOM_H_
