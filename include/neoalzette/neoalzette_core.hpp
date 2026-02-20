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
		// NeoAlzette Cross-branch XOR/ROT bridge constants (between add/sub and injection)
		//
		// This two-line bridge is:
		//   A ^= rotl(B, R0);
		//   B ^= rotl(A, R1);
		//
		// In the current V6 instantiation we use the ordered constants:
		//   R0 = 22, R1 = 13
		//
		// IMPORTANT: keep these constants synchronized across all code paths:
		//   - src/neoalzette/neoalzette_core.cpp
		//       Real cipher implementation
		//   - include/auto_search_frame/detail/differential_best_search_math.hpp
		//       Differential search model declarations
		//   - src/auto_search_frame/differential_best_search_math.cpp
		//       Differential search model implementation
		//   - test_neoalzette_arx_trace.cpp
		//       Trace / instrumentation tool
		//
		// If any of the above files use different values, the implementation,
		// differential model, and trace outputs will silently diverge. That kind of
		// mismatch is poison: experiments may look "interesting" while actually
		// measuring inconsistent objects.
		//
		// ------------------------------------------------------------------------
		// Structural security rationale
		// ------------------------------------------------------------------------
		// These constants were NOT chosen as arbitrary decoration.
		//
		// Earlier experiments with looser rotation choices showed an undesirable
		// pattern in automated ARX cryptanalysis on 32-bit words: some rotation
		// settings created alignment effects that made the injection layer easier
		// to bypass. In such cases, the best trails often kept only a very small
		// number of active modular additions.
		//
		// Since modular addition is the main nonlinear source in this ARX setting,
		// "too few active additions" is treated as a structural warning sign rather
		// than a harmless implementation quirk.
		//
		// For the bridge
		//   A ^= rotl(B, R0);
		//   B ^= rotl(A, R1);
		//
		// the XOR-difference entering the injection side can contain a term of the form
		//   dB' = dB ^ rotl(dB, R0 + R1) ^ rotl(dA, R1)
		//
		// Therefore, the quantity (R0 + R1) mod 32 matters structurally.
		// If gcd((R0 + R1) mod 32, 32) is large, then large periodic rotation
		// subspaces may exist in which
		//   dB ^ rotl(dB, R0 + R1) == 0
		// holds too easily.
		//
		// In plain language: some repeating bit-pattern classes can partially
		// "cancel through the bridge", making the bridge too transparent for
		// single-branch XOR-difference propagation. This can yield weight-0 style
		// bypass behavior at the injection interface.
		//
		// To suppress this obvious escape hatch, we require:
		//   gcd(((R0 + R1) mod 32), 32) == 1
		//
		// Because 32 = 2^5, this is equivalent to requiring:
		//   ((R0 + R1) mod 32) to be odd
		//
		// For the current constants:
		//   (22 + 13) mod 32 = 35 mod 32 = 3
		// and gcd(3, 32) = 1.
		//
		// ------------------------------------------------------------------------
		// Empirical screening rules used for the current pair
		// ------------------------------------------------------------------------
		// The underlying screened unordered pair is (13, 22), instantiated here as
		// the ordered bridge assignment (R0, R1) = (22, 13).
		//
		// The pair was selected by the following empirical design filters:
		//   1) gcd(((r1 + r2) mod 32), 32) == 1
		//      -> avoids bridge sums aligned with large divisors of the word size
		//
		//   2) Exclude 8, 16, 24
		//      -> quarter-turn / half-turn / three-quarter-turn positions
		//         on a 32-bit word
		//      -> these repeatedly showed fragile alignment behavior in automated
		//         searches, often making the injection layer easier to bypass
		//
		//   3) Prefer moderate circular distance:
		//         d(r1, r2) = min((r2 - r1) mod 32, (r1 - r2) mod 32)
		//      with
		//         5 <= d(r1, r2) <= 11
		//      -> avoids both overly local gaps and near-half-turn symmetry
		//
		//   4) Prefer one constant in [1, 15] and the other in [17, 31]
		//      -> reduces same-region clustering and discourages visually symmetric
		//         or structurally fragile layouts
		//
		// For the chosen pair (13, 22):
		//   d(13, 22) = 9
		//   (13 + 22) mod 32 = 3
		//
		// These rules are empirical engineering filters derived from automated
		// cryptanalytic experiments. They are NOT a proof of security. Their role is
		// to avoid rotation settings that repeatedly behaved badly in practice.
		//
		// Bottom line:
		// do not "clean up", "simplify", or "make symmetric" these constants unless
		// the full differential / linear / trace tooling is rerun and checked again.
		// ========================================================================
		static constexpr int CROSS_XOR_ROT_R0 = 22;	 
		static constexpr int CROSS_XOR_ROT_R1 = 13;
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

		static constexpr std::uint32_t generate_dynamic_diffusion_mask0( std::uint32_t x ) noexcept
		{
			const std::uint32_t v0 = x;
			const std::uint32_t v1 = v0 ^ rotl( v0, 2 );
			const std::uint32_t v2 = v0 ^ rotl( v1, 17 );
			const std::uint32_t v3 = v0 ^ rotl( v2, 4 );
			const std::uint32_t v4 = v3 ^ rotl( v3, 24 );
			return v2 ^ rotl( v4, 7 );
		}

		static constexpr std::uint32_t generate_dynamic_diffusion_mask1( std::uint32_t x ) noexcept
		{
			const std::uint32_t v0 = x;
			const std::uint32_t v1 = v0 ^ rotr( v0, 2 );
			const std::uint32_t v2 = v0 ^ rotr( v1, 17 );
			const std::uint32_t v3 = v0 ^ rotr( v2, 4 );
			const std::uint32_t v4 = v3 ^ rotr( v3, 24 );
			return v2 ^ rotr( v4, 7 );
		}

		// ========================================================================
		// Cross-branch injection (value domain with constants)
		// ========================================================================

		// Cross-branch injection from B branch
		static std::pair<std::uint32_t, std::uint32_t> cd_injection_from_B( std::uint32_t B ) noexcept;

		// Cross-branch injection from A branch
		static std::pair<std::uint32_t, std::uint32_t> cd_injection_from_A( std::uint32_t A ) noexcept;

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
