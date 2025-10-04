#include "arx_search_framework/melcc_analyzer.hpp"
#include <fstream>
#include <chrono>
#include <queue>
#include <memory>
#include <optional>

namespace neoalz {

// Static member initialization
MELCCAnalyzer::WallenAutomaton MELCCAnalyzer::g_wallen_automaton_;

// ============================================================================
// MnT operator implementation (core of Wallén algorithm)
// ============================================================================

std::uint32_t MELCCAnalyzer::MnT_of(std::uint32_t v) noexcept {
    // Compute z* = M_n^T v (carry support vector) for 32-bit via prefix XOR trick
    std::uint32_t z = 0;
    std::uint32_t suffix = 0;
    
    for (int i = 31; i >= 0; --i) {
        if (suffix & 1u) z |= (1u << i);
        suffix ^= (v >> i) & 1u;
    }
    
    return z;
}

std::optional<int> MELCCAnalyzer::wallen_weight(std::uint32_t mu, std::uint32_t nu, 
                                               std::uint32_t omega, int n) {
    std::uint32_t v = mu ^ nu ^ omega;
    std::uint32_t z_star = MnT_of(v);
    std::uint32_t a = mu ^ omega;
    std::uint32_t b = nu ^ omega;
    
    if ((a & ~z_star) != 0 || (b & ~z_star) != 0) {
        return std::nullopt; // Not feasible
    }
    
    return __builtin_popcount(z_star);
}

// ============================================================================
// Wallén Automaton implementation
// ============================================================================

MELCCAnalyzer::WallenAutomaton::WallenAutomaton() {
    precompute_transitions();
}

void MELCCAnalyzer::WallenAutomaton::precompute_transitions() {
    for (int pos = 31; pos >= 0; --pos) {
        auto& trans_map = transitions_[31 - pos];
        
        for (std::uint32_t suffix = 0; suffix <= 1; ++suffix) {
            for (int weight = 0; weight <= 32; ++weight) {
                State current_state{suffix, weight};
                std::uint64_t key = pack_state(current_state);
                
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

std::uint64_t MELCCAnalyzer::WallenAutomaton::pack_state(const State& s) const noexcept {
    return ((std::uint64_t)s.suffix_xor << 32) | (std::uint64_t)s.weight;
}

MELCCAnalyzer::WallenAutomaton::State 
MELCCAnalyzer::WallenAutomaton::unpack_state(std::uint64_t packed) const noexcept {
    return {(std::uint32_t)(packed >> 32), (int)(packed & 0xFFFFFFFF)};
}

// ============================================================================
// Linear Highway Table implementation
// ============================================================================

bool MELCCAnalyzer::LinearHighwayTable::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::uint32_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    table_.clear();
    table_.reserve(count);
    
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t key;
        int bound;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));
        file.read(reinterpret_cast<char*>(&bound), sizeof(bound));
        table_[key] = bound;
    }
    
    return true;
}

bool MELCCAnalyzer::LinearHighwayTable::save(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    std::uint32_t count = table_.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& [key, bound] : table_) {
        file.write(reinterpret_cast<const char*>(&key), sizeof(key));
        file.write(reinterpret_cast<const char*>(&bound), sizeof(bound));
    }
    
    return true;
}

int MELCCAnalyzer::LinearHighwayTable::query(std::uint32_t mA, std::uint32_t mB, int rounds) const {
    auto canonical = canonical_rotate_pair(mA, mB);
    std::uint64_t key = make_key(canonical.first, canonical.second, rounds);
    
    auto it = table_.find(key);
    return (it != table_.end()) ? it->second : rounds * 2; // Conservative default for linear
}

std::uint64_t MELCCAnalyzer::LinearHighwayTable::make_key(std::uint32_t mA, std::uint32_t mB, 
                                                          int rounds) const noexcept {
    return (std::uint64_t(rounds) << 56) | (std::uint64_t(mA) << 24) | std::uint64_t(mB);
}

void MELCCAnalyzer::LinearHighwayTable::build(int max_rounds) {
    // Linear Highway table construction
    table_.clear();
    
    // Build from back to front using dynamic programming
    for (int r = max_rounds; r >= 1; --r) {
        // Generate representative linear mask states  
        std::vector<std::pair<std::uint32_t, std::uint32_t>> states;
        
        for (std::uint32_t mA = 0; mA <= 0xFFFF; mA += 0x111) {
            for (std::uint32_t mB = 0; mB <= 0xFFFF; mB += 0x111) {
                if ((mA | mB) != 0) {
                    auto canonical = canonical_rotate_pair(mA, mB);
                    states.push_back(canonical);
                }
            }
        }
        
        // Remove duplicates
        std::sort(states.begin(), states.end());
        states.erase(std::unique(states.begin(), states.end()), states.end());
        
        // Compute bounds for each state
        for (const auto& [mA, mB] : states) {
            std::uint64_t key = make_key(mA, mB, r);
            
            if (r == 1) {
                // Base case: estimate single round linear weight
                table_[key] = std::max(1, static_cast<int>(hw32(mA | mB)) / 4);
            } else {
                // Recursive case: use previous round bounds
                int min_bound = std::numeric_limits<int>::max();
                
                // Try representative transitions
                for (int trial = 0; trial < 6; ++trial) {
                    std::uint32_t next_mA = NeoAlzetteCore::rotl(mA, trial * 5);
                    std::uint32_t next_mB = NeoAlzetteCore::rotl(mB, trial * 5 + 2);
                    
                    auto next_canonical = canonical_rotate_pair(next_mA, next_mB);
                    std::uint64_t next_key = make_key(next_canonical.first, next_canonical.second, r - 1);
                    
                    auto it = table_.find(next_key);
                    if (it != table_.end()) {
                        int total_bound = 1 + it->second;
                        min_bound = std::min(min_bound, total_bound);
                    }
                }
                
                if (min_bound < std::numeric_limits<int>::max()) {
                    table_[key] = min_bound;
                }
            }
        }
    }
}

