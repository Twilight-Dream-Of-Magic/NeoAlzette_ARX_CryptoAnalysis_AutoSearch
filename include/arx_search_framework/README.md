# ARX自動化搜索算法框架

本文件夾包含所有ARX密碼自動化搜索算法框架的實現。

## 📁 文件結構

### pDDT（部分差分分佈表）

```
pddt/
└── pddt_algorithm1.hpp     # Algorithm 1: pDDT構建
```

- **論文**："Automatic Search for Differential Trails in ARX Ciphers"
- **功能**：構建閾值過濾的差分分佈表
- **用途**：差分特徵搜索的預計算表

### cLAT（組合線性近似表）

```
clat/
├── algorithm1_const.hpp    # Algorithm 1: Const(S_Cw)掩碼空間構建
├── clat_builder.hpp        # Algorithm 2: cLAT構建（8位分塊）
└── clat_search.hpp         # Algorithm 3: 自動搜索框架（SLR）
```

- **論文**：Huang & Wang (2020), "Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers"
- **功能**：
  - Algorithm 1: 構建指定權重的掩碼空間
  - Algorithm 2: 8位cLAT (~1.2GB)
  - Algorithm 3: Splitting-Lookup-Recombination (SLR)搜索
- **用途**：線性特徵高效搜索

### Matsui閾值搜索

```
matsui/
└── matsui_algorithm2.hpp   # Algorithm 2: 閾值搜索（Highways/Country-roads）
```

- **論文**："Automatic Search for Differential Trails in ARX Ciphers"
- **功能**：Branch-and-bound閾值搜索
- **策略**：Highways（高概率路徑）vs Country-roads（低概率路徑）

### 分析器

```
├── medcp_analyzer.hpp              # MEDCP（最大期望差分特徵概率）分析
├── melcc_analyzer.hpp              # MELCC（最大期望線性特徵相關性）分析
└── threshold_search_framework.hpp  # 通用閾值搜索框架
```

## 🎯 使用流程

### 1. 差分分析（MEDCP）

```cpp
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
#include "arx_search_framework/medcp_analyzer.hpp"

// 1. 構建pDDT
pDDT_Builder builder;
builder.build(threshold);

// 2. Matsui搜索
MatsuiAlgorithm2 searcher;
auto trail = searcher.search(num_rounds);

// 3. 計算MEDCP
auto medcp = MEDCP_Analyzer::compute(trail);
```

### 2. 線性分析（MELCC）

```cpp
#include "arx_search_framework/clat/algorithm1_const.hpp"
#include "arx_search_framework/clat/clat_builder.hpp"
#include "arx_search_framework/clat/clat_search.hpp"
#include "arx_search_framework/melcc_analyzer.hpp"

// 1. 構建cLAT（預計算）
cLAT<8> clat;
clat.build();

// 2. Algorithm 3搜索
LinearSearchAlgorithm3::Config config;
config.clat_ptr = &clat;
auto result = LinearSearchAlgorithm3::search(config, known_bounds);

// 3. 計算MELCC
auto melcc = MELCC_Analyzer::compute(result.best_trail);
```

## 📊 算法對應表

| 框架 | 論文 | 算法 | 用途 |
|------|------|------|------|
| pDDT | ARX Differential Trails | Algorithm 1 | 差分表構建 |
| Matsui | ARX Differential Trails | Algorithm 2 | 差分搜索 |
| cLAT Const | Huang & Wang 2020 | Algorithm 1 | 掩碼空間 |
| cLAT Build | Huang & Wang 2020 | Algorithm 2 | 線性表構建 |
| cLAT Search | Huang & Wang 2020 | Algorithm 3 | 線性搜索 |

## ✅ 實現狀態

- ✅ pDDT Algorithm 1完整實現
- ✅ Matsui Algorithm 2完整實現
- ✅ cLAT Algorithm 1/2/3完整實現
- ✅ SLR (Splitting-Lookup-Recombination)完整實現
- ✅ MEDCP/MELCC分析器完整實現
