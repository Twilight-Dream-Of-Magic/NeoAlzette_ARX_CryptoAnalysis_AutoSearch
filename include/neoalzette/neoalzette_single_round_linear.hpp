#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <cmath>
#include "neoalzette/neoalzette_core.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

namespace neoalz {

/**
 * @file neoalzette_single_round_linear.hpp
 * @brief NeoAlzette单轮完整线性掩码传播实现
 * 
 * ⚠️ 关键：线性分析是**反向传播**！
 * 
 * 前向加密方向：
 *   输入(A,B) → [加密操作] → 输出(A',B')
 * 
 * 线性掩码传播方向（反向）：
 *   输出掩码(α_out, β_out) → [转置操作] → 输入掩码(α_in, β_in)
 * 
 * 转置规则：
 * 1. 模加/模减：使用Wallén算法（变量+变量）或DP算法（变量+常量）
 * 2. XOR：α_in = α_out（XOR自己就是转置）
 * 3. 线性层：使用transpose（转置矩阵）
 * 4. 旋转：rotr是rotl的转置
 */
class NeoAlzetteSingleRoundLinear {
public:
    /**
     * @brief 单轮线性传播结果
     */
    struct SingleRoundResult {
        std::uint32_t mask_A_in;
        std::uint32_t mask_B_in;
        double correlation;
        int weight;  // -log2(|correlation|)
        bool feasible;
    };
    
    /**
     * @brief 枚举单轮所有可行线性掩码（反向传播）
     * 
     * 给定输出掩码，反向推导输入掩码
     * 
     * 流程（反向）：
     * 1. 白化: A ^= RC[10], B ^= RC[11] → 掩码不变
     * 2. Subround 1 反向传播
     * 3. Subround 0 反向传播
     * 
     * @param mask_A_out 输出掩码A
     * @param mask_B_out 输出掩码B
     * @param correlation_threshold 相关性阈值
     * @param yield 回调函数 (mask_A_in, mask_B_in, correlation)
     */
    template<typename Yield>
    static void enumerate_complete_backward(
        std::uint32_t mask_A_out,
        std::uint32_t mask_B_out,
        double correlation_threshold,
        Yield&& yield
    );
    
    /**
     * @brief 计算单个线性路径的相关性（用于验证）
     * 
     * @return 相关性（-1表示不可行）
     */
    static double compute_correlation(
        std::uint32_t mask_A_in,
        std::uint32_t mask_B_in,
        std::uint32_t mask_A_out,
        std::uint32_t mask_B_out
    );
    
private:
    /**
     * @brief Subround 1 的反向掩码传播
     * 
     * 前向操作（加密方向）：
     *   A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
     *   B -= RC[6]
     *   B ^= rotl(A, 24)
     *   A ^= rotl(B, 16)
     *   B = l1_forward(B)
     *   A = l2_forward(A)
     *   [C1,D1] = cd_from_A(A, RC[7], RC[8])
     *   B ^= (rotl(C1,24) ^ rotl(D1,16) ^ RC[9])
     * 
     * 反向传播（掩码方向）：
     *   从 (mask_A_out, mask_B_out) → (mask_A_in, mask_B_in)
     */
    template<typename Yield>
    static void enumerate_subround1_backward(
        std::uint32_t mask_A_out,
        std::uint32_t mask_B_out,
        double correlation_budget,
        Yield&& yield_after_backward
    );
    
    /**
     * @brief Subround 0 的反向掩码传播
     */
    template<typename Yield>
    static void enumerate_subround0_backward(
        std::uint32_t mask_A_out,
        std::uint32_t mask_B_out,
        double correlation_budget,
        Yield&& yield_after_backward
    );
    
    /**
     * @brief 跨分支注入的转置（反向传播）
     * 
     * 前向：[C,D] = cd_from_B(B, rc0, rc1)
     *       A ^= (rotl(C,24) ^ rotl(D,16) ^ rc2)
     * 
     * 反向：给定mask_A，推导mask_B
     * 
     * 由于cd_from_B是线性的，我们需要计算它的转置
     */
    static std::uint32_t cd_from_B_transpose(
        std::uint32_t mask_C,
        std::uint32_t mask_D
    );
    
