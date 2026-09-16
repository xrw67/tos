#include "tos/app/context.h"

#include <utility>

#include "context_factory.h"
#include "service_registry.h"

namespace tos {

namespace {

class AppContext final : public Context {
   public:
    AppContext(ServiceRegistry& registry_value, EventBus& events_value, Executor& executor_value,
               ScheduledExecutor& scheduler_value, const Config& config_value, Logger& logger_value,
               DebugController& debug_value) noexcept
        : registry_(registry_value),
          events_(events_value),
          executor_(executor_value),
          scheduler_(scheduler_value),
          config_(config_value),
          logger_(logger_value),
          debug_(debug_value) {}

    const Config& config() const noexcept override { return config_; }
    Logger& logger() noexcept override { return logger_; }
    EventBus& events() noexcept override { return events_; }
    Executor& executor() noexcept override { return executor_; }
    ScheduledExecutor& scheduler() noexcept override { return scheduler_; }
    Status RegisterDebugHandler(const std::string& command, const std::string& description,
                                DebugHandler handler) override {
        return debug_.RegisterHandler(command, description, std::move(handler));
    }
    Status UnregisterDebugHandler(std::string_view command) override {
        return debug_.UnregisterHandler(command);
    }

   protected:
    Status RegisterServiceImpl(std::type_index type, const Service* service) override {
        return registry_.Register(type, service);
    }

    const Service* AcquireServiceImpl(std::type_index type) const override {
        return registry_.Acquire(type);
    }

    Status ReleaseServiceImpl(std::type_index type, const Service* service) const override {
        return registry_.Release(type, service);
    }

    Status UnregisterServiceImpl(std::type_index type, const Service* expected) override {
        return registry_.Unregister(type, expected);
    }

   private:
    ServiceRegistry& registry_;
    EventBus& events_;
    Executor& executor_;
    ScheduledExecutor& scheduler_;
    const Config& config_;
    Logger& logger_;
    DebugController& debug_;
};

}  // namespace

std::unique_ptr<Context> CreateAppContext(ServiceRegistry& registry, EventBus& events,
                                          Executor& executor, ScheduledExecutor& scheduler,
                                          const Config& config, Logger& logger,
                                          DebugController& debug) {
    return std::make_unique<AppContext>(registry, events, executor, scheduler, config, logger,
                                        debug);
}

}  // namespace tos
