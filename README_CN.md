# NeoAlzette ARX 密碼分析 AutoSearch（文件：繁體中文）

> 提醒：本 repo 含 **計算密集** 的研究/實驗程式。請從小輪數、小樣本數開始。

[English README](README_EN.md)

本專案目前包含：

- **NeoAlzette 核心實作**：`TwilightDream::NeoAlzetteCore`（單輪 `forward/backward`、線性層 `mask0/mask1`、以及跨分支注入 helper）。
- **ARX 論文算子（header-only）**：用於模加/常數加減在差分與線性模型下的權重計算。
- **根工程研究/實驗程式**（`test_*.cpp`）：差分 trail trace、residual-frontier 最佳 trail 搜尋、以及 strict-hull wrapper。
- **PNB / neutral bits 工具線**：目前不包含在此倉庫內，正在另一個獨立倉庫持續開發，暫時不會出現在這個 repo。

## 文件地圖

- **本 README**：面向使用者的建置 / 執行 / CLI 指南。
- **架構 / runtime / checkpoint 文件**：`AUTO_SEARCH_FRAME_ARCHITECTURE_EN.md`
- **嚴格數學 BNB 藍圖**：`AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md`

如果你要確認「目前數學語義到底以哪份文件為準」，請把數學藍圖視為上位嚴格契約，再用架構文件對照當前程式碼承載位置。

## 自動搜尋方法的論文承襲

這個專案的自動搜尋工程，**不只是吸收了 pDDT / cLAT 兩張表的想法**，而是連搜尋偽碼的骨架與中間輪候選生成方式也直接承襲了相關論文：

- **差分側**：直接承接 Biryukov-Velichkov 的 pDDT / threshold-search / Matsui branch-and-bound 路線，但把原本「機率門檻 `DP >= p_thres`」的 pDDT，升級成按**精確整數權重**分層的 `Weight-Sliced pDDT (w-pDDT)`。也就是對固定 \((\alpha,\beta)\) 快取
  `S_t(alpha,beta) = { gamma | w_diff(alpha,beta->gamma) = t }`，
  真值仍由精確枚舉器定義，快取只是可重建加速器。
- **線性側**：數學上直接使用 Schulte-Geers 顯式公式的
  `z = M^T(u xor v xor w)` 與 `|Cor(u,v,w)| = 2^{-wt(z)}`
  這條主軸，工程上則直接吸收 Huang/Wang 2019 那篇「specific correlation weight space + split-lookup-recombine / cLAT」的結構。故本專案目前最準確的說法是：
  `z-shell based Weight-Sliced cLAT`。
- 因此，本 repo 的 residual-frontier 搜尋堆疊（`auto_search_frame` + `auto_subspace_hull`）不只是「借用術語」，而是把論文中的**低權首輪展開 / 中間輪 lookup + recombine / Matsui 式剪枝**，重寫成適合 NeoAlzette 與 checkpoint-resume 的顯式棧搜尋引擎。

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
  - `test_neoalzette_differential_hull_wrapper.cpp`
  - `test_neoalzette_linear_hull_wrapper.cpp`
- **Residual-frontier best-search 核心**
  - `include/auto_search_frame/detail/`
  - `src/auto_search_frame/`
- **Strict-hull / wrapper / collector 層**
  - `include/auto_subspace_hull/`
  - `src/auto_subspace_hull/`
- **PNB / neutral bits 工具線**
  - 目前不在此倉庫，正在另一個獨立倉庫開發中

---

## 建置（Windows / Linux）

### 推薦方式：CMake（跨平台）

這個 repo 已經具備 Linux 執行期分支（`__linux__`）與跨平台
`CMakeLists.txt`。CMake 只會在 Windows 連結 `psapi`，並在雙系統上都
使用 `Threads::Threads`。

在 Windows 上，目前最穩妥的建議是顯式使用 `-G Ninja`，這樣比較符合
repo 目前的 `CLANG64` / MinGW-clang 流程，也能避開 Visual Studio
generator 在這類工作區路徑上的一些坑。

Windows：

```powershell
cmake -G Ninja -S . -B build\windows-release -DNEOALZETTE_BUILD_PROGRAMS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build\windows-release --config Release --parallel
```

Linux：

```bash
cmake -S . -B build/linux-release -DNEOALZETTE_BUILD_PROGRAMS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-release --parallel
```

### 快速建置腳本

