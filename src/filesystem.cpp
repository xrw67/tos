#include "tos/filesystem.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

namespace tos {
namespace {

bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        unsigned char minimum_second = 0x80;
        unsigned char maximum_second = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
        } else if (first == 0xe0) {
            length = 3;
            minimum_second = 0xa0;
        } else if (first >= 0xe1 && first <= 0xec) {
            length = 3;
        } else if (first == 0xed) {
            length = 3;
            maximum_second = 0x9f;
        } else if (first >= 0xee && first <= 0xef) {
            length = 3;
        } else if (first == 0xf0) {
            length = 4;
            minimum_second = 0x90;
        } else if (first >= 0xf1 && first <= 0xf3) {
            length = 4;
        } else if (first == 0xf4) {
            length = 4;
            maximum_second = 0x8f;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if (second < minimum_second || second > maximum_second) {
            return false;
        }
        for (std::size_t continuation = 2; continuation < length; ++continuation) {
            const unsigned char byte = static_cast<unsigned char>(text[index + continuation]);
            if (byte < 0x80 || byte > 0xbf) {
                return false;
            }
        }
        index += length;
    }
    return true;
}

Status FileError(const std::error_code& error, std::string_view action, const Path& path) {
    StatusCode code = StatusCode::kUnavailable;
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) {
        code = StatusCode::kPermissionDenied;
    } else if (error == std::errc::no_such_file_or_directory) {
        code = StatusCode::kNotFound;
    } else if (error == std::errc::no_space_on_device) {
        code = StatusCode::kResourceExhausted;
    } else if (error == std::errc::file_exists) {
        code = StatusCode::kAlreadyExists;
    } else if (error == std::errc::directory_not_empty || error == std::errc::not_a_directory ||
               error == std::errc::is_a_directory) {
        code = StatusCode::kFailedPrecondition;
    }
    return Status(code, std::string(action) + " '" + path.utf8() + "': " + error.message());
}

#ifndef _WIN32
Status ErrnoError(int error, std::string_view action, const Path& path) {
    return FileError(std::error_code(error, std::generic_category()), action, path);
}
#else
Status WindowsError(DWORD error, std::string_view action, const Path& path) {
    StatusCode code = StatusCode::kUnavailable;
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            code = StatusCode::kNotFound;
            break;
        case ERROR_ACCESS_DENIED:
            code = StatusCode::kPermissionDenied;
            break;
        case ERROR_DISK_FULL:
            code = StatusCode::kResourceExhausted;
            break;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            code = StatusCode::kAlreadyExists;
            break;
        case ERROR_DIR_NOT_EMPTY:
            code = StatusCode::kFailedPrecondition;
            break;
        default:
            break;
    }
    return Status(code, std::string(action) + " '" + path.utf8() + "': Windows error " +
                            std::to_string(error));
}
#endif

#ifdef _WIN32
Result<std::filesystem::path> NativePath(const Path& path) {
    if (path.utf8().size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Status(StatusCode::kOutOfRange, "path is too long for Windows UTF-16 conversion");
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.utf8().data(),
                                             static_cast<int>(path.utf8().size()), nullptr, 0);
    if (required == 0 && !path.empty()) {
        return WindowsError(GetLastError(), "could not convert UTF-8 path", path);
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (required != 0 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.utf8().data(),
                            static_cast<int>(path.utf8().size()), wide.data(), required) == 0) {
        return WindowsError(GetLastError(), "could not convert UTF-8 path", path);
    }
    return std::filesystem::path(std::move(wide));
}

Result<Path> PathFromNative(const std::filesystem::path& path) {
    const std::wstring wide = path.native();
    if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Status(StatusCode::kDataLoss, "native path is too long for UTF-8 conversion");
    }
    const int required =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                            static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (required == 0 && !wide.empty()) {
        return Status(StatusCode::kDataLoss, "could not convert native path to UTF-8");
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (required != 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                             static_cast<int>(wide.size()), utf8.data(), required,
                                             nullptr, nullptr) == 0) {
        return Status(StatusCode::kDataLoss, "could not convert native path to UTF-8");
    }
    return Path::Parse(utf8);
}
#else
Result<std::filesystem::path> NativePath(const Path& path) {
    return std::filesystem::path(path.utf8());
}

Result<Path> PathFromNative(const std::filesystem::path& path) {
    return Path::Parse(path.native());
}
#endif

