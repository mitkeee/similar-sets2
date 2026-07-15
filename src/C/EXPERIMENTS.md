# Experiments guide (master thesis)

This document explains what is implemented, how it maps to the supervisor's
requirements from the meetings, how to run the experiments, and which graphs
to produce from the data.

## 1. What the data structure is

`cskiplist` is a **block skip list**: keys (+ value pointers) live in sorted
arrays ("blocks"); a skip list is built **on top of the blocks**, so a search
first skips from block to block in O(log n_blocks) and then searches inside
one cache-resident block.

Per-block search has three regimes (see `blk_binary_search` /
`blk_eytzinger_search` in `cskiplist.c`):

1. **SIMD linear scan** (SSE2, 4 keys per instruction) for blocks with
   ≤ 32 items — no branch mispredictions, sequential access.
2. **Branchless binary search** (CMOV-based lower bound, Khuong & Morin §3.1)
   for larger sorted blocks.
3. **Eytzinger layout** (heap-ordered / BFS layout of the sorted array,
   Khuong & Morin, "Array Layouts for Comparison-Based Searching",
   https://arxiv.org/abs/1509.05053) with a branchless descent and explicit
   prefetch — enabled per skip list with `csl_set_eytzinger(sl, 1)`.

Key properties matching the supervisor's requirements:

* **Block size is a runtime parameter stored in the structure**
  (`csl_create_with_block_cap(cap)`, first parameter of the skip list —
  no recompilation needed for block-size experiments).
* **Skip pointers are maintained incrementally on insert/delete**
  (new blocks get probabilistic tower heights and are spliced into all
  levels, like a classic skip list whose nodes are blocks). Searches are
  O(log n) at all times — no rebuild required. `csl_rebuild_skips()` is
  still available to produce perfectly balanced deterministic skips after
  a bulk load.
* **Keys carry values** (`csl_kv = {int key, void* val}`) — the value is the
  child-pointer slot needed when a skip list becomes the node structure of
  the set-trie (`connector_csl.c` adapts it to the existing set-trie code).
* Per-trie-level block sizing heuristic: `csl_create_for_level(level)`
  (large root blocks, small leaf blocks).

## 2. The experiment driver

`test-experiment.c` → build with `make experiment` or:

```
gcc -O3 -msse2 -o experiment cskiplist.c skiplist.c test-experiment.c -lpsapi
```

One binary compares six structures on **the identical query sequence**:

| structure  | layout | description                                        |
|------------|--------|----------------------------------------------------|
| `array`    | sorted | plain sorted array, classic branchy binary search  |
| `array-bl` | sorted | plain sorted array, branchless binary search       |
| `array-eyt`| eyt    | whole array in Eytzinger layout, branchless        |
| `skiplist` | nodes  | classic probabilistic skip list (one node per key) |
| `csl`      | sorted | block skip list, sorted blocks (swept over caps)   |
| `csl-eyt`  | eyt    | block skip list, Eytzinger blocks (swept over caps)|

The plain-array rows answer the supervisor's question *"one possible result
is that the simple array representation without skip list is faster"* —
they are the strongest static baseline.

Options:

```
experiment [-m search|insert] [-n keys] [-q queries] [-b cap,cap,...]
           [-r reps] [-s seed] [-H hit_pct] [-d uniform|dense|cluster]
           [-f keyfile] [-o outdir]
```

* Queries are drawn per the supervisor's spec: random keys between the data
  minimum and maximum with `-H` percent hits (default **50% hit / 50% miss**).
* Distributions: `uniform` (random half of a 2N universe), `dense`
  (even keys present, odd keys absent), `cluster` (runs of consecutive keys
  separated by random gaps — models prefix-heavy set-trie key streams).
* Every structure must report the **same hit count** ("results have to be
  identical") — the run fails loudly otherwise (`[VERIFY]` line, exit code).
* `-f file` loads a real data set (whitespace-separated integers, e.g. one
  line of a `.mapd.sorted` file) instead of generating keys — use this when
  the supervisor provides his set data sets.
* `-m insert` benchmarks **random-order insertion** (exercises incremental
  skip maintenance and block splitting), then verifies by searching.

Output: one CSV per run in `results/`, with **all parameters encoded in the
file name** (the supervisor's advice about organizing experiment data):

```
results/exp_search_uniform_n1000000_q500000_hit50_seed42.csv
```

CSV columns:

| column        | meaning                                                  |
|---------------|----------------------------------------------------------|
| `structure`   | one of the six names above                               |
| `layout`      | sorted / eyt / nodes                                     |
| `block_cap`   | block capacity (0 = not applicable)                      |
| `n`, `q`      | number of keys / queries                                 |
| `dist`        | key distribution (uniform / dense / file)                |
| `hit_pct`     | requested hit percentage                                 |
| `seed`, `rep` | RNG seed and repetition index (rows are raw, not averaged) |
| `build_ms`    | bulk-load time (sorted appends, or inserts in insert mode) |
| `prep_ms`     | `csl_rebuild_skips` + Eytzinger conversion time          |
| `search_ns`   | average wall-clock ns per query in this repetition       |
| `insert_ns`   | ns per random-order insert (insert mode only)            |
| `hits` / `expected_hits` | must match — correctness cross-check          |
| `mem_bytes`, `bytes_per_key` | exact structural memory (counted, not RSS) |

## 3. Running the full matrix

```
.\run-experiments.ps1          # full matrix
.\run-experiments.ps1 -Quick   # reduced matrix
```

This produces:

1. **Search sweep** — n ∈ {10K, 100K, 1M, 4M} (data spans L2 → L3 → DRAM),
   block caps 8…4096, 3 repetitions.
2. **Tiny-set regime** — n ∈ {8, 32, 256}: the set-trie case where most
   nodes hold only a handful of keys and every nanosecond is multiplied by
   millions of nodes.
3. **Insert benchmark** — random-order inserts with incremental skips.

All CSVs are merged into `results/all-results.csv` (one header) for pandas.

Ready-made plots: `python plot-results.py` reads `results/all-results.csv`
and writes PNGs (block-cap sweep per n, size sweep, memory, inserts) to
`results/plots/`.

## 4. Graphs for the thesis

From `all-results.csv` (group by structure/layout, average `search_ns`
over `rep`, or show min–max as error bars):

1. **Search latency vs block capacity** (x = `block_cap` log scale,
   y = `search_ns`, one line per n, structures `csl` / `csl-eyt`)
   → shows the optimal block size and how it depends on data-set size;
   relate the optimum to L1/L2 sizes.
2. **Search latency vs data-set size** (x = n log scale, y = `search_ns`;
   lines: best-cap `csl`, best-cap `csl-eyt`, `array-bl`, `array-eyt`,
   `skiplist`) → shows cache-level transitions and where the block skip
   list beats the classic skip list / plain array.
3. **Eytzinger vs sorted blocks** (ratio of `csl-eyt` to `csl` per cap/n)
   → isolates the layout effect the whole thesis is about.
4. **Memory** (`bytes_per_key` per structure) → block structure ≈ 8–9 B/key
   vs classic skip list ≈ 17+ B/key.
5. **Insert throughput** (`insert_ns` vs `block_cap`, plus `skiplist`
   baseline) → cost of keeping blocks sorted + incremental skip splicing.
6. **Hit vs miss latency** — rerun with `-H 100` and `-H 0`, same seed, and
   compare `search_ns` (misses traverse the full search path; interesting
   for the set-trie, where most child lookups miss).

Suggested pandas starter:

```python
import pandas as pd
df = pd.read_csv("results/all-results.csv")
g = (df.groupby(["structure","layout","block_cap","n"])
       .search_ns.agg(["mean","min","std"]).reset_index())
```

## 5. Correctness / regression tests

| binary               | what it checks                                        |
|----------------------|-------------------------------------------------------|
| `cskiptest`          | basic insert/search                                   |
| `cskiptest-enh`      | random ops, runtime block caps, per-level caps        |
| `cskiptest-million`  | 100K–2M keys: insert/search/delete/iterate/update     |
| `eyttest`            | Eytzinger conversion, search, seek, iterate + caches  |
| `skiptest`           | classic skip list baseline                            |
| `testproc` vs `testproc-base` | set-trie similarity search (Hamming or LCS: `testproc data test lcs SKP ADD`): cskiplist connector must produce identical results to the original array connector |
| `conntest-base` vs `conntest-csl` | connector API conformance: same canonical trace through the original array connector and the cskiplist adapter — outputs must be byte-identical |
| `set2` vs `set2-csl`  | the professor's original program built with each connector (A/B build switch); outputs identical modulo timing lines |
| `experiment` `[VERIFY]` | all six structures agree on every query            |

## 6. Notes / history

* 2026-07: fixed a correctness bug in the SIMD block search (wrong insertion
  position when a missing key fell inside a 4-key SIMD group → corrupted
  block order on random inserts). Added incremental skip maintenance
  (probabilistic block towers), O(1) tail appends, duplicate handling at
  block boundaries, and this experiment harness.
* Deferred (documented for the thesis "future work" chapter): TLB-aware
  block counting (radix-join style), manual vectorization of block search
  beyond the simple SSE2 scan, delta/key compression inside blocks.
