#pragma once
/*
 * Unified Wallén Linear Approximation Library
 * 
 * Combines wallen_fast.hpp and wallen_optimized.hpp into single unified interface
 * Provides both original fast enumeration and optimized automaton-based versions
 */

#include <cstdint>
#include <functional>
#include <vector>
#include <array>
#include <unordered_map>
#include "neoalzette.hpp"

namespace neoalz {

// Fast Hamming weight
static inline int hw32(uint32_t x) noexcept { return __builtin_popcount(x); }

// Compute z* = M_n^T v (carry support vector) for 32-bit via prefix XOR trick
static inline uint32_t MnT_of(uint32_t v) noexcept {
    uint32_t z = 0;
    uint32_t suffix = 0;
    for (int i = 31; i >= 0; --i) {
        if (suffix & 1u) z |= (1u << i);
        suffix ^= (v >> i) & 1u;
    }
    return z;
}

/*
 * Original Fast Wallén Enumeration
 * Heuristic approach with limited completeness but good performance
 */
template<class Yield>
inline void enumerate_wallen_omegas(uint32_t mu, uint32_t nu, int cap, Yield&& yield) {
    const uint32_t base = mu ^ nu;
    
    auto try_v = [&](uint32_t v){
        uint32_t zstar = MnT_of(v);
        int w = hw32(zstar);
        if (w >= cap) return;
        uint32_t omega = v ^ base;
        uint32_t a = mu ^ omega;
        uint32_t b = nu ^ omega;
        if ((a & ~zstar) == 0u && (b & ~zstar) == 0u) {
            yield(omega, w);
        }
    };
    
    // Heuristic order: v=0, then 32 singles, then 2-bit combos
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

/*
 * Optimized Wallén with Precomputed Automaton
 * Complete enumeration with intelligent pruning
 */
class WallenAutomaton {
private:
    struct State {
        uint32_t suffix_xor;
        int weight;
        bool operator==(const State& other) const noexcept {
            return suffix_xor == other.suffix_xor && weight == other.weight;
        }
    };
    
    std::array<std::unordered_map<uint64_t, std::vector<std::pair<State, int>>>, 32> transitions;
    
    static uint64_t pack_state(const State& s) noexcept {
        return ((uint64_t)s.suffix_xor << 32) | (uint64_t)s.weight;
    }
    
    static State unpack_state(uint64_t packed) noexcept {
        return {(uint32_t)(packed >> 32), (int)(packed & 0xFFFFFFFF)};
    }

public:
    WallenAutomaton() {
        precompute_transitions();
    }

private:
    void precompute_transitions() {
        for (int pos = 31; pos >= 0; --pos) {
            auto& trans_map = transitions[31 - pos];
            
            for (uint32_t suffix = 0; suffix <= 1; ++suffix) {
                for (int weight = 0; weight <= 32; ++weight) {
                    State current_state{suffix, weight};
                    uint64_t key = pack_state(current_state);
                    
                    std::vector<std::pair<State, int>> next_states;
                    
                    for (int v_bit = 0; v_bit <= 1; ++v_bit) {
                        State next_state;
                        int z_bit = suffix & 1;
                        next_state.weight = weight + z_bit;
                        next_state.suffix_xor = suffix ^ v_bit;
                        next_states.emplace_back(next_state, v_bit);
                    }
                    
                    trans_map[key] = std::move(next_states);
                }
            }
        }
    }

public:
    template<class Yield>
    void enumerate_omegas_optimized(uint32_t mu, uint32_t nu, int weight_cap, Yield&& yield) const {
        const uint32_t base = mu ^ nu;
        
        struct SearchState {
            int pos;
            State automaton_state;
            uint32_t v_partial;
        };
        
        std::vector<SearchState> stack;
        stack.reserve(64);
        stack.push_back({31, {0, 0}, 0});
        
        while (!stack.empty()) {
            SearchState current = stack.back();
            stack.pop_back();
            
            if (current.pos < 0) {
                uint32_t v = current.v_partial;
                uint32_t omega = v ^ base;
                uint32_t zstar = MnT_of(v);
                uint32_t a = mu ^ omega;
                uint32_t b = nu ^ omega;
                
                if ((a & ~zstar) == 0 && (b & ~zstar) == 0) {
                    int weight = __builtin_popcount(zstar);
                    yield(omega, weight);
                }
                continue;
            }
            
            if (current.automaton_state.weight >= weight_cap) {
                continue;
            }
            
            uint64_t state_key = pack_state(current.automaton_state);
            int trans_idx = 31 - current.pos;
            
            if (trans_idx >= 0 && trans_idx < 32) {
                auto it = transitions[trans_idx].find(state_key);
                if (it != transitions[trans_idx].end()) {
                    for (const auto& [next_state, v_bit] : it->second) {
                        int remaining_bits = current.pos;
                        if (next_state.weight + remaining_bits < weight_cap) {
                            SearchState next_search;
                            next_search.pos = current.pos - 1;
                            next_search.automaton_state = next_state;
                            next_search.v_partial = current.v_partial | ((uint32_t)v_bit << current.pos);
                            stack.push_back(next_search);
                        }
                    }
                }
            }
        }
    }
};

// Global instance for optimized enumeration
static WallenAutomaton g_wallen_automaton;

// Optimized enumeration interface
template<class Yield>
inline void enumerate_wallen_omegas_optimized(uint32_t mu, uint32_t nu, int cap, Yield&& yield) {
    g_wallen_automaton.enumerate_omegas_optimized(mu, nu, cap, std::forward<Yield>(yield));
}

// Weight computation for single mask combination
inline std::optional<int> wallen_weight(uint32_t mu, uint32_t nu, uint32_t omega, int n = 32) {
    uint32_t v = mu ^ nu ^ omega;
    uint32_t z_star = MnT_of(v);
    uint32_t a = mu ^ omega;
    uint32_t b = nu ^ omega;
    
    if ((a & ~z_star) != 0 || (b & ~z_star) != 0) {
        return std::nullopt; // Not feasible
    }
    
    return __builtin_popcount(z_star);
}

} // namespace neoalz