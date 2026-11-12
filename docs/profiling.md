# Sequential baseline 的 profiling（在 Linux server 上）

編譯 profiling 用的執行檔（與 baseline 同樣的 `-O2`，另外加 `-g -fno-omit-frame-pointer`）：

```
make profile            # 產生 bin/bpe_seq_prof
```

輸入：用 perf 的話 `data/wikitext-103/test.txt` 就夠了。valgrind 要用小一點的片段
（它會慢 20 到 50 倍），例如 `head -c 200000 data/wikitext-103/test.txt > /tmp/small.txt`。

在 server 上，要用 `run -c 1 --` 在計算節點執行，例如：`run -c 1 -- perf stat ...`。

## 1. perf（先試這個）

```
which perf                                  # 有沒有安裝？
perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses \
    ./bin/bpe_seq_prof data/wikitext-103/test.txt
perf record -g ./bin/bpe_seq_prof data/wikitext-103/test.txt
perf report --stdio --no-children | head -60   # 各函式占的時間
perf annotate --stdio -s Tokenizer::encode_word | head -100   # 各行原始碼占的時間
```

如果 perf 顯示「Permission denied / perf_event_paranoid」，代表 server 擋掉了，改用 valgrind。

`perf report` 要看的項目：
- `std::_Hashtable::find` / `_M_find_node`  -> merge table 的查詢
- `malloc`、`free`、`operator new`          -> 每個 word 配置 vector 的成本
- `Tokenizer::encode_word` 本身             -> 掃描迴圈與原地 merge
- `Tokenizer::pretokenize`                  -> 把文字切成 word

`perf stat`：cache miss 比例高，支持「查表受限於記憶體」的說法。

## 2. valgrind callgrind（不需要特殊權限，要用小輸入）

```
valgrind --tool=callgrind --cache-sim=yes ./bin/bpe_seq_prof /tmp/small.txt
callgrind_annotate callgrind.out.<pid> | head -50
```

（有顯示環境的話，`kcachegrind` 可以開啟同一個檔案。）

## 3. gprof（粗略，只到函式層級）

```
g++ -std=c++17 -O2 -pg src/main_seq.cpp src/bpe.cpp -o bin/bpe_seq_gprof
./bin/bpe_seq_gprof data/wikitext-103/test.txt   # 會寫出 gmon.out
gprof bin/bpe_seq_gprof gmon.out | head -30
```

## 要記錄什麼
把 `perf report`（或 callgrind_annotate）的前幾名和 `perf stat` 的數字貼出來，
另外附上機器資訊（`lscpu | head -20`）。這些資料可以說明 encode 的時間有多少花在
查表、配置記憶體、merge，進而決定先優化哪一項。

已量到的結果見 [baseline-results.md](baseline-results.md)。
