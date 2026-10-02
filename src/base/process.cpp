#include "tos/base/process.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "native_resource.h"
#include "process_internal.h"
#include "tos/base/scope_exit.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace tos {
namespace {

bool ContainsNul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

Status ValidateProcessOptions(const ProcessOptions& options) {
    if (options.executable.empty()) {
        return Status(StatusCode::kInvalidArgument, "executable path must not be empty");
    }
    if (options.working_directory && options.working_directory->empty()) {
        return Status(StatusCode::kInvalidArgument, "working directory must not be empty");
    }
    for (const std::string& argument : options.arguments) {
        if (ContainsNul(argument)) {
            return Status(StatusCode::kInvalidArgument,
                          "process arguments must not contain NUL bytes");
        }
    }
    for (const auto& entry : options.environment_overrides) {
        if (entry.first.empty() || entry.first.find('=') != std::string::npos ||
            ContainsNul(entry.first)) {
            return Status(
                StatusCode::kInvalidArgument,
                "environment variable names must be nonempty and contain neither '=' nor NUL");
        }
        if (entry.second && ContainsNul(*entry.second)) {
            return Status(StatusCode::kInvalidArgument,
                          "environment variable values must not contain NUL bytes");
        }
    }
    return Status::Ok();
}

Status FilesystemError(const std::error_code& error, std::string_view action) {
    StatusCode code = StatusCode::kUnavailable;
    if (error == std::errc::no_such_file_or_directory) {
        code = StatusCode::kNotFound;
    } else if (error == std::errc::permission_denied ||
               error == std::errc::operation_not_permitted) {
        code = StatusCode::kPermissionDenied;
    } else if (error == std::errc::no_space_on_device || error == std::errc::too_many_files_open) {
        code = StatusCode::kResourceExhausted;
    }
    return Status(code, std::string(action) + ": " + error.message());
}

struct ResolvedPaths {
    std::filesystem::path executable;
    std::filesystem::path working_directory;
};

Result<ResolvedPaths> ResolvePaths(const ProcessOptions& options) {
    std::error_code error;
    std::filesystem::path working_directory;
    if (options.working_directory) {
        working_directory = std::filesystem::u8path(options.working_directory->utf8());
    } else {
        working_directory = std::filesystem::current_path(error);
        if (error) {
            return FilesystemError(error, "could not determine current directory");
        }
    }
    if (!working_directory.is_absolute()) {
        working_directory = std::filesystem::absolute(working_directory, error);
        if (error) {
            return FilesystemError(error, "could not resolve working directory");
        }
    }

    std::filesystem::path executable = std::filesystem::u8path(options.executable.utf8());
    if (!executable.is_absolute()) {
        executable = working_directory / executable;
    }
    executable = std::filesystem::absolute(executable, error);
    if (error) {
        return FilesystemError(error, "could not resolve executable path");
    }
    return ResolvedPaths{std::move(executable), std::move(working_directory)};
}

struct CaptureState {
    std::string output;
    std::size_t limit = 0;
    std::atomic<bool>* overflow = nullptr;
    std::exception_ptr exception;
    int error = 0;
};

#ifndef _WIN32

Status ErrnoStatus(int error, std::string_view action) {
    return FilesystemError(std::error_code(error, std::generic_category()), action);
}

struct PosixLaunchData {
    std::string executable;
    std::string working_directory;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::vector<char*> argv;
    std::vector<char*> envp;
};

Result<PosixLaunchData> BuildPosixLaunchData(const ProcessOptions& options) {
    auto paths = ResolvePaths(options);
    if (!paths) {
        return std::move(paths).status();
    }

    PosixLaunchData data;
    data.executable = paths->executable.native();
    data.working_directory = paths->working_directory.native();
    data.arguments.reserve(options.arguments.size() + 1);
    data.arguments.push_back(data.executable);
    data.arguments.insert(data.arguments.end(), options.arguments.begin(), options.arguments.end());

    std::map<std::string, std::string> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string variable(*entry);
        const std::size_t separator = variable.find('=');
        if (separator != std::string::npos && separator != 0) {
            environment.emplace(variable.substr(0, separator), variable.substr(separator + 1));
        }
    }
    for (const auto& entry : options.environment_overrides) {
        if (entry.second) {
            environment[entry.first] = *entry.second;
        } else {
            environment.erase(entry.first);
        }
    }
    data.environment.reserve(environment.size());
    for (const auto& entry : environment) {
        data.environment.push_back(entry.first + "=" + entry.second);
    }
    data.argv.reserve(data.arguments.size() + 1);
    for (std::string& argument : data.arguments) {
        data.argv.push_back(argument.data());
    }
    data.argv.push_back(nullptr);
    data.envp.reserve(data.environment.size() + 1);
    for (std::string& entry : data.environment) {
        data.envp.push_back(entry.data());
    }
    data.envp.push_back(nullptr);
    return data;
}

