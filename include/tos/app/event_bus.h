#ifndef TOS_APP_EVENT_BUS_H_
#define TOS_APP_EVENT_BUS_H_

#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "tos/base/result.h"
#include "tos/base/status.h"

namespace tos {

/// Thread-safe, type-indexed synchronous event dispatcher.
///
/// PublishSync dispatches in its caller's thread and only borrows the event for that call; it
/// neither queues nor copies events. Subscription, publishing, resetting, and shutdown may run
/// concurrently. Handlers must synchronize any shared state they access. Destroying an EventBus
/// while another thread calls one of its members requires caller synchronization.
class EventBus {
   private:
    class Entry;
    class State;
    using Callback = std::function<void(const void*)>;

   public:
    /// Move-only RAII registration for one event handler.
    class Subscription {
       public:
        Subscription() noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        ~Subscription() noexcept;

        /// Removes this handler and waits for callbacks running on other threads. Reset from this
        /// handler's own callback does not wait for itself. Repeated calls succeed. Concurrent
        /// Reset or move operations on this same Subscription require caller synchronization.
        /// Mutex exceptions propagate.
        [[nodiscard]] Status Reset();

        /// Returns true while this object still owns an active or resetting registration. Reading
        /// while another thread modifies this same Subscription requires caller synchronization.
        [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }

       private:
        class Impl;

        friend class EventBus;
        explicit Subscription(std::unique_ptr<Impl> impl) noexcept;

        bool ResetNoexcept() noexcept;

        std::unique_ptr<Impl> impl_;
    };

    /// Creates an accepting EventBus. Allocation exceptions propagate.
    EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /// Stops new subscriptions and publications, then waits for callbacks running on other
    /// threads. A call from a handler does not wait for its own publication. It is idempotent.
    /// Mutex exceptions propagate; destruction performs a best-effort shutdown and never throws.
    ~EventBus() noexcept;

    /// Registers handler for the exact unqualified event type. The handler receives a borrowed
    /// const Event reference only during PublishSync. A closed bus returns kFailedPrecondition.
    /// Handler and implementation allocations may throw.
    template <typename Event, typename Handler>
    [[nodiscard]] Result<Subscription> Subscribe(Handler&& handler) {
        using StoredEvent = std::decay_t<Event>;
        using StoredHandler = std::decay_t<Handler>;
        static_assert(std::is_object_v<StoredEvent>, "event type must be an object type");
        static_assert(std::is_copy_constructible_v<StoredHandler>,
                      "event handlers must be copy constructible");
        static_assert(std::is_invocable_v<StoredHandler&, const StoredEvent&>,
                      "event handler must accept const Event&");

        Callback callback = [stored_handler =
                                 std::forward<Handler>(handler)](const void* event) mutable {
            stored_handler(*static_cast<const StoredEvent*>(event));
        };
        return Subscribe(std::type_index(typeid(StoredEvent)), std::move(callback));
    }

    /// Invokes current handlers for the exact event type in registration order on the calling
    /// thread. The event is borrowed and must outlive the call. No subscriber returns kNotFound;
    /// a closed bus returns kFailedPrecondition. Handler exceptions propagate unchanged.
    template <typename Event>
    [[nodiscard]] Status PublishSync(const Event& event) {
        using StoredEvent = std::decay_t<Event>;
        static_assert(std::is_object_v<StoredEvent>, "event type must be an object type");
        return PublishSync(std::type_index(typeid(StoredEvent)), std::addressof(event));
    }

    /// Stops new subscriptions and publications and waits for callbacks running on other threads.
    /// A publication already in progress may finish its snapshot. Repeated calls succeed. Mutex
    /// exceptions propagate.
    [[nodiscard]] Status Shutdown();

   private:
    class Impl;

    [[nodiscard]] Result<Subscription> Subscribe(std::type_index type, Callback callback);
    [[nodiscard]] Status PublishSync(std::type_index type, const void* event);
    [[nodiscard]] static Status ResetSubscription(Subscription::Impl& subscription);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_EVENT_BUS_H_
