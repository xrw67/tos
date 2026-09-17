#include "tos/base/scheduler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tos/base/thread_pool.h"

namespace {

using namespace std::chrono_literals;

constexpr tos::Duration Nanos(std::int64_t value) { return tos::Duration::FromNanoseconds(value); }

class RejectingExecutor final : public tos::Executor {
   public:
    tos::Status Post(tos::Task) override {
        return tos::Status(tos::StatusCode::kResourceExhausted, "executor is full");
    }
};

class DispatchObservingExecutor final : public tos::Executor {
   public:
    explicit DispatchObservingExecutor(tos::Executor& executor) : executor_(executor) {}

    tos::Status Post(tos::Task task) override {
        tos::Status admitted = executor_.Post(std::move(task));
        if (admitted) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                submitted_ = true;
            }
            submitted_changed_.notify_all();
        }
        return admitted;
    }

    bool WaitForSubmission(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return submitted_changed_.wait_for(lock, timeout, [this] { return submitted_; });
    }

   private:
    tos::Executor& executor_;
    std::mutex mutex_;
    std::condition_variable submitted_changed_;
    bool submitted_ = false;
};

void WaitForCount(std::mutex& mutex, std::condition_variable& changed, const std::size_t& count,
                  std::size_t expected) {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(changed.wait_for(lock, 1s, [&] { return count >= expected; }));
}

