#ifndef TOS_APP_MODULE_REGISTRY_INTERNAL_H_
#define TOS_APP_MODULE_REGISTRY_INTERNAL_H_

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "tos/app/module.h"
#include "tos/base/result.h"

namespace tos {

// Internal implementation detail. App owns this registry and serializes all access to it.
class ModuleRegistry {
   public:
    [[nodiscard]] Status Register(std::unique_ptr<Module> module);
    [[nodiscard]] Result<std::vector<std::size_t>> ResolveLoadOrder() const;

    [[nodiscard]] Module& Get(std::size_t index) noexcept;
    [[nodiscard]] const std::string& Name(std::size_t index) const noexcept;

    void MarkLoaded(std::size_t index);
    [[nodiscard]] const std::vector<std::size_t>& loaded() const noexcept { return loaded_; }
    void ClearLoaded() noexcept { loaded_.clear(); }

   private:
    struct Entry {
        std::unique_ptr<Module> module;
        std::string name;
        std::vector<std::string> dependencies;
    };

    std::vector<Entry> modules_;
    std::map<std::string, std::size_t> indices_;
    std::vector<std::size_t> loaded_;
};

}  // namespace tos

#endif  // TOS_APP_MODULE_REGISTRY_INTERNAL_H_
