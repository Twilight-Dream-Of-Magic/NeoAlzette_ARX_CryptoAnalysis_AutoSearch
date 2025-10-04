#include "neoalzette/neoalzette_melcc.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <queue>
#include <iomanip>

namespace neoalz {

// ============================================================================
// MELCC計算實現
// ============================================================================

NeoAlzetteMELCCAnalyzer::Result 
NeoAlzetteMELCCAnalyzer::compute_MELCC(const Config& config) {
    if (config.use_matrix_chain) {
        // 方法1：矩陣乘法鏈（精確方法）
        auto start_time = std::chrono::steady_clock::now();
        
        Result result;
        result.MELCC = compute_MELCC_matrix_chain(config.num_rounds);
        result.search_complete = true;
        result.nodes_visited = config.num_rounds;  // 只需計算R個矩陣
        
        // 構建矩陣序列
        for (int r = 0; r < config.num_rounds; ++r) {
            result.matrices.push_back(
                NeoAlzetteLinearModel::build_round_correlation_matrix(r)
            );
        }
        
        auto end_time = std::chrono::steady_clock::now();
        result.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count();
        
        if (config.verbose) {
            std::cout << "\n=== MELCC Analysis (Matrix Chain Method) ===\n";
            std::cout << "Rounds: " << config.num_rounds << "\n";
            std::cout << "MELCC: " << result.MELCC << "\n";
            std::cout << "Log2(MELCC): " << std::log2(result.MELCC) << "\n";
            std::cout << "Time: " << result.time_ms << " ms\n";
        }
        
        return result;
    } else {
        // 方法2：搜索方法（啟發式）
        return compute_MELCC_search(config);
    }
}

// ============================================================================
// 矩陣乘法鏈方法
// ============================================================================

double NeoAlzetteMELCCAnalyzer::compute_MELCC_matrix_chain(int num_rounds) {
    return NeoAlzetteLinearModel::compute_MELCC_via_matrix_chain(num_rounds);
}

// ============================================================================
// 搜索方法實現
// ============================================================================

NeoAlzetteMELCCAnalyzer::Result 
NeoAlzetteMELCCAnalyzer::compute_MELCC_search(const Config& config) {
    auto start_time = std::chrono::steady_clock::now();
    
    Result result;
    result.MELCC = 0.0;
    result.nodes_visited = 0;
    result.search_complete = false;
    
    // 初始狀態
    SearchState initial;
    initial.round = 0;
    initial.mask_A = config.initial_mask_A;
    initial.mask_B = config.initial_mask_B;
    initial.accumulated_correlation = 1.0;
    initial.partial_trail.add(0, config.initial_mask_A, config.initial_mask_B, 1.0);
    
    // 使用優先隊列（按相關性降序）
    auto cmp = [](const SearchState& a, const SearchState& b) {
        return a.accumulated_correlation < b.accumulated_correlation;
    };
    std::priority_queue<SearchState, std::vector<SearchState>, decltype(cmp)> pq(cmp);
    pq.push(initial);
    
    if (config.verbose) {
        std::cout << "Starting MELCC search for " << config.num_rounds << " rounds\n";
        std::cout << "Initial: mA=0x" << std::hex << config.initial_mask_A 
                  << " mB=0x" << config.initial_mask_B << std::dec << "\n";
        std::cout << "Correlation threshold: " << config.correlation_threshold << "\n";
    }
    
    // Branch-and-bound搜索
    while (!pq.empty()) {
        SearchState current = pq.top();
        pq.pop();
        
        result.nodes_visited++;
        
        // 剪枝：如果當前相關性已低於閾值，跳過
        if (current.accumulated_correlation < config.correlation_threshold) {
            continue;
        }
        
        // 到達目標輪數
        if (current.round == config.num_rounds) {
            if (std::abs(current.accumulated_correlation) > std::abs(result.MELCC)) {
                result.MELCC = current.accumulated_correlation;
                result.best_trail = current.partial_trail;
                result.best_trail.compute_total();
                
                if (config.verbose) {
                    std::cout << "New best trail found! Correlation: " 
                              << current.accumulated_correlation << "\n";
                }
            }
            continue;
        }
        
        // 枚舉下一輪狀態
        auto next_states = enumerate_next_round_linear(current, config);
        
        for (const auto& next : next_states) {
            if (std::abs(next.accumulated_correlation) >= config.correlation_threshold) {
                pq.push(next);
            }
        }
        
        // 進度報告
        if (config.verbose && result.nodes_visited % 10000 == 0) {
            std::cout << "Nodes visited: " << result.nodes_visited 
                      << ", Queue size: " << pq.size()
                      << ", Best correlation: " << result.MELCC << "\n";
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    result.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    ).count();
    
    result.search_complete = true;
    
    if (config.verbose) {
        std::cout << "\n=== MELCC Analysis Complete (Search Method) ===\n";
        std::cout << "Rounds: " << config.num_rounds << "\n";
        std::cout << "MELCC: " << result.MELCC << "\n";
        std::cout << "Log2(MELCC): " << std::log2(std::abs(result.MELCC)) << "\n";
        std::cout << "Nodes visited: " << result.nodes_visited << "\n";
        std::cout << "Time: " << result.time_ms << " ms\n";
    }
    
    return result;
}

// ============================================================================
// 單輪線性枚舉
// ============================================================================

std::vector<NeoAlzetteMELCCAnalyzer::SearchState> 
NeoAlzetteMELCCAnalyzer::enumerate_next_round_linear(
    const SearchState& current,
    const Config& config
) {
    std::vector<SearchState> next_states;
    
    // 簡化版本：枚舉一些候選掩碼
    // 完整版本應該使用Wallén完整枚舉
    
    std::vector<std::uint32_t> candidate_masks;
    
    // 添加基本候選
    candidate_masks.push_back(current.mask_A);
    candidate_masks.push_back(current.mask_B);
    candidate_masks.push_back(current.mask_A ^ current.mask_B);
    
    // 添加旋轉變體
    for (int shift = 1; shift <= 16; shift *= 2) {
        candidate_masks.push_back(NeoAlzetteCore::rotl(current.mask_A, shift));
        candidate_masks.push_back(NeoAlzetteCore::rotl(current.mask_B, shift));
    }
    
    // 檢查每個候選
    for (std::uint32_t mask_A_next : candidate_masks) {
        for (std::uint32_t mask_B_next : candidate_masks) {
            // 計算線性相關性
            // 簡化：假設單輪相關性基於Hamming距離
            int hamming_dist = __builtin_popcount(mask_A_next ^ current.mask_A) +
                               __builtin_popcount(mask_B_next ^ current.mask_B);
            
            double round_correlation = std::pow(2.0, -hamming_dist / 8.0);
            
            if (round_correlation >= config.correlation_threshold) {
                SearchState next;
                next.round = current.round + 1;
                next.mask_A = mask_A_next;
                next.mask_B = mask_B_next;
                next.accumulated_correlation = current.accumulated_correlation * round_correlation;
                next.partial_trail = current.partial_trail;
                next.partial_trail.add(next.round, mask_A_next, mask_B_next, round_correlation);
                
                next_states.push_back(next);
            }
        }
    }
    
    return next_states;
}

// ============================================================================
// 軌道驗證
// ============================================================================

bool NeoAlzetteMELCCAnalyzer::verify_trail(const LinearTrail& trail) {
    if (trail.elements.empty()) return false;
    
    // 驗證相關性計算
    double computed_correlation = 1.0;
    for (const auto& elem : trail.elements) {
        computed_correlation *= elem.correlation;
    }
    
    // 檢查是否與總相關性一致
    double diff = std::abs(computed_correlation - trail.total_correlation);
    return diff < 1e-9;
}

// ============================================================================
// 軌道導出
// ============================================================================

void NeoAlzetteMELCCAnalyzer::export_trail(
    const LinearTrail& trail,
    const std::string& filename
) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    file << "# NeoAlzette Linear Trail\n";
    file << "# Total correlation: " << trail.total_correlation << "\n";
    file << "# Log2(correlation): " << std::log2(std::abs(trail.total_correlation)) << "\n";
    file << "#\n";
    file << "# Round | Mask_A (hex) | Mask_B (hex) | Correlation\n";
    file << "#-------+--------------+--------------+-------------\n";
    
    for (const auto& elem : trail.elements) {
        file << std::dec << std::setw(6) << elem.round << " | ";
        file << "0x" << std::hex << std::setw(8) << std::setfill('0') << elem.mask_A << " | ";
        file << "0x" << std::hex << std::setw(8) << std::setfill('0') << elem.mask_B << " | ";
        file << std::scientific << elem.correlation << "\n";
    }
    
    file.close();
}

} // namespace neoalz
