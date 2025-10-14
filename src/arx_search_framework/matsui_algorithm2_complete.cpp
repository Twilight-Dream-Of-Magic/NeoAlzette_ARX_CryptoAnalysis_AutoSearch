#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <queue>
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"

namespace neoalz {

// ============================================================================
// HighwayTable Implementation
// ============================================================================

void MatsuiAlgorithm2Complete::HighwayTable::add(const DifferentialEntry& entry) {
    entries_.push_back(entry);
}

void MatsuiAlgorithm2Complete::HighwayTable::build_index() {
    input_index_.clear();
    output_index_.clear();
    
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        
        // Build input index: (α, β) → entry indices
        std::uint64_t key = make_key(entry.alpha, entry.beta);
        input_index_[key].push_back(i);
        
        // Build output index: γ → entry indices
        output_index_[entry.gamma].insert(i);
    }
}

std::vector<MatsuiAlgorithm2Complete::DifferentialEntry> 
MatsuiAlgorithm2Complete::HighwayTable::query(std::uint32_t alpha, std::uint32_t beta) const {
    std::vector<DifferentialEntry> result;
    
    if (beta == 0) {
        // Query all differentials with input alpha (any beta)
        for (const auto& entry : entries_) {
            if (entry.alpha == alpha) {
                result.push_back(entry);
            }
        }
    } else {
        // Query specific (α, β) pair
        std::uint64_t key = make_key(alpha, beta);
        auto it = input_index_.find(key);
        if (it != input_index_.end()) {
            for (std::size_t idx : it->second) {
                result.push_back(entries_[idx]);
            }
        }
    }
    
    return result;
}

bool MatsuiAlgorithm2Complete::HighwayTable::contains(std::uint32_t alpha, std::uint32_t beta) const {
    std::uint64_t key = make_key(alpha, beta);
    return input_index_.find(key) != input_index_.end();
}

bool MatsuiAlgorithm2Complete::HighwayTable::contains_output(std::uint32_t gamma) const {
    return output_index_.find(gamma) != output_index_.end();
}

std::vector<MatsuiAlgorithm2Complete::DifferentialEntry> 
MatsuiAlgorithm2Complete::HighwayTable::get_all() const {
    return entries_;
}

void MatsuiAlgorithm2Complete::HighwayTable::clear() {
    entries_.clear();
    input_index_.clear();
    output_index_.clear();
}

// ============================================================================
// CountryRoadsTable Implementation
// ============================================================================

void MatsuiAlgorithm2Complete::CountryRoadsTable::add(const DifferentialEntry& entry) {
    entries_.push_back(entry);
}

// ============================================================================
// Main Algorithm 2 Implementation
// ============================================================================

MatsuiAlgorithm2Complete::SearchResult 
MatsuiAlgorithm2Complete::execute_threshold_search(const SearchConfig& config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    SearchResult result;
    DifferentialTrail initial_trail;
    
    // Initialize best probabilities vector if not provided
    SearchConfig mutable_config = config;
    if (mutable_config.best_probs.empty()) {
        mutable_config.best_probs.resize(config.num_rounds, 1.0);
    }
    
    // Start recursive search from round 1
    threshold_search_recursive(mutable_config, 1, initial_trail, result);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    result.search_complete = true;
    
    return result;
}

void MatsuiAlgorithm2Complete::threshold_search_recursive(
    const SearchConfig& config,
    int current_round,
    DifferentialTrail& current_trail,
    SearchResult& result
) {
    // Termination check: max nodes explored
    if (result.nodes_explored >= config.max_nodes) {
        return;
    }
    
    result.nodes_explored++;
    
    const int n = config.num_rounds;
    const int r = current_round;
    
    // Route to appropriate processing function based on round number
    // Paper Algorithm 2, lines 2-36
    
    if (((r == 1) || (r == 2)) && (r != n)) {
        // Lines 3-8: Process rounds 1 and 2
        process_early_rounds(config, r, current_trail, result);
    }
    else if ((r > 2) && (r != n)) {
        // Lines 10-21: Process intermediate rounds with highways/country roads
        process_intermediate_rounds(config, r, current_trail, result);
    }
    else if (r == n) {
        // Lines 23-36: Process final round
        process_final_round(config, r, current_trail, result);
    }
}

