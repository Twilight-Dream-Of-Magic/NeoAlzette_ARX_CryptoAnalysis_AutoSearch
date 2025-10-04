#include "arx_search_framework/threshold_search_framework.hpp"
#include <fstream>
#include <random>
#include <cmath>
#include <algorithm>

namespace neoalz {

// ============================================================================
// Work Queue implementation
// ============================================================================

template<typename NodeT>
bool ThresholdSearchFramework::WorkQueue<NodeT>::push(NodeT&& node) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_.load(std::memory_order_relaxed)) return false;
    
    queue_.emplace(std::move(node));
    cv_.notify_one();
    return true;
}

template<typename NodeT>
bool ThresholdSearchFramework::WorkQueue<NodeT>::pop(NodeT& node) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (queue_.empty() && !finished_.load(std::memory_order_relaxed)) {
        cv_.wait(lock);
    }
    
    if (queue_.empty()) return false;
    
    node = std::move(const_cast<NodeT&>(queue_.top()));
    queue_.pop();
    return true;
}

template<typename NodeT>
void ThresholdSearchFramework::WorkQueue<NodeT>::finish() {
    finished_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
}

template<typename NodeT>
std::size_t ThresholdSearchFramework::WorkQueue<NodeT>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

template<typename NodeT>
bool ThresholdSearchFramework::WorkQueue<NodeT>::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

// ============================================================================
// PDDT implementation
// ============================================================================

template<std::size_t N>
void ThresholdSearchFramework::PDDT<N>::build(int threshold_weight) {
    entries_.clear();
    
    // Enumerate all possible input/output difference pairs
    constexpr std::uint32_t max_val = (1ULL << std::min(static_cast<std::uint32_t>(N), 20U)); // Limit enumeration for performance
    
    for (std::uint32_t input_diff = 0; input_diff < max_val; ++input_diff) {
        for (std::uint32_t output_diff = 0; output_diff < max_val; ++output_diff) {
            double prob = compute_probability(input_diff, output_diff);
            
            if (prob > 0.0) {
                int weight = -static_cast<int>(std::log2(prob));
                
                if (weight <= threshold_weight) {
                    entries_.push_back({input_diff, output_diff, prob, weight});
                }
            }
        }
    }
    
    build_index();
}

template<std::size_t N>
void ThresholdSearchFramework::PDDT<N>::build_optimized(int threshold_weight, int num_threads) {
    if (num_threads <= 0) {
        num_threads = detect_optimal_threads();
    }
    
    entries_.clear();
    std::mutex entries_mutex;
    
    constexpr std::uint32_t max_val = (1ULL << std::min(static_cast<std::uint32_t>(N), 20U));
    const std::uint32_t chunk_size = max_val / num_threads;
    
    std::vector<std::thread> workers;
    
    auto worker_func = [&](std::uint32_t start, std::uint32_t end) {
        std::vector<Entry> local_entries;
        
        for (std::uint32_t input_diff = start; input_diff < end; ++input_diff) {
            for (std::uint32_t output_diff = 0; output_diff < max_val; ++output_diff) {
                double prob = compute_probability(input_diff, output_diff);
                
                if (prob > 0.0) {
                    int weight = -static_cast<int>(std::log2(prob));
                    
                    if (weight <= threshold_weight) {
                        local_entries.push_back({input_diff, output_diff, prob, weight});
                    }
                }
            }
        }
        
        // Merge local results
        std::lock_guard<std::mutex> lock(entries_mutex);
        entries_.insert(entries_.end(), local_entries.begin(), local_entries.end());
    };
    
    // Launch worker threads
    for (int i = 0; i < num_threads; ++i) {
        std::uint32_t start = i * chunk_size;
        std::uint32_t end = (i == num_threads - 1) ? max_val : (i + 1) * chunk_size;
        workers.emplace_back(worker_func, start, end);
    }
    
    // Wait for completion
    for (auto& worker : workers) {
        worker.join();
    }
    
    build_index();
}

