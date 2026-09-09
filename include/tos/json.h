#ifndef TOS_JSON_H_
#define TOS_JSON_H_

// nlohmann/json is shipped in the public include tree so tos consumers only
// need the tos::tos target or the include directory.
#include <vendor/nlohmann/json.hpp>

namespace tos {

/// JSON types re-exported from nlohmann/json for tos consumers.
using ::nlohmann::basic_json;
using ::nlohmann::json;
using ::nlohmann::json_pointer;
using ::nlohmann::ordered_json;

}  // namespace tos

#endif  // TOS_JSON_H_