void MatsuiAlgorithm2Complete::process_early_rounds(
    const SearchConfig& config,
    int current_round,
    DifferentialTrail& current_trail,
    SearchResult& result
) {
    // Paper Algorithm 2, lines 3-8:
    // if ((r = 1) ∨ (r = 2)) ∧ (r ≠ n) then
    //     for all (α, β, p) in H do
    //         p_r ← p, B̂_n ← p₁···p_r·B̂_{n-r}
    //         if B̂_n ≥ B_n then
    //             α_r ← α, β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
    //             call threshold_search(n, r+1, H, B̂, B_n, T̂)
    
    const int n = config.num_rounds;
    const int r = current_round;
    
    // Iterate over all entries in highway table H
    auto highways = config.highway_table.get_all();
    
    for (const auto& highway : highways) {
        // Line 5: p_r ← p
        double p_r = highway.probability;
        
        // Line 5: B̂_n ← p₁···p_r·B̂_{n-r}
        // Current trail probability so far: p₁···p_{r-1}
        double prob_so_far = current_trail.total_probability;
        
        // Estimated remaining probability: B̂_{n-r}
        double remaining_estimate = 1.0;
        if (r < n) {
            // Use best known probabilities for remaining rounds
            for (int i = r; i < n; ++i) {
                if (i < static_cast<int>(config.best_probs.size())) {
                    remaining_estimate *= config.best_probs[i];
                }
            }
        }
        
        // Estimated total probability: B̂_n
        double estimated_total = prob_so_far * p_r * remaining_estimate;
        
        // Line 6: if B̂_n ≥ B_n then
        if (check_pruning_condition(prob_so_far * p_r, remaining_estimate, config.initial_estimate)) {
            // Line 7: α_r ← α, β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
            TrailElement elem(highway.alpha, highway.beta, p_r, highway.weight);
            current_trail.add_round(elem);
            
            result.highways_used++;
            
            // Line 8: call threshold_search(n, r+1, H, B̂, B_n, T̂)
            threshold_search_recursive(config, r + 1, current_trail, result);
            
            // Backtrack: remove added element
            current_trail.rounds.pop_back();
            current_trail.total_probability /= p_r;
            current_trail.total_weight -= highway.weight;
        } else {
            result.nodes_pruned++;
        }
    }
}

void MatsuiAlgorithm2Complete::process_intermediate_rounds(
    const SearchConfig& config,
    int current_round,
    DifferentialTrail& current_trail,
    SearchResult& result
) {
    // Paper Algorithm 2, lines 10-21:
    // if (r > 2) ∧ (r ≠ n) then
    //     α_r ← (α_{r-2} + β_{r-1})
    //     p_{r,min} ← B_n/(p₁p₂···p_{r-1}·B̂_{n-r})
    //     C ← ∅
    //     for all β_r : (p_r(α_r → β_r) ≥ p_{r,min}) ∧ ((α_{r-1} + β_r) = γ ∈ H) do
    //         add (α_r, β_r, p_r) to C
    //     if C = ∅ then
    //         (β_r, p_r) ← p_r = max_β p(α_r → β)
    //         add (α_r, β_r, p_r) to C
    //     for all (α, β, p) : α = α_r in H and all (α, β, p) ∈ C do
    //         p_r ← p, B̂_n ← p₁p₂...p_r·B̂_{n-r}
    //         if B̂_n ≥ B_n then
    //             β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
    //             call threshold_search(n, r+1, H, B̂, B_n, T̂)
    
    const int n = config.num_rounds;
    const int r = current_round;
    
    if (static_cast<int>(current_trail.rounds.size()) < r - 1) {
        // Need at least r-1 previous rounds
        return;
    }
    
    // Line 11: α_r ← (α_{r-2} + β_{r-1})
    // Note: Feistel structure, input diff for round r comes from round r-2 output and r-1 output
    // Rounds are 1-indexed in paper, 0-indexed in vector
    // For round r (1-indexed), we need data from rounds r-2 and r-1
    // In vector: rounds[0] is round 1, rounds[1] is round 2, etc.
    // So for round r, we access rounds[r-1] (current), rounds[r-2] (previous), rounds[r-3] (two back)
    
    size_t idx_r_minus_2 = r - 3;  // Round r-2 in vector (0-indexed)
    size_t idx_r_minus_1 = r - 2;  // Round r-1 in vector (0-indexed)
    
    if (idx_r_minus_2 >= current_trail.rounds.size() || idx_r_minus_1 >= current_trail.rounds.size()) {
        // Not enough previous rounds
        return;
    }
    
    std::uint32_t alpha_r_minus_2 = current_trail.rounds[idx_r_minus_2].alpha_r;
    std::uint32_t beta_r_minus_1 = current_trail.rounds[idx_r_minus_1].beta_r;
    std::uint32_t alpha_r = alpha_r_minus_2 + beta_r_minus_1; // Modular addition
    
    std::uint32_t alpha_r_minus_1 = current_trail.rounds[idx_r_minus_1].alpha_r;
    
    // Line 11: p_{r,min} ← B_n/(p₁p₂···p_{r-1}·B̂_{n-r})
    double prob_so_far = current_trail.total_probability;
    
    double remaining_estimate = 1.0;
    for (int i = r; i < n; ++i) {
        if (i < static_cast<int>(config.best_probs.size())) {
            remaining_estimate *= config.best_probs[i];
        }
    }
    
    double p_r_min = config.initial_estimate / (prob_so_far * remaining_estimate);
    
    // Line 12: C ← ∅ (Initialize country roads table)
    CountryRoadsTable country_roads;
    
    if (config.use_country_roads) {
        // Lines 13-14: Build country roads table
        country_roads = build_country_roads_table(
            config, alpha_r, alpha_r_minus_1, p_r_min, 32
        );
        
        // Lines 15-16: If C = ∅, find maximum probability country road
        if (country_roads.empty()) {
            auto [best_beta, best_prob] = find_max_probability(alpha_r, 0, 32);
            int weight = probability_to_weight(best_prob);
            DifferentialEntry max_entry(alpha_r, best_beta, 0, best_prob, weight);
            country_roads.add(max_entry);
        }
    }
    
    // Line 17: for all (α, β, p) : α = α_r in H
    auto highways_with_alpha_r = config.highway_table.query(alpha_r, 0);
    
    // Combine highways and country roads for exploration
    std::vector<DifferentialEntry> candidates;
    
    // Add highways with matching input
    for (const auto& hw : highways_with_alpha_r) {
        candidates.push_back(hw);
    }
    
    // Add country roads
    for (const auto& cr : country_roads.get_all()) {
        candidates.push_back(cr);
    }
    
    // Lines 18-21: Explore all candidates
    for (const auto& candidate : candidates) {
        // Line 18: p_r ← p, B̂_n ← p₁p₂...p_r·B̂_{n-r}
        double p_r = candidate.probability;
        double estimated_total = prob_so_far * p_r * remaining_estimate;
        
        // Line 19: if B̂_n ≥ B_n then
        if (check_pruning_condition(prob_so_far * p_r, remaining_estimate, config.initial_estimate)) {
            // Line 20: β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
            TrailElement elem(alpha_r, candidate.beta, p_r, candidate.weight);
            current_trail.add_round(elem);
            
            // Track whether this is highway or country road
            bool is_highway = config.highway_table.contains(candidate.alpha, candidate.beta);
            if (is_highway) {
                result.highways_used++;
            } else {
                result.country_roads_used++;
            }
            
            // Line 21: call threshold_search(n, r+1, H, B̂, B_n, T̂)
            threshold_search_recursive(config, r + 1, current_trail, result);
            
            // Backtrack
            current_trail.rounds.pop_back();
            current_trail.total_probability /= p_r;
            current_trail.total_weight -= candidate.weight;
        } else {
            result.nodes_pruned++;
        }
    }
}

