#include "tos/app/app.h"

#include <exception>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

#include "context_impl.h"
#include "service_registry.h"

namespace tos {

struct App::Impl {
    struct ModuleEntry {
        std::unique_ptr<Module> module;
        std::string name;
        std::vector<std::string> dependencies;
    };

    explicit Impl(AppOptions options)
        : name(std::move(options.name)),
          config(std::move(options.config)),
          logger(std::move(options.log)),
          context(service_registry, config, logger) {}

    std::string name;
    Config config;
    Logger logger;
    ServiceRegistry service_registry;
    ContextImpl context;
    std::vector<ModuleEntry> modules;
    std::map<std::string, std::size_t> indices;
    std::vector<std::size_t> order;
    std::vector<std::size_t> loaded;
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
    if (!module) {
        return Status(StatusCode::kInvalidArgument, "module must not be null");
    }
    Module* module_ptr = module.get();
    std::string name = module_ptr->Name();
    if (name.empty()) {
        return Status(StatusCode::kInvalidArgument, "module name must not be empty");
    }
    if (impl_->indices.count(name) != 0) {
        return Status(StatusCode::kAlreadyExists, "module name is already registered");
    }
    std::vector<std::string> dependencies = module_ptr->Dependencies();
    for (const std::string& dependency : dependencies) {
        if (dependency.empty() || dependency == name) {
            return Status(StatusCode::kInvalidArgument, "module dependency is invalid");
        }
    }
    const std::size_t index = impl_->modules.size();
    impl_->indices.emplace(name, index);
    impl_->modules.push_back({std::move(module), std::move(name), std::move(dependencies)});
    return Status::Ok();
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

    std::vector<std::vector<std::size_t>> outgoing(impl_->modules.size());
    std::vector<std::size_t> indegree(impl_->modules.size(), 0);
    for (std::size_t index = 0; index < impl_->modules.size(); ++index) {
        for (const std::string& dependency : impl_->modules[index].dependencies) {
            const auto iterator = impl_->indices.find(dependency);
            if (iterator == impl_->indices.end()) {
                impl_->state = AppState::kFailed;
                return Status(StatusCode::kNotFound, "module dependency is not registered");
            }
            outgoing[iterator->second].push_back(index);
            ++indegree[index];
        }
    }

    std::priority_queue<std::pair<std::string, std::size_t>,
                        std::vector<std::pair<std::string, std::size_t>>,
                        std::greater<std::pair<std::string, std::size_t>>>
        ready;
    for (std::size_t index = 0; index < indegree.size(); ++index) {
        if (indegree[index] == 0) {
            ready.emplace(impl_->modules[index].name, index);
        }
    }
    impl_->order.clear();
    while (!ready.empty()) {
        const auto current = ready.top();
        ready.pop();
        impl_->order.push_back(current.second);
        for (const std::size_t dependent : outgoing[current.second]) {
            if (--indegree[dependent] == 0) {
                ready.emplace(impl_->modules[dependent].name, dependent);
            }
        }
    }
    if (impl_->order.size() != impl_->modules.size()) {
        impl_->state = AppState::kFailed;
        return Status(StatusCode::kInvalidArgument, "module dependency graph contains a cycle");
    }

    const auto invoke = [this](std::size_t index, std::string_view phase) {
        impl_->callback_thread = std::this_thread::get_id();
        Status result;
        try {
            result = phase == "OnLoad" ? impl_->modules[index].module->OnLoad(impl_->context)
                                       : impl_->modules[index].module->OnUnload(impl_->context);
        } catch (const std::exception&) {
            result = CallbackException(impl_->modules[index].name, phase);
        } catch (...) {
            result = CallbackException(impl_->modules[index].name, phase);
        }
        impl_->callback_thread = std::thread::id();
        return result;
    };

    Status failure;
    for (const std::size_t index : impl_->order) {
        Status result = invoke(index, "OnLoad");
        if (!result) {
            failure = std::move(result);
            break;
        }
        impl_->loaded.push_back(index);
    }
    if (!failure.ok()) {
        for (auto iterator = impl_->loaded.rbegin(); iterator != impl_->loaded.rend(); ++iterator) {
            static_cast<void>(invoke(*iterator, "OnUnload"));
        }
        impl_->loaded.clear();
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
            result = impl_->modules[index].module->OnUnload(impl_->context);
        } catch (const std::exception&) {
            result = CallbackException(impl_->modules[index].name, phase);
        } catch (...) {
            result = CallbackException(impl_->modules[index].name, phase);
        }
        impl_->callback_thread = std::thread::id();
        return result;
    };

    for (auto iterator = impl_->loaded.rbegin(); iterator != impl_->loaded.rend(); ++iterator) {
        failure = FirstFailure(std::move(failure), invoke(*iterator, "OnUnload"));
    }
    impl_->loaded.clear();
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
