#ifndef TOS_APP_CONTEXT_FACTORY_INTERNAL_H_
#define TOS_APP_CONTEXT_FACTORY_INTERNAL_H_

#include <memory>

#include "tos/app/context.h"

namespace tos {

class ServiceRegistry;
class DebugController;

[[nodiscard]] std::unique_ptr<Context> CreateAppContext(ServiceRegistry& registry, EventBus& events,
                                                        Executor& executor,
                                                        ScheduledExecutor& scheduler,
                                                        const Config& config, Logger& logger,
                                                        DebugController& debug);

}  // namespace tos

#endif  // TOS_APP_CONTEXT_FACTORY_INTERNAL_H_
