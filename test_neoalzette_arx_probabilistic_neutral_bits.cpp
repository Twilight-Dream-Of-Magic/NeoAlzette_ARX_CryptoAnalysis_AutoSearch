// pnb_neoalzette.cpp
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>

#include "neoalzette/neoalzette_core.hpp"

using std::uint32_t;

// ---------- Adapter: call forward/backward whether static or member ----------
template <typename T>
concept HasStaticForward = requires(uint32_t& a, uint32_t& b) { T::forward(a, b); };

template <typename T>
concept HasMemberForward = requires(T t, uint32_t& a, uint32_t& b) { t.forward(a, b); };

template <typename T>
concept HasStaticBackward = requires(uint32_t& a, uint32_t& b) { T::backward(a, b); };

template <typename T>
concept HasMemberBackward = requires(T t, uint32_t& a, uint32_t& b) { t.backward(a, b); };

inline void neo_forward(uint32_t& a, uint32_t& b) {
    TwilightDream::NeoAlzetteCore::forward(a, b);
}

inline void neo_backward(uint32_t& a, uint32_t& b) {
    TwilightDream::NeoAlzetteCore::backward(a, b);
}

inline void run_rounds(int rounds, uint32_t& a, uint32_t& b) {
    for (int r = 0; r < rounds; ++r) neo_forward(a, b);
}

// ---------- Helpers ----------
inline uint32_t u32_bit(int bit) { return (bit >= 0 && bit < 32) ? (uint32_t(1) << bit) : 0; }

inline uint32_t byte_mask(int byte_index /*0=LSB..3=MSB*/) {
    return uint32_t(0xFFu) << (8 * byte_index);
}

inline int popcount_u32(uint32_t x) {
#if defined(__GNUG__) || defined(__clang__)
    return __builtin_popcount(x);
#else
    // portable fallback
    int c = 0;
    while (x) { x &= (x - 1); ++c; }
    return c;
#endif
}

// ---------- OD signature + event predicate for "byte-neutral" experiments ----------
struct ByteEvent {
    // input difference
    uint32_t da = 0;
    uint32_t db = 0;

    // output condition for the "biased event":
    //   (OD_A & maskA) == targetA  AND  (OD_B & maskB) == targetB
    uint32_t maskA = 0;
    uint32_t maskB = 0;
    uint32_t targetA = 0;
    uint32_t targetB = 0;

    // baseline probability for the predicate under random behavior
    double baseline = 1.0 / 256.0;

    std::string name;
};

struct MaskedODSignature {
    uint32_t sA = 0;
    uint32_t sB = 0;
};

inline MaskedODSignature eval_signature(const ByteEvent& ev, int rounds, uint32_t a, uint32_t b) {
    uint32_t a2 = a ^ ev.da;
    uint32_t b2 = b ^ ev.db;

    run_rounds(rounds, a, b);
    run_rounds(rounds, a2, b2);

    uint32_t dA = a ^ a2;
    uint32_t dB = b ^ b2;

    MaskedODSignature sig;
    sig.sA = dA & ev.maskA;
    sig.sB = dB & ev.maskB;
    return sig;
}

inline bool eval_event(const ByteEvent& ev, int rounds, uint32_t a, uint32_t b) {
    const auto sig = eval_signature(ev, rounds, a, b);
    if (sig.sA != ev.targetA) return false;
    if (sig.sB != ev.targetB) return false;
    return true;
}

// ---------- Monte Carlo for p, ratio, log2ratio ----------
struct ProbStats {
    double p = 0.0;
    double ratio = 0.0;
    double log2ratio = 0.0;
    uint64_t hits = 0;
    uint64_t trials = 0;
};

ProbStats estimate_event_prob(std::mt19937_64& rng, const ByteEvent& ev, int rounds, uint64_t trials) {
    uint64_t hits = 0;
    for (uint64_t i = 0; i < trials; ++i) {
        uint32_t a = uint32_t(rng());
        uint32_t b = uint32_t(rng());
        if (eval_event(ev, rounds, a, b)) ++hits;
    }
    double p = (trials == 0) ? 0.0 : double(hits) / double(trials);
    double ratio = (ev.baseline > 0) ? (p / ev.baseline) : 0.0;
    double log2r = (ratio > 0) ? std::log2(ratio) : -INFINITY;
    return {p, ratio, log2r, hits, trials};
}

