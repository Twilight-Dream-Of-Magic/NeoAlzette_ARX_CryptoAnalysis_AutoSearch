#pragma once
#pragma once
/*
 * NeoAlzette linear layers and cross-branch delta injectors (value domain)
 *
 * Symbols (GF(2) bit-vectors of length n=32 by default):
 *   x << r, x >> r   : bitwise rotations (rotl/rotr)
 *   L1(x) = x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24)
 *   L2(x) = x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30)
 *
 * Cross-branch linearized mix (delta domain; round constants vanish):
 *   From B: (C0,D0) = ( L2(B), L1(rotr(B,3)) );
 *           t = rotl(C0 ^ D0, 31);
 *           C0 ^= rotl(D0,17);  D0 ^= rotr(t,16)
 *   From A: (C1,D1) = ( L1(A), L2(rotl(A,24)) );
 *           t = rotr(C1 ^ D1, 31);
 *           C1 ^= rotr(D1,17);  D1 ^= rotl(t,16)
 * These equal the linear parts used in NeoAlzette’s subrounds with constants removed.
 */
#include <cstdint>
#include "neoalzette.hpp"

namespace neoalz {

// Linear diffusion used by NeoAlzette
static inline uint32_t l1_forward(uint32_t x) noexcept {
    return x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24);
}
static inline uint32_t l2_forward(uint32_t x) noexcept {
    return x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30);
}

// Cross-branch linear injectors (delta domain; constants vanish)
static inline std::pair<uint32_t,uint32_t> cd_from_B_delta(uint32_t B) noexcept {
    uint32_t c = l2_forward(B);
    uint32_t d = l1_forward(rotr(B,3));
    uint32_t t = rotl(c ^ d, 31);
    c ^= rotl(d,17);
    d ^= rotr(t,16);
    return {c,d};
}
static inline std::pair<uint32_t,uint32_t> cd_from_A_delta(uint32_t A) noexcept {
    uint32_t c = l1_forward(A);
    uint32_t d = l2_forward(rotl(A,24));
    uint32_t t = rotr(c ^ d, 31);
    c ^= rotr(d,17);
    d ^= rotl(t,16);
    return {c,d};
}

} // namespace neoalz

