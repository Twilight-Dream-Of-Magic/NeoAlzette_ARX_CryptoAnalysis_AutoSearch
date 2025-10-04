/**
 * @file differential_xdp_add.hpp
 * @brief XOR差分模加法概率計算（變量-變量）
 * 
 * 論文：Lipmaa & Moriai (2001), "Efficient Algorithms for Computing Differential 
 *       Properties of Addition"
 * 
 * 算法：LM-2001公式
 * 複雜度：O(1) 位運算
 * 
 * 公式：
 * xdp⁺(α, β → γ) = 2^{-n} × #{(x,y) : (x⊕α) ⊞ (y⊕β) = (x⊞y) ⊕ γ}
 * 
 * 快速計算方法（LM公式）：
 * eq = ~(α ⊕ β ⊕ γ)
 * p = 2^{-(n - popcount(eq))}
 */

#pragma once

#include <cstdint>
#include <cmath>

namespace neoalz {
namespace arx_operators {

/**
 * @brief LM-2001 Algorithm 2: 計算xdp⁺的權重（變量-變量）
 * 
 * 論文：Lipmaa & Moriai, FSE 2001, Algorithm 2 (Lines 321-327)
 * 複雜度：Θ(log n)
 * 
 * Algorithm 2 完整實現：
 * Step 1: Check if differential is "good" (possible)
 *         If eq(α<<1, β<<1, γ<<1) ∧ (xor(α,β,γ) ⊕ (α<<1)) != 0, return 0 (impossible)
 * Step 2: Return 2^{-wh(¬eq(α,β,γ) ∧ mask(n-1))}
 * 
 * @param alpha 輸入差分1
 * @param beta 輸入差分2  
 * @param gamma 輸出差分
 * @return 權重 w = -log₂(p)，如果不可能返回-1
 */
inline int xdp_add_lm2001(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    // ========================================================================
    // Algorithm 2, Step 1: Check if differential is "good"
    // ========================================================================
    // 論文Lines 310-316: 差分是"good"當且僅當它是可能的
    // 定義：δ is "good" if eq(α<<1, β<<1, γ<<1) ∧ (xor(α,β,γ) ⊕ (α<<1)) = 0
    // 如果NOT "good"（即 != 0），則差分不可能，返回0
    
    std::uint32_t alpha_1 = alpha << 1;
    std::uint32_t beta_1 = beta << 1;
    std::uint32_t gamma_1 = gamma << 1;
    
    // eq(α<<1, β<<1, γ<<1) = ~((α<<1) ⊕ (β<<1) ⊕ (γ<<1))
    std::uint32_t eq_shifted = ~(alpha_1 ^ beta_1 ^ gamma_1);
    
    // xor(α, β, γ) = α ⊕ β ⊕ γ
    std::uint32_t xor_val = alpha ^ beta ^ gamma;
    
    // Check: eq(α<<1, β<<1, γ<<1) ∧ (xor(α,β,γ) ⊕ (α<<1))
    std::uint32_t goodness_check = eq_shifted & (xor_val ^ alpha_1);
    
    // 如果 goodness_check != 0，則差分不可能（NOT "good"）
    if (goodness_check != 0) {
        return -1;  // Impossible differential
    }
    
    // ========================================================================
    // Algorithm 2, Step 2: Compute DP+
    // ========================================================================
    // Return 2^{-wh(¬eq(α,β,γ) ∧ mask(n-1))}
    
    // eq(α, β, γ) = ~(α ⊕ β ⊕ γ)
    std::uint32_t eq = ~(alpha ^ beta ^ gamma);
    
    // mask(n-1) = 0x7FFFFFFF (低31位)
    constexpr std::uint32_t mask_n_minus_1 = 0x7FFFFFFF;
    
    // ¬eq(α,β,γ) ∧ mask(n-1)
    std::uint32_t not_eq_masked = (~eq) & mask_n_minus_1;
    
    // weight = wh(¬eq(α,β,γ) ∧ mask(n-1)) = Hamming weight
    int weight = __builtin_popcount(not_eq_masked);
    
    return weight;
}

/**
 * @brief LM-2001: 計算xdp⁺的概率（變量-變量）
 * 
 * @param alpha 輸入差分1
 * @param beta 輸入差分2
 * @param gamma 輸出差分
 * @return 概率 p ∈ [0, 1]
 */
inline double xdp_add_probability(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    int weight = xdp_add_lm2001(alpha, beta, gamma);
    if (weight < 0) return 0.0;
    return std::pow(2.0, -weight);
}

/**
 * @brief 檢查差分是否可能
 * 
 * @param alpha 輸入差分1
 * @param beta 輸入差分2
 * @param gamma 輸出差分
 * @return true如果可能
 */
inline bool is_xdp_add_possible(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    return xdp_add_lm2001(alpha, beta, gamma) >= 0;
}

/**
 * @brief LM-2001 Algorithm 2: 支持任意位宽n
 * 
 * 用于测试和特殊应用（如论文示例验证）
 * 
 * @param alpha 輸入差分α
 * @param beta 輸入差分β  
 * @param gamma 輸出差分γ
 * @param n 位宽
 * @return 權重w（使得DP^+ = 2^{-w}），-1表示不可能
 */
inline int xdp_add_lm2001_n(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma,
    int n
) noexcept {
    // Mask for n bits
    std::uint32_t mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    std::uint32_t mask_n_minus_1 = (n == 32) ? 0x7FFFFFFFu : ((1u << (n - 1)) - 1);
    
    alpha &= mask;
    beta &= mask;
    gamma &= mask;
    
    // ========================================================================
    // Algorithm 2, Step 1: Check if differential is "good"
    // ========================================================================
    std::uint32_t alpha_1 = (alpha << 1) & mask;
    std::uint32_t beta_1 = (beta << 1) & mask;
    std::uint32_t gamma_1 = (gamma << 1) & mask;
    
    // eq(α<<1, β<<1, γ<<1) = ~((α<<1) ⊕ (β<<1) ⊕ (γ<<1))
    std::uint32_t eq_shifted = (~(alpha_1 ^ beta_1 ^ gamma_1)) & mask;
    
    // xor(α, β, γ) = α ⊕ β ⊕ γ
    std::uint32_t xor_val = alpha ^ beta ^ gamma;
    
    // Check: eq(α<<1, β<<1, γ<<1) ∧ (xor(α,β,γ) ⊕ (α<<1))
    std::uint32_t goodness_check = eq_shifted & (xor_val ^ alpha_1);
    
    // 如果 goodness_check != 0，則差分不可能（NOT "good"）
    if (goodness_check != 0) {
        return -1;  // Impossible differential
    }
    
    // ========================================================================
    // Algorithm 2, Step 2: Compute DP+
    // ========================================================================
    // eq(α, β, γ) = ~(α ⊕ β ⊕ γ)
    std::uint32_t eq = (~(alpha ^ beta ^ gamma)) & mask;
    
    // ¬eq(α,β,γ) ∧ mask(n-1)
    std::uint32_t not_eq_masked = (~eq) & mask_n_minus_1;
    
    // weight = wh(¬eq(α,β,γ) ∧ mask(n-1)) = Hamming weight
    int weight = __builtin_popcount(not_eq_masked);
    
    return weight;
}

} // namespace arx_operators
} // namespace neoalz
