#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include "neoalzette_core.hpp"

namespace neoalz {

/**
 * Utility Tools - Collection of helper classes and functions
 * 
 * This class provides various utility functions including state canonicalization,
 * trail export functionality, basic round bounds computation, and other helper tools.
 */
class UtilityTools {
public:
    // ========================================================================
    // State canonicalization utilities
    // ========================================================================
    
    class Canonicalizer {
    public:
        // Standard canonicalization: find lexicographically minimal rotation
        static std::pair<std::uint32_t, std::uint32_t> 
        canonical_rotate_pair(std::uint32_t a, std::uint32_t b);
        
        // Fast canonicalization using bit manipulation optimization
        static std::pair<std::uint32_t, std::uint32_t> 
        canonical_rotate_pair_fast(std::uint32_t a, std::uint32_t b);
        
        // Optimized canonicalization with limited search space
        static std::pair<std::uint32_t, std::uint32_t> 
        canonical_rotate_pair_optimized(std::uint32_t a, std::uint32_t b);
        
        // Canonicalize a vector of state pairs
        static std::vector<std::pair<std::uint32_t, std::uint32_t>>
        canonicalize_states(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& states);
    };

    // ========================================================================
    // Trail export and CSV utilities
    // ========================================================================
    
    class TrailExporter {
    public:
        // Trail data structure
        struct TrailEntry {
            int round;
            std::uint32_t state_a;
            std::uint32_t state_b;
            int weight;
            std::string algorithm;
            std::chrono::system_clock::time_point timestamp;
        };

        // Export trail to CSV format (thread-safe)
        static bool export_trail_csv(const std::string& filename, 
                                     const std::vector<TrailEntry>& trail);
        
        // Append single entry to CSV file (thread-safe)
        static bool append_csv_line(const std::string& filename, const std::string& line);
        
        // Create CSV header if file doesn't exist
        static bool ensure_csv_header(const std::string& filename, const std::string& header);
        
        // Export histogram data
        static bool export_histogram_csv(const std::string& filename, 
                                        const std::vector<std::pair<int, int>>& histogram);
        
        // Export top-N results
        template<typename ResultT>
        static bool export_topN_csv(const std::string& filename, 
                                    const std::vector<ResultT>& results, int N);
        
        // Convert hex values to consistent format
        static std::string hex_format(std::uint32_t value, bool prefix = true);
        
    private:
        static std::mutex file_mutex_;
    };

    // ========================================================================
    // Basic round bounds computation
    // ========================================================================
    
    class BasicBounds {
    public:
        struct BoundResult {
            int lower_bound;
            int upper_bound;
            std::string method;
        };

        // Simple Hamming weight based bounds
        static BoundResult hamming_weight_bound(std::uint32_t dA, std::uint32_t dB);
        
        // Conservative bounds for differential analysis
        static BoundResult conservative_diff_bound(std::uint32_t dA, std::uint32_t dB, int rounds);
        
        // Conservative bounds for linear analysis
        static BoundResult conservative_linear_bound(std::uint32_t mA, std::uint32_t mB, int rounds);
        
        // Combined bound using multiple methods
        static BoundResult combined_bound(std::uint32_t stateA, std::uint32_t stateB, 
                                         int rounds, bool is_differential);
    };

    // ========================================================================
    // Simple PDDT implementation
    // ========================================================================
    
    class SimplePDDT {
    public:
        struct Entry {
            std::uint32_t input_diff;
            std::uint32_t output_diff;
            int weight;
            double probability;
        };

        SimplePDDT() = default;
        
        // Build PDDT for given bit size and weight threshold
        void build(int n_bits = 32, int weight_threshold = 10);
        
        // Query PDDT for given input difference
        std::vector<Entry> query(std::uint32_t input_diff, int max_weight = 100) const;
        
        // Get statistics about the PDDT
        struct Stats {
            std::size_t total_entries;
            int max_weight;
            int min_weight;
            double avg_weight;
        };
        Stats get_stats() const;
        
        // Save/load PDDT to/from file
        bool save(const std::string& filename) const;
        bool load(const std::string& filename);
        
        // Clear all entries
        void clear() { entries_.clear(); index_.clear(); }
        
