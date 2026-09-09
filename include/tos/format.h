#ifndef TOS_FORMAT_H_
#define TOS_FORMAT_H_

// fmt is shipped in the public include tree. Header-only mode keeps tos::tos
// free of a separate fmt link dependency.
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

#include <vendor/fmt/format.h>

namespace tos {

/// Formatting functions re-exported from fmt for tos consumers.
using ::fmt::format;
using ::fmt::format_to;
using ::fmt::format_to_n;
using ::fmt::formatted_size;
using ::fmt::print;
using ::fmt::println;
using ::fmt::to_string;
using ::fmt::vformat;
using ::fmt::vformat_to;
using ::fmt::vformat_to_n;
using ::fmt::vprint;
using ::fmt::vprintln;

}  // namespace tos

#endif  // TOS_FORMAT_H_
