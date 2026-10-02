#include "tos/base/base64.h"
#include "tos/base/file_lock.h"
#include "tos/base/scope_exit.h"
#include "tos/base/secure_random.h"
#include "tos/base/termination.h"
#include "tos/base/thread_pool.h"
#include "tos/base/uuid.h"
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
    int cleaned = 0;
    {
        auto guard = tos::MakeScopeExit([&] { ++cleaned; });
        auto moved = std::move(guard);
    }
    if (cleaned != 1) return 12;
    const auto decoded = tos::Base64Decode("Zg==");
    if (!decoded || decoded->size() != 1 || (*decoded)[0] != 'f') return 1;
    tos::ThreadPool pool(1);
    if (!pool.Shutdown()) return 2;
    const auto random = tos::SecureRandomBytes(16);
    const auto uuid = tos::GenerateUuidV4();
    if (!random || random->size() != 16 || !uuid || !tos::IsUuid(*uuid)) return 7;
    // Link the platform APIs even in the dependency-free base consumer.
    if (tos::FileLock::TryAcquire(tos::Path{})) return 8;
    if (tos::AtomicFileWriter::Create(tos::Path{})) return 9;
    tos::TerminationController termination;
    if (!termination.Install()) return 10;
    termination.RequestStop();
    if (!termination.Wait(std::chrono::milliseconds(0))) return 11;
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
