#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_linear_step.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

namespace neoalz {

/**
 * @file neoalzette_linear_search_v2.hpp
 * @brief NeoAlzette线性搜索 - 改进的候选枚举
 * 
 * 关键改进：
 * - 从3个候选→100+个候选
 * - 使用启发式枚举策略
 * - 后续可集成cLAT查询
 */
class NeoAlzetteLinearSearch {
public:
    struct LinearState {
        std::uint32_t mA, mB;
        double correlation;
        
        LinearState(std::uint32_t ma, std::uint32_t mb, double corr = 1.0)
            : mA(ma), mB(mb), correlation(corr) {}
    };
    
    struct SearchConfig {
        int num_rounds = 4;
        double correlation_threshold = 0.001;
        std::uint32_t final_mA = 1;
        std::uint32_t final_mB = 0;
        
        // 候选枚举策略
        int max_hamming_weight = 4;  // 枚举汉明重量≤此值的掩码
        int max_candidates_per_step = 200;  // 每步最多尝试的候选数
        
        // 常量
        std::uint32_t c0 = NeoAlzetteCore::ROUND_CONSTANTS[0];
        std::uint32_t c1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
    };
    
    struct SearchResult {
        double best_correlation;
        std::vector<LinearState> best_trail;
        std::uint64_t nodes_visited;
        bool found;
    };
    
    static SearchResult search(const SearchConfig& config);
    
private:
    static void search_recursive(
        const SearchConfig& config,
        const LinearState& current,
        int round,
        std::vector<LinearState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 枚举线性掩码候选（核心改进！）
     * 
     * 给定：
     * - output_mask: 输出掩码
     * - input_diff: 输入差分（如rotl(mB,31) ^ rotl(mB,17)）
     * - correlation_budget: 相关性阈值
     * 
     * 枚举：所有可能的input_mask，使得：
     * - linear_cor(input_mask, input_diff, output_mask) > threshold
     * 
     * 策略：
     * 1. 尝试低汉明重量的掩码
     * 2. 尝试output_mask附近的掩码
     * 3. 对每个候选，调用linear_cor_add_value_logn计算实际相关性
     */
    template<typename Yield>
    static void enumerate_linear_masks(
        std::uint32_t output_mask,
        std::uint32_t beta_mask,
        double correlation_budget,
        int max_hamming_weight,
        int max_candidates,
        Yield&& yield  // yield(input_mask, correlation)
    ) {
        std::vector<std::pair<std::uint32_t, double>> candidates;
        
        // ====================================================================
        // 策略1：尝试output_mask本身及其附近
        // ====================================================================
        std::vector<std::uint32_t> base_candidates = {
            output_mask,
            output_mask ^ beta_mask,
            0,  // 全0掩码
        };
        
        for (std::uint32_t candidate : base_candidates) {
            double corr = arx_operators::linear_cor_add_value_logn(
                candidate, beta_mask, output_mask
            );
            if (std::abs(corr) >= correlation_budget) {
                candidates.push_back({candidate, corr});
            }
        }
        
        // ====================================================================
        // 策略2：枚举低汉明重量的掩码
        // ====================================================================
        // 
        // 低汉明重量的掩码通常有较高的相关性
        // 枚举所有hw≤max_hamming_weight的32位掩码
        // 
        
        // hw=1: 单比特掩码
        for (int bit = 0; bit < 32; ++bit) {
            std::uint32_t candidate = 1u << bit;
            double corr = arx_operators::linear_cor_add_value_logn(
                candidate, beta_mask, output_mask
            );
            if (std::abs(corr) >= correlation_budget) {
                candidates.push_back({candidate, corr});
            }
        }
        
        // hw=2: 双比特掩码
        if (max_hamming_weight >= 2) {
            for (int bit1 = 0; bit1 < 32; ++bit1) {
                for (int bit2 = bit1 + 1; bit2 < 32 && bit2 < bit1 + 16; ++bit2) {
                    std::uint32_t candidate = (1u << bit1) | (1u << bit2);
                    double corr = arx_operators::linear_cor_add_value_logn(
                        candidate, beta_mask, output_mask
                    );
                    if (std::abs(corr) >= correlation_budget) {
                        candidates.push_back({candidate, corr});
                    }
                }
            }
        }
        
        // hw=3: 三比特掩码（限制搜索范围）
        if (max_hamming_weight >= 3) {
            for (int bit1 = 0; bit1 < 16; ++bit1) {
                for (int bit2 = bit1 + 1; bit2 < 20; ++bit2) {
                    for (int bit3 = bit2 + 1; bit3 < 24; ++bit3) {
                        std::uint32_t candidate = (1u << bit1) | (1u << bit2) | (1u << bit3);
                        double corr = arx_operators::linear_cor_add_value_logn(
                            candidate, beta_mask, output_mask
                        );
                        if (std::abs(corr) >= correlation_budget) {
                            candidates.push_back({candidate, corr});
                        }
                    }
                }
            }
        }
        
        // ====================================================================
        // 策略3：output_mask附近的扰动
        // ====================================================================
        for (int bit = 0; bit < 32; ++bit) {
            std::uint32_t candidate = output_mask ^ (1u << bit);
            double corr = arx_operators::linear_cor_add_value_logn(
                candidate, beta_mask, output_mask
            );
            if (std::abs(corr) >= correlation_budget) {
                candidates.push_back({candidate, corr});
            }
        }
        
        // ====================================================================
        // 输出候选（按相关性排序，取前max_candidates个）
        // ====================================================================
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
                return std::abs(a.second) > std::abs(b.second);
            });
        
