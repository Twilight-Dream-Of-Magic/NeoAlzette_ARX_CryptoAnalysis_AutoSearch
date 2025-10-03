#pragma once
/*
 * LM-2001 fast enumeration (Lipmaa–Moriai) for addition x + y = z over Z/2^n Z
 *
 * Inputs:
 *   alpha = x ⊕ y  (input difference)
 *   beta  = carry-related projection depending on F(A) in ARX var–var
 * Output:
 *   Enumerates feasible gamma with exact LM weight w = HW(psi mod 2^{n-1})
 *   using prefix pruning and incremental popcount.
 * Reference: Lipmaa–Moriai 2001; "Automatic Search for Differential Trails in ARX Ciphers".
 */
#include <cstdint>
#include <functional>
#include <array>
#include "neoalzette.hpp"

namespace neoalz {

// rotate helpers are provided by neoalzette.hpp

// Fast incremental LM-2001 enumeration with prefix-pruning.
// Tracks psi low-bit count incrementally to avoid recomputing popcounts.
template<typename Yield>
static inline void enumerate_lm_gammas_fast(uint32_t alpha, uint32_t beta, int n, int w_cap, Yield&& yield)
{
    struct Node{ int i; uint32_t g; uint32_t a_pref; uint32_t b_pref; uint32_t psi_pref; int w_pref; };
    // prefix mask for bit i included: (1<<(i+1))-1 but capped at 31 bits for LM weight
    auto pref_mask = [&](int i)->uint32_t{
        if (n==32) return (i>=30)? 0x7FFFFFFFu : ((1u<<(i+1))-1);
        int upto = (i+1 < n-1)? (i+1) : (n-1);
        return (upto==32)? 0xFFFFFFFFu : ((1u<<upto)-1);
    };
    std::array<int, 256> pc{};
    for (int v=0; v<256; ++v){ pc[v] = __builtin_popcount((unsigned)v); }

    std::vector<Node> st; st.reserve(128);
    st.push_back({0u, 0u, 0u, 0u, 0u, 0});
    while(!st.empty()){
        auto cur = st.back(); st.pop_back();
        int i = cur.i;
        if (i == n){
            // exact LM weight
            // reuse full computation for safety at leaf (cheap)
            uint32_t a = alpha, b = beta, g = cur.g;
            // impossibility guard
            uint32_t a1 = (a<<1), b1=(b<<1), g1=(g<<1);
            if (((a1 ^ b1) & (a1 ^ g1) & 0xFFFFFFFFu) & ((a ^ b ^ g ^ b1) & 0xFFFFFFFFu)) {
                continue;
            }
            uint32_t psi = (a ^ b) & (a ^ g);
            int w = __builtin_popcount( (n==32)? (psi & 0x7FFFFFFFu) : (psi & ((1u<<(n-1))-1)) );
            if (w <= w_cap) yield(g, w);
            continue;
        }
        // branch on gamma bit
        for (int bit=0; bit<=1; ++bit){
            uint32_t g2 = cur.g | (uint32_t(bit)<<i);
            uint32_t a_pref2 = cur.a_pref | (alpha & (1u<<i));
            uint32_t b_pref2 = cur.b_pref | (beta  & (1u<<i));
            // update psi on low prefix bits: psi_i = (a^b)&(a^g)
            uint32_t psi_bit = (( ( (a_pref2^b_pref2) & (1u<<i) ) && ( (a_pref2 ^ g2) & (1u<<i) ) ) ? (1u<<i) : 0u);
            uint32_t psi_pref2 = cur.psi_pref | psi_bit;

            // prefix impossibility on shifted psi * xorcond
            uint32_t pm = (1u<<(i+1)) - 1;
            uint32_t a1 = (a_pref2<<1) & pm, b1=(b_pref2<<1) & pm, g1=(g2<<1) & pm;
            uint32_t psi1 = ( (a1 ^ b1) & (a1 ^ g1) );
            uint32_t xorcond = ( (a_pref2 ^ b_pref2 ^ g2 ^ b1) & pm );
            if ((psi1 & xorcond) != 0) continue;

            // prefix weight lower bound from psi_pref2
            uint32_t mask = pref_mask(i);
            int wlb = 0;
            uint32_t x = psi_pref2 & mask;
            // popcount via 8-bit LUT
            while (x){ wlb += pc[x & 0xFF]; x >>= 8; }
            if (wlb > w_cap) continue;

            st.push_back({i+1, g2, a_pref2, b_pref2, psi_pref2, wlb});
        }
    }
}

// convenience alias matching older call sites
template<typename Yield>
static inline void enumerate_lm_gammas(uint32_t alpha, uint32_t beta, int n, int w_cap, Yield&& yield){
    enumerate_lm_gammas_fast(alpha, beta, n, w_cap, std::forward<Yield>(yield));
}

} // namespace neoalz