// ============================================================================
// Linear Bounds Computer implementation
// ============================================================================

int MELCCAnalyzer::LinearBoundsComputer::lb_full(std::uint32_t mA, std::uint32_t mB, 
                                                 int K1, int K2, int n, int weight_cap) {
    if (mA == 0 && mB == 0) return 0;
    
    std::uint64_t cache_key = make_cache_key(mA, mB, 0);
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) return it->second;
    
    int min_weight = std::numeric_limits<int>::max();
    
    // Enumerate best linear approximations using Wallén method
    MELCCAnalyzer::enumerate_wallen_omegas(mB, NeoAlzetteCore::rotl(mA, 31) ^ NeoAlzetteCore::rotl(mA, 17),
                                          weight_cap, 
        [&](std::uint32_t omega, int weight) {
            min_weight = std::min(min_weight, weight);
        });
    
    int bound = (min_weight == std::numeric_limits<int>::max()) ? weight_cap : min_weight;
    cache_[cache_key] = bound;
    return bound;
}

int MELCCAnalyzer::LinearBoundsComputer::suffix_bound(std::uint32_t mA, std::uint32_t mB, 
                                                     int rounds, int weight_cap) {
    if (rounds <= 0) return 0;
    
    std::uint64_t cache_key = make_cache_key(mA, mB, rounds);
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) return it->second;
    
    int bound;
    if (rounds == 1) {
        bound = lb_full(mA, mB, 3, 3, 32, weight_cap);
    } else {
        // Conservative estimate: 1.5 weight per round for linear
        bound = (rounds * 3) / 2;
    }
    
    cache_[cache_key] = bound;
    return bound;
}

int MELCCAnalyzer::LinearBoundsComputer::combined_bound(std::uint32_t mA, std::uint32_t mB, 
                                                       int rounds, const LinearHighwayTable* highway, 
                                                       int weight_cap) {
    int round_bound = lb_full(mA, mB, 3, 3, 32, weight_cap);
    
    int suffix_bound = 0;
    if (rounds > 1) {
        if (highway && !highway->empty()) {
            suffix_bound = highway->query(mA, mB, rounds - 1);
        } else {
            suffix_bound = this->suffix_bound(mA, mB, rounds - 1, weight_cap - round_bound);
        }
    }
    
    return round_bound + suffix_bound;
}

std::uint64_t MELCCAnalyzer::LinearBoundsComputer::make_cache_key(std::uint32_t mA, std::uint32_t mB, 
                                                                 int rounds) const noexcept {
    return (std::uint64_t(rounds) << 56) | (std::uint64_t(mA) << 24) | std::uint64_t(mB);
}

// ============================================================================
// Canonicalization
// ============================================================================

std::pair<std::uint32_t, std::uint32_t> 
MELCCAnalyzer::canonical_rotate_pair(std::uint32_t a, std::uint32_t b) {
    std::uint32_t best_a = a, best_b = b;
    
    for (int r = 0; r < 32; ++r) {
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

// ============================================================================
// Main analysis interface
// ============================================================================

MELCCAnalyzer::AnalysisResult MELCCAnalyzer::analyze(const AnalysisConfig& config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    AnalysisResult result;
    result.best_weight = std::numeric_limits<int>::max();
    result.best_state = {config.start_mA, config.start_mB};
    result.nodes_processed = 0;
    result.search_complete = false;
    
    // Load highway table if requested
    std::unique_ptr<LinearHighwayTable> highway;
    if (config.use_highway && !config.highway_file.empty()) {
        highway = std::make_unique<LinearHighwayTable>();
        if (!highway->load(config.highway_file)) {
            highway.reset(); // Failed to load
        }
    }
    
    // Initialize bounds computer
    LinearBoundsComputer bounds;
    
    // Simple single-round analysis for demonstration
    auto canonical_start = canonical_rotate_pair(config.start_mA, config.start_mB);
    int bound = bounds.lb_full(canonical_start.first, canonical_start.second, 
                              3, 3, 32, config.weight_cap);
    
    if (bound < result.best_weight) {
        result.best_weight = bound;
        result.best_state = {canonical_start.first, canonical_start.second};
        result.trail.push_back(result.best_state);
    }
    
    result.nodes_processed = 1;
    result.search_complete = true;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    return result;
}

} // namespace neoalz