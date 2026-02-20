# NeoAlzette ARX 密碼分析 AutoSearch（文件：繁體中文）

> 提醒：本 repo 含 **計算密集** 的研究/實驗程式。請從小輪數、小樣本數開始。

[English README](README_EN.md)

本專案目前包含：

- **NeoAlzette 核心實作**：`TwilightDream::NeoAlzetteCore`（單輪 `forward/backward`、線性層 `l1/l2`、以及跨分支注入 helper）。
- **ARX 論文算子（header-only）**：用於模加/常數加減在差分與線性模型下的權重計算。
- **研究/實驗程式**（`test_*.cpp`）：差分 trail trace、含注入模型的最佳 trail 搜尋、以及 PNB/neutral bits 相關實驗。

---

## 目前 repo 內容（以現有程式碼為準）

- **核心 ARX-box**
  - `include/neoalzette/neoalzette_core.hpp`
  - `src/neoalzette/neoalzette_core.cpp`
- **ARX 分析算子（header-only）**
  - `include/arx_analysis_operators/`  
    相關說明與引用見 `include/arx_analysis_operators/README.md`。
- **可執行入口（main）**
  - `test_neoalzette_arx_trace.cpp`
  - `test_neoalzette_differential_best_search.cpp`
  - `test_neoalzette_linear_best_search.cpp`
  - `test_neoalzette_arx_probabilistic_neutral_bits.cpp`
  - `test_neoalzette_arx_probabilistic_neutral_bits_average.cpp`

---

## 建置（Windows / MinGW-w64）

### 快速建置腳本（推薦）

- 執行 `build_and_test.bat`（會編譯全部並執行 `test_neoalzette_arx_probabilistic_neutral_bits.exe`）。
- 其假設你有設定環境變數 **`%MINGW64%`** 指向 toolchain 的 `bin` 目錄，並使用 `"%MINGW64%\clang++"`。

### 手動編譯範例

以下指令會各自產生一個 `.exe`（C++20）：

```bat
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_trace.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_trace.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_differential_best_search.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_differential_best_search.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_linear_best_search.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_linear_best_search.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_probabilistic_neutral_bits.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_probabilistic_neutral_bits.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_probabilistic_neutral_bits_average.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_probabilistic_neutral_bits_average.exe
```

---

## 執行方式

### `test_neoalzette_arx_trace.exe`（逐步差分 trace）

- 無參數。
- 會 trace 兩組起始差分：\((\Delta A,\Delta B)=(1,0)\) 與 \((0,1)\)。
- 內部使用：
  - LM2001 最佳 \(\gamma\) 算子（模加）
  - BvWeight 算子（常數模減）
  - **Affine-derivative** 的注入模型（以 rank 作為權重）

```bat
test_neoalzette_arx_trace.exe
```

### `test_neoalzette_differential_best_search.exe`（含注入模型的最佳 trail 搜尋）

命令列（4 種前端 / 子命令）：

- **策略模式（推薦，少量參數）**：
  - `test_neoalzette_differential_best_search.exe strategy <time|balanced|space> --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [strategy_flags]`
- **詳細模式（全參數，長參數名）**：
  - `test_neoalzette_differential_best_search.exe detail --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [detail_flags]`
- **Auto 模式（兩階段：breadth 掃描 -> deep 搜尋；要求明確起始差分）**：
  - `test_neoalzette_differential_best_search.exe auto --round-count <R> --delta-a <DA> --delta-b <DB> [auto_flags]`
- **Legacy 模式（相容舊語法）**：
  - `test_neoalzette_differential_best_search.exe legacy <round_count> <deltaA_hex> <deltaB_hex> [flags]`
  - `test_neoalzette_differential_best_search.exe legacy <round_count> --seed <seed> [flags]`

注意：

- 若省略 `--delta-a/--delta-b`，**必須提供 `--seed`**（不再默默使用預設輸入差分 / 預設 seed）。
- `--maximum-constant-subtraction-candidates 0` 與 `--maximum-affine-mixing-output-differences 0` 代表 **精確/無上限枚舉**（可能非常慢）。
- 策略模式會在啟動時印出 **`[Strategy] resolved settings`**（所有自動推導的參數都會列出）。
- Batch 模式只有在「隨機生成 job」（例如 `--batch-job-count N`）時 **才必須**提供 `--seed`。若使用 `--batch-file` 讀入 job，則不需要 seed。
- 當啟用 shared cache 且未明確指定 `--shared-cache-shards`（別名 `--cache-shards`）時，程式可能會依照執行緒數與 shared cache 大小 **自動把 shards 調大**。
- 入口別名：
  - 也可以用 `--mode strategy|detail|auto|legacy` 選擇前端（等價於使用子命令）。
  - 若不帶子命令直接執行，預設使用 legacy/detail 相容的 parser（仍可使用舊的 positional 參數）。

策略模式額外“終點/總量”參數：

- `--total-work N`（別名 `--total`）：設定一個“總數量/終點”。`N` 越大，自動推導的 budget 越大：
  - 單次/批處理：會自動提高每個 job 的 `maximum_search_nodes`（增長較溫和，約按數量級放大）。
  - 批處理：搭配 `--batch` 啟用 batch 模式時，會用 `N` 當作 batch job 數量。
- `--batch`（策略模式專用）：只用來「啟用 batch」，job 數量交給 `--total-work N` 決定（仍需 `--seed`）。

詳細模式參數（長名；舊短名仍可當別名使用）：

