# Usage

Commands for building, running and checking the BPE tokenizer. Run everything from the repository root.

## 1. Setup

**Python** (only for data download and verification; the tokenizer itself is pure C++):

```
conda activate PP_project            # any env with: pip install tiktoken datasets
```

**Data** (stored in `data/`, which is git-ignored, so every machine downloads its own copy):

```
python scripts/download_data.py              # GPT-2 files + WikiText-103 test split (~1 MB)  -> local development
python scripts/download_data.py --full       # GPT-2 files + all WikiText-103 splits (~540 MB) -> server experiments
python scripts/download_data.py --gpt2-only
```

Result:

```
data/gpt2/vocab.json  data/gpt2/merges.txt
data/wikitext-103/test.txt  (validation.txt, train.txt with --full)
```

If the server cannot reach the internet, download on your machine and `scp -r data <server>:<repo>/`.

## 2. Build

```
make                    # -> bin/bpe_seq          (Linux / server)
make profile            # -> bin/bpe_seq_prof     (same -O2, plus -g for perf / valgrind)
make clean
```

Windows (MSYS2): the make is called `mingw32-make`, and link statically to avoid picking up a
wrong `libstdc++` DLL from another install (e.g. Git's):

```
mingw32-make LDFLAGS=-static
```

Requires a C++17 compiler (g++ 12 on the server is fine).

## 3. Run

```
bin/bpe_seq <input.txt> [-o ids.bin] [--algo naive|heap] [--vocab PATH] [--merges PATH]
```

| Option | Meaning |
|---|---|
| `-o ids.bin` | also write the token ids as raw int32 (needed for verification) |
| `--algo naive` | O(n^2) Sennrich merge loop (default; the ground truth) |
| `--algo heap` | O(n log n): linked list + min-heap |
| `--vocab`, `--merges` | default to `data/gpt2/vocab.json` and `data/gpt2/merges.txt` |

Example and output:

```
$ bin/bpe_seq data/wikitext-103/test.txt
algorithm  : naive
bytes      : 1287656
words      : 247230
tokens     : 283287
pretokenize: 5.34 ms
encode     : 45.49 ms
total      : 50.83 ms  (5573356 tokens/s)
```

Loading vocab/merges and reading the file are not timed; only `pretokenize` and `encode` are.

**On the server**, run timed experiments on a compute node, not the login node (login is shared and
noisy). `-c` is the number of CPU cores:

```
run -c 1 -- bin/bpe_seq data/wikitext-103/train.txt
```

## 4. Check correctness against tiktoken

```
bin/bpe_seq data/wikitext-103/test.txt -o ids.bin --algo heap
python scripts/verify.py data/wikitext-103/test.txt ids.bin
```

Prints `OK: all N token ids match tiktoken`, or the first mismatching position with context.
Compare the two algorithms directly with `cmp`:

```
bin/bpe_seq data/wikitext-103/test.txt --algo naive -o a.bin
bin/bpe_seq data/wikitext-103/test.txt --algo heap  -o b.bin
cmp a.bin b.bin && echo identical
```

`verify.py` loads everything into Python lists, so do not use it on the full train split.

## 5. Word-length experiment (naive vs heap)

Real text has short words (about 5 bytes), where the simple O(n^2) loop wins. To find where the heap
version overtakes it, generate files whose words all have the same length L:

```
python scripts/gen_long_words.py                 # -> data/long/words_{4,8,...,4096}.txt (256 KB each)
for L in 4 8 16 32 64 128 256 512 1024 2048 4096; do
  echo "L=$L"
  bin/bpe_seq data/long/words_$L.txt --algo naive | grep encode
  bin/bpe_seq data/long/words_$L.txt --algo heap  | grep encode
done
```

Prefix the `bin/bpe_seq` calls with `run -c 1 --` on the server. Local result: break-even at L of about 16 bytes.

## 6. Profiling

See [profiling.md](profiling.md) (perf, valgrind, gprof on the server).

## 7. Source layout

| File | Role |
|---|---|
| `src/bpe.hpp`, `src/bpe.cpp` | `Tokenizer`: load tables, GPT-2 pre-tokenizer, `encode_word` (naive), `encode_word_heap` |
| `src/main_seq.cpp` | command-line program for the sequential baseline |
| `src/unicode_tables.inc` | generated `\p{L}` / `\p{N}` ranges (`scripts/gen_unicode_table.py`) |
| `scripts/` | data download, Unicode table generator, tiktoken verifier, long-word generator |
