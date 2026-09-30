#include "tos/base/base64.h"
#include "tos/base/thread_pool.h"
#ifdef TOS_TEST_APP
#include "tos/app/app.h"
#endif
#ifdef TOS_TEST_CRYPTO
#include "tos/base/certificate.h"
#include "tos/base/crypto.h"
#endif
#ifdef TOS_TEST_HTTP
#include "tos/base/http.h"
#endif

int main() {
    const auto decoded = tos::Base64Decode("Zg==");
    if (!decoded || decoded->size() != 1 || (*decoded)[0] != 'f') return 1;
    tos::ThreadPool pool(1);
    if (!pool.Shutdown()) return 2;
#ifdef TOS_TEST_APP
    tos::AppOptions options;
    options.thread_pool_worker_count = 1;
    tos::App app(std::move(options));
    if (!app.Start() || !app.Stop()) return 3;
#endif
#ifdef TOS_TEST_CRYPTO
    if (tos::Sha256String("abc") !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return 4;
    if (tos::ValidateClientCertificate("", tos::Path{})) return 5;
#endif
#ifdef TOS_TEST_HTTP
    const auto response = tos::PerformHttpRequest(tos::HttpRequest{});
    if (response || !tos::IsInvalidArgument(response.status())) return 6;
#endif
    return 0;
}
