#ifndef TOS_BASE_FILE_LOCK_H_
#define TOS_BASE_FILE_LOCK_H_

#include <memory>

#include "tos/base/filesystem.h"

namespace tos {

/// Move-only, process-wide exclusive lock held by an open lock file.
/// TryAcquire never waits; a lock held by another process returns kUnavailable. Destruction
/// releases the native handle, while the lock file itself remains on disk. POSIX locks are
/// advisory flock locks (cooperating users must lock the same inode); Windows denies sharing the
/// handle. Parents must already exist; do not rename/unlink a held lock file. No inherited handle
/// is passed through exec/CreateProcess. Methods/moves/destruction on one instance require caller
/// synchronization. Operational errors return Result; allocation exceptions propagate.
class FileLock final {
   public:
    FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&&) noexcept;
    FileLock& operator=(FileLock&&) noexcept;
    ~FileLock();

    [[nodiscard]] static Result<FileLock> TryAcquire(const Path& path);
    [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }

   private:
    struct Impl;
    explicit FileLock(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tos

#endif  // TOS_BASE_FILE_LOCK_H_
