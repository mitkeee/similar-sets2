#!/usr/bin/env bash
# Experiment 2 (the professor's datasets): every version, every dataset, REPS runs.
# Each run is a new program.  Every dataset.txt needs its dataset.test.
#   ./run-data.sh                           all $DATADIR/*.txt, 10 runs
#   ./run-data.sh path/to/stack-dedup.txt   only the given files
#   REPS=5 ./run-data.sh
# DATADIR is /data/milos/datasets by default (the server).
# Results: results/datasets-<date>.csv       every run
#          results/datasets-<date>-size.csv  every run, search by test-set size
#          results/datasets-<date>.txt       the datasets, every run, the median
# Linux: pinned to CPU 2 (CPU=...).
set -e

FILES=()
for f in "$@"; do
    FILES+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")")
done
cd "$(dirname "$0")"
if [ ${#FILES[@]} -eq 0 ]; then
    FILES=("${DATADIR:-/data/milos/datasets}"/*.txt)
fi

MAKE=${MAKE:-$(command -v make || command -v mingw32-make)}
echo "building and testing ..." >&2
"$MAKE" -s all
out=$("$MAKE" -s test 2>&1) || { echo "$out"; exit 1; }     # correctness test, silent when OK

REPS=${REPS:-10}
OUT=results/datasets-$(date +%Y%m%d-%H%M%S)
mkdir -p results

PIN=""
if command -v taskset >/dev/null 2>&1; then PIN="taskset -c ${CPU:-2}"; fi

echo "version,dataset,insert_ns,search_ns" > "$OUT.csv"
echo "version,dataset,size_from,size_to,test_sets,lookups,ns_per_test_set" > "$OUT-size.csv"

BINS=(data-v*)
TOTAL=$(( ${#FILES[@]} * ${#BINS[@]} * REPS ))

# the datasets and the test, the same for every version
INFO=$(mktemp)
for f in "${FILES[@]}"; do
    echo "reading $(basename "$f") ..." >&2
    ./"${BINS[0]}" -i "$f" >> "$INFO"
done

echo "experiment 2: $TOTAL runs, results in $OUT.txt" >&2
k=0
for f in "${FILES[@]}"; do
    for b in "${BINS[@]}"; do
        v=${b#data-}; v=${v%.exe}
        for r in $(seq "$REPS"); do
            k=$((k + 1))
            printf "[%${#TOTAL}d/%d] %-14s %-38s run %2d/%d  " "$k" "$TOTAL" "$v" "$(basename "$f")" "$r" "$REPS" >&2
            s0=$SECONDS
            line=$($PIN ./"$b" -s "$OUT-size.csv" "$f")
            echo "$line" >> "$OUT.csv"
            echo "$line" | awk -F, -v t=$((SECONDS - s0)) '{ printf "insert %6.1f  search %6.1f ns  (%d s)\n", $3, $4, t }' >&2
        done
    done
done
echo >&2

# the datasets, every run, then the median per version and dataset
awk -F, -v runs="$REPS" '
function median(k, c,    i, j, a, t, m) {
    m = cnt[k]
    for (i = 1; i <= m; i++) a[i] = val[k, c, i] + 0
    for (i = 2; i <= m; i++) { t = a[i]; for (j = i - 1; j >= 1 && a[j] > t; j--) a[j + 1] = a[j]; a[j + 1] = t }
    return m % 2 ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
}
FNR == NR { info[++ni] = $0; next }
FNR > 1 { k = $1 FS $2; if (!(k in cnt)) order[++nk] = k; cnt[k]++; for (c = 3; c <= 4; c++) val[k, c, cnt[k]] = $c; row[++nr] = $0; run[nr] = cnt[k] }
END {
    printf "the datasets and the test (the same for every version)\n"
    printf "%-38s %9s %9s %8s %7s %12s %9s %12s %8s %10s\n", "dataset", "sigma", "N sets", "M tests", "avg |X|",
           "dictionaries", "largest", "lookups/test", "in root", "dict size"
    for (i = 1; i <= ni; i++) {
        split(info[i], f, FS)
        printf "%-38s %9s %9s %8s %7s %12s %9s %12s %7s%% %10s\n", f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10]
    }
    printf "\nevery run, ns per operation\n"
    printf "%-14s %-38s %4s %8s %8s\n", "version", "dataset", "run", "insert", "search"
    for (i = 1; i <= nr; i++) {
        split(row[i], f, FS)
        printf "%-14s %-38s %4d %8.1f %8.1f\n", f[1], f[2], run[i], f[3], f[4]
    }
    printf "\nmedian of %d runs, ns per operation\n", runs
    printf "%-14s %-38s %8s %8s\n", "version", "dataset", "insert", "search"
    for (i = 1; i <= nk; i++) {
        split(order[i], f, FS)
        printf "%-14s %-38s", f[1], f[2]
        for (c = 3; c <= 4; c++) printf " %8.1f", median(order[i], c)
        printf "\n"
    }
}' "$INFO" "$OUT.csv" | tee "$OUT.txt"
rm -f "$INFO"