- Windows：執行 `build_and_test.bat`
- Linux：執行 `chmod +x ./build_and_test.sh && ./build_and_test.sh`
- `build_and_test.bat` 保留原本的直接 `clang++` 流程，會把 `.exe` 輸出到 repo 根目錄。
- `build_and_test.sh` 使用 CMake，Linux binary 會輸出到 `build/linux-release/`。
- PNB / neutral bits 工具目前不由此倉庫建置；它們正在另一個獨立倉庫開發中。
- best-search 對外 public header 仍維持穩定 façade：
  - `include/auto_search_frame/test_neoalzette_linear_best_search.hpp`
  - `include/auto_search_frame/test_neoalzette_differential_best_search.hpp`
- best-search 核心已搬到：
  - `include/auto_search_frame/detail/`
  - `src/auto_search_frame/`
- strict-hull collector / wrapper 支撐層位於：
  - `include/auto_subspace_hull/`
  - `src/auto_subspace_hull/`
- linear / differential best-search 與 hull wrapper 現在共用同一組可恢復 core 與 build source list：
  - `src/auto_search_frame/best_search_shared_core.cpp`
  - `src/auto_search_frame/linear_best_search_engine.cpp`
  - `src/auto_search_frame/differential_best_search_engine.cpp`
  - `src/auto_subspace_hull/linear_best_search_collector.cpp`
  - `src/auto_subspace_hull/differential_best_search_collector.cpp`
  - `src/auto_search_frame_bnb_detail/`

### Windows 直接編譯（進階 / 可選）

若你要看**當前**直接編譯的完整 source list，請以：

- `build_and_test.bat`
- `CMakeLists.txt`

為準。best-search / hull-wrapper 現在會共享 residual-frontier core、BNB detail source，以及 `auto_subspace_hull` collector 層；直接編譯命令列很長，且會隨程式碼演進，因此不再在這份 README 重複維護整段 source list。

相對穩定、仍適合手打的簡單 trace target 如下：

```bat
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_arx_trace.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_trace.exe
```

---

## 執行方式

以下執行範例以 Windows 的 `.exe` 名稱表示。若在 Linux 上執行，請改用
相同 target 名稱但**不帶 `.exe`**。如果你是用 `build_and_test.sh` 或上面的
Linux CMake 指令建置，binary 會位於 `build/linux-release/`。

如果你不想只看原始 `--help` 輸出，而是想直接看「這麼多情況到底該怎麼跑」，
請直接打開 `program command.txt`。那份檔案現在就是這個工程的場景化命令手冊，
包含：

- build / CLI 可用性檢查，
- exact-best strict 跑法，
- threshold-certified 跑法，
- explicit-cap exact hull 收集，
- auto 模式，
- 以及 Windows / Linux 的 batch wrapper 工作流。

### `test_neoalzette_arx_trace.exe`（逐步差分 trace）

- 無參數。
- 會 trace 兩組起始差分：\((\Delta A,\Delta B)=(1,0)\) 與 \((0,1)\)。
- 內部使用：
  - LM2001 最佳 \(\gamma\) 算子（模加）
  - 精確整數權重封裝（常數模減；主路徑直接使用 `diff_subconst_exact_weight_ceil_int` 這類 exact 命名接口）
  - **Affine-derivative** 的注入模型（以 rank 作為權重）

```bat
test_neoalzette_arx_trace.exe
```

### `test_neoalzette_differential_best_search.exe`（單次最佳 trail 搜尋 / auto pipeline）

這個可執行檔目前的責任範圍是：

- `strategy` / `detail`：單次 best-search。輸入可以是明確起始差分，或在提供 `--seed` 時由 RNG 生成一組起始差分。
- `auto`：對一組**明確**起始差分做 breadth 掃描 -> deep 搜尋。
- 多 job 的 batch breadth->deep **已不再由這個入口承擔**；請改用 `test_neoalzette_differential_hull_wrapper.exe`。

命令列（3 種前端 / 子命令）：

- **策略模式（推薦，參數較少）**：
  - `test_neoalzette_differential_best_search.exe strategy <time|balanced|space> --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [strategy_flags]`
- **詳細模式（完整長參數）**：
  - `test_neoalzette_differential_best_search.exe detail --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [detail_flags]`
- **Auto 模式（兩階段：breadth 掃描 -> deep 搜尋）**：
  - `test_neoalzette_differential_best_search.exe auto --round-count <R> --delta-a <DA> --delta-b <DB> [auto_flags]`

