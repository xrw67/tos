#ifndef TOS_APP_MODULE_H_
#define TOS_APP_MODULE_H_

#include <string>
#include <string_view>
#include <vector>

#include "tos/base/status.h"

// Marks a dynamic-module getter function for export.
#ifdef _WIN32
#define TOS_DYNAMIC_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define TOS_DYNAMIC_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define TOS_DYNAMIC_MODULE_EXPORT
#endif

namespace tos {

class Context;

/// A module owned by an App.
///
/// Names and dependencies are copied at registration. App calls OnLoad in dependency order and
/// OnUnload in reverse order; callback exceptions become kInternal.
class Module {
   public:
    /// App destroys owned modules after cleanup. Destruction must not throw.
    virtual ~Module() = default;

    /// Returns a nonempty stable name copied by App::AddModule. Allocation exceptions propagate.
    [[nodiscard]] virtual std::string Name() const = 0;

    /// Returns prerequisite names. Missing dependencies and cycles are rejected before callbacks;
    /// the vector is copied by App::AddModule and allocation exceptions propagate.
    [[nodiscard]] virtual std::vector<std::string> Dependencies() const = 0;

    /// Loads resources during Start. Failure rolls back earlier modules; exceptions become
    /// kInternal.
    [[nodiscard]] virtual Status OnLoad(Context&) = 0;

    /// Releases resources during Stop or rollback. Cleanup continues after failures; exceptions
    /// become kInternal.
    [[nodiscard]] virtual Status OnUnload(Context&) = 0;
};

/// The required C-linkage getter function exported by a dynamic module library.
inline constexpr std::string_view kDynamicModuleSymbol = "tos_get_module";

/// The type used to resolve kDynamicModuleSymbol from a DynamicLibrary.
///
/// A C-linkage getter returning a Module borrowed until library unload.
/// It must be noexcept; an exception terminates the process.
using DynamicModuleExport = Module* (*)() noexcept;

}  // namespace tos

#endif  // TOS_APP_MODULE_H_
