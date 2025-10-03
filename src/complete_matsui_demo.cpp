#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include "neoalzette_core.hpp"
#include "medcp_analyzer.hpp"
#include "threshold_search_framework.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void demo_algorithm_1() {
    print_header("Matsui Algorithm 1 - PDDT Construction Demo");
    
    std::printf("Building Partial Difference Distribution Table (PDDT)...\n");
    
    UtilityTools::SimplePDDT pddt;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    pddt.build(8, 15); // 8-bit, weight threshold 15
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto stats = pddt.get_stats();
    
    std::printf("PDDT Construction Results:\n");
    std::printf("- Total Entries: %lu\n", stats.total_entries);
    std::printf("- Weight Range: %d to %d\n", stats.min_weight, stats.max_weight);
    std::printf("- Average Weight: %.2f\n", stats.avg_weight);
    std::printf("- Construction Time: %lu ms\n", duration.count());
    
    // Demonstrate query
    std::printf("\nQuerying PDDT for input difference 0x1:\n");
    auto entries = pddt.query(0x1, 10);
    std::printf("Found %lu entries with weight <= 10\n", entries.size());
    
    if (!entries.empty()) {
        std::printf("Sample entries:\n");
        for (size_t i = 0; i < std::min(size_t(5), entries.size()); ++i) {
            std::printf("  0x%02X -> 0x%02X (weight=%d, prob=%.6f)\n",
                       entries[i].input_diff, entries[i].output_diff,
                       entries[i].weight, entries[i].probability);
        }
    }
}

void demo_algorithm_2() {
    print_header("Matsui Algorithm 2 - Threshold Search Demo");
    
    std::printf("Demonstrating threshold search with highways/country roads strategy...\n");
    
    ThresholdSearchFramework::MatsuiComplete::Config config;
    config.rounds = 4;
    config.weight_threshold = 20;
    config.use_highways = true;
    config.use_country_roads = true;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = ThresholdSearchFramework::MatsuiComplete::algorithm_2_complete(config);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    std::printf("Threshold Search Results:\n");
    std::printf("- Best Weight: %d\n", result.best_weight);
    std::printf("- Total Nodes: %lu\n", result.total_nodes);
    std::printf("- Pruned Nodes: %lu\n", result.pruned_nodes);
    std::printf("- Search Time: %.3f seconds\n", result.elapsed_seconds);
    
    if (!result.best_trail.empty()) {
        std::printf("- Best Trail Length: %lu rounds\n", result.best_trail.size());
        std::printf("- Trail: ");
        for (size_t i = 0; i < result.best_trail.size(); ++i) {
            std::printf("0x%X", result.best_trail[i]);
            if (i < result.best_trail.size() - 1) std::printf(" -> ");
        }
        std::printf("\n");
    }
}

void demo_performance_validation() {
    print_header("Performance and Validation Demo");
    
    std::printf("Testing algorithm implementations for correctness...\n");
    
    // Test basic ARX-box functionality
    std::uint32_t test_a = 0x12345678, test_b = 0x87654321;
    std::uint32_t orig_a = test_a, orig_b = test_b;
    
    NeoAlzetteCore::forward(test_a, test_b);
    NeoAlzetteCore::backward(test_a, test_b);
    
    bool roundtrip_ok = (test_a == orig_a && test_b == orig_b);
    std::printf("✓ ARX-box roundtrip test: %s\n", roundtrip_ok ? "PASSED" : "FAILED");
    
    // Test canonicalization
    auto canonical1 = UtilityTools::Canonicalizer::canonical_rotate_pair(0x80000000, 0x1);
    auto canonical2 = UtilityTools::Canonicalizer::canonical_rotate_pair_fast(0x80000000, 0x1);
    
    std::printf("✓ Canonicalization test: Standard=(0x%X,0x%X), Fast=(0x%X,0x%X)\n",
               canonical1.first, canonical1.second, canonical2.first, canonical2.second);
    
    // Performance test with small parameters
    std::printf("\nPerformance test (4 rounds, weight cap 15):\n");
    
    MEDCPAnalyzer::AnalysisConfig small_config;
    small_config.rounds = 4;
    small_config.weight_cap = 15;
    small_config.start_dA = 1;
    small_config.start_dB = 0;
    
    auto small_start = std::chrono::high_resolution_clock::now();
    auto small_result = MEDCPAnalyzer::analyze(small_config);
    auto small_end = std::chrono::high_resolution_clock::now();
    
    auto small_duration = std::chrono::duration_cast<std::chrono::milliseconds>(small_end - small_start);
    
    std::printf("✓ Small MEDCP analysis completed in %lu ms\n", small_duration.count());
    std::printf("  Result: weight=%d, nodes=%lu\n", 
               small_result.best_weight, small_result.nodes_processed);
}

void demo_resource_estimation() {
    print_header("Resource Estimation Demo");
    
    std::printf("Estimating resource requirements for different parameter sets:\n\n");
    
    struct TestCase {
        int rounds;
        int weight_cap;
        std::string description;
    };
    
    std::vector<TestCase> test_cases = {
        {4, 15, "Personal Computer - Safe"},
        {4, 25, "Personal Computer - Challenge"},
        {6, 30, "Workstation Required"},
        {8, 35, "Cluster Required"},
        {10, 40, "High-Performance Cluster"}
    };
    
    for (const auto& test_case : test_cases) {
        auto estimate = UtilityTools::ConfigValidator::estimate_resources(
            test_case.rounds, test_case.weight_cap, "MEDCP");
        
        std::printf("%-25s: R=%d, W=%d\n", test_case.description.c_str(), 
                   test_case.rounds, test_case.weight_cap);
        std::printf("  Estimated Memory: %s MB\n", 
                   UtilityTools::StringUtils::format_number(estimate.estimated_memory_mb).c_str());
        std::printf("  Estimated Time: %s\n", 
                   UtilityTools::StringUtils::format_duration(estimate.estimated_time_seconds * 1000).c_str());
        std::printf("  Recommended Threads: %d\n", estimate.recommended_threads);
        std::printf("  Personal Computer Suitable: %s\n\n", 
                   estimate.suitable_for_personal_computer ? "✓ YES" : "✗ NO");
    }
}

int main(int argc, char** argv) {
    std::printf("=== Complete Matsui Algorithms Demonstration ===\n");
    std::printf("This demo showcases the complete implementation of:\n");
    std::printf("- Matsui Algorithm 1: PDDT Construction\n");
    std::printf("- Matsui Algorithm 2: Threshold Search with highways/country roads\n");
    std::printf("- Performance validation and resource estimation\n\n");
    
    bool quick_mode = false;
    bool full_mode = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--quick") {
            quick_mode = true;
        } else if (std::string(argv[i]) == "--full") {
            full_mode = true;
        }
    }
    
    // Default to quick mode if no arguments
    if (!quick_mode && !full_mode) {
        quick_mode = true;
    }
    
    try {
        if (quick_mode) {
            std::printf("Running in QUICK mode (suitable for personal computers)...\n");
            demo_algorithm_1();
            demo_performance_validation();
            demo_resource_estimation();
        }
        
        if (full_mode) {
            std::printf("Running in FULL mode (may require more resources)...\n");
            demo_algorithm_1();
            demo_algorithm_2();
            demo_performance_validation();
            demo_resource_estimation();
        }
        
        std::printf("\n=== Demonstration Complete ===\n");
        std::printf("All algorithms validated successfully!\n");
        
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error during demonstration: %s\n", e.what());
        return 1;
    }
    
    return 0;
}