注意：

- 在 `strategy` / `detail` 中，如果省略 `--delta-a/--delta-b`，**就必須提供 `--seed`**。
- `--mode strategy|detail|auto` 可直接選擇前端，等價於使用子命令。
- `strategy` 模式啟動時會印出 **`[Strategy] resolved settings`**，列出自動推導後的設定。
- `time` 策略會關閉啟發式，通常會比 `balanced` / `space` 慢很多。
- Auto 模式 **必須明確提供** `--delta-a` 與 `--delta-b`，而且 **不支援 batch**。

單次搜尋 / detail 常用參數：

- `--total-work N`（別名 `--total`）：自動放大單次搜尋的 node budget 與相關 cache 配置
- `--addition-weight-cap N`（別名 `--add`）與 `--constant-subtraction-weight-cap N`（別名 `--subtract`）
- `--maximum-search-nodes N`（別名 `--maxnodes`）與 `--maximum-search-seconds T`（別名 `--maxsec`）
- `--target-best-weight W`（別名 `--target-weight`）
  若在其他 strict 條件都成立時命中，輸出現在會標成 `best_weight_certification=threshold_target_certified`。
  這和 `exact_best_certified` 是兩回事。
- `--search-mode strict|fast`
- `--enable-pddt`、`--disable-pddt`、`--pddt-max-weight W`
- `--disable-state-memoization`（別名 `--nomemo`）與 `--enable-verbose-output`（別名 `--verbose`）
- `--memory-headroom-mib M`、`--memory-ballast`、`--allow-high-memory-usage`、`--rebuildable-reserve-mib M`
- `--resume PATH`、`--runtime-log PATH`、`--checkpoint-out PATH`、`--checkpoint-every-seconds S`

Auto 階段常用參數：

- Breadth 階段：
  - `--auto-breadth-jobs N`（別名 `--auto-breadth-max-runs`）
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T`
  - `--auto-breadth-seed S`
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-max-bitflips F`
  - `--auto-breadth-heuristic-branch-cap N`（別名 `--auto-breadth-hcap`，因 breadth 為 strict，所以會被忽略）
  - `--auto-print-breadth-candidates`
- Deep 階段：
  - `--auto-deep-maxnodes N`
  - `--auto-max-time T`
  - `--auto-target-best-weight W`
    若在其他 strict 區域條件都成立時命中，輸出會標成 `best_weight_certification=threshold_target_certified`。
    它不代表 `exact_best_certified`。
範例：

```bat
test_neoalzette_differential_best_search.exe strategy balanced --round-count 4 --delta-a 0x0 --delta-b 0x1
test_neoalzette_differential_best_search.exe detail --round-count 4 --seed 0x1234 --maximum-search-nodes 5000000
test_neoalzette_differential_best_search.exe auto --round-count 4 --delta-a 0x0 --delta-b 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_linear_best_search.exe`（單次線性最佳 trail / mask 搜尋）

此程式搜尋 **線性相關**（reverse-mask 模型）。輸入是兩個「輸出 mask」：

- `--output-branch-a-mask MASK_A`（別名：`--out-mask-a`、`--mask-a`）
- `--output-branch-b-mask MASK_B`（別名：`--out-mask-b`、`--mask-b`）

這個可執行檔目前的責任範圍是：

- `strategy` / `detail`：單次 best-search。輸入可以是明確 mask pair，或在提供 `--seed` 時由 RNG 生成一組 mask pair。
- `auto`：對一組**明確** mask pair 做 breadth 掃描 -> deep 搜尋。
- 多 job 的 batch breadth->deep **已不再由這個入口承擔**；請改用 `test_neoalzette_linear_hull_wrapper.exe`。

命令列（3 種前端 / 子命令；風格與差分程式一致）：

- `test_neoalzette_linear_best_search.exe strategy <time|balanced|space> --round-count <R> [--output-branch-a-mask <MA> --output-branch-b-mask <MB> | --seed <S>]`
- `test_neoalzette_linear_best_search.exe detail --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`
- `test_neoalzette_linear_best_search.exe auto --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`

注意：

- 在 `strategy` / `detail` 中，如果省略兩個 mask，**就必須提供 `--seed`**。
- 這裡也支援 `--mode strategy|detail|auto`。
- Auto breadth 一律走 **strict**，所以 `--auto-breadth-hcap` 只為 CLI 相容性保留，實際上會被忽略。

