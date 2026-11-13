# MiniSearch — C++20 中文全文 + 向量混合检索引擎（MVP）

从零手写的中文检索 MVP：**自写 HTTP/1.1 服务（winsock2 + 线程池）、自写极简 JSON、倒排索引（docID delta + VByte 压缩 posting list）、BM25（k1/b 可配）、确定性哈希向量 + 余弦暴力检索、RRF 混合融合、单页 Web 检索台、压测脚本**。

除 Windows 系统库 `ws2_32` 外**零第三方依赖**：不用 vcpkg、不用 JSON 库、不用 HTTP 框架、不用分词库。端口 **8204**。

```
┌────────────────────── Browser / curl / bench.py ──────────────────────┐
│ GET /            → 内嵌检索台（HTML+CSS+JS，单文件）                    │
│ GET /api/search  → BM25 / vector / hybrid，返回 highlights+breakdown  │
│ POST /api/docs   /api/docs/bulk /api/seed /api/rebuild /api/reset     │
└───────────────────────────────────┬───────────────────────────────────┘
                                    │ HTTP/1.1（Connection: close）
                          ┌─────────▼──────────┐
                          │  HttpServer        │ accept 循环 + 固定线程池
                          │  route / render    │ 自写 JSON parser/serializer
                          └─────────┬──────────┘
                          ┌─────────▼──────────────────────────┐
                          │  SearchIndex（std::shared_mutex）   │
                          │  ├─ DocTable: id/title/text/len     │
                          │  ├─ InvertedIndex                   │
                          │  │    term → arena[VByte(delta,tf)] │
                          │  ├─ BM25 scorer（k1=1.2, b=0.75）   │
                          │  ├─ HashVectorIndex（512 维，余弦）  │
                          │  └─ RRF 融合（k=60）                 │
                          └────────────────────────────────────┘
```

---

## 1. 快速开始（精确命令）

### 1.1 构建（Windows 11 + pwsh）

本机工具链（PATH 里没有，脚本内用绝对路径，可被环境变量/参数覆盖）：

| 工具 | 绝对路径 |
|---|---|
| vcvars64.bat | `C:\develop\vs2022\community\VC\Auxiliary\Build\vcvars64.bat` |
| cmake.exe | `C:\develop\vs2022\community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| ninja.exe | `C:\develop\vs2022\community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |

```powershell
cd C:\Users\Liyou\Documents\deepseek\replicas\04-minisearch

# 方式 A（推荐，纯 cmd，无执行策略问题）：
cmd /c build.cmd

# 方式 B：build.ps1（内部同样调用 build.cmd；若策略禁止未签名脚本需加 Bypass）
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

等价的核心命令（`build.cmd` 内部执行的就是它）：

```bat
call "C:\develop\vs2022\community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\develop\vs2022\community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
"C:\...\cmake.exe" -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="C:\...\ninja.exe"
"C:\...\cmake.exe" --build build
```

产物：`build\minisearch.exe`（服务端）、`build\selftest.exe`（自检，48 项断言）。

> 注：`build.cmd` 里 `chcp 65001` 是刻意为之——MSVC 的 `/showIncludes` 输出 UTF-8 中文前缀，CMake 用非 UTF-8 代码页配置时 Ninja 无法解析依赖，会退化成每次都全量重编并刷屏 `注意: 包含文件:`。

本机实测 clean build（删掉 build 后）**6.7 秒**，13 个编译/链接步骤：

```
[9/13] Building CXX object CMakeFiles\selftest.dir\src\index.cpp.obj
[10/13] Building CXX object CMakeFiles\minisearch.dir\src\http.cpp.obj
[11/13] Building CXX object CMakeFiles\minisearch.dir\src\index.cpp.obj
[12/13] Linking CXX executable selftest.exe
[13/13] Linking CXX executable minisearch.exe
== build OK ==
```

### 1.2 启动

```powershell
cd C:\Users\Liyou\Documents\deepseek\replicas\04-minisearch
.\build\minisearch.exe --port 8204
```

启动时可选的预加载：

```powershell
# 启动即加载 1200 条 demo 语料（corpus/corpus.jsonl，已随仓库提供）
.\build\minisearch.exe --port 8204 --corpus corpus\corpus.jsonl

# 或在服务端确定性生成 N 条中文演示文档
.\build\minisearch.exe --port 8204 --seed 300
```

打开 **http://127.0.0.1:8204/** 即为检索台（搜索框、三种模式切换、高亮片段、score_breakdown 条形图、耗时/串行等效 QPS、索引统计面板、一键灌 300 条演示文档）。

Ctrl+C 退出（会停掉监听、释放 8204）。

### 1.3 灌数据 / 压测

```powershell
# 生成 5000 条压测语料（确定性，seed 固定）
python bench\gen_corpus.py --n 5000 --out bench\corpus_bench.jsonl --seed 7

