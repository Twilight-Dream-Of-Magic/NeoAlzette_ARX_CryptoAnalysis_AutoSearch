/**
 * @file algorithm1_bvweight.hpp
 * @brief Algorithm 1: BvWeight - 對數算法實現
 * 
 * 論文：A Bit-Vector Differential Model for the Modular Addition by a Constant (2022)
 * Algorithm 1, Lines 1701-1749
 * 
 * 複雜度：O(log² n)
 * 精確度：誤差 ≤ 0.029(n-1) ≈ 0.9位（對32位）
 */

#pragma once

#include <cstdint>
#include <cmath>
#include "bitvector_ops.hpp"

namespace neoalz {

/**
 * @brief BvWeight - 計算模加常量差分的權重（對數算法）
 * 
 * 論文Algorithm 1的完整實現
 * 
 * @param u 輸入差分
 * @param v 輸出差分
 * @param a 常量
 * @param nbits 位寬（默認32）
 * @return 差分權重（-1表示不可行）
 * 
 * 算法流程：
 * 1. 計算s000（狀態標記）
 * 2. 計算w（權重向量）
 * 3. 使用ParallelLog計算整數部分
 * 4. 使用ParallelTrunc計算小數部分
 * 5. 組合結果：weight = (int << 4) ⊕ frac
 */
inline int BvWeight(uint32_t u, uint32_t v, uint32_t a, int nbits = 32) noexcept {
    using namespace bitvector;
    
    // ========================================================================
    // Algorithm 1, Lines 1704-1709
    // ========================================================================
    
    // s000 ← ¬(u << 1) ∧ ¬(v << 1)
    uint32_t s000 = ~(u << 1) & ~(v << 1);
    
    // s000' ← s000 ∧ ¬LZ(¬s000)
    uint32_t s000_prime = s000 & ~LZ(~s000);
    
    // ========================================================================
    // Algorithm 1, Lines 1712-1720
    // ========================================================================
    
    // t ← ¬s000' ∧ (s000 << 1)
    uint32_t t = ~s000_prime & (s000 << 1);
    
    // t' ← s000' ∧ ¬(s000 << 1)
    uint32_t t_prime = s000_prime & ~(s000 << 1);
    
    // ========================================================================
    // Algorithm 1, Lines 1722-1723
    // ========================================================================
    
    // s ← ((a << 1) ∧ t) ⊕ (a ∧ (s000 << 1))
    uint32_t s = ((a << 1) & t) ^ (a & (s000 << 1));
    
    // ========================================================================
    // Algorithm 1, Lines 1726-1730
    // ========================================================================
    
    // q ← ¬((a << 1) ⊕ u ⊕ v)
    uint32_t q = ~((a << 1) ^ u ^ v);
    
    // d ← RevCarry(s000' << 1 ∧ t', q) ∨ q
    uint32_t d = RevCarry((s000_prime << 1) & t_prime, q) | q;
    
    // ========================================================================
    // Algorithm 1, Line 1731
    // ========================================================================
    
    // w ← (q << (s ∧ d)) ∨ (s ∧ ¬d)
    uint32_t w = (q << (s & d)) | (s & ~d);
    
    // ========================================================================
    // Algorithm 1, Lines 1733-1735
    // ========================================================================
    
    // int ← HW((u ⊕ v) << 1) ⊕ HW(s000') ⊕ ParallelLog((w ∧ s000') << 1, s000' << 1)
    uint32_t int_part = HW((u ^ v) << 1) ^ HW(s000_prime) ^ ParallelLog((w & s000_prime) << 1, s000_prime << 1);
    
    // ========================================================================
    // Algorithm 1, Lines 1738-1742
    // ========================================================================
    
    // frac ← ParallelTrunc(w << 1, RevCarry((w ∧ s000') << 1, s000' << 1))
    uint32_t frac = ParallelTrunc(w << 1, RevCarry((w & s000_prime) << 1, s000_prime << 1));
    
    // ========================================================================
    // Algorithm 1, Line 1743
    // ========================================================================
    
    // return (int << 4) ⊕ frac
    // 注意：論文中使用"⊕"表示位拼接，這裡使用"|"
    uint32_t bvweight = (int_part << 4) | frac;
    
    // ========================================================================
    // 轉換為權重
    // ========================================================================
    
    // 論文Lemma 8：apxweight_a(u, v) = 2^{-4} × BvWeight(u, v, a)
    // weight = -log₂(prob) ≈ 2^{-4} × bvweight
    
    // 如果bvweight為0，表示概率為1，權重為0
    if (bvweight == 0) return 0;
    
    // 將bvweight轉換為實際權重
    // bvweight的格式：高位是整數部分，低4位是小數部分
    double approx_weight = static_cast<double>(bvweight) / 16.0;
    
    // 向上取整
    return static_cast<int>(std::ceil(approx_weight));
}

/**
 * @brief 檢查差分是否可行
 * 
 * 根據論文第3.1節，如果狀態S_i = 001，則差分不可行
 * 
 * @param u 輸入差分
 * @param v 輸出差分
 * @param a 常量
 * @return true表示可行，false表示不可行
 */
inline bool is_diff_valid(uint32_t u, uint32_t v, uint32_t a) noexcept {
    // 檢查s001狀態（論文第3.1節）
    // s001[i] = 1 ⟺ S_i = 001 ⟺ 不可行
    
    for (int i = 0; i < 32; ++i) {
        int u_prev = (i > 0) ? ((u >> (i-1)) & 1) : 0;
        int v_prev = (i > 0) ? ((v >> (i-1)) & 1) : 0;
        int u_i = (u >> i) & 1;
        int v_i = (v >> i) & 1;
        
        // S_i = (u[i-1], v[i-1], u[i]⊕v[i])
        if (u_prev == 0 && v_prev == 0 && (u_i ^ v_i) == 1) {
            return false;  // 狀態001，不可行
        }
    }
    
    return true;
}

/**
 * @brief compute_diff_weight_addconst_bvweight - 使用Algorithm 1計算差分權重
 * 
 * 這是對外接口，內部調用BvWeight
 * 
 * @param delta_x 輸入差分
 * @param constant 常量
 * @param delta_y 輸出差分
 * @return 差分權重（-1表示不可行）
 */
inline int compute_diff_weight_addconst_bvweight(
    uint32_t delta_x,
    uint32_t constant,
    uint32_t delta_y
) noexcept {
    // 先檢查可行性
    if (!is_diff_valid(delta_x, delta_y, constant)) {
        return -1;
    }
    
    // 調用Algorithm 1
    return BvWeight(delta_x, delta_y, constant, 32);
}

} // namespace neoalz
