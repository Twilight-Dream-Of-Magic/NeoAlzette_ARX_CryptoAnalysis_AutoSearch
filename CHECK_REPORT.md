# 底層ARX算子最優化實現檢查報告

## ✅ 差分算子檢查

### 1. Lipmaa-Moriai AOP（變量+變量）

**論文公式（LM-2001）**：
```
AOP(α,β,γ) = (α⊕β⊕γ) ⊕ ((α∧β)⊕((α⊕β)∧γ))<<1
```

**我們的實現**：
```cpp
static std::uint32_t compute_aop(uint32_t alpha, uint32_t beta, uint32_t gamma) {
    uint32_t xor_part = alpha ^ beta ^ gamma;
    uint32_t and_part = (alpha & beta) ^ ((alpha ^ beta) & gamma);
    return xor_part ^ (and_part << 1);
}
```

**檢查結果**：✅ 完全一致，O(1)時間

### 2. Bit-Vector模型（變量+常量差分）

**論文方法（Bit-Vector 2022）**：
- 逐位檢查carry鏈的一致性
- 複雜度：O(n)

**我們的實現**：
```cpp
static int compute_diff_weight_addconst(uint32_t delta_x, uint32_t constant, uint32_t delta_y) {
    // 簡化版本：啟發式
    int weight = 0;
    for (int i = 0; i < 32; ++i) {
        if ((dx_bit ^ dy_bit) != 0) weight++;
    }
    return weight;
}
```

**檢查結果**：⚠️ 這是簡化版本，不是論文的完整實現

## ✅ 線性算子檢查

### 1. Wallén M_n^T（變量+變量）

**論文公式（Wallén 2003）**：
```
z*[i] = ⊕_{j=i+1}^{n-1} v[j]
```

**我們的實現**：
```cpp
static uint32_t compute_MnT(uint32_t v) {
    uint32_t z = 0, suffix = 0;
    for (int i = 31; i >= 0; --i) {
        if (suffix & 1) z |= (1u << i);
        suffix ^= (v >> i) & 1u;
    }
    return z;
}
```

**檢查結果**：✅ 完全一致，O(n)時間

### 2. Wallén按位DP（變量+常量）

**論文方法（Wallén 2003）**：
- 按位進位DP
- 兩狀態遞推
- Walsh和累加

**我們的實現**：
```cpp
inline LinearCorrelation corr_add_x_plus_const32(...) {
    int64_t v0 = 1, v1 = 0;
    for (int i = 0; i < nbits; ++i) {
        // 枚舉x_i, carry_in
        // 計算y_i, carry_out
        // 累加Walsh項
    }
    int64_t S = v0 + v1;
    double corr = ldexp(S, -nbits);
    return {corr, weight};
}
```

**檢查結果**：✅ 完全一致，O(n)時間，精確無近似

## 總結

| 算子 | 實現狀態 | 是否最優 | 問題 |
|------|---------|---------|------|
| LM-2001 AOP | ✅ | ✅ 是 | 無 |
| Bit-Vector差分 | ⚠️ | ❌ 否 | **簡化版本，需改進** |
| Wallén M_n^T | ✅ | ✅ 是 | 無 |
| Wallén按位DP | ✅ | ✅ 是 | 無 |

**關鍵問題**：
- Bit-Vector的模加常量差分實現是簡化版本
- 需要實現論文的完整版本（精確的carry鏈分析）

