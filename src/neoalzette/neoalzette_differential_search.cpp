#include "neoalzette/neoalzette_differential_search.hpp"
#include <algorithm>

namespace neoalz {

// ============================================================================
// 主搜索入口
// ============================================================================

NeoAlzetteDifferentialSearch::SearchResult 
NeoAlzetteDifferentialSearch::search(const SearchConfig& config) {
    SearchResult result;
    result.best_weight = std::numeric_limits<int>::max();
    result.nodes_visited = 0;
    result.found = false;
    
    // ========================================================================
    // 初始化搜索
    // ========================================================================
    // 
    // 通常差分搜索从简单的差分开始：
    // - dA = 1（单比特差分）
    // - dB = 0（无差分）
    // 
    // 为什么？
    // - 单比特差分最容易分析
    // - 可以后续尝试其他初始差分
    // 
    DiffState initial(config.initial_dA, config.initial_dB, 0);
    std::vector<DiffState> trail;
    trail.push_back(initial);
    
    // ========================================================================
    // 开始递归搜索
    // ========================================================================
    // 
    // 从第0轮开始，逐轮向前搜索
    // 每一轮都直接执行NeoAlzette的所有操作
    // 
    search_recursive(config, initial, 0, trail, result);
    
    return result;
}

// ============================================================================
// 递归搜索核心
// ============================================================================

void NeoAlzetteDifferentialSearch::search_recursive(
    const SearchConfig& config,
    const DiffState& current,
    int round,
    std::vector<DiffState>& trail,
    SearchResult& result
) {
    result.nodes_visited++;
    
    // ========================================================================
    // 终止条件1：到达目标轮数
    // ========================================================================
    // 
    // 如果已经搜索了config.num_rounds轮，搜索结束
    // 记录当前轨道的权重
    // 
    if (round == config.num_rounds) {
        if (current.weight < result.best_weight) {
            result.best_weight = current.weight;
            result.best_trail = trail;
            result.found = true;
        }
        return;
    }
    
    // ========================================================================
    // 终止条件2：剪枝 - 权重已超过上限
    // ========================================================================
    // 
    // 如果当前累积权重已经 >= weight_cap，不可能找到更优解
    // 立即停止这个分支（这就是Branch-and-Bound的"Bound"）
    // 
    if (current.weight >= config.weight_cap) return;
    
    int remaining_budget = config.weight_cap - current.weight;
    
    // ========================================================================
    // 执行NeoAlzette单轮（两个Subround）
    // ========================================================================
    // 
    // 关键：这里直接执行NeoAlzette的每一步操作！
    // 不是调用一个"enumerate_single_round"函数！
    // 
    
    // === Subround 0 ===
    execute_subround0(current, remaining_budget,
        [&](std::uint32_t dA_after_sub0, std::uint32_t dB_after_sub0, int weight_sub0) {
            // ------------------------------------------------------------
            // 回调函数：处理Subround 0的每一个可行输出
            // ------------------------------------------------------------
            // 
            // 参数：
            // - dA_after_sub0, dB_after_sub0: Subround 0的输出差分
            // - weight_sub0: Subround 0消耗的权重（w1 + w2）
            // 
            // 为什么用回调？
            // - 不缓存Subround 0的所有结果
            // - 立即处理每一个结果
            // - 可以立即剪枝
            // 
            
            // 剪枝：Subround 0已经用完了预算
            if (weight_sub0 >= remaining_budget) return;
            
            DiffState after_sub0(dA_after_sub0, dB_after_sub0, current.weight + weight_sub0);
            int remaining_after_sub0 = remaining_budget - weight_sub0;
            
            // === Subround 1 ===
            execute_subround1(after_sub0, remaining_after_sub0,
                [&](std::uint32_t dA_final, std::uint32_t dB_final, int weight_sub1) {
                    // --------------------------------------------------------
                    // 回调函数：处理Subround 1的每一个可行输出
                    // --------------------------------------------------------
                    
                    int total_weight_this_round = weight_sub0 + weight_sub1;
                    
                    // 剪枝：本轮总权重超过预算
                    if (total_weight_this_round >= remaining_budget) return;
                    
                    // ========================================================
                    // 递归搜索下一轮
                    // ========================================================
                    // 
                    // 关键：立即递归，不缓存！
                    // 
                    // 构建下一轮的状态
                    DiffState next_state(dA_final, dB_final, 
                                        current.weight + total_weight_this_round);
                    
                    // 递归搜索下一轮（round+1）
                    trail.push_back(next_state);
                    search_recursive(config, next_state, round + 1, trail, result);
                    trail.pop_back();  // 回溯
                    
                    // ========================================================
                    // 为什么要回溯（pop_back）？
                    // ========================================================
                    // 
                    // 因为我们在搜索树中深度优先遍历：
                    // - push: 进入一个分支
                    // - 递归搜索
                    // - pop: 退出这个分支，尝试其他分支
                    // 
                    // 这样trail始终保持当前搜索路径，不会爆炸
                    // 
                });
        });
}

} // namespace neoalz
