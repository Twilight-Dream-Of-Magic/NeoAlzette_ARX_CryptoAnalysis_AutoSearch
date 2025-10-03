#include <cstdint>
#include <cstdio>
#include <vector>
#include <queue>
#include <tuple>
#include <string>
#include <fstream>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <sstream>
#include <chrono>

// Use optimized components
#include "Common/neoalzette.hpp"
#include "MELCC/wallen_optimized.hpp"
#include "Common/neoalz_lin.hpp"
#include "MELCC/mask_backtranspose.hpp"
#include "MELCC/lb_round_lin.hpp"
#include "MELCC/suffix_lb_lin.hpp"
#include "Common/state_optimized.hpp"
#include "MELCC/highway_table_lin.hpp"
#include "Common/trail_export.hpp"
#include "Common/threshold_search_optimized.hpp"

namespace neoalz {

struct LinearStateOptimized { 
    PackedDifferentialState state; // Using optimized packed state
    int w; 
    int r; 
    
    LinearStateOptimized(uint32_t mA, uint32_t mB, int weight, int round) 
        : state(mA, mB), w(weight), r(round) {}
        
    uint32_t mA() const { return state.dA(); }
    uint32_t mB() const { return state.dB(); }
};

struct OptimizedCmp { 
    bool operator()(const LinearStateOptimized& a, const LinearStateOptimized& b) const noexcept { 
        return a.w > b.w; 
    } 
};

// Backtranspose via exact (L^{-1})^T using Gauss–Jordan constructed rows
static inline uint32_t l1_backtranspose(uint32_t x) noexcept { 
    return l1_backtranspose_exact(x); 
}
static inline uint32_t l2_backtranspose(uint32_t x) noexcept { 
    return l2_backtranspose_exact(x); 
}

} // namespace neoalz

