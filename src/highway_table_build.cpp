#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <random>
#include <algorithm>
#include <chrono>
#include "arx_search_framework/medcp_analyzer.hpp"
#include "neoalzette/neoalzette_core.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, 
            "Highway Table Builder - Differential suffix lower bounds\n"
            "Usage: %s <output.bin> <max_rounds>\n\n"
            "Arguments:\n"
            "  output.bin    Output highway table file\n"
            "  max_rounds    Maximum rounds to compute (recommended: 6-10)\n\n"
            "Example:\n"
            "  %s highway_diff.bin 8\n\n"
            "Note: This may take 30min-3hours depending on max_rounds.\n"
            "      Use computing cluster for max_rounds > 10.\n\n",
            argv[0], argv[0]);
        return 1;
    }

    std::string output_file = argv[1];
    int max_rounds = std::stoi(argv[2]);
    
    if (max_rounds <= 0 || max_rounds > 15) {
        std::fprintf(stderr, "Error: max_rounds must be between 1 and 15\n");
        return 1;
    }
    
    std::printf("Building differential Highway table...\n");
    std::printf("Output: %s\n", output_file.c_str());
    std::printf("Max Rounds: %d\n", max_rounds);
    
    // Estimate resources
    auto estimate = UtilityTools::ConfigValidator::estimate_resources(max_rounds, 30, "HIGHWAY_BUILD");
    std::printf("Estimated Memory: %s MB\n", 
               UtilityTools::StringUtils::format_number(estimate.estimated_memory_mb).c_str());
    std::printf("Estimated Time: %s\n", 
               UtilityTools::StringUtils::format_duration(estimate.estimated_time_seconds * 1000).c_str());
    
    if (!estimate.suitable_for_personal_computer) {
        std::printf("⚠️  WARNING: This may require significant computing resources!\n");
    }
    std::printf("\n");
    
    // Build highway table
    MEDCPAnalyzer::HighwayTable highway;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::printf("Building highway table (this may take a while)...\n");
    highway.build(max_rounds);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Save results
    if (highway.save(output_file)) {
        std::printf("✓ Highway table built successfully!\n");
        std::printf("  Entries: %s\n", UtilityTools::StringUtils::format_number(highway.size()).c_str());
        std::printf("  Build Time: %s\n", UtilityTools::StringUtils::format_duration(duration.count()).c_str());
        std::printf("  Saved to: %s\n", output_file.c_str());
        
        // Test query functionality
        std::printf("\nTesting query functionality:\n");
        int test_bound = highway.query(0x1, 0x0, std::min(max_rounds, 4));
        std::printf("  Query(0x1, 0x0, %d rounds) = %d\n", 
                   std::min(max_rounds, 4), test_bound);
        
    } else {
        std::fprintf(stderr, "✗ Failed to save highway table to: %s\n", output_file.c_str());
        return 1;
    }
    
    return 0;
}