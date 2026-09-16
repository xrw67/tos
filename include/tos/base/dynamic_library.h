#ifndef TOS_BASE_DYNAMIC_LIBRARY_H_
#define TOS_BASE_DYNAMIC_LIBRARY_H_

#include <string_view>
#include <type_traits>
#include <utility>

#include "tos/base/filesystem.h"
#include "tos/base/result.h"
#include "tos/base/status.h"

namespace tos {

/// An owning handle to a dynamically loaded library.
///
/// Load accepts only an absolute UTF-8 Path and uses the native dynamic loader. DynamicLibrary is
/// move-only; its destructor best-effort unloads a still-loaded library. GetSymbol and Unload,
/// moves, or destruction of the same object are not safe to call concurrently. Separate instances
/// may be loaded independently on concurrent threads. All symbols returned by GetSymbol are
/// borrowed and become invalid after a successful Unload or destruction. Allocation exceptions
/// while converting paths or constructing diagnostics propagate.
class [[nodiscard]] DynamicLibrary {
   public:
    /// Loads the library at an absolute path. Empty or relative paths return kInvalidArgument.
    /// Native loader failures return a classified Status with the native diagnostic text.
    [[nodiscard]] static Result<DynamicLibrary> Load(const Path& path);

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    ~DynamicLibrary();

    /// Returns whether this object currently owns a loaded native library handle.
    [[nodiscard]] bool loaded() const noexcept { return handle_ != nullptr; }

    /// Unloads the library. A previously unloaded object succeeds without action. On failure the
    /// object retains its handle and borrowed symbols remain valid; native failure diagnostics are
    /// returned as kUnavailable or their Windows error classification.
    [[nodiscard]] Status Unload();

    /// Relinquishes this object's library handle without calling the native unload operation.
    /// The library remains loaded until process exit or a separate native owner unloads it. This
    /// object becomes unloaded, so GetSymbol subsequently returns kFailedPrecondition. Existing
    /// symbol pointers are no longer covered by this object's lifetime guarantee.
    void Detach() noexcept;

    /// Resolves name as a borrowed pointer to an exported function or data symbol.
    ///
    /// Symbol must be a non-cv pointer type. Empty names and names containing NUL return
    /// kInvalidArgument; calling this on an unloaded object returns kFailedPrecondition; a missing
    /// export returns kNotFound. The caller must ensure this DynamicLibrary stays loaded for every
    /// use of the returned pointer. This conversion follows the platform dynamic-loader ABI;
    /// supplying an incorrect Symbol type has undefined behavior when the pointer is used.
    template <typename Symbol>
    [[nodiscard]] Result<Symbol> GetSymbol(std::string_view name) const {
        static_assert(
            std::is_pointer_v<Symbol> && !std::is_const_v<Symbol> && !std::is_volatile_v<Symbol>,
            "DynamicLibrary::GetSymbol requires a non-cv pointer type");
        auto address = GetSymbolAddress(name);
        if (!address) {
            return std::move(address).status();
        }
        return reinterpret_cast<Symbol>(std::move(address).value());
    }

   private:
    explicit DynamicLibrary(void* handle) noexcept : handle_(handle) {}

    [[nodiscard]] Result<void*> GetSymbolAddress(std::string_view name) const;
    void Reset() noexcept;

    void* handle_ = nullptr;
};

}  // namespace tos

#endif  // TOS_BASE_DYNAMIC_LIBRARY_H_