bool CreatePipe(detail::UniqueFd (&owners)[2]) noexcept {
    int descriptors[2];
    if (pipe(descriptors) != 0) return false;
    owners[0].Reset(descriptors[0]);
    owners[1].Reset(descriptors[1]);
    return fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) == 0 &&
           fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) == 0;
}

void ReapPosix(pid_t pid) noexcept {
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
}

void ReportChildError(int descriptor, int error) noexcept {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&error);
    std::size_t written = 0;
    while (written < sizeof(error)) {
        const ssize_t result = write(descriptor, bytes + written, sizeof(error) - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

struct SpawnedProcess {
    pid_t pid = -1;
    detail::UniqueFd stdout_descriptor;
    detail::UniqueFd stderr_descriptor;
};

Result<SpawnedProcess> SpawnPosix(const ProcessOptions& options, bool capture_output) {
    auto data = BuildPosixLaunchData(options);
    if (!data) {
        return std::move(data).status();
    }

    detail::UniqueFd launch_error[2];
    detail::UniqueFd stdout_pipe[2];
    detail::UniqueFd stderr_pipe[2];
    if (!CreatePipe(launch_error)) {
        return ErrnoStatus(errno, "could not create process launch pipe");
    }
    if (capture_output && !CreatePipe(stdout_pipe)) {
        const int saved_errno = errno;
        return ErrnoStatus(saved_errno, "could not create stdout pipe");
    }
    if (capture_output && !CreatePipe(stderr_pipe)) {
        const int saved_errno = errno;
        return ErrnoStatus(saved_errno, "could not create stderr pipe");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        const int saved_errno = errno;
        return ErrnoStatus(saved_errno, "could not start process");
    }
    if (pid == 0) {
        // Only async-signal-safe native operations after fork; no exceptions or allocation.
        launch_error[0].Reset();
        if (capture_output) {
            stdout_pipe[0].Reset();
            stderr_pipe[0].Reset();
            if (dup2(stdout_pipe[1].Get(), STDOUT_FILENO) < 0 ||
                dup2(stderr_pipe[1].Get(), STDERR_FILENO) < 0) {
                ReportChildError(launch_error[1].Get(), errno);
                _exit(127);
            }
            stdout_pipe[1].Reset();
            stderr_pipe[1].Reset();
            detail::UniqueFd input(open("/dev/null", O_RDONLY));
            if (!input || dup2(input.Get(), STDIN_FILENO) < 0) {
                ReportChildError(launch_error[1].Get(), errno);
                _exit(127);
            }
            input.Reset();
        }
        if (chdir(data->working_directory.c_str()) != 0) {
            ReportChildError(launch_error[1].Get(), errno);
            _exit(127);
        }
        execve(data->executable.c_str(), data->argv.data(), data->envp.data());
        ReportChildError(launch_error[1].Get(), errno);
        _exit(127);
    }

    auto cleanup = MakeScopeExit([&]() noexcept {
        kill(pid, SIGKILL);
        ReapPosix(pid);
    });
    launch_error[1].Reset();
    stdout_pipe[1].Reset();
    stderr_pipe[1].Reset();
    int child_error = 0;
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&child_error);
    std::size_t received = 0;
    while (received < sizeof(child_error)) {
        const ssize_t result =
            read(launch_error[0].Get(), bytes + received, sizeof(child_error) - received);
        if (result > 0) {
            received += static_cast<std::size_t>(result);
        } else if (result == 0) {
            break;
        } else if (errno != EINTR) {
            const int saved_errno = errno;
            return ErrnoStatus(saved_errno, "could not observe process launch");
        }
    }
    launch_error[0].Reset();
    if (received != 0) {
        return ErrnoStatus(child_error, "could not start process");
    }
    cleanup.Release();
    return SpawnedProcess{pid, std::move(stdout_pipe[0]), std::move(stderr_pipe[0])};
}

Result<ProcessExit> WaitPosix(pid_t pid, int options) {
    int status = 0;
    while (waitpid(pid, &status, options) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return ErrnoStatus(errno, "could not wait for process");
    }
    if (WIFEXITED(status)) {
        return ProcessExit{static_cast<std::uint32_t>(WEXITSTATUS(status)), std::nullopt};
    }
    if (WIFSIGNALED(status)) {
        return ProcessExit{std::nullopt, WTERMSIG(status)};
    }
    return Status(StatusCode::kUnavailable, "process ended with an unrecognized wait status");
}

void ReadPosixCapture(int descriptor, CaptureState* state) {
    char buffer[8192];
    bool discard = false;
    for (;;) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            state->error = errno;
            break;
        }
        if (!discard) {
            const std::size_t size = static_cast<std::size_t>(count);
            if (size > state->limit - state->output.size()) {
                state->overflow->store(true, std::memory_order_relaxed);
                discard = true;
            } else {
                try {
                    state->output.append(buffer, size);
                } catch (...) {
                    state->exception = std::current_exception();
                    discard = true;
                }
            }
        }
    }
}

