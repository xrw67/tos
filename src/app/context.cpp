#include "tos/app/context.h"

#include "service_registry.h"

namespace tos {

class Context::Impl {
   public:
    Impl(ServiceRegistry& registry_value, const Config& config_value, Logger& logger_value) noexcept
        : registry(registry_value), config(config_value), logger(logger_value) {}

    ServiceRegistry& registry;
    const Config& config;
    Logger& logger;
};

Context::Context(ServiceRegistry& registry, const Config& config, Logger& logger)
    : impl_(std::make_unique<Impl>(registry, config, logger)) {}

Context::~Context() noexcept = default;

const Config& Context::config() const noexcept { return impl_->config; }

Logger& Context::logger() noexcept { return impl_->logger; }

Status Context::RegisterService(std::type_index type, const Service* service) {
    return impl_->registry.Register(type, service);
}

const Service* Context::AcquireService(std::type_index type) const {
    return impl_->registry.Acquire(type);
}

Status Context::ReleaseService(std::type_index type, const Service* service) const {
    return impl_->registry.Release(type, service);
}

Status Context::UnregisterService(std::type_index type, const Service* expected) {
    return impl_->registry.Unregister(type, expected);
}

}  // namespace tos
