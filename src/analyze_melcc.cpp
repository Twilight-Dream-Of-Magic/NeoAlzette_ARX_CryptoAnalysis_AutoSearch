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
#include "neoalzette_core.hpp"
#include "melcc_analyzer.hpp"
#include "threshold_search_framework.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "\nMELCC Analyzer - Linear Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [--start-hex mA mB] [--export out.csv]\n\n"
            "Arguments:\n"
            "  R                    Number of rounds\n"
            "  Wcap                 Global weight cap for threshold/beam search\n\n"
            "Options:\n"
            "  --start-hex mA mB      Initial masks in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv       Export results to CSV file\n"
            "  --lin-highway H.bin    Optional linear Highway suffix-LB file\n\n"
            "Example:\n"
            "  %s 4 18 --start-hex 0x80000000 0x1 --export linear_results.csv\n\n",
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
    
    std::string export_path;
    
    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "--start-hex" && i + 2 < argc) {
            config.start_mA = std::stoul(argv[i + 1], nullptr, 16);
            config.start_mB = std::stoul(argv[i + 2], nullptr, 16);
            i += 2;
        } else if (std::string(argv[i]) == "--export" && i + 1 < argc) {
            export_path = argv[i + 1];
            i++;
        } else if (std::string(argv[i]) == "--lin-highway" && i + 1 < argc) {
            config.highway_file = argv[i + 1];
            config.use_highway = true;
            i++;
        }
    }
    
    // Perform analysis
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = MELCCAnalyzer::analyze(config);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Display results
    std::printf("MELCC Analysis Results:\n");
    std::printf("Rounds: %d, Weight Cap: %d\n", R, Wcap);
    std::printf("Start State: (0x%08X, 0x%08X)\n", config.start_mA, config.start_mB);
    std::printf("Best Weight: %d\n", result.best_weight);
    std::printf("Best State: (0x%08X, 0x%08X)\n", result.best_state.mA, result.best_state.mB);
    std::printf("Nodes Processed: %lu\n", result.nodes_processed);
    std::printf("Search Time: %lu ms\n", result.elapsed_ms);
    std::printf("Search Complete: %s\n", result.search_complete ? "Yes" : "No");
    
    // Export results if requested
    if (!export_path.empty()) {
        std::ostringstream summary;
        summary << "MELCC," << R << "," << Wcap << ",0x" << std::hex << config.start_mA 
                << ",0x" << config.start_mB << "," << std::dec << result.best_weight 
                << "," << result.nodes_processed << "," << duration.count();
        
        if (UtilityTools::TrailExporter::append_csv_line(export_path, summary.str())) {
            std::printf("Results exported to: %s\n", export_path.c_str());
        } else {
            std::fprintf(stderr, "Failed to export results to: %s\n", export_path.c_str());
        }
    }
    
    return 0;
}