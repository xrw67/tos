#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "tos/app/event.h"

namespace tos {

class EventBus::Entry {
   public:
    explicit Entry(Callback entry_callback) : callback(std::move(entry_callback)) {}

    Callback callback;
    std::mutex mutex;
    std::condition_variable idle;
    std::size_t in_flight = 0;
    bool active = true;
};

class EventBus::State {
   public:
    std::mutex mutex;
    std::condition_variable idle;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<Entry>>> handlers;
    std::size_t active_publications = 0;
    bool accepting = true;
};

class EventBus::Subscription::Impl {
   public:
    Impl(std::weak_ptr<State> state_value, std::type_index type_value,
         std::shared_ptr<Entry> entry_value)
        : state(std::move(state_value)), type(type_value), entry(std::move(entry_value)) {}

    std::weak_ptr<State> state;
    std::type_index type;
    std::shared_ptr<Entry> entry;
};

class EventBus::Impl {
   public:
    Impl() : state(std::make_shared<State>()) {}

    std::shared_ptr<State> state;
};

namespace {

thread_local std::vector<const void*> current_states;
thread_local std::vector<const void*> current_entries;

bool IsCurrentEntry(const void* entry) {
    return std::find(current_entries.begin(), current_entries.end(), entry) !=
           current_entries.end();
}

std::size_t CurrentStateCount(const void* state) {
    return static_cast<std::size_t>(
        std::count(current_states.begin(), current_states.end(), state));
}

}  // namespace

EventBus::Subscription::Subscription(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

EventBus::Subscription::Subscription() noexcept = default;

EventBus::Subscription::Subscription(Subscription&& other) noexcept
    : impl_(std::move(other.impl_)) {}

EventBus::Subscription& EventBus::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other && ResetNoexcept()) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

EventBus::Subscription::~Subscription() noexcept { static_cast<void>(ResetNoexcept()); }

Status EventBus::Subscription::Reset() {
    if (!impl_) {
        return Status::Ok();
    }
    Status result = EventBus::ResetSubscription(*impl_);
    if (result.ok()) {
        impl_.reset();
    }
    return result;
}

bool EventBus::Subscription::ResetNoexcept() noexcept {
    try {
        return Reset().ok();
    } catch (...) {
        return false;
    }
}

EventBus::EventBus() : impl_(std::make_unique<Impl>()) {}

EventBus::~EventBus() noexcept {
    try {
        static_cast<void>(Shutdown());
    } catch (...) {
    }
}

Result<EventBus::Subscription> EventBus::Subscribe(std::type_index type, Callback callback) {
    const std::shared_ptr<State> state = impl_->state;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->accepting) {
        return Status(StatusCode::kFailedPrecondition, "event bus has been shut down");
    }

    const auto entry = std::make_shared<Entry>(std::move(callback));
    auto subscription = std::make_unique<Subscription::Impl>(state, type, entry);
    state->handlers[type].push_back(entry);
    return Subscription(std::move(subscription));
}

Status EventBus::PublishSync(std::type_index type, const void* event) {
    const std::shared_ptr<State> state = impl_->state;
    std::vector<std::shared_ptr<Entry>> entries;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->accepting) {
            return Status(StatusCode::kFailedPrecondition, "event bus has been shut down");
        }
        const auto iterator = state->handlers.find(type);
        if (iterator == state->handlers.end() || iterator->second.empty()) {
            return Status(StatusCode::kNotFound, "event type has no subscribers");
        }
        entries = iterator->second;
        ++state->active_publications;
    }

    current_states.push_back(state.get());
    const auto finish_publication = [&] {
        current_states.pop_back();
        std::lock_guard<std::mutex> lock(state->mutex);
        --state->active_publications;
        state->idle.notify_all();
    };

    try {
        for (const std::shared_ptr<Entry>& entry : entries) {
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                if (!entry->active) {
                    continue;
                }
                ++entry->in_flight;
            }

            current_entries.push_back(entry.get());
            try {
                entry->callback(event);
            } catch (...) {
                current_entries.pop_back();
                {
                    std::lock_guard<std::mutex> lock(entry->mutex);
                    --entry->in_flight;
                    if (entry->in_flight == 0) {
                        entry->idle.notify_all();
                    }
                }
                throw;
            }
            current_entries.pop_back();
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                --entry->in_flight;
                if (entry->in_flight == 0) {
                    entry->idle.notify_all();
                }
            }
        }
    } catch (...) {
        finish_publication();
        throw;
    }

    finish_publication();
    return Status::Ok();
}

Status EventBus::Shutdown() {
    const std::shared_ptr<State> state = impl_->state;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->accepting = false;
    const std::size_t current_publications = CurrentStateCount(state.get());
    state->idle.wait(lock, [&] { return state->active_publications <= current_publications; });
    return Status::Ok();
}

Status EventBus::ResetSubscription(Subscription::Impl& subscription) {
    const std::shared_ptr<Entry> entry = subscription.entry;
    if (!entry) {
        return Status::Ok();
    }
    const std::shared_ptr<State> state = subscription.state.lock();
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto iterator = state->handlers.find(subscription.type);
        if (iterator != state->handlers.end()) {
            std::vector<std::shared_ptr<Entry>>& entries = iterator->second;
            entries.erase(std::remove(entries.begin(), entries.end(), entry), entries.end());
            if (entries.empty()) {
                state->handlers.erase(iterator);
            }
        }
    }

    std::unique_lock<std::mutex> lock(entry->mutex);
    entry->active = false;
    if (!IsCurrentEntry(entry.get())) {
        entry->idle.wait(lock, [&] { return entry->in_flight == 0; });
    }
    return Status::Ok();
}

}  // namespace tos
