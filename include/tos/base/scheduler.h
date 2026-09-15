#ifndef TOS_BASE_SCHEDULER_H_
#define TOS_BASE_SCHEDULER_H_

#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "tos/base/executor.h"
#include "tos/base/result.h"
#include "tos/base/time.h"

namespace tos {

/// Move-only RAII control for one scheduled task.
///
/// Destroying this handle or calling Cancel() prevents future timer selections. A callback already
/// selected by Scheduler or submitted to its Executor may still run once. Concurrent operations on
/// the same handle require caller synchronization; cancellation itself is safe to race with timer
/// selection and task execution.
class ScheduledTask final {
   public:
    ScheduledTask() noexcept = default;
    ScheduledTask(const ScheduledTask&) = delete;
    ScheduledTask& operator=(const ScheduledTask&) = delete;

    ScheduledTask(ScheduledTask&& other) noexcept
        : cancelled_(std::move(other.cancelled_)), notify_(std::move(other.notify_)) {}

    ScheduledTask& operator=(ScheduledTask&& other) noexcept {
        if (this != &other && CancelNoexcept()) {
            cancelled_ = std::move(other.cancelled_);
            notify_ = std::move(other.notify_);
        }
        return *this;
    }

    ~ScheduledTask() noexcept { static_cast<void>(CancelNoexcept()); }

    /// Cancels future selections and wakes the timer thread. Repeated calls succeed. The scheduler
    /// may already have selected or submitted one final callback. This operation does not throw.
    [[nodiscard]] Status Cancel() noexcept;

    /// Returns true after cancellation or for an empty handle. A callback already selected when
    /// its Scheduler ends may remain uncancelled while it races with executor submission.
    [[nodiscard]] bool IsCancelled() const noexcept {
        return !cancelled_ || cancelled_->load(std::memory_order_acquire);
    }

   private:
    friend class Scheduler;

    ScheduledTask(std::shared_ptr<std::atomic_bool> cancelled,
                  std::function<void()> notify) noexcept
        : cancelled_(std::move(cancelled)), notify_(std::move(notify)) {}

    bool CancelNoexcept() noexcept { return Cancel().ok(); }

    std::shared_ptr<std::atomic_bool> cancelled_;
    std::function<void()> notify_;
};

/// Interface for scheduling future execution through a separate Executor.
///
/// Implementations own accepted timers, while ScheduledTask owns cancellation control. Scheduling
/// calls are safe for concurrent callers when the implementation documents it. Callers must retain
/// the returned ScheduledTask for as long as the task should remain scheduled.
class ScheduledExecutor {
   public:
    virtual ~ScheduledExecutor() = default;

    /// Schedules one task after a nonnegative monotonic delay. Empty tasks and negative delays
    /// return kInvalidArgument; a closed scheduler returns kFailedPrecondition. Task construction,
    /// queue allocation, and clock subscription exceptions propagate.
    [[nodiscard]] virtual Result<ScheduledTask> ScheduleAfter(Duration delay, Task task) = 0;

    /// Schedules a task at fixed monotonic frequency. The first execution occurs after interval;
    /// nonpositive intervals and empty tasks return kInvalidArgument. Overdue ticks are skipped,
    /// and callbacks may overlap on the target Executor.
    [[nodiscard]] virtual Result<ScheduledTask> ScheduleEvery(Duration interval, Task task) = 0;

    /// Convenience overload that stores an invocable callable as a move-only Task.
    template <typename Function, std::enable_if_t<!std::is_same_v<std::decay_t<Function>, Task> &&
                                                      std::is_invocable_v<std::decay_t<Function>&>,
                                                  int> = 0>
    [[nodiscard]] Result<ScheduledTask> ScheduleAfter(Duration delay, Function&& function) {
        return ScheduleAfter(delay, Task(std::forward<Function>(function)));
    }

    /// Convenience overload that stores an invocable callable as a move-only Task.
    template <typename Function, std::enable_if_t<!std::is_same_v<std::decay_t<Function>, Task> &&
                                                      std::is_invocable_v<std::decay_t<Function>&>,
                                                  int> = 0>
    [[nodiscard]] Result<ScheduledTask> ScheduleEvery(Duration interval, Function&& function) {
        return ScheduleEvery(interval, Task(std::forward<Function>(function)));
    }
};

/// One-thread monotonic timer queue that dispatches due callbacks through a borrowed Executor.
///
/// Scheduler owns one timer thread and all accepted timers but never owns, shuts down, or keeps
/// alive the supplied Executor or IMonotonicClock; both must outlive it. Scheduling, cancellation,
/// error-handler replacement, and shutdown are safe for concurrent calls. Fixed-frequency timers
/// skip overdue deadlines rather than replaying them, though target Executor workers may overlap
/// callback invocations. The destructor performs best-effort shutdown and never throws.
class Scheduler final : public ScheduledExecutor {
   public:
    using ScheduledExecutor::ScheduleAfter;
    using ScheduledExecutor::ScheduleEvery;

    /// Uses an owned system MonotonicClock and borrows executor. Thread creation and allocation
    /// exceptions propagate.
    explicit Scheduler(Executor& executor);

    /// Borrows executor and clock. The caller must keep both alive until this Scheduler is shut
    /// down or destroyed. Thread creation, subscription, and allocation exceptions propagate.
    Scheduler(Executor& executor, IMonotonicClock& clock);
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;
    ~Scheduler() noexcept override;

    [[nodiscard]] Result<ScheduledTask> ScheduleAfter(Duration delay, Task task) override;
    [[nodiscard]] Result<ScheduledTask> ScheduleEvery(Duration interval, Task task) override;

    /// Stops new scheduling, cancels unselected timers, and stops the timer thread. It does not
    /// stop the borrowed Executor or cancel callbacks already submitted to it. Repeated calls
    /// succeed. A timer-thread call starts shutdown without waiting for itself.
    [[nodiscard]] Status Shutdown() noexcept;

    /// Installs a handler for executor admission failures of due callbacks. The handler is called
    /// outside Scheduler locks and its exceptions are suppressed. Handler assignment and allocation
    /// exceptions propagate.
    void SetErrorHandler(std::function<void(Status)> handler);

   private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_BASE_SCHEDULER_H_