std::filesystem::path LexicalPath(const Path& path) { return std::filesystem::u8path(path.utf8()); }

Path PathFromLexical(std::filesystem::path path) {
    return std::move(Path::Parse(path.u8string())).value();
}

Result<std::filesystem::path> CheckedNativePath(const Path& path) {
    if (path.empty()) {
        return Status(StatusCode::kInvalidArgument, "path must not be empty");
    }
    return NativePath(path);
}

FileType ToFileType(std::filesystem::file_type type) noexcept {
    switch (type) {
        case std::filesystem::file_type::regular:
            return FileType::kRegular;
        case std::filesystem::file_type::directory:
            return FileType::kDirectory;
        case std::filesystem::file_type::symlink:
            return FileType::kSymlink;
        default:
            return FileType::kOther;
    }
}

#ifdef _WIN32
Status WriteDirectWindows(const std::filesystem::path& target, const Path& display_path,
                          std::string_view text) {
    HANDLE handle = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return WindowsError(GetLastError(), "could not open file", display_path);
    }

    bool success = true;
    DWORD failure = ERROR_SUCCESS;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t remaining = text.size() - offset;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, text.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            success = false;
            failure = GetLastError();
            break;
        }
        offset += written;
    }
    if (success && !FlushFileBuffers(handle)) {
        success = false;
        failure = GetLastError();
    }
    if (!CloseHandle(handle) && success) {
        success = false;
        failure = GetLastError();
    }
    return success ? Status::Ok() : WindowsError(failure, "could not write file", display_path);
}

Status WriteAtomicWindows(const std::filesystem::path& target, const Path& display_path,
                          std::string_view text) {
    const std::filesystem::path parent =
        target.parent_path().empty() ? std::filesystem::path(L".") : target.parent_path();
    static std::atomic<std::uint64_t> next_id{0};
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 100; ++attempt) {
        temporary = parent / (target.filename().wstring() + L".tos-tmp-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(next_id.fetch_add(1)));
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            return WindowsError(GetLastError(), "could not create temporary file", display_path);
        }
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return Status(StatusCode::kUnavailable,
                      "could not create a unique temporary file '" + display_path.utf8() + "'");
    }

    bool success = true;
    DWORD failure = ERROR_SUCCESS;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t remaining = text.size() - offset;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, text.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            success = false;
            failure = GetLastError();
            break;
        }
        offset += written;
    }
    if (success && !FlushFileBuffers(handle)) {
        success = false;
        failure = GetLastError();
    }
    if (!CloseHandle(handle) && success) {
        success = false;
        failure = GetLastError();
    }
    if (!success) {
        DeleteFileW(temporary.c_str());
        return WindowsError(failure, "could not write temporary file", display_path);
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        return WindowsError(error, "could not atomically replace file", display_path);
    }
    return Status::Ok();
}
#else
Status WriteDirectPosix(const std::filesystem::path& target, const Path& display_path,
                        std::string_view text) {
    const int descriptor = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor == -1) {
        return ErrnoError(errno, "could not open file", display_path);
    }

    bool success = true;
    int failure = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t written = write(descriptor, text.data() + offset, text.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            success = false;
            failure = errno;
            break;
        }
        if (written == 0) {
            success = false;
            failure = EIO;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (close(descriptor) != 0 && success) {
        success = false;
        failure = errno;
    }
    return success ? Status::Ok() : ErrnoError(failure, "could not write file", display_path);
}

Status WriteAtomicPosix(const std::filesystem::path& target, const Path& display_path,
                        std::string_view text) {
    const std::filesystem::path parent =
        target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
    std::string temporary = (parent / (target.filename().string() + ".tos-tmp-XXXXXX")).string();
    const int descriptor = mkstemp(temporary.data());
    if (descriptor == -1) {
        return ErrnoError(errno, "could not create temporary file", display_path);
    }

    bool success = true;
    int failure = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t written = write(descriptor, text.data() + offset, text.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            success = false;
            failure = errno;
            break;
        }
        if (written == 0) {
            success = false;
            failure = EIO;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (close(descriptor) != 0 && success) {
        success = false;
        failure = errno;
    }
    if (!success) {
        unlink(temporary.c_str());
        return ErrnoError(failure, "could not write temporary file", display_path);
    }
    if (rename(temporary.c_str(), target.c_str()) != 0) {
        const int error = errno;
        unlink(temporary.c_str());
        return ErrnoError(error, "could not atomically replace file", display_path);
    }
    return Status::Ok();
}
#endif

}  // namespace

