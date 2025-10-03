#include "neoalzette.hpp"
#include "lm_wallen.hpp"
#include <vector>
#include <tuple>
#include <unordered_map>

namespace neoalz {

std::pair<uint32_t,uint32_t> canonical(uint32_t valueA, uint32_t valueB){
    uint32_t bestA=valueA,bestB=valueB;
    for(int rot=0;rot<32;++rot){
        uint32_t rotatedA = rotl(valueA,rot);
        uint32_t rotatedB = rotl(valueB,rot);
        if (std::tie(rotatedA,rotatedB) < std::tie(bestA,bestB)) { bestA=rotatedA; bestB=rotatedB; }
    }
    return {bestA,bestB};
}

// LM prefix-pruning enumeration for gamma
void enumerate_lm_gammas(uint32_t lmAlpha, uint32_t lmBeta, int bitWidth, auto&& yield){
    struct Frame{ int bitIndex; uint32_t gammaPrefix; };
    std::vector<Frame> st; st.reserve(96);
    st.push_back({0,0});
    while(!st.empty()){
        auto [i,g] = st.back(); st.pop_back();
        if (i==bitWidth){ yield(g); continue; }
        for(int bit=0; bit<=1; ++bit){
            uint32_t g2 = g | (uint32_t(bit)<<i);
            uint32_t pm = (i==31)? 0xFFFFFFFFu : ((1u<<(i+1))-1);
            uint32_t a = lmAlpha & pm, b = lmBeta & pm, gg = g2 & pm;
            uint32_t a1 = (a<<1)&pm, b1=(b<<1)&pm, g1=(gg<<1)&pm;
            uint32_t psi1 = (a1 ^ b1) & (a1 ^ g1);
            uint32_t xorcond = (a ^ b ^ gg ^ b1);
            if ((psi1 & xorcond) != 0) continue;
            st.push_back({i+1,g2});
        }
    }
}

} // namespace neoalz
