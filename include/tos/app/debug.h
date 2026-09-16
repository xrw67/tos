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
/// args contains the command's whitespace-delimited arguments, excluding the command name.
/// The span and every string within it are borrowed for the duration of the call and must not be
/// retained. output is borrowed for the duration of the call and is owned by Execute's caller.
/// Handlers write their result or expected error text to output; handler and output exceptions
/// propagate.
using DebugHandler = std::function<void(const span<std::string>& args, std::ostream& output)>;

/// Embedded text-command dispatcher.
///
/// DebugController neither opens a listener nor performs terminal I/O. Hosts may adapt Execute to
/// a terminal, GUI, or separately secured transport. Execute, RegisterHandler, and
/// UnregisterHandler are safe concurrently. Every controller provides the built-in `help` command,
/// which lists registered commands and their descriptions. Destroying this controller while another
/// thread uses it requires caller synchronization. Allocation and mutex-locking exceptions
/// propagate.
class DebugController final {
   public:
    /// Creates a command registry with the built-in `help` command. Allocation exceptions
    /// propagate.
    DebugController();
    DebugController(const DebugController&) = delete;
    DebugController& operator=(const DebugController&) = delete;
    DebugController(DebugController&&) = delete;
    DebugController& operator=(DebugController&&) = delete;

    /// Destroys the command registry and never throws. Callers must synchronize destruction with
    /// Execute, RegisterHandler, and UnregisterHandler.
    ~DebugController() noexcept;

    /// Parses and executes one ASCII-whitespace-delimited command line.
    ///
    /// Leading, trailing, and repeated ASCII whitespace are ignored. Quotes and backslashes are
    /// ordinary bytes; no shell quoting or escaping is supported. Empty input or embedded NUL
    /// returns kInvalidArgument. An unknown command returns kNotFound. A failed output stream
    /// returns kUnavailable. Handler and output exceptions propagate. The caller owns output and
    /// must synchronize it when shared by concurrent Execute calls.
    [[nodiscard]] Status Execute(std::string_view command_line, std::ostream& output);

    /// Registers a custom command and its single-line description by exact, case-sensitive name.
    ///
    /// command must be nonempty and contain neither ASCII whitespace nor NUL. description must be
    /// nonempty and contain neither a line break nor NUL. handler must be nonempty. Invalid inputs
    /// return kInvalidArgument; already registered names, including the built-in `help`, return
    /// kAlreadyExists. The controller takes ownership of handler. Handler and registry allocation
    /// exceptions propagate.
    [[nodiscard]] Status RegisterHandler(const std::string& command, const std::string& description,
                                         DebugHandler handler);

    /// Removes a custom command by exact, case-sensitive name.
    ///
    /// Invalid names return kInvalidArgument; a missing command returns kNotFound; `help` returns
    /// kFailedPrecondition because it is built in. Once this method returns, new Execute calls
    /// cannot select the removed handler, but an already selected handler may continue without
    /// waiting. The controller keeps that handler object alive until its in-flight call finishes;
    /// callers remain responsible for synchronizing external state captured by the handler.
    [[nodiscard]] Status UnregisterHandler(std::string_view command);

   private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_DEBUG_H_
