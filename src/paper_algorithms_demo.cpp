/*
 * Demonstration of Paper Algorithms Implementation
 * 
 * Shows both Algorithm 1 (pDDT construction) and Algorithm 2 (Matsui threshold search)
 * from "Automatic Search for Differential Trails in ARX Ciphers"
 *
 * Includes both standard and optimized versions as requested by Erica.
 */

#include <iostream>
#include <chrono>
#include <iomanip>
#include <cmath>
#include "pddt.hpp"
#include "pddt_optimized.hpp"
#include "matsui_original.hpp"
#include "threshold_search.hpp"

using namespace neoalz;

void demo_algorithm1_standard() {
    std::cout << "\n=== Algorithm 1: Standard pDDT Construction ===\n";
    
    // Test with different word sizes and thresholds
    std::vector<std::pair<int, int>> test_cases = {
        {8, 4},   // Small test: 8-bit, weight ≤ 4
        {12, 6},  // Medium test: 12-bit, weight ≤ 6
        {16, 8}   // Larger test: 16-bit, weight ≤ 8
    };
    
    for (auto [n, w_thresh] : test_cases) {
        std::cout << "\nTesting n=" << n << ", weight_thresh=" << w_thresh << "\n";
        
        auto start = std::chrono::high_resolution_clock::now();
        
        PDDTConfig config{n, w_thresh};
        PDDTAdder generator(config);
        auto pddt = generator.compute();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  Found " << pddt.size() << " differentials in " 
                  << duration.count() << "ms\n";
        
        // Show first few results
        std::cout << "  Sample results:\n";
        for (size_t i = 0; i < std::min<size_t>(5, pddt.size()); ++i) {
            const auto& t = pddt[i];
            double prob = pow(2.0, -t.weight);
            std::cout << "    α=0x" << std::hex << t.alpha 
                      << " β=0x" << t.beta << " γ=0x" << t.gamma
                      << std::dec << " weight=" << t.weight
                      << " prob=2^{-" << t.weight << "}\n";
        }
    }
}

void demo_algorithm1_optimized() {
    std::cout << "\n=== Algorithm 1: Optimized Version (Appendix D.4) ===\n";
    
    // Test structure-specific optimizations
    std::vector<std::pair<PDDTOptimized::StructureType, std::string>> structures = {
        {PDDTOptimized::TEA_LIKE, "TEA-like"},
        {PDDTOptimized::SPECK_LIKE, "SPECK-like"},
        {PDDTOptimized::GENERAL_ARX, "General ARX"}
    };
    
    for (auto [structure, name] : structures) {
        std::cout << "\nTesting " << name << " optimization:\n";
        
        auto start = std::chrono::high_resolution_clock::now();
        
        PDDTOptimized::OptConfig config;
        config.n = 16;  // Manageable size for demo
        config.w_thresh = 8;
        config.structure = structure;
        
        PDDTOptimized optimizer(config);
        auto pddt_opt = optimizer.compute_optimized();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  Found " << pddt_opt.size() << " differentials in " 
                  << duration.count() << "ms\n";
        
        // Compare with standard version
        auto pddt_std = PDDTFactory::create_pddt(config, false);
        std::cout << "  Standard version found: " << pddt_std.size() << " differentials\n";
        std::cout << "  Optimization ratio: " << 
                     (double)pddt_opt.size() / pddt_std.size() * 100 << "%\n";
    }
}

void demo_algorithm2_matsui() {
    std::cout << "\n=== Algorithm 2: Matsui Threshold Search ===\n";
    
    // First, build a small pDDT for demonstration
    PDDTConfig config{12, 6};  // 12-bit, weight ≤ 6
    PDDTAdder generator(config);
    auto pddt = generator.compute();
    
    std::cout << "Built pDDT with " << pddt.size() << " highways\n";
    
    // Test both our optimized version and paper-exact version
    std::cout << "\n--- Our Optimized Implementation ---\n";
    
    // Simple demonstration of our threshold search
    struct SimpleDiff { uint32_t dA, dB; };
    
    auto next_states = [&](const SimpleDiff& d, int round, int slack) 
        -> std::vector<std::pair<SimpleDiff, int>> {
        
        std::vector<std::pair<SimpleDiff, int>> results;
        
        // Use pDDT to find possible transitions
        for (const auto& highway : pddt) {
            if (highway.alpha == d.dA) {  // Matching input
                SimpleDiff next{highway.gamma, highway.beta};  // Example transition
                if (highway.weight <= slack) {
                    results.emplace_back(next, highway.weight);
                }
            }
        }
        
        // Limit results to prevent explosion
        if (results.size() > 10) {
            results.resize(10);
        }
        
        return results;
    };
    
    auto lower_bound = [](const SimpleDiff& d, int remaining_rounds) -> int {
        // Conservative lower bound: assume minimum 2 weight per remaining round
        return remaining_rounds * 2;
    };
    
    SimpleDiff start{1, 0};  // Simple starting difference
    int rounds = 4;
    int weight_cap = 15;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = matsui_threshold_search(rounds, start, weight_cap, next_states, lower_bound);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    std::cout << "  Found trail with weight: " << result.first << "\n";
    std::cout << "  Search time: " << duration.count() << "μs\n";
    
    std::cout << "\n--- Paper-Exact Algorithm 2 (TODO) ---\n";
    std::cout << "  Full highways/country-roads implementation: In Progress\n";
    std::cout << "  Round-specific processing (1-2, intermediate, final): In Progress\n";
    std::cout << "  Complete paper Algorithm 2 matching: Planned\n";
}

