#ifndef TOS_APP_APP_H_
#define TOS_APP_APP_H_

#include <memory>
#include <string>
#include <utility>

#include "tos/app/context.h"
#include "tos/app/module.h"
#include "tos/base/config.h"
#include "tos/base/executor.h"
#include "tos/base/logging.h"
#include "tos/base/scheduler.h"
#include "tos/base/status.h"
#include "tos/base/thread_pool.h"

namespace tos {

class DebugController;
class Path;

/// Lifecycle state of an App. A failed app is not restartable.
enum class AppState { kCreated, kStarting, kRunning, kStopping, kStopped, kFailed };

/// Configuration used to construct an App.
/// Config is moved into the App; log and thread-pool fields configure its shared infrastructure.
/// version is diagnostic metadata only.
struct AppOptions {
    std::string name = "tos";
    /// Application version reported by the built-in debug `version` command.
    std::string version = "unknown";
    Config config;
    LoggerOptions log;
    /// Zero uses DefaultThreadPoolWorkerCount().
    std::size_t thread_pool_worker_count = 0;
    std::size_t thread_pool_queue_capacity = 1024;
};

/// Owns modules and coordinates their deterministic lifecycle.
///
/// App is non-copyable. Module changes are allowed only in kCreated; lifecycle calls are
/// serialized and safe to make concurrently. Start validates dependencies and rolls back on
/// failure; Stop continues cleanup and returns the first error. Destruction never throws.
class [[nodiscard]] App {
   public:
    explicit App(AppOptions options = {});
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
    ~App() noexcept;

    /// Takes ownership of module. Empty/duplicate names, self-dependencies, and invalid states
    /// return kInvalidArgument, kAlreadyExists, or kFailedPrecondition respectively.
    [[nodiscard]] Status AddModule(std::unique_ptr<Module> module);

    /// Loads and registers one Module exported by a trusted dynamic library at an absolute path.
    ///
    /// The library must export the non-throwing C-linkage `tos_get_module()` declared in
    /// <tos/app/module.h>, returning a non-null Module that remains valid until unload. App
    /// borrows it and retains the library until destruction; Stop() only calls OnUnload. Relative
    /// paths return kInvalidArgument; loader, export, null-pointer, and registration failures
    /// return Status. This is valid only in kCreated. The plugin must share the host's tos version,
    /// compiler, and C++ runtime; untrusted native code is unsafe. Allocation exceptions propagate.
    [[nodiscard]] Status AddDynamicModule(const Path& path);

    /// Loads every module in deterministic topological order.
    [[nodiscard]] Status Start();

    /// Closes the EventBus, unloads modules in reverse topological order, stops the shared
    /// scheduler, then drains the shared executor. It is idempotent and also closes both task
    /// components for never-started applications.
    [[nodiscard]] Status Stop();

    [[nodiscard]] AppState state() const noexcept;
    [[nodiscard]] Context& context() noexcept;
    [[nodiscard]] const Config& config() const noexcept;
    [[nodiscard]] Logger& logger() noexcept;

    /// Returns the App-owned dispatcher, including standard `status` and `log-level` commands.
    /// The reference remains valid while this App lives.
    [[nodiscard]] DebugController& debug() noexcept;

    /// Returns the App-owned executor. The reference remains valid while this App lives and cannot
    /// stop its ThreadPool.
    [[nodiscard]] Executor& executor() noexcept;

    /// Returns the App-owned scheduler. The reference remains valid while this App lives and
    /// cannot stop it.
    [[nodiscard]] ScheduledExecutor& scheduler() noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_APP_H_