// ---------- Neutrality measure gamma_i (PNB-style) ----------
struct GammaBit {
    bool isA = true;   // flip bit in A (true) or B (false)
    int bit = 0;       // 0..31
    double gamma = 0;  // in [-1,1]
};

double compute_gamma(std::mt19937_64& rng, const ByteEvent& ev, int rounds,
                     bool flip_in_A, int bit_pos, uint64_t trials) {
    // Algorithm-1 style neutrality:
    //   - compute a signature of OD (here: masked OD bits)
    //   - flip bit i in BOTH paired inputs (X and X')
    //   - count how often the OD-signature stays identical
    uint64_t same = 0;
    uint32_t mA = flip_in_A ? u32_bit(bit_pos) : 0;
    uint32_t mB = flip_in_A ? 0 : u32_bit(bit_pos);

    for (uint64_t i = 0; i < trials; ++i) {
        uint32_t a = uint32_t(rng());
        uint32_t b = uint32_t(rng());

        const auto sig0 = eval_signature(ev, rounds, a, b);

        // Flip the same bit in BOTH paired inputs (X and X') is implicitly handled by eval_signature(),
        // since it derives X' via XOR with (da,db):
        //   (a^mA)^da = (a^da)^mA.
        const auto sig1 = eval_signature(ev, rounds, a ^ mA, b ^ mB);

        if (sig0.sA == sig1.sA && sig0.sB == sig1.sB) ++same;
    }
    double pr = (trials == 0) ? 0.0 : double(same) / double(trials);
    return 2.0 * pr - 1.0;
}

// Greedy PNB-set: add bits while combined gamma >= threshold
struct PNBSetResult {
    uint32_t maskA = 0;
    uint32_t maskB = 0;
    std::vector<GammaBit> chosen;
};

double compute_gamma_multibit(std::mt19937_64& rng, const ByteEvent& ev, int rounds,
                              uint32_t flip_maskA, uint32_t flip_maskB, uint64_t trials) {
    uint64_t same = 0;
    for (uint64_t i = 0; i < trials; ++i) {
        uint32_t a = uint32_t(rng());
        uint32_t b = uint32_t(rng());

        const auto sig0 = eval_signature(ev, rounds, a, b);
        const auto sig1 = eval_signature(ev, rounds, a ^ flip_maskA, b ^ flip_maskB);

        if (sig0.sA == sig1.sA && sig0.sB == sig1.sB) ++same;
    }
    double pr = (trials == 0) ? 0.0 : double(same) / double(trials);
    return 2.0 * pr - 1.0;
}

PNBSetResult greedy_pnb_set(std::mt19937_64& rng, const ByteEvent& ev, int rounds,
                            uint64_t gamma_trials_single, uint64_t gamma_trials_set,
                            double threshold, double keep_min_single_gamma = -1.0) {
    std::vector<GammaBit> cand;
    cand.reserve(64);

    for (int i = 0; i < 32; ++i) {
        // IMPORTANT: if the flipped bit overlaps the input-difference bit in this branch,
        // flipping it in BOTH X and X' swaps the pair (X, X') -> (X', X), which makes the
        // OD signature identical by symmetry and yields a trivial gamma=1.
        if (ev.da & u32_bit(i)) continue;
        double g = compute_gamma(rng, ev, rounds, true, i, gamma_trials_single);
        if (g >= keep_min_single_gamma) cand.push_back({true, i, g});
    }
    for (int i = 0; i < 32; ++i) {
        if (ev.db & u32_bit(i)) continue;
        double g = compute_gamma(rng, ev, rounds, false, i, gamma_trials_single);
        if (g >= keep_min_single_gamma) cand.push_back({false, i, g});
    }

    std::sort(cand.begin(), cand.end(),
              [](const GammaBit& a, const GammaBit& b) { return a.gamma > b.gamma; });

    PNBSetResult res;
    for (const auto& gb : cand) {
        uint32_t testA = res.maskA | (gb.isA ? u32_bit(gb.bit) : 0);
        uint32_t testB = res.maskB | (gb.isA ? 0 : u32_bit(gb.bit));

        // Use fresh RNG stream for the set-check (important: avoid reusing same samples pathologically)
        double gset = compute_gamma_multibit(rng, ev, rounds, testA, testB, gamma_trials_set);
        if (gset >= threshold) {
            res.maskA = testA;
            res.maskB = testB;
            res.chosen.push_back(gb);
            std::cout << "  + add " << (gb.isA ? "A[" : "B[") << gb.bit
                      << "], single_gamma=" << std::fixed << std::setprecision(4) << gb.gamma
                      << ", combined_gamma=" << std::fixed << std::setprecision(4) << gset
                      << "\n";
        }
    }
    return res;
}

