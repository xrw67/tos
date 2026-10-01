#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "test_util.h"
#include "tos/app/event.h"

namespace {

using namespace std::chrono_literals;

struct NumberEvent {
    int value;
};

struct OtherEvent {
    int value;
};

TEST(EventBusTest, DispatchesExactTypesInRegistrationOrderOnThePublisherThread) {
    tos::EventBus events;
    std::vector<int> values;
    std::thread::id callback_thread;
    const std::thread::id publisher_thread = std::this_thread::get_id();
    auto first_result = events.Subscribe<NumberEvent>([&](const NumberEvent& event) {
        callback_thread = std::this_thread::get_id();
        values.push_back(event.value);
    });
    auto second_result = events.Subscribe<NumberEvent>(
        [&](const NumberEvent& event) { values.push_back(event.value + 10); });
    auto other_result = events.Subscribe<OtherEvent>(
        [&](const OtherEvent& event) { values.push_back(event.value + 100); });
    ASSERT_TRUE(first_result);
    ASSERT_TRUE(second_result);
    ASSERT_TRUE(other_result);
    auto first = std::move(first_result).value();
    auto second = std::move(second_result).value();
    auto other = std::move(other_result).value();

    ASSERT_TRUE(events.PublishSync(NumberEvent{3}));
    EXPECT_EQ(values, (std::vector<int>{3, 13}));
    EXPECT_EQ(callback_thread, publisher_thread);
    ASSERT_TRUE(events.PublishSync(OtherEvent{2}));
    EXPECT_EQ(values, (std::vector<int>{3, 13, 102}));
}

TEST(EventBusTest, ReportsMissingSubscribersAndShutdown) {
    tos::EventBus events;
    EXPECT_EQ(events.PublishSync(NumberEvent{1}).code(), tos::StatusCode::kNotFound);

    auto subscription_result = events.Subscribe<NumberEvent>([](const NumberEvent&) {});
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();
    ASSERT_TRUE(events.Shutdown());
    EXPECT_EQ(events.PublishSync(NumberEvent{1}).code(), tos::StatusCode::kFailedPrecondition);
    auto after_shutdown = events.Subscribe<NumberEvent>([](const NumberEvent&) {});
    EXPECT_FALSE(after_shutdown);
    EXPECT_EQ(after_shutdown.status().code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(subscription.Reset());
    EXPECT_TRUE(events.Shutdown());
}

TEST(EventBusTest, PropagatesSynchronousHandlerExceptions) {
    tos::EventBus events;
    auto subscription_result = events.Subscribe<NumberEvent>(
        [](const NumberEvent&) { throw std::runtime_error("handler failed"); });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();
    EXPECT_THROW(static_cast<void>(events.PublishSync(NumberEvent{1})), std::runtime_error);
    EXPECT_TRUE(subscription.Reset());
}

TEST(EventBusTest, SupportsMutableHandlers) {
    tos::EventBus events;
    auto subscription_result =
        events.Subscribe<NumberEvent>([total = 0](const NumberEvent& event) mutable {
            total += event.value;
            EXPECT_EQ(total, event.value);
        });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();

    EXPECT_TRUE(events.PublishSync(NumberEvent{4}));
    EXPECT_TRUE(subscription.Reset());
}

TEST(EventBusTest, ResetWaitsForOtherThreadCallbacksAndSelfResetDoesNotDeadlock) {
    tos::EventBus events;
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable released;
    bool running = false;
    bool proceed = false;
    auto subscription_result = events.Subscribe<NumberEvent>([&](const NumberEvent&) {
        std::unique_lock<std::mutex> lock(mutex);
        running = true;
        entered.notify_all();
        released.wait(lock, [&] { return proceed; });
    });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();

    tos::Status publish_status;
    std::thread publisher([&] { publish_status = events.PublishSync(NumberEvent{1}); });
    tos::test::ScopeExit cleanup([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            proceed = true;
        }
        released.notify_all();
        if (publisher.joinable()) publisher.join();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(entered.wait_for(lock, 5s, [&] { return running; }));
    }

    std::promise<void> reset_started;
    std::future<tos::Status> reset_finished = std::async(std::launch::async, [&] {
        reset_started.set_value();
        return subscription.Reset();
    });
    reset_started.get_future().wait();
    EXPECT_EQ(reset_finished.wait_for(20ms), std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(mutex);
        proceed = true;
    }
    released.notify_all();
    publisher.join();
    EXPECT_TRUE(publish_status);
    EXPECT_TRUE(reset_finished.get());
    EXPECT_EQ(events.PublishSync(NumberEvent{2}).code(), tos::StatusCode::kNotFound);

    tos::EventBus::Subscription self_reset;
    auto self_reset_result =
        events.Subscribe<NumberEvent>([&](const NumberEvent&) { EXPECT_TRUE(self_reset.Reset()); });
    ASSERT_TRUE(self_reset_result);
    self_reset = std::move(self_reset_result).value();
    EXPECT_TRUE(events.PublishSync(NumberEvent{3}));
    EXPECT_FALSE(self_reset);
}

TEST(EventBusTest, ShutdownWaitsForOtherThreadCallbacksAndSupportsConcurrentOperations) {
    tos::EventBus events;
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable released;
    bool running = false;
    bool proceed = false;
    std::atomic<int> calls{0};
    auto subscription_result = events.Subscribe<NumberEvent>([&](const NumberEvent&) {
        ++calls;
        std::unique_lock<std::mutex> lock(mutex);
        running = true;
        entered.notify_all();
        released.wait(lock, [&] { return proceed; });
    });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();

    std::thread publisher([&] { static_cast<void>(events.PublishSync(NumberEvent{1})); });
    tos::test::ScopeExit cleanup([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            proceed = true;
        }
        released.notify_all();
        if (publisher.joinable()) publisher.join();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(entered.wait_for(lock, 5s, [&] { return running; }));
    }
    std::future<tos::Status> shutdown =
        std::async(std::launch::async, [&] { return events.Shutdown(); });
    EXPECT_EQ(shutdown.wait_for(20ms), std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(mutex);
        proceed = true;
    }
    released.notify_all();
    publisher.join();
    EXPECT_TRUE(shutdown.get());
    EXPECT_EQ(events.PublishSync(NumberEvent{2}).code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(subscription.Reset());
    EXPECT_EQ(calls.load(), 1);
}

TEST(EventBusTest, ShutdownFromAHandlerDoesNotWaitForItsOwnPublication) {
    tos::EventBus events;
    auto subscription_result =
        events.Subscribe<NumberEvent>([&](const NumberEvent&) { EXPECT_TRUE(events.Shutdown()); });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();

    EXPECT_TRUE(events.PublishSync(NumberEvent{1}));
    EXPECT_EQ(events.PublishSync(NumberEvent{2}).code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(subscription.Reset());
}

TEST(EventBusTest, AllowsConcurrentSubscriptionPublishingAndReset) {
    tos::EventBus events;
    std::atomic<int> callbacks{0};
    auto stable_result = events.Subscribe<NumberEvent>([&](const NumberEvent&) { ++callbacks; });
    ASSERT_TRUE(stable_result);
    auto stable = std::move(stable_result).value();

    std::vector<std::thread> publishers;
    for (int thread_index = 0; thread_index < 4; ++thread_index) {
        publishers.emplace_back([&] {
            for (int value = 0; value < 100; ++value) {
                const tos::Status status = events.PublishSync(NumberEvent{value});
                EXPECT_TRUE(status.ok() || status.code() == tos::StatusCode::kNotFound);
            }
        });
    }
    std::thread subscriptions([&] {
        for (int index = 0; index < 50; ++index) {
            auto temporary_result = events.Subscribe<NumberEvent>([](const NumberEvent&) {});
            ASSERT_TRUE(temporary_result);
            auto temporary = std::move(temporary_result).value();
            ASSERT_TRUE(temporary.Reset());
        }
    });
    for (auto& publisher : publishers) {
        publisher.join();
    }
    subscriptions.join();
    EXPECT_TRUE(stable.Reset());
    EXPECT_GT(callbacks.load(), 0);
}

TEST(EventBusTest, ResetAndShutdownWaitForEveryActivePublisher) {
    for (bool shutdown : {false, true}) {
        tos::EventBus events;
        tos::test::Gate release;
        std::atomic<int> calls{0};
        std::promise<void> all_entered, closing_started;
        auto entered = all_entered.get_future();
        auto started = closing_started.get_future();
        auto held = std::make_shared<int>(42);
        std::weak_ptr<int> lifetime = held;
        auto result = events.Subscribe<NumberEvent>([&, held](const NumberEvent&) {
            EXPECT_EQ(*held, 42);
            if (calls.fetch_add(1) == 3) all_entered.set_value();
            release.Wait();
        });
        ASSERT_TRUE(result);
        auto subscription = std::move(result).value();
        held.reset();
        std::promise<tos::Status> closing_result;
        auto finished = closing_result.get_future();
        std::vector<std::thread> threads;
        tos::test::ScopeExit cleanup([&] {
            release.Open();
            for (auto& thread : threads)
                if (thread.joinable()) thread.join();
        });
        for (int index = 0; index < 4; ++index)
            threads.emplace_back([&] { EXPECT_TRUE(events.PublishSync(NumberEvent{1})); });
        ASSERT_EQ(entered.wait_for(5s), std::future_status::ready);
        threads.emplace_back([&] {
            closing_started.set_value();
            closing_result.set_value(shutdown ? events.Shutdown() : subscription.Reset());
        });
        ASSERT_EQ(started.wait_for(5s), std::future_status::ready);
        EXPECT_EQ(finished.wait_for(50ms), std::future_status::timeout);
        EXPECT_FALSE(lifetime.expired());
        release.Open();
        for (auto& thread : threads) thread.join();
        ASSERT_EQ(finished.wait_for(5s), std::future_status::ready);
        EXPECT_TRUE(finished.get());
        EXPECT_EQ(events.PublishSync(NumberEvent{2}).code(),
                  shutdown ? tos::StatusCode::kFailedPrecondition : tos::StatusCode::kNotFound);
        EXPECT_EQ(calls.load(), 4);
        EXPECT_TRUE(subscription.Reset());
        EXPECT_TRUE(lifetime.expired());
    }
}

TEST(EventBusTest, ConcurrentExceptionsReleasePublicationAndCallbackCounts) {
    tos::EventBus events;
    tos::test::Gate release;
    std::atomic<int> entered{0}, exceptions{0};
    std::promise<void> all_entered;
    auto ready = all_entered.get_future();
    auto result = events.Subscribe<NumberEvent>([&](const NumberEvent&) {
        if (entered.fetch_add(1) == 3) all_entered.set_value();
        release.Wait();
        throw std::runtime_error("expected concurrent callback failure");
    });
    ASSERT_TRUE(result);
    auto subscription = std::move(result).value();
    std::vector<std::thread> threads;
    tos::test::ScopeExit cleanup([&] {
        release.Open();
        for (auto& thread : threads)
            if (thread.joinable()) thread.join();
    });
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([&] {
            try {
                static_cast<void>(events.PublishSync(NumberEvent{1}));
            } catch (const std::runtime_error&) {
                ++exceptions;
            }
        });
    ASSERT_EQ(ready.wait_for(5s), std::future_status::ready);
    release.Open();
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(exceptions.load(), 4);
    EXPECT_TRUE(subscription.Reset());
    EXPECT_TRUE(events.Shutdown());
}

}  // namespace
