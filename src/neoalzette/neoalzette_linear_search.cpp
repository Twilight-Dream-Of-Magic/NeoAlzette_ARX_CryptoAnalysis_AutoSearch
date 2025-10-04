#include "neoalzette/neoalzette_linear_search.hpp"
#include <algorithm>

namespace neoalz {

// ============================================================================
// 主搜索入口
// ============================================================================

NeoAlzetteLinearSearch::SearchResult 
NeoAlzetteLinearSearch::search(const SearchConfig& config) {
    SearchResult result;
    result.best_correlation = 0.0;
    result.nodes_visited = 0;
    result.found = false;
    
    // ========================================================================
    // 初始化搜索（注意：线性分析是反向的！）
    // ========================================================================
    // 
    // 从最终输出掩码开始：
    // - final_mA: 攻击目标（通常是单比特掩码，如0x00000001）
    // - final_mB: 通常为0
    // - 初始相关性: 1.0（还没有任何操作）
    // 
    // 为什么从最终掩码开始？
    // - 线性分析关注：α·输入 ⊕ β·输出 = 0
    // - 我们固定输出掩码β（攻击目标），反推输入掩码α
    // - 所以搜索是反向的：从β开始，推导α
    // 
    LinearState final_state(config.final_mA, config.final_mB, 1.0);
    std::vector<LinearState> trail;
    trail.push_back(final_state);
    
    // ========================================================================
    // 开始递归搜索（反向传播）
    // ========================================================================
    // 
    // 从第num_rounds轮开始，逐轮向前反推
    // 每一轮都反向执行NeoAlzette的所有操作
    // 
    // 注意：round是倒着数的
    // - round = num_rounds: 从最后一轮开始
    // - round = 0: 到达初始输入（搜索完成）
    // 
    search_recursive(config, final_state, config.num_rounds, trail, result);
    
    return result;
}

// ============================================================================
// 递归搜索核心（反向传播）
// ============================================================================

void NeoAlzetteLinearSearch::search_recursive(
    const SearchConfig& config,
    const LinearState& current,
    int round,
    std::vector<LinearState>& trail,
    SearchResult& result
) {
    result.nodes_visited++;
    
    // ========================================================================
    // 终止条件1：到达初始输入（round = 0）
    // ========================================================================
    // 
    // 如果已经反向传播到第0轮（初始输入），搜索结束
    // 记录当前的累积相关性
    // 
    if (round == 0) {
        double abs_corr = std::abs(current.correlation);
        if (abs_corr > result.best_correlation) {
            result.best_correlation = abs_corr;
            result.best_trail = trail;
            result.found = true;
        }
        return;
    }
    
    // ========================================================================
    // 终止条件2：剪枝 - 相关性已低于阈值
    // ========================================================================
    // 
    // 如果累积相关性 < 阈值，不可能找到有效的线性逼近
    // 立即停止这个分支
    // 
    if (std::abs(current.correlation) < config.correlation_threshold) return;
    
    double remaining_budget = std::abs(current.correlation);
    
    // ========================================================================
    // 反向执行NeoAlzette单轮（两个Subround）
    // ========================================================================
    // 
    // 前向加密顺序：Subround 0 → Subround 1 → 白化
    // 反向传播顺序：白化 → Subround 1 → Subround 0
    // 
    
    // === 白化反向（掩码不变）===
    // 前向：A ^= RC[10], B ^= RC[11]
    // 反向：掩码不变（XOR常量对掩码无影响）
    LinearState before_whitening = current;
    
    // === Subround 1 反向 ===
    execute_subround1_backward(before_whitening, remaining_budget,
        [&](std::uint32_t mA_before_sub1, std::uint32_t mB_before_sub1, double corr1) {
            // ------------------------------------------------------------
            // 回调函数：处理Subround 1反向传播的每一个可行输入掩码
            // ------------------------------------------------------------
            // 
            // 参数：
            // - mA_before_sub1, mB_before_sub1: Subround 1的输入掩码
            // - corr1: Subround 1的线性相关性因子
            // 
            // 累积相关性：
            // - 线性相关性是乘法的：total_corr = corr0 * corr1 * ...
            // - 权重是加法的：total_weight = w0 + w1 + ...
            // - 因为 weight = -log2(|corr|)，log(ab) = log(a) + log(b)
            // 
            
            double new_corr_after_sub1 = current.correlation * corr1;
            
            // 剪枝：相关性太小
            if (std::abs(new_corr_after_sub1) < config.correlation_threshold) return;
            
            LinearState before_sub1(mA_before_sub1, mB_before_sub1, new_corr_after_sub1);
            
            // === Subround 0 反向 ===
            execute_subround0_backward(before_sub1, std::abs(new_corr_after_sub1),
                [&](std::uint32_t mA_before_sub0, std::uint32_t mB_before_sub0, double corr0) {
                    
                    double final_corr = new_corr_after_sub1 * corr0;
                    
                    // 剪枝：累积相关性太小
                    if (std::abs(final_corr) < config.correlation_threshold) return;
                    
                    // ========================================================
                    // 递归搜索前一轮（round - 1）
                    // ========================================================
                    // 
                    // 注意：线性分析是反向的，所以round递减
                    // - round = num_rounds → round = num_rounds - 1 → ... → round = 0
                    // 
                    LinearState prev_state(mA_before_sub0, mB_before_sub0, final_corr);
                    
                    trail.push_back(prev_state);
                    search_recursive(config, prev_state, round - 1, trail, result);
                    trail.pop_back();  // 回溯
                });
        });
}

} // namespace neoalz
