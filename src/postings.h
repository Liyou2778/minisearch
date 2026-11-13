// VByte compression for posting lists.
//
// A posting list for one term is encoded as:
//   repeat df times: [delta docID (vbyte)] [term frequency (vbyte)]
// Doc IDs inside a term's list are strictly increasing during index build, so
// deltas are small (usually 1..few for common words) and each posting costs
// 2 bytes on average instead of 8 for a (uint32, uint32) pair.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ms {

inline void vbyte_append(std::vector<uint8_t>& out, uint32_t v) {
    while (v >= 0x80u) {
        out.push_back(static_cast<uint8_t>(v) | 0x80u);
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

inline bool vbyte_next(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    uint32_t v = 0;
    int shift = 0;
    while (p < end) {
        uint8_t b = *p++;
        v |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) {
            out = v;
            return true;
        }
        shift += 7;
        if (shift > 28) return false;
    }
    return false;
}

// Decodes a posting list. fn(doc_id, term_freq) is called for every entry.
template <typename Fn>
bool for_each_posting(const uint8_t* data, size_t bytes, uint32_t df, Fn&& fn) {
    const uint8_t* p = data;
    const uint8_t* end = data + bytes;
    uint32_t doc = 0;
    for (uint32_t i = 0; i < df; ++i) {
        uint32_t delta = 0;
        uint32_t tf = 0;
        if (!vbyte_next(p, end, delta)) return false;
        if (!vbyte_next(p, end, tf)) return false;
        doc += delta;
        fn(doc, tf);
    }
    return true;
}

}  // namespace ms
