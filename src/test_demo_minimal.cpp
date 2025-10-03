#include <iostream>
#include <iomanip>
#include "pddt_algorithm1_complete.hpp"

using namespace neoalz;

int main() {
    std::cout << "Testing demo code...\n";
    
    PDDTAlgorithm1Complete::PDDTConfig config;
    config.bit_width = 8;
    config.set_weight_threshold(10);
    config.enable_pruning = true;
    
    std::cout << "Config set\n";
    
    PDDTAlgorithm1Complete::PDDTStats stats;
    std::cout << "Stats created\n";
    
    auto pddt = PDDTAlgorithm1Complete::compute_pddt_with_stats(config, stats);
    std::cout << "PDDT computed, size=" << pddt.size() << "\n";
    
    std::cout << "|D| = " << stats.total_entries << "\n";
    std::cout << "Nodes explored = " << stats.nodes_explored << "\n";
    
    // Show first few
    for (size_t i = 0; i < std::min(size_t(3), pddt.size()); ++i) {
        const auto& entry = pddt[i];
        std::cout << "Entry " << i << ": "
                  << "alpha=0x" << std::hex << entry.alpha
                  << " beta=0x" << entry.beta
                  << " gamma=0x" << entry.gamma
                  << " weight=" << std::dec << entry.weight << "\n";
    }
    
    std::cout << "Success!\n";
    return 0;
}
