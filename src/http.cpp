#include "http.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string_view>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "json.h"
#include "text.h"
#include "web_ui.h"

namespace ms {

struct HttpServer::Request {
    std::string method;
    std::string path;
    std::string query;
    std::string version;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;
    std::string body;
};

struct HttpServer::Response {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

namespace {

using namespace std::chrono;

constexpr size_t kMaxHeaderBytes = 256 * 1024;
constexpr size_t kMaxBodyBytes = 64 * 1024 * 1024;

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '+') {
            out += ' ';
            continue;
        }
        if (c == '%' && i + 2 < s.size()) {
            const int hi = hex_val(s[i + 1]);
            const int lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::map<std::string, std::string> parse_query(std::string_view qs) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= qs.size()) {
        const size_t amp = qs.find('&', pos);
        const std::string_view pair = amp == std::string_view::npos ? qs.substr(pos) : qs.substr(pos, amp - pos);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            std::string key;
            std::string val;
            if (eq == std::string_view::npos) {
                key = url_decode(pair);
            } else {
                key = url_decode(pair.substr(0, eq));
                val = url_decode(pair.substr(eq + 1));
            }
            if (!key.empty()) out[key] = val;
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

HttpServer::Response make_json(int status, const JsonValue& v, bool pretty) {
    HttpServer::Response r;
    r.status = status;
    r.content_type = "application/json; charset=utf-8";
    r.body = v.dump(pretty);
    return r;
}

HttpServer::Response make_error(int status, const std::string& code, const std::string& message) {
    return make_json(status, make_error_json(code, message), false);
}

void send_all(SOCKET s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const int chunk = static_cast<int>(std::min<size_t>(data.size() - off, 1u << 20));
        const int n = ::send(s, data.data() + off, chunk, 0);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
}

bool read_request(SOCKET s, HttpServer::Request& req, std::string& err) {
    std::string buf;
    buf.reserve(16384);
    size_t header_end = std::string::npos;
    char chunk[16384];
    while (header_end == std::string::npos) {
        if (buf.size() > kMaxHeaderBytes) {
            err = "request headers too large";
            return false;
        }
        const int n = ::recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            err = "connection closed before request headers";
            return false;
        }
        buf.append(chunk, static_cast<size_t>(n));
        header_end = buf.find("\r\n\r\n");
    }

    const size_t eol = buf.find("\r\n");
    if (eol == std::string::npos) {
        err = "malformed request line";
        return false;
    }
    const std::string_view request_line(buf.data(), eol);
    const size_t sp1 = request_line.find(' ');
    const size_t sp2 = sp1 == std::string_view::npos ? std::string_view::npos : request_line.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
        err = "malformed request line";
        return false;
    }
    req.method = std::string(request_line.substr(0, sp1));
    std::string target(request_line.substr(sp1 + 1, sp2 - sp1 - 1));
    req.version = std::string(request_line.substr(sp2 + 1));
    const size_t qpos = target.find('?');
    if (qpos == std::string::npos) {
        req.path = std::move(target);
    } else {
        req.path = target.substr(0, qpos);
        req.query = target.substr(qpos + 1);
    }

    size_t pos = eol + 2;
    while (pos < header_end) {
        const size_t le = buf.find("\r\n", pos);
        if (le == std::string::npos || le > header_end) break;
        const std::string_view line(buf.data() + pos, le - pos);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string key = to_lower(line.substr(0, colon));
            size_t vstart = colon + 1;
            while (vstart < line.size() && (line[vstart] == ' ' || line[vstart] == '\t')) ++vstart;
            req.headers[key] = std::string(line.substr(vstart));
        }
        pos = le + 2;
    }
    req.params = parse_query(req.query);

