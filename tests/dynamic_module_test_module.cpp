#include <ostream>
#include <string>
#include <vector>

#include "tos/app/context.h"
#include "tos/app/module.h"

namespace {

class DynamicModuleTestModule final : public tos::Module {
   public:
    std::string Name() const override { return "dynamic-module"; }

    std::vector<std::string> Dependencies() const override { return {}; }

    tos::Status OnLoad(tos::Context& context) override {
        return context.RegisterDebugHandler(
            "dynamic-module", [](const tos::span<std::string>&, std::ostream& output) {
                output << "dynamic module loaded\n";
            });
    }

    tos::Status OnUnload(tos::Context& context) override {
        return context.UnregisterDebugHandler("dynamic-module");
    }
};

DynamicModuleTestModule module;

}  // namespace

extern "C" {
TOS_DYNAMIC_MODULE_EXPORT_DECLARATION extern tos::Module* const tos_dynamic_module;
}

TOS_DYNAMIC_MODULE_EXPORT tos::Module* const tos_dynamic_module = &module;
