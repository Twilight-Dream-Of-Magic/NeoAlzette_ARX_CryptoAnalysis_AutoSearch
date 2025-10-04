#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include <utility>
#include <algorithm>
#include <limits>
#include <array>
#include <string>
#include <optional>
#include "neoalzette_core.hpp"

namespace neoalz {

/**
 * MELCC Analyzer - Maximum Expected Linear Characteristic Correlation
 * 
 * This class encapsulates all linear cryptanalysis functionality for NeoAlzette,
 * including Wallén enumeration, linear Highway tables, and linear bounds computation.
 */
class MELCCAnalyzer {
public:
    // ========================================================================
    // Configuration structures
    // ========================================================================
    
    struct LinearState {
        std::uint32_t mA, mB;
        
        LinearState() = default;
        LinearState(std::uint32_t ma, std::uint32_t mb) : mA(ma), mB(mb) {}
        
        bool operator==(const LinearState& other) const noexcept {
            return mA == other.mA && mB == other.mB;
        }
    };

    struct LinearResult {
        int weight;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> best_omega;
        int weight_cap;
    };

    // ========================================================================
    // Wallén linear approximation enumeration
    // ========================================================================
    
    template<typename Yield>
    static void enumerate_wallen_omegas(
        std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield);

    template<typename Yield>  
    static void enumerate_wallen_omegas_optimized(
        std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield);

    // ========================================================================
    // Linear mask backward propagation
    // ========================================================================
    
    // Exact backtranspose for L1 (computed via Gauss-Jordan elimination)
    static constexpr std::uint32_t l1_backtranspose_exact(std::uint32_t x) noexcept;
    
    // Exact backtranspose for L2 (computed via Gauss-Jordan elimination)
    static constexpr std::uint32_t l2_backtranspose_exact(std::uint32_t x) noexcept;

    // ========================================================================
    // Wallén automaton for optimized enumeration
    // ========================================================================
    
    class WallenAutomaton {
    public:
        WallenAutomaton();
        
        template<typename Yield>
        void enumerate_omegas(std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield) const;

    private:
        struct State {
            std::uint32_t suffix_xor;
            int weight;
            
            bool operator==(const State& other) const noexcept {
                return suffix_xor == other.suffix_xor && weight == other.weight;
            }
        };
        
        std::array<std::unordered_map<std::uint64_t, std::vector<std::pair<State, int>>>, 32> transitions_;
        
        void precompute_transitions();
        std::uint64_t pack_state(const State& s) const noexcept;
        State unpack_state(std::uint64_t packed) const noexcept;
    };

    // ========================================================================
    // Linear Highway table management
    // ========================================================================
    
    class LinearHighwayTable {
    public:
        bool load(const std::string& filename);
        bool save(const std::string& filename) const;
        int query(std::uint32_t mA, std::uint32_t mB, int rounds) const;
        void build(int max_rounds);
        size_t size() const noexcept { return table_.size(); }
        bool empty() const noexcept { return table_.empty(); }

    private:
        std::unordered_map<std::uint64_t, int> table_;
        
        std::uint64_t make_key(std::uint32_t mA, std::uint32_t mB, int rounds) const noexcept;
    };

    // ========================================================================
    // Linear bounds computation
    // ========================================================================
    
    class LinearBoundsComputer {
    public:
        // Single round linear bound
        int lb_full(std::uint32_t mA, std::uint32_t mB, int K1, int K2, int n, int weight_cap);
        
        // Multi-round suffix bound
        int suffix_bound(std::uint32_t mA, std::uint32_t mB, int rounds, int weight_cap);
        
        // Combined bound with highway table
        int combined_bound(std::uint32_t mA, std::uint32_t mB, int rounds, 
                          const LinearHighwayTable* highway, int weight_cap);

    private:
        mutable std::unordered_map<std::uint64_t, int> cache_;
        
        std::uint64_t make_cache_key(std::uint32_t mA, std::uint32_t mB, int rounds) const noexcept;
    };

    // ========================================================================
    // Main analysis interface
    // ========================================================================
    
    struct AnalysisConfig {
        int rounds = 4;
        int weight_cap = 20;
        std::uint32_t start_mA = 0;
        std::uint32_t start_mB = 0;
        bool use_highway = false;
        std::string highway_file;
        bool use_canonical = true;
        bool use_optimized_wallen = false;
    };

    struct AnalysisResult {
        int best_weight;
        LinearState best_state;
        std::vector<LinearState> trail;
        std::uint64_t nodes_processed;
        std::uint64_t elapsed_ms;
        bool search_complete;
    };

    // Perform complete MELCC analysis
    static AnalysisResult analyze(const AnalysisConfig& config);

private:
    // Internal helper methods
    static std::pair<std::uint32_t, std::uint32_t> canonical_rotate_pair(std::uint32_t a, std::uint32_t b);
    static std::uint32_t hw32(std::uint32_t x) noexcept { return __builtin_popcount(x); }
    
    // MnT operator for carry support vector computation
    static std::uint32_t MnT_of(std::uint32_t v) noexcept;
    
