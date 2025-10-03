#include <iostream>
#include <fstream>
#include <vector>
#include <tuple>
#include <cstdint>
#include <random>
#include "pddt.hpp"
#include "lb_round.hpp"

using namespace neoalz;

int main(int argc, char** argv){
    int n = (argc>1)? std::atoi(argv[1]) : 8;
    std::string mode = (argc>2)? std::string(argv[2]) : "exhaust";
    std::string out  = (argc>3)? std::string(argv[3]) : "round_lb_table.csv";
    LbCache LB;

    std::ofstream ofs(out);
    ofs << "n,"<<n<<",mode,"<<mode<<"\n";
    ofs << "dA,dB,lb_first_two\n";

    if (mode == "exhaust"){
        uint32_t maxv = (1u<<n);
        for (uint32_t dA=0; dA<maxv; ++dA){
            for (uint32_t dB=0; dB<maxv; ++dB){
                int lb = LB.lb_first_two(dA, dB, n, 64);
                ofs << dA << "," << dB << "," << lb << "\n";
            }
        }
    } else {
        int samples = (argc>4)? std::atoi(argv[4]) : 50000;
        std::mt19937_64 rng(0xC0FFEE);
        for (int i=0;i<samples;++i){
            uint32_t dA = uint32_t(rng()); uint32_t dB = uint32_t(rng());
            int lb = LB.lb_first_two(dA, dB, 32, 64);
            ofs << dA << "," << dB << "," << lb << "\n";
        }
    }
    std::cerr << "wrote " << out << "\n";
    return 0;
}