    size_t content_length = 0;
    const auto it = req.headers.find("content-length");
    if (it != req.headers.end() && !it->second.empty()) {
        try {
            content_length = static_cast<size_t>(std::stoull(it->second));
        } catch (...) {
            err = "invalid Content-Length header";
            return false;
        }
    }
    if (content_length > kMaxBodyBytes) {
        err = "request body too large (max 64 MiB)";
        return false;
    }
    const size_t body_start = header_end + 4;
    req.body.assign(buf, body_start, buf.size() - body_start);
    while (req.body.size() < content_length) {
        const int n = ::recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            err = "connection closed while reading request body";
            return false;
        }
        req.body.append(chunk, static_cast<size_t>(n));
        if (req.body.size() > kMaxBodyBytes) {
            err = "request body too large (max 64 MiB)";
            return false;
        }
    }
    if (req.body.size() > content_length) req.body.resize(content_length);
    return true;
}

std::string build_response(int status, const std::string& content_type, const std::string& body, bool head_only) {
    std::string out;
    out.reserve(body.size() + 256);
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += status_text(status);
    out += "\r\nContent-Type: ";
    out += content_type;
    out += "\r\nContent-Length: ";
    out += std::to_string(body.size());
    out += "\r\nServer: minisearch";
    out += "\r\nAccess-Control-Allow-Origin: *";
    out += "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS";
    out += "\r\nAccess-Control-Allow-Headers: Content-Type";
    out += "\r\nConnection: close\r\n\r\n";
    if (!head_only) out += body;
    return out;
}

double parse_double(const std::string& s, double def) {
    if (s.empty()) return def;
    try {
        const double v = std::stod(s);
        if (std::isfinite(v)) return v;
    } catch (...) {
    }
    return def;
}

JsonValue render_search(const SearchResult& r) {
    JsonValue root = JsonValue::object();
    root.set("query", JsonValue::string(r.query));
    root.set("mode", JsonValue::string(r.mode));
    root.set("k", JsonValue::number(static_cast<double>(r.k)));
    root.set("took_ms", JsonValue::number(r.took_ms));
    root.set("matched_terms", JsonValue::number(static_cast<double>(r.matched_terms)));
    root.set("total_candidates", JsonValue::number(static_cast<double>(r.total_candidates)));
    JsonValue terms = JsonValue::array();
    for (const auto& t : r.query_terms) terms.push(JsonValue::string(t));
    root.set("query_terms", std::move(terms));

    JsonValue hits = JsonValue::array();
    for (const auto& h : r.hits) {
        JsonValue obj = JsonValue::object();
        obj.set("doc_id", JsonValue::string(h.doc_id));
        obj.set("title", JsonValue::string(h.title));
        obj.set("score", JsonValue::number(h.score));
        obj.set("rank", JsonValue::number(static_cast<double>(h.rank)));
        obj.set("doc_len", JsonValue::number(static_cast<double>(h.doc_len)));
        JsonValue breakdown = JsonValue::object();
        for (const auto& f : h.breakdown) {
            const double v = f.is_rank ? static_cast<double>(static_cast<long long>(f.value)) : f.value;
            breakdown.set(f.name, JsonValue::number(v));
        }
        obj.set("score_breakdown", std::move(breakdown));
        JsonValue highlights = JsonValue::array();
        for (const auto& snippet : h.highlights) highlights.push(JsonValue::string(snippet));
        obj.set("highlights", std::move(highlights));
        obj.set("preview", JsonValue::string(h.preview));
        hits.push(std::move(obj));
    }
    root.set("hits", std::move(hits));
    return root;
}

