#include "tos/base/thread_pool.h"

#include <condition_variable>
#include <deque>
#include <stdexcept>
#include <utility>

namespace tos {

class ThreadPool::State {
   public:
    State(std::size_t worker_count_value, std::size_t queue_capacity_value)
        : worker_count(worker_count_value), queue_capacity(queue_capacity_value) {}

    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable joined;
    std::deque<Task> tasks;
    const std::size_t worker_count;
    const std::size_t queue_capacity;
    std::size_t running = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t completed = 0;
    bool accepting = true;
    bool joining = false;
    bool workers_joined = false;
};

namespace {

thread_local const void* current_worker_state = nullptr;

}  // namespace

std::size_t DefaultThreadPoolWorkerCount() noexcept {
    const std::size_t worker_count = std::thread::hardware_concurrency();
    return worker_count == 0 ? 1 : worker_count;
}

ThreadPool::ThreadPool(std::size_t worker_count, std::size_t queue_capacity)
    : state_(std::make_shared<State>(
          worker_count == 0 ? DefaultThreadPoolWorkerCount() : worker_count, queue_capacity)) {
    if (queue_capacity == 0) {
        throw std::invalid_argument("ThreadPool queue_capacity must be nonzero");
    }

    workers_.reserve(state_->worker_count);
    try {
        for (std::size_t index = 0; index < state_->worker_count; ++index) {
            workers_.emplace_back([state = state_] { WorkerLoop(state); });
        }
    } catch (...) {
        static_cast<void>(Shutdown());
        throw;
    }
}

ThreadPool::~ThreadPool() noexcept {
    if (current_worker_state != state_.get()) {
        static_cast<void>(Shutdown());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->accepting = false;
    }
    state_->ready.notify_all();

    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers.swap(workers_);
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.detach();
        }
    }
}

Status ThreadPool::Post(Task task) {
    if (!task) {
        return Status(StatusCode::kInvalidArgument, "task must not be empty");
    }

    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting) {
            ++state_->rejected;
            return Status(StatusCode::kFailedPrecondition, "thread pool has been shut down");
        }
        if (state_->tasks.size() >= state_->queue_capacity) {
            ++state_->rejected;
            return Status(StatusCode::kResourceExhausted, "thread pool queue is full");
        }
        state_->tasks.emplace_back(std::move(task));
        ++state_->accepted;
    }
    state_->ready.notify_one();
    return Status::Ok();
}

Status ThreadPool::Shutdown() noexcept {
    bool joins_workers = false;
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->accepting = false;
        if (current_worker_state == state_.get()) {
            state_->ready.notify_all();
            return Status::Ok();
        }
        if (state_->workers_joined) {
            state_->ready.notify_all();
            return Status::Ok();
        }
        if (state_->joining) {
            state_->ready.notify_all();
            state_->joined.wait(lock, [&] { return state_->workers_joined; });
            return Status::Ok();
        }
        state_->joining = true;
        joins_workers = true;
    }
    state_->ready.notify_all();

    std::vector<std::thread> workers;
    if (joins_workers) {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers.swap(workers_);
    }
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->workers_joined = true;
        state_->joining = false;
    }
    state_->joined.notify_all();
    return Status::Ok();
}

ThreadPoolStats ThreadPool::stats() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return ThreadPoolStats{state_->worker_count, state_->queue_capacity, state_->tasks.size(),
                           state_->running,      state_->accepted,       state_->rejected,
                           state_->completed};
}

void ThreadPool::WorkerLoop(const std::shared_ptr<State>& state) noexcept {
    current_worker_state = state.get();
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->ready.wait(lock, [&] { return !state->accepting || !state->tasks.empty(); });
            if (!state->accepting && state->tasks.empty()) {
                current_worker_state = nullptr;
                return;
            }
            task = std::move(state->tasks.front());
            state->tasks.pop_front();
            ++state->running;
        }

        try {
            task();
        } catch (...) {
            // A fire-and-forget task must not terminate its worker.
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            --state->running;
            ++state->completed;
        }
    }
}

}  // namespace tos
