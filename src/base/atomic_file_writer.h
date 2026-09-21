#ifndef TOS_BASE_ATOMIC_FILE_WRITER_H_
#define TOS_BASE_ATOMIC_FILE_WRITER_H_

#include <memory>
#include <string_view>

#include "tos/base/filesystem.h"

namespace tos::detail {

class AtomicFileWriter final {
   public:
    static Result<AtomicFileWriter> Create(const Path& target);

    AtomicFileWriter(AtomicFileWriter&& other) noexcept;
    AtomicFileWriter& operator=(AtomicFileWriter&& other) noexcept;
    AtomicFileWriter(const AtomicFileWriter&) = delete;
    AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;
    ~AtomicFileWriter();

    Status Write(std::string_view bytes);
    Status Commit();

   private:
    struct Impl;
    explicit AtomicFileWriter(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace tos::detail

#endif  // TOS_BASE_ATOMIC_FILE_WRITER_H_
