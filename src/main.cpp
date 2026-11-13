// minisearch entry point: configure, optional preload, then serve.
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "http.h"
#include "index.h"
#include "json.h"

namespace {

struct Args {
    std::string host = "127.0.0.1";
    int port = 8204;
    int threads = 0;
    bool verbose = false;
    bool help = false;
    std::string corpus;
    size_t seed_docs = 0;
    double k1 = 1.2;
    double b = 0.75;
    int rrf_k = 60;
    uint32_t dim = 512;
};

std::atomic<bool> g_quit{false};

BOOL WINAPI console_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_quit.store(true, std::memory_order_relaxed);
            return TRUE;
        default:
            return FALSE;
    }
}

void print_usage() {
    std::printf(
        "minisearch - C++20 full-text + vector search engine (MVP)\n"
        "\n"
        "Usage: minisearch [options]\n"
        "\n"
        "  --port N         listen port (default 8204)\n"
        "  --host H         bind host (default 127.0.0.1; use 0.0.0.0 for LAN)\n"
        "  --threads N      worker threads (default: hardware concurrency, clamped 4..32)\n"
        "  --k1 X           BM25 k1 (default 1.2)\n"
        "  --b X            BM25 b  (default 0.75)\n"
        "  --rrf-k N        RRF constant (default 60)\n"
        "  --dim N          hashing vector dimension (default 512)\n"
        "  --corpus FILE    JSONL/JSON array of {id,title,text} loaded at startup\n"
        "  --seed N         generate N synthetic Chinese docs at startup\n"
        "  --verbose        log every request to stdout\n"
        "  --help           show this help\n");
}

bool parse_int(const char* s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_size(const char* s, size_t& out) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<size_t>(v);
    return true;
}

bool parse_double(const char* s, double& out) {
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            a.help = true;
        } else if (arg == "--verbose" || arg == "-v") {
            a.verbose = true;
        } else if (arg == "--port") {
            const char* v = next("--port");
            if (v == nullptr || !parse_int(v, a.port) || a.port < 1 || a.port > 65535) {
                std::fprintf(stderr, "invalid --port\n");
                return false;
            }
        } else if (arg == "--host") {
            const char* v = next("--host");
            if (v == nullptr) return false;
            a.host = v;
        } else if (arg == "--threads") {
            const char* v = next("--threads");
            if (v == nullptr || !parse_int(v, a.threads) || a.threads < 1 || a.threads > 256) {
                std::fprintf(stderr, "invalid --threads\n");
                return false;
            }
        } else if (arg == "--k1") {
            const char* v = next("--k1");
            if (v == nullptr || !parse_double(v, a.k1) || a.k1 <= 0.0) {
                std::fprintf(stderr, "invalid --k1\n");
                return false;
            }
        } else if (arg == "--b") {
            const char* v = next("--b");
            if (v == nullptr || !parse_double(v, a.b) || a.b < 0.0 || a.b > 1.0) {
                std::fprintf(stderr, "invalid --b\n");
                return false;
            }
        } else if (arg == "--rrf-k") {
            const char* v = next("--rrf-k");
            if (v == nullptr || !parse_int(v, a.rrf_k) || a.rrf_k < 1) {
                std::fprintf(stderr, "invalid --rrf-k\n");
                return false;
            }
        } else if (arg == "--dim") {
            const char* v = next("--dim");
            int dim = 0;
            if (v == nullptr || !parse_int(v, dim) || dim < 8 || dim > 8192) {
                std::fprintf(stderr, "invalid --dim (8..8192)\n");
                return false;
            }
            a.dim = static_cast<uint32_t>(dim);
        } else if (arg == "--corpus") {
            const char* v = next("--corpus");
            if (v == nullptr) return false;
            a.corpus = v;
        } else if (arg == "--seed") {
            const char* v = next("--seed");
            if (v == nullptr || !parse_size(v, a.seed_docs) || a.seed_docs > 200000) {
                std::fprintf(stderr, "invalid --seed\n");
                return false;
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s (try --help)\n", arg.c_str());
            return false;
        }
    }
    return true;
}

