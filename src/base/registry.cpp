#include "tos/base/registry.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "tos/base/strconv.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tos {

struct RegistryKey::State {
#ifdef _WIN32
    explicit State(HKEY key, REGSAM view_flags) : key(key), view_flags(view_flags) {}

    ~State() {
        if (key != nullptr) {
            RegCloseKey(key);
        }
    }

    HKEY key = nullptr;
    REGSAM view_flags = 0;
#endif
};

namespace {

#ifdef _WIN32

bool ContainsNul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

Status WindowsRegistryStatus(LONG error, std::string_view action) {
    StatusCode code = StatusCode::kUnavailable;
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            code = StatusCode::kNotFound;
            break;
        case ERROR_ACCESS_DENIED:
            code = StatusCode::kPermissionDenied;
            break;
        case ERROR_ALREADY_EXISTS:
            code = StatusCode::kAlreadyExists;
            break;
        case ERROR_KEY_HAS_CHILDREN:
            code = StatusCode::kFailedPrecondition;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            code = StatusCode::kResourceExhausted;
            break;
        default:
            break;
    }
    return Status(code, std::string(action) + ": Windows error " +
                            std::to_string(static_cast<unsigned long>(error)));
}

Result<HKEY> NativeHive(RegistryHive hive) {
    switch (hive) {
        case RegistryHive::kCurrentUser:
            return HKEY_CURRENT_USER;
        case RegistryHive::kLocalMachine:
            return HKEY_LOCAL_MACHINE;
        default:
            return Status(StatusCode::kInvalidArgument, "unknown registry hive");
    }
}

Result<REGSAM> ViewFlags(RegistryView view) {
    switch (view) {
        case RegistryView::kNative:
            return static_cast<REGSAM>(0);
        case RegistryView::k32Bit:
            return KEY_WOW64_32KEY;
        case RegistryView::k64Bit:
            return KEY_WOW64_64KEY;
        default:
            return Status(StatusCode::kInvalidArgument, "unknown registry view");
    }
}

Result<REGSAM> AccessFlags(RegistryAccess access) {
    switch (access) {
        case RegistryAccess::kRead:
            return KEY_READ;
        case RegistryAccess::kReadWrite:
            return KEY_READ | KEY_WRITE;
        default:
            return Status(StatusCode::kInvalidArgument, "unknown registry access");
    }
}

Result<std::wstring> RegistryInput(std::string_view input, std::string_view description) {
    if (ContainsNul(input)) {
        return Status(StatusCode::kInvalidArgument,
                      std::string(description) + " must not contain NUL bytes");
    }
    auto wide = Utf8ToWide(input);
    if (!wide) {
        return std::move(wide).status();
    }
    return std::move(wide).value();
}

Result<std::string> RegistryOutput(std::wstring_view input, std::string_view description) {
    auto utf8 = WideToUtf8(input);
    if (!utf8) {
        return Status(StatusCode::kDataLoss,
                      std::string(description) + " contains invalid UTF-16 data");
    }
    if (ContainsNul(utf8.value())) {
        return Status(StatusCode::kDataLoss,
                      std::string(description) + " contains an embedded NUL byte");
    }
    return std::move(utf8).value();
}

bool Utf8ByteLess(const std::string& left, const std::string& right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char left_byte, char right_byte) {
            return static_cast<unsigned char>(left_byte) < static_cast<unsigned char>(right_byte);
        });
}

Result<std::wstring> DecodeRegistryString(const std::vector<std::uint8_t>& data,
                                          std::string_view description) {
    if (data.size() < sizeof(wchar_t) || data.size() % sizeof(wchar_t) != 0) {
        return Status(StatusCode::kDataLoss,
                      std::string(description) + " is missing a UTF-16 terminator");
    }
    std::wstring text(data.size() / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), data.data(), data.size());
    if (text.back() != L'\0' || text.find(L'\0') != text.size() - 1) {
        return Status(StatusCode::kDataLoss,
                      std::string(description) + " has an invalid UTF-16 terminator");
    }
    text.pop_back();
    return text;
}

