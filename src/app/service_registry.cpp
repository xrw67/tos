#include "service_registry.h"

namespace tos {

Status ServiceRegistry::Register(std::type_index type, const Service* service) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (services_.find(type) != services_.end()) {
        return Status(StatusCode::kAlreadyExists, "service type is already registered");
    }
    services_.emplace(type, Entry{service});
    return Status::Ok();
}

const Service* ServiceRegistry::Acquire(std::type_index type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = services_.find(type);
    if (iterator == services_.end()) {
        return nullptr;
    }
    ++iterator->second.borrows;
    return iterator->second.service;
}

Status ServiceRegistry::Release(std::type_index type, const Service* service) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = services_.find(type);
    if (iterator == services_.end() || iterator->second.service != service ||
        iterator->second.borrows == 0) {
        return Status(StatusCode::kFailedPrecondition, "service borrow is not active");
    }
    --iterator->second.borrows;
    return Status::Ok();
}

Status ServiceRegistry::Unregister(std::type_index type, const Service* expected) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = services_.find(type);
    if (iterator == services_.end()) {
        return Status(StatusCode::kNotFound, "service type is not registered");
    }
    if (iterator->second.service != expected) {
        return Status(StatusCode::kFailedPrecondition,
                      "service pointer does not match the registered service");
    }
    if (iterator->second.borrows != 0) {
        return Status(StatusCode::kFailedPrecondition, "service has active borrows");
    }
    services_.erase(iterator);
    return Status::Ok();
}

}  // namespace tos