Result<Path> Path::Parse(std::string_view utf8) {
    if (utf8.find('\0') != std::string_view::npos) {
        return Status(StatusCode::kInvalidArgument, "UTF-8 paths must not contain NUL bytes");
    }
    if (!IsValidUtf8(utf8)) {
        return Status(StatusCode::kInvalidArgument, "path must be valid UTF-8");
    }
    return Path(std::string(utf8));
}

bool Path::is_absolute() const { return LexicalPath(*this).is_absolute(); }

Path Path::filename() const { return PathFromLexical(LexicalPath(*this).filename()); }

Path Path::parent_path() const { return PathFromLexical(LexicalPath(*this).parent_path()); }

Path Path::stem() const { return PathFromLexical(LexicalPath(*this).stem()); }

Path Path::extension() const { return PathFromLexical(LexicalPath(*this).extension()); }

Path Path::lexically_normal() const {
    return PathFromLexical(LexicalPath(*this).lexically_normal());
}

Path Path::Join(const Path& child) const {
    return PathFromLexical(LexicalPath(*this) / LexicalPath(child));
}

Result<std::string> ReadTextFile(const Path& path) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    const std::filesystem::path& native = native_result.value();
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(native, error);
    if (error) {
        return FileError(error, "could not inspect file", path);
    }
    if (!std::filesystem::exists(status)) {
        return Status(StatusCode::kNotFound, "file does not exist '" + path.utf8() + "'");
    }
    if (!std::filesystem::is_regular_file(status)) {
        return Status(StatusCode::kFailedPrecondition,
                      "path is not a regular file '" + path.utf8() + "'");
    }
    const std::uintmax_t size = std::filesystem::file_size(native, error);
    if (error) {
        return FileError(error, "could not inspect file size", path);
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return Status(StatusCode::kOutOfRange, "file is too large to read into a string");
    }
    std::ifstream input(native, std::ios::binary);
    if (!input.is_open()) {
        return Status(StatusCode::kUnavailable, "could not open file '" + path.utf8() + "'");
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    }
    if (!input && !input.eof()) {
        return Status(StatusCode::kUnavailable, "could not read file '" + path.utf8() + "'");
    }
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

Status WriteTextFile(const Path& path, std::string_view text) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    const std::filesystem::path& native = native_result.value();
    std::error_code error;
    const std::filesystem::file_status existing = std::filesystem::status(native, error);
    if (!error && std::filesystem::is_directory(existing)) {
        return Status(StatusCode::kFailedPrecondition, "path is a directory '" + path.utf8() + "'");
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        return FileError(error, "could not inspect file", path);
    }
    const std::filesystem::path parent =
        native.parent_path().empty() ? std::filesystem::path(".") : native.parent_path();
    error.clear();
    const std::filesystem::file_status parent_status = std::filesystem::status(parent, error);
    if (error) {
        return FileError(error, "could not inspect parent directory", path);
    }
    if (!std::filesystem::exists(parent_status)) {
        return Status(StatusCode::kNotFound,
                      "parent directory does not exist for '" + path.utf8() + "'");
    }
    if (!std::filesystem::is_directory(parent_status)) {
        return Status(StatusCode::kFailedPrecondition,
                      "parent path is not a directory for '" + path.utf8() + "'");
    }
#ifdef _WIN32
    return WriteDirectWindows(native, path, text);
#else
    return WriteDirectPosix(native, path, text);
#endif
}

Status WriteTextFileAtomic(const Path& path, std::string_view text) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
#ifdef _WIN32
    return WriteAtomicWindows(native_result.value(), path, text);
#else
    return WriteAtomicPosix(native_result.value(), path, text);
#endif
}

Status CreateDirectories(const Path& path) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    const std::filesystem::path& native = native_result.value();
    std::error_code error;
    const std::filesystem::file_status current = std::filesystem::status(native, error);
    if (!error && std::filesystem::exists(current)) {
        if (std::filesystem::is_directory(current)) {
            return Status::Ok();
        }
        return Status(StatusCode::kAlreadyExists,
                      "path already exists and is not a directory '" + path.utf8() + "'");
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        return FileError(error, "could not inspect directory", path);
    }
    error.clear();
    std::filesystem::create_directories(native, error);
    if (error) {
        return FileError(error, "could not create directories", path);
    }
    return Status::Ok();
}

