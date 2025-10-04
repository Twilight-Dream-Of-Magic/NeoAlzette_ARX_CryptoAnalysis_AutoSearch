#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <functional>
#include <cmath>

namespace neoalz {

/**
 * @brief Complete implementation of Algorithm 1 for pDDT construction
 * 
 * This implementation faithfully follows the algorithm described in:
 * "Automatic Search for Differential Trails in ARX Ciphers" by Biryukov & Velichkov
 * Section 4: Partial Difference Distribution Tables
 * 
 * Mathematical Foundation:
 * ========================
 * 
 * 1. XOR Differential Probability of Modular Addition (xdp⁺):
 *    
 *    xdp⁺(α, β → γ) = 2^{-2n} · |{(x,y) : ((x⊕α)+(y⊕β))⊕(x+y) = γ}|
 *    
 *    where:
 *    - α, β: Input XOR differences
 *    - γ: Output XOR difference
 *    - n: Bit width
 *    - +: Modular addition mod 2^n
 *    - ⊕: Bitwise XOR
 * 
 * 2. Lipmaa-Moriai Formula (Efficient Computation):
 *    
 *    xdp⁺(α, β → γ) = 2^{-w} where w = hw(AOP(α, β, γ))
 *    
 *    AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
 *    
 *    where:
 *    - AOP: All Output Positions (positions where carry can occur)
 *    - hw: Hamming weight (popcount)
 *    - ∧: Bitwise AND
 *    - <<: Left shift
 * 
 * 3. Monotonicity Property (Proposition 1):
 *    
 *    For k-bit prefixes: p_n ≤ ... ≤ p_k ≤ p_{k-1} ≤ ... ≤ p_1 ≤ p_0 = 1
 *    
 *    where p_k = DP(α_k, β_k → γ_k) for k LSBs
 *    
 *    This enables efficient recursive construction with early pruning.
 * 
 * 4. Partial DDT Definition (Definition 4):
 *    
 *    D = {(α, β, γ, p) : DP(α, β → γ) ≥ p_thres}
 *    
 *    Contains only high-probability differentials (≥ threshold)
 * 
 * Algorithm 1 Pseudocode (from paper):
 * =====================================
 * 
 * procedure compute_pddt(n, p_thres, k, p_k, α_k, β_k, γ_k) do
 *     if n = k then
 *         Add (α, β, γ) ← (α_k, β_k, γ_k) to D
 *         return
 *     for x, y, z ∈ {0, 1} do
 *         α_{k+1} ← x|α_k, β_{k+1} ← y|β_k, γ_{k+1} ← z|γ_k
 *         p_{k+1} = DP(α_{k+1}, β_{k+1} → γ_{k+1})
 *         if p_{k+1} ≥ p_thres then
 *             compute_pddt(n, p_thres, k+1, p_{k+1}, α_{k+1}, β_{k+1}, γ_{k+1})
 * 
 * Initial call: compute_pddt(n, p_thres, 0, 1, ∅, ∅, ∅)
 * 
 * Key Insight:
 * ============
 * 
 * The monotonicity property allows branch-and-bound pruning:
 * - If p_k < p_thres at bit k, all extensions will also have p < p_thres
 * - This dramatically reduces the search space from 2^{3n} to manageable size
 * 
 * Complexity:
 * ===========
 * 
 * - Worst case: O(2^{3n}) if p_thres = 0
 * - Practical: O(poly(n)) for reasonable thresholds (e.g., p_thres ≥ 2^{-10})
 * - Memory: O(|D|) where |D| is number of differentials ≥ p_thres
 */
class PDDTAlgorithm1Complete {
public:
    // ========================================================================
    // Core data structures
    // ========================================================================
    
    /**
     * @brief Triple representing a differential (α, β → γ) with its weight
     * 
     * Mathematical notation:
     * - α (alpha): First input difference
     * - β (beta): Second input difference
     * - γ (gamma): Output difference
     * - w (weight): -log₂(p) where p = DP(α, β → γ)
     */
    struct PDDTTriple {
        std::uint32_t alpha;    ///< α: First input XOR difference
        std::uint32_t beta;     ///< β: Second input XOR difference
        std::uint32_t gamma;    ///< γ: Output XOR difference
        int weight;             ///< w = -log₂(DP(α, β → γ))
        
        PDDTTriple() = default;
        PDDTTriple(std::uint32_t a, std::uint32_t b, std::uint32_t g, int w)
            : alpha(a), beta(b), gamma(g), weight(w) {}
        
        /**
         * @brief Compute probability from weight
         * @return p = 2^{-w}
         */
        double probability() const {
            return std::pow(2.0, -static_cast<double>(weight));
        }
    };
    
