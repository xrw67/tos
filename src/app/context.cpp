#include "context_impl.h"
#include "service_registry.h"

namespace tos {

ContextImpl::ContextImpl(ServiceRegistry& registry, const Config& config, Logger& logger) noexcept
    : Context(config, logger), registry_(registry) {}

Status ContextImpl::RegisterService(std::type_index type, const Service* service) {
    return registry_.Register(type, service);
}

const Service* ContextImpl::AcquireService(std::type_index type) const {
    return registry_.Acquire(type);
}

Status ContextImpl::ReleaseService(std::type_index type, const Service* service) const {
    return registry_.Release(type, service);
}

Status ContextImpl::UnregisterService(std::type_index type, const Service* expected) {
    return registry_.Unregister(type, expected);
}

}  // namespace tos
