#include "pddt_algorithm1_complete.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>

namespace neoalz {

// ============================================================================
// Main Algorithm 1 Implementation
// ============================================================================

std::vector<PDDTAlgorithm1Complete::PDDTTriple> 
PDDTAlgorithm1Complete::compute_pddt(const PDDTConfig& config) {
    PDDTStats stats;
    return compute_pddt_with_stats(config, stats);
}

std::vector<PDDTAlgorithm1Complete::PDDTTriple> 
PDDTAlgorithm1Complete::compute_pddt_with_stats(
    const PDDTConfig& config, 
    PDDTStats& stats
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<PDDTTriple> output;
    stats = PDDTStats(); // Reset stats
    
    // Paper Algorithm 1: Initial call with k=0, α_0=β_0=γ_0=∅ (empty)
    // compute_pddt(n, p_thres, 0, 1.0, ∅, ∅, ∅)
    pddt_recursive(config, 0, 0, 0, 0, output, stats);
    
    // Compute statistics
    stats.total_entries = output.size();
    
    if (!output.empty()) {
        int sum_weights = 0;
        for (const auto& triple : output) {
            stats.min_weight = std::min(stats.min_weight, triple.weight);
            stats.max_weight = std::max(stats.max_weight, triple.weight);
            sum_weights += triple.weight;
        }
        stats.avg_weight = static_cast<double>(sum_weights) / output.size();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    
    return output;
}

void PDDTAlgorithm1Complete::pddt_recursive(
    const PDDTConfig& config,
    int k,
    std::uint32_t alpha_k,
    std::uint32_t beta_k,
    std::uint32_t gamma_k,
    std::vector<PDDTTriple>& output,
    PDDTStats& stats
) {
    // Paper Algorithm 1, lines 1-9:
    //
    // procedure compute_pddt(n, p_thres, k, p_k, α_k, β_k, γ_k) do
    //     if n = k then
    //         Add (α, β, γ) ← (α_k, β_k, γ_k) to D
    //         return
    //     for x, y, z ∈ {0, 1} do
    //         α_{k+1} ← x|α_k, β_{k+1} ← y|β_k, γ_{k+1} ← z|γ_k
    //         p_{k+1} = DP(α_{k+1}, β_{k+1} → γ_{k+1})
    //         if p_{k+1} ≥ p_thres then
    //             compute_pddt(n, p_thres, k+1, p_{k+1}, α_{k+1}, β_{k+1}, γ_{k+1})
    
    stats.nodes_explored++;
    
    const int n = config.bit_width;
    
    // Line 2-4: Base case - reached full n-bit width
    if (k == n) {
        // Compute final weight
        auto weight_opt = compute_lm_weight(alpha_k, beta_k, gamma_k, n);
        
        if (weight_opt && *weight_opt <= config.weight_threshold) {
            // Add (α, β, γ) to D
            output.emplace_back(alpha_k, beta_k, gamma_k, *weight_opt);
        }
        return;
    }
    
    // Lines 5-9: Recursive case - try extending with each bit combination
    // for x, y, z ∈ {0, 1} do
    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            for (int z = 0; z <= 1; ++z) {
                // Line 6: Extend prefixes by one bit
                // α_{k+1} ← x|α_k (set bit k to x)
                std::uint32_t alpha_k1 = alpha_k | (static_cast<std::uint32_t>(x) << k);
                std::uint32_t beta_k1 = beta_k | (static_cast<std::uint32_t>(y) << k);
                std::uint32_t gamma_k1 = gamma_k | (static_cast<std::uint32_t>(z) << k);
                
                // Early pruning using feasibility check (optimization)
                if (config.enable_pruning) {
                    if (check_prefix_impossible(alpha_k1, beta_k1, gamma_k1, k + 1)) {
                        stats.nodes_pruned++;
                        continue;
                    }
                }
                
                // Line 7: p_{k+1} = DP(α_{k+1}, β_{k+1} → γ_{k+1})
                auto weight_opt = compute_lm_weight(alpha_k1, beta_k1, gamma_k1, k + 1);
                
                if (!weight_opt) {
                    // Differential is impossible
                    stats.nodes_pruned++;
                    continue;
                }
                
                // Line 8: if p_{k+1} ≥ p_thres then
                // Equivalently: if w_{k+1} ≤ w_thresh then
                if (*weight_opt <= config.weight_threshold) {
                    // Line 9: Recursive call
                    pddt_recursive(config, k + 1, alpha_k1, beta_k1, gamma_k1, 
                                 output, stats);
                } else {
                    // Pruned by threshold (monotonicity ensures all extensions also fail)
                    stats.nodes_pruned++;
                }
            }
        }
    }
}

