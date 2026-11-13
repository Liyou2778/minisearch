#include "text.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace ms {

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------
bool utf8_decode(std::string_view s, size_t pos, uint32_t& cp, size_t& next) {
    if (pos >= s.size()) return false;
    unsigned char c0 = static_cast<unsigned char>(s[pos]);
    if (c0 < 0x80) {
        cp = c0;
        next = pos + 1;
        return true;
    }
    int len = 0;
    uint32_t v = 0;
    if ((c0 & 0xE0) == 0xC0) {
        len = 2;
        v = c0 & 0x1Fu;
    } else if ((c0 & 0xF0) == 0xE0) {
        len = 3;
        v = c0 & 0x0Fu;
    } else if ((c0 & 0xF8) == 0xF0) {
        len = 4;
        v = c0 & 0x07u;
    } else {
        cp = 0xFFFD;
        next = pos + 1;
        return true;
    }
    if (pos + static_cast<size_t>(len) > s.size()) {
        cp = 0xFFFD;
        next = pos + 1;
        return true;
    }
    for (int k = 1; k < len; ++k) {
        unsigned char c = static_cast<unsigned char>(s[pos + static_cast<size_t>(k)]);
        if ((c & 0xC0) != 0x80) {
            cp = 0xFFFD;
            next = pos + 1;
            return true;
        }
        v = (v << 6) | (c & 0x3Fu);
    }
    cp = v;
    next = pos + static_cast<size_t>(len);
    return true;
}