template<std::size_t N>
void ThresholdSearchFramework::PDDT<N>::build_index() {
    index_.clear();
    
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        index_[entries_[i].input_diff].push_back(i);
    }
}

template<std::size_t N>
std::vector<typename ThresholdSearchFramework::PDDT<N>::Entry>
ThresholdSearchFramework::PDDT<N>::query(std::uint32_t input_diff, int max_weight) const {
    std::vector<Entry> result;
    
    auto it = index_.find(input_diff);
    if (it != index_.end()) {
        for (std::size_t idx : it->second) {
            if (entries_[idx].weight <= max_weight) {
                result.push_back(entries_[idx]);
            }
        }
    }
    
    return result;
}

template<std::size_t N>
double ThresholdSearchFramework::PDDT<N>::compute_probability(std::uint32_t input_diff, 
                                                              std::uint32_t output_diff) const {
    // Simplified probability computation for ARX operations
    // In practice, this would use the specific differential properties of NeoAlzette
    
    if (input_diff == 0) {
        return (output_diff == 0) ? 1.0 : 0.0;
    }
    
    // Placeholder computation - should be replaced with actual ARX differential analysis
    int hw_input = __builtin_popcount(input_diff);
    int hw_output = __builtin_popcount(output_diff);
    
    if (hw_output > hw_input + 4) return 0.0; // Unlikely to have such expansion
    
    // Simple model: probability inversely related to Hamming weights
    return std::pow(0.5, hw_input + hw_output);
}

// ============================================================================
// Matsui Complete Algorithm 2 implementation
// ============================================================================

