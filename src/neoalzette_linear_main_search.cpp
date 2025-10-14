#include "neoalzette/neoalzette_with_framework.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    using TwilightDream::NeoAlzetteFullPipeline;

    int rounds = (argc >= 2) ? std::atoi(argv[1]) : 4;
    NeoAlzetteFullPipeline::LinearPipeline::Config cfg;
    cfg.rounds = rounds;
    cfg.weight_cap = 30;
    cfg.start_mask_A = 0x00000001u;
    cfg.start_mask_B = 0x00000000u;
    cfg.precompute_clat = true;

    double melcc = NeoAlzetteFullPipeline::run_linear_analysis(cfg);
    std::printf("MELCC (rounds=%d): 2^{-%g}\n", rounds, -std::log2(melcc));
    return 0;
}
