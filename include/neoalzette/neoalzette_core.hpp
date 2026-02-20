#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace TwilightDream
{

	/**
	 * NeoAlzette Core - ARX-box implementation with linear layers
	 * 
	 * This class encapsulates the complete NeoAlzette ARX-box functionality,
	 * including basic rotation operations, linear diffusion layers,
	 * cross-branch injection, and forward/backward transformations.
	 */
	class NeoAlzetteCore
	{
	public:
		// ==== NeoAlzette ARX-box constants / NeoAlzette ARX-box 常量 ====
		static constexpr std::array<std::uint32_t, 16> ROUND_CONSTANTS
		{ 
			//1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181 (Fibonacci numbers)
			//Concatenation of Fibonacci numbers : 123581321345589144233377610987159725844181
			//Hexadecimal : 16b2c40bc117176a0f9a2598a1563aca6d5
			0x16B2C40B, 0xC117176A, 0x0F9A2598, 0xA1563ACA,

			/*
					Mathematical Constants - Millions of Digits
					http://www.numberworld.org/constants.html
			*/

			//π Pi (3.243f6a8885a308d313198a2e0370734)
			0x243F6A88, 0x85A308D3, 0x13198102, 0xE0370734,
			//φ Golden ratio (1.9e3779b97f4a7c15f39cc0605cedc834)
			0x9E3779B9, 0x7F4A7C15, 0xF39CC060, 0x5CEDC834,
			//e Natural Constant (2.b7e151628aed2a6abf7158809cf4f3c7)
			0xB7E15162, 0x8AED2A6A, 0xBF715880, 0x9CF4F3C7
		};

		// ========================================================================
		// Cross-branch XOR/ROT mixing constants (between add/sub and injection)
		//
		// These rotations must be kept consistent across:
		//   - src/neoalzette/neoalzette_core.cpp (real cipher)
		//   - include/auto_search_frame/test_neoalzette_differential_best_search.hpp (differential model)
		//   - test_neoalzette_arx_trace.cpp (trace tool)
		//
		// Security note:
		// For the structure:
		//   A ^= rotl(B, R0);  B ^= rotl(A, R1)
		// the injection input branch difference can become:
		//   dB' = dB ⊕ rotl(dB, R0+R1) ⊕ rotl(dA, R1).
		// If (R0+R1) shares a large gcd with (n) 32 (e.g. 8), there exist large periodic subspaces
		// such that dB ⊕ rotl(dB, R0+R1) == 0, which can "bypass" single-branch injection
		// in XOR-difference trails at weight 0 for injection.
		// ========================================================================
		static constexpr int CROSS_XOR_ROT_R0 = 23;	 // was 24
		static constexpr int CROSS_XOR_ROT_R1 = 16;	 // unchanged
		static constexpr int CROSS_XOR_ROT_SUM = ( ( CROSS_XOR_ROT_R0 + CROSS_XOR_ROT_R1 ) & 31 );
		static_assert( ( CROSS_XOR_ROT_SUM & 1 ) == 1, "CROSS_XOR_ROT_R0 + CROSS_XOR_ROT_R1 must be odd (coprime with 32) to avoid large rotation fixed-point subspaces." );

		// ========================================================================
		// Basic rotation operations (inline templates for performance)
		// ========================================================================

		template <typename T>
		static constexpr T rotl( T x, int r ) noexcept
		{
			r &= ( sizeof( T ) * 8 - 1 );
			return ( x << r ) | ( x >> ( sizeof( T ) * 8 - r ) );
		}

		template <typename T>
		static constexpr T rotr( T x, int r ) noexcept
		{
			r &= ( sizeof( T ) * 8 - 1 );
			return ( x >> r ) | ( x << ( sizeof( T ) * 8 - r ) );
		}

		// ========================================================================
		// Cross-branch injection (value domain with constants)
		// ========================================================================

		// Cross-branch injection from B branch
		static std::pair<std::uint32_t, std::uint32_t> cd_injection_from_B( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept;

		// Cross-branch injection from A branch
		static std::pair<std::uint32_t, std::uint32_t> cd_injection_from_A( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept;

		// ========================================================================
		// Main ARX-box transformations
		// ========================================================================

		// Forward transformation (encryption direction)
		static void forward( std::uint32_t& a, std::uint32_t& b ) noexcept;

		// Backward transformation (decryption direction)
		static void backward( std::uint32_t& a, std::uint32_t& b ) noexcept;

		// ========================================================================
		// Convenience methods
		// ========================================================================

		// Apply forward transformation and return result
		static std::pair<std::uint32_t, std::uint32_t> encrypt( std::uint32_t a, std::uint32_t b ) noexcept;

		// Apply backward transformation and return result
		static std::pair<std::uint32_t, std::uint32_t> decrypt( std::uint32_t a, std::uint32_t b ) noexcept;

	private:
		// Private constructor - this is a static utility class
		NeoAlzetteCore() = delete;
	};

}  // namespace TwilightDream