#else

Status WindowsStatus(DWORD error, std::string_view action) {
    StatusCode code = StatusCode::kUnavailable;
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            code = StatusCode::kNotFound;
            break;
        case ERROR_ACCESS_DENIED:
            code = StatusCode::kPermissionDenied;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_TOO_MANY_OPEN_FILES:
            code = StatusCode::kResourceExhausted;
            break;
        default:
            break;
    }
    return Status(code, std::string(action) + ": Windows error " + std::to_string(error));
}

wchar_t ToAsciiLower(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value;
}

std::wstring WindowsEnvironmentKey(std::wstring_view value) {
    std::wstring key;
    key.reserve(value.size());
    for (const wchar_t character : value) {
        key.push_back(ToAsciiLower(character));
    }
    return key;
}

void AppendQuotedWindowsArgument(std::wstring_view argument, std::wstring* command_line) {
    const bool needs_quotes =
        argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!needs_quotes) {
        command_line->append(argument.data(), argument.size());
        return;
    }
    command_line->push_back(L'\"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            command_line->append(backslashes * 2 + 1, L'\\');
            command_line->push_back(character);
            backslashes = 0;
        } else {
            command_line->append(backslashes, L'\\');
            command_line->push_back(character);
            backslashes = 0;
        }
    }
    command_line->append(backslashes * 2, L'\\');
    command_line->push_back(L'\"');
}

