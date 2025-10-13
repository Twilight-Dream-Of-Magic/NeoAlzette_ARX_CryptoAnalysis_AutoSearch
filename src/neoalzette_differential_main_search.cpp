#include "neoalzette/neoalzette_with_framework.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    using neoalz::NeoAlzetteFullPipeline;

    int rounds = (argc >= 2) ? std::atoi(argv[1]) : 4;
    NeoAlzetteFullPipeline::DifferentialPipeline::Config cfg;
    cfg.rounds = rounds;
    cfg.weight_cap = 30;
    cfg.start_dA = 0x00000001u;
    cfg.start_dB = 0x00000000u;
    cfg.precompute_pddt = true;
    cfg.pddt_seed_stride = 8;

    double medcp = NeoAlzetteFullPipeline::DifferentialPipeline::run_differential_analysis(cfg);
    std::printf("MEDCP (rounds=%d): 2^{-%g}\n", rounds, -std::log2(medcp));
    return 0;
}
