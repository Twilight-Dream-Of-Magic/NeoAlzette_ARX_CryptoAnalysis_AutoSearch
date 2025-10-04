#include "neoalzette/neoalzette_linear_search.hpp"
#include <algorithm>

namespace neoalz {

NeoAlzetteLinearSearch::SearchResult 
NeoAlzetteLinearSearch::search(const SearchConfig& config) {
    SearchResult result;
    result.best_correlation = 0.0;
    result.nodes_visited = 0;
    result.found = false;
    
    // 从最终掩码开始（反向）
    LinearState final_state(config.final_mA, config.final_mB, 1.0);
    std::vector<LinearState> trail;
    trail.push_back(final_state);
    
    // 开始递归搜索（反向）
    search_recursive(config, final_state, config.num_rounds, trail, result);
    
    return result;
}

void NeoAlzetteLinearSearch::search_recursive(
    const SearchConfig& config,
    const LinearState& current,
    int round,
    std::vector<LinearState>& trail,
    SearchResult& result
) {
    result.nodes_visited++;
    
    // 到达第0轮（初始输入）
    if (round == 0) {
        double abs_corr = std::abs(current.correlation);
        if (abs_corr > result.best_correlation) {
            result.best_correlation = abs_corr;
            result.best_trail = trail;
            result.found = true;
        }
        return;
    }
    
    // 剪枝：相关性太小
    if (std::abs(current.correlation) < config.correlation_threshold) return;
    
    double remaining_budget = std::abs(current.correlation);
    
    // === 反向执行NeoAlzette单轮（两个Subround）===
    
    // 白化反向（掩码不变）
    LinearState before_whitening = current;
    
    // Subround 1 反向
    execute_subround1_backward(before_whitening, remaining_budget,
        [&](std::uint32_t mA_before_sub1, std::uint32_t mB_before_sub1, double corr1) {
            
            double new_corr_after_sub1 = current.correlation * corr1;
            if (std::abs(new_corr_after_sub1) < config.correlation_threshold) return;
            
            LinearState before_sub1(mA_before_sub1, mB_before_sub1, new_corr_after_sub1);
            
            // Subround 0 反向
            execute_subround0_backward(before_sub1, std::abs(new_corr_after_sub1),
                [&](std::uint32_t mA_before_sub0, std::uint32_t mB_before_sub0, double corr0) {
                    
                    double final_corr = new_corr_after_sub1 * corr0;
                    if (std::abs(final_corr) < config.correlation_threshold) return;
                    
                    // 构建前一轮的状态
                    LinearState prev_state(mA_before_sub0, mB_before_sub0, final_corr);
                    
                    // 递归搜索前一轮
                    trail.push_back(prev_state);
                    search_recursive(config, prev_state, round - 1, trail, result);
                    trail.pop_back();
                });
        });
}

} // namespace neoalz
