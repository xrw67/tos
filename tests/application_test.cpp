#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "tos/app/app.h"
#include "tos/app/context.h"
#include "tos/app/module.h"
#include "tos/app/service.h"

namespace {

struct ModuleSpec {
    std::string name;
    std::vector<std::string> dependencies;
    std::vector<std::string>* events;
    bool fail_load = false;
    bool fail_unload = false;
    bool throw_load = false;
    bool throw_unload = false;
};

struct TestService final : tos::Service {
    explicit TestService(int value) : value(value) {}
    int value;
};

struct OtherService final : tos::Service {};

struct AppEvent {
    int value;
};

class TestModule final : public tos::Module {
   public:
    explicit TestModule(ModuleSpec spec) : spec_(std::move(spec)) {}

    std::string Name() const override { return spec_.name; }
    std::vector<std::string> Dependencies() const override { return spec_.dependencies; }

    tos::Status OnLoad(tos::Context&) override {
        spec_.events->push_back(spec_.name + ".load");
        if (spec_.throw_load) {
            throw std::runtime_error("load failed");
        }
        if (spec_.fail_load) {
            return {tos::StatusCode::kAborted, "load failed"};
        }
        return tos::Status::Ok();
    }
    tos::Status OnUnload(tos::Context&) override {
        spec_.events->push_back(spec_.name + ".unload");
        if (spec_.throw_unload) {
            throw std::runtime_error("unload failed");
        }
        if (spec_.fail_unload) {
            return {tos::StatusCode::kAborted, "unload failed"};
        }
        return tos::Status::Ok();
    }

   private:
    ModuleSpec spec_;
};

tos::AppOptions QuietOptions() {
    tos::AppOptions options;
    options.log.console = false;
    return options;
}

}  // namespace

TEST(AppTest, DeterministicLifecycleAndReverseUnload) {
    std::vector<std::string> events;
    tos::App app(QuietOptions());
    ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"z", {"a"}, &events})));
    ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {}, &events})));
    ASSERT_TRUE(app.Start());
    EXPECT_EQ(app.state(), tos::AppState::kRunning);
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "z.load"}));
    ASSERT_TRUE(app.Stop());
    EXPECT_EQ(app.state(), tos::AppState::kStopped);
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "z.load", "z.unload", "a.unload"}));
    EXPECT_TRUE(app.Stop());
}

TEST(AppTest, ValidatesDependenciesAndNames) {
    std::vector<std::string> events;
    tos::App app(QuietOptions());
    EXPECT_EQ(app.AddModule(nullptr).code(), tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"", {}, &events})).code(),
              tos::StatusCode::kInvalidArgument);
    ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {}, &events})));
    EXPECT_EQ(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {}, &events})).code(),
              tos::StatusCode::kAlreadyExists);
    EXPECT_EQ(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"b", {"b"}, &events})).code(),
              tos::StatusCode::kInvalidArgument);

    tos::App missing(QuietOptions());
    ASSERT_TRUE(
        missing.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {"missing"}, &events})));
    EXPECT_EQ(missing.Start().code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(missing.state(), tos::AppState::kFailed);

    tos::App cycle(QuietOptions());
    ASSERT_TRUE(cycle.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {"b"}, &events})));
    ASSERT_TRUE(cycle.AddModule(std::make_unique<TestModule>(ModuleSpec{"b", {"a"}, &events})));
    EXPECT_EQ(cycle.Start().code(), tos::StatusCode::kInvalidArgument);
}

TEST(AppTest, LoadFailureRollsBackAndConvertsExceptions) {
    std::vector<std::string> events;
    tos::App app(QuietOptions());
    ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {}, &events})));
    ASSERT_TRUE(app.AddModule(
        std::make_unique<TestModule>(ModuleSpec{"b", {"a"}, &events, false, false, true, false})));
    const tos::Status result = app.Start();
    EXPECT_EQ(result.code(), tos::StatusCode::kInternal);
    EXPECT_EQ(app.state(), tos::AppState::kFailed);
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "b.load", "a.unload"}));
    EXPECT_EQ(app.Start().code(), tos::StatusCode::kFailedPrecondition);
}

TEST(AppTest, UnloadContinuesAfterErrors) {
    std::vector<std::string> events;
    tos::App app(QuietOptions());
    ASSERT_TRUE(app.AddModule(
        std::make_unique<TestModule>(ModuleSpec{"a", {}, &events, false, true, false, false})));
    ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"b", {"a"}, &events})));
    ASSERT_TRUE(app.Start());
    EXPECT_EQ(app.Stop().code(), tos::StatusCode::kAborted);
    EXPECT_EQ(app.state(), tos::AppState::kFailed);
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "b.load", "b.unload", "a.unload"}));
}

TEST(AppTest, UnloadExceptionsBecomeInternal) {
    std::vector<std::string> events;
    tos::App app(QuietOptions());
    ASSERT_TRUE(app.AddModule(
        std::make_unique<TestModule>(ModuleSpec{"a", {}, &events, false, false, false, true})));
    ASSERT_TRUE(app.Start());
    EXPECT_EQ(app.Stop().code(), tos::StatusCode::kInternal);
    EXPECT_EQ(app.state(), tos::AppState::kFailed);
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "a.unload"}));
}