# 通过 HTTP bulk 灌入（--reset 先清空；chunk 默认 1000 条/请求）
python bench\load_corpus.py --file bench\corpus_bench.jsonl --reset --chunk 5000

# 一条命令完成「灌 5000 条 + 1/8/32 并发 + 三模式」压测
python bench\bench.py --docs 5000 --duration 4 --concurrency 1,8,32 --modes bm25,vector,hybrid
```

压测结果写入 `bench/results.json`，表格同时打印到 stdout（§5.4 的表格就是该脚本的真实输出）。

### 1.4 手动验证 API

```powershell
# 健康检查 / 统计
curl "http://127.0.0.1:8204/api/health"
curl "http://127.0.0.1:8204/api/stats?pretty=1"

# 单条入库
curl -X POST "http://127.0.0.1:8204/api/docs" -H "Content-Type: application/json" ^
  -d "{\"id\":\"d1\",\"title\":\"倒排索引\",\"text\":\"倒排索引是全文检索的核心数据结构。\"}"

# 三种模式（中文需 URL 编码；?pretty=1 便于人读）
curl "http://127.0.0.1:8204/api/search?q=%E5%80%92%E6%8E%92%E7%B4%A2%E5%BC%95&k=10&mode=bm25&pretty=1"
curl "http://127.0.0.1:8204/api/search?q=%E5%90%91%E9%87%8F%E6%A3%80%E7%B4%A2&k=10&mode=vector&pretty=1"
curl "http://127.0.0.1:8204/api/search?q=%E5%B9%B6%E5%8F%91%20%E7%BC%93%E5%AD%98&k=10&mode=hybrid&pretty=1"
```

---

## 2. 目录与代码规模

| 文件 | 行数 | 说明 |
|---|---:|---|
| `src/json.h/.cpp` | 53 + 442 | 自写 JSON：对象/数组/字符串转义/`\uXXXX`(含代理对)/数字/布尔/null，解析 + 序列化（compact/pretty） |
| `src/text.h/.cpp` | 64 + 391 | UTF-8 编解码、CJK 判定、内置 290 词词典、最大匹配 + bigram + unigram 分词、HTML 安全高亮 |
| `src/postings.h` | 51 | VByte 编解码 + posting list 遍历（header-only） |
| `src/index.h/.cpp` | 142 + 546 | 文档表、压缩倒排索引、向量索引、BM25、RRF、搜索编排、统计、演示数据生成入口 |
| `src/http.h/.cpp` | 52 + 605 | winsock2 HTTP/1.1 服务、线程池、请求解析、路由、API JSON 渲染 |
| `src/web_ui.h` | 338 | 单页检索台（内嵌 HTML/CSS/JS，无 CDN） |
| `src/main.cpp` | 278 | 参数解析、语料预加载、Ctrl+C 处理、启动/关闭 |
| `src/seed.cpp` | 108 | 确定性中文演示文档生成器 |
| `tests/selftest.cpp` | 177 | JSON/VByte/分词/高亮/索引/搜索自检（48 项断言） |
| `bench/gen_corpus.py` | 140 | 确定性中文语料生成（24 主题池 + 每文档唯一编号） |
| `bench/load_corpus.py` | 84 | JSONL 语料通过 HTTP bulk 灌入 |
| `bench/bench.py` | 252 | 压测：灌数 + 1/8/32 并发 + 三模式 QPS/P50/P95/P99 |
| `corpus/corpus.jsonl` | 1200 行 | demo 语料（359,588 字符，均值 300 字符/篇） |
| `bench/samples/search_*.json` | 3 个 | 三模式真实返回（§5.3 的 JSON 即这些文件） |

纯 C++20 代码约 **3,150 行**（含测试 177 行）；Python 工具 ~476 行。

---

## 3. HTTP API

Base URL：`http://127.0.0.1:8204`。所有 JSON 响应 `Content-Type: application/json; charset=utf-8`；任意 GET JSON 接口支持 `?pretty=1` 格式化。错误统一 `{"error":"...","code":"..."}`。请求体上限 64 MiB，header 上限 256 KiB，socket 超时 15 s，每个连接处理一个请求后关闭（`Connection: close`）。

| 方法 | 路径 | 说明 | 参数 / Body |
|---|---|---|---|
| GET | `/` | 内嵌检索台 HTML | — |
| GET | `/api/health` | 健康检查 | — → `{status,version,docs,uptime_s}` |
| GET | `/api/stats` | 索引统计 | — → 见 §5.2 |
| GET | `/api/search` | 检索 | `q`(必填)、`k`(默认10，≤100)、`mode=bm25\|vector\|hybrid`(默认 bm25)、`k1`、`b`、`rrf_k`、`pretty` |
| POST | `/api/docs` | 单条入库 | `{id?,title?,text}`；`id` 重复返回 409；缺 `text` 400 |
| POST | `/api/docs/bulk` | 批量入库 | `[{id?,title?,text}, ...]` 或 `{"docs":[...]}`；单次 ≤ 200000 条；重复 id 跳过并计数 |
| POST | `/api/seed` | 服务端生成演示文档 | `?count=300` 或 body `{"count":300}`（≤50000） |
| POST | `/api/rebuild` | 全量重建索引 | — → `{ok,docs,terms,postings,build_ms}` |
| POST | `/api/reset` | 清空全部文档与索引 | — → `{ok,total_docs:0}` |
| OPTIONS | `*` | CORS 预检 | — → 204 |