Result<std::vector<std::wstring>> DecodeRegistryMultiString(const std::vector<std::uint8_t>& data) {
    if (data.size() < 2 * sizeof(wchar_t) || data.size() % sizeof(wchar_t) != 0) {
        return Status(StatusCode::kDataLoss, "REG_MULTI_SZ is missing its double terminator");
    }
    std::wstring text(data.size() / sizeof(wchar_t), L'\0');
    std::memcpy(text.data(), data.data(), data.size());
    if (text[text.size() - 1] != L'\0' || text[text.size() - 2] != L'\0') {
        return Status(StatusCode::kDataLoss, "REG_MULTI_SZ is missing its double terminator");
    }

    std::vector<std::wstring> strings;
    std::size_t begin = 0;
    for (;;) {
        if (begin == text.size() - 1) {
            return strings;
        }
        const std::size_t end = text.find(L'\0', begin);
        if (end == std::wstring::npos) {
            return Status(StatusCode::kDataLoss, "REG_MULTI_SZ is missing a string terminator");
        }
        if (end == begin) {
            if (begin == text.size() - 2) {
                return strings;
            }
            return Status(StatusCode::kDataLoss, "REG_MULTI_SZ contains an empty element");
        }
        strings.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
}

Result<std::vector<std::uint8_t>> QueryValue(HKEY key, const std::wstring& name, DWORD* type) {
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, name.c_str(), nullptr, type, nullptr, &size);
    if (result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(result, "could not query registry value");
    }

    std::vector<std::uint8_t> data(size);
    for (;;) {
        DWORD actual_size = static_cast<DWORD>(data.size());
        result = RegQueryValueExW(key, name.c_str(), nullptr, type,
                                  data.empty() ? nullptr : data.data(), &actual_size);
        if (result != ERROR_MORE_DATA) {
            if (result != ERROR_SUCCESS) {
                return WindowsRegistryStatus(result, "could not read registry value");
            }
            data.resize(actual_size);
            return data;
        }
        data.resize(actual_size);
    }
}

Result<bool> HasSubkeys(HKEY parent, const std::wstring& subkey, REGSAM view_flags) {
    HKEY key = nullptr;
    const LONG open_result =
        RegOpenKeyExW(parent, subkey.c_str(), 0, KEY_ENUMERATE_SUB_KEYS | view_flags, &key);
    if (open_result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(open_result, "could not open registry key");
    }
    wchar_t name[1] = {L'\0'};
    DWORD length = 0;
    const LONG enum_result =
        RegEnumKeyExW(key, 0, name, &length, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    if (enum_result == ERROR_NO_MORE_ITEMS) {
        return false;
    }
    if (enum_result == ERROR_SUCCESS || enum_result == ERROR_MORE_DATA) {
        return true;
    }
    return WindowsRegistryStatus(enum_result, "could not enumerate registry subkeys");
}

Status DeleteKeyHandle(HKEY parent, const std::wstring& subkey, REGSAM view_flags, bool recursive) {
    if (!recursive) {
        auto has_subkeys = HasSubkeys(parent, subkey, view_flags);
        if (!has_subkeys) {
            return std::move(has_subkeys).status();
        }
        if (has_subkeys.value()) {
            return Status(StatusCode::kFailedPrecondition,
                          "registry key has subkeys and recursive deletion was not requested");
        }
    }
    const LONG result = recursive ? RegDeleteTreeW(parent, subkey.c_str())
                                  : RegDeleteKeyExW(parent, subkey.c_str(), view_flags, 0);
    if (result == ERROR_SUCCESS) {
        return Status::Ok();
    }
    return WindowsRegistryStatus(result, "could not delete registry key");
}

#else

Status UnsupportedRegistry() {
    return Status(StatusCode::kUnimplemented,
                  "Windows registry operations are only available on Windows");
}

#endif

}  // namespace

