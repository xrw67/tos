#include "tos/base/network.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <array>
#include <cstring>

namespace tos {
namespace {

std::uint64_t SwapNetwork64(std::uint64_t value) noexcept {
    const auto high = static_cast<std::uint32_t>(value >> 32U);
    const auto low = static_cast<std::uint32_t>(value);
    return (static_cast<std::uint64_t>(htonl(low)) << 32U) | htonl(high);
}

template <std::size_t Size>
Result<std::array<std::uint8_t, Size>> ParseAddress(std::string_view text, int family,
                                                    std::string_view kind) {
    if (text.empty() || text.find('\0') != std::string_view::npos) {
        return Status(StatusCode::kInvalidArgument, std::string(kind) + " text is invalid");
    }
    std::array<std::uint8_t, Size> address{};
    std::array<char, INET6_ADDRSTRLEN> input{};
    if (text.size() >= input.size()) {
        return Status(StatusCode::kInvalidArgument, std::string(kind) + " text is invalid");
    }
    std::memcpy(input.data(), text.data(), text.size());
    if (inet_pton(family, input.data(), address.data()) != 1) {
        return Status(StatusCode::kInvalidArgument, std::string(kind) + " text is invalid");
    }
    return address;
}

template <std::size_t Size>
std::string FormatAddress(const std::array<std::uint8_t, Size>& address, int family) {
    std::array<char, INET6_ADDRSTRLEN> text{};
#ifdef _WIN32
    const auto text_size = static_cast<DWORD>(text.size());
#else
    const auto text_size = static_cast<socklen_t>(text.size());
#endif
    if (inet_ntop(family, address.data(), text.data(), text_size) == nullptr) {
        return {};
    }
    return text.data();
}

}  // namespace

std::uint16_t HostToNetwork16(std::uint16_t value) noexcept { return htons(value); }

std::uint16_t NetworkToHost16(std::uint16_t value) noexcept { return ntohs(value); }

std::uint32_t HostToNetwork32(std::uint32_t value) noexcept { return htonl(value); }

std::uint32_t NetworkToHost32(std::uint32_t value) noexcept { return ntohl(value); }

std::uint64_t HostToNetwork64(std::uint64_t value) noexcept {
    return htonl(1) == 1 ? value : SwapNetwork64(value);
}

std::uint64_t NetworkToHost64(std::uint64_t value) noexcept {
    return htonl(1) == 1 ? value : SwapNetwork64(value);
}

Result<IPv4Address> ParseIPv4(std::string_view text) {
    return ParseAddress<IPv4Address{}.size()>(text, AF_INET, "IPv4");
}

std::string FormatIPv4(const IPv4Address& address) { return FormatAddress(address, AF_INET); }

Result<IPv6Address> ParseIPv6(std::string_view text) {
    return ParseAddress<IPv6Address{}.size()>(text, AF_INET6, "IPv6");
}

std::string FormatIPv6(const IPv6Address& address) { return FormatAddress(address, AF_INET6); }

}  // namespace tos
