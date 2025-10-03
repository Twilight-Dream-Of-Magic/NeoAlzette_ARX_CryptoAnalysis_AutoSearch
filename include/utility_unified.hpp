#pragma once
/*
 * Unified Utility Library
 * 
 * Combines small utility files:
 * - canonicalize.hpp (state canonicalization)  
 * - trail_export.hpp (CSV export utilities)
 * - mask_backtranspose.hpp (linear mask operations)
 */

#include <cstdint>
#include <tuple>
#include <string>
#include <fstream>
#include <immintrin.h>
#include "neoalzette.hpp"

namespace neoalz {

// ============================================================================
// STATE CANONICALIZATION (from canonicalize.hpp)
// ============================================================================

// Standard canonicalization: find lexicographically minimal rotation
static inline std::pair<uint32_t,uint32_t> canonical_rotate_pair(uint32_t a, uint32_t b){
    uint32_t bestA=a, bestB=b;
    for(int r=0;r<32;++r){
        uint32_t aa = rotl(a,r);
        uint32_t bb = rotl(b,r);
        if (std::tie(aa,bb) < std::tie(bestA,bestB)){ bestA=aa; bestB=bb; }
    }
    return {bestA,bestB};
}

// Fast canonicalization using bit manipulation optimization
static inline std::pair<uint32_t,uint32_t> canonical_rotate_pair_fast(uint32_t a, uint32_t b){
    if (a == 0 || b == 0) {
        if (a > b) std::swap(a, b);
        return {a, b};
    }
    
    // Use leading zero count to limit search space
    int lz_a = __builtin_clz(a);
    int lz_b = __builtin_clz(b);
    int max_useful_rot = std::min(lz_a, lz_b);
    
    uint32_t best_a = a, best_b = b;
    int check_rotations = std::min(32, max_useful_rot + 8);
    
    for (int r = 1; r < check_rotations; ++r) {
        uint32_t rot_a = rotl(a, r);
        uint32_t rot_b = rotl(b, r);
        
        if (std::tie(rot_a, rot_b) < std::tie(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

// ============================================================================
// TRAIL EXPORT UTILITIES (from trail_export.hpp)  
// ============================================================================

class TrailExport {
public:
    // Append a line to CSV file (thread-safe)
    static void append_csv(const std::string& filename, const std::string& line) {
        static std::mutex file_mutex;
        std::lock_guard<std::mutex> lock(file_mutex);
        
        std::ofstream file(filename, std::ios::app);
        if (file.is_open()) {
            file << line << "\n";
            file.close();
        }
    }
    
    // Create CSV header if file doesn't exist
    static void ensure_csv_header(const std::string& filename, const std::string& header) {
        std::ifstream test(filename);
        if (!test.good()) {
            append_csv(filename, header);
        }
        test.close();
    }
    
    // Convert hex values to consistent format
    static std::string hex_format(uint32_t value, bool prefix = true) {
        std::ostringstream ss;
        if (prefix) ss << "0x";
        ss << std::hex << std::uppercase << value << std::dec;
        return ss.str();
    }
};

// ============================================================================
// MASK BACKTRANSPOSE (from mask_backtranspose.hpp)
// ============================================================================

// Exact backtranspose for L1 linear layer (computed via Gauss-Jordan)
constexpr uint32_t l1_backtranspose_exact(uint32_t x) noexcept {
    // Precomputed exact inverse of L1 transformation
    // L1(y) = y ^ rotl(y,2) ^ rotl(y,10) ^ rotl(y,18) ^ rotl(y,24)
    return x ^ rotr(x,2) ^ rotr(x,8) ^ rotr(x,10) ^ rotr(x,14)
             ^ rotr(x,16)^ rotr(x,18)^ rotr(x,20)^ rotr(x,24)
             ^ rotr(x,28)^ rotr(x,30);
}

// Exact backtranspose for L2 linear layer (computed via Gauss-Jordan)  
constexpr uint32_t l2_backtranspose_exact(uint32_t x) noexcept {
    // Precomputed exact inverse of L2 transformation
    // L2(y) = y ^ rotl(y,8) ^ rotl(y,14) ^ rotl(y,22) ^ rotl(y,30)
    return x ^ rotr(x,2) ^ rotr(x,4) ^ rotr(x,8) ^ rotr(x,12)
             ^ rotr(x,14)^ rotr(x,16)^ rotr(x,18)^ rotr(x,22)
             ^ rotr(x,24)^ rotr(x,30);
}

// Forward linear functions (from neoalzette.hpp)
constexpr uint32_t l1_forward(uint32_t x) noexcept {
    return x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24);
}

constexpr uint32_t l2_forward(uint32_t x) noexcept {
    return x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30);
}

// Utility functions for cross-branch injection analysis
inline std::pair<uint32_t, uint32_t> cd_from_B_delta(uint32_t B_delta) noexcept {
    // Simplified version for differential analysis
    uint32_t c = l2_forward(B_delta);
    uint32_t d = l1_forward(rotr(B_delta, 3));
    uint32_t t = rotl(c ^ d, 31);
    c ^= rotl(d, 17);
    d ^= rotr(t, 16);
    return {c, d};
}

inline std::pair<uint32_t, uint32_t> cd_from_A_delta(uint32_t A_delta) noexcept {
    // Simplified version for differential analysis
    uint32_t c = l1_forward(A_delta);
    uint32_t d = l2_forward(rotl(A_delta, 24));
    uint32_t t = rotr(c ^ d, 31);
    c ^= rotr(d, 17);
    d ^= rotl(t, 16);
    return {c, d};
}

// ============================================================================
// PERFORMANCE MONITORING UTILITIES
// ============================================================================

class PerformanceMonitor {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    std::atomic<uint64_t> nodes_processed_{0};
    std::atomic<uint64_t> nodes_pruned_{0};
    
public:
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
        nodes_processed_.store(0);
        nodes_pruned_.store(0);
    }
    
    void record_node_processed() {
        nodes_processed_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_node_pruned() {
        nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
    }
    
    struct Stats {
        uint64_t total_nodes;
        uint64_t pruned_nodes;
        double pruning_rate;
        uint64_t elapsed_ms;
    };
    
    Stats get_stats() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
        
        uint64_t total = nodes_processed_.load(std::memory_order_relaxed);
        uint64_t pruned = nodes_pruned_.load(std::memory_order_relaxed);
        
        Stats stats;
        stats.total_nodes = total;
        stats.pruned_nodes = pruned;
        stats.pruning_rate = (total > 0) ? (double)pruned / total * 100.0 : 0.0;
        stats.elapsed_ms = duration.count();
        
        return stats;
    }
};

} // namespace neoalz