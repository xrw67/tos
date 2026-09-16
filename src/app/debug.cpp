#include "tos/app/debug.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "tos/base/string.h"

namespace tos {
namespace {

Status ValidateCommandName(std::string_view command) {
    if (command.empty()) {
        return debug_detail::InvalidDebugCommand("command name must not be empty");
    }
    for (const char character : command) {
        if (character == '\0' || string_detail::IsAsciiWhitespace(character)) {
            return debug_detail::InvalidDebugCommand(
                "command name must not contain whitespace or NUL");
        }
    }
    return Status::Ok();
}

Status OutputUnavailable() {
    return Status(StatusCode::kUnavailable, "debug command output stream failed");
}

Status SplitCommandLine(std::string_view command_line, std::vector<std::string>* tokens) {
    if (command_line.find('\0') != std::string_view::npos) {
        return debug_detail::InvalidDebugCommand("command line must not contain NUL");
    }

    std::size_t position = 0;
    while (position < command_line.size()) {
        while (position < command_line.size() &&
               string_detail::IsAsciiWhitespace(command_line[position])) {
            ++position;
        }
        const std::size_t begin = position;
        while (position < command_line.size() &&
               !string_detail::IsAsciiWhitespace(command_line[position])) {
            ++position;
        }
        if (begin != position) {
            tokens->emplace_back(command_line.substr(begin, position - begin));
        }
    }
    if (tokens->empty()) {
        return debug_detail::InvalidDebugCommand("command line must not be empty");
    }
    return Status::Ok();
}

}  // namespace

class DebugController::Impl {
   public:
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<DebugHandler>> handlers;
};

DebugController::DebugController() : impl_(std::make_unique<Impl>()) {}

DebugController::~DebugController() noexcept = default;

Status DebugController::Execute(std::string_view command_line, std::ostream& output) {
    std::vector<std::string> tokens;
    Status split = SplitCommandLine(command_line, &tokens);
    if (!split) {
        return split;
    }
    std::shared_ptr<DebugHandler> handler;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto iterator = impl_->handlers.find(tokens.front());
        if (iterator == impl_->handlers.end()) {
            return Status(StatusCode::kNotFound, "debug command is not registered");
        }
        handler = iterator->second;
    }
    if (!output) {
        return OutputUnavailable();
    }

    const span<std::string> args = span<std::string>(tokens).subspan(1);
    (*handler)(args, output);
    return output ? Status::Ok() : OutputUnavailable();
}

Status DebugController::RegisterHandler(const std::string& command, DebugHandler handler) {
    Status name_status = ValidateCommandName(command);
    if (!name_status) {
        return name_status;
    }
    if (!handler) {
        return debug_detail::InvalidDebugCommand("handler must not be empty");
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->handlers.count(command) != 0) {
        return Status(StatusCode::kAlreadyExists, "debug command is already registered");
    }
    impl_->handlers.emplace(std::move(command), std::make_shared<DebugHandler>(std::move(handler)));
    return Status::Ok();
}

Status DebugController::UnregisterHandler(std::string_view command) {
    Status name_status = ValidateCommandName(command);
    if (!name_status) {
        return name_status;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto iterator = impl_->handlers.find(std::string(command));
    if (iterator == impl_->handlers.end()) {
        return Status(StatusCode::kNotFound, "debug command is not registered");
    }
    impl_->handlers.erase(iterator);
    return Status::Ok();
}

}  // namespace tos
