#include "tos/base/environment.h"

#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tos/base/strconv.h"
#include "tos/base/string.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tos {
namespace {

std::mutex& EnvironmentMutex() {
    static std::mutex mutex;
    return mutex;
}

Status ValidateName(std::string_view variable_name) {
    if (variable_name.empty() || variable_name.find('=') != std::string_view::npos ||
        variable_name.find('\0') != std::string_view::npos) {
        return Status(
            StatusCode::kInvalidArgument,
            "environment variable names must be nonempty and contain neither '=' nor NUL");
    }
    return Status::Ok();
}

Status ValidateValue(std::string_view value) {
    if (value.find('\0') != std::string_view::npos) {
        return Status(StatusCode::kInvalidArgument,
                      "environment variable values must not contain NUL bytes");
    }
    return Status::Ok();
}

#ifdef _WIN32
Result<std::wstring> NativeName(std::string_view variable_name) {
    return Utf8ToWide(variable_name);
}

Result<std::wstring> NativeValue(std::string_view value) { return Utf8ToWide(value); }

Result<std::string> GetVarLocked(std::string_view variable_name) {
    auto wide_name = NativeName(variable_name);
    if (!wide_name) {
        return std::move(wide_name).status();
    }

    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(wide_name->c_str(), nullptr, 0);
    if (required == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) {
            return Status(StatusCode::kNotFound,
                          "environment variable '" + std::string(variable_name) + "' is not set");
        }
        return std::string();
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(required));
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableW(wide_name->c_str(), buffer.data(), required);
    if (length == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) {
            return Status(StatusCode::kNotFound,
                          "environment variable '" + std::string(variable_name) + "' is not set");
        }
        return std::string();
    }
    if (length >= required) {
        return Status(StatusCode::kUnavailable,
                      "environment variable changed while its value was being read");
    }
    return WideToUtf8(std::wstring_view(buffer.data(), length));
}
#else
Result<std::string> GetVarLocked(std::string_view variable_name) {
    const std::string name(variable_name);
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return Status(StatusCode::kNotFound, "environment variable '" + name + "' is not set");
    }
    return std::string(value);
}
#endif

}  // namespace

Result<std::string> Environment::GetVar(std::string_view variable_name) {
    Status valid_name = ValidateName(variable_name);
    if (!valid_name) {
        return valid_name;
    }
    std::lock_guard<std::mutex> lock(EnvironmentMutex());
    return GetVarLocked(variable_name);
}

std::string Environment::GetVarOr(std::string_view variable_name, std::string_view default_value) {
    auto value = GetVar(variable_name);
    if (value) {
        return std::move(value).value();
    }
    return std::string(default_value);
}

bool Environment::HasVar(std::string_view variable_name) {
    return static_cast<bool>(GetVar(variable_name));
}

Status Environment::SetVar(std::string_view variable_name, std::string_view value) {
    Status valid_name = ValidateName(variable_name);
    if (!valid_name) {
        return valid_name;
    }
    Status valid_value = ValidateValue(value);
    if (!valid_value) {
        return valid_value;
    }

    std::lock_guard<std::mutex> lock(EnvironmentMutex());
#ifdef _WIN32
    auto wide_name = NativeName(variable_name);
    if (!wide_name) {
        return std::move(wide_name).status();
    }
    auto wide_value = NativeValue(value);
    if (!wide_value) {
        return std::move(wide_value).status();
    }
    if (!SetEnvironmentVariableW(wide_name->c_str(), wide_value->c_str())) {
        return WindowsError(GetLastError(), "could not set environment variable");
    }
#else
    const std::string name(variable_name);
    const std::string new_value(value);
    if (setenv(name.c_str(), new_value.c_str(), 1) != 0) {
        return ErrnoError(errno, "could not set environment variable");
    }
#endif
    return Status::Ok();
}

Status Environment::UnsetVar(std::string_view variable_name) {
    Status valid_name = ValidateName(variable_name);
    if (!valid_name) {
        return valid_name;
    }

    std::lock_guard<std::mutex> lock(EnvironmentMutex());
#ifdef _WIN32
    auto wide_name = NativeName(variable_name);
    if (!wide_name) {
        return std::move(wide_name).status();
    }
    if (!SetEnvironmentVariableW(wide_name->c_str(), nullptr)) {
        return WindowsError(GetLastError(), "could not unset environment variable");
    }
#else
    const std::string name(variable_name);
    if (unsetenv(name.c_str()) != 0) {
        return ErrnoError(errno, "could not unset environment variable");
    }
#endif
    return Status::Ok();
}

std::vector<std::string> Environment::GetPathVar() {
    auto path = GetVar("PATH");
    if (!path) {
        return {};
    }
#ifdef _WIN32
    return StrSplit(path.value(), ";");
#else
    return StrSplit(path.value(), ":");
#endif
}

}  // namespace tos
