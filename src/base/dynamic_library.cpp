#include "tos/base/dynamic_library.h"

#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tos {
namespace {

Status InvalidLibraryPath() {
    return Status(StatusCode::kInvalidArgument,
                  "dynamic library path must be absolute and nonempty");
}

#ifdef _WIN32
Status WindowsLibraryError(DWORD error, std::string_view action) {
    return WindowsError(error, action);
}
#else
Status DynamicLoaderError(StatusCode code, std::string_view action) {
    const char* detail = dlerror();
    std::string message(action);
    if (detail != nullptr) {
        message.append(": ");
        message.append(detail);
    }
    return Status(code, std::move(message));
}
#endif

}  // namespace

Result<DynamicLibrary> DynamicLibrary::Load(const Path& path) {
    if (path.empty() || !path.is_absolute()) {
        return InvalidLibraryPath();
    }

#ifdef _WIN32
    auto wide_path = Utf8ToWide(path.utf8());
    if (!wide_path) {
        return std::move(wide_path).status();
    }
    HMODULE module = LoadLibraryW(wide_path->c_str());
    if (module == nullptr) {
        return WindowsLibraryError(GetLastError(),
                                   "could not load dynamic library '" + path.utf8() + "'");
    }
    return DynamicLibrary(reinterpret_cast<void*>(module));
#else
    dlerror();
    void* handle = dlopen(path.utf8().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return DynamicLoaderError(StatusCode::kUnavailable,
                                  "could not load dynamic library '" + path.utf8() + "'");
    }
    return DynamicLibrary(handle);
#endif
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() { Reset(); }

Status DynamicLibrary::Unload() {
    if (!loaded()) {
        return Status::Ok();
    }

#ifdef _WIN32
    if (!FreeLibrary(reinterpret_cast<HMODULE>(handle_))) {
        return WindowsLibraryError(GetLastError(), "could not unload dynamic library");
    }
#else
    dlerror();
    if (dlclose(handle_) != 0) {
        return DynamicLoaderError(StatusCode::kUnavailable, "could not unload dynamic library");
    }
#endif
    handle_ = nullptr;
    return Status::Ok();
}

void DynamicLibrary::Detach() noexcept { handle_ = nullptr; }

Result<void*> DynamicLibrary::GetSymbolAddress(std::string_view name) const {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        return Status(StatusCode::kInvalidArgument,
                      "dynamic library symbol names must be nonempty and contain no NUL bytes");
    }
    if (!loaded()) {
        return Status(StatusCode::kFailedPrecondition, "dynamic library is not loaded");
    }

    const std::string native_name(name);
#ifdef _WIN32
    FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle_), native_name.c_str());
    if (symbol == nullptr) {
        const DWORD error = GetLastError();
        if (error == ERROR_PROC_NOT_FOUND) {
            return Status(StatusCode::kNotFound,
                          "dynamic library symbol was not found: " + native_name);
        }
        return WindowsLibraryError(
            error, "could not resolve dynamic library symbol '" + native_name + "'");
    }
    return reinterpret_cast<void*>(symbol);
#else
    dlerror();
    void* symbol = dlsym(handle_, native_name.c_str());
    const char* error = dlerror();
    if (error != nullptr) {
        return Status(StatusCode::kNotFound,
                      "dynamic library symbol was not found '" + native_name + "': " + error);
    }
    return symbol;
#endif
}

void DynamicLibrary::Reset() noexcept {
    if (!loaded()) {
        return;
    }
#ifdef _WIN32
    static_cast<void>(FreeLibrary(reinterpret_cast<HMODULE>(handle_)));
#else
    static_cast<void>(dlclose(handle_));
#endif
    handle_ = nullptr;
}

}  // namespace tos
