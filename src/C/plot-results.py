#!/usr/bin/env python3
"""plot-results.py — generate thesis graphs from results/all-results.csv.

Usage:
    python plot-results.py [results/all-results.csv] [output-dir]

Defaults: reads results/all-results.csv, writes PNGs to results/plots/.
Produces:
    search_vs_blockcap_n<N>.png   one per data-set size (csl vs csl-eyt,
                                  with array/skiplist baselines as lines)
    search_vs_n.png               best-cap block skiplist vs baselines
    memory_bytes_per_key.png      structural memory comparison
    insert_ns.png                 random-order insert benchmark (if present)
"""
import csv
import os
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib is required: pip install matplotlib")

CSV_PATH = sys.argv[1] if len(sys.argv) > 1 else os.path.join("results", "all-results.csv")
OUT_DIR = sys.argv[2] if len(sys.argv) > 2 else os.path.join("results", "plots")
os.makedirs(OUT_DIR, exist_ok=True)

# utf-8-sig: PowerShell writes a BOM
with open(CSV_PATH, encoding="utf-8-sig") as f:
    rows = list(csv.DictReader(f))

for r in rows:
    r["n"] = int(r["n"])
    r["block_cap"] = int(r["block_cap"])
    r["search_ns"] = float(r["search_ns"])
    r["insert_ns"] = float(r["insert_ns"])
    r["bytes_per_key"] = float(r["bytes_per_key"])

def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")

def agg_search(structure, n, cap=None):
    vals = [r["search_ns"] for r in rows
            if r["structure"] == structure and r["n"] == n
            and (cap is None or r["block_cap"] == cap)
            and r["insert_ns"] == 0.0]
    return mean(vals)

Ns = sorted({r["n"] for r in rows if r["insert_ns"] == 0.0})
CAPS = sorted({r["block_cap"] for r in rows if r["block_cap"] > 0})
BASELINES = [("array", "tab:gray"), ("array-bl", "tab:green"),
             ("array-eyt", "tab:olive"), ("skiplist", "tab:red")]

# ---- 1. search_ns vs block_cap, one figure per n ----
for n in Ns:
    caps = [c for c in CAPS
            if any(r["structure"] == "csl" and r["n"] == n and r["block_cap"] == c
                   for r in rows)]
    if not caps:
        continue
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for st, color in [("csl", "tab:blue"), ("csl-eyt", "tab:orange")]:
        ys = [agg_search(st, n, c) for c in caps]
        ax.plot(caps, ys, "o-", color=color, label=st)
    for st, color in BASELINES:
        v = agg_search(st, n)
        if v == v:  # not NaN
            ax.axhline(v, color=color, linestyle="--", linewidth=1, label=st)
    ax.set_xscale("log", base=2)
    ax.set_xlabel("block capacity (items)")
    ax.set_ylabel("search ns/query")
    ax.set_title(f"Search latency vs block size (n={n:,}, 50% hits)")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, f"search_vs_blockcap_n{n}.png"), dpi=150)
    plt.close(fig)

# ---- 2. search_ns vs n (best cap per structure) ----
big_ns = [n for n in Ns if n >= 1000]
if len(big_ns) >= 2:
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for st, color in [("csl", "tab:blue"), ("csl-eyt", "tab:orange")]:
        ys = []
        for n in big_ns:
            vals = [agg_search(st, n, c) for c in CAPS]
            vals = [v for v in vals if v == v]
            ys.append(min(vals) if vals else float("nan"))
        ax.plot(big_ns, ys, "o-", color=color, label=f"{st} (best cap)")
    for st, color in BASELINES:
        ys = [agg_search(st, n) for n in big_ns]
        ax.plot(big_ns, ys, "s--", color=color, label=st, linewidth=1)
    ax.set_xscale("log")
    ax.set_xlabel("keys in structure (n)")
    ax.set_ylabel("search ns/query")
    ax.set_title("Search latency vs data-set size (50% hits)")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "search_vs_n.png"), dpi=150)
    plt.close(fig)

# ---- 3. memory bytes/key at the largest n ----
if Ns:
    n = max(Ns)
    mems = {}
    for r in rows:
        if r["n"] == n and r["insert_ns"] == 0.0:
            key = r["structure"] if r["block_cap"] == 0 else f'{r["structure"]}@{r["block_cap"]}'
            mems.setdefault(key, r["bytes_per_key"])
    # keep baselines + a representative pair of caps
    keep = [k for k in mems if "@" not in k] + \
           [k for k in mems if k.endswith("@128") or k.endswith("@1024")]
    keep = sorted(set(keep), key=lambda k: mems[k])
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.barh(keep, [mems[k] for k in keep], color="tab:blue")
    ax.set_xlabel("bytes per key (structural, exact)")
    ax.set_title(f"Memory per key (n={n:,})")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "memory_bytes_per_key.png"), dpi=150)
    plt.close(fig)

# ---- 4. insert benchmark ----
ins = defaultdict(list)
for r in rows:
    if r["insert_ns"] > 0:
        key = r["structure"] if r["block_cap"] == 0 else f'{r["structure"]}@{r["block_cap"]}'
        ins[key].append(r["insert_ns"])
if ins:
    keys = sorted(ins, key=lambda k: mean(ins[k]))
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.barh(keys, [mean(ins[k]) for k in keys], color="tab:orange")
    ax.set_xlabel("ns per random-order insert")
    ax.set_title("Insert benchmark (incremental skip maintenance)")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "insert_ns.png"), dpi=150)
    plt.close(fig)

print(f"plots written to {OUT_DIR}/")