// ---------- Heatmap (byte event ratios) ----------
struct HeatCell {
    // input flip
    bool inA = true;
    int inBit = 0;

    // output observed
    bool outA = true;
    int outByte = 0;

    double ratio = 0.0;
    double log2ratio = 0.0;
    double p = 0.0;
};

struct HeatmapSummary {
    // [inBit][outByte] for A_in and B_in separately, each stores A_out and B_out
    double ratio_Ain_Aout[32][4]{};
    double ratio_Ain_Bout[32][4]{};
    double ratio_Bin_Aout[32][4]{};
    double ratio_Bin_Bout[32][4]{};
};

HeatmapSummary compute_heatmaps(std::mt19937_64& rng, int rounds, uint64_t trials_per_inputbit) {
    HeatmapSummary hm;

    auto one_row = [&](bool inA, int bit, uint64_t N,
                       double outA_ratio[4], double outB_ratio[4]) {
        uint64_t hitsA[4] = {0,0,0,0};
        uint64_t hitsB[4] = {0,0,0,0};

        uint32_t da = inA ? u32_bit(bit) : 0;
        uint32_t db = inA ? 0 : u32_bit(bit);

        for (uint64_t i = 0; i < N; ++i) {
            uint32_t a = uint32_t(rng());
            uint32_t b = uint32_t(rng());
            uint32_t a2 = a ^ da;
            uint32_t b2 = b ^ db;

            run_rounds(rounds, a, b);
            run_rounds(rounds, a2, b2);

            uint32_t dA = a ^ a2;
            uint32_t dB = b ^ b2;

            for (int by = 0; by < 4; ++by) {
                uint32_t byteA = (dA >> (8*by)) & 0xFFu;
                uint32_t byteB = (dB >> (8*by)) & 0xFFu;
                if (byteA == 0) ++hitsA[by];
                if (byteB == 0) ++hitsB[by];
            }
        }

        double baseline = 1.0 / 256.0;
        for (int by = 0; by < 4; ++by) {
            double pA = double(hitsA[by]) / double(N);
            double pB = double(hitsB[by]) / double(N);
            outA_ratio[by] = pA / baseline;
            outB_ratio[by] = pB / baseline;
        }
    };

    for (int bit = 0; bit < 32; ++bit) {
        double rAA[4], rAB[4];
        double rBA[4], rBB[4];

        one_row(true,  bit, trials_per_inputbit, rAA, rAB); // A_in -> (A_out,B_out)
        one_row(false, bit, trials_per_inputbit, rBA, rBB); // B_in -> (A_out,B_out)

        for (int by = 0; by < 4; ++by) {
            hm.ratio_Ain_Aout[bit][by] = rAA[by];
            hm.ratio_Ain_Bout[bit][by] = rAB[by];
            hm.ratio_Bin_Aout[bit][by] = rBA[by];
            hm.ratio_Bin_Bout[bit][by] = rBB[by];
        }
    }
    return hm;
}

HeatCell find_extreme_cell(const HeatmapSummary& hm, bool pick_max_abs_log2,
                           bool inA, bool outA) {
    HeatCell best;
    best.ratio = 1.0;
    best.log2ratio = 0.0;

    auto get_ratio = [&](int inBit, int outByte) -> double {
        if (inA && outA) return hm.ratio_Ain_Aout[inBit][outByte];
        if (inA && !outA) return hm.ratio_Ain_Bout[inBit][outByte];
        if (!inA && outA) return hm.ratio_Bin_Aout[inBit][outByte];
        return hm.ratio_Bin_Bout[inBit][outByte];
    };

    bool first = true;
    for (int i = 0; i < 32; ++i) {
        for (int by = 0; by < 4; ++by) {
            double ratio = get_ratio(i, by);
            double l2 = (ratio > 0) ? std::log2(ratio) : -INFINITY;

            double score = pick_max_abs_log2 ? std::fabs(l2) : l2;
            double bestScore = pick_max_abs_log2 ? std::fabs(best.log2ratio) : best.log2ratio;

            if (first || score > bestScore) {
                first = false;
                best.inA = inA;
                best.inBit = i;
                best.outA = outA;
                best.outByte = by;
                best.ratio = ratio;
                best.log2ratio = l2;
            }
        }
    }
    return best;
}

