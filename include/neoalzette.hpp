#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace neoalz {

constexpr uint32_t rotl(uint32_t x, int r) noexcept {
    r &= 31;
    return (x << r) | (x >> (32 - r));
}
constexpr uint32_t rotr(uint32_t x, int r) noexcept {
    r &= 31;
    return (x >> r) | (x << (32 - r));
}

struct NeoAlzetteBox {
    static constexpr std::array<std::uint32_t, 16> ROUND_CONSTANT
    { 
        0x16B2C40B, 0xC117176A, 0x0F9A2598, 0xA1563ACA,
        0x243F6A88, 0x85A308D3, 0x13198102, 0xE0370734,
        0x9E3779B9, 0x7F4A7C15, 0xF39CC060, 0x5CEDC834,
        0xB7E15162, 0x8AED2A6A, 0xBF715880, 0x9CF4F3C7
    };

    static constexpr std::uint32_t l1_forward(std::uint32_t in) noexcept {
        return in ^ rotl(in,2) ^ rotl(in,10) ^ rotl(in,18) ^ rotl(in,24);
    }
    static constexpr std::uint32_t l1_backward(std::uint32_t out) noexcept {
        return out ^ rotr(out,2) ^ rotr(out,8) ^ rotr(out,10) ^ rotr(out,14)
                   ^ rotr(out,16)^ rotr(out,18)^ rotr(out,20)^ rotr(out,24)
                   ^ rotr(out,28)^ rotr(out,30);
    }
    static constexpr std::uint32_t l2_forward(std::uint32_t in) noexcept {
        return in ^ rotl(in,8) ^ rotl(in,14) ^ rotl(in,22) ^ rotl(in,30);
    }
    static constexpr std::uint32_t l2_backward(std::uint32_t out) noexcept {
        return out ^ rotr(out,2) ^ rotr(out,4) ^ rotr(out,8) ^ rotr(out,12)
                   ^ rotr(out,14)^ rotr(out,16)^ rotr(out,18)^ rotr(out,22)
                   ^ rotr(out,24)^ rotr(out,30);
    }

    static inline std::pair<std::uint32_t, std::uint32_t>
    cd_from_B( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept {
        std::uint32_t c = l2_forward( B ^ rc0 );
        std::uint32_t d = l1_forward( rotr( B, 3 ) ^ rc1 );
        std::uint32_t t = rotl( c ^ d, 31 );
        c ^= rotl( d, 17 );
        d ^= rotr( t, 16 );
        return { c, d };
    }

    static inline std::pair<std::uint32_t, std::uint32_t>
    cd_from_A( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept {
        std::uint32_t c = l1_forward( A ^ rc0 );
        std::uint32_t d = l2_forward( rotl( A, 24 ) ^ rc1 );
        std::uint32_t t = rotr( c ^ d, 31 );
        c ^= rotr( d, 17 );
        d ^= rotl( t, 16 );
        return { c, d };
    }

    static inline void forward(std::uint32_t& a, std::uint32_t& b) noexcept {
        const auto& R = ROUND_CONSTANT;
        std::uint32_t A = a, B = b;

        B += ( rotl( A, 31 ) ^ rotl( A, 17 ) ^ R[ 0 ] );
        A -= R[ 1 ];
        A ^= rotl( B, 24 );
        B ^= rotl( A, 16 );
        A = l1_forward( A );
        B = l2_forward( B );
        {
            auto [ C0, D0 ] = cd_from_B( B, R[ 2 ], R[ 3 ] );
            A ^= ( rotl( C0, 24 ) ^ rotl( D0, 16 ) ^ R[ 4 ] );
        }

        A += ( rotl( B, 31 ) ^ rotl( B, 17 ) ^ R[ 5 ] );
        B -= R[ 6 ];
        B ^= rotl( A, 24 );
        A ^= rotl( B, 16 );
        B = l1_forward( B );
        A = l2_forward( A );
        {
            auto [ C1, D1 ] = cd_from_A( A, R[ 7 ], R[ 8 ] );
            B ^= ( rotl( C1, 24 ) ^ rotl( D1, 16 ) ^ R[ 9 ] );
        }

        A ^= R[ 10 ];
        B ^= R[ 11 ];
        a = A; b = B;
    }

    static inline void backward(std::uint32_t& a, std::uint32_t& b) noexcept {
        const auto& R = ROUND_CONSTANT;
        std::uint32_t A = a, B = b;
        B ^= R[ 11 ]; A ^= R[ 10 ];
        {
            auto [ C1, D1 ] = cd_from_A( A, R[ 7 ], R[ 8 ] );
            B ^= ( rotl( C1, 24 ) ^ rotl( D1, 16 ) ^ R[ 9 ] );
        }
        B = l1_backward( B );
        A = l2_backward( A );
        A ^= rotl( B, 16 );
        B ^= rotl( A, 24 );
        B += R[ 6 ];
        A -= ( rotl( B, 31 ) ^ rotl( B, 17 ) ^ R[ 5 ] );

        {
            auto [ C0, D0 ] = cd_from_B( B, R[ 2 ], R[ 3 ] );
            A ^= ( rotl( C0, 24 ) ^ rotl( D0, 16 ) ^ R[ 4 ] );
        }
        A = l1_backward( A );
        B = l2_backward( B );
        B ^= rotl( A, 16 );
        A ^= rotl( B, 24 );
        A += R[ 1 ];
        B -= ( rotl( A, 31 ) ^ rotl( A, 17 ) ^ R[ 0 ] );
        a = A; b = B;
    }
};

} // namespace neoalz
