# Profiling the sequential baseline (on the Linux server)

Build the profiling binary (same `-O2` as the baseline, plus `-g -fno-omit-frame-pointer`):

```
make profile            # produces bin/bpe_seq_prof
```

Input: `data/wikitext-103/test.txt` is enough for perf. Use a smaller slice for valgrind
(it is 20-50x slower), e.g. `head -c 200000 data/wikitext-103/test.txt > /tmp/small.txt`.

## 1. perf (try this first)

```
which perf                                  # is it installed?
perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses \
    ./bin/bpe_seq_prof data/wikitext-103/test.txt
perf record -g ./bin/bpe_seq_prof data/wikitext-103/test.txt
perf report --stdio --no-children | head -60   # time per function
perf annotate --stdio -s Tokenizer::encode_word | head -100   # time per source line
```

If perf says "Permission denied / perf_event_paranoid", the server blocks it: use valgrind.

What to look for in `perf report`:
- `std::_Hashtable::find` / `_M_find_node`  -> merge-table lookups
- `malloc`, `free`, `operator new`          -> per-word vector allocation
- `Tokenizer::encode_word` itself           -> the scan loop and the in-place merge
- `Tokenizer::pretokenize`                  -> splitting text into words

`perf stat`: a high cache-miss rate supports "lookup is memory-bound".

## 2. valgrind callgrind (no special permission, small input)

```
valgrind --tool=callgrind --cache-sim=yes ./bin/bpe_seq_prof /tmp/small.txt
callgrind_annotate callgrind.out.<pid> | head -50
```

(`kcachegrind` can open the same file if you have a display.)

## 3. gprof (coarse, function level only)

```
g++ -std=c++17 -O2 -pg src/main_seq.cpp src/bpe.cpp -o bin/bpe_seq_gprof
./bin/bpe_seq_gprof data/wikitext-103/test.txt   # writes gmon.out
gprof bin/bpe_seq_gprof gmon.out | head -30
```

## What to record
Paste the top of `perf report` (or callgrind_annotate) and the `perf stat` numbers,
plus the machine (`lscpu | head -20`). These tell us how much of encode time is
lookup / allocation / merge, which decides what to optimize first.
