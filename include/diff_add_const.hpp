#pragma once
#include <cstdint>
#include <optional>
#include <array>

namespace neoalz {

// Result for best gamma of addition-by-constant
struct AddConstBest { uint32_t gamma; int weight; };

// Inputs/Outputs/Complexity/Reference
// Inputs:
//   inputBit x       ∈ {0,1}, constantBit c ∈ {0,1}, carryIn k ∈ {0,1}
// Outputs:
//   carryOut bit for x + c + k (full-adder)
// Complexity:
//   O(1)
// Reference:
//   Standard full-adder majority relation: k_{i+1} = MAJ(x_i, c_i, k_i)
static inline int compute_carry_next_bit(int inputBit, int constantBit, int carryIn) noexcept {
    return ( (inputBit & constantBit) | (inputBit & carryIn) | (constantBit & carryIn) );
}

// addconst_best
// Inputs:
//   alpha  : input difference α = x ⊕ x'
//   c      : constant word added to x and x'
//   n      : word size (bits)
// Outputs:
//   Best (gamma, weight) for z = x + c and z' = x' + c under α, where γ = z ⊕ z'
// Complexity:
//   O(n · 4) DP over (carryIn, carryIn') with constant work per bit; negligible memory
// Reference:
//   "A Bit-Vector Differential Model for the Modular Addition by a Constant..."
static inline AddConstBest addconst_best(uint32_t alpha, uint32_t c, int n){
    const uint32_t mask = (n==32)? 0xFFFFFFFFu : ((1u<<n)-1);
    alpha &= mask; c &= mask;
    // DP from i=n to 0: f[i][carryIn][carryIn'] = max count of x_{i..n-1}
    std::array<std::array<std::array<uint32_t,2>,2>,33> f{};
    for (int k=0;k<2;++k) for (int kp=0;kp<2;++kp) f[n][k][kp] = 1u;
    for (int i=n-1;i>=0;--i){
        int bitAlpha = (alpha>>i)&1u;
        int bitConst = (c>>i)&1u;
        for (int carryIn=0;carryIn<2;++carryIn){
            for (int carryInPrime=0;carryInPrime<2;++carryInPrime){
                uint32_t best = 0;
                for (int inputBit=0; inputBit<=1; ++inputBit){
                    int carryOut     = compute_carry_next_bit(inputBit,      bitConst, carryIn);
                    int carryOutPrim = compute_carry_next_bit(inputBit^bitAlpha, bitConst, carryInPrime);
                    uint32_t cnt = f[i+1][carryOut][carryOutPrim];
                    if (cnt > best) best = cnt;
                }
                f[i][carryIn][carryInPrime] = (best<<1); // one free bit doubles count
            }
        }
    }
    // Reconstruct a maximizing path and gamma
    uint32_t gamma = 0;
    int carryIn=0, carryInPrime=0;
    for (int i=0;i<n;++i){
        int bitAlpha = (alpha>>i)&1u;
        int bitConst = (c>>i)&1u;
        // gamma_i = α_i ⊕ carryIn_i ⊕ carryIn'_i
        int gi = bitAlpha ^ carryIn ^ carryInPrime;
        if (gi) gamma |= (1u<<i);
        // choose x that maximizes suffix count
        uint32_t best = 0; int bestCarryOut=0, bestCarryOutPrim=0;
        for (int inputBit=0; inputBit<=1; ++inputBit){
            int carryOut     = compute_carry_next_bit(inputBit,      bitConst, carryIn);
            int carryOutPrim = compute_carry_next_bit(inputBit^bitAlpha, bitConst, carryInPrime);
            uint32_t cnt = f[i+1][carryOut][carryOutPrim];
            if (cnt > best){ best = cnt; bestCarryOut = carryOut; bestCarryOutPrim = carryOutPrim; }
        }
        carryIn = bestCarryOut; carryInPrime = bestCarryOutPrim;
    }
    // Total solutions = f[0][0][0]. Since each step doubled, f[0][0][0] is power of two.
    uint32_t cnt = f[0][0][0];
    // weight = n - log2(cnt)
    int w = n;
    while (cnt > 1){ cnt >>= 1; --w; }
    if (w<0) w=0;
    return {gamma, w};
}

// addconst_weight
// Inputs:
//   alpha, gamma, c, n  (same as addconst_best)
// Outputs:
//   Exact integer weight w if feasible; std::nullopt otherwise
// Complexity:
//   O(n · 4) DP over (carryIn, carryIn') with constant work per bit
// Reference:
//   Same as addconst_best
static inline std::optional<int> addconst_weight(uint32_t alpha, uint32_t gamma, uint32_t c, int n){
    const uint32_t mask = (n==32)? 0xFFFFFFFFu : ((1u<<n)-1);
    alpha &= mask; gamma &= mask; c &= mask;
    std::array<std::array<std::array<uint32_t,2>,2>,33> g{};
    for (int k=0;k<2;++k) for (int kp=0;kp<2;++kp) g[n][k][kp] = 1u;
    for (int i=n-1;i>=0;--i){
        int bitAlpha = (alpha>>i)&1u; int bitGamma = (gamma>>i)&1u; int bitConst = (c>>i)&1u;
        for (int carryIn=0; carryIn<2; ++carryIn){
            for (int carryInPrime=0; carryInPrime<2; ++carryInPrime){
                if ( (bitAlpha ^ carryIn ^ carryInPrime) != bitGamma ){ g[i][carryIn][carryInPrime] = 0; continue; }
                uint32_t sum = 0;
                for (int inputBit=0; inputBit<=1; ++inputBit){
                    int carryOut     = compute_carry_next_bit(inputBit,      bitConst, carryIn);
                    int carryOutPrim = compute_carry_next_bit(inputBit^bitAlpha, bitConst, carryInPrime);
                    sum += g[i+1][carryOut][carryOutPrim];
                }
                g[i][carryIn][carryInPrime] = sum;
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

