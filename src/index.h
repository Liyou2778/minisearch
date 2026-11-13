// SearchIndex: documents, compressed inverted index, BM25, hashing-vector
// retrieval, RRF hybrid fusion, highlights and stats. Thread-safe: concurrent
// readers (shared lock), serialized writers (unique lock).
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.h"
#include "postings.h"
#include "text.h"

namespace ms {

enum class SearchMode { Bm25, Vector, Hybrid };

const char* search_mode_name(SearchMode mode);
bool parse_search_mode(const std::string& s, SearchMode& out);

struct IndexConfig {
    double k1 = 1.2;          // BM25 term-frequency saturation
    double b = 0.75;          // BM25 length normalization
    int rrf_k = 60;           // RRF smoothing constant
    uint32_t vector_dim = 512;
    size_t max_highlights = 3;
};

struct SearchParams {
    std::string query;
    size_t k = 10;
    SearchMode mode = SearchMode::Bm25;
    double k1 = 1.2;
    double b = 0.75;
    int rrf_k = 60;
};

struct ScoreField {
    std::string name;
    double value = 0.0;
    bool is_rank = false;
};

struct Hit {
    std::string doc_id;
    std::string title;
    std::string preview;
    double score = 0.0;
    int rank = 0;
    uint32_t doc_len = 0;
    std::vector<std::string> highlights;
    std::vector<ScoreField> breakdown;
};

struct SearchResult {
    std::string mode;
    std::string query;
    size_t k = 0;
    double took_ms = 0.0;
    size_t total_candidates = 0;
    size_t matched_terms = 0;
    std::vector<std::string> query_terms;
    std::vector<Hit> hits;
};

struct IndexStats {
    size_t docs = 0;
    size_t terms = 0;
    size_t postings = 0;
    size_t index_bytes = 0;
    size_t postings_bytes = 0;
    size_t term_bytes = 0;
    size_t vector_bytes = 0;
    size_t doc_bytes = 0;
    double build_ms = 0.0;
    double avg_doc_len = 0.0;
    uint32_t vector_dim = 0;
    uint64_t queries = 0;
    double uptime_s = 0.0;
};

struct BulkAddResult {
    size_t inserted = 0;
    size_t skipped_duplicate = 0;
    size_t total_docs = 0;
    std::string error;  // non-empty => nothing was inserted
};

class SearchIndex {
public:
    explicit SearchIndex(IndexConfig cfg = {});

    BulkAddResult add_bulk(const std::vector<JsonValue>& docs);
    BulkAddResult add_document(const std::string& id, const std::string& title, const std::string& text);
    void reset();
    void rebuild();
    void ensure_built();
    size_t seed_demo_docs(size_t count);

    SearchResult search(const SearchParams& params);
    IndexStats stats();

    const IndexConfig& config() const { return cfg_; }
    size_t doc_count() const { return doc_count_.load(std::memory_order_relaxed); }
    double uptime_s() const;

private:
    struct DocRecord {
        std::string id;
        std::string title;
        std::string text;
        uint32_t len_tokens = 0;
    };

    struct TermEntry {
        uint32_t df = 0;
        size_t offset = 0;
        uint32_t bytes = 0;
    };

    struct Bm25Detail {
        std::vector<float> score;
        std::vector<float> idf_sum;
        std::vector<uint8_t> matched;
        size_t touched_count = 0;
    };

    void rebuild_locked();
    std::vector<std::pair<float, uint32_t>> rank_bm25_locked(const std::vector<std::string>& qterms, size_t limit,
                                                             double k1, double b, Bm25Detail* detail) const;
    std::vector<std::pair<float, uint32_t>> rank_vector_locked(const std::string& query, size_t limit,
                                                               std::vector<float>& cos_scores) const;
    void build_vector_locked(std::string_view text, std::vector<float>& out) const;

    IndexConfig cfg_;
    mutable std::shared_mutex mu_;
    std::vector<DocRecord> docs_;
    std::unordered_map<std::string, uint32_t> id_to_doc_;
    std::unordered_map<std::string, TermEntry> terms_;
    std::vector<uint8_t> postings_;
    std::vector<float> vectors_;  // docs_.size() * cfg_.vector_dim, L2 normalized
    uint64_t total_tokens_ = 0;
    size_t posting_count_ = 0;
    uint64_t auto_id_seq_ = 0;
    double build_ms_ = 0.0;
    std::atomic<bool> dirty_{true};
    std::atomic<size_t> doc_count_{0};
    std::atomic<uint64_t> query_count_{0};
    std::chrono::steady_clock::time_point started_;
};

// Deterministic Chinese demo document generator (used by POST /api/seed).
struct SeedDoc {
    std::string id;
    std::string title;
    std::string text;
};
std::vector<SeedDoc> generate_seed_docs(size_t count, uint64_t seed = 20240418);

}  // namespace ms
