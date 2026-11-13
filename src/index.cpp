#include "index.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace ms {
namespace {

constexpr size_t kMaxDocsPerRequest = 200000;
constexpr size_t kMaxSearchK = 100;

inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Deterministic feature hashing: one bucket per character and per character
// bigram, sign taken from the hash MSB. This keeps the vector stable across
// runs and machines (unlike std::hash).
inline void add_feature(std::vector<float>& acc, uint64_t h, float w) {
    const size_t dim = acc.size();
    const size_t idx = static_cast<size_t>(h % static_cast<uint64_t>(dim));
    const float sign = ((h >> 63) & 1u) ? -1.0f : 1.0f;
    acc[idx] += sign * w;
}

void hash_text_features(std::string_view text, std::vector<float>& acc) {
    uint32_t prev = 0;
    bool have_prev = false;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        size_t next = 0;
        if (!utf8_decode(text, i, cp, next)) {
            ++i;
            continue;
        }
        i = next;
        const bool keep = (cp < 0x80) ? is_ascii_alnum(static_cast<unsigned char>(cp)) : is_cjk(cp);
        if (!keep) {
            have_prev = false;
            continue;
        }
        add_feature(acc, mix64(cp), 0.5f);
        if (have_prev) add_feature(acc, mix64((static_cast<uint64_t>(prev) << 32) | cp), 1.0f);
        prev = cp;
        have_prev = true;
    }
}

inline double round6(double v) { return std::round(v * 1e6) / 1e6; }

struct RankCmp {
    bool operator()(const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) const {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    }
};

std::vector<std::pair<float, uint32_t>> top_k_from(const std::vector<uint32_t>& cand,
                                                   const std::vector<float>& scores, size_t limit) {
    std::vector<std::pair<float, uint32_t>> items;
    items.reserve(cand.size());
    for (uint32_t d : cand) {
        const float s = scores[d];
        if (s > 0.0f && std::isfinite(s)) items.emplace_back(s, d);
    }
    if (items.size() > limit) {
        std::nth_element(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(limit), items.end(), RankCmp{});
        items.resize(limit);
    }
    std::sort(items.begin(), items.end(), RankCmp{});
    return items;
}

std::vector<std::pair<float, uint32_t>> top_k_all(const std::vector<float>& scores, size_t limit) {
    std::vector<std::pair<float, uint32_t>> items;
    items.reserve(scores.size());
    for (size_t d = 0; d < scores.size(); ++d) {
        const float s = scores[d];
        if (s > 0.0f && std::isfinite(s)) items.emplace_back(s, static_cast<uint32_t>(d));
    }
    if (items.size() > limit) {
        std::nth_element(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(limit), items.end(), RankCmp{});
        items.resize(limit);
    }
    std::sort(items.begin(), items.end(), RankCmp{});
    return items;
}

}  // namespace

const char* search_mode_name(SearchMode mode) {
    switch (mode) {
        case SearchMode::Bm25: return "bm25";
        case SearchMode::Vector: return "vector";
        case SearchMode::Hybrid: return "hybrid";
    }
    return "bm25";
}

bool parse_search_mode(const std::string& s, SearchMode& out) {
    if (s.empty() || s == "bm25") {
        out = SearchMode::Bm25;
        return true;
    }
    if (s == "vector") {
        out = SearchMode::Vector;
        return true;
    }
    if (s == "hybrid") {
        out = SearchMode::Hybrid;
        return true;
    }
    return false;
}

SearchIndex::SearchIndex(IndexConfig cfg)
    : cfg_(cfg), started_(std::chrono::steady_clock::now()) {}

double SearchIndex::uptime_s() const {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - started_).count();
}

