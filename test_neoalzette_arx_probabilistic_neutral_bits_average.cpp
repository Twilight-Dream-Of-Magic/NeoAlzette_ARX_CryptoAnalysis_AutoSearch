#include <iostream>
#include <random>
#include <iomanip>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <string>

// DO NOT TOUCH YOUR HEADER
#include "neoalzette/neoalzette_core.hpp"

using namespace TwilightDream;

/**
 * Average / Per-bit Neutrality (PNB-style) Analyzer
 *
 * Closely follows Aumasson et al. Algorithm 1 idea:
 *   - Pick random X, define X' = X xor ID (fixed input difference)
 *   - Compute OD = Enc(X) xor Enc(X')
 *   - Flip input bit i in BOTH X and X', recompute OD'
 *   - Count how often signature(OD) == signature(OD')
 *   - gamma_i = 2 * p_eq - 1
 *
 * Notes:
 *   - If signature is FULL64 (OD_a, OD_b), for random-like permutations p_eq ~ 2^-64 => gamma ~ -1.
 *     That's expected. To get more texture, use truncated signature modes.
 */

class AveragePNBAnalyzer {
public:
    enum class SigMode {
        FULL64,     // signature = (od_a, od_b)
        A32,        // signature = od_a
        B32,        // signature = od_b
        A_BYTE,     // signature = byte of od_a
        B_BYTE,     // signature = byte of od_b
        MASKED      // signature = (od_a & maskA, od_b & maskB)
    };

    struct SigSpec {
        SigMode mode = SigMode::FULL64;

        // For BYTE mode
        int byte_index = 0; // 0..3

        // For MASKED mode
        std::uint32_t maskA = 0xFFFFFFFFu;
        std::uint32_t maskB = 0xFFFFFFFFu;
    };

    struct BitResult {
        bool isA = true;  // true => flip A[i], false => flip B[i]
        int  pos  = 0;    // 0..31
        double p_eq = 0.0;
        double gamma = 0.0;
    };

private:
    std::mt19937 rng;

    struct Signature {
        std::uint32_t s0 = 0;
        std::uint32_t s1 = 0;
    };

    static inline std::uint32_t get_byte(std::uint32_t x, int byte_index) {
        return (x >> (8 * byte_index)) & 0xFFu;
    }

    static inline int popcnt32(std::uint32_t x) {
#if defined(__GNUG__) || defined(__clang__)
        return __builtin_popcount(x);
#else
        // portable fallback
        int c = 0;
        while (x) { x &= (x - 1); ++c; }
        return c;
#endif
    }

    static inline Signature make_signature(std::uint32_t od_a, std::uint32_t od_b, const SigSpec& spec) {
        Signature sig{};
        switch (spec.mode) {
            case SigMode::FULL64:
                sig.s0 = od_a;
                sig.s1 = od_b;
                break;
            case SigMode::A32:
                sig.s0 = od_a;
                sig.s1 = 0;
                break;
            case SigMode::B32:
                sig.s0 = od_b;
                sig.s1 = 0;
                break;
            case SigMode::A_BYTE:
                sig.s0 = get_byte(od_a, spec.byte_index);
                sig.s1 = 0;
                break;
            case SigMode::B_BYTE:
                sig.s0 = get_byte(od_b, spec.byte_index);
                sig.s1 = 0;
                break;
            case SigMode::MASKED:
                sig.s0 = od_a & spec.maskA;
                sig.s1 = od_b & spec.maskB;
                break;
        }
        return sig;
    }

    static inline bool sig_equal(const Signature& a, const Signature& b) {
        return (a.s0 == b.s0) && (a.s1 == b.s1);
    }

    static inline int signature_bits(const SigSpec& spec) {
        switch (spec.mode) {
            case SigMode::FULL64: return 64;
            case SigMode::A32:
            case SigMode::B32:    return 32;
            case SigMode::A_BYTE:
            case SigMode::B_BYTE: return 8;
            case SigMode::MASKED: return popcnt32(spec.maskA) + popcnt32(spec.maskB);
        }
        return 64;
    }

    static inline void run_rounds(std::uint32_t& a, std::uint32_t& b, int rounds) {
        for (int r = 0; r < rounds; ++r) {
            NeoAlzetteCore::forward(a, b);
        }
    }

public:
    explicit AveragePNBAnalyzer(std::uint32_t seed = 12345) : rng(seed) {}

