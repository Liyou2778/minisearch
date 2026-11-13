#!/usr/bin/env python3
"""Load a JSONL corpus into a running minisearch instance.

Usage:
    python bench/load_corpus.py --file corpus/corpus.jsonl
    python bench/load_corpus.py --file bench/corpus_bench.jsonl --reset --chunk 1000

If --reset is passed the index is cleared first; otherwise documents are appended.
Prints inserted counts, load wall time, and final /api/stats.
"""
import argparse
import http.client
import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def http_json(host, port, method, path, payload=None, timeout=120.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        headers = {"Connection": "close"}
        body = None
        if payload is not None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json; charset=utf-8"
        conn.request(method, path, body=body, headers=headers)
        resp = conn.getresponse()
        raw = resp.read()
        data = json.loads(raw.decode("utf-8")) if raw else {}
        if resp.status != 200:
            raise RuntimeError("%s %s -> HTTP %d: %s" % (method, path, resp.status,
                                                        data.get("error", raw[:200])))
        return data
    finally:
        conn.close()


def load_file(path, host, port, chunk=1000, reset=False):
    docs = []
    with open(path, "r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                docs.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise SystemExit("invalid JSON at %s:%d: %s" % (path, line_no, exc))
    if not docs:
        raise SystemExit("no documents found in %s" % path)
    if reset:
        http_json(host, port, "POST", "/api/reset")
        print("reset index")
    print("loading %d docs from %s (chunk=%d)" % (len(docs), path, chunk))
    t0 = time.perf_counter()
    inserted = 0
    skipped = 0
    build_ms = 0.0
    for start in range(0, len(docs), chunk):
        part = docs[start:start + chunk]
        r = http_json(host, port, "POST", "/api/docs/bulk", part)
        inserted += int(r.get("inserted", 0))
        skipped += int(r.get("skipped_duplicate", 0))
        build_ms += float(r.get("build_ms", 0.0))
        print("  chunk %d..%d -> inserted=%d total_docs=%d build_ms=%.1f"
              % (start, start + len(part) - 1, r.get("inserted"), r.get("total_docs"),
                 r.get("build_ms", 0.0)))
    wall = time.perf_counter() - t0
    stats = http_json(host, port, "GET", "/api/stats")
    print("done: inserted=%d skipped=%d wall=%.2fs (sum chunk build=%.1f ms)"
          % (inserted, skipped, wall, build_ms))
    print("stats: docs=%d terms=%d postings=%d index=%.2f MiB avg_len=%.1f build_ms=%.1f"
          % (stats["docs"], stats["terms"], stats["postings"],
             stats["index_bytes"] / 1048576.0, stats["avg_doc_len"], stats["build_ms"]))
    return stats


def main(argv=None):
    parser = argparse.ArgumentParser(description="Load a corpus into minisearch")
    parser.add_argument("--file", default=os.path.join(ROOT, "corpus", "corpus.jsonl"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8204)
    parser.add_argument("--chunk", type=int, default=1000)
    parser.add_argument("--reset", action="store_true")
    args = parser.parse_args(argv)
    path = args.file if os.path.isabs(args.file) else os.path.join(ROOT, args.file)
    load_file(path, args.host, args.port, args.chunk, args.reset)
    return 0


if __name__ == "__main__":
    sys.exit(main())
