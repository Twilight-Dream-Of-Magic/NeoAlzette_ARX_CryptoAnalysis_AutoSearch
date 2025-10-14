## NeoAlzette ARX 自動化分析與搜索框架（工程說明）

> 重要提醒：本專案包含計算密集流程。請不要在不了解計算需求的情況下執行多輪搜索；在個人電腦上僅建議做單輪或少量輪次的快速測試。

本文件從「工程實作與思路」出發，說明本專案如何把 ARX 底層算子、NeoAlzette 一輪黑盒評分器（BlackBox），以及 pDDT / Matsui / cLAT 等搜索組件組裝為可用系統。本文不解析論文細節，只描述工程設計、接口與使用方式。

- 命名空間：全域採用 `TwilightDream`
- 目標產物：
  - `neoalz_diff_search`（差分演示程式，勿長時間運行）
  - `neoalz_lin_search`（線性演示程式，勿長時間運行）
- 已移除的舊模組：`utility_tools.*`、`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`、`highway_table_build*.cpp`

---

### 一、整體結構（工程視角）

本專案採三層設計，彼此解耦、可獨立測試：

1) 底層 ARX 算子（`include/arx_analysis_operators/`）
- 封裝「模加」在差分/線性分析下的精確計算與常用包裝：
  - 差分：`differential_xdp_add.hpp`、`differential_optimal_gamma.hpp`
  - 線性：`linear_correlation_add_logn.hpp`（對數時間）、`linear_correlation_addconst.hpp`（2×2 轉移矩陣鏈，含 32/64 位封裝）
- 工程目標：提供可靠、可審核且可重用的 API，供上層一輪模型直接調用。

2) NeoAlzette 一輪黑盒評分器（BlackBox，`include/neoalzette/` + `src/neoalzette/`）
- 差分：`neoalzette_differential_step.hpp`（聲明）
- 線性：`neoalzette_linear_step.hpp`（聲明）、`src/neoalzette/neoalzette_linear_step.cpp`（定義）
- 思路：
  - 把 NeoAlzette 一輪的所有原語操作（旋轉、XOR、模加/模減、常數注入、線性層 L1/L2）形式化；
  - 以「掩碼/差分」為資料流，做轉置/回推，遇到模加處即調用底層算子取得單步精確權重；
  - 維護相位與輪常數使用集合，返回一輪的「入口掩碼/差分」與總權重（差分為 −log₂DP，線性為 −log₂|corr|）。

3) 搜索框架（`include/arx_search_framework/` + `src/arx_search_framework/`）
- 差分：
  - pDDT 構建（Algorithm 1）：`pddt/pddt_algorithm1.hpp`、`src/arx_search_framework/pddt_algorithm1_complete.cpp`
  - Matsui 閾值搜索（Algorithm 2）：`matsui/matsui_algorithm2.hpp`、`src/arx_search_framework/matsui_algorithm2_complete.cpp`
- 線性：
  - cLAT 結構與查詢（Algorithm 2）：`clat/clat_builder.hpp`（可選 `clat_search.hpp`）
- 工程思路：以 BlackBox 為最小評分單元，pDDT/Highways 作為先驗骨幹，輔以 Country-roads 與分支限界剪枝；線性方向則以 cLAT+SLR 生成候選、BlackBox 精確評分。

---

### 二、如何建置

- 需求：C++20 編譯器（Clang/GCC/MSVC）、CMake 3.14+
- 建置：
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"
```
- 產物：`build/neoalz_diff_search`、`build/neoalz_lin_search`
- 不要在低配環境長時間運行上述可執行檔。

---

### 三、命令列使用（演示程式）

兩個主程式已做參數規範化（支援等號與空白兩種形式）：

1) 差分 `neoalz_diff_search`
- 參數：
  - `--rounds, -r <int>`：輪數（預設 4）
  - `--weight-cap, -w <int>`：權重上限（預設 30）
  - `--start-a <hex>`、`--start-b <hex>`：起始差分 A/B（支援 0x 前綴）
  - `--precompute` / `--no-precompute`：是否預計算 pDDT（預設開啟）
  - `--pddt-seed-stride <int>`：pDDT 種子步長（預設 8）
- 範例：
```bash
./neoalz_diff_search -r 6 -w 32 --start-a 0x1 --start-b 0x0 --no-precompute --pddt-seed-stride 8
```

2) 線性 `neoalz_lin_search`
- 參數：
  - `--rounds, -r <int>`：輪數（預設 4）
  - `--weight-cap, -w <int>`：權重上限（預設 30）
  - `--start-mask-a <hex>`、`--start-mask-b <hex>`：輸出端起始線性掩碼 A/B（逆向）（支援 0x 前綴）
  - `--precompute` / `--no-precompute`：是否預計算 cLAT（預設開啟）
- 範例：
```bash
./neoalz_lin_search -r 6 -w 32 --start-mask-a 0x1 --start-mask-b 0x0 --precompute
```

> 以上程式僅作範例與 smoke test；請勿在筆電/低配機長時間執行。

---

### 四、程式庫接口（直接在 C++ 代碼中使用）

- 差分 pDDT：
```cpp
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
using namespace TwilightDream;

