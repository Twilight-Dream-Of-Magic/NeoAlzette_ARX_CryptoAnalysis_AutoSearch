#include <cstdint>
#include <cstdio>
#include <vector>
#include <queue>
#include <tuple>
#include <string>
#include <fstream>
#include <sstream>
#include <limits>
#include <algorithm>
#include "neoalzette.hpp"
#include "lm_fast.hpp"
#include "lb_round_full.hpp"
#include "suffix_lb.hpp"
#include "highway_table.hpp"
#include "threshold_search.hpp"
#include "neoalz_lin.hpp"
#include "canonicalize.hpp"
#include "diff_add_const.hpp"

namespace neoalz {

struct DiffPair { uint32_t dA, dB; };

static constexpr uint32_t RC[16] = {
    0x16B2C40B, 0xC117176A, 0x0F9A2598, 0xA1563ACA,
    0x243F6A88, 0x85A308D3, 0x13198102, 0xE0370734,
    0x9E3779B9, 0x7F4A7C15, 0xF39CC060, 0x5CEDC834,
    0xB7E15162, 0x8AED2A6A, 0xBF715880, 0x9CF4F3C7
};

} // namespace neoalz

int main(int argc, char** argv){
    using namespace neoalz;
    if (argc < 3){
        std::fprintf(stderr, "Usage: %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv] [--k1 K] [--k2 K]\n", argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    HighwayTable HW; bool use_hw = false;
    if (argc >= 4 && argv[3][0] != '-') { use_hw = HW.load(argv[3]); }

    // defaults
    uint32_t start_dA = 0u, start_dB = 0u;
    std::string export_path;
    int K1 = 4, K2 = 4;

    // parse options
    for (int i=3; i<argc; ++i){
        std::string t = argv[i];
        if (t == "--start-hex" && i+2 < argc){
            start_dA = (uint32_t)std::stoul(argv[++i], nullptr, 16);
            start_dB = (uint32_t)std::stoul(argv[++i], nullptr, 16);
        } else if (t == "--export" && i+1 < argc){
            export_path = argv[++i];
        } else if (t == "--k1" && i+1 < argc){
            K1 = std::stoi(argv[++i]);
        } else if (t == "--k2" && i+1 < argc){
            K2 = std::stoi(argv[++i]);
        }
    }

    LbFullRound LBF;
    SuffixLB SFX;

    auto next_states = [&](const DiffPair& d, int r, int slack_w){
        std::vector<std::pair<DiffPair,int>> out;
        const int n = 32;
        auto [dA0, dB0] = canonical_rotate_pair(d.dA, d.dB);

        // Subround 0 - Add 1 (var-var)
        uint32_t alpha0 = dB0;
        uint32_t beta0  = rotl(dA0,31) ^ rotl(dA0,17);
        enumerate_lm_gammas_fast(alpha0, beta0, n, slack_w, [&](uint32_t gB1, int w1){
            int slack1 = slack_w - w1; if (slack1 < 0) return;
            // Add 2: A = A - RC[1]  (var-const) -> use add-constant model with c = -RC[1]
            uint32_t c1 = (uint32_t)(-int32_t(RC[1]));
            // best gamma and weight for add-constant
            auto best1 = addconst_best(dA0, c1, n);
            if (best1.weight <= slack1){
                uint32_t gA1 = best1.gamma; int w2 = best1.weight;
                int slack2 = slack1 - w2; if (slack2 < 0) return;
                // Linear mix
                uint32_t A2 = gA1 ^ rotl(gB1,24);
                uint32_t B2 = gB1 ^ rotl(A2,16);
                A2 = l1_forward(A2);
                B2 = l2_forward(B2);
                auto [C0, D0] = cd_from_B_delta(B2);
                uint32_t Astar = A2 ^ rotl(C0,24) ^ rotl(D0,16);
                uint32_t Bkeep = B2;
                // Subround 1 - Add 3 (var-var)
                uint32_t alpha1 = Astar;
                uint32_t beta1  = rotl(Bkeep,31) ^ rotl(Bkeep,17);
                enumerate_lm_gammas_fast(alpha1, beta1, n, slack2, [&](uint32_t gA3, int w3){
                    int slack3 = slack2 - w3; if (slack3 < 0) return;
                    // Add 4: B = B - RC[6]  (var-const)
                    uint32_t c2 = (uint32_t)(-int32_t(RC[6]));
                    auto best2 = addconst_best(Bkeep, c2, n);
                    if (best2.weight <= slack3){
                        uint32_t gB3 = best2.gamma; int w4 = best2.weight;
                        int slack4 = slack3 - w4; if (slack4 < 0) return;
                        uint32_t Bhat = gB3 ^ rotl(gA3,24);
                        uint32_t Ahat = gA3 ^ rotl(Bhat,16);
                        uint32_t Aplus = l2_forward(Ahat);
                        uint32_t Bplus = l1_forward(Bhat);
                        auto [C1, D1] = cd_from_A_delta(Aplus);
                        uint32_t Bstar = Bplus ^ rotl(C1,24) ^ rotl(D1,16);
                        auto cn = canonical_rotate_pair(Aplus, Bstar);
                        DiffPair dn{cn.first, cn.second};
                        out.push_back({dn, w1+w2+w3+w4});
                    }
                });
            }
        });
        return out;
    };

    auto lower_bound = [&](const DiffPair& d, int r){
        auto c = canonical_rotate_pair(d.dA, d.dB);
        int rem = R - r;
        int lb_round = LBF.lb_full(c.first, c.second, K1, K2, 32, Wcap);
        int lb_tail = 0;
        if (rem > 1){
            lb_tail = use_hw ? HW.query(c.first, c.second, rem-1)
                             : SFX.bound(c.first, c.second, rem-1, Wcap);
        }
        return lb_round + lb_tail;
    };

    DiffPair start{start_dA,start_dB};
    auto res = matsui_threshold_search<DiffPair>(R, start, Wcap, next_states, lower_bound);
    int best_w = res.first;
    if (!export_path.empty()){
        std::ofstream ofs(export_path, std::ios::app);
        if (ofs){
            ofs << "algo,MEDCP"
                << ",R," << R
                << ",Wcap," << Wcap
                << ",start_dA,0x" << std::hex << start_dA << std::dec
                << ",start_dB,0x" << std::hex << start_dB << std::dec
                << ",K1," << K1
                << ",K2," << K2
                << ",best_w," << best_w
                << "\n";
        }
    }
    std::fprintf(stderr, "[analyze_medcp] best weight = %d (prob >= 2^-%d)\n", best_w, best_w);
    return 0;
}