Result<std::vector<wchar_t>> BuildWindowsEnvironment(const ProcessOptions& options) {
    if (options.environment_overrides.empty()) {
        return std::vector<wchar_t>();
    }
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> environment;
    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) {
        return WindowsStatus(GetLastError(), "could not read inherited environment");
    }
    auto cleanup = MakeScopeExit([&]() noexcept { FreeEnvironmentStringsW(inherited); });
    for (const wchar_t* current = inherited; *current != L'\0';
         current += std::wcslen(current) + 1) {
        const std::wstring entry(current);
        const std::size_t separator = entry.find(L'=', entry.front() == L'=' ? 1 : 0);
        if (separator != std::wstring::npos) {
            const std::wstring name = entry.substr(0, separator);
            environment[WindowsEnvironmentKey(name)] = {name, entry.substr(separator + 1)};
        }
    }

    std::map<std::wstring, std::string> override_names;
    for (const auto& entry : options.environment_overrides) {
        auto wide_name = Utf8ToWide(entry.first);
        if (!wide_name) {
            return std::move(wide_name).status();
        }
        if (!override_names.emplace(WindowsEnvironmentKey(wide_name.value()), entry.first).second) {
            return Status(StatusCode::kInvalidArgument,
                          "Windows environment overrides must not differ only by ASCII case");
        }
    }
    for (const auto& entry : options.environment_overrides) {
        auto wide_name = Utf8ToWide(entry.first);
        if (!wide_name) {
            return std::move(wide_name).status();
        }
        const std::wstring key = WindowsEnvironmentKey(wide_name.value());
        if (entry.second) {
            auto wide_value = Utf8ToWide(*entry.second);
            if (!wide_value) {
                return std::move(wide_value).status();
            }
            environment[key] = {std::move(wide_name).value(), std::move(wide_value).value()};
        } else {
            environment.erase(key);
        }
    }
    std::vector<wchar_t> result;
    for (const auto& entry : environment) {
        result.insert(result.end(), entry.second.first.begin(), entry.second.first.end());
        result.push_back(L'=');
        result.insert(result.end(), entry.second.second.begin(), entry.second.second.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

struct SpawnedProcess {
    detail::UniqueHandle process;
    DWORD process_id = 0;
    detail::UniqueHandle stdout_handle;
    detail::UniqueHandle stderr_handle;
};

bool CreateWindowsPipe(detail::UniqueHandle& read, detail::UniqueHandle& write,
                       SECURITY_ATTRIBUTES* attributes) noexcept {
    HANDLE native_read = nullptr;
    HANDLE native_write = nullptr;
    const BOOL created = CreatePipe(&native_read, &native_write, attributes, 0);
    read.Reset(native_read);
    write.Reset(native_write);
    return created && SetHandleInformation(read.Get(), HANDLE_FLAG_INHERIT, 0);
}

Result<SpawnedProcess> SpawnWindows(const ProcessOptions& options, bool capture_output) {
    auto paths = ResolvePaths(options);
    if (!paths) {
        return std::move(paths).status();
    }
    auto wide_executable = Utf8ToWide(paths->executable.u8string());
    if (!wide_executable) {
        return std::move(wide_executable).status();
    }
    auto wide_directory = Utf8ToWide(paths->working_directory.u8string());
    if (!wide_directory) {
        return std::move(wide_directory).status();
    }
    std::wstring command_line;
    AppendQuotedWindowsArgument(wide_executable.value(), &command_line);
    for (const std::string& argument : options.arguments) {
        auto wide_argument = Utf8ToWide(argument);
        if (!wide_argument) {
            return std::move(wide_argument).status();
        }
        command_line.push_back(L' ');
        AppendQuotedWindowsArgument(wide_argument.value(), &command_line);
    }
    command_line.push_back(L'\0');
    auto environment = BuildWindowsEnvironment(options);
    if (!environment) {
        return std::move(environment).status();
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    detail::UniqueHandle stdout_read;
    detail::UniqueHandle stdout_write;
    detail::UniqueHandle stderr_read;
    detail::UniqueHandle stderr_write;
    detail::UniqueHandle stdin_handle;
    if (capture_output) {
        if (!CreateWindowsPipe(stdout_read, stdout_write, &attributes) ||
            !CreateWindowsPipe(stderr_read, stderr_write, &attributes)) {
            return WindowsStatus(GetLastError(), "could not create output pipes");
        }
        stdin_handle.Reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!stdin_handle) {
            return WindowsStatus(GetLastError(), "could not create closed process input");
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (capture_output) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_handle.Get();
        startup.hStdOutput = stdout_write.Get();
        startup.hStdError = stderr_write.Get();
    }
    PROCESS_INFORMATION information{};
    const BOOL created =
        CreateProcessW(wide_executable->c_str(), command_line.data(), nullptr, nullptr,
                       capture_output ? TRUE : FALSE, CREATE_UNICODE_ENVIRONMENT,
                       environment->empty() ? nullptr : environment->data(),
                       wide_directory->c_str(), &startup, &information);
    const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    detail::UniqueHandle process(information.hProcess);
    detail::UniqueHandle thread(information.hThread);
    if (!created) return WindowsStatus(create_error, "could not start process");
    return SpawnedProcess{std::move(process), information.dwProcessId, std::move(stdout_read),
                          std::move(stderr_read)};
}

Result<ProcessExit> WaitWindows(HANDLE process) {
    if (WaitForSingleObject(process, INFINITE) != WAIT_OBJECT_0) {
        return WindowsStatus(GetLastError(), "could not wait for process");
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process, &code)) {
        return WindowsStatus(GetLastError(), "could not obtain process exit code");
    }
    return ProcessExit{static_cast<std::uint32_t>(code), std::nullopt};
}

void ReadWindowsCapture(HANDLE handle, CaptureState* state) {
    char buffer[8192];
    bool discard = false;
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE) {
                state->error = static_cast<int>(error);
            }
            break;
        }
        if (count == 0) {
            break;
        }
        if (!discard) {
            const std::size_t size = static_cast<std::size_t>(count);
            if (size > state->limit - state->output.size()) {
                state->overflow->store(true, std::memory_order_relaxed);
                discard = true;
            } else {
                try {
                    state->output.append(buffer, size);
                } catch (...) {
                    state->exception = std::current_exception();
                    discard = true;
                }
            }
        }
    }
}