PDDTAlgorithm1Complete::PDDTConfig cfg; cfg.bit_width = 32; cfg.set_weight_threshold(7);
auto entries = PDDTAlgorithm1Complete::compute_pddt(cfg);
```
- Matsui 閾值搜索：
```cpp
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
using namespace TwilightDream;

MatsuiAlgorithm2Complete::SearchConfig sc; sc.num_rounds = 4; sc.initial_estimate = 1e-12;
auto result = MatsuiAlgorithm2Complete::execute_threshold_search(sc);
```
- cLAT 構建與查詢（8 位分塊）：
```cpp
#include "arx_search_framework/clat/clat_builder.hpp"
using namespace TwilightDream;

cLAT<8> clat; clat.build();
clat.lookup_and_recombine(0x12345678u, /*t=*/4, /*weight_cap=*/30,
  [](uint32_t u, uint32_t w, int weight) {/* 使用 (u,w,weight) */});
```
- NeoAlzette + 搜索外殼（示例管線）：
```cpp
#include "neoalzette/neoalzette_with_framework.hpp"
using TwilightDream::NeoAlzetteFullPipeline;

NeoAlzetteFullPipeline::DifferentialPipeline::Config cfg; cfg.rounds = 4;
auto medcp = NeoAlzetteFullPipeline::run_differential_analysis(cfg);
```

---

### 五、工程強調事項（必讀）

- 計算密集：多輪搜索呈指數增長；請從小輪數開始、再逐步放大。
- 單執行緒：目前未做平行化；請不要以為可以自動吃滿多核心。
- 精確一輪評分：BlackBox 僅在模加處產生權重；旋轉/XOR/線性層只做掩碼轉置與組合，不額外加權。
- 一致的命名空間：`TwilightDream`；ARX 搜索框架、NeoAlzette、底層算子皆統一。
- 移除舊模組：`utility_tools.*`、`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`、`highway_table_build*.cpp` 皆已刪除，不再提供/引用。

---

### 六、專案目錄（同步現況）

```
.
├── CMakeLists.txt
├── include/
│   ├── arx_analysis_operators/
│   │   ├── differential_xdp_add.hpp
│   │   ├── differential_optimal_gamma.hpp
│   │   ├── linear_correlation_add_logn.hpp
│   │   └── linear_correlation_addconst.hpp
│   ├── arx_search_framework/
│   │   ├── pddt/pddt_algorithm1.hpp
│   │   ├── matsui/matsui_algorithm2.hpp
│   │   ├── clat/algorithm1_const.hpp
│   │   ├── clat/clat_builder.hpp
│   │   └── README.md
│   └── neoalzette/
│       ├── neoalzette_core.hpp
│       ├── neoalzette_differential_step.hpp
│       ├── neoalzette_linear_step.hpp
│       └── neoalzette_with_framework.hpp
├── src/
│   ├── neoalzette_differential_main_search.cpp
│   ├── neoalzette_linear_main_search.cpp
│   ├── neoalzette/
│   │   └── neoalzette_linear_step.cpp
│   └── arx_search_framework/
│       ├── pddt_algorithm1_complete.cpp
│       └── matsui_algorithm2_complete.cpp
└── papers_txt/  （論文純文字備查；本 README 不解析）
```

---

### 七、疑難排解（常見）

- 連結或重複定義：確認標頭內部工具函式（如 `inline`）標註正確，避免多重定義。
- 名稱不一致：確保所有檔案皆使用 `TwilightDream` 命名空間。
- 編譯通過但運行太慢：減小輪數或提高剪枝門檻（weight cap / initial estimate）。

---

### 授權

MIT License（見 `LICENSE`）。演算法出處屬各論文作者；如用於學術論文，請引用原始論文與本專案。
