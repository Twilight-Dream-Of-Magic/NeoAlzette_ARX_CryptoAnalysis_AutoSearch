# ARX分析算子（底層優化實現）

本文件夾包含所有底層ARX密碼分析算子的最優化實現。

## 📁 文件結構

### 差分分析算子

1. **`differential_xdp_add.hpp`** - XOR差分模加法（變量-變量）
   - 論文：Lipmaa & Moriai (2001)
   - 算法：LM-2001公式
   - 複雜度：**O(1)** 位運算
   - 函數：`xdp_add_lm2001(α, β, γ)`

2. **`differential_addconst.hpp`** - 常量加法差分（變量-常量）
   - 論文：Bit-Vector Differential Model (2022)
   - 算法：Algorithm 1 (BvWeight)
   - 複雜度：**O(log²n)** 對數複雜度
   - 函數：`diff_addconst_bvweight(Δx, K, Δy)`

### 線性分析算子

3. **`linear_cor_add.hpp`** - 線性相關度（變量-變量）
   - 論文：Wallén (2003), FSE 2003
   - 算法：M_n^T矩陣方法
   - 複雜度：**O(n)** 線性複雜度
   - 函數：`linear_cor_add_wallen(α, β, γ)`

4. **`linear_cor_addconst.hpp`** - 常量加法線性（變量-常量）
   - 論文：Wallén (2003)
   - 算法：Bit-wise Carry DP
   - 複雜度：**O(n)** 精確方法
   - 函數：`corr_add_x_plus_const32(α, β, K, n)`

### 輔助函數

5. **`bitvector_ops.hpp`** - 位向量操作
   - 論文：Bit-Vector Differential Model (2022)
   - 函數：`HW`, `Rev`, `Carry`, `RevCarry`, `LZ`, `ParallelLog`, `ParallelTrunc`
   - 用於BvWeight算法

## 🎯 使用示例

```cpp
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/linear_cor_add.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

using namespace neoalz::arx_operators;

// 差分分析（變量-變量）
int weight = xdp_add_lm2001(0x1, 0x1, 0x2);

// 差分分析（變量-常量）
int weight_const = diff_addconst_bvweight(0x1, 0x5, 0x4);

// 線性分析（變量-變量）
int cor_weight = linear_cor_add_wallen(0x1, 0x1, 0x1);

// 線性分析（變量-常量）
auto [corr, weight_lin] = corr_add_x_plus_const32(0x1, 0x1, 0x5, 32);
```

## 📊 複雜度對比

| 算子 | 論文 | 複雜度 | 精確度 |
|------|------|--------|--------|
| xdp⁺ (變量-變量) | LM-2001 | O(1) | 精確 |
| xdp⁺ (變量-常量) | BvWeight | O(log²n) | 近似 |
| cor (變量-變量) | Wallén M_n^T | O(n) | 精確 |
| cor (變量-常量) | Wallén DP | O(n) | 精確 |

## ✅ 優化狀態

- ✅ 所有算子對照論文實現
- ✅ 使用最優複雜度算法
- ✅ 變量-變量 vs 變量-常量嚴格區分
- ✅ 無AVX/SIMD極端優化（保持可讀性）
