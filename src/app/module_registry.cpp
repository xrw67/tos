#include "module_registry.h"

#include <functional>
#include <queue>
#include <utility>

namespace tos {

Status ModuleRegistry::Register(std::unique_ptr<Module> module) {
    if (!module) {
        return Status(StatusCode::kInvalidArgument, "module must not be null");
    }
    Module* const module_ptr = module.get();
    std::string name = module_ptr->Name();
    if (name.empty()) {
        return Status(StatusCode::kInvalidArgument, "module name must not be empty");
    }
    if (indices_.count(name) != 0) {
        return Status(StatusCode::kAlreadyExists, "module name is already registered");
    }
    std::vector<std::string> dependencies = module_ptr->Dependencies();
    for (const std::string& dependency : dependencies) {
        if (dependency.empty() || dependency == name) {
            return Status(StatusCode::kInvalidArgument, "module dependency is invalid");
        }
    }
    const std::size_t index = modules_.size();
    indices_.emplace(name, index);
    modules_.push_back({std::move(module), std::move(name), std::move(dependencies)});
    return Status::Ok();
}

Result<std::vector<std::size_t>> ModuleRegistry::ResolveLoadOrder() const {
    std::vector<std::vector<std::size_t>> outgoing(modules_.size());
    std::vector<std::size_t> indegree(modules_.size(), 0);
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        for (const std::string& dependency : modules_[index].dependencies) {
            const auto iterator = indices_.find(dependency);
            if (iterator == indices_.end()) {
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
            ready.emplace(modules_[index].name, index);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(modules_.size());
    while (!ready.empty()) {
        const auto current = ready.top();
        ready.pop();
        order.push_back(current.second);
        for (const std::size_t dependent : outgoing[current.second]) {
            if (--indegree[dependent] == 0) {
                ready.emplace(modules_[dependent].name, dependent);
            }
        }
    }
    if (order.size() != modules_.size()) {
        return Status(StatusCode::kInvalidArgument, "module dependency graph contains a cycle");
    }
    return order;
}

Module& ModuleRegistry::Get(std::size_t index) noexcept { return *modules_[index].module; }

const std::string& ModuleRegistry::Name(std::size_t index) const noexcept {
    return modules_[index].name;
}

void ModuleRegistry::MarkLoaded(std::size_t index) { loaded_.push_back(index); }

}  // namespace tos
