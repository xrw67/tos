#include "tos/base/system.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>

#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace tos {
namespace {

std::size_t FallbackCpuCount() noexcept {
    const std::size_t count = std::thread::hardware_concurrency();
    return count == 0 ? 1 : count;
}

#ifdef _WIN32
std::size_t CountBits(std::uintptr_t value) noexcept {
    std::size_t count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

Result<Path> PathFromWide(std::wstring_view path) {
    auto utf8 = WideToUtf8(path);
    if (!utf8) {
        return std::move(utf8).status();
    }
    return Path::Parse(utf8.value());
}
#endif

}  // namespace

std::uint64_t System::CurrentProcessId() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#elif defined(__APPLE__) || defined(__linux__)
    return static_cast<std::uint64_t>(::getpid());
#else
    return 0;
#endif
}

std::uint64_t System::CurrentThreadId() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentThreadId());
#elif defined(__APPLE__)
    std::uint64_t thread_id = 0;
    return pthread_threadid_np(nullptr, &thread_id) == 0 ? thread_id : 0;
#elif defined(__linux__)
    const long thread_id = ::syscall(SYS_gettid);
    return thread_id > 0 ? static_cast<std::uint64_t>(thread_id) : 0;
#else
    return 0;
#endif
}

Result<std::string> System::GetHostName() {
#ifdef _WIN32
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> host_name{};
    DWORD length = static_cast<DWORD>(host_name.size());
    if (GetComputerNameW(host_name.data(), &length) == 0) {
        return WindowsError(GetLastError(), "GetComputerNameW");
    }
    return WideToUtf8(std::wstring_view(host_name.data(), length));
#elif defined(__APPLE__) || defined(__linux__)
    std::array<char, 256> host_name{};
    if (::gethostname(host_name.data(), host_name.size() - 1) != 0) {
        return ErrnoError(errno, "gethostname");
    }
    host_name.back() = '\0';
    return std::string(host_name.data());
#else
    return Status(StatusCode::kUnimplemented, "host name is unsupported on this platform");
#endif
}

Result<Path> System::CurrentProcessPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(260);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return WindowsError(GetLastError(), "could not query current process path");
        }
        if (length < buffer.size() - 1) {
            return PathFromWide(std::wstring_view(buffer.data(), length));
        }
        if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max() / 2)) {
            return Status(StatusCode::kOutOfRange, "current process path is too long");
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t required = 0;
    static_cast<void>(_NSGetExecutablePath(nullptr, &required));
    if (required == 0) {
        return Status(StatusCode::kUnavailable, "could not determine current process path size");
    }
    std::vector<char> buffer(required);
    if (_NSGetExecutablePath(buffer.data(), &required) != 0) {
        return Status(StatusCode::kUnavailable, "could not query current process path");
    }
    return Path::Parse(buffer.data());
#elif defined(__linux__)
    std::vector<char> buffer(256);
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            return ErrnoError(errno, "could not query current process path");
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return Path::Parse(std::string_view(buffer.data(), static_cast<std::size_t>(length)));
        }
        if (buffer.size() > std::numeric_limits<std::size_t>::max() / 2) {
            return Status(StatusCode::kOutOfRange, "current process path is too long");
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return Status(StatusCode::kUnimplemented,
                  "current process path is unsupported on this platform");
#endif
}

Result<Path> System::CurrentProcessDirectory() {
    auto path = CurrentProcessPath();
    if (!path) {
        return std::move(path).status();
    }
    return path->parent_path();
}

Result<Path> System::CurrentWorkingDirectory() {
#ifdef _WIN32
    const DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0) {
        return WindowsError(GetLastError(), "could not query current working directory");
    }
    std::vector<wchar_t> buffer(required);
    const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0) {
        return WindowsError(GetLastError(), "could not query current working directory");
    }
    if (length >= buffer.size()) {
        return Status(StatusCode::kUnavailable,
                      "current working directory changed while it was being read");
    }
    auto path = PathFromWide(std::wstring_view(buffer.data(), length));
#elif defined(__APPLE__) || defined(__linux__)
    std::vector<char> buffer(256);
    for (;;) {
        if (::getcwd(buffer.data(), buffer.size()) != nullptr) {
            auto path = Path::Parse(buffer.data());
            if (!path) {
                return std::move(path).status();
            }
            if (!path->is_absolute()) {
                return Status(StatusCode::kUnavailable,
                              "current working directory is not absolute");
            }
            return path;
        }
        if (errno != ERANGE) {
            return ErrnoError(errno, "could not query current working directory");
        }
        if (buffer.size() > std::numeric_limits<std::size_t>::max() / 2) {
            return Status(StatusCode::kOutOfRange, "current working directory is too long");
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return Status(StatusCode::kUnimplemented,
                  "current working directory is unsupported on this platform");
#endif

#ifdef _WIN32
    if (!path) {
        return std::move(path).status();
    }
    if (!path->is_absolute()) {
        return Status(StatusCode::kUnavailable, "current working directory is not absolute");
    }
    return path;
#endif
}

std::size_t System::NumberOfProcessors() noexcept {
#ifdef _WIN32
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) &&
        process_mask != 0) {
        return CountBits(static_cast<std::uintptr_t>(process_mask));
    }
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count == 0 ? FallbackCpuCount() : static_cast<std::size_t>(count);
#elif defined(__linux__)
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    if (::sched_getaffinity(0, sizeof(cpus), &cpus) == 0) {
        const int count = CPU_COUNT(&cpus);
        if (count > 0) {
            return static_cast<std::size_t>(count);
        }
    }
    return FallbackCpuCount();
#elif defined(__APPLE__)
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<std::size_t>(count) : FallbackCpuCount();
#else
    return FallbackCpuCount();
#endif
}

void System::Sleep(std::uint64_t milliseconds) noexcept {
    using Milliseconds = std::chrono::milliseconds;
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<Milliseconds::rep>::max());
    const Milliseconds duration(milliseconds > maximum
                                    ? std::numeric_limits<Milliseconds::rep>::max()
                                    : static_cast<Milliseconds::rep>(milliseconds));
    std::this_thread::sleep_for(duration);
}

}  // namespace tos
