#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_differential_step.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"

namespace neoalz {

/**
 * @file neoalzette_differential_search.hpp
 * @brief NeoAlzette差分搜索 - 修复了pDDT使用错误
 * 
 * 关键修复：
 * - 模加有两个输入差分(α, β)
 * - 直接枚举候选γ，调用xdp_add(α, β, γ)计算权重
 * - 不使用pDDT（pDDT适用于单输入，不适用于双输入模加）
 */
class NeoAlzetteDifferentialSearch {
public:
    struct DiffState {
        std::uint32_t dA, dB;
        int weight;
        
        DiffState(std::uint32_t da, std::uint32_t db, int w = 0)
            : dA(da), dB(db), weight(w) {}
    };
    
    struct SearchConfig {
        int num_rounds = 4;
        int weight_cap = 30;
        std::uint32_t initial_dA = 1;
        std::uint32_t initial_dB = 0;
        bool use_optimal_gamma = true;  // 使用Algorithm 4优化
        std::uint32_t c0 = NeoAlzetteCore::ROUND_CONSTANTS[0];
        std::uint32_t c1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
    };
    
    struct SearchResult {
        int best_weight;
        std::vector<DiffState> best_trail;
        std::uint64_t nodes_visited;
        bool found;
    };
    
    static SearchResult search(const SearchConfig& config);
    
private:
    static void search_recursive(
        const SearchConfig& config,
        const DiffState& current,
        int round,
        std::vector<DiffState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 🆕 使用Algorithm 4查找最优γ（推荐！）
     * 
     * Lipmaa & Moriai (2001) Algorithm 4
     * 复杂度：Θ(log n) - 对数时间
     * 
     * 优势：
     * - 直接找到最优γ，无需枚举
     * - 保证找到DP+(α, β → γ)的最大值
     * - 比启发式枚举更快更准确
     */
    template<typename Yield>
    static void find_optimal_diff(
        std::uint32_t input_diff_alpha,
        std::uint32_t input_diff_beta,
        int weight_budget,
        Yield&& yield  // yield(output_diff, weight)
    ) {
        auto [gamma, weight] = arx_operators::find_optimal_gamma_with_weight(
            input_diff_alpha, input_diff_beta
        );
        
        if (weight >= 0 && weight < weight_budget) {
            yield(gamma, weight);
        }
    }
    
    /**
     * @brief 枚举候选差分（启发式版本）
     * 
     * 给定：两个输入差分(alpha, beta)
     * 枚举：候选输出差分gamma
     * 计算：对每个gamma，调用xdp_add(alpha, beta, gamma)
     * 
     * 注意：推荐使用find_optimal_diff()代替此函数！
     */
    template<typename Yield>
    static void enumerate_diff_candidates(
        std::uint32_t input_diff_alpha,
        std::uint32_t input_diff_beta,
        int weight_budget,
        Yield&& yield  // yield(output_diff, weight)
    ) {
        std::vector<std::uint32_t> candidates;
        
        // 基础候选
        candidates.push_back(input_diff_alpha);
        candidates.push_back(input_diff_alpha ^ input_diff_beta);
        
        // 单比特枚举
        for (int bit = 0; bit < 32; ++bit) {
            candidates.push_back(input_diff_alpha ^ (1u << bit));
            candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ (1u << bit));
        }
        
        // 双比特枚举
        for (int bit1 = 0; bit1 < 32; ++bit1) {
            for (int bit2 = bit1 + 1; bit2 < bit1 + 8 && bit2 < 32; ++bit2) {
                std::uint32_t mask = (1u << bit1) | (1u << bit2);
                candidates.push_back(input_diff_alpha ^ mask);
                candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ mask);
            }
        }
        
        // 调用ARX算子计算权重
        for (std::uint32_t gamma : candidates) {
            int w = arx_operators::xdp_add_lm2001(
                input_diff_alpha, input_diff_beta, gamma
            );
            if (w >= 0 && w < weight_budget) {
                yield(gamma, w);
            }
        }
    }
    
    /**
     * @brief 执行Subround 1（使用新单步函数）
     * 
     * Subround 1步骤：
     * 1. 线性层: L1(A), L2(B)
     * 2. cd_from_B(B)
     * 3. 跨分支注入: A ^= (rotl(C0,24) ^ rotl(D0,16))
     * 4. 模加: A + B
     * 5. 减常量: A - c0
     */
    template<typename Yield>
    static void execute_subround1(
        const SearchConfig& config,
        const DiffState& input,
        int weight_budget,
        Yield&& yield
    );
    