#endif

#ifdef _WIN32
using CaptureResource = detail::UniqueHandle;
#else
using CaptureResource = detail::UniqueFd;
#endif
struct CaptureJob {
    CaptureResource stream;
    CaptureState* state;
};

void CaptureEntry(void* argument) noexcept {
    auto& job = *static_cast<CaptureJob*>(argument);
#ifdef _WIN32
    ReadWindowsCapture(job.stream.Get(), job.state);
#else
    ReadPosixCapture(job.stream.Get(), job.state);
#endif
    job.stream.Reset();
}

std::thread StartCaptureThread(void (*entry)(void*), void* argument, std::uint64_t, void*) {
    return std::thread(entry, argument);
}

}  // namespace

struct Process::State {
#ifdef _WIN32
    detail::UniqueHandle process;
    DWORD process_id = 0;
#else
    pid_t process_id = -1;
#endif
    std::optional<ProcessExit> exit;
};

Process::Process(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

Process::Process(Process&& other) noexcept = default;

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        Process previous(std::move(state_));
        state_ = std::move(other.state_);
    }
    return *this;
}

Process::~Process() {
    if (!state_ || state_->exit) return;
#ifdef _WIN32
    TerminateProcess(state_->process.Get(), 1);
    WaitForSingleObject(state_->process.Get(), INFINITE);
#else
    kill(state_->process_id, SIGKILL);
    ReapPosix(state_->process_id);
#endif
}

Result<Process> Process::Start(const ProcessOptions& options) {
    Status valid = ValidateProcessOptions(options);
    if (!valid) {
        return valid;
    }
    auto state = std::make_unique<State>();
#ifdef _WIN32
    auto spawned = SpawnWindows(options, false);
    if (!spawned) {
        return std::move(spawned).status();
    }
    state->process = std::move(spawned->process);
    state->process_id = spawned->process_id;
#else
    auto spawned = SpawnPosix(options, false);
    if (!spawned) {
        return std::move(spawned).status();
    }
    state->process_id = spawned->pid;
#endif
    return Process(std::move(state));
}