RegistryKey::RegistryKey(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

RegistryKey::RegistryKey(RegistryKey&& other) noexcept = default;

RegistryKey& RegistryKey::operator=(RegistryKey&& other) noexcept = default;

RegistryKey::~RegistryKey() = default;

Result<RegistryKey> RegistryKey::Open(RegistryHive hive, std::string_view subkey,
                                      RegistryOpenOptions options) {
#ifdef _WIN32
    auto root = NativeHive(hive);
    if (!root) {
        return std::move(root).status();
    }
    auto wide_subkey = RegistryInput(subkey, "registry subkey");
    if (!wide_subkey) {
        return std::move(wide_subkey).status();
    }
    auto access = AccessFlags(options.access);
    if (!access) {
        return std::move(access).status();
    }
    auto view_flags = ViewFlags(options.view);
    if (!view_flags) {
        return std::move(view_flags).status();
    }

    HKEY key = nullptr;
    const LONG result =
        RegOpenKeyExW(root.value(), wide_subkey->empty() ? nullptr : wide_subkey->c_str(), 0,
                      access.value() | view_flags.value(), &key);
    if (result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(result, "could not open registry key");
    }
    return RegistryKey(std::make_unique<State>(key, view_flags.value()));
#else
    static_cast<void>(hive);
    static_cast<void>(subkey);
    static_cast<void>(options);
    return UnsupportedRegistry();
#endif
}

Result<RegistryKey> RegistryKey::Create(RegistryHive hive, std::string_view subkey,
                                        RegistryView view) {
#ifdef _WIN32
    if (subkey.empty()) {
        return Status(StatusCode::kInvalidArgument, "registry subkey must not be empty");
    }
    auto root = NativeHive(hive);
    if (!root) {
        return std::move(root).status();
    }
    auto wide_subkey = RegistryInput(subkey, "registry subkey");
    if (!wide_subkey) {
        return std::move(wide_subkey).status();
    }
    auto view_flags = ViewFlags(view);
    if (!view_flags) {
        return std::move(view_flags).status();
    }

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG result =
        RegCreateKeyExW(root.value(), wide_subkey->c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_READ | KEY_WRITE | view_flags.value(), nullptr, &key, &disposition);
    static_cast<void>(disposition);
    if (result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(result, "could not create registry key");
    }
    return RegistryKey(std::make_unique<State>(key, view_flags.value()));
#else
    static_cast<void>(hive);
    static_cast<void>(subkey);
    static_cast<void>(view);
    return UnsupportedRegistry();
#endif
}

Result<RegistryValue> RegistryKey::GetValue(std::string_view name) const {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    auto wide_name = RegistryInput(name, "registry value name");
    if (!wide_name) {
        return std::move(wide_name).status();
    }

    DWORD type = 0;
    auto data = QueryValue(state_->key, wide_name.value(), &type);
    if (!data) {
        return std::move(data).status();
    }
    switch (type) {
        case REG_SZ:
        case REG_EXPAND_SZ: {
            auto wide_text = DecodeRegistryString(data.value(), "registry string value");
            if (!wide_text) {
                return std::move(wide_text).status();
            }
            auto text = RegistryOutput(wide_text.value(), "registry string value");
            if (!text) {
                return std::move(text).status();
            }
            if (type == REG_EXPAND_SZ) {
                return RegistryExpandString{std::move(text).value()};
            }
            return std::move(text).value();
        }
        case REG_DWORD: {
            if (data->size() != sizeof(std::uint32_t)) {
                return Status(StatusCode::kDataLoss, "REG_DWORD has an invalid size");
            }
            std::uint32_t value = 0;
            std::memcpy(&value, data->data(), sizeof(value));
            return value;
        }
        case REG_QWORD: {
            if (data->size() != sizeof(std::uint64_t)) {
                return Status(StatusCode::kDataLoss, "REG_QWORD has an invalid size");
            }
            std::uint64_t value = 0;
            std::memcpy(&value, data->data(), sizeof(value));
            return value;
        }
        case REG_BINARY:
            return std::move(data).value();
        case REG_MULTI_SZ: {
            auto wide_strings = DecodeRegistryMultiString(data.value());
            if (!wide_strings) {
                return std::move(wide_strings).status();
            }
            std::vector<std::string> strings;
            strings.reserve(wide_strings->size());
            for (const std::wstring& wide_string : wide_strings.value()) {
                auto string = RegistryOutput(wide_string, "REG_MULTI_SZ element");
                if (!string) {
                    return std::move(string).status();
                }
                strings.push_back(std::move(string).value());
            }
            return strings;
        }
        default:
            return Status(StatusCode::kUnimplemented, "registry value type is not supported");
    }
#else
    static_cast<void>(name);
    return UnsupportedRegistry();
#endif
}

Status RegistryKey::SetValue(std::string_view name, const RegistryValue& value) {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    auto wide_name = RegistryInput(name, "registry value name");
    if (!wide_name) {
        return std::move(wide_name).status();
    }

    DWORD type = 0;
    std::vector<std::uint8_t> bytes;
    const auto append_wide = [&bytes](const std::wstring& text) {
        const std::size_t old_size = bytes.size();
        bytes.resize(old_size + text.size() * sizeof(wchar_t));
        std::memcpy(bytes.data() + old_size, text.data(), text.size() * sizeof(wchar_t));
    };

    if (const auto* string = std::get_if<std::string>(&value)) {
        auto wide = RegistryInput(*string, "REG_SZ value");
        if (!wide) {
            return std::move(wide).status();
        }
        wide->push_back(L'\0');
        type = REG_SZ;
        append_wide(wide.value());
    } else if (const auto* string = std::get_if<RegistryExpandString>(&value)) {
        auto wide = RegistryInput(string->value, "REG_EXPAND_SZ value");
        if (!wide) {
            return std::move(wide).status();
        }
        wide->push_back(L'\0');
        type = REG_EXPAND_SZ;
        append_wide(wide.value());
    } else if (const auto* number = std::get_if<std::uint32_t>(&value)) {
        type = REG_DWORD;
        bytes.resize(sizeof(*number));
        std::memcpy(bytes.data(), number, sizeof(*number));
    } else if (const auto* number = std::get_if<std::uint64_t>(&value)) {
        type = REG_QWORD;
        bytes.resize(sizeof(*number));
        std::memcpy(bytes.data(), number, sizeof(*number));
    } else if (const auto* binary = std::get_if<std::vector<std::uint8_t>>(&value)) {
        type = REG_BINARY;
        bytes = *binary;
    } else {
        const auto& strings = std::get<std::vector<std::string>>(value);
        std::wstring encoded;
        for (const std::string& string : strings) {
            auto wide = RegistryInput(string, "REG_MULTI_SZ element");
            if (!wide) {
                return std::move(wide).status();
            }
            encoded.append(wide.value());
            encoded.push_back(L'\0');
        }
        encoded.push_back(L'\0');
        if (strings.empty()) {
            encoded.push_back(L'\0');
        }
        type = REG_MULTI_SZ;
        append_wide(encoded);
    }

    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        return Status(StatusCode::kOutOfRange, "registry value is too large");
    }
    const LONG result =
        RegSetValueExW(state_->key, wide_name->c_str(), 0, type,
                       bytes.empty() ? nullptr : bytes.data(), static_cast<DWORD>(bytes.size()));
    if (result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(result, "could not set registry value");
    }
    return Status::Ok();
#else
    static_cast<void>(name);
    static_cast<void>(value);
    return UnsupportedRegistry();
#endif
}

