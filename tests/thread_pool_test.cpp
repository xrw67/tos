#include "tos/base/thread_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "test_util.h"

namespace {

using namespace std::chrono_literals;

std::future<void> SubmitBlocker(tos::Executor& executor, std::mutex& mutex,
                                std::condition_variable& ready, std::condition_variable& release,
                                bool& started, bool& may_finish) {
    auto submitted = executor.Submit([&] {
        std::unique_lock<std::mutex> lock(mutex);
        started = true;
        ready.notify_one();
        release.wait(lock, [&] { return may_finish; });
    });
    EXPECT_TRUE(submitted);
    return std::move(submitted).value();
}

TEST(ThreadPoolTest, ExecutesMoveOnlyTasksInFifoOrderAndReportsStatistics) {
    tos::ThreadPool pool(1, 3);
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable release;
    bool started = false;
    bool may_finish = false;
    std::future<void> blocker = SubmitBlocker(pool, mutex, ready, release, started, may_finish);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return started; });
    }

    std::vector<int> observed;
    auto first =
        pool.Submit([state = std::make_unique<int>(1), &observed] { observed.push_back(*state); });
    auto second = pool.Submit([&observed] { observed.push_back(2); });
    auto third = pool.Submit([&observed] { observed.push_back(3); });
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    const tos::ThreadPoolStats queued = pool.stats();
    EXPECT_EQ(queued.worker_count, 1U);
    EXPECT_EQ(queued.queue_capacity, 3U);
    EXPECT_EQ(queued.queued, 3U);
    EXPECT_EQ(queued.running, 1U);
    EXPECT_EQ(queued.accepted, 4U);

    {
        std::lock_guard<std::mutex> lock(mutex);
        may_finish = true;
    }
    release.notify_one();
    blocker.get();
    std::move(first).value().get();
    std::move(second).value().get();
    std::move(third).value().get();
    ASSERT_TRUE(pool.Shutdown());
    EXPECT_EQ(observed, (std::vector<int>{1, 2, 3}));
    const tos::ThreadPoolStats completed = pool.stats();
    EXPECT_EQ(completed.queued, 0U);
    EXPECT_EQ(completed.running, 0U);
    EXPECT_EQ(completed.completed, 4U);
}

TEST(ThreadPoolTest, RejectsEmptyFullAndClosedSubmission) {
    tos::ThreadPool pool(1, 1);
    EXPECT_EQ(pool.Post(tos::Task()).code(), tos::StatusCode::kInvalidArgument);

    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable release;
    bool started = false;
    bool may_finish = false;
    std::future<void> blocker = SubmitBlocker(pool, mutex, ready, release, started, may_finish);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return started; });
    }
    auto queued = pool.Submit([] {});
    ASSERT_TRUE(queued);
    EXPECT_EQ(pool.Submit([] {}).status().code(), tos::StatusCode::kResourceExhausted);
    {
        std::lock_guard<std::mutex> lock(mutex);
        may_finish = true;
    }
    release.notify_one();
    blocker.get();
    std::move(queued).value().get();
    ASSERT_TRUE(pool.Shutdown());
    EXPECT_TRUE(pool.Shutdown());
    EXPECT_EQ(pool.Submit([] {}).status().code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_EQ(pool.stats().rejected, 2U);
}

TEST(ThreadPoolTest, ReturnsValuesVoidAndCallableExceptionsThroughFutures) {
    tos::ThreadPool pool(1, 8);
    auto value = pool.Submit([] { return 42; });
    auto void_value = pool.Submit([] {});
    auto exception = pool.Submit([] { throw std::runtime_error("task failed"); });
    ASSERT_TRUE(value);
    ASSERT_TRUE(void_value);
    ASSERT_TRUE(exception);
    EXPECT_EQ(std::move(value).value().get(), 42);
    std::move(void_value).value().get();
    EXPECT_THROW(std::move(exception).value().get(), std::runtime_error);
    EXPECT_TRUE(pool.Shutdown());
}