單次搜尋 / detail 常用參數：

- `--total-work N`（別名 `--total`）
- `--maximum-search-nodes N`（別名 `--maxnodes`）與 `--maximum-search-seconds T`（別名 `--maxsec`）
- `--target-best-weight W`（別名 `--target-weight`）
  若在其他 strict 條件都成立時命中，輸出現在會標成 `best_weight_certification=threshold_target_certified`。
  這和 `exact_best_certified` 是兩回事。
- `--search-mode strict|fast`
- `--addition-weight-cap N`、`--constant-subtraction-weight-cap N`
- `--maximum-injection-input-masks N`
- `--maximum-round-predecessors N`
- `--enable-z-shell`、`--disable-z-shell`、`--z-shell-max-candidates N`
- `--disable-state-memoization`、`--enable-verbose-output`
- `--memory-headroom-mib M`、`--memory-ballast`、`--allow-high-memory-usage`、`--rebuildable-reserve-mib M`
- `--resume PATH`、`--runtime-log PATH`、`--checkpoint-out PATH`、`--checkpoint-every-seconds S`

Auto 階段常用參數：

- Breadth 階段：
  - `--auto-breadth-jobs N`（別名 `--auto-breadth-max-runs`）
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T`
  - `--auto-breadth-seed S`
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-max-bitflips F`
  - `--auto-print-breadth-candidates`
- Deep 階段：
  - `--auto-deep-maxnodes N`
  - `--auto-max-time T`
  - `--auto-target-best-weight W`
    若在其他 strict 區域條件都成立時命中，輸出會標成 `best_weight_certification=threshold_target_certified`。
    它不代表 `exact_best_certified`。
範例：

```bat
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1
test_neoalzette_linear_best_search.exe detail --round-count 4 --seed 0x1234 --maximum-search-nodes 5000000
test_neoalzette_linear_best_search.exe auto --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_*_hull_wrapper.exe`（strict hull wrapper / batch endpoint 聚合）

兩個 hull wrapper 是目前面向 paper 的 batch 端點：

- `test_neoalzette_differential_hull_wrapper.exe`
- `test_neoalzette_linear_hull_wrapper.exe`

它們共同提供：

- 單來源 strict hull 收集，
- batch source selection（`breadth -> deep -> strict hull`），
- `source_hulls` / `endpoint_hulls` 的共享聚合，
- stage / job 邊界的 batch checkpoint / resume，
- 以及可審計的 batch runtime event log。

best weight 認證文字：

- `best_weight_certified=1` 表示 best-search 階段認證了 exact best weight。
- `best_weight_certification=threshold_target_certified` 表示在其他 strict 區域條件都成立時，命中了目標 threshold。
- 後者屬於 threshold statement，不是 exact-best statement。

Wrapper 側加速器開關：

- 差分 wrapper：`--enable-pddt`、`--disable-pddt`、`--pddt-max-weight W`
- 線性 wrapper：`--enable-z-shell`、`--disable-z-shell`、`--z-shell-max-candidates N`
- 線性 wrapper 一律跑 strict mode，所以非零的 `--z-shell-max-candidates` 會被強制改回 `0`

快速命令範例：

```bat
test_neoalzette_differential_hull_wrapper.exe --round-count 4 --delta-a 0x0 --delta-b 0x1 --enable-pddt --pddt-max-weight 12 --collect-weight-window 0
test_neoalzette_differential_hull_wrapper.exe --round-count 4 --batch-job-count 512 --seed 0x1234 --thread-count 16 --enable-pddt --pddt-max-weight 12 --auto-breadth-maxnodes 1048576 --auto-deep-maxnodes 0 --auto-max-time-seconds 3600 --collect-weight-window 0
test_neoalzette_linear_hull_wrapper.exe --round-count 4 --batch-file linear_jobs.txt --thread-count 16 --auto-breadth-maxnodes 1048576 --auto-deep-maxnodes 0 --auto-max-time-seconds 3600 --collect-weight-window 0
```

Batch 檔案格式：

- differential wrapper：每個非空白行是 `delta_a delta_b` 或 `round_count delta_a delta_b`
- linear wrapper：每個非空白行是 `mask_a mask_b` 或 `round_count mask_a mask_b`
- 數字可用十六進位（`0x...`）或十進位；可帶逗號；`#` 開頭視為註解

