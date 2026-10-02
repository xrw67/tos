#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "native_resource.h"
#include "tos/base/filesystem.h"
#include "tos/base/scope_exit.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tos {
namespace {
#ifdef _WIN32
Status NativeError(DWORD error, const Path& path, std::string_view action) {
    return WindowsError(error, std::string(action) + " '" + path.utf8() + "'");
}
#else
Status NativeError(int error, const Path& path, std::string_view action) {
    if (error == ENOTSUP || error == EOPNOTSUPP) {
        return Status(StatusCode::kUnimplemented, std::string(action) + " is unsupported");
    }
    return ErrnoError(error, std::string(action) + " '" + path.utf8() + "'");
}
#endif
}  // namespace

struct AtomicFileWriter::Impl final {
    Impl(Path destination, AtomicWriteDurability mode)
        : target(std::move(destination)), durability(mode) {}
    ~Impl() {
#ifdef _WIN32
        handle.Reset();
        if (!replaced && !temporary.empty()) DeleteFileW(temporary.c_str());
#else
        descriptor.Reset();
        if (!replaced && !temporary.empty()) unlink(temporary.c_str());
#endif
    }

    Path target;
    std::filesystem::path destination;
    std::filesystem::path temporary;
    AtomicWriteDurability durability;
    bool replaced{false};
    bool failed{false};
#ifdef _WIN32
    detail::UniqueHandle handle;
#else
    detail::UniqueFd descriptor;
#endif
};

AtomicFileWriter::AtomicFileWriter() = default;
AtomicFileWriter::AtomicFileWriter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
AtomicFileWriter::AtomicFileWriter(AtomicFileWriter&&) noexcept = default;
AtomicFileWriter& AtomicFileWriter::operator=(AtomicFileWriter&&) noexcept = default;
AtomicFileWriter::~AtomicFileWriter() = default;

Result<AtomicFileWriter> AtomicFileWriter::Create(const Path& target,
                                                  AtomicWriteDurability durability) {
    if (target.empty()) return Status(StatusCode::kInvalidArgument, "destination path is empty");
    if (durability != AtomicWriteDurability::kNone && durability != AtomicWriteDurability::kData &&
        durability != AtomicWriteDurability::kDataAndDirectory) {
        return Status(StatusCode::kInvalidArgument, "invalid atomic-write durability");
    }
#ifdef _WIN32
    if (durability == AtomicWriteDurability::kDataAndDirectory) {
        return Status(StatusCode::kUnimplemented, "Windows cannot flush a parent directory");
    }
#endif
    auto impl = std::make_unique<Impl>(target, durability);
    impl->destination = std::filesystem::u8path(target.utf8());
    const auto parent = impl->destination.parent_path().empty() ? std::filesystem::path(".")
                                                                : impl->destination.parent_path();
#ifdef _WIN32
    static std::atomic<std::uint64_t> sequence{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        impl->temporary = parent / (impl->destination.filename().wstring() + L".tos-tmp-" +
                                    std::to_wstring(GetCurrentProcessId()) + L"-" +
                                    std::to_wstring(sequence.fetch_add(1)));
        impl->handle.Reset(CreateFileW(impl->temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (impl->handle) return AtomicFileWriter(std::move(impl));
        const DWORD error = GetLastError();
        // Never remove a colliding file owned by another writer.
        impl->temporary.clear();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return NativeError(error, target, "create temporary file");
    }
    return Status(StatusCode::kUnavailable, "temporary file name collision limit reached");
#else
    std::string name =
        (parent / (impl->destination.filename().string() + ".tos-tmp-XXXXXX")).string();
    impl->descriptor.Reset(mkstemp(name.data()));
    if (!impl->descriptor) return NativeError(errno, target, "create temporary file");
    auto cleanup = MakeScopeExit([&]() noexcept {
        impl->descriptor.Reset();
        unlink(name.c_str());
    });
    impl->temporary = name;
    cleanup.Release();
    if (fcntl(impl->descriptor.Get(), F_SETFD, FD_CLOEXEC) != 0)
        return NativeError(errno, target, "set temporary file close-on-exec");
    return AtomicFileWriter(std::move(impl));
#endif
}

Status AtomicFileWriter::Write(std::string_view bytes) {
    if (!impl_ || impl_->replaced || impl_->failed)
        return Status(StatusCode::kFailedPrecondition, "atomic writer is not writable");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
#ifdef _WIN32
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(impl_->handle.Get(), bytes.data() + offset, chunk, &written, nullptr)) {
            impl_->failed = true;
            return NativeError(GetLastError(), impl_->target, "write temporary file");
        }
#else
        const std::size_t chunk =
            std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<ssize_t>::max());
        const ssize_t written = write(impl_->descriptor.Get(), bytes.data() + offset, chunk);
        if (written < 0) {
            if (errno == EINTR) continue;
            impl_->failed = true;
            return NativeError(errno, impl_->target, "write temporary file");
        }
#endif
        if (written == 0) {
            impl_->failed = true;
            return Status(StatusCode::kUnavailable, "zero-length temporary file write");
        }
        offset += static_cast<std::size_t>(written);
    }
    return Status::Ok();
}