    /**
     * @brief Configuration for pDDT construction
     * 
     * Parameters from paper Algorithm 1:
     * - n: Bit width
     * - p_thres: Probability threshold
     * - w_thresh: Weight threshold (= -log₂(p_thres))
     */
    struct PDDTConfig {
        int bit_width;          ///< n: Number of bits (typically 32)
        double prob_threshold;  ///< p_thres: Minimum probability to include
        int weight_threshold;   ///< w_thresh: Maximum weight (= -log₂(p_thres))
        bool enable_pruning;    ///< Enable early pruning optimization
        
        PDDTConfig()
            : bit_width(32)
            , prob_threshold(0.01)  // 2^{-6.64}
            , weight_threshold(7)    // -log₂(0.01) ≈ 6.64
            , enable_pruning(true) {}
        
        /**
         * @brief Set threshold by probability
         * @param p p_thres: Probability threshold
         */
        void set_probability_threshold(double p) {
            prob_threshold = p;
            weight_threshold = static_cast<int>(-std::log2(p));
        }
        
        /**
         * @brief Set threshold by weight
         * @param w w_thresh: Weight threshold
         */
        void set_weight_threshold(int w) {
            weight_threshold = w;
            prob_threshold = std::pow(2.0, -static_cast<double>(w));
        }
    };
    
    /**
     * @brief Statistics for pDDT construction
     */
    struct PDDTStats {
        std::size_t total_entries;      ///< |D|: Number of differentials in pDDT
        std::uint64_t nodes_explored;   ///< Total recursive calls
        std::uint64_t nodes_pruned;     ///< Nodes pruned by threshold
        int min_weight;                 ///< Minimum weight in pDDT
        int max_weight;                 ///< Maximum weight in pDDT
        double avg_weight;              ///< Average weight
        double elapsed_seconds;         ///< Construction time
        
        PDDTStats() 
            : total_entries(0), nodes_explored(0), nodes_pruned(0)
            , min_weight(std::numeric_limits<int>::max())
            , max_weight(0), avg_weight(0.0), elapsed_seconds(0.0) {}
    };
    
    // ========================================================================
    // Main Algorithm 1 Implementation
    // ========================================================================
    
    /**
     * @brief Compute partial DDT using Algorithm 1
     * 
     * Paper Algorithm 1: compute_pddt(n, p_thres, k, p_k, α_k, β_k, γ_k)
     * 
     * Recursively builds all differentials (α, β → γ) such that
     * DP(α, β → γ) ≥ p_thres using monotonicity property for pruning.
     * 
     * @param config Configuration with n, p_thres, etc.
     * @return Vector of PDDTTriple containing pDDT D
     * 
     * Time complexity: O(|D| · n) where |D| = |{(α,β,γ) : DP ≥ p_thres}|
     * Space complexity: O(|D|)
     */
    static std::vector<PDDTTriple> compute_pddt(const PDDTConfig& config);
    
    /**
     * @brief Compute pDDT with statistics tracking
     * 
     * Same as compute_pddt but also returns construction statistics
     * 
     * @param config Configuration
     * @param stats Output statistics
     * @return Vector of PDDTTriple
     */
    static std::vector<PDDTTriple> compute_pddt_with_stats(
        const PDDTConfig& config, 
        PDDTStats& stats
    );
    
    // ========================================================================
    // Optimized variant using structural constraints (Appendix D.4)
    // ========================================================================
    
    /**
     * @brief Compute pDDT with structural constraints for efficiency
     * 
     * From paper Appendix D.4: "Improving the efficiency of Algorithm 1"
     * 
     * For specific ARX structures (e.g., TEA F-function), we can exploit
     * dependencies between α, β, γ to reduce search space.
     * 
     * Example constraint for TEA-like operations:
     * - β = α ≪ r₁ (rotation by r₁)
     * - γ ∈ {α ≫ r₂, α ≫ r₂ ± 1, α ≫ r₂ ± 2^{n-r₂}, ...}
     * 
     * This reduces enumeration from O(2^{3n}) to O(2^n · k) for small k.
     * 
     * Trade-off:
     * - Pro: 10-1000x faster construction
     * - Con: May miss some differentials not satisfying constraints
     * 
     * @param config Configuration
     * @param rotation_constraint Rotation amount for β = α ≪ r
     * @return Vector of PDDTTriple (incomplete pDDT)
     */
    static std::vector<PDDTTriple> compute_pddt_with_constraints(
        const PDDTConfig& config,
        int rotation_constraint = 0
    );
    
    // ========================================================================
    // Helper functions for differential probability computation
    // ========================================================================
    
    /**
     * @brief Compute Lipmaa-Moriai weight for k-bit prefix
     * 
     * For k LSBs of (α, β, γ), compute:
     * w_k = hw(AOP(α_k, β_k, γ_k))
     * 
     * where α_k = α[k-1:0] (k least significant bits)
     * 
     * Mathematical formula:
     * AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
     * 
     * @param alpha_k α_k: k-bit prefix of α
     * @param beta_k β_k: k-bit prefix of β
     * @param gamma_k γ_k: k-bit prefix of γ
     * @param k Number of bits
     * @return w_k: Weight for k-bit prefix, or nullopt if infeasible
     */
    static std::optional<int> compute_lm_weight(
        std::uint32_t alpha_k,
        std::uint32_t beta_k,
        std::uint32_t gamma_k,
        int k
    );
    
