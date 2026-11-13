// Self tests for JSON, VByte postings, tokenizer, highlights and the index.
// Build target: selftest (not part of the server binary).
#include <cstdio>
#include <string>
#include <vector>

#include "index.h"
#include "json.h"
#include "postings.h"
#include "text.h"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("ok  : %s\n", what);
    }
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

ms::JsonValue make_doc(const std::string& id, const std::string& title, const std::string& text) {
    ms::JsonValue j = ms::JsonValue::object();
    j.set("id", ms::JsonValue::string(id));
    j.set("title", ms::JsonValue::string(title));
    j.set("text", ms::JsonValue::string(text));
    return j;
}

void test_json() {
    const std::string src =
        "{\"a\":1,\"b\":[true,null,\"x\\u4e2d\"],\"c\":-2.5e-3,\"nested\":{\"k\":\"v\"},"
        "\"esc\":\"quote\\\" slash\\\\ newline\\n done\"}";
    std::string err;
    const auto parsed = ms::JsonValue::parse(src, &err);
    check(parsed.has_value(), "json: parses valid document");
    if (!parsed) {
        std::printf("     parse error: %s\n", err.c_str());
        return;
    }
    check(parsed->find("a") != nullptr && parsed->find("a")->as_num() == 1.0, "json: integer field");
    const ms::JsonValue* b = parsed->find("b");
    check(b != nullptr && b->is_arr() && b->arr().size() == 3, "json: array field");
    check(b->arr()[0].as_bool() == true && b->arr()[1].is_null(), "json: bool/null elements");
    check(b->arr()[2].str() == "x\xE4\xB8\xAD", "json: \\u4e2d decoded to UTF-8 中 (after 'x')");
    check(parsed->find("c") != nullptr && parsed->find("c")->as_num() < 0.0, "json: negative exponent number");
    const ms::JsonValue* nested = parsed->find("nested");
    check(nested != nullptr && nested->find("k") != nullptr && nested->find("k")->str() == "v",
          "json: nested object");
    const ms::JsonValue* esc = parsed->find("esc");
    check(esc != nullptr && esc->str().find('\n') != std::string::npos &&
              esc->str().find('"') != std::string::npos && esc->str().find('\\') != std::string::npos,
          "json: escape sequences");

    const std::string dumped = parsed->dump(false);
    std::string err2;
    const auto round = ms::JsonValue::parse(dumped, &err2);
    check(round.has_value(), "json: dump round-trips through parser");
    check(dumped.find("\\\"") != std::string::npos, "json: dump escapes quotes");
    const std::string pretty = parsed->dump(true);
    check(pretty.find('\n') != std::string::npos, "json: pretty output has newlines");

    const auto bad = ms::JsonValue::parse("{\"a\": }", nullptr);
    check(!bad.has_value(), "json: rejects malformed input");
    check(!ms::JsonValue::parse("[1,2", nullptr).has_value(), "json: rejects unterminated array");
    check(!ms::JsonValue::parse("{} trailing", nullptr).has_value(), "json: rejects trailing garbage");
}

void test_vbyte() {
    const std::vector<uint32_t> values = {0, 1, 127, 128, 300, 16384, 1000000, 4294967295u};
    std::vector<uint8_t> buf;
    for (uint32_t v : values) ms::vbyte_append(buf, v);
    const uint8_t* p = buf.data();
    const uint8_t* end = buf.data() + buf.size();
    bool ok = true;
    for (uint32_t expected : values) {
        uint32_t got = 0;
        if (!ms::vbyte_next(p, end, got) || got != expected) ok = false;
    }
    check(ok, "vbyte: encode/decode round-trip incl. large values");

    // Posting list: (doc 1, tf 2), (doc 4, tf 1), (doc 9, tf 5) as deltas.
    std::vector<uint8_t> plist;
    ms::vbyte_append(plist, 1);
    ms::vbyte_append(plist, 2);
    ms::vbyte_append(plist, 3);
    ms::vbyte_append(plist, 1);
    ms::vbyte_append(plist, 5);
    ms::vbyte_append(plist, 5);
    std::vector<std::pair<uint32_t, uint32_t>> got;
    const bool decoded = ms::for_each_posting(plist.data(), plist.size(), 3,
                                              [&got](uint32_t d, uint32_t tf) { got.emplace_back(d, tf); });
    check(decoded && got.size() == 3, "vbyte: posting list decodes 3 entries");
    check(got[0] == std::make_pair(1u, 2u) && got[1] == std::make_pair(4u, 1u) &&
              got[2] == std::make_pair(9u, 5u),
          "vbyte: delta doc ids reconstruct correctly");
}

