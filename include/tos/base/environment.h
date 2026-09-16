#ifndef TOS_BASE_ENVIRONMENT_H_
#define TOS_BASE_ENVIRONMENT_H_

#include <string>
#include <string_view>
#include <vector>

#include "tos/base/result.h"

namespace tos {

/// Accesses the current process environment.
///
/// The environment is process-global. These functions serialize calls made through Environment,
/// so they may be called concurrently with one another. Code that calls getenv, setenv, unsetenv,
/// _putenv_s, SetEnvironmentVariableW, or another environment-mutating library directly must not
/// run concurrently with this API. A child process receives the environment present when it is
/// created; this class does not synchronize environment changes with child creation.
class Environment {
   public:
    /// Returns the value of variable_name, including an empty value.
    ///
    /// An empty name, a name containing '=' or NUL, or a value returned by Windows that cannot be
    /// represented as UTF-8 returns kInvalidArgument. An unset variable returns kNotFound. POSIX
    /// names are case-sensitive; Windows names are case-insensitive. Allocation exceptions while
    /// copying the name, value, or diagnostic propagate.
    [[nodiscard]] static Result<std::string> GetVar(std::string_view variable_name);

    /// Returns variable_name's value, or an independent copy of default_value when it cannot be
    /// read.
    ///
    /// This convenience function returns default_value for an unset variable, invalid name, or
    /// platform lookup failure. Use GetVar when the caller must distinguish those conditions.
    /// Allocation exceptions while reading or copying the returned value propagate.
    [[nodiscard]] static std::string GetVarOr(std::string_view variable_name,
                                              std::string_view default_value);

    /// Returns whether variable_name is set, including when its value is empty.
    ///
    /// An unset variable, invalid name, or platform lookup failure returns false. Use GetVar when
    /// the caller must distinguish those conditions. Allocation exceptions while preparing the
    /// native name or diagnostics propagate.
    [[nodiscard]] static bool HasVar(std::string_view variable_name);

    /// Sets variable_name to value for the current process.
    ///
    /// Names must be nonempty and contain neither '=' nor NUL; values must not contain NUL.
    /// Invalid input returns kInvalidArgument. Native failures return a classified Status. The
    /// update is visible to later Environment calls and children created afterward. Allocation and
    /// UTF-8 conversion exceptions propagate.
    [[nodiscard]] static Status SetVar(std::string_view variable_name, std::string_view value);

    /// Removes variable_name from the current process environment.
    ///
    /// Names must be nonempty and contain neither '=' nor NUL. Removing an already-unset variable
    /// succeeds. Invalid input returns kInvalidArgument; native failures return a classified
    /// Status. Allocation and UTF-8 conversion exceptions propagate.
    [[nodiscard]] static Status UnsetVar(std::string_view variable_name);

    /// Splits PATH using ':' on POSIX and ';' on Windows, preserving empty fields.
    ///
    /// An unset PATH, invalid native data, or lookup failure returns an empty vector; a set empty
    /// PATH returns one empty field. Use GetVar("PATH") when the caller must distinguish those
    /// conditions. Allocation exceptions while copying or splitting PATH propagate.
    [[nodiscard]] static std::vector<std::string> GetPathVar();
};

}  // namespace tos

#endif  // TOS_BASE_ENVIRONMENT_H_
