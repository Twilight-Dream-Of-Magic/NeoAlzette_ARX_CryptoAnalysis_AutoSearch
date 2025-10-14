// Linear search CLI (standardized arguments)
#include "neoalzette/neoalzette_with_framework.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    int rounds = 4;
    int weightCap = 30;
    std::uint32_t startMaskA = 0x00000001u;
    std::uint32_t startMaskB = 0x00000000u;
    bool precompute = true; // cLAT precompute
    int clatMBits = 8;      // reserved for future (fixed 8 now)
};

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  -r, --rounds <int>           Number of rounds (default 4)\n"
        << "  -w, --weight-cap <int>       Per-trail weight cap (default 30)\n"
        << "      --start-mask-a <hex>     Start linear mask A_out (default 0x1)\n"
        << "      --start-mask-b <hex>     Start linear mask B_out (default 0x0)\n"
        << "      --precompute             Enable cLAT precompute (default)\n"
        << "      --no-precompute          Disable cLAT precompute\n"
        << "  -h, --help                   Show this help\n";
}

static bool parse_uint32(const char* s, std::uint32_t& out) {
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(s, &end, 0);
    if (end == s || *end != '\0') return false;
    if (v > 0xFFFFFFFFul) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

static bool parse_int(const char* s, int& out) {
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

static bool parse_args(int argc, char** argv, CliOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto consume_value = [&](int& idx) -> const char* {
            if (idx + 1 >= argc) return nullptr;
            return argv[++idx];
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "-r" || arg == "--rounds") {
            const char* v = consume_value(i);
            if (!parse_int(v, opts.rounds)) return false;
        } else if (arg.rfind("--rounds=", 0) == 0) {
            if (!parse_int(arg.c_str() + 9, opts.rounds)) return false;
        } else if (arg == "-w" || arg == "--weight-cap") {
            const char* v = consume_value(i);
            if (!parse_int(v, opts.weightCap)) return false;
        } else if (arg.rfind("--weight-cap=", 0) == 0) {
            if (!parse_int(arg.c_str() + 13, opts.weightCap)) return false;
        } else if (arg == "--start-mask-a") {
            const char* v = consume_value(i);
            if (!parse_uint32(v, opts.startMaskA)) return false;
        } else if (arg.rfind("--start-mask-a=", 0) == 0) {
            if (!parse_uint32(arg.c_str() + 15, opts.startMaskA)) return false;
        } else if (arg == "--start-mask-b") {
            const char* v = consume_value(i);
            if (!parse_uint32(v, opts.startMaskB)) return false;
        } else if (arg.rfind("--start-mask-b=", 0) == 0) {
            if (!parse_uint32(arg.c_str() + 15, opts.startMaskB)) return false;
        } else if (arg == "--precompute") {
            opts.precompute = true;
        } else if (arg == "--no-precompute") {
            opts.precompute = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    if (opts.rounds <= 0) {
        std::cerr << "Error: --rounds must be positive.\n";
        return false;
    }
    if (opts.weightCap < 0) {
        std::cerr << "Error: --weight-cap must be >= 0.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    using TwilightDream::NeoAlzetteFullPipeline;

    CliOptions options;
    if (!parse_args(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    NeoAlzetteFullPipeline::LinearPipeline::Config cfg;
    cfg.rounds = options.rounds;
    cfg.weight_cap = options.weightCap;
    cfg.start_mask_A = options.startMaskA;
    cfg.start_mask_B = options.startMaskB;
    cfg.precompute_clat = options.precompute;

    const double melcc = NeoAlzetteFullPipeline::run_linear_analysis(cfg);
    std::cout << "MELCC (rounds=" << options.rounds << "): 2^{-" << std::log2(1.0 / melcc) << "}\n";
    return 0;
}
