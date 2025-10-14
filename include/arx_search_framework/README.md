## ARX 自動化搜索框架（TwilightDream 命名空間）

本目錄提供 ARX 密碼分析中常用的自動化搜索組件，涵蓋差分（pDDT/Matsui）與線性（cLAT）兩大方向，均採用 `TwilightDream` 命名空間。

### 📁 目錄結構

- `pddt/`
  - `pddt_algorithm1.hpp`：差分部分分佈表（pDDT）構建，Algorithm 1
- `matsui/`
  - `matsui_algorithm2.hpp`：Matsui 閾值搜索，Algorithm 2（Highways/Country-roads）
- `clat/`
  - `algorithm1_const.hpp`：線性掩碼空間構建，Algorithm 1（常數子問題）
  - `clat_builder.hpp`：cLAT 構建（預設 8 位分塊），Algorithm 2
  - `clat_search.hpp`：基於 cLAT 的 SLR 搜索（如需）

提示：舊版的通用分析器與框架（`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`）已被移除，請直接使用下述 API 或倚賴頂層示例程式。

### 🚀 快速開始（可執行程式）

專案已提供兩個主程式（僅用於演示，不建議在低配環境長時間運行）：

- 差分：`neoalz_diff_search`
  - 參數：`--rounds/-r`、`--weight-cap/-w`、`--start-a`、`--start-b`、`--precompute/--no-precompute`、`--pddt-seed-stride`
  - 範例：
```bash
./neoalz_diff_search -r 6 -w 32 --start-a 0x1 --start-b 0x0 --no-precompute --pddt-seed-stride 8
```

- 線性：`neoalz_lin_search`
  - 參數：`--rounds/-r`、`--weight-cap/-w`、`--start-mask-a`、`--start-mask-b`、`--precompute/--no-precompute`
  - 範例：
```bash
./neoalz_lin_search -r 6 -w 32 --start-mask-a 0x1 --start-mask-b 0x0 --precompute
```

### 🧩 程式庫 API（C++）

#### 差分 pDDT（Algorithm 1）
```cpp
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
using namespace TwilightDream;

PDDTAlgorithm1Complete::PDDTConfig cfg;
cfg.bit_width = 32;
cfg.set_weight_threshold(7);   // 或 cfg.set_probability_threshold(p)

auto entries = PDDTAlgorithm1Complete::compute_pddt(cfg);
// 或帶統計：
PDDTAlgorithm1Complete::PDDTStats stats;
auto entries2 = PDDTAlgorithm1Complete::compute_pddt_with_stats(cfg, stats);
```

#### Matsui 閾值搜索（Algorithm 2）
```cpp
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
using namespace TwilightDream;

MatsuiAlgorithm2Complete::SearchConfig sc;
sc.num_rounds = 4;
sc.prob_threshold = 0.01;    // 構建 highways 的閾值
sc.initial_estimate = 1e-12; // B_n（0 代表關閉門檻剪枝）

auto result = MatsuiAlgorithm2Complete::execute_threshold_search(sc);
// result.best_trail / result.best_weight / result.best_probability
```

#### cLAT 構建與查詢（Algorithm 2）
```cpp
#include "arx_search_framework/clat/clat_builder.hpp"
using namespace TwilightDream;

cLAT<8> clat;        // 預設 8 位分塊
clat.build();        // 構建表（記憶體與耗時取決於分塊與平台）

// 查詢/重組（SLR 操作的一部分）
clat.lookup_and_recombine(/*v_full=*/0x12345678u, /*t=*/4, /*weight_cap=*/30,
    [](uint32_t u, uint32_t w, int weight){ /* 使用 (u,w,weight) */ });
```

### 📚 參考與備註

- **差分**：Biryukov & Velichkov, “Automatic Search for Differential Trails in ARX Ciphers”。
- **線性**：Huang & Wang (2020), “Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers”。
- 目前程式碼假定 32 位字尺寸；若需 64 位/混合位寬，請在演算法實作與接口處擴展。
- cLAT 構建可能佔用大量記憶體（視分塊與平台而定），請在伺服器或資源允許的環境執行。

### ✅ 目前實現

- **pDDT**：Algorithm 1 完整實作，支持權重/機率閾值與統計輸出
- **Matsui**：Algorithm 2 完整實作，含 Highways/Country-roads 策略與剪枝
- **cLAT**：Algorithm 2 完整實作，提供查詢與 SLR 支援方法

（已移除）舊的通用分析器與框架：`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`。