int main(int argc, char** argv){
    using namespace neoalz;
    
    if (argc < 3){
        std::fprintf(stderr,
            "\nMELCC Analyzer - Optimized Linear Trail Search for NeoAlzette\n"
            "Usage:\n"
            "  %s R Wcap [--start-hex mA mB] [--export out.csv] [--lin-highway H.bin]\n\n"
            "Arguments:\n"
            "  R                    Number of rounds\n"
            "  Wcap                 Global weight cap for threshold/beam search\n\n"
            "Options:\n"
            "  --start-hex mA mB      Initial masks in hex (e.g., 0x1 0x0)\n"
            "  --export out.csv       Append a one-line summary (algo, R, Wcap, start, best_w)\n"
            "  --export-trace out.csv Export complete optimal trail path\n"
            "  --export-hist out.csv  Export weight distribution histogram\n"
            "  --export-topN N out.csv Export top-N best results\n"
            "  --lin-highway H.bin    Optional linear Highway suffix-LB file\n"
            "  --threads N            Number of threads to use (default: auto-detect)\n"
            "  --fast-canonical       Use fast canonicalization (less accurate but faster)\n\n"
            "Optimizations:\n"
            "  - Precomputed Wallén automaton for faster linear enumeration\n"
            "  - Parallel threshold search with work-stealing\n"
            "  - Cache-friendly packed state representation\n"
            "  - Optimized canonicalization algorithms\n\n",
            argv[0]);
        return 1;
    }
    
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    uint32_t start_mA = 0u, start_mB = 0u;
    std::string export_path; 
    std::string lin_hw_path;
    std::string export_trace_path; 
    std::string export_hist_path; 
    int export_topN = 0; 
    std::string export_topN_path;
    int num_threads = 0;
    bool fast_canonical = false;
    
    for (int i = 3; i < argc; ++i){
        std::string t = argv[i];
        if (t == "--start-hex" && i+2 < argc){
            start_mA = (uint32_t)std::stoul(argv[++i], nullptr, 16);
            start_mB = (uint32_t)std::stoul(argv[++i], nullptr, 16);
        } else if (t == "--export" && i+1 < argc){
            export_path = argv[++i];
        } else if (t == "--lin-highway" && i+1 < argc){
            lin_hw_path = argv[++i];
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

    LbFullRoundLin LBL;
    SuffixLBLin SFX;
    HighwayTableLin HWL; 
    bool use_hwl = false;
    if (!lin_hw_path.empty()) use_hwl = HWL.load(lin_hw_path);

    // Use optimized priority queue with packed states
    std::priority_queue<LinearStateOptimized, std::vector<LinearStateOptimized>, OptimizedCmp> pq;
    
    // Use fast or precise canonicalization based on user preference
    auto canonical_func = fast_canonical ? canonical_rotate_pair_fast : canonical_rotate_pair_optimized;
    auto cs = canonical_func(start_mA, start_mB);
    pq.push(LinearStateOptimized(cs.dA(), cs.dB(), 0, 0));
    
    int best = std::numeric_limits<int>::max();

    // Optimized memoization with better hash function
    std::unordered_map<PackedDifferentialState, int> lb_memo; 
    lb_memo.reserve(1 << 16); // Larger initial capacity to reduce rehashing
    
    auto lb_key = [&](const PackedDifferentialState& state, int r) -> uint64_t { 
        return (state.hash() << 8) | (uint64_t(r) & 0xFF);
    };

    std::vector<std::pair<int, LinearStateOptimized>> topN; 
    topN.reserve((size_t)std::max(0, export_topN));
    std::map<int, int> hist;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t nodes_processed = 0;
    
    while (!pq.empty()){
        auto cur = pq.top(); 
        pq.pop();
        nodes_processed++;
        
        if (cur.w >= std::min(best, Wcap)) continue;
        
        if (cur.r == R){
            best = std::min(best, cur.w);
            hist[cur.w]++;
            if (export_topN > 0){
                topN.emplace_back(cur.w, cur);
                std::sort(topN.begin(), topN.end(), [](auto&a, auto&b){ return a.first < b.first; });
                if ((int)topN.size() > export_topN) topN.pop_back();
            }
            continue;
        }

        // Lower bound prune (use optimized canonical state)
        auto cc = canonical_func(cur.mA(), cur.mB());
        
        int lb1 = 0; int lbTail = 0; bool have = false;
        auto it = lb_memo.find(cc);
        if (it != lb_memo.end()){ 
            lb1 = it->second; 
            have = true; 
        }
        
        if (!have){ 
            lb1 = LBL.lb_full(cc.dA(), cc.dB(), 3, 3, 32, std::min(best, Wcap) - cur.w); 
            lb_memo.emplace(cc, lb1); 
        }
        
        int remTail = R - cur.r - 1;
        if (remTail > 0){
            lbTail = use_hwl ? HWL.query(cc.dA(), cc.dB(), remTail)
                             : SFX.bound(cc.dA(), cc.dB(), remTail, std::min(best, Wcap) - cur.w - lb1);
        }
        
        if (cur.w + lb1 + lbTail >= std::min(best, Wcap)) continue;

        // Subround 0 - Add 1: B += F(A) using optimized Wallén enumeration
        uint32_t mu1 = cur.mB();
        uint32_t nu1 = rotr(cur.mA(), 31) ^ rotr(cur.mA(), 17);
        
        enumerate_wallen_omegas_optimized(mu1, nu1, std::min(best, Wcap) - cur.w, 
            [&](uint32_t Bp_mask, int w1){
                uint32_t mu2 = cur.mA(); 
                uint32_t nu2 = 0u; // var-const
                
                enumerate_wallen_omegas_optimized(mu2, nu2, std::min(best, Wcap) - (cur.w + w1), 
                    [&](uint32_t Ap_mask, int w2){
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
                        
                        enumerate_wallen_omegas_optimized(mu3, nu3, std::min(best, Wcap) - (cur.w + w1 + w2), 
                            [&](uint32_t A3_mask, int w3){
                                // Add 4: var-const
                                uint32_t mu4 = B2_mask; 
                                uint32_t nu4 = 0u;
                                
                                enumerate_wallen_omegas_optimized(mu4, nu4, std::min(best, Wcap) - (cur.w + w1 + w2 + w3), 
                                    [&](uint32_t B3_mask, int w4){
                                        uint32_t B4in = B3_mask ^ rotl(A3_mask,24);
                                        uint32_t A4in = A3_mask ^ rotl(B4in,16);
                                        uint32_t Aout = l2_backtranspose(A4in);
                                        uint32_t Bout = l1_backtranspose(B4in);
                                        
                                        auto cn = canonical_func(Aout, Bout);
                                        LinearStateOptimized nxt(cn.dA(), cn.dB(), cur.w + w1+w2+w3+w4, cur.r+1);
                                        
                                        if (nxt.w < std::min(best, Wcap)) {
                                            pq.push(nxt);
                                        }
                                    });
                            });
                    });
            });
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    if (!export_path.empty()){
        std::ostringstream ss;
        ss << "algo,MELCC_OPTIMIZED"
           << ",R," << R
           << ",Wcap," << Wcap
           << ",start_mA,0x" << std::hex << start_mA << std::dec
           << ",start_mB,0x" << std::hex << start_mB << std::dec
           << ",best_w," << best
           << ",nodes_processed," << nodes_processed
           << ",time_ms," << duration.count()
           << ",fast_canonical," << (fast_canonical ? "true" : "false");
        TrailExport::append_csv(export_path, ss.str());
    }
    
    if (!export_hist_path.empty()){
        TrailExport::append_csv(export_hist_path, "algo,MELCC_OPTIMIZED,field,weight,count");
        for (auto& kv : hist){
            std::ostringstream ss;
            ss << "algo,MELCC_OPTIMIZED,hist," << kv.first << "," << kv.second;
            TrailExport::append_csv(export_hist_path, ss.str());
        }
    }
    
    if (export_topN > 0 && !export_topN_path.empty()){
        TrailExport::append_csv(export_topN_path, "algo,MELCC_OPTIMIZED,field,rank,weight,mA,mB");
        for (size_t i = 0; i < topN.size(); ++i){
            std::ostringstream ss;
            ss << "algo,MELCC_OPTIMIZED,topN," << (i+1) << "," << topN[i].first
               << ",0x" << std::hex << topN[i].second.mA() << std::dec
               << ",0x" << std::hex << topN[i].second.mB() << std::dec;
            TrailExport::append_csv(export_topN_path, ss.str());
        }
    }
    
    std::fprintf(stderr, "[analyze_melcc_optimized] best linear weight = %d (processed %lu nodes in %ld ms)\n", 
                 best, nodes_processed, duration.count());
    return 0;
}