Batch resume 相關參數：

- `--batch-checkpoint-out PATH`
  - 在對應 stage / job 邊界寫出 batch checkpoint。
- `--batch-resume PATH`
  - 從 batch checkpoint 恢復。
  - wrapper 會自動判斷 `PATH` 是：
    - **source-selection checkpoint**（`*_HullBatchSelection`），或
    - **selected-source strict-hull checkpoint**（`*_HullBatch`）。
- `--batch-runtime-log PATH`
  - 輸出 wrapper batch pipeline 的 runtime event log。

目前的 resume 語義是刻意保守的：

- **Selection checkpoint**
  - 保存 source-selection / breadth 階段狀態：
    - jobs，
    - 已完成的 breadth job flags，
    - 累積 breadth nodes，
    - 當前 `top_candidates`，
    - stage（`selection_breadth` / `selection_deep_ready`）。
- **Strict-hull checkpoint**
  - 保存 selected-source strict-hull 階段狀態：
    - selected jobs，
    - per-job strict-hull summary，
    - 已完成的 strict-hull job flags，
    - combined callback aggregator（`source_hulls`、`endpoint_hulls`、可選 stored trails）。
- **Resume 粒度**
  - batch resume 是 **stage / job 邊界恢復**；
  - 不主張對 batch 內某個 in-flight job 做 same-node DFS resume；
  - same-node DFS resume 仍屬於嵌入式 single-run best-search engine 自己的語義。

Batch runtime-event 契約：

- `batch_start`
  - 記錄 batch 啟動時的 checkpoint / runtime-log 路徑與 resume 意圖。
- `batch_resume_start`
  - 載入 batch checkpoint 後寫出。
  - 包含：
    - `checkpoint_kind`，
    - `stage`，
    - `batch_resume_fingerprint_hash`，
    - `batch_resume_fingerprint_completed_jobs`，
    - `batch_resume_fingerprint_payload_count`，
    - `batch_resume_fingerprint_payload_digest`，
    - strict-hull checkpoint 額外還有 `source_hull_count` / `endpoint_hull_count` / `collected_trail_count`。
- `batch_checkpoint_write`
  - 每次成功寫出 batch checkpoint 後寫出。
  - 包含：
    - `checkpoint_kind`，
    - `checkpoint_reason`，
    - `checkpoint_path`，
    - 同一組 `batch_resume_fingerprint_*` 欄位。
- `batch_stop`
  - wrapper batch invocation 結束時寫出。
  - 記錄最終 job 數、selected-source 數，以及可用時的最終 strict-hull fingerprint。

閱讀與引用時請注意：

- 真正承載數學語義的仍然是執行中的 / checkpoint 內的 combined callback aggregator，
  不是 console summary，也不是 CSV。
- runtime log 是這個 batch object 的審計軌跡，而不是 batch object 本身。

回歸測試：

- `build_and_test.bat` 會跑 wrapper 的 selftest。
- `QA_checkpoint_resume.ps1` 目前會驗證：
  - single-run best-search checkpoint / resume，
  - auto pipeline checkpoint / resume，
  - wrapper batch selection checkpoint / resume，
  - wrapper strict-hull checkpoint / resume，
  - 以及 wrapper batch runtime-event / fingerprint 的連續性。

### PNB / neutral bits 工具現況

PNB / neutral bits 工具**目前不包含在此倉庫內**。

先前的 PNB 工作區目前不在這個 repo；相關工作正在**另一個獨立倉庫**重新整理並持續開發。

因此，對目前這個倉庫而言：

- 不存在 `pnb_distinguisher/` 目錄，
- 不存在根工程的 PNB build target，
- 這個 repo 應被理解為 NeoAlzette core + ARX analysis + residual-frontier search / hull 的主倉庫。

---

## 參考與使用到的算子

- **模加 XOR 差分**：LM2001（`xdp_add_lm2001`、`find_optimal_gamma_with_weight`）
- **常數模加/模減**：精確整數權重封裝（`diff_addconst_exact_weight_ceil_int`、`diff_subconst_exact_weight_ceil_int`；舊的 `diff_*_bvweight` 別名已移除，避免和近似 BvWeight 混淆）
- **模加線性相關**（亦包含於本 repo）：見 `include/arx_analysis_operators/linear_correlation_add_*.hpp`

---

## 授權

MIT License（見 `LICENSE`）。如用於學術研究，請依情境引用原論文。
