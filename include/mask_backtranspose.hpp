#pragma once
#include <cstdint>
#include <array>
#include "neoalz_lin.hpp"

namespace neoalz {

// Build inverse of L via Gauss–Jordan over GF(2) and provide (L^{-1})^T rows.
// Given an output mask x (column vector), y = (L^{-1})^T x can be computed as
// XOR of inv_rows[j] over set bits j in x.

struct InvTRows {
    std::array<uint32_t, 32> row; // row[j] used when bit j of x is set
};

// Build matrix A (rows) from a forward function: A[r] has bit c if (forward(e_c))_r = 1.
template<typename Forward>
static inline InvTRows build_invT_rows_from_forward(Forward f){
    uint32_t A[32] = {0};
    // columns by basis images
    for (int c=0; c<32; ++c){
        uint32_t y = f(1u<<c);
        for (int r=0; r<32; ++r){
            if ((y>>r) & 1u) A[r] |= (1u<<c);
        }
    }
    // Gauss–Jordan on A | I to compute inv(A)
    uint32_t B[32];
    for (int r=0; r<32; ++r) B[r] = (1u<<r);
    for (int c=0; c<32; ++c){
        int p = -1;
        for (int r=c; r<32; ++r){ if ((A[r]>>c) & 1u){ p=r; break; } }
        // Assume invertible; if not found, leave identity (safe fallback)
        if (p == -1) continue;
        if (p != c){ uint32_t ta=A[p]; A[p]=A[c]; A[c]=ta; uint32_t tb=B[p]; B[p]=B[c]; B[c]=tb; }
        for (int r=0; r<32; ++r){
            if (r==c) continue;
            if ((A[r]>>c) & 1u){ A[r] ^= A[c]; B[r] ^= B[c]; }
        }
    }
    InvTRows R{};
    // Using property: y = (L^{-1})^T x = XOR_{j in supp(x)} inv_row[j]
    for (int j=0; j<32; ++j) R.row[(size_t)j] = B[j];
    return R;
}

static inline uint32_t apply_invT_rows(const InvTRows& T, uint32_t x) noexcept {
    uint32_t y = 0;
    while (x){
        unsigned j = (unsigned)__builtin_ctz(x);
        y ^= T.row[j];
        x &= x - 1;
    }
    return y;
}

// Exact backtranspose for L1 and L2
static inline uint32_t l1_backtranspose_exact(uint32_t x) {
    static const InvTRows T = build_invT_rows_from_forward([](uint32_t v){ return l1_forward(v); });
    return apply_invT_rows(T, x);
}
static inline uint32_t l2_backtranspose_exact(uint32_t x) {
    static const InvTRows T = build_invT_rows_from_forward([](uint32_t v){ return l2_forward(v); });
    return apply_invT_rows(T, x);
}

} // namespace neoalz

