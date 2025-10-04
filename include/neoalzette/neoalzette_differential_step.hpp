#pragma once
#include <cstdint>
#include <utility>
#include "neoalzette_core.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"

namespace neoalz {

/**
 * @brief NeoAlzette单轮差分传播步骤包装
 * 
 * 用于将NeoAlzette的ARX步骤嵌入到Matsui搜索框架中
 * 每个函数处理一个原子操作的差分传播
 */
class NeoAlzetteDifferentialStep {
public:
    /**
     * @brief Subround 1: 线性层L1/L2传播
     * 
     * 差分域：Δout = L(Δin)
     * 线性操作直接传播差分，weight = 0
     */
    static inline std::pair<std::uint32_t, std::uint32_t> 
    subround1_linear_layer(std::uint32_t dA, std::uint32_t dB) noexcept {
        return {
            NeoAlzetteCore::l1_forward(dA),  // ΔA' = L1(ΔA)
            NeoAlzetteCore::l2_forward(dB)   // ΔB' = L2(ΔB)
        };
    }
    
    /**
     * @brief Subround 1: cd_from_B差分传播
     * 
     * cd_from_B是XOR操作：B ⊕ rc0, rotr(B,3) ⊕ rc1
     * 差分域常量消失：ΔC = L2(ΔB), ΔD = L1(rotr(ΔB,3))
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround1_cd_from_B(std::uint32_t dB) noexcept {
        return NeoAlzetteCore::cd_from_B_delta(dB);
    }
    
    /**
     * @brief Subround 1: 跨分支注入差分传播
     * 
     * A ^= (rotl(C, 24) ^ rotl(D, 16) ^ R[4])
     * 差分域：ΔA ^= (rotl(ΔC, 24) ^ rotl(ΔD, 16))
     * R[4]常量消失，weight = 0
     */
    static inline std::uint32_t
    subround1_cross_injection(std::uint32_t dA, std::uint32_t dC, std::uint32_t dD) noexcept {
        using Core = NeoAlzetteCore;
        std::uint32_t delta_injection = Core::rotl(dC, 24) ^ Core::rotl(dD, 16);
        return dA ^ delta_injection;
    }
    
    /**
     * @brief Subround 1: 模加操作 A + B
     * 
     * 使用LM-2001 xdp+ 算法
     * 
     * @param dA_in 输入差分ΔA
     * @param dB_in 输入差分ΔB
     * @param dOut_candidate 候选输出差分γ
     * @return weight，如果不可行返回-1
     */
    static inline int
    subround1_modular_add_weight(std::uint32_t dA_in, std::uint32_t dB_in, 
                                  std::uint32_t dOut_candidate) noexcept {
        return arx_operators::xdp_add_lm2001(dA_in, dB_in, dOut_candidate);
    }
    
    /**
     * @brief Subround 1: 模加最优输出差分查找
     * 
     * 使用LM-2001 Algorithm 4快速找到最优γ
     */
    static inline std::pair<std::uint32_t, int>
    subround1_modular_add_optimal(std::uint32_t dA_in, std::uint32_t dB_in) noexcept {
        return arx_operators::find_optimal_gamma_with_weight(dA_in, dB_in, 32);
    }
    
    /**
     * @brief Subround 1: 减常量 A - c0
     * 
     * 使用BvWeight算法
     * 
     * @param dA_in 输入差分
     * @param c0 常量（用于权重计算）
     * @param dOut_candidate 候选输出差分
     * @return weight，如果不可行返回-1
     */
    static inline int
    subround1_subtract_const_weight(std::uint32_t dA_in, std::uint32_t c0,
                                     std::uint32_t dOut_candidate) noexcept {
        // A - c0 等价于 A + (-c0)
        std::uint32_t neg_c0 = (~c0 + 1);
        return arx_operators::diff_addconst_bvweight(dA_in, neg_c0, dOut_candidate);
    }
    
    /**
     * @brief Subround 2: 线性层L1/L2传播
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround2_linear_layer(std::uint32_t dB, std::uint32_t dA) noexcept {
        return {
            NeoAlzetteCore::l1_forward(dB),  // ΔB' = L1(ΔB)
            NeoAlzetteCore::l2_forward(dA)   // ΔA' = L2(ΔA)
        };
    }
    
    /**
     * @brief Subround 2: cd_from_A差分传播
     */
    static inline std::pair<std::uint32_t, std::uint32_t>
    subround2_cd_from_A(std::uint32_t dA) noexcept {
        return NeoAlzetteCore::cd_from_A_delta(dA);
    }
    
    /**
     * @brief Subround 2: 跨分支注入差分传播
     */
    static inline std::uint32_t
    subround2_cross_injection(std::uint32_t dB, std::uint32_t dC, std::uint32_t dD) noexcept {
        using Core = NeoAlzetteCore;
        std::uint32_t delta_injection = Core::rotl(dC, 24) ^ Core::rotl(dD, 16);
        return dB ^ delta_injection;
    }
    
    /**
     * @brief Subround 2: 模加操作 B + A
     */
    static inline int
    subround2_modular_add_weight(std::uint32_t dB_in, std::uint32_t dA_in,
                                  std::uint32_t dOut_candidate) noexcept {
        return arx_operators::xdp_add_lm2001(dB_in, dA_in, dOut_candidate);
    }
    
    /**
     * @brief Subround 2: 模加最优输出差分查找
     */
    static inline std::pair<std::uint32_t, int>
    subround2_modular_add_optimal(std::uint32_t dB_in, std::uint32_t dA_in) noexcept {
        return arx_operators::find_optimal_gamma_with_weight(dB_in, dA_in, 32);
    }
    
    /**
     * @brief Subround 2: 减常量 B - c1
     */
    static inline int
    subround2_subtract_const_weight(std::uint32_t dB_in, std::uint32_t c1,
                                     std::uint32_t dOut_candidate) noexcept {
        std::uint32_t neg_c1 = (~c1 + 1);
        return arx_operators::diff_addconst_bvweight(dB_in, neg_c1, dOut_candidate);
    }
    
    /**
     * @brief 完整单轮差分传播（简化版，用于快速估计）
     * 
     * 输入：(ΔA_in, ΔB_in)
     * 输出：(ΔA_out, ΔB_out) 和总权重
     * 
     * 注意：这是启发式版本，不保证找到最优路径
     * 实际搜索应该使用Matsui框架逐步枚举
     */
    struct SingleRoundResult {
        std::uint32_t dA_out;
        std::uint32_t dB_out;
        int total_weight;
        bool feasible;
    };
    
    static SingleRoundResult propagate_single_round_heuristic(
        std::uint32_t dA_in, 
        std::uint32_t dB_in,
        std::uint32_t c0,
        std::uint32_t c1
    ) noexcept;
};

} // namespace neoalz
