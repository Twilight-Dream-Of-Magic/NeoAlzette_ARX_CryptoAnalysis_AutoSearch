#pragma once
#include <cstdint>
#include <utility>
#include "neoalzette_core.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

namespace neoalz {

/**
 * @brief NeoAlzette单轮线性掩码传播步骤包装（逆向）
 * 
 * 🔴 关键：线性分析是**逆向**的！从输出掩码向输入掩码传播
 * 
 * 逆向顺序：
 * - Subround 2 (Step 10 → 6)
 * - Subround 1 (Step 5 → 1)
 * 
 * 论文参考：
 * - Wallén (2003): 线性相关性算法
 * - Huang & Wang (2020): cLAT框架
 */
class NeoAlzetteLinearStep {
public:
    /**
     * @brief 线性相关性结果
     */
    struct LinearCorrelation {
        double correlation;
        int weight;
        bool feasible;
        
        LinearCorrelation() : correlation(0.0), weight(-1), feasible(false) {}
        LinearCorrelation(double c, int w) 
            : correlation(c), weight(w), feasible(w >= 0) {}
    };
    
    // ========================================================================
    // Subround 2 逆向传播（Step 10 → 6）
    // ========================================================================
    
    /**
     * @brief Subround 2, Step 10逆向: B ⊕ A 的掩码传播
     * 
     * 前向：B_out = B ⊕ A
     * 逆向：给定mask_B_out，枚举(mask_B, mask_A)使得 mask_B_out = mask_B ⊕ mask_A
     * 
     * 线性操作：weight = 0
     * 
     * 注意：这是一个"拆分"操作，需要枚举所有可能的(mask_B, mask_A)组合
     * 实际搜索框架中应该枚举，这里提供单一候选的验证
     */
    static inline bool
    subround2_step10_xor_verify(std::uint32_t mask_B_out, 
                                 std::uint32_t mask_B_candidate,
                                 std::uint32_t mask_A_candidate) noexcept {
        // XOR线性：mask_B_out = mask_B ⊕ mask_A
        return mask_B_out == (mask_B_candidate ^ mask_A_candidate);
    }
    
    /**
     * @brief Subround 2, Step 9逆向: L1(B), L2(A) 转置传播
     * 
     * 前向：B' = L1(B), A' = L2(A)
     * 逆向：mask_B_in = L1^T(mask_B_out), mask_A_in = L2^T(mask_A_out)
     * 
     * 关键：使用**转置**L^T，不是逆矩阵L^(-1)
     * 对于XOR-rotate线性层：L^T是将rotl替换为rotr（或反之）
     * 
     * weight = 0（线性操作）
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround2_step9_linear_transpose(std::uint32_t mask_B_out, 
                                      std::uint32_t mask_A_out) noexcept {
        return {
            NeoAlzetteCore::l1_transpose(mask_B_out),  // mask_B_in = L1^T(mask_B_out)
            NeoAlzetteCore::l2_transpose(mask_A_out)   // mask_A_in = L2^T(mask_A_out)
        };
    }
    
    /**
     * @brief Subround 2, Step 8逆向: B - c1 的线性相关性
     * 
     * 前向：B_out = B_in - c1
     * 逆向：给定(mask_B_out, mask_B_in)，计算相关性
     * 
     * 使用Wallén Lemma 7：常量情况也适用通用算法
     * 
     * 论文：Wallén (2003) Lemma 7
     * 复杂度：Θ(log n)
     */
    static inline LinearCorrelation
    subround2_step8_subtract_const(std::uint32_t mask_B_out,
                                    std::uint32_t mask_B_in,
                                    std::uint32_t c1) noexcept {
        // B - c1 等价于 B + (-c1)
        std::uint32_t neg_c1 = (~c1 + 1);
        
        auto result = corr_add_x_plus_const32(
            mask_B_in,   // α (输入掩码)
            mask_B_out,  // β (输出掩码)
            neg_c1       // K (常量)
        );
        
        return LinearCorrelation(result.correlation, static_cast<int>(result.weight));
    }
    
