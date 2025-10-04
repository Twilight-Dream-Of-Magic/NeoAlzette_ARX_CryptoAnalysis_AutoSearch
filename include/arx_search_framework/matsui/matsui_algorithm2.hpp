#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <limits>
#include <cmath>
#include <optional>
#include <algorithm>

namespace neoalz {

/**
 * @brief Complete implementation of Matsui's Algorithm 2 for threshold search
 *        in ARX ciphers using partial Difference Distribution Tables (pDDT).
 * 
 * This implementation faithfully follows the algorithm described in:
 * "Automatic Search for Differential Trails in ARX Ciphers" by Biryukov & Velichkov
 * 
 * Mathematical Foundation:
 * ========================
 * 
 * 1. Differential Probability (DP):
 *    For XOR differences α, β, γ:
 *    xdp⁺(α, β → γ) = 2^{-2n} · |{(x,y) : ((x⊕α)+(y⊕β))⊕(x+y) = γ}|
 * 
 * 2. Partial DDT (pDDT):
 *    H = {(α, β, γ, p) : DP(α, β → γ) ≥ p_thres}
 *    Contains only "highways" - high probability differentials
 * 
 * 3. Weight Computation:
 *    w = -log₂(p) where p is the differential probability
 * 
 * 4. Trail Probability:
 *    For n-round trail T = {(α₁,β₁), (α₂,β₂), ..., (αₙ,βₙ)}:
 *    P(T) = ∏ᵢ₌₁ⁿ p(αᵢ → βᵢ)
 *    W(T) = ∑ᵢ₌₁ⁿ w(αᵢ → βᵢ) = -log₂(P(T))
 * 
 * Key Concepts:
 * =============
 * 
 * - Highways (H): High-probability differentials stored in pDDT
 * - Country Roads (C): Low-probability differentials computed on-demand
 * - Threshold Search: Branch-and-bound with probability-based pruning
 * 
 * Algorithm Strategy:
 * ===================
 * 
 * Rounds 1-2: Freely choose from highways (pDDT)
 * Rounds 3-(n-1): Use highways when possible, country roads to connect back
 * Round n: Final round - maximize probability or select from highways
 */
class MatsuiAlgorithm2Complete {
public:
    // ========================================================================
    // Core data structures matching paper notation
    // ========================================================================
    
    /**
     * @brief Differential entry in pDDT or country roads table
     * 
     * Represents a differential (α, β → γ) with its probability p
     * 
     * Mathematical notation from paper:
     * - α (alpha): Input difference 1 (first operand)
     * - β (beta): Input difference 2 (second operand)
     * - γ (gamma): Output difference
     * - p: Differential probability DP(α, β → γ)
     * - w: Weight = -log₂(p)
     */
    struct DifferentialEntry {
        std::uint32_t alpha;        ///< α: First input difference
        std::uint32_t beta;         ///< β: Second input difference  
        std::uint32_t gamma;        ///< γ: Output difference
        double probability;         ///< p: DP(α, β → γ)
        int weight;                 ///< w = -log₂(p)
        
        DifferentialEntry() = default;
        DifferentialEntry(std::uint32_t a, std::uint32_t b, std::uint32_t g, 
                         double p, int w)
            : alpha(a), beta(b), gamma(g), probability(p), weight(w) {}
    };
    
    /**
     * @brief Partial Difference Distribution Table (pDDT)
     * 
     * Highway table H containing high-probability differentials:
     * H = {(α, β, γ, p) : DP(α, β → γ) ≥ p_thres}
     * 
     * Indexed by input differences (α, β) for efficient lookup
     */
    class HighwayTable {
    public:
        /**
         * @brief Add differential to highway table
         * @param entry Differential entry (α, β → γ, p)
         */
        void add(const DifferentialEntry& entry);
        
        /**
         * @brief Query all differentials with given input differences
         * @param alpha α: First input difference
         * @param beta β: Second input difference (optional, if 0 then query all β)
         * @return Vector of matching differential entries
         */
        std::vector<DifferentialEntry> query(std::uint32_t alpha, 
                                             std::uint32_t beta = 0) const;
        
