#include <iostream>
#include <vector>
#include <tuple>
#include <cstdint>
#include <functional>
#include "pddt.hpp"
#include "lb_round.hpp"
#include "lb_round_full.hpp"
#include "suffix_lb.hpp"
#include "highway_table.hpp"
#include "neoalzette.hpp"
#include "lm_fast.hpp"
#include "threshold_search.hpp"

// We do not rely on the full class from neoalzette.hpp here to minimise coupling.
// Instead we inline the exact forward linear pieces we need for difference propagation.
namespace neoalz {

static inline uint32_t l1_forward(uint32_t x) noexcept {
    return x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24);
}
static inline uint32_t l2_forward(uint32_t x) noexcept {
    return x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30);
}

// Linearised cross-branch injectors (delta versions: constants drop out)
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

// Round constants (identical to your C++ reference; only RC[0..11] are used here)
static constexpr uint32_t RC[16] = {
    0x16B2C40B, 0xC117176A, 0x0F9A2598, 0xA1563ACA,
    0x243F6A88, 0x85A308D3, 0x13198102, 0xE0370734,
    0x9E3779B9, 0x7F4A7C15, 0xF39CC060, 0x5CEDC834,
    0xB7E15162, 0x8AED2A6A, 0xBF715880, 0x9CF4F3C7
};

struct DiffPair { uint32_t dA, dB; };

} // namespace neoalz

using namespace neoalz;

// use enumerate_lm_gammas(_fast) from lm_fast.hpp

int main(int argc, char** argv)
{
    int R = (argc>1)? std::atoi(argv[1]) : 2;
    int Wcap = (argc>2)? std::atoi(argv[2]) : 128;

    auto next_states = [&](const DiffPair& d, int r, int slack_w){
        std::vector<std::pair<DiffPair,int>> out;
        const int n = 32;
        uint32_t dA0 = d.dA, dB0 = d.dB;

        // ---------- Subround 0 ----------
        // (1) var–var: B' = B + (rotl(A,31) ^ rotl(A,17) ^ RC[0])
        uint32_t alpha0 = dB0;
        uint32_t beta0  = rotl(dA0,31) ^ rotl(dA0,17);
        enumerate_lm_gammas(alpha0, beta0, n, slack_w, [&](uint32_t gB1, int w1){

            int slack1 = slack_w - w1;
            if (slack1 < 0) return;

            // (2) var–const: A' = A - RC[1]   (addition by two's-complement)
            uint32_t bconst1 = (uint32_t)(-int32_t(RC[1]));
            enumerate_lm_gammas(dA0, bconst1, n, slack1, [&](uint32_t gA1, int w2){

                int slack2 = slack1 - w2;
                if (slack2 < 0) return;

                // (3) linear triangular mix
                uint32_t A2 = gA1 ^ rotl(gB1,24);
                uint32_t B2 = gB1 ^ rotl(A2,16);
                A2 = l1_forward(A2);
                B2 = l2_forward(B2);

                // (4) cross-branch from B (linear)
                auto [C0, D0] = cd_from_B_delta(B2);
                uint32_t Astar = A2 ^ rotl(C0,24) ^ rotl(D0,16);
                uint32_t Bkeep = B2;

                // ---------- Subround 1 ----------
                // (5) var–var: A''' = A* + (rotl(Bkeep,31) ^ rotl(Bkeep,17) ^ RC[5])
                uint32_t alpha1 = Astar;
                uint32_t beta1  = rotl(Bkeep,31) ^ rotl(Bkeep,17);
                enumerate_lm_gammas(alpha1, beta1, n, slack2, [&](uint32_t gA3, int w3){

                    int slack3 = slack2 - w3;
                    if (slack3 < 0) return;

                    // (6) var–const: B''' = Bkeep - RC[6]
                    uint32_t bconst2 = (uint32_t)(-int32_t(RC[6]));
                    enumerate_lm_gammas(Bkeep, bconst2, n, slack3, [&](uint32_t gB3, int w4){

                        int slack4 = slack3 - w4;
                        if (slack4 < 0) return;

                        // (7) linear triangular mix
                        uint32_t Bhat = gB3 ^ rotl(gA3,24);
                        uint32_t Ahat = gA3 ^ rotl(Bhat,16);
                        uint32_t Aplus = l2_forward(Ahat);
                        uint32_t Bplus = l1_forward(Bhat);

                        // (8) cross-branch from A (linear) + whitening (drops out for diffs)
                        auto [C1, D1] = cd_from_A_delta(Aplus);
                        uint32_t Bstar = Bplus ^ rotl(C1,24) ^ rotl(D1,16);
                        uint32_t Anext = Aplus;
                        uint32_t Bnext = Bstar;

                        DiffPair dn{Anext, Bnext};
                        int add_weight = w1 + w2 + w3 + w4;
                        out.push_back({dn, add_weight});
                    });
                });
            });
        });

        return out;
    };

    neoalz::LbCache LB;
    neoalz::LbFullRound LBF;
    neoalz::SuffixLB SFX;
    neoalz::HighwayTable HW;
    bool use_hw = false;
    if (argc >= 5) { // optional: highway table path
        use_hw = HW.load(argv[4]);
        if (use_hw) std::fprintf(stderr, "[Info] Loaded Highway table from %s\n", argv[4]);
    }
    auto lower_bound = [&](const DiffPair& d, int r){
        int rem = R - r;
        int lb_round = LBF.lb_full(d.dA, d.dB, 4, 4, 32, 64);
        int lb_tail  = 0;
        if (rem>1){
            if (use_hw){
                lb_tail = HW.query(d.dA, d.dB, rem-1);
            } else {
                lb_tail = SFX.bound(d.dA, d.dB, rem-1, 64);
            }
        }
        return lb_round + lb_tail;
    };

    DiffPair start{0u, 0u}; // 入口差分（可替换为你想要的 ΔA0, ΔB0）
    auto res = neoalz::matsui_threshold_search<DiffPair>(R, start, Wcap, next_states, lower_bound);
    int best_w = res.first;
    DiffPair end = res.second;

    std::cout << "Best round-sum weight = " << best_w
              << " ; end dA="<< std::hex << end.dA
              << " dB=" << end.dB << std::dec << "\n";
    std::cout << "NOTE: This uses LM-2001 exactly on four additions per round, "
                 "and exact linear propagation for L1/L2/cd_from_*().\n";
    return 0;
}
