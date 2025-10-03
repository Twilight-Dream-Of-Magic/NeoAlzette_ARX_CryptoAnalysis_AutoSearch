#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <random>
#include <algorithm>
#include "MELCC/highway_table_lin.hpp"
#include "MELCC/lb_round_lin.hpp"
#include "Common/canonicalize.hpp"

using neoalz::HighwayTableLin;

struct RNG { std::mt19937_64 eng{std::random_device{}()}; uint32_t word(){ return (uint32_t)eng(); } };

int main(int argc, char** argv){
    if (argc < 5){
        std::cerr << "Usage: " << argv[0] << " R samples_per_bucket out.bin nbits(=32) [seed]\n";
        return 1;
    }
    int R = std::stoi(argv[1]);
    int S = std::stoi(argv[2]);
    std::string out = argv[3];
    int n = (argc>4)? std::stoi(argv[4]) : 32;

    HighwayTableLin HT; HT.init(R);
    neoalz::LbFullRoundLin LBL;
    RNG rng; if (argc>5){ rng.eng.seed( (uint64_t)std::stoull(argv[5]) ); }

    for (int rem=1; rem<=R; ++rem){
        std::vector<uint16_t> best( (size_t)HighwayTableLin::BLOCK, 0xFFFFu );
        for (int wA=0; wA<=std::min(63,n); ++wA){
            for (int wB=0; wB<=std::min(63,n); ++wB){
                for (int pa=0; pa<2; ++pa){
                    for (int pb=0; pb<2; ++pb){
                        uint16_t key = (wA<<8)|(wB<<2)|(pa<<1)|pb;
                        int best_w = 0x3FFF;
                        for (int s=0; s<S; ++s){
                            auto pick = [&](int wt, int parity){
                                uint32_t x = 0; int placed = 0;
                                while (placed < wt){ int i = rng.word() & (n-1); uint32_t bit = 1u<<i; if ((x & bit)==0){ x|=bit; ++placed; } }
                                if ((x & 1u) != (uint32_t)parity) x ^= 1u; return x;
                            };
                            auto ca = pick(wA, pa); auto cb = pick(wB, pb);
                            auto cn = neoalz::canonical_rotate_pair(ca, cb);
                            int lb1 = LBL.lb_full(cn.first, cn.second, 3,3, n, 128);
                            int lb = lb1 * rem;
                            if (lb < best_w) best_w = lb;
                        }
                        if (best_w>0x3FFF) best_w=0x3FFF;
                        best[key] = (uint16_t)best_w;
                    }
                }
            }
        }
        for (size_t i=0;i<best.size();++i) HT.at(rem, (uint16_t)i) = best[i];
        std::cerr << "[HighwayLin] rem="<<rem<<" done.\n";
    }

    if (!HT.save(out)){
        std::cerr << "Failed to save to "<<out<<"\n"; return 2;
    }
    std::cout << "Saved Linear Highway table to "<<out<<"\n";
    return 0;
}

