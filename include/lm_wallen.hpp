#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace neoalz {

inline constexpr uint32_t MASK32 = 0xFFFFFFFFu;

inline std::optional<int> lm_weight(uint32_t alpha, uint32_t beta, uint32_t gamma, int n=32) {
    uint32_t mask = (n==32)? MASK32 : ((1u<<n)-1);
    uint32_t a = alpha & mask, b = beta & mask, g = gamma & mask;
    uint32_t a1 = (a << 1) & mask, b1 = (b << 1) & mask, g1 = (g << 1) & mask;
    uint32_t psi1 = (a1 ^ b1) & (a1 ^ g1);
    uint32_t xorcond = (a ^ b ^ g ^ b1);
    if ((psi1 & xorcond) != 0) return std::nullopt;
    uint32_t psi = (a ^ b) & (a ^ g);
    uint32_t low = (n==32)? (psi & 0x7FFFFFFFu) : (psi & ((1u<<(n-1))-1));
    int w = __builtin_popcount(low);
    return w;
}

// MSB-first enumerate omega feasible under Wallen for given mu,nu
template<class F>
inline void enumerate_wallen_omegas(uint32_t mu, uint32_t nu, int n, F yield) {
    struct Frame { int i; uint32_t omega; int prefix; };
    std::vector<Frame> st; st.reserve(64);
    st.push_back({n-1, 0u, 0});
    while(!st.empty()){
        auto [i, omega, prefix] = st.back(); st.pop_back();
        auto bit = [&](uint32_t x){ return int((x>>i)&1u); };
        for(int wi=0; wi<=1; ++wi){
            int v_i = (bit(mu) ^ bit(nu) ^ wi);
            int zstar_i = prefix; // XOR of higher v bits
            int ai = (bit(mu) ^ wi);
            int bi = (bit(nu) ^ wi);
            if (ai==1 && zstar_i==0) continue;
            if (bi==1 && zstar_i==0) continue;
            uint32_t omega2 = omega | (uint32_t(wi)<<i);
            int prefix2 = prefix ^ v_i;
            if (i==0) {
                yield(omega2);
            } else {
                st.push_back({i-1, omega2, prefix2});
            }
        }
    }
}

inline std::optional<int> wallen_weight(uint32_t mu, uint32_t nu, uint32_t omega, int n=32) {
    uint32_t v = mu ^ nu ^ omega;
    int wt = 0; int prefix = 0;
    for(int i=n-1;i>=0;--i){
        int zst = prefix;
        int ai = ((mu^omega)>>i)&1u;
        int bi = ((nu^omega)>>i)&1u;
        if (ai && !zst) return std::nullopt;
        if (bi && !zst) return std::nullopt;
        wt += zst;
        prefix ^= ((v>>i)&1u);
    }
    return wt;
}

} // namespace neoalz