状态码：`200` 成功、`204` 无内容（favicon/OPTIONS）、`400` 参数/JSON/文档非法、`404` 路由不存在、`405` 方法不支持、`409` 文档 id 重复。

`/api/search` 响应结构：

```json
{
  "query": "…", "mode": "bm25|vector|hybrid", "k": 10, "took_ms": 0.32,
  "matched_terms": 5, "total_candidates": 3631, "query_terms": ["倒排", "索引"],
  "hits": [{
    "doc_id": "gen-2311", "title": "…", "score": 9.286949, "rank": 1, "doc_len": 385,
    "score_breakdown": { "bm25": 9.286949, "idf_sum": 5.844895, "matched_terms": 5,
                         "doc_len": 385, "avg_doc_len": 414.5456 },
    "highlights": ["…<mark>索引</mark>构建阶段…"],
    "preview": "前 140 个字符纯文本预览"
  }]
}
```

三种模式的 `score_breakdown` 字段不同：

- **bm25**：`bm25`（总分）、`idf_sum`（命中词的 IDF 之和）、`matched_terms`、`doc_len`、`avg_doc_len`
- **vector**：`cosine`、`doc_len`、`vector_dim`
- **hybrid**：`rrf`（融合分）、`bm25`、`cosine`、`bm25_rank`、`vector_rank`（0 = 该路未进候选池）

---

## 4. 设计

### 4.1 HTTP 服务（`src/http.cpp`）

- winsock2：`WSAStartup` → `socket` → `SO_REUSEADDR` → `bind` → `listen(SOMAXCONN)`；主线程只做 `accept`，每个连接设置 `TCP_NODELAY` / 15 s `SO_RCVTIMEO`/`SO_SNDTIMEO` 后丢进 `queue<SOCKET>`。
- 固定线程池：`--threads` 指定，默认 `clamp(hardware_concurrency, 4, 32)`（本机 32 逻辑核 → 32 worker），worker 在 `condition_variable` 上取连接，处理完 `shutdown(SD_SEND)+closesocket`。
- **单请求单连接**（响应头 `Connection: close`）：小线程池 + 短连接不会出现 keep-alive 场景下「客户占满连接、其他连接排队」的饥饿；压测客户端也按同样模型测端到端延迟。
- 请求解析：请求行（method/target/version）→ header → `Content-Length` 循环 `recv`；percent-decode 支持 UTF-8（`q=%E5%80%92...`，`+` 视为空格）。
- 路由用前缀/精确匹配；静态页是编译进二进制的 `kWebUiHtml`，不读磁盘。
- JSON 响应由自写 serializer 生成，`?pretty=1` 输出 2 空格缩进。

### 4.2 自写 JSON（`src/json.cpp`）

`JsonValue` 为递归 variant：Null/Bool/Num/Str/Arr/Obj（对象保持插入顺序）。解析器递归下降、深度上限 128、错误信息定位；字符串支持 `\" \\ \/ \b \f \n \r \t` 与 `\uXXXX`（含 UTF-16 代理对 → UTF-8）；数字用 `std::from_chars`；序列化用 `std::to_chars` 最短往返表示，整数值不带小数点，控制字符转 `\u00XX`。

### 4.3 中文分词（`src/text.cpp`）

索引与查询使用同一套分析器，但视图不同：

**索引视图**（最大化召回）对每个连续 CJK 片段：
1. **词典最大匹配**：内置 290 个中文词条（最长 5 字），从当前位置尝试 `min(maxWordLen, 剩余)`→2 字，命中即切，未命中前进 1 字；
2. **字符 bigram**：片段内所有相邻二字组合（覆盖未登录词）；
3. **字符 unigram**：单字（保证单字查询可命中）。

ASCII 连续字母数字整体小写化为一个 term（长度 ≥2）。文档长度 `dl` = 上述 term 总数；标题参与分词（`title + ' ' + text`）。

**查询视图**：term = 最大匹配词 + bigram（去重），unigram 只作为兜底：若主 term 在索引中命中 0 条，自动用单字 term 重试一次（`matched_terms` 会随之更新）。

高亮：在原文（ASCII 小写化、字节偏移不变）上定位查询 surface 的所有出现，合并区间，围绕前 3 处各开 180 字节窗口（对齐 UTF-8 码点边界），转义 HTML 后把命中包进 `<mark>`；若正文无命中但标题有，退化为标题片段。