Status RegistryKey::DeleteValue(std::string_view name) {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    auto wide_name = RegistryInput(name, "registry value name");
    if (!wide_name) {
        return std::move(wide_name).status();
    }
    const LONG result = RegDeleteValueW(state_->key, wide_name->c_str());
    return result == ERROR_SUCCESS
               ? Status::Ok()
               : WindowsRegistryStatus(result, "could not delete registry value");
#else
    static_cast<void>(name);
    return UnsupportedRegistry();
#endif
}

Result<std::vector<std::string>> RegistryKey::ListSubkeys() const {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    std::vector<std::string> names;
    for (DWORD index = 0;; ++index) {
        std::vector<wchar_t> buffer(256, L'\0');
        for (;;) {
            DWORD length = static_cast<DWORD>(buffer.size() - 1);
            const LONG result = RegEnumKeyExW(state_->key, index, buffer.data(), &length, nullptr,
                                              nullptr, nullptr, nullptr);
            if (result == ERROR_NO_MORE_ITEMS) {
                std::sort(names.begin(), names.end(), Utf8ByteLess);
                return names;
            }
            if (result == ERROR_MORE_DATA) {
                buffer.resize(buffer.size() * 2, L'\0');
                continue;
            }
            if (result != ERROR_SUCCESS) {
                return WindowsRegistryStatus(result, "could not enumerate registry subkeys");
            }
            auto name = RegistryOutput(std::wstring_view(buffer.data(), length), "registry subkey");
            if (!name) {
                return std::move(name).status();
            }
            names.push_back(std::move(name).value());
            break;
        }
    }
#else
    return UnsupportedRegistry();
#endif
}

