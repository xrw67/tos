#include "tos/app/app.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "context_factory.h"
#include "module_registry.h"
#include "service_registry.h"
#include "tos/app/debug.h"
#include "tos/base/dynamic_library.h"
#include "tos/base/format.h"
namespace tos {

struct App::Impl {
    explicit Impl(AppOptions options)
        : name(std::move(options.name)),
          config(std::move(options.config)),
          logger(std::move(options.log)),
          executor(options.thread_pool_worker_count, options.thread_pool_queue_capacity),
          scheduler(executor),
          context(CreateAppContext(service_registry, event_bus, executor, scheduler, config, logger,
                                   debug)) {}

    std::string name;
    Config config;
    Logger logger;
    DebugController debug;
    ServiceRegistry service_registry;
    EventBus event_bus;
    ThreadPool executor;
    Scheduler scheduler;
    std::unique_ptr<Context> context;
    ModuleRegistry module_registry;
    mutable std::recursive_mutex mutex;
    AppState state = AppState::kCreated;
    std::thread::id callback_thread;
};

namespace {

Status InvalidState(std::string_view operation) {
    return Status(StatusCode::kFailedPrecondition,
                  std::string(operation) + " is not valid in the current application state");
}

Status CallbackException(std::string_view module_name, std::string_view phase) {
    return Status(StatusCode::kInternal,
                  "module '" + std::string(module_name) + "' threw during " + std::string(phase));
}

Status FirstFailure(Status current, Status candidate) {
    return current.ok() && !candidate.ok() ? std::move(candidate) : std::move(current);
}

class DynamicModule final : public Module {
   public:
    DynamicModule(DynamicLibrary library, Module* module) noexcept
        : library_(std::move(library)), module_(module) {}

    std::string Name() const override { return module_->Name(); }

    std::vector<std::string> Dependencies() const override { return module_->Dependencies(); }

    Status OnLoad(Context& context) override { return module_->OnLoad(context); }

    Status OnUnload(Context& context) override { return module_->OnUnload(context); }

