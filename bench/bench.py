#!/usr/bin/env python3
"""minisearch benchmark: bulk-load N generated Chinese docs, then measure QPS and
latency percentiles for bm25 / vector / hybrid at several concurrency levels.

Usage (server must already be running on the target port):
    python bench/bench.py
    python bench/bench.py --docs 2000 --duration 3 --concurrency 1,8,32
    python bench/bench.py --modes bm25 --concurrency 1,8,32,64
    python bench/bench.py --load-only --docs 5000

Method:
  * index is reset, then one POST /api/docs/bulk per chunk (default: one big request)
  * QPS = completed search requests / wall time (client side, includes connect)
  * latency = client round trip in ms; server_ms = took_ms reported by the server
  * every worker uses its own random query stream; all workers start on a barrier
"""
import argparse
import http.client
import json
import math
import os
import random
import sys
import threading
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from gen_corpus import generate  # noqa: E402

QUERIES = [
    "倒排索引", "向量检索", "中文分词 最大匹配", "BM25 打分 融合",
    "并发 线程池 缓存", "P99 延迟 抖动", "消息队列 消费", "分布式 一致性",
    "增量 合并 索引", "余弦相似度 重排", "RRF 混合检索", "缓存穿透 命中率",
    "日志 分析 管道", "容器 调度 编排", "数据库 事务 隔离", "网络 协议 握手",
    "推荐 排序 特征", "文本 摘要 抽取", "高可用 多副本", "压测 QPS 并发",
]


def http_request(host, port, method, path, payload=None, timeout=120.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        headers = {"Connection": "close"}
        body = None
        if payload is not None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json; charset=utf-8"
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        return resp.status, resp.read()
    finally:
        conn.close()


def api_json(host, port, method, path, payload=None, timeout=120.0):
    status, raw = http_request(host, port, method, path, payload, timeout)
    data = json.loads(raw.decode("utf-8")) if raw else {}
    if status != 200:
        raise RuntimeError("%s %s -> HTTP %d: %s" % (method, path, status,
                                                    data.get("error", raw[:200])))
    return data


def percentile(sorted_values, p):
    if not sorted_values:
        return 0.0
    n = len(sorted_values)
    rank = max(1, int(math.ceil(p / 100.0 * n)))
    return sorted_values[min(n - 1, rank - 1)]


def run_cell(host, port, mode, k, queries, concurrency, duration, warmup_seconds):
    barrier = threading.Barrier(concurrency + 1)
    lock = threading.Lock()
    client_lat = []
    server_ms = []
    errors = [0]

    def worker(tid):
        rng = random.Random(9000 + tid)
        local_lat = []
        local_srv = []
        try:
            barrier.wait()
            deadline = time.perf_counter() + duration
            while time.perf_counter() < deadline:
                q = queries[rng.randrange(len(queries))]
                path = "/api/search?q=%s&k=%d&mode=%s" % (urllib.parse.quote(q), k, mode)
                t0 = time.perf_counter()
                try:
                    status, raw = http_request(host, port, "GET", path, timeout=30.0)
                    if status != 200:
                        raise RuntimeError("HTTP %d" % status)
                    data = json.loads(raw.decode("utf-8"))
                    local_lat.append((time.perf_counter() - t0) * 1000.0)
                    local_srv.append(float(data.get("took_ms", 0.0)))
                except Exception:
                    with lock:
                        errors[0] += 1
        finally:
            with lock:
                client_lat.extend(local_lat)
                server_ms.extend(local_srv)

    threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(concurrency)]
    for t in threads:
        t.start()
    barrier.wait()
    wall0 = time.perf_counter()
    for t in threads:
        t.join()
    wall = time.perf_counter() - wall0

    client_sorted = sorted(client_lat)
    server_sorted = sorted(server_ms)
    reqs = len(client_lat)
    qps = reqs / wall if wall > 0 else 0.0
    return {
        "mode": mode,
        "concurrency": concurrency,
        "k": k,
        "duration_s": round(wall, 3),
        "requests": reqs,
        "errors": errors[0],
        "qps": round(qps, 2),
        "client_mean_ms": round(sum(client_lat) / reqs, 3) if reqs else 0.0,
        "client_p50_ms": round(percentile(client_sorted, 50), 3),
        "client_p95_ms": round(percentile(client_sorted, 95), 3),
        "client_p99_ms": round(percentile(client_sorted, 99), 3),
        "server_mean_ms": round(sum(server_ms) / len(server_ms), 3) if server_ms else 0.0,
        "server_p50_ms": round(percentile(server_sorted, 50), 3),
        "server_p95_ms": round(percentile(server_sorted, 95), 3),
        "server_p99_ms": round(percentile(server_sorted, 99), 3),
    }


def warmup(host, port, queries, count=30):
    for i in range(count):
        q = urllib.parse.quote(queries[i % len(queries)])
        try:
            http_request(host, port, "GET", "/api/search?q=%s&k=10&mode=bm25" % q, timeout=30.0)
        except Exception:
            pass


def reset_and_load(host, port, docs, chunk):
    api_json(host, port, "POST", "/api/reset")
    total = 0
    build_ms = 0.0
    t0 = time.perf_counter()
    for start in range(0, len(docs), chunk):
        part = docs[start:start + chunk]
        r = api_json(host, port, "POST", "/api/docs/bulk", part, timeout=300.0)
        total += int(r.get("inserted", 0))
        build_ms += float(r.get("build_ms", 0.0))
    wall_ms = (time.perf_counter() - t0) * 1000.0
    stats = api_json(host, port, "GET", "/api/stats")
    return {
        "inserted": total,
        "load_wall_ms": round(wall_ms, 1),
        "sum_build_ms": round(build_ms, 1),
        "stats": stats,
    }


