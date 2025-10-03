#pragma once
/*
 * Unified Threshold Search Framework
 * 
 * Combines threshold_search.hpp and threshold_search_optimized.hpp
 * Provides both standard Matsui algorithm and optimized parallel versions
 */

#include <cstdint>
#include <vector>
#include <queue>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace neoalz {

// Standard trail node for basic threshold search
template<typename DiffT>
struct TrailNode {
    DiffT diff;
    int   r;       // depth (round index)
    int   w;       // accumulated weight so far
    int   lb;      // lower bound for remaining suffix (for PQ ordering)
    bool operator<(const TrailNode& other) const {
        return lb > other.lb; // min-heap by lb
    }
};

// Optimized packed state for performance
template<typename DiffT>
struct PackedState {
    static_assert(sizeof(DiffT) <= 16, "DiffT too large for efficient packing");
    
    DiffT diff;
    uint32_t round_and_weight; // packed: round(8 bits) + weight(24 bits)
    
    PackedState() = default;
    PackedState(const DiffT& d, int r, int w) : diff(d) {
        round_and_weight = ((uint32_t(r) & 0xFF) << 24) | (uint32_t(w) & 0xFFFFFF);
    }
    
    int round() const noexcept { return (round_and_weight >> 24) & 0xFF; }
    int weight() const noexcept { return round_and_weight & 0xFFFFFF; }
    
    size_t hash() const noexcept {
        size_t h1 = std::hash<uint64_t>{}(*(uint64_t*)&diff);
        size_t h2 = std::hash<uint32_t>{}(round_and_weight);
        return h1 ^ (h2 << 1);
    }
};

// Standard Matsui threshold search (original paper algorithm)
template<typename DiffT, typename NextFunc, typename LbFunc>
auto matsui_threshold_search(
    int R,
    const DiffT& diff0,
    int weight_cap,
    NextFunc&& next_states,  // (diff, r, slack_w) -> vector<pair<DiffT,int>>
    LbFunc&& lower_bound     // (diff, r) -> int optimistic remaining weight
) {
    using Node = TrailNode<DiffT>;
    std::priority_queue<Node> pq;
    int best = std::numeric_limits<int>::max();
    DiffT best_diff = diff0;

    auto push = [&](const DiffT& d, int r, int w){
        int lb = w + lower_bound(d, r);
        if (lb >= std::min(best, weight_cap)) return;
        pq.push(Node{d,r,w,lb});
    };

    push(diff0, 0, 0);
    while(!pq.empty()){
        auto cur = pq.top(); pq.pop();
        if (cur.lb >= std::min(best, weight_cap)) continue;
        if (cur.r == R){
            if (cur.w < best){ best = cur.w; best_diff = cur.diff; }
            continue;
        }
        int slack = std::min(best, weight_cap) - cur.w;
        auto children = next_states(cur.diff, cur.r, slack);
        for (auto& [d2, addw] : children){
            int w2 = cur.w + addw;
            push(d2, cur.r+1, w2);
        }
    }
    return std::make_pair(best, best_diff);
}

// Thread-safe work queue for parallel search
template<typename NodeT>
class WorkQueue {
private:
    std::priority_queue<NodeT> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> finished_{false};
    
public:
    bool push(NodeT&& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (finished_.load(std::memory_order_relaxed)) return false;
        
        queue_.emplace(std::move(node));
        cv_.notify_one();
        return true;
    }
    
    bool pop(NodeT& node) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (queue_.empty() && !finished_.load(std::memory_order_relaxed)) {
            cv_.wait(lock);
        }
        
        if (queue_.empty()) return false;
        