    /**
     * @brief Subround 2, Step 7逆向: B + A 的线性相关性
     * 
     * 前向：B_out = B_in + A_in
     * 逆向：给定(mask_B_out, mask_B_in, mask_A_in)，计算相关性
     * 
     * 使用Wallén核心算法
     * 
     * 论文：Wallén (2003)
     * 复杂度：Θ(log n)
     */
    static inline LinearCorrelation
    subround2_step7_modular_add(std::uint32_t mask_B_in,
                                 std::uint32_t mask_A_in,
                                 std::uint32_t mask_B_out) noexcept {
        int weight = arx_operators::linear_cor_add_wallen_logn(
            mask_B_out,  // γ (输出掩码)
            mask_B_in,   // α (第一输入掩码)
            mask_A_in    // β (第二输入掩码)
        );
        
        if (weight < 0) {
            return LinearCorrelation();  // 不可行
        }
        
        double correlation = std::pow(2.0, -weight);
        return LinearCorrelation(correlation, weight);
    }
    
    /**
     * @brief Subround 2, Step 6逆向: cd_from_A 的掩码传播
     * 
     * cd_from_A是XOR操作：
     * - c = L1(A ⊕ rc0)
     * - d = L2(rotl(A, 24) ⊕ rc1)
     * 
     * 逆向（掩码域常量消失）：
     * - mask_A_c = L1^T(mask_c)
     * - mask_A_d = L2^T(rotr(mask_d, 24))  // 注意：rotl逆向是rotr
     * 
     * 最终：mask_A = mask_A_c ⊕ mask_A_d（需要枚举拆分）
     * 
     * weight = 0
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround2_step6_cd_from_A_transpose(std::uint32_t mask_c, 
                                         std::uint32_t mask_d) noexcept {
        using Core = NeoAlzetteCore;
        
        // c = L1(A ⊕ rc0) → mask_A_c = L1^T(mask_c)
        std::uint32_t mask_A_from_c = Core::l1_transpose(mask_c);
        
        // d = L2(rotl(A, 24) ⊕ rc1) → mask_A_d = rotr(L2^T(mask_d), 24)
        std::uint32_t mask_d_through_L2T = Core::l2_transpose(mask_d);
        std::uint32_t mask_A_from_d = Core::rotr(mask_d_through_L2T, 24);
        
        return {mask_A_from_c, mask_A_from_d};
    }
    
    // ========================================================================
    // Subround 1 逆向传播（Step 5 → 1）
    // ========================================================================
    
    /**
     * @brief Subround 1, Step 5逆向: B ⊕ A_L1 的掩码传播
     * 
     * 前向：B_out = B_in ⊕ A_L1
     * 逆向：mask_B_out = mask_B_in ⊕ mask_A_L1（需要枚举拆分）
     */
    static inline bool
    subround1_step5_xor_verify(std::uint32_t mask_B_out,
                                std::uint32_t mask_B_in_candidate,
                                std::uint32_t mask_A_L1_candidate) noexcept {
        return mask_B_out == (mask_B_in_candidate ^ mask_A_L1_candidate);
    }
    
    /**
     * @brief Subround 1, Step 4逆向: L1(A) 转置传播
     * 
     * 前向：A_L1 = L1(A)
     * 逆向：mask_A = L1^T(mask_A_L1)
     */
    static inline std::uint32_t
    subround1_step4_linear_transpose(std::uint32_t mask_A_L1) noexcept {
        return NeoAlzetteCore::l1_transpose(mask_A_L1);
    }
    
    /**
     * @brief Subround 1, Step 3逆向: A - c0 的线性相关性
     */
    static inline LinearCorrelation
    subround1_step3_subtract_const(std::uint32_t mask_A_out,
                                    std::uint32_t mask_A_in,
                                    std::uint32_t c0) noexcept {
        std::uint32_t neg_c0 = (~c0 + 1);
        
        auto result = corr_add_x_plus_const32(
            mask_A_in,
            mask_A_out,
            neg_c0
        );
        
        return LinearCorrelation(result.correlation, static_cast<int>(result.weight));
    }
    