### 4.4 倒排索引 + VByte（`src/postings.h` + `src/index.cpp`）

- 词典：`unordered_map<string, TermEntry{df, offset, bytes}>`
- posting 数据：单一 `std::vector<uint8_t>` arena，每个 term 的列表按 **docID delta + tf** 交替写 VByte：

```
term "索引" df=1876 bytes=... → [Δdoc VByte][tf VByte] × 1876
```

- 构建时按 docID 升序遍历文档，delta 天然非负且通常很小（常见词平均 1 字节）；解码端 `for_each_posting` 顺序还原 docID 与 tf。
- 实测（5000 文档）：**1,437,715 条 posting 压缩后 2,895,896 字节 ≈ 2.01 B/posting**；若按 `(uint32 docID, uint32 tf)` 裸存需 8 B/posting，约 **4× 压缩**，且省掉每条 posting 的 8 B 指针/容器开销。
- 构建/重建是 O(总 token 数)：5000 文档实测 **245 ms**；`term → temp vector` 先按 docID 顺序写，最后一次性拼进 arena。

### 4.5 BM25（`src/index.cpp`）

```
idf(t) = ln(1 + (N - df + 0.5) / (df + 0.5))
score(q,d) = Σ_t idf(t) · tf(t,d)·(k1+1) / (tf(t,d) + k1·(1 - b + b·dl/avgdl))
```

- 默认 `k1=1.2, b=0.75`，可由启动参数 `--k1/--b` 或每次请求 `?k1=&b=` 覆盖（`/api/stats` 返回当前默认值）。
- 打分用 `std::vector<float>` 全量累加 + `touched` 列表，取 top-k 用 `nth_element` + 排序；同时累计 `idf_sum`、`matched_terms` 供 `score_breakdown` 使用。
- 当前实现没有块索引 / WAND / Block-Max 剪枝：每个查询 term 的 posting list 全量解码（5000 文档下单查询 p99 ≤ 0.48 ms，可接受）。

### 4.6 向量检索（`src/index.cpp`）

- **确定性 hashing 向量**：对文档 `title + ' ' + text` 转小写后按 Unicode 码点取特征：unigram 权重 0.5、bigram 权重 1.0；用 splitmix64 派生 `idx = hash % dim`、`sign = hash>>63 ? -1 : +1` 累加到 512 维向量，最后 L2 归一化。**不依赖随机种子/进程**，重启后同一文档向量不变。
- **查询**：同样抽稀疏特征并归一化，对全部文档做暴力余弦（稀疏点积）：5000 文档 × 查询约 10–40 个非零特征，单查询 p99 ≤ 1.2 ms。
- **内存**：`docs × dim × 4B`，5000 文档 = 10,240,000 B（约 10 MB）。
- 这条路径是「字面 n-gram 相似度」，不是语义 embedding；因此 vector 模式有意义的前提是查询与文档共享字符 n-gram，语义同义词召回需外部 embedding（见 TODO）。

### 4.7 混合检索（RRF）

```cpp
// pool = max(k*5, 50)
bm_list  = bm25 top-pool
vec_list = cosine top-pool
rrf[d] = Σ_{ranker∈{bm25,vector}} 1/(rrf_k + rank_ranker(d))   // 默认 rrf_k=60
```

对两路候选并集按 `rrf` 降序取前 k；两路互不依赖对方的原始分数量纲。某一路为空时另一路仍可返回结果（例如词汇不匹配时 cosine 仍能给出近似文档）。`score_breakdown` 同时给出两路原始分和名次，便于排查「谁把这条顶上来」。

### 4.8 并发模型

`SearchIndex` 用 `std::shared_mutex`：查询持 shared lock 并发执行；写入（`add_bulk`/`reset`/`rebuild`）持 unique lock。`dirty_` 为 atomic，写入后置位，下一次读（search/stats）在 unique lock 下做一次全量重建（`POST /api/docs*` 也会在返回前同步重建，响应里带回 `build_ms`）。批量写建议一次 bulk 提交，避免「每条都重建」。

---

## 5. 实测（全部为自己跑出来的真实数字）

### 5.1 环境

| 项 | 值 |
|---|---|
| OS | Windows 11（本机） |
| CPU | 32 逻辑处理器（`NUMBER_OF_PROCESSORS=32`） |
| 编译器 | MSVC 19.43.34808（VS 2022 Community），`-std:c++20 /O2 /utf-8` |
| 构建 | CMake 3.30.5 + Ninja，Release，clean build 6.7 s |
| 服务线程 | 默认 32 worker |
| 压测客户端 | 同机 Python 3.12（`http.client`，每请求新建连接，与服务端短连接模型一致） |

### 5.2 灌 5000+ 文档（bulk）与 stats