    /**
     * @brief 执行Subround 2（使用新单步函数）
     * 
     * Subround 2步骤：
     * 6. 线性层: L1(B), L2(A)
     * 7. cd_from_A(A)
     * 8. 跨分支注入: B ^= (rotl(C1,24) ^ rotl(D1,16))
     * 9. 模加: B + A
     * 10. 减常量: B - c1
     */
    template<typename Yield>
    static void execute_subround2(
        const SearchConfig& config,
        const DiffState& input,
        int weight_budget,
        Yield&& yield
    );
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround1(
    const SearchConfig& config,
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    using Step = NeoAlzetteDifferentialStep;
    
    const std::uint32_t dA_in = input.dA;
    const std::uint32_t dB_in = input.dB;
    
    // Step 1-3: 线性层 + cd_from_B + 跨分支注入（weight = 0）
    auto [dA_l1, dB_l2] = Step::subround1_linear_layer(dA_in, dB_in);
    auto [dC0, dD0] = Step::subround1_cd_from_B(dB_l2);
    std::uint32_t dA_injected = Step::subround1_cross_injection(dA_l1, dC0, dD0);
    
    // Step 4: 模加 A + B（关键：枚举候选γ）
    if (config.use_optimal_gamma) {
        // 使用Algorithm 4快速找到最优γ
        auto [gamma_optimal, w_add] = Step::subround1_modular_add_optimal(dA_injected, dB_l2);
        
        if (w_add >= 0 && w_add < weight_budget) {
            // Step 5: 减常量 A - c0
            std::uint32_t dA_after_add = gamma_optimal;
            
            // 启发式：尝试几个候选输出差分
            std::vector<std::uint32_t> candidates = {
                dA_after_add,           // 差分不变
                dA_after_add ^ 1,       // 翻转LSB
                dA_after_add ^ 0x80000000  // 翻转MSB
            };
            
            for (std::uint32_t dA_after_sub : candidates) {
                int w_sub = Step::subround1_subtract_const_weight(dA_after_add, config.c0, dA_after_sub);
                
                if (w_sub >= 0) {
                    int total_weight = w_add + w_sub;
                    if (total_weight < weight_budget) {
                        yield(dA_after_sub, dB_l2, total_weight);
                    }
                }
            }
        }
    } else {
        // 启发式枚举：尝试多个候选γ
        enumerate_diff_candidates(dA_injected, dB_l2, weight_budget,
            [&](std::uint32_t gamma_candidate, int w_add) {
                if (w_add >= weight_budget) return;
                
                // Step 5: 减常量
                std::vector<std::uint32_t> candidates = {
                    gamma_candidate, gamma_candidate ^ 1
                };
                
                for (std::uint32_t dA_after_sub : candidates) {
                    int w_sub = Step::subround1_subtract_const_weight(gamma_candidate, config.c0, dA_after_sub);
                    
                    if (w_sub >= 0) {
                        int total_weight = w_add + w_sub;
                        if (total_weight < weight_budget) {
                            yield(dA_after_sub, dB_l2, total_weight);
                        }
                    }
                }
            });
    }
}

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround2(
    const SearchConfig& config,
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    using Step = NeoAlzetteDifferentialStep;
    
    const std::uint32_t dA_in = input.dA;
    const std::uint32_t dB_in = input.dB;
    
    // Step 6-8: 线性层 + cd_from_A + 跨分支注入（weight = 0）
    auto [dB_l1, dA_l2] = Step::subround2_linear_layer(dB_in, dA_in);
    auto [dC1, dD1] = Step::subround2_cd_from_A(dA_l2);
    std::uint32_t dB_injected = Step::subround2_cross_injection(dB_l1, dC1, dD1);
    
    // Step 9: 模加 B + A（关键：枚举候选γ）
    if (config.use_optimal_gamma) {
        // 使用Algorithm 4快速找到最优γ
        auto [gamma_optimal, w_add] = Step::subround2_modular_add_optimal(dB_injected, dA_l2);
        
        if (w_add >= 0 && w_add < weight_budget) {
            // Step 10: 减常量 B - c1
            std::uint32_t dB_after_add = gamma_optimal;
            
            // 启发式：尝试几个候选输出差分
            std::vector<std::uint32_t> candidates = {
                dB_after_add,
                dB_after_add ^ 1,
                dB_after_add ^ 0x80000000
            };
            
            for (std::uint32_t dB_after_sub : candidates) {
                int w_sub = Step::subround2_subtract_const_weight(dB_after_add, config.c1, dB_after_sub);
                
                if (w_sub >= 0) {
                    int total_weight = w_add + w_sub;
                    if (total_weight < weight_budget) {
                        yield(dA_l2, dB_after_sub, total_weight);
                    }
                }
            }
        }
    } else {
        // 启发式枚举
        enumerate_diff_candidates(dB_injected, dA_l2, weight_budget,
            [&](std::uint32_t gamma_candidate, int w_add) {
                if (w_add >= weight_budget) return;
                
                // Step 10: 减常量
                std::vector<std::uint32_t> candidates = {
                    gamma_candidate, gamma_candidate ^ 1
                };
                
                for (std::uint32_t dB_after_sub : candidates) {
                    int w_sub = Step::subround2_subtract_const_weight(gamma_candidate, config.c1, dB_after_sub);
                    
                    if (w_sub >= 0) {
                        int total_weight = w_add + w_sub;
                        if (total_weight < weight_budget) {
                            yield(dA_l2, dB_after_sub, total_weight);
                        }
                    }
                }
            });
    }
}

} // namespace neoalz
