#pragma once
/*
 * Unified Highway Table Library
 * 
 * Combines highway_table.hpp and highway_table_lin.hpp
 * Provides O(1) suffix lower bound queries for both differential and linear analysis
 */

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <algorithm>

namespace neoalz {

// Base highway table template for code reuse
template<typename StateT>
class HighwayTableBase {
protected:
    struct Entry {
        StateT state;
        int rounds;
        int bound;
        
        uint64_t key() const {
            return hash_state(state, rounds);
        }
    };
    
    std::unordered_map<uint64_t, int> table_;
    std::string file_path_;
    
    virtual uint64_t hash_state(const StateT& state, int rounds) const = 0;
    virtual StateT canonicalize_state(const StateT& state) const = 0;
    
public:
    virtual ~HighwayTableBase() = default;
    
    bool load(const std::string& path) {
        file_path_ = path;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        table_.clear();
        table_.reserve(count);
        
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t key;
            int bound;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            file.read(reinterpret_cast<char*>(&bound), sizeof(bound));
            table_[key] = bound;
        }
        
        file.close();
        return true;
    }
    
    bool save(const std::string& path) const {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        
        uint32_t count = table_.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        for (const auto& [key, bound] : table_) {
            file.write(reinterpret_cast<const char*>(&key), sizeof(key));
            file.write(reinterpret_cast<const char*>(&bound), sizeof(bound));
        }
        
        file.close();
        return true;
    }
    
    template<typename StateType>
    int query(const StateType& state, int rounds) const {
        auto canonical = canonicalize_state(state);
        uint64_t key = hash_state(canonical, rounds);
        auto it = table_.find(key);
        return (it != table_.end()) ? it->second : rounds * 3; // Conservative default
    }
    
    size_t size() const { return table_.size(); }
    bool empty() const { return table_.empty(); }
};

// Differential highway table
class HighwayTable : public HighwayTableBase<std::pair<uint32_t, uint32_t>> {
public:
    using StateType = std::pair<uint32_t, uint32_t>;
    
protected:
    uint64_t hash_state(const StateType& state, int rounds) const override {
        return (uint64_t(rounds) << 56) | (uint64_t(state.first) << 24) | uint64_t(state.second);
    }
    
    StateType canonicalize_state(const StateType& state) const override {
        return canonical_rotate_pair(state.first, state.second);
    }
    
public:
    // Direct query interface for differential states
    int query(uint32_t dA, uint32_t dB, int rounds) const {
        auto canonical = canonical_rotate_pair(dA, dB);
        uint64_t key = hash_state(canonical, rounds);
        auto it = table_.find(key);
        return (it != table_.end()) ? it->second : rounds * 3;
    }
    
    // Build highway table for given maximum rounds
    template<typename NextStatesFunc, typename WeightFunc>
    void build(int max_rounds, NextStatesFunc&& next_states, WeightFunc&& compute_weight) {
        table_.clear();
        
        // Build from back to front (dynamic programming)
        for (int r = max_rounds; r >= 1; --r) {
            std::vector<StateType> current_states;
            
            // For r = max_rounds, enumerate all possible states
            if (r == max_rounds) {
                // Start with a representative set of canonical states
                for (uint32_t a = 0; a <= 0xFFFF; ++a) {
                    for (uint32_t b = 0; b <= 0xFFFF; ++b) {
                        if ((a | b) != 0) {  // Skip zero state
                            auto canonical = canonical_rotate_pair(a, b);
                            current_states.push_back(canonical);
                        }
                    }
                }
                
                // Remove duplicates
                std::sort(current_states.begin(), current_states.end());
                current_states.erase(std::unique(current_states.begin(), current_states.end()), 
                                   current_states.end());
            }
            
            // For each state, compute optimal bound for r rounds
            for (const auto& state : current_states) {
                if (r == 1) {
                    // Base case: single round weight
                    int weight = compute_weight(state);
                    uint64_t key = hash_state(state, r);
                    table_[key] = weight;
                } else {
                    // Recursive case: min over all possible next states
                    int best_bound = std::numeric_limits<int>::max();
                    
                    auto children = next_states(state);
                    for (const auto& [child_state, add_weight] : children) {
                        auto child_canonical = canonicalize_state(child_state);
                        uint64_t child_key = hash_state(child_canonical, r - 1);
                        
                        auto it = table_.find(child_key);
                        if (it != table_.end()) {
                            int total_weight = add_weight + it->second;
                            best_bound = std::min(best_bound, total_weight);
                        }
                    }
                    
                    if (best_bound < std::numeric_limits<int>::max()) {
                        uint64_t key = hash_state(state, r);
                        table_[key] = best_bound;
                    }
                }
            }
        }
    }
};

