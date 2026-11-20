#!/usr/bin/env bash
# Build, test and run the dataset benchmark of every version.
#   ./run-data.sh [dataset.txt ...]     default: all $DATADIR/*.txt
#   REPS=5 QUERIES=1000000 ./run-data.sh
# Output: results/data-<date>.csv.  Linux: pinned to CPU 2 (CPU=...).
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
"$MAKE" all
"$MAKE" test

QUERIES=${QUERIES:-500000}
WALK=${WALK:-16}
REPS=${REPS:-3}
OUT=${OUT:-results/data-$(date +%Y%m%d-%H%M%S).csv}
mkdir -p "$(dirname "$OUT")"

PIN=""
if command -v taskset >/dev/null 2>&1; then PIN="taskset -c ${CPU:-2}"; fi

for f in "${FILES[@]}"; do
    for b in data-v*; do
        [ -f "$b" ] || continue
        $PIN ./"$b" -d "$f" -q "$QUERIES" -w "$WALK" -r "$REPS" -o "$OUT"
    done
done
echo "results: $OUT"
