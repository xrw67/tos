#ifndef TOS_STRCONV_H_
#define TOS_STRCONV_H_

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "tos/result.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tos {
namespace strconv_detail {

inline bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        unsigned char minimum_second = 0x80;
        unsigned char maximum_second = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
        } else if (first == 0xe0) {
            length = 3;
            minimum_second = 0xa0;
        } else if (first >= 0xe1 && first <= 0xec) {
            length = 3;
        } else if (first == 0xed) {
            length = 3;
            maximum_second = 0x9f;
        } else if (first >= 0xee && first <= 0xef) {
            length = 3;
        } else if (first == 0xf0) {
            length = 4;
            minimum_second = 0x90;
        } else if (first >= 0xf1 && first <= 0xf3) {
            length = 4;
        } else if (first == 0xf4) {
            length = 4;
            maximum_second = 0x8f;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if (second < minimum_second || second > maximum_second) {
            return false;
        }
        for (std::size_t offset = 2; offset < length; ++offset) {
            const unsigned char byte = static_cast<unsigned char>(text[index + offset]);
            if (byte < 0x80 || byte > 0xbf) {
                return false;
            }
        }
        index += length;
    }
    return true;
}

inline std::uint32_t DecodeUtf8(std::string_view text, std::size_t* index) noexcept {
    const unsigned char first = static_cast<unsigned char>(text[*index]);
    if (first <= 0x7f) {
        ++*index;
        return first;
    }
    std::size_t length = 0;
    std::uint32_t value = 0;
    if (first <= 0xdf) {
        length = 2;
        value = first & 0x1fU;
    } else if (first <= 0xef) {
        length = 3;
        value = first & 0x0fU;
    } else {
        length = 4;
        value = first & 0x07U;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        value = (value << 6U) | (static_cast<unsigned char>(text[*index + offset]) & 0x3fU);
    }
    *index += length;
    return value;
}

inline void AppendUtf8(std::string* output, std::uint32_t value) {
    if (value <= 0x7f) {
        output->push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
        output->push_back(static_cast<char>(0xc0U | (value >> 6U)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffff) {
        output->push_back(static_cast<char>(0xe0U | (value >> 12U)));
        output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else {
        output->push_back(static_cast<char>(0xf0U | (value >> 18U)));
        output->push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        output->push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
}

}  // namespace strconv_detail

/// Converts valid UTF-8 text to the platform wchar_t representation without using the process
/// locale. Malformed UTF-8 returns kInvalidArgument; embedded NUL characters are preserved.
/// Allocation exceptions propagate. The function retains no input and is safe to call concurrently.
[[nodiscard]] inline Result<std::wstring> Utf8ToWide(std::string_view utf8) {
    if (!strconv_detail::IsValidUtf8(utf8)) {
        return Status(StatusCode::kInvalidArgument, "text must be valid UTF-8");
    }
    std::wstring wide;
    wide.reserve(utf8.size());
    std::size_t index = 0;
    while (index < utf8.size()) {
        const std::uint32_t value = strconv_detail::DecodeUtf8(utf8, &index);
        if (sizeof(wchar_t) == 2 && value > 0xffff) {
            const std::uint32_t supplementary = value - 0x10000;
            wide.push_back(static_cast<wchar_t>(0xd800U + (supplementary >> 10U)));
            wide.push_back(static_cast<wchar_t>(0xdc00U + (supplementary & 0x3ffU)));
        } else {
            wide.push_back(static_cast<wchar_t>(value));
        }
    }
    return wide;
}

/// Converts platform wchar_t text to UTF-8 without using the process locale. Invalid Unicode
/// scalars, including unpaired UTF-16 surrogate code units where wchar_t is 16 bits, return
/// kInvalidArgument; embedded NUL characters are preserved. Allocation exceptions propagate. The
/// function retains no input and is safe to call concurrently.
[[nodiscard]] inline Result<std::string> WideToUtf8(std::wstring_view wide) {
    std::string utf8;
    utf8.reserve(wide.size());
    std::size_t index = 0;
    while (index < wide.size()) {
        std::uint32_t value = static_cast<std::uint32_t>(wide[index++]);
        if (sizeof(wchar_t) == 2) {
            if (value >= 0xd800 && value <= 0xdbff) {
                if (index == wide.size()) {
                    return Status(StatusCode::kInvalidArgument,
                                  "wide text contains an unpaired surrogate");
                }
                const std::uint32_t low = static_cast<std::uint32_t>(wide[index++]);
                if (low < 0xdc00 || low > 0xdfff) {
                    return Status(StatusCode::kInvalidArgument,
                                  "wide text contains an unpaired surrogate");
                }
                value = 0x10000 + ((value - 0xd800) << 10U) + (low - 0xdc00);
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                return Status(StatusCode::kInvalidArgument,
                              "wide text contains an unpaired surrogate");
            }
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return Status(StatusCode::kInvalidArgument,
                          "wide text contains an invalid Unicode scalar");
        }
        strconv_detail::AppendUtf8(&utf8, value);
    }
    return utf8;
}

/// Converts bytes encoded in the current Windows ANSI code page (CP_ACP) to wide text. Embedded
/// NUL characters are preserved. Conversion and allocation failures return Status. On platforms
/// other than Windows, this capability returns kUnimplemented rather than using a locale-dependent
/// fallback. The function retains no input and is safe to call concurrently.
[[nodiscard]] inline Result<std::wstring> AnsiToWide(std::string_view ansi) {
#ifdef _WIN32
    if (ansi.empty()) {
        return std::wstring();
    }
    if (ansi.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Status(StatusCode::kOutOfRange, "ANSI text is too long for Windows conversion");
    }
    const int required =
        MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), nullptr, 0);
    if (required == 0) {
        return Status(StatusCode::kUnavailable, "could not convert ANSI text to wide text");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), wide.data(),
                            required) == 0) {
        return Status(StatusCode::kUnavailable, "could not convert ANSI text to wide text");
    }
    return wide;
