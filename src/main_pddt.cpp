#include <iostream>
#include "utility_tools.hpp"

using namespace neoalz;

int main(int argc, char** argv) {
    int n = (argc > 1) ? std::atoi(argv[1]) : 8;
    int wth = (argc > 2) ? std::atoi(argv[2]) : 10;
    
    std::printf("Building Simple PDDT with n=%d bits, weight threshold=%d\n", n, wth);
    
    UtilityTools::SimplePDDT pddt;
    
    auto start = std::chrono::high_resolution_clock::now();
    pddt.build(n, wth);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    auto stats = pddt.get_stats();
    
    std::printf("PDDT Construction Results:\n");
    std::printf("- Total entries: %lu\n", stats.total_entries);
    std::printf("- Weight range: %d to %d\n", stats.min_weight, stats.max_weight);
    std::printf("- Average weight: %.2f\n", stats.avg_weight);
    std::printf("- Construction time: %lu ms\n", duration.count());
    
    // Show sample entries
    std::printf("\nSample entries (input->output, weight):\n");
    auto sample_entries = pddt.query(0x1, wth);
    
    for (size_t i = 0; i < std::min<size_t>(10, sample_entries.size()); ++i) {
        const auto& entry = sample_entries[i];
        std::printf("  0x%02X -> 0x%02X (weight=%d, prob=%.6f)\n",
                   entry.input_diff, entry.output_diff, 
                   entry.weight, entry.probability);
    }
    
    if (sample_entries.size() > 10) {
        std::printf("  ... and %lu more entries\n", sample_entries.size() - 10);
    }
    
    return 0;
}