JsonValue render_stats(const IndexStats& s, const IndexConfig& cfg) {
    JsonValue j = JsonValue::object();
    j.set("docs", JsonValue::number(static_cast<double>(s.docs)));
    j.set("terms", JsonValue::number(static_cast<double>(s.terms)));
    j.set("postings", JsonValue::number(static_cast<double>(s.postings)));
    j.set("index_bytes", JsonValue::number(static_cast<double>(s.index_bytes)));
    j.set("postings_bytes", JsonValue::number(static_cast<double>(s.postings_bytes)));
    j.set("term_bytes", JsonValue::number(static_cast<double>(s.term_bytes)));
    j.set("vector_bytes", JsonValue::number(static_cast<double>(s.vector_bytes)));
    j.set("doc_bytes", JsonValue::number(static_cast<double>(s.doc_bytes)));
    j.set("build_ms", JsonValue::number(s.build_ms));
    j.set("avg_doc_len", JsonValue::number(s.avg_doc_len));
    j.set("vector_dim", JsonValue::number(static_cast<double>(s.vector_dim)));
    j.set("queries", JsonValue::number(static_cast<double>(s.queries)));
    j.set("uptime_s", JsonValue::number(s.uptime_s));
    j.set("k1", JsonValue::number(cfg.k1));
    j.set("b", JsonValue::number(cfg.b));
    j.set("rrf_k", JsonValue::number(static_cast<double>(cfg.rrf_k)));
    return j;
}

}  // namespace

HttpServer::HttpServer(SearchIndex& index, ServerOptions opts) : index_(index), opts_(std::move(opts)) {}

HttpServer::~HttpServer() { stop(); }

int HttpServer::worker_count() const {
    if (opts_.threads > 0) return std::max(1, opts_.threads);
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    return static_cast<int>(std::clamp<unsigned>(hw, 4u, 32u));
}

bool HttpServer::prepare(std::string* error) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        *error = "WSAStartup failed";
        return false;
    }
    wsa_ready_ = true;
    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        *error = "socket() failed";
        WSACleanup();
        wsa_ready_ = false;
        return false;
    }
    BOOL yes = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(opts_.port));
    const std::string host = opts_.host.empty() ? "127.0.0.1" : opts_.host;
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        *error = "bind() failed on " + host + ":" + std::to_string(opts_.port) +
                 " (port already in use or permission denied)";
        ::closesocket(s);
        WSACleanup();
        wsa_ready_ = false;
        return false;
    }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        *error = "listen() failed";
        ::closesocket(s);
        WSACleanup();
        wsa_ready_ = false;
        return false;
    }
    listen_socket_.store(static_cast<SocketHandle>(s), std::memory_order_release);
    return true;
}

bool HttpServer::run(std::string* error) {
    if (!prepare(error)) return false;
    const int n = worker_count();
    workers_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) workers_.emplace_back([this] { worker_loop(); });
    std::printf("[minisearch] listening on http://%s:%d  workers=%d\n", opts_.host.c_str(), opts_.port, n);
    std::fflush(stdout);

    accept_loop();

    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_.store(true, std::memory_order_release);
        while (!queue_.empty()) {
            ::closesocket(static_cast<SOCKET>(queue_.front()));
            queue_.pop();
        }
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();

    const SocketHandle ls = listen_socket_.exchange(kInvalidSocket);
    if (ls != kInvalidSocket) ::closesocket(static_cast<SOCKET>(ls));
    if (wsa_ready_) {
        WSACleanup();
        wsa_ready_ = false;
    }
    return true;
}

void HttpServer::stop() {
    stop_.store(true, std::memory_order_release);
    const SocketHandle ls = listen_socket_.exchange(kInvalidSocket);
    if (ls != kInvalidSocket) {
        ::shutdown(static_cast<SOCKET>(ls), SD_BOTH);
        ::closesocket(static_cast<SOCKET>(ls));
    }
    cv_.notify_all();
}

void HttpServer::accept_loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        const SOCKET ls = static_cast<SOCKET>(listen_socket_.load(std::memory_order_acquire));
        if (ls == INVALID_SOCKET) break;
        sockaddr_in peer{};
        int peer_len = static_cast<int>(sizeof(peer));
        const SOCKET client = ::accept(ls, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (client == INVALID_SOCKET) {
            if (stop_.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        DWORD timeout = 15000;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        BOOL nodelay = TRUE;
        ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
        submit(static_cast<SocketHandle>(client));
    }
}

void HttpServer::submit(SocketHandle client) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_.load(std::memory_order_acquire)) {
            ::closesocket(static_cast<SOCKET>(client));
            return;
        }
        queue_.push(client);
    }
    cv_.notify_one();
}

