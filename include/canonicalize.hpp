#pragma once
#include <cstdint>
#include <tuple>
#include "neoalzette.hpp"

namespace neoalz {

static inline std::pair<uint32_t,uint32_t> canonical_rotate_pair(uint32_t a, uint32_t b){
    uint32_t bestA=a, bestB=b;
    for(int r=0;r<32;++r){
        uint32_t aa = rotl(a,r);
        uint32_t bb = rotl(b,r);
        if (std::tie(aa,bb) < std::tie(bestA,bestB)){ bestA=aa; bestB=bb; }
    }
    return {bestA,bestB};
}

} // namespace neoalz