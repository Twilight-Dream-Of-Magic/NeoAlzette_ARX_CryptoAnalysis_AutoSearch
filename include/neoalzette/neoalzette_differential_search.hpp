#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include "neoalzette/neoalzette_core.hpp"
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
    
    template<typename Yield>
    static void execute_subround0(
        const SearchConfig& config,
        const DiffState& input,
        int weight_budget,
        Yield&& yield
    );
    
    template<typename Yield>
    static void execute_subround1(
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
void NeoAlzetteDifferentialSearch::execute_subround0(
    const SearchConfig& config,
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
    std::uint32_t beta = NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17);
    
    enumerate_diff_candidates(dB, beta, weight_budget,
        [&](std::uint32_t dB_after, int w1) {
            if (w1 >= weight_budget) return;
            
            // Step 2: A -= RC[1]
            const std::uint32_t RC1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
            
            std::vector<std::uint32_t> dA_candidates = {
                dA, dA ^ 1, dA ^ 3,
            };
            
            for (std::uint32_t dA_after : dA_candidates) {
                int w2 = arx_operators::diff_addconst_bvweight(dA, RC1, dA_after);
                if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
                
                // Step 3-7: 线性操作
                std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_after, 24);
                std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_temp, 16);
                dA_temp = NeoAlzetteCore::l1_forward(dA_temp);
                dB_temp = NeoAlzetteCore::l2_forward(dB_temp);
                auto [dC0, dD0] = NeoAlzetteCore::cd_from_B_delta(dB_temp);
                dA_temp ^= (NeoAlzetteCore::rotl(dC0, 24) ^ NeoAlzetteCore::rotl(dD0, 16));
                
                yield(dA_temp, dB_temp, w1 + w2);
            }
        });
}

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround1(
    const SearchConfig& config,
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    std::uint32_t beta = NeoAlzetteCore::rotl(dB, 31) ^ NeoAlzetteCore::rotl(dB, 17);
    
    enumerate_diff_candidates(dA, beta, weight_budget,
        [&](std::uint32_t dA_after, int w1) {
            if (w1 >= weight_budget) return;
            
            // Step 2: B -= RC[6]
            const std::uint32_t RC6 = NeoAlzetteCore::ROUND_CONSTANTS[6];
            std::vector<std::uint32_t> dB_candidates = {dB, dB ^ 1};
            
            for (std::uint32_t dB_after : dB_candidates) {
                int w2 = arx_operators::diff_addconst_bvweight(dB, RC6, dB_after);
                if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
                
                // Step 3-7: 线性操作
                std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_after, 24);
                std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_temp, 16);
                dB_temp = NeoAlzetteCore::l1_forward(dB_temp);
                dA_temp = NeoAlzetteCore::l2_forward(dA_temp);
                auto [dC1, dD1] = NeoAlzetteCore::cd_from_A_delta(dA_temp);
                dB_temp ^= (NeoAlzetteCore::rotl(dC1, 24) ^ NeoAlzetteCore::rotl(dD1, 16));
                
                yield(dA_temp, dB_temp, w1 + w2);
            }
        });
}

} // namespace neoalz
