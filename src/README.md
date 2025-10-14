## NeoAlzette ARX 分析與搜索框架總覽（BlackBox 說明）

本檔說明 `src/` 下的核心可執行項目與黑盒（BlackBox）分析邏輯，並對支撐它們的數學原理與搜索框架作一個精簡而系統的總結。配合 `include/` 內的對應頭檔可快速定位實作。

### 1) BlackBox 是什麼？

- 本專案中的 BlackBox 指「一輪 NeoAlzette 模型的精確差分/線性評分器」，它不做全域枚舉，而是：
  - 將輸入/輸出差分或線性掩碼，沿著一輪 NeoAlzette 的資料流（含旋轉、XOR、模加/模減、常數注入與線性層）做嚴格的轉置/回推；
  - 在需要時調用經過驗證的底層 ARX 算子（差分：LM-2001；線性：Wallén/carry-矩陣）計算單步精確權重；
  - 最終得到一輪的「入輪掩碼/差分」與其總權重（差分為 −log₂DP，線性為 −log₂|corr|），以及對應相位與使用到的輪常數集合。

- 這個 BlackBox 提供兩條路徑：
  - 差分路徑：`TwilightDream::diff_one_round_analysis(...)`（聲明見 `include/neoalzette/neoalzette_differential_step.hpp`）
  - 線性路徑：`TwilightDream::linear_one_round_backward_analysis(...)`（見 `include/neoalzette/neoalzette_linear_step.hpp`；定義於 `src/neoalzette/neoalzette_linear_step.cpp`）

它們是搜索演算法（pDDT、Matsui、cLAT+SLR）的最小計分單元與過濾器。

### 2) 底層 ARX 數學原理（BlackBox 依賴）

- 差分（XOR differential of addition）：採用 Lipmaa–Moriai 2001（LM-2001）
  - Algorithm 2：計算給定 (α, β → γ) 的權重（−log₂DP）。
  - Algorithm 4：搜尋最佳 γ 與其權重（用於 max_γ xdp⁺）。
  - 封裝於 `include/arx_analysis_operators/differential_xdp_add.hpp` 與 `differential_optimal_gamma.hpp`。

- 線性（Linear correlation of addition）：兩種等價且互補的實現供使用情境選擇
  - Wallén（2003）對數時間演算法：`linear_cor_add_wallen_logn(u, v, w)`，快速給出權重（|corr|=2^{-W}）。
  - 2×2 carry 轉移矩陣鏈（精確 O(n)）：`linear_correlation_addconst.hpp`，支援變量-常數/變量-變量與 32/64 位；並提供便捷封裝 `linear_x_modulo_{plus,minus}_const{32,64}`。

- 其他基元：
  - 模加/模減與旋轉的轉置、round constants 的相位計入、線性層 L1/L2 的轉置，皆封裝於 `include/neoalzette/neoalzette_core.hpp` 與 step 檔案。

### 3) 搜索框架與數學依據（如何把 BlackBox 串起來）

- pDDT（Partial Difference Distribution Table）— 來源：Biryukov & Velichkov
  - 檔案：`include/arx_search_framework/pddt/pddt_algorithm1.hpp`、`src/arx_search_framework/pddt_algorithm1_complete.cpp`
  - 內容：根據門檻（機率或權重）構建部分差分表 D = {(α, β, γ, p)}，並支援統計。核心數學來自 XOR 模加差分的可行性與單步 DP 的單調性（前綴 monotonicity），用以剪枝。

- Matsui Algorithm 2（Threshold Search）— 來源：Biryukov & Velichkov 將 Matsui 思想應用於 ARX
  - 檔案：`include/arx_search_framework/matsui/matsui_algorithm2.hpp`、`src/arx_search_framework/matsui_algorithm2_complete.cpp`
  - 思想：以 pDDT 的「Highways」作為高機率骨幹，必要時使用「Country-roads」補全；使用分支限界（Branch-and-Bound）與動態門檻 B_n 進行剪枝與估計。

- cLAT + SLR（Linear, Huang & Wang 2020）
  - 檔案：`include/arx_search_framework/clat/algorithm1_const.hpp`、`clat_builder.hpp`、（如需）`clat_search.hpp`
  - Algorithm 1：構建限制條件下的掩碼空間；Algorithm 2：以 m 位分塊構建 cLAT；Algorithm 3：SLR 將 m 位塊重組回 32 位，與 BlackBox 線性評分器配合進行高效搜尋。

### 4) `src/` 內的可執行項目

- `neoalzette_differential_main_search.cpp`（差分）
  - 說明：標準化 CLI，提供 `--rounds/-r`、`--weight-cap/-w`、`--start-a`、`--start-b`、`--precompute/--no-precompute`、`--pddt-seed-stride`，內部呼叫 pDDT 與 Matsui（以及 BlackBox）構成的管線。
  - 注意：此程式會進行計算密集操作，請務必在合適的環境執行。

- `neoalzette_linear_main_search.cpp`（線性）
  - 說明：標準化 CLI，提供 `--rounds/-r`、`--weight-cap/-w`、`--start-mask-a`、`--start-mask-b`、`--precompute/--no-precompute`，內部呼叫 cLAT + SLR 與 BlackBox 線性評分器。

兩者的呼叫皆透過 `include/neoalzette/neoalzette_with_framework.hpp` 的外殼類 `NeoAlzetteFullPipeline` 封裝，避免使用者直接接觸底層複雜細節。

### 5) 設計準則與可擴展性

- 可重複使用的「一輪評分黑盒」：將演算法與密碼結構解耦，便於把 BlackBox 接到不同的搜索演算法。
- 嚴格使用經典文獻公式：差分（LM-2001）與線性（Wallén/carry 矩陣）均可審核與替換。
- 32/64 位可擴展：核心 API 接口採用 n 參數與掩碼截斷，便於擴展到 64 位。
- 搜索框架組件化：pDDT/Matsui/cLAT 相互獨立又可拼裝，適應差分、線性、差分-線性混合等研究場景。

### 6) 推薦閱讀與對照

- Biryukov & Velichkov, "Automatic Search for Differential Trails in ARX Ciphers"（pDDT 與 Matsui 在 ARX 中的應用）
- Wallén, "Linear Approximations of Addition Modulo 2^n", FSE 2003（線性相關度對數算法與理論）
- Lipmaa & Moriai, 2001（模加差分權重與最佳 γ 搜索）
- Huang & Wang, 2020（cLAT 與 SLR 的線性搜索方法）

如需將 BlackBox 嵌入你的研究代碼或替換某一子模塊，建議從 `neoalzette_*_step.*` 開始，並對照 `arx_analysis_operators/` 內的底層算子接口。