Status AtomicFileWriter::Commit() {
    if (!impl_ || impl_->replaced || impl_->failed)
        return Status(StatusCode::kFailedPrecondition, "atomic writer is not pending");
    // Every commit attempt is terminal, including a failed directory flush after replacement.
    impl_->failed = true;
#ifdef _WIN32
    if (impl_->durability != AtomicWriteDurability::kNone && !FlushFileBuffers(impl_->handle.Get()))
        return NativeError(GetLastError(), impl_->target, "flush temporary file");
    const DWORD close_error = impl_->handle.Close();
    if (close_error) return NativeError(close_error, impl_->target, "close temporary file");
    const DWORD flags =
        MOVEFILE_REPLACE_EXISTING |
        (impl_->durability == AtomicWriteDurability::kNone ? 0 : MOVEFILE_WRITE_THROUGH);
    if (!MoveFileExW(impl_->temporary.c_str(), impl_->destination.c_str(), flags))
        return NativeError(GetLastError(), impl_->target, "replace destination");
    impl_->replaced = true;
#else
    if (impl_->durability != AtomicWriteDurability::kNone) {
        int result;
        do {
            result = fsync(impl_->descriptor.Get());
        } while (result != 0 && errno == EINTR);
        if (result != 0) return NativeError(errno, impl_->target, "flush temporary file");
    }
    const int close_error = impl_->descriptor.Close();
    if (close_error) return NativeError(close_error, impl_->target, "close temporary file");
    if (rename(impl_->temporary.c_str(), impl_->destination.c_str()) != 0)
        return NativeError(errno, impl_->target, "replace destination");
    impl_->replaced = true;
    if (impl_->durability == AtomicWriteDurability::kDataAndDirectory)
        return SyncDirectory(impl_->target.parent_path().empty() ? Path::Parse(".").value()
                                                                 : impl_->target.parent_path());
#endif
    return Status::Ok();
}

Status SyncDirectory(const Path& path) {
    if (path.empty()) return Status(StatusCode::kInvalidArgument, "directory path is empty");
#ifdef _WIN32
    return Status(StatusCode::kUnimplemented, "Windows cannot flush a directory");
#else
    detail::UniqueFd descriptor(open(path.utf8().c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (!descriptor) return NativeError(errno, path, "open directory");
    int result;
    do {
        result = fsync(descriptor.Get());
    } while (result != 0 && errno == EINTR);
    const int error = errno;
    const int close_error = descriptor.Close();
    if (result != 0) return NativeError(error, path, "flush directory");
    if (close_error) return NativeError(close_error, path, "close directory");
    return Status::Ok();
#endif
}

}  // namespace tos
