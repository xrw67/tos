#ifndef TOS_APP_CONTEXT_H_
#define TOS_APP_CONTEXT_H_

#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "tos/app/debug.h"
#include "tos/app/event.h"
#include "tos/app/service.h"
#include "tos/base/config.h"
#include "tos/base/executor.h"
#include "tos/base/logging.h"
#include "tos/base/scheduler.h"
#include "tos/base/status.h"

namespace tos {

class Context;

/// Move-only RAII borrow of a registered service.
///
/// The handle does not own the service or outlive its Context. Destruction returns the borrow and
/// suppresses failures; call Reset() to observe them.
template <typename T>
class ServiceHandle final {
   public:
    ServiceHandle() noexcept = default;
    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;

    ServiceHandle(ServiceHandle&& other) noexcept
        : context_(other.context_), service_(other.service_) {
        other.context_ = nullptr;
        other.service_ = nullptr;
    }

    ServiceHandle& operator=(ServiceHandle&& other) noexcept {
        if (this != &other && ResetNoexcept()) {
            context_ = other.context_;
            service_ = other.service_;
            other.context_ = nullptr;
            other.service_ = nullptr;
        }
        return *this;
    }

    ~ServiceHandle() noexcept { ResetNoexcept(); }

    /// Returns the borrowed service pointer, or nullptr for an empty handle.
    [[nodiscard]] T* get() const noexcept { return service_; }
    [[nodiscard]] T* operator->() const noexcept { return service_; }
    [[nodiscard]] T& operator*() const noexcept { return *service_; }
    [[nodiscard]] explicit operator bool() const noexcept { return service_ != nullptr; }

    /// Returns this borrow. On failure it stays active for retry; allocation and mutex exceptions
    /// propagate.
    [[nodiscard]] Status Reset();

   private:
    friend class Context;

    ServiceHandle(const Context* context, T* service) noexcept
        : context_(context), service_(service) {}

    bool ResetNoexcept() noexcept {
        try {
            return Reset().ok();
        } catch (...) {
            return false;
        }
    }

    const Context* context_{nullptr};
    T* service_{nullptr};
};

/// Application services and infrastructure supplied to modules.
///
/// This abstract interface does not own services. GetService borrows block unregistration until
/// reset or destruction. Implementations synchronize the registry, not services; reference
/// lifetimes are implementation-defined.
class Context {
   public:
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    virtual ~Context() noexcept = default;

    /// Returns immutable configuration; lifetime is implementation-defined and does not throw.
    [[nodiscard]] virtual const Config& config() const noexcept = 0;

    /// Returns the logger; lifetime is implementation-defined and does not throw.
    [[nodiscard]] virtual Logger& logger() noexcept = 0;

    /// Returns the synchronous EventBus; lifetime is implementation-defined and does not throw.
    [[nodiscard]] virtual EventBus& events() noexcept = 0;

    /// Returns the shared executor; lifetime is implementation-defined, shutdown remains Context
    /// owned, and the accessor does not throw.
    [[nodiscard]] virtual Executor& executor() noexcept = 0;

    /// Returns the monotonic scheduler; lifetime is implementation-defined, shutdown remains
    /// Context owned, and the accessor does not throw.
    [[nodiscard]] virtual ScheduledExecutor& scheduler() noexcept = 0;

    /// Registers a debug handler. Semantics match DebugController::RegisterHandler; modules must
    /// unregister handlers before captured state is destroyed. Allocation exceptions propagate.
    [[nodiscard]] virtual Status RegisterDebugHandler(const std::string& command,
                                                      const std::string& description,
                                                      DebugHandler handler) = 0;

    /// Removes a debug handler with DebugController::UnregisterHandler semantics. Allocation
    /// exceptions propagate.
    [[nodiscard]] virtual Status UnregisterDebugHandler(std::string_view command) = 0;

    /// Registers a non-owning, unqualified Service pointer. Null returns kInvalidArgument; a
    /// duplicate exact type returns kAlreadyExists. Allocation and mutex exceptions propagate.
    template <typename T>
    [[nodiscard]] Status RegisterService(T* service) {
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                      "registered service type must not be cv-qualified");
        static_assert(std::is_base_of_v<Service, T>,
                      "registered service type must derive from tos::Service");
        if (!service) {
            return Status(StatusCode::kInvalidArgument, "service pointer must not be null");
        }
        return RegisterServiceImpl(std::type_index(typeid(T)), service);
    }

    /// Borrows the exact service type, or returns an empty handle when absent. The handle must not
    /// outlive this Context; mutex exceptions propagate.
    template <typename T>
    [[nodiscard]] ServiceHandle<T> GetService() const {
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                      "requested service type must not be cv-qualified");
        static_assert(std::is_base_of_v<Service, T>,
                      "requested service type must derive from tos::Service");
        const auto* service = AcquireServiceImpl(std::type_index(typeid(T)));
        return ServiceHandle<T>(this, const_cast<T*>(static_cast<const T*>(service)));
    }

    /// Removes the exact type registration only if expected matches. Null returns kInvalidArgument,
    /// a missing type returns kNotFound, and a mismatch returns kFailedPrecondition. Does not
    /// destroy expected; mutex exceptions propagate.
    template <typename T>
    [[nodiscard]] Status UnregisterService(T* expected) {
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                      "registered service type must not be cv-qualified");
        static_assert(std::is_base_of_v<Service, T>,
                      "registered service type must derive from tos::Service");
        if (!expected) {
            return Status(StatusCode::kInvalidArgument, "service pointer must not be null");
        }
        return UnregisterServiceImpl(std::type_index(typeid(T)), expected);
    }

   protected:
    Context() = default;

    /// Implements service registration for the exact, unqualified service type. The public
    /// template validates its arguments before calling this function. Allocation and mutex
    /// exceptions propagate.
    [[nodiscard]] virtual Status RegisterServiceImpl(std::type_index type,
                                                     const Service* service) = 0;

    /// Implements acquisition of a service borrow. Returning nullptr represents an unregistered
    /// type. Mutex exceptions propagate.
    [[nodiscard]] virtual const Service* AcquireServiceImpl(std::type_index type) const = 0;

    /// Implements release of a previous service borrow. Allocation and mutex exceptions
    /// propagate.
    [[nodiscard]] virtual Status ReleaseServiceImpl(std::type_index type,
                                                    const Service* service) const = 0;

    /// Implements removal of an unborrowed service registration. Allocation and mutex exceptions
    /// propagate.
    [[nodiscard]] virtual Status UnregisterServiceImpl(std::type_index type,
                                                       const Service* expected) = 0;

   private:
    template <typename>
    friend class ServiceHandle;
};

}  // namespace tos

template <typename T>
tos::Status tos::ServiceHandle<T>::Reset() {
    if (!service_) {
        return tos::Status::Ok();
    }
    tos::Status result = context_->ReleaseServiceImpl(std::type_index(typeid(T)), service_);
    if (result.ok()) {
        context_ = nullptr;
        service_ = nullptr;
    }
    return result;
}

#endif  // TOS_APP_CONTEXT_H_
