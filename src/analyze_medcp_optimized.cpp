#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>
#include "neoalzette_core.hpp"
#include "medcp_analyzer.hpp"
#include "threshold_search_framework.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "\nMEDCP Analyzer - Optimized Differential Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv] [--threads N]\n\n"
            "Arguments:\n"
            "  R                 Number of rounds\n"
            "  Wcap              Global weight cap for threshold search\n"
            "  highway.bin       Optional differential Highway suffix-LB file\n\n"
            "Options:\n"
            "  --start-hex dA dB     Initial differences in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv      Export results to CSV file\n"
            "  --export-trace out.csv Export complete optimal trail path\n"
            "  --export-hist out.csv  Export weight distribution histogram\n"
            "  --export-topN N out.csv Export top-N best results\n"
            "  --k1 K               Top-K candidates for var–var in one-round LB (default 4)\n"
            "  --k2 K               Top-K candidates for var–const in one-round LB (default 4)\n"
            "  --threads N          Number of threads to use (default: auto-detect)\n"
            "  --fast-canonical     Use fast canonicalization (less accurate but faster)\n\n"
            "Optimizations:\n"
            "  - Parallel threshold search with work-stealing queues\n"
            "  - Cache-friendly packed state representation\n"
            "  - Optimized canonicalization with bit manipulation\n"
            "  - Improved memory access patterns and deduplication\n\n"
            "Example:\n"
            "  %s 4 20 --start-hex 0x1 0x0 --threads 4 --export results.csv\n\n",
            argv[0], argv[0]);
        return 1;
    }

    // Parse arguments
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);
    
    MEDCPAnalyzer::AnalysisConfig config;
    config.rounds = R;
    config.weight_cap = Wcap;
    config.start_dA = 0;
    config.start_dB = 0;
    config.K1 = 4;
    config.K2 = 4;
    
    std::string export_path, export_trace_path, export_hist_path, export_topN_path;
    int export_topN = 0;
    int num_threads = 0;
    bool fast_canonical = false;
    
    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "--start-hex" && i + 2 < argc) {
            config.start_dA = std::stoul(argv[i + 1], nullptr, 16);
            config.start_dB = std::stoul(argv[i + 2], nullptr, 16);
            i += 2;
        } else if (std::string(argv[i]) == "--export" && i + 1 < argc) {
            export_path = argv[i + 1];
            i++;
        } else if (std::string(argv[i]) == "--export-trace" && i + 1 < argc) {
            export_trace_path = argv[i + 1];
            i++;
        } else if (std::string(argv[i]) == "--export-hist" && i + 1 < argc) {
            export_hist_path = argv[i + 1];
            i++;
        } else if (std::string(argv[i]) == "--export-topN" && i + 2 < argc) {
            export_topN = std::stoi(argv[i + 1]);
            export_topN_path = argv[i + 2];
            i += 2;
        } else if (std::string(argv[i]) == "--k1" && i + 1 < argc) {
            config.K1 = std::stoi(argv[i + 1]);
            i++;
        } else if (std::string(argv[i]) == "--k2" && i + 1 < argc) {
            config.K2 = std::stoi(argv[i + 1]);
            i++;
        } else if (std::string(argv[i]) == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[i + 1]);
            i++;
        } else if (std::string(argv[i]) == "--fast-canonical") {
            fast_canonical = true;
        } else if (argv[i][0] != '-') {
            // Assume it's a highway file
            config.highway_file = argv[i];
            config.use_highway = true;
        }
    }
    
    // Validate configuration
    auto validation = UtilityTools::ConfigValidator::validate_diff_params(
        R, Wcap, config.start_dA, config.start_dB);
    
    if (!validation.valid) {
        std::fprintf(stderr, "Configuration errors:\n");
        for (const auto& error : validation.errors) {
            std::fprintf(stderr, "- %s\n", error.c_str());
        }
        return 1;
    }
    
    if (!validation.warnings.empty()) {
        std::printf("Configuration warnings:\n");
        for (const auto& warning : validation.warnings) {
            std::printf("- %s\n", warning.c_str());
        }
        std::printf("\n");
    }
    
    // Show resource estimate
    auto estimate = UtilityTools::ConfigValidator::estimate_resources(R, Wcap, "MEDCP_OPTIMIZED");
    std::printf("Resource Estimate:\n");
    std::printf("- Memory: ~%s MB\n", 
               UtilityTools::StringUtils::format_number(estimate.estimated_memory_mb).c_str());
    std::printf("- Time: ~%s\n", 
               UtilityTools::StringUtils::format_duration(estimate.estimated_time_seconds * 1000).c_str());
    std::printf("- Personal Computer Suitable: %s\n", 
               estimate.suitable_for_personal_computer ? "✓ YES" : "✗ NO");
    
    if (!estimate.suitable_for_personal_computer) {
        std::printf("\n⚠️  WARNING: This configuration may require computing cluster resources!\n");
    }
    std::printf("\n");
    
    // Perform optimized analysis
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = MEDCPAnalyzer::analyze(config);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Display results
    std::printf("MEDCP Optimized Analysis Results:\n");
    std::printf("================================\n");
    std::printf("Configuration:\n");
    std::printf("  Rounds: %d, Weight Cap: %d\n", R, Wcap);
    std::printf("  Start State: (0x%08X, 0x%08X)\n", config.start_dA, config.start_dB);
    std::printf("  K1=%d, K2=%d, Threads=%d\n", config.K1, config.K2, num_threads);
    std::printf("\nResults:\n");
    std::printf("  Best Weight: %d\n", result.best_weight);
    std::printf("  Best State: (0x%08X, 0x%08X)\n", result.best_state.dA, result.best_state.dB);
    std::printf("  Nodes Processed: %s\n", 
               UtilityTools::StringUtils::format_number(result.nodes_processed).c_str());
    std::printf("  Search Time: %s\n", 
               UtilityTools::StringUtils::format_duration(result.elapsed_ms).c_str());
    std::printf("  Search Complete: %s\n", result.search_complete ? "✓" : "✗");
    
    // Export results if requested
    if (!export_path.empty()) {
        std::ostringstream summary;
        summary << "MEDCP_OPTIMIZED," << R << "," << Wcap << ",0x" << std::hex 
                << config.start_dA << ",0x" << config.start_dB << "," << std::dec 
                << config.K1 << "," << config.K2 << "," << result.best_weight 
                << "," << result.nodes_processed << "," << duration.count();
        
        if (UtilityTools::TrailExporter::append_csv_line(export_path, summary.str())) {
            std::printf("\n✓ Results exported to: %s\n", export_path.c_str());
        } else {
            std::fprintf(stderr, "\n✗ Failed to export results to: %s\n", export_path.c_str());
        }
    }
    
    return 0;
}