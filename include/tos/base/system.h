#ifndef TOS_BASE_SYSTEM_H_
#define TOS_BASE_SYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "tos/base/filesystem.h"
#include "tos/base/result.h"

namespace tos {

/// Process and host information helpers for the current platform.
///
/// All functions retain no state and are safe to call concurrently. OperatingSystem, process and
/// thread identifiers, and NumberOfProcessors do not allocate or throw. Process path and directory
/// lookups return native-query failures through Result<Path>; conversion and Path construction
/// allocation exceptions propagate. CurrentProcessDirectory is the parent directory of the running
/// executable, while CurrentWorkingDirectory returns the process working directory.
class System final {
   public:
    /// Returns the normalized build target name: "windows", "macos", "linux", or "unknown".
    [[nodiscard]] static constexpr std::string_view OperatingSystem() noexcept {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown";
#endif
    }

    /// Returns the normalized build target architecture: "x86", "x64", "arm64", or "unknown".
    [[nodiscard]] static constexpr std::string_view Architecture() noexcept {
#if defined(_M_IX86) || defined(__i386__)
        return "x86";
#elif defined(_M_X64) || defined(__x86_64__)
        return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
        return "arm64";
#else
        return "unknown";
#endif
    }

    /// Returns the native identifier of the current process, or zero if unavailable.
    [[nodiscard]] static std::uint64_t CurrentProcessId() noexcept;

    /// Returns the native identifier of the calling thread, or zero if unavailable. The value is
    /// platform-specific and is suitable only for identity comparison and diagnostics.
    [[nodiscard]] static std::uint64_t CurrentThreadId() noexcept;

    /// Returns the current host name as UTF-8. Unsupported platforms return kUnimplemented;
    /// native query and character-conversion failures return a classified Status.
    [[nodiscard]] static Result<std::string> GetHostName();

    /// Returns the absolute UTF-8 path of the currently running executable. Unsupported platforms
    /// return kUnimplemented; native query failures return a classified Status.
    [[nodiscard]] static Result<Path> CurrentProcessPath();

    /// Returns the absolute UTF-8 directory containing the currently running executable. Its
    /// failure and exception behavior matches CurrentProcessPath().
    [[nodiscard]] static Result<Path> CurrentProcessDirectory();

    /// Returns the absolute UTF-8 working directory of the current process. Its failure and
    /// exception behavior matches CurrentProcessPath().
    [[nodiscard]] static Result<Path> CurrentWorkingDirectory();

    /// Returns CPUs available to the current process, never less than one. Linux honors the
    /// process CPU-affinity mask when available; other platforms use their native online-CPU query.
    [[nodiscard]] static std::size_t NumberOfProcessors() noexcept;

    /// Blocks the calling thread for at least milliseconds. Zero returns immediately; operating
    /// system scheduling may make the actual wait longer. This function does not allocate or throw.
    static void Sleep(std::uint64_t milliseconds) noexcept;
};

}  // namespace tos

#endif  // TOS_BASE_SYSTEM_H_