TEST(ThreadPoolTest, CooperativeCancellationIsVisibleToQueuedAndRunningTasks) {
    tos::ThreadPool pool(1, 4);
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable release;
    bool started = false;
    bool may_finish = false;
    std::future<void> blocker = SubmitBlocker(pool, mutex, ready, release, started, may_finish);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return started; });
    }

    auto queued = pool.SubmitCancellable(
        [](tos::CancellationToken token) { return token.IsCancellationRequested(); });
    ASSERT_TRUE(queued);
    auto queued_task = std::move(queued).value();
    queued_task.cancellation.Cancel();
    {
        std::lock_guard<std::mutex> lock(mutex);
        may_finish = true;
    }
    release.notify_one();
    blocker.get();
    EXPECT_TRUE(queued_task.future.get());

    std::promise<void> running;
    std::future<void> running_started = running.get_future();
    auto active = pool.SubmitCancellable([&running](tos::CancellationToken token) {
        running.set_value();
        while (!token.IsCancellationRequested()) {
            std::this_thread::yield();
        }
        return token.IsCancellationRequested();
    });
    ASSERT_TRUE(active);
    auto active_task = std::move(active).value();
    running_started.wait();
    active_task.cancellation.Cancel();
    EXPECT_TRUE(active_task.future.get());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(ThreadPoolTest, ShutdownFromWorkerDoesNotDeadlock) {
    tos::ThreadPool pool(1, 2);
    auto submitted = pool.Submit([&pool] { return pool.Shutdown().ok(); });
    ASSERT_TRUE(submitted);
    std::future<bool> future = std::move(submitted).value();
    EXPECT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    EXPECT_TRUE(pool.Shutdown());
}

TEST(ThreadPoolTest, DestructorDrainsAcceptedTasks) {
    std::atomic<bool> finished{false};
    {
        tos::ThreadPool pool(1, 2);
        auto submitted = pool.Submit([&finished] { finished.store(true); });
        ASSERT_TRUE(submitted);
    }
    EXPECT_TRUE(finished.load());
}

TEST(ThreadPoolTest, CancellationCanReleaseDestructorWaitingForAnActiveTask) {
    auto pool = std::make_unique<tos::ThreadPool>(1, 2);
    std::promise<void> running;
    std::future<void> running_started = running.get_future();
    auto submitted = pool->SubmitCancellable([&running](tos::CancellationToken token) {
        running.set_value();
        while (!token.IsCancellationRequested()) {
            std::this_thread::yield();
        }
        return true;
    });
    ASSERT_TRUE(submitted);
    auto task = std::move(submitted).value();
    running_started.wait();

    std::thread destroying([&pool] { pool.reset(); });
    task.cancellation.Cancel();
    destroying.join();
    EXPECT_TRUE(task.future.get());
}

TEST(ThreadPoolTest, UsesDefaultWorkerCountForZeroAndValidatesQueueCapacity) {
    tos::ThreadPool default_worker_pool(0, 1);
    EXPECT_EQ(default_worker_pool.stats().worker_count, tos::DefaultThreadPoolWorkerCount());
    EXPECT_TRUE(default_worker_pool.Shutdown());
    EXPECT_THROW((tos::ThreadPool(1, 0)), std::invalid_argument);
    EXPECT_GE(tos::DefaultThreadPoolWorkerCount(), 1U);
}

TEST(ThreadPoolTest, SupportsConcurrentSubmission) {
    tos::ThreadPool pool(2, 128);
    std::atomic<int> completed{0};
    std::vector<std::thread> submitters;
    for (int index = 0; index < 4; ++index) {
        submitters.emplace_back([&pool, &completed] {
            for (int task = 0; task < 20; ++task) {
                auto submitted = pool.Submit([&completed] { ++completed; });
                EXPECT_TRUE(submitted);
                if (submitted) {
                    std::move(submitted).value().get();
                }
            }
        });
    }
    for (std::thread& submitter : submitters) {
        submitter.join();
    }
    EXPECT_TRUE(pool.Shutdown());
    EXPECT_EQ(completed.load(), 80);
    EXPECT_EQ(pool.stats().completed, 80U);
}