#else
    static_cast<void>(ansi);
    return Status(StatusCode::kUnimplemented,
                  "ANSI code page conversion is only available on Windows");
#endif
}

/// Converts wide text to bytes encoded in the current Windows ANSI code page (CP_ACP). Embedded
/// NUL characters are preserved. A character requiring the ANSI default replacement byte returns
/// kInvalidArgument so callers do not silently lose information. On platforms other than Windows,
/// this capability returns kUnimplemented. Allocation failures propagate; the function retains no
/// input and is safe to call concurrently.
[[nodiscard]] inline Result<std::string> WideToAnsi(std::wstring_view wide) {
#ifdef _WIN32
    if (wide.empty()) {
        return std::string();
    }
    if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Status(StatusCode::kOutOfRange, "wide text is too long for Windows conversion");
    }
    BOOL used_default = FALSE;
    const int required =
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(),
                            static_cast<int>(wide.size()), nullptr, 0, nullptr, &used_default);
    if (required == 0) {
        return Status(StatusCode::kUnavailable, "could not convert wide text to ANSI text");
    }
    if (used_default) {
        return Status(StatusCode::kInvalidArgument,
                      "wide text cannot be represented by the Windows ANSI code page");
    }
    std::string ansi(static_cast<std::size_t>(required), '\0');
    used_default = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(),
                            static_cast<int>(wide.size()), ansi.data(), required, nullptr,
                            &used_default) == 0) {
        return Status(StatusCode::kUnavailable, "could not convert wide text to ANSI text");
    }
    if (used_default) {
        return Status(StatusCode::kInvalidArgument,
                      "wide text cannot be represented by the Windows ANSI code page");
    }
    return ansi;
#else
    static_cast<void>(wide);
    return Status(StatusCode::kUnimplemented,
                  "ANSI code page conversion is only available on Windows");
#endif
}

}  // namespace tos

#endif  // TOS_STRCONV_H_
