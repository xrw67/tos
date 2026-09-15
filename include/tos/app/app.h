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

/// Lifecycle state of an App. A failed app is not restartable.
enum class AppState { kCreated, kStarting, kRunning, kStopping, kStopped, kFailed };

/// Configuration used to construct an App.
/// The Config value is moved into the application; Logger and the shared ThreadPool are
/// constructed from log and the thread pool fields respectively.
struct AppOptions {
    std::string name = "tos";
    Config config;
    LoggerOptions log;
    /// Zero uses DefaultThreadPoolWorkerCount().
    std::size_t thread_pool_worker_count = 0;
    std::size_t thread_pool_queue_capacity = 1024;
};

/// Owns modules and coordinates their deterministic lifecycle.
///
/// App is non-copyable and non-movable. AddModule is valid only in kCreated. Start and
/// Stop are safe to call concurrently; transitions and module callbacks are serialized. Start
/// validates the complete dependency graph before invoking callbacks, and rolls back loaded
/// modules on failure. Stop continues cleanup after an error and returns the first failure.
/// Destruction best-effort unloads active modules and never throws.
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

    /// Returns the App-owned shared executor. The reference remains valid while this App is alive
    /// and supports concurrent submissions, but cannot be used to stop the shared ThreadPool.
    [[nodiscard]] Executor& executor() noexcept;

    /// Returns the App-owned monotonic scheduler. The reference remains valid while this App is
    /// alive and supports concurrent scheduling, but cannot be used to stop it.
    [[nodiscard]] ScheduledExecutor& scheduler() noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_APP_APP_H_