    // Compute gamma for a single input bit (Algorithm-1 style, with signature)
    BitResult compute_gamma_one_bit(
        int rounds,
        std::size_t samples,
        std::uint32_t in_diff_a,
        std::uint32_t in_diff_b,
        bool flipA,
        int bit_pos,
        const SigSpec& sigspec
    ) {
        std::size_t eq = 0;
        const std::uint32_t maskA = flipA ? (1u << bit_pos) : 0u;
        const std::uint32_t maskB = flipA ? 0u : (1u << bit_pos);

        for (std::size_t i = 0; i < samples; ++i) {
            std::uint32_t x_a  = rng();
            std::uint32_t x_b  = rng();
            std::uint32_t xs_a = x_a ^ in_diff_a;
            std::uint32_t xs_b = x_b ^ in_diff_b;

            // base encrypt
            std::uint32_t z_a  = x_a,  z_b  = x_b;
            std::uint32_t zs_a = xs_a, zs_b = xs_b;
            run_rounds(z_a,  z_b,  rounds);
            run_rounds(zs_a, zs_b, rounds);

            std::uint32_t od_a = z_a ^ zs_a;
            std::uint32_t od_b = z_b ^ zs_b;
            Signature sig0 = make_signature(od_a, od_b, sigspec);

            // flip bit in BOTH X and X'
            std::uint32_t xp_a  = x_a  ^ maskA;
            std::uint32_t xp_b  = x_b  ^ maskB;
            std::uint32_t xsp_a = xs_a ^ maskA;
            std::uint32_t xsp_b = xs_b ^ maskB;

            std::uint32_t zp_a  = xp_a,  zp_b  = xp_b;
            std::uint32_t zsp_a = xsp_a, zsp_b = xsp_b;
            run_rounds(zp_a,  zp_b,  rounds);
            run_rounds(zsp_a, zsp_b, rounds);

            Signature sig1 = make_signature(zp_a ^ zsp_a, zp_b ^ zsp_b, sigspec);

            if (sig_equal(sig0, sig1)) {
                ++eq;
            }
        }

        double p = static_cast<double>(eq) / static_cast<double>(samples);
        double g = 2.0 * p - 1.0;

        BitResult br{};
        br.isA = flipA;
        br.pos = bit_pos;
        br.p_eq = p;
        br.gamma = g;
        return br;
    }

