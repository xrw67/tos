#ifndef TOS_APP_SERVICE_H_
#define TOS_APP_SERVICE_H_

namespace tos {

/// Base class for services registered through a Context.
///
/// Services are non-owning: the caller manages their lifetime and must release all
/// ServiceHandle borrows before unregistering and destroying a service.
class Service {
   public:
    virtual ~Service() = default;
};

}  // namespace tos

#endif  // TOS_APP_SERVICE_H_
