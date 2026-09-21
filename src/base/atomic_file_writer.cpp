#include "atomic_file_writer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tos::detail {
namespace {

Status FileError(std::string_view action, const Path& path, std::string_view detail) {
    return Status(StatusCode::kUnavailable,
                  std::string(action) + " '" + path.utf8() + "': " + std::string(detail));
}

std::filesystem::path NativePath(const Path& path) { return std::filesystem::u8path(path.utf8()); }

}  // namespace

struct AtomicFileWriter::Impl final {
    explicit Impl(Path target) : target(std::move(target)) {}

    Path target;
    std::filesystem::path target_native;
    std::filesystem::path temporary_native;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif
    bool committed = false;
};

Result<AtomicFileWriter> AtomicFileWriter::Create(const Path& target) {
    if (target.empty()) {
        return Status(StatusCode::kInvalidArgument, "destination path must not be empty");
    }
    auto impl = std::make_unique<Impl>(target);
    impl->target_native = NativePath(target);
    const auto parent = impl->target_native.parent_path().empty()
                            ? std::filesystem::path(".")
                            : impl->target_native.parent_path();
#ifdef _WIN32
    static std::atomic<std::uint64_t> next_id{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        impl->temporary_native =
            parent /
            (impl->target_native.filename().wstring() + L".tos-http-tmp-" +
             std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(next_id.fetch_add(1)));
        impl->handle = CreateFileW(impl->temporary_native.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (impl->handle != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            return FileError("could not create temporary file", target,
                             std::system_category().message(static_cast<int>(GetLastError())));
        }
    }
    if (impl->handle == INVALID_HANDLE_VALUE) {
        return FileError("could not create temporary file", target, "name collision limit reached");
    }
#else
    std::string temporary =
        (parent / (impl->target_native.filename().string() + ".tos-http-tmp-XXXXXX")).string();
    impl->descriptor = mkstemp(temporary.data());
    if (impl->descriptor == -1) {
        return FileError("could not create temporary file", target, std::strerror(errno));
    }
    impl->temporary_native = std::filesystem::path(std::move(temporary));
#endif
    return AtomicFileWriter(std::move(impl));
}

AtomicFileWriter::AtomicFileWriter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

AtomicFileWriter::AtomicFileWriter(AtomicFileWriter&& other) noexcept = default;

AtomicFileWriter& AtomicFileWriter::operator=(AtomicFileWriter&& other) noexcept = default;

AtomicFileWriter::~AtomicFileWriter() {
    if (!impl_) {
        return;
    }
#ifdef _WIN32
    if (impl_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
    if (!impl_->committed) {
        DeleteFileW(impl_->temporary_native.c_str());
    }
#else
    if (impl_->descriptor != -1) {
        close(impl_->descriptor);
        impl_->descriptor = -1;
    }
    if (!impl_->committed) {
        unlink(impl_->temporary_native.c_str());
    }
#endif
}

Status AtomicFileWriter::Write(std::string_view bytes) {
    if (!impl_ || impl_->committed) {
        return Status(StatusCode::kFailedPrecondition, "temporary file is not writable");
    }
#ifdef _WIN32
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(impl_->handle, bytes.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            return FileError("could not write temporary file", impl_->target,
                             std::system_category().message(static_cast<int>(GetLastError())));
        }
        offset += written;
    }
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written =
            write(impl_->descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FileError("could not write temporary file", impl_->target, std::strerror(errno));
        }
        if (written == 0) {
            return FileError("could not write temporary file", impl_->target, "short write");
        }
        offset += static_cast<std::size_t>(written);
    }
#endif
    return Status::Ok();
}

Status AtomicFileWriter::Commit() {
    if (!impl_ || impl_->committed) {
        return Status(StatusCode::kFailedPrecondition, "temporary file is not pending");
    }
#ifdef _WIN32
    if (!FlushFileBuffers(impl_->handle) || !CloseHandle(impl_->handle)) {
        const auto error = GetLastError();
        impl_->handle = INVALID_HANDLE_VALUE;
        return FileError("could not finalize temporary file", impl_->target,
                         std::system_category().message(static_cast<int>(error)));
    }
    impl_->handle = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(impl_->temporary_native.c_str(), impl_->target_native.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return FileError("could not atomically replace file", impl_->target,
                         std::system_category().message(static_cast<int>(GetLastError())));
    }
#else
    if (close(impl_->descriptor) != 0) {
        const int error = errno;
        impl_->descriptor = -1;
        return FileError("could not finalize temporary file", impl_->target, std::strerror(error));
    }
    impl_->descriptor = -1;
    if (rename(impl_->temporary_native.c_str(), impl_->target_native.c_str()) != 0) {
        return FileError("could not atomically replace file", impl_->target, std::strerror(errno));
    }
#endif
    impl_->committed = true;
    return Status::Ok();
}

}  // namespace tos::detail
