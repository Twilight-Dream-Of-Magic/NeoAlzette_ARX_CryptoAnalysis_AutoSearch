#include <iostream>
#include "pddt_algorithm1_complete.hpp"

using namespace neoalz;

int main() {
    std::cout << "Testing Algorithm 1 (pDDT construction)...\n";
    
    try {
        PDDTAlgorithm1Complete::PDDTConfig config;
        config.bit_width = 4;  // Very small for testing
        config.set_weight_threshold(6);
        config.enable_pruning = true;
        
        std::cout << "Configuration: n=" << config.bit_width 
                  << ", w_thresh=" << config.weight_threshold << "\n";
        
        PDDTAlgorithm1Complete::PDDTStats stats;
        auto pddt = PDDTAlgorithm1Complete::compute_pddt_with_stats(config, stats);
        
        std::cout << "Success! Found " << pddt.size() << " differentials\n";
        std::cout << "Nodes explored: " << stats.nodes_explored << "\n";
        std::cout << "Nodes pruned: " << stats.nodes_pruned << "\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
