#include "tos/base/http.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <gtest/gtest.h>

namespace tos {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void CloseSocket(Socket socket) { close(socket); }
#endif

class LoopbackServer final {
   public:
    explicit LoopbackServer(std::chrono::milliseconds response_delay = {},
                            int response_status = 201, std::string response_body = "created")
        : response_delay_(response_delay),
          response_status_(response_status),
          response_body_(std::move(response_body)) {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            std::abort();
        }
        winsock_started_ = true;
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == kInvalidSocket) {
            std::abort();
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(0);
        if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            std::abort();
        }
#if defined(_WIN32)
        int address_length = sizeof(address);
#else
        socklen_t address_length = sizeof(address);
#endif
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
            std::abort();
        }
        port_ = ntohs(address.sin_port);
        if (listen(listener_, 1) != 0) {
            std::abort();
        }
        worker_ = std::thread(&LoopbackServer::Run, this);
    }

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    ~LoopbackServer() {
        if (listener_ != kInvalidSocket) {
            CloseSocket(listener_);
            listener_ = kInvalidSocket;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
#if defined(_WIN32)
        if (winsock_started_) {
            WSACleanup();
        }
#endif
    }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    [[nodiscard]] std::string request() const {
        std::lock_guard<std::mutex> lock(request_mutex_);
        return request_;
    }

   private:
    void Run() {
        sockaddr_in address{};
#if defined(_WIN32)
        int address_length = sizeof(address);
#else
        socklen_t address_length = sizeof(address);
#endif
        const Socket client =
            accept(listener_, reinterpret_cast<sockaddr*>(&address), &address_length);
        if (client == kInvalidSocket) {
            return;
        }

        std::string request;
        char buffer[1024];
        for (;;) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                break;
            }
            request.append(buffer, static_cast<std::size_t>(received));
            const std::size_t header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                continue;
            }
            std::size_t content_length = 0;
            const std::string marker = "Content-Length: ";
            const std::size_t marker_position = request.find(marker);
            if (marker_position != std::string::npos) {
                const std::size_t value_start = marker_position + marker.size();
                const std::size_t value_end = request.find('\r', value_start);
                content_length = std::stoull(request.substr(value_start, value_end - value_start));
            }
            if (request.size() >= header_end + 4 + content_length) {
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(request_mutex_);
            request_ = std::move(request);
        }
        std::this_thread::sleep_for(response_delay_);
        const std::string response =
            "HTTP/1.1 " + std::to_string(response_status_) +
            " Test\r\nContent-Length: " + std::to_string(response_body_.size()) +
            "\r\nConnection: close\r\nX-Test-Response: yes\r\n\r\n" + response_body_;
        std::size_t offset = 0;
        while (offset < response.size()) {
            const int sent = send(client, response.data() + offset,
                                  static_cast<int>(response.size() - offset), 0);
            if (sent <= 0) {
                break;
            }
            offset += static_cast<std::size_t>(sent);
        }
        CloseSocket(client);
    }

    Socket listener_ = kInvalidSocket;
    std::uint16_t port_ = 0;
    std::chrono::milliseconds response_delay_;
    int response_status_;
    std::string response_body_;
    std::thread worker_;
    mutable std::mutex request_mutex_;
    std::string request_;
#if defined(_WIN32)
    bool winsock_started_ = false;
#endif
};

std::string Url(const LoopbackServer& server) {
    return "http://127.0.0.1:" + std::to_string(server.port()) + "/test";
}

Path TemporaryPath(std::string_view suffix) {
    static std::atomic_uint64_t next_id{0};
    return Path::Parse(
               (std::filesystem::temp_directory_path() /
                ("tos-http-" + std::to_string(next_id.fetch_add(1)) + "-" + std::string(suffix)))
                   .u8string())
        .value();
}

TEST(HttpTest, SendsPostAndReturnsStatusBodyAndServerRequest) {
    LoopbackServer server;
    HttpRequest request;
    request.method = "POST";
    request.url = Url(server);
    request.headers = {{"Content-Type", "application/octet-stream"}, {"X-Test", "header"}};
    request.body = "payload";
    request.timeout = std::chrono::seconds(2);

    const auto response = PerformHttpRequest(request);
    ASSERT_TRUE(response) << response.status().ToString();
    EXPECT_EQ(response->status_code, 201);
    EXPECT_EQ(response->body, "created");
    const std::string received = server.request();
    const std::string request_line = "POST /test HTTP/1.1\r\n";
    EXPECT_EQ(received.compare(0, request_line.size(), request_line), 0);
    EXPECT_NE(received.find("Content-Type: application/octet-stream\r\n"), std::string::npos);
    EXPECT_NE(received.find("X-Test: header\r\n"), std::string::npos);
    EXPECT_TRUE(received.size() >= 7 && received.compare(received.size() - 7, 7, "payload") == 0);
}

