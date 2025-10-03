#pragma once
#include <cstdint>
#include <array>
#include "../Common/neoalz_lin.hpp"

namespace neoalz {

// Rows of (L^{-1})^T for a linear map L over GF(2). For any mask x, y = (L^{-1})^T x
// can be computed as XOR of rows[j] for bits j set in x.
struct InverseTransposeRows {
    std::array<uint32_t, 32> rows;
};

// Build matrix A from forward mapping f(e_c), then compute inv(A) via Gauss–Jordan over GF(2).
// The returned structure stores rows of (L^{-1})^T for fast application.
template<typename Forward>
static inline InverseTransposeRows build_inverse_transpose_rows_from_forward(Forward f){
    uint32_t A[32] = {0};
    for (int c=0; c<32; ++c){
        uint32_t y = f(1u<<c);
        for (int r=0; r<32; ++r){ if ((y>>r) & 1u) A[r] |= (1u<<c); }
    }
    uint32_t B[32];
    for (int r=0; r<32; ++r) B[r] = (1u<<r);
    for (int c=0; c<32; ++c){
        int p = -1;
        for (int r=c; r<32; ++r){ if ((A[r]>>c) & 1u){ p=r; break; } }
        if (p == -1) continue;
        if (p != c){ uint32_t ta=A[p]; A[p]=A[c]; A[c]=ta; uint32_t tb=B[p]; B[p]=B[c]; B[c]=tb; }
        for (int r=0; r<32; ++r){ if (r!=c && ((A[r]>>c) & 1u)){ A[r] ^= A[c]; B[r] ^= B[c]; } }
    }
    InverseTransposeRows R{};
    for (int j=0; j<32; ++j) R.rows[(size_t)j] = B[j];
    return R;
}

static inline uint32_t apply_inverse_transpose_rows(const InverseTransposeRows& T, uint32_t x) noexcept {
    uint32_t y = 0;
    while (x){ unsigned j = (unsigned)__builtin_ctz(x); y ^= T.rows[j]; x &= x - 1; }
    return y;
}

static inline uint32_t l1_backtranspose_exact(uint32_t x) {
    static const InverseTransposeRows T = build_inverse_transpose_rows_from_forward([](uint32_t v){ return l1_forward(v); });
    return apply_inverse_transpose_rows(T, x);
}
static inline uint32_t l2_backtranspose_exact(uint32_t x) {
    static const InverseTransposeRows T = build_inverse_transpose_rows_from_forward([](uint32_t v){ return l2_forward(v); });
    return apply_inverse_transpose_rows(T, x);
}

} // namespace neoalz