`python bench\bench.py --load-only --docs 5000` 的真实输出：

```
== target http://127.0.0.1:8204 ==
server: minisearch/1.0.0 docs=0 uptime=0.9s
generated 5000 docs (301 chars/doc, seed=7) in 129.0 ms
loaded: inserted=5000 wall=303.3 ms sum_build=245.2 ms
index: docs=5000 terms=12119 postings=1437715 bytes=17.42 MiB build_ms=245.2 avg_len=414.5
```

> 5000 条文档通过 **一次** `POST /api/docs/bulk` 灌入（HTTP body 约 2.5 MB）；`wall=303.3 ms` 含 JSON 解析、入库、全量重建索引（其中重建 `build_ms=245.2 ms`）。

`GET /api/stats?pretty=1` 的真实返回：

```json
{
  "docs": 5000,
  "terms": 12119,
  "postings": 1437715,
  "index_bytes": 18270112,
  "postings_bytes": 2895896,
  "term_bytes": 739125,
  "vector_bytes": 10240000,
  "doc_bytes": 4395091,
  "build_ms": 245.2314,
  "avg_doc_len": 414.5456,
  "vector_dim": 512,
  "queries": 0,
  "uptime_s": 7.2584417,
  "k1": 1.2,
  "b": 0.75,
  "rrf_k": 60
}
```

数字合理性：`index_bytes = postings_bytes + term_bytes + vector_bytes + doc_bytes`（2,895,896 + 739,125 + 10,240,000 + 4,395,091 = 18,270,112，逐字节吻合）；`postings / docs ≈ 288`（每篇 ~415 token，词表 12,119 项，重复词命中使 posting 数低于 token 总数）；`avg_doc_len=414.5` 与语料均值 301 汉字 + 标题/分词膨胀一致。

### 5.3 三种模式各搜一次（真实 JSON）

以下为当前进程在 5000 文档状态下的真实响应（完整文件见 `bench/samples/`）。

**bm25**：`GET /api/search?q=倒排索引 VByte 压缩&k=3&mode=bm25&pretty=1`

```json
{
  "query": "倒排索引 VByte 压缩",
  "mode": "bm25",
  "k": 3,
  "took_ms": 0.3242,
  "matched_terms": 5,
  "total_candidates": 3631,
  "query_terms": ["倒排", "索引", "排索", "vbyte", "压缩"],
  "hits": [
    {
      "doc_id": "gen-2311",
      "title": "倒排索引入门与压测记录（2311）",
      "score": 9.286949,
      "rank": 1,
      "doc_len": 385,
      "score_breakdown": {
        "bm25": 9.286949,
        "idf_sum": 5.844895,
        "matched_terms": 5,
        "doc_len": 385,
        "avg_doc_len": 414.5456
      },
      "highlights": [
        "<mark>索引</mark>构建阶段会对文档做分词与<mark>倒排压缩</mark>，然后用 <mark>VByte</mark> <mark>压缩倒排</mark>表以减少内存占用。 线上观察到同义词漏召回，通过停用词过滤与缓存预…",
        "…可接受范围。 针对排序抖动，团队引入<mark>VByte</mark> 编码，并配合<mark>VByte</mark> 编码做兜底，整体表现更加稳定。 针对锁竞争，团队引入最大匹配，并配合…"
      ],
      "preview": "索引构建阶段会对文档做分词与倒排压缩，然后用 VByte 压缩倒排表以减少内存占用。 线上观察到同义词漏召回，通过停用词过滤与缓存预热，最终把错误率稳定在可接受范围。…"
    },
    { "doc_id": "gen-2104", "score": 9.043204, "rank": 2, "title": "倒排索引设计取舍（2104）",
      "score_breakdown": { "bm25": 9.043204, "idf_sum": 5.844895, "matched_terms": 5, "doc_len": 412, "avg_doc_len": 414.5456 },
      "highlights": ["…<mark>索引</mark>构建阶段会对文档做分词与异步刷盘，然后用 <mark>VByte</mark> <mark>压缩倒排</mark>表以减少内存占用。…"] },
    { "doc_id": "gen-4089", "score": 9.001041, "rank": 3, "title": "一次倒排索引故障复盘（4089）",
      "score_breakdown": { "bm25": 9.001041, "idf_sum": 5.844895, "matched_terms": 5, "doc_len": 325, "avg_doc_len": 414.5456 },
      "highlights": ["…文档编号 T4YQVE，主题为<mark>倒排索引</mark>，由基础设施组维护。"] }
  ]
}
```

**vector**：`GET /api/search?q=向量检索 余弦相似度 混合重排&k=3&mode=vector&pretty=1`

