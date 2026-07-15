# Related work notes (issue #8)

Key papers for the thesis, what each contributes, and how it maps to this
implementation. (Verify the exact venues/years against the PDFs before
citing in the thesis text.)

## 1. Pugh — Skip Lists: A Probabilistic Alternative to Balanced Trees
*Communications of the ACM, 33(6), 1990.*

The original skip list. Defines the probabilistic tower construction
(each node promoted to the next level with probability p) and the
expected O(log n) search/insert/delete bounds.

**Maps to:** `skiplist.c` (the classic baseline) and the tower-height
scheme now used for whole blocks in `cskiplist.c` (`random_height()`,
`splice_block()`). The thesis story: Pugh's structure is pointer-chasing
and cache-hostile; ours replaces nodes with cache-sized blocks.

## 2. Khuong & Morin — Array Layouts for Comparison-Based Searching
*ACM Journal of Experimental Algorithmics 22, 2017. arXiv:1509.05053.*

THE core reference (the paper the supervisor attached). Compares sorted,
Eytzinger, (B-)tree and van Emde Boas layouts of a static array and shows
the Eytzinger layout + branchless search + prefetching is usually fastest
on modern CPUs, despite touching slightly more cache lines — because the
access pattern is predictable and prefetchable.

**Maps to:** `blk_sorted_to_eytzinger()` / `blk_eytzinger_search()` in
`cskiplist.c`, the branchless lower-bound loop in `blk_binary_search()`,
and the `array-eyt` / `array-bl` baselines in `test-experiment.c`.

## 3. Sprenger, Zeuch & Leser — Cache-Sensitive Skip List (CSSL)
*IMDM workshop @ VLDB 2016 ("Cache-Sensitive Skip List: Efficient Range
Queries on modern CPUs").*

The closest published relative of this thesis' structure: a skip list
whose "fast lanes" are linearized into contiguous arrays traversed with
SIMD, targeting in-memory range queries. Shows large speedups over the
classic skip list and competitive results against ART/B+-trees for range
workloads.

**Maps to:** the block skip list concept itself (contiguous key arrays +
skip structure above) and the SSE2 scan in `blk_binary_search()`.
Difference to highlight in the thesis: CSSL linearizes the *lanes*, we
block the *data level* and make each block internally cache-friendly
(Eytzinger); our structure also supports incremental inserts without a
full rebuild.

## 4. Rao & Ross — Cache Conscious Indexing (CSS-trees, CSB+-trees)
*"Cache Conscious Indexing for Decision-Support in Main Memory", VLDB
1999 (CSS-trees); "Making B+-Trees Cache Conscious in Main Memory",
SIGMOD 2000 (CSB+-trees).*

The classic argument that in-memory index nodes should be sized to cache
lines/blocks and that pointer elimination (computing child positions
instead of storing pointers) buys large speedups.

**Maps to:** block sizing against L1/L2 (`csl_choose_block_cap_for_level`,
the block-cap sweep experiment) and the implicit-tree navigation of the
Eytzinger layout (children at 2k+1/2k+2 — no pointers).

## Where this thesis sits

Static sorted array (fastest search, no inserts)
→ Khuong-Morin layouts make it cache-optimal.
Classic skip list (dynamic, cache-hostile)
→ Pugh.
**This thesis: block skip list = dynamic structure whose per-block search
is Khuong-Morin-optimal**, with CSSL and CSB+ as the closest published
designs to compare and contrast against.

Experimental evidence for the positioning is in `results/all-results.csv`
(see EXPERIMENTS.md §4 for the graphs to draw).
