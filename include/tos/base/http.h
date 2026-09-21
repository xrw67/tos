#ifndef TOS_BASE_HTTP_H_
#define TOS_BASE_HTTP_H_

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tos/base/filesystem.h"
#include "tos/base/result.h"

namespace tos {

/// Optional CA and client credentials used for a verified HTTPS request.
struct HttpTlsOptions final {
    Path ca_file;
    Path client_certificate;
    Path client_key;
};

/// Common options shared by the convenience HTTP helpers.
struct HttpRequestOptions final {
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<HttpTlsOptions> tls;
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    const std::atomic_bool* cancellation = nullptr;
};

/// A synchronous HTTP request. The cancellation pointer is borrowed for the duration of the
/// request and may be queried concurrently by the HTTP implementation.
struct HttpRequest final {
    std::string method;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::optional<HttpTlsOptions> tls;
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    const std::atomic_bool* cancellation = nullptr;
};

/// The HTTP response. HTTP status codes, including 4xx and 5xx, are returned as responses rather
/// than Status failures; Status reports transport, configuration, timeout, and cancellation
/// failures.
struct HttpResponse final {
    long status_code = 0;
    std::string body;
};

/// Performs one synchronous HTTP request through libcurl.
[[nodiscard]] Result<HttpResponse> PerformHttpRequest(const HttpRequest& request);

/// Performs a synchronous GET request. HTTP error status codes are returned in HttpResponse.
[[nodiscard]] Result<HttpResponse> HttpGet(std::string_view url,
                                           const HttpRequestOptions& options = {});

/// Performs a synchronous POST request with an in-memory request body.
[[nodiscard]] Result<HttpResponse> HttpPost(std::string_view url, std::string_view body,
                                            const HttpRequestOptions& options = {});

/// Streams a GET response into destination and atomically replaces it for a 2xx response.
[[nodiscard]] Result<HttpResponse> DownloadFile(std::string_view url, const Path& destination,
                                                const HttpRequestOptions& options = {});

/// Streams source as the raw POST request body.
[[nodiscard]] Result<HttpResponse> UploadFile(std::string_view url, const Path& source,
                                              const HttpRequestOptions& options = {});

}  // namespace tos

#endif  // TOS_BASE_HTTP_H_