   private:
    DynamicLibrary library_;
    Module* module_;
};

Result<std::unique_ptr<Module>> LoadDynamicModule(const Path& path) {
    auto library_result = DynamicLibrary::Load(path);
    if (!library_result) {
        return std::move(library_result).status();
    }
    DynamicLibrary library = std::move(library_result).value();
    auto exported = library.GetSymbol<DynamicModuleExport>(kDynamicModuleSymbol);
    if (!exported) {
        return std::move(exported).status();
    }
    Module* const module = *exported.value();
    if (module == nullptr) {
        return Status(StatusCode::kInternal, "dynamic module export must not be null");
    }
    return std::unique_ptr<Module>(new DynamicModule(std::move(library), module));
}

const char* AppStateName(AppState state) noexcept {
    switch (state) {
        case AppState::kCreated:
            return "created";
        case AppState::kStarting:
            return "starting";
        case AppState::kRunning:
            return "running";
        case AppState::kStopping:
            return "stopping";
        case AppState::kStopped:
            return "stopped";
        case AppState::kFailed:
            return "failed";
    }
    return "unknown";
}

void WriteDebugStatus(App& app, ThreadPool& executor, std::ostream& output) {
    const ThreadPoolStats stats = executor.stats();
    tos::println(output, "{}={}", "app_state", AppStateName(app.state()));
    tos::println(output, "{}={}", "log_level", tos::LogLevelName(app.logger().level()));
    tos::println(output, "{}={}", "executor.worker_count",
                 static_cast<std::uint64_t>(stats.worker_count));
    tos::println(output, "{}={}", "executor.queue_capacity",
                 static_cast<std::uint64_t>(stats.queue_capacity));
    tos::println(output, "{}={}", "executor.queued", static_cast<std::uint64_t>(stats.queued));
    tos::println(output, "{}={}", "executor.running", static_cast<std::uint64_t>(stats.running));
    tos::println(output, "{}={}", "executor.accepted", stats.accepted);
    tos::println(output, "{}={}", "executor.rejected", stats.rejected);
    tos::println(output, "{}={}", "executor.completed", stats.completed);
}

void WriteDebugError(std::ostream& output, const Status& status) {
    tos::println(output, "error={}", status.ToString());
}

Status RegisterAppDebugHandlers(App& app, DebugController& debug, ThreadPool& executor) {
    Status status = debug.RegisterHandler(
        "status", [&app, &executor](const span<std::string>& args, std::ostream& output) {
            if (!args.empty()) {
                WriteDebugError(
                    output, debug_detail::InvalidDebugCommand("status does not accept arguments"));
                return;
            }
            WriteDebugStatus(app, executor, output);
        });
    if (!status) {
        return status;
    }

    return debug.RegisterHandler(
        "log-level", [&app, &executor](const span<std::string>& args, std::ostream& output) {
            if (args.size() != 1) {
                WriteDebugError(output, debug_detail::InvalidDebugCommand(
                                            "log-level requires exactly one argument"));
                return;
            }
            auto level = ParseDebugLogLevel(args.front());
            if (!level) {
                WriteDebugError(output, level.status());
                return;
            }
            Status set_level = app.logger().SetLevel(std::move(level).value());
            if (!set_level) {
                WriteDebugError(output, set_level);
                return;
            }
            WriteDebugStatus(app, executor, output);
        });
}

}  // namespace

App::App(AppOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {
    Status registered = RegisterAppDebugHandlers(*this, impl_->debug, impl_->executor);
    if (!registered) {
        throw std::logic_error(registered.ToString());
    }
}

App::~App() noexcept {
    try {
        if (impl_) {
            static_cast<void>(Stop());
        }
    } catch (...) {
    }
}

Status App::AddModule(std::unique_ptr<Module> module) {
    if (!impl_) {
        return InvalidState("AddModule");
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->state != AppState::kCreated) {
        return InvalidState("AddModule");
    }
    return impl_->module_registry.Register(std::move(module));
}

Status App::AddDynamicModule(const Path& path) {
    if (!impl_) {
        return InvalidState("AddDynamicModule");
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->state != AppState::kCreated) {
        return InvalidState("AddDynamicModule");
    }
    auto module = LoadDynamicModule(path);
    if (!module) {
        return std::move(module).status();
    }
    return impl_->module_registry.Register(std::move(module).value());
}

Status App::Start() {
    if (!impl_) {
        return InvalidState("Start");
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->state != AppState::kCreated) {
        return InvalidState("Start");
    }
    impl_->state = AppState::kStarting;

    auto load_order = impl_->module_registry.ResolveLoadOrder();
    if (!load_order) {
        impl_->state = AppState::kFailed;
        return std::move(load_order).status();
    }
    std::vector<std::size_t> order = std::move(load_order).value();

    const auto invoke = [this](std::size_t index, std::string_view phase) {
        impl_->callback_thread = std::this_thread::get_id();
        Status result;
        try {
            result = phase == "OnLoad"
                         ? impl_->module_registry.Get(index).OnLoad(*impl_->context)
                         : impl_->module_registry.Get(index).OnUnload(*impl_->context);
        } catch (const std::exception&) {
            result = CallbackException(impl_->module_registry.Name(index), phase);
        } catch (...) {
            result = CallbackException(impl_->module_registry.Name(index), phase);
        }
        impl_->callback_thread = std::thread::id();
        return result;
    };

    Status failure;
    for (const std::size_t index : order) {
        Status result = invoke(index, "OnLoad");
        if (!result) {
            failure = std::move(result);
            break;
        }
        impl_->module_registry.MarkLoaded(index);
    }
    if (!failure.ok()) {
        for (auto iterator = impl_->module_registry.loaded().rbegin();
             iterator != impl_->module_registry.loaded().rend(); ++iterator) {
            static_cast<void>(invoke(*iterator, "OnUnload"));
        }
        impl_->module_registry.ClearLoaded();
        impl_->state = AppState::kFailed;
        return failure;
    }
    impl_->state = AppState::kRunning;
    return Status::Ok();
}

Status App::Stop() {
    if (!impl_) {
        return InvalidState("Stop");
    }
    std::unique_lock<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->state == AppState::kStopped) {
        return Status::Ok();
    }
    if (impl_->state == AppState::kStopping || impl_->state == AppState::kStarting) {
        if (impl_->callback_thread == std::this_thread::get_id()) {
            return InvalidState("Stop");
        }
        return InvalidState("Stop");
    }

    impl_->state = AppState::kStopping;
    lock.unlock();
    Status failure = impl_->event_bus.Shutdown();
    lock.lock();
    const auto invoke = [this](std::size_t index, std::string_view phase) {
        impl_->callback_thread = std::this_thread::get_id();
        Status result;
        try {
            result = impl_->module_registry.Get(index).OnUnload(*impl_->context);
        } catch (const std::exception&) {
            result = CallbackException(impl_->module_registry.Name(index), phase);
        } catch (...) {
            result = CallbackException(impl_->module_registry.Name(index), phase);
        }
        impl_->callback_thread = std::thread::id();
        return result;
    };

    for (auto iterator = impl_->module_registry.loaded().rbegin();
         iterator != impl_->module_registry.loaded().rend(); ++iterator) {
        failure = FirstFailure(std::move(failure), invoke(*iterator, "OnUnload"));
    }
    impl_->module_registry.ClearLoaded();
    lock.unlock();
    failure = FirstFailure(std::move(failure), impl_->scheduler.Shutdown());
    failure = FirstFailure(std::move(failure), impl_->executor.Shutdown());
    lock.lock();
    impl_->state = failure.ok() ? AppState::kStopped : AppState::kFailed;
    return failure;
}

AppState App::state() const noexcept {
    if (!impl_) {
        return AppState::kFailed;
    }
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    return impl_->state;
}

Context& App::context() noexcept { return *impl_->context; }
const Config& App::config() const noexcept { return impl_->config; }
Logger& App::logger() noexcept { return impl_->logger; }
DebugController& App::debug() noexcept { return impl_->debug; }
Executor& App::executor() noexcept { return impl_->executor; }
ScheduledExecutor& App::scheduler() noexcept { return impl_->scheduler; }

}  // namespace tos