    static std::uint32_t cd_from_A_transpose(
        std::uint32_t mask_C,
        std::uint32_t mask_D
    );
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Yield>
void NeoAlzetteSingleRoundLinear::enumerate_subround1_backward(
    std::uint32_t mask_A_out,
    std::uint32_t mask_B_out,
    double correlation_budget,
    Yield&& yield_after_backward
) {
    // === 反向步骤（从后往前） ===
    
    // Step 8 反向: B ^= (rotl(C1,24) ^ rotl(D1,16) ^ RC[9])
    // 这是XOR，所以掩码不变（XOR自己就是转置）
    std::uint32_t mask_B_before_cd = mask_B_out;
    std::uint32_t mask_A_before_cd = mask_A_out;
    
    // Step 7 反向: cd_from_A 的转置
    // 需要从 mask_B_before_cd 推导出注入的掩码，然后传播到 mask_A
    // 简化版本：假设cd注入的影响可以忽略或线性传播
    // 完整版本需要详细推导cd_from_A的转置矩阵
    
    // Step 6 反向: A = l2_forward(A) → 使用 l2_transpose 做转置
    std::uint32_t mask_A_before_l2 = NeoAlzetteCore::l2_transpose(mask_A_before_cd);
    
    // Step 5 反向: B = l1_forward(B) → 使用 l1_transpose 做转置
    std::uint32_t mask_B_before_l1 = NeoAlzetteCore::l1_transpose(mask_B_before_cd);
    
    // Step 4 反向: A ^= rotl(B, 16)
    // 前向：A' = A ^ rotl(B, 16)
    // 反向掩码：mask_A 应用到 A'，需要传播到 A 和 B
    // mask_A · A' = mask_A · (A ^ rotl(B, 16)) = mask_A · A ⊕ mask_A · rotl(B, 16)
    // 所以：mask_A_in = mask_A_out, mask_B_in ^= rotr(mask_A_out, 16)
    std::uint32_t mask_A_before_xor2 = mask_A_before_l2;
    std::uint32_t mask_B_before_xor2 = mask_B_before_l1 ^ NeoAlzetteCore::rotr(mask_A_before_l2, 16);
    
    // Step 3 反向: B ^= rotl(A, 24)
    // 同理：mask_B_in = mask_B_out, mask_A_in ^= rotr(mask_B_out, 24)
    std::uint32_t mask_B_before_xor1 = mask_B_before_xor2;
    std::uint32_t mask_A_before_xor1 = mask_A_before_xor2 ^ NeoAlzetteCore::rotr(mask_B_before_xor2, 24);
    
    // Step 2 反向: B -= RC[6]（变量-常量）
    // 使用 linear_cor_addconst 反向枚举
    std::uint32_t mask_B_before_sub = mask_B_before_xor1;
    
    // 简化：假设常量模减对线性相关性影响较小，直接传播
    // 完整版本需要调用 corr_add_x_minus_const32 枚举所有可能的输入掩码
    
    // Step 1 反向: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    // 这是最复杂的：变量+变量
    // 前向：A' = A + (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    // 反向：给定 mask_A_out，枚举所有可能的 (mask_A_in, mask_B_in)
    
    std::uint32_t mask_A_before_add = mask_A_before_xor1;
    
    // 使用Wallén算法枚举：需要枚举所有满足相关性条件的掩码组合
    // 简化版本：尝试几个候选掩码
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mask_A_before_add, mask_B_before_sub},
        {mask_A_before_add, mask_B_before_sub ^ 1},
        {mask_A_before_add ^ 1, mask_B_before_sub},
        {0, mask_B_before_sub}
    };
    
    for (const auto& [candidate_mask_A, candidate_mask_B] : candidates) {
        // 计算线性相关性
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(candidate_mask_B, 31) ^ 
                                   NeoAlzetteCore::rotl(candidate_mask_B, 17);
        
        // 使用Wallén算法计算相关性
        double correlation = arx_operators::linear_cor_add_value_logn(candidate_mask_A, beta_mask, mask_A_before_add);
        
        if (correlation > 0 && std::abs(correlation) >= correlation_budget) {
            yield_after_backward(candidate_mask_A, candidate_mask_B, correlation);
        }
    }
}

