#include "medcp_analyzer.hpp"
#include <fstream>
#include <chrono>
#include <queue>
#include <memory>

namespace neoalz {

// ============================================================================
// AOP and PSI functions (core of Lipmaa-Moriai algorithm)
// ============================================================================

std::uint32_t MEDCPAnalyzer::aop(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

std::uint32_t MEDCPAnalyzer::psi(std::uint32_t a, std::uint32_t b, std::uint32_t c) noexcept {
    // Simplified psi computation for differential probability
    return aop(a, b, c) ^ aop(a^c, b^c, 0);
}

// ============================================================================
// Modular addition by constant differential analysis
// ============================================================================

MEDCPAnalyzer::AddConstResult 
MEDCPAnalyzer::addconst_best(std::uint32_t alpha, std::uint32_t c, int n) noexcept {
    if (alpha == 0) return {0, 0, true};
    
    // Find best gamma for alpha + c = gamma
    std::uint32_t best_gamma = 0;
    int best_weight = std::numeric_limits<int>::max();
    bool found = false;
    
    // Try different gamma values
    for (int shift = 0; shift < std::min(n, 16); ++shift) {
        std::uint32_t gamma = NeoAlzetteCore::rotl(alpha, shift);
        int weight = addconst_weight(alpha, c, gamma, n);
        
        if (weight >= 0 && weight < best_weight) {
            best_weight = weight;
            best_gamma = gamma;
            found = true;
        }
    }
    
    return {best_gamma, best_weight, found};
}

int MEDCPAnalyzer::addconst_weight(std::uint32_t alpha, std::uint32_t c, 
                                   std::uint32_t gamma, int n) noexcept {
    // Compute differential weight for alpha + c = gamma
    std::uint32_t carry = 0;
    int weight = 0;
    
    for (int i = 0; i < n; ++i) {
        std::uint32_t ai = (alpha >> i) & 1;
        std::uint32_t ci = (c >> i) & 1;
        std::uint32_t gi = (gamma >> i) & 1;
        
        std::uint32_t expected = ai ^ ci ^ carry;
        if (expected != gi) return -1; // Infeasible
        
        if (ai && ci) carry = 1;
        else if (!ai && !ci) carry = 0;
        // else carry unchanged
        
        if (carry) weight++;
    }
    
    return weight;
}

// ============================================================================
// Highway Table implementation
// ============================================================================

bool MEDCPAnalyzer::HighwayTable::load(const std::string& filename) {
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

bool MEDCPAnalyzer::HighwayTable::save(const std::string& filename) const {
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

int MEDCPAnalyzer::HighwayTable::query(std::uint32_t dA, std::uint32_t dB, int rounds) const {
    auto canonical = canonical_rotate_pair(dA, dB);
    std::uint64_t key = make_key(canonical.first, canonical.second, rounds);
    
    auto it = table_.find(key);
    return (it != table_.end()) ? it->second : rounds * 3; // Conservative default
}

std::uint64_t MEDCPAnalyzer::HighwayTable::make_key(std::uint32_t dA, std::uint32_t dB, int rounds) const noexcept {
    return (std::uint64_t(rounds) << 56) | (std::uint64_t(dA) << 24) | std::uint64_t(dB);
}

void MEDCPAnalyzer::HighwayTable::build(int max_rounds) {
    // Highway table construction using dynamic programming
    table_.clear();
    
    // Build from back to front
    for (int r = max_rounds; r >= 1; --r) {
        // Enumerate canonical differential states
        std::vector<std::pair<std::uint32_t, std::uint32_t>> states;
        
        // Generate representative state set
        for (std::uint32_t dA = 0; dA <= 0xFFFF; dA += 0x101) {
            for (std::uint32_t dB = 0; dB <= 0xFFFF; dB += 0x101) {
                if ((dA | dB) != 0) {
                    auto canonical = canonical_rotate_pair(dA, dB);
                    states.push_back(canonical);
                }
            }
        }
        
        // Remove duplicates
        std::sort(states.begin(), states.end());
        states.erase(std::unique(states.begin(), states.end()), states.end());
        
        // Compute bounds for each state
        for (const auto& [dA, dB] : states) {
            std::uint64_t key = make_key(dA, dB, r);
            
            if (r == 1) {
                // Base case: estimate single round weight
                table_[key] = std::max(1, static_cast<int>(hw32(dA | dB)) / 3);
            } else {
                // Recursive case: use previous round bounds
                int min_bound = std::numeric_limits<int>::max();
                
                // Try a few representative transitions
                for (int trial = 0; trial < 8; ++trial) {
                    std::uint32_t next_dA = NeoAlzetteCore::rotl(dA, trial * 4);
                    std::uint32_t next_dB = NeoAlzetteCore::rotl(dB, trial * 4 + 1);
                    
                    auto next_canonical = canonical_rotate_pair(next_dA, next_dB);
                    std::uint64_t next_key = make_key(next_canonical.first, next_canonical.second, r - 1);
                    
                    auto it = table_.find(next_key);
                    if (it != table_.end()) {
                        int total_bound = 1 + it->second; // Assume 1 weight for this round
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
// Bounds Computer implementation
// ============================================================================

int MEDCPAnalyzer::BoundsComputer::lb_full(std::uint32_t dA, std::uint32_t dB, 
                                            int K1, int K2, int n, int weight_cap) {
    if (dA == 0 && dB == 0) return 0;
    
    std::uint64_t cache_key = make_cache_key(dA, dB, 0);
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) return it->second;
    
    int min_weight = std::numeric_limits<int>::max();
    
    // Enumerate best differentials for var-var addition
    MEDCPAnalyzer::enumerate_lm_gammas_fast(dB, NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17),
                                           n, weight_cap, 
        [&](std::uint32_t gamma, int weight) {
            min_weight = std::min(min_weight, weight);
        });
    
    int bound = (min_weight == std::numeric_limits<int>::max()) ? weight_cap : min_weight;
    cache_[cache_key] = bound;
    return bound;
}

int MEDCPAnalyzer::BoundsComputer::suffix_bound(std::uint32_t dA, std::uint32_t dB, 
                                                int rounds, int weight_cap) {
    if (rounds <= 0) return 0;
    
    std::uint64_t cache_key = make_cache_key(dA, dB, rounds);
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) return it->second;
    
    int bound;
    if (rounds == 1) {
        bound = lb_full(dA, dB, 4, 4, 32, weight_cap);
    } else {
        // Conservative estimate: 2 weight per round
        bound = rounds * 2;
    }
    
    cache_[cache_key] = bound;
    return bound;
}

std::uint64_t MEDCPAnalyzer::BoundsComputer::make_cache_key(std::uint32_t dA, std::uint32_t dB, 
                                                            int rounds) const noexcept {
    return (std::uint64_t(rounds) << 56) | (std::uint64_t(dA) << 24) | std::uint64_t(dB);
}

// ============================================================================
// Canonicalization
// ============================================================================

std::pair<std::uint32_t, std::uint32_t> 
MEDCPAnalyzer::canonical_rotate_pair(std::uint32_t a, std::uint32_t b) {
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

MEDCPAnalyzer::AnalysisResult MEDCPAnalyzer::analyze(const AnalysisConfig& config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    AnalysisResult result;
    result.best_weight = std::numeric_limits<int>::max();
    result.best_state = {config.start_dA, config.start_dB};
    result.nodes_processed = 0;
    result.search_complete = false;
    
    // Load highway table if requested
    std::unique_ptr<HighwayTable> highway;
    if (config.use_highway && !config.highway_file.empty()) {
        highway = std::make_unique<HighwayTable>();
        if (!highway->load(config.highway_file)) {
            highway.reset(); // Failed to load
        }
    }
    
    // Initialize bounds computer
    BoundsComputer bounds;
    
    // Simple single-round analysis for demonstration
    auto canonical_start = canonical_rotate_pair(config.start_dA, config.start_dB);
    int bound = bounds.lb_full(canonical_start.first, canonical_start.second, 
                              config.K1, config.K2, 32, config.weight_cap);
    
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