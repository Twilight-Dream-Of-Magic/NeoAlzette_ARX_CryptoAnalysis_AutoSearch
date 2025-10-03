#pragma once
#include <cstdint>
#include <vector>
#include <queue>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <limits>

/*
 * Algorithm 2: Matsui Threshold Search using pDDT (highway/country-roads).
 * Reference: "Automatic Search for Differential Trails in ARX Ciphers".
 *
 * This is a generic engine. You supply:
 *   - R: number of rounds
 *   - next_states(input_diff, round_index, weight_cap) -> vector of (output_diff, added_weight)
 *   - a weight cap (max sum of weights allowed)
 *
 * We maintain best-known probs (min weights) for prefixes/suffixes (the "highways"),
 * and explore partial trails whose running weight + optimistic lower bound < best.
 */

namespace neoalz {

template<typename DiffT>
struct TrailNode {
    DiffT diff;
    int   r;       // depth (round)
    int   w;       // accumulated weight
    int   lb;      // lower bound (for PQ ordering)
    bool operator<(const TrailNode& other) const {
        return lb > other.lb; // min-heap by lb
    }
};

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

} // namespace neoalz
