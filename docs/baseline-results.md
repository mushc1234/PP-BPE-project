# Sequential baseline 結果

Sequential encoder 的量測紀錄，量測日期 2026-11-12。重現方式見 [usage.md](usage.md) 與
[profiling.md](profiling.md)。標示 *單次量測* 的數字還沒有重複量測過。

## 環境

| 名稱 | 硬體 | 備註 |
|---|---|---|
| **server-compute** | Intel Core i5-10500 @ 3.10 GHz，12 vCPU（回報為每核 1 執行緒），KVM 虛擬機，11 GiB RAM | 用 `run -c 1 --` 啟動；**報告用的數字以此為準** |
| server-login | 同型機器，所有人共用 | 很吵，約比 compute 慢 1.7 倍，不要拿來當結果 |
| local | Windows 11，MSYS2 g++ 15.2（`-O2`，靜態連結） | 只用於開發 |

Server 是 KVM 虛擬機，而 i5-10500 實體上是 6 核 / 12 執行緒，所以這 12 個 vCPU 很可能是宿主機的
hyper-thread。解讀超過 6 個 thread 的 OpenMP scalability 時要記得這一點。

編譯：`g++ -std=c++17 -O2`（server 的 g++ 12.2）。程式碼：O(n^2) 的路徑自 commit `2fe35be` 之後沒有變動。

## 1. 重複量測（server-compute，`-c 1`，naive，`scripts/bench.sh`，取中位數）

輸入各為 WikiText-103 的 test 與 train 切分。train 重複 5 次，test 重複 20 次（皆已丟掉 1 次暖機）。

| 輸入 | 項目 | 最小值 | 中位數 | 最大值 |
|---|---|---|---|---|
| train（5 次） | pretokenize | 2,827 ms | 2,936 ms | 2,966 ms |
| | encode | 34,190 ms | 36,086 ms | 36,604 ms |
| | 總計 | 37,018 ms | **39,053 ms** | 39,555 ms |
| test（20 次） | pretokenize | 7.17 ms | 7.27 ms | 7.51 ms |
| | encode | 87.47 ms | 89.09 ms | 96.99 ms |
| | 總計 | 94.63 ms | **96.38 ms** | 104.27 ms |

由中位數導出的數值：

| 項目 | train | test |
|---|---|---|
| 輸入 | 539,295,549 bytes、104,066,999 個 word、117,920,140 個 token | 1,287,656 bytes、247,230 個 word、283,287 個 token |
| encode 占總時間 | 92% | 92% |
| 吞吐量 | **302 萬 tokens/s**（約 13.8 MB/s） | **294 萬 tokens/s** |
| 每個 word / 每個 token 的 encode 時間 | 約 347 ns / 約 306 ns | 約 360 ns / 約 314 ns |
| 每個 word 的 token 數 | 1.13 | 1.15 |

重複量測的觀察：

- 兩種輸入的吞吐量只差 3%，在雜訊範圍內，所以吞吐量與輸入大小無關。
- 單次量測的雜訊約 7%（train）到 10%（test）。之後比較 OpenMP 的 speedup，小於這個範圍的差異不能視為真實差異。
- 較早的單次量測（train 36.03 秒、327 萬 tokens/s）落在範圍的低端，偏樂觀；**報告請用上表的中位數**。

### heap 版（`--algo heap`，同一環境與方法）

| 輸入 | 項目 | 最小值 | 中位數 | 最大值 |
|---|---|---|---|---|
| train（5 次） | pretokenize | 2,835 ms | 2,892 ms | 2,931 ms |
| | encode | 49,050 ms | 49,860 ms | 51,912 ms |
| | 總計 | 51,947 ms | **52,695 ms** | 54,804 ms |
| test（20 次） | pretokenize | 6.94 ms | 7.12 ms | 7.60 ms |
| | encode | 119.27 ms | 122.09 ms | 132.62 ms |
| | 總計 | 126.44 ms | **129.23 ms** | 140.22 ms |

| 項目 | train | test |
|---|---|---|
| 吞吐量（以總計中位數算） | 224 萬 tokens/s | 219 萬 tokens/s |
| 每個 word / 每個 token 的 encode 時間 | 約 479 ns / 約 423 ns | 約 494 ns / 約 431 ns |
| encode 時間，heap ÷ naive | **1.38** | **1.37** |

在真實 WikiText 上（word 約 5 bytes），heap 版比 naive 慢約 1.4 倍，且 test 與 train 的比例一致。本機的
差距較大（約 2.3 倍，見第 3 節），所以這個比例會隨環境改變，交叉點也可能不同。注意 naive 與 heap 是在
**不同的 `run` 工作**中量測的，節點與負載可能略有差異；嚴格比較應放在同一個工作內。

