# 使用說明

編譯、執行與檢查 BPE tokenizer 的指令。所有指令都在專案根目錄執行。

## 1. 環境設定

**Python**（只用在下載資料與驗證，tokenizer 本身是純 C++）：

```
conda activate PP_project            # 任何有安裝 tiktoken、datasets 的環境都可以（pip install tiktoken datasets）
```

**資料**（放在 `data/`，已被 git 忽略，所以每台機器都要各自下載一份）：

```
python scripts/download_data.py              # GPT-2 檔案 + WikiText-103 test 切分（約 1 MB）   -> 本機開發用
python scripts/download_data.py --full       # GPT-2 檔案 + WikiText-103 全部切分（約 540 MB） -> server 實驗用
python scripts/download_data.py --gpt2-only
```

下載後的結構：

```
data/gpt2/vocab.json  data/gpt2/merges.txt
data/wikitext-103/test.txt  （加 --full 還會有 validation.txt、train.txt）
```

如果 server 不能連外網，就在自己的電腦下載，再用 `scp -r data <server>:<repo>/` 傳過去。

## 2. 編譯

```
make                    # -> bin/bpe_seq          （Linux / server）
make profile            # -> bin/bpe_seq_prof     （同樣 -O2，另外加 -g，給 perf / valgrind 用）
make clean
```

Windows（MSYS2）：make 的名稱是 `mingw32-make`。另外要用靜態連結，避免載入到別的安裝
（例如 Git 附的）錯誤版本的 `libstdc++` DLL：

```
mingw32-make LDFLAGS=-static
```

需要支援 C++17 的編譯器（server 的 g++ 12 即可）。

## 3. 執行

```
bin/bpe_seq <input.txt> [-o ids.bin] [--algo naive|heap] [--vocab PATH] [--merges PATH]
```

| 選項 | 說明 |
|---|---|
| `-o ids.bin` | 同時把 token id 以原始 int32 格式寫出（驗證時需要） |
| `--algo naive` | O(n^2) 的 Sennrich merge 迴圈（預設；作為 ground truth） |
| `--algo heap` | O(n log n)：linked list + min-heap |
| `--vocab`、`--merges` | 預設為 `data/gpt2/vocab.json` 與 `data/gpt2/merges.txt` |

範例與輸出：

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

載入 vocab / merges 與讀檔的時間不計入，只量 `pretokenize` 與 `encode`。

**在 server 上**，要在計算節點跑計時實驗，不要在 login 節點（login 是多人共用，數字很吵）。`-c` 是 CPU 核心數：

```
run -c 1 -- bin/bpe_seq data/wikitext-103/train.txt
```

### 重複量測（建議用於所有要寫進報告的數字）

單次量測有雜訊。`scripts/bench.sh` 會重複執行同一個指令，丟掉暖機的第一次，並輸出每個計時欄位的
最小值、中位數、最大值（報告用中位數，並一併寫出範圍）：

```
bash scripts/bench.sh [-n 次數] [-w 暖機次數] <指令...>
```

```
$ bash scripts/bench.sh -n 20 bin/bpe_seq data/wikitext-103/test.txt --algo heap
command : bin/bpe_seq data/wikitext-103/test.txt --algo heap
runs    : 20 (after 1 warm-up)
pretokenize min      5.11   median      5.48   max      6.47  ms
encode      min    107.58   median    110.26   max    116.91  ms
total       min    113.21   median    115.59   max    122.75  ms
```

在 server 上，要把**整個腳本**放進同一個 `run` 工作，這樣所有次數都在同一個節點上（分開呼叫 `run` 可能被
分到不同節點，負載不同）：

```
run -c 1 -- bash scripts/bench.sh -n 20 bin/bpe_seq data/wikitext-103/test.txt
```

次數建議：test 檔與 word 長度測試檔用 20 次；完整 train（約 36 秒）用 `-n 5` 即可。
被量測的程式必須印出像 `encode     : 45.49 ms` 這樣的行（`bin/bpe_seq` 已經是這個格式，之後的
OpenMP / CUDA 版也請沿用）。

## 4. 與 tiktoken 比對正確性

```
bin/bpe_seq data/wikitext-103/test.txt -o ids.bin --algo heap
python scripts/verify.py data/wikitext-103/test.txt ids.bin
```

成功會印出 `OK: all N token ids match tiktoken`；不一致則印出第一個不同的位置與前後文。
兩種演算法也可以直接用 `cmp` 互相比較：

```
bin/bpe_seq data/wikitext-103/test.txt --algo naive -o a.bin
bin/bpe_seq data/wikitext-103/test.txt --algo heap  -o b.bin
cmp a.bin b.bin && echo identical
```

`verify.py` 會把所有資料讀進 Python list，所以不要拿它跑完整的 train 切分。

## 5. word 長度實驗（naive 對 heap）

真實文字的 word 很短（約 5 bytes），這時簡單的 O(n^2) 迴圈比較快。要找出 heap 版從哪個長度開始超越它，
可以產生「每個 word 長度都相同」的測試檔：

```
python scripts/gen_long_words.py                 # -> data/long/words_{4,8,...,4096}.txt（每個 256 KB）
for L in 4 8 16 32 64 128 256 512 1024 2048 4096; do
  echo "L=$L"
  bin/bpe_seq data/long/words_$L.txt --algo naive | grep encode
  bin/bpe_seq data/long/words_$L.txt --algo heap  | grep encode
done
```

在 server 上，要在每個 `bin/bpe_seq` 前面加上 `run -c 1 --`。本機結果：交叉點約在 L = 16 bytes。

## 6. Profiling

見 [profiling.md](profiling.md)（在 server 上使用 perf、valgrind、gprof）。

## 7. 原始碼對照

| 檔案 | 功能 |
|---|---|
| `src/bpe.hpp`、`src/bpe.cpp` | `Tokenizer`：載入表、GPT-2 pre-tokenizer、`encode_word`（naive）、`encode_word_heap` |
| `src/main_seq.cpp` | sequential baseline 的命令列程式 |
| `src/unicode_tables.inc` | 產生出來的 `\p{L}` / `\p{N}` 區間表（由 `scripts/gen_unicode_table.py` 產生） |
| `scripts/` | 下載資料、產生 Unicode 表、用 tiktoken 驗證、產生固定長度 word 的測試檔 |
