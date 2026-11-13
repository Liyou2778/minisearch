#!/usr/bin/env python3
"""Deterministic Chinese document generator for minisearch demo + benchmark.

Usage:
    python bench/gen_corpus.py --n 1200 --out corpus/corpus.jsonl
    python bench/gen_corpus.py --n 5000 --out bench/corpus_bench.jsonl --seed 7

Output is JSONL, one {"id","title","text"} object per line (UTF-8).
"""
import argparse
import json
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TOPICS = [
    "向量检索", "倒排索引", "中文分词", "分布式存储", "缓存系统", "并发编程",
    "编译器优化", "机器学习平台", "日志分析", "消息队列", "微服务治理", "数据库事务",
    "网络协议", "容器调度", "搜索排序", "推荐系统", "文本摘要", "高可用架构",
    "流式计算", "对象存储", "检索系统", "索引服务", "查询引擎", "压测实践",
    "特征工程", "模型推理", "数据管道", "权限网关", "实时计算", "监控告警",
]

CONCEPTS = [
    "倒排索引", "向量检索", "BM25 打分", "余弦相似度", "RRF 融合", "中文分词",
    "词典最大匹配", "二元组切分", "线程池", "布隆过滤器", "前缀树", "倒排表",
    "滑动窗口", "词频统计", "逆文档频率", "段落压缩", "分片路由", "副本同步",
    "一致性哈希", "消息确认", "令牌桶", "熔断降级", "冷热分离", "增量日志",
    "批量写入", "缓存预热", "热点探测", "查询重写", "同义扩展", "拼写纠错",
    "跳表", "红黑树", "并查集", "后缀自动机", "图索引", "层次可导航",
    "语义嵌入", "稀疏向量", "稠密向量", "重排序", "混合检索", "召回归一化",
]

SCENES = [
    "高并发搜索", "离线构建", "在线检索", "冷启动", "热点查询", "大促流量",
    "日志分析", "语义召回", "多租户", "灰度发布", "故障复盘", "容量规划",
    "长尾查询", "秒杀场景", "数据迁移", "多机房容灾", "夜间批处理", "小流量灰度",
    "全文检索", "商品搜索", "知识库问答", "舆情监控",
]

PROBLEMS = [
    "内存占用过高", "查询延迟抖动", "索引构建缓慢", "长尾查询", "中文歧义",
    "同义词漏召回", "缓存穿透", "数据倾斜", "热点 key 集中", "重复文档",
    "写入放大", "排序抖动", "磁盘 IO 瓶颈", "GC 停顿", "锁竞争",
    "倒排膨胀", "分词粒度不一致", "冷数据堆积", "队列积压", "网络重传",
]

METHODS = [
    "最大匹配", "二元组切分", "增量合并", "分段存储", "批量写入", "批量重建",
    "倒排压缩", "VByte 编码", "哈希特征", "余弦重排", "RRF 融合", "滑动窗口",
    "布隆过滤", "一致性哈希", "双写校验", "异步刷盘", "热点缓存", "预计算",
    "分治归并", "位图索引", "前缀剪枝", "词干还原", "停用词过滤", "加权融合",
]

METRICS = [
    "QPS", "P50 延迟", "P99 延迟", "P999 延迟", "召回率", "准确率",
    "内存占用", "索引体积", "构建耗时", "缓存命中率", "CPU 使用率", "磁盘写入量",
    "平均响应时间", "错误率", "长尾命中率", "首屏耗时",
]

TEAMS = [
    "检索组", "索引组", "平台组", "存储组", "基础设施组", "算法组",
    "搜索质量组", "性能优化组", "架构组", "数据组",
]

NUMBERS = ["1200", "2600", "3500", "4800", "8200", "12000", "24000", "36", "64",
           "96", "128", "256", "512", "1024", "2048", "4096"]

UNITS = ["毫秒", "微秒", "秒", "MB", "GB", "万次"]