void MatsuiAlgorithm2Complete::process_final_round(
    const SearchConfig& config,
    int current_round,
    DifferentialTrail& current_trail,
    SearchResult& result
) {
    // Paper Algorithm 2, lines 23-36:
    // if (r = n) then
    //     α_r ← (α_{r-2} + β_{r-1})
    //     if (α_r in H) then
    //         (β_r, p_r) ← p_r = max_{β∈H} p(α_r → β)
    //     else
    //         (β_r, p_r) ← p_r = max_β p(α_r → β)
    //     if p_r ≥ p_thres then
    //         add (α_r, β_r, p_r) to H
    //     p_n ← p_r, B̂_n ← p₁p₂...p_n
    //     if B̂_n ≥ B_n then
    //         α_n ← α_r, β_n ← β, add T̂_n ← (α_n, β_n, p_n) to T̂
    //         B_n ← B̂_n, T ← T̂
    
    const int r = current_round;
    
    if (static_cast<int>(current_trail.rounds.size()) < r - 1) {
        return;
    }
    
    // Line 24: α_r ← (α_{r-2} + β_{r-1})
    size_t idx_r_minus_2 = r - 3;
    size_t idx_r_minus_1 = r - 2;
    
    if (idx_r_minus_2 >= current_trail.rounds.size() || idx_r_minus_1 >= current_trail.rounds.size()) {
        return;
    }
    
    std::uint32_t alpha_r_minus_2 = current_trail.rounds[idx_r_minus_2].alpha_r;
    std::uint32_t beta_r_minus_1 = current_trail.rounds[idx_r_minus_1].beta_r;
    std::uint32_t alpha_r = alpha_r_minus_2 + beta_r_minus_1;
    
    std::uint32_t best_beta = 0;
    double best_prob = 0.0;
    
    // Lines 25-28: Find best output difference
    if (config.highway_table.contains(alpha_r, 0)) {
        // Line 26: (β_r, p_r) ← p_r = max_{β∈H} p(α_r → β)
        auto highways = config.highway_table.query(alpha_r, 0);
        
        for (const auto& hw : highways) {
            if (hw.probability > best_prob) {
                best_prob = hw.probability;
                best_beta = hw.beta;
            }
        }
    } else {
        // Line 28: (β_r, p_r) ← p_r = max_β p(α_r → β)
        auto [beta, prob] = find_max_probability(alpha_r, 0, 32);
        best_beta = beta;
        best_prob = prob;
    }
    
    // Line 29-30: if p_r ≥ p_thres then add to H
    // (Note: In practice, we don't modify H during search to maintain const correctness)
    
    // Line 31: p_n ← p_r, B̂_n ← p₁p₂...p_n
    double p_n = best_prob;
    int w_n = probability_to_weight(p_n);
    
    double total_prob = current_trail.total_probability * p_n;
    int total_weight = current_trail.total_weight + w_n;
    
    // Line 32: if B̂_n ≥ B_n then
    if (total_prob >= config.initial_estimate || total_weight < result.best_weight) {
        // Lines 33-35: Update best found trail
        TrailElement final_elem(alpha_r, best_beta, p_n, w_n);
        current_trail.add_round(final_elem);
        
        // Update best result
        if (total_weight < result.best_weight) {
            result.best_weight = total_weight;
            result.best_probability = total_prob;
            result.best_trail = current_trail;
        }
        
        // Backtrack
        current_trail.rounds.pop_back();
        current_trail.total_probability /= p_n;
        current_trail.total_weight -= w_n;
    }
}

