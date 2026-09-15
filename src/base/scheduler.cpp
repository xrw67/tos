#include "tos/base/scheduler.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace tos {

namespace {

thread_local const void* current_timer_state = nullptr;

}  // namespace

class Scheduler::Impl {
   public:
    struct Timer {
        Duration deadline;
        Duration interval;
        std::shared_ptr<Task> task;
        std::shared_ptr<std::atomic_bool> cancelled;
        std::size_t sequence;
    };

    struct Later {
        bool operator()(const Timer& left, const Timer& right) const noexcept {
            return left.deadline == right.deadline ? left.sequence > right.sequence
                                                   : left.deadline > right.deadline;
        }
    };

    class State {
       public:
        std::mutex mutex;
        std::condition_variable changed;
        std::condition_variable joined;
        std::priority_queue<Timer, std::vector<Timer>, Later> timers;
        std::function<void(Status)> error_handler;
        std::size_t next_sequence = 1;
        bool accepting = true;
        bool joining = false;
        bool timer_joined = false;
    };

    explicit Impl(Executor& executor_value)
        : state(std::make_shared<State>()),
          executor(executor_value),
          owned_clock(std::make_unique<MonotonicClock>()),
          clock(*owned_clock) {
        Start();
    }

    Impl(Executor& executor_value, IMonotonicClock& clock_value)
        : state(std::make_shared<State>()), executor(executor_value), clock(clock_value) {
        Start();
    }

    ~Impl() noexcept { ShutdownForDestruction(); }

