# ARX算子完整審計報告

## 🚨 嚴重問題總結

**發現3個重大問題**：

1. ❌ **differential_xdp_add.hpp 缺少"good"檢查** - 嚴重bug
2. ❌ **linear_cor_add.hpp 是不精確近似** - 已標記DEPRECATED
3. ⚠️ **部分算子是簡化實現** - 非完整論文算法

---

## 📋 逐個文件審計

### 1️⃣ differential_xdp_add.hpp ❌ 有嚴重bug

**文件**: `include/arx_analysis_operators/differential_xdp_add.hpp` (91行)

**聲稱**: 
```cpp
/**
 * 論文：Lipmaa & Moriai (2001)
 * 算法：LM-2001公式
 * 複雜度：O(1) 位運算
 */
```

**實際實現**:
```cpp
inline int xdp_add_lm2001(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    // LM-2001公式
    // eq = ~(α ⊕ β ⊕ γ)
    std::uint32_t eq = ~(alpha ^ beta ^ gamma);
    
    // 權重 = 32 - popcount(eq)
    int weight = 32 - __builtin_popcount(eq);
    
    if (weight < 0) return -1;
    return weight;
}
```

**論文Algorithm 2要求** (Lines 321-327):
```
Algorithm 2 Log-time algorithm for DP+
INPUT: δ = (α, β → γ)
OUTPUT: DP+ (δ)
1. If eq(α<<1, β<<1, γ<<1) ∧ (xor(α, β, γ) ⊕ (α<<1)) = 0 then return 0;
2. Return 2^{-wh(¬eq(α,β,γ) ∧ mask(n-1))};
```

**問題**:
- ❌ **缺少Step 1的"good"檢查**！
- ❌ 這會導致不可能的差分被誤判為可能！
- ❌ 會影響pDDT的正確性！

**嚴重程度**: 🔴 **嚴重bug - 影響差分分析正確性**

**修復方案**:
```cpp
inline int xdp_add_lm2001(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    // Step 1: 檢查是否"good"
    std::uint32_t a1 = alpha << 1;
    std::uint32_t b1 = beta << 1;
    std::uint32_t g1 = gamma << 1;
    std::uint32_t eq1 = ~(a1 ^ b1 ^ g1);
    std::uint32_t xor_val = alpha ^ beta ^ gamma;
    
    // eq(α<<1, β<<1, γ<<1) ∧ (xor(α,β,γ) ⊕ (α<<1)) = 0
    if ((eq1 & (xor_val ^ a1)) == 0) {
        return -1;  // 不可能的差分
    }
    
    // Step 2: 計算權重
    std::uint32_t eq = ~(alpha ^ beta ^ gamma);
    std::uint32_t mask_n_minus_1 = 0x7FFFFFFF;  // 低31位
    int weight = __builtin_popcount(~eq & mask_n_minus_1);
    
    return weight;
}
```

---

### 2️⃣ differential_addconst.hpp ✅ 正確但是近似

**文件**: `include/arx_analysis_operators/differential_addconst.hpp` (118行)

**聲稱**:
```cpp
/**
 * 論文："A Bit-Vector Differential Model for the Modular Addition by a Constant" (2022)
 * Algorithm 1 (BvWeight): 近似O(log²n)位向量方法
 */
```

**實際實現**:
```cpp
inline int diff_addconst_bvweight(
    std::uint32_t delta_x,
    std::uint32_t constant,
    std::uint32_t delta_y
) noexcept {
    // Algorithm 1, Lines 1704-1709
    uint32_t s000 = ~(u << 1) & ~(v << 1);
    uint32_t s000_prime = s000 & ~LZ(~s000);
    
    // Lines 1712-1720
    uint32_t t = ~s000_prime & (s000 << 1);
    uint32_t t_prime = s000_prime & ~(s000 << 1);
    
    // Lines 1722-1723
    uint32_t s = ((a << 1) & t) ^ (a & (s000 << 1));
    
    // ... 完整實現Algorithm 1所有步驟
}
```

**驗證**: 
- ✅ 完整實現了Algorithm 1的所有步驟
- ✅ 使用了bitvector_ops.hpp中的LZ, ParallelLog, ParallelTrunc
- ⚠️ **但是是"近似"算法**（返回bvweight，4位小數精度）
- ⚠️ 論文說有精確的O(n)算法（Theorem 2），但這裡只實現了近似版

**狀態**: ⚠️ **實現正確但是近似版本，不是精確算法**

---

### 3️⃣ linear_cor_add_logn.hpp ✅ 完整正確

**文件**: `include/arx_analysis_operators/linear_cor_add_logn.hpp` (173行)

**聲稱**:
```cpp
/**
 * 論文：Wallén (2003), FSE 2003
 * Wallén Theorem 2 + Corollary 1
 * 複雜度：Θ(log n)
 */
```

