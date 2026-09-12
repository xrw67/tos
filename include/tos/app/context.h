#ifndef TOS_APP_CONTEXT_H_
#define TOS_APP_CONTEXT_H_

#include <type_traits>
#include <typeindex>
#include <utility>

#include "tos/app/service.h"
#include "tos/base/config.h"
#include "tos/base/logging.h"
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

/// Type-indexed service registry supplied by App.
///
/// Context does not own services. Each non-empty GetService handle keeps one borrow active until
/// Reset() or handle destruction, after which its owner may unregister and destroy the service.
/// Context synchronizes registry operations only; services provide their own synchronization.
/// Context is owned by App and must not outlive it. Config and Logger references are valid only
/// while the owning App is alive.
class Context {
   public:
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    virtual ~Context() = default;

    /// Returns the App-owned immutable configuration. The reference remains valid while the
    /// owning App is alive and does not throw.
    [[nodiscard]] const Config& config() const noexcept { return *config_; }

    /// Returns the App-owned logger. The reference remains valid while the owning App is alive and
    /// does not throw. Logger operations remain safe for concurrent callers.
    [[nodiscard]] Logger& logger() noexcept { return *logger_; }

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
        return RegisterService(std::type_index(typeid(T)), service);
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
        const auto* service = AcquireService(std::type_index(typeid(T)));
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
        return UnregisterService(std::type_index(typeid(T)), expected);
    }

   protected:
    template <typename>
    friend class ServiceHandle;

    Context(const Config& config, Logger& logger) noexcept : config_(&config), logger_(&logger) {}

    [[nodiscard]] virtual Status RegisterService(std::type_index type, const Service* service) = 0;
    [[nodiscard]] virtual const Service* AcquireService(std::type_index type) const = 0;
    [[nodiscard]] virtual Status ReleaseService(std::type_index type,
                                                const Service* service) const = 0;
    [[nodiscard]] virtual Status UnregisterService(std::type_index type,
                                                   const Service* expected) = 0;

   private:
    const Config* config_;
    Logger* logger_;
};

}  // namespace tos

template <typename T>
tos::Status tos::ServiceHandle<T>::Reset() {
    if (!service_) {
        return tos::Status::Ok();
    }
    tos::Status result = context_->ReleaseService(std::type_index(typeid(T)), service_);
    if (result.ok()) {
        context_ = nullptr;
        service_ = nullptr;
    }
    return result;
}

#endif  // TOS_APP_CONTEXT_H_
