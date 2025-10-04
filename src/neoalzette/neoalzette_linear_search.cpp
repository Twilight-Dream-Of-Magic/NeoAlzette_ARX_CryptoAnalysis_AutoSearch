#include "neoalzette/neoalzette_linear_search.hpp"
#include <algorithm>

namespace neoalz {

NeoAlzetteLinearSearch::SearchResult 
NeoAlzetteLinearSearch::search(const SearchConfig& config) {
    SearchResult result;
    result.best_correlation = 0.0;
    result.nodes_visited = 0;
    result.found = false;
    
    LinearState final_state(config.final_mA, config.final_mB, 1.0);
    std::vector<LinearState> trail;
    trail.push_back(final_state);
    
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
    
    if (round == 0) {
        double abs_corr = std::abs(current.correlation);
        if (abs_corr > result.best_correlation) {
            result.best_correlation = abs_corr;
            result.best_trail = trail;
            result.found = true;
        }
        return;
    }
    
    if (std::abs(current.correlation) < config.correlation_threshold) return;
    
    double remaining_budget = std::abs(current.correlation);
    
    // 🔴 关键：线性分析是逆向的！
    // 从当前轮的输出掩码，反向推导上一轮的输出掩码
    
    // Subround 2逆向分析（Steps 10 → 6）
    execute_subround2_backward_analysis(config, current, remaining_budget,
        [&](std::uint32_t mA_before_sub2, std::uint32_t mB_before_sub2, double corr2) {
            double new_corr_after_sub2 = current.correlation * corr2;
            
            if (std::abs(new_corr_after_sub2) < config.correlation_threshold) return;
            
            LinearState before_sub2(mA_before_sub2, mB_before_sub2, new_corr_after_sub2);
            
            // Subround 1逆向分析（Steps 5 → 1）
            execute_subround1_backward_analysis(config, before_sub2, std::abs(new_corr_after_sub2),
                [&](std::uint32_t mA_before_sub1, std::uint32_t mB_before_sub1, double corr1) {
                    double final_corr = new_corr_after_sub2 * corr1;
                    
                    if (std::abs(final_corr) < config.correlation_threshold) return;
                    
                    LinearState prev_state(mA_before_sub1, mB_before_sub1, final_corr);
                    
                    trail.push_back(prev_state);
                    search_recursive(config, prev_state, round - 1, trail, result);
                    trail.pop_back();
                });
        });
}

} // namespace neoalz
