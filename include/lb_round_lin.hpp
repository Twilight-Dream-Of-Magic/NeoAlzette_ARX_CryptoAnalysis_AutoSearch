#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "wallen_fast.hpp"
#include "mask_backtranspose.hpp"

namespace neoalz {

struct LbFullRoundLin {
    std::unordered_map<uint64_t,int> cache;

    // Compute tight-ish LB for one full round from (mA0,mB0) by
    //  - exploring top-K masks for the first two additions (Wallén),
    //  - push through linear layers exactly using (L^{-1})^T,
    //  - exploring minimal weights for the next two additions.
    int lb_full(uint32_t mA0, uint32_t mB0, int K1=4, int K2=4, int n=32, int cap=64){
        uint64_t key = ( (uint64_t)mA0 << 32 ) | mB0;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        struct Cand { uint32_t A2m, B2m; int w; };
        std::vector<Cand> stage1; stage1.reserve((size_t)K1*K2);

        // (1) B += F(A)
        uint32_t mu1 = mB0;
        uint32_t nu1 = rotr(mA0,31) ^ rotr(mA0,17);
        std::vector<std::pair<uint32_t,int>> G1; G1.reserve((size_t)K1*2);
        enumerate_wallen_omegas(mu1, nu1, cap, [&](uint32_t Bp_mask, int w){ G1.emplace_back(Bp_mask, w); });
        std::sort(G1.begin(), G1.end(), [](auto&a, auto&b){ return a.second<b.second; });
        if ((int)G1.size()>K1) G1.resize(K1);

        // (2) A -= RC (var-const)
        uint32_t mu2 = mA0; uint32_t nu2 = 0u;
        std::vector<std::pair<uint32_t,int>> G2; G2.reserve((size_t)K2*2);
        enumerate_wallen_omegas(mu2, nu2, cap, [&](uint32_t Ap_mask, int w){ G2.emplace_back(Ap_mask, w); });
        std::sort(G2.begin(), G2.end(), [](auto&a, auto&b){ return a.second<b.second; });
        if ((int)G2.size()>K2) G2.resize(K2);

        for (auto [Bp, w1] : G1){
            for (auto [Ap, w2] : G2){
                int w12 = w1 + w2; if (w12 >= cap) continue;
                // linear diffusion with exact backtranspose
                uint32_t A2m = l1_backtranspose_exact( Ap ^ rotl(Bp,24) );
                uint32_t B2m = l2_backtranspose_exact( Bp ^ rotl(Ap ^ rotl(Bp,24),16) );
                stage1.push_back({A2m, B2m, w12});
            }
        }

        int best = cap;
        for (auto &c : stage1){
            // (3) A* += F(B2)
            uint32_t mu3 = c.A2m;
            uint32_t nu3 = rotr(c.B2m,31) ^ rotr(c.B2m,17);
            int w3_min = cap;
            enumerate_wallen_omegas(mu3, nu3, best - c.w, [&](uint32_t /*A3*/, int w3){ if (w3 < w3_min) w3_min = w3; });
            if (c.w + w3_min >= best) continue;

            // (4) B -= RC (var-const)
            uint32_t mu4 = c.B2m; uint32_t nu4 = 0u;
            int w4_min = cap;
            enumerate_wallen_omegas(mu4, nu4, best - c.w - w3_min, [&](uint32_t /*B3*/, int w4){ if (w4 < w4_min) w4_min = w4; });

            int total = c.w + w3_min + w4_min;
            if (total < best) best = total;
        }

        cache.emplace(key, best);
        return best;
    }
};

} // namespace neoalz