BulkAddResult SearchIndex::add_bulk(const std::vector<JsonValue>& docs) {
    BulkAddResult res;
    if (docs.size() > kMaxDocsPerRequest) {
        res.error = "too many documents in one request (max 200000)";
        return res;
    }
    struct Pending {
        std::string id;
        std::string title;
        std::string text;
    };
    std::vector<Pending> pending;
    pending.reserve(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) {
        const JsonValue& v = docs[i];
        if (!v.is_obj()) {
            res.error = "document at index " + std::to_string(i) + " is not a JSON object";
            return res;
        }
        const JsonValue* text = v.find("text");
        if (text == nullptr || !text->is_str()) {
            res.error = "document at index " + std::to_string(i) + " is missing string field 'text'";
            return res;
        }
        Pending p;
        p.text = text->str();
        if (const JsonValue* id = v.find("id")) {
            if (!id->is_null()) {
                if (!id->is_str() || id->str().empty()) {
                    res.error = "document at index " + std::to_string(i) + " has invalid 'id' (must be non-empty string)";
                    return res;
                }
                p.id = id->str();
            }
        }
        if (const JsonValue* title = v.find("title")) {
            if (!title->is_null()) {
                if (!title->is_str()) {
                    res.error = "document at index " + std::to_string(i) + " has invalid 'title' (must be string)";
                    return res;
                }
                p.title = title->str();
            }
        }
        pending.push_back(std::move(p));
    }

    std::unique_lock lk(mu_);
    for (auto& p : pending) {
        if (p.id.empty()) {
            do {
                p.id = "auto-" + std::to_string(++auto_id_seq_);
            } while (id_to_doc_.count(p.id) != 0);
        }
        if (id_to_doc_.count(p.id) != 0) {
            ++res.skipped_duplicate;
            continue;
        }
        const uint32_t d = static_cast<uint32_t>(docs_.size());
        DocRecord rec;
        rec.id = p.id;
        rec.title = std::move(p.title);
        rec.text = std::move(p.text);
        docs_.push_back(std::move(rec));
        id_to_doc_.emplace(p.id, d);
        ++res.inserted;
    }
    if (res.inserted > 0) {
        dirty_.store(true, std::memory_order_release);
        doc_count_.store(docs_.size(), std::memory_order_relaxed);
    }
    res.total_docs = docs_.size();
    return res;
}

BulkAddResult SearchIndex::add_document(const std::string& id, const std::string& title,
                                        const std::string& text) {
    JsonValue doc = JsonValue::object();
    if (!id.empty()) doc.set("id", JsonValue::string(id));
    doc.set("title", JsonValue::string(title));
    doc.set("text", JsonValue::string(text));
    std::vector<JsonValue> arr;
    arr.push_back(std::move(doc));
    BulkAddResult r = add_bulk(arr);
    if (r.error.empty() && r.inserted == 0 && r.skipped_duplicate > 0) {
        r.error = "duplicate document id: " + id;
    }
    return r;
}

size_t SearchIndex::seed_demo_docs(size_t count) {
    const std::vector<SeedDoc> seed = generate_seed_docs(count);
    std::vector<JsonValue> batch;
    batch.reserve(seed.size());
    for (const auto& d : seed) {
        JsonValue obj = JsonValue::object();
        obj.set("id", JsonValue::string(d.id));
        obj.set("title", JsonValue::string(d.title));
        obj.set("text", JsonValue::string(d.text));
        batch.push_back(std::move(obj));
    }
    const BulkAddResult r = add_bulk(batch);
    return r.inserted;
}

void SearchIndex::reset() {
    std::unique_lock lk(mu_);
    docs_.clear();
    docs_.shrink_to_fit();
    id_to_doc_.clear();
    terms_.clear();
    postings_.clear();
    postings_.shrink_to_fit();
    vectors_.clear();
    vectors_.shrink_to_fit();
    total_tokens_ = 0;
    posting_count_ = 0;
    auto_id_seq_ = 0;
    build_ms_ = 0.0;
    dirty_.store(true, std::memory_order_release);
    doc_count_.store(0, std::memory_order_relaxed);
}

void SearchIndex::rebuild() {
    std::unique_lock lk(mu_);
    rebuild_locked();
}

void SearchIndex::ensure_built() {
    if (!dirty_.load(std::memory_order_acquire)) return;
    std::unique_lock lk(mu_);
    if (!dirty_.load(std::memory_order_acquire)) return;
    rebuild_locked();
}