        /**
         * @brief Check if (α, β) exists in highway table
         * @param alpha α: First input difference
         * @param beta β: Second input difference
         * @return True if differential exists in H
         */
        bool contains(std::uint32_t alpha, std::uint32_t beta) const;
        
        /**
         * @brief Check if output difference γ exists in highway table
         * @param gamma γ: Output difference to search for
         * @return True if any differential with output γ exists
         */
        bool contains_output(std::uint32_t gamma) const;
        
        /**
         * @brief Get all entries from highway table
         * @return Vector of all differential entries
         */
        std::vector<DifferentialEntry> get_all() const;
        
        /**
         * @brief Get number of entries in highway table
         * @return Size of H
         */
        std::size_t size() const { return entries_.size(); }
        
        /**
         * @brief Clear all entries
         */
        void clear();
        
        /**
         * @brief Build index for fast lookups
         */
        void build_index();
        
    private:
        std::vector<DifferentialEntry> entries_;
        
        // Index: (α, β) → vector of entry indices
        std::unordered_map<std::uint64_t, std::vector<std::size_t>> input_index_;
        
        // Index: γ → set of entry indices (for country roads condition check)
        std::unordered_map<std::uint32_t, std::unordered_set<std::size_t>> output_index_;
        
        // Helper to make key from (α, β)
        static std::uint64_t make_key(std::uint32_t alpha, std::uint32_t beta) {
            return (std::uint64_t(alpha) << 32) | beta;
        }
    };
    
    /**
     * @brief Country roads table for intermediate rounds
     * 
     * Temporary table C constructed on-demand for each input difference
     * encountered in intermediate rounds (rounds 3 to n-1).
     * 
     * Contains low-probability differentials that:
     * 1. Have probability ≥ p_r,min (minimum required for current round)
     * 2. Lead back to a highway: (α_{r-1} + β_r) = γ ∈ H
     * 
     * Mathematical condition from paper (line 13):
     * C = {(α_r, β_r, p_r) : p_r(α_r → β_r) ≥ p_r,min ∧ (α_{r-1} + β_r = γ ∈ H)}
     */
    class CountryRoadsTable {
    public:
        /**
         * @brief Add entry to country roads table
         * @param entry Differential entry
         */
        void add(const DifferentialEntry& entry);
        
        /**
         * @brief Get all entries from country roads table
         * @return Vector of all differential entries in C
         */
        std::vector<DifferentialEntry> get_all() const { return entries_; }
        
        /**
         * @brief Check if table is empty
         * @return True if C = ∅
         */
        bool empty() const { return entries_.empty(); }
        
        /**
         * @brief Get number of entries
         * @return |C|
         */
        std::size_t size() const { return entries_.size(); }
        
        /**
         * @brief Clear all entries
         */
        void clear() { entries_.clear(); }
        
    private:
        std::vector<DifferentialEntry> entries_;
    };
    
    /**
     * @brief Trail element representing one round's differential
     * 
     * Paper notation: T_r = (α_r, β_r, p_r)
     * Represents the differential at round r with:
     * - α_r: Input difference to round r
     * - β_r: Output difference from round r
     * - p_r: Probability of the round differential
     */
    struct TrailElement {
        std::uint32_t alpha_r;      ///< α_r: Input difference for round r
        std::uint32_t beta_r;       ///< β_r: Output difference from round r
        double prob_r;              ///< p_r: Round probability
        int weight_r;               ///< w_r: Round weight = -log₂(p_r)
        
        TrailElement() = default;
        TrailElement(std::uint32_t a, std::uint32_t b, double p, int w)
            : alpha_r(a), beta_r(b), prob_r(p), weight_r(w) {}
    };
    
