#include "tos/app/context.h"

#include "context_factory.h"
#include "service_registry.h"

namespace tos {

namespace {

class AppContext final : public Context {
   public:
    AppContext(ServiceRegistry& registry_value, EventBus& events_value, Executor& executor_value,
               ScheduledExecutor& scheduler_value, const Config& config_value,
               Logger& logger_value) noexcept
        : registry_(registry_value),
          events_(events_value),
          executor_(executor_value),
          scheduler_(scheduler_value),
          config_(config_value),
          logger_(logger_value) {}

    const Config& config() const noexcept override { return config_; }
    Logger& logger() noexcept override { return logger_; }
    EventBus& events() noexcept override { return events_; }
    Executor& executor() noexcept override { return executor_; }
    ScheduledExecutor& scheduler() noexcept override { return scheduler_; }

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
};

}  // namespace

std::unique_ptr<Context> CreateAppContext(ServiceRegistry& registry, EventBus& events,
                                          Executor& executor, ScheduledExecutor& scheduler,
                                          const Config& config, Logger& logger) {
    return std::make_unique<AppContext>(registry, events, executor, scheduler, config, logger);
}

}  // namespace tos
