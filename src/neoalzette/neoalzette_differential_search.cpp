#include "neoalzette/neoalzette_differential_search.hpp"
#include <algorithm>

namespace neoalz {

NeoAlzetteDifferentialSearch::SearchResult 
NeoAlzetteDifferentialSearch::search(const SearchConfig& config) {
    SearchResult result;
    result.best_weight = std::numeric_limits<int>::max();
    result.nodes_visited = 0;
    result.found = false;
    
    DiffState initial(config.initial_dA, config.initial_dB, 0);
    std::vector<DiffState> trail;
    trail.push_back(initial);
    
    search_recursive(config, initial, 0, trail, result);
    
    return result;
}

void NeoAlzetteDifferentialSearch::search_recursive(
    const SearchConfig& config,
    const DiffState& current,
    int round,
    std::vector<DiffState>& trail,
    SearchResult& result
) {
    result.nodes_visited++;
    
    if (round == config.num_rounds) {
        if (current.weight < result.best_weight) {
            result.best_weight = current.weight;
            result.best_trail = trail;
            result.found = true;
        }
        return;
    }
    
    if (current.weight >= config.weight_cap) return;
    
    int remaining_budget = config.weight_cap - current.weight;
    
    // Subround 0
    execute_subround0(config, current, remaining_budget,
        [&](std::uint32_t dA_after_sub0, std::uint32_t dB_after_sub0, int weight_sub0) {
            if (weight_sub0 >= remaining_budget) return;
            
            DiffState after_sub0(dA_after_sub0, dB_after_sub0, current.weight + weight_sub0);
            int remaining_after_sub0 = remaining_budget - weight_sub0;
            
            // Subround 1
            execute_subround1(config, after_sub0, remaining_after_sub0,
                [&](std::uint32_t dA_final, std::uint32_t dB_final, int weight_sub1) {
                    int total_weight_this_round = weight_sub0 + weight_sub1;
                    if (total_weight_this_round >= remaining_budget) return;
                    
                    DiffState next_state(dA_final, dB_final, 
                                        current.weight + total_weight_this_round);
                    
                    trail.push_back(next_state);
                    search_recursive(config, next_state, round + 1, trail, result);
                    trail.pop_back();
                });
        });
}

} // namespace neoalz
