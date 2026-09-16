#ifndef TOS_APP_EVENT_H_
#define TOS_APP_EVENT_H_

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
/// PublishSync runs in the caller's thread and borrows, rather than queues or copies, the event.
/// Operations may run concurrently, but handlers synchronize their own shared state. Destruction
/// requires caller synchronization.
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

        /// Removes this handler and waits for other-thread callbacks. A callback may reset itself
        /// without waiting. Repeated calls succeed; concurrent mutation needs caller
        /// synchronization. Mutex exceptions propagate.
        [[nodiscard]] Status Reset();

        /// Returns true while this object owns an active or resetting registration. Concurrent
        /// mutation requires caller synchronization.
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

    /// Stops new work and waits for other-thread callbacks. A handler does not wait for its own
    /// publication. Destruction performs best-effort shutdown and never throws.
    ~EventBus() noexcept;

    /// Registers a handler for the exact event type. It borrows Event during PublishSync; a closed
    /// bus returns kFailedPrecondition. Handler and allocation exceptions propagate.
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

    /// Invokes current exact-type handlers in registration order on the caller's thread. The event
    /// must outlive the call. No subscriber returns kNotFound; handler exceptions propagate.
    template <typename Event>
    [[nodiscard]] Status PublishSync(const Event& event) {
        using StoredEvent = std::decay_t<Event>;
        static_assert(std::is_object_v<StoredEvent>, "event type must be an object type");
        return PublishSync(std::type_index(typeid(StoredEvent)), std::addressof(event));
    }

    /// Stops new work and waits for other-thread callbacks. An active publication may finish its
    /// snapshot; repeated calls succeed. Mutex exceptions propagate.
    [[nodiscard]] Status Shutdown();

   private:
    class Impl;

    [[nodiscard]] Result<Subscription> Subscribe(std::type_index type, Callback callback);
    [[nodiscard]] Status PublishSync(std::type_index type, const void* event);
    [[nodiscard]] static Status ResetSubscription(Subscription::Impl& subscription);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_EVENT_H_
