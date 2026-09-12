#ifndef TOS_APP_SERVICE_REGISTRY_INTERNAL_H_
#define TOS_APP_SERVICE_REGISTRY_INTERNAL_H_

#include <cstddef>
#include <mutex>
#include <unordered_map>

#include "tos/app/context.h"

namespace tos {

// Internal implementation detail. The public service API is Context; App owns this registry.
class ServiceRegistry {
   public:
    [[nodiscard]] Status Register(std::type_index type, const Service* service);
    [[nodiscard]] const Service* Acquire(std::type_index type) const;
    [[nodiscard]] Status Release(std::type_index type, const Service* service) const;
    [[nodiscard]] Status Unregister(std::type_index type, const Service* expected);

   private:
    struct Entry {
        const Service* service;
        std::size_t borrows = 0;
    };

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::type_index, Entry> services_;
};

}  // namespace tos

#endif  // TOS_APP_SERVICE_REGISTRY_INTERNAL_H_
