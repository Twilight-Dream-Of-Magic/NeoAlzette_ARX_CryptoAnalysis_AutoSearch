#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include "lm_fast.hpp"
#include "pddt.hpp"

namespace neoalz {

struct LbFullRound {
    struct Entry { uint32_t dA, dB; int w; };
    std::unordered_map<uint64_t,int> cache;

    static inline uint32_t l1_forward(uint32_t x) noexcept {
        return x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24);
    }
    static inline uint32_t l2_forward(uint32_t x) noexcept {
        return x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30);
    }
    static inline std::pair<uint32_t,uint32_t> cd_from_B_delta(uint32_t B) noexcept {
        uint32_t c = l2_forward(B);
        uint32_t d = l1_forward(rotr(B,3));
        uint32_t t = rotl(c ^ d, 31);
        c ^= rotl(d,17);
        d ^= rotr(t,16);
        return {c,d};
    }
    static inline std::pair<uint32_t,uint32_t> cd_from_A_delta(uint32_t A) noexcept {
        uint32_t c = l1_forward(A);
        uint32_t d = l2_forward(rotl(A,24));
        uint32_t t = rotr(c ^ d, 31);
        c ^= rotr(d,17);
        d ^= rotl(t,16);
        return {c,d};
    }

    // RC needed for var–const in both subrounds
    static constexpr uint32_t RC1 = 0xC117176A;
    static constexpr uint32_t RC6 = 0x13198102; // RC[6] per your table

    // compute tight LB for one full round from (dA,dB) by
    //   - exploring top-K gammas for the first two additions,
    //   - push through linear layers,
    //   - exploring minimal weights for the next two additions,
    // return minimal total
    int lb_full(uint32_t dA0, uint32_t dB0, int K1=4, int K2=4, int n=32, int cap=64){
        uint64_t key = (uint64_t(dA0)<<32) | dB0;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        struct Cand { uint32_t A2, B2; int w; }; // end of Subround-0 (after L1/L2 + cd_from_B)
        std::vector<Cand> stage1; stage1.reserve(K1*K2);

        // (1) var–var
        uint32_t alpha0 = dB0;
        uint32_t beta0  = rotl(dA0,31) ^ rotl(dA0,17);

        std::vector<std::pair<uint32_t,int>> G1; G1.reserve(K1);
        enumerate_lm_gammas_fast(alpha0, beta0, n, cap, [&](uint32_t g, int w){
            G1.emplace_back(g,w);
        });
        std::sort(G1.begin(), G1.end(), [](auto&x, auto&y){ return x.second<y.second; });
        if ((int)G1.size() > K1) G1.resize(K1);

        // (2) var–const
        uint32_t bconst1 = (uint32_t)(-int32_t(RC1));
        std::vector<std::pair<uint32_t,int>> G2; G2.reserve(K2);
        enumerate_lm_gammas_fast(dA0, bconst1, n, cap, [&](uint32_t g, int w){
            G2.emplace_back(g,w);
        });
        std::sort(G2.begin(), G2.end(), [](auto&x, auto&y){ return x.second<y.second; });
        if ((int)G2.size() > K2) G2.resize(K2);

        // merge + push through linear to get (Astar, Bkeep) at start of subround-1
        for (auto [gB1, w1] : G1){
            for (auto [gA1, w2] : G2){
                int w12 = w1 + w2;
                if (w12 >= cap) continue;
                uint32_t A2 = gA1 ^ rotl(gB1,24);
                uint32_t B2 = gB1 ^ rotl(A2,16);
                A2 = l1_forward(A2);
                B2 = l2_forward(B2);
                auto [C0, D0] = cd_from_B_delta(B2);
                uint32_t Astar = A2 ^ rotl(C0,24) ^ rotl(D0,16);
                stage1.push_back({Astar, B2, w12});
            }
        }

        int best = cap;
        // (3)(4) for subround-1 from each stage1 candidate, compute minimal extra
        uint32_t bconst2 = (uint32_t)(-int32_t(RC6));
        for (auto &c : stage1){
            // (3) var–var: A''' = A* + (rotl(Bkeep,31) ^ rotl(Bkeep,17))
            uint32_t alpha1 = c.A2;
            uint32_t beta1  = rotl(c.B2,31) ^ rotl(c.B2,17);
            int w3_min = cap;
            enumerate_lm_gammas_fast(alpha1, beta1, n, best - c.w, [&](uint32_t /*gA3*/, int w3){
                if (w3 < w3_min) w3_min = w3;
            });
            if (c.w + w3_min >= best) continue;

            // (4) var–const: B''' = Bkeep - RC6
            int w4_min = cap;
            enumerate_lm_gammas_fast(c.B2, bconst2, n, best - c.w - w3_min, [&](uint32_t /*gB3*/, int w4){
                if (w4 < w4_min) w4_min = w4;
            });

            int total = c.w + w3_min + w4_min;
            if (total < best) best = total;
        }

        cache.emplace(key, best);
        return best;
    }
};

} // namespace neoalz
