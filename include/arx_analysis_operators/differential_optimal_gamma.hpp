/**
 * @file differential_optimal_gamma.hpp
 * @brief 查找最优输出差分γ - Lipmaa & Moriai (2001) Algorithm 4
 * 
 * 论文："Efficient Algorithms for Computing Differential Properties of Addition"
 *       Lipmaa & Moriai, FSE 2001
 * 
 * 功能：给定输入差分(α, β)，在Θ(log n)时间内找到最优输出差分γ
 *       使得 DP+(α, β → γ) = DP+_max(α, β)
 * 
 * 依赖算法：
 * - Algorithm 1: aop(x) - All-one parity, Θ(log n)
 * - Algorithm 4: find_optimal_gamma(α, β) - Θ(log n)
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include "arx_analysis_operators/differential_xdp_add.hpp"

namespace neoalz {
namespace arx_operators {

/**
 * @brief Algorithm 1: 计算All-One Parity - Θ(log n)时间
 * 
 * 论文Algorithm 1 (Lines 292-299):
 * 
 * aop(x)_i = 1 iff x的第i位开始的连续1序列长度为奇数
 * 
 * 算法步骤（n = 32）：
 * 1. x[1] = x ∧ (x >> 1)        ← 检测相邻的1对
 * 2. 循环: x[i] = x[i-1] ∧ (x[i-1] >> 2^(i-1))  ← 倍增
 * 3. y[1] = x ∧ ¬x[1]           ← 找单独的1
 * 4. 循环: y[i] = y[i-1] ∨ ((y[i-1] >> 2^(i-1)) ∧ x[i-1])
 * 5. Return y[log2(n)]
 * 
 * @param x 输入32位整数
 * @return aop(x)
 */
inline std::uint32_t aop(std::uint32_t x) noexcept {
    constexpr int log2_n = 5;  // log2(32) = 5
    
    // Step 1: x[1] = x ∧ (x >> 1)
    std::uint32_t x_arr[6];  // x[0] to x[5]
    x_arr[0] = x;
    x_arr[1] = x & (x >> 1);
    
    // Step 2: For i = 2 to log2(n) - 1
    for (int i = 2; i < log2_n; ++i) {
        int shift = 1 << (i - 1);  // 2^(i-1)
        x_arr[i] = x_arr[i - 1] & (x_arr[i - 1] >> shift);
    }
    
    // Step 3: y[1] = x ∧ ¬x[1]
    std::uint32_t y_arr[6];  // y[0] to y[5]
    y_arr[0] = 0;
    y_arr[1] = x & ~x_arr[1];
    
    // Step 4: For i = 2 to log2(n)
    for (int i = 2; i <= log2_n; ++i) {
        int shift = 1 << (i - 1);  // 2^(i-1)
        y_arr[i] = y_arr[i - 1] | ((y_arr[i - 1] >> shift) & x_arr[i - 1]);
    }
    
    // Step 5: Return y[log2(n)]
    return y_arr[log2_n];
}

/**
 * @brief Bit-reverse函数
 * 
 * 论文Line 302: x'_i := x_{n-i}
 * 
 * @param x 输入32位整数
 * @return bit-reversed的x
 */
inline std::uint32_t bitreverse32(std::uint32_t x) noexcept {
    // 使用标准bit-reverse算法
    x = ((x & 0x55555555) << 1) | ((x >> 1) & 0x55555555);  // 交换相邻位
    x = ((x & 0x33333333) << 2) | ((x >> 2) & 0x33333333);  // 交换2位块
    x = ((x & 0x0F0F0F0F) << 4) | ((x >> 4) & 0x0F0F0F0F);  // 交换4位块
    x = ((x & 0x00FF00FF) << 8) | ((x >> 8) & 0x00FF00FF);  // 交换字节
    x = (x << 16) | (x >> 16);                               // 交换半字
    return x;
}

