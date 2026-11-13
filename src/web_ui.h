// Embedded single-page console served at GET /. No build step, no CDN.
#pragma once

namespace ms {

inline const char* kWebUiHtml = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MiniSearch · 中文检索台</title>
<style>
  :root {
    --bg: #0a0e17; --panel: #111827; --panel2: #0e1524; --line: #1e2a3f;
    --txt: #e6edf7; --muted: #8b9bb4; --accent: #5b8cff; --accent2: #34d399;
    --warn: #fbbf24; --danger: #f87171;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: radial-gradient(1200px 600px at 20% -10%, #16233d 0%, var(--bg) 55%);
    color: var(--txt); font: 14px/1.55 "Segoe UI", "Microsoft YaHei", system-ui, sans-serif;
    min-height: 100vh;
  }
  .app { max-width: 1080px; margin: 0 auto; padding: 26px 20px 60px; }
  header { display: flex; align-items: center; gap: 16px; flex-wrap: wrap; margin-bottom: 20px; }
  .brand { display: flex; align-items: center; gap: 12px; }
  .logo {
    width: 44px; height: 44px; border-radius: 12px; display: grid; place-items: center;
    background: linear-gradient(135deg, #2563eb, #0ea5e9); font-weight: 800; letter-spacing: 1px;
    box-shadow: 0 6px 24px rgba(59,130,246,.35);
  }
  h1 { font-size: 21px; margin: 0; letter-spacing: .3px; }
  .sub { margin: 2px 0 0; color: var(--muted); font-size: 12px; }
  .stats { display: flex; gap: 8px; flex-wrap: wrap; margin-left: auto; }
  .chip {
    background: var(--panel); border: 1px solid var(--line); border-radius: 10px;
    padding: 6px 10px; min-width: 74px;
  }
  .chip span { display: block; font-size: 10px; color: var(--muted); text-transform: uppercase; letter-spacing: .7px; }
  .chip b { font-size: 15px; font-variant-numeric: tabular-nums; }
  .panel { background: var(--panel); border: 1px solid var(--line); border-radius: 16px; padding: 16px; }
  form { display: flex; gap: 10px; flex-wrap: wrap; }
  input[type=text] {
    flex: 1 1 340px; min-width: 200px; background: var(--panel2); color: var(--txt);
    border: 1px solid var(--line); border-radius: 12px; padding: 12px 14px; font-size: 15px; outline: none;
  }
  input[type=text]:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(91,140,255,.18); }
  .seg { display: flex; background: var(--panel2); border: 1px solid var(--line); border-radius: 12px; overflow: hidden; }
  .seg button {
    background: transparent; color: var(--muted); border: 0; padding: 0 14px; font-size: 13px;
    cursor: pointer; font-family: inherit; transition: .15s;
  }
  .seg button.active { background: rgba(91,140,255,.18); color: #cfe0ff; font-weight: 600; }
  select, .btn {
    background: var(--panel2); color: var(--txt); border: 1px solid var(--line);
    border-radius: 12px; padding: 10px 12px; font-size: 13px; font-family: inherit; cursor: pointer;
  }
  .btn.primary { background: linear-gradient(135deg, #2563eb, #3b82f6); border-color: #3b82f6; font-weight: 600; }
  .btn:hover { border-color: var(--accent); }
  .toolbar { display: flex; gap: 8px; align-items: center; margin-top: 12px; flex-wrap: wrap; }
  .toolbar .spacer { flex: 1; }
  .meta { color: var(--muted); font-size: 12px; margin: 14px 2px 6px; display: flex; gap: 14px; flex-wrap: wrap; align-items: center; }
  .meta b { color: #cfe0ff; font-variant-numeric: tabular-nums; }
  .term {
    background: rgba(91,140,255,.14); border: 1px solid rgba(91,140,255,.3); color: #b9ccff;
    border-radius: 7px; padding: 1px 7px; font-size: 12px;
  }
  .hit {
    background: var(--panel); border: 1px solid var(--line); border-radius: 14px;
    padding: 14px 16px; margin-bottom: 12px;
  }
  .hit:hover { border-color: #2b3d5c; }
  .hit-head { display: flex; align-items: baseline; gap: 10px; flex-wrap: wrap; }
  .rank { color: var(--accent2); font-weight: 700; font-size: 12px; }
  .hit h2 { font-size: 16px; margin: 0; font-weight: 600; }
  .doc-id { color: var(--muted); font-family: Consolas, monospace; font-size: 11px; }
  .score { margin-left: auto; font-family: Consolas, monospace; color: var(--warn); font-size: 13px; }
  .brks { display: flex; flex-wrap: wrap; gap: 6px 16px; margin-top: 10px; }
  .brk { display: flex; align-items: center; gap: 7px; min-width: 190px; }
  .brk-k { color: var(--muted); font-size: 11px; width: 92px; text-align: right; font-family: Consolas, monospace; }
  .brk-bar { flex: 1; height: 6px; background: #16203399; border-radius: 99px; overflow: hidden; min-width: 60px; }
  .brk-bar i { display: block; height: 100%; background: linear-gradient(90deg, #2563eb, #38bdf8); border-radius: 99px; }
  .brk-v { font-family: Consolas, monospace; font-size: 11px; color: #c7d5ee; width: 68px; }
  .rank-chip {
    font-size: 11px; color: #a7f3d0; background: rgba(52,211,153,.1);
    border: 1px solid rgba(52,211,153,.25); border-radius: 7px; padding: 2px 8px;
  }
  .highlights { margin: 10px 0 0; padding-left: 18px; color: #c9d6ea; }
  .highlights li { margin: 4px 0; font-size: 13px; }
  mark { background: rgba(251,191,36,.85); color: #1a1200; border-radius: 3px; padding: 0 2px; }
  .empty { color: var(--muted); text-align: center; padding: 40px 10px; }
  details { margin-top: 16px; }
  summary { cursor: pointer; color: var(--muted); font-size: 12px; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 8px; margin-top: 10px; }
  .kv { background: var(--panel2); border: 1px solid var(--line); border-radius: 9px; padding: 8px 10px; }
  .kv span { display: block; color: var(--muted); font-size: 10px; text-transform: uppercase; letter-spacing: .6px; }
  .kv b { font-family: Consolas, monospace; font-size: 12px; font-weight: 500; }
  footer { margin-top: 26px; color: #5b6b85; font-size: 11px; text-align: center; }
  #toast {
    position: fixed; right: 22px; bottom: 22px; background: #1b2740; border: 1px solid #2f4266;
    color: var(--txt); border-radius: 10px; padding: 10px 14px; font-size: 13px;
    opacity: 0; transform: translateY(8px); transition: .2s; pointer-events: none; max-width: 420px;
  }
  #toast.show { opacity: 1; transform: none; }
  #toast.err { border-color: #7f2a2a; background: #2a1414; }
</style>
</head>
<body>
<div class="app">
  <header>
    <div class="brand">
      <div class="logo">MS</div>
      <div>
        <h1>MiniSearch 检索台</h1>
        <p class="sub">C++20 · 倒排索引 + BM25 + 哈希向量 + RRF 混合检索</p>
      </div>
    </div>
    <div class="stats" id="stats"></div>
  </header>

  <section class="panel">
    <form id="search-form">
      <input type="text" id="q" placeholder="输入中文或英文关键词，例如 倒排索引 / 向量检索 / 并发 缓存" autocomplete="off">
      <div class="seg" id="mode-seg">
        <button type="button" data-mode="bm25" class="active">BM25</button>
        <button type="button" data-mode="vector">向量</button>
        <button type="button" data-mode="hybrid">混合</button>
      </div>
      <select id="k">
        <option value="5">Top 5</option>
        <option value="10" selected>Top 10</option>
        <option value="20">Top 20</option>
        <option value="50">Top 50</option>
      </select>
      <button class="btn primary" type="submit">检索</button>
    </form>
    <div class="toolbar">
      <span class="meta" id="meta" style="margin:0">就绪</span>
      <span class="spacer"></span>
      <button class="btn" id="seed">灌入 300 演示文档</button>
      <button class="btn" id="rebuild">重建索引</button>
      <button class="btn" id="reset">清空索引</button>
    </div>
  </section>

  <main id="results">
    <div class="empty">输入关键词开始检索，或先点「灌入 300 演示文档」。</div>
  </main>

  <details>
    <summary>索引统计详情</summary>
    <div class="grid" id="stats-detail"></div>
  </details>

  <footer>MiniSearch MVP · 单进程内存索引 · HTTP API 见 README · 压测脚本 bench/bench.py</footer>
</div>
<div id="toast"></div>

<script>
(function () {
  'use strict';
  const $ = function (id) { return document.getElementById(id); };
  const state = { mode: 'bm25', latencies: [], stats: null };

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }
  function fmtNum(n) {
    if (n == null || isNaN(n)) return '-';
    return Number(n).toLocaleString('en-US');
  }
  function fmtBytes(n) {
    if (n == null || isNaN(n)) return '-';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / 1024 / 1024).toFixed(2) + ' MB';
  }
  function fmtVal(v) {
    if (typeof v !== 'number') return esc(v);
    if (Number.isInteger(v)) return String(v);
    return v.toFixed(4);
  }
  let toastTimer = null;
  function toast(msg, isErr) {
    const t = $('toast');
    t.textContent = msg;
    t.className = 'show' + (isErr ? ' err' : '');
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.className = ''; }, 3200);
  }
  async function api(path, opts) {
    const res = await fetch(path, opts);
    const text = await res.text();
    let data = null;
    try { data = text ? JSON.parse(text) : {}; } catch (e) { throw new Error(text.slice(0, 200)); }
    if (!res.ok) {
      throw new Error((data && data.error) ? data.error : ('HTTP ' + res.status));
    }
    return data;
  }

  function renderStats(s) {
    state.stats = s;
    const chips = [
      ['docs', fmtNum(s.docs)],
      ['terms', fmtNum(s.terms)],
      ['postings', fmtNum(s.postings)],
      ['index', fmtBytes(s.index_bytes)],
      ['avg len', s.avg_doc_len ? s.avg_doc_len.toFixed(1) : '0'],
      ['build', (s.build_ms || 0).toFixed(1) + 'ms'],
      ['queries', fmtNum(s.queries)]
    ];
    $('stats').innerHTML = chips.map(function (kv) {
      return '<div class="chip"><span>' + kv[0] + '</span><b>' + kv[1] + '</b></div>';
    }).join('');
    const detail = [
      ['k1', s.k1], ['b', s.b], ['rrf_k', s.rrf_k], ['vector_dim', s.vector_dim],
      ['postings_bytes', fmtBytes(s.postings_bytes)], ['term_bytes', fmtBytes(s.term_bytes)],
      ['vector_bytes', fmtBytes(s.vector_bytes)], ['doc_bytes', fmtBytes(s.doc_bytes)],
      ['uptime', Math.round(s.uptime_s) + ' s'], ['queries', fmtNum(s.queries)]
    ];
    $('stats-detail').innerHTML = detail.map(function (kv) {
      return '<div class="kv"><span>' + kv[0] + '</span><b>' + esc(kv[1]) + '</b></div>';
    }).join('');
  }
  async function loadStats() {
    try { renderStats(await api('/api/stats')); } catch (e) { /* server not ready yet */ }
  }

  function setMode(mode) {
    state.mode = mode;
    Array.prototype.forEach.call($('mode-seg').children, function (b) {
      b.classList.toggle('active', b.dataset.mode === mode);
    });
    if ($('q').value.trim()) doSearch();
  }
  Array.prototype.forEach.call($('mode-seg').children, function (b) {
    b.addEventListener('click', function () { setMode(b.dataset.mode); });
  });

  async function doSearch(ev) {
    if (ev) ev.preventDefault();
    const q = $('q').value.trim();
    if (!q) return;
    const mode = state.mode;
    const k = $('k').value;
    const t0 = performance.now();
    try {
      const data = await api('/api/search?q=' + encodeURIComponent(q) + '&k=' + encodeURIComponent(k) + '&mode=' + encodeURIComponent(mode));
      const rtt = performance.now() - t0;
      renderResults(data, rtt);
      state.latencies.push(data.took_ms);
      if (state.latencies.length > 20) state.latencies.shift();
    } catch (e) {
      $('meta').textContent = '检索失败：' + e.message;
      toast('检索失败：' + e.message, true);
    }
  }

  function renderResults(data, rtt) {
    const hits = data.hits || [];
    const avg = state.latencies.length
      ? state.latencies.reduce(function (a, b) { return a + b; }, 0) / state.latencies.length
      : data.took_ms;
    const equivQps = avg > 0 ? (1000 / avg) : 0;
    $('meta').innerHTML =
      '<span>' + fmtNum(hits.length) + ' 条结果</span>' +
      '<span>服务端 <b>' + data.took_ms.toFixed(2) + ' ms</b></span>' +
      '<span>往返 <b>' + rtt.toFixed(1) + ' ms</b></span>' +
      '<span>候选 <b>' + fmtNum(data.total_candidates) + '</b></span>' +
      '<span>命中词 <b>' + fmtNum(data.matched_terms) + '</b></span>' +
      '<span>串行等效 <b>' + equivQps.toFixed(0) + ' QPS</b></span>' +
      ((data.query_terms || []).length
        ? '<span>分词：' + data.query_terms.map(function (t) { return '<span class="term">' + esc(t) + '</span>'; }).join(' ') + '</span>'
        : '');

    const maxima = {};
    hits.forEach(function (h) {
      Object.keys(h.score_breakdown || {}).forEach(function (k) {
        if (k.slice(-5) === '_rank') return;
        const v = Number(h.score_breakdown[k]);
        if (!isNaN(v)) maxima[k] = Math.max(maxima[k] || 0, v);
      });
    });

    if (!hits.length) {
      $('results').innerHTML = '<div class="empty">没有结果。先灌入演示文档，或换个关键词 / 换个模式试试。</div>';
      return;
    }
    $('results').innerHTML = hits.map(function (h) {
      const bd = h.score_breakdown || {};
      const parts = [];
      Object.keys(bd).forEach(function (k) {
        if (k.slice(-5) === '_rank') {
          const label = k === 'bm25_rank' ? 'BM25' : (k === 'vector_rank' ? '向量' : k);
          parts.push('<span class="rank-chip">' + esc(label) + ' ' + (bd[k] > 0 ? '#' + bd[k] : '—') + '</span>');
          return;
        }
        const max = maxima[k] || 1;
        const pct = Math.max(0, Math.min(100, (Number(bd[k]) / max) * 100));
        parts.push('<div class="brk"><span class="brk-k">' + esc(k) + '</span>' +
          '<span class="brk-bar"><i style="width:' + pct.toFixed(1) + '%"></i></span>' +
          '<span class="brk-v">' + fmtVal(bd[k]) + '</span></div>');
      });
      const hl = (h.highlights && h.highlights.length)
        ? '<ul class="highlights">' + h.highlights.map(function (s) { return '<li>' + s + '</li>'; }).join('') + '</ul>'
        : '<div class="highlights">' + esc(h.preview || '') + '</div>';
      return '<article class="hit">' +
        '<div class="hit-head">' +
          '<span class="rank">#' + h.rank + '</span>' +
          '<h2>' + esc(h.title || '(无标题)') + '</h2>' +
          '<span class="doc-id">' + esc(h.doc_id) + '</span>' +
          '<span class="score">' + Number(h.score).toFixed(4) + '</span>' +
        '</div>' +
        '<div class="brks">' + parts.join('') + '</div>' +
        hl +
        '</article>';
    }).join('');
  }

  async function postAction(path, okMsg) {
    try {
      const r = await api(path, { method: 'POST' });
      toast(okMsg + (r.total_docs != null ? '，当前文档 ' + fmtNum(r.total_docs) : '') +
        (r.build_ms != null ? '，构建 ' + r.build_ms.toFixed(1) + ' ms' : ''));
      await loadStats();
    } catch (e) {
      toast('操作失败：' + e.message, true);
    }
  }
  $('search-form').addEventListener('submit', doSearch);
  $('seed').addEventListener('click', function () { postAction('/api/seed?count=300', '已灌入演示文档'); });
  $('rebuild').addEventListener('click', function () { postAction('/api/rebuild', '索引已重建'); });
  $('reset').addEventListener('click', function () {
    if (!confirm('确认清空全部文档和索引？')) return;
    api('/api/reset', { method: 'POST' }).then(function () {
      toast('索引已清空');
      $('results').innerHTML = '<div class="empty">索引已清空。</div>';
      loadStats();
    }).catch(function (e) { toast('操作失败：' + e.message, true); });
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === '/' && document.activeElement !== $('q')) { e.preventDefault(); $('q').focus(); }
  });
  loadStats();
  setInterval(loadStats, 3000);
})();
</script>
</body>
</html>)HTML";

}  // namespace ms