        node = std::move(const_cast<NodeT&>(queue_.top()));
        queue_.pop();
        return true;
    }
    
    void finish() {
        finished_.store(true, std::memory_order_relaxed);
        cv_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

template<typename DiffT>
struct TrailNodeOptimized {
    PackedState<DiffT> state;
    int lb;
    
    bool operator<(const TrailNodeOptimized& other) const noexcept {
        return lb > other.lb;
    }
};

// Optimized parallel threshold search
template<typename DiffT, typename NextFunc, typename LbFunc>
auto matsui_threshold_search_optimized(
    int R,
    const DiffT& diff0,
    int weight_cap,
    NextFunc&& next_states,
    LbFunc&& lower_bound,
    int num_threads = 0
) {
    using Node = TrailNodeOptimized<DiffT>;
    using PackedStateT = PackedState<DiffT>;
    
    if (num_threads <= 0) {
        #ifdef _OPENMP
        num_threads = omp_get_max_threads();
        #else
        num_threads = std::thread::hardware_concurrency();
        #endif
        if (num_threads <= 0) num_threads = 1;
    }
    
    std::atomic<int> global_best{std::numeric_limits<int>::max()};
    DiffT best_diff = diff0;
    std::mutex best_diff_mutex;
    
    if (num_threads == 1) {
        // Single-threaded fallback to standard algorithm
        return matsui_threshold_search(R, diff0, weight_cap, 
                                     std::forward<NextFunc>(next_states),
                                     std::forward<LbFunc>(lower_bound));
    }
    
    WorkQueue<Node> work_queue;
    std::vector<std::thread> workers;
    
    constexpr int NUM_PARTITIONS = 16;
    std::array<std::unordered_map<size_t, int>, NUM_PARTITIONS> visited_partitions;
    std::array<std::mutex, NUM_PARTITIONS> partition_mutexes;
    
    auto get_partition = [](size_t hash) -> int { return hash % NUM_PARTITIONS; };
    
    auto is_visited = [&](const PackedStateT& state, int current_weight) -> bool {
        size_t hash_val = state.hash();
        int partition = get_partition(hash_val);
        
        std::lock_guard<std::mutex> lock(partition_mutexes[partition]);
        auto& visited = visited_partitions[partition];
        
        auto it = visited.find(hash_val);
        if (it != visited.end() && it->second <= current_weight) {
            return true;
        }
        
        visited[hash_val] = current_weight;
        return false;
    };
    
    auto worker_func = [&]() {
        Node current_node;
        
        while (work_queue.pop(current_node)) {
            const auto& state = current_node.state;
            int current_best = global_best.load(std::memory_order_relaxed);
            
            if (current_node.lb >= std::min(current_best, weight_cap)) {
                continue;
            }
            
            if (state.round() == R) {
                int trail_weight = state.weight();
                int expected = global_best.load(std::memory_order_relaxed);
                while (trail_weight < expected && 
                       !global_best.compare_exchange_weak(expected, trail_weight,
                                                         std::memory_order_relaxed)) {
                }
                
                if (trail_weight < current_best) {
                    std::lock_guard<std::mutex> lock(best_diff_mutex);
                    if (trail_weight < global_best.load(std::memory_order_relaxed)) {
                        best_diff = state.diff;
                    }
                }
                continue;
            }
            
            int slack = std::min(current_best, weight_cap) - state.weight();
            if (slack <= 0) continue;
            
            auto children = next_states(state.diff, state.round(), slack);
            
            for (const auto& [child_diff, add_weight] : children) {
                int new_weight = state.weight() + add_weight;
                PackedStateT child_state(child_diff, state.round() + 1, new_weight);
                
                if (is_visited(child_state, new_weight)) {
                    continue;
                }
                
                int child_lb = new_weight + lower_bound(child_diff, state.round() + 1);
                
                if (child_lb < std::min(global_best.load(std::memory_order_relaxed), weight_cap)) {
                    Node child_node;
                    child_node.state = child_state;
                    child_node.lb = child_lb;
                    
                    if (!work_queue.push(std::move(child_node))) {
                        break;
                    }
                }
            }
        }
    };
    
    PackedStateT initial_state(diff0, 0, 0);
    int initial_lb = lower_bound(diff0, 0);
    
    if (initial_lb < weight_cap) {
        Node root_node;
        root_node.state = initial_state;
        root_node.lb = initial_lb;
        work_queue.push(std::move(root_node));
    }
    
    if (num_threads > 1) {
        workers.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back(worker_func);
        }
        
        auto last_size = work_queue.size();
        int idle_count = 0;
        
        while (idle_count < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto current_size = work_queue.size();
            
            if (current_size == 0 && last_size == 0) {
                idle_count++;
            } else {
                idle_count = 0;
            }
            
            last_size = current_size;
        }
        
        work_queue.finish();
        
        for (auto& worker : workers) {
            worker.join();
        }
    } else {
        worker_func();
    }
    
    int final_best = global_best.load(std::memory_order_relaxed);
    return std::make_pair(final_best, best_diff);
}

// Convenience wrapper for automatic optimization selection
template<typename DiffT, typename NextFunc, typename LbFunc>
auto matsui_search_auto(
    int R,
    const DiffT& diff0,
    int weight_cap,
    NextFunc&& next_states,
    LbFunc&& lower_bound,
    bool prefer_optimized = true
) {
    int available_threads = std::thread::hardware_concurrency();
    
    if (prefer_optimized && available_threads > 1 && weight_cap > 20) {
        return matsui_threshold_search_optimized(R, diff0, weight_cap,
                                               std::forward<NextFunc>(next_states),
                                               std::forward<LbFunc>(lower_bound));
    } else {
        return matsui_threshold_search(R, diff0, weight_cap,
                                     std::forward<NextFunc>(next_states),
                                     std::forward<LbFunc>(lower_bound));
    }
}

} // namespace neoalz