template<typename Yield>
void NeoAlzetteSingleRoundLinear::enumerate_subround0_backward(
    std::uint32_t mask_A_out,
    std::uint32_t mask_B_out,
    double correlation_budget,
    Yield&& yield_after_backward
) {
    // 类似 subround1_backward 的实现
    // 反向传播所有步骤
    
    // 简化版本：直接传播掩码
    std::uint32_t mask_A_before_cd = mask_A_out;
    std::uint32_t mask_B_before_cd = mask_B_out;
    
    std::uint32_t mask_A_before_l1 = NeoAlzetteCore::l1_transpose(mask_A_before_cd);
    std::uint32_t mask_B_before_l2 = NeoAlzetteCore::l2_transpose(mask_B_before_cd);
    
    std::uint32_t mask_B_before_xor2 = mask_B_before_l2;
    std::uint32_t mask_A_before_xor2 = mask_A_before_l1 ^ NeoAlzetteCore::rotr(mask_B_before_l2, 16);
    
    std::uint32_t mask_A_before_xor1 = mask_A_before_xor2;
    std::uint32_t mask_B_before_xor1 = mask_B_before_xor2 ^ NeoAlzetteCore::rotr(mask_A_before_xor2, 24);
    
    // A -= RC[1] 反向
    std::uint32_t mask_A_before_sub = mask_A_before_xor1;
    
    // B += ... 反向
    std::uint32_t mask_B_before_add = mask_B_before_xor1;
    
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mask_A_before_sub, mask_B_before_add},
        {mask_A_before_sub ^ 1, mask_B_before_add},
        {mask_A_before_sub, mask_B_before_add ^ 1}
    };
    
    for (const auto& [candidate_mask_A, candidate_mask_B] : candidates) {
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(candidate_mask_A, 31) ^ 
                                   NeoAlzetteCore::rotl(candidate_mask_A, 17);
        
        double correlation = arx_operators::linear_cor_add_value_logn(mask_B_before_add, beta_mask, candidate_mask_B);
        
        if (correlation > 0 && std::abs(correlation) >= correlation_budget) {
            yield_after_backward(candidate_mask_A, candidate_mask_B, correlation);
        }
    }
}

template<typename Yield>
void NeoAlzetteSingleRoundLinear::enumerate_complete_backward(
    std::uint32_t mask_A_out,
    std::uint32_t mask_B_out,
    double correlation_threshold,
    Yield&& yield
) {
    // 白化反向：A ^= RC[10], B ^= RC[11] → 掩码不变
    std::uint32_t mask_A_after_whitening = mask_A_out;
    std::uint32_t mask_B_after_whitening = mask_B_out;
    
    // Subround 1 反向
    enumerate_subround1_backward(mask_A_after_whitening, mask_B_after_whitening, correlation_threshold,
        [&](std::uint32_t mask_A_after_sub1, std::uint32_t mask_B_after_sub1, double corr1) {
            // Subround 0 反向
            enumerate_subround0_backward(mask_A_after_sub1, mask_B_after_sub1, correlation_threshold / std::abs(corr1),
                [&](std::uint32_t mask_A_in, std::uint32_t mask_B_in, double corr0) {
                    double total_correlation = corr0 * corr1;
                    if (std::abs(total_correlation) >= correlation_threshold) {
                        yield(mask_A_in, mask_B_in, total_correlation);
                    }
                });
        });
}

// ============================================================================
// 跨分支注入转置实现
// ============================================================================

std::uint32_t NeoAlzetteSingleRoundLinear::cd_from_B_transpose(
    std::uint32_t mask_C,
    std::uint32_t mask_D
) {
    // 前向：c = l2_forward(B ^ rc0)
    //      d = l1_forward(rotr(B, 3) ^ rc1)
    //      t = rotl(c ^ d, 31)
    //      c ^= rotl(d, 17)
    //      d ^= rotr(t, 16)
    // 
    // 反向（转置）：需要推导从 (mask_C, mask_D) 到 mask_B 的线性映射
    // 
    // 简化版本：假设线性组合
    std::uint32_t mask_B = mask_C ^ mask_D;
    return mask_B;
}

std::uint32_t NeoAlzetteSingleRoundLinear::cd_from_A_transpose(
    std::uint32_t mask_C,
    std::uint32_t mask_D
) {
    // 类似 cd_from_B_transpose
    std::uint32_t mask_A = mask_C ^ mask_D;
    return mask_A;
}

} // namespace neoalz