void utf8_append(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool is_cjk(uint32_t cp) {
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;    // CJK Unified Ideographs
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;    // Extension A
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;    // Compatibility Ideographs
    if (cp >= 0x3040 && cp <= 0x30FF) return true;    // Hiragana / Katakana
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;    // Hangul syllables
    if (cp == 0x3005 || cp == 0x3007) return true;    // 々, 〇
    return false;
}

bool is_ascii_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Dictionary
// ---------------------------------------------------------------------------
void Dictionary::add_word(std::string word) {
    if (word.empty()) return;
    size_t cps = 0;
    size_t i = 0;
    while (i < word.size()) {
        uint32_t cp = 0;
        size_t next = 0;
        if (!utf8_decode(word, i, cp, next)) break;
        i = next;
        ++cps;
    }
    if (cps < 2) return;
    max_cp_ = std::max(max_cp_, cps);
    words_.insert(std::move(word));
}

bool Dictionary::contains(std::string_view word) const {
    if (word.empty()) return false;
    return words_.find(std::string(word)) != words_.end();
}

const Dictionary& Dictionary::builtin() {
    static const Dictionary dict = [] {
        Dictionary d;
        // IR / search internals
        static const char* const words[] = {
            "检索", "索引", "倒排", "分词", "向量", "引擎", "文档", "搜索", "排序", "压缩",
            "查询", "服务", "线程", "并发", "性能", "延迟", "吞吐", "数据", "结构", "算法",
            "系统", "网络", "存储", "分片", "副本", "集群", "缓存", "命中", "召回", "精确",
            "相似", "余弦", "距离", "嵌入", "语义", "关键词", "匹配", "最大", "二元", "词典",
            "中文", "全文", "构建", "编译", "优化", "测试", "压测", "负载", "接口", "协议",
            "请求", "响应", "解析", "序列", "编码", "解码", "增量", "更新", "合并", "文件",
            "磁盘", "内存", "分配", "回收", "线程池", "任务", "队列", "原子", "状态", "监控",
            "指标", "日志", "错误", "处理", "分析", "报告", "结果", "阈值", "精度", "误差",
            "训练", "推理", "模型", "特征", "样本", "标签", "分类", "回归", "聚类", "神经",
            "深度", "学习", "机器", "强化", "预测", "评估", "数据集", "分布式", "微服务", "消息队列",
            "容器", "虚拟化", "前端", "后端", "移动端", "安全", "加密", "签名", "认证", "授权",
            "数据库", "事务", "隔离", "一致性", "主从", "复制", "快照", "备份", "恢复", "写入",
            "读取", "删除", "插入", "页面", "组件", "渲染", "浏览器", "客户端", "服务器", "网关",
            "代理", "负载均衡", "熔断", "限流", "降级", "可观测", "追踪", "链路", "容器化", "编排",
            "调度", "资源", "配额", "弹性", "扩缩容", "云原生", "对象存储", "文件系统", "页表", "虚拟内存",
            "进程", "协程", "调度器", "死锁", "竞态", "屏障", "原子操作", "无锁", "哈希", "布隆",
            "过滤器", "跳表", "红黑树", "堆排序", "归并", "快速", "二分", "递归", "迭代", "动态规划",
            "贪心", "图论", "最短路径", "拓扑", "深度优先", "广度优先", "并查集", "前缀树", "后缀", "自动机",
            "倒排表", "词频", "逆文档频率", "打分", "排序器", "分页", "高亮", "片段", "摘要", "去重",
            "归一化", "标准化", "向量化", "相似度", "欧氏", "内积", "点积", "归一", "维度", "降维",
            "量化", "近似", "最近邻", "图索引", "层次", "可导航", "小世界", "候选", "重排", "融合",
            "倒数", "排名", "评估指标", "准确率", "召回率", "平均", "真实", "正例", "负例", "交叉",
            "验证", "过拟合", "正则", "激活", "卷积", "循环", "注意力", "变换", "编码器", "解码器",
            "生成", "对话", "问答", "知识", "图谱", "关系", "实体", "抽取", "标注", "语料",
            "训练集", "开发集", "测试集", "基准", "评测", "排行榜", "开源", "框架", "依赖", "链接",
            "静态", "动态", "头文件", "源文件", "提交", "分支", "冲突", "版本", "发布", "部署",
            "流水线", "自动化", "单元测试", "集成测试", "端到端", "覆盖率", "窗口", "滑动", "切分", "短语",
            "同义", "扩展", "拼写", "纠错", "停用词", "词干", "词形", "还原", "多字段", "权重",
            "点击", "反馈", "排序学习", "个性化", "推荐", "用户", "行为", "日志分析", "离线", "在线",
        };
        for (const char* w : words) d.add_word(w);
        return d;
    }();
    return dict;
}

// ---------------------------------------------------------------------------
// Tokenization
// ---------------------------------------------------------------------------
namespace {

bool is_ascii_token_char(unsigned char c) {
    return is_ascii_alnum(c) || c == '_' || c == '+' || c == '#' || c == '@' || c == '.' || c == '-';
}

void emit_ascii(std::string_view text, size_t start, size_t end, bool for_index, Analysis& a,
                std::unordered_set<std::string>& seen) {
    std::string tok = to_lower(text.substr(start, end - start));
    // Trim leading/trailing punctuation-ish bytes so ".net-cpp." -> "net-cpp".
    size_t b = 0;
    while (b < tok.size() && !is_ascii_alnum(static_cast<unsigned char>(tok[b]))) ++b;
    size_t e = tok.size();
    while (e > b && !is_ascii_alnum(static_cast<unsigned char>(tok[e - 1]))) --e;
    tok = tok.substr(b, e - b);
    if (tok.size() < 2) return;
    if (for_index) {
        a.terms.push_back(std::move(tok));
        return;
    }
    if (seen.insert(tok).second) {
        a.surfaces.push_back(tok);
        a.terms.push_back(std::move(tok));
    }
}

void emit_query_term(std::string t, bool surface, Analysis& a, std::unordered_set<std::string>& seen) {
    if (t.empty()) return;
    if (!seen.insert(t).second) return;
    if (surface) a.surfaces.push_back(t);
    a.terms.push_back(std::move(t));
}

// Handles one contiguous CJK run. starts has run_len + 1 entries: byte offsets,
// starts[run_len] is the byte just past the run.
void process_cjk_run(std::string_view text, const std::vector<size_t>& starts, bool for_index, Analysis& a,
                     const Dictionary& dict, std::unordered_set<std::string>& seen,
                     std::unordered_set<std::string>& seen_fallback) {
    const size_t m = starts.size() - 1;
    auto slice = [&](size_t p, size_t len) -> std::string {
        return std::string(text.substr(starts[p], starts[p + len] - starts[p]));
    };

    if (for_index) {
        // Dictionary maximum matching: longest word wins at each position.
        size_t p = 0;
        const size_t max_len = dict.max_word_codepoints();
        while (p < m) {
            size_t best = 0;
            size_t upper = std::min(max_len, m - p);
            for (size_t L = upper; L >= 2; --L) {
                std::string_view cand(text.data() + starts[p], starts[p + L] - starts[p]);
                if (dict.contains(cand)) {
                    best = L;
                    break;
                }
            }
            if (best >= 2) {
                a.terms.push_back(slice(p, best));
                p += best;
            } else {
                p += 1;
            }
        }
        // Character bigrams (fallback recall for out-of-dictionary text).
        for (size_t p = 0; p + 1 < m; ++p) a.terms.push_back(slice(p, 2));
        // Unigrams so single-character queries still hit.
        for (size_t p = 0; p < m; ++p) a.terms.push_back(slice(p, 1));
        return;
    }

    // Query analysis: words + bigrams as primary terms, unigrams as fallback.
    size_t p = 0;
    const size_t max_len = dict.max_word_codepoints();
    while (p < m) {
        size_t best = 0;
        size_t upper = std::min(max_len, m - p);
        for (size_t L = upper; L >= 2; --L) {
            std::string_view cand(text.data() + starts[p], starts[p + L] - starts[p]);
            if (dict.contains(cand)) {
                best = L;
                break;
            }
        }
        if (best >= 2) {
            emit_query_term(slice(p, best), true, a, seen);
            p += best;
        } else {
            p += 1;
        }
    }
    for (size_t q = 0; q + 1 < m; ++q) emit_query_term(slice(q, 2), true, a, seen);
    for (size_t u = 0; u < m; ++u) {
        std::string uni = slice(u, 1);
        if (seen_fallback.insert(uni).second) a.fallback.push_back(std::move(uni));
    }
}

Analysis analyze_impl(std::string_view text, const Dictionary& dict, bool for_index) {
    Analysis a;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> seen_fallback;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (is_ascii_token_char(c)) {
            size_t start = i;
            while (i < n && is_ascii_token_char(static_cast<unsigned char>(text[i]))) ++i;
            emit_ascii(text, start, i, for_index, a, seen);
            continue;
        }
        uint32_t cp = 0;
        size_t next = 0;
        if (!utf8_decode(text, i, cp, next)) {
            i = next;
            continue;
        }
        if (is_cjk(cp)) {
            std::vector<size_t> starts;
            while (i < n) {
                uint32_t cp2 = 0;
                size_t next2 = 0;
                if (!utf8_decode(text, i, cp2, next2)) break;
                if (!is_cjk(cp2)) break;
                starts.push_back(i);
                i = next2;
            }
            starts.push_back(i);
            process_cjk_run(text, starts, for_index, a, dict, seen, seen_fallback);
            continue;
        }
        i = next;  // whitespace / punctuation / other scripts
    }
    return a;
}

}  // namespace