    Result<ScheduledTask> Schedule(Duration delay, Duration interval, Task task) {
        if (!task) {
            return Status(StatusCode::kInvalidArgument, "scheduled task must not be empty");
        }
        if (delay < Duration() || interval < Duration()) {
            return Status(StatusCode::kInvalidArgument,
                          "scheduler delay and interval must not be negative");
        }

        const Duration now = clock.Elapsed();
        Duration deadline;
        if (!CheckedAdd(now, delay, &deadline)) {
            return Status(StatusCode::kInvalidArgument,
                          "scheduled deadline is outside the supported range");
        }

        const auto cancelled = std::make_shared<std::atomic_bool>(false);
        const auto stored_task = std::make_shared<Task>(std::move(task));
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->accepting) {
                return Status(StatusCode::kFailedPrecondition, "scheduler has been shut down");
            }
            state->timers.push(
                Timer{deadline, interval, stored_task, cancelled, state->next_sequence++});
        }
        state->changed.notify_all();
        return ScheduledTask(cancelled, [weak_state = std::weak_ptr<State>(state)] {
            if (const std::shared_ptr<State> locked_state = weak_state.lock()) {
                locked_state->changed.notify_all();
            }
        });
    }

    Status Shutdown() noexcept {
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->accepting = false;
            CancelQueuedTimersLocked(*state);
            if (current_timer_state == state.get()) {
                state->changed.notify_all();
                return Status::Ok();
            }
            if (state->timer_joined) {
                state->changed.notify_all();
                return Status::Ok();
            }
            if (state->joining) {
                state->changed.notify_all();
                state->joined.wait(lock, [&] { return state->timer_joined; });
                return Status::Ok();
            }
            state->joining = true;
        }
        state->changed.notify_all();

        {
            std::lock_guard<std::mutex> lock(timer_thread_mutex);
            if (timer_thread.joinable()) {
                timer_thread.join();
            }
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->timer_joined = true;
            state->joining = false;
        }
        state->joined.notify_all();
        return Status::Ok();
    }

    void ShutdownForDestruction() noexcept {
        if (current_timer_state != state.get()) {
            static_cast<void>(Shutdown());
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->accepting = false;
            CancelQueuedTimersLocked(*state);
        }
        state->changed.notify_all();
        {
            std::lock_guard<std::mutex> lock(timer_thread_mutex);
            if (timer_thread.joinable()) {
                timer_thread.detach();
            }
        }
    }

    void SetErrorHandler(std::function<void(Status)> handler) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error_handler = std::move(handler);
    }

    static bool CheckedAdd(Duration left, Duration right, Duration* output) noexcept {
        const std::int64_t left_value = left.Nanoseconds();
        const std::int64_t right_value = right.Nanoseconds();
        if (right_value > 0 &&
            left_value > std::numeric_limits<std::int64_t>::max() - right_value) {
            return false;
        }
        *output = Duration::FromNanoseconds(left_value + right_value);
        return true;
    }

    static void CancelQueuedTimersLocked(State& state_value) noexcept {
        while (!state_value.timers.empty()) {
            state_value.timers.top().cancelled->store(true, std::memory_order_release);
            state_value.timers.pop();
        }
    }

    static void RemoveCancelledTimersLocked(State& state_value) noexcept {
        while (!state_value.timers.empty() &&
               state_value.timers.top().cancelled->load(std::memory_order_acquire)) {
            state_value.timers.pop();
        }
    }

    static void Report(const std::shared_ptr<State>& state_value, Status status) noexcept {
        try {
            std::function<void(Status)> handler;
            {
                std::lock_guard<std::mutex> lock(state_value->mutex);
                handler = state_value->error_handler;
            }
            if (handler) {
                handler(std::move(status));
            }
        } catch (...) {
        }
    }

    static void ReportInternal(const std::shared_ptr<State>& state_value) noexcept {
        try {
            Report(state_value, Status(StatusCode::kInternal, "scheduler dispatch threw"));
        } catch (...) {
        }
    }

    static void TimerLoop(const std::shared_ptr<State>& state_value, Executor& executor_value,
                          IMonotonicClock& clock_value) noexcept {
        current_timer_state = state_value.get();
        std::unique_lock<std::mutex> lock(state_value->mutex);
        for (;;) {
            RemoveCancelledTimersLocked(*state_value);
            if (!state_value->accepting) {
                current_timer_state = nullptr;
                return;
            }
            if (state_value->timers.empty()) {
                state_value->changed.wait(
                    lock, [&] { return !state_value->accepting || !state_value->timers.empty(); });
                continue;
            }

            const Duration now = clock_value.Elapsed();
            const Timer timer = state_value->timers.top();
            if (timer.deadline > now) {
                const std::int64_t remaining = timer.deadline.Nanoseconds() - now.Nanoseconds();
                state_value->changed.wait_for(lock, std::chrono::nanoseconds(remaining));
                continue;
            }

            state_value->timers.pop();
            bool interval_overflow = false;
            if (timer.interval > Duration() && !timer.cancelled->load(std::memory_order_acquire)) {
                Duration next_deadline = timer.deadline;
                do {
                    if (!CheckedAdd(next_deadline, timer.interval, &next_deadline)) {
                        interval_overflow = true;
                        timer.cancelled->store(true, std::memory_order_release);
                        break;
                    }
                } while (next_deadline <= now);
                if (!interval_overflow) {
                    state_value->timers.push(Timer{next_deadline, timer.interval, timer.task,
                                                   timer.cancelled, state_value->next_sequence++});
                }
            }

            lock.unlock();
            if (interval_overflow) {
                try {
                    Report(state_value,
                           Status(StatusCode::kOutOfRange,
                                  "periodic scheduler deadline is outside the supported range"));
                } catch (...) {
                }
            }
            if (!timer.cancelled->load(std::memory_order_acquire)) {
                try {
                    Status admitted =
                        executor_value.Post(Task([task = timer.task, cancelled = timer.cancelled] {
                            if (!cancelled->load(std::memory_order_acquire)) {
                                (*task)();
                            }
                        }));
                    if (!admitted) {
                        Report(state_value, std::move(admitted));
                    }
                } catch (...) {
                    ReportInternal(state_value);
                }
            }
            lock.lock();
        }
    }

    void Start() {
        clock_subscription = clock.Subscribe([weak_state = std::weak_ptr<State>(state)] {
            if (const std::shared_ptr<State> locked_state = weak_state.lock()) {
                locked_state->changed.notify_all();
            }
        });
        timer_thread =
            std::thread([state_value = state, &executor_value = executor, &clock_value = clock] {
                TimerLoop(state_value, executor_value, clock_value);
            });
    }

    std::shared_ptr<State> state;
    Executor& executor;
    std::unique_ptr<MonotonicClock> owned_clock;
    IMonotonicClock& clock;
    MonotonicClockSubscription clock_subscription;
    std::mutex timer_thread_mutex;
    std::thread timer_thread;
};

Status ScheduledTask::Cancel() noexcept {
    if (!cancelled_) {
        return Status::Ok();
    }
    cancelled_->store(true, std::memory_order_release);
    try {
        if (notify_) {
            notify_();
        }
    } catch (...) {
        // The internally supplied notification is noexcept; suppress a defensive failure here.
    }
    return Status::Ok();
}

Scheduler::Scheduler(Executor& executor) : impl_(std::make_unique<Impl>(executor)) {}

Scheduler::Scheduler(Executor& executor, IMonotonicClock& clock)
    : impl_(std::make_unique<Impl>(executor, clock)) {}

Scheduler::~Scheduler() noexcept = default;

Result<ScheduledTask> Scheduler::ScheduleAfter(Duration delay, Task task) {
    return impl_->Schedule(delay, Duration(), std::move(task));
}

Result<ScheduledTask> Scheduler::ScheduleEvery(Duration interval, Task task) {
    if (interval <= Duration()) {
        return Status(StatusCode::kInvalidArgument, "scheduler interval must be greater than zero");
    }
    return impl_->Schedule(interval, interval, std::move(task));
}

Status Scheduler::Shutdown() noexcept { return impl_->Shutdown(); }

void Scheduler::SetErrorHandler(std::function<void(Status)> handler) {
    impl_->SetErrorHandler(std::move(handler));
}

}  // namespace tos