Result<ProcessExit> Process::Wait() {
    if (!state_) {
        return Status(StatusCode::kFailedPrecondition, "cannot wait on a moved-from process");
    }
    if (state_->exit) {
        return *state_->exit;
    }
#ifdef _WIN32
    auto exit = WaitWindows(state_->process.Get());
#else
    auto exit = WaitPosix(state_->process_id, 0);
#endif
    if (!exit) {
        return std::move(exit).status();
    }
    state_->exit = exit.value();
    return std::move(exit).value();
}

Status Process::Terminate() {
    if (!state_) {
        return Status(StatusCode::kFailedPrecondition, "cannot terminate a moved-from process");
    }
    if (state_->exit) {
        return Status::Ok();
    }
#ifdef _WIN32
    if (!TerminateProcess(state_->process.Get(), 1)) {
        const DWORD error = GetLastError();
        DWORD exit_code = STILL_ACTIVE;
        if (error != ERROR_ACCESS_DENIED ||
            !GetExitCodeProcess(state_->process.Get(), &exit_code) || exit_code == STILL_ACTIVE) {
            return WindowsStatus(error, "could not terminate process");
        }
    }
#else
    if (kill(state_->process_id, SIGKILL) != 0 && errno != ESRCH) {
        return ErrnoStatus(errno, "could not terminate process");
    }
#endif
    return Status::Ok();
}

std::uint64_t Process::id() const noexcept {
    if (!state_) {
        return 0;
    }
#ifdef _WIN32
    return state_->process_id;
#else
    return static_cast<std::uint64_t>(state_->process_id);
#endif
}