// Linear highway table (similar structure, different state type)
class HighwayTableLin : public HighwayTableBase<std::pair<uint32_t, uint32_t>> {
public:
    using StateType = std::pair<uint32_t, uint32_t>;
    
protected:
    uint64_t hash_state(const StateType& state, int rounds) const override {
        return (uint64_t(rounds) << 56) | (uint64_t(state.first) << 24) | uint64_t(state.second);
    }
    
    StateType canonicalize_state(const StateType& state) const override {
        return canonical_rotate_pair(state.first, state.second);
    }
    
public:
    // Direct query interface for linear masks
    int query(uint32_t mA, uint32_t mB, int rounds) const {
        auto canonical = canonical_rotate_pair(mA, mB);
        uint64_t key = hash_state(canonical, rounds);
        auto it = table_.find(key);
        return (it != table_.end()) ? it->second : rounds * 2; // Conservative for linear
    }
};

// ============================================================================
// BOUNDS COMPUTATION UTILITIES
// ============================================================================

// Unified suffix bounds computation
class SuffixBounds {
public:
    // Differential suffix bound
    static int diff_suffix_bound(uint32_t dA, uint32_t dB, int rounds, int weight_cap) {
        if (rounds <= 0) return 0;
        if (rounds == 1) {
            // Single round bound using local model
            return estimate_single_round_diff_bound(dA, dB);
        }
        
        // Multi-round bound using recursive estimation
        int min_bound = rounds * 2;  // Conservative: 2 weight per round
        
        // Try to improve bound with local analysis
        auto local_bound = estimate_single_round_diff_bound(dA, dB);
        if (local_bound < weight_cap) {
            int remaining_bound = diff_suffix_bound(dA, dB, rounds - 1, weight_cap - local_bound);
            min_bound = std::min(min_bound, local_bound + remaining_bound);
        }
        
        return min_bound;
    }
    
    // Linear suffix bound  
    static int linear_suffix_bound(uint32_t mA, uint32_t mB, int rounds, int weight_cap) {
        if (rounds <= 0) return 0;
        if (rounds == 1) {
            return estimate_single_round_linear_bound(mA, mB);
        }
        
        int min_bound = rounds * 2;  // Conservative estimate
        
        auto local_bound = estimate_single_round_linear_bound(mA, mB);
        if (local_bound < weight_cap) {
            int remaining_bound = linear_suffix_bound(mA, mB, rounds - 1, weight_cap - local_bound);
            min_bound = std::min(min_bound, local_bound + remaining_bound);
        }
        
        return min_bound;
    }

private:
    static int estimate_single_round_diff_bound(uint32_t dA, uint32_t dB) {
        // Conservative estimate based on Hamming weight
        int hw_total = __builtin_popcount(dA | dB);
        return std::max(1, hw_total / 4);
    }
    
    static int estimate_single_round_linear_bound(uint32_t mA, uint32_t mB) {
        // Conservative estimate for linear analysis
        int hw_total = __builtin_popcount(mA | mB);
        return std::max(1, hw_total / 3);  // Linear bounds are typically tighter
    }
};

// ============================================================================
// UNIFIED INTERFACES  
// ============================================================================

// Fallback suffix bound computation when Highway tables are unavailable
template<bool is_differential>
class FallbackSuffixBound {
public:
    static int bound(uint32_t state_a, uint32_t state_b, int rounds, int weight_cap) {
        if constexpr (is_differential) {
            return SuffixBounds::diff_suffix_bound(state_a, state_b, rounds, weight_cap);
        } else {
            return SuffixBounds::linear_suffix_bound(state_a, state_b, rounds, weight_cap);
        }
    }
};

// Type aliases for convenience
using SuffixLB = FallbackSuffixBound<true>;   // Differential suffix bounds
using SuffixLBLin = FallbackSuffixBound<false>; // Linear suffix bounds

} // namespace neoalz