#pragma once
#include <cstdint>
#include <vector>
#include <queue>
#include <functional>
#include <unordered_map>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <chrono>
#include <string>
#include <optional>
#include "neoalzette_core.hpp"

namespace neoalz {

/**
 * Threshold Search Framework - Matsui algorithms and parallel search optimization
 * 
 * This class encapsulates threshold search algorithms, including the original Matsui
 * Algorithm 1 & 2, optimized parallel implementations, and state management utilities.
 */
class ThresholdSearchFramework {
public:
    // ========================================================================
    // Configuration and result structures
    // ========================================================================
    
    template<typename StateT>
    struct TrailNode {
        StateT state;
        int round;
        int weight;
        int lower_bound;
        
        bool operator<(const TrailNode& other) const {
            return lower_bound > other.lower_bound; // min-heap by lower bound
        }
    };

    template<typename StateT>
    struct SearchResult {
        int best_weight;
        StateT best_state;
        std::vector<StateT> trail;
        std::uint64_t nodes_processed;
        std::uint64_t nodes_pruned;
        std::uint64_t elapsed_ms;
        bool search_complete;
    };

    struct SearchConfig {
        int max_rounds;
        int weight_cap;
        int num_threads; // 0 = auto-detect
        bool use_fast_canonical;
        bool enable_pruning;
        std::uint64_t max_nodes;
        
        SearchConfig() : max_rounds(4), weight_cap(25), num_threads(0), 
                        use_fast_canonical(false), enable_pruning(true), max_nodes(1000000) {}
    };

    // ========================================================================
    // Packed state representation for cache efficiency
    // ========================================================================
    
    template<typename StateT>
    class PackedState {
    public:
        static_assert(sizeof(StateT) <= 16, "StateT too large for efficient packing");
        
        PackedState() = default;
        explicit PackedState(const StateT& state) : data_(state) {}
        
        const StateT& get() const noexcept { return data_; }
        void set(const StateT& state) noexcept { data_ = state; }
        
        std::size_t hash() const noexcept {
            return std::hash<StateT>{}(data_);
        }
        
        bool operator==(const PackedState& other) const noexcept {
            return data_ == other.data_;
        }

    private:
        StateT data_;
    };

    // ========================================================================
    // Thread-safe work queue for parallel search
    // ========================================================================
    
    template<typename NodeT>
    class WorkQueue {
    public:
        bool push(NodeT&& node);
        bool pop(NodeT& node);
        void finish();
        std::size_t size() const;
        bool empty() const;

    private:
        std::priority_queue<NodeT> queue_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<bool> finished_{false};
    };

    // ========================================================================
    // Matsui Algorithm implementations
    // ========================================================================
    
    // Standard Matsui threshold search (original paper algorithm)
    template<typename StateT, typename NextFunc, typename LbFunc>
    static SearchResult<StateT> matsui_threshold_search(
        int max_rounds,
        const StateT& initial_state,
        int weight_cap,
        NextFunc&& next_states,  // (state, round, slack_weight) -> vector<pair<StateT,int>>
        LbFunc&& lower_bound     // (state, round) -> int optimistic remaining weight
    );

    // Optimized parallel threshold search
    template<typename StateT, typename NextFunc, typename LbFunc>
    static SearchResult<StateT> matsui_threshold_search_parallel(
        int max_rounds,
        const StateT& initial_state,
        int weight_cap,
        NextFunc&& next_states,
        LbFunc&& lower_bound,
        const SearchConfig& config = {}
    );

    // ========================================================================
    // PDDT (Partial Difference Distribution Table) for Algorithm 1
    // ========================================================================
    
    template<std::size_t N>
    class PDDT {
    public:
        struct Entry {
            std::uint32_t input_diff;
            std::uint32_t output_diff;
            double probability;
            int weight;
        };

        PDDT() = default;
        
        void build(int threshold_weight);
        void build_optimized(int threshold_weight, int num_threads = 0);
        
        std::vector<Entry> query(std::uint32_t input_diff, int max_weight) const;
        std::size_t size() const noexcept { return entries_.size(); }
        
        bool save(const std::string& filename) const;
        bool load(const std::string& filename);

    private:
        std::vector<Entry> entries_;
        std::unordered_map<std::uint32_t, std::vector<std::size_t>> index_;
        
        void build_index();
        double compute_probability(std::uint32_t input_diff, std::uint32_t output_diff) const;
    };

    // ========================================================================
    // Complete Matsui Algorithm 2 with highways/country roads
    // ========================================================================
    
    class MatsuiComplete {
    public:
        struct Config {
            int rounds;
            int weight_threshold;
            bool use_highways;
            bool use_country_roads;
            std::string highway_file;
        };

