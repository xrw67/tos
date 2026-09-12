#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tos/app/app.h"

namespace {

class GreetingService final : public tos::Service {
   public:
    explicit GreetingService(std::string message) : message_(std::move(message)) {}

    [[nodiscard]] const std::string& message() const noexcept { return message_; }

   private:
    std::string message_;
};

class GreetingServiceModule final : public tos::Module {
   public:
    std::string Name() const override { return "greeting-service"; }
    std::vector<std::string> Dependencies() const override { return {}; }
    tos::Status OnLoad(tos::Context& context) override {
        return context.RegisterService(&service_);
    }
    tos::Status OnUnload(tos::Context& context) override {
        return context.UnregisterService(&service_);
    }

   private:
    GreetingService service_{"hello from GreetingService"};
};

class GreetingModule final : public tos::Module {
   public:
    std::string Name() const override { return "greeting"; }
    std::vector<std::string> Dependencies() const override { return {"greeting-service"}; }
    tos::Status OnLoad(tos::Context& context) override {
        auto service = context.GetService<GreetingService>();
        if (!service) {
            return {tos::StatusCode::kNotFound, "GreetingService is not registered"};
        }
        tos::Status logged = context.logger().Info({{"service", std::string("GreetingService")}},
                                                   "{}", service->message());
        return logged;
    }
    tos::Status OnUnload(tos::Context&) override { return tos::Status::Ok(); }
};

}  // namespace

int main() {
    tos::AppOptions options;
    options.name = "application-example";
    tos::App application(std::move(options));
    if (!application.AddModule(std::make_unique<GreetingServiceModule>()) ||
        !application.AddModule(std::make_unique<GreetingModule>()) || !application.Start()) {
        return 1;
    }
    std::cout << "application example ready\n";
    return application.Stop() ? 0 : 1;
}
