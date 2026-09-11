#ifndef TOS_REGISTRY_H_
#define TOS_REGISTRY_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "tos/result.h"
#include "tos/status.h"

namespace tos {

/// The local Windows registry root hives supported by RegistryKey.
enum class RegistryHive { kCurrentUser, kLocalMachine };

/// The Windows registry view used when opening, creating, or deleting a key.
/// kNative uses the current process's default view.
enum class RegistryView { kNative, k32Bit, k64Bit };

/// Access requested when opening an existing registry key.
enum class RegistryAccess { kRead, kReadWrite };

/// Options for opening an existing registry key.
struct RegistryOpenOptions {
    RegistryAccess access = RegistryAccess::kRead;
    RegistryView view = RegistryView::kNative;
};

/// A raw REG_EXPAND_SZ registry value. Its text is not environment-expanded.
struct RegistryExpandString {
    std::string value;
};

/// Registry value representations supported by RegistryKey.
///
/// std::string maps to REG_SZ, RegistryExpandString to REG_EXPAND_SZ, uint32_t to REG_DWORD,
/// uint64_t to REG_QWORD, byte vectors to REG_BINARY, and string vectors to REG_MULTI_SZ.
using RegistryValue = std::variant<std::string, RegistryExpandString, std::uint32_t, std::uint64_t,
                                   std::vector<std::uint8_t>, std::vector<std::string>>;

/// An owning handle to a local Windows registry key.
///
/// RegistryKey is move-only and closes its handle when destroyed. Public methods on the same
/// instance are not safe to call concurrently; separate instances may be used concurrently under
/// the normal Windows registry concurrency rules. Strings are UTF-8 and reject embedded NUL bytes.
/// On non-Windows platforms all registry entry points return kUnimplemented. Operational failures
/// return Status or Result; allocation and value-construction exceptions propagate.
class [[nodiscard]] RegistryKey {
   public:
    /// Opens an existing subkey in hive. An empty subkey opens the selected root hive.
    [[nodiscard]] static Result<RegistryKey> Open(RegistryHive hive, std::string_view subkey,
                                                  RegistryOpenOptions options = {});

    /// Creates subkey in hive, or opens it when it already exists. Empty subkeys are rejected to
    /// prevent treating a root hive as a creatable key.
    [[nodiscard]] static Result<RegistryKey> Create(RegistryHive hive, std::string_view subkey,
                                                    RegistryView view = RegistryView::kNative);

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&& other) noexcept;
    RegistryKey& operator=(RegistryKey&& other) noexcept;
    ~RegistryKey();

    /// Reads name, where an empty name denotes the default value. Missing values return kNotFound.
    /// Unsupported native registry value types return kUnimplemented; malformed registry text
    /// returns kDataLoss.
    [[nodiscard]] Result<RegistryValue> GetValue(std::string_view name) const;

    /// Writes name, where an empty name denotes the default value. String values and all
    /// REG_MULTI_SZ elements must be valid UTF-8 and contain no NUL bytes.
    [[nodiscard]] Status SetValue(std::string_view name, const RegistryValue& value);

    /// Deletes name, where an empty name denotes the default value. Missing values return
    /// kNotFound.
    [[nodiscard]] Status DeleteValue(std::string_view name);

    /// Lists direct subkey or value names in UTF-8 byte-lexicographic order.
    [[nodiscard]] Result<std::vector<std::string>> ListSubkeys() const;
    [[nodiscard]] Result<std::vector<std::string>> ListValueNames() const;

    /// Deletes a direct descendant of this key. Empty relative paths are rejected. A nonempty key
    /// requires recursive to be true; otherwise a nonempty target returns kFailedPrecondition.
    [[nodiscard]] Status DeleteSubkey(std::string_view relative_subkey, bool recursive = false);

    /// Deletes a key selected by hive and subkey. Empty paths are rejected to prevent deleting a
    /// root hive. A nonempty target requires recursive to be true.
    [[nodiscard]] static Status DeleteKey(RegistryHive hive, std::string_view subkey,
                                          bool recursive = false,
                                          RegistryView view = RegistryView::kNative);

   private:
    struct State;

    explicit RegistryKey(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace tos

#endif  // TOS_REGISTRY_H_
