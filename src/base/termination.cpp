#include "tos/base/termination.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

#include "native_resource.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace tos {
namespace detail {
struct TerminationState {
    std::atomic<bool> installed{false};
    // Zero means no request; positive values encode TerminationReason + 1.
    std::atomic<int> request{0};
#ifdef _WIN32
    UniqueHandle event;
#else
    UniqueFd read_fd;
    UniqueFd write_fd;
    std::array<struct sigaction, 2> previous{};
#endif

    void Notify(TerminationReason reason) noexcept {
        const int value = static_cast<int>(reason);
        if (value < 0 || value > 3) return;
        const int encoded = value + 1;
        int expected = 0;
        if (!request.compare_exchange_strong(expected, encoded)) return;
#ifdef _WIN32
        SetEvent(event.Get());
#else
        const unsigned char byte = 1;
        ssize_t result;
        do {
            result = write(write_fd.Get(), &byte, 1);
        } while (result < 0 && errno == EINTR);
#endif
    }
};
}  // namespace detail

namespace {
// Lock-free atomics are required in asynchronous POSIX signal handlers.
static_assert(std::atomic<detail::TerminationState*>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
std::atomic<detail::TerminationState*> active_state{nullptr};
std::atomic<int> active_handlers{0};
std::mutex installation_mutex;

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD event) noexcept {
    TerminationReason reason;
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            reason = TerminationReason::kInterrupt;
            break;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            reason = TerminationReason::kConsoleClose;
            break;
        default:
            return FALSE;
    }
    active_handlers.fetch_add(1);
    auto* state = active_state.load();
    if (state) state->Notify(reason);
    active_handlers.fetch_sub(1);
    return state ? TRUE : FALSE;
}
#else
void SignalHandler(int signal) noexcept {
    const int saved_errno = errno;
    // Increment before loading the pointer so Uninstall can safely retire native resources.
    active_handlers.fetch_add(1);
    auto* state = active_state.load();
    if (state)
        state->Notify(signal == SIGINT ? TerminationReason::kInterrupt
                                       : TerminationReason::kTerminate);
    active_handlers.fetch_sub(1);
    errno = saved_errno;
}
#endif
}  // namespace

TerminationController::TerminationController()
    : state_(std::make_unique<detail::TerminationState>()) {}
TerminationController::~TerminationController() { Uninstall(); }

Status TerminationController::Install() {
    std::lock_guard<std::mutex> guard(installation_mutex);
    if (IsInstalled()) return Status::Ok();
    if (active_state.load())
        return Status(StatusCode::kAlreadyExists, "another termination controller is active");
    state_->request.store(0);
#ifdef _WIN32
    state_->event.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!state_->event) return WindowsError(GetLastError(), "create termination event");
    active_state.store(state_.get());
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        const DWORD error = GetLastError();
        active_state.store(nullptr);
        while (active_handlers.load() != 0) std::this_thread::yield();
        state_->event.Reset();
        return WindowsError(error, "install console termination handler");
    }
#else
    int descriptors[2];
    if (pipe(descriptors) != 0) return ErrnoError(errno, "create termination pipe");
    detail::UniqueFd read_fd(descriptors[0]);
    detail::UniqueFd write_fd(descriptors[1]);
    for (int descriptor : descriptors) {
        const int fd_flags = fcntl(descriptor, F_GETFD);
        const int flags = fcntl(descriptor, F_GETFL);
        if (fd_flags < 0 || flags < 0 || fcntl(descriptor, F_SETFD, fd_flags | FD_CLOEXEC) < 0 ||
            fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
            const int error = errno;
            return ErrnoError(error, "configure termination pipe");
        }
    }
    state_->read_fd = std::move(read_fd);
    state_->write_fd = std::move(write_fd);
    struct sigaction action = {};
    action.sa_handler = SignalHandler;
    sigemptyset(&action.sa_mask);
    active_state.store(state_.get());
    const bool interrupt_installed = sigaction(SIGINT, &action, &state_->previous[0]) == 0;
    if (!interrupt_installed || sigaction(SIGTERM, &action, &state_->previous[1]) != 0) {
        const int error = errno;
        if (interrupt_installed) sigaction(SIGINT, &state_->previous[0], nullptr);
        active_state.store(nullptr);
        while (active_handlers.load() != 0) std::this_thread::yield();
        state_->read_fd.Reset();
        state_->write_fd.Reset();
        return ErrnoError(error, "install termination signal handlers");
    }
#endif
    state_->installed.store(true);
    return Status::Ok();
}

Result<TerminationReason> TerminationController::Wait(std::chrono::milliseconds timeout) {
    if (!IsInstalled())
        return Status(StatusCode::kFailedPrecondition, "termination controller is not installed");
    if (timeout.count() < 0)
        return Status(StatusCode::kInvalidArgument, "termination timeout is negative");
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (Requested()) return reason();
        const bool infinite = timeout == std::chrono::milliseconds::max();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        const auto remaining =
            infinite ? timeout : std::max(timeout - elapsed, std::chrono::milliseconds::zero());
#ifdef _WIN32
        const DWORD duration =
            infinite ? INFINITE
                     : static_cast<DWORD>(std::min<std::int64_t>(remaining.count(), INFINITE - 1));
        const DWORD result = WaitForSingleObject(state_->event.Get(), duration);
        if (result == WAIT_FAILED) return WindowsError(GetLastError(), "wait for termination");
#else
        const int duration = infinite ? -1
                                      : static_cast<int>(std::min<std::int64_t>(
                                            remaining.count(), std::numeric_limits<int>::max()));
        pollfd descriptor{state_->read_fd.Get(), POLLIN, 0};
        const int result = poll(&descriptor, 1, duration);
        if (result < 0) {
            if (errno == EINTR) continue;
            return ErrnoError(errno, "wait for termination");
        }
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return Status(StatusCode::kUnavailable, "termination pipe unavailable");
        }
        // Leave the byte unread: all waiters observe the same latched notification.
#endif
        if (Requested()) return reason();
        if (!infinite && std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start) >= timeout)
            return Status(StatusCode::kTimeout, "termination wait timed out");
    }
}

void TerminationController::RequestStop(TerminationReason reason) noexcept {
    if (IsInstalled()) state_->Notify(reason);
}

void TerminationController::Uninstall() noexcept {
    std::lock_guard<std::mutex> guard(installation_mutex);
    if (!state_->installed.exchange(false)) return;
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
#else
    sigaction(SIGINT, &state_->previous[0], nullptr);
    sigaction(SIGTERM, &state_->previous[1], nullptr);
#endif
    active_state.store(nullptr);
    while (active_handlers.load() != 0) std::this_thread::yield();
#ifdef _WIN32
    state_->event.Reset();
#else
    state_->read_fd.Reset();
    state_->write_fd.Reset();
#endif
    state_->request.store(0);
}

bool TerminationController::IsInstalled() const noexcept { return state_->installed.load(); }
bool TerminationController::Requested() const noexcept { return state_->request.load() != 0; }
TerminationReason TerminationController::reason() const noexcept {
    const int value = state_->request.load();
    return value == 0 ? TerminationReason::kExplicitRequest
                      : static_cast<TerminationReason>(value - 1);
}
}  // namespace tos
