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
 * 注意：論文使用eq函數，不是psi函數！
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <limits>

namespace neoalz {
namespace arx_operators {

/**
 * @brief eq函数：論文中的核心函數
 * 
 * 定義：eq(α,β,γ) = ~(α ⊕ β ⊕ γ)
 * 
 * @param alpha 输入差分α
 * @param beta 输入差分β
 * @param gamma 输出差分γ
 * @return eq值
 */
inline std::uint32_t eq_func(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) noexcept {
    return ~(alpha ^ beta ^ gamma);
}

/**
 * @brief LM-2001 Algorithm 2: 計算xdp⁺的權重（32位）
 * 
 * 論文Algorithm 2 (Lines 321-327):
 * Step 1: If eq(α<<1,β<<1,γ<<1) ∧ (xor(α,β,γ) ⊕ (β<<1)) = 0 then return 0
 * Step 2: Return 2^{-wh(¬eq(α,β,γ) ∧ mask(n-1))}
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
    // Step 1: Good check
    std::uint32_t alpha_shifted = alpha << 1;
    std::uint32_t beta_shifted = beta << 1;
    std::uint32_t gamma_shifted = gamma << 1;
    
    std::uint32_t eq_shifted = eq_func(alpha_shifted, beta_shifted, gamma_shifted);
    std::uint32_t xor_val = alpha ^ beta ^ gamma;
    std::uint32_t check = eq_shifted & (xor_val ^ beta_shifted);
    
    // If check == 0, differential is impossible
    if (check == 0) {
        return -1;
    }
    
    // Step 2: Compute weight
    std::uint32_t eq_val = eq_func(alpha, beta, gamma);
    constexpr std::uint32_t mask_lower = 0x7FFFFFFF;  // n-1 = 31 bits
    std::uint32_t not_eq_masked = (~eq_val) & mask_lower;
    
    return __builtin_popcount(not_eq_masked);
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
 * @brief LM-2001 Algorithm 2: 支持任意位宽n
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
    
    std::uint32_t eq_shifted = (~(alpha_shifted ^ beta_shifted ^ gamma_shifted)) & mask;
    std::uint32_t xor_val = (alpha ^ beta ^ gamma) & mask;
    std::uint32_t check = eq_shifted & (xor_val ^ beta_shifted);
    
    if (check == 0) {
        return -1;
    }
    
    std::uint32_t eq_val = (~(alpha ^ beta ^ gamma)) & mask;
    std::uint32_t mask_lower = (n == 32) ? 0x7FFFFFFFu : ((1u << (n - 1)) - 1);
    std::uint32_t not_eq_masked = (~eq_val) & mask_lower;
    
    return __builtin_popcount(not_eq_masked);
}

} // namespace arx_operators
} // namespace neoalz
