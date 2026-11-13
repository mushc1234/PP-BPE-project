#!/usr/bin/env bash
# Naive vs heap encoder as a function of word length (median encode time over several runs).
#
# Usage: bash scripts/bench_long.sh [-n RUNS] [BINARY]
#   -n RUNS  measured runs per (length, algorithm) pair (default 10)
#   BINARY   encoder executable (default bin/bpe_seq)
#
# Needs data/long/words_<L>.txt (created by scripts/gen_long_words.py; generated here if missing).
# On the server run it inside ONE job so every number comes from the same node:
#   run -c 1 -- bash scripts/bench_long.sh -n 10

RUNS=10
while getopts "n:" opt; do
    case $opt in
        n) RUNS=$OPTARG ;;
        *) echo "usage: bash $0 [-n RUNS] [BINARY]" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
BIN=${1:-bin/bpe_seq}
LENGTHS="4 8 16 32 64 128 256 512 1024 2048 4096"

if [ ! -f data/long/words_4.txt ]; then
    echo "generating data/long/*.txt ..." >&2
    python3 scripts/gen_long_words.py >&2 || python scripts/gen_long_words.py >&2 || exit 1
fi

# Median encode time (ms) of one configuration, taken from bench.sh's "encode ... median X" line.
median_encode() {
    bash scripts/bench.sh -n "$RUNS" "$BIN" "$1" --algo "$2" | awk '/^encode/ { print $5 }'
}

echo "runs per cell: $RUNS (median of encode time, ms)"
printf "%-6s %12s %12s %12s\n" "L" "naive" "heap" "naive/heap"
for L in $LENGTHS; do
    f=data/long/words_$L.txt
    n=$(median_encode "$f" naive)
    h=$(median_encode "$f" heap)
    printf "%-6s %12s %12s %12s\n" "$L" "$n" "$h" "$(awk -v a="$n" -v b="$h" 'BEGIN { printf "%.2f", a / b }')"
done
