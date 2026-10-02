#ifndef TOS_BASE_SCOPE_EXIT_H_
#define TOS_BASE_SCOPE_EXIT_H_

#include <type_traits>
#include <utility>

namespace tos {

/// Executes an owned callback once on scope exit, including exception unwinding.
/// Release or move construction disarms the source. The callback must be nothrow movable
/// and destructible; copying it during construction may throw and propagates unchanged.
/// Cleanup must not throw: an escaping exception calls std::terminate. No allocation is
/// performed by the guard itself. Captured references must outlive the guard. Concurrent
/// operations on the same guard require external synchronization.
template <typename F>
class [[nodiscard]] ScopeExit final {
    static_assert(std::is_invocable_v<F&>, "ScopeExit requires a callable with no arguments");
    static_assert(std::is_nothrow_move_constructible_v<F>,
                  "ScopeExit requires a nothrow movable callback");
    static_assert(std::is_nothrow_destructible_v<F>,
                  "ScopeExit requires a nothrow destructible callback");

   public:
    explicit ScopeExit(F cleanup) noexcept : cleanup_(std::move(cleanup)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&& other) noexcept
        : cleanup_(std::move(other.cleanup_)), active_(std::exchange(other.active_, false)) {}
    ScopeExit& operator=(ScopeExit&&) = delete;
    ~ScopeExit() noexcept {
        if (active_) cleanup_();
    }

    void Release() noexcept { active_ = false; }

   private:
    F cleanup_;
    bool active_{true};
};

template <typename F>
ScopeExit(F) -> ScopeExit<F>;

/// Owns a decayed callback. Callback construction exceptions propagate; ownership of
/// external resources remains with the caller until construction succeeds.
template <typename F>
[[nodiscard]] auto MakeScopeExit(F&& cleanup) noexcept(
    std::is_nothrow_constructible_v<std::decay_t<F>, F&&>) {
    return ScopeExit<std::decay_t<F>>(std::forward<F>(cleanup));
}

}  // namespace tos

#endif  // TOS_BASE_SCOPE_EXIT_H_