    /**
     * @brief Complete differential trail for n rounds
     * 
     * Paper notation: T = (T₁, T₂, ..., Tₙ)
     * where each T_r = (α_r, β_r, p_r)
     * 
     * Trail probability: P(T) = ∏ᵢ₌₁ⁿ p_i
     * Trail weight: W(T) = ∑ᵢ₌₁ⁿ w_i = -log₂(P(T))
     */
    struct DifferentialTrail {
        std::vector<TrailElement> rounds;   ///< T = (T₁, ..., Tₙ)
        double total_probability;           ///< P(T) = ∏pᵢ
        int total_weight;                   ///< W(T) = ∑wᵢ
        
        DifferentialTrail() : total_probability(1.0), total_weight(0) {}
        
        /**
         * @brief Add round differential to trail
         * @param elem Trail element for round r
         */
        void add_round(const TrailElement& elem) {
            rounds.push_back(elem);
            total_probability *= elem.prob_r;
            total_weight += elem.weight_r;
        }
        
        /**
         * @brief Get number of rounds in trail
         * @return n = |T|
         */
        std::size_t num_rounds() const { return rounds.size(); }
    };
    
    /**
     * @brief Search result containing best found trail
     * 
     * Paper notation:
     * - B̂_n: Best found probability for n rounds
     * - T̂: Best found trail
     * - Condition: B_n ≤ B̂_n ≤ Bₙ where Bₙ is optimal
     */
    struct SearchResult {
        DifferentialTrail best_trail;       ///< T̂: Best found trail
        double best_probability;            ///< B̂_n: Best found probability
        int best_weight;                    ///< Ŵ_n: Best found weight
        
        std::uint64_t nodes_explored;       ///< Total nodes in search tree
        std::uint64_t nodes_pruned;         ///< Nodes pruned by threshold
        std::uint64_t highways_used;        ///< Number of highway transitions
        std::uint64_t country_roads_used;   ///< Number of country road transitions
        
        bool search_complete;               ///< True if search finished
        double elapsed_seconds;             ///< Search time
        
        SearchResult() 
            : best_probability(0.0)
            , best_weight(std::numeric_limits<int>::max())
            , nodes_explored(0)
            , nodes_pruned(0)
            , highways_used(0)
            , country_roads_used(0)
            , search_complete(false)
            , elapsed_seconds(0.0) {}
    };
    
    /**
     * @brief Configuration for threshold search algorithm
     * 
     * Paper inputs:
     * - n: Number of rounds
     * - H: Highway table (pDDT)
     * - B̂ = (B̂₁, B̂₂, ..., B̂_{n-1}): Best probabilities for first (n-1) rounds
     * - B_n: Initial estimate (lower bound)
     * - p_thres: Probability threshold for pDDT construction
     */
    struct SearchConfig {
        int num_rounds;                     ///< n: Total number of rounds
        HighwayTable highway_table;         ///< H: pDDT containing highways
        std::vector<double> best_probs;     ///< B̂ᵢ: Best probs for rounds 1..n-1
        double initial_estimate;            ///< B_n: Initial estimate
        double prob_threshold;              ///< p_thres: Highway threshold
        
        std::uint64_t max_nodes;            ///< Max nodes to explore (termination)
        bool use_country_roads;             ///< Enable country roads strategy
        
        SearchConfig()
            : num_rounds(4)
            , initial_estimate(1e-12)
            , prob_threshold(0.01)
            , max_nodes(10000000)
            , use_country_roads(true) {}
    };
    
    // ========================================================================
    // Main Algorithm 2 Implementation
    // ========================================================================
    
    /**
     * @brief Execute complete Matsui Algorithm 2 threshold search
     * 
     * Paper Algorithm 2: threshold_search(n, r, H, B̂, B_n, T)
     * 
     * This is the main entry point that implements the complete recursive
     * threshold search with highways/country roads strategy.
     * 
     * @param config Search configuration with n, H, B̂, B_n, p_thres
     * @return SearchResult containing best trail and statistics
     * 
     * Mathematical guarantee:
     * B_n ≤ B̂_n ≤ Bₙ where Bₙ is the optimal n-round probability
     */
    static SearchResult execute_threshold_search(const SearchConfig& config);
    
    // ========================================================================
    // Helper functions for differential probability computation
    // ========================================================================
    
