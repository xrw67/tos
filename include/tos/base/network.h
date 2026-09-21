#ifndef TOS_BASE_NETWORK_H_
#define TOS_BASE_NETWORK_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "tos/base/result.h"

namespace tos {

using IPv4Address = std::array<std::uint8_t, 4>;
using IPv6Address = std::array<std::uint8_t, 16>;

[[nodiscard]] std::uint16_t HostToNetwork16(std::uint16_t value) noexcept;
[[nodiscard]] std::uint16_t NetworkToHost16(std::uint16_t value) noexcept;
[[nodiscard]] std::uint32_t HostToNetwork32(std::uint32_t value) noexcept;
[[nodiscard]] std::uint32_t NetworkToHost32(std::uint32_t value) noexcept;
[[nodiscard]] std::uint64_t HostToNetwork64(std::uint64_t value) noexcept;
[[nodiscard]] std::uint64_t NetworkToHost64(std::uint64_t value) noexcept;

[[nodiscard]] Result<IPv4Address> ParseIPv4(std::string_view text);
[[nodiscard]] std::string FormatIPv4(const IPv4Address& address);
[[nodiscard]] Result<IPv6Address> ParseIPv6(std::string_view text);
[[nodiscard]] std::string FormatIPv6(const IPv6Address& address);

}  // namespace tos

#endif  // TOS_BASE_NETWORK_H_
