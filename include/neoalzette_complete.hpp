#pragma once
/*
 * NeoAlzette Complete Analysis Library
 * 
 * Unified header that includes all necessary components for NeoAlzette analysis
 * Replaces multiple scattered includes with single comprehensive interface
 */

// Core algorithm implementations
#include "neoalzette.hpp"           // NeoAlzette ARX-box core
#include "lm_fast.hpp"              // Lipmaa-Moriai differential enumeration
#include "diff_add_const.hpp"       // Modular addition by constant differential

// Unified libraries (replacing scattered files)
#include "wallen_unified.hpp"       // Wallén linear analysis (fast + optimized)
#include "threshold_search_unified.hpp" // Threshold search (standard + optimized)
#include "highway_unified.hpp"      // Highway tables (diff + linear)
#include "utility_unified.hpp"      // Utilities (canonicalize + export + backtranspose)

// Specialized analysis components
#include "neoalz_lin.hpp"          // NeoAlzette linear layer functions
#include "lb_round_full.hpp"       // Full round differential bounds  
#include "lb_round_lin.hpp"        // Full round linear bounds

namespace neoalz {

// ============================================================================
// SIMPLIFIED INTERFACES FOR COMMON OPERATIONS
// ============================================================================

// Unified differential analysis interface
class DifferentialAnalysis {
public:
    struct Config {
        int rounds = 4;
        int weight_cap = 25;
        uint32_t start_dA = 0;
        uint32_t start_dB = 0;
        int k1 = 4, k2 = 4;
        bool use_optimized = true;
        int num_threads = 0;
        std::string highway_file;
    };
    
    struct Result {
        int best_weight;
        std::pair<uint32_t, uint32_t> best_state;
        bool search_complete;
        uint64_t nodes_processed;
        uint64_t elapsed_ms;
    };
    
    static Result analyze(const Config& config) {
        // Implementation would use unified search framework
        Result result;
        result.best_weight = std::numeric_limits<int>::max();
        result.search_complete = false;
        result.nodes_processed = 0;
        result.elapsed_ms = 0;
        
        // This is a placeholder - actual implementation would call
        // the unified threshold search with NeoAlzette-specific next_states
        
        return result;
    }
};

// Unified linear analysis interface  
class LinearAnalysis {
public:
    struct Config {
        int rounds = 4;
        int weight_cap = 20;
        uint32_t start_mA = 0;
        uint32_t start_mB = 0;
        bool use_optimized = true;
        int num_threads = 0;
        std::string highway_file;
    };
    
    struct Result {
        int best_weight;
        std::pair<uint32_t, uint32_t> best_state;
        bool search_complete;
        uint64_t nodes_processed;
        uint64_t elapsed_ms;
    };
    
    static Result analyze(const Config& config) {
        // Implementation would use unified search framework
        Result result;
        result.best_weight = std::numeric_limits<int>::max();
        result.search_complete = false;
        result.nodes_processed = 0;
        result.elapsed_ms = 0;
        
        return result;
    }
};

// ============================================================================
// VALIDATION AND TESTING UTILITIES
// ============================================================================

class ValidationTests {
public:
    // Test basic algorithm functionality
    static bool test_algorithms() {
        bool all_passed = true;
        
        // Test LM enumeration
        int count = 0;
        enumerate_lm_gammas_fast(1, 1, 8, 5, [&](uint32_t gamma, int weight) {
            count++;
        });
        all_passed &= (count > 0);
        
        // Test Wallén enumeration
        count = 0;
        enumerate_wallen_omegas(1, 1, 5, [&](uint32_t omega, int weight) {
            count++;
        });
        all_passed &= (count > 0);
        
        // Test canonicalization
        auto canon = canonical_rotate_pair(0x12345678, 0x87654321);
        all_passed &= (canon.first != 0 || canon.second != 0);
        
        return all_passed;
    }
    
    // Test performance with small parameters
    static void benchmark_small() {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Small benchmark to verify reasonable performance
        DifferentialAnalysis::Config config;
        config.rounds = 3;
        config.weight_cap = 10;
        config.use_optimized = false;
        
        auto result = DifferentialAnalysis::analyze(config);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::printf("Small benchmark: %ld ms\n", duration.count());
    }
};

} // namespace neoalz