Result<std::vector<std::string>> RegistryKey::ListValueNames() const {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    std::vector<std::string> names;
    for (DWORD index = 0;; ++index) {
        std::vector<wchar_t> buffer(256, L'\0');
        for (;;) {
            DWORD length = static_cast<DWORD>(buffer.size() - 1);
            const LONG result = RegEnumValueW(state_->key, index, buffer.data(), &length, nullptr,
                                              nullptr, nullptr, nullptr);
            if (result == ERROR_NO_MORE_ITEMS) {
                std::sort(names.begin(), names.end(), Utf8ByteLess);
                return names;
            }
            if (result == ERROR_MORE_DATA) {
                buffer.resize(buffer.size() * 2, L'\0');
                continue;
            }
            if (result != ERROR_SUCCESS) {
                return WindowsRegistryStatus(result, "could not enumerate registry values");
            }
            auto name =
                RegistryOutput(std::wstring_view(buffer.data(), length), "registry value name");
            if (!name) {
                return std::move(name).status();
            }
            names.push_back(std::move(name).value());
            break;
        }
    }
#else
    return UnsupportedRegistry();
#endif
}

Status RegistryKey::DeleteSubkey(std::string_view relative_subkey, bool recursive) {
#ifdef _WIN32
    if (!state_ || state_->key == nullptr) {
        return Status(StatusCode::kFailedPrecondition, "registry key has been moved from");
    }
    if (relative_subkey.empty()) {
        return Status(StatusCode::kInvalidArgument, "registry subkey must not be empty");
    }
    auto wide_subkey = RegistryInput(relative_subkey, "registry subkey");
    if (!wide_subkey) {
        return std::move(wide_subkey).status();
    }
    return DeleteKeyHandle(state_->key, wide_subkey.value(), state_->view_flags, recursive);
#else
    static_cast<void>(relative_subkey);
    static_cast<void>(recursive);
    return UnsupportedRegistry();
#endif
}

Status RegistryKey::DeleteKey(RegistryHive hive, std::string_view subkey, bool recursive,
                              RegistryView view) {
#ifdef _WIN32
    if (subkey.empty()) {
        return Status(StatusCode::kInvalidArgument, "registry subkey must not be empty");
    }
    auto root = NativeHive(hive);
    if (!root) {
        return std::move(root).status();
    }
    auto wide_subkey = RegistryInput(subkey, "registry subkey");
    if (!wide_subkey) {
        return std::move(wide_subkey).status();
    }
    auto view_flags = ViewFlags(view);
    if (!view_flags) {
        return std::move(view_flags).status();
    }
    if (!recursive) {
        return DeleteKeyHandle(root.value(), wide_subkey.value(), view_flags.value(), false);
    }

    const std::size_t separator = wide_subkey->find_last_of(L'\\');
    const std::wstring parent_name =
        separator == std::wstring::npos ? std::wstring() : wide_subkey->substr(0, separator);
    const std::wstring child_name =
        separator == std::wstring::npos ? wide_subkey.value() : wide_subkey->substr(separator + 1);
    HKEY parent = nullptr;
    const LONG open_result =
        RegOpenKeyExW(root.value(), parent_name.empty() ? nullptr : parent_name.c_str(), 0,
                      KEY_READ | KEY_WRITE | view_flags.value(), &parent);
    if (open_result != ERROR_SUCCESS) {
        return WindowsRegistryStatus(open_result, "could not open registry key parent");
    }
    Status result = DeleteKeyHandle(parent, child_name, view_flags.value(), true);
    RegCloseKey(parent);
    return result;
#else
    static_cast<void>(hive);
    static_cast<void>(subkey);
    static_cast<void>(recursive);
    static_cast<void>(view);
    return UnsupportedRegistry();
#endif
}

}  // namespace tos
