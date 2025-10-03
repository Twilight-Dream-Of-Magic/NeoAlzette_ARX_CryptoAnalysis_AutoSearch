#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "pddt.hpp"
#include "../MEDCP/lm_fast.hpp"

namespace neoalz {

struct LbCache {
    // cache key: (dA<<32) | dB
    std::unordered_map<uint64_t,int> cache;

    template<typename Yield>
    static inline void enumerate_min_weight(uint32_t alpha, uint32_t beta, int n, int cap, Yield&& yield){
        // Enumerate gammas with prefix-prune to find *minimal* LM weight for given alpha,beta.
        int best = cap;
        enumerate_lm_gammas(alpha, beta, n, cap, [&](uint32_t /*g*/, int w){
            if (w < best){ best = w; yield(best); }
        });
        yield(best);
    }

    // Lower bound for *current* round: only the first two additions (Subround 0),
    // computed exactly by taking minima for var–var and var–const independently.
    int lb_first_two(uint32_t dA, uint32_t dB, int n = 32, int cap = 64){
        uint64_t key = (uint64_t(dA) << 32) | dB;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        // (1) var–var: B' = B + (rotl(A,31) ^ rotl(A,17) ^ RC[0])
        uint32_t alpha0 = dB;
        uint32_t beta0  = rotl(dA,31) ^ rotl(dA,17);

        int w1_min = cap;
        enumerate_lm_gammas(alpha0, beta0, n, cap, [&](uint32_t /*gB1*/, int w1){
            if (w1 < w1_min) w1_min = w1;
        });

        // (2) var–const: A' = A - RC[1]
        uint32_t bconst1 = (uint32_t)(-int32_t(0xC117176A)); // RC[1]
        int w2_min = cap;
        enumerate_lm_gammas(dA, bconst1, n, cap, [&](uint32_t /*gA1*/, int w2){
            if (w2 < w2_min) w2_min = w2;
        });

        int lb = std::min(cap, w1_min + w2_min);
        cache.emplace(key, lb);
        return lb;
    }
};

} // namespace neoalz
