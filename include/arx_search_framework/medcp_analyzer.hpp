#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include <utility>
#include <algorithm>
#include <limits>
#include <string>
#include <optional>
#include "neoalzette/neoalzette_core.hpp"

namespace neoalz {

/**
 * MEDCP Analyzer - Maximum Expected Differential Characteristic Probability
 * 
 * This class encapsulates all differential cryptanalysis functionality for NeoAlzette,
 * including Lipmaa-Moriai enumeration, Highway table management, and differential bounds computation.
 */
class MEDCPAnalyzer {
public:
    // ========================================================================
    // Configuration structures
    // ========================================================================
    
    struct DifferentialState {
        std::uint32_t dA, dB;
        
        DifferentialState() = default;
        DifferentialState(std::uint32_t da, std::uint32_t db) : dA(da), dB(db) {}
        
        bool operator==(const DifferentialState& other) const noexcept {
            return dA == other.dA && dB == other.dB;
        }
    };

    struct DifferentialResult {
        int weight;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> best_gamma;
        int weight_cap;
    };

    struct HighwayConfig {
        std::string filename;
        int max_rounds;
        bool precompute;
    };

    // ========================================================================
    // Lipmaa-Moriai differential enumeration
    // ========================================================================
    
    template<typename Yield>
    static void enumerate_lm_gammas_fast(
        std::uint32_t alpha, std::uint32_t beta, int n, int weight_cap, Yield&& yield);

    template<typename Yield>
    static void enumerate_lm_gammas_complete(
        std::uint32_t alpha, std::uint32_t beta, int n, int weight_cap, Yield&& yield);

    // ========================================================================
    // Modular addition by constant differential analysis
    // ========================================================================
    
    struct AddConstResult {
        std::uint32_t gamma;
        int weight;
        bool feasible;
    };
    
    static AddConstResult addconst_best(std::uint32_t alpha, std::uint32_t c, int n) noexcept;
    static int addconst_weight(std::uint32_t alpha, std::uint32_t c, std::uint32_t gamma, int n) noexcept;

    // ========================================================================
    // Highway table management
    // ========================================================================
    
    class HighwayTable {
    public:
        bool load(const std::string& filename);
        bool save(const std::string& filename) const;
        int query(std::uint32_t dA, std::uint32_t dB, int rounds) const;
        void build(int max_rounds);
        size_t size() const noexcept { return table_.size(); }
        bool empty() const noexcept { return table_.empty(); }

    private:
        std::unordered_map<std::uint64_t, int> table_;
        
        std::uint64_t make_key(std::uint32_t dA, std::uint32_t dB, int rounds) const noexcept;
    };

    // ========================================================================
    // Differential bounds computation
    // ========================================================================
    
    class BoundsComputer {
    public:
        // Single round differential bound
        int lb_full(std::uint32_t dA, std::uint32_t dB, int K1, int K2, int n, int weight_cap);
        
        // Multi-round suffix bound
        int suffix_bound(std::uint32_t dA, std::uint32_t dB, int rounds, int weight_cap);
        
        // Combined bound with highway table
        int combined_bound(std::uint32_t dA, std::uint32_t dB, int rounds, 
                          const HighwayTable* highway, int weight_cap);

    private:
        struct CacheEntry {
            std::uint64_t key;
            int bound;
        };
        
        mutable std::unordered_map<std::uint64_t, int> cache_;
        
        std::uint64_t make_cache_key(std::uint32_t dA, std::uint32_t dB, int rounds) const noexcept;
    };

    // ========================================================================
    // Main analysis interface
    // ========================================================================
    
    struct AnalysisConfig {
        int rounds = 4;
        int weight_cap = 25;
        std::uint32_t start_dA = 0;
        std::uint32_t start_dB = 0;
        int K1 = 4, K2 = 4;
        bool use_highway = false;
        std::string highway_file;
        bool use_canonical = true;
    };

    struct AnalysisResult {
        int best_weight;
        DifferentialState best_state;
        std::vector<DifferentialState> trail;
        std::uint64_t nodes_processed;
        std::uint64_t elapsed_ms;
        bool search_complete;
    };

    // Perform complete MEDCP analysis
    static AnalysisResult analyze(const AnalysisConfig& config);

private:
    // Internal helper methods
    static std::pair<std::uint32_t, std::uint32_t> canonical_rotate_pair(std::uint32_t a, std::uint32_t b);
    static std::uint32_t hw32(std::uint32_t x) noexcept { return __builtin_popcount(x); }
    
    // AOP function for differential probability computation
    static std::uint32_t aop(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
    static std::uint32_t psi(std::uint32_t a, std::uint32_t b, std::uint32_t c) noexcept;
};

// ============================================================================
// Template implementations (must be in header)
// ============================================================================

template<typename Yield>
void MEDCPAnalyzer::enumerate_lm_gammas_fast(
    std::uint32_t alpha, std::uint32_t beta, int n, int weight_cap, Yield&& yield) {
    
    if (alpha == 0 && beta == 0) {
        yield(0, 0);
        return;
    }

    // Use Lipmaa-Moriai AOP-based enumeration
    for (std::uint32_t gamma = 0; gamma < (1ULL << std::min(n, 20)); ++gamma) {
        std::uint32_t aop_val = aop(alpha, beta, gamma);
        int weight = hw32(aop_val);
        
        if (weight < weight_cap) {
            // Verify feasibility using ψ function
            std::uint32_t psi_val = psi(alpha, beta, gamma);
            if (psi_val == aop_val) {
                yield(gamma, weight);
            }
        }
    }
}

template<typename Yield>
void MEDCPAnalyzer::enumerate_lm_gammas_complete(
    std::uint32_t alpha, std::uint32_t beta, int n, int weight_cap, Yield&& yield) {
    
    // Complete enumeration using recursive backtracking
    std::function<void(int, std::uint32_t, int)> backtrack = 
        [&](int pos, std::uint32_t gamma_partial, int weight_so_far) {
            
        if (pos == n) {
            if (weight_so_far < weight_cap) {
                // Final feasibility check
                std::uint32_t psi_val = psi(alpha, beta, gamma_partial);
                std::uint32_t aop_val = aop(alpha, beta, gamma_partial);
                if (psi_val == aop_val) {
                    yield(gamma_partial, weight_so_far);
                }
            }
            return;
        }
        
        if (weight_so_far >= weight_cap) return;
        
        // Try gamma[pos] = 0
        backtrack(pos + 1, gamma_partial, weight_so_far);
        
        // Try gamma[pos] = 1 (if it doesn't exceed weight cap)
        std::uint32_t new_gamma = gamma_partial | (1U << pos);
        std::uint32_t partial_aop = aop(alpha, beta, new_gamma);
        int new_weight = weight_so_far + ((partial_aop >> pos) & 1);
        
        if (new_weight < weight_cap) {
            backtrack(pos + 1, new_gamma, new_weight);
        }
    };
    
    backtrack(0, 0, 0);
}

} // namespace neoalz