TEST(SchedulerTest, RunsOneShotTasksWhenManualMonotonicTimeReachesDeadline) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 4);
    tos::Scheduler scheduler(pool, clock);
    std::promise<int> completed;
    std::future<int> future = completed.get_future();

    auto scheduled = scheduler.ScheduleAfter(Nanos(10), [&completed] { completed.set_value(42); });
    ASSERT_TRUE(scheduled);
    ASSERT_TRUE(clock.Advance(Nanos(9)));
    EXPECT_EQ(future.wait_for(20ms), std::future_status::timeout);
    ASSERT_TRUE(clock.Advance(Nanos(1)));
    EXPECT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), 42);
    EXPECT_TRUE(scheduler.Shutdown());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, UsesFixedFrequencyAndSkipsOverdueTicks) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 8);
    tos::Scheduler scheduler(pool, clock);
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t count = 0;

    auto scheduled = scheduler.ScheduleEvery(Nanos(10), [&] {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
        changed.notify_all();
    });
    ASSERT_TRUE(scheduled);
    ASSERT_TRUE(clock.Advance(Nanos(10)));
    WaitForCount(mutex, changed, count, 1);
    ASSERT_TRUE(clock.Advance(Nanos(25)));
    WaitForCount(mutex, changed, count, 2);
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_FALSE(changed.wait_for(lock, 20ms, [&] { return count > 2; }));
    }
    ASSERT_TRUE(clock.Advance(Nanos(5)));
    WaitForCount(mutex, changed, count, 3);
    EXPECT_TRUE(std::move(scheduled).value().Cancel());
    EXPECT_TRUE(scheduler.Shutdown());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, ValidatesSchedulingInputsAndRejectsClosedScheduler) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 2);
    tos::Scheduler scheduler(pool, clock);
    EXPECT_EQ(scheduler.ScheduleAfter(Nanos(-1), [] {}).status().code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(scheduler.ScheduleAfter(tos::Duration(), tos::Task()).status().code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(scheduler.ScheduleEvery(tos::Duration(), [] {}).status().code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(scheduler.ScheduleEvery(Nanos(-1), [] {}).status().code(),
              tos::StatusCode::kInvalidArgument);
    ASSERT_TRUE(scheduler.Shutdown());
    EXPECT_EQ(scheduler.ScheduleAfter(tos::Duration(), [] {}).status().code(),
              tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, HandleDestructionAndCancellationPreventFutureDispatch) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 4);
    tos::Scheduler scheduler(pool, clock);
    std::atomic<int> invoked{0};
    {
        auto scheduled = scheduler.ScheduleAfter(Nanos(100), [&] { ++invoked; });
        ASSERT_TRUE(scheduled);
    }
    ASSERT_TRUE(clock.Advance(Nanos(100)));
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(invoked.load(), 0);

    auto scheduled = scheduler.ScheduleEvery(Nanos(10), [&] { ++invoked; });
    ASSERT_TRUE(scheduled);
    auto handle = std::move(scheduled).value();
    EXPECT_FALSE(handle.IsCancelled());
    ASSERT_TRUE(handle.Cancel());
    EXPECT_TRUE(handle.Cancel());
    EXPECT_TRUE(handle.IsCancelled());
    ASSERT_TRUE(clock.Advance(Nanos(100)));
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(invoked.load(), 0);
    EXPECT_TRUE(scheduler.Shutdown());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, CancellationCanWinAfterTaskIsSubmittedButBeforeExecution) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 2);
    DispatchObservingExecutor executor(pool);
    tos::Scheduler scheduler(executor, clock);
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable release;
    bool running = false;
    bool may_finish = false;
    auto blocker = pool.Submit([&] {
        std::unique_lock<std::mutex> lock(mutex);
        running = true;
        ready.notify_one();
        release.wait(lock, [&] { return may_finish; });
    });
    ASSERT_TRUE(blocker);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return running; });
    }

    std::atomic<int> invoked{0};
    auto scheduled = scheduler.ScheduleAfter(tos::Duration(), [&] { ++invoked; });
    ASSERT_TRUE(scheduled);
    auto handle = std::move(scheduled).value();
    EXPECT_TRUE(executor.WaitForSubmission(1s));
    EXPECT_EQ(pool.stats().queued, 1U);
    ASSERT_TRUE(handle.Cancel());
    {
        std::lock_guard<std::mutex> lock(mutex);
        may_finish = true;
    }
    release.notify_one();
    std::move(blocker).value().get();
    EXPECT_EQ(invoked.load(), 0);
    EXPECT_TRUE(scheduler.Shutdown());
    ASSERT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, ShutdownCancelsLongDelayWithoutWaitingForDeadline) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 2);
    tos::Scheduler scheduler(pool, clock);
    std::atomic<bool> invoked{false};
    auto scheduled = scheduler.ScheduleAfter(tos::Hour, [&] { invoked.store(true); });
    ASSERT_TRUE(scheduled);
    EXPECT_TRUE(scheduler.Shutdown());
    ASSERT_TRUE(clock.Advance(tos::Hour));
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(invoked.load());
    EXPECT_TRUE(std::move(scheduled).value().IsCancelled());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(SchedulerTest, ReportsSaturatedAndClosedExecutorAdmission) {
    tos::ManualMonotonicClock clock;
    tos::ThreadPool pool(1, 1);
    tos::Scheduler scheduler(pool, clock);
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable release;
    bool running = false;
    bool may_finish = false;
    auto blocker = pool.Submit([&] {
        std::unique_lock<std::mutex> lock(mutex);
        running = true;
        ready.notify_one();
        release.wait(lock, [&] { return may_finish; });
    });
    ASSERT_TRUE(blocker);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return running; });
    }
    auto queued = pool.Submit([] {});
    ASSERT_TRUE(queued);

    std::promise<tos::Status> error;
    std::future<tos::Status> error_future = error.get_future();
    scheduler.SetErrorHandler([&error](tos::Status status) { error.set_value(std::move(status)); });
    auto scheduled = scheduler.ScheduleAfter(tos::Duration(), [] {});
    ASSERT_TRUE(scheduled);
    EXPECT_EQ(error_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(error_future.get().code(), tos::StatusCode::kResourceExhausted);
    {
        std::lock_guard<std::mutex> lock(mutex);
        may_finish = true;
    }
    release.notify_one();
    std::move(blocker).value().get();
    std::move(queued).value().get();
    EXPECT_TRUE(scheduler.Shutdown());
    EXPECT_TRUE(pool.Shutdown());

    tos::ThreadPool closed_pool(1, 1);
    tos::Scheduler closed_scheduler(closed_pool, clock);
    ASSERT_TRUE(closed_pool.Shutdown());
    std::promise<tos::Status> closed_error;
    std::future<tos::Status> closed_future = closed_error.get_future();
    closed_scheduler.SetErrorHandler(
        [&closed_error](tos::Status status) { closed_error.set_value(std::move(status)); });
    auto closed_task = closed_scheduler.ScheduleAfter(tos::Duration(), [] {});
    ASSERT_TRUE(closed_task);
    EXPECT_EQ(closed_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(closed_future.get().code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(closed_scheduler.Shutdown());
}

TEST(SchedulerTest, TimerThreadCanStartShutdownWithoutDeadlock) {
    tos::ManualMonotonicClock clock;
    RejectingExecutor executor;
    tos::Scheduler scheduler(executor, clock);
    std::promise<bool> stopped;
    std::future<bool> stopped_future = stopped.get_future();
    scheduler.SetErrorHandler(
        [&scheduler, &stopped](tos::Status) { stopped.set_value(scheduler.Shutdown().ok()); });
    auto scheduled = scheduler.ScheduleAfter(tos::Duration(), [] {});
    ASSERT_TRUE(scheduled);
    EXPECT_EQ(stopped_future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(stopped_future.get());
    EXPECT_TRUE(scheduler.Shutdown());
}

}  // namespace
