#include <iostream>
#include "matsui_algorithm2_complete.hpp"
#include "pddt_algorithm1_complete.hpp"

using namespace neoalz;

int main() {
    std::cout << "Testing Algorithm 2 (Matsui threshold search)...\n";
    
    try {
        // Build a small pDDT
        std::cout << "Step 1: Building pDDT...\n";
        PDDTAlgorithm1Complete::PDDTConfig pddt_config;
        pddt_config.bit_width = 4;
        pddt_config.set_weight_threshold(6);
        
        auto pddt = PDDTAlgorithm1Complete::compute_pddt(pddt_config);
        std::cout << "  pDDT size: " << pddt.size() << "\n";
        
        // Build highway table
        std::cout << "Step 2: Building highway table...\n";
        MatsuiAlgorithm2Complete::HighwayTable highway_table;
        
        for (const auto& triple : pddt) {
            MatsuiAlgorithm2Complete::DifferentialEntry entry(
                triple.alpha, triple.beta, triple.gamma,
                triple.probability(), triple.weight
            );
            highway_table.add(entry);
        }
        highway_table.build_index();
        std::cout << "  Highway table built with " << highway_table.size() << " entries\n";
        
        // Configure search
        std::cout << "Step 3: Configuring search...\n";
        MatsuiAlgorithm2Complete::SearchConfig search_config;
        search_config.num_rounds = 2;  // Very small
        search_config.highway_table = highway_table;
        search_config.prob_threshold = pddt_config.prob_threshold;
        search_config.initial_estimate = 1e-6;
        search_config.use_country_roads = false;  // Disable for first test
        search_config.max_nodes = 1000;
        
        std::cout << "  Rounds: " << search_config.num_rounds << "\n";
        std::cout << "  Max nodes: " << search_config.max_nodes << "\n";
        
        // Run search
        std::cout << "Step 4: Running threshold search...\n";
        auto result = MatsuiAlgorithm2Complete::execute_threshold_search(search_config);
        
        std::cout << "\nSuccess!\n";
        std::cout << "  Best weight: " << result.best_weight << "\n";
        std::cout << "  Nodes explored: " << result.nodes_explored << "\n";
        std::cout << "  Trail length: " << result.best_trail.num_rounds() << "\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