def format_table(results):
    lines = []
    header = ("| mode | conc | requests | QPS | client P50 | client P95 | client P99 "
              "| server P50 | server P99 | errors |")
    sep = "|---|---|---|---|---|---|---|---|---|---|"
    lines.append(header)
    lines.append(sep)
    for r in results:
        lines.append("| %s | %d | %d | %.1f | %.2f ms | %.2f ms | %.2f ms | %.2f ms | %.2f ms | %d |"
                     % (r["mode"], r["concurrency"], r["requests"], r["qps"],
                        r["client_p50_ms"], r["client_p95_ms"], r["client_p99_ms"],
                        r["server_p50_ms"], r["server_p99_ms"], r["errors"]))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description="minisearch QPS/latency benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8204)
    parser.add_argument("--docs", type=int, default=5000, help="documents to generate + load")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--chunk", type=int, default=5000, help="docs per bulk request")
    parser.add_argument("--k", type=int, default=10, help="results per query")
    parser.add_argument("--modes", default="bm25,vector,hybrid")
    parser.add_argument("--concurrency", default="1,8,32")
    parser.add_argument("--duration", type=float, default=4.0, help="measure seconds per cell")
    parser.add_argument("--warmup", type=float, default=0.0, help="unused (kept for CLI compat)")
    parser.add_argument("--load-only", action="store_true", help="load docs, print stats, do not benchmark")
    parser.add_argument("--no-reset", action="store_true", help="keep existing docs (append)")
    parser.add_argument("--out", default=os.path.join(HERE, "results.json"))
    args = parser.parse_args(argv)

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    concs = [int(c) for c in args.concurrency.split(",") if c.strip()]
    out_path = args.out if os.path.isabs(args.out) else os.path.join(ROOT, args.out)

    print("== target http://%s:%d ==" % (args.host, args.port))
    try:
        health = api_json(args.host, args.port, "GET", "/api/health")
    except Exception as exc:
        print("cannot reach server: %s" % exc)
        print("start it first, e.g.: build\\minisearch.exe --port %d" % args.port)
        return 1
    print("server: %s docs=%d uptime=%.1fs" % (health.get("version"), health.get("docs", 0),
                                               health.get("uptime_s", 0.0)))

    t_gen0 = time.perf_counter()
    docs = generate(args.docs, args.seed)
    gen_ms = (time.perf_counter() - t_gen0) * 1000.0
    avg_chars = sum(len(d["title"]) + len(d["text"]) for d in docs) / max(1, len(docs))
    print("generated %d docs (%.0f chars/doc, seed=%d) in %.1f ms"
          % (len(docs), avg_chars, args.seed, gen_ms))

    load = None
    if not args.no_reset:
        load = reset_and_load(args.host, args.port, docs, args.chunk)
        st = load["stats"]
        print("loaded: inserted=%d wall=%.1f ms sum_build=%.1f ms"
              % (load["inserted"], load["load_wall_ms"], load["sum_build_ms"]))
        print("index: docs=%d terms=%d postings=%d bytes=%.2f MiB build_ms=%.1f avg_len=%.1f"
              % (st["docs"], st["terms"], st["postings"], st["index_bytes"] / 1048576.0,
                 st["build_ms"], st["avg_doc_len"]))
        if load["inserted"] < args.docs:
            print("WARNING: inserted %d < requested %d" % (load["inserted"], args.docs))
    stats = api_json(args.host, args.port, "GET", "/api/stats")

    if args.load_only:
        result = {"docs": len(docs), "gen_ms": round(gen_ms, 1), "load": load, "stats": stats,
                  "bench": []}
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
        print("wrote", out_path)
        return 0

    warmup(args.host, args.port, QUERIES, count=30)
    print("warmup done; measuring %d cells (%.1fs each)" % (len(modes) * len(concs), args.duration))

    results = []
    queries_before = int(api_json(args.host, args.port, "GET", "/api/stats")["queries"])
    for mode in modes:
        for conc in concs:
            cell = run_cell(args.host, args.port, mode, args.k, QUERIES, conc, args.duration, 0.0)
            results.append(cell)
            print("  %-6s c=%-2d reqs=%-6d QPS=%8.1f  p50=%7.2f ms  p99=%7.2f ms  (server p99=%6.2f ms) err=%d"
                  % (mode, conc, cell["requests"], cell["qps"], cell["client_p50_ms"],
                     cell["client_p99_ms"], cell["server_p99_ms"], cell["errors"]))

    queries_after = int(api_json(args.host, args.port, "GET", "/api/stats")["queries"])
    total_wall = sum(r["duration_s"] for r in results)
    server_qps = (queries_after - queries_before) / total_wall if total_wall > 0 else 0.0
    print("\nserver-side query counter: %d -> %d over %.1f s = %.1f queries/s (all cells)"
          % (queries_before, queries_after, total_wall, server_qps))

    table = format_table(results)
    print("\n" + table)

    payload = {
        "target": "http://%s:%d" % (args.host, args.port),
        "server": health,
        "docs_requested": args.docs,
        "corpus_seed": args.seed,
        "corpus_avg_chars": round(avg_chars, 1),
        "gen_ms": round(gen_ms, 1),
        "load": load,
        "stats_after_load": stats,
        "k": args.k,
        "queries": QUERIES,
        "results": results,
        "server_qps_all_cells": round(server_qps, 2),
        "table_markdown": table,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    print("\nwrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
