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
            "dynamic-module", "Report that the dynamic module is loaded.",
            [](const tos::span<std::string>&, std::ostream& output) {
                output << "dynamic module loaded\n";
            });
    }

    tos::Status OnUnload(tos::Context& context) override {
        return context.UnregisterDebugHandler("dynamic-module");
    }
};

}  // namespace

extern "C" TOS_DYNAMIC_MODULE_EXPORT tos::Module* tos_get_module() noexcept {
    static DynamicModuleTestModule module;
    return &module;
}
