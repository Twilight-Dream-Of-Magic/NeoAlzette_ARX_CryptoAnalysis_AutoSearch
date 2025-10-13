/**
 * @file differential_addconst.hpp
 * @brief 常量加法差分分析（變量-常量）
 * 
 * 論文："A Bit-Vector Differential Model for the Modular Addition by a Constant" (2022)
 * 
 * 兩種算法：
 * 1. Theorem 2 (Machado 2015): 精確O(n)迭代方法
 * 2. Algorithm 1 (BvWeight): 近似O(log²n)位向量方法
 * 
 * 當前實現：Algorithm 1 (BvWeight) - 對數複雜度
 */

#pragma once

#include <cstdint>
#include <cmath>
#include "arx_analysis_operators/bitvector_ops.hpp"
#include "arx_analysis_operators/math_util.hpp"

namespace neoalz {
namespace arx_operators {

/**
 * @brief Algorithm 1: BvWeight - 計算常量加法差分權重
 * 
 * 論文Algorithm 1, Lines 1701-1749
 * 複雜度：O(log²n)
 * 
 * @param delta_x 輸入差分
 * @param constant 常量K
 * @param delta_y 輸出差分
 * @return 近似權重，0表示權重為0，-1表示不可能
 */
inline int diff_addconst_bvweight(
    std::uint32_t delta_x,
    std::uint32_t constant,
    std::uint32_t delta_y
) noexcept {
    using namespace bitvector;
    
    const std::uint32_t u = delta_x;
    const std::uint32_t v = delta_y;
    const std::uint32_t a = constant;
    
    // Algorithm 1, Lines 1704-1709
    uint32_t s000 = ~(u << 1) & ~(v << 1);
    uint32_t s000_prime = s000 & ~LZ(~s000);
    
    // Lines 1712-1720
    uint32_t t = ~s000_prime & (s000 << 1);
    uint32_t t_prime = s000_prime & ~(s000 << 1);
    
    // Lines 1722-1723
    uint32_t s = ((a << 1) & t) ^ (a & (s000 << 1));
    
    // Lines 1726-1730
    uint32_t q = ~((a << 1) ^ u ^ v);
    uint32_t d = RevCarry((s000_prime << 1) & t_prime, q) | q;
    
    // Line 1731
    uint32_t w = (q << (s & d)) | (s & ~d);
    
    // Lines 1733-1735
    uint32_t int_part = HW((u ^ v) << 1) ^ HW(s000_prime) ^ 
                       ParallelLog((w & s000_prime) << 1, s000_prime << 1);
    
    // Lines 1738-1742
    uint32_t frac = ParallelTrunc(w << 1, RevCarry((w & s000_prime) << 1, s000_prime << 1));
    
    // Line 1743: bvweight = int_part || frac（4位小數）
    uint32_t bvweight = (int_part << 4) | frac;
    
    // 轉換為權重
    if (bvweight == 0) return 0;
    double approx_weight = static_cast<double>(bvweight) / 16.0;
    return static_cast<int>(std::ceil(approx_weight));
}

// 使用公共 math_util.hpp 的 neg_mod_2n

/**
 * @brief 常量減法差分權重
 * 
 * X ⊟ C = X ⊞ (~C + 1)
 * 
 * @param delta_x 輸入差分
 * @param constant 常量C
 * @param delta_y 輸出差分
 * @return 近似權重
 */
inline int diff_subconst_bvweight(
    std::uint32_t delta_x,
    std::uint32_t constant,
    std::uint32_t delta_y
) noexcept {
    // X - C = X + ((~C) + 1)
    std::uint32_t neg_constant = neoalz::arx_operators::neg_mod_2n<uint32_t>(constant, 32);
    return diff_addconst_bvweight(delta_x, neg_constant, delta_y);
}

/**
 * @brief 計算常量加法差分概率
 * 
 * @param delta_x 輸入差分
 * @param constant 常量K
 * @param delta_y 輸出差分
 * @return 近似概率
 */
inline double diff_addconst_probability(
    std::uint32_t delta_x,
    std::uint32_t constant,
    std::uint32_t delta_y
) noexcept {
    int weight = diff_addconst_bvweight(delta_x, constant, delta_y);
    if (weight < 0) return 0.0;
    return std::pow(2.0, -weight);
}

/**
 * @brief 計算常量减法差分概率
 * 
 * @param delta_x 輸入差分
 * @param constant 常量K
 * @param delta_y 輸出差分
 * @return 近似概率
 */
inline double diff_subconst_probability(
    std::uint32_t delta_x,
    std::uint32_t constant,
    std::uint32_t delta_y
) noexcept {
    int weight = diff_subconst_bvweight(delta_x, constant, delta_y);
    if (weight < 0) return 0.0;
    return std::pow(2.0, -weight);
}

} // namespace arx_operators
} // namespace neoalz