/**
 * @brief aopr(x) - All-One Parity Reverse
 * 
 * 论文Line 301-302:
 * aopr(x) = aop(x'), where x'_i := x_{n-i}
 * 
 * @param x 输入32位整数
 * @return aopr(x)
 */
inline std::uint32_t aopr(std::uint32_t x) noexcept {
    std::uint32_t x_rev = bitreverse32(x);
    std::uint32_t result = aop(x_rev);
    return bitreverse32(result);
}

/**
 * @brief Algorithm 4: 查找最优输出差分γ - Θ(log n)时间
 * 
 * 论文Algorithm 4 (Lines 653-664):
 * 
 * 给定输入差分(α, β)，找到使得DP+(α, β → γ)最大的γ
 * 
 * 算法步骤：
 * 1. r ← α ∧ 1;
 * 2. e ← ¬(α ⊕ β) ∧ ¬r;
 * 3. a ← e ∧ (e << 1) ∧ (α ⊕ (α << 1));
 * 4. p ← aopr(a);
 * 5. a ← (a ∨ (a >> 1)) ∧ ¬r;
 * 6. b ← (a ∨ e) << 1;
 * 7. γ ← ((α ⊕ p) ∧ a) ∨ ((α ⊕ β ⊕ (α << 1)) ∧ ¬a ∧ b) ∨ (α ∧ ¬a ∧ ¬b);
 * 8. γ ← (γ ∧ ¬1) ∨ ((α ⊕ β) ∧ 1);
 * 9. Return γ
 * 
 * @param alpha 输入差分α
 * @param beta 输入差分β
 * @return 最优输出差分γ
 */
inline std::uint32_t find_optimal_gamma(
    std::uint32_t alpha,
    std::uint32_t beta
) noexcept {
    // Step 1: r ← α ∧ 1
    std::uint32_t r = alpha & 1;
    
    // Step 2: e ← ¬(α ⊕ β) ∧ ¬r
    std::uint32_t e = ~(alpha ^ beta) & ~r;
    
    // Step 3: a ← e ∧ (e << 1) ∧ (α ⊕ (α << 1))
    std::uint32_t a = e & (e << 1) & (alpha ^ (alpha << 1));
    
    // Step 4: p ← aopr(a)
    std::uint32_t p = aopr(a);
    
    // Step 5: a ← (a ∨ (a >> 1)) ∧ ¬r
    a = (a | (a >> 1)) & ~r;
    
    // Step 6: b ← (a ∨ e) << 1
    std::uint32_t b = (a | e) << 1;
    
    // Step 7: γ ← ((α ⊕ p) ∧ a) ∨ ((α ⊕ β ⊕ (α << 1)) ∧ ¬a ∧ b) ∨ (α ∧ ¬a ∧ ¬b)
    std::uint32_t gamma = ((alpha ^ p) & a) 
                        | ((alpha ^ beta ^ (alpha << 1)) & ~a & b) 
                        | (alpha & ~a & ~b);
    
    // Step 8: γ ← (γ ∧ ¬1) ∨ ((α ⊕ β) ∧ 1)
    gamma = (gamma & ~1u) | ((alpha ^ beta) & 1);
    
    // Step 9: Return γ
    return gamma;
}

/**
 * @brief 包装函数：查找最优γ并计算其权重
 * 
 * 用于差分搜索框架，直接返回(最优γ, 权重)
 * 
 * @param alpha 输入差分α
 * @param beta 输入差分β
 * @return pair<最优γ, 权重>
 */
inline std::pair<std::uint32_t, int> find_optimal_gamma_with_weight(
    std::uint32_t alpha,
    std::uint32_t beta
) noexcept {
    // Step 1: 使用Algorithm 4找到最优γ
    std::uint32_t gamma = find_optimal_gamma(alpha, beta);
    
    // Step 2: 使用Algorithm 2计算权重
    int weight = xdp_add_lm2001(alpha, beta, gamma);
    
    return {gamma, weight};
}

} // namespace arx_operators
} // namespace neoalz
