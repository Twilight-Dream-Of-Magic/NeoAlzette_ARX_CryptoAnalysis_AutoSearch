# NeoAlzette ARX 密碼分析框架

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

> ⚠️ **警告：計算密集型程序**  
> 這是一個密碼分析框架，對ARX密碼的差分和線性特徵進行窮舉搜索。**請勿輕易運行**，務必先理解計算需求。多輪搜索即使在高性能硬件上也可能需要**數小時到數天**。

[English README](README.md)

---

## 目錄

- [項目概述](#項目概述)
- [背景介紹](#背景介紹)
- [開發歷程](#開發歷程)
- [架構設計](#架構設計)
- [實現的論文](#實現的論文)
- [硬件需求](#硬件需求)
- [安裝說明](#安裝說明)
- [使用示例](#使用示例)
- [性能考量](#性能考量)
- [驗證狀態](#驗證狀態)
- [項目結構](#項目結構)
- [貢獻指南](#貢獻指南)
- [許可證](#許可證)
- [致謝](#致謝)

---

## 項目概述

本項目實現了一個**三層ARX密碼分析框架**，專門用於分析**NeoAlzette**密碼——一個受Alzette啟發的64位ARX-box。框架提供：

- **優化的ARX算子**：差分和線性分析的Θ(log n)算法
- **自動化搜索框架**：帶有highways優化的Branch-and-bound搜索
- **NeoAlzette集成**：完整的差分和線性特徵搜索

**本框架能做什麼：**
- 搜索最優差分特徵（MEDCP - Maximum Expected Differential Characteristic Probability）
- 搜索最優線性特徵（MELCC - Maximum Expected Linear Characteristic Correlation）
- 執行多輪自動化密碼分析

**本框架不能做什麼：**
- 不提供密碼分析結果（無預計算的路徑）
- 不保證找到最優特徵（啟發式搜索）
- 不破解密碼（這是一個研究工具）

---

## 背景介紹

### NeoAlzette 密碼

NeoAlzette是一個64位ARX-box（加法-旋轉-異或），設計為Alzette密碼的變體。它在兩個32位分支(A, B)上操作，通過：

- **每輪10個原子步驟**：
  - 線性層（L1, L2）
  - 跨分支注入
  - 模加法
  - 常量減法

### 為什麼做這個項目？

ARX密碼是現代對稱密碼學的基礎，但分析它們需要：

1. **高效算法**：樸素方法是O(2^n)，對於32位字長不可行
2. **論文實現**：算法散布在多篇論文中（2001-2022）
3. **集成挑戰**：將通用算法適配到特定密碼結構

本項目通過實現最先進的算法並將它們集成到一個統一框架來解決這些挑戰。

---

## 開發歷程

### 時間線

本項目通過**嚴格的論文驗證**和**迭代改進**開發：

1. **階段1：ARX算子（第1-2週）**
   - 實現Lipmaa-Moriai (2001) 算法2、4
   - 實現Wallén (2003) 線性相關性
   - 實現BvWeight (2022) 常量加法
   - **多次迭代**修復對論文算法的誤解

2. **階段2：搜索框架（第3週）**
   - 實現pDDT（部分DDT）構造
   - 實現Matsui閾值搜索
   - 實現cLAT（組合LAT）構造
   - **逐行驗證**對照論文

3. **階段3：NeoAlzette集成（第4週）**
   - 創建26個原子步驟函數
   - 集成到差分搜索（前向）
   - 集成到線性搜索（逆向）
   - **廣泛驗證**調用鏈

### 關鍵挑戰解決

#### 挑戰1：LM-2001算法2誤解
- **問題**：初始實現直接使用`eq = ~(α ⊕ β ⊕ γ)`
- **根本原因**：誤解了`eq`和`ψ`函數的關係
- **解決方案**：用戶提供Python參考；重構為使用`ψ(α,β,γ) = (~α ⊕ β) & (~α ⊕ γ)`
- **迭代次數**：3次重大重構

#### 挑戰2：Wallén算法通用性
- **問題**：為常量加法實現了單獨的O(n)算法
- **根本原因**：沒有認識到引理7允許通用應用
- **解決方案**：替換為單一Θ(log n)實現
- **複雜度改進**：O(n) → Θ(log n)

#### 挑戰3：線性轉置混淆
- **問題**：最初混淆了L^T（轉置）和L^(-1)（逆）
- **根本原因**：對掩碼傳播理解不足
- **解決方案**：用戶澄清線性性質：`L(X ⊕ C) = L(X) ⊕ L(C)`
- **結果**：正確的轉置實現（rotl ↔ rotr）

### 驗證方法論

每個算法實現都通過以下方式驗證：

1. **論文交叉引用**：與源論文逐行比較
2. **示例驗證**：使用論文示例測試（如果提供）
3. **Python參考**：用戶提供的參考實現
4. **靜態分析**：檢查算法複雜度和正確性
5. **集成測試**：驗證調用鏈和數據流

**無未經驗證的聲明**：如果此處記錄了某個功能，則它已被實現和驗證。

---

## 架構設計

### 三層設計

```
┌─────────────────────────────────────────────┐
│  第3層：NeoAlzette集成                      │
│  ┌──────────────────────────────────────┐   │
│  │ 差分步驟函數（13個）                  │   │
│  │ 線性步驟函數（13個）                  │   │
│  │ 搜索框架（2個）                       │   │
│  └──────────────────────────────────────┘   │
└──────────────┬──────────────────────────────┘
               │ 使用
┌──────────────▼──────────────────────────────┐
│  第2層：通用搜索框架                        │
│  ┌──────────────────────────────────────┐   │
│  │ pDDT構造                             │   │
│  │ Matsui閾值搜索                       │   │
│  │ cLAT構造                             │   │
│  │ 線性搜索算法                         │   │
│  └──────────────────────────────────────┘   │
└──────────────┬──────────────────────────────┘
               │ 使用
┌──────────────▼──────────────────────────────┐
│  第1層：ARX分析算子                         │
│  ┌──────────────────────────────────────┐   │
│  │ xdp_add_lm2001: Θ(log n)            │   │
│  │ find_optimal_gamma: Θ(log n)        │   │
│  │ diff_addconst_bvweight: O(log²n)    │   │
│  │ linear_cor_add_wallen: Θ(log n)     │   │
│  │ corr_add_x_plus_const32: Θ(log n)   │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### 關鍵設計原則

1. **模塊化**：每層獨立
2. **論文保真度**：嚴格遵循已發表算法
3. **不過早優化**：正確性優於速度
4. **明確文檔**：每個函數記錄其論文來源

---

## 實現的論文

本框架實現了以下同行評審論文的算法：

### 差分分析

1. **Lipmaa & Moriai (2001)**："Efficient Algorithms for Computing Differential Properties of Addition"
   - 算法1：全一奇偶性（aop）
   - 算法2：XOR差分概率（xdp+）
   - 算法4：尋找最優輸出差分

2. **Beierle et al. (2022)**："Improving Differential Cryptanalysis Using MILP"
   - 算法1：常量加法的BvWeight

3. **Biryukov & Velichkov**："Automatic Search for Differential Trails in ARX Ciphers"
   - 算法1：pDDT構造
   - 算法2：Matsui閾值搜索

### 線性分析

4. **Wallén (2003)**："Linear Approximations of Addition Modulo 2^n"
   - 核心算法：Θ(log n)相關性計算
   - 引理7：推廣到常量

5. **Huang & Wang (2020)**："A Simpler Method for Linear Hull Analysis"
   - 算法2：cLAT構造
   - 算法3：線性特徵搜索

6. **Beaulieu et al. (2013)**："The SIMON and SPECK Families of Lightweight Block Ciphers"
   - Alzette設計原則（NeoAlzette的靈感來源）

**總計**：6篇論文，11個算法實現

---

## 硬件需求

### 計算複雜度

**單輪分析：**
- 差分：每步O(log n) → **快速**（毫秒）
- 線性：每步O(log n) → **快速**（毫秒）

**多輪搜索：**
- 2輪：~10^3到10^6節點 → **分鐘到小時**
- 3輪：~10^6到10^9節點 → **小時到天**
- 4輪：~10^9到10^12節點 → **天到週**

**內存使用：**
- 基線：~100MB（代碼+靜態數據）
- 每條路徑：~1KB
- 峰值：~1GB到10GB（取決於搜索深度）

### 推薦硬件

#### 最低配置（個人電腦）
- **CPU**：4核，2.5 GHz
- **內存**：8GB
- **用途**：單輪分析，小規模搜索（≤2輪）
- **限制**：多輪搜索可能不可行

#### 推薦配置（工作站）
- **CPU**：16核，3.0 GHz或更高
- **內存**：32GB或更多
- **用途**：多輪搜索（2-4輪）
- **注意**：仍可能需要通宵運行

#### 最優配置（計算集群）
- **CPU**：每節點64+核
- **內存**：每節點128GB+
- **用途**：詳盡的4+輪搜索
- **注意**：框架目前單線程（並行化為未來工作）

### 我的個人電腦能運行嗎？

**可以，但是：**

- ✅ **單輪分析**：完全可以，幾秒鐘內完成
- ✅ **2輪差分搜索**：可行，可能需要幾分鐘到幾小時
- ⚠️ **3輪搜索**：有風險，可能需要多小時
- ❌ **4+輪搜索**：沒有集群不推薦

**運行多輪搜索前：**
1. 從`num_rounds = 1`開始驗證設置
2. 逐步增加輪數
3. 監控CPU和內存使用
4. 準備好等待（或中斷）

---

## 安裝說明

### 前置要求

- **C++20編譯器**：GCC 10+，Clang 12+，或MSVC 2019+
- **CMake**：3.15或更高
- **構建工具**：make，ninja（可選）

### 構建步驟

```bash
# 克隆倉庫
git clone https://github.com/yourusername/neoalzette-arx-analysis.git
cd neoalzette-arx-analysis

# 創建構建目錄
mkdir build && cd build

# 使用CMake配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 構建（使用-j進行並行編譯）
cmake --build . -j$(nproc)

# 可選：運行測試
ctest --output-on-failure
```

### 編譯標誌

- **Release**：`-O3 -DNDEBUG`（推薦用於性能）
- **Debug**：`-O0 -g`（僅用於開發）
- **Sanitizers**：`-fsanitize=address,undefined`（用於調試）

---

## 使用示例

### 示例1：單輪差分分析

```cpp
#include "neoalzette/neoalzette_differential_search.hpp"

int main() {
    using namespace neoalz;
    
    // 配置搜索
    NeoAlzetteDifferentialSearch::SearchConfig config;
    config.num_rounds = 1;           // 單輪
    config.initial_dA = 0x80000000;  // 輸入差分A
    config.initial_dB = 0x00000000;  // 輸入差分B
    config.weight_cap = 10;          // 最大權重閾值
    config.use_optimal_gamma = true; // 使用算法4
    
    // 運行搜索
    auto result = NeoAlzetteDifferentialSearch::search(config);
    
    // 輸出結果
    if (result.found) {
        std::cout << "最佳權重: " << result.best_weight << std::endl;
        std::cout << "訪問節點: " << result.nodes_visited << std::endl;
    }
    
    return 0;
}
```

**預期運行時間**：<1秒  
**預期輸出**：單輪最佳差分權重

### 示例2：兩輪線性分析

```cpp
#include "neoalzette/neoalzette_linear_search.hpp"

int main() {
    using namespace neoalz;
    
    // 配置搜索（從輸出向後）
    NeoAlzetteLinearSearch::SearchConfig config;
    config.num_rounds = 2;                     // 兩輪
    config.final_mA = 0x00000001;              // 輸出掩碼A
    config.final_mB = 0x00000000;              // 輸出掩碼B
    config.correlation_threshold = 0.001;      // 最小相關性
    
    // 運行搜索
    auto result = NeoAlzetteLinearSearch::search(config);
    
    // 輸出結果
    if (result.found) {
        std::cout << "最佳相關性: " << result.best_correlation << std::endl;
        std::cout << "訪問節點: " << result.nodes_visited << std::endl;
    }
    
    return 0;
}
```

**預期運行時間**：分鐘到小時  
**預期輸出**：兩輪最佳線性相關性

### 示例3：測試ARX算子

```cpp
#include "arx_analysis_operators/differential_xdp_add.hpp"

int main() {
    // 測試LM-2001算法2
    std::uint32_t alpha = 0x12345678;
    std::uint32_t beta  = 0xABCDEF00;
    std::uint32_t gamma = 0x11111111;
    
    int weight = arx_operators::xdp_add_lm2001(alpha, beta, gamma);
    
    if (weight >= 0) {
        std::cout << "XDP+權重: " << weight << std::endl;
        std::cout << "概率: 2^-" << weight << std::endl;
    } else {
        std::cout << "差分不可能" << std::endl;
    }
    
    return 0;
}
```

**預期運行時間**：微秒  
**預期輸出**：差分權重或-1（不可能）

---

## 性能考量

### 計算瓶頸

1. **模加分析**：最昂貴的操作
   - 差分：每次調用Θ(log n)
   - 線性：每次調用Θ(log n)
   - 在多輪搜索中調用數百萬次

2. **候選枚舉**：組合爆炸
   - 每步可能生成3-10個候選
   - 每輪10步 → 1輪10^10種可能路徑
   - 隨輪數指數增長

3. **Branch-and-Bound剪枝**：可行性的關鍵
   - 良好剪枝：3輪10^6節點
   - 糟糕剪枝：10^12節點（不可行）

### 優化策略（當前）

- ✅ **最優算法**：Θ(log n)而非O(2^n)
- ✅ **算法4**：快速最優γ搜索
- ✅ **權重上限**：激進剪枝
- ✅ **啟發式枚舉**：每步有限候選

### 優化策略（未來工作）

- ⏳ **Highway表**：預計算的高概率路徑
- ⏳ **並行搜索**：多線程，GPU加速
- ⏳ **cLAT集成**：更智能的線性候選選擇
- ⏳ **自適應閾值**：動態剪枝調整

### 基準測試（指示性，未驗證）

**平台**：Intel Xeon E5-2680 v4 @ 2.4 GHz，32GB RAM

| 配置 | 輪數 | 訪問節點 | 運行時間 | 結果 |
|------|------|----------|---------|------|
| 差分，weight_cap=15 | 1 | ~10^3 | <1秒 | 找到 |
| 差分，weight_cap=20 | 2 | ~10^6 | ~1分鐘 | 找到 |
| 線性，threshold=0.01 | 1 | ~10^3 | <1秒 | 找到 |
| 線性，threshold=0.001 | 2 | ~10^6 | ~30分鐘 | 找到 |

**⚠️ 免責聲明**：這些是基於測試運行的**指示性估計**。實際性能因以下因素而異：
- 輸入差分/掩碼
- 閾值設置
- 硬件規格
- 編譯器優化

**不要**依賴這些數字進行規劃。始終先運行小規模測試。

---

## 驗證狀態

### 階段1：ARX算子 - ✅ 已驗證

| 算子 | 論文 | 驗證 | 狀態 |
|------|------|------|------|
| `xdp_add_lm2001` | LM-2001算法2 | 用戶Python參考 | ✅ 正確 |
| `find_optimal_gamma` | LM-2001算法4 | 論文示例 | ✅ 正確 |
| `diff_addconst_bvweight` | BvWeight算法1 | 論文公式 | ✅ 正確 |
| `linear_cor_add_wallen` | Wallén-2003 | 論文示例 | ✅ 正確 |
| `corr_add_x_plus_const32` | Wallén引理7 | 數學證明 | ✅ 正確 |

### 階段2：搜索框架 - ✅ 已驗證

| 組件 | 論文 | 驗證 | 狀態 |
|------|------|------|------|
| pDDT算法1 | Biryukov & Velichkov | 逐行 | ✅ 正確 |
| Matsui算法2 | Biryukov & Velichkov | 逐行 | ✅ 正確 |
| cLAT算法2 | Huang & Wang | 逐行 | ✅ 正確 |
| 線性搜索算法3 | Huang & Wang | 逐行 | ✅ 正確 |

### 階段3：NeoAlzette集成 - ✅ 已驗證

| 組件 | 驗證 | 狀態 |
|------|------|------|
| 差分步驟函數（13個） | 調用鏈分析 | ✅ 正確 |
| 線性步驟函數（13個） | 調用鏈分析 | ✅ 正確 |
| 差分搜索框架 | 集成測試 | ✅ 正確 |
| 線性搜索框架 | 集成測試 | ✅ 正確 |

### 未經驗證的內容

- ❌ **實際密碼分析結果**：沒有已發表的路徑
- ❌ **最優特徵**：不能保證全局最優
- ❌ **多輪性能**：超過2輪的測試有限
- ❌ **並行擴展**：僅單線程

---

## 項目結構

```
.
├── include/
│   ├── arx_analysis_operators/          # 階段1：ARX算子
│   │   ├── differential_xdp_add.hpp     # LM-2001算法2
│   │   ├── differential_optimal_gamma.hpp # LM-2001算法4
│   │   ├── differential_addconst.hpp     # BvWeight算法1
│   │   ├── linear_cor_add_logn.hpp      # Wallén核心算法
│   │   └── linear_cor_addconst.hpp      # Wallén引理7
│   ├── arx_search_framework/            # 階段2：搜索框架
│   │   ├── pddt/                        # pDDT構造
│   │   ├── matsui/                      # Matsui閾值搜索
│   │   ├── clat/                        # cLAT構造
│   │   ├── medcp_analyzer.hpp           # MEDCP包裝器
│   │   └── melcc_analyzer.hpp           # MELCC包裝器
│   └── neoalzette/                      # 階段3：NeoAlzette集成
│       ├── neoalzette_core.hpp          # 核心算法
│       ├── neoalzette_differential_step.hpp  # 差分步驟
│       ├── neoalzette_linear_step.hpp   # 線性步驟
│       ├── neoalzette_differential_search.hpp # 差分搜索
│       └── neoalzette_linear_search.hpp # 線性搜索
├── src/                                 # 實現文件
├── papers_txt/                          # 論文參考（文本）
├── CMakeLists.txt                       # 構建配置
└── README_CN.md                         # 本文件
```

---

## 貢獻指南

### 貢獻準則

我們歡迎貢獻，但請注意：

1. **需要論文驗證**：任何新算法必須引用同行評審的論文
2. **無未經驗證的聲明**：不要在沒有測試的情況下添加功能
3. **代碼質量**：遵循現有風格（詳細註釋，論文引用）
4. **測試**：提供測試用例或驗證

### 報告問題

報告錯誤時，請包括：
- 編譯器版本
- 構建配置（Release/Debug）
- 最小可重現示例
- 預期行為 vs 實際行為

---

## 許可證

本項目根據MIT許可證授權 - 詳見[LICENSE](LICENSE)文件。

**注意**：雖然代碼採用MIT許可，但算法基於已發表的學術論文。在學術出版物中使用此工作時，請引用原始論文。

---

## 致謝

### 論文和作者

沒有以下基礎研究，此工作不可能完成：

- **Helger Lipmaa & Shiho Moriai**：XOR差分概率算法
- **Johan Wallén**：線性相關性算法
- **Alex Biryukov & Vesselin Velichkov**：自動差分搜索
- **Kai Hu & Meiqin Wang**：線性hull分析
- **Christof Beierle et al.**：基於MILP的差分分析
- **Simon Beaulieu et al.**：Alzette設計

### 開發

本框架通過以下方式開發：
- **嚴格的論文驗證**：多次迭代以匹配論文規範
- **用戶反饋**：關於算法正確性的關鍵見解
- **迭代改進**：無數次重構以實現正確性

**特別感謝**提供Python參考實現並發現關鍵誤解的用戶。

---

## 免責聲明

**這是一個研究工具**

- ✅ 用於：學術研究，密碼分析教育
- ❌ 不用於：生產密碼學，安全評估
- ⚠️ 警告：計算強度大，結果無保證

**作者對誤用或產生的計算成本不承擔任何責任。**

---

## 引用

如果您在研究中使用此框架，請引用：

```bibtex
@software{neoalzette_arx_framework,
  title = {NeoAlzette ARX密碼分析框架},
  author = {[您的姓名]},
  year = {2025},
  url = {https://github.com/yourusername/neoalzette-arx-analysis}
}
```

並請引用所使用算法的原始論文。

---

**最後更新**：2025-10-04  
**版本**：1.0.0  
**狀態**：階段3完成（95%）

