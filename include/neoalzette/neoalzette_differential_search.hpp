#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include "neoalzette/neoalzette_core.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"

namespace neoalz {

/**
 * @brief NeoAlzette差分搜索 - 把算法操作直接嵌入搜索框架
 * 
 * 关键：不是先枚举后搜索，而是在搜索过程中逐步执行NeoAlzette的每个操作！
 * 
 * 搜索流程：
 * 1. 从初始差分(dA, dB)开始
 * 2. 执行 NeoAlzette Step 1: B += (rotl(A,31)^rotl(A,17)^RC[0])
 *    → 调用 xdp_add_lm2001 或查询pDDT表
 *    → 对每个可行的dB_after，检查权重并继续
 * 3. 执行 NeoAlzette Step 2: A -= RC[1]
 *    → 调用 diff_addconst_bvweight
 * 4. 执行 NeoAlzette Step 3-8: 线性操作（确定性传播）
 * 5. 重复Subround 1
 * 6. 递归搜索下一轮
 */
class NeoAlzetteDifferentialSearch {
public:
    /**
     * @brief 差分状态
     */
    struct DiffState {
        std::uint32_t dA, dB;
        int weight;  // 累积权重
        
        DiffState(std::uint32_t da, std::uint32_t db, int w = 0)
            : dA(da), dB(db), weight(w) {}
    };
    
    /**
     * @brief 搜索配置
     */
    struct SearchConfig {
        int num_rounds = 4;
        int weight_cap = 30;
        std::uint32_t initial_dA = 1;
        std::uint32_t initial_dB = 0;
        bool use_pddt = true;  // 是否使用pDDT表加速
        int pddt_threshold = 10;  // pDDT表的权重阈值
    };
    
    /**
     * @brief 搜索结果
     */
    struct SearchResult {
        int best_weight;
        std::vector<DiffState> best_trail;  // 最优轨道
        std::uint64_t nodes_visited;
        bool found;
    };
    
    /**
     * @brief 执行完整搜索
     */
    static SearchResult search(const SearchConfig& config);
    
private:
    /**
     * @brief 递归搜索实现
     * 
     * 在这个函数里直接执行NeoAlzette的每一步操作！
     */
    static void search_recursive(
        const SearchConfig& config,
        const DiffState& current,
        int round,
        std::vector<DiffState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 执行NeoAlzette Subround 0的差分传播
     * 
     * 在搜索过程中，逐步执行每个操作：
     * 1. B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
     * 2. A -= RC[1]
     * 3. A ^= rotl(B, 24)
     * 4. B ^= rotl(A, 16)
     * 5. A = l1_forward(A)
     * 6. B = l2_forward(B)
     * 7. cd_from_B → A ^= ...
     */
    template<typename Yield>
    static void execute_subround0(
        const DiffState& input,
        int weight_budget,
        Yield&& yield  // 回调：(dA_out, dB_out, weight_consumed)
    );
    
    /**
     * @brief 执行NeoAlzette Subround 1的差分传播
     */
    template<typename Yield>
    static void execute_subround1(
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
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // === Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0]) ===
    // 差分：β = rotl(dA,31) ^ rotl(dA,17)  (RC[0]在差分域消失)
    std::uint32_t beta = NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17);
    
    // 关键：在这里枚举所有可能的dB_after
    // 使用pDDT表或直接调用xdp_add_lm2001
    
    // 简化版本：尝试几个高概率的候选
    // TODO: 后续集成pDDT表进行高效查询
    std::vector<std::uint32_t> dB_candidates = {
        dB,           // 权重0（最可能）
        dB ^ beta,    // 直接传播
    };
    
    // 枚举低权重的变化
    for (int bit = 0; bit < 32; ++bit) {
        dB_candidates.push_back(dB ^ (1u << bit));
        dB_candidates.push_back((dB ^ beta) ^ (1u << bit));
    }
    
    for (std::uint32_t dB_after : dB_candidates) {
        // 调用ARX算子计算权重
        int w1 = arx_operators::xdp_add_lm2001(dB, beta, dB_after);
        if (w1 < 0 || w1 >= weight_budget) continue;
        
        // === Step 2: A -= RC[1] ===
        const std::uint32_t RC1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
        
        // 枚举几个候选的dA_after
        std::vector<std::uint32_t> dA_candidates = {
            dA,  // 权重0（常量不影响差分）
            dA ^ 1,
            dA ^ 3,
        };
        
        for (std::uint32_t dA_after : dA_candidates) {
            // 调用ARX算子计算权重
            int w2 = arx_operators::diff_addconst_bvweight(dA, RC1, dA_after);
            if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
            
            // === Step 3: A ^= rotl(B, 24) ===（线性，确定性）
            std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_after, 24);
            
            // === Step 4: B ^= rotl(A, 16) ===（线性，确定性）
            std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_temp, 16);
            
            // === Step 5: A = l1_forward(A) ===（线性，确定性）
            dA_temp = NeoAlzetteCore::l1_forward(dA_temp);
            
            // === Step 6: B = l2_forward(B) ===（线性，确定性）
            dB_temp = NeoAlzetteCore::l2_forward(dB_temp);
            
            // === Step 7: cd_from_B → A ^= ... ===（线性，确定性）
            auto [dC0, dD0] = NeoAlzetteCore::cd_from_B_delta(dB_temp);
            dA_temp ^= (NeoAlzetteCore::rotl(dC0, 24) ^ NeoAlzetteCore::rotl(dD0, 16));
            // RC[4]在差分域消失
            
            // 输出Subround 0的结果
            int weight_consumed = w1 + w2;
            yield(dA_temp, dB_temp, weight_consumed);
        }
    }
}

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround1(
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // === Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5]) ===
    std::uint32_t beta = NeoAlzetteCore::rotl(dB, 31) ^ NeoAlzetteCore::rotl(dB, 17);
    
    std::vector<std::uint32_t> dA_candidates = {
        dA, dA ^ beta,
    };
    for (int bit = 0; bit < 32; ++bit) {
        dA_candidates.push_back(dA ^ (1u << bit));
    }
    
    for (std::uint32_t dA_after : dA_candidates) {
        int w1 = arx_operators::xdp_add_lm2001(dA, beta, dA_after);
        if (w1 < 0 || w1 >= weight_budget) continue;
        
        // === Step 2: B -= RC[6] ===
        const std::uint32_t RC6 = NeoAlzetteCore::ROUND_CONSTANTS[6];
        std::vector<std::uint32_t> dB_candidates = {dB, dB ^ 1};
        
        for (std::uint32_t dB_after : dB_candidates) {
            int w2 = arx_operators::diff_addconst_bvweight(dB, RC6, dB_after);
            if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
            
            // === Step 3-7: 线性操作（确定性）===
            std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_after, 24);
            std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_temp, 16);
            dB_temp = NeoAlzetteCore::l1_forward(dB_temp);
            dA_temp = NeoAlzetteCore::l2_forward(dA_temp);
            
            auto [dC1, dD1] = NeoAlzetteCore::cd_from_A_delta(dA_temp);
            dB_temp ^= (NeoAlzetteCore::rotl(dC1, 24) ^ NeoAlzetteCore::rotl(dD1, 16));
            
            // === 白化: A ^= RC[10], B ^= RC[11] ===（差分不变）
            
            int weight_consumed = w1 + w2;
            yield(dA_temp, dB_temp, weight_consumed);
        }
    }
}

} // namespace neoalz
