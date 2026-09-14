#include "tos/app/app.h"

#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "module_registry.h"
#include "service_registry.h"

namespace tos {

struct App::Impl {
    explicit Impl(AppOptions options)
        : name(std::move(options.name)),
          config(std::move(options.config)),
          logger(std::move(options.log)),
          context(service_registry, config, logger) {}

    std::string name;
    Config config;
    Logger logger;
    ServiceRegistry service_registry;
    Context context;
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

}  // namespace

App::App(AppOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

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
            result = phase == "OnLoad" ? impl_->module_registry.Get(index).OnLoad(impl_->context)
                                       : impl_->module_registry.Get(index).OnUnload(impl_->context);
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
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (impl_->state == AppState::kCreated || impl_->state == AppState::kStopped) {
        return Status::Ok();
    }
    if (impl_->state == AppState::kStopping || impl_->state == AppState::kStarting) {
        if (impl_->callback_thread == std::this_thread::get_id()) {
            return InvalidState("Stop");
        }
        return InvalidState("Stop");
    }

    impl_->state = AppState::kStopping;
    Status failure;
    const auto invoke = [this](std::size_t index, std::string_view phase) {
        impl_->callback_thread = std::this_thread::get_id();
        Status result;
        try {
            result = impl_->module_registry.Get(index).OnUnload(impl_->context);
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

Context& App::context() noexcept { return impl_->context; }
const Config& App::config() const noexcept { return impl_->config; }
Logger& App::logger() noexcept { return impl_->logger; }

}  // namespace tos