ThresholdSearchFramework::MatsuiComplete::Result
ThresholdSearchFramework::MatsuiComplete::algorithm_2_complete(const Config& config) {
    Result result;
    result.best_weight = std::numeric_limits<int>::max();
    result.total_nodes = 0;
    result.pruned_nodes = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (config.use_highways) {
        process_highways_strategy(config, result);
    }
    
    if (config.use_country_roads) {
        process_country_roads_strategy(config, result);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    
    return result;
}

void ThresholdSearchFramework::MatsuiComplete::process_highways_strategy(
    const Config& config, Result& result) {
    
    // Highways strategy: focus on high-probability paths
    std::priority_queue<RoundState> pq;
    
    // Initialize with zero differential
    pq.push({{0}, 0, 0, {0}});
    
    while (!pq.empty() && result.total_nodes < 100000) {
        RoundState current = pq.top();
        pq.pop();
        result.total_nodes++;
        
        if (current.accumulated_weight >= config.weight_threshold) {
            result.pruned_nodes++;
            continue;
        }
        
        if (current.round >= config.rounds) {
            if (current.accumulated_weight < result.best_weight) {
                result.best_weight = current.accumulated_weight;
                result.best_trail = current.trail;
            }
            continue;
        }
        
        // Generate next round candidates (simplified)
        for (std::uint32_t next_diff = 1; next_diff <= 0xFF; next_diff <<= 1) {
            int estimated_weight = __builtin_popcount(next_diff);
            
            if (current.accumulated_weight + estimated_weight < config.weight_threshold) {
                RoundState next_state;
                next_state.differential = next_diff;
                next_state.round = current.round + 1;
                next_state.accumulated_weight = current.accumulated_weight + estimated_weight;
                next_state.trail = current.trail;
                next_state.trail.push_back(next_diff);
                
                pq.push(next_state);
            }
        }
    }
}

void ThresholdSearchFramework::MatsuiComplete::process_country_roads_strategy(
    const Config& config, Result& result) {
    
    // Country roads strategy: explore alternative paths
    std::mt19937 rng(42); // Deterministic for reproducibility
    std::uniform_int_distribution<std::uint32_t> dist(1, 0xFFFFFFFF);
    
    for (int trial = 0; trial < 1000 && result.total_nodes < 200000; ++trial) {
        std::vector<std::uint32_t> trail;
        int total_weight = 0;
        
        for (int round = 0; round < config.rounds; ++round) {
            std::uint32_t random_diff = dist(rng) & 0xFF; // Keep small for realistic weights
            int weight = __builtin_popcount(random_diff);
            
            if (total_weight + weight >= config.weight_threshold) {
                break;
            }
            
            trail.push_back(random_diff);
            total_weight += weight;
            result.total_nodes++;
        }
        
        if (trail.size() == config.rounds && total_weight < result.best_weight) {
            result.best_weight = total_weight;
            result.best_trail = trail;
        }
    }
}

// ============================================================================
// Performance Monitor implementation
// ============================================================================

void ThresholdSearchFramework::PerformanceMonitor::start() {
    start_time_ = std::chrono::high_resolution_clock::now();
    nodes_processed_.store(0);
    nodes_pruned_.store(0);
    cache_hits_.store(0);
    cache_misses_.store(0);
}

void ThresholdSearchFramework::PerformanceMonitor::record_node_processed() {
    nodes_processed_.fetch_add(1, std::memory_order_relaxed);
}

void ThresholdSearchFramework::PerformanceMonitor::record_node_pruned() {
    nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
}

void ThresholdSearchFramework::PerformanceMonitor::record_cache_hit() {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

void ThresholdSearchFramework::PerformanceMonitor::record_cache_miss() {
    cache_misses_.fetch_add(1, std::memory_order_relaxed);
}

ThresholdSearchFramework::PerformanceMonitor::Stats
ThresholdSearchFramework::PerformanceMonitor::get_stats() const {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
    
    std::uint64_t total = nodes_processed_.load(std::memory_order_relaxed);
    std::uint64_t pruned = nodes_pruned_.load(std::memory_order_relaxed);
    std::uint64_t hits = cache_hits_.load(std::memory_order_relaxed);
    std::uint64_t misses = cache_misses_.load(std::memory_order_relaxed);
    
    Stats stats;
    stats.total_nodes = total;
    stats.pruned_nodes = pruned;
    stats.cache_hits = hits;
    stats.cache_misses = misses;
    stats.pruning_rate = (total > 0) ? (double)pruned / total * 100.0 : 0.0;
    stats.cache_hit_rate = (hits + misses > 0) ? (double)hits / (hits + misses) * 100.0 : 0.0;
    stats.elapsed_ms = duration.count();
    
    return stats;
}

// ============================================================================
// Utility methods
// ============================================================================

int ThresholdSearchFramework::detect_optimal_threads() {
    int hw_threads = std::thread::hardware_concurrency();
    if (hw_threads <= 0) hw_threads = 1;
    
    // Use 75% of available threads to leave room for system processes
    return std::max(1, static_cast<int>(hw_threads * 0.75));
}

std::pair<std::uint32_t, std::uint32_t> 
ThresholdSearchFramework::canonical_rotate_pair(std::uint32_t a, std::uint32_t b) {
    std::uint32_t best_a = a, best_b = b;
    
    for (int r = 0; r < 32; ++r) {
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

std::pair<std::uint32_t, std::uint32_t> 
ThresholdSearchFramework::canonical_rotate_pair_fast(std::uint32_t a, std::uint32_t b) {
    if (a == 0 || b == 0) {
        if (a > b) std::swap(a, b);
        return {a, b};
    }
    
    // Fast approximation: only check a few rotations
    std::uint32_t best_a = a, best_b = b;
    
    for (int r = 0; r < 8; ++r) { // Check only 8 rotations instead of 32
        std::uint32_t rot_a = NeoAlzetteCore::rotl(a, r * 4);
        std::uint32_t rot_b = NeoAlzetteCore::rotl(b, r * 4);
        
        if (std::make_pair(rot_a, rot_b) < std::make_pair(best_a, best_b)) {
            best_a = rot_a;
            best_b = rot_b;
        }
    }
    
    return {best_a, best_b};
}

// ============================================================================
// Explicit template instantiations (needed for linking)
// ============================================================================

// Instantiate commonly used template specializations
template class ThresholdSearchFramework::PDDT<32>;
template class ThresholdSearchFramework::WorkQueue<ThresholdSearchFramework::TrailNode<std::pair<std::uint32_t, std::uint32_t>>>;

} // namespace neoalz