#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace TwilightDream {

/**
 * NeoAlzette Core - ARX-box implementation with linear layers
 * 
 * This class encapsulates the complete NeoAlzette ARX-box functionality,
 * including basic rotation operations, linear diffusion layers,
 * cross-branch injection, and forward/backward transformations.
 */
class NeoAlzetteCore {
public:
    // Round constants for NeoAlzette
    static constexpr std::array<std::uint32_t, 16> ROUND_CONSTANTS = {
        0x16B2C40B, 0xC117176A, 0x0F9A2598, 0xA1563ACA,
        0x243F6A88, 0x85A308D3, 0x13198102, 0xE0370734,
        0x9E3779B9, 0x7F4A7C15, 0xF39CC060, 0x5CEDC834,
        0xB7E15162, 0x8AED2A6A, 0xBF715880, 0x9CF4F3C7
    };

    // ========================================================================
    // Basic rotation operations (inline templates for performance)
    // ========================================================================
    
    template<typename T>
    static constexpr T rotl(T x, int r) noexcept {
        r &= (sizeof(T) * 8 - 1);
        return (x << r) | (x >> (sizeof(T) * 8 - r));
    }
    
    template<typename T>
    static constexpr T rotr(T x, int r) noexcept {
        r &= (sizeof(T) * 8 - 1);
        return (x >> r) | (x << (sizeof(T) * 8 - r));
    }

    // ========================================================================
    // Linear diffusion layers
    // ========================================================================
    
    // L1 forward transformation
    static constexpr std::uint32_t l1_forward(std::uint32_t x) noexcept;
    
    // L1 backward transformation (inverse) - 用于解密
    static constexpr std::uint32_t l1_backward(std::uint32_t x) noexcept;
    
    // L1 transpose transformation - 用于线性密码分析的掩码传播
    static constexpr std::uint32_t l1_transpose(std::uint32_t x) noexcept;
    
    // L2 forward transformation
    static constexpr std::uint32_t l2_forward(std::uint32_t x) noexcept;
    
    // L2 backward transformation (inverse) - 用于解密
    static constexpr std::uint32_t l2_backward(std::uint32_t x) noexcept;
    
    // L2 transpose transformation - 用于线性密码分析的掩码传播
    static constexpr std::uint32_t l2_transpose(std::uint32_t x) noexcept;

    // ========================================================================
    // Cross-branch injection (value domain with constants)
    // ========================================================================
    
    // Cross-branch injection from B branch
    static std::pair<std::uint32_t, std::uint32_t>
    cd_injection_from_B(std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1) noexcept;
    
    // Cross-branch injection from A branch
    static std::pair<std::uint32_t, std::uint32_t>
    cd_injection_from_A(std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1) noexcept;

    // ========================================================================
    // Main ARX-box transformations
    // ========================================================================
    
    // Forward transformation (encryption direction)
    static void forward(std::uint32_t& a, std::uint32_t& b) noexcept;
    
    // Backward transformation (decryption direction)
    static void backward(std::uint32_t& a, std::uint32_t& b) noexcept;

    // ========================================================================
    // Convenience methods
    // ========================================================================
    
    // Apply forward transformation and return result
    static std::pair<std::uint32_t, std::uint32_t>
    encrypt(std::uint32_t a, std::uint32_t b) noexcept;
    
    // Apply backward transformation and return result
    static std::pair<std::uint32_t, std::uint32_t>
    decrypt(std::uint32_t a, std::uint32_t b) noexcept;

private:
    // Private constructor - this is a static utility class
    NeoAlzetteCore() = delete;
};

// ============================================================================
// Inline implementations for performance-critical template functions
// ============================================================================

constexpr std::uint32_t NeoAlzetteCore::l1_forward(std::uint32_t x) noexcept {
    return x ^ rotl(x, 2) ^ rotl(x, 10) ^ rotl(x, 18) ^ rotl(x, 24);
}

constexpr std::uint32_t NeoAlzetteCore::l1_backward(std::uint32_t x) noexcept {
    return x ^ rotr(x, 2) ^ rotr(x, 8) ^ rotr(x, 10) ^ rotr(x, 14)
             ^ rotr(x, 16) ^ rotr(x, 18) ^ rotr(x, 20) ^ rotr(x, 24)
             ^ rotr(x, 28) ^ rotr(x, 30);
}

constexpr std::uint32_t NeoAlzetteCore::l2_forward(std::uint32_t x) noexcept {
    return x ^ rotl(x, 8) ^ rotl(x, 14) ^ rotl(x, 22) ^ rotl(x, 30);
}

constexpr std::uint32_t NeoAlzetteCore::l2_backward(std::uint32_t x) noexcept {
    return x ^ rotr(x, 2) ^ rotr(x, 4) ^ rotr(x, 8) ^ rotr(x, 12)
             ^ rotr(x, 14) ^ rotr(x, 16) ^ rotr(x, 18) ^ rotr(x, 22)
             ^ rotr(x, 24) ^ rotr(x, 30);
}

// ============================================================================
// Transpose transformations for linear cryptanalysis
// ============================================================================

constexpr std::uint32_t NeoAlzetteCore::l1_transpose(std::uint32_t x) noexcept {
    // 转置：把所有 rotl 改成 rotr
    // L1(x) = x ^ rotl(x, 2) ^ rotl(x, 10) ^ rotl(x, 18) ^ rotl(x, 24)
    // L1^T(x) = x ^ rotr(x, 2) ^ rotr(x, 10) ^ rotr(x, 18) ^ rotr(x, 24)
    return x ^ rotr(x, 2) ^ rotr(x, 10) ^ rotr(x, 18) ^ rotr(x, 24);
}

constexpr std::uint32_t NeoAlzetteCore::l2_transpose(std::uint32_t x) noexcept {
    // 转置：把所有 rotl 改成 rotr
    // L2(x) = x ^ rotl(x, 8) ^ rotl(x, 14) ^ rotl(x, 22) ^ rotl(x, 30)
    // L2^T(x) = x ^ rotr(x, 8) ^ rotr(x, 14) ^ rotr(x, 22) ^ rotr(x, 30)
    return x ^ rotr(x, 8) ^ rotr(x, 14) ^ rotr(x, 22) ^ rotr(x, 30);
}

} // namespace TwilightDream