void test_text() {
    const auto q = ms::analyze_for_query("倒排索引");
    check(!q.terms.empty(), "text: query tokenization produces terms");
    check(contains(q.terms, "倒排") && contains(q.terms, "索引"), "text: dictionary maximum matching finds 倒排/索引");
    check(contains(q.surfaces, "倒排"), "text: query surface forms for highlighting");

    const auto q2 = ms::analyze_for_query("BM25 检索");
    check(contains(q2.terms, "bm25"), "text: ASCII tokens are lowercased and indexed whole");
    check(contains(q2.terms, "检索"), "text: mixed ASCII + CJK query");

    const auto idx = ms::analyze_for_index("向量检索使用余弦相似度");
    check(contains(idx.terms, "向量") || contains(idx.terms, "检索"), "text: index analysis yields words");
    check(contains(idx.terms, "向量检索") || contains(idx.terms, "索使"), "text: index analysis yields bigrams");
    check(contains(idx.terms, "余"), "text: index analysis yields unigrams for recall");

    const auto fallback = ms::analyze_for_query("检");
    check(contains(fallback.terms, "检"), "text: single-char query falls back to unigram");

    const auto hl = ms::make_highlights("这是一个倒排索引的实现，倒排索引很常用。", {"倒排", "索引"}, 3, 180);
    check(!hl.empty(), "text: highlights produced");
    bool marked = false;
    for (const auto& s : hl) {
        if (s.find("<mark>") != std::string::npos && s.find("</mark>") != std::string::npos) marked = true;
    }
    check(marked, "text: highlights wrap matches in <mark>");
    check(ms::escape_html("<a href=\"x\">&</a>") == "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;",
          "text: html escaping");
    check(ms::utf8_prefix("中文abc", 2) == "中文", "text: utf8 prefix respects codepoints");
}

void test_index() {
    ms::IndexConfig cfg;
    ms::SearchIndex index(cfg);

    std::vector<ms::JsonValue> docs;
    docs.push_back(make_doc("d1", "倒排索引入门", "倒排索引是全文检索的核心数据结构，中文分词影响召回。"));
    docs.push_back(make_doc("d2", "向量检索实践", "向量检索使用余弦相似度做最近邻查询，常见于语义召回。"));
    docs.push_back(make_doc("d3", "并发压测记录", "线程池与缓存命中率决定并发检索的 QPS 和 P99 延迟。"));
    const ms::BulkAddResult added = index.add_bulk(docs);
    check(added.error.empty() && added.inserted == 3, "index: bulk insert of 3 docs");
    const ms::BulkAddResult dup = index.add_bulk(docs);
    check(dup.inserted == 0 && dup.skipped_duplicate == 3, "index: duplicate ids are skipped");

    const ms::IndexStats st = index.stats();
    check(st.docs == 3, "index: stats doc count");
    check(st.terms > 0 && st.postings > 0, "index: stats terms/postings non-zero");
    check(st.build_ms >= 0.0, "index: stats build_ms recorded");
    check(st.avg_doc_len > 0.0, "index: stats average doc length");
    check(st.index_bytes >= st.postings_bytes, "index: stats index bytes");

    ms::SearchParams sp;
    sp.query = "倒排索引";
    sp.k = 3;
    sp.mode = ms::SearchMode::Bm25;
    sp.k1 = cfg.k1;
    sp.b = cfg.b;
    sp.rrf_k = cfg.rrf_k;
    const ms::SearchResult bm = index.search(sp);
    check(!bm.hits.empty(), "index: bm25 search returns hits");
    check(bm.hits[0].doc_id == "d1", "index: bm25 ranks the inverted-index doc first");
    check(!bm.hits[0].highlights.empty(), "index: bm25 hit has highlights");
    check(bm.hits[0].score > 0.0, "index: bm25 hit has positive score");
    check(bm.query_terms.size() > 0, "index: query terms reported");

    sp.mode = ms::SearchMode::Vector;
    const ms::SearchResult vec = index.search(sp);
    check(!vec.hits.empty(), "index: vector search returns hits");
    check(vec.hits[0].score > 0.0 && vec.hits[0].score <= 1.000001, "index: cosine score in (0,1]");

    sp.mode = ms::SearchMode::Hybrid;
    const ms::SearchResult hy = index.search(sp);
    check(!hy.hits.empty(), "index: hybrid search returns hits");

    const size_t generated = index.seed_demo_docs(20);
    check(generated == 20, "index: generated 20 demo docs");
    check(index.stats().docs == 23, "index: total docs after seeding");

    index.reset();
    check(index.stats().docs == 0, "index: reset clears docs");
}

}  // namespace

int main() {
    test_json();
    test_vbyte();
    test_text();
    test_index();
    if (g_failures == 0) {
        std::printf("\nall self tests passed\n");
        return 0;
    }
    std::printf("\n%d self test(s) FAILED\n", g_failures);
    return 1;
}
