#!/usr/bin/env bash
# Repeat a command and summarize its timing lines (min / median / max).
#
# Usage: bash scripts/bench.sh [-n RUNS] [-w WARMUP] <command> [args...]
#   -n RUNS    measured runs (default 20)
#   -w WARMUP  extra runs executed first and discarded (default 1)
#
# The command must print lines like "encode     : 45.49 ms" (as bin/bpe_seq does).
# Example:
#   bash scripts/bench.sh -n 20 bin/bpe_seq data/wikitext-103/test.txt --algo heap
# On the server, run the whole script inside ONE job so all runs share the same node:
#   run -c 1 -- bash scripts/bench.sh -n 20 bin/bpe_seq data/wikitext-103/test.txt

RUNS=20
WARMUP=1
while getopts "n:w:" opt; do
    case $opt in
        n) RUNS=$OPTARG ;;
        w) WARMUP=$OPTARG ;;
        *) echo "usage: bash $0 [-n RUNS] [-w WARMUP] <command> [args...]" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { echo "usage: bash $0 [-n RUNS] [-w WARMUP] <command> [args...]" >&2; exit 1; }

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

for ((i = 1; i <= WARMUP + RUNS; i++)); do
    out=$("$@") || { echo "command failed on run $i" >&2; exit 1; }
    [ "$i" -gt "$WARMUP" ] && printf '%s\n' "$out" >> "$tmp"
done

echo "command : $*"
echo "runs    : $RUNS (after $WARMUP warm-up)"
for key in pretokenize encode total; do
    # Take the number after the colon on lines that start with the key.
    vals=$(awk -F: -v k="$key" '$1 ~ "^" k "[ ]*$" { split($2, a, " "); print a[1] }' "$tmp" | sort -n)
    [ -n "$vals" ] || continue
    echo "$vals" | awk -v k="$key" '
        { v[NR] = $1 }
        END {
            med = (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2
            printf "%-11s min %9.2f   median %9.2f   max %9.2f  ms\n", k, v[1], med, v[NR]
        }'
done