```json
{
  "query": "向量检索 余弦相似度 混合重排",
  "mode": "vector",
  "k": 3,
  "took_ms": 0.7895,
  "matched_terms": 0,
  "total_candidates": 4291,
  "query_terms": ["向量", "检索", "量检", "余弦", "相似度", "弦相", "相似", "似度", "重排", "混合", "合重"],
  "hits": [
    {
      "doc_id": "gen-4772",
      "title": "向量检索：从原理到线上优化（4772）",
      "score": 0.444147,
      "rank": 1,
      "doc_len": 279,
      "score_breakdown": { "cosine": 0.444147, "doc_len": 279, "vector_dim": 512 },
      "highlights": [
        "…re_breakdown 字段。 经过三轮优化，全文<mark>检索</mark>下的缓存命中率从 64 毫秒 下降到 64 毫秒。 <mark>向量检索</mark>使用 512 维哈希特征，通过<mark>余弦相似度</mark>计算…",
        "…。 <mark>向量检索</mark>使用 512 维哈希特征，通过<mark>余弦相似度</mark>计算得分，再用 RRF 与 BM25 融合排序。 文档编号 XUSBY6，主题为<mark>向量检索</mark>，由架构组维护…"
      ],
      "preview": "接口协议使用 HTTP/1.1 短连接，每个请求返回 JSON，包含 score_breakdown 字段。…"
    },
    { "doc_id": "gen-1962", "score": 0.442095, "rank": 2, "title": "关于监控告警的实践笔记（1962）",
      "score_breakdown": { "cosine": 0.442095, "doc_len": 363, "vector_dim": 512 },
      "highlights": ["<mark>向量检索</mark>使用 512 维哈希特征，通过<mark>余弦相似度</mark>计算得分，再用 RRF 与 BM25 融合排序。…"] },
    { "doc_id": "gen-4049", "score": 0.431825, "rank": 3, "title": "一次向量检索故障复盘（4049）",
      "score_breakdown": { "cosine": 0.431825, "doc_len": 401, "vector_dim": 512 },
      "highlights": ["…<mark>向量检索</mark>使用 512 维哈希…"] }
  ]
}
```

**hybrid**：`GET /api/search?q=并发 缓存 线程池 P99&k=3&mode=hybrid&pretty=1`

```json
{
  "query": "并发 缓存 线程池 P99",
  "mode": "hybrid",
  "k": 3,
  "took_ms": 0.6929,
  "matched_terms": 6,
  "total_candidates": 84,
  "query_terms": ["并发", "缓存", "线程池", "线程", "程池", "p99"],
  "hits": [
    {
      "doc_id": "gen-2071",
      "title": "缓存系统性能调优清单（2071）",
      "score": 0.032002,
      "rank": 1,
      "doc_len": 258,
      "score_breakdown": { "rrf": 0.032002, "bm25": 9.320768, "cosine": 0.356926, "bm25_rank": 2, "vector_rank": 3 },
      "highlights": [
        "日志显示查询<mark>并发</mark>达到 1024 时，<mark>线程池</mark>队列开始堆积，需要调整 worker 数量。 在压测中，系统在 32 <mark>并发</mark>下 QPS 约为 2600，平均响应时间 保…",
        "…在 2600 MB 以内。 服务网格中的网关会把<mark>P99</mark> 延迟写入 Prometheus，再通过告警规则触发扩容。 经过三轮优化，故障复盘下的<mark>P99</mark> 延迟从 1200 微秒…"
      ],
      "preview": "日志显示查询并发达到 1024 时，线程池队列开始堆积，需要调整 worker 数量。…"
    },
    { "doc_id": "gen-2946", "score": 0.030331, "rank": 2, "title": "容器调度：从原理到线上优化（2946）",
      "score_breakdown": { "rrf": 0.030331, "bm25": 8.705961, "cosine": 0.318032, "bm25_rank": 4, "vector_rank": 8 },
      "highlights": ["线上观察到锁竞争，通过一致性哈希与<mark>缓存</mark>预热，最终把QPS稳定在可接受范围。…"] },
    { "doc_id": "gen-1224", "score": 0.02681, "rank": 3, "title": "特征工程：从原理到线上优化（1224）",
      "score_breakdown": { "rrf": 0.02681, "bm25": 7.605366, "cosine": 0.367291, "bm25_rank": 36, "vector_rank": 1 },
      "highlights": ["…日志显示查询<mark>并发</mark>达到 64 时，<mark>线程池</mark>队列开始堆积，需要调整 w…"] }
  ]
}
```

> 上面每条 `score_breakdown` 都是引擎实际计算值；第三条 hybrid 命中 `gen-1224` 的 `bm25_rank=36` 但 `vector_rank=1`，正是 RRF 把「语义相似但词面排名靠后」的文档提上来的例子。

### 5.4 压测结果（1/8/32 并发 × 三模式，真实输出）

命令：`python bench\bench.py --docs 5000 --duration 4 --concurrency 1,8,32 --modes bm25,vector,hybrid`（每格测 4 秒，`k=10`，20 条查询轮换，0 error）

