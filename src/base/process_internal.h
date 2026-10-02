#ifndef TOS_BASE_PROCESS_INTERNAL_H_
#define TOS_BASE_PROCESS_INTERNAL_H_

#include <cstdint>
#include <thread>

#include "tos/base/process.h"

namespace tos {
namespace detail {

// Private deterministic thread-start seam. A factory either returns a joinable thread
// running entry(argument), or throws without starting a thread. The argument remains
// owned by the caller until join. child_id is an OS process id, not a native HANDLE.
using CaptureThreadFactory = std::thread (*)(void (*entry)(void*), void* argument,
                                             std::uint64_t child_id, void* context);
Result<CommandResult> RunCommandWithThreadFactory(const ProcessOptions& options,
                                                  const RunCommandOptions& run_options,
                                                  CaptureThreadFactory factory, void* context);

}  // namespace detail
}  // namespace tos

#endif  // TOS_BASE_PROCESS_INTERNAL_H_
