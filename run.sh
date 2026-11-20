#!/usr/bin/env bash
# Build, test and benchmark every version on generated keys.
#   SIZES="1000 1000000" REPS=5 QUERIES=2000000 ./run.sh
# Output: results/results-<date>.csv.  Linux: pinned to CPU 2 (CPU=...).
set -e
cd "$(dirname "$0")"

MAKE=${MAKE:-$(command -v make || command -v mingw32-make)}
"$MAKE" all
"$MAKE" test

SIZES=${SIZES:-"1000 10000 100000 1000000"}
QUERIES=${QUERIES:-1000000}
WALK=${WALK:-16}
REPS=${REPS:-3}
OUT=${OUT:-results/results-$(date +%Y%m%d-%H%M%S).csv}
mkdir -p "$(dirname "$OUT")"

PIN=""
if command -v taskset >/dev/null 2>&1; then PIN="taskset -c ${CPU:-2}"; fi

for n in $SIZES; do
    for b in bench-v*; do
        [ -f "$b" ] || continue
        $PIN ./"$b" -n "$n" -q "$QUERIES" -w "$WALK" -r "$REPS" -o "$OUT"
    done
done
echo "results: $OUT"
