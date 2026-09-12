#ifndef TOS_BASE_STRING_H_
#define TOS_BASE_STRING_H_

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace tos {
namespace string_detail {

inline bool IsAsciiWhitespace(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

inline char ToAsciiLower(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                : character;
}

inline char ToAsciiUpper(char character) noexcept {
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A')
                                                : character;
}

template <typename Iterator>
std::string Join(Iterator begin, Iterator end, std::string_view delimiter) {
    std::string result;
    for (Iterator current = begin; current != end; ++current) {
        if (current != begin && !delimiter.empty()) {
            result.append(delimiter.data(), delimiter.size());
        }
        const std::string_view part(*current);
        if (!part.empty()) {
            result.append(part.data(), part.size());
        }
    }
    return result;
}

}  // namespace string_detail

/// Removes leading ASCII whitespace (space, tab, newline, carriage return, form-feed, and
/// vertical-tab) and returns an owning copy. Allocation exceptions propagate.
inline std::string StrTrimLeft(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && string_detail::IsAsciiWhitespace(text[begin])) {
        ++begin;
    }
    return std::string(text.substr(begin));
}

/// Removes trailing ASCII whitespace and returns an owning copy. Allocation exceptions propagate.
inline std::string StrTrimRight(std::string_view text) {
    std::size_t end = text.size();
    while (end != 0 && string_detail::IsAsciiWhitespace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(0, end));
}

/// Removes leading and trailing ASCII whitespace and returns an owning copy.
/// Allocation exceptions propagate.
inline std::string StrTrim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && string_detail::IsAsciiWhitespace(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && string_detail::IsAsciiWhitespace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

/// Converts ASCII A-Z bytes to lowercase. Non-ASCII bytes are preserved unchanged.
/// Allocation exceptions propagate.
inline std::string StrToLower(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        character = string_detail::ToAsciiLower(character);
    }
    return result;
}

/// Converts ASCII a-z bytes to uppercase. Non-ASCII bytes are preserved unchanged.
/// Allocation exceptions propagate.
inline std::string StrToUpper(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        character = string_detail::ToAsciiUpper(character);
    }
    return result;
}

/// Returns whether text starts with prefix using an exact byte comparison. Never throws.
inline bool StrStartsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/// Returns whether text ends with suffix using an exact byte comparison. Never throws.
inline bool StrEndsWith(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Returns whether text contains needle using an exact byte comparison. Never throws.
inline bool StrContains(std::string_view text, std::string_view needle) noexcept {
    return text.find(needle) != std::string_view::npos;
}

/// Returns whether text contains needle after ASCII-only case folding. Non-ASCII bytes remain
/// distinct and are compared exactly. Never throws.
inline bool StrContainsIgnoreCase(std::string_view text, std::string_view needle) noexcept {
    if (needle.size() > text.size()) {
        return false;
    }
    for (std::size_t begin = 0; begin <= text.size() - needle.size(); ++begin) {
        std::size_t offset = 0;
        while (offset < needle.size() && string_detail::ToAsciiLower(text[begin + offset]) ==
                                             string_detail::ToAsciiLower(needle[offset])) {
            ++offset;
        }
        if (offset == needle.size()) {
            return true;
        }
    }
    return false;
}

/// Splits text on each exact byte delimiter and preserves empty fields, including leading and
/// trailing fields. An empty delimiter returns a single field containing text. The returned
/// strings own their data; allocation exceptions propagate.
inline std::vector<std::string> StrSplit(std::string_view text, std::string_view delimiter) {
    std::vector<std::string> fields;
    if (delimiter.empty()) {
        fields.emplace_back(text);
        return fields;
    }

    std::size_t begin = 0;
    while (true) {
        const std::size_t position = text.find(delimiter, begin);
        if (position == std::string_view::npos) {
            fields.emplace_back(text.substr(begin));
            return fields;
        }
        fields.emplace_back(text.substr(begin, position - begin));
        begin = position + delimiter.size();
    }
}

/// Joins owning strings with delimiter and returns an owning result. Allocation exceptions
/// propagate.
inline std::string StrJoin(const std::vector<std::string>& values, std::string_view delimiter) {
    return string_detail::Join(values.begin(), values.end(), delimiter);
}

/// Joins borrowed string views with delimiter and returns an owning result. Borrowed values are
/// read only for the duration of this call; allocation exceptions propagate.
inline std::string StrJoin(const std::vector<std::string_view>& values,
                           std::string_view delimiter) {
    return string_detail::Join(values.begin(), values.end(), delimiter);
}

/// Joins string views with delimiter and returns an owning result. Allocation exceptions
/// propagate.
inline std::string StrJoin(std::initializer_list<std::string_view> values,
                           std::string_view delimiter) {
    return string_detail::Join(values.begin(), values.end(), delimiter);
}

/// Replaces non-overlapping occurrences of from from left to right. An empty from returns an
/// owning copy of text unchanged. Allocation exceptions propagate.
inline std::string StrReplaceAll(std::string_view text, std::string_view from,
                                 std::string_view to) {
    if (from.empty()) {
        return std::string(text);
    }

    std::string result;
    std::size_t begin = 0;
    while (true) {
        const std::size_t position = text.find(from, begin);
        if (position == std::string_view::npos) {
            const std::string_view suffix = text.substr(begin);
            if (!suffix.empty()) {
                result.append(suffix.data(), suffix.size());
            }
            return result;
        }
        const std::string_view prefix = text.substr(begin, position - begin);
        if (!prefix.empty()) {
            result.append(prefix.data(), prefix.size());
        }
        if (!to.empty()) {
            result.append(to.data(), to.size());
        }
        begin = position + from.size();
    }
}

}  // namespace tos

#endif  // TOS_BASE_STRING_H_
