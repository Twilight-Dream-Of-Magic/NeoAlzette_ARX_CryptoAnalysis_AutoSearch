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
#include <unordered_map>
#include <map>
#include "neoalzette.hpp"
#include "lm_fast.hpp"
#include "lb_round_full.hpp"
#include "suffix_lb.hpp"
#include "highway_table.hpp"
#include "threshold_search.hpp"
#include "neoalz_lin.hpp"
#include "canonicalize.hpp"
#include "diff_add_const.hpp"
#include "trail_export.hpp"

namespace neoalz {

struct DifferentialState { uint32_t dA, dB; };

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
        std::fprintf(stderr,
            "\nMEDCP Analyzer - Differential Trail Search for NeoAlzette (build only)\n"
            "Usage:\n"
            "  %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv] [--k1 K] [--k2 K]\n\n"
            "Arguments:\n"
            "  R                 Number of rounds\n"
            "  Wcap              Global weight cap for threshold search\n"
            "  highway.bin       Optional differential Highway suffix-LB file\n\n"
            "Options:\n"
            "  --start-hex dA dB   Initial differences in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv     Append a one-line summary (algo, R, Wcap, start, K1, K2, best_w)\n"
            "  --k1 K               Top-K candidates for var–var in one-round LB (default 4)\n"
            "  --k2 K               Top-K candidates for var–const in one-round LB (default 4)\n\n"
            "Notes:\n"
            "  - Only builds; do not run heavy searches in this environment.\n"
            "  - Highway provides O(1) suffix lower bounds with linear space.\n\n",
            argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    HighwayTable HW; bool use_hw = false;
    if (argc >= 4 && argv[3][0] != '-') { use_hw = HW.load(argv[3]); }

    // defaults
    uint32_t start_dA = 0u, start_dB = 0u;
    std::string export_path;
    std::string export_trace_path;
    std::string export_hist_path;
    int export_topN = 0; std::string export_topN_path;
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
        } else if (t == "--export-trace" && i+1 < argc){
            export_trace_path = argv[++i];
        } else if (t == "--export-hist" && i+1 < argc){
            export_hist_path = argv[++i];
        } else if (t == "--export-topN" && i+2 < argc){
            export_topN = std::stoi(argv[++i]);
            export_topN_path = argv[++i];
        }
    }

    LbFullRound LBF;
    SuffixLB SFX;

    std::unordered_map<uint64_t,int> lb_memo; lb_memo.reserve(1<<14);
    auto lb_key = [&](uint32_t a, uint32_t b, int r){ return ( (uint64_t)(r & 0xFF) << 56) ^ ( (uint64_t)a << 24) ^ (uint64_t)b; };

    // Expand one round from a given differential state using exact local models
    auto next_states = [&](const DifferentialState& d, int roundIndex, int slackWeight){
        std::vector<std::pair<DifferentialState,int>> out;
        const int n = 32;
        auto [dA0, dB0] = canonical_rotate_pair(d.dA, d.dB);

        // Subround 0 - Add 1 (var-var)
        uint32_t alpha0 = dB0;
        uint32_t beta0  = rotl(dA0,31) ^ rotl(dA0,17);
        enumerate_lm_gammas_fast(alpha0, beta0, n, slackWeight, [&](uint32_t gammaAfterAdd1, int weightAdd1){
            int slackAfterAdd1 = slackWeight - weightAdd1; if (slackAfterAdd1 < 0) return;
            // Add 2: A = A - RC[1]  (var-const) -> use add-constant model with c = -RC[1]
            uint32_t c1 = (uint32_t)(-int32_t(RC[1]));
            auto bestAdd2 = addconst_best(dA0, c1, n);
            if (bestAdd2.weight <= slackAfterAdd1){
                uint32_t gammaAfterAdd2 = bestAdd2.gamma; int weightAdd2 = bestAdd2.weight;
                int slackAfterAdd2 = slackAfterAdd1 - weightAdd2; if (slackAfterAdd2 < 0) return;
                // Linear mix
                uint32_t A2 = gammaAfterAdd2 ^ rotl(gammaAfterAdd1,24);
                uint32_t B2 = gammaAfterAdd1 ^ rotl(A2,16);
                A2 = l1_forward(A2);
                B2 = l2_forward(B2);
                auto [C0, D0] = cd_from_B_delta(B2);
                uint32_t Astar = A2 ^ rotl(C0,24) ^ rotl(D0,16);
                uint32_t Bkeep = B2;
                // Subround 1 - Add 3 (var-var)
                uint32_t alpha1 = Astar;
                uint32_t beta1  = rotl(Bkeep,31) ^ rotl(Bkeep,17);
                enumerate_lm_gammas_fast(alpha1, beta1, n, slackAfterAdd2, [&](uint32_t gammaAfterAdd3, int weightAdd3){
                    int slackAfterAdd3 = slackAfterAdd2 - weightAdd3; if (slackAfterAdd3 < 0) return;
                    // Add 4: B = B - RC[6]  (var-const)
                    uint32_t c2 = (uint32_t)(-int32_t(RC[6]));
                    auto bestAdd4 = addconst_best(Bkeep, c2, n);
                    if (bestAdd4.weight <= slackAfterAdd3){
                        uint32_t gammaAfterAdd4 = bestAdd4.gamma; int weightAdd4 = bestAdd4.weight;
                        int slackAfterAdd4 = slackAfterAdd3 - weightAdd4; if (slackAfterAdd4 < 0) return;
                        uint32_t Bhat = gammaAfterAdd4 ^ rotl(gammaAfterAdd3,24);
                        uint32_t Ahat = gammaAfterAdd3 ^ rotl(Bhat,16);
                        uint32_t Aplus = l2_forward(Ahat);
                        uint32_t Bplus = l1_forward(Bhat);
                        auto [C1, D1] = cd_from_A_delta(Aplus);
                        uint32_t Bstar = Bplus ^ rotl(C1,24) ^ rotl(D1,16);
                        auto cn = canonical_rotate_pair(Aplus, Bstar);
                        DifferentialState nextState{cn.first, cn.second};
                        int totalRoundWeight = weightAdd1 + weightAdd2 + weightAdd3 + weightAdd4;
                        out.push_back({nextState, totalRoundWeight});
                    }
                });
            }
        });
        return out;
    };

    auto lower_bound = [&](const DifferentialState& d, int roundIndex){
        auto c = canonical_rotate_pair(d.dA, d.dB);
        uint64_t k = lb_key(c.first, c.second, roundIndex);
        auto it = lb_memo.find(k); if (it != lb_memo.end()) return it->second;
        int rem = R - roundIndex;
        int lb_round = LBF.lb_full(c.first, c.second, K1, K2, 32, Wcap);
        int lb_tail = 0;
        if (rem > 1){ lb_tail = use_hw ? HW.query(c.first, c.second, rem-1) : SFX.bound(c.first, c.second, rem-1, Wcap); }
        int v = lb_round + lb_tail; lb_memo.emplace(k, v); return v;
    };

    DifferentialState start{start_dA,start_dB};
    // If any advanced export requested, run a path-capturing search; otherwise use the generic helper
    int best_w = 0;
    DifferentialState best_state{};
    if (export_trace_path.empty() && export_hist_path.empty() && export_topN == 0){
        auto res = matsui_threshold_search<DifferentialState>(R, start, Wcap, next_states, lower_bound);
        best_w = res.first; best_state = res.second;
    } else {
        struct SearchNode { DifferentialState s; int r; int w; int lb; };
        auto cmp = [](const SearchNode& a, const SearchNode& b){ return a.lb > b.lb; };
        std::priority_queue<SearchNode, std::vector<SearchNode>, decltype(cmp)> pq(cmp);
        auto key_of = [](const DifferentialState& s, int r){ return ((uint64_t)(r & 0xFF) << 56) ^ ((uint64_t)s.dA << 24) ^ (uint64_t)s.dB; };
        std::unordered_map<uint64_t,uint64_t> parent; parent.reserve(1<<14);
        std::unordered_map<uint64_t,int>        weight_at; weight_at.reserve(1<<14);
        std::unordered_map<uint64_t,DifferentialState> state_at; state_at.reserve(1<<14);
        std::map<int,int> weight_histogram; // weight histogram for completed paths

        auto push_node = [&](const DifferentialState& s, int r, int w, uint64_t parent_key){
            int lb = w + lower_bound(s, r);
            int cap = Wcap;
            if (lb >= cap) return;
            SearchNode node{s,r,w,lb};
            pq.push(node);
            uint64_t k = key_of(s,r);
            parent[k] = parent_key;
            weight_at[k] = w;
            state_at[k]  = s;
        };

        push_node(start, 0, 0, key_of(start,0));
        int best = std::numeric_limits<int>::max();
        std::vector<std::pair<int,DifferentialState>> topN; topN.reserve((size_t)std::max(0, export_topN));
        while(!pq.empty()){
            auto cur = pq.top(); pq.pop();
            if (cur.lb >= std::min(best, Wcap)) continue;
            if (cur.r == R){
                best = std::min(best, cur.w);
                // update histogram
                weight_histogram[cur.w]++;
                // collect topN
                if (export_topN > 0){
                    topN.emplace_back(cur.w, cur.s);
                    std::sort(topN.begin(), topN.end(), [](auto&a, auto&b){ return a.first < b.first; });
                    if ((int)topN.size() > export_topN) topN.pop_back();
                }
                best_state = cur.s;
                continue;
            }
            int slack = std::min(best, Wcap) - cur.w;
            auto children = next_states(cur.s, cur.r, slack);
            uint64_t parent_key = key_of(cur.s, cur.r);
            for (auto& [child, addw] : children){
                int w2 = cur.w + addw;
                push_node(child, cur.r+1, w2, parent_key);
            }
        }
        best_w = best;
        // Export trace
        if (!export_trace_path.empty() && best < std::numeric_limits<int>::max()){
            std::vector<std::tuple<int,uint32_t,uint32_t,int>> path; path.reserve((size_t)R+1);
            uint64_t k = key_of(best_state, R);
            while (true){
                auto itS = state_at.find(k); if (itS==state_at.end()) break;
                int r = (int)((k>>56)&0xFF);
                int w = weight_at[k];
                auto s = itS->second;
                path.emplace_back(r, s.dA, s.dB, w);
                uint64_t pk = parent[k]; if (pk == k) break; k = pk;
            }
            std::reverse(path.begin(), path.end());
            // write CSV header
            TrailExport::append_csv(export_trace_path, "algo,MEDCP,field,round,dA,dB,acc_weight");
            for (auto& t : path){
                int r; uint32_t a,b; int w; std::tie(r,a,b,w)=t;
                std::ostringstream ss;
                ss << "algo,MEDCP,trace,"
                   << r << ",0x" << std::hex << a << std::dec
                   << ",0x" << std::hex << b << std::dec
                   << "," << w;
                TrailExport::append_csv(export_trace_path, ss.str());
            }
        }
        // Export histogram
        if (!export_hist_path.empty()){
            TrailExport::append_csv(export_hist_path, "algo,MEDCP,field,weight,count");
            for (auto& kv : weight_histogram){
                std::ostringstream ss;
                ss << "algo,MEDCP,hist," << kv.first << "," << kv.second;
                TrailExport::append_csv(export_hist_path, ss.str());
            }
        }
        // Export topN
        if (export_topN > 0 && !export_topN_path.empty()){
            TrailExport::append_csv(export_topN_path, "algo,MEDCP,field,rank,weight,dA,dB");
            for (size_t i=0;i<topN.size();++i){
                std::ostringstream ss;
                ss << "algo,MEDCP,topN," << (i+1) << "," << topN[i].first
                   << ",0x" << std::hex << topN[i].second.dA << std::dec
                   << ",0x" << std::hex << topN[i].second.dB << std::dec;
                TrailExport::append_csv(export_topN_path, ss.str());
            }
        }
    }
    if (!export_path.empty()){
        std::ostringstream ss;
        ss << "algo,MEDCP"
           << ",R," << R
           << ",Wcap," << Wcap
           << ",start_dA,0x" << std::hex << start_dA << std::dec
           << ",start_dB,0x" << std::hex << start_dB << std::dec
           << ",K1," << K1
           << ",K2," << K2
           << ",best_w," << best_w;
        TrailExport::append_csv(export_path, ss.str());
    }
    std::fprintf(stderr, "[analyze_medcp] best weight = %d (prob >= 2^-%d)\n", best_w, best_w);
    return 0;
}

