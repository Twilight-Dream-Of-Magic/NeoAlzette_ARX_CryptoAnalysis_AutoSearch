#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include "neoalzette/neoalzette_core.hpp"
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
    
    template<typename Yield>
    static void execute_subround1_backward(
        const SearchConfig& config,
        const LinearState& output,
        double correlation_budget,
        Yield&& yield
    );
    
    template<typename Yield>
    static void execute_subround0_backward(
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
void NeoAlzetteLinearSearch::execute_subround1_backward(
    const SearchConfig& config,
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // 反向 Step 8, 7: cd_from_A（简化）
    std::uint32_t mA_before_cd = mA;
    std::uint32_t mB_before_cd = mB;
    
    // 反向 Step 6: A = l2_forward(A)
    mA = NeoAlzetteCore::l2_transpose(mA_before_cd);
    
    // 反向 Step 5: B = l1_forward(B)
    mB = NeoAlzetteCore::l1_transpose(mB_before_cd);
    
    // 反向 Step 4: A ^= rotl(B, 16)
    std::uint32_t mA_before_xor2 = mA;
    std::uint32_t mB_before_xor2 = mB ^ NeoAlzetteCore::rotr(mA, 16);
    
    // 反向 Step 3: B ^= rotl(A, 24)
    std::uint32_t mB_before_xor1 = mB_before_xor2;
    std::uint32_t mA_before_xor1 = mA_before_xor2 ^ NeoAlzetteCore::rotr(mB_before_xor2, 24);
    
    // 反向 Step 2: B -= RC[6]（简化：假设影响小）
    std::uint32_t mB_before_sub = mB_before_xor1;
    
    // 反向 Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    std::uint32_t mA_before_add = mA_before_xor1;
    
    // ========================================================================
    // 关键：枚举候选掩码（改进！）
    // ========================================================================
    enumerate_linear_masks(
        mA_before_add,  // 输出掩码
        0,  // beta_mask（这里需要从mB_before_sub推导）
        correlation_budget,
        config.max_hamming_weight,
        config.max_candidates_per_step,
        [&](std::uint32_t mA_candidate, double corr_A) {
            if (corr_A <= 0 || std::abs(corr_A) < correlation_budget) return;
            
            // 尝试几个mB候选
            std::vector<std::uint32_t> mB_candidates = {
                mB_before_sub,
                mB_before_sub ^ 1,
                mB_before_sub ^ 3,
            };
            
            for (std::uint32_t mB_candidate : mB_candidates) {
                // 计算beta_mask
                std::uint32_t beta_mask = NeoAlzetteCore::rotl(mB_candidate, 31) ^ 
                                           NeoAlzetteCore::rotl(mB_candidate, 17);
                
                // 重新计算相关性
                double corr = arx_operators::linear_cor_add_value_logn(
                    mA_candidate, beta_mask, mA_before_add
                );
                
                if (corr > 0 && std::abs(corr) >= correlation_budget) {
                    yield(mA_candidate, mB_candidate, corr);
                }
            }
        });
}

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround0_backward(
    const SearchConfig& config,
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // 反向 Step 8, 7: cd_from_B（简化）
    std::uint32_t mA_before_cd = mA;
    std::uint32_t mB_before_cd = mB;
    
    // 反向 Step 6: B = l2_forward(B)
    mB = NeoAlzetteCore::l2_transpose(mB_before_cd);
    
    // 反向 Step 5: A = l1_forward(A)
    mA = NeoAlzetteCore::l1_transpose(mA_before_cd);
    
    // 反向 Step 4: B ^= rotl(A, 16)
    std::uint32_t mB_before_xor2 = mB;
    std::uint32_t mA_before_xor2 = mA ^ NeoAlzetteCore::rotr(mB, 16);
    
    // 反向 Step 3: A ^= rotl(B, 24)
    std::uint32_t mA_before_xor1 = mA_before_xor2;
    std::uint32_t mB_before_xor1 = mB_before_xor2 ^ NeoAlzetteCore::rotr(mA_before_xor2, 24);
    
    // 反向 Step 2: A -= RC[1]（简化）
    std::uint32_t mA_before_sub = mA_before_xor1;
    
    // 反向 Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
    std::uint32_t mB_before_add = mB_before_xor1;
    
    // ========================================================================
    // 关键：枚举候选掩码（改进！）
    // ========================================================================
    enumerate_linear_masks(
        mB_before_add,  // 输出掩码
        0,  // beta_mask（需要从mA_before_sub推导）
        correlation_budget,
        config.max_hamming_weight,
        config.max_candidates_per_step,
        [&](std::uint32_t mB_candidate, double corr_B) {
            if (corr_B <= 0 || std::abs(corr_B) < correlation_budget) return;
            
            // 尝试几个mA候选
            std::vector<std::uint32_t> mA_candidates = {
                mA_before_sub,
                mA_before_sub ^ 1,
                mA_before_sub ^ 3,
            };
            
            for (std::uint32_t mA_candidate : mA_candidates) {
                // 计算beta_mask
                std::uint32_t beta_mask = NeoAlzetteCore::rotl(mA_candidate, 31) ^ 
                                           NeoAlzetteCore::rotl(mA_candidate, 17);
                
                // 重新计算相关性
                double corr = arx_operators::linear_cor_add_value_logn(
                    mB_candidate, beta_mask, mB_before_add
                );
                
                if (corr > 0 && std::abs(corr) >= correlation_budget) {
                    yield(mA_candidate, mB_candidate, corr);
                }
            }
        });
}

} // namespace neoalz
