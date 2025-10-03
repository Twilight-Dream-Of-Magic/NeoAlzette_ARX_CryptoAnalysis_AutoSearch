#include <iostream>
#include <vector>
#include "lm_wallen.hpp"
using namespace neoalz;

int main(){
    uint32_t mu=0, nu=0;
    int cnt=0, best=1e9;
    enumerate_wallen_omegas(mu,nu,32,[&](uint32_t omega){
        auto w = wallen_weight(mu,nu,omega,32);
        if (w){ best = std::min(best,*w); ++cnt; }
    });
    std::cout << "Feasible omegas: " << cnt << ", min linear weight=" << best << "\n";
    return 0;
}
