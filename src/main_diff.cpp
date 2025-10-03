#include <iostream>
#include "neoalzette.hpp"
#include "lm_wallen.hpp"
#include <vector>

using namespace neoalz;

int main(int argc, char** argv){
    (void)argv;
    int R = (argc>1)? std::atoi(argv[1]) : 4;
    uint32_t dA0 = 0, dB0 = 0;
    // demo: single local add min weight
    uint32_t alpha = dB0;
    uint32_t beta  = (rotl(dA0,31) ^ rotl(dA0,17));
    int best = 1e9;
    // local generator is in bnb.cpp as free function (but we can inline a copy)
    auto enum_gammas = [&](auto&& yield){
        struct F{ int i; uint32_t g; };
        std::vector<F> st; st.reserve(96);
        st.push_back({0,0});
        while(!st.empty()){
            auto [i,g] = st.back(); st.pop_back();
            if (i==32){ yield(g); continue; }
            for(int bit=0; bit<=1; ++bit){
                uint32_t g2 = g | (uint32_t(bit)<<i);
                uint32_t pm = (i==31)? 0xFFFFFFFFu : ((1u<<(i+1))-1);
                uint32_t a = alpha & pm, b = beta & pm, gg = g2 & pm;
                uint32_t a1 = (a<<1)&pm, b1=(b<<1)&pm, g1=(gg<<1)&pm;
                uint32_t psi1 = (a1 ^ b1) & (a1 ^ g1);
                uint32_t xorcond = (a ^ b ^ gg ^ b1);
                if ((psi1 & xorcond) != 0) continue;
                st.push_back({i+1,g2});
            }
        }
    };
    enum_gammas([&](uint32_t gamma){
        auto w = lm_weight(alpha,beta,gamma,32);
        if (w) best = std::min(best, *w);
    });
    std::cout << "Demo: one-add min LM weight = " << best << "\n";
    std::cout << "(TODO: expand full two-subround round + PQ with LB.)\n";
    return 0;
}
