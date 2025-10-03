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
#include "medcp_analyzer.hpp"
#include "threshold_search_framework.hpp"
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "\nMEDCP Analyzer - Differential Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv]\n\n"
            "Arguments:\n"
            "  R                 Number of rounds\n"
            "  Wcap              Global weight cap for threshold search\n"
            "  highway.bin       Optional differential Highway suffix-LB file\n\n"
            "Options:\n"
            "  --start-hex dA dB     Initial differences in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv      Export results to CSV file\n\n"
            "Example:\n"
            "  %s 4 20 --start-hex 0x1 0x0 --export results.csv\n\n",
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
    
    std::string export_path;
    
    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "--start-hex" && i + 2 < argc) {
            config.start_dA = std::stoul(argv[i + 1], nullptr, 16);
            config.start_dB = std::stoul(argv[i + 2], nullptr, 16);
            i += 2;
        } else if (std::string(argv[i]) == "--export" && i + 1 < argc) {
            export_path = argv[i + 1];
            i++;
        } else if (argv[i][0] != '-') {
            // Assume it's a highway file
            config.highway_file = argv[i];
            config.use_highway = true;
        }
    }
    
    // Perform analysis
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = MEDCPAnalyzer::analyze(config);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Display results
    std::printf("MEDCP Analysis Results:\n");
    std::printf("Rounds: %d, Weight Cap: %d\n", R, Wcap);
    std::printf("Start State: (0x%08X, 0x%08X)\n", config.start_dA, config.start_dB);
    std::printf("Best Weight: %d\n", result.best_weight);
    std::printf("Best State: (0x%08X, 0x%08X)\n", result.best_state.dA, result.best_state.dB);
    std::printf("Nodes Processed: %lu\n", result.nodes_processed);
    std::printf("Search Time: %lu ms\n", result.elapsed_ms);
    std::printf("Search Complete: %s\n", result.search_complete ? "Yes" : "No");
    
    // Export results if requested
    if (!export_path.empty()) {
        std::ostringstream summary;
        summary << "MEDCP," << R << "," << Wcap << ",0x" << std::hex << config.start_dA 
                << ",0x" << config.start_dB << "," << std::dec << result.best_weight 
                << "," << result.nodes_processed << "," << duration.count();
        
        if (UtilityTools::TrailExporter::append_csv_line(export_path, summary.str())) {
            std::printf("Results exported to: %s\n", export_path.c_str());
        } else {
            std::fprintf(stderr, "Failed to export results to: %s\n", export_path.c_str());
        }
    }
    
    return 0;
}