    // Analyze all 64 input bits and print summary
    void analyze_rounds(
        int rounds,
        std::size_t samples,
        std::uint32_t in_diff_a,
        std::uint32_t in_diff_b,
        const SigSpec& sigspec,
        double report_abs_gamma_threshold = 0.05
    ) {
        std::cout << "=================================================\n";
        std::cout << "  AveragePNBAnalyzer (Algorithm-1 style)\n";
        std::cout << "  rounds=" << rounds
                  << ", samples=" << samples
                  << ", ID=(" << std::hex << "0x" << in_diff_a << ", 0x" << in_diff_b << std::dec << ")\n";

        // Signature info + random baseline
        int k = signature_bits(sigspec);
        // p_rand = 2^-k (assuming uniform signature under random permutation)
        double p_rand = std::ldexp(1.0, -k);
        double gamma_rand = 2.0 * p_rand - 1.0;

        std::cout << "  signature_mode=";
        switch (sigspec.mode) {
            case SigMode::FULL64: std::cout << "FULL64"; break;
            case SigMode::A32:    std::cout << "A32"; break;
            case SigMode::B32:    std::cout << "B32"; break;
            case SigMode::A_BYTE: std::cout << "A_BYTE(byte=" << sigspec.byte_index << ")"; break;
            case SigMode::B_BYTE: std::cout << "B_BYTE(byte=" << sigspec.byte_index << ")"; break;
            case SigMode::MASKED: std::cout << "MASKED(maskA=0x" << std::hex << sigspec.maskA
                                            << ", maskB=0x" << sigspec.maskB << std::dec << ")"; break;
        }
        std::cout << "\n";
        std::cout << "  baseline (random-ish): p_eq ~ 2^-" << k
                  << " = " << std::setprecision(12) << p_rand
                  << ", gamma ~ " << std::setprecision(12) << gamma_rand << "\n";
        std::cout << "-------------------------------------------------\n";

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<BitResult> results;
        results.reserve(64);

        // A bits
        for (int i = 0; i < 32; ++i) {
            results.push_back(compute_gamma_one_bit(rounds, samples, in_diff_a, in_diff_b, true, i, sigspec));
        }
        // B bits
        for (int i = 0; i < 32; ++i) {
            results.push_back(compute_gamma_one_bit(rounds, samples, in_diff_a, in_diff_b, false, i, sigspec));
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> dt = t1 - t0;

        // Sort by |gamma| descending (most “interesting” first)
        std::sort(results.begin(), results.end(),
            [](const BitResult& a, const BitResult& b) {
                return std::fabs(a.gamma) > std::fabs(b.gamma);
            });

        // Summaries
        double mean_abs_gamma = 0.0;
        double max_abs_gamma = 0.0;
        int count_over = 0;
        for (const auto& r : results) {
            double ag = std::fabs(r.gamma);
            mean_abs_gamma += ag;
            if (ag > max_abs_gamma) max_abs_gamma = ag;
            if (ag >= report_abs_gamma_threshold) ++count_over;
        }
        mean_abs_gamma /= 64.0;

        std::cout << "Time: " << std::fixed << std::setprecision(3) << dt.count() << "s\n";
        std::cout << "Mean |gamma| over 64 bits = " << std::setprecision(6) << mean_abs_gamma << "\n";
        std::cout << "Max  |gamma| over 64 bits = " << std::setprecision(6) << max_abs_gamma << "\n";
        std::cout << "Count |gamma| >= " << report_abs_gamma_threshold << " : " << count_over << " / 64\n";
        std::cout << "-------------------------------------------------\n";

        // Print top 16
        std::cout << "Top 16 bits by |gamma|:\n";
        for (int i = 0; i < 16 && i < (int)results.size(); ++i) {
            const auto& r = results[i];
            std::cout << "  " << (r.isA ? "A[" : "B[") << std::dec << r.pos << "]"
                      << "  p_eq=" << std::fixed << std::setprecision(6) << r.p_eq
                      << "  gamma=" << std::showpos << std::fixed << std::setprecision(6) << r.gamma
                      << std::noshowpos << "\n";
        }

        // Optional: also print the full table (comment out if you don't want spam)
        std::cout << "-------------------------------------------------\n";
        std::cout << "Full table (A[0..31], B[0..31]) sorted by |gamma|:\n";
        for (const auto& r : results) {
            std::cout << "  " << (r.isA ? "A[" : "B[") << std::dec << r.pos << "]"
                      << "  p_eq=" << std::fixed << std::setprecision(6) << r.p_eq
                      << "  gamma=" << std::showpos << std::fixed << std::setprecision(6) << r.gamma
                      << std::noshowpos << "\n";
        }

        std::cout << "=================================================\n\n";
    }
};

static AveragePNBAnalyzer::SigSpec make_default_sigspec() {
    // Default: look at FULL64 OD equality (strict Algorithm-1 vibe).
    // If you want more granularity, try A32 or A_BYTE(byte=2).
    AveragePNBAnalyzer::SigSpec s;
    s.mode = AveragePNBAnalyzer::SigMode::FULL64;
    return s;
}

int main() {
    // ====== CONFIG YOU MOST LIKELY WANT TO TWEAK ======
    std::uint32_t seed = 12345;

    // Fixed Input Difference ID (important!):
    // Example: B[3] => ΔB = 1<<3
    std::uint32_t in_diff_a = 0x00000000u;
    std::uint32_t in_diff_b = 0x00000008u;

    // Signature selection:
    // FULL64  : strict, will often give gamma ~ -1 for random-like behavior
    // A32/B32 : less strict
    // A_BYTE/B_BYTE(byte=2) : byte-level signature (more texture, but baseline changes)
    // MASKED(maskA,maskB) : custom truncated signature
    auto sigspec = make_default_sigspec();
    // Example alternatives (uncomment one):
    // sigspec.mode = AveragePNBAnalyzer::SigMode::A32;
    // sigspec.mode = AveragePNBAnalyzer::SigMode::A_BYTE; sigspec.byte_index = 2;
    // sigspec.mode = AveragePNBAnalyzer::SigMode::MASKED; sigspec.maskA = 0x00FF0000u; sigspec.maskB = 0;

    int rounds_list[] = {1, 2, 3};

    // Samples per bit (each bit costs ~ 2 * samples encryptions):
    // If rounds small (1-3), 50k is okay; if you crank rounds, reduce.
    std::size_t samples = 50000;

    // Report threshold for |gamma|
    double report_abs_gamma_threshold = 0.05;
    // ==================================================

    AveragePNBAnalyzer analyzer(seed);

    for (int r : rounds_list) {
        analyzer.analyze_rounds(
            r,
            samples,
            in_diff_a,
            in_diff_b,
            sigspec,
            report_abs_gamma_threshold
        );
    }
    return 0;
}
