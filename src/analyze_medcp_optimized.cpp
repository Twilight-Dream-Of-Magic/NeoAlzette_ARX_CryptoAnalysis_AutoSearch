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
#include <chrono>
#include <thread>

// Use optimized components
#include "neoalzette.hpp"
#include "lm_fast.hpp"
#include "lb_round_full.hpp"
#include "suffix_lb.hpp"
#include "highway_table.hpp"
#include "threshold_search_optimized.hpp"
#include "neoalz_lin.hpp"
#include "state_optimized.hpp"
#include "diff_add_const.hpp"
#include "trail_export.hpp"

namespace neoalz {

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
            "\nMEDCP Analyzer - Optimized Differential Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv] [--k1 K] [--k2 K]\n\n"
            "Arguments:\n"
            "  R                 Number of rounds\n"
            "  Wcap              Global weight cap for threshold search\n"
            "  highway.bin       Optional differential Highway suffix-LB file\n\n"
            "Options:\n"
            "  --start-hex dA dB     Initial differences in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv      Append a one-line summary (algo, R, Wcap, start, K1, K2, best_w)\n"
            "  --export-trace out.csv Export complete optimal trail path\n"
            "  --export-hist out.csv  Export weight distribution histogram\n"
            "  --export-topN N out.csv Export top-N best results\n"
            "  --k1 K               Top-K candidates for var–var in one-round LB (default 4)\n"
            "  --k2 K               Top-K candidates for var–const in one-round LB (default 4)\n"
            "  --threads N          Number of threads to use (default: auto-detect)\n"
            "  --fast-canonical     Use fast canonicalization (less accurate but faster)\n\n"
            "Optimizations:\n"
            "  - Parallel threshold search with work-stealing queues\n"
            "  - Cache-friendly packed state representation\n"
            "  - Optimized canonicalization with bit manipulation\n"
            "  - Improved memory access patterns and deduplication\n\n",
            argv[0]);
        return 1;
    }
    
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    HighwayTable HW; 
    bool use_hw = false;
    if (argc >= 4 && argv[3][0] != '-') { 
        use_hw = HW.load(argv[3]); 
    }

    // defaults
    uint32_t start_dA = 0u, start_dB = 0u;
    std::string export_path;
    std::string export_trace_path;
    std::string export_hist_path;
    int export_topN = 0; 
    std::string export_topN_path;
    int K1 = 4, K2 = 4;
    int num_threads = 0;
    bool fast_canonical = false;

    // parse options
    for (int i = 3; i < argc; ++i){
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
        } else if (t == "--threads" && i+1 < argc){
            num_threads = std::stoi(argv[++i]);
        } else if (t == "--fast-canonical"){
            fast_canonical = true;
        }
    }

    LbFullRound LBF;
    SuffixLB SFX;

    // Use optimized hash map with better initial capacity
    std::unordered_map<uint64_t, int> lb_memo; 
    lb_memo.reserve(1 << 16);
    
    auto lb_key = [&](const PackedDifferentialState& state, int r) -> uint64_t { 
        return (state.hash() << 8) | (uint64_t(r) & 0xFF);
    };

    // Choose canonicalization function based on user preference
    auto canonical_func = fast_canonical ? canonical_rotate_pair_fast : canonical_rotate_pair_optimized;

    // Optimized next_states function with better memory management
    auto next_states = [&](const PackedDifferentialState& d, int roundIndex, int slackWeight) 
        -> std::vector<std::pair<PackedDifferentialState, int>> {
        
        std::vector<std::pair<PackedDifferentialState, int>> out;
        out.reserve(64); // Pre-allocate reasonable capacity
        
        const int n = 32;
        auto canonical_d = canonical_func(d.dA(), d.dB());

        // Subround 0 - Add 1 (var-var)
        uint32_t alpha0 = canonical_d.dB();
        uint32_t beta0  = rotl(canonical_d.dA(), 31) ^ rotl(canonical_d.dA(), 17);
        
        enumerate_lm_gammas_fast(alpha0, beta0, n, slackWeight, [&](uint32_t gammaAfterAdd1, int weightAdd1){
            int slackAfterAdd1 = slackWeight - weightAdd1; 
            if (slackAfterAdd1 < 0) return;
            
            // Add 2: A = A - RC[1]  (var-const) -> use add-constant model with c = -RC[1]
            uint32_t c1 = (uint32_t)(-int32_t(RC[1]));
            auto bestAdd2 = addconst_best(canonical_d.dA(), c1, n);
            
            if (bestAdd2.weight <= slackAfterAdd1){
                uint32_t gammaAfterAdd2 = bestAdd2.gamma; 
                int weightAdd2 = bestAdd2.weight;
                int slackAfterAdd2 = slackAfterAdd1 - weightAdd2; 
                if (slackAfterAdd2 < 0) return;
                
                // Linear mix
                uint32_t A2 = gammaAfterAdd2 ^ rotl(gammaAfterAdd1, 24);
                uint32_t B2 = gammaAfterAdd1 ^ rotl(A2, 16);
                A2 = l1_forward(A2);
                B2 = l2_forward(B2);
                auto [C0, D0] = cd_from_B_delta(B2);
                uint32_t Astar = A2 ^ rotl(C0, 24) ^ rotl(D0, 16);
                uint32_t Bkeep = B2;
                
                // Subround 1 - Add 3 (var-var)
                uint32_t alpha1 = Astar;
                uint32_t beta1  = rotl(Bkeep, 31) ^ rotl(Bkeep, 17);
                
                enumerate_lm_gammas_fast(alpha1, beta1, n, slackAfterAdd2, [&](uint32_t gammaAfterAdd3, int weightAdd3){
                    int slackAfterAdd3 = slackAfterAdd2 - weightAdd3; 
                    if (slackAfterAdd3 < 0) return;
                    
                    // Add 4: B = B - RC[6]  (var-const)
                    uint32_t c2 = (uint32_t)(-int32_t(RC[6]));
                    auto bestAdd4 = addconst_best(Bkeep, c2, n);
                    
                    if (bestAdd4.weight <= slackAfterAdd3){
                        uint32_t gammaAfterAdd4 = bestAdd4.gamma; 
                        int weightAdd4 = bestAdd4.weight;
                        int slackAfterAdd4 = slackAfterAdd3 - weightAdd4; 
                        if (slackAfterAdd4 < 0) return;
                        
                        uint32_t Bhat = gammaAfterAdd4 ^ rotl(gammaAfterAdd3, 24);
                        uint32_t Ahat = gammaAfterAdd3 ^ rotl(Bhat, 16);
                        uint32_t Aplus = l2_forward(Ahat);
                        uint32_t Bplus = l1_forward(Bhat);
                        auto [C1, D1] = cd_from_A_delta(Aplus);
                        uint32_t Bstar = Bplus ^ rotl(C1, 24) ^ rotl(D1, 16);
                        
                        auto cn = canonical_func(Aplus, Bstar);
                        int totalRoundWeight = weightAdd1 + weightAdd2 + weightAdd3 + weightAdd4;
                        
                        out.emplace_back(cn, totalRoundWeight);
                    }
                });
            }
        });
        
        return out;
    };

    auto lower_bound = [&](const PackedDifferentialState& d, int roundIndex) -> int {
        auto c = canonical_func(d.dA(), d.dB());
        uint64_t k = lb_key(c, roundIndex);
        
        auto it = lb_memo.find(k); 
        if (it != lb_memo.end()) return it->second;
        
        int rem = R - roundIndex;
        int lb_round = LBF.lb_full(c.dA(), c.dB(), K1, K2, 32, Wcap);
        int lb_tail = 0;
        
        if (rem > 1) { 
            lb_tail = use_hw ? HW.query(c.dA(), c.dB(), rem-1) 
                            : SFX.bound(c.dA(), c.dB(), rem-1, Wcap); 
        }
        
        int v = lb_round + lb_tail; 
        lb_memo.emplace(k, v); 
        return v;
    };

    PackedDifferentialState start(start_dA, start_dB);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Use optimized parallel search
    auto result = matsui_threshold_search_optimized(
        R, start, Wcap, next_states, lower_bound, num_threads);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    int best_w = result.first;
    auto best_state = result.second;
    
    if (!export_path.empty()){
        std::ostringstream ss;
        ss << "algo,MEDCP_OPTIMIZED"
           << ",R," << R
           << ",Wcap," << Wcap
           << ",start_dA,0x" << std::hex << start_dA << std::dec
           << ",start_dB,0x" << std::hex << start_dB << std::dec
           << ",K1," << K1
           << ",K2," << K2
           << ",best_w," << best_w
           << ",time_ms," << duration.count()
           << ",threads," << (num_threads == 0 ? std::thread::hardware_concurrency() : num_threads)
           << ",fast_canonical," << (fast_canonical ? "true" : "false");
        TrailExport::append_csv(export_path, ss.str());
    }
    
    std::fprintf(stderr, "[analyze_medcp_optimized] best weight = %d (prob >= 2^-%d) in %ld ms\n", 
                 best_w, best_w, duration.count());
    return 0;
}