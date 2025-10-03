#pragma once
#include <cstdint>
#include <optional>
#include <array>

namespace neoalz {

struct AddConstBest { uint32_t gamma; int weight; };

// Compute next carry of full-adder for bit values (x, c, k)
static inline int carry_next_bit(int x, int c, int k) noexcept {
    return ( (x & c) | (x & k) | (c & k) );
}

// Exact best gamma and weight for z = x + c, with input diff alpha = x ^ x'.
// Uses DP on carries (k, k'). Count of x solutions determines probability 2^{-w} with integer w.
static inline AddConstBest addconst_best(uint32_t alpha, uint32_t c, int n){
    const uint32_t mask = (n==32)? 0xFFFFFFFFu : ((1u<<n)-1);
    alpha &= mask; c &= mask;
    // DP from i=n to 0: f[i][k][k'] = max count of x_{i..n-1}
    std::array<std::array<std::array<uint32_t,2>,2>,33> f{};
    for (int k=0;k<2;++k) for (int kp=0;kp<2;++kp) f[n][k][kp] = 1u;
    for (int i=n-1;i>=0;--i){
        int a = (alpha>>i)&1u;
        int cbit = (c>>i)&1u;
        for (int k=0;k<2;++k){
            for (int kp=0;kp<2;++kp){
                uint32_t best = 0;
                for (int x=0;x<=1;++x){
                    int kn = carry_next_bit(x, cbit, k);
                    int kpn = carry_next_bit(x^a, cbit, kp);
                    uint32_t cnt = f[i+1][kn][kpn];
                    if (cnt > best) best = cnt;
                }
                f[i][k][kp] = (best<<1); // one free bit x doubles count
            }
        }
    }
    // Reconstruct a maximizing path and gamma
    uint32_t gamma = 0;
    int k=0, kp=0;
    for (int i=0;i<n;++i){
        int a = (alpha>>i)&1u;
        int cbit = (c>>i)&1u;
        // gamma_i = a ^ k ^ kp
        int gi = a ^ k ^ kp;
        if (gi) gamma |= (1u<<i);
        // choose x that maximizes suffix count
        uint32_t best = 0; int bestx = 0; int best_kn=0, best_kpn=0;
        for (int x=0;x<=1;++x){
            int kn = carry_next_bit(x, cbit, k);
            int kpn = carry_next_bit(x^a, cbit, kp);
            uint32_t cnt = f[i+1][kn][kpn];
            if (cnt > best){ best = cnt; bestx = x; best_kn = kn; best_kpn = kpn; }
        }
        (void)bestx;
        k = best_kn; kp = best_kpn;
    }
    // Total solutions = f[0][0][0]. Since each step doubled, f[0][0][0] is power of two.
    uint32_t cnt = f[0][0][0];
    // weight = n - log2(cnt)
    int w = n;
    while (cnt > 1){ cnt >>= 1; --w; }
    if (w<0) w=0;
    return {gamma, w};
}

// Exact weight for given (alpha, gamma, c) via DP on carries
static inline std::optional<int> addconst_weight(uint32_t alpha, uint32_t gamma, uint32_t c, int n){
    const uint32_t mask = (n==32)? 0xFFFFFFFFu : ((1u<<n)-1);
    alpha &= mask; gamma &= mask; c &= mask;
    std::array<std::array<std::array<uint32_t,2>,2>,33> g{};
    for (int k=0;k<2;++k) for (int kp=0;kp<2;++kp) g[n][k][kp] = 1u;
    for (int i=n-1;i>=0;--i){
        int a = (alpha>>i)&1u; int gi = (gamma>>i)&1u; int cbit = (c>>i)&1u;
        for (int k=0;k<2;++k){
            for (int kp=0;kp<2;++kp){
                if ( (a ^ k ^ kp) != gi ){ g[i][k][kp] = 0; continue; }
                uint32_t sum = 0;
                for (int x=0;x<=1;++x){
                    int kn = carry_next_bit(x, cbit, k);
                    int kpn = carry_next_bit(x^a, cbit, kp);
                    sum += g[i+1][kn][kpn];
                }
                g[i][k][kp] = sum;
            }
        }
    }
    uint32_t cnt = g[0][0][0];
    if (cnt == 0) return std::nullopt;
    int w = n;
    while (cnt > 1){ cnt >>= 1; --w; }
    if (w<0) w=0;
    return w;
}

} // namespace neoalz