void HttpServer::worker_loop() {
    while (true) {
        SocketHandle client = kInvalidSocket;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_.load(std::memory_order_acquire) || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_.load(std::memory_order_acquire)) break;
                continue;
            }
            client = queue_.front();
            queue_.pop();
        }
        handle_connection(client);
    }
}

void HttpServer::handle_connection(SocketHandle client_handle) {
    const SOCKET client = static_cast<SOCKET>(client_handle);
    const auto t0 = steady_clock::now();
    Request req;
    std::string err;
    Response resp;
    if (!read_request(client, req, err)) {
        resp = make_error(400, "bad_request", err);
    } else {
        resp = route(req);
    }
    const std::string out = build_response(resp.status, resp.content_type, resp.body, req.method == "HEAD");
    send_all(client, out);
    ::shutdown(client, SD_SEND);
    ::closesocket(client);
    if (opts_.verbose) {
        const double ms = duration<double, std::milli>(steady_clock::now() - t0).count();
        std::printf("[minisearch] %s %s -> %d (%.2f ms, %zu body bytes)\n", req.method.c_str(), req.path.c_str(),
                    resp.status, ms, resp.body.size());
        std::fflush(stdout);
    }
}

HttpServer::Response HttpServer::route(const Request& req) {
    auto param = [&req](const std::string& name) -> std::string {
        const auto it = req.params.find(name);
        return it == req.params.end() ? std::string() : it->second;
    };
    const bool pretty = param("pretty") == "1" || param("pretty") == "true";

    if (req.method == "OPTIONS") {
        Response r;
        r.status = 204;
        r.content_type = "text/plain; charset=utf-8";
        return r;
    }

    if (req.method == "GET" || req.method == "HEAD") {
        if (req.path == "/" || req.path == "/index.html") {
            Response r;
            r.content_type = "text/html; charset=utf-8";
            r.body = kWebUiHtml;
            return r;
        }
        if (req.path == "/favicon.ico") {
            Response r;
            r.status = 204;
            return r;
        }
        if (req.path == "/api/health") {
            JsonValue j = JsonValue::object();
            j.set("status", JsonValue::string("ok"));
            j.set("version", JsonValue::string(opts_.version));
            j.set("docs", JsonValue::number(static_cast<double>(index_.doc_count())));
            j.set("uptime_s", JsonValue::number(index_.uptime_s()));
            return make_json(200, j, pretty);
        }
        if (req.path == "/api/stats") {
            return make_json(200, render_stats(index_.stats(), index_.config()), pretty);
        }
        if (req.path == "/api/search") {
            const std::string q = param("q");
            if (q.empty()) {
                return make_error(400, "missing_query", "query parameter 'q' is required");
            }
            SearchParams sp;
            sp.query = q;
            sp.k = 10;
            const std::string k = param("k");
            if (!k.empty()) {
                try {
                    sp.k = static_cast<size_t>(std::stoul(k));
                } catch (...) {
                }
            }
            if (sp.k == 0) sp.k = 10;
            if (sp.k > 100) sp.k = 100;
            SearchMode mode = SearchMode::Bm25;
            if (!parse_search_mode(param("mode"), mode)) {
                return make_error(400, "bad_mode", "mode must be one of bm25|vector|hybrid");
            }
            sp.mode = mode;
            const IndexConfig& cfg = index_.config();
            sp.k1 = std::clamp(parse_double(param("k1"), cfg.k1), 0.01, 100.0);
            sp.b = std::clamp(parse_double(param("b"), cfg.b), 0.0, 1.0);
            sp.rrf_k = static_cast<int>(std::clamp(parse_double(param("rrf_k"), static_cast<double>(cfg.rrf_k)), 1.0, 1000.0));
            const SearchResult sr = index_.search(sp);
            return make_json(200, render_search(sr), pretty);
        }
        return make_error(404, "not_found", "no route for " + req.path);
    }

    if (req.method == "POST") {
        if (req.path == "/api/docs" || req.path == "/api/docs/bulk") {
            if (req.body.empty()) {
                return make_error(400, "empty_body", "request body must be a JSON document or array");
            }
            std::string parse_err;
            const auto parsed = JsonValue::parse(req.body, &parse_err);
            if (!parsed) {
                return make_error(400, "bad_json", "invalid JSON body: " + parse_err);
            }
            const std::vector<JsonValue>* docs = nullptr;
            std::vector<JsonValue> single;
            if (parsed->is_arr()) {
                docs = &parsed->arr();
            } else if (parsed->is_obj()) {
                if (const JsonValue* inner = parsed->find("docs"); inner != nullptr && inner->is_arr()) {
                    docs = &inner->arr();
                } else {
                    single.push_back(*parsed);
                    docs = &single;
                }
            } else {
                return make_error(400, "bad_body", "request body must be a JSON object or array");
            }
            const BulkAddResult r = index_.add_bulk(*docs);
            if (!r.error.empty()) {
                return make_error(400, "invalid_document", r.error);
            }
            if (r.inserted == 0 && r.skipped_duplicate > 0 && req.path == "/api/docs") {
                return make_error(409, "duplicate_id", "document id already exists");
            }
            index_.rebuild();
            const IndexStats st = index_.stats();
            JsonValue j = JsonValue::object();
            j.set("ok", JsonValue::boolean(true));
            j.set("inserted", JsonValue::number(static_cast<double>(r.inserted)));
            j.set("skipped_duplicate", JsonValue::number(static_cast<double>(r.skipped_duplicate)));
            j.set("total_docs", JsonValue::number(static_cast<double>(st.docs)));
            j.set("build_ms", JsonValue::number(st.build_ms));
            j.set("terms", JsonValue::number(static_cast<double>(st.terms)));
            return make_json(200, j, pretty);
        }
        if (req.path == "/api/seed") {
            size_t count = 300;
            const std::string cs = param("count");
            if (!cs.empty()) {
                try {
                    count = static_cast<size_t>(std::stoul(cs));
                } catch (...) {
                }
            } else if (!req.body.empty()) {
                std::string parse_err;
                const auto parsed = JsonValue::parse(req.body, &parse_err);
                if (!parsed) {
                    return make_error(400, "bad_json", "invalid JSON body: " + parse_err);
                }
                if (const JsonValue* c = parsed->is_obj() ? parsed->find("count") : nullptr; c != nullptr && c->is_num()) {
                    count = static_cast<size_t>(c->as_num());
                }
            }
            count = std::clamp<size_t>(count, 1, 50000);
            const size_t inserted = index_.seed_demo_docs(count);
            index_.rebuild();
            const IndexStats st = index_.stats();
            JsonValue j = JsonValue::object();
            j.set("ok", JsonValue::boolean(true));
            j.set("inserted", JsonValue::number(static_cast<double>(inserted)));
            j.set("total_docs", JsonValue::number(static_cast<double>(st.docs)));
            j.set("build_ms", JsonValue::number(st.build_ms));
            return make_json(200, j, pretty);
        }
        if (req.path == "/api/rebuild") {
            index_.rebuild();
            const IndexStats st = index_.stats();
            JsonValue j = JsonValue::object();
            j.set("ok", JsonValue::boolean(true));
            j.set("docs", JsonValue::number(static_cast<double>(st.docs)));
            j.set("terms", JsonValue::number(static_cast<double>(st.terms)));
            j.set("postings", JsonValue::number(static_cast<double>(st.postings)));
            j.set("build_ms", JsonValue::number(st.build_ms));
            return make_json(200, j, pretty);
        }
        if (req.path == "/api/reset") {
            index_.reset();
            JsonValue j = JsonValue::object();
            j.set("ok", JsonValue::boolean(true));
            j.set("total_docs", JsonValue::number(0));
            return make_json(200, j, pretty);
        }
        return make_error(404, "not_found", "no route for " + req.path);
    }

    return make_error(405, "method_not_allowed", "method " + req.method + " is not supported");
}

}  // namespace ms
