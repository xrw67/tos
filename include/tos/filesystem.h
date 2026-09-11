#ifndef TOS_FILESYSTEM_H_
#define TOS_FILESYSTEM_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tos/result.h"
#include "tos/status.h"

namespace tos {

/// A validated UTF-8 filesystem path.
///
/// Path owns its UTF-8 representation and has value semantics. It does not access the
/// filesystem; copies and concurrent const operations are safe. Parse rejects malformed UTF-8
/// and embedded NUL bytes with kInvalidArgument. Allocation exceptions propagate. On Windows,
/// filesystem helpers convert the stored UTF-8 to UTF-16 before calling native APIs; on POSIX the
/// validated bytes are used as the native path representation.
class Path {
   public:
    /// Parses a UTF-8 path. Empty paths are permitted, but filesystem operations may reject them.
    [[nodiscard]] static Result<Path> Parse(std::string_view utf8);

    /// Returns the owned, validated UTF-8 path text. The returned reference is invalid after this
    /// object is modified, moved from, or destroyed.
    [[nodiscard]] const std::string& utf8() const noexcept { return utf8_; }

    /// Returns whether the path has no bytes.
    [[nodiscard]] bool empty() const noexcept { return utf8_.empty(); }

    /// Returns whether the path is absolute according to the current platform's lexical rules.
    /// Allocation exceptions from native path adaptation may propagate.
    [[nodiscard]] bool is_absolute() const;

    /// Returns the final path component, parent, stem, extension, or lexically normalized path.
    /// These operations never access the filesystem. Allocation exceptions propagate.
    [[nodiscard]] Path filename() const;
    [[nodiscard]] Path parent_path() const;
    [[nodiscard]] Path stem() const;
    [[nodiscard]] Path extension() const;
    [[nodiscard]] Path lexically_normal() const;

    /// Appends child using the current platform's lexical path rules. This operation does not
    /// access the filesystem and does not retain child. Allocation exceptions propagate.
    [[nodiscard]] Path Join(const Path& child) const;

    friend bool operator==(const Path& lhs, const Path& rhs) noexcept {
        return lhs.utf8_ == rhs.utf8_;
    }
    friend bool operator!=(const Path& lhs, const Path& rhs) noexcept { return !(lhs == rhs); }
    friend bool operator<(const Path& lhs, const Path& rhs) noexcept {
        return lhs.utf8_ < rhs.utf8_;
    }

   private:
    explicit Path(std::string utf8) : utf8_(std::move(utf8)) {}

    std::string utf8_;
};

/// Filesystem entry categories reported by GetFileMetadata without following symlinks.
enum class FileType { kRegular, kDirectory, kSymlink, kOther };

/// Type and byte size for a filesystem entry.
///
/// size is the file size for regular files and zero for directories, symbolic links, and other
/// entry types because this API intentionally does not follow symlinks or expose platform-specific
/// storage accounting.
struct FileMetadata {
    FileType type;
    std::uintmax_t size;
};

/// Reads a file as uninterpreted bytes into a string. Missing paths return kNotFound and directory
/// paths return kFailedPrecondition. I/O failures return Status; output allocation exceptions
/// propagate. The function retains neither path nor result and is safe to call concurrently with
/// other helpers, subject to the operating system's normal concurrent-file semantics.
[[nodiscard]] Result<std::string> ReadTextFile(const Path& path);

/// Replaces a file's content with text bytes. Parent directories are not created. I/O failures
/// return Status and allocation exceptions while creating diagnostics propagate. This write is not
/// atomic with readers or concurrent writers.
[[nodiscard]] Status WriteTextFile(const Path& path, std::string_view text);

/// Atomically replaces the target with text bytes once the write is complete. A unique temporary
/// file is created in the target directory and is removed on failures before replacement. This
/// guarantees atomic visibility to readers on the same filesystem, but does not promise power-loss
/// durability. Parent directories are not created; I/O failures return Status and allocation
/// exceptions propagate.
[[nodiscard]] Status WriteTextFileAtomic(const Path& path, std::string_view text);

/// Creates path and missing parent directories. An existing directory succeeds; an existing
/// non-directory returns kAlreadyExists. I/O failures return Status and allocation exceptions
/// propagate.
[[nodiscard]] Status CreateDirectories(const Path& path);

/// Lists direct children of a directory as UTF-8 paths sorted in byte-lexicographic order. Missing
/// paths return kNotFound and non-directories return kFailedPrecondition. Entries with a native
/// name that cannot be represented as UTF-8 return kDataLoss. Allocation exceptions propagate.
[[nodiscard]] Result<std::vector<Path>> ListDirectory(const Path& path);

/// Returns metadata without following symbolic links. Missing paths return kNotFound; I/O failures
/// return Status. Allocation exceptions while creating diagnostics propagate.
[[nodiscard]] Result<FileMetadata> GetFileMetadata(const Path& path);

/// Removes a file, empty directory, or (when recursive is true) a complete directory tree. Missing
/// paths return kNotFound; a nonempty directory with recursive false returns kFailedPrecondition.
/// I/O failures return Status and allocation exceptions while creating diagnostics propagate.
[[nodiscard]] Status RemovePath(const Path& path, bool recursive = false);

/// Renames from to to without replacing an existing destination. Missing source returns kNotFound
/// and an existing destination returns kAlreadyExists. I/O failures return Status and allocation
/// exceptions while creating diagnostics propagate.
[[nodiscard]] Status RenamePath(const Path& from, const Path& to);

}  // namespace tos

#endif  // TOS_FILESYSTEM_H_