        int count = 0;
        for (const auto& [mask, corr] : candidates) {
            if (count >= max_candidates) break;
            yield(mask, corr);
            count++;
        }
    }
    
    /**
     * @brief 执行Subround 2逆向（使用新单步函数）
     * 
     * 逆向顺序：Step 10 → 6
     * 10. B - c1 (减常量)
     * 9. B + A (模加)
     * 8. B ^= (rotl(C1,24)^rotl(D1,16)) (跨分支注入)
     * 7. cd_from_A(A) (XOR操作)
     * 6. L1(B), L2(A) (线性层转置)
     */
    template<typename Yield>
    static void execute_subround2_backward(
        const SearchConfig& config,
        const LinearState& output,
        double correlation_budget,
        Yield&& yield
    );
    
    /**
     * @brief 执行Subround 1逆向（使用新单步函数）
     * 
     * 逆向顺序：Step 5 → 1
     * 5. B ^= A_L1 (跨分支注入)
     * 4. L1(A) (线性层转置)
     * 3. A - c0 (减常量)
     * 2. A + tmp1 (模加)
     * 1. cd_from_B(B) (XOR操作)
     */
    template<typename Yield>
    static void execute_subround1_backward(
        const SearchConfig& config,
        const LinearState& output,
        double correlation_budget,
        Yield&& yield
    );
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround2_backward(
    const SearchConfig& config,
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    using Step = NeoAlzetteLinearStep;
    
    std::uint32_t mask_A_out = output.mA;
    std::uint32_t mask_B_out = output.mB;
    
    // Step 10逆向: B = B ⊕ A (XOR拆分，简化：假设mask_B不变，mask_A=0)
    std::uint32_t mask_B_before_xor = mask_B_out;
    std::uint32_t mask_A_for_xor = 0;
    
    // Step 9逆向: L1(B), L2(A) 转置
    auto [mask_B_s2, mask_A_s2] = Step::subround2_step9_linear_transpose(
        mask_B_before_xor, mask_A_for_xor
    );
    
    // Step 8逆向: B - c1 (减常量)
    // 启发式：尝试几个候选输入掩码
    std::vector<std::uint32_t> mask_B_before_sub_candidates = {
        mask_B_s2, mask_B_s2 ^ 1, mask_B_s2 ^ 3
    };
    
    for (std::uint32_t mask_B_before_sub : mask_B_before_sub_candidates) {
        auto corr_sub = Step::subround2_step8_subtract_const(
            mask_B_s2, mask_B_before_sub, config.c1
        );
        
        if (!corr_sub.feasible || corr_sub.correlation < correlation_budget) continue;
        
        // Step 7逆向: B + A (模加) - 枚举候选
        std::vector<std::uint32_t> mask_A_candidates = {
            mask_A_s2, mask_A_s2 ^ 1, mask_A_s2 ^ 3
        };
        
        for (std::uint32_t mask_A_for_add : mask_A_candidates) {
            std::vector<std::uint32_t> mask_B_before_add_candidates = {
                mask_B_before_sub, mask_B_before_sub ^ 1
            };
            
            for (std::uint32_t mask_B_before_add : mask_B_before_add_candidates) {
                auto corr_add = Step::subround2_step7_modular_add(
                    mask_B_before_add, mask_A_for_add, mask_B_before_sub
                );
                
                if (!corr_add.feasible) continue;
                
                double total_corr = corr_sub.correlation * corr_add.correlation;
                if (total_corr < correlation_budget) continue;
                
                // Steps 6-8逆向: cd_from_A + 跨分支注入 + 线性层转置
                // 简化：假设掩码组合
                std::uint32_t mask_C1 = 0;
                std::uint32_t mask_D1 = 0;
                auto [mask_A_from_c, mask_A_from_d] = Step::subround2_step6_cd_from_A_transpose(
                    mask_C1, mask_D1
                );
                std::uint32_t mask_A_final = mask_A_from_c ^ mask_A_from_d;
                
                yield(mask_A_final, mask_B_before_add, total_corr);
            }
        }
    }
}

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround1_backward(
    const SearchConfig& config,
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    using Step = NeoAlzetteLinearStep;
    
    std::uint32_t mask_A_out = output.mA;
    std::uint32_t mask_B_out = output.mB;
    
    // Step 5逆向: B = B ⊕ A_L1 (XOR拆分，简化)
    std::uint32_t mask_B_before_xor = mask_B_out;
    std::uint32_t mask_A_L1 = 0;
    
    // Step 4逆向: L1(A) 转置
    std::uint32_t mask_A_before_L1 = Step::subround1_step4_linear_transpose(mask_A_L1);
    
    // Step 3逆向: A - c0 (减常量)
    std::vector<std::uint32_t> mask_A_before_sub_candidates = {
        mask_A_before_L1, mask_A_before_L1 ^ 1
    };
    
    for (std::uint32_t mask_A_before_sub : mask_A_before_sub_candidates) {
        auto corr_sub = Step::subround1_step3_subtract_const(
            mask_A_before_L1, mask_A_before_sub, config.c0
        );
        
        if (!corr_sub.feasible || corr_sub.correlation < correlation_budget) continue;
        
        // Step 2逆向: A + tmp1 (模加) - 枚举候选
        std::vector<std::uint32_t> mask_tmp1_candidates = {0, 1, 3};
        
        for (std::uint32_t mask_tmp1 : mask_tmp1_candidates) {
            std::vector<std::uint32_t> mask_A_before_add_candidates = {
                mask_A_before_sub, mask_A_before_sub ^ 1
            };
            
            for (std::uint32_t mask_A_before_add : mask_A_before_add_candidates) {
                auto corr_add = Step::subround1_step2_modular_add(
                    mask_A_before_add, mask_tmp1, mask_A_before_sub
                );
                
                if (!corr_add.feasible) continue;
                
                double total_corr = corr_sub.correlation * corr_add.correlation;
                if (total_corr < correlation_budget) continue;
                
                // Step 1逆向: cd_from_B + 线性层（简化）
                std::uint32_t mask_C0 = 0;
                std::uint32_t mask_D0 = 0;
                auto [mask_B_from_c, mask_B_from_d] = Step::subround1_step1_cd_from_B_transpose(
                    mask_C0, mask_D0
                );
                std::uint32_t mask_B_final = mask_B_from_c ^ mask_B_from_d;
                
                yield(mask_A_before_add, mask_B_final, total_corr);
            }
        }
    }
}

} // namespace neoalz
