#ifndef TOS_BASE_EXECUTOR_H_
#define TOS_BASE_EXECUTOR_H_

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

#include "tos/base/result.h"
#include "tos/base/status.h"

namespace tos {

/// Move-only, type-erased work accepted by an Executor.
///
/// A Task owns its callable. Constructing a non-empty Task may allocate and propagate allocation
/// or callable-construction exceptions. Calling an empty Task violates its precondition.
class Task final {
   public:
    Task() noexcept = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    ~Task() = default;

    template <typename Function, std::enable_if_t<!std::is_same_v<std::decay_t<Function>, Task> &&
                                                      std::is_invocable_v<std::decay_t<Function>&>,
                                                  int> = 0>
    explicit Task(Function&& function)
        : impl_(std::make_unique<Model<std::decay_t<Function>>>(std::forward<Function>(function))) {
    }

    /// Invokes the owned callable. Callable exceptions propagate unchanged.
    void operator()() { impl_->Invoke(); }

    /// Returns true when this object owns a callable.
    [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }

   private:
    class Concept {
       public:
        virtual ~Concept() = default;
        virtual void Invoke() = 0;
    };

    template <typename Function>
    class Model final : public Concept {
       public:
        template <typename Source>
        explicit Model(Source&& function) : function_(std::forward<Source>(function)) {}

        void Invoke() override { std::invoke(function_); }

       private:
        Function function_;
    };

    std::unique_ptr<Concept> impl_;
};

/// Read-only, copyable view of one cooperative cancellation request.
///
/// The token does not own task execution and never interrupts a running thread. It is safe to
/// query concurrently and remains valid after its CancellationSource is moved or destroyed.
class CancellationToken final {
   public:
    CancellationToken() noexcept = default;

    /// Returns whether cancellation has been requested for this token.
    [[nodiscard]] bool IsCancellationRequested() const noexcept {
        return state_ && state_->load(std::memory_order_acquire);
    }

   private:
    friend class CancellationSource;

    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) noexcept
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic_bool> state_;
};

/// Move-only owner used to request cooperative cancellation through its token.
class CancellationSource final {
   public:
    CancellationSource() : state_(std::make_shared<std::atomic_bool>(false)) {}
    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) noexcept = default;
    CancellationSource& operator=(CancellationSource&&) noexcept = default;
    ~CancellationSource() = default;

    /// Requests cancellation. Repeated and concurrent calls succeed and do not throw.
    void Cancel() noexcept {
        if (state_) {
            state_->store(true, std::memory_order_release);
        }
    }

    /// Returns a copyable token that observes this source's cancellation request.
    [[nodiscard]] CancellationToken token() const noexcept { return CancellationToken(state_); }

   private:
    std::shared_ptr<std::atomic_bool> state_;
};

/// A future and the source that can cooperatively cancel its submitted task.
template <typename T>
struct [[nodiscard]] SubmittedTask final {
    std::future<T> future;
    CancellationSource cancellation;
};

/// Abstract asynchronous task executor.
///
/// Implementations own accepted Task objects until execution. Post and the template helpers are
/// safe for concurrent calls when the implementation documents concurrent Post support. Submit
/// reports admission failures through Status; allocation and callable-construction exceptions
/// propagate, and callable exceptions remain observable through the returned future.
class Executor {
   public:
    virtual ~Executor() = default;

    /// Accepts one owned task. Empty tasks return kInvalidArgument; capacity exhaustion and a
    /// closed executor return implementation-defined operational Status values.
    [[nodiscard]] virtual Status Post(Task task) = 0;

    /// Submits a callable and returns its future result. Admission failures are returned as a
    /// Status; exceptions thrown by the callable are stored in the future unchanged.
    template <typename Function,
              typename ResultType = std::invoke_result_t<std::decay_t<Function>&>,
              std::enable_if_t<std::is_invocable_v<std::decay_t<Function>&>, int> = 0>
    [[nodiscard]] Result<std::future<ResultType>> Submit(Function&& function) {
        std::packaged_task<ResultType()> packaged(std::forward<Function>(function));
        std::future<ResultType> future = packaged.get_future();
        Status status = Post(Task([task = std::move(packaged)]() mutable { task(); }));
        if (!status) {
            return status;
        }
        return std::move(future);
    }

    /// Submits a callable that receives a cooperative cancellation token. Cancellation never
    /// preempts execution; the callable must inspect its token. Admission failures are returned
    /// as a Status, while callable exceptions remain observable through the future.
    template <
        typename Function,
        typename ResultType = std::invoke_result_t<std::decay_t<Function>&, CancellationToken>,
        std::enable_if_t<std::is_invocable_v<std::decay_t<Function>&, CancellationToken>, int> = 0>
    [[nodiscard]] Result<SubmittedTask<ResultType>> SubmitCancellable(Function&& function) {
        CancellationSource cancellation;
        const CancellationToken token = cancellation.token();
        std::packaged_task<ResultType()> packaged(
            [callable = std::decay_t<Function>(std::forward<Function>(function)),
             token]() mutable -> ResultType { return std::invoke(callable, token); });
        std::future<ResultType> future = packaged.get_future();
        Status status = Post(Task([task = std::move(packaged)]() mutable { task(); }));
        if (!status) {
            return status;
        }
        return SubmittedTask<ResultType>{std::move(future), std::move(cancellation)};
    }
};

}  // namespace tos

#endif  // TOS_BASE_EXECUTOR_H_
