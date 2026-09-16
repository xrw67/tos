#include "tos/base/base64.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tos {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

Status InvalidArgument(std::string_view message) {
    return Status(StatusCode::kInvalidArgument, message);
}

Status OutOfRange(std::string_view message) { return Status(StatusCode::kOutOfRange, message); }

int DecodeValue(char character, bool url_safe) noexcept {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (url_safe && character == '-') {
        return 62;
    }
    if (url_safe && character == '_') {
        return 63;
    }
    if (!url_safe && character == '+') {
        return 62;
    }
    if (!url_safe && character == '/') {
        return 63;
    }
    return -1;
}

Result<std::size_t> EncodedSize(std::size_t input_size) {
    if (input_size > (std::numeric_limits<std::size_t>::max() - 2) / 3) {
        return OutOfRange("Base64 input is too large to encode");
    }
    return ((input_size + 2) / 3) * 4;
}

Result<std::string> Encode(span<const std::uint8_t> input, const char* alphabet,
                           bool include_padding) {
    auto output_size = EncodedSize(input.size());
    if (!output_size) {
        return std::move(output_size).status();
    }

    std::string output(output_size.value(), '=');
    std::size_t source = 0;
    std::size_t destination = 0;
    while (input.size() - source >= 3) {
        const std::uint32_t value = (static_cast<std::uint32_t>(input[source]) << 16) |
                                    (static_cast<std::uint32_t>(input[source + 1]) << 8) |
                                    static_cast<std::uint32_t>(input[source + 2]);
        output[destination++] = alphabet[(value >> 18) & 0x3f];
        output[destination++] = alphabet[(value >> 12) & 0x3f];
        output[destination++] = alphabet[(value >> 6) & 0x3f];
        output[destination++] = alphabet[value & 0x3f];
        source += 3;
    }

    const std::size_t remaining = input.size() - source;
    if (remaining == 1) {
        const std::uint32_t value = static_cast<std::uint32_t>(input[source]) << 16;
        output[destination++] = alphabet[(value >> 18) & 0x3f];
        output[destination++] = alphabet[(value >> 12) & 0x3f];
    } else if (remaining == 2) {
        const std::uint32_t value = (static_cast<std::uint32_t>(input[source]) << 16) |
                                    (static_cast<std::uint32_t>(input[source + 1]) << 8);
        output[destination++] = alphabet[(value >> 18) & 0x3f];
        output[destination++] = alphabet[(value >> 12) & 0x3f];
        output[destination++] = alphabet[(value >> 6) & 0x3f];
    }

    if (!include_padding) {
        output.resize(destination);
    }
    return output;
}

Result<std::vector<std::uint8_t>> DecodeStandard(std::string_view encoded) {
    if (encoded.size() % 4 != 0) {
        return InvalidArgument("Base64 input must have required padding");
    }

    std::size_t padding = 0;
    if (!encoded.empty() && encoded.back() == '=') {
        padding = 1;
        if (encoded.size() >= 2 && encoded[encoded.size() - 2] == '=') {
            padding = 2;
        }
    }
    if (padding != 0 && encoded.size() < 4) {
        return InvalidArgument("Base64 input has invalid padding");
    }

    const std::size_t content_size = encoded.size() - padding;
    for (std::size_t index = 0; index < content_size; ++index) {
        if (DecodeValue(encoded[index], false) < 0) {
            return InvalidArgument("Base64 input contains an invalid character");
        }
    }
    for (std::size_t index = content_size; index < encoded.size(); ++index) {
        if (encoded[index] != '=') {
            return InvalidArgument("Base64 padding must appear only at the end");
        }
    }
    if (padding == 1 && (DecodeValue(encoded[content_size - 1], false) & 0x03) != 0) {
        return InvalidArgument("Base64 input has non-canonical pad bits");
    }
    if (padding == 2 && (DecodeValue(encoded[content_size - 1], false) & 0x0f) != 0) {
        return InvalidArgument("Base64 input has non-canonical pad bits");
    }

    std::vector<std::uint8_t> output((encoded.size() / 4) * 3 - padding);
    std::size_t destination = 0;
    for (std::size_t source = 0; source < encoded.size(); source += 4) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(DecodeValue(encoded[source], false)) << 18) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 1], false)) << 12) |
            (static_cast<std::uint32_t>(
                 encoded[source + 2] == '=' ? 0 : DecodeValue(encoded[source + 2], false))
             << 6) |
            static_cast<std::uint32_t>(
                encoded[source + 3] == '=' ? 0 : DecodeValue(encoded[source + 3], false));
        output[destination++] = static_cast<std::uint8_t>(value >> 16);
        if (encoded[source + 2] != '=') {
            output[destination++] = static_cast<std::uint8_t>(value >> 8);
        }
        if (encoded[source + 3] != '=') {
            output[destination++] = static_cast<std::uint8_t>(value);
        }
    }
    return output;
}