    /**
     * @brief Subround 1, Step 2逆向: A + tmp1 的线性相关性
     */
    static inline LinearCorrelation
    subround1_step2_modular_add(std::uint32_t mask_A_in,
                                 std::uint32_t mask_tmp1,
                                 std::uint32_t mask_A_out) noexcept {
        int weight = arx_operators::linear_cor_add_wallen_logn(
            mask_A_out,
            mask_A_in,
            mask_tmp1
        );
        
        if (weight < 0) {
            return LinearCorrelation();
        }
        
        double correlation = std::pow(2.0, -weight);
        return LinearCorrelation(correlation, weight);
    }
    
    /**
     * @brief Subround 1, Step 1逆向: cd_from_B 的掩码传播
     * 
     * cd_from_B是XOR操作：
     * - C0 = L2(B ⊕ rc0)
     * - D0 = L1(rotr(B, 3) ⊕ rc1)
     * 
     * 逆向：
     * - mask_B_c = L2^T(mask_C0)
     * - mask_B_d = rotl(L1^T(mask_D0), 3)  // rotr逆向是rotl
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround1_step1_cd_from_B_transpose(std::uint32_t mask_C0,
                                         std::uint32_t mask_D0) noexcept {
        using Core = NeoAlzetteCore;
        
        // C0 = L2(B ⊕ rc0) → mask_B_c = L2^T(mask_C0)
        std::uint32_t mask_B_from_c = Core::l2_transpose(mask_C0);
        
        // D0 = L1(rotr(B, 3) ⊕ rc1) → mask_B_d = rotl(L1^T(mask_D0), 3)
        std::uint32_t mask_d_through_L1T = Core::l1_transpose(mask_D0);
        std::uint32_t mask_B_from_d = Core::rotl(mask_d_through_L1T, 3);
        
        return {mask_B_from_c, mask_B_from_d};
    }
    
    /**
     * @brief Subround 1/2: 跨分支注入逆向传播
     * 
     * 前向：A/B ^= (rotl(C, 24) ^ rotl(D, 16) ^ rc)
     * 逆向：给定mask_out，枚举拆分为(mask_in, mask_C, mask_D)
     * 
     * 线性操作，weight = 0，但需要枚举
     */
    static inline std::uint32_t
    cross_injection_compute_injection_mask(std::uint32_t mask_C, 
                                           std::uint32_t mask_D) noexcept {
        using Core = NeoAlzetteCore;
        return Core::rotl(mask_C, 24) ^ Core::rotl(mask_D, 16);
    }
    
    /**
     * @brief 验证跨分支注入掩码分解
     */
    static inline bool
    cross_injection_verify_split(std::uint32_t mask_out,
                                  std::uint32_t mask_in,
                                  std::uint32_t mask_C,
                                  std::uint32_t mask_D) noexcept {
        std::uint32_t injection = cross_injection_compute_injection_mask(mask_C, mask_D);
        return mask_out == (mask_in ^ injection);
    }
    
    /**
     * @brief 完整单轮线性掩码逆向传播（启发式，用于快速估计）
     * 
     * 输入：(mask_A_out, mask_B_out) - 输出掩码
     * 输出：(mask_A_in, mask_B_in) - 输入掩码，总权重
     * 
     * 注意：这是启发式版本，不保证找到最优路径
     * 实际搜索应该使用Linear Search框架逐步枚举
     */
    struct SingleRoundLinearResult {
        std::uint32_t mask_A_in;
        std::uint32_t mask_B_in;
        int total_weight;
        bool feasible;
    };
    
    static SingleRoundLinearResult propagate_single_round_backward_heuristic(
        std::uint32_t mask_A_out,
        std::uint32_t mask_B_out,
        std::uint32_t c0,
        std::uint32_t c1
    ) noexcept;
};

} // namespace neoalz