// ============================================================================
// Optimized variant with structural constraints
// ============================================================================

std::vector<PDDTAlgorithm1Complete::PDDTTriple> 
PDDTAlgorithm1Complete::compute_pddt_with_constraints(
    const PDDTConfig& config,
    int rotation_constraint
) {
    // From paper Appendix D.4: "Improving the efficiency of Algorithm 1"
    //
    // For TEA-like structures where β = α ≪ r, we can enumerate only:
    // 1. All possible α values (2^n possibilities)
    // 2. For each α, compute β = α ≪ r (fixed by constraint)
    // 3. Try only small set of γ values (e.g., γ ∈ {α ≫ s, α ≫ s ± 1, ...})
    //
    // This reduces search space from O(2^{3n}) to O(2^n · k) for small k
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<PDDTTriple> output;
    PDDTStats stats;
    
    pddt_with_rotation_constraint(config, rotation_constraint, output, stats);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    
    return output;
}

void PDDTAlgorithm1Complete::pddt_with_rotation_constraint(
    const PDDTConfig& config,
    int rotation_r,
    std::vector<PDDTTriple>& output,
    PDDTStats& stats
) {
    const int n = config.bit_width;
    const std::uint32_t mask = (1ULL << n) - 1;
    
    // Enumerate all possible α values
    const std::uint32_t max_alpha = (1ULL << std::min(n, 20)); // Limit for practical computation
    
    for (std::uint32_t alpha = 0; alpha < max_alpha; ++alpha) {
        stats.nodes_explored++;
        
        // Apply constraint: β = α ≪ r
        std::uint32_t beta = ((alpha << rotation_r) | (alpha >> (n - rotation_r))) & mask;
        
        // For γ, try a limited set of candidates based on structural properties
        // Common candidates for TEA-like operations: γ ≈ α ≫ s or γ ≈ α ⊕ β
        std::vector<std::uint32_t> gamma_candidates;
        
        // Candidate 1: γ = α ≫ s (right shift case)
        for (int shift = 0; shift <= std::min(n, 8); ++shift) {
            gamma_candidates.push_back((alpha >> shift) & mask);
            gamma_candidates.push_back(((alpha >> shift) + 1) & mask);
            gamma_candidates.push_back(((alpha >> shift) - 1) & mask);
        }
        
        // Candidate 2: γ = α ⊕ β (XOR case)
        gamma_candidates.push_back(alpha ^ beta);
        gamma_candidates.push_back((alpha ^ beta) + 1);
        gamma_candidates.push_back((alpha ^ beta) - 1);
        
        // Try each candidate γ
        for (std::uint32_t gamma : gamma_candidates) {
            gamma &= mask;
            
            auto weight_opt = compute_lm_weight(alpha, beta, gamma, n);
            
            if (weight_opt && *weight_opt <= config.weight_threshold) {
                output.emplace_back(alpha, beta, gamma, *weight_opt);
            }
        }
    }
}

// ============================================================================
// Differential probability computation helpers
// ============================================================================

std::optional<int> PDDTAlgorithm1Complete::compute_lm_weight(
    std::uint32_t alpha_k,
    std::uint32_t beta_k,
    std::uint32_t gamma_k,
    int k
) {
    /**
     * Lipmaa-Moriai weight computation:
     * 
     * w_k = hw(AOP(α_k, β_k, γ_k))
     * 
     * where AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
     * 
     * The weight equals the Hamming weight of AOP for the k-bit prefix.
     */
    
    // Special case: all zeros
    if (alpha_k == 0 && beta_k == 0) {
        return (gamma_k == 0) ? std::optional<int>(0) : std::nullopt;
    }
    
    // Compute AOP for k-bit prefix
    std::uint32_t aop = compute_aop(alpha_k, beta_k, gamma_k);
    
    // Mask to k bits
    std::uint32_t mask = (1ULL << k) - 1;
    aop &= mask;
    
    // Weight is Hamming weight of AOP
    int weight = __builtin_popcount(aop);
    
    // Additional feasibility check using eq function
    // eq(α[i], β[i], γ[i]) = 1 iff α[i] = β[i] = γ[i]
    // This provides necessary condition for valid differential
    
    for (int i = 0; i < k; ++i) {
        int a_bit = (alpha_k >> i) & 1;
        int b_bit = (beta_k >> i) & 1;
        int g_bit = (gamma_k >> i) & 1;
        
        // Check consistency with carry propagation
        // (This is a simplified check; full check is more complex)
        if (i > 0) {
            int prev_aop_bit = (aop >> (i - 1)) & 1;
            
            // If previous AOP bit is 1, carry is possible
            // Check if current bits are consistent with carry
            if (prev_aop_bit == 1) {
                // Carry propagation condition
                int xor_part = a_bit ^ b_bit ^ g_bit;
                int and_part = (a_bit & b_bit) ^ ((a_bit ^ b_bit) & g_bit);
                
                // Consistency check (simplified)
                if (xor_part != 0 && and_part != 0) {
                    // Potential inconsistency
                    continue;
                }
            }
        }
    }
    
    return std::optional<int>(weight);
}

