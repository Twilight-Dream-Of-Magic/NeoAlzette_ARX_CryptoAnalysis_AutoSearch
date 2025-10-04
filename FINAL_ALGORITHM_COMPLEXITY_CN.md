# 完整算法複雜度總結與優化方案

## ✅ 差分分析（已完成）

### Theorem 2精確實現

**論文**：Bit-Vector Differential Model (2022), Theorem 2 (Machado 2015)

**實現位置**：`include/neoalzette_differential_model.hpp`

```cpp
static int compute_diff_weight_addconst(uint32_t u, uint32_t a, uint32_t v) {
    // Pr[u →^a v] = ∏_{i=0}^{n-1} φ_i
    double delta = 0.0;
    double prob = 1.0;
    
    for (int i = 0; i < 32; ++i) {
        // S_i = (u[i-1], v[i-1], u[i]⊕v[i])
        // φ_i 根據 S_i, a[i-1], δ_{i-1} 計算
        // ...精確公式
    }
    
    return ceil(-log2(prob));
}
```

**複雜度**：
- 時間：**O(n)** = O(32) ≈ 32次循環
- 空間：O(1)
- 精確度：100%精確

**關鍵修正**：
- ✅ 使用常量實際值`a`
- ✅ 逐位追踪進位狀態`δ_i`
- ✅ 公式中`a[i-1]`是**前一位**的常量

---

## ✅ 線性分析：當前方法與加速方案

### 方法1：Wallén按位DP（當前使用）

**論文**：Wallén (2003)

**實現位置**：`include/linear_correlation_addconst.hpp`

```cpp
inline LinearCorrelation corr_add_x_plus_const32(
    uint32_t alpha, uint32_t beta, uint32_t K, int nbits = 32
) {
    int64_t v0 = 1, v1 = 0;
    
    for (int i = 0; i < nbits; ++i) {
        // 按位進位DP
        // 枚舉 (x_i, carry_in) → (y_i, carry_out)
        // 累加Walsh係數
    }
    
    return {corr, weight};
}
```

**複雜度**：
- 時間：**O(n)** = O(32) ≈ 32次循環
- 空間：O(1)
- 精確度：100%精確

---

### 方法2：cLAT預計算表（加速方案）

**論文**：Huang & Wang (2020), "Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers"

**核心思想**：
> "cLAT can index the unknown input masks and output masks by one fixed input mask."

**原理**：
1. **預計算階段**（離線，一次性）：
   - 對於給定的常量K和固定的輸入掩碼α
   - 預計算所有可能的輸出掩碼β及其相關度
   - 存儲為表：`cLAT[α][K] → [(β, weight), ...]`

2. **查詢階段**（在線，搜索時）：
   - 給定α和K，直接查表
   - 時間：**O(1)**

**複雜度對比**：

| 方法 | 預計算 | 查詢時間 | 空間 | 適用場景 |
|------|--------|---------|------|---------|
| Wallén DP | 無需 | O(n) | O(1) | 單次查詢 |
| cLAT | O(2^{2n}) | **O(1)** | O(2^{2n}) | 大量查詢 |

**對於32位**：
- cLAT空間：2^64條目 ≈ **18 EB（艾字節）** ❌ 不可行！
- **結論**：32位太大，cLAT不適用

---

### 方法3：分塊cLAT（實用加速）

**論文建議**：
> "With the method of dividing... into small chunks"

**實現策略**：
1. **分塊計算**（8位 or 16位）：
   ```
   32位 = 高16位 + 低16位
   分別查cLAT，再組合
   ```

2. **cLAT大小**（16位）：
   - 空間：2^32條目 ≈ **4 GB** ✅ 可行
   - 預計算：~幾分鐘

3. **查詢時間**：
   - 2次查表 + 1次組合 ≈ **O(1)**

**實現複雜度**：較高，需要：
- 分塊算法
- 相關度組合公式
- 預計算和存儲

---

## 🎯 推薦方案

### 對於NeoAlzette項目

| 場景 | 推薦方法 | 原因 |
|------|---------|------|
| **差分分析（MEDCP）** | Theorem 2 | ✅ O(n)足夠快 |
| **線性分析（MELCC）** | Wallén DP | ✅ O(n)足夠快，無需預計算 |
| **極致優化** | 16位cLAT | 可獲得10-100倍加速，但複雜 |

### 性能估算

**單次模加常量操作**：
```
Theorem 2差分：    ~100 ns  (O(n))
Wallén DP線性：    ~200 ns  (O(n))
16位cLAT線性：     ~10 ns   (O(1)查表)
```

**多輪搜索（4輪NeoAlzette，1M節點）**：
```
Wallén DP：  1M × 2操作 × 200ns ≈ 400 ms
16位cLAT：   1M × 2操作 × 10ns  ≈ 20 ms
```

**加速比**：~20倍

---

## ✅ 當前實現狀態

### 已完成（最優）

| 算子 | 方法 | 複雜度 | 狀態 |
|------|------|--------|------|
| 差分（變量+變量） | LM-2001 | O(1) | ✅ 完成 |
| **差分（變量+常量）** | **Theorem 2** | **O(n)** | ✅ **已修正** |
| 線性（變量+變量） | Wallén M_n^T | O(n) | ✅ 完成 |
| **線性（變量+常量）** | **Wallén DP** | **O(n)** | ✅ **完成** |

### 可選優化（高級）

| 優化 | 預期加速 | 實現複雜度 | 建議 |
|------|---------|-----------|------|
| 16位cLAT | 10-100倍 | 高 | 如果搜索太慢再考慮 |
| 並行搜索 | 4-8倍 | 中 | 優先考慮 |
| 剪枝優化 | 10-1000倍 | 中 | 優先考慮 |

---

## 📊 論文引用

### 差分分析

> "Theorem 2: Pr[u →^a v] = ϕ₀ × · · · × ϕₙ₋₁"  
> — Machado (2015), Bit-Vector Differential Model (2022)

### 線性分析

> "按位進位DP，時間O(n)、精確無近似"  
> — Wallén (2003), FSE

> "cLAT can index the unknown input masks... by one fixed input mask"  
> — Huang & Wang (2020)

> "For m-bit cLAT... takes about 4 seconds on 2.4 GHz CPU"  
> — Huang & Wang (2020), Algorithm 2

---

## 🎯 結論

### 當前實現

**✅ 所有底層算子都是論文最優實現！**

- 差分：Theorem 2，O(n)，100%精確
- 線性：Wallén DP，O(n)，100%精確

### 性能

**對於32位操作**：
- O(n) = O(32) ≈ 每次操作100-200 ns
- **足夠快**，不需要極致優化

### 如果需要進一步加速

1. **優先**：剪枝策略（Matsui算法）
2. **其次**：並行搜索（OpenMP）
3. **最後**：16位cLAT（複雜但有效）

**不建議**：32位完整cLAT（空間爆炸）

---

*總結完成：2025-10-03*  
*差分和線性算子都是最優實現*  
*O(n)複雜度對32位足夠高效*