TEST(ThreadPoolTest, ConcurrentSubmissionAndShutdownAccountForAllAcceptedTasks) {
    std::atomic<std::uint64_t> accepted{0}, completed{0}, rejected{0};
    tos::ThreadPool pool(2, 512);
    tos::test::Gate start;
    std::vector<std::thread> threads;
    tos::test::ScopeExit cleanup([&] {
        start.Open();
        for (auto& thread : threads)
            if (thread.joinable()) thread.join();
    });
    for (int index = 0; index < 16; ++index) {
        ASSERT_TRUE(pool.Post(tos::Task([&] { ++completed; })));
        ++accepted;
    }
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([&] {
            start.Wait();
            for (int task = 0; task < 64; ++task) {
                auto status = pool.Post(tos::Task([&] { ++completed; }));
                if (status) {
                    ++accepted;
                } else {
                    EXPECT_EQ(status.code(), tos::StatusCode::kFailedPrecondition);
                    ++rejected;
                }
            }
        });
    for (int index = 0; index < 3; ++index)
        threads.emplace_back([&] {
            start.Wait();
            EXPECT_TRUE(pool.Shutdown());
        });
    start.Open();
    for (auto& thread : threads) thread.join();
    auto stats = pool.stats();
    EXPECT_EQ(completed.load(), accepted.load());
    EXPECT_EQ(stats.accepted, accepted.load());
    EXPECT_EQ(stats.completed, completed.load());
    EXPECT_EQ(stats.rejected, rejected.load());
    EXPECT_EQ(accepted.load() + rejected.load(), 272U);
    EXPECT_EQ(stats.queued, 0U);
    EXPECT_EQ(stats.running, 0U);
    EXPECT_EQ(pool.Post(tos::Task([] {})).code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(pool.Shutdown());
}

TEST(ThreadPoolTest, WorkerInitiatedShutdownStillRequiresExternalCallersToJoinAndDrain) {
    tos::ThreadPool pool(1, 4);
    tos::test::Gate begin_shutdown, release_worker;
    std::promise<void> entered, worker_closed, closers_started;
    auto running = entered.get_future();
    auto closed = worker_closed.get_future();
    auto joining = closers_started.get_future();
    std::promise<tos::Status> results[2];
    std::future<tos::Status> finished[2];
    std::atomic<int> completed{0};
    std::atomic<int> joining_callers{0};
    std::vector<std::thread> closers;
    tos::test::ScopeExit cleanup([&] {
        begin_shutdown.Open();
        release_worker.Open();
        for (auto& thread : closers)
            if (thread.joinable()) thread.join();
        static_cast<void>(pool.Shutdown());
    });
    auto active = pool.Submit([&] {
        entered.set_value();
        begin_shutdown.Wait();
        EXPECT_TRUE(pool.Shutdown());
        worker_closed.set_value();
        release_worker.Wait();
    });
    ASSERT_TRUE(active);
    ASSERT_EQ(running.wait_for(5s), std::future_status::ready);
    ASSERT_TRUE(pool.Post(tos::Task([&] { ++completed; })));
    ASSERT_TRUE(pool.Post(tos::Task([&] { ++completed; })));
    begin_shutdown.Open();
    ASSERT_EQ(closed.wait_for(5s), std::future_status::ready);
    for (int index = 0; index < 2; ++index) {
        finished[index] = results[index].get_future();
        closers.emplace_back([&, index] {
            if (joining_callers.fetch_add(1) == 1) closers_started.set_value();
            results[index].set_value(pool.Shutdown());
        });
    }
    ASSERT_EQ(joining.wait_for(5s), std::future_status::ready);
    for (auto& result : finished) EXPECT_EQ(result.wait_for(50ms), std::future_status::timeout);
    release_worker.Open();
    for (auto& thread : closers) thread.join();
    for (auto& result : finished) EXPECT_TRUE(result.get());
    std::move(active).value().get();
    EXPECT_EQ(completed.load(), 2);
    EXPECT_EQ(pool.stats().completed, 3U);
}

}  // namespace
