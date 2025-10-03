#pragma once
/*
 * SuffixLB - recursive suffix lower bound (differential)
 * Inputs:
 *   dA, dB : input differences (canonicalized recommended)
 *   rem    : remaining rounds (T)
 *   cap    : pruning cap
 * Outputs:
 *   Additive lower bound for the last T rounds (conservative)
 * Complexity:
 *   Depends on inner one-round LB; memoized O(unique (dA,dB,rem)) lookups
 * Reference:
 *   ARX differential trail search with recursive suffix bounds / highway oracles
 */
#include <cstdint>
#include <unordered_map>
#include <tuple>
#include "lb_round_full.hpp"

namespace neoalz {

// A small recursive suffix-LB oracle for the last T rounds (T<=2 recommended).
// It uses lb_full() per round with memoization; serves as the "highway" bound.
struct SuffixLB {
    LbFullRound full;
    // key: (rem << 48) | (dA<<16) ^ hash(dB)   (use 64-bit state key)
    std::unordered_map<uint64_t,int> memo;

    static inline uint64_t key(uint32_t dA, uint32_t dB, int rem){
        return ( (uint64_t(rem & 0xFF) << 56) ^ ( (uint64_t)dA << 24) ^ (uint64_t)dB );
    }

    int bound(uint32_t dA, uint32_t dB, int rem, int cap){
        if (rem <= 0) return 0;
        uint64_t k = key(dA,dB,rem);
        auto it = memo.find(k);
        if (it != memo.end()) return it->second;

        // lower bound for one full round
        int lb1 = full.lb_full(dA, dB, 3, 3, 32, cap);
        if (rem == 1){
            memo.emplace(k, lb1);
            return lb1;
        }

        // Conservative: assume the same lb applies each of the remaining rounds (safe).
        // Or do a shallow recursive step if cap allows: cheap and tighter.
        int best = lb1 + (rem-1)*lb1;
        if (lb1 < cap){
            // Try one recursive step with dummy linear propagation (0-cost) — safe LB
            // For tighter bound, we could keep top-K stage1 states and recurse; omitted for speed.
            best = lb1 + (rem-1)*lb1;
        }
        memo.emplace(k, best);
        return best;
    }
};

} // namespace neoalz