    // Weight computation for single mask combination
    static std::optional<int> wallen_weight(std::uint32_t mu, std::uint32_t nu, 
                                           std::uint32_t omega, int n = 32);

    // Global automaton instance
    static WallenAutomaton g_wallen_automaton_;
};

// ============================================================================
// Template implementations (must be in header)
// ============================================================================

template<typename Yield>
void MELCCAnalyzer::enumerate_wallen_omegas(
    std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield) {
    
    const std::uint32_t base = mu ^ nu;
    
    auto try_v = [&](std::uint32_t v) {
        std::uint32_t zstar = MnT_of(v);
        int w = hw32(zstar);
        if (w >= weight_cap) return;
        
        std::uint32_t omega = v ^ base;
        std::uint32_t a = mu ^ omega;
        std::uint32_t b = nu ^ omega;
        
        if ((a & ~zstar) == 0u && (b & ~zstar) == 0u) {
            yield(omega, w);
        }
    };
    
    // Heuristic enumeration order: v=0, then singles, then pairs
    try_v(0);
    
    for (int i = 0; i < 32; i++) {
        try_v(1u << i);
    }
    
    for (int i = 0; i < 32; i++) {
        for (int j = i + 1; j < 32; j++) {
            try_v((1u << i) | (1u << j));
        }
    }
}

template<typename Yield>
void MELCCAnalyzer::enumerate_wallen_omegas_optimized(
    std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield) {
    
    g_wallen_automaton_.enumerate_omegas(mu, nu, weight_cap, std::forward<Yield>(yield));
}

template<typename Yield>
void MELCCAnalyzer::WallenAutomaton::enumerate_omegas(
    std::uint32_t mu, std::uint32_t nu, int weight_cap, Yield&& yield) const {
    
    const std::uint32_t base = mu ^ nu;
    
    struct SearchState {
        int pos;
        State automaton_state;
        std::uint32_t v_partial;
    };
    
    std::vector<SearchState> stack;
    stack.reserve(64);
    stack.push_back({31, {0, 0}, 0});
    
    while (!stack.empty()) {
        SearchState current = stack.back();
        stack.pop_back();
        
        if (current.pos < 0) {
            std::uint32_t v = current.v_partial;
            std::uint32_t omega = v ^ base;
            std::uint32_t zstar = MELCCAnalyzer::MnT_of(v);
            std::uint32_t a = mu ^ omega;
            std::uint32_t b = nu ^ omega;
            
            if ((a & ~zstar) == 0 && (b & ~zstar) == 0) {
                int weight = __builtin_popcount(zstar);
                yield(omega, weight);
            }
            continue;
        }
        
        if (current.automaton_state.weight >= weight_cap) {
            continue;
        }
        
        std::uint64_t state_key = pack_state(current.automaton_state);
        int trans_idx = 31 - current.pos;
        
        if (trans_idx >= 0 && trans_idx < 32) {
            auto it = transitions_[trans_idx].find(state_key);
            if (it != transitions_[trans_idx].end()) {
                for (const auto& [next_state, v_bit] : it->second) {
                    int remaining_bits = current.pos;
                    if (next_state.weight + remaining_bits < weight_cap) {
                        SearchState next_search;
                        next_search.pos = current.pos - 1;
                        next_search.automaton_state = next_state;
                        next_search.v_partial = current.v_partial | ((std::uint32_t)v_bit << current.pos);
                        stack.push_back(next_search);
                    }
                }
            }
        }
    }
}

// ============================================================================
// Inline constexpr implementations
// ============================================================================

constexpr std::uint32_t MELCCAnalyzer::l1_backtranspose_exact(std::uint32_t x) noexcept {
    // Precomputed exact inverse of L1 transformation
    return x ^ NeoAlzetteCore::rotr(x,2) ^ NeoAlzetteCore::rotr(x,8) ^ NeoAlzetteCore::rotr(x,10) ^ NeoAlzetteCore::rotr(x,14)
             ^ NeoAlzetteCore::rotr(x,16)^ NeoAlzetteCore::rotr(x,18)^ NeoAlzetteCore::rotr(x,20)^ NeoAlzetteCore::rotr(x,24)
             ^ NeoAlzetteCore::rotr(x,28)^ NeoAlzetteCore::rotr(x,30);
}

constexpr std::uint32_t MELCCAnalyzer::l2_backtranspose_exact(std::uint32_t x) noexcept {
    // Precomputed exact inverse of L2 transformation  
    return x ^ NeoAlzetteCore::rotr(x,2) ^ NeoAlzetteCore::rotr(x,4) ^ NeoAlzetteCore::rotr(x,8) ^ NeoAlzetteCore::rotr(x,12)
             ^ NeoAlzetteCore::rotr(x,14)^ NeoAlzetteCore::rotr(x,16)^ NeoAlzetteCore::rotr(x,18)^ NeoAlzetteCore::rotr(x,22)
             ^ NeoAlzetteCore::rotr(x,24)^ NeoAlzetteCore::rotr(x,30);
}

} // namespace neoalz