void SearchIndex::rebuild_locked() {
    const auto t0 = std::chrono::steady_clock::now();
    terms_.clear();
    terms_.reserve(0);
    postings_.clear();
    posting_count_ = 0;
    total_tokens_ = 0;

    struct TempPostings {
        uint32_t last_doc = 0;
        uint32_t df = 0;
        std::vector<uint8_t> bytes;
    };
    std::unordered_map<std::string, TempPostings> tmp;
    tmp.reserve(docs_.size() * 24 + 64);

    const Dictionary& dict = Dictionary::builtin();
    std::string combined;
    std::unordered_map<std::string, uint32_t> tf;

    for (uint32_t d = 0; d < docs_.size(); ++d) {
        combined.clear();
        combined += docs_[d].title;
        combined += ' ';
        combined += docs_[d].text;
        const Analysis an = analyze_for_index(combined, dict);
        docs_[d].len_tokens = static_cast<uint32_t>(an.terms.size());
        total_tokens_ += an.terms.size();

        tf.clear();
        tf.reserve(an.terms.size() * 2 + 1);
        for (const auto& t : an.terms) ++tf[t];
        for (const auto& kv : tf) {
            TempPostings& e = tmp[kv.first];
            vbyte_append(e.bytes, d - e.last_doc);
            e.last_doc = d;
            vbyte_append(e.bytes, kv.second);
            ++e.df;
            ++posting_count_;
        }
    }

    size_t arena_bytes = 0;
    for (const auto& kv : tmp) arena_bytes += kv.second.bytes.size();
    postings_.reserve(arena_bytes);
    terms_.reserve(tmp.size());
    for (auto& kv : tmp) {
        TermEntry e;
        e.df = kv.second.df;
        e.offset = postings_.size();
        e.bytes = static_cast<uint32_t>(kv.second.bytes.size());
        postings_.insert(postings_.end(), kv.second.bytes.begin(), kv.second.bytes.end());
        terms_.emplace(kv.first, e);
    }

    // Dense hashing vectors: deterministic, L2 normalized, brute-force cosine.
    const size_t dim = cfg_.vector_dim;
    vectors_.assign(docs_.size() * dim, 0.0f);
    std::vector<float> vec(dim, 0.0f);
    std::string lowered;
    for (uint32_t d = 0; d < docs_.size(); ++d) {
        lowered.clear();
        lowered += docs_[d].title;
        lowered += ' ';
        lowered += docs_[d].text;
        lowered = to_lower(lowered);
        build_vector_locked(lowered, vec);
        std::copy(vec.begin(), vec.end(), vectors_.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(d) * dim));
    }

    const auto t1 = std::chrono::steady_clock::now();
    build_ms_ = round6(std::chrono::duration<double, std::milli>(t1 - t0).count());
    doc_count_.store(docs_.size(), std::memory_order_relaxed);
    dirty_.store(false, std::memory_order_release);
}

void SearchIndex::build_vector_locked(std::string_view text, std::vector<float>& out) const {
    out.assign(cfg_.vector_dim, 0.0f);
    hash_text_features(text, out);
    double n2 = 0.0;
    for (float v : out) n2 += static_cast<double>(v) * v;
    if (n2 > 0.0) {
        const float inv = static_cast<float>(1.0 / std::sqrt(n2));
        for (float& v : out) v *= inv;
    }
}

std::vector<std::pair<float, uint32_t>> SearchIndex::rank_bm25_locked(
    const std::vector<std::string>& qterms, size_t limit, double k1, double b, Bm25Detail* detail) const {
    const size_t n = docs_.size();
    Bm25Detail local;
    Bm25Detail& det = detail != nullptr ? *detail : local;
    det.score.assign(n, 0.0f);
    det.idf_sum.assign(n, 0.0f);
    det.matched.assign(n, 0);
    det.touched_count = 0;
    if (n == 0 || qterms.empty()) return {};

    const double avgdl = total_tokens_ > 0 ? static_cast<double>(total_tokens_) / static_cast<double>(n) : 1.0;
    std::vector<uint32_t> touched;
    touched.reserve(n);

    for (const auto& term : qterms) {
        const auto it = terms_.find(term);
        if (it == terms_.end()) continue;
        const TermEntry& e = it->second;
        if (e.df == 0) continue;
        const double idf = std::log(1.0 + (static_cast<double>(n) - e.df + 0.5) / (e.df + 0.5));
        const uint8_t* data = postings_.data() + e.offset;
        for_each_posting(data, e.bytes, e.df, [&](uint32_t doc, uint32_t tf) {
            const double dl = docs_[doc].len_tokens;
            const double norm = k1 * (1.0 - b + b * dl / avgdl);
            const double s = idf * static_cast<double>(tf) * (k1 + 1.0) / (static_cast<double>(tf) + norm);
            if (det.score[doc] == 0.0f) touched.push_back(doc);
            det.score[doc] += static_cast<float>(s);
            det.idf_sum[doc] += static_cast<float>(idf);
            if (det.matched[doc] < 255) det.matched[doc] += 1;
        });
    }
    det.touched_count = touched.size();
    return top_k_from(touched, det.score, limit);
}

