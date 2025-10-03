#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include "lm_fast.hpp"
#include "pddt.hpp"
#include "neoalz_lin.hpp"
#include "diff_add_const.hpp"

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
    static constexpr uint32_t RC6 = 0x13198102; // RC[6]

    int lb_full(uint32_t dA0, uint32_t dB0, int K1=4, int K2=4, int n=32, int cap=64){
        uint64_t key = (uint64_t(dA0)<<32) | dB0;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        struct Cand { uint32_t A2, B2; int w; };
        std::vector<Cand> stage1; stage1.reserve(K1*K2);

        // (1) var–var
        uint32_t alpha0 = dB0;
        uint32_t beta0  = rotl(dA0,31) ^ rotl(dA0,17);

        std::vector<std::pair<uint32_t,int>> G1; G1.reserve(K1);
        enumerate_lm_gammas_fast(alpha0, beta0, n, cap, [&](uint32_t g, int w){ G1.emplace_back(g,w); });
        std::sort(G1.begin(), G1.end(), [](auto&x, auto&y){ return x.second<y.second; });
        if ((int)G1.size() > K1) G1.resize(K1);

        // (2) var–const via add-constant model
        uint32_t c1 = (uint32_t)(-int32_t(RC1));
        std::vector<std::pair<uint32_t,int>> G2; G2.reserve(K2);
        // enumerate best single candidate (greedy) and a few neighbors by flipping low bits heuristically
        auto best1 = addconst_best(dA0, c1, n);
        G2.emplace_back(best1.gamma, best1.weight);

        std::sort(G2.begin(), G2.end(), [](auto&x, auto&y){ return x.second<y.second; });
        if ((int)G2.size() > K2) G2.resize(K2);

        for (auto [gB1, w1] : G1){
            for (auto [gA1, w2] : G2){
                int w12 = w1 + w2; if (w12 >= cap) continue;
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
        uint32_t c2 = (uint32_t)(-int32_t(RC6));
        for (auto &c : stage1){
            // (3) var–var: A* += F(B2)
            uint32_t alpha1 = c.A2;
            uint32_t beta1  = rotl(c.B2,31) ^ rotl(c.B2,17);
            int w3_min = cap;
            enumerate_lm_gammas_fast(alpha1, beta1, n, best - c.w, [&](uint32_t /*gA3*/, int w3){ if (w3 < w3_min) w3_min = w3; });
            if (c.w + w3_min >= best) continue;

            // (4) var–const via add-constant model
            auto best2 = addconst_best(c.B2, c2, n);
            int total = c.w + w3_min + best2.weight;
            if (total < best) best = total;
        }

        cache.emplace(key, best);
        return best;
    }
};

} // namespace neoalz
