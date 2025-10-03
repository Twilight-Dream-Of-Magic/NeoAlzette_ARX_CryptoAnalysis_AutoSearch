#include "neoalzette_medcp_analyzer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <queue>

namespace neoalz {

// ============================================================================
// MEDCP計算實現
// ============================================================================

NeoAlzetteMEDCPAnalyzer::Result 
NeoAlzetteMEDCPAnalyzer::compute_MEDCP(const Config& config) {
    auto start_time = std::chrono::steady_clock::now();
    
    Result result;
    result.MEDCP = 0.0;
    result.best_weight = std::numeric_limits<int>::max();
    result.nodes_visited = 0;
    result.search_complete = false;
    result.num_highways = 0;
    result.num_country_roads = 0;
    
    // 初始狀態
    SearchState initial;
    initial.round = 0;
    initial.delta_A = config.initial_dA;
    initial.delta_B = config.initial_dB;
    initial.accumulated_weight = 0;
    initial.partial_trail.add(0, config.initial_dA, config.initial_dB, 0);
    
    // 使用優先隊列進行Branch-and-bound搜索
    auto cmp = [](const SearchState& a, const SearchState& b) {
        return a.accumulated_weight > b.accumulated_weight;
    };
    std::priority_queue<SearchState, std::vector<SearchState>, decltype(cmp)> pq(cmp);
    pq.push(initial);
    
    if (config.verbose) {
        std::cout << "Starting MEDCP search for " << config.num_rounds << " rounds\n";
        std::cout << "Initial: dA=0x" << std::hex << config.initial_dA 
                  << " dB=0x" << config.initial_dB << std::dec << "\n";
        std::cout << "Weight cap: " << config.weight_cap << "\n";
    }
    
    // Branch-and-bound搜索
    while (!pq.empty()) {
        SearchState current = pq.top();
        pq.pop();
        
        result.nodes_visited++;
        
        // 剪枝：如果當前權重已超過最佳，跳過
        if (current.accumulated_weight >= result.best_weight) {
            continue;
        }
        
        // 剪枝：如果超過權重上限，跳過
        if (current.accumulated_weight >= config.weight_cap) {
            continue;
        }
        
        // 到達目標輪數
        if (current.round == config.num_rounds) {
            if (current.accumulated_weight < result.best_weight) {
                result.best_weight = current.accumulated_weight;
                result.best_trail = current.partial_trail;
                result.MEDCP = std::pow(2.0, -current.accumulated_weight);
                
                if (config.verbose) {
                    std::cout << "New best trail found! Weight: " << current.accumulated_weight 
                              << " (2^{-" << current.accumulated_weight << "})\n";
                }
            }
            continue;
        }
        
        // 枚舉下一輪狀態
        auto next_states = enumerate_next_round(current, config);
        
        for (const auto& next : next_states) {
            if (next.accumulated_weight < config.weight_cap) {
                pq.push(next);
            }
        }
        
        // 進度報告
        if (config.verbose && result.nodes_visited % 10000 == 0) {
            std::cout << "Nodes visited: " << result.nodes_visited 
                      << ", Queue size: " << pq.size()
                      << ", Best weight: " << result.best_weight << "\n";
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    result.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    ).count();
    
    result.search_complete = true;
    
    if (config.verbose) {
        std::cout << "\n=== MEDCP Analysis Complete ===\n";
        std::cout << "Rounds: " << config.num_rounds << "\n";
        std::cout << "Best weight: " << result.best_weight << "\n";
        std::cout << "MEDCP: 2^{-" << result.best_weight << "} ≈ " 
                  << result.MEDCP << "\n";
        std::cout << "Nodes visited: " << result.nodes_visited << "\n";
        std::cout << "Time: " << result.time_ms << " ms\n";
    }
    
    return result;
}

// ============================================================================
// 單輪差分枚舉
// ============================================================================

std::vector<NeoAlzetteMEDCPAnalyzer::SearchState> 
NeoAlzetteMEDCPAnalyzer::enumerate_next_round(
    const SearchState& current,
    const Config& config
) {
    std::vector<SearchState> next_states;
    
    // 使用NeoAlzetteDifferentialModel枚舉所有可能的下一輪差分
    int remaining_budget = config.weight_cap - current.accumulated_weight;
    
    NeoAlzetteDifferentialModel::enumerate_single_round_diffs(
        current.delta_A,
        current.delta_B,
        remaining_budget,
        [&](std::uint32_t dA_out, std::uint32_t dB_out, int weight) {
            SearchState next;
            next.round = current.round + 1;
            next.delta_A = dA_out;
            next.delta_B = dB_out;
            next.accumulated_weight = current.accumulated_weight + weight;
            next.partial_trail = current.partial_trail;
            next.partial_trail.add(next.round, dA_out, dB_out, weight);
            
            next_states.push_back(next);
        }
    );
    
    return next_states;
}

// ============================================================================
// 軌道驗證
// ============================================================================

bool NeoAlzetteMEDCPAnalyzer::verify_trail(const DifferentialTrail& trail) {
    if (trail.elements.empty()) return false;
    
    // 驗證每個輪次的連續性
    for (size_t i = 0; i + 1 < trail.elements.size(); ++i) {
        const auto& curr = trail.elements[i];
        const auto& next = trail.elements[i + 1];
        
        // 檢查差分是否可行
        bool feasible = NeoAlzetteDifferentialModel::is_diff_feasible(
            curr.delta_A, curr.delta_B,
            next.delta_A, next.delta_B,
            curr.weight + 10  // 寬鬆檢查
        );
        
        if (!feasible) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 軌道導出
// ============================================================================

void NeoAlzetteMEDCPAnalyzer::export_trail(
    const DifferentialTrail& trail,
    const std::string& filename
) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    file << "# NeoAlzette Differential Trail\n";
    file << "# Total weight: " << trail.total_weight << "\n";
    file << "# Total probability: 2^{-" << trail.total_weight << "}\n";
    file << "#\n";
    file << "# Round | Delta_A (hex) | Delta_B (hex) | Weight | Probability\n";
    file << "#-------+---------------+---------------+--------+-------------\n";
    
    for (const auto& elem : trail.elements) {
        file << std::dec << std::setw(6) << elem.round << " | ";
        file << "0x" << std::hex << std::setw(8) << std::setfill('0') << elem.delta_A << " | ";
        file << "0x" << std::hex << std::setw(8) << std::setfill('0') << elem.delta_B << " | ";
        file << std::dec << std::setw(6) << elem.weight << " | ";
        file << "2^{-" << elem.weight << "}\n";
    }
    
    file.close();
}

} // namespace neoalz