std::vector<std::pair<float, uint32_t>> SearchIndex::rank_vector_locked(const std::string& query, size_t limit,
                                                                       std::vector<float>& cos_scores) const {
    const size_t n = docs_.size();
    const size_t dim = cfg_.vector_dim;
    cos_scores.assign(n, 0.0f);
    if (n == 0 || dim == 0) return {};

    std::vector<float> qacc(dim, 0.0f);
    hash_text_features(to_lower(query), qacc);
    double n2 = 0.0;
    for (float v : qacc) n2 += static_cast<double>(v) * v;
    if (n2 <= 0.0) return {};
    const float inv = static_cast<float>(1.0 / std::sqrt(n2));
    std::vector<std::pair<uint32_t, float>> qfeat;
    qfeat.reserve(dim);
    for (size_t i = 0; i < dim; ++i) {
        if (qacc[i] != 0.0f) qfeat.emplace_back(static_cast<uint32_t>(i), qacc[i] * inv);
    }

    for (size_t d = 0; d < n; ++d) {
        const float* v = vectors_.data() + d * dim;
        float s = 0.0f;
        for (const auto& f : qfeat) s += f.second * v[f.first];
        cos_scores[d] = s;
    }
    return top_k_all(cos_scores, limit);
}

SearchResult SearchIndex::search(const SearchParams& params) {
    SearchResult result;
    result.mode = search_mode_name(params.mode);
    result.query = params.query;
    result.k = params.k;
    ensure_built();

    std::shared_lock lk(mu_);
    const auto t0 = std::chrono::steady_clock::now();
    query_count_.fetch_add(1, std::memory_order_relaxed);

    const size_t n = docs_.size();
    const size_t k = std::min(params.k == 0 ? size_t{10} : params.k, kMaxSearchK);
    const Analysis an = analyze_for_query(params.query, Dictionary::builtin());
    result.query_terms = an.terms;

    std::vector<std::string> surfaces = an.surfaces;
    if (surfaces.empty()) surfaces = an.fallback;

    if (n == 0 || (an.terms.empty() && an.fallback.empty())) {
        const auto t1 = std::chrono::steady_clock::now();
        result.took_ms = round6(std::chrono::duration<double, std::milli>(t1 - t0).count());
        return result;
    }

    auto make_hit = [&](uint32_t d, double score) {
        Hit h;
        h.doc_id = docs_[d].id;
        h.title = docs_[d].title;
        h.doc_len = docs_[d].len_tokens;
        h.preview = utf8_prefix(docs_[d].text, 140);
        h.highlights = make_highlights(docs_[d].text, surfaces, cfg_.max_highlights);
        if (h.highlights.empty()) {
            // Title-only matches still deserve a marked fragment.
            h.highlights = make_highlights(docs_[d].title, surfaces, 1);
        }
        h.score = round6(score);
        h.rank = 0;
        return h;
    };

    if (params.mode == SearchMode::Bm25) {
        Bm25Detail det;
        std::vector<std::string> scored_terms = an.terms;
        auto ranked = rank_bm25_locked(scored_terms, k, params.k1, params.b, &det);
        if (ranked.empty() && !an.fallback.empty() && an.fallback != scored_terms) {
            // Nothing matched on dictionary words/bigrams: retry with single chars.
            scored_terms = an.fallback;
            surfaces = an.fallback;
            ranked = rank_bm25_locked(scored_terms, k, params.k1, params.b, &det);
            result.query_terms = scored_terms;
        }
        size_t matched = 0;
        for (const auto& t : scored_terms) {
            if (terms_.find(t) != terms_.end()) ++matched;
        }
        result.matched_terms = matched;
        result.total_candidates = det.touched_count;
        const double avgdl = n > 0 && total_tokens_ > 0
                                 ? static_cast<double>(total_tokens_) / static_cast<double>(n)
                                 : 0.0;
        result.hits.reserve(ranked.size());
        int rank = 0;
        for (const auto& item : ranked) {
            const uint32_t d = item.second;
            Hit h = make_hit(d, item.first);
            h.rank = ++rank;
            h.breakdown.push_back({"bm25", round6(item.first), false});
            h.breakdown.push_back({"idf_sum", round6(det.idf_sum[d]), false});
            h.breakdown.push_back({"matched_terms", static_cast<double>(det.matched[d]), false});
            h.breakdown.push_back({"doc_len", static_cast<double>(docs_[d].len_tokens), false});
            h.breakdown.push_back({"avg_doc_len", round6(avgdl), false});
            result.hits.push_back(std::move(h));
        }
    } else if (params.mode == SearchMode::Vector) {
        std::vector<float> cos_scores;
        const auto ranked = rank_vector_locked(params.query, k, cos_scores);
        size_t non_zero = 0;
        for (float s : cos_scores) {
            if (s > 0.0f) ++non_zero;
        }
        result.total_candidates = non_zero;
        result.hits.reserve(ranked.size());
        int rank = 0;
        for (const auto& item : ranked) {
            const uint32_t d = item.second;
            Hit h = make_hit(d, item.first);
            h.rank = ++rank;
            h.breakdown.push_back({"cosine", round6(item.first), false});
            h.breakdown.push_back({"doc_len", static_cast<double>(docs_[d].len_tokens), false});
            h.breakdown.push_back({"vector_dim", static_cast<double>(cfg_.vector_dim), false});
            result.hits.push_back(std::move(h));
        }
    } else {
        const size_t pool = std::max<size_t>(k * 5, 50);
        Bm25Detail det;
        const auto bm_list = rank_bm25_locked(an.terms, pool, params.k1, params.b, &det);
        std::vector<float> cos_scores;
        const auto vec_list = rank_vector_locked(params.query, pool, cos_scores);

        std::unordered_map<uint32_t, double> rrf;
        rrf.reserve(bm_list.size() + vec_list.size());
        std::vector<uint32_t> bm_rank(n, 0);
        std::vector<uint32_t> vec_rank(n, 0);
        const double rrf_k = static_cast<double>(params.rrf_k > 0 ? params.rrf_k : 60);
        for (size_t i = 0; i < bm_list.size(); ++i) {
            const uint32_t d = bm_list[i].second;
            bm_rank[d] = static_cast<uint32_t>(i + 1);
            rrf[d] += 1.0 / (rrf_k + static_cast<double>(i + 1));
        }
        for (size_t i = 0; i < vec_list.size(); ++i) {
            const uint32_t d = vec_list[i].second;
            vec_rank[d] = static_cast<uint32_t>(i + 1);
            rrf[d] += 1.0 / (rrf_k + static_cast<double>(i + 1));
        }
        std::vector<std::pair<double, uint32_t>> items;
        items.reserve(rrf.size());
        for (const auto& kv : rrf) items.emplace_back(kv.second, kv.first);
        const size_t take = std::min(k, items.size());
        std::partial_sort(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(take), items.end(),
                          [](const std::pair<double, uint32_t>& a, const std::pair<double, uint32_t>& b) {
                              if (a.first != b.first) return a.first > b.first;
                              return a.second < b.second;
                          });
        items.resize(take);

        size_t matched = 0;
        for (const auto& t : an.terms) {
            if (terms_.find(t) != terms_.end()) ++matched;
        }
        result.matched_terms = matched;
        result.total_candidates = rrf.size();
        result.hits.reserve(items.size());
        int rank = 0;
        for (const auto& item : items) {
            const uint32_t d = item.second;
            Hit h = make_hit(d, item.first);
            h.rank = ++rank;
            h.breakdown.push_back({"rrf", round6(item.first), false});
            h.breakdown.push_back({"bm25", round6(det.score[d]), false});
            h.breakdown.push_back({"cosine", round6(cos_scores[d]), false});
            h.breakdown.push_back({"bm25_rank", static_cast<double>(bm_rank[d]), true});
            h.breakdown.push_back({"vector_rank", static_cast<double>(vec_rank[d]), true});
            result.hits.push_back(std::move(h));
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    result.took_ms = round6(std::chrono::duration<double, std::milli>(t1 - t0).count());
    return result;
}

IndexStats SearchIndex::stats() {
    ensure_built();
    std::shared_lock lk(mu_);
    IndexStats s;
    s.docs = docs_.size();
    s.terms = terms_.size();
    s.postings = posting_count_;
    s.postings_bytes = postings_.size();
    size_t term_bytes = 0;
    for (const auto& kv : terms_) term_bytes += kv.first.size() + sizeof(TermEntry) + 32;
    s.term_bytes = term_bytes;
    s.vector_bytes = vectors_.size() * sizeof(float);
    size_t doc_bytes = 0;
    for (const auto& d : docs_) {
        doc_bytes += d.id.size() + d.title.size() + d.text.size() + sizeof(DocRecord) + 32;
    }
    s.doc_bytes = doc_bytes;
    s.index_bytes = s.postings_bytes + s.term_bytes + s.vector_bytes + s.doc_bytes;
    s.build_ms = build_ms_;
    s.avg_doc_len = docs_.empty() ? 0.0 : static_cast<double>(total_tokens_) / static_cast<double>(docs_.size());
    s.vector_dim = cfg_.vector_dim;
    s.queries = query_count_.load(std::memory_order_relaxed);
    s.uptime_s = uptime_s();
    return s;
}

}  // namespace ms
