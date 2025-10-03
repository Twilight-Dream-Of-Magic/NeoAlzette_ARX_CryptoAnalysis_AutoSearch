#pragma once
/*
 * Algorithm 1 Optimized Version Implementation
 * 
 * Based on "Automatic Search for Differential Trails in ARX Ciphers", Appendix D.4:
 * "Improving the efficiency of Algorithm 1"
 *
 * Key optimization: exploit strong dependencies between XOR operation inputs
 * for specific ARX structures (e.g., TEA, XTEA)
 *
 * Trade-off: Significant efficiency gain vs slightly incomplete pDDT
 */

#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include "pddt.hpp"
#include "neoalzette.hpp"

namespace neoalz {

// Optimized pDDT construction for specific ARX structures
class PDDTOptimized {
public:
    enum StructureType {
        TEA_LIKE,       // TEA/XTEA with LSH+RSH+XOR pattern
        SPECK_LIKE,     // SPECK with rotation+ADD pattern  
        GENERAL_ARX     // Generic ARX without specific constraints
    };
    
    struct OptConfig {
        int n = 32;
        int w_thresh = 20;
        StructureType structure = GENERAL_ARX;
        bool allow_incomplete = true;  // Allow missing some differentials for efficiency
    };

private:
    OptConfig config_;
    
public:
    explicit PDDTOptimized(OptConfig cfg = {}) : config_(cfg) {}
    
    // Main computation with structure-specific optimizations
    std::vector<PDDTTriple> compute_optimized() const {
        switch (config_.structure) {
            case TEA_LIKE:
                return compute_tea_optimized();
            case SPECK_LIKE:
                return compute_speck_optimized();
            default:
                return compute_general_optimized();
        }
    }

private:
    // Algorithm 1 optimization for TEA-like structures
    // Based on paper's Appendix D.4 constraint analysis
    std::vector<PDDTTriple> compute_tea_optimized() const {
        std::vector<PDDTTriple> out;
        out.reserve(1 << 18);  // Estimate based on constraints
        
        // Paper's constraint equation (37):
        // (α, β, γ) : (β = (α ≪ 4)) ∧ 
        //            (γ ∈ {(α ≫ 5), (α ≫ 5) + 1, (α ≫ 5) − 2^{n-5}, (α ≫ 5) − 2^{n-5} + 1})
        
        const int n = config_.n;
        const uint32_t mask = (n == 32) ? 0xFFFFFFFF : ((1U << n) - 1);
        
        for (uint32_t alpha = 1; alpha <= mask; ++alpha) {  // Skip alpha=0 (trivial)
            // Constraint: β = (α ≪ 4)
            uint32_t beta = rotl(alpha, 4) & mask;
            
            // Constraint: γ ∈ {limited set of 4 values}
            std::vector<uint32_t> gamma_candidates = {
                rotr(alpha, 5) & mask,
                (rotr(alpha, 5) + 1) & mask,
                (rotr(alpha, 5) - (1U << (n - 5))) & mask,
                (rotr(alpha, 5) - (1U << (n - 5)) + 1) & mask
            };
            
            for (uint32_t gamma : gamma_candidates) {
                auto weight = detail::lm_weight(alpha, beta, gamma, n);
                if (weight && *weight <= config_.w_thresh) {
                    out.push_back({alpha, beta, gamma, *weight});
                }
            }
        }
        
        return out;
    }
    
    // Optimization for SPECK-like structures
    std::vector<PDDTTriple> compute_speck_optimized() const {
        std::vector<PDDTTriple> out;
        
        // For SPECK: x = (x + (y ≪ r1)) ⊕ k
        // The rotation and key addition create specific constraints
        
        const int n = config_.n;
        const uint32_t mask = (n == 32) ? 0xFFFFFFFF : ((1U << n) - 1);
        
        // Enumerate with SPECK-specific constraints
        for (uint32_t alpha = 1; alpha <= (mask >> 4); ++alpha) {  // Reduce search space
            for (uint32_t beta = 1; beta <= (mask >> 4); ++beta) {
                // For SPECK, many gamma values are constrained by the structure
                std::vector<uint32_t> gamma_candidates;
                
                // Add most likely gamma values based on SPECK's round function
                gamma_candidates.push_back(alpha ^ beta);  // XOR case
                gamma_candidates.push_back(alpha ^ rotl(beta, 8));   // Common rotation
                gamma_candidates.push_back(alpha ^ rotl(beta, 16));  // Half rotation
                
                for (uint32_t gamma : gamma_candidates) {
                    gamma &= mask;
                    auto weight = detail::lm_weight(alpha, beta, gamma, n);
                    if (weight && *weight <= config_.w_thresh) {
                        out.push_back({alpha, beta, gamma, *weight});
                    }
                }
            }
        }
        
        return out;
    }
    
    // General optimization: use statistical sampling instead of exhaustive enumeration
    std::vector<PDDTTriple> compute_general_optimized() const {
        std::vector<PDDTTriple> out;
        
        // Statistical sampling approach for general ARX
        const int samples_per_alpha = 1000;  // Much less than 2^32
        
        for (uint32_t alpha = 1; alpha < (1U << 16); ++alpha) {  // Reduced alpha space
            // Sample beta values instead of exhaustive enumeration
            for (int sample = 0; sample < samples_per_alpha; ++sample) {
                uint32_t beta = fast_random() & ((1U << 16) - 1);
                
                // For each (alpha, beta), try most promising gamma values
                std::vector<uint32_t> gamma_candidates = {
                    alpha ^ beta,                    // XOR difference
                    alpha + beta,                    // ADD difference (mod mask)
                    rotl(alpha ^ beta, 8),          // Rotated XOR
                    rotr(alpha + beta, 8)           // Rotated ADD
                };
                
                for (uint32_t gamma : gamma_candidates) {
                    if (gamma == 0) continue;  // Skip trivial
                    gamma &= ((1U << config_.n) - 1);
                    
                    auto weight = detail::lm_weight(alpha, beta, gamma, config_.n);
                    if (weight && *weight <= config_.w_thresh) {
                        out.push_back({alpha, beta, gamma, *weight});
                    }
                }
            }
        }
        
        // Remove duplicates and sort by weight
        std::sort(out.begin(), out.end(), 
                  [](const PDDTTriple& a, const PDDTTriple& b) {
                      return a.weight < b.weight;
                  });
        out.erase(std::unique(out.begin(), out.end(), 
                              [](const PDDTTriple& a, const PDDTTriple& b) {
                                  return a.alpha == b.alpha && a.beta == b.beta && a.gamma == b.gamma;
                              }), out.end());
        
        return out;
    }
    
    static uint32_t fast_random() {
        // Simple fast PRNG for sampling
        static uint32_t state = 1;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

// Factory function for choosing appropriate algorithm version
class PDDTFactory {
public:
    static std::vector<PDDTTriple> create_pddt(
        PDDTOptimized::OptConfig config,
        bool use_paper_optimization = false) {
        
        if (use_paper_optimization) {
            // Use paper's Appendix D.4 optimization
            PDDTOptimized optimizer(config);
            return optimizer.compute_optimized();
        } else {
            // Use standard Algorithm 1 from paper
            PDDTConfig standard_config;
            standard_config.n = config.n;
            standard_config.w_thresh = config.w_thresh;
            
            PDDTAdder standard_generator(standard_config);
            return standard_generator.compute();
        }
    }
};

} // namespace neoalz