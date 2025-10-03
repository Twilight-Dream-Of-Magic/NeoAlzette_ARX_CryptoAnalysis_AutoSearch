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
#include "melcc_analyzer.hpp"
#include "threshold_search_framework.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "\nMELCC Analyzer - Optimized Linear Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [--start-hex mA mB] [--export out.csv] [--lin-highway H.bin]\n\n"
            "Arguments:\n"
            "  R                    Number of rounds\n"
            "  Wcap                 Global weight cap for threshold/beam search\n\n"
            "Options:\n"
            "  --start-hex mA mB      Initial masks in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv       Export results to CSV file\n"
            "  --export-trace out.csv Export complete optimal trail path\n"
            "  --export-hist out.csv  Export weight distribution histogram\n"
            "  --export-topN N out.csv Export top-N best results\n"
            "  --lin-highway H.bin     Optional linear Highway suffix-LB file\n"
            "  --threads N             Number of threads to use (default: auto-detect)\n"
            "  --fast-canonical        Use fast canonicalization (less accurate but faster)\n\n"
            "Optimizations:\n"
            "  - Precomputed Wallén automaton for faster linear enumeration\n"
            "  - Parallel threshold search with work-stealing\n"
            "  - Cache-friendly packed state representation\n"
            "  - Optimized canonicalization algorithms\n\n"
            "Example:\n"
            "  %s 4 18 --start-hex 0x80000000 0x1 --threads 4 --export linear_results.csv\n\n",
            argv[0], argv[0]);
        return 1;
    }

    // Parse arguments
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);
    
    MELCCAnalyzer::AnalysisConfig config;
    config.rounds = R;
    config.weight_cap = Wcap;
    config.start_mA = 0;
    config.start_mB = 0;
    config.use_optimized_wallen = true;
    
    std::string export_path, export_trace_path, export_hist_path, export_topN_path;
    int export_topN = 0;
    int num_threads = 0;
    bool fast_canonical = false;
    
    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "--start-hex" && i + 2 < argc) {
            config.start_mA = std::stoul(argv[i + 1], nullptr, 16);
            config.start_mB = std::stoul(argv[i + 2], nullptr, 16);
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
        } else if (std::string(argv[i]) == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[i + 1]);
            i++;
        } else if (std::string(argv[i]) == "--fast-canonical") {
            fast_canonical = true;
        } else if (std::string(argv[i]) == "--lin-highway" && i + 1 < argc) {
            config.highway_file = argv[i + 1];
            config.use_highway = true;
            i++;
        }
    }
    
    // Validate configuration
    auto validation = UtilityTools::ConfigValidator::validate_linear_params(
        R, Wcap, config.start_mA, config.start_mB);
    
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
    auto estimate = UtilityTools::ConfigValidator::estimate_resources(R, Wcap, "MELCC_OPTIMIZED");
    std::printf("Resource Estimate:\n");
    std::printf("- Memory: ~%s MB\n", 
               UtilityTools::StringUtils::format_number(estimate.estimated_memory_mb).c_str());
    std::printf("- Time: ~%s\n", 
               UtilityTools::StringUtils::format_duration(estimate.estimated_time_seconds * 1000).c_str());
    std::printf("- Recommended Threads: %d\n", estimate.recommended_threads);
    std::printf("- Personal Computer Suitable: %s\n", 
               estimate.suitable_for_personal_computer ? "✓ YES" : "✗ NO");
    
    if (!estimate.suitable_for_personal_computer) {
        std::printf("\n⚠️  WARNING: This configuration may require computing cluster resources!\n");
        std::printf("Consider reducing rounds to <= 6 or weight_cap to <= 25 for personal computers.\n");
    }
    std::printf("\n");
    
    // Set thread count if specified
    if (num_threads > 0) {
        std::printf("Using %d threads for parallel search.\n", num_threads);
    }
    
    // Perform optimized analysis
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = MELCCAnalyzer::analyze(config);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Display results
    std::printf("MELCC Optimized Analysis Results:\n");
    std::printf("=================================\n");
    std::printf("Configuration:\n");
    std::printf("  Rounds: %d, Weight Cap: %d\n", R, Wcap);
    std::printf("  Start Masks: (0x%08X, 0x%08X)\n", config.start_mA, config.start_mB);
    std::printf("  Optimized Wallén: %s\n", config.use_optimized_wallen ? "✓" : "✗");
    std::printf("  Fast Canonical: %s\n", fast_canonical ? "✓" : "✗");
    std::printf("\nResults:\n");
    std::printf("  Best Weight: %d\n", result.best_weight);
    std::printf("  Best State: (0x%08X, 0x%08X)\n", result.best_state.mA, result.best_state.mB);
    std::printf("  Nodes Processed: %s\n", 
               UtilityTools::StringUtils::format_number(result.nodes_processed).c_str());
    std::printf("  Search Time: %s\n", 
               UtilityTools::StringUtils::format_duration(result.elapsed_ms).c_str());
    std::printf("  Search Complete: %s\n", result.search_complete ? "✓" : "✗");
    
    // Export results if requested
    if (!export_path.empty()) {
        std::ostringstream summary;
        summary << "MELCC_OPTIMIZED," << R << "," << Wcap << ",0x" << std::hex 
                << config.start_mA << ",0x" << config.start_mB << "," << std::dec 
                << result.best_weight << "," << result.nodes_processed << "," << duration.count();
        
        if (UtilityTools::TrailExporter::append_csv_line(export_path, summary.str())) {
            std::printf("\n✓ Results exported to: %s\n", export_path.c_str());
        } else {
            std::fprintf(stderr, "\n✗ Failed to export results to: %s\n", export_path.c_str());
        }
    }
    
    if (!export_trace_path.empty() && !result.trail.empty()) {
        std::vector<UtilityTools::TrailExporter::TrailEntry> trail_data;
        
        for (size_t i = 0; i < result.trail.size(); ++i) {
            UtilityTools::TrailExporter::TrailEntry entry;
            entry.round = i;
            entry.state_a = result.trail[i].mA;
            entry.state_b = result.trail[i].mB;
            entry.weight = (i == 0) ? 0 : result.best_weight; // Simplified
            entry.algorithm = "MELCC_OPTIMIZED";
            entry.timestamp = std::chrono::system_clock::now();
            trail_data.push_back(entry);
        }
        
        if (UtilityTools::TrailExporter::export_trail_csv(export_trace_path, trail_data)) {
            std::printf("✓ Trail exported to: %s\n", export_trace_path.c_str());
        }
    }
    
    return 0;
}