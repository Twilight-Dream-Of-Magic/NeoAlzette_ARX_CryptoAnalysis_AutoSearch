/**
 * @file differential_xdp_add.hpp
 * @brief XOR差分模加法概率計算（變量-變量）
 * 
 * 論文：Lipmaa & Moriai (2001), "Efficient Algorithms for Computing Differential 
 *       Properties of Addition"
 * 
 * 算法：LM-2001 Algorithm 2
 * 複雜度：O(1) 位運算
 * 
 * ⚠️ 關鍵：論文中的eq必須用psi-constraint實現！
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <limits>

namespace neoalz {
namespace arx_operators {

/**
 * @brief ψ函数（psi-constraint）：LM-2001的核心約束
 * 
 * 用於實現論文中的eq函數
 * 
 * 公式：ψ(α,β,γ) = (~α ⊕ β) & (~α ⊕ γ)
 * 
 * @param alpha 输入差分α
 * @param beta 输入差分β
 * @param gamma 输出差分γ
 * @return ψ值
 */
inline std::uint32_t psi_32(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    std::uint32_t not_alpha = ~alpha;
    std::uint32_t term1 = not_alpha ^ beta;
    std::uint32_t term2 = not_alpha ^ gamma;
    return term1 & term2;
}

/**
 * @brief LM-2001 Algorithm 2: 計算xdp⁺的權重（32位）
 * 
 * 論文Algorithm 2 (Lines 321-327)，使用psi實現eq
 * 
 * Step 1: If psi(α<<1,β<<1,γ<<1) ∧ (xor(α,β,γ) ⊕ (β<<1)) != 0 then return ∞
 * Step 2: Return popcount(psi(α,β,γ) & mask(n-1))
 * 
 * @param alpha 輸入差分α
 * @param beta 輸入差分β  
 * @param gamma 輸出差分γ
 * @return 權重w，-1表示不可能
 */
inline int xdp_add_lm2001(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    // Step 1: Good check (使用psi)
    std::uint32_t alpha_shifted = alpha << 1;
    std::uint32_t beta_shifted = beta << 1;
    std::uint32_t gamma_shifted = gamma << 1;
    
    std::uint32_t psi_shifted = psi_32(alpha_shifted, beta_shifted, gamma_shifted);
    std::uint32_t xor_val = alpha ^ beta ^ gamma;
    std::uint32_t xor_condition = xor_val ^ beta_shifted;
    
    // If (psi_shifted & xor_condition) != 0, differential is impossible
    if ((psi_shifted & xor_condition) != 0) {
        return -1;  // Impossible differential
    }
    
    // Step 2: Compute weight (使用psi)
    std::uint32_t psi_val = psi_32(alpha, beta, gamma);
    
    // mask(n-1) = 0x7FFFFFFF (低31位)
    constexpr std::uint32_t mask_lower = 0x7FFFFFFF;
    
    std::uint32_t masked_psi = psi_val & mask_lower;
    
    // weight = popcount(psi(α,β,γ) & mask(n-1))
    int weight = __builtin_popcount(masked_psi);
    
    return weight;
}

/**
 * @brief LM-2001: 計算xdp⁺的概率
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
 */
inline bool is_xdp_add_possible(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    return xdp_add_lm2001(alpha, beta, gamma) >= 0;
}

/**
 * @brief LM-2001 Algorithm 2: 支持任意位宽n（使用psi）
 */
inline int xdp_add_lm2001_n(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma,
    int n
) noexcept {
    std::uint32_t mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    alpha &= mask;
    beta &= mask;
    gamma &= mask;
    
    std::uint32_t alpha_shifted = (alpha << 1) & mask;
    std::uint32_t beta_shifted = (beta << 1) & mask;
    std::uint32_t gamma_shifted = (gamma << 1) & mask;
    
    // psi_shifted
    std::uint32_t not_alpha_s = (~alpha_shifted) & mask;
    std::uint32_t psi_shifted = (not_alpha_s ^ beta_shifted) & (not_alpha_s ^ gamma_shifted) & mask;
    
    std::uint32_t xor_val = (alpha ^ beta ^ gamma) & mask;
    std::uint32_t xor_condition = (xor_val ^ beta_shifted) & mask;
    
    if ((psi_shifted & xor_condition) != 0) {
        return -1;
    }
    
    // psi_val
    std::uint32_t not_alpha = (~alpha) & mask;
    std::uint32_t psi_val = (not_alpha ^ beta) & (not_alpha ^ gamma) & mask;
    
    std::uint32_t mask_lower = (n == 32) ? 0x7FFFFFFFu : ((1u << (n - 1)) - 1);
    std::uint32_t masked_psi = psi_val & mask_lower;
    
    return __builtin_popcount(masked_psi);
}

} // namespace arx_operators
} // namespace neoalz
