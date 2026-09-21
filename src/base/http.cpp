#include "tos/base/http.h"

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <curl/curl.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "atomic_file_writer.h"

namespace tos {
namespace {

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using HeaderListPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

struct CurlRuntime final {
    std::once_flag initialization;
    CURLcode result = CURLE_FAILED_INIT;
};

struct ResponseContext final {
    std::string body;
    detail::AtomicFileWriter* writer = nullptr;
    std::optional<Status> write_failure;
    std::exception_ptr callback_exception;
};

struct UploadContext final {
    std::ifstream input;
    curl_off_t expected_size = 0;
    curl_off_t bytes_read = 0;
    bool read_failure = false;
    std::exception_ptr callback_exception;
};

CurlRuntime& Runtime() {
    static CurlRuntime runtime;
    return runtime;
}

void CleanupCurl() { curl_global_cleanup(); }

Status CurlError(CURLcode code, std::string_view operation) {
    StatusCode status_code = StatusCode::kUnavailable;
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        status_code = StatusCode::kCancelled;
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        status_code = StatusCode::kTimeout;
    } else if (code == CURLE_URL_MALFORMAT || code == CURLE_BAD_FUNCTION_ARGUMENT) {
        status_code = StatusCode::kInvalidArgument;
    }
    std::string message(operation);
    const char* detail = curl_easy_strerror(code);
    if (detail != nullptr && *detail != '\0') {
        message.append(": ");
        message.append(detail);
    }
    return Status(status_code, std::move(message));
}

Status EnsureCurlInitialized() {
    auto& runtime = Runtime();
    std::call_once(runtime.initialization, [&runtime] {
        runtime.result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (runtime.result == CURLE_OK) {
            static_cast<void>(std::atexit(CleanupCurl));
        }
    });
    return runtime.result == CURLE_OK ? Status::Ok()
                                      : CurlError(runtime.result, "could not initialize libcurl");
}

bool ContainsLineBreak(std::string_view value) {
    return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
}

bool ContainsNul(std::string_view value) { return value.find('\0') != std::string_view::npos; }

Status ValidateRequest(const HttpRequest& request) {
    if (request.method.empty() || ContainsLineBreak(request.method) ||
        ContainsNul(request.method)) {
        return Status(StatusCode::kInvalidArgument, "HTTP method must not be empty");
    }
    if (request.url.empty() || ContainsNul(request.url)) {
        return Status(StatusCode::kInvalidArgument, "HTTP URL must not be empty");
    }
    if (request.timeout.count() <= 0 ||
        request.timeout.count() > static_cast<std::int64_t>(LONG_MAX)) {
        return Status(StatusCode::kInvalidArgument, "HTTP timeout is outside the valid range");
    }
    if (request.body.size() > static_cast<std::size_t>(LONG_MAX)) {
        return Status(StatusCode::kOutOfRange, "HTTP request body is too large");
    }
    for (const auto& [name, value] : request.headers) {
        if (name.empty() || ContainsLineBreak(name) || ContainsLineBreak(value) ||
            ContainsNul(name) || ContainsNul(value)) {
            return Status(StatusCode::kInvalidArgument, "HTTP headers contain invalid characters");
        }
    }
    if (request.tls && request.tls->client_certificate.empty() != request.tls->client_key.empty()) {
        return Status(StatusCode::kInvalidArgument,
                      "client certificate and private key must be provided together");
    }
    if (request.tls &&
        (!request.tls->ca_file.empty() && ContainsNul(request.tls->ca_file.utf8()))) {
        return Status(StatusCode::kInvalidArgument, "TLS paths must not contain NUL bytes");
    }
    return Status::Ok();
}

std::size_t WriteCallback(char* data, std::size_t size, std::size_t count, void* context) {
    auto* response = static_cast<ResponseContext*>(context);
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
        return 0;
    }
    const std::size_t bytes = size * count;
    try {
        if (response->writer != nullptr) {
            Status status = response->writer->Write(std::string_view(data, bytes));
            if (!status) {
                response->write_failure.emplace(std::move(status));
                return 0;
            }
        } else {
            response->body.append(data, bytes);
        }
    } catch (...) {
        response->callback_exception = std::current_exception();
        return 0;
    }
    return bytes;
}

std::size_t ReadCallback(char* data, std::size_t size, std::size_t count, void* context) {
    auto* upload = static_cast<UploadContext*>(context);
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
        upload->read_failure = true;
        return CURL_READFUNC_ABORT;
    }
    const std::size_t requested = size * count;
    try {
        upload->input.read(data, static_cast<std::streamsize>(requested));
        const std::streamsize received = upload->input.gcount();
        if (received > 0) {
            upload->bytes_read += static_cast<curl_off_t>(received);
            if (upload->input.bad()) {
                upload->read_failure = true;
            }
            return static_cast<std::size_t>(received);
        }
        if (upload->bytes_read < upload->expected_size) {
            upload->read_failure = true;
            return CURL_READFUNC_ABORT;
        }
        return 0;
    } catch (...) {
        upload->callback_exception = std::current_exception();
        return CURL_READFUNC_ABORT;
    }
}

