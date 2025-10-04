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
 * @brief LM-2001: 計算xdp⁺的權重（變量-變量）
 * 
 * 論文：Lipmaa & Moriai, FSE 2001
 * 複雜度：O(1)
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
    // LM-2001公式
    // eq = ~(α ⊕ β ⊕ γ)
    std::uint32_t eq = ~(alpha ^ beta ^ gamma);
    
    // 權重 = 32 - popcount(eq)
    int weight = 32 - __builtin_popcount(eq);
    
    // 如果weight < 0，則不可能
    if (weight < 0) return -1;
    
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

} // namespace arx_operators
} // namespace neoalz