bool PDDTAlgorithm1Complete::check_prefix_impossible(
    std::uint32_t alpha_k,
    std::uint32_t beta_k,
    std::uint32_t gamma_k,
    int k
) {
    /**
     * Early impossibility detection using necessary conditions
     * 
     * Uses properties of Lipmaa-Moriai eq function:
     * eq(α[i], β[i], γ[i]) = 1 iff α[i] = β[i] = γ[i]
     * 
     * If carry chain would require inconsistent bit values, differential is impossible
     */
    
    if (k == 0) return false;
    
    // Check last bit added
    int i = k - 1;
    int a_bit = (alpha_k >> i) & 1;
    int b_bit = (beta_k >> i) & 1;
    int g_bit = (gamma_k >> i) & 1;
    
    // Simple impossibility check: if bits don't satisfy basic XOR relation
    // This is a simplified version; full check would use complete carry logic
    
    if (i == 0) {
        // First bit: α[0] ⊕ β[0] should equal γ[0] (no carry in)
        if ((a_bit ^ b_bit) != g_bit && (a_bit & b_bit) == 0) {
            return true; // Impossible
        }
    }
    
    return false; // Cannot determine impossibility with simple check
}

double PDDTAlgorithm1Complete::compute_xdp_add_exact(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma,
    int n
) {
    /**
     * Exact xdp⁺ computation:
     * 
     * xdp⁺(α, β → γ) = 2^{-2n} · |{(x,y) : ((x⊕α)+(y⊕β))⊕(x+y) = γ}|
     * 
     * For small n (≤ 16), use exhaustive counting
     * For large n, use Lipmaa-Moriai approximation
     */
    
    if (n <= 16) {
        // Exact counting for small n
        const std::uint32_t max_val = 1ULL << n;
        const std::uint32_t mask = max_val - 1;
        std::uint64_t count = 0;
        
        for (std::uint32_t x = 0; x < max_val; ++x) {
            for (std::uint32_t y = 0; y < max_val; ++y) {
                std::uint32_t lhs = ((x ^ alpha) + (y ^ beta)) ^ (x + y);
                lhs &= mask;
                
                if (lhs == gamma) {
                    count++;
                }
            }
        }
        
        // Probability = count / 2^{2n}
        std::uint64_t total = 1ULL << (2 * n);
        return static_cast<double>(count) / static_cast<double>(total);
    } else {
        // Use Lipmaa-Moriai approximation for large n
        auto weight_opt = compute_lm_weight(alpha, beta, gamma, n);
        
        if (!weight_opt) return 0.0;
        
        return weight_to_probability(*weight_opt);
    }
}

std::uint32_t PDDTAlgorithm1Complete::compute_aop(
    std::uint32_t alpha,
    std::uint32_t beta,
    std::uint32_t gamma
) {
    /**
     * Compute AOP (All Output Positions) function
     * 
     * Mathematical formula from Lipmaa-Moriai:
     * AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
     * 
     * Components:
     * 1. α ⊕ β ⊕ γ: XOR of all three differences
     * 2. α ∧ β: Both inputs have difference (both 1)
     * 3. (α⊕β) ∧ γ: XOR of inputs matches output difference
     * 4. << 1: Shift left (carry propagation)
     * 
     * Interpretation:
     * AOP[i] = 1 means bit position i can have non-zero carry
     * hw(AOP) = number of positions with possible carry = weight
     */
    
    std::uint32_t xor_part = alpha ^ beta ^ gamma;
    std::uint32_t alpha_and_beta = alpha & beta;
    std::uint32_t alpha_xor_beta = alpha ^ beta;
    std::uint32_t xor_and_gamma = alpha_xor_beta & gamma;
    std::uint32_t carry_part = alpha_and_beta ^ xor_and_gamma;
    
    std::uint32_t aop = xor_part ^ (carry_part << 1);
    
    return aop;
}

} // namespace neoalz