int ProgressCallback(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancellation = static_cast<const std::atomic_bool*>(context);
    return cancellation != nullptr && cancellation->load(std::memory_order_acquire) ? 1 : 0;
}

HttpRequest MakeRequest(std::string_view method, std::string_view url,
                        const HttpRequestOptions& options) {
    HttpRequest request;
    request.method = std::string(method);
    request.url = std::string(url);
    request.headers = options.headers;
    request.tls = options.tls;
    request.timeout = options.timeout;
    request.cancellation = options.cancellation;
    return request;
}

bool HeaderNameEquals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto lower = [](unsigned char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a')
                                                : static_cast<char>(value);
        };
        if (lower(static_cast<unsigned char>(lhs[index])) !=
            lower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

bool HasHeader(const std::vector<std::pair<std::string, std::string>>& headers,
               std::string_view name) {
    for (const auto& header : headers) {
        if (HeaderNameEquals(header.first, name)) {
            return true;
        }
    }
    return false;
}

Result<HttpResponse> PerformRequestInternal(const HttpRequest& request,
                                            ResponseContext& response_context,
                                            UploadContext* upload) {
    Status valid = ValidateRequest(request);
    if (!valid) {
        return std::move(valid);
    }
    Status initialized = EnsureCurlInitialized();
    if (!initialized) {
        return std::move(initialized);
    }

    CurlPtr curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        return CurlError(CURLE_FAILED_INIT, "could not create libcurl handle");
    }

    curl_slist* raw_headers = nullptr;
    HeaderListPtr headers(raw_headers, curl_slist_free_all);
    std::vector<std::string> header_lines;
    header_lines.reserve(request.headers.size());
    for (const auto& [name, value] : request.headers) {
        header_lines.push_back(name + ": " + value);
        raw_headers = curl_slist_append(headers.get(), header_lines.back().c_str());
        if (raw_headers == nullptr) {
            return Status(StatusCode::kResourceExhausted, "could not allocate HTTP headers");
        }
        headers.release();
        headers.reset(raw_headers);
    }

    auto set_option = [&curl](CURLoption option, auto value, std::string_view operation) {
        const CURLcode result = curl_easy_setopt(curl.get(), option, value);
        return result == CURLE_OK ? Status::Ok() : CurlError(result, operation);
    };

    Status status = set_option(CURLOPT_URL, request.url.c_str(), "could not configure HTTP URL");
    if (!status) {
        return std::move(status);
    }
    if (request.method == "GET") {
        status = set_option(CURLOPT_HTTPGET, 1L, "could not configure HTTP method");
    } else {
        status = set_option(CURLOPT_POST, request.method == "POST" ? 1L : 0L,
                            "could not configure HTTP method");
        if (status && request.method != "POST") {
            status = set_option(CURLOPT_CUSTOMREQUEST, request.method.c_str(),
                                "could not configure HTTP method");
        }
    }
    if (!status) {
        return std::move(status);
    }
    if (upload != nullptr) {
        status =
            set_option(CURLOPT_READFUNCTION, ReadCallback, "could not configure upload callback");
        if (status) {
            status = set_option(CURLOPT_READDATA, upload, "could not configure upload data");
        }
        if (status) {
            status = set_option(CURLOPT_POSTFIELDSIZE_LARGE, upload->expected_size,
                                "could not configure upload size");
        }
    } else if (request.method != "GET" || !request.body.empty()) {
        status = set_option(CURLOPT_POSTFIELDS, request.body.data(),
                            "could not configure HTTP request body");
        if (status) {
            status = set_option(CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()),
                                "could not configure HTTP request body size");
        }
    }
    if (!status) {
        return std::move(status);
    }
    status = set_option(CURLOPT_HTTPHEADER, headers.get(), "could not configure HTTP headers");
    if (!status) {
        return std::move(status);
    }
    status = set_option(CURLOPT_SSL_VERIFYPEER, 1L, "could not configure TLS peer verification");
    if (!status) {
        return std::move(status);
    }
    status = set_option(CURLOPT_SSL_VERIFYHOST, 2L, "could not configure TLS host verification");
    if (!status) {
        return std::move(status);
    }
    status =
        set_option(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1, "could not configure HTTP version");
    if (!status) {
        return std::move(status);
    }
    status = set_option(CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()),
                        "could not configure HTTP timeout");
    if (!status) {
        return std::move(status);
    }
    status =
        set_option(CURLOPT_WRITEFUNCTION, WriteCallback, "could not configure response callback");
    if (status) {
        status =
            set_option(CURLOPT_WRITEDATA, &response_context, "could not configure response data");
    }
    if (status) {
        status = set_option(CURLOPT_NOPROGRESS, 0L, "could not configure cancellation callback");
    }
    if (status) {
        status = set_option(CURLOPT_XFERINFOFUNCTION, ProgressCallback,
                            "could not configure cancellation callback");
    }
    if (status) {
        status = set_option(CURLOPT_XFERINFODATA, request.cancellation,
                            "could not configure cancellation data");
    }
    if (request.tls && status) {
        if (!request.tls->ca_file.empty()) {
            status = set_option(CURLOPT_CAINFO, request.tls->ca_file.utf8().c_str(),
                                "could not configure CA file");
        }
        if (status && !request.tls->client_certificate.empty()) {
            status = set_option(CURLOPT_SSLCERT, request.tls->client_certificate.utf8().c_str(),
                                "could not configure client certificate");
        }
        if (status && !request.tls->client_key.empty()) {
            status = set_option(CURLOPT_SSLKEY, request.tls->client_key.utf8().c_str(),
                                "could not configure client private key");
        }
    }
    if (!status) {
        return std::move(status);
    }

    const CURLcode result = curl_easy_perform(curl.get());
    if (response_context.callback_exception) {
        return Status(StatusCode::kResourceExhausted, "could not store HTTP response body");
    }
    if (response_context.write_failure) {
        return std::move(*response_context.write_failure);
    }
    if (upload != nullptr && upload->callback_exception) {
        return Status(StatusCode::kUnavailable, "could not read upload file");
    }
    if (upload != nullptr && upload->read_failure) {
        return Status(StatusCode::kUnavailable, "could not read upload file");
    }
    if (result != CURLE_OK) {
        return CurlError(result, "HTTP request failed");
    }

    HttpResponse response;
    const CURLcode info_result =
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    if (info_result != CURLE_OK) {
        return CurlError(info_result, "could not read HTTP response status");
    }
    if (response_context.writer == nullptr) {
        response.body = std::move(response_context.body);
    }
    return response;
}

}  // namespace