MatsuiAlgorithm2Complete::CountryRoadsTable 
MatsuiAlgorithm2Complete::build_country_roads_table(
    const SearchConfig& config,
    std::uint32_t alpha_r,
    std::uint32_t alpha_prev,
    double prob_min,
    int bit_width
) {
    // Paper Algorithm 2, lines 13-14:
    // for all β_r : (p_r(α_r → β_r) ≥ p_{r,min}) ∧ ((α_{r-1} + β_r) = γ ∈ H) do
    //     add (α_r, β_r, p_r) to C
    
    CountryRoadsTable country_roads;
    
    // Enumerate possible output differences β_r
    // In practice, we limit enumeration for performance
    const std::uint32_t max_enumerate = 1 << std::min(bit_width, 16);
    
    for (std::uint32_t beta_r = 0; beta_r < max_enumerate; ++beta_r) {
        // Condition 1: p_r(α_r → β_r) ≥ p_{r,min}
        double prob = compute_xdp_add(alpha_r, 0, beta_r, bit_width);
        
        if (prob < prob_min) {
            continue; // Skip if probability too low
        }
        
        // Condition 2: (α_{r-1} + β_r) = γ ∈ H
        std::uint32_t next_alpha = alpha_prev + beta_r; // Input for next round
        
        if (config.highway_table.contains_output(next_alpha)) {
            // Both conditions satisfied: add to country roads
            int weight = probability_to_weight(prob);
            DifferentialEntry entry(alpha_r, beta_r, 0, prob, weight);
            country_roads.add(entry);
        }
    }
    
    return country_roads;
}

bool MatsuiAlgorithm2Complete::check_pruning_condition(
    double current_prob,
    double remaining_estimate,
    double target_bound
) {
    // Paper pruning condition:
    // B̂_n = p₁·p₂·...·p_r·B̂_{n-r} ≥ B_n
    
    double estimated_total = current_prob * remaining_estimate;
    return estimated_total >= target_bound;
}

// ============================================================================
// Differential Probability Computation
// ============================================================================

double MatsuiAlgorithm2Complete::compute_xdp_add(
    std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma, int n
) {
    // 精確：使用 LM-2001 Algorithm 2 權重（含 "good" 條件）
    (void)n; // 位寬固定為32於底層實現中已處理
    int weight = TwilightDream::arx_operators::xdp_add_lm2001(alpha, beta, gamma);
    if (weight < 0 || weight > 1024) return 0.0;
    return std::exp2(-weight); // 2^{-weight}
}

std::pair<std::uint32_t, double>
MatsuiAlgorithm2Complete::find_max_probability(
    std::uint32_t alpha, std::uint32_t beta, int n
) {
    // 精確：使用 LM-2001 Algorithm 4 搜索最優 γ，再以 Algorithm 2 求權重
    auto [best_gamma, best_weight] = TwilightDream::arx_operators::find_optimal_gamma_with_weight(alpha, beta, n);
    if (best_weight < 0 || best_weight > 1024) return {best_gamma, 0.0};
    return {best_gamma, std::exp2(-best_weight)};
}

} // namespace neoalz