Result<CommandResult> detail::RunCommandWithThreadFactory(const ProcessOptions& process_options,
                                                          const RunCommandOptions& options,
                                                          CaptureThreadFactory factory,
                                                          void* context) {
    Status valid = ValidateProcessOptions(process_options);
    if (!valid) {
        return valid;
    }
    if (options.timeout && options.timeout->Nanoseconds() <= 0) {
        return Status(StatusCode::kInvalidArgument, "process timeout must be positive");
    }

#ifdef _WIN32
    auto spawned = SpawnWindows(process_options, true);
#else
    auto spawned = SpawnPosix(process_options, true);
#endif
    if (!spawned) {
        return std::move(spawned).status();
    }

    bool reaped = false;
    auto stop_and_reap = [&]() noexcept {
        if (reaped) return;
#ifdef _WIN32
        TerminateProcess(spawned->process.Get(), 1);
        WaitForSingleObject(spawned->process.Get(), INFINITE);
#else
        kill(spawned->pid, SIGKILL);
        ReapPosix(spawned->pid);
#endif
        reaped = true;
    };
    auto child_cleanup = MakeScopeExit(stop_and_reap);
    std::atomic<bool> overflow{false};
    CaptureState stdout_state{std::string(), options.max_output_bytes_per_stream, &overflow,
                              nullptr, 0};
    CaptureState stderr_state{std::string(), options.max_output_bytes_per_stream, &overflow,
                              nullptr, 0};
#ifdef _WIN32
    CaptureJob stdout_job{std::move(spawned->stdout_handle), &stdout_state};
    CaptureJob stderr_job{std::move(spawned->stderr_handle), &stderr_state};
    const auto child_id = static_cast<std::uint64_t>(spawned->process_id);
#else
    CaptureJob stdout_job{std::move(spawned->stdout_descriptor), &stdout_state};
    CaptureJob stderr_job{std::move(spawned->stderr_descriptor), &stderr_state};
    const auto child_id = static_cast<std::uint64_t>(spawned->pid);
#endif
    std::thread stdout_reader;
    std::thread stderr_reader;
    auto reader_cleanup = MakeScopeExit([&]() noexcept {
        // Killing/reaping before joining ensures a blocked reader sees pipe EOF.
        stop_and_reap();
        if (stdout_reader.joinable()) stdout_reader.join();
        if (stderr_reader.joinable()) stderr_reader.join();
    });
    stdout_reader = factory(CaptureEntry, &stdout_job, child_id, context);
    stderr_reader = factory(CaptureEntry, &stderr_job, child_id, context);

    enum class StopReason { kNone, kTimeout, kOutputLimit };
    StopReason stop_reason = StopReason::kNone;
    std::optional<ProcessExit> exit;
    const auto deadline = options.timeout
                              ? std::optional<std::chrono::steady_clock::time_point>(
                                    std::chrono::steady_clock::now() +
                                    std::chrono::nanoseconds(options.timeout->Nanoseconds()))
                              : std::nullopt;
    while (!exit) {
#ifdef _WIN32
        const DWORD wait_result = WaitForSingleObject(spawned->process.Get(), 1);
        if (wait_result == WAIT_OBJECT_0) {
            reaped = true;
            auto waited = WaitWindows(spawned->process.Get());
            if (!waited) return std::move(waited).status();
            exit = std::move(waited).value();
        } else if (wait_result == WAIT_FAILED) {
            return WindowsStatus(GetLastError(), "could not wait for process");
        }
#else
        int raw_status = 0;
        const pid_t waited = waitpid(spawned->pid, &raw_status, WNOHANG);
        if (waited == spawned->pid) {
            reaped = true;
            if (WIFEXITED(raw_status)) {
                exit =
                    ProcessExit{static_cast<std::uint32_t>(WEXITSTATUS(raw_status)), std::nullopt};
            } else if (WIFSIGNALED(raw_status)) {
                exit = ProcessExit{std::nullopt, WTERMSIG(raw_status)};
            } else {
                return Status(StatusCode::kUnavailable,
                              "process ended with an unrecognized wait status");
            }
        } else if (waited < 0 && errno != EINTR) {
            const int error = errno;
            if (error == ECHILD) reaped = true;
            return ErrnoStatus(error, "could not wait for process");
        }
#endif
        if (exit) break;
        if (overflow.load(std::memory_order_relaxed)) {
            stop_reason = StopReason::kOutputLimit;
        } else if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            stop_reason = StopReason::kTimeout;
        }
        if (stop_reason != StopReason::kNone) {
#ifdef _WIN32
            if (!TerminateProcess(spawned->process.Get(), 1)) {
                const DWORD error = GetLastError();
                if (error != ERROR_ACCESS_DENIED)
                    return WindowsStatus(error, "could not terminate process");
            }
            if (WaitForSingleObject(spawned->process.Get(), INFINITE) != WAIT_OBJECT_0)
                return WindowsStatus(GetLastError(), "could not wait for process");
#else
            if (kill(spawned->pid, SIGKILL) != 0 && errno != ESRCH)
                return ErrnoStatus(errno, "could not terminate process");
            ReapPosix(spawned->pid);
#endif
            reaped = true;
            break;
        }
    }
    stdout_reader.join();
    stderr_reader.join();
    reader_cleanup.Release();
    child_cleanup.Release();
    if (stdout_state.exception) {
        std::rethrow_exception(stdout_state.exception);
    }
    if (stderr_state.exception) {
        std::rethrow_exception(stderr_state.exception);
    }
    if (stdout_state.error != 0 || stderr_state.error != 0) {
#ifdef _WIN32
        return WindowsStatus(
            static_cast<DWORD>(stdout_state.error != 0 ? stdout_state.error : stderr_state.error),
            "could not read process output");
#else
        return ErrnoStatus(stdout_state.error != 0 ? stdout_state.error : stderr_state.error,
                           "could not read process output");
#endif
    }
    if (stop_reason == StopReason::kOutputLimit || overflow.load(std::memory_order_relaxed)) {
        return Status(StatusCode::kResourceExhausted, "process output exceeded capture limit");
    }
    if (stop_reason == StopReason::kTimeout) {
        return Status(StatusCode::kTimeout, "process execution timed out");
    }
    return CommandResult{std::move(*exit), std::move(stdout_state.output),
                         std::move(stderr_state.output)};
}

Result<CommandResult> RunCommand(const ProcessOptions& options,
                                 const RunCommandOptions& run_options) {
    return detail::RunCommandWithThreadFactory(options, run_options, StartCaptureThread, nullptr);
}

}  // namespace tos
