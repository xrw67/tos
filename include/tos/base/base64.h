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

/// Encodes binary input with RFC 4648 standard Base64 and required '=' padding.
/// The implementation does not depend on OpenSSL. An unrepresentable encoded size throws
/// std::length_error; output allocation exceptions propagate. The function does not retain input
/// and is safe to call concurrently.
std::string Base64Encode(span<const std::uint8_t> input);

/// Strictly decodes RFC 4648 standard Base64.
/// Whitespace, URL-safe characters, missing padding, non-canonical pad bits, and malformed input
/// return kInvalidArgument. The implementation does not depend on OpenSSL. Output allocation
/// exceptions propagate. The function does not retain input and is safe to call concurrently.
Result<std::vector<std::uint8_t>> Base64Decode(std::string_view encoded);

/// Encodes binary input with RFC 4648 Base64url without '=' padding.
/// The implementation does not depend on OpenSSL. An unrepresentable encoded size throws
/// std::length_error; output allocation exceptions propagate. The function does not retain input
/// and is safe to call concurrently.
std::string Base64UrlEncode(span<const std::uint8_t> input);

/// Strictly decodes unpadded RFC 4648 Base64url.
/// Standard Base64 characters, '=', whitespace, non-canonical pad bits, and malformed input
/// return kInvalidArgument. The implementation does not depend on OpenSSL. Output allocation
/// exceptions propagate. The function does not retain input and is safe to call concurrently.
Result<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view encoded);

}  // namespace tos

#endif  // TOS_BASE_BASE64_H_
