#ifndef TOS_BASE_UUID_H_
#define TOS_BASE_UUID_H_

#include <string>
#include <string_view>

#include "tos/base/result.h"

namespace tos {

/// Generates a lowercase RFC 4122 version 4 UUID using the operating system CSPRNG.
/// Random-source failures return kUnavailable; allocation exceptions propagate. Thread-safe.
[[nodiscard]] Result<std::string> GenerateUuidV4();

/// Returns whether value has the canonical 36-byte UUID shape and hexadecimal fields.
/// Both uppercase and lowercase hexadecimal digits are accepted for compatibility.
/// Does not enforce version/variant, allocate, or throw. Concurrent calls are safe.
[[nodiscard]] bool IsUuid(std::string_view value) noexcept;

}  // namespace tos

#endif  // TOS_BASE_UUID_H_
