#include <cstdint>
#include <cstdio>
#include <vector>
#include <queue>
#include <tuple>
#include <limits>
#include <algorithm>
#include "wallen_fast.hpp"
#include "lb_round_full.hpp" // reuse diff linear pieces (rot/lin maps)
#include "highway_table.hpp"
#include "neoalzette.hpp"
#include "mask_backtranspose.hpp"

namespace neoalz {

struct LinPair { uint32_t mA, mB; int w; int r; };

constexpr uint32_t rotl(uint32_t x, int r) noexcept { r&=31; return (x<<r)|(x>>(32-r)); }
constexpr uint32_t rotr(uint32_t x, int r) noexcept { r&=31; return (x>>r)|(x<<(32-r)); }

static inline uint32_t l1_forward(uint32_t x) noexcept {
    return x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24);
}
static inline uint32_t l2_forward(uint32_t x) noexcept {
    return x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30);
}

static inline uint32_t l1_backtranspose(uint32_t x) noexcept {
    return l1_backtranspose_exact(x);
}
static inline uint32_t l2_backtranspose(uint32_t x) noexcept {
    return l2_backtranspose_exact(x);
}

struct Cmp {
    bool operator()(const LinPair& a, const LinPair& b) const noexcept {
        return a.w > b.w; // min-heap by weight
    }
};

} // namespace neoalz

int main(int argc, char** argv){
    using namespace neoalz;
    if (argc < 3){
        std::fprintf(stderr, "Usage: %s R Wcap [highway_lin.bin]\n", argv[0]);
        return 1;
    }
    int R = std::stoi(argv[1]);
    int Wcap = std::stoi(argv[2]);

    // Initial masks (can be parameterised; use nonzero seeds)
    LinPair root{0u, 0u, 0, 0};
    std::priority_queue<LinPair, std::vector<LinPair>, Cmp> pq;
    pq.push(root);

    int best = std::numeric_limits<int>::max();

    auto lower_bound = [&](const LinPair& s){
        // very conservative for now: 0
        return 0;
    };

    while (!pq.empty()){
        auto cur = pq.top(); pq.pop();
        if (cur.w >= std::min(best, Wcap)) continue;
        if (cur.r == R){
            best = std::min(best, cur.w);
            continue;
        }
        // ---- Subround 0 ----
        // Add 1: B += F(A)
        uint32_t mu1 = cur.mB;
        uint32_t nu1 = rotr(cur.mA, 31) ^ rotr(cur.mA, 17);
        auto push_after_add1 = [&](uint32_t Bp_mask, int w1){
            // Add 2: A -= RC (var-const)
            uint32_t mu2 = cur.mA;
            uint32_t nu2 = 0u;
            enumerate_wallen_omegas(mu2, nu2, Wcap - (cur.w + w1), [&](uint32_t Ap_mask, int w2){
                // Linear diffusion (choose consistent forward masks)
                uint32_t A2_mask = l1_backtranspose( Ap_mask ^ rotl(Bp_mask,24) );
                uint32_t B2_mask = l2_backtranspose( Bp_mask ^ rotl(Ap_mask ^ rotl(Bp_mask,24),16) );
                // Cross-branch injection into A*
                uint32_t C0m = l2_backtranspose( B2_mask );
                uint32_t D0m = l1_backtranspose( rotr(B2_mask,3) );
                uint32_t Astar_mask = A2_mask ^ rotl(C0m,24) ^ rotl(D0m,16);

                // ---- Subround 1 ----
                // Add 3: A* += F(B2)
                uint32_t mu3 = Astar_mask;
                uint32_t nu3 = rotr(B2_mask,31) ^ rotr(B2_mask,17);
                enumerate_wallen_omegas(mu3, nu3, Wcap - (cur.w + w1 + w2), [&](uint32_t A3_mask, int w3){
                    // Add 4: B -= RC
                    uint32_t mu4 = B2_mask;
                    uint32_t nu4 = 0u;
                    enumerate_wallen_omegas(mu4, nu4, Wcap - (cur.w + w1 + w2 + w3), [&](uint32_t B3_mask, int w4){
                        // Linear diffusion to round output
                        uint32_t B4in = B3_mask ^ rotl(A3_mask,24);
                        uint32_t A4in = A3_mask ^ rotl(B4in,16);
                        uint32_t Aout = l2_backtranspose(A4in);
                        uint32_t Bout = l1_backtranspose(B4in);

                        LinPair nxt{Aout, Bout, cur.w + w1+w2+w3+w4, cur.r+1};
                        int lb = lower_bound(nxt);
                        if (nxt.w + lb < std::min(best, Wcap)) pq.push(nxt);
                    });
                });
            });
        };
        enumerate_wallen_omegas(mu1, nu1, Wcap - cur.w, [&](uint32_t Bp_mask, int w1){
            push_after_add1(Bp_mask, w1);
        });
    }

    if (best < std::numeric_limits<int>::max()){
        std::printf("Best linear weight for R=%d: %d\n", R, best);
        return 0;
    } else {
        std::puts("No trail below cap.");
        return 2;
    }
}