void demonstrate_performance_comparison() {
    std::cout << "\n=== Performance Comparison: Standard vs Optimized ===\n";
    
    struct TestCase {
        int n;
        int w_thresh;
        std::string description;
    };
    
    std::vector<TestCase> cases = {
        {8, 3, "Tiny (8-bit)"},
        {10, 4, "Small (10-bit)"},
        {12, 5, "Medium (12-bit)"},
        {14, 6, "Large (14-bit)"}
    };
    
    std::cout << std::setw(12) << "Test Case" 
              << std::setw(15) << "Standard Time" 
              << std::setw(15) << "Optimized Time"
              << std::setw(12) << "Speedup"
              << std::setw(15) << "Completeness\n";
    std::cout << std::string(70, '-') << "\n";
    
    for (const auto& test : cases) {
        // Standard Algorithm 1
        auto start1 = std::chrono::high_resolution_clock::now();
        auto pddt_std = PDDTFactory::create_pddt(
            {test.n, test.w_thresh, PDDTOptimized::GENERAL_ARX}, false);
        auto end1 = std::chrono::high_resolution_clock::now();
        auto time_std = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1);
        
        // Optimized Algorithm 1  
        auto start2 = std::chrono::high_resolution_clock::now();
        auto pddt_opt = PDDTFactory::create_pddt(
            {test.n, test.w_thresh, PDDTOptimized::GENERAL_ARX}, true);
        auto end2 = std::chrono::high_resolution_clock::now();
        auto time_opt = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);
        
        double speedup = (time_std.count() > 0) ? 
                        (double)time_std.count() / time_opt.count() : 1.0;
        double completeness = (pddt_std.size() > 0) ? 
                             (double)pddt_opt.size() / pddt_std.size() * 100 : 100.0;
        
        std::cout << std::setw(12) << test.description
                  << std::setw(15) << (time_std.count() + 1) << "ms"
                  << std::setw(15) << (time_opt.count() + 1) << "ms"
                  << std::setw(12) << std::fixed << std::setprecision(1) << speedup << "x"
                  << std::setw(15) << std::setprecision(1) << completeness << "%\n";
    }
}

int main(int argc, char** argv) {
    std::cout << "Paper Algorithms Demo - ARX Differential Trail Search\n";
    std::cout << "Reference: 'Automatic Search for Differential Trails in ARX Ciphers'\n";
    std::cout << "Implementation: C++20 with optimizations\n";
    
    if (argc > 1 && std::string(argv[1]) == "--quick") {
        std::cout << "\nQuick demo mode (small parameters)\n";
        
        // Quick demonstration
        PDDTConfig config{8, 3};
        PDDTAdder generator(config);
        auto pddt = generator.compute();
        
        std::cout << "Algorithm 1 result: " << pddt.size() << " differentials\n";
        std::cout << "Algorithm 2 integration: Available via threshold_search.hpp\n";
        std::cout << "Use --full for complete demonstration\n";
        return 0;
    }
    
    try {
        demo_algorithm1_standard();
        demo_algorithm1_optimized(); 
        demo_algorithm2_matsui();
        demonstrate_performance_comparison();
        
        std::cout << "\n=== Implementation Summary ===\n";
        std::cout << "✅ Algorithm 1 (pDDT): Fully implemented with optimizations\n";
        std::cout << "🟡 Algorithm 2 (Matsui): Core implemented, highways/country-roads in progress\n";
        std::cout << "✅ C++20 modern implementation with performance enhancements\n";
        std::cout << "✅ Both standard and optimized versions available\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error during demonstration: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}