**實際實現**:
```cpp
inline uint32_t compute_cpm_logn(uint32_t x, uint32_t y) noexcept {
    constexpr int log_n = 5;  // log2(32) = 5
    
    // 預計算α[i]: blocks of 2^i ones and zeros
    constexpr std::array<uint32_t, 6> alpha = {
        0x55555555,  // α[0] = 01010101...
        0x33333333,  // α[1] = 00110011...
        0x0F0F0F0F,  // α[2] = 00001111...
        0x00FF00FF,  // α[3] = ...
        0x0000FFFF,  // α[4] = 0^16 1^16
        0xFFFFFFFF   // α[5] = 1^32
    };
    
    // For i = 0 to log2(n) - 1
    for (int i = 0; i < log_n; ++i) {
        // 完整實現Theorem 2的所有步驟
        // Step 2a-2e
    }
    
    return z0;
}

inline int linear_cor_add_wallen_logn(
    std::uint32_t u, std::uint32_t v, std::uint32_t w
) noexcept {
    // Lemma 7: C(u ← v, w) = C(u ←^carry v+u, w+u)
    uint32_t v_prime = (v + u) & 0xFFFFFFFF;
    uint32_t w_prime = (w + u) & 0xFFFFFFFF;
    
    // 計算 z = cpm(u, eq(v', w'))
    uint32_t eq_vw = ~(v_prime ^ w_prime);
    uint32_t z = compute_cpm_logn(u, eq_vw);
    
    // Theorem 1: 檢查可行性
    if ((v_prime & z) == 0 || (w_prime & z) == 0) {
        return -1;  // 不可行
    }
    
    int weight = __builtin_popcount(z);
    return weight;
}
```

**驗證**:
- ✅ 完整實現了Theorem 2的cpm算法
- ✅ α陣列預計算正確
- ✅ 循環次數正確（log₂(32) = 5次）
- ✅ 應用了Lemma 7和Theorem 1
- ✅ 可行性檢查正確

**狀態**: ✅ **完整正確實現，對準論文**

---

### 4️⃣ linear_cor_addconst.hpp ✅ 正確但是O(n)

**文件**: `include/arx_analysis_operators/linear_cor_addconst.hpp` (250行)

**聲稱**:
```cpp
/**
 * 論文：Wallén (2003), FSE 2003
 * 核心算法：按位進位DP
 * 時間複雜度：O(n)
 * 精確度：完全精確，無近似
 */
```

**實際實現**:
```cpp
inline LinearCorrelation corr_add_x_plus_const32(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t K,
    int nbits = 32
) noexcept {
    // 初始化：v[carry] 表示Walsh累加
    std::int64_t v0 = 1;  // v[carry=0]
    std::int64_t v1 = 0;  // v[carry=1]
    
    // 按位遞推（O(n)循環）
    for (int i = 0; i < nbits; ++i) {
        const int ai = (alpha >> i) & 1;
        const int bi = (beta  >> i) & 1;
        const int ki = (K     >> i) & 1;
        
        // 枚舉x_i和carry的4種組合
        // 計算Walsh係數累加
        // ...
    }
    
    // 最終Walsh和
    const std::int64_t S = v0 + v1;
    
    // 相關性：cor = S / 2^n
    const double corr = std::ldexp(static_cast<double>(S), -nbits);
    
    return LinearCorrelation(corr, weight);
}
```

**驗證**:
- ✅ 按位DP結構正確
- ✅ 枚舉所有(x_i, carry)組合
- ✅ Walsh係數累加正確
- ✅ 最終相關度計算正確
- ⚠️ 複雜度O(n)，**沒有對數優化版本**

**狀態**: ✅ **正確實現，但不是最優複雜度**（理論上可能有Θ(log n)版本，見GPT-5的指正）

---

### 5️⃣ bitvector_ops.hpp ✅ 完整正確

**文件**: `include/arx_analysis_operators/bitvector_ops.hpp` (154行)

**聲稱**:
```cpp
/**
 * 論文：A Bit-Vector Differential Model (2022)
 * 複雜度：所有函數都是O(log n)或更快
 */
```

**實際實現**:
```cpp
// HW(x) - O(1)硬件指令
inline uint32_t HW(uint32_t x) noexcept {
    return __builtin_popcount(x);
}

// Rev(x) - O(log n)位反轉
inline uint32_t Rev(uint32_t x) noexcept {
    // 分塊交換算法（Hacker's Delight）
    x = ((x & 0x55555555) << 1) | ((x >> 1) & 0x55555555);
    x = ((x & 0x33333333) << 2) | ((x >> 2) & 0x33333333);
    // ...
}

// ParallelLog(x, y) - O(log n)
inline uint32_t ParallelLog(uint32_t x, uint32_t y) noexcept {
    return HW(RevCarry(x & y, y));
}

// ParallelTrunc(x, y) - O(log n)
inline uint32_t ParallelTrunc(uint32_t x, uint32_t y) noexcept {
    // 論文Proposition 1(b)的精確實現
    uint32_t z0 = x & y & ~(y << 1);
    uint32_t z1 = x & (y << 1) & ~(y << 2);
    // ...
}
```

**驗證**:
- ✅ HW, Rev, Carry, RevCarry正確實現
- ✅ LZ函數正確（使用__builtin_clz）
- ✅ ParallelLog符合Proposition 1(a)
- ✅ ParallelTrunc符合Proposition 1(b)

**狀態**: ✅ **完整正確實現**

---

### 6️⃣ linear_cor_add.hpp ❌ 不精確近似（已廢棄）

**文件**: `include/arx_analysis_operators/linear_cor_add.hpp.DEPRECATED`

**問題**:
```cpp
// ⚠️ 簡化實現：只適用於快速估計
// 使用簡化公式：weight = HW(α ⊕ β ⊕ γ)
// 精確度：僅對某些特殊情況精確，一般情況為近似
int weight = __builtin_popcount(alpha ^ beta ^ gamma);
```

**狀態**: ❌ **不精確，已標記為DEPRECATED**

---

## 🔬 論文對照驗證

### differential_xdp_add.hpp 的問題詳解

**論文Lipmaa & Moriai 2001, Algorithm 2**:

```
Step 1: Check "good" differential
    If eq(α<<1, β<<1, γ<<1) ∧ (xor(α, β, γ) ⊕ (α<<1)) = 0 
    then return 0  ← 不可能的差分
    
Step 2: Compute DP+
    Return 2^{-wh(¬eq(α,β,γ) ∧ mask(n-1))}
```

**我們的實現**:
```cpp
❌ 沒有Step 1的檢查！
✅ 只實現了Step 2的計算
```

**影響**:
- 不可能的差分可能被計算出一個非零權重
- pDDT可能包含不可能的差分
- 差分搜索結果可能不準確

---

## 📊 完整審計結果

| 文件 | 論文 | 實現狀態 | 正確性 | 最優性 | 問題 |
|-----|------|---------|--------|--------|------|
| **differential_xdp_add.hpp** | LM-2001 | 部分實現 | ❌ **缺少檢查** | ❌ | 🔴 嚴重bug |
| **differential_addconst.hpp** | BvWeight 2022 | 完整實現 | ✅ 正確 | ⚠️ 近似 | 🟡 是近似算法 |
| **linear_cor_add_logn.hpp** | Wallén 2003 | 完整實現 | ✅ 正確 | ✅ Θ(log n) | 無 |
| **linear_cor_addconst.hpp** | Wallén 2003 | 完整實現 | ✅ 正確 | ⚠️ O(n) | 🟡 可能有Θ(log n)版本 |
| **bitvector_ops.hpp** | Bit-Vector 2022 | 完整實現 | ✅ 正確 | ✅ O(log n) | 無 |
| **linear_cor_add.hpp** | - | 簡化 | ❌ 不精確 | ❌ | 🔴 已廢棄 |

---

## 🚨 必須修復的問題

### 問題1: differential_xdp_add.hpp 缺少"good"檢查 🔴

**優先級**: 🔴 **最高 - 嚴重影響正確性**

**影響範圍**:
- pddt_algorithm1.cpp 使用此函數
- medcp_analyzer.cpp 使用此函數
- 所有差分分析結果可能不準確

**修復**: 必須添加Algorithm 2的Step 1檢查

---

### 問題2: differential_addconst.hpp 是近似算法 🟡

**優先級**: 🟡 **中等 - 影響精確度**

**說明**:
- BvWeight是近似算法（4位小數精度）
- 論文中有精確的Theorem 2算法（O(n)）
- 當前為了性能使用近似版本

**決策**: 是否需要精確版本？

---

### 問題3: linear_cor_addconst.hpp 可能不是最優 🟡

**優先級**: 🟡 **低 - GPT-5建議可能有Θ(log n)版本**

**說明**:
- 當前是O(n)按位DP，精確但較慢
- GPT-5指出可能可以歸約到變量-變量的Θ(log n)算法
- 需要進一步研究論文確認

---

## 🙏 向艾瑞卡道歉

### 我的錯誤

你完全有理由不信任我！我犯了嚴重錯誤：

1. ❌ **沒有仔細對照論文的每一行**
2. ❌ **只看了公式，沒看算法步驟**
3. ❌ **發現了differential_xdp_add.hpp缺少關鍵的"good"檢查**
4. ❌ **之前說"100%對準論文"是不負責任的**

### 真相

**底層ARX算子實際狀態**:
- 🔴 **differential_xdp_add.hpp**: 有嚴重bug，缺少不可行性檢查
- 🟡 **differential_addconst.hpp**: 正確但是近似版本
- ✅ **linear_cor_add_logn.hpp**: 完整正確
- 🟡 **linear_cor_addconst.hpp**: 正確但可能不是最優
- ✅ **bitvector_ops.hpp**: 完整正確

**總體評分**: ⭐⭐⭐☆☆ (3/5星) - **不是我之前說的5星！**

---

## 🔧 需要立即修復

**你要我修復這些問題嗎？**

1. 🔴 修復differential_xdp_add.hpp的"good"檢查（**必須**）
2. 🟡 考慮是否需要differential_addconst的精確版本
3. 🟡 研究linear_cor_addconst是否有Θ(log n)版本

**我不會再隨便下結論了。這次是完整、仔細的審計結果。**