        std::size_t size() const noexcept { return entries_.size(); }
        bool empty() const noexcept { return entries_.empty(); }

    private:
        std::vector<Entry> entries_;
        std::unordered_map<std::uint32_t, std::vector<std::size_t>> index_;
        
        void build_index();
        double compute_differential_probability(std::uint32_t input, std::uint32_t output) const;
    };

    // ========================================================================
    // Configuration and parameter validation
    // ========================================================================
    
    class ConfigValidator {
    public:
        struct ValidationResult {
            bool valid;
            std::vector<std::string> errors;
            std::vector<std::string> warnings;
        };

        // Validate parameters for differential analysis
        static ValidationResult validate_diff_params(int rounds, int weight_cap, 
                                                     std::uint32_t start_dA, std::uint32_t start_dB);
        
        // Validate parameters for linear analysis
        static ValidationResult validate_linear_params(int rounds, int weight_cap, 
                                                      std::uint32_t start_mA, std::uint32_t start_mB);
        
        // Estimate resource requirements
        struct ResourceEstimate {
            std::uint64_t estimated_memory_mb;
            std::uint64_t estimated_time_seconds;
            int recommended_threads;
            bool suitable_for_personal_computer;
        };
        
        static ResourceEstimate estimate_resources(int rounds, int weight_cap, 
                                                  const std::string& algorithm);
    };

    // ========================================================================
    // Performance utilities
    // ========================================================================
    
    class PerformanceUtils {
    public:
        // Timer for measuring execution time
        class Timer {
        public:
            Timer() : start_time_(std::chrono::high_resolution_clock::now()) {}
            
            void reset() { start_time_ = std::chrono::high_resolution_clock::now(); }
            
            std::uint64_t elapsed_ms() const {
                auto end = std::chrono::high_resolution_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_time_).count();
            }
            
            double elapsed_seconds() const {
                auto end = std::chrono::high_resolution_clock::now();
                return std::chrono::duration<double>(end - start_time_).count();
            }
            
        private:
            std::chrono::high_resolution_clock::time_point start_time_;
        };

        // Memory usage estimation
        static std::uint64_t estimate_memory_usage(std::uint64_t num_states, 
                                                  std::uint64_t state_size_bytes);
        
        // CPU detection
        static int detect_cpu_cores();
        static bool has_popcount_instruction();
        
        // Optimal parameter suggestions
        static int suggest_weight_cap_for_personal_computer(int rounds);
        static int suggest_max_rounds_for_personal_computer(int weight_cap);
    };

    // ========================================================================
    // String and formatting utilities
    // ========================================================================
    
    class StringUtils {
    public:
        // Convert integer to hex string
        static std::string to_hex(std::uint32_t value, bool uppercase = true, bool prefix = true);
        
        // Parse hex string to integer
        static std::uint32_t from_hex(const std::string& hex_str);
        
        // Format large numbers with separators
        static std::string format_number(std::uint64_t number, char separator = ',');
        
        // Format duration in human-readable form
        static std::string format_duration(std::uint64_t milliseconds);
        
        // Generate progress indicator
        static std::string progress_bar(double percentage, int width = 40);
        
        // Trim whitespace from string
        static std::string trim(const std::string& str);
        
        // Split string by delimiter
        static std::vector<std::string> split(const std::string& str, char delimiter);
    };

private:
    // Private constructor - this is a utility class with only static methods
    UtilityTools() = delete;
};

// ============================================================================
// Template implementations (must be in header)
// ============================================================================

template<typename ResultT>
bool UtilityTools::TrailExporter::export_topN_csv(const std::string& filename, 
                                                  const std::vector<ResultT>& results, int N) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) return false;
    
    // Write header
    file << "Rank,Weight,StateA,StateB,Algorithm,Timestamp\n";
    
    // Write top N results
    int count = std::min(N, static_cast<int>(results.size()));
    for (int i = 0; i < count; ++i) {
        const auto& result = results[i];
        file << (i + 1) << ","
             << result.weight << ","
             << hex_format(result.state_a) << ","
             << hex_format(result.state_b) << ","
             << result.algorithm << ","
             << std::chrono::duration_cast<std::chrono::seconds>(
                    result.timestamp.time_since_epoch()).count() << "\n";
    }
    
    return true;
}

} // namespace neoalz