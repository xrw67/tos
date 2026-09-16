#ifndef TOS_APP_DEBUG_H_
#define TOS_APP_DEBUG_H_

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "tos/base/span.h"
#include "tos/base/status.h"

namespace tos {

namespace debug_detail {

inline Status InvalidDebugCommand(std::string_view detail) {
    return Status(StatusCode::kInvalidArgument,
                  std::string("invalid debug command: ") + std::string(detail));
}

}  // namespace debug_detail

/// A custom embedded debug command handler.
///
/// args and output are borrowed for the call and must not be retained. args excludes the command
/// name; handlers write results or error text. Handler and output exceptions propagate.
using DebugHandler = std::function<void(const span<std::string>& args, std::ostream& output)>;

/// Embedded text-command dispatcher.
///
/// This class performs no I/O; hosts adapt Execute to their transport. Its operations are safe
/// concurrently and `help` lists registered commands. Destruction requires caller synchronization
/// with other operations. Allocation and mutex exceptions propagate.
class DebugController final {
   public:
    /// Creates a registry with the built-in `help` command. Allocation exceptions propagate.
    DebugController();
    DebugController(const DebugController&) = delete;
    DebugController& operator=(const DebugController&) = delete;
    DebugController(DebugController&&) = delete;
    DebugController& operator=(DebugController&&) = delete;

    /// Destroys the registry without throwing. Callers synchronize with other operations.
    ~DebugController() noexcept;

    /// Parses and executes one ASCII-whitespace-delimited command line.
    ///
    /// ASCII whitespace delimits arguments; quotes and backslashes are ordinary bytes. Empty or
    /// NUL input returns kInvalidArgument, unknown commands kNotFound, and failed output
    /// kUnavailable. The caller owns and synchronizes shared output; exceptions propagate.
    [[nodiscard]] Status Execute(std::string_view command_line, std::ostream& output);

    /// Registers a custom command and its single-line description by exact, case-sensitive name.
    ///
    /// command and description must be nonempty single-line, NUL-free text; command cannot contain
    /// ASCII whitespace. Invalid input returns kInvalidArgument and duplicate names, including
    /// `help`, return kAlreadyExists. The controller owns handler; allocation exceptions propagate.
    [[nodiscard]] Status RegisterHandler(const std::string& command, const std::string& description,
                                         DebugHandler handler);

    /// Removes a custom command by exact, case-sensitive name.
    ///
    /// Invalid names return kInvalidArgument, missing commands kNotFound, and `help`
    /// kFailedPrecondition. New calls cannot select a removed handler, but an active call may
    /// finish; callers synchronize captured state. Allocation exceptions propagate.
    [[nodiscard]] Status UnregisterHandler(std::string_view command);

   private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_DEBUG_H_
