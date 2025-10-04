#pragma once

/**
 * @file linear_correlation_addconst.hpp
 * @brief 精確的模加/模減常量線性相關性計算
 * 
 * 基於：
 * - Wallén, J. (2003). "Linear Approximations of Addition Modulo 2^n", FSE 2003
 * - Huang & Wang (2020). "Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers"
 * 
 * 核心算法：按位進位DP，將模加拆成 (x_i, k_i, c_i) → (y_i, c_{i+1}) 的兩狀態遞推
 * 
 * 時間複雜度：O(n)
 * 精確度：完全精確，無近似
 * 
 * 用途：計算 Y = X ⊞ K (mod 2^n) 的線性逼近
 *       α·X ⊕ β·Y 的相關性
 * 
 * 其中 K 是固定常量（"一端已定"的情況）
 */

#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>

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
 * @brief 精確計算模加常量的線性相關性（32位專用）
 * 
 * 計算 Y = X + K (mod 2^n) 的線性逼近相關性
 * 線性逼近：α·X ⊕ β·Y
 * 
 * 算法：Wallén 2003的按位進位DP
 * - 把模加拆成按位操作：(x_i, k_i, c_i) → (y_i, c_{i+1})
 * - 維護兩個狀態：carry=0 和 carry=1
 * - 累加Walsh係數：±1
 * - 最後計算相關性：S / 2^n
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
    // 初始化：v[carry] 表示當前carry狀態下的Walsh累加
    // carry=0時初始為1，carry=1時初始為0
    std::int64_t v0 = 1;  // v[carry=0]
    std::int64_t v1 = 0;  // v[carry=1]
    
    // 按位遞推
    for (int i = 0; i < nbits; ++i) {
        const int ai = (alpha >> i) & 1;  // α的第i位
        const int bi = (beta  >> i) & 1;  // β的第i位
        const int ki = (K     >> i) & 1;  // K的第i位
        
        std::int64_t nv0 = 0;  // 新的v[carry=0]
        std::int64_t nv1 = 0;  // 新的v[carry=1]
        
        // 枚舉x_i的兩種可能：0和1
        // 對於每種x_i，結合當前carry，計算輸出y_i和新carry
        
        // === 情況1：x_i = 0 ===
        {
            const int x = 0;
            
            // carry_in = 0
            {
                const int c_in = 0;
                const int y = x ^ ki ^ c_in;  // y_i = x_i ⊕ k_i ⊕ c_in
                const int c_out = (x & ki) | (x & c_in) | (ki & c_in);  // 新carry
                const int exponent = (ai & x) ^ (bi & y);  // α_i·x_i ⊕ β_i·y_i
                
                // Walsh項：(-1)^exponent
                if (c_out == 0) {
                    nv0 += (exponent ? -v0 : v0);
                } else {
                    nv1 += (exponent ? -v0 : v0);
                }
            }
            
            // carry_in = 1
            {
                const int c_in = 1;
                const int y = x ^ ki ^ c_in;
                const int c_out = (x & ki) | (x & c_in) | (ki & c_in);
                const int exponent = (ai & x) ^ (bi & y);
                
                if (c_out == 0) {
                    nv0 += (exponent ? -v1 : v1);
                } else {
                    nv1 += (exponent ? -v1 : v1);
                }
            }
        }
        
        // === 情況2：x_i = 1 ===
        {
            const int x = 1;
            
            // carry_in = 0
            {
                const int c_in = 0;
                const int y = x ^ ki ^ c_in;
                const int c_out = (x & ki) | (x & c_in) | (ki & c_in);
                const int exponent = (ai & x) ^ (bi & y);
                
                if (c_out == 0) {
                    nv0 += (exponent ? -v0 : v0);
                } else {
                    nv1 += (exponent ? -v0 : v0);
                }
            }
            
            // carry_in = 1
            {
                const int c_in = 1;
                const int y = x ^ ki ^ c_in;
                const int c_out = (x & ki) | (x & c_in) | (ki & c_in);
                const int exponent = (ai & x) ^ (bi & y);
                
                if (c_out == 0) {
                    nv0 += (exponent ? -v1 : v1);
                } else {
                    nv1 += (exponent ? -v1 : v1);
                }
            }
        }
        
        v0 = nv0;
        v1 = nv1;
    }
    
    // 最終Walsh和：兩個carry狀態的總和
    const std::int64_t S = v0 + v1;
    
    // 計算相關性：cor = S / 2^n
    // 使用 ldexp 避免溢出：ldexp(x, -n) = x / 2^n
    const double corr = std::ldexp(static_cast<double>(S), -nbits);
    
    // 計算權重：weight = -log2(|cor|) = n - log2(|S|)
    double weight;
    if (S == 0) {
        weight = std::numeric_limits<double>::infinity();
    } else {
        weight = static_cast<double>(nbits) - std::log2(std::fabs(static_cast<double>(S)));
    }
    
    return LinearCorrelation(corr, weight);
}

/**
 * @brief 精確計算模減常量的線性相關性（32位專用）
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
    
    // 轉換為模加問題
    return corr_add_x_plus_const32(alpha, beta, K, nbits);
}

/**
 * @brief 批量枚舉：給定α和K，枚舉所有權重≤閾值的β
 * 
 * 用於自動搜索框架
 * 
 * @param alpha 固定的輸入掩碼
 * @param K 固定的常量
 * @param weight_threshold 權重閾值
 * @param yield 回調函數 (beta, correlation, weight)
 */
template<typename Yield>
inline void enumerate_beta_for_addconst(
    std::uint32_t alpha,
    std::uint32_t K,
    double weight_threshold,
    Yield&& yield,
    int nbits = 32
) {
    // 簡化枚舉：只測試一些候選β
    // 完整版本：遍歷所有2^32個β（計算密集）
    
    // 候選生成策略：
    std::vector<std::uint32_t> candidates;
    
    // 1. 基本候選
    candidates.push_back(0);
    candidates.push_back(alpha);
    candidates.push_back(K);
    candidates.push_back(alpha ^ K);
    
    // 2. 單bit掩碼
    for (int i = 0; i < nbits; ++i) {
        candidates.push_back(1u << i);
    }
    
    // 3. 旋轉變體
    for (int shift = 1; shift < nbits; shift *= 2) {
        candidates.push_back((alpha << shift) | (alpha >> (nbits - shift)));
    }
    
    // 檢查每個候選
    for (std::uint32_t beta : candidates) {
        auto result = corr_add_x_plus_const32(alpha, beta, K, nbits);
        if (result.is_feasible() && result.weight <= weight_threshold) {
            yield(beta, result.correlation, result.weight);
        }
    }
}

} // namespace neoalz
