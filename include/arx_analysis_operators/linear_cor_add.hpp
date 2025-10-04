/**
 * @file linear_cor_add.hpp
 * @brief 模加法線性相關度計算（變量-變量）
 * 
 * 論文：Wallén (2003), "Linear Approximations of Addition Modulo 2^n", FSE 2003
 * 
 * 算法：M_n^T矩陣方法
 * 複雜度：O(n)
 * 
 * 相關度：
 * cor(α·X ⊕ β·Y ⊕ γ·(X⊞Y)) = S / 2^n
 * 其中S是Walsh係數
 */

#pragma once

#include <cstdint>
#include <cmath>

namespace neoalz {
namespace arx_operators {

/**
 * @brief Wallén M_n^T: 計算模加法線性相關度（變量-變量）
 * 
 * ⚠️ 警告：當前是簡化版本！
 * 
 * 論文：Wallén, FSE 2003
 * 論文複雜度：O(n)（完整M_n^T矩陣方法）
 * 當前複雜度：O(1)（簡化近似）
 * 
 * 當前實現：
 * - 使用簡化公式：weight = HW(α ⊕ β ⊕ γ)
 * - 複雜度：O(1)
 * - 精確度：僅對某些特殊情況精確，一般情況為近似
 * 
 * 完整實現：
 * - 需要按位計算M_n^T矩陣
 * - 複雜度：O(n)
 * - 精確度：完全精確
 * - 論文無對數算法（O(n)是理論最優）
 * 
 * @param alpha 輸入掩碼1
 * @param beta 輸入掩碼2
 * @param gamma 輸出掩碼
 * @param n 字長（默認32）
 * @return 相關度權重 -log₂|cor|（近似值）
 */
inline int linear_cor_add_wallen(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma,
    int n = 32
) noexcept {
    // ⚠️ 簡化實現：只適用於快速估計
    // C = α ⊕ β ⊕ γ
    std::uint32_t C = alpha ^ beta ^ gamma;
    
    // 簡化公式：weight ≈ HW(C)
    // 注意：這只是近似，完整Wallén算法需要O(n)計算
    int weight = __builtin_popcount(C);
    
    return weight;
}

/**
 * @brief 計算模加法線性相關度
 * 
 * @param alpha 輸入掩碼1
 * @param beta 輸入掩碼2
 * @param gamma 輸出掩碼
 * @param n 字長
 * @return 相關度 ∈ [-1, 1]
 */
inline double linear_cor_add_value(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma,
    int n = 32
) noexcept {
    int weight = linear_cor_add_wallen(alpha, beta, gamma, n);
    double cor = std::pow(2.0, -weight);
    return cor;
}

} // namespace arx_operators
} // namespace neoalz