- `--addition-weight-cap N`（別名 `--add`，0..31）
- `--constant-subtraction-weight-cap N`（別名 `--subtract`，0..32）
- `--maximum-constant-subtraction-candidates N`（別名 `--maxconst`，0=精確/全部）
- `--maximum-affine-mixing-output-differences N`（別名 `--maxmix`，0=精確/全部）
- `--maximum-search-nodes N`（別名 `--maxnodes`，`0`=不限制）
- `--disable-state-memoization`（別名 `--nomemo`）
- `--enable-verbose-output`（別名 `--verbose`）
- Batch：
  - `--batch-job-count N`（別名 `--batch`）
  - `--batch-file PATH`（從檔案讀入 jobs；每行：`dA dB` 或 `R dA dB`；支援 `0x..`；`#` 開頭為註解）
  - `--thread-count T`（別名 `--threads`，`0`=auto）
  - `--seed S`
  - `--progress-every-jobs N`（別名 `--progress`，`0`=關閉）
  - `--progress-every-seconds S`（別名 `--progress-sec`，`0`=關閉）
  - `--memory-headroom-mib M`（保留約 M MiB 的可用 RAM；預設：`max(2GiB, min(4GiB, avail/10))`）
  - `--memory-ballast`（自適應 ballast：分配/釋放 RAM，讓可用 RAM 盡量維持在 headroom 附近）
  - `--cache-max-entries-per-thread N`（別名 `--cache`，`0`=關閉）
  - `--shared-cache-total-entries N`（別名 `--cache-shared`，`0`=關閉）
  - `--shared-cache-shards S`（別名 `--cache-shards`）
- `--legacy`（Legacy Route-B regression）

Auto 模式參數（只列最常用的 knobs）：

- Breadth 階段（多候選、低資源）：
  - `--auto-breadth-jobs N`（別名 `--auto-breadth-max-runs`）
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T`（0=auto）
  - `--auto-breadth-seed S`（預設：由提供的起始差分推導）
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-maxconst N`
  - `--auto-breadth-heuristic-branch-cap N`（別名 `--auto-breadth-hcap`）
  - `--auto-breadth-max-bitflips F`
  - `--auto-print-breadth-candidates`
- Deep 階段（對 top-K 候選做高資源搜尋）：
  - `--auto-deep-maxnodes N`（0=unlimited）
  - `--auto-max-time T`（只在 deep maxnodes=0 時生效；支援 `3600`、`60m`、`24h`、`30d`、`4w`）
  - `--auto-target-best-weight W`

Auto 模式限制：

- Auto 模式 **不支援 batch**。
- Auto 模式 **必須明確提供** `--delta-a` 與 `--delta-b`（不支援 `--seed` fallback）。
- Auto 模式 **不支援** `--legacy`。

範例：

```bat
test_neoalzette_differential_best_search.exe strategy balanced --round-count 3 --delta-a 0x0 --delta-b 0x1
test_neoalzette_differential_best_search.exe strategy balanced --round-count 4 --batch-job-count 2000 --thread-count 16 --seed 0x1234
test_neoalzette_differential_best_search.exe detail --round-count 3 --delta-a 0x0 --delta-b 0x1 --maximum-search-nodes 5000000
test_neoalzette_differential_best_search.exe auto --round-count 4 --delta-a 0x0 --delta-b 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
test_neoalzette_differential_best_search.exe legacy 3 0x0 0x1 --maxnodes 5000000
```

### `test_neoalzette_linear_best_search.exe`（線性最佳 trail / mask 搜尋）

此程式搜尋 **線性相關**（reverse-mask 模型）。輸入是兩個「輸出 mask」：

- `--output-branch-a-mask MASK_A`（別名：`--out-mask-a`、`--mask-a`）
- `--output-branch-b-mask MASK_B`（別名：`--out-mask-b`、`--mask-b`）

命令列（3 種前端 / 子命令；風格與差分程式一致）：

- `test_neoalzette_linear_best_search.exe strategy <time|balanced|space> --round-count <R> [--output-branch-a-mask <MA> --output-branch-b-mask <MB> | --seed <S>]`
- `test_neoalzette_linear_best_search.exe detail --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`
- `test_neoalzette_linear_best_search.exe auto --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`

範例：

```bat
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --batch-job-count 2000 --thread-count 16 --seed 0x1234
test_neoalzette_linear_best_search.exe auto --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_arx_probabilistic_neutral_bits.exe`（PNB-style + heatmap，會輸出 CSV）

快速用法：

- `test_neoalzette_arx_probabilistic_neutral_bits.exe <rounds> <trials_per_inputbit>`
- 範例：

```bat
test_neoalzette_arx_probabilistic_neutral_bits.exe 1 200000
```

會在目前目錄輸出 `neoalzette_heatmap_r<rounds>_*.csv`。

### `test_neoalzette_arx_probabilistic_neutral_bits_average.exe`（平均 PNB / per-bit neutrality）

此程式 **以原始碼設定**（無命令列）：請編輯 `test_neoalzette_arx_probabilistic_neutral_bits_average.cpp` 內的：

- 固定輸入差分 \((\Delta A,\Delta B)\)
- signature 模式（FULL64 / A32 / byte / masked）
- rounds 清單與 samples 數量

---

## 參考與使用到的算子

- **模加 XOR 差分**：LM2001（`xdp_add_lm2001`、`find_optimal_gamma_with_weight`）
- **常數模加/模減**：bit-vector weight（`diff_addconst_bvweight`、`diff_subconst_bvweight`）
- **模加線性相關**（亦包含於本 repo）：見 `include/arx_analysis_operators/linear_correlation_add_*.hpp`

---

## 授權

MIT License（見 `LICENSE`）。如用於學術研究，請依情境引用原論文。