    /**
     * @brief Check if k-bit prefix is feasible
     * 
     * Uses necessary conditions from Lipmaa-Moriai to detect impossible
     * differentials early (before computing full weight).
     * 
     * Necessary condition:
     * eq(α[i], β[i], γ[i]) must be consistent with carry propagation
     * where eq(a,b,c) = 1 iff a = b = c
     * 
     * @param alpha_k α_k: k-bit prefix
     * @param beta_k β_k: k-bit prefix
     * @param gamma_k γ_k: k-bit prefix
     * @param k Number of bits
     * @return True if definitely impossible (can prune)
     */
    static bool check_prefix_impossible(
        std::uint32_t alpha_k,
        std::uint32_t beta_k,
        std::uint32_t gamma_k,
        int k
    );
    
    /**
     * @brief Compute full xdp⁺ probability (for verification)
     * 
     * xdp⁺(α, β → γ) = 2^{-2n} · |{(x,y) : ((x⊕α)+(y⊕β))⊕(x+y) = γ}|
     * 
     * Uses exact counting for small n (≤ 16), Lipmaa-Moriai for larger n
     * 
     * @param alpha α: First input difference
     * @param beta β: Second input difference
     * @param gamma γ: Output difference
     * @param n Bit width
     * @return Exact probability DP(α, β → γ)
     */
    static double compute_xdp_add_exact(
        std::uint32_t alpha,
        std::uint32_t beta,
        std::uint32_t gamma,
        int n
    );
    
    /**
     * @brief Convert probability to weight
     * 
     * w = -log₂(p)
     * 
     * @param probability p
     * @return w = ⌈-log₂(p)⌉
     */
    static int probability_to_weight(double probability) {
        if (probability <= 0.0) return std::numeric_limits<int>::max();
        return static_cast<int>(std::ceil(-std::log2(probability)));
    }
    
    /**
     * @brief Convert weight to probability
     * 
     * p = 2^{-w}
     * 
     * @param weight w
     * @return p = 2^{-w}
     */
    static double weight_to_probability(int weight) {
        return std::pow(2.0, -static_cast<double>(weight));
    }

private:
    // ========================================================================
    // Internal recursive implementation
    // ========================================================================
    
    /**
     * @brief Recursive pDDT construction (Algorithm 1 lines 1-9)
     * 
     * Paper pseudocode:
     * procedure compute_pddt(n, p_thres, k, p_k, α_k, β_k, γ_k) do
     *     if n = k then
     *         Add (α, β, γ) ← (α_k, β_k, γ_k) to D
     *         return
     *     for x, y, z ∈ {0, 1} do
     *         α_{k+1} ← x|α_k, β_{k+1} ← y|β_k, γ_{k+1} ← z|γ_k
     *         p_{k+1} = DP(α_{k+1}, β_{k+1} → γ_{k+1})
     *         if p_{k+1} ≥ p_thres then
     *             compute_pddt(n, p_thres, k+1, p_{k+1}, α_{k+1}, β_{k+1}, γ_{k+1})
     * 
     * @param config Configuration
     * @param k Current bit position (0 to n-1)
     * @param alpha_k α_k: k-bit prefix of α
     * @param beta_k β_k: k-bit prefix of β
     * @param gamma_k γ_k: k-bit prefix of γ
     * @param output Vector to collect results
     * @param stats Statistics tracker
     */
    static void pddt_recursive(
        const PDDTConfig& config,
        int k,
        std::uint32_t alpha_k,
        std::uint32_t beta_k,
        std::uint32_t gamma_k,
        std::vector<PDDTTriple>& output,
        PDDTStats& stats
    );
    
    /**
     * @brief Enumerate differentials with rotation constraint
     * 
     * For β = α ≪ r, only enumerate α and valid γ values
     * 
     * @param config Configuration
     * @param rotation_r Rotation amount r
     * @param output Vector to collect results
     * @param stats Statistics tracker
     */
    static void pddt_with_rotation_constraint(
        const PDDTConfig& config,
        int rotation_r,
        std::vector<PDDTTriple>& output,
        PDDTStats& stats
    );
    
    /**
     * @brief Compute AOP (All Output Positions) function
     * 
     * AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
     * 
     * This function characterizes all bit positions where carry can occur
     * in the differential (α, β → γ) through modular addition.
     * 
     * @param alpha α
     * @param beta β
     * @param gamma γ
     * @return AOP value
     */
    static std::uint32_t compute_aop(
        std::uint32_t alpha,
        std::uint32_t beta,
        std::uint32_t gamma
    );
};

} // namespace neoalz