峰值記憶體沒有量到（程式有跑完；估計 3.5 到 4 GB，可用 11 GiB）。

## 2. test 切分的硬體計數器與 profile（server-compute，`-c 1`，naive，單次量測）

輸入：`data/wikitext-103/test.txt`（1,287,656 bytes、247,230 個 word、283,287 個 token），用 `make profile` 編譯。

`perf stat`（整個程序，包含載入 vocab 與 merges）：

| 計數器 | 數值 |
|---|---|
| cycles / instructions | 5.242 億 / 5.215 億，**IPC 0.99** |
| cache-references / cache-misses | 1,275 萬 / 100 萬（**7.85%**） |
| branch-misses | 451 萬 |
| 經過時間 | 0.156 秒（其中 pretokenize + encode 只有約 0.087 秒） |

`perf record`（524 個取樣，所以每個函式約有幾個百分點的誤差）：

| 占比 | 函式 |
|---|---|
| 65.4% | `Tokenizer::encode_word`（merge table 的查詢被內嵌在這裡） |
| 約 10% | 載入：`_Hashtable<string,int>` 的 find / rehash、`Tokenizer::load`、`unordered_map<uint64,MergeInfo>::operator[]` |
| 3.5% | `Tokenizer::pretokenize`（另有 `decode_utf8` 1.2%） |
| 約 4% | `malloc` / `free` 一類 |
| 約 4% | 核心的 page fault、`memset`、`memmove` |

解讀：`encode_word` 占大宗。IPC 約 1，加上明顯的 cache miss 與 branch miss 比例，代表迴圈常常在等記憶體。
`encode_word` 內部的 hash 查詢、掃描迴圈、merge 壓縮各占多少，**目前還不知道**（需要 `perf annotate`）。
每個 word 配置記憶體的成本很小（約 4%，其中一部分來自載入階段）。

## 3. 依 word 長度比較 naive 與 heap encoder（本機，單次量測，只計 encode 時間）

檔案由 `scripts/gen_long_words.py` 產生：把真實英文字母切成等長的 word，每個檔案 256 KB。

| L（bytes） | naive (ms) | heap (ms) | naive / heap |
|---|---|---|---|
| 4 | 16.7 | 19.8 | 0.84 |
| 8 | 14.7 | 22.7 | 0.65 |
| 16 | 25.3 | 22.9 | 1.10 |
| 32 | 38.1 | 24.5 | 1.56 |
| 64 | 54.7 | 25.3 | 2.17 |
| 128 | 70.8 | 27.5 | 2.57 |
| 256 | 97.0 | 27.8 | 3.49 |
| 512 | 137.5 | 28.8 | 4.78 |
| 1024 | 209.5 | 31.3 | 6.69 |
| 2048 | 303.4 | 31.7 | 9.58 |
| 4096 | 414.6 | 39.4 | 10.51 |

在真實的 WikiText test 切分（word 約 5 bytes）上，本機結果：naive 45.5 ms，heap 106.6 ms（heap 慢 2.3 倍）。
Heap 的時間幾乎不隨 L 改變（輸入大小固定時為 O(n log n)）；naive 從 L=4 到 L=4096 增加約 25 倍。
交叉點在 L = 16 附近。兩種 encoder 在每個檔案上的輸出都逐位元組相同，且 L = 16、256、4096 與 tiktoken 一致。

## 4. 正確性

| 檢查 | 結果 |
|---|---|
| WikiText-103 test（283,287 個 token），naive 與 heap 對 tiktoken | 所有 token id 皆一致 |
| 85 個 token 的邊界測試文字（連續空白、縮寫、CJK、重音字母、emoji、特殊字串） | 全部一致 |
| train 切分（117,920,140 個 token） | 沒有與 tiktoken 比對（只確認有跑完） |

## 尚未量測

- 第 2 節的 `perf` 結果仍是單次量測（`perf` 取樣本身不需要重複，但 `perf stat` 的數字可以重跑幾次確認）。
- 第 3 節的 word 長度表是本機單次量測，要在 server-compute 上用 `bench.sh` 重做（server 上 heap 的相對劣勢較小，交叉點可能不同）。
- naive 與 heap 放在同一個 `run` 工作內的對照量測。
- train 的峰值記憶體（試試 `sacct -j <jobid> --format=MaxRSS`）。
- `encode_word` 內部的時間分配（`perf annotate`）。