bool read_file(const std::string& path, std::string& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open file: " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0 || size > 200LL * 1024 * 1024) {
        err = "corpus file too large or unreadable: " + path;
        return false;
    }
    out.resize(static_cast<size_t>(size));
    if (size > 0) in.read(out.data(), static_cast<std::streamsize>(size));
    return true;
}

std::vector<ms::JsonValue> load_corpus(const std::string& path, std::string& err) {
    std::vector<ms::JsonValue> docs;
    std::string text;
    if (!read_file(path, text, err)) return docs;
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    if (first < text.size() && text[first] == '[') {
        const auto parsed = ms::JsonValue::parse(text, &err);
        if (!parsed || !parsed->is_arr()) {
            if (err.empty()) err = "corpus JSON array expected";
            return docs;
        }
        docs = parsed->arr();
        return docs;
    }
    // JSONL: one JSON object per line.
    size_t line_no = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++line_no;
        if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) continue;
        std::string line_err;
        const auto parsed = ms::JsonValue::parse(line, &line_err);
        if (!parsed || !parsed->is_obj()) {
            err = "invalid JSONL at line " + std::to_string(line_no) + ": " + line_err;
            docs.clear();
            return docs;
        }
        docs.push_back(*parsed);
    }
    return docs;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) return 2;
    if (args.help) {
        print_usage();
        return 0;
    }

    SetConsoleCtrlHandler(console_handler, TRUE);

    ms::IndexConfig cfg;
    cfg.k1 = args.k1;
    cfg.b = args.b;
    cfg.rrf_k = args.rrf_k;
    cfg.vector_dim = args.dim;
    ms::SearchIndex index(cfg);

    std::printf("minisearch 1.0.0\n");
    std::printf("  bm25   : k1=%.2f b=%.2f\n", cfg.k1, cfg.b);
    std::printf("  vector : dim=%u (deterministic hashing, cosine)\n", cfg.vector_dim);
    std::printf("  hybrid : RRF k=%d\n", cfg.rrf_k);

    if (!args.corpus.empty()) {
        std::string err;
        const std::vector<ms::JsonValue> docs = load_corpus(args.corpus, err);
        if (!err.empty()) {
            std::fprintf(stderr, "corpus error: %s\n", err.c_str());
            return 2;
        }
        const ms::BulkAddResult r = index.add_bulk(docs);
        if (!r.error.empty()) {
            std::fprintf(stderr, "corpus error: %s\n", r.error.c_str());
            return 2;
        }
        std::printf("  corpus : loaded %zu docs from %s\n", r.inserted, args.corpus.c_str());
    }
    if (args.seed_docs > 0) {
        const size_t inserted = index.seed_demo_docs(args.seed_docs);
        std::printf("  seed   : generated %zu docs\n", inserted);
    }

    if (index.doc_count() > 0) {
        index.rebuild();
        const ms::IndexStats st = index.stats();
        std::printf("  index  : docs=%zu terms=%zu postings=%zu bytes=%.2f MiB build=%.1f ms avg_len=%.1f\n",
                    st.docs, st.terms, st.postings, static_cast<double>(st.index_bytes) / 1048576.0, st.build_ms,
                    st.avg_doc_len);
    }

    ms::ServerOptions opts;
    opts.host = args.host;
    opts.port = args.port;
    opts.threads = args.threads;
    opts.verbose = args.verbose;

    ms::HttpServer server(index, opts);
    std::printf("  console: http://%s:%d/\n", args.host.c_str(), args.port);
    std::printf("  api    : http://%s:%d/api/search?q=<query>&k=10&mode=bm25|vector|hybrid\n",
                args.host.c_str(), args.port);
    std::printf("  stop   : Ctrl+C\n");
    std::fflush(stdout);

    std::thread watcher([&server] {
        while (!g_quit.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
    });

    std::string err;
    const bool ok = server.run(&err);
    g_quit.store(true, std::memory_order_relaxed);
    if (watcher.joinable()) watcher.join();
    if (!ok) {
        std::fprintf(stderr, "server error: %s\n", err.c_str());
        return 1;
    }
    std::printf("[minisearch] stopped\n");
    return 0;
}