Result<std::vector<Path>> ListDirectory(const Path& path) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    const std::filesystem::path& native = native_result.value();
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(native, error);
    if (error) {
        return FileError(error, "could not inspect directory", path);
    }
    if (!std::filesystem::exists(status)) {
        return Status(StatusCode::kNotFound, "directory does not exist '" + path.utf8() + "'");
    }
    if (!std::filesystem::is_directory(status)) {
        return Status(StatusCode::kFailedPrecondition,
                      "path is not a directory '" + path.utf8() + "'");
    }

    std::vector<Path> entries;
    std::filesystem::directory_iterator iterator(native, error);
    if (error) {
        return FileError(error, "could not list directory", path);
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        auto utf8 = PathFromNative(iterator->path());
        if (!utf8) {
            return std::move(utf8).status();
        }
        entries.push_back(std::move(utf8).value());
        iterator.increment(error);
        if (error) {
            return FileError(error, "could not list directory", path);
        }
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

Result<FileMetadata> GetFileMetadata(const Path& path) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(native_result.value(), error);
    if (error) {
        return FileError(error, "could not inspect path", path);
    }
    if (!std::filesystem::exists(status) && status.type() != std::filesystem::file_type::symlink) {
        return Status(StatusCode::kNotFound, "path does not exist '" + path.utf8() + "'");
    }
    FileMetadata metadata{ToFileType(status.type()), 0};
    if (metadata.type == FileType::kRegular) {
        metadata.size = std::filesystem::file_size(native_result.value(), error);
        if (error) {
            return FileError(error, "could not inspect file size", path);
        }
    }
    return metadata;
}

Status RemovePath(const Path& path, bool recursive) {
    auto native_result = CheckedNativePath(path);
    if (!native_result) {
        return std::move(native_result).status();
    }
    const std::filesystem::path& native = native_result.value();
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(native, error);
    if (error) {
        return FileError(error, "could not inspect path", path);
    }
    if (!std::filesystem::exists(status) && status.type() != std::filesystem::file_type::symlink) {
        return Status(StatusCode::kNotFound, "path does not exist '" + path.utf8() + "'");
    }
    if (recursive) {
        std::filesystem::remove_all(native, error);
        if (error) {
            return FileError(error, "could not remove path", path);
        }
        return Status::Ok();
    }
    if (std::filesystem::is_directory(status) && !std::filesystem::is_empty(native, error)) {
        if (error) {
            return FileError(error, "could not inspect directory", path);
        }
        return Status(StatusCode::kFailedPrecondition,
                      "cannot remove a nonempty directory without recursive=true");
    }
    if (!std::filesystem::remove(native, error)) {
        if (error) {
            return FileError(error, "could not remove path", path);
        }
        return Status(StatusCode::kNotFound, "path does not exist '" + path.utf8() + "'");
    }
    return Status::Ok();
}

Status RenamePath(const Path& from, const Path& to) {
    auto source_result = CheckedNativePath(from);
    if (!source_result) {
        return std::move(source_result).status();
    }
    auto destination_result = CheckedNativePath(to);
    if (!destination_result) {
        return std::move(destination_result).status();
    }
    std::error_code error;
    const std::filesystem::file_status source =
        std::filesystem::symlink_status(source_result.value(), error);
    if (error) {
        return FileError(error, "could not inspect rename source", from);
    }
    if (!std::filesystem::exists(source) && source.type() != std::filesystem::file_type::symlink) {
        return Status(StatusCode::kNotFound, "rename source does not exist '" + from.utf8() + "'");
    }
    const std::filesystem::file_status destination =
        std::filesystem::symlink_status(destination_result.value(), error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return FileError(error, "could not inspect rename destination", to);
    }
    if (!error && (std::filesystem::exists(destination) ||
                   destination.type() == std::filesystem::file_type::symlink)) {
        return Status(StatusCode::kAlreadyExists,
                      "rename destination already exists '" + to.utf8() + "'");
    }
    error.clear();
    std::filesystem::rename(source_result.value(), destination_result.value(), error);
    if (error) {
        return FileError(error, "could not rename path", from);
    }
    return Status::Ok();
}

}  // namespace tos
