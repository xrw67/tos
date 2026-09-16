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
/// Name and Dependencies are copied during registration. OnLoad callbacks are invoked serially in
/// dependency order; OnUnload callbacks are invoked serially in reverse dependency order.
/// Callback exceptions are converted to kInternal by App; allocation exceptions outside callbacks
/// propagate.
class Module {
   public:
    /// Destroys the module after App has completed its cleanup callbacks. Destruction exceptions
    /// are not permitted; the App owns the module exclusively.
    virtual ~Module() = default;

    /// Returns a nonempty, stable module name used for dependency references and diagnostics.
    /// The returned string is copied by App::AddModule; allocation exceptions propagate.
    [[nodiscard]] virtual std::string Name() const = 0;

    /// Returns names of modules that must load before this module. Names are interpreted as byte
    /// strings; missing dependencies and cycles are rejected before callbacks.
    /// The returned vector is copied by App::AddModule; allocation exceptions propagate.
    [[nodiscard]] virtual std::vector<std::string> Dependencies() const = 0;

    /// Loads resources and registers services needed by the module. App invokes this once in
    /// deterministic dependency order during Start. A failure prevents this module from being
    /// marked loaded; earlier successful modules are unloaded in reverse order. Exceptions are
    /// converted to kInternal by App.
    [[nodiscard]] virtual Status OnLoad(Context&) = 0;

    /// Stops active work, releases resources, and unregisters module-owned services. App invokes
    /// this once in reverse dependency order during Stop or startup rollback for each module whose
    /// OnLoad succeeded. Cleanup continues after failures and exceptions become kInternal.
    [[nodiscard]] virtual Status OnUnload(Context&) = 0;
};

/// The required C-linkage getter function exported by a dynamic module library.
inline constexpr std::string_view kDynamicModuleSymbol = "tos_get_module";

/// The type used to resolve kDynamicModuleSymbol from a DynamicLibrary.
///
/// A plugin exports a function that returns a borrowed Module pointer. The pointer must remain
/// valid until the shared library is unloaded. The function must use C linkage and noexcept; a
/// thrown exception terminates the process.
using DynamicModuleExport = Module* (*)() noexcept;

}  // namespace tos

#endif  // TOS_APP_MODULE_H_
