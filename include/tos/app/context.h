#ifndef TOS_APP_CONTEXT_H_
#define TOS_APP_CONTEXT_H_

#include <type_traits>
#include <typeindex>
#include <utility>

#include "tos/app/event_bus.h"
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
/// The handle does not own the service and must not outlive its Context. Its destructor returns
/// the borrow automatically and suppresses release failures or exceptions. Call Reset() when the
/// release status must be observed.
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

    /// Returns this borrow to its Context. On failure the handle remains active for a retry.
    /// Allocation and mutex exceptions propagate.
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
/// This is an abstract interface. App supplies its own implementation, and alternate hosts may
/// implement it for a different application environment. Context does not own services. Each
/// non-empty GetService handle keeps one borrow active until Reset() or handle destruction, after
/// which its owner may unregister and destroy the service. Implementations synchronize registry
/// operations only; services provide their own synchronization. References returned by this
/// interface remain valid only for the lifetime documented by the implementation.
class Context {
   public:
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    virtual ~Context() noexcept = default;

    /// Returns immutable configuration. The reference lifetime is defined by the implementation
    /// and this accessor does not throw.
    [[nodiscard]] virtual const Config& config() const noexcept = 0;

    /// Returns the logger. The reference lifetime is defined by the implementation and this
    /// accessor does not throw. Logger operations remain safe for concurrent callers.
    [[nodiscard]] virtual Logger& logger() noexcept = 0;

    /// Returns the synchronous EventBus. The reference lifetime is defined by the implementation
    /// and this accessor does not throw. EventBus operations are safe for concurrent callers.
    [[nodiscard]] virtual EventBus& events() noexcept = 0;

    /// Returns the shared executor. The reference lifetime is defined by the implementation. It
    /// supports concurrent submissions but does not grant shutdown control.
    [[nodiscard]] virtual Executor& executor() noexcept = 0;

    /// Returns the monotonic scheduler. The reference lifetime is defined by the implementation;
    /// it supports concurrent scheduling but does not grant shutdown control.
    [[nodiscard]] virtual ScheduledExecutor& scheduler() noexcept = 0;

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

    /// Borrows the registered Service through a move-only RAII handle, or returns an empty handle
    /// when the exact type is not registered. The handle must not outlive this Context. Mutex
    /// exceptions propagate.
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