TEST(HttpTest, RejectsInvalidRequestAndUnpairedClientCredentials) {
    HttpRequest empty_method;
    empty_method.url = "http://127.0.0.1";
    auto result = PerformHttpRequest(empty_method);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);

    HttpRequest empty_url;
    empty_url.method = "GET";
    result = PerformHttpRequest(empty_url);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);

    HttpRequest invalid_timeout;
    invalid_timeout.method = "GET";
    invalid_timeout.url = "http://127.0.0.1";
    invalid_timeout.timeout = std::chrono::milliseconds(0);
    result = PerformHttpRequest(invalid_timeout);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);

    HttpRequest invalid_header = invalid_timeout;
    invalid_header.timeout = std::chrono::seconds(1);
    invalid_header.headers = {{"X-Test\nInvalid", "value"}};
    result = PerformHttpRequest(invalid_header);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);

    HttpRequest invalid_tls = invalid_timeout;
    invalid_tls.timeout = std::chrono::seconds(1);
    invalid_tls.tls = HttpTlsOptions{};
    invalid_tls.tls->client_certificate = Path::Parse("client.pem").value();
    result = PerformHttpRequest(invalid_tls);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
}

TEST(HttpTest, MapsCancellationAndTimeoutFailures) {
    LoopbackServer server(std::chrono::milliseconds(200));
    std::atomic_bool cancelled{true};
    HttpRequest cancelled_request;
    cancelled_request.method = "GET";
    cancelled_request.url = Url(server);
    cancelled_request.timeout = std::chrono::seconds(2);
    cancelled_request.cancellation = &cancelled;
    auto cancelled_result = PerformHttpRequest(cancelled_request);
    ASSERT_FALSE(cancelled_result);
    EXPECT_EQ(cancelled_result.status().code(), StatusCode::kCancelled);

    LoopbackServer slow_server(std::chrono::milliseconds(200));
    HttpRequest timeout_request;
    timeout_request.method = "GET";
    timeout_request.url = Url(slow_server);
    timeout_request.timeout = std::chrono::milliseconds(20);
    const auto timeout_result = PerformHttpRequest(timeout_request);
    ASSERT_FALSE(timeout_result);
    EXPECT_EQ(timeout_result.status().code(), StatusCode::kTimeout);
}

TEST(HttpTest, ConvenienceHelpersTransferBodiesAndPreserveHttpErrors) {
    LoopbackServer get_server({}, 200, "get-body");
    const auto get_response = HttpGet(Url(get_server));
    ASSERT_TRUE(get_response) << get_response.status().ToString();
    EXPECT_EQ(get_response->status_code, 200);
    EXPECT_EQ(get_response->body, "get-body");

    LoopbackServer post_server({}, 200, "post-body");
    HttpRequestOptions options;
    options.headers.emplace_back("X-Helper", "yes");
    const auto post_response = HttpPost(Url(post_server), "post-payload", options);
    ASSERT_TRUE(post_response) << post_response.status().ToString();
    EXPECT_EQ(post_response->status_code, 200);
    EXPECT_EQ(post_response->body, "post-body");
    EXPECT_NE(post_server.request().find("X-Helper: yes\r\n"), std::string::npos);

    LoopbackServer error_server({}, 404, "not-found");
    const auto error_response = HttpGet(Url(error_server));
    ASSERT_TRUE(error_response) << error_response.status().ToString();
    EXPECT_EQ(error_response->status_code, 404);
    EXPECT_EQ(error_response->body, "not-found");
}

TEST(HttpTest, StreamsUploadAndDownloadsAtomically) {
    const Path source = TemporaryPath("source.bin");
    const Path destination = TemporaryPath("destination.bin");
    const Path failed_destination = TemporaryPath("failed.bin");
    ASSERT_TRUE(WriteTextFile(source, std::string(256 * 1024, 'u')));
    ASSERT_TRUE(WriteTextFile(destination, "old-content"));
    ASSERT_TRUE(WriteTextFile(failed_destination, "keep-content"));

    LoopbackServer upload_server({}, 200, "uploaded");
    const auto upload_response = UploadFile(Url(upload_server), source);
    ASSERT_TRUE(upload_response) << upload_response.status().ToString();
    EXPECT_EQ(upload_response->status_code, 200);
    EXPECT_NE(upload_server.request().find("Content-Type: application/octet-stream\r\n"),
              std::string::npos);
    EXPECT_EQ(upload_server.request().substr(upload_server.request().size() - 256 * 1024),
              std::string(256 * 1024, 'u'));

    LoopbackServer download_server({}, 200, "downloaded");
    const auto download_response = DownloadFile(Url(download_server), destination);
    ASSERT_TRUE(download_response) << download_response.status().ToString();
    EXPECT_EQ(download_response->status_code, 200);
    EXPECT_TRUE(download_response->body.empty());
    auto downloaded = ReadTextFile(destination);
    ASSERT_TRUE(downloaded) << downloaded.status().ToString();
    EXPECT_EQ(downloaded.value(), "downloaded");

    LoopbackServer failed_download_server({}, 404, "error-body");
    const auto failed_download = DownloadFile(Url(failed_download_server), failed_destination);
    ASSERT_TRUE(failed_download) << failed_download.status().ToString();
    EXPECT_EQ(failed_download->status_code, 404);
    auto preserved = ReadTextFile(failed_destination);
    ASSERT_TRUE(preserved) << preserved.status().ToString();
    EXPECT_EQ(preserved.value(), "keep-content");

    EXPECT_TRUE(RemovePath(source));
    EXPECT_TRUE(RemovePath(destination));
    EXPECT_TRUE(RemovePath(failed_destination));
}

}  // namespace
}  // namespace tos
