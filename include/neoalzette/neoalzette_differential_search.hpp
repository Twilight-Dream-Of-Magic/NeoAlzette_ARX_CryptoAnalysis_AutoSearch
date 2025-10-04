#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include "neoalzette/neoalzette_core.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "utility_tools.hpp"  // SimplePDDT

namespace neoalz {

/**
 * @file neoalzette_differential_search_v2.hpp
 * @brief NeoAlzette差分搜索 - 正确使用pDDT查询
 * 
 * 关键理解（用户指正）：
 * - ARX差分分析工作在**纯差分域**
 * - 不需要实际数据(x, y)，只需要差分(Δx, Δy)
 * - pDDT表存储的是：给定输入差分 → 所有高概率输出差分
 * - 应用时：查询pDDT表获取候选，而不是固定枚举！
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
        
        // pDDT表指针（可选，如果提供则使用）
        const neoalz::UtilityTools::SimplePDDT* pddt_table = nullptr;
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
     * @brief 枚举候选差分（核心！）
     * 
     * 给定：(input_diff, weight_budget)
     * 枚举：所有权重≤weight_budget的output_diff
     * 
     * 方法1：如果有pDDT表，直接查询
     * 方法2：否则，启发式枚举
     */
    template<typename Yield>
    static void enumerate_diff_candidates(
        std::uint32_t input_diff_alpha,
        std::uint32_t input_diff_beta,
        int weight_budget,
        const neoalz::UtilityTools::SimplePDDT* pddt,
        Yield&& yield  // yield(output_diff, weight)
    ) {
        if (pddt != nullptr && !pddt->empty()) {
            // ================================================================
            // 方法1：使用pDDT表查询（最优）
            // ================================================================
            // 
            // 这才是论文中"应用"pDDT的方式！
            // pDDT表预先计算了所有高概率差分
            // 
            // 查询：给定输入差分beta，返回所有权重≤threshold的输出差分
            // 
            auto entries = pddt->query(input_diff_beta, weight_budget);
            
            for (const auto& entry : entries) {
                yield(entry.output_diff, entry.weight);
            }
            
            // 如果pDDT表查询到结果，直接返回
            if (!entries.empty()) return;
        }
        
        // ====================================================================
        // 方法2：启发式枚举（fallback，如果没有pDDT表）
        // ====================================================================
        // 
        // 注意：这不保证最优！只是fallback方案
        // 
        std::vector<std::uint32_t> candidates = {
            input_diff_alpha,  // 权重0：差分不传播
            input_diff_alpha ^ input_diff_beta,  // 直接传播
        };
        
        // 枚举低汉明重量的变化
        for (int bit = 0; bit < 32 && bit < weight_budget; ++bit) {
            candidates.push_back(input_diff_alpha ^ (1u << bit));
            candidates.push_back((input_diff_alpha ^ input_diff_beta) ^ (1u << bit));
        }
        
        // 对每个候选，用xdp_add计算实际权重
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
    
    // ========================================================================
    // 关键：枚举候选（使用pDDT查询）
    // ========================================================================
    enumerate_diff_candidates(dB, beta, weight_budget, config.pddt_table,
        [&](std::uint32_t dB_after, int w1) {
            if (w1 >= weight_budget) return;
            
            // Step 2: A -= RC[1]
            const std::uint32_t RC1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
            
            // 简化：假设常量模减影响小，只尝试几个候选
            std::vector<std::uint32_t> dA_candidates = {
                dA, dA ^ 1, dA ^ 3,
            };
            
            for (std::uint32_t dA_after : dA_candidates) {
                int w2 = arx_operators::diff_addconst_bvweight(dA, RC1, dA_after);
                if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
                
                // Step 3-7: 线性操作（确定性）
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
    
    enumerate_diff_candidates(dA, beta, weight_budget, config.pddt_table,
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