Result<HttpResponse> PerformHttpRequest(const HttpRequest& request) {
    ResponseContext response_context;
    return PerformRequestInternal(request, response_context, nullptr);
}

Result<HttpResponse> HttpGet(std::string_view url, const HttpRequestOptions& options) {
    return PerformHttpRequest(MakeRequest("GET", url, options));
}

Result<HttpResponse> HttpPost(std::string_view url, std::string_view body,
                              const HttpRequestOptions& options) {
    HttpRequest request = MakeRequest("POST", url, options);
    request.body = std::string(body);
    return PerformHttpRequest(request);
}

Result<HttpResponse> DownloadFile(std::string_view url, const Path& destination,
                                  const HttpRequestOptions& options) {
    auto writer_result = detail::AtomicFileWriter::Create(destination);
    if (!writer_result) {
        return std::move(writer_result).status();
    }
    HttpRequest request = MakeRequest("GET", url, options);
    ResponseContext response_context;
    response_context.writer = &writer_result.value();
    auto response = PerformRequestInternal(request, response_context, nullptr);
    if (!response) {
        return std::move(response).status();
    }
    if (response->status_code < 200 || response->status_code >= 300) {
        return std::move(response).value();
    }
    Status committed = writer_result.value().Commit();
    if (!committed) {
        return std::move(committed);
    }
    return std::move(response).value();
}

Result<HttpResponse> UploadFile(std::string_view url, const Path& source,
                                const HttpRequestOptions& options) {
    auto metadata = GetFileMetadata(source);
    if (!metadata) {
        return std::move(metadata).status();
    }
    if (metadata.value().type != FileType::kRegular) {
        return Status(StatusCode::kFailedPrecondition, "upload source is not a regular file");
    }
    if (metadata.value().size >
        static_cast<std::uintmax_t>(std::numeric_limits<curl_off_t>::max())) {
        return Status(StatusCode::kOutOfRange, "upload source is too large");
    }
    std::ifstream input(std::filesystem::u8path(source.utf8()), std::ios::binary);
    if (!input.is_open()) {
        return Status(StatusCode::kUnavailable,
                      "could not open upload file '" + source.utf8() + "'");
    }
    UploadContext upload;
    upload.input = std::move(input);
    upload.expected_size = static_cast<curl_off_t>(metadata.value().size);
    HttpRequest request = MakeRequest("POST", url, options);
    if (!HasHeader(request.headers, "Content-Type")) {
        request.headers.emplace_back("Content-Type", "application/octet-stream");
    }
    ResponseContext response_context;
    return PerformRequestInternal(request, response_context, &upload);
}

}  // namespace tos