        struct Result {
            std::vector<std::uint32_t> best_trail;
            int best_weight;
            std::uint64_t total_nodes;
            std::uint64_t pruned_nodes;
            double elapsed_seconds;
        };

        static Result algorithm_2_complete(const Config& config);
        
    private:
        struct RoundState {
            std::uint32_t differential;
            int round;
            int accumulated_weight;
            std::vector<std::uint32_t> trail;
            
            bool operator<(const RoundState& other) const {
                return accumulated_weight > other.accumulated_weight; // min-heap by weight
            }
        };
        
        static void process_highways_strategy(const Config& config, Result& result);
        static void process_country_roads_strategy(const Config& config, Result& result);
    };

    // ========================================================================
    // Performance monitoring and statistics
    // ========================================================================
    
    class PerformanceMonitor {
    public:
        void start();
        void record_node_processed();
        void record_node_pruned();
        void record_cache_hit();
        void record_cache_miss();
        
        struct Stats {
            std::uint64_t total_nodes;
            std::uint64_t pruned_nodes;
            std::uint64_t cache_hits;
            std::uint64_t cache_misses;
            double pruning_rate;
            double cache_hit_rate;
            std::uint64_t elapsed_ms;
        };
        
        Stats get_stats() const;

    private:
        std::chrono::high_resolution_clock::time_point start_time_;
        std::atomic<std::uint64_t> nodes_processed_{0};
        std::atomic<std::uint64_t> nodes_pruned_{0};
        std::atomic<std::uint64_t> cache_hits_{0};
        std::atomic<std::uint64_t> cache_misses_{0};
    };

    // ========================================================================
    // Utility methods
    // ========================================================================
    
    // Auto-detect optimal number of threads
    static int detect_optimal_threads();
    
    // Canonicalization functions (fast and precise variants)
    static std::pair<std::uint32_t, std::uint32_t> canonical_rotate_pair(std::uint32_t a, std::uint32_t b);
    static std::pair<std::uint32_t, std::uint32_t> canonical_rotate_pair_fast(std::uint32_t a, std::uint32_t b);

private:
    // Internal implementation details
    template<typename StateT>
    static std::uint64_t hash_state(const StateT& state, int round) noexcept;
    
    template<typename StateT>
    static bool should_prune(const StateT& state, int current_weight, int weight_cap);
};

// ============================================================================
// Template implementations (must be in header)
// ============================================================================

template<typename StateT, typename NextFunc, typename LbFunc>
ThresholdSearchFramework::SearchResult<StateT> 
ThresholdSearchFramework::matsui_threshold_search(
    int max_rounds, const StateT& initial_state, int weight_cap,
    NextFunc&& next_states, LbFunc&& lower_bound) {
    
    using Node = TrailNode<StateT>;
    std::priority_queue<Node> pq;
    
    SearchResult<StateT> result;
    result.best_weight = std::numeric_limits<int>::max();
    result.best_state = initial_state;
    result.nodes_processed = 0;
    result.nodes_pruned = 0;
    result.search_complete = false;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Initialize with root node
    int initial_lb = lower_bound(initial_state, 0);
    if (initial_lb < weight_cap) {
        pq.push({initial_state, 0, 0, initial_lb});
    }
    
    while (!pq.empty()) {
        Node current = pq.top();
        pq.pop();
        result.nodes_processed++;
        
        // Pruning check
        if (current.lower_bound >= std::min(result.best_weight, weight_cap)) {
            result.nodes_pruned++;
            continue;
        }
        
        // Terminal check
        if (current.round == max_rounds) {
            if (current.weight < result.best_weight) {
                result.best_weight = current.weight;
                result.best_state = current.state;
            }
            continue;
        }
        
        // Expand node
        int slack = std::min(result.best_weight, weight_cap) - current.weight;
        auto children = next_states(current.state, current.round, slack);
        
        for (const auto& [child_state, add_weight] : children) {
            int new_weight = current.weight + add_weight;
            int new_lb = new_weight + lower_bound(child_state, current.round + 1);
            
            if (new_lb < std::min(result.best_weight, weight_cap)) {
                pq.push({child_state, current.round + 1, new_weight, new_lb});
            } else {
                result.nodes_pruned++;
            }
        }
        
        // Early termination check
        if (result.nodes_processed > 1000000) break;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    result.search_complete = pq.empty();
    
    return result;
}

template<typename StateT>
std::uint64_t ThresholdSearchFramework::hash_state(const StateT& state, int round) noexcept {
    return std::hash<StateT>{}(state) ^ (std::uint64_t(round) << 32);
}

} // namespace neoalz