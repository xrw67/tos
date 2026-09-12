#ifndef TOS_BASE_PROCESS_H_
#define TOS_BASE_PROCESS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tos/base/filesystem.h"
#include "tos/base/result.h"
#include "tos/base/time.h"

namespace tos {

/// Describes a child process launched without invoking a shell.
///
/// executable is resolved relative to working_directory, when provided, or the parent's current
/// directory. It is never searched through PATH. Environment overrides replace inherited variables;
/// a nullopt value removes a variable. POSIX variable names are case-sensitive; Windows compares
/// ASCII variable names case-insensitively. Allocation exceptions propagate.
struct ProcessOptions {
    Path executable;
    std::vector<std::string> arguments;
    std::optional<Path> working_directory;
    std::map<std::string, std::optional<std::string>> environment_overrides;
};

/// Options for synchronous command execution with captured output.
struct RunCommandOptions {
    /// Maximum captured bytes for each of stdout and stderr. Zero permits only empty output.
    std::size_t max_output_bytes_per_stream = 1024 * 1024;
    /// A positive execution limit. nullopt waits indefinitely.
    std::optional<Duration> timeout;
};

/// The way a child process finished.
///
/// Exactly one member is set for a successful wait. Windows always reports exit_code. POSIX reports
/// terminating_signal when the child was terminated by a signal instead of exiting normally.
struct ProcessExit {
    std::optional<std::uint32_t> exit_code;
    std::optional<int> terminating_signal;
};

/// Captured output and exit information from RunCommand.
struct CommandResult {
    ProcessExit exit;
    std::string stdout_output;
    std::string stderr_output;
};

/// An owning handle to a directly launched child process.
///
/// Start inherits the parent's standard streams. Process is move-only and public operations on one
/// object are not safe to call concurrently; callers must synchronize Wait, Terminate, moves, and
/// destruction. The destructor best-effort terminates and reaps a still-running direct child. It
/// does not terminate descendants. Operational failures return Status; allocation exceptions while
/// preparing launch data or diagnostics propagate.
class [[nodiscard]] Process {
   public:
    /// Starts a process using executable and independent arguments, without a shell or PATH search.
    /// Launch, path, environment, and operating-system failures return Status.
    [[nodiscard]] static Result<Process> Start(const ProcessOptions& options);

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    ~Process();

    /// Waits for completion and returns cached information on later calls. A moved-from Process
    /// returns kFailedPrecondition. This call can block indefinitely.
    [[nodiscard]] Result<ProcessExit> Wait();

    /// Best-effort terminates the direct child and is successful when it has already exited or was
    /// previously terminated. It does not wait; call Wait to reap and obtain the final exit result.
    [[nodiscard]] Status Terminate();

    /// Returns the operating-system process identifier, or zero after this object was moved from.
    [[nodiscard]] std::uint64_t id() const noexcept;

   private:
    struct State;

    explicit Process(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
};

/// Runs a child with stdin closed and captures stdout and stderr independently.
///
/// A nonzero program exit is represented by a successful CommandResult. A non-positive timeout,
/// malformed environment data, or embedded NUL in an argument returns kInvalidArgument. Timeout
/// terminates and reaps the direct child before returning kTimeout; exceeding either output limit
/// does the same before returning kResourceExhausted. The function is safe to call concurrently
/// with other instances, subject to the process-wide environment caveat in ProcessOptions.
/// Allocation exceptions propagate.
[[nodiscard]] Result<CommandResult> RunCommand(const ProcessOptions& process_options,
                                               const RunCommandOptions& options = {});

}  // namespace tos

#endif  // TOS_BASE_PROCESS_H_
