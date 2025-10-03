\
#include <cstdint>
#include <vector>
#include <queue>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <tuple>
#include "include/lb_round_full.hpp"
#include "include/suffix_lb.hpp"
#include "include/highway_table.hpp"

namespace neoalz {

struct DiffState { uint32_t dA, dB; int w; int r; };

struct Cmp {
    bool operator()(const DiffState& a, const DiffState& b) const noexcept {
        return a.w > b.w; // min-heap
    }
};

} // namespace neoalz

int main(int argc, char** argv){
    using namespace neoalz;
    if (argc < 5){
        std::fprintf(stderr, "Usage: %s R Wcap BEAM [highway.bin]\n", argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);
    int BEAM = std::stoi(argv[3]);

    LbFullRound LBF;
    SuffixLB    SFX;
    HighwayTable HW;
    bool use_hw = false;
    if (argc >= 5){
        use_hw = HW.load(argv[4]);
        if (use_hw) std::fprintf(stderr, "[Info] Loaded Highway table from %s\n", argv[4]);
    }

    auto lower_bound = [&](const DiffState& s){
        int rem = R - s.r;
        int lb_tail = 0;
        if (rem>0){
            if (use_hw) lb_tail = HW.query(s.dA, s.dB, rem);
            else        lb_tail = SFX.bound(s.dA, s.dB, rem, 64);
        }
        return lb_tail;
    };

    std::vector<DiffState> cur, nxt;
    cur.push_back({0u,0u,0,0}); // root

    int best = std::numeric_limits<int>::max();
    int round = 0;

    while (!cur.empty()){
        // Expand one round using best-first within a beam
        std::priority_queue<DiffState, std::vector<DiffState>, Cmp> pq;
        for (auto& s : cur) pq.push(s);

        nxt.clear();
        int pushed = 0;

        while (!pq.empty() && pushed < BEAM){
            auto s = pq.top(); pq.pop();
            if (s.w >= std::min(best, Wcap)) continue;
            if (s.r == R){
                best = std::min(best, s.w);
                continue;
            }
            // Use full-round LB to generate a proxy next-state (no state choice here; for demo)
            // In practice we should enumerate LM-feasible local choices then keep top-K children.
            int lb_round = LBF.lb_full(s.dA, s.dB, 4,4, 32, 64);
            DiffState child = s;
            child.w += lb_round;
            child.r += 1;

            int lb = lower_bound(child);
            if (child.w + lb < std::min(best, Wcap)){
                nxt.push_back(child);
                ++pushed;
            }
        }

        cur.swap(nxt);
        ++round;
        if (round > R) break;
    }

    if (best < std::numeric_limits<int>::max()){
        std::printf("Beam best diff weight R=%d, BEAM=%d: %d\n", R, BEAM, best);
        return 0;
    } else {
        std::puts("No trail below cap.");
        return 2;
    }
}
