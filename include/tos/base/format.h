#ifndef TOS_BASE_FORMAT_H_
#define TOS_BASE_FORMAT_H_

// fmt is shipped in the public include tree. Header-only mode keeps tos::base
// free of a separate fmt link dependency.
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

#include "tos/vendor/fmt/chrono.h"
#include "tos/vendor/fmt/core.h"
#include "tos/vendor/fmt/format.h"
#include "tos/vendor/fmt/os.h"
#include "tos/vendor/fmt/ostream.h"
#include "tos/vendor/fmt/printf.h"
#include "tos/vendor/fmt/ranges.h"
#include "tos/vendor/fmt/std.h"

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

#endif  // TOS_BASE_FORMAT_H_