| mode | conc | requests | QPS | client P50 | client P95 | client P99 | server P50 | server P99 | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| bm25 | 1 | 379 | 94.5 | 14.64 ms | 16.05 ms | 16.42 ms | 0.20 ms | 0.48 ms | 0 |
| bm25 | 8 | 9858 | 2450.9 | 3.06 ms | 4.69 ms | 6.84 ms | 0.16 ms | 0.40 ms | 0 |
| bm25 | 32 | 9774 | 2436.8 | 12.61 ms | 20.68 ms | 25.06 ms | 0.17 ms | 0.40 ms | 0 |
| vector | 1 | 337 | 84.0 | 14.84 ms | 16.45 ms | 24.90 ms | 0.54 ms | 1.20 ms | 0 |
| vector | 8 | 9662 | 2413.8 | 3.06 ms | 4.65 ms | 11.64 ms | 0.34 ms | 0.76 ms | 0 |
| vector | 32 | 9875 | 2462.9 | 12.46 ms | 20.58 ms | 25.30 ms | 0.34 ms | 0.78 ms | 0 |
| hybrid | 1 | 339 | 84.7 | 14.87 ms | 16.23 ms | 16.68 ms | 0.64 ms | 0.99 ms | 0 |
| hybrid | 8 | 8499 | 2122.8 | 3.48 ms | 5.46 ms | 13.52 ms | 0.46 ms | 0.95 ms | 0 |
| hybrid | 32 | 8278 | 2064.4 | 15.02 ms | 24.17 ms | 28.73 ms | 0.45 ms | 0.93 ms | 0 |

- `QPS` = 客户端完成请求数 / 墙钟时间；`client Pxx` = 端到端（含 TCP connect）延迟；`server Pxx` = 引擎在响应里返回的 `took_ms`。
- 全 9 格共处理 59,264 次查询，服务器查询计数器增量换算为 **1,579.8 queries/s**（含并发=1 的低速格子）。

**为什么并发=1 只有 ~90 QPS，而服务端只要 0.2 ms？**
本机 Windows loopback 上短连接 `connect()` 呈双峰：约一半 0.3–0.5 ms、另一半约 15 ms（15 ms 与 Windows 定时器 tick 接近）。用一个纯 Python `listen/accept` 的空服务复测，同样出现 0.4 ms / 15 ms 双峰，因此这是**本机环境现象，不是 minisearch 服务端耗时**：并发 1 时 QPS 被 connect 抖动卡住；并发 8/32 时连接建立被并行化，QPS 稳定在 ~2,100–2,460。服务端自身耗时 `server P50 0.16–0.64 ms / P99 0.40–1.20 ms` 才是引擎真实水平。

复现：`bench/results.json`（本次完整结果）、`bench/load_results.json`（灌数结果）。

---

## 6. 与 zvec / manticore 的差异

先说明：本项目是**教学/验证型 MVP**，没有和它们做同机对比跑分，下面的差异是架构层面的。

