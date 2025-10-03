#include <cstdint>
#include <cstdio>
#include <vector>
#include <queue>
#include <tuple>
#include <string>
#include <fstream>
#include <limits>
#include <algorithm>
#include "neoalzette.hpp"
#include "wallen_fast.hpp"
#include "neoalz_lin.hpp"
#include "mask_backtranspose.hpp"
#include "lb_round_lin.hpp"
#include "suffix_lb_lin.hpp"
#include "canonicalize.hpp"
#include "highway_table_lin.hpp"

namespace neoalz {

struct LinPair { uint32_t mA, mB; int w; int r; };

// Backtranspose via exact (L^{-1})^T using Gauss–Jordan constructed rows
static inline uint32_t l1_backtranspose(uint32_t x) noexcept { return l1_backtranspose_exact(x); }
static inline uint32_t l2_backtranspose(uint32_t x) noexcept { return l2_backtranspose_exact(x); }

struct Cmp { bool operator()(const LinPair& a, const LinPair& b) const noexcept { return a.w > b.w; } };

} // namespace neoalz

int main(int argc, char** argv){
    using namespace neoalz;
    if (argc < 3){
        std::fprintf(stderr, "Usage: %s R Wcap [--start-hex mA mB] [--export out.csv] [--lin-highway H.bin]\n", argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    uint32_t start_mA = 0u, start_mB = 0u;
    std::string export_path; std::string lin_hw_path;
    for (int i=3; i<argc; ++i){
        std::string t = argv[i];
        if (t == "--start-hex" && i+2 < argc){
            start_mA = (uint32_t)std::stoul(argv[++i], nullptr, 16);
            start_mB = (uint32_t)std::stoul(argv[++i], nullptr, 16);
        } else if (t == "--export" && i+1 < argc){
            export_path = argv[++i];
        } else if (t == "--lin-highway" && i+1 < argc){
            lin_hw_path = argv[++i];
        }
    }

    LbFullRoundLin LBL;
    SuffixLBLin SFX;
    HighwayTableLin HWL; bool use_hwl = false;
    if (!lin_hw_path.empty()) use_hwl = HWL.load(lin_hw_path);

    std::priority_queue<LinPair, std::vector<LinPair>, Cmp> pq;
    auto cs = canonical_rotate_pair(start_mA, start_mB);
    pq.push({cs.first, cs.second, 0, 0});
    int best = std::numeric_limits<int>::max();

    while (!pq.empty()){
        auto cur = pq.top(); pq.pop();
        if (cur.w >= std::min(best, Wcap)) continue;
        if (cur.r == R){ best = std::min(best, cur.w); continue; }

        // Lower bound prune (use canonical state)
        auto cc = canonical_rotate_pair(cur.mA, cur.mB);
        int lb1 = LBL.lb_full(cc.first, cc.second, 3,3, 32, std::min(best, Wcap) - cur.w);
        int lbTail = 0;
        int remTail = R - cur.r - 1;
        if (remTail > 0){
            lbTail = use_hwl ? HWL.query(cc.first, cc.second, remTail)
                             : SFX.bound(cc.first, cc.second, remTail, std::min(best, Wcap) - cur.w - lb1);
        }
        if (cur.w + lb1 + lbTail >= std::min(best, Wcap)) continue;

        // Subround 0 - Add 1: B += F(A)
        uint32_t mu1 = cur.mB;
        uint32_t nu1 = rotr(cur.mA, 31) ^ rotr(cur.mA, 17);
        enumerate_wallen_omegas(mu1, nu1, std::min(best, Wcap) - cur.w, [&](uint32_t Bp_mask, int w1){
            uint32_t mu2 = cur.mA; uint32_t nu2 = 0u; // var-const
            enumerate_wallen_omegas(mu2, nu2, std::min(best, Wcap) - (cur.w + w1), [&](uint32_t Ap_mask, int w2){
                // Linear diffusion
                uint32_t A2_mask = l1_backtranspose( Ap_mask ^ rotl(Bp_mask,24) );
                uint32_t B2_mask = l2_backtranspose( Bp_mask ^ rotl(Ap_mask ^ rotl(Bp_mask,24),16) );
                // Cross-branch linear injection (mask domain)
                uint32_t C0m = l2_backtranspose( B2_mask );
                uint32_t D0m = l1_backtranspose( rotr(B2_mask,3) );
                uint32_t Astar_mask = A2_mask ^ rotl(C0m,24) ^ rotl(D0m,16);

                // Subround 1 - Add 3: A* += F(B2)
                uint32_t mu3 = Astar_mask;
                uint32_t nu3 = rotr(B2_mask,31) ^ rotr(B2_mask,17);
                enumerate_wallen_omegas(mu3, nu3, std::min(best, Wcap) - (cur.w + w1 + w2), [&](uint32_t A3_mask, int w3){
                    // Add 4: var-const
                    uint32_t mu4 = B2_mask; uint32_t nu4 = 0u;
                    enumerate_wallen_omegas(mu4, nu4, std::min(best, Wcap) - (cur.w + w1 + w2 + w3), [&](uint32_t B3_mask, int w4){
                        uint32_t B4in = B3_mask ^ rotl(A3_mask,24);
                        uint32_t A4in = A3_mask ^ rotl(B4in,16);
                        uint32_t Aout = l2_backtranspose(A4in);
                        uint32_t Bout = l1_backtranspose(B4in);
                        auto cn = canonical_rotate_pair(Aout, Bout);
                        LinPair nxt{cn.first, cn.second, cur.w + w1+w2+w3+w4, cur.r+1};
                        if (nxt.w < std::min(best, Wcap)) pq.push(nxt);
                    });
                });
            });
        });
    }

    if (!export_path.empty()){
        std::ofstream ofs(export_path, std::ios::app);
        if (ofs){
            ofs << "algo,MELCC"
                << ",R," << R
                << ",Wcap," << Wcap
                << ",start_mA,0x" << std::hex << start_mA << std::dec
                << ",start_mB,0x" << std::hex << start_mB << std::dec
                << ",best_w," << best
                << "\n";
        }
    }
    std::fprintf(stderr, "[analyze_melcc] best linear weight = %d\n", best);
    return 0;
}