TEST(AppTest, ContextServicesAndInfrastructure) {
    tos::App app(QuietOptions());
    tos::Context& context = app.context();
    TestService value(42);
    TestService unexpected(7);
    ASSERT_TRUE(context.RegisterService(&value));
    EXPECT_EQ(context.RegisterService(&value).code(), tos::StatusCode::kAlreadyExists);
    auto borrowed = context.GetService<TestService>();
    ASSERT_TRUE(borrowed);
    ASSERT_EQ(borrowed.get(), &value);
    EXPECT_EQ(borrowed->value, 42);
    EXPECT_EQ(context.UnregisterService(&unexpected).code(), tos::StatusCode::kFailedPrecondition);
    EXPECT_EQ(context.UnregisterService(&value).code(), tos::StatusCode::kFailedPrecondition);
    ASSERT_TRUE(borrowed.Reset());
    ASSERT_TRUE(context.UnregisterService(&value));
    EXPECT_FALSE(context.GetService<TestService>());
    EXPECT_EQ(context.UnregisterService(&value).code(), tos::StatusCode::kNotFound);
    EXPECT_EQ(context.RegisterService(static_cast<TestService*>(nullptr)).code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(context.UnregisterService(static_cast<TestService*>(nullptr)).code(),
              tos::StatusCode::kInvalidArgument);
    EXPECT_EQ(&context.config(), &app.config());
    EXPECT_EQ(&context.logger(), &app.logger());
}

TEST(AppTest, ContextProvidesConfigAndLoggerDirectly) {
    tos::AppOptions options = QuietOptions();
    options.config = tos::Config::Parse(R"({"name":"demo"})", tos::ConfigFormat::kJson).value();
    tos::App app(std::move(options));
    tos::Context& context = app.context();
    EXPECT_EQ(&context.config(), &app.config());
    EXPECT_EQ(context.config().GetString("name").value(), "demo");
    const tos::Context& const_context = context;
    EXPECT_EQ(&const_context.config(), &app.config());
    EXPECT_EQ(&context.logger(), &app.logger());
    ASSERT_TRUE(context.logger().Info("context logger ready"));
}

TEST(AppTest, ContextProvidesEventBusAndStopClosesIt) {
    tos::App app(QuietOptions());
    ASSERT_TRUE(app.Start());
    int observed = 0;
    auto subscription_result = app.context().events().Subscribe<AppEvent>(
        [&](const AppEvent& event) { observed = event.value; });
    ASSERT_TRUE(subscription_result);
    auto subscription = std::move(subscription_result).value();
    ASSERT_TRUE(app.context().events().PublishSync(AppEvent{7}));
    EXPECT_EQ(observed, 7);
    ASSERT_TRUE(app.Stop());
    EXPECT_EQ(app.context().events().PublishSync(AppEvent{8}).code(),
              tos::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(subscription.Reset());
}

TEST(AppTest, ServiceHandleIsMoveOnlyAndReleasesOnScopeExit) {
    static_assert(!std::is_copy_constructible_v<tos::ServiceHandle<TestService>>);
    static_assert(!std::is_copy_assignable_v<tos::ServiceHandle<TestService>>);
    static_assert(std::is_move_constructible_v<tos::ServiceHandle<TestService>>);
    static_assert(std::is_move_assignable_v<tos::ServiceHandle<TestService>>);

    tos::App app(QuietOptions());
    tos::Context& context = app.context();
    TestService service(42);
    ASSERT_TRUE(context.RegisterService(&service));
    {
        auto first = context.GetService<TestService>();
        ASSERT_TRUE(first);
        auto second = std::move(first);
        EXPECT_FALSE(first);
        ASSERT_TRUE(second);
        EXPECT_EQ(second->value, 42);
        EXPECT_EQ(context.UnregisterService(&service).code(), tos::StatusCode::kFailedPrecondition);
        ASSERT_TRUE(second.Reset());
        EXPECT_FALSE(second);
    }
    ASSERT_TRUE(context.UnregisterService(&service));

    ASSERT_TRUE(context.RegisterService(&service));
    try {
        auto borrowed = context.GetService<TestService>();
        ASSERT_TRUE(borrowed);
        throw std::runtime_error("scope exit");
    } catch (const std::runtime_error&) {
    }
    EXPECT_TRUE(context.UnregisterService(&service));
}

TEST(AppTest, ContextSupportsConcurrentAccess) {
    tos::App app(QuietOptions());
    tos::Context* const context_ptr = &app.context();
    TestService service(0);
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([context_ptr, &service, &completed] {
            for (int j = 0; j < 100; ++j) {
                const tos::Status registration = context_ptr->RegisterService(&service);
                {
                    auto borrowed = context_ptr->GetService<TestService>();
                    if (borrowed && borrowed.get() == &service) {
                        ++completed;
                    }
                }
                if (registration.ok()) {
                    static_cast<void>(context_ptr->UnregisterService(&service));
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_GT(completed.load(), 0);
    const tos::Status cleanup = context_ptr->UnregisterService(&service);
    EXPECT_TRUE(cleanup.ok() || cleanup.code() == tos::StatusCode::kNotFound);
}

TEST(AppTest, DestructorUnloadsRunningModules) {
    std::vector<std::string> events;
    {
        tos::App app(QuietOptions());
        ASSERT_TRUE(app.AddModule(std::make_unique<TestModule>(ModuleSpec{"a", {}, &events})));
        ASSERT_TRUE(app.Start());
    }
    EXPECT_EQ(events, (std::vector<std::string>{"a.load", "a.unload"}));
}
