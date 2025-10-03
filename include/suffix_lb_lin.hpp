#pragma once
#include <cstdint>
#include <unordered_map>
#include "lb_round_lin.hpp"

namespace neoalz {

struct SuffixLBLin {
    LbFullRoundLin round;
    std::unordered_map<uint64_t,int> memo;

    static inline uint64_t key(uint32_t mA, uint32_t mB, int rem){
        return ( (uint64_t(rem & 0xFF) << 56) ^ ( (uint64_t)mA << 24) ^ (uint64_t)mB );
    }

    int bound(uint32_t mA, uint32_t mB, int rem, int cap){
        if (rem <= 0) return 0;
        uint64_t k = key(mA,mB,rem);
        auto it = memo.find(k);
        if (it != memo.end()) return it->second;
        int lb1 = round.lb_full(mA, mB, 3, 3, 32, cap);
        int best = lb1 + (rem-1)*lb1; // conservative product
        memo.emplace(k, best);
        return best;
    }
};

} // namespace neoalz