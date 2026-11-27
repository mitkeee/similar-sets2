#!/usr/bin/env bash
# Experiment 1 (generated data): every version, every size n, REPS runs.
# Each run is a new program with its own seed (run r uses seed r).
#   ./run.sh                          n = 2^10 .. 2^20, 10 runs
#   SIZES="65536" REPS=5 ./run.sh
# Results: results/generated-<date>.csv  every run
#          results/generated-<date>.txt  every run, then the median
# Linux: pinned to CPU 2 (CPU=...).
set -e
cd "$(dirname "$0")"

MAKE=${MAKE:-$(command -v make || command -v mingw32-make)}
echo "building and testing ..." >&2
"$MAKE" -s all
out=$("$MAKE" -s test 2>&1) || { echo "$out"; exit 1; }     # correctness test, silent when OK

SIZES=${SIZES:-"1024 4096 16384 65536 262144 1048576"}
REPS=${REPS:-10}
OUT=results/generated-$(date +%Y%m%d-%H%M%S)
mkdir -p results

PIN=""
if command -v taskset >/dev/null 2>&1; then PIN="taskset -c ${CPU:-2}"; fi

echo "version,n,seed,insert_ns,search_ns,walk_ns,delete_ns" > "$OUT.csv"
BINS=(bench-v*)
TOTAL=$(( $(echo $SIZES | wc -w) * ${#BINS[@]} * REPS ))
echo "experiment 1: $TOTAL runs, results in $OUT.txt" >&2
k=0
for n in $SIZES; do
    for b in "${BINS[@]}"; do
        v=${b#bench-}; v=${v%.exe}
        for r in $(seq "$REPS"); do
            k=$((k + 1))
            printf "[%${#TOTAL}d/%d] %-14s n=%-8s run %2d/%d  " "$k" "$TOTAL" "$v" "$n" "$r" "$REPS" >&2
            s0=$SECONDS
            line=$($PIN ./"$b" -n "$n" -s "$r")
            echo "$line" >> "$OUT.csv"
            echo "$line" | awk -F, -v t=$((SECONDS - s0)) '{ printf "insert %6.1f  search %6.1f  walk %6.1f  delete %6.1f ns  (%d s)\n", $4, $5, $6, $7, t }' >&2
        done
    done
done
echo >&2

# every run, then the median per version and n
awk -F, -v runs="$REPS" '
function median(k, c,    i, j, a, t, m) {
    m = cnt[k]
    for (i = 1; i <= m; i++) a[i] = val[k, c, i] + 0
    for (i = 2; i <= m; i++) { t = a[i]; for (j = i - 1; j >= 1 && a[j] > t; j--) a[j + 1] = a[j]; a[j + 1] = t }
    return m % 2 ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
}
NR > 1 { k = $1 FS $2; if (!(k in cnt)) order[++nk] = k; cnt[k]++; for (c = 4; c <= 7; c++) val[k, c, cnt[k]] = $c; row[++nr] = $0 }
END {
    printf "every run, ns per operation\n"
    printf "%-14s %8s %4s %8s %8s %8s %8s\n", "version", "n", "run", "insert", "search", "walk", "delete"
    for (i = 1; i <= nr; i++) {
        split(row[i], f, FS)
        printf "%-14s %8s %4s %8.1f %8.1f %8.1f %8.1f\n", f[1], f[2], f[3], f[4], f[5], f[6], f[7]
    }
    printf "\nmedian of %d runs, ns per operation\n", runs
    printf "%-14s %8s %8s %8s %8s %8s\n", "version", "n", "insert", "search", "walk", "delete"
    for (i = 1; i <= nk; i++) {
        split(order[i], f, FS)
        printf "%-14s %8s", f[1], f[2]
        for (c = 4; c <= 7; c++) printf " %8.1f", median(order[i], c)
        printf "\n"
    }
}' "$OUT.csv" | tee "$OUT.txt"