Result<std::vector<std::uint8_t>> DecodeUrl(std::string_view encoded) {
    const std::size_t remainder = encoded.size() % 4;
    if (remainder == 1 || encoded.find('=') != std::string_view::npos) {
        return InvalidArgument("Base64url input has invalid padding");
    }
    for (const char character : encoded) {
        if (DecodeValue(character, true) < 0) {
            return InvalidArgument("Base64url input contains an invalid character");
        }
    }

    const std::size_t complete_groups = encoded.size() / 4;
    if (complete_groups > std::numeric_limits<std::size_t>::max() / 3) {
        return OutOfRange("Base64url input is too large to decode");
    }
    std::size_t output_size = complete_groups * 3;
    if (remainder == 2) {
        ++output_size;
    } else if (remainder == 3) {
        output_size += 2;
    }

    if (remainder == 2 && (DecodeValue(encoded.back(), true) & 0x0f) != 0) {
        return InvalidArgument("Base64url input has non-canonical pad bits");
    }
    if (remainder == 3 && (DecodeValue(encoded.back(), true) & 0x03) != 0) {
        return InvalidArgument("Base64url input has non-canonical pad bits");
    }

    std::vector<std::uint8_t> output(output_size);
    std::size_t destination = 0;
    std::size_t source = 0;
    for (; encoded.size() - source >= 4; source += 4) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(DecodeValue(encoded[source], true)) << 18) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 1], true)) << 12) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 2], true)) << 6) |
            static_cast<std::uint32_t>(DecodeValue(encoded[source + 3], true));
        output[destination++] = static_cast<std::uint8_t>(value >> 16);
        output[destination++] = static_cast<std::uint8_t>(value >> 8);
        output[destination++] = static_cast<std::uint8_t>(value);
    }
    if (remainder == 2) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(DecodeValue(encoded[source], true)) << 18) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 1], true)) << 12);
        output[destination] = static_cast<std::uint8_t>(value >> 16);
    } else if (remainder == 3) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(DecodeValue(encoded[source], true)) << 18) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 1], true)) << 12) |
            (static_cast<std::uint32_t>(DecodeValue(encoded[source + 2], true)) << 6);
        output[destination++] = static_cast<std::uint8_t>(value >> 16);
        output[destination] = static_cast<std::uint8_t>(value >> 8);
    }
    return output;
}

}  // namespace

std::string Base64Encode(span<const std::uint8_t> input) {
    auto encoded = Encode(input, kBase64Alphabet, true);
    if (!encoded) {
        throw std::length_error("Base64 input is too large to encode");
    }
    return std::move(encoded).value();
}

Result<std::vector<std::uint8_t>> Base64Decode(std::string_view encoded) {
    return DecodeStandard(encoded);
}

std::string Base64UrlEncode(span<const std::uint8_t> input) {
    auto encoded = Encode(input, kBase64UrlAlphabet, false);
    if (!encoded) {
        throw std::length_error("Base64url input is too large to encode");
    }
    return std::move(encoded).value();
}

Result<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view encoded) {
    return DecodeUrl(encoded);
}

}  // namespace tos
