#include "neoalzette/neoalzette_core.hpp"

namespace neoalz {

// ============================================================================
// Cross-branch injection (value domain with constants)
// ============================================================================

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::cd_from_B(std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1) noexcept {
    std::uint32_t c = l2_forward(B ^ rc0);
    std::uint32_t d = l1_forward(rotr(B, 3) ^ rc1);
    std::uint32_t t = rotl(c ^ d, 31);
    c ^= rotl(d, 17);
    d ^= rotr(t, 16);
    return {c, d};
}

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::cd_from_A(std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1) noexcept {
    std::uint32_t c = l1_forward(A ^ rc0);
    std::uint32_t d = l2_forward(rotl(A, 24) ^ rc1);
    std::uint32_t t = rotr(c ^ d, 31);
    c ^= rotr(d, 17);
    d ^= rotl(t, 16);
    return {c, d};
}

// ============================================================================
// Cross-branch injection (delta domain, constants vanish)
// ============================================================================

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::cd_from_B_delta(std::uint32_t B_delta) noexcept {
    std::uint32_t c = l2_forward(B_delta);
    std::uint32_t d = l1_forward(rotr(B_delta, 3));
    std::uint32_t t = rotl(c ^ d, 31);
    c ^= rotl(d, 17);
    d ^= rotr(t, 16);
    return {c, d};
}

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::cd_from_A_delta(std::uint32_t A_delta) noexcept {
    std::uint32_t c = l1_forward(A_delta);
    std::uint32_t d = l2_forward(rotl(A_delta, 24));
    std::uint32_t t = rotr(c ^ d, 31);
    c ^= rotr(d, 17);
    d ^= rotl(t, 16);
    return {c, d};
}

// ============================================================================
// Main ARX-box transformations
// ============================================================================

void NeoAlzetteCore::forward(std::uint32_t& a, std::uint32_t& b) noexcept {
    const auto& R = ROUND_CONSTANTS;
    std::uint32_t A = a, B = b;

    // First subround
    B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0]);
    A -= R[1];
    A ^= rotl(B, 24);
    B ^= rotl(A, 16);
    A = l1_forward(A);
    B = l2_forward(B);
    {
        auto [C0, D0] = cd_from_B(B, R[2], R[3]);
        A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ R[4]);
    }

    // Second subround
    A += (rotl(B, 31) ^ rotl(B, 17) ^ R[5]);
    B -= R[6];
    B ^= rotl(A, 24);
    A ^= rotl(B, 16);
    B = l1_forward(B);
    A = l2_forward(A);
    {
        auto [C1, D1] = cd_from_A(A, R[7], R[8]);
        B ^= (rotl(C1, 24) ^ rotl(D1, 16) ^ R[9]);
    }

    // Final constant addition
    A ^= R[10];
    B ^= R[11];
    a = A; 
    b = B;
}

void NeoAlzetteCore::backward(std::uint32_t& a, std::uint32_t& b) noexcept {
    const auto& R = ROUND_CONSTANTS;
    std::uint32_t A = a, B = b;
    
    // Reverse final constant addition
    B ^= R[11]; 
    A ^= R[10];
    
    // Reverse second subround
    {
        auto [C1, D1] = cd_from_A(A, R[7], R[8]);
        B ^= (rotl(C1, 24) ^ rotl(D1, 16) ^ R[9]);
    }
    B = l1_backward(B);
    A = l2_backward(A);
    A ^= rotl(B, 16);
    B ^= rotl(A, 24);
    B += R[6];
    A -= (rotl(B, 31) ^ rotl(B, 17) ^ R[5]);

    // Reverse first subround
    {
        auto [C0, D0] = cd_from_B(B, R[2], R[3]);
        A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ R[4]);
    }
    A = l1_backward(A);
    B = l2_backward(B);
    B ^= rotl(A, 16);
    A ^= rotl(B, 24);
    A += R[1];
    B -= (rotl(A, 31) ^ rotl(A, 17) ^ R[0]);
    
    a = A; 
    b = B;
}

// ============================================================================
// Convenience methods
// ============================================================================

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::encrypt(std::uint32_t a, std::uint32_t b) noexcept {
    forward(a, b);
    return {a, b};
}

std::pair<std::uint32_t, std::uint32_t>
NeoAlzetteCore::decrypt(std::uint32_t a, std::uint32_t b) noexcept {
    backward(a, b);
    return {a, b};
}

} // namespace neoalz