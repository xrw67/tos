#ifndef TOS_SPAN_H_
#define TOS_SPAN_H_

#include <cstddef>

#include "tos/vendor/nonstd/span.hpp"

namespace tos {

/// A non-owning, contiguous view over a sequence of T.
///
/// The caller retains ownership of the referenced storage and must ensure it
/// remains valid while this view is used. A span does not synchronize access:
/// concurrent access follows the rules of the underlying storage. Span itself
/// does not allocate; exceptions from a user-defined container's data() or
/// size() propagate. Out-of-range access and invalid subviews violate the
/// underlying span-lite preconditions. This API has identical behavior on
/// Linux, macOS, and Windows.
template <typename T, std::size_t Extent = nonstd::dynamic_extent>
using span = nonstd::span<T, Extent>;

/// Sentinel extent used by a span whose size is stored at runtime.
constexpr std::size_t dynamic_extent = nonstd::dynamic_extent;

}  // namespace tos

#endif  // TOS_SPAN_H_
