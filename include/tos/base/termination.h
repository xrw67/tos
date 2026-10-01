#ifndef TOS_BASE_TERMINATION_H_
#define TOS_BASE_TERMINATION_H_

#include <chrono>
#include <memory>

#include "tos/base/result.h"

namespace tos {
namespace detail {
struct TerminationState;
}

enum class TerminationReason {
    kInterrupt,
    kTerminate,
    kConsoleClose,
    kExplicitRequest,
};

/// Converts SIGINT/SIGTERM (POSIX) and console controls (Windows) into a latched event.
/// The first request wins until Uninstall or the next Install. Concurrent Wait, RequestStop,
/// Requested, reason and IsInstalled calls are safe. Install/Uninstall/destruction require
/// external synchronization with all other calls on this object. Only one installed controller
/// is allowed per process. POSIX restores previous handlers; Windows removes this handler.
/// No user code runs in signal/control handlers. Windows close/shutdown controls have OS-imposed
/// exit deadlines; this adapter cannot guarantee completion of graceful shutdown or host services.
/// Operational failures return Status/Result; allocation exceptions propagate. Destruction never
/// throws and performs best-effort handler restoration.
class TerminationController final {
   public:
    TerminationController();
    TerminationController(const TerminationController&) = delete;
    TerminationController& operator=(const TerminationController&) = delete;
    TerminationController(TerminationController&&) = delete;
    TerminationController& operator=(TerminationController&&) = delete;
    ~TerminationController();

    [[nodiscard]] Status Install();
    /// Waits for a latched request, using a monotonic timeout. Zero polls, negative values return
    /// kInvalidArgument, and milliseconds::max waits indefinitely. kTimeout means no request.
    [[nodiscard]] Result<TerminationReason> Wait(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    void RequestStop(TerminationReason reason = TerminationReason::kExplicitRequest) noexcept;
    void Uninstall() noexcept;
    [[nodiscard]] bool IsInstalled() const noexcept;
    [[nodiscard]] bool Requested() const noexcept;
    /// Returns the latched reason, or kExplicitRequest when no request has been observed.
    [[nodiscard]] TerminationReason reason() const noexcept;

   private:
    std::unique_ptr<detail::TerminationState> state_;
};

}  // namespace tos

#endif  // TOS_BASE_TERMINATION_H_