ByteEvent cell_to_event(const HeatCell& c) {
    ByteEvent ev;
    ev.da = c.inA ? u32_bit(c.inBit) : 0;
    ev.db = c.inA ? 0 : u32_bit(c.inBit);
    ev.maskA = c.outA ? byte_mask(c.outByte) : 0;
    ev.maskB = c.outA ? 0 : byte_mask(c.outByte);
    ev.targetA = 0;
    ev.targetB = 0;
    ev.baseline = 1.0 / 256.0;
    ev.name = std::string(c.inA ? "A_in" : "B_in")
            + " bit" + std::to_string(c.inBit)
            + " -> " + (c.outA ? "A_out" : "B_out")
            + " byte" + std::to_string(c.outByte)
            + " (byte==0)";
    return ev;
}

void dump_heatmap_csv(const HeatmapSummary& hm, const std::string& path_prefix) {
    auto dump = [&](const std::string& path, const double arr[32][4]) {
        std::ofstream f(path);
        f << "inBit,byte0,byte1,byte2,byte3\n";
        for (int i = 0; i < 32; ++i) {
            f << i;
            for (int by = 0; by < 4; ++by) f << "," << std::setprecision(10) << arr[i][by];
            f << "\n";
        }
    };

    dump(path_prefix + "_Ain_Aout.csv", hm.ratio_Ain_Aout);
    dump(path_prefix + "_Ain_Bout.csv", hm.ratio_Ain_Bout);
    dump(path_prefix + "_Bin_Aout.csv", hm.ratio_Bin_Aout);
    dump(path_prefix + "_Bin_Bout.csv", hm.ratio_Bin_Bout);
}

