#ifndef TOS_APP_CONTEXT_IMPL_INTERNAL_H_
#define TOS_APP_CONTEXT_IMPL_INTERNAL_H_

#include "tos/app/context.h"

namespace tos {

class ServiceRegistry;

class ContextImpl final : public Context {
   public:
    using Context::GetService;
    using Context::RegisterService;
    using Context::UnregisterService;

    ContextImpl(ServiceRegistry& registry, const Config& config, Logger& logger) noexcept;

   private:
    Status RegisterService(std::type_index type, const Service* service) override;
    const Service* AcquireService(std::type_index type) const override;
    Status ReleaseService(std::type_index type, const Service* service) const override;
    Status UnregisterService(std::type_index type, const Service* expected) override;

    ServiceRegistry& registry_;
};

}  // namespace tos

#endif  // TOS_APP_CONTEXT_IMPL_INTERNAL_H_