    /**
     * @brief Compute XOR differential probability of modular addition
     * 
     * xdp⁺(α, β → γ) = 2^{-2n} · |{(x,y) : ((x⊕α)+(y⊕β))⊕(x+y) = γ}|
     * 
     * @param alpha α: First input XOR difference
     * @param beta β: Second input XOR difference
     * @param gamma γ: Output XOR difference
     * @param n Bit width
     * @return Differential probability (exact computation)
     */
    static double compute_xdp_add(std::uint32_t alpha, std::uint32_t beta, 
                                  std::uint32_t gamma, int n);
    
    /**
     * @brief Find maximum probability output difference
     * 
     * max_γ xdp⁺(α, β → γ)
     * 
     * @param alpha α: First input difference
     * @param beta β: Second input difference
     * @param n Bit width
     * @return Pair of (best γ, maximum probability)
     */
    static std::pair<std::uint32_t, double> 
    find_max_probability(std::uint32_t alpha, std::uint32_t beta, int n);
    
    /**
     * @brief Compute weight from probability
     * 
     * w = -log₂(p)
     * 
     * @param probability p: Differential probability
     * @return w: Weight
     */
    static int probability_to_weight(double probability) {
        if (probability <= 0.0) return std::numeric_limits<int>::max();
        return static_cast<int>(-std::log2(probability));
    }
    
    /**
     * @brief Compute probability from weight
     * 
     * p = 2^{-w}
     * 
     * @param weight w: Weight
     * @return p: Differential probability
     */
    static double weight_to_probability(int weight) {
        return std::pow(2.0, -static_cast<double>(weight));
    }

private:
    // ========================================================================
    // Internal recursive search implementation
    // ========================================================================
    
    /**
     * @brief Internal recursive threshold search (matching paper's structure)
     * 
     * Paper Algorithm 2, lines 1-36
     * 
     * @param config Search configuration
     * @param current_round r: Current round (1-indexed)
     * @param current_trail T: Trail constructed so far
     * @param result Search result (updated during recursion)
     */
    static void threshold_search_recursive(
        const SearchConfig& config,
        int current_round,
        DifferentialTrail& current_trail,
        SearchResult& result
    );
    
    /**
     * @brief Process rounds 1 and 2 (paper lines 3-8)
     * 
     * In rounds 1 and 2, both input and output differences can be freely
     * chosen from the highway table H (pDDT).
     * 
     * Logic from paper:
     * if ((r = 1) ∨ (r = 2)) ∧ (r ≠ n) then
     *     for all (α, β, p) in H do
     *         p_r ← p, B̂_n ← p₁···p_r·B̂_{n-r}
     *         if B̂_n ≥ B_n then
     *             α_r ← α, β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
     *             call threshold_search(n, r+1, H, B̂, B_n, T̂)
     * 
     * @param config Search configuration
     * @param current_round r: Current round (1 or 2)
     * @param current_trail T: Current trail
     * @param result Search result
     */
    static void process_early_rounds(
        const SearchConfig& config,
        int current_round,
        DifferentialTrail& current_trail,
        SearchResult& result
    );
    