int main(int argc, char** argv) {
    // ---- knobs ----
    int rounds = 1;                       // set 1 or 2
    uint64_t trials_per_inputbit = 200000; // heatmap sample size per input bit
    uint64_t trials_event = 500000;        // event probability estimate
    uint64_t gamma_trials_single = 200000; // per-bit gamma estimate
    uint64_t gamma_trials_set = 200000;    // combined gamma check
    double greedy_threshold = 0.90;        // greedy acceptance threshold
    double keep_min_single_gamma = -1.0;   // keep all bits by default (filter is applied after estimating per-bit gamma)
    uint64_t seed = 12345;

    // quick CLI:
    //   test_neoalzette_arx_probabilistic_neutral_bits.exe <rounds> <trials_per_inputbit>
    //   test_neoalzette_arx_probabilistic_neutral_bits.exe <rounds> <trials_per_inputbit> <trials_event> <gamma_trials_single> <gamma_trials_set> <greedy_threshold> <keep_min_single_gamma> <seed>
    if (argc >= 2) rounds = std::stoi(argv[1]);
    if (argc >= 3) trials_per_inputbit = std::stoull(argv[2]);
    if (argc >= 4) trials_event = std::stoull(argv[3]);
    if (argc >= 5) gamma_trials_single = std::stoull(argv[4]);
    if (argc >= 6) gamma_trials_set = std::stoull(argv[5]);
    if (argc >= 7) greedy_threshold = std::stod(argv[6]);
    if (argc >= 8) keep_min_single_gamma = std::stod(argv[7]);
    if (argc >= 9) seed = std::stoull(argv[8]);

    std::mt19937_64 rng(seed);

    std::cout << "=================================================\n";
    std::cout << " NeoAlzette PNB-style + Heatmap\n";
    std::cout << "   - heatmap/event baseline: 1/256 (byte==0)\n";
    std::cout << "   - neutrality gamma: Algorithm-1 style, compares masked-OD signature equality\n";
    std::cout << " rounds=" << rounds
              << ", trials_per_inputbit=" << trials_per_inputbit
              << ", trials_event=" << trials_event
              << ", gamma_trials_single=" << gamma_trials_single
              << ", gamma_trials_set=" << gamma_trials_set
              << ", greedy_threshold=" << std::fixed << std::setprecision(3) << greedy_threshold
              << ", keep_min_single_gamma=" << std::fixed << std::setprecision(3) << keep_min_single_gamma
              << ", seed=" << std::dec << seed << "\n";
    std::cout << "=================================================\n\n";

    // 1) Heatmaps
    std::cout << "[1] Computing heatmaps...\n";
    auto hm = compute_heatmaps(rng, rounds, trials_per_inputbit);
    dump_heatmap_csv(hm, "neoalzette_heatmap_r" + std::to_string(rounds));
    std::cout << "    CSV dumped: neoalzette_heatmap_r" << rounds << "_*.csv\n";

    // 2) Pick extreme cells (example: focus on B_in -> A_out and A_in -> B_out)
    //    You can change these two lines to the direction you care about.
    HeatCell extreme1 = find_extreme_cell(hm, /*max abs log2*/ true, /*inA*/ false, /*outA*/ true);  // B_in -> A_out
    HeatCell extreme2 = find_extreme_cell(hm, /*max abs log2*/ true, /*inA*/ true,  /*outA*/ false); // A_in -> B_out

    auto print_cell = [&](const HeatCell& c, const char* tag) {
        std::cout << "    [" << tag << "] "
                  << (c.inA ? "A_in" : "B_in") << "[" << c.inBit << "] -> "
                  << (c.outA ? "A_out" : "B_out") << ".byte" << c.outByte
                  << " ratio=" << std::fixed << std::setprecision(6) << c.ratio
                  << " log2=" << std::fixed << std::setprecision(6) << c.log2ratio
                  << "\n";
    };
    std::cout << "\n[2] Extreme cells (max |log2(ratio)|):\n";
    print_cell(extreme1, "B_in->A_out");
    print_cell(extreme2, "A_in->B_out");

    // choose one to run PNB-set greedy
    ByteEvent ev = cell_to_event(extreme1);
    std::cout << "\n[3] Using event for PNB-set greedy:\n";
    std::cout << "    " << ev.name << "\n";
    std::cout << "    da=0x" << std::hex << ev.da << " db=0x" << ev.db
              << " maskA=0x" << ev.maskA << " maskB=0x" << ev.maskB
              << std::dec << "\n";
    {
        const int sig_bits = popcount_u32(ev.maskA) + popcount_u32(ev.maskB);
        const double p_eq_rand = (sig_bits <= 0) ? 1.0 : std::ldexp(1.0, -sig_bits); // 2^-k
        const double gamma_rand = 2.0 * p_eq_rand - 1.0;
        std::cout << "    signature_bits=" << sig_bits
                  << " -> random baseline: p_eq ~ 2^(-" << sig_bits << ")"
                  << " = " << std::fixed << std::setprecision(12) << p_eq_rand
                  << ", gamma ~ " << std::fixed << std::setprecision(12) << gamma_rand
                  << "\n";
    }

    // 3) Estimate event probability (sanity)
    std::cout << "\n[4] Estimating event probability...\n";
    auto st = estimate_event_prob(rng, ev, rounds, trials_event);
    std::cout << "    hits=" << st.hits << "/" << st.trials
              << " p=" << std::fixed << std::setprecision(8) << st.p
              << " ratio=" << std::fixed << std::setprecision(6) << st.ratio
              << " log2ratio=" << std::fixed << std::setprecision(6) << st.log2ratio
              << "\n";

    // 4) Greedy PNB-set
    std::cout << "\n[5] Greedy PNB-set (threshold=" << greedy_threshold
              << ", keep_min_single_gamma=" << keep_min_single_gamma
              << ", single_trials=" << gamma_trials_single
              << ", set_trials=" << gamma_trials_set << ")\n";

    auto res = greedy_pnb_set(rng, ev, rounds, gamma_trials_single, gamma_trials_set, greedy_threshold, keep_min_single_gamma);

    std::cout << "\n[6] Result PNB-set size=" << res.chosen.size() << "\n";
    std::cout << "    flip_maskA=0x" << std::hex << res.maskA
              << " flip_maskB=0x" << res.maskB << std::dec << "\n";
    std::cout << "    bits:";
    for (auto& gb : res.chosen) {
        std::cout << " " << (gb.isA ? "A[" : "B[") << gb.bit << "]";
    }
    std::cout << "\n";

    // optional: final combined gamma report
    if (res.maskA == 0 && res.maskB == 0) {
        std::cout << "    combined_gamma(check)=N/A (empty flip-mask)\n";
    } else {
        double gfinal = compute_gamma_multibit(rng, ev, rounds, res.maskA, res.maskB, gamma_trials_set);
        std::cout << "    combined_gamma(check)=" << std::fixed << std::setprecision(6) << gfinal << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
