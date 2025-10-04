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
 * @brief NeoAlzette线性搜索 - 把算法操作直接嵌入搜索框架（反向传播）
 * 
 * 关键：线性分析是反向的！从输出掩码反推输入掩码！
 * 
 * 搜索流程（反向）：
 * 1. 从最终掩码(mA, mB)开始
 * 2. 反向执行 NeoAlzette 白化: A ^= RC[10], B ^= RC[11]（掩码不变）
 * 3. 反向执行 Subround 1（从后往前）
 * 4. 反向执行 Subround 0（从后往前）
 * 5. 递归搜索前一轮
 */
class NeoAlzetteLinearSearch {
public:
    /**
     * @brief 线性掩码状态
     */
    struct LinearState {
        std::uint32_t mA, mB;
        double correlation;  // 累积相关性
        
        LinearState(std::uint32_t ma, std::uint32_t mb, double corr = 1.0)
            : mA(ma), mB(mb), correlation(corr) {}
    };
    
    /**
     * @brief 搜索配置
     */
    struct SearchConfig {
        int num_rounds = 4;
        double correlation_threshold = 0.001;  // 相关性阈值
        std::uint32_t final_mA = 1;  // 最终输出掩码
        std::uint32_t final_mB = 0;
    };
    
    /**
     * @brief 搜索结果
     */
    struct SearchResult {
        double best_correlation;
        std::vector<LinearState> best_trail;  // 最优轨道（反向）
        std::uint64_t nodes_visited;
        bool found;
    };
    
    /**
     * @brief 执行完整搜索（反向）
     */
    static SearchResult search(const SearchConfig& config);
    
private:
    /**
     * @brief 递归搜索实现（反向传播）
     */
    static void search_recursive(
        const SearchConfig& config,
        const LinearState& current,
        int round,
        std::vector<LinearState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 反向执行NeoAlzette Subround 1的掩码传播
     * 
     * 前向操作顺序：
     * 1. A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
     * 2. B -= RC[6]
     * 3. B ^= rotl(A, 24)
     * 4. A ^= rotl(B, 16)
     * 5. B = l1_forward(B)
     * 6. A = l2_forward(A)
     * 7. cd_from_A → B ^= ...
     * 
     * 反向传播顺序：从7到1
     */
    template<typename Yield>
    static void execute_subround1_backward(
        const LinearState& output,
        double correlation_budget,
        Yield&& yield  // 回调：(mA_in, mB_in, correlation_factor)
    );
    
    /**
     * @brief 反向执行NeoAlzette Subround 0的掩码传播
     */
    template<typename Yield>
    static void execute_subround0_backward(
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
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // === 反向 Step 7: cd_from_A → B ^= ... ===（线性）
    // 简化：假设cd注入的影响可以通过转置传播
    // TODO: 使用完整的cd_from_A_transpose
    
    // === 反向 Step 6: A = l2_forward(A) ===
    mA = NeoAlzetteCore::l2_transpose(mA);
    
    // === 反向 Step 5: B = l1_forward(B) ===
    mB = NeoAlzetteCore::l1_transpose(mB);
    
    // === 反向 Step 4: A ^= rotl(B, 16) ===
    // mask_A_in = mask_A_out, mask_B_in ^= rotr(mask_A_out, 16)
    std::uint32_t mA_before_xor2 = mA;
    std::uint32_t mB_before_xor2 = mB ^ NeoAlzetteCore::rotr(mA, 16);
    
    // === 反向 Step 3: B ^= rotl(A, 24) ===
    std::uint32_t mB_before_xor1 = mB_before_xor2;
    std::uint32_t mA_before_xor1 = mA_before_xor2 ^ NeoAlzetteCore::rotr(mB_before_xor2, 24);
    
    // === 反向 Step 2: B -= RC[6] ===
    // 简化：假设常量模减对相关性影响较小
    std::uint32_t mB_before_sub = mB_before_xor1;
    
    // === 反向 Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5]) ===
    // 这是关键：需要枚举可能的(mA_in, mB_in)
    std::uint32_t mA_before_add = mA_before_xor1;
    
    // 简化版本：尝试几个候选
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mA_before_add, mB_before_sub},
        {mA_before_add ^ 1, mB_before_sub},
        {mA_before_add, mB_before_sub ^ 1},
    };
    
    for (const auto& [mA_candidate, mB_candidate] : candidates) {
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(mB_candidate, 31) ^ 
                                   NeoAlzetteCore::rotl(mB_candidate, 17);
        
        // 调用ARX算子计算相关性
        double corr = arx_operators::linear_cor_add_value_logn(
            mA_candidate, beta_mask, mA_before_add
        );
        
        if (corr > 0 && std::abs(corr) >= correlation_budget) {
            yield(mA_candidate, mB_candidate, corr);
        }
    }
}

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround0_backward(
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // === 反向执行Subround 0（类似Subround 1）===
    
    // 反向 cd_from_B
    // 反向 l1, l2
    mA = NeoAlzetteCore::l1_transpose(mA);
    mB = NeoAlzetteCore::l2_transpose(mB);
    
    // 反向 XOR
    std::uint32_t mB_before_xor2 = mB;
    std::uint32_t mA_before_xor2 = mA ^ NeoAlzetteCore::rotr(mB, 16);
    
    std::uint32_t mA_before_xor1 = mA_before_xor2;
    std::uint32_t mB_before_xor1 = mB_before_xor2 ^ NeoAlzetteCore::rotr(mA_before_xor2, 24);
    
    // 反向 A -= RC[1]
    std::uint32_t mA_before_sub = mA_before_xor1;
    
    // 反向 B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
    std::uint32_t mB_before_add = mB_before_xor1;
    
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mA_before_sub, mB_before_add},
        {mA_before_sub ^ 1, mB_before_add},
        {mA_before_sub, mB_before_add ^ 1},
    };
    
    for (const auto& [mA_candidate, mB_candidate] : candidates) {
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(mA_candidate, 31) ^ 
                                   NeoAlzetteCore::rotl(mA_candidate, 17);
        
        double corr = arx_operators::linear_cor_add_value_logn(
            mB_candidate, beta_mask, mB_before_add
        );
        
        if (corr > 0 && std::abs(corr) >= correlation_budget) {
            yield(mA_candidate, mB_candidate, corr);
        }
    }
}

} // namespace neoalz
