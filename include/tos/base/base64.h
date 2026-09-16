#ifndef TOS_BASE_BASE64_H_
#define TOS_BASE_BASE64_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tos/base/result.h"
#include "tos/base/span.h"

namespace tos {

/// Encodes borrowed binary input as padded RFC 4648 Base64.
/// Unrepresentable output size throws std::length_error; allocation exceptions propagate.
/// Thread-safe.
std::string Base64Encode(span<const std::uint8_t> input);

/// Strictly decodes borrowed RFC 4648 Base64.
/// Whitespace, URL-safe characters, missing padding, non-canonical pad bits, and malformed input
/// return kInvalidArgument; allocation exceptions propagate. Thread-safe.
Result<std::vector<std::uint8_t>> Base64Decode(std::string_view encoded);

/// Encodes borrowed binary input as unpadded RFC 4648 Base64url.
/// Unrepresentable output size throws std::length_error; allocation exceptions propagate.
/// Thread-safe.
std::string Base64UrlEncode(span<const std::uint8_t> input);

/// Strictly decodes borrowed unpadded RFC 4648 Base64url.
/// Standard Base64 characters, '=', whitespace, non-canonical pad bits, and malformed input
/// return kInvalidArgument; allocation exceptions propagate. Thread-safe.
Result<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view encoded);

}  // namespace tos

#endif  // TOS_BASE_BASE64_H_