Analysis analyze_for_index(std::string_view text, const Dictionary& dict) {
    return analyze_impl(text, dict, true);
}

Analysis analyze_for_query(std::string_view text, const Dictionary& dict) {
    Analysis a = analyze_impl(text, dict, false);
    if (a.terms.empty() && !a.fallback.empty()) {
        a.terms = a.fallback;
        a.surfaces = a.fallback;
    }
    return a;
}

// ---------------------------------------------------------------------------
// Highlighting
// ---------------------------------------------------------------------------
std::string escape_html(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char ch : s) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += ch;
        }
    }
    return out;
}

std::string utf8_prefix(std::string_view text, size_t codepoints) {
    size_t i = 0;
    size_t count = 0;
    while (i < text.size() && count < codepoints) {
        uint32_t cp = 0;
        size_t next = 0;
        if (!utf8_decode(text, i, cp, next)) break;
        i = next;
        ++count;
    }
    return std::string(text.substr(0, i));
}

std::vector<std::string> make_highlights(std::string_view text, const std::vector<std::string>& surfaces,
                                         size_t max_snippets, size_t window_bytes) {
    std::vector<std::string> out;
    if (text.empty() || surfaces.empty() || max_snippets == 0) return out;
    const std::string lower = to_lower(text);

    struct Span {
        size_t b;
        size_t e;
    };
    std::vector<Span> spans;
    for (const auto& s : surfaces) {
        if (s.empty()) continue;
        size_t from = 0;
        int found = 0;
        while (found < 40) {
            size_t p = lower.find(s, from);
            if (p == std::string::npos) break;
            spans.push_back({p, p + s.size()});
            from = p + s.size();
            ++found;
        }
    }
    if (spans.empty()) return out;
    std::sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) { return x.b < y.b; });

    std::vector<Span> merged;
    for (const auto& sp : spans) {
        if (!merged.empty() && sp.b <= merged.back().e) {
            merged.back().e = std::max(merged.back().e, sp.e);
        } else {
            merged.push_back(sp);
        }
    }

    size_t last_end = 0;
    for (const auto& sp : merged) {
        if (out.size() >= max_snippets) break;
        size_t wstart = sp.b > 50 ? sp.b - 50 : 0;
        if (wstart < last_end) continue;
        size_t wend = std::min(text.size(), wstart + window_bytes);
        if (wend < sp.e) wend = std::min(text.size(), sp.e);
        if (wend - wstart < window_bytes && wstart > 0) {
            size_t extra = std::min(wstart, window_bytes - (wend - wstart));
            wstart -= extra;
        }
        // Snap to codepoint boundaries (UTF-8 continuation bytes are 10xxxxxx).
        while (wstart > 0 && (static_cast<unsigned char>(text[wstart]) & 0xC0) == 0x80) --wstart;
        while (wend < text.size() && (static_cast<unsigned char>(text[wend]) & 0xC0) == 0x80) ++wend;
        if (wstart < last_end) continue;
        last_end = wend;

        std::string snippet;
        if (wstart > 0) snippet += "\xE2\x80\xA6";  // U+2026 horizontal ellipsis
        size_t cursor = wstart;
        for (const auto& ms : merged) {
            if (ms.e <= wstart) continue;
            if (ms.b >= wend) break;
            size_t mb = std::max(ms.b, wstart);
            size_t me = std::min(ms.e, wend);
            if (mb > cursor) snippet += escape_html(text.substr(cursor, mb - cursor));
            snippet += "<mark>";
            snippet += escape_html(text.substr(mb, me - mb));
            snippet += "</mark>";
            cursor = me;
        }
        if (cursor < wend) snippet += escape_html(text.substr(cursor, wend - cursor));
        if (wend < text.size()) snippet += "\xE2\x80\xA6";
        out.push_back(std::move(snippet));
    }
    return out;
}

}  // namespace ms
