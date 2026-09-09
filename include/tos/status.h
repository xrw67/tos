#ifndef TOS_STATUS_H_
#define TOS_STATUS_H_

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace tos {

/// 操作结果的错误码；仅 kOk 表示成功。
enum class StatusCode {
    kOk = 0,               ///< 操作成功。
    kInvalidArgument = 1,  ///< 参数不合法。
    kNotFound = 2,         ///< 请求的资源不存在。
    kTimeout = 3,          ///< 操作超时。
    kInternal = 4,         ///< 内部错误。
};

/// 无返回值操作的结果，独占错误信息，只支持移动；调用方应检查返回状态。
/// 成功状态不分配内存，所有错误状态均分配错误表示。
/// 支持并发只读访问；修改、移动或析构同一对象时需要调用方同步。
class [[nodiscard]] Status {
   public:
    /// 创建成功状态，不分配内存。
    Status() noexcept = default;
    /// 禁止复制构造；使用移动转移所有权。
    Status(const Status&) = delete;
    /// 禁止复制赋值。
    Status& operator=(const Status&) = delete;
    /// 转移错误表示，源对象变为成功状态；不分配内存。
    Status(Status&&) noexcept = default;
    /// 释放原错误并接管源对象；源对象变为成功状态，自移动保留内容。
    Status& operator=(Status&&) noexcept = default;
    /// 释放拥有的错误表示及消息。
    ~Status() = default;

    /// 从错误码和消息构造状态；错误消息会复制到自身存储中。
    /// kOk 忽略消息且不分配；非 kOk 即使消息为空也分配，分配失败抛出 std::bad_alloc。
    Status(StatusCode code, std::string_view message = {}) {
        if (code != StatusCode::kOk) {
            rep_ = std::make_unique<ErrorRep>(code, message);
        }
    }

    /// 接管 std::string 右值消息；仅匹配非 const 的 std::string 右值。
    /// kOk 不消耗消息；错误状态仍需分配错误表示，分配失败抛出 std::bad_alloc。
    template <typename String, std::enable_if_t<std::is_same_v<String, std::string>, int> = 0>
    Status(StatusCode code, String&& message) {
        if (code != StatusCode::kOk) {
            rep_ = std::make_unique<ErrorRep>(code, std::forward<String>(message));
        }
    }

    /// 返回成功状态，不分配内存。
    static Status Ok() noexcept {
        return {};
    }

    /// 当错误码为 kOk 时返回 true。
    bool ok() const noexcept {
        return rep_ == nullptr;
    }

    /// 等价于 ok()，可用于 if (status) 等布尔判断。
    explicit operator bool() const noexcept {
        return ok();
    }

    /// 返回错误码；成功或被移动后的对象返回 kOk。
    StatusCode code() const noexcept {
        return rep_ ? rep_->code : StatusCode::kOk;
    }

    /// 返回借用的消息视图；成功或无消息错误返回空视图，可包含嵌入的空字符。
    /// 不应依赖视图在本对象被赋值、移动或销毁后仍有效；独立保存需构造 std::string。
    std::string_view message() const noexcept {
        return rep_ ? std::string_view(rep_->message) : std::string_view{};
    }

    /// 返回独立字符串：成功为 OK，错误为名称或“名称: 消息”。
    /// 未识别的错误码表示为 UNKNOWN(数值)；消息保留原始字节，不转义空字符。
    /// 不修改本对象；字符串构造及分配异常直接传播。
    std::string ToString() const {
        std::string text;
        switch (code()) {
            case StatusCode::kOk:
                return "OK";
            case StatusCode::kInvalidArgument:
                text = "INVALID_ARGUMENT";
                break;
            case StatusCode::kNotFound:
                text = "NOT_FOUND";
                break;
            case StatusCode::kTimeout:
                text = "TIMEOUT";
                break;
            case StatusCode::kInternal:
                text = "INTERNAL";
                break;
            default:
                text = "UNKNOWN(";
                text.append(std::to_string(static_cast<int>(code())));
                text.push_back(')');
                break;
        }
        const auto detail = message();
        if (!detail.empty()) {
            text.append(": ");
            text.append(detail);
        }
        return text;
    }

    /// 比较错误码及消息内容是否相同，不转移所有权。
    friend bool operator==(const Status& lhs, const Status& rhs) noexcept {
        return lhs.rep_ == rhs.rep_ || (lhs.code() == rhs.code() && lhs.message() == rhs.message());
    }

    /// 错误码或消息内容不同时返回 true。
    friend bool operator!=(const Status& lhs, const Status& rhs) noexcept {
        return !(lhs == rhs);
    }

   private:
    struct ErrorRep {
        template <typename Message>
        ErrorRep(StatusCode code, Message&& message)
            : code(code), message(std::forward<Message>(message)) {}

        const StatusCode code;
        const std::string message;
    };

    // Null is success; every error exclusively owns its representation.
    std::unique_ptr<ErrorRep> rep_;
};

}  // namespace tos

#endif  // TOS_STATUS_H_
