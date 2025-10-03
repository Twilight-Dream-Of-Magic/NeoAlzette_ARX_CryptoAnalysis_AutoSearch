\
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <random>
#include <algorithm>
#include <chrono>
#include "include/highway_table.hpp"
#include "include/lb_round_full.hpp"

using neoalz::HighwayTable;

static uint32_t rotl(uint32_t x, int r){ r&=31; return (x<<r)|(x>>(32-r)); }

struct RNG {
    std::mt19937_64 eng;
    RNG(): eng(std::random_device{}()){}
    uint32_t word(){ return (uint32_t)eng(); }
};

int main(int argc, char** argv){
    if (argc < 5){
        std::cerr << "Usage: " << argv[0] << " R samples_per_bucket out.bin nbits(=32)\n";
        return 1;
    }
    int R = std::stoi(argv[1]);
    int S = std::stoi(argv[2]);
    std::string out = argv[3];
    int n = (argc>4)? std::stoi(argv[4]) : 32;

    HighwayTable HT; HT.init(R);
    neoalz::LbFullRound LBF;

    RNG rng;
    // Buckets: (wA=0..min(63,n), wB=0..min(63,n), pa in {0,1}, pb in {0,1})
    for (int rem=1; rem<=R; ++rem){
        std::vector<uint16_t> best( (size_t)HighwayTable::BLOCK, 0xFFFFu );
        for (int wA=0; wA<=std::min(63,n); ++wA){
            for (int wB=0; wB<=std::min(63,n); ++wB){
                for (int pa=0; pa<2; ++pa){
                    for (int pb=0; pb<2; ++pb){
                        uint16_t key = (wA<<8)|(wB<<2)|(pa<<1)|pb;
                        int best_w = 0x3FFF;
                        for (int s=0; s<S; ++s){
                            // sample words with required wt and parity
                            auto pick = [&](int wt, int parity){
                                uint32_t x = 0;
                                // simple random wt sampler
                                int placed = 0;
                                while (placed < wt){
                                    int i = rng.word() & (n-1);
                                    uint32_t bit = 1u<<i;
                                    if ((x & bit)==0){ x |= bit; ++placed; }
                                }
                                if ((x & 1u) != (uint32_t)parity){
                                    // flip one random bit to fix parity (may change weight by ±1 within bucket quality)
                                    x ^= 1u;
                                }
                                return x;
                            };
                            uint32_t dA = pick(wA, pa);
                            uint32_t dB = pick(wB, pb);

                            // Lower bound for a suffix of 'rem' rounds:
                            // conservative product: rem * lb_full(dA,dB)
                            int lb1 = LBF.lb_full(dA,dB, 4,4, n, 128);
                            int lb = lb1 * rem;
                            if (lb < best_w) best_w = lb;
                        }
                        if (best_w>0x3FFF) best_w=0x3FFF;
                        best[key] = (uint16_t)best_w;
                    }
                }
            }
        }
        // write block
        for (size_t i=0;i<best.size();++i){
            HT.at(rem, (uint16_t)i) = best[i];
        }
        std::cerr << "[Highway] rem="<<rem<<" done.\n";
    }

    if (!HT.save(out)){
        std::cerr << "Failed to save to "<<out<<"\n";
        return 2;
    }
    std::cout << "Saved Highway table to "<<out<<"\n";
    return 0;
}
