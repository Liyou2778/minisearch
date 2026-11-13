// Deterministic Chinese demo corpus generator for POST /api/seed.
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "index.h"

namespace ms {
namespace {

const std::array<const char*, 20> kTopics = {
    "向量检索", "倒排索引", "中文分词", "分布式存储", "缓存系统",
    "并发编程", "编译器优化", "机器学习平台", "日志分析", "消息队列",
    "微服务治理", "数据库事务", "网络协议", "容器调度", "搜索排序",
    "推荐系统", "文本摘要", "高可用架构", "流式计算", "对象存储",
};

const std::array<const char*, 12> kConcepts = {
    "倒排索引", "向量检索", "BM25 打分", "余弦相似度", "RRF 融合", "中文分词",
    "词典最大匹配", "二元组切分", "线程池", "布隆过滤器", "前缀树", "倒排表",
};

const std::array<const char*, 10> kScenarios = {
    "高并发搜索", "离线构建", "在线检索", "冷启动", "热点查询",
    "大促流量", "日志分析", "语义召回", "多租户", "灰度发布",
};

const std::array<const char*, 10> kProblems = {
    "内存占用过高", "查询延迟抖动", "索引构建缓慢", "长尾查询", "中文歧义",
    "同义词漏召回", "缓存穿透", "数据倾斜", "热点 key 集中", "重复文档",
};

const std::array<const char*, 10> kMethods = {
    "最大匹配", "二元组切分", "增量合并", "分段存储", "批量写入",
    "批量重建", "倒排压缩", "VByte 编码", "哈希特征", "余弦重排",
};

const std::array<const char*, 9> kMetrics = {
    "QPS", "P50 延迟", "P99 延迟", "召回率", "准确率",
    "内存占用", "索引体积", "构建耗时", "缓存命中率",
};

const std::array<const char*, 9> kNumbers = {
    "1200", "2600", "3500", "4800", "8200", "12000", "512", "1024", "4096",
};

const std::array<const char*, 3> kUnits = {"毫秒", "微秒", "秒"};

template <size_t N>
const char* pick(std::mt19937_64& rng, const std::array<const char*, N>& pool) {
    return pool[static_cast<size_t>(rng() % N)];
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string fill(std::mt19937_64& rng, const char* tmpl) {
    std::string s(tmpl);
    s = replace_all(std::move(s), "{concept}", pick(rng, kConcepts));
    s = replace_all(std::move(s), "{scenario}", pick(rng, kScenarios));
    s = replace_all(std::move(s), "{problem}", pick(rng, kProblems));
    s = replace_all(std::move(s), "{method}", pick(rng, kMethods));
    s = replace_all(std::move(s), "{method2}", pick(rng, kMethods));
    s = replace_all(std::move(s), "{metric}", pick(rng, kMetrics));
    s = replace_all(std::move(s), "{num}", pick(rng, kNumbers));
    s = replace_all(std::move(s), "{num2}", pick(rng, kNumbers));
    s = replace_all(std::move(s), "{unit}", pick(rng, kUnits));
    return s;
}

const std::array<const char*, 10> kSentences = {
    "本文讨论{concept}在{scenario}场景下的实现细节，重点分析{problem}的成因与优化路径。",
    "我们采用{method}来处理{concept}，在{scenario}下把{metric}降低了 {num} {unit}。",
    "针对{problem}，团队引入{method}，并配合{method2}做兜底，整体表现更加稳定。",
    "{concept}是{scenario}的核心模块之一，常见做法是先用{method}，再用{method2}重排。",
    "在压测中，系统在 32 并发下 QPS 约为 {num}，{metric} 保持在 {num2} {unit} 以内。",
    "索引构建阶段会对文档做分词与{method}，然后用 VByte 压缩倒排表以减少内存占用。",
    "向量检索使用 512 维哈希特征，通过余弦相似度计算得分，再用 RRF 与 BM25 融合排序。",
    "{scenario}对{problem}很敏感，因此需要在{method}和{method2}之间做权衡，并持续监控{metric}。",
    "文档入库时校验 {num} 条记录，{metric} 从 {num2} {unit} 优化到 {num} {unit}。",
    "线上观察到{problem}，通过{method}与缓存预热，最终把{metric}稳定在可接受范围。",
};

const std::array<const char*, 5> kTitleForms = {
    "关于{topic}的实践笔记（%d）",
    "{topic}：从原理到线上优化（%d）",
    "一次{topic}故障复盘（%d）",
    "{topic}入门与压测记录（%d）",
    "{topic}设计取舍（%d）",
};

}  // namespace

std::vector<SeedDoc> generate_seed_docs(size_t count, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<SeedDoc> docs;
    docs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const char* topic = pick(rng, kTopics);
        std::string title = fill(rng, pick(rng, kTitleForms));
        title = replace_all(std::move(title), "{topic}", topic);
        title = replace_all(std::move(title), "%d", std::to_string(i + 1));

        std::string body;
        const size_t sentences = 3 + static_cast<size_t>(rng() % 3);
        for (size_t s = 0; s < sentences; ++s) {
            if (!body.empty()) body += ' ';
            body += fill(rng, pick(rng, kSentences));
        }
        SeedDoc d;
        d.id = "seed-" + std::to_string(i + 1);
        d.title = title;
        d.text = std::move(body);
        docs.push_back(std::move(d));
    }
    return docs;
}

}  // namespace ms
