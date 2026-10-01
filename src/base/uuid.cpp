#include "tos/base/uuid.h"

#include <array>
#include <string>
#include <utility>

#include "tos/base/secure_random.h"

namespace tos {
namespace {

constexpr char kHex[] = "0123456789abcdef";

}  // namespace

bool IsUuid(std::string_view value) noexcept {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
        } else {
            const char digit = value[index];
            if (!(digit >= '0' && digit <= '9') && !(digit >= 'a' && digit <= 'f') &&
                !(digit >= 'A' && digit <= 'F')) {
                return false;
            }
        }
    }
    return true;
}

Result<std::string> GenerateUuidV4() {
    auto random = SecureRandomBytes(16);
    if (!random) {
        return std::move(random).status();
    }
    std::array<unsigned char, 16> bytes{};
    const std::string& source = random.value();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<unsigned char>(source[index]);
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);

    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            result.push_back('-');
        }
        result.push_back(kHex[(bytes[index] >> 4U) & 0x0fU]);
        result.push_back(kHex[bytes[index] & 0x0fU]);
    }
    return result;
}

}  // namespace tos