[zvec](https://github.com/alibaba/zvec) 是阿里开源的**嵌入式向量数据库**，v0.5.0 起把原生全文检索做进数据库（[官方博客](https://zvec.org/en/blog/2026-07-07-zvec-fts/)）：Segment 内同时承载正排、向量索引、标量倒排与 FTS 索引；写入期用 Roaring Bitmap + RocksDB MergeOperator 降低写放大，持久化时转成 BitPacked posting（把 docID、tf、文档长度、block max score 打包），查询用 BM25 + WAND/Block-Max WAND 剪枝；中文用 Jieba 分词（据该博客）；FTS 与向量多路召回后用 RRF 融合。

[Manticore Search](https://manticoresearch.com/) 是成熟的 C++ 全文搜索引擎（Sphinx 分支）：磁盘倒排段 + RT 索引、SQL/HTTP 协议、丰富分词器、属性过滤与聚合、集群/复制等生产特性。

| 维度 | MiniSearch（本项目） | zvec | Manticore Search |
|---|---|---|---|
| 定位 | 单文件可跑的检索 MVP + 教学代码 | 嵌入式向量库，FTS 是库内一等公民 | 生产级全文搜索服务器 |
| 中文分词 | 内置 290 词词典最大匹配 + bigram + unigram | Jieba（官方博客口径） | 可配置分词器/插件 |
| 倒排编码 | 内存 arena，ΔdocID+tf 双 VByte，~2.0 B/posting | 写 Roaring、读 BitPacked（含 block max score） | 磁盘压缩倒排段 |
| 打分/剪枝 | BM25(k1/b 可配)，无 WAND；全量解码 posting | BM25 + WAND/Block-Max WAND | BM25 等多种 + 生产级优化 |
| 向量检索 | 512 维确定性 hashing 特征 + 暴力余弦（O(N)） | 原生向量索引（ANN，Segment 管理） | 支持向量检索（按版本/部署） |
| 混合 | 自写 RRF（k 可配） | 多路召回 + RRF/加权重排 | 可用多查询/脚本组合 |
| 持久化 | **无**（纯内存，重启丢失） | Segment/RocksDB 持久化、恢复、compaction | 磁盘索引 + RT 索引落盘 |
| 删除/更新 | 仅整体 reset | delete bitmap + compaction 过滤 | 支持按文档更新/删除 |
| 协议 | 自写 HTTP/1.1 + 自定义 JSON API | 嵌入式库 API（Python 等） | SQL / HTTP / MySQL 协议等 |
| 依赖 | 仅 `ws2_32` | RocksDB 等 | 自身依赖较多 |
| 分布式 | 无（单进程） | 嵌入式（无独立服务） | 支持分布式部署（视版本） |

一句话：zvec/manticore 解决的是「持久化、段合并、ANN、剪枝、生产运维」；本项目把「倒排 + VByte + BM25 + 向量 + RRF + HTTP + Web 台 + 压测」这条链路完整手写了一遍并留下真实数字，规模与可靠性不在一个量级。

---

## 7. 已知限制与 TODO

**未实现（按优先级）**

1. **无落盘**：无 WAL/快照/段文件，进程退出即丢；`POST /api/rebuild` 只是内存重建。→ TODO：mmap 倒排段 + 启动加载。
2. **无 HNSW/IVF/PQ**：向量是暴力 O(N·d)，5000 条 p99 ~1 ms，但 100 万条会线性变慢。→ TODO：HNSW 或 IVF-Flat + 量化。
3. **无块索引 / WAND / Block-Max 剪枝**：长 posting 全量解码。→ TODO：每 128 篇存 block max score 做剪枝。
4. **写入即全量重建**（O(index)，5000 文档 245 ms）：无增量段/段合并，批量写必须一次 bulk 提交。→ TODO：内存段 + 不可变段 + merge。
5. **单文档更新/删除不支持**（只有 `/api/reset` 全清）：无 delete bitmap。
6. **词典固定**：内置静态词表，不支持上传自定义词典/jieba；未做同义词、停用词表（语料里部分模板词会作为普通 term 参与打分）。
7. **无短语/布尔查询**：无位置索引，只有 bag-of-ngrams，`"a b"` 顺序无法约束；高亮是子串匹配。
8. **无 keep-alive/HTTP2/TLS/认证/限流**：短连接模型；无鉴权，只应绑定 127.0.0.1（`--host 0.0.0.0` 暴露前请自行加防护）。
9. **单进程**：无分片/副本/分布式；无多索引/多租户隔离。
10. **中文语义召回弱**：hashing 向量只表达字面 n-gram，不是 embedding；同义改写（如「缓存」vs「高速缓冲」）不能召回。→ TODO：可插拔 embedding 向量 + 外部模型。
11. **前端未做移动端深度适配**（基础响应式可用）。

**已知取舍**

- 分词把标题并入正文，标题权重与正文相同（没有字段权重）。
- BM25 打分是 float 累加，极端大 index 可能有精度漂移（演示规模无影响）。
- `postings_bytes` 只统计 arena，不含 term 词典（`term_bytes` 单独列出）。

---

## 8. 复现清单（从零到数字）

```powershell
cd C:\Users\Liyou\Documents\deepseek\replicas\04-minisearch

# 1) 构建
cmd /c build.cmd
.\build\selftest.exe          # 48 项自检，输出 all self tests passed

# 2) 启动（另开一个终端，前台运行；Ctrl+C 停）
.\build\minisearch.exe --port 8204

# 3) 灌 1200 条 demo 语料（或用检索台上「灌入 300 演示文档」按钮）
python bench\load_corpus.py --file corpus\corpus.jsonl --reset --chunk 500

# 4) 三种模式各搜一次
curl "http://127.0.0.1:8204/api/search?q=%E5%80%92%E6%8E%92%E7%B4%A2%E5%BC%95&k=3&mode=bm25&pretty=1"
curl "http://127.0.0.1:8204/api/search?q=%E5%90%91%E9%87%8F%E6%A3%80%E7%B4%A2&k=3&mode=vector&pretty=1"
curl "http://127.0.0.1:8204/api/search?q=%E5%B9%B6%E5%8F%91%20%E7%BC%93%E5%AD%98&k=3&mode=hybrid&pretty=1"

# 5) 压测（会 reset 索引并灌 5000 条生成语料，跑 9 个格子约 40 s）
python bench\bench.py --docs 5000 --duration 4 --concurrency 1,8,32 --modes bm25,vector,hybrid

# 6) 结束：在服务终端按 Ctrl+C（或 Stop-Process 结束 minisearch.exe），确认 8204 已释放
```

---

*本项目所有代码/语料均在本仓库内，未改动工作区其他目录。*
