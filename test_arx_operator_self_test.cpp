#include <bit>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <array>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/linear_correlation_addconst.hpp"
#include "arx_analysis_operators/linear_correlation_add_logn.hpp"

using TwilightDream::arx_operators::xdp_add_lm2001_n;
using TwilightDream::arx_operators::find_optimal_gamma;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::diff_addconst_bvweight_q4_n;

namespace {

static inline int floor_log2_uint64(std::uint64_t value) {
    // C++20 portable floor(log2(value)), returns -1 for value==0.
    return value ? (static_cast<int>(std::bit_width(value)) - 1) : -1;
}

static bool is_power_of_two_uint64(std::uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static std::pair<std::uint32_t, int> brute_force_best_gamma_for_xdp_add_n(
    std::uint32_t alpha,
    std::uint32_t beta,
    int n
) {
    if (n <= 0) return {0u, 0};
    const std::uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    alpha &= mask;
    beta &= mask;

    std::uint32_t best_gamma = 0u;
    int best_w = std::numeric_limits<int>::max();

    for (std::uint32_t gamma = 0; gamma <= mask; ++gamma) {
        const int w = xdp_add_lm2001_n(alpha, beta, gamma, n);
        if (w < 0) continue;
        if (w < best_w) {
            best_w = w;
            best_gamma = gamma;
        }
    }

    // For any fixed (alpha,beta), DP+ over all gamma sums to 1 => at least one gamma is possible.
    if (best_w == std::numeric_limits<int>::max()) return {0u, -1};
    return {best_gamma, best_w};
}

static std::uint64_t brute_force_addconst_count_n(std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n) {
    if (n <= 0) return (delta_x == 0u && delta_y == 0u) ? 1ull : 0ull;
    if (n > 16) {
        // This is a brute-force helper; keep it small.
        std::cerr << "brute_force_addconst_count_n only supports n<=16\n";
        return 0ull;
    }

    const std::uint32_t mask = (1u << n) - 1u;
    delta_x &= mask;
    delta_y &= mask;
    constant &= mask;

    const std::uint32_t domain_size = (1u << n);
    std::uint64_t count = 0;
    for (std::uint32_t x = 0; x < domain_size; ++x) {
        const std::uint32_t y0 = (x + constant) & mask;
        const std::uint32_t x1 = (x ^ delta_x) & mask;
        const std::uint32_t y1 = (x1 + constant) & mask;
        if (((y0 ^ y1) & mask) == delta_y) ++count;
    }
    return count;
}

static inline int parity_u32(std::uint32_t v) noexcept {
    return static_cast<int>(std::popcount(v) & 1u);
}

static inline int msb_index_u32(std::uint32_t v) noexcept {
    return v ? (static_cast<int>(std::bit_width(v)) - 1) : -1;
}

static inline std::uint32_t strip_msb_once_u32(std::uint32_t v) noexcept {
    const int msb = msb_index_u32(v);
    if (msb < 0) return 0u;
    return v & ~(1u << msb);
}

// Definition 3 (Wallén 2003): cpm_k^i(x) on n-bit domain.
// For each j, cpm_k^i(x)_j = 1 iff k <= j < k+i and x_ℓ = 1 for all j < ℓ < k+i.
static std::uint32_t cpmki_naive(std::uint32_t x, int k, int i, int n) noexcept {
    if (n <= 0 || i <= 0) return 0u;
    if (k < 0) return 0u;

    const std::uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    x &= mask;

    const int end = k + i - 1;
    if (k >= n || end < 0) return 0u;
    if (end >= n) return 0u; // undefined by definition if window exceeds domain

    std::uint32_t out = 0u;
    bool all_ones_above = true;
    for (int j = end; j >= k; --j) {
        if (all_ones_above) out |= (1u << j);
        all_ones_above = all_ones_above && (((x >> j) & 1u) != 0u);
    }
    return out & mask;
}

// Definition 6 (Wallén 2003): cpm(x,y) on n-bit domain.
// NOTE: `strip_b(x)` in the paper corresponds to stripping 1 or 2 highest '1' bits (strip applied b times).
static std::uint32_t cpm_naive(std::uint32_t x, std::uint32_t y, int n) noexcept {
    if (n <= 0) return 0u;
    const std::uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    x &= mask;
    y &= mask;

    if (x == 0u) return 0u;

    const int j = msb_index_u32(x);
    const std::uint32_t x_stripped1 = strip_msb_once_u32(x);
    const int k = (x_stripped1 != 0u) ? msb_index_u32(x_stripped1) : 0;
    const int i = j - k;

    const std::uint32_t z = cpmki_naive(y, k, i, n);
    const bool z_subset_y = ((z & y) == z);

    std::uint32_t next_x = strip_msb_once_u32(x);
    if (z_subset_y) next_x = strip_msb_once_u32(next_x); // b=2, else b=1

    return (z ^ cpm_naive(next_x, y, n)) & mask;
}

static int run_cpm_logn_sanity_n8_exhaustive() {
    using TwilightDream::arx_operators::compute_cpm_logn_bitsliced;

    constexpr int n = 8;
    constexpr std::uint32_t mask = (1u << n) - 1u;

    for (std::uint32_t x = 0; x <= mask; ++x) {
        for (std::uint32_t y = 0; y <= mask; ++y) {
            const std::uint32_t ref = cpm_naive(x, y, n) & mask;
            const std::uint32_t got = static_cast<std::uint32_t>(compute_cpm_logn_bitsliced(x, y, n)) & mask;
            if (ref != got) {
                std::cout << "ARX Linear Analysis: [cpm(logn)] FAIL n=" << n
                          << " x=0x" << std::hex << x
                          << " y=0x" << y
                          << " ref=0x" << ref
                          << " got=0x" << got
                          << std::dec << "\n";
                return 1;
            }
        }
    }

    std::cout << "ARX Linear Analysis: [cpm(logn)] PASS (n=8 exhaustive)\n";
    return 0;
}

static double brute_force_corr_add_varvar_n(std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma, int n) {
    if (n <= 0) return 1.0;
    if (n > 16) {
        // Keep brute force small and auditable.
        std::cerr << "brute_force_corr_add_varvar_n only supports n<=16\n";
        return 0.0;
    }

    const std::uint32_t mask = (1u << n) - 1u;
    alpha &= mask;
    beta &= mask;
    gamma &= mask;

    const std::uint32_t domain_size = (1u << n);
    std::int64_t sum = 0;
    for (std::uint32_t x = 0; x < domain_size; ++x) {
        for (std::uint32_t y = 0; y < domain_size; ++y) {
            const std::uint32_t z = (x + y) & mask;
            const int e = parity_u32(alpha & x) ^ parity_u32(beta & y) ^ parity_u32(gamma & z);
            sum += (e ? -1 : 1);
        }
    }

    // Denominator is 2^(2n), exact as dyadic rational.
    const double denom = std::ldexp(1.0, 2 * n);
    return static_cast<double>(sum) / denom;
}

static std::uint32_t eq_mask_n(std::uint32_t x, std::uint32_t y, int n) {
    if (n <= 0) return 0u;
    const std::uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    return (~(x ^ y)) & mask;
}

static std::uint32_t cpm_naive_u32(std::uint32_t x, std::uint32_t y, int n) {
    if (n <= 0) return 0u;
    const std::uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    x &= mask;
    y &= mask;
    if (x == 0u) return 0u;

    auto msb = [](std::uint32_t v) -> int { return v ? (static_cast<int>(std::bit_width(v)) - 1) : -1; };
    auto strip1 = [&](std::uint32_t v) -> std::uint32_t {
        const int m = msb(v);
        return (m < 0) ? 0u : (v & ~(1u << m));
    };
    auto cpmki = [&](std::uint32_t vec, int k, int i) -> std::uint32_t {
        if (i <= 0 || k < 0) return 0u;
        const int end = k + i - 1;
        if (k >= n || end < 0 || end >= n) return 0u;
        std::uint32_t out = 0u;
        bool all_ones_above = true;
        for (int j = end; j >= k; --j) {
            if (all_ones_above) out |= (1u << j);
            all_ones_above = all_ones_above && (((vec >> j) & 1u) != 0u);
        }
        return out & mask;
    };

    std::function<std::uint32_t(std::uint32_t)> rec = [&](std::uint32_t xx) -> std::uint32_t {
        xx &= mask;
        if (xx == 0u) return 0u;
        const int j = msb(xx);
        const std::uint32_t xs1 = strip1(xx);
        const int k = (xs1 != 0u) ? msb(xs1) : 0;
        const int i = j - k;
        const std::uint32_t z = cpmki(y, k, i);
        const bool z_subset_y = ((z & y) == z);
        std::uint32_t next = strip1(xx);
        if (z_subset_y) next = strip1(next);
        return (z ^ rec(next)) & mask;
    };

    return rec(x) & mask;
}

static double brute_force_corr_add_const_n(std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n) {
    if (n <= 0) return 1.0;
    if (n > 16) {
        std::cerr << "brute_force_corr_add_const_n only supports n<=16\n";
        return 0.0;
    }

    const std::uint32_t mask = (1u << n) - 1u;
    alpha &= mask;
    constant &= mask;
    beta &= mask;

    const std::uint32_t domain_size = (1u << n);
    std::int64_t sum = 0;
    for (std::uint32_t x = 0; x < domain_size; ++x) {
        const std::uint32_t z = (x + constant) & mask;
        const int e = parity_u32(alpha & x) ^ parity_u32(beta & z);
        sum += (e ? -1 : 1);
    }

    const double denom = std::ldexp(1.0, n);
    return static_cast<double>(sum) / denom;
}

static double brute_force_corr_sub_const_n(std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n) {
    if (n <= 0) return 1.0;
    if (n > 16) {
        std::cerr << "brute_force_corr_sub_const_n only supports n<=16\n";
        return 0.0;
    }

    const std::uint32_t mask = (1u << n) - 1u;
    alpha &= mask;
    constant &= mask;
    beta &= mask;

    const std::uint32_t domain_size = (1u << n);
    std::int64_t sum = 0;
    for (std::uint32_t x = 0; x < domain_size; ++x) {
        const std::uint32_t z = (x - constant) & mask;
        const int e = parity_u32(alpha & x) ^ parity_u32(beta & z);
        sum += (e ? -1 : 1);
    }

    const double denom = std::ldexp(1.0, n);
    return static_cast<double>(sum) / denom;
}

static int run_addconst_exact_sanity_n8_random() {
    using TwilightDream::arx_operators::diff_addconst_exact_count_n;
    using TwilightDream::arx_operators::diff_addconst_exact_weight_n;
    using TwilightDream::arx_operators::diff_addconst_exact_weight_ceil_int_n;
    using TwilightDream::arx_operators::diff_addconst_weight_log2pi_n;
    using TwilightDream::arx_operators::diff_addconst_bvweight_q4_n;

    constexpr int n = 8;
    constexpr std::uint32_t mask = (1u << n) - 1u;

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<std::uint32_t> dist(0u, mask);

    int failures = 0;
    long double max_abs_weight_err = 0.0L;
    long double max_abs_q4_weight_err = 0.0L;

    // Random sampling (brute-force cost per sample is 2^n = 256 here).
    for (int t = 0; t < 5000; ++t) {
        const std::uint32_t dx = dist(rng);
        const std::uint32_t k = dist(rng);
        const std::uint32_t dy = dist(rng);

        const std::uint64_t brute = brute_force_addconst_count_n(dx, k, dy, n);
        const std::uint64_t dp = diff_addconst_exact_count_n(dx, k, dy, n);
        if (brute != dp) {
            std::cerr << "  [AddConstExact8] COUNT MISMATCH n=" << n
                      << " dx=0x" << std::hex << dx
                      << " k=0x" << k
                      << " dy=0x" << dy
                      << std::dec
                      << " brute=" << brute
                      << " dp=" << dp
                      << "\n";
            ++failures;
            if (failures > 20) break;
        }

        // Exact weight from DP count (double)
        const double w_dp = diff_addconst_exact_weight_n(dx, k, dy, n);
        const double w_log2pi = diff_addconst_weight_log2pi_n(dx, k, dy, n);

        if (brute == 0ull) {
            if (!std::isinf(w_dp) || !std::isinf(w_log2pi)) {
                std::cerr << "ARX Differential Analysis: [AddConstExact8] WEIGHT EXPECT INF but got finite: "
                          << " w_dp=" << w_dp << " w_log2pi=" << w_log2pi << "\n";
                ++failures;
            }
        } else {
            const long double err = std::fabs(static_cast<long double>(w_dp - w_log2pi));
            if (err > max_abs_weight_err) max_abs_weight_err = err;

            // Integer ceiling weight should match the no-float formula.
            const int w_int = diff_addconst_exact_weight_ceil_int_n(dx, k, dy, n);
            const int expected_w_int = n - (static_cast<int>(std::bit_width(brute)) - 1);
            if (w_int != expected_w_int) {
                std::cerr << "ARX Differential Analysis: [AddConstExact8] INT WEIGHT MISMATCH: w_int=" << w_int
                          << " expected=" << expected_w_int << "\n";
                ++failures;
            }

            // Q4 BvWeight error (informational)
            const std::uint32_t q4 = diff_addconst_bvweight_q4_n(dx, k, dy, n);
            if (q4 != 0xFFFFFFFFu) {
                const long double w_q4 = static_cast<long double>(q4) / 16.0L;
                const long double q4_err = std::fabs(w_q4 - static_cast<long double>(w_dp));
                if (q4_err > max_abs_q4_weight_err) max_abs_q4_weight_err = q4_err;
            }
        }
    }

    if (failures != 0) return 1;

    std::cout << "ARX Differential Analysis: [AddConstExact8] OK: addconst exact DP matches brute-force for n=" << n << "\n";
    std::cout << "  [AddConstExact8] Max |weight_dp - weight_log2pi| = " << std::setprecision(17)
              << static_cast<double>(max_abs_weight_err) << "\n";
    std::cout << "  [AddConstExact8] Max |weight_dp - weight_q4|     = " << std::setprecision(17)
              << static_cast<double>(max_abs_q4_weight_err) << " (informational, Q4 is approximate)\n";
    return 0;
}

struct Segment {
    int i; // MSB index of the segment (inclusive)
    int j; // LSB index of the segment (inclusive), with y[j]=0 as delimiter
};

static std::vector<Segment> make_random_segments(int n, std::mt19937& rng) {
    std::uniform_int_distribution<int> seg_count_dist(1, 6);
    std::uniform_int_distribution<int> width_dist(1, 10);
    std::uniform_int_distribution<int> gap_dist(0, 2);

    const int seg_count = seg_count_dist(rng);
    std::vector<Segment> segments;
    segments.reserve(static_cast<std::size_t>(seg_count));

    int pos = n - 1;
    for (int t = 0; t < seg_count && pos >= 1; ++t) {
        const int max_width = (pos >= 1) ? std::min(width_dist.max(), pos) : 1;
        std::uniform_int_distribution<int> wdist(1, max_width);
        const int width = wdist(rng); // number of '1' bits in y for this segment

        const int i = pos;
        const int j = i - width; // delimiter bit at j (y[j]=0), ones are (j+1..i)
        if (j < 0) break;

        segments.push_back(Segment{i, j});

        pos = j - 1 - gap_dist(rng);
    }

    return segments;
}

static std::uint32_t build_y_from_segments(const std::vector<Segment>& segments) {
    std::uint32_t y = 0u;
    for (const auto& s : segments) {
        for (int bit = s.j + 1; bit <= s.i; ++bit) y |= (1u << bit);
        // s.j is delimiter => keep 0
    }
    return y;
}

static std::uint32_t build_x_from_segments(const std::vector<Segment>& segments, std::mt19937& rng) {
    std::uint32_t x = 0u;
    for (const auto& s : segments) {
        const int data_width = s.i - s.j; // bits (s.j+1 .. s.i)
        if (data_width <= 0) continue;

        const std::uint32_t max_val = (data_width >= 31) ? 0x7FFFFFFFu : ((1u << data_width) - 1u);
        std::uniform_int_distribution<std::uint32_t> val_dist(1u, max_val);
        const std::uint32_t val = val_dist(rng);

        for (int k = 0; k < data_width; ++k) {
            if ((val >> k) & 1u) x |= (1u << (s.j + 1 + k));
        }
        // Keep delimiter bit 0 for clarity.
    }
    return x;
}

static std::uint32_t direct_sum_floor_log2_segments(std::uint32_t x, const std::vector<Segment>& segments) {
    std::uint32_t sum = 0u;
    for (const auto& s : segments) {
        // Find MSB within [j..i], then floor(log2(x[i,j])) = msb_pos - j
        int msb = -1;
        for (int bit = s.i; bit >= s.j; --bit) {
            if ((x >> bit) & 1u) {
                msb = bit;
                break;
            }
        }
        if (msb < 0) return 0xFFFFFFFFu; // invalid (should not happen due to generation)
        sum += static_cast<std::uint32_t>(msb - s.j);
    }
    return sum;
}

static std::uint32_t direct_sum_truncate_segments(std::uint32_t x, const std::vector<Segment>& segments) {
    std::uint32_t sum = 0u;
    for (const auto& s : segments) {
        // Truncate(x[i_t, j_t+1]) returns the 4 MSB bits of the sub-vector (padding zeros if width<4).
        std::uint32_t t = 0u;
        for (int lambda = 0; lambda < 4; ++lambda) {
            const int bit = s.i - lambda;
            const std::uint32_t b = (bit > s.j) ? ((x >> bit) & 1u) : 0u;
            t |= b << (3 - lambda);
        }
        sum += t;
    }
    return sum;
}

static int parallel_log_trunc_run_once(int n, std::mt19937& rng) {
    const auto segments = make_random_segments(n, rng);
    const std::uint32_t y = build_y_from_segments(segments);
    const std::uint32_t x = build_x_from_segments(segments, rng);

    const std::uint32_t direct_log = direct_sum_floor_log2_segments(x, segments);
    const std::uint32_t direct_trunc = direct_sum_truncate_segments(x, segments);
    if (direct_log == 0xFFFFFFFFu) return 0; // skip (should be rare)

    const std::uint32_t par_log = (n == 32)
                                      ? TwilightDream::bitvector::ParallelLog(x, y)
                                      : TwilightDream::bitvector::ParallelLog_n(x, y, n);
    const std::uint32_t par_trunc = (n == 32)
                                        ? TwilightDream::bitvector::ParallelTrunc(x, y)
                                        : TwilightDream::bitvector::ParallelTrunc_n(x, y, n);

    if (par_log != direct_log || par_trunc != direct_trunc) {
        std::printf("ARX Differential Analysis: [ParallelLog/Trunc] Mismatch (n=%d)\n", n);
        std::printf("    x=0x%08X y=0x%08X\n", x, y);
        std::printf("    ParallelLog=%u direct_log=%u\n", par_log, direct_log);
        std::printf("    ParallelTrunc=%u direct_trunc=%u\n", par_trunc, direct_trunc);
        std::printf("    segments:\n");
        for (const auto& s : segments) std::printf("      [i=%d, j=%d]\n", s.i, s.j);
        return 1;
    }

    return 0;
}

static int run_parallel_log_trunc_sanity() {
    std::mt19937 rng(0xA11CEu);
    for (int t = 0; t < 20000; ++t) {
        // Mix n=32 (native) and a smaller n (wrapper)
        if (parallel_log_trunc_run_once(32, rng) != 0) return 1;
        if (parallel_log_trunc_run_once(13, rng) != 0) return 1;
    }
    return 0;
}

} // namespace

int run_arx_operator_self_test() {
    std::cout << "[SelfTest] ARX analysis operators validation\n";

    // ------------------------------------------------------------
    // 1) Azimi et al. (DCC 2022) Example 3:
    //    - BvWeight(u,v,a) = 28 (Q4)  => apxweight = 28/16 = 1.75
    //    - exact DP = 5/16            => weight = -log2(5/16) ≈ 1.678
    // From papers_txt/... line ~2091.
    // ------------------------------------------------------------
    {
        constexpr int n = 10;
        const std::uint32_t u = 0x28Eu; // delta_x
        const std::uint32_t v = 0x28Au; // delta_y
        const std::uint32_t a = 0x22Eu; // constant

        const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n(u, a, v, n);
        std::cout << "  [BvWeight] expected_q4=28, got_q4=" << bvweight_q4 << "\n";
        if (bvweight_q4 != 28u) {
            // If this fails, print a small diagnostic by exploring a few plausible OCR-decoding variants
            // for Lemma 6 (w-construction). This makes it easier to pin down symbol/shift ambiguities.
            const std::uint32_t mask = (1u << n) - 1u;
            const std::uint32_t s000 = ( ~(u << 1) & ~(v << 1) ) & mask;
            const std::uint32_t s000_hat = s000 & ~TwilightDream::bitvector::LeadingZeros_n((~s000) & mask, n);
            const std::uint32_t t0 = (~s000_hat & ((s000 << 1) & mask)) & mask;
            const std::uint32_t t1 = (s000_hat & ~((s000 << 1) & mask)) & mask;

            const std::uint32_t s1 = ((a << 1) & mask) & t0;
            const std::uint32_t s2 = a & ((s000_hat << 1) & mask);
            const std::uint32_t qbase = (~((((a << 1) & mask) ^ (u & mask) ^ (v & mask))) ) & mask;

            // Baseline reconstruction using the current header formula (for easy comparison).
            {
                const std::uint32_t s_header = ( ((a << 1) & t0) ^ (a & ((s000 << 1) & mask)) ) & mask;
                std::uint32_t q_header = qbase & t1;
                const std::uint32_t d_header =
                    (TwilightDream::bitvector::RevCarry_n((((s000_hat << 1) & t1) & mask), q_header, n) | q_header) & mask;
                const std::uint32_t w_header = ((q_header - (s_header & d_header)) | (s_header & ~d_header)) & mask;
                std::cout << "  [BvWeight] baseline(header-like): w=0x" << std::hex << w_header << std::dec << "\n";
            }

            struct CombineOp { const char* name; std::uint32_t (*fn)(std::uint32_t,std::uint32_t,std::uint32_t); };

            struct MaskVariant { const char* name; std::uint32_t value; };
            const MaskVariant q_masks[] = {
                {"(none)", mask},
                {"t0", t0},
                {"t1", t1},
                {"t0>>1", (t0 >> 1) & mask},
                {"t1>>1", (t1 >> 1) & mask},
                {"t0<<1", (t0 << 1) & mask},
                {"t1<<1", (t1 << 1) & mask},
            };

            const CombineOp s_ops[] = {
                {"xor", +[](std::uint32_t x, std::uint32_t y, std::uint32_t m){ return (x ^ y) & m; }},
                {"add", +[](std::uint32_t x, std::uint32_t y, std::uint32_t m){ return (x + y) & m; }},
            };

            auto compute_bvweight_from_w = [&](std::uint32_t w_value) -> std::uint32_t {
                const std::uint32_t hw_uv = TwilightDream::bitvector::HammingWeight(((u ^ v) << 1) & mask);
                const std::uint32_t hw_s000hat = TwilightDream::bitvector::HammingWeight(s000_hat);
                const std::uint32_t parlog =
                    TwilightDream::bitvector::ParallelLog_n(((w_value & s000_hat) << 1) & mask, (s000_hat << 1) & mask, n);
                const std::uint32_t int_part = (hw_uv + hw_s000hat) - parlog;
                const std::uint32_t revcarry =
                    TwilightDream::bitvector::RevCarry_n(((w_value & s000_hat) << 1) & mask, (s000_hat << 1) & mask, n);
                const std::uint32_t frac =
                    TwilightDream::bitvector::ParallelTrunc_n((w_value << 1) & mask, revcarry & mask, n);
                return (int_part << 4) - (frac & 0xFu);
            };

            // Sanity: Table 4 states w = 0001010010 (0x052) and BvWeight = 28.
            {
                const std::uint32_t w_known = 0x052u;
                const std::uint32_t hw_uv = TwilightDream::bitvector::HammingWeight(((u ^ v) << 1) & mask);
                const std::uint32_t hw_s000hat = TwilightDream::bitvector::HammingWeight(s000_hat);
                const std::uint32_t parlog =
                    TwilightDream::bitvector::ParallelLog_n(((w_known & s000_hat) << 1) & mask, (s000_hat << 1) & mask, n);
                const std::uint32_t int_part = (hw_uv + hw_s000hat) - parlog;
                const std::uint32_t revcarry =
                    TwilightDream::bitvector::RevCarry_n(((w_known & s000_hat) << 1) & mask, (s000_hat << 1) & mask, n);
                const std::uint32_t frac =
                    TwilightDream::bitvector::ParallelTrunc_n((w_known << 1) & mask, revcarry & mask, n);
                const std::uint32_t bv_q4 = (int_part << 4) - (frac & 0xFu);
                std::cout << "  [BvWeight] diagnostic(known w=0x052): hw_uv=" << hw_uv
                          << " hw_s000hat=" << hw_s000hat
                          << " parlog=" << parlog
                          << " int=" << int_part
                          << " frac=" << (frac & 0xFu)
                          << " bv_q4=" << bv_q4
                          << "\n";
                std::cout << "  [BvWeight] s000=0x" << std::hex << s000
                          << " s000_hat=0x" << s000_hat
                          << " (w&s000_hat)<<1=0x" << (((w_known & s000_hat) << 1) & mask)
                          << " s000_hat<<1=0x" << ((s000_hat << 1) & mask)
                          << " RevCarry=0x" << (revcarry & mask)
                          << std::dec << "\n";

                // Detailed fraction components for debugging Proposition 1(b) mapping.
                const std::uint32_t x_val = (w_known << 1) & mask;
                const std::uint32_t y_val = revcarry & mask;
                const std::uint32_t y1 = (y_val << 1) & mask;
                const std::uint32_t y2 = (y_val << 2) & mask;
                const std::uint32_t y3 = (y_val << 3) & mask;
                const std::uint32_t y4 = (y_val << 4) & mask;
                const std::uint32_t z0 = x_val & y_val & ~y1;
                const std::uint32_t z1 = x_val & y1 & ~y2;
                const std::uint32_t z2 = x_val & y2 & ~y3;
                const std::uint32_t z3 = x_val & y3 & ~y4;
                const std::uint32_t hw0 = TwilightDream::bitvector::HammingWeight(z0);
                const std::uint32_t hw1 = TwilightDream::bitvector::HammingWeight(z1);
                const std::uint32_t hw2 = TwilightDream::bitvector::HammingWeight(z2);
                const std::uint32_t hw3 = TwilightDream::bitvector::HammingWeight(z3);
                const std::uint32_t frac_xor = (hw0) ^ (hw1 << 2) ^ (hw2 << 1) ^ (hw3);
                const std::uint32_t frac_add = (hw0) + (hw1 << 2) + (hw2 << 1) + (hw3);
                std::cout << "  [BvWeight] frac-debug hex: x=0x" << std::hex << x_val
                          << " y=0x" << y_val
                          << " z0=0x" << z0
                          << " z1=0x" << z1
                          << " z2=0x" << z2
                          << " z3=0x" << z3
                          << std::dec << "\n";
                std::cout << "  [BvWeight] frac-components: hw0=" << hw0
                          << " hw1=" << hw1
                          << " hw2=" << hw2
                          << " hw3=" << hw3
                          << " frac_xor=" << (frac_xor & 0xFu)
                          << " frac_add=" << (frac_add & 0xFu)
                          << "\n";
            }

            bool found = false;
            for (const auto& s_op : s_ops) {
                const std::uint32_t s_value = s_op.fn(s1, s2, mask);
                for (const auto& q_mask : q_masks) {
                    for (int q_shift = 0; q_shift <= 1; ++q_shift) {
                        const std::uint32_t q_value = q_shift ? (((qbase << 1) & mask) & q_mask.value) : (qbase & q_mask.value);

                        // d variants: try a small set of plausible first-arg masks around s000_hat and t0/t1
                        const std::uint32_t d_inputs[] = {
                            ((s000_hat << 1) & t0) & mask,
                            ((s000_hat << 1) & t1) & mask,
                            ((s000_hat << 1)) & mask,
                            (t0) & mask,
                            (t1) & mask,
                        };
                        const char* d_input_names[] = {
                            "(s000_hat<<1)&t0",
                            "(s000_hat<<1)&t1",
                            "(s000_hat<<1)",
                            "t0",
                            "t1",
                        };

                        for (int di = 0; di < 5; ++di) {
                            const std::uint32_t d_input = d_inputs[di];
                            const std::uint32_t d_value = (TwilightDream::bitvector::RevCarry_n(d_input, q_value, n) | q_value) & mask;

                            const std::uint32_t w_value = ((q_value - (s_value & d_value)) | (s_value & ~d_value)) & mask;
                            const std::uint32_t bv_q4 = compute_bvweight_from_w(w_value);
                            if (w_value == 0x052u && bv_q4 == 28u) {
                                std::cout << "  [BvWeight] Found matching variant:\n";
                                std::cout << "    s_op=" << s_op.name
                                          << "  q_mask=" << q_mask.name
                                          << "  q_shift=" << (q_shift ? "left1" : "none")
                                          << "  d_input=" << d_input_names[di] << "\n";
                                found = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                    if (found) break;
                }
                if (found) break;
            }
            if (!found) {
                std::cout << "ARX Differential Analysis: [BvWeight] No matching variant found in the small search set.\n";
            }

            std::cout << "ARX Differential Analysis: [BvWeight] FAIL\n";
            return 1;
        }

        // Exact reference: DP = 5/16, hence count = (5/16)*2^10 = 320.
        const std::uint64_t exact_count =
            TwilightDream::arx_operators::diff_addconst_exact_count_n(u, a, v, n);
        std::cout << " [ExactDP] expected_count=320, got_count=" << exact_count << "\n";
        if (exact_count != 320ull) {
            std::cout << "ARX Differential Analysis: [ExactDP] FAIL\n";
            return 1;
        }

        const double exact_probability =
            TwilightDream::arx_operators::diff_addconst_exact_probability_n(u, a, v, n);
        std::cout << " [ExactDP] expected_DP=0.3125, got_DP=" << std::setprecision(16) << exact_probability << "\n";
        if (exact_probability != 0.3125) {
            std::cout << "ARX Differential Analysis: [ExactDP] FAIL\n";
            return 1;
        }

        const double expected_weight = -std::log2(0.3125);
        const double exact_weight =
            TwilightDream::arx_operators::diff_addconst_exact_weight_n(u, a, v, n);
        const double exact_weight_log2pi =
            TwilightDream::arx_operators::diff_addconst_weight_log2pi_n(u, a, v, n);
        std::cout << " [ExactWeight] exact_weight=" << std::setprecision(16) << exact_weight
                  << " weight_log2pi=" << exact_weight_log2pi
                  << " expected=" << expected_weight << "\n";
        if (std::abs(exact_weight - expected_weight) > 1e-12) {
            std::cout << "ARX Differential Analysis: [ExactWeight] FAIL (count-DP)\n";
            return 1;
        }
        if (std::abs(exact_weight_log2pi - expected_weight) > 1e-12) {
            std::cout << "ARX Differential Analysis: [ExactWeight] FAIL (log2pi)\n";
            return 1;
        }

        std::cout << "ARX Differential Analysis: [BvWeight] PASS\n";
    }

    // ------------------------------------------------------------
    // 2) Machado exact DP baseline: exhaustive check for n=4
    // Verify diff_addconst_exact_count_n against brute force enumeration of x, for all (Δx, K, Δy).
    // Also cross-check:
    //   - is_diff_addconst_possible_n  <=>  count != 0
    //   - exact_weight(count) == weight_log2pi (Lemma 3/4/5) when count != 0
    // Reference:
    //   - Machado, "Differential Probability of Modular Addition with a Constant Operand" (ePrint 2001/052)
    //   - Azimi et al. restate an equivalent recursion as Theorem 2 (DCC 2022 / ASIACRYPT 2020 extended)
    // ------------------------------------------------------------
    {
        constexpr int n = 4;
        constexpr std::uint32_t mask = (1u << n) - 1u;

        for (std::uint32_t constant = 0; constant <= mask; ++constant) {
            for (std::uint32_t input_difference = 0; input_difference <= mask; ++input_difference) {
                std::array<std::uint64_t, 16> brute_counts{};
                brute_counts.fill(0);

                for (std::uint32_t x = 0; x <= mask; ++x) {
                    const std::uint32_t y = (x + constant) & mask;
                    const std::uint32_t y_star = ((x ^ input_difference) + constant) & mask;
                    const std::uint32_t output_difference = (y ^ y_star) & mask;
                    brute_counts[size_t(output_difference)]++;
                }

                for (std::uint32_t output_difference = 0; output_difference <= mask; ++output_difference) {
                    const std::uint64_t dp_count =
                        TwilightDream::arx_operators::diff_addconst_exact_count_n(
                            input_difference,
                            constant,
                            output_difference,
                            n
                        );
                    const std::uint64_t brute_count = brute_counts[size_t(output_difference)];
                    if (dp_count != brute_count) {
                        std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 input_difference=" << input_difference
                                  << " constant=" << constant
                                  << " output_difference=" << output_difference
                                  << " expected_count=" << brute_count
                                  << " got_count=" << dp_count
                                  << "\n";
                        return 5;
                    }

                    const bool possible =
                        TwilightDream::arx_operators::is_diff_addconst_possible_n(
                            input_difference,
                            constant,
                            output_difference,
                            n
                        );
                    if (possible != (dp_count != 0ull)) {
                        std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 input_difference=" << input_difference
                                  << " constant=" << constant
                                  << " output_difference=" << output_difference
                                  << " possible=" << (possible ? 1 : 0)
                                  << " count=" << dp_count
                                  << "\n";
                        return 6;
                    }

                    const double exact_weight =
                        TwilightDream::arx_operators::diff_addconst_exact_weight_n(
                            input_difference,
                            constant,
                            output_difference,
                            n
                        );
                    const double weight_log2pi =
                        TwilightDream::arx_operators::diff_addconst_weight_log2pi_n(
                            input_difference,
                            constant,
                            output_difference,
                            n
                        );
                    if (dp_count == 0ull) {
                        if (!std::isinf(exact_weight) || !std::isinf(weight_log2pi)) {
                            std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 expected +inf weights for impossible case\n";
                            return 7;
                        }
                    } else {
                        if (std::abs(exact_weight - weight_log2pi) > 1e-12) {
                            std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 weight mismatch input_difference=" << input_difference
                                      << " constant=" << constant
                                      << " output_difference=" << output_difference
                                      << " exact_weight=" << std::setprecision(16) << exact_weight
                                      << " weight_log2pi=" << weight_log2pi
                                      << "\n";
                            return 8;
                        }
                    }
                }
            }
        }

        std::cout << "ARX Differential Analysis: [AddConstExact] PASS (n=4 exhaustive)\n";
    }

    // ------------------------------------------------------------
    // 3) LM2001 xdp_add_lm2001_n brute-force check for n=4
    // Verify: count == 0 <=> weight == -1, else weight == 2n - log2(count).
    // ------------------------------------------------------------
    {
        constexpr int n = 4;
        constexpr std::uint32_t mask = (1u << n) - 1u;

        for (std::uint32_t alpha = 0; alpha <= mask; ++alpha) {
            for (std::uint32_t beta = 0; beta <= mask; ++beta) {
                std::array<std::uint64_t, 16> counts{};
                counts.fill(0);

                for (std::uint32_t x = 0; x <= mask; ++x) {
                    const std::uint32_t x_star = x ^ alpha;
                    for (std::uint32_t y = 0; y <= mask; ++y) {
                        const std::uint32_t y_star = y ^ beta;
                        const std::uint32_t z = (x + y) & mask;
                        const std::uint32_t z_star = (x_star + y_star) & mask;
                        const std::uint32_t gamma = (z ^ z_star) & mask;
                        counts[size_t(gamma)]++;
                    }
                }

                for (std::uint32_t gamma = 0; gamma <= mask; ++gamma) {
                    const std::uint64_t count = counts[size_t(gamma)];
                    const int computed_weight = xdp_add_lm2001_n(alpha, beta, gamma, n);

                    if (count == 0) {
                        if (computed_weight >= 0) {
                            std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma
                                      << " expected impossible, got weight=" << computed_weight << "\n";
                            return 2;
                        }
                        continue;
                    }

                    if (!is_power_of_two_uint64(count)) {
                        std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma
                                  << " count=" << count << " is not a power of two\n";
                        return 3;
                    }

                    const int expected_weight = 2 * n - floor_log2_uint64(count);
                    if (computed_weight != expected_weight) {
                        std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma
                                  << " expected_weight=" << expected_weight << " got_weight=" << computed_weight << "\n";
                        return 4;
                    }
                }
            }
        }

        std::cout << "ARX Differential Analysis: [LM2001] PASS (n=4 exhaustive)\n";
    }

    // ------------------------------------------------------------
    // 3.5) LM2001 Algorithm 4: find_optimal_gamma() correctness check
    // Verify: gamma returned by Algorithm 4 achieves the MIN weight among all gamma (small n brute-force).
    // ------------------------------------------------------------
    {
        // Exhaustive (small n) check: n=6 => 64*64 pairs, each brute-forces 64 gammas (fast).
        constexpr int n = 6;
        constexpr std::uint32_t mask = (1u << n) - 1u;

        for (std::uint32_t alpha = 0; alpha <= mask; ++alpha) {
            for (std::uint32_t beta = 0; beta <= mask; ++beta) {
                const auto [brute_gamma, brute_w] = brute_force_best_gamma_for_xdp_add_n(alpha, beta, n);
                if (brute_w < 0) {
                    std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n
                              << " alpha=" << alpha << " beta=" << beta
                              << " no feasible gamma found (unexpected)\n";
                    return 11;
                }

                const std::uint32_t opt_gamma = find_optimal_gamma(alpha, beta, n) & mask;
                const int opt_w = xdp_add_lm2001_n(alpha, beta, opt_gamma, n);
                if (opt_w != brute_w) {
                    std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n
                              << " alpha=" << alpha << " beta=" << beta
                              << " opt_gamma=" << opt_gamma << " opt_w=" << opt_w
                              << " brute_gamma=" << brute_gamma << " brute_w=" << brute_w
                              << "\n";
                    return 12;
                }

                const auto [opt_gamma2, opt_w2] = find_optimal_gamma_with_weight(alpha, beta, n);
                if (((opt_gamma2 & mask) != opt_gamma) || (opt_w2 != opt_w)) {
                    std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n
                              << " alpha=" << alpha << " beta=" << beta
                              << " wrapper_gamma=" << (opt_gamma2 & mask) << " wrapper_w=" << opt_w2
                              << " direct_gamma=" << opt_gamma << " direct_w=" << opt_w
                              << "\n";
                    return 13;
                }
            }
        }
        std::cout << "ARX Differential Analysis: [LM2001-Alg4] PASS (n=6 exhaustive)\n";

        // Random (medium n) check: n=10 => brute-force 1024 gammas per sample.
        // This is still cheap but catches masking/bit-reverse mistakes that only show up beyond tiny widths.
        {
            constexpr int n2 = 10;
            constexpr std::uint32_t mask2 = (1u << n2) - 1u;
            std::mt19937 rng(0xB16B00B5u);
            std::uniform_int_distribution<std::uint32_t> dist(0u, mask2);

            for (int t = 0; t < 2000; ++t) {
                const std::uint32_t alpha = dist(rng);
                const std::uint32_t beta = dist(rng);
                const auto [brute_gamma, brute_w] = brute_force_best_gamma_for_xdp_add_n(alpha, beta, n2);
                const std::uint32_t opt_gamma = find_optimal_gamma(alpha, beta, n2) & mask2;
                const int opt_w = xdp_add_lm2001_n(alpha, beta, opt_gamma, n2);
                if (opt_w != brute_w) {
                    std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n2
                              << " alpha=" << alpha << " beta=" << beta
                              << " opt_gamma=" << opt_gamma << " opt_w=" << opt_w
                              << " brute_gamma=" << brute_gamma << " brute_w=" << brute_w
                              << "\n";
                    return 14;
                }
            }
            std::cout << "ARX Differential Analysis: [LM2001-Alg4] PASS (n=10 random)\n";
        }
    }

    // ------------------------------------------------------------
    // 4) differential_addconst exact sanity: random sampling for n=8
    // Validate exact DP count/weight against brute force, and check log2pi closed-form.
    // ------------------------------------------------------------
    {
        if (run_addconst_exact_sanity_n8_random() != 0) {
            std::cout << "ARX Differential Analysis: [AddConstExact8] FAIL\n";
            return 9;
        }
        std::cout << "ARX Differential Analysis: [AddConstExact8] PASS\n";
    }

    // ------------------------------------------------------------
    // 5) Azimi Proposition 1 sanity: ParallelLog / ParallelTrunc consistency
    // ------------------------------------------------------------
    {
        if (run_parallel_log_trunc_sanity() != 0) {
            std::cout << "ARX Differential Analysis: [ParallelLog/Trunc] FAIL\n";
            return 10;
        }
        std::cout << "ARX Differential Analysis: [ParallelLog/Trunc] PASS\n";
    }

    // ------------------------------------------------------------
    // 6) Linear correlations of modular addition (exact, small-n brute-force)
    //    + Wallén Θ(log n) weight (32-bit) vs exact DP (random)
    // ------------------------------------------------------------
    {
        if (run_cpm_logn_sanity_n8_exhaustive() != 0) {
            std::cout << "ARX Linear Analysis: [cpm(logn)] FAIL\n";
            return 22;
        }
    }

    {
        constexpr int n = 4;
        constexpr std::uint32_t mask = (1u << n) - 1u;

        for (std::uint32_t alpha = 0; alpha <= mask; ++alpha) {
            for (std::uint32_t beta = 0; beta <= mask; ++beta) {
                for (std::uint32_t gamma = 0; gamma <= mask; ++gamma) {
                    const double brute = brute_force_corr_add_varvar_n(alpha, beta, gamma, n);
                    const auto lc = TwilightDream::arx_operators::linear_add_varvar32(alpha, beta, gamma, n);
                    if (lc.correlation != brute) {
                        std::cout << "ARX Linear Analysis: [AddVarVarLinear] FAIL n=" << n
                                  << " alpha=0x" << std::hex << alpha
                                  << " beta=0x" << beta
                                  << " gamma=0x" << gamma
                                  << std::dec
                                  << " brute=" << std::setprecision(17) << brute
                                  << " dp=" << lc.correlation
                                  << "\n";
                        return 15;
                    }
                }
            }
        }
        std::cout << "ARX Linear Analysis: [AddVarVarLinear] PASS (n=4 exhaustive)\n";
    }

    {
        constexpr int n = 4;
        constexpr std::uint32_t mask = (1u << n) - 1u;

        for (std::uint32_t constant = 0; constant <= mask; ++constant) {
            for (std::uint32_t alpha = 0; alpha <= mask; ++alpha) {
                for (std::uint32_t beta = 0; beta <= mask; ++beta) {
                    const double brute_add = brute_force_corr_add_const_n(alpha, constant, beta, n);
                    const auto lc_add =
                        TwilightDream::arx_operators::linear_x_modulo_plus_const32(alpha, constant, beta, n);
                    if (lc_add.correlation != brute_add) {
                        std::cout << "ARX Linear Analysis: [AddConstLinear] FAIL (add) n=" << n
                                  << " alpha=0x" << std::hex << alpha
                                  << " constant=0x" << constant
                                  << " beta=0x" << beta
                                  << std::dec
                                  << " brute=" << std::setprecision(17) << brute_add
                                  << " dp=" << lc_add.correlation
                                  << "\n";
                        return 16;
                    }

                    const double brute_sub = brute_force_corr_sub_const_n(alpha, constant, beta, n);
                    const auto lc_sub =
                        TwilightDream::arx_operators::linear_x_modulo_minus_const32(alpha, constant, beta, n);
                    if (lc_sub.correlation != brute_sub) {
                        std::cout << "ARX Linear Analysis: [AddConstLinear] FAIL (sub) n=" << n
                                  << " alpha=0x" << std::hex << alpha
                                  << " constant=0x" << constant
                                  << " beta=0x" << beta
                                  << std::dec
                                  << " brute=" << std::setprecision(17) << brute_sub
                                  << " dp=" << lc_sub.correlation
                                  << "\n";
                        return 17;
                    }
                }
            }
        }
        std::cout << "ARX Linear Analysis: [AddConstLinear] PASS (n=4 exhaustive)\n";
    }

    {
        std::mt19937 rng(0x1A2B3C4Du);
        std::uniform_int_distribution<std::uint32_t> dist(0u, 0xFFFFFFFFu);

        for (int t = 0; t < 10000; ++t) {
            const std::uint32_t u = dist(rng); // output mask
            const std::uint32_t v = dist(rng); // input mask x
            const std::uint32_t w = dist(rng); // input mask y

            const int wallen_w = TwilightDream::arx_operators::internal_addition_wallen_logn(u, v, w);
            const auto lc = TwilightDream::arx_operators::linear_add_varvar32(v, w, u, 32);
            const double corr = lc.correlation;

            // Extra sanity: check cpm computed inside Wallén formula against a direct Definition 6 implementation.
            const std::uint32_t vprime = v ^ u;
            const std::uint32_t wprime = w ^ u;
            const std::uint32_t eqvw = eq_mask_n(vprime, wprime, 32);
            const std::uint32_t z_ref = cpm_naive_u32(u, eqvw, 32);
            const int expected_feasible = ((vprime & ~z_ref) == 0u && (wprime & ~z_ref) == 0u) ? 1 : 0;

            int expected_w = -1;
            if (corr != 0.0) {
                int exp = 0;
                const double m = std::frexp(std::fabs(corr), &exp);
                if (m != 0.5) {
                    std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL (corr not power-of-two)"
                              << " corr=" << std::setprecision(17) << corr
                              << "\n";
                    return 18;
                }
                expected_w = 1 - exp; // because frexp(|corr|)=0.5*2^exp
            }

            if (wallen_w != expected_w) {
                std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL weight mismatch"
                          << " u=0x" << std::hex << u
                          << " v=0x" << v
                          << " w=0x" << w
                          << std::dec
                          << " wallen=" << wallen_w
                          << " expected=" << expected_w
                          << " corr=" << std::setprecision(17) << corr
                          << " expected_feasible=" << expected_feasible
                          << " z_ref=0x" << std::hex << z_ref << std::dec
                          << "\n";
                return 19;
            }

            if (wallen_w >= 0) {
                const double abs_corr = std::fabs(corr);
                const double abs_expected = std::ldexp(1.0, -wallen_w);
                if (abs_corr != abs_expected) {
                    std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL |corr| mismatch"
                              << " wallen=" << wallen_w
                              << " |corr|=" << std::setprecision(17) << abs_corr
                              << " expected=" << abs_expected
                              << "\n";
                    return 20;
                }
            } else {
                if (corr != 0.0) {
                    std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL expected corr=0 for infeasible case\n";
                    return 21;
                }
            }
        }
        std::cout << "ARX Linear Analysis: [WallenLogn32] PASS (n=32 random)\n";
    }

    std::cout << "[SelfTest] PASS\n\n";
    return 0;
}

