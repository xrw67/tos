#ifndef TOS_RESULT_H_
#define TOS_RESULT_H_

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "tos/status.h"

namespace tos {

/// 有返回值操作的结果，独占成功值或失败状态，只支持移动；调用方应检查返回结果。
/// T 必须是可析构、非数组、非 const/volatile 的对象类型，且不能为 Status。
/// 不支持 Result<void>，无返回值操作使用 Status；T 无需支持默认构造。
/// 取值失败抛出包含错误码及消息的 std::logic_error；构造异常和分配异常直接传播。
/// 并发访问遵循 T 的规则；修改、移动或析构同一对象时需要调用方同步。
template <typename T>
class [[nodiscard]] Result {
    static_assert(std::is_object_v<T> && !std::is_array_v<T> && std::is_destructible_v<T>,
                  "Result<T> requires a destructible, non-array object type; use Status for void");
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                  "Result<T> requires an unqualified value type");
    static_assert(!std::is_same_v<T, Status>, "Result<Status> is ambiguous; use Status directly");

   public:
    /// 禁止默认构造，必须提供成功值或失败状态。
    Result() = delete;
    /// 禁止复制构造，即使 T 可复制也不支持。
    Result(const Result&) = delete;
    /// 移动成功值或错误状态；可用性及是否抛异常取决于 T 的移动构造。
    /// 源成功结果仍持有被移动后的 T；源失败结果通过 status() 报告 kInternal。
    Result(Result&&) = default;
    /// 禁止复制赋值。
    Result& operator=(const Result&) = delete;
    /// 移动赋值，可用性及异常规格取决于 T；源状态遵循移动构造的约定。
    /// 切换存储类型时若异常导致无活动值，则报告 kInternal，之后可重新赋值恢复。
    Result& operator=(Result&&) = default;

    /// 用可构造为 T 的值隐式创建成功结果，支持直接 return value。
    /// 完美转发参数；T 的构造异常直接传播，不自动转换为失败状态。
    template <typename U, std::enable_if_t<!std::is_same_v<std::decay_t<U>, Result> &&
                                               !std::is_same_v<std::decay_t<U>, Status> &&
                                               std::is_constructible_v<T, U&&>,
                                           int> = 0>
    Result(U&& value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    /// 接管失败 Status，支持返回临时错误或移动已有错误。
    /// 传入成功 Status 时抛出 std::invalid_argument，避免产生成功但无值的结果。
    Result(Status status) : storage_(std::in_place_index<1>, std::move(status)) {
        if (std::get<1>(storage_).ok()) {
            throw std::invalid_argument("Cannot construct Result from a success Status");
        }
    }

    /// 持有成功值时返回 true，与值本身是否为 false 或空指针无关。
    bool ok() const noexcept {
        return storage_.index() == 0;
    }

    /// 等价于 ok()，可用于 if (result) 等布尔判断。
    explicit operator bool() const noexcept {
        return ok();
    }

    /// 借用只读状态，不转移所有权；不要依赖引用在本对象被修改、移动或销毁后仍有效。
    /// 成功时返回 kOk；无活动值或错误已移走时返回对应的 kInternal 兜底状态。
    /// 兜底状态首次初始化可能分配并抛出异常，初始化后存活至进程结束。
    const Status& status() const& {
        if (ok()) {
            static const PersistentStatus success;
            return success.get();
        }
        // A throwing alternative change can leave std::variant without a value.
        if (storage_.valueless_by_exception()) {
            static const PersistentStatus failure(StatusCode::kInternal,
                                                  "Result has no value after an exception");
            return failure.get();
        }
        const auto& error = std::get<1>(storage_);
        // Moving a Status resets it to success, but this Result still has no T.
        if (error.ok()) {
            static const PersistentStatus failure(StatusCode::kInternal,
                                                  "Result error has been moved");
            return failure.get();
        }
        return error;
    }

    /// 返回独立 Status；正常错误直接转移所有权，不分配内存。
    /// 源错误结果仍无值，后续 status() 报告 kInternal；成功值不受影响。
    /// 无活动值或错误已移走时构造独立的兜底错误，可能因分配失败抛出异常。
    Status status() && {
        if (storage_.index() == 1 && !std::get<1>(storage_).ok()) {
            return std::get<1>(std::move(storage_));
        }
        const auto& failure = std::as_const(*this).status();
        return Status(failure.code(), failure.message());
    }

    /// 禁止从 const 右值提取状态，避免复制或返回临时对象中的悬空引用。
    Status status() const&& = delete;

    /// 返回成功值的可变引用；失败时抛出 std::logic_error。
    /// 所有取值接口返回的引用或指针均为借用，切换存储类型或析构后失效。
    T& value() & {
        check_value();
        return std::get<0>(storage_);
    }

    /// 返回成功值的只读引用；失败时抛出 std::logic_error。
    const T& value() const& {
        check_value();
        return std::get<0>(storage_);
    }

    /// 返回成功值的右值引用，供调用方移动；调用本身不移动或清空值。
    /// 失败时抛出 std::logic_error，返回的引用不延长本对象的生命周期。
    T&& value() && {
        check_value();
        return std::get<0>(std::move(storage_));
    }

    /// 返回成功值的 const 右值引用；通常不能用于转移所有权。
    /// 失败时抛出 std::logic_error，返回的引用不延长本对象的生命周期。
    const T&& value() const&& {
        check_value();
        return std::get<0>(std::move(storage_));
    }

    /// 等价于 value()，返回可变借用引用并检查失败状态。
    T& operator*() & {
        return value();
    }

    /// 等价于 value()，返回只读借用引用并检查失败状态。
    const T& operator*() const& {
        return value();
    }

    /// 等价于 std::move(*this).value()，返回右值引用并检查失败状态。
    T&& operator*() && {
        return std::move(*this).value();
    }

    /// 等价于 std::move(*this).value()，返回 const 右值引用并检查失败状态。
    const T&& operator*() const&& {
        return std::move(*this).value();
    }

    /// 返回成功值的可变借用指针；失败时抛出 std::logic_error。
    T* operator->() {
        return std::addressof(value());
    }

    /// 返回成功值的只读借用指针；失败时抛出 std::logic_error。
    const T* operator->() const {
        return std::addressof(value());
    }

   private:
    // Fallback references must remain valid during global destruction. Construct
    // Status in static storage without registering its destructor; the bounded
    // error allocations intentionally remain reachable until process exit.
    struct PersistentStatus {
        explicit PersistentStatus(StatusCode code = StatusCode::kOk,
                                  std::string_view message = {}) {
            ::new (static_cast<void*>(storage)) Status(code, message);
        }

        const Status& get() const noexcept {
            return *std::launder(reinterpret_cast<const Status*>(storage));
        }

        alignas(Status) unsigned char storage[sizeof(Status)];
    };
    static_assert(std::is_trivially_destructible_v<PersistentStatus>);

    void check_value() const {
        if (!ok()) {
            const Status& failure = status();
            std::string message = "Cannot access Result value (code=" +
                                  std::to_string(static_cast<int>(failure.code())) + "): ";
            message.append(failure.message());
            throw std::logic_error(message);
        }
    }

    std::variant<T, Status> storage_;
};

}  // namespace tos

#endif  // TOS_RESULT_H_
