#pragma once

/**
 * @file linear_correlation_addconst.hpp
 * @brief 模加/模減常量線性相關性計算
 * 
 * ✅ 關鍵發現：Wallén 2003的算法是**通用的**！
 * 
 * 論文：Wallén, J. (2003). "Linear Approximations of Addition Modulo 2^n", FSE 2003
 * 
 * Lemma 7: C(u ← v, w) = C(u ←^carry v+u, w+u)
 * Theorem 1: C(u ←^carry v, w) = ±2^{-HW(z)}
 * 
 * **這個算法對任意v, w都成立，不管是變量還是常量！**
 * 
 * 因此，常量加法 Y = X + K 可以直接調用 linear_cor_add_logn(β, α, K)
 * 
 * 時間複雜度：Θ(log n)
 * 精確度：完全精確
 */

#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>
#include "arx_analysis_operators/linear_cor_add_logn.hpp"

namespace neoalz {

/**
 * @brief 線性相關性結果
 */
struct LinearCorrelation {
    double correlation;  ///< 相關性，範圍 [-1, 1]
    double weight;       ///< 權重 = -log2(|correlation|)，不可行時為 INF
    
    LinearCorrelation() : correlation(0.0), weight(std::numeric_limits<double>::infinity()) {}
    LinearCorrelation(double corr, double w) : correlation(corr), weight(w) {}
    
    bool is_feasible() const noexcept {
        return !std::isinf(weight) && correlation != 0.0;
    }
};

/**
 * @brief 計算模加常量的線性相關性（通用Wallén算法）
 * 
 * 計算 Y = X + K (mod 2^n) 的線性逼近相關性
 * 線性逼近：α·X ⊕ β·Y
 * 
 * ✅ 關鍵理解：Wallén的算法是**通用的**！
 * 
 * Lemma 7: C(u ← v, w) = C(u ←^carry v+u, w+u)
 * Theorem 1: C(u ←^carry v, w) = ±2^{-HW(z)} if vz≠0 and wz≠0
 * 
 * 對於常量加法 Y = X + K：
 * - u = β（輸出掩碼）
 * - v = α（輸入掩碼）  
 * - w = K（常量的實際值！）
 * 
 * **K雖然是常量，但在算法中就是一個uint32_t值，直接用即可！**
 * 
 * 算法：直接包裝 linear_cor_add_logn()
 * 複雜度：Θ(log n)
 * 
 * @param alpha 輸入掩碼（變量X）
 * @param beta 輸出掩碼（變量Y）
 * @param K 固定常量
 * @param nbits 位寬（通常是32）
 * @return 線性相關性和權重
 */
inline LinearCorrelation corr_add_x_plus_const32(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t K,
    int nbits = 32
) noexcept {
    // 直接調用Wallén通用算法！
    // linear_cor_add_logn(u, v, w) 對應:
    //   u = beta（輸出掩碼）
    //   v = alpha（變量X的掩碼）
    //   w = K（常量的實際值）
    int weight = arx_operators::linear_cor_add_wallen_logn(beta, alpha, K);
    
    if (weight < 0) {
        // 不可行
        return LinearCorrelation(0.0, std::numeric_limits<double>::infinity());
    }
    
    // 計算相關度：±2^{-weight}
    double corr = std::pow(2.0, -weight);
    
    return LinearCorrelation(corr, static_cast<double>(weight));
}

/**
 * @brief 計算模減常量的線性相關性
 * 
 * 計算 Y = X - C (mod 2^n) 的線性逼近相關性
 * 
 * 轉換：X - C = X + (2^n - C) = X + (~C + 1)
 * 
 * @param alpha 輸入掩碼（變量X）
 * @param beta 輸出掩碼（變量Y）
 * @param C 被減的常量
 * @param nbits 位寬（通常是32）
 * @return 線性相關性和權重
 */
inline LinearCorrelation corr_add_x_minus_const32(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t C,
    int nbits = 32
) noexcept {
    // 計算補數：2^n - C = ~C + 1
    const std::uint32_t mask = (nbits == 32) ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
    const std::uint32_t K = (~C + 1u) & mask;
    
    // 轉換為模加問題，直接調用Wallén通用算法
    return corr_add_x_plus_const32(alpha, beta, K, nbits);
}

} // namespace neoalz
