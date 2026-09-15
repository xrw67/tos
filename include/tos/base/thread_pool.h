#ifndef TOS_BASE_THREAD_POOL_H_
#define TOS_BASE_THREAD_POOL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "tos/base/executor.h"

namespace tos {

/// A thread-safe snapshot of a ThreadPool's queue and cumulative work counts.
struct ThreadPoolStats {
    /// Number of worker threads owned by the pool.
    std::size_t worker_count = 0;

    /// Maximum number of tasks that may wait in the FIFO queue.
    std::size_t queue_capacity = 0;

    /// Tasks accepted by the pool that have not started execution.
    std::size_t queued = 0;

    /// Tasks currently executing on worker threads.
    std::size_t running = 0;

    /// Total tasks accepted by Post since pool construction.
    std::uint64_t accepted = 0;

    /// Total submissions rejected because the pool was full or shut down.
    std::uint64_t rejected = 0;

    /// Total accepted tasks whose execution has finished, including failed Post tasks.
    std::uint64_t completed = 0;
};

/// Returns the worker count a ThreadPool uses when constructed without an explicit worker count:
/// std::thread::hardware_concurrency(), or 1 when the system cannot report a value. This function
/// does not throw.
[[nodiscard]] std::size_t DefaultThreadPoolWorkerCount() noexcept;

/// Fixed-worker executor with a bounded FIFO queue.
///
/// The pool owns its worker threads and accepted tasks. Post, Shutdown, and statistics are safe for
/// concurrent callers. A full queue returns kResourceExhausted; a pool after Shutdown returns
/// kFailedPrecondition. Exceptions from fire-and-forget tasks posted through Post are suppressed;
/// exceptions from Submit tasks remain observable through their futures. Shutdown is idempotent,
/// rejects new work, and drains accepted tasks. A call from one of this pool's workers starts
/// shutdown without waiting for that worker, so an external caller is required to perform the
/// final joins. The destructor performs best-effort draining shutdown and never throws;
/// destruction from a worker detaches the worker handles after requesting drain, so an external
/// owner should normally perform shutdown first.
class ThreadPool final : public Executor {
   public:
    /// Starts worker_count threads with a queue of queue_capacity tasks. Both arguments default to
    /// the documented standard values. A zero worker_count uses DefaultThreadPoolWorkerCount(); a
    /// zero queue_capacity throws std::invalid_argument. Allocation and thread-creation exceptions
    /// propagate.
    explicit ThreadPool(std::size_t worker_count = 0, std::size_t queue_capacity = 1024);
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    ~ThreadPool() noexcept override;

    /// Queues an owned task. Empty tasks return kInvalidArgument; full queues return
    /// kResourceExhausted; a shut down pool returns kFailedPrecondition. Allocation and mutex
    /// exceptions propagate.
    [[nodiscard]] Status Post(Task task) override;

    /// Stops admission and completes every task accepted before shutdown. Repeated calls succeed.
    /// This method does not throw.
    [[nodiscard]] Status Shutdown() noexcept;

    /// Returns a synchronized snapshot of current queue state and cumulative counters. This method
    /// does not throw.
    [[nodiscard]] ThreadPoolStats stats() const noexcept;

   private:
    class State;

    static void WorkerLoop(const std::shared_ptr<State>& state) noexcept;

    std::shared_ptr<State> state_;
    mutable std::mutex workers_mutex_;
    std::vector<std::thread> workers_;
};

}  // namespace tos

#endif  // TOS_BASE_THREAD_POOL_H_