    /**
     * @brief Process intermediate rounds 3 to (n-1) (paper lines 10-21)
     * 
     * Core highways/country roads strategy:
     * 
     * 1. Compute input difference: α_r = α_{r-2} + β_{r-1}
     * 2. Build country roads table C:
     *    C = {(α_r, β_r, p_r) : p_r ≥ p_{r,min} ∧ (α_{r-1} + β_r = γ ∈ H)}
     * 3. If C = ∅, find maximum probability country road
     * 4. Search both highways H and country roads C
     * 
     * Logic from paper:
     * if (r > 2) ∧ (r ≠ n) then
     *     α_r ← (α_{r-2} + β_{r-1}); p_{r,min} ← B_n/(p₁p₂···p_{r-1}·B̂_{n-r})
     *     C ← ∅
     *     for all β_r : (p_r(α_r → β_r) ≥ p_{r,min}) ∧ ((α_{r-1} + β_r) = γ ∈ H) do
     *         add (α_r, β_r, p_r) to C
     *     if C = ∅ then
     *         (β_r, p_r) ← p_r = max_β p(α_r → β)
     *         add (α_r, β_r, p_r) to C
     *     for all (α, β, p) : α = α_r in H and all (α, β, p) ∈ C do
     *         p_r ← p, B̂_n ← p₁p₂...p_r·B̂_{n-r}
     *         if B̂_n ≥ B_n then
     *             β_r ← β, add T̂_r ← (α_r, β_r, p_r) to T̂
     *             call threshold_search(n, r+1, H, B̂, B_n, T̂)
     * 
     * @param config Search configuration
     * @param current_round r: Current round (3 ≤ r < n)
     * @param current_trail T: Current trail
     * @param result Search result
     */
    static void process_intermediate_rounds(
        const SearchConfig& config,
        int current_round,
        DifferentialTrail& current_trail,
        SearchResult& result
    );
    
    /**
     * @brief Process final round n (paper lines 23-36)
     * 
     * Final round processing:
     * 1. Compute input difference: α_n = α_{n-2} + β_{n-1}
     * 2. If α_n ∈ H, select max probability from H
     * 3. Otherwise, compute max probability
     * 4. Update best found trail if better
     * 
     * Logic from paper:
     * if (r = n) then
     *     α_r ← (α_{r-2} + β_{r-1})
     *     if (α_r in H) then
     *         (β_r, p_r) ← p_r = max_{β∈H} p(α_r → β)
     *     else
     *         (β_r, p_r) ← p_r = max_β p(α_r → β)
     *     if p_r ≥ p_thres then
     *         add (α_r, β_r, p_r) to H
     *     p_n ← p_r, B̂_n ← p₁p₂...p_n
     *     if B̂_n ≥ B_n then
     *         α_n ← α_r, β_n ← β, add T̂_n ← (α_n, β_n, p_n) to T̂
     *         B_n ← B̂_n, T ← T̂
     * 
     * @param config Search configuration
     * @param current_round r: Current round (= n)
     * @param current_trail T: Current trail
     * @param result Search result
     */
    static void process_final_round(
        const SearchConfig& config,
        int current_round,
        DifferentialTrail& current_trail,
        SearchResult& result
    );
    
    /**
     * @brief Build country roads table for intermediate round
     * 
     * Constructs C according to paper line 13:
     * C = {(α_r, β_r, p_r) : p_r(α_r → β_r) ≥ p_{r,min} ∧ (α_{r-1} + β_r = γ ∈ H)}
     * 
     * Condition explanation:
     * - p_r ≥ p_{r,min}: Probability sufficient to potentially improve best trail
     * - (α_{r-1} + β_r = γ ∈ H): Next round input leads back to a highway
     * 
     * @param config Search configuration
     * @param alpha_r α_r: Input difference for current round
     * @param alpha_prev α_{r-1}: Input diff from previous round (for condition 2)
     * @param prob_min p_{r,min}: Minimum required probability
     * @param bit_width n: Bit width for differential computation
     * @return Country roads table C
     */
    static CountryRoadsTable build_country_roads_table(
        const SearchConfig& config,
        std::uint32_t alpha_r,
        std::uint32_t alpha_prev,
        double prob_min,
        int bit_width
    );
    
    /**
     * @brief Check if current trail can potentially improve best found
     * 
     * Pruning condition from paper:
     * B̂_n = p₁·p₂·...·p_r·B̂_{n-r} ≥ B_n
     * 
     * If this condition fails, prune this branch
     * 
     * @param current_prob p₁·p₂·...·p_r: Product of probabilities so far
     * @param remaining_estimate B̂_{n-r}: Optimistic estimate for remaining rounds
     * @param target_bound B_n: Current target probability to beat
     * @return True if should continue search, false if should prune
     */
    static bool check_pruning_condition(
        double current_prob,
        double remaining_estimate,
        double target_bound
    );
};

} // namespace neoalz
