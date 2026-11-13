// Minimal HTTP/1.1 server: winsock2 + fixed thread pool. Serves the embedded web
// console and a JSON API. One request per connection (Connection: close) so a
// small worker pool can serve many concurrent clients without keep-alive stalls.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "index.h"

namespace ms {

struct ServerOptions {
    std::string host = "127.0.0.1";
    int port = 8204;
    int threads = 0;  // 0 => auto (hardware concurrency, clamped to [4, 32])
    bool verbose = false;
    std::string version = "minisearch/1.0.0";
};

class HttpServer {
public:
    explicit HttpServer(SearchIndex& index, ServerOptions opts);
    ~HttpServer();

    // Binds, listens, serves until stop() is called. Returns false on bind error.
    bool run(std::string* error);
    void stop();

    // Internal request/response types (defined in http.cpp).
    struct Request;
    struct Response;

private:
    using SocketHandle = uintptr_t;
    static constexpr SocketHandle kInvalidSocket = static_cast<SocketHandle>(~static_cast<SocketHandle>(0));

    bool prepare(std::string* error);
    void accept_loop();
    void worker_loop();
    void submit(SocketHandle client);
    void handle_connection(SocketHandle client);
    Response route(const Request& req);
    int worker_count() const;

    SearchIndex& index_;
    ServerOptions opts_;
    std::vector<std::thread> workers_;
    std::queue<SocketHandle> queue_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<SocketHandle> listen_socket_{kInvalidSocket};
    bool wsa_ready_ = false;
};

}  // namespace ms
