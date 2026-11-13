// UTF-8 helpers, Chinese tokenization (dictionary maximum matching + bigram
// fallback), and HTML-safe highlight snippet generation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ms {

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------
bool utf8_decode(std::string_view s, size_t pos, uint32_t& cp, size_t& next);
void utf8_append(std::string& out, uint32_t cp);
bool is_cjk(uint32_t cp);
bool is_ascii_alnum(unsigned char c);

// ASCII-only lowercasing that preserves byte offsets (safe for UTF-8 text).
std::string to_lower(std::string_view s);

// ---------------------------------------------------------------------------
// Dictionary for maximum matching. Built-in word list; extensible via add_word.
// ---------------------------------------------------------------------------
class Dictionary {
public:
    static const Dictionary& builtin();

    bool contains(std::string_view word) const;
    size_t max_word_codepoints() const { return max_cp_; }
    size_t size() const { return words_.size(); }
    void add_word(std::string word);

private:
    Dictionary() = default;
    std::unordered_set<std::string> words_;
    size_t max_cp_ = 2;
};

// ---------------------------------------------------------------------------
// Tokenization
// ---------------------------------------------------------------------------
struct Analysis {
    // Terms used to score the document/query (dictionary words + bigrams).
    std::vector<std::string> terms;
    // Original surface forms, used for highlighting.
    std::vector<std::string> surfaces;
    // Additional terms for a query retry when nothing matched (single chars).
    std::vector<std::string> fallback;
};

// Indexing view: dictionary words + bigrams + unigrams (maximum recall for CJK;
// ASCII words are indexed whole).
Analysis analyze_for_index(std::string_view text, const Dictionary& dict = Dictionary::builtin());

// Query view: dictionary words + bigrams, deduplicated; unigrams kept separately
// as a fallback used only when the primary terms match nothing.
Analysis analyze_for_query(std::string_view text, const Dictionary& dict = Dictionary::builtin());

// ---------------------------------------------------------------------------
// Highlighting
// ---------------------------------------------------------------------------
std::string escape_html(std::string_view s);

// Returns up to max_snippets escaped text fragments around the best matches of
// the query surfaces, wrapping matched spans in <mark>...</mark>.
std::vector<std::string> make_highlights(std::string_view text,
                                         const std::vector<std::string>& surfaces,
                                         size_t max_snippets = 3,
                                         size_t window_bytes = 180);

// First `n` codepoints of a text (used for previews without splitting UTF-8).
std::string utf8_prefix(std::string_view text, size_t codepoints);

}  // namespace ms