SENTENCES = [
    "本文讨论{concept}在{scene}场景下的实现细节，重点分析{problem}的成因与优化路径。",
    "我们采用{method}来处理{concept}，在{scene}下把{metric}降低了 {num} {unit}。",
    "针对{problem}，团队引入{method}，并配合{method}做兜底，整体表现更加稳定。",
    "{concept}是{scene}的核心模块之一，常见做法是先用{method}，再用{method}重排。",
    "在压测中，系统在 32 并发下 QPS 约为 {num}，{metric} 保持在 {num} {unit} 以内。",
    "索引构建阶段会对文档做分词与{method}，然后用 VByte 压缩倒排表以减少内存占用。",
    "向量检索使用 512 维哈希特征，通过余弦相似度计算得分，再用 RRF 与 BM25 融合排序。",
    "{scene}对{problem}很敏感，因此需要在{method}和{method}之间做权衡，并持续监控{metric}。",
    "文档入库时校验 {num} 条记录，{metric} 从 {num} {unit} 优化到 {num} {unit}。",
    "线上观察到{problem}，通过{method}与缓存预热，最终把{metric}稳定在可接受范围。",
    "为了验证{concept}的效果，我们对比了 zvec 与 manticore 的 BM25 延迟和召回。",
    "日志显示查询并发达到 {num} 时，线程池队列开始堆积，需要调整 worker 数量。",
    "该方案把{concept}的构建拆成增量段与全量段，merge 时按 docID delta 解码。",
    "接口协议使用 HTTP/1.1 短连接，每个请求返回 JSON，包含 score_breakdown 字段。",
    "数据倾斜会导致分片负载不均，通常用{method}加二次哈希来缓解。",
    "服务网格中的网关会把{metric}写入 Prometheus，再通过告警规则触发扩容。",
    "{concept}的离线评估依赖标注样本，{team}每周会更新一次评测集。",
    "经过三轮优化，{scene}下的{metric}从 {num} {unit} 下降到 {num} {unit}。",
]

TITLES = [
    "关于{topic}的实践笔记（%d）",
    "{topic}：从原理到线上优化（%d）",
    "一次{topic}故障复盘（%d）",
    "{topic}入门与压测记录（%d）",
    "{topic}设计取舍（%d）",
    "{topic}性能调优清单（%d）",
    "从零实现{topic}（%d）",
    "{topic}常见坑与规避（%d）",
]


def _fill(rng, template):
    values = {
        "concept": rng.choice(CONCEPTS),
        "scene": rng.choice(SCENES),
        "problem": rng.choice(PROBLEMS),
        "method": rng.choice(METHODS),
        "metric": rng.choice(METRICS),
        "team": rng.choice(TEAMS),
        "num": rng.choice(NUMBERS),
        "unit": rng.choice(UNITS),
    }
    out = template
    for key, value in values.items():
        out = out.replace("{" + key + "}", value)
    return out


_SERIAL_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def generate(n, seed=42):
    """Return a list of {"id","title","text"} dicts (deterministic for a seed)."""
    rng = random.Random(seed)
    docs = []
    for i in range(n):
        topic = rng.choice(TOPICS)
        title = rng.choice(TITLES).replace("{topic}", topic).replace("%d", str(i + 1))
        sentence_count = rng.randint(4, 8)
        body = " ".join(_fill(rng, rng.choice(SENTENCES)) for _ in range(sentence_count))
        serial = "".join(rng.choice(_SERIAL_ALPHABET) for _ in range(6))
        # Per-doc serial + topic keep the term dictionary from collapsing to the
        # template vocabulary while still producing repeated n-grams.
        body += " 文档编号 %s，主题为%s，由%s维护。" % (serial, topic, rng.choice(TEAMS))
        doc_id = "gen-%d" % (i + 1)
        docs.append({"id": doc_id, "title": title, "text": body})
    return docs


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate a deterministic Chinese corpus")
    parser.add_argument("--n", type=int, default=1200, help="number of documents")
    parser.add_argument("--seed", type=int, default=42, help="PRNG seed")
    parser.add_argument("--out", default=os.path.join(ROOT, "corpus", "corpus.jsonl"),
                        help="output JSONL path")
    args = parser.parse_args(argv)

    docs = generate(args.n, args.seed)
    out_path = args.out if os.path.isabs(args.out) else os.path.join(ROOT, args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        for doc in docs:
            f.write(json.dumps(doc, ensure_ascii=False, separators=(",", ":")) + "\n")
    total_chars = sum(len(d["title"]) + len(d["text"]) for d in docs)
    print("wrote %d docs (%d chars, avg %.0f chars/doc) -> %s"
          % (len(docs), total_chars, total_chars / max(1, len(docs)), out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
