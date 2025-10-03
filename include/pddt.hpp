\
#pragma once
#include <cstdint>
#include <vector>
#include <tuple>
#include <functional>
#include <optional>
#include <algorithm>

/*
 * Algorithm 1: pDDT for ADD and XOR (prefix-checked, probability thresholded)
 * Reference: "Automatic Search for Differential Trails in ARX Ciphers" (Alg.1)
 *
 * We implement word-size n (default 32). For ADD, we use Lipmaa–Moriai (2001)
 * exact model and its prefix infeasibility to prune; the running score is the
 * negative log2 probability (the "weight"). We threshold on weight.
 *
 * For XOR, DP is 1 if gamma == alpha ^ beta (weight 0), else 0 (infeasible).
 */

namespace neoalz {

struct PDDTTriple {
    uint32_t alpha;
    uint32_t beta;
    uint32_t gamma;
    int      weight; // LM-2001 exact weight (when n bits complete)
};

struct PDDTConfig {
    int n = 32;
    int w_thresh = 32; // keep triples with weight <= w_thresh (i.e., prob >= 2^-w_thresh)
};

namespace detail {
    inline uint32_t mask_low(int k){ return (k>=32)? 0xFFFFFFFFu : ((1u<<k)-1); }

    // Prefix infeasibility for LM-2001 on low (k) bits (bits 0..k-1).
    inline bool lm_prefix_impossible(uint32_t a, uint32_t b, uint32_t g, int k){
        uint32_t pm = mask_low(k);
        uint32_t a1 = (a<<1) & pm;
        uint32_t b1 = (b<<1) & pm;
        uint32_t g1 = (g<<1) & pm;
        uint32_t psi1 = (a1 ^ b1) & (a1 ^ g1);
        uint32_t xorcond = (a ^ b ^ g ^ b1) & pm;
        return (psi1 & xorcond) != 0;
    }
    // Exact LM weight on n bits; returns nullopt if impossible
    inline std::optional<int> lm_weight(uint32_t a, uint32_t b, uint32_t g, int n){
        uint32_t mask = (n==32)? 0xFFFFFFFFu : ((1u<<n)-1);
        a &= mask; b &= mask; g &= mask;
        // impossibility on all bits
        if (lm_prefix_impossible(a,b,g,n)) return std::nullopt;
        // w = HW( psi mod 2^(n-1) )
        uint32_t psi = (a ^ b) & (a ^ g);
        uint32_t low = (n==32)? (psi & 0x7FFFFFFFu) : (psi & ((1u<<(n-1))-1));
        int w = __builtin_popcount(low);
        return w;
    }
}

class PDDTAdder {
public:
    explicit PDDTAdder(PDDTConfig cfg={}): cfg_(cfg) {}

    // Compute all (alpha,beta,gamma) with weight <= w_thresh
    std::vector<PDDTTriple> compute() const {
        std::vector<PDDTTriple> out;
        recurse(0, 0, 0, 0, out); // k=0, empty prefixes
        return out;
    }

private:
    PDDTConfig cfg_;

    void recurse(int k, uint32_t ak, uint32_t bk, uint32_t gk,
                 std::vector<PDDTTriple>& out) const
    {
        if (k == cfg_.n) {
            auto w = detail::lm_weight(ak,bk,gk,cfg_.n);
            if (w && *w <= cfg_.w_thresh){
                out.push_back({ak,bk,gk,*w});
            }
            return;
        }
        // Try next bits x,y,z ∈ {0,1}, extend prefixes at bit k (LSB→MSB)
        for(int x=0;x<=1;++x){
            for(int y=0;y<=1;++y){
                for(int z=0;z<=1;++z){
                    uint32_t a2 = ak | (uint32_t(x)<<k);
                    uint32_t b2 = bk | (uint32_t(y)<<k);
                    uint32_t g2 = gk | (uint32_t(z)<<k);
                    // prune by prefix impossibility on (k+1) bits
                    if (detail::lm_prefix_impossible(a2,b2,g2,k+1)) continue;
                    // lower bound on weight using known low bits of psi
                    uint32_t psi = (a2 ^ b2) & (a2 ^ g2);
                    uint32_t low = (k+1==32)? (psi & 0x7FFFFFFFu)
                                            : (psi & detail::mask_low(std::min(cfg_.n-1,k+1)));
                    int w_lb = __builtin_popcount(low);
                    if (w_lb > cfg_.w_thresh) continue;
                    recurse(k+1, a2,b2,g2, out);
                }
            }
        }
    }
};

// XOR pDDT is trivial; we still provide an interface for uniformity
class PDDTXor {
public:
    explicit PDDTXor(PDDTConfig cfg={}): cfg_(cfg) {}
    std::vector<PDDTTriple> compute() const {
        std::vector<PDDTTriple> out;
        out.reserve(1u<<std::min(cfg_.n,16)); // not exact; just avoids 0
        // enumerate alpha,beta; gamma is determined; weight=0
        // For practical use, typically you don't precompute full XOR pDDT.
        for(uint32_t a=0; a < (1u<<std::min(cfg_.n,16)); ++a){
            for(uint32_t b=0; b < (1u<<std::min(cfg_.n,16)); ++b){
                uint32_t g = (a ^ b);
                out.push_back({a,b,g,0});
            }
        }
        return out;
    }
private:
    PDDTConfig cfg_;
};

} // namespace neoalz
