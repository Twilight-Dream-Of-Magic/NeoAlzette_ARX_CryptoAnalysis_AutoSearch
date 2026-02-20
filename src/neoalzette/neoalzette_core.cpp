#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream
{
	// ========================================================================
	// Linear diffusion layers
	// ========================================================================

	/*
	
	find_linear_box.exe --bits 32 --efficient-implementation --quality-threshold-branch-number 12 --max-xor 6 --seed 4 --need-found-result 2 --no-progress
	M(rotl)_hex = 0xd05a0889  M(rotl)^{-1}_hex = 0x5fc08ef4
	M(rotr)_hex = 0x2220b417  M(rotr)^{-1}_hex = 0x5ee207f4
	minimum weight found (pair) = 12
	rotl: diff=12 lin=12 combined=12
	rotr: diff=12 lin=12 combined=12
	Operations(rotl): start_bit=0 steps=6
	v0 = (1 << 0)  [0x00000001]
	v1 = v0 ^ rotl(v0,2)  [0x00000005]
	v2 = v0 ^ rotl(v1,17)  [0x000a0001]
	v3 = v0 ^ rotl(v2,4)  [0x00a00011]
	v4 = v3 ^ rotl(v3,24)  [0x11a0a011]
	v5 = v2 ^ rotl(v4,7)  [0xd05a0889]
	Operations(rotr): start_bit=0 steps=6
	v0 = (1 << 0)  [0x00000001]
	v1 = v0 ^ rotr(v0,2)  [0x40000001]
	v2 = v0 ^ rotr(v1,17)  [0x0000a001]
	v3 = v0 ^ rotr(v2,4)  [0x10000a01]
	v4 = v3 ^ rotr(v3,24)  [0x100a0b11]
	v5 = v2 ^ rotr(v4,7)  [0x2220b417]

	M(rotl)_hex = 0x29082a87  M(rotl)^{-1}_hex = 0x7868ab73
	M(rotr)_hex = 0xc2a82129  M(rotr)^{-1}_hex = 0x9daa2c3d
	minimum weight found (pair) = 12
	rotl: diff=12 lin=12 combined=12
	rotr: diff=12 lin=12 combined=12
	Operations(rotl): start_bit=0 steps=6
	v0 = (1 << 0)  [0x00000001]
	v1 = v0 ^ rotl(v0,2)  [0x00000005]
	v2 = v1 ^ rotl(v0,24)  [0x01000005]
	v3 = v2 ^ rotl(v1,4)  [0x01000055]
	v4 = v2 ^ rotl(v3,27)  [0xa9080007]
	v5 = v4 ^ rotl(v3,7)  [0x29082a87]
	Operations(rotr): start_bit=0 steps=6
	v0 = (1 << 0)  [0x00000001]
	v1 = v0 ^ rotr(v0,2)  [0x40000001]
	v2 = v1 ^ rotr(v0,24)  [0x40000101]
	v3 = v2 ^ rotr(v1,4)  [0x54000101]
	v4 = v2 ^ rotr(v3,27)  [0xc000212b]
	v5 = v4 ^ rotr(v3,7)  [0xc2a82129]

	DONE.
	Tested candidates = 5736
	Accepted candidates (printed) = 2
	Accepted candidates (total)   = 2
	Elapsed seconds = 1.35393
	Random ISD iterations executed (Prange screen) = 8896
	Random ISD iterations executed (Quality gate)  = 32768
	Random ISD iterations executed (Final confirm) = 16384
	quality_threshold_branch_number(B) = 12
	Quality gate = ON  (quality_trials=4096 exhaustive_input_weight_max=2 full_unit_scan=yes)

	--- Best candidate (post-search confirmation) ---
	M(rotl)_hex = 0x29082a87  M(rotl)^{-1}_hex = 0x7868ab73
	M(rotr)_hex = 0xc2a82129  M(rotr)^{-1}_hex = 0x9daa2c3d
	minimum weight found = 12


	--- Search-phase upper bounds (before final confirmation) ---
	Best(rotl) differential branch upper bound = 12
	Best(rotl) linear branch upper bound       = 12
	Best(rotl) combined branch upper bound     = 12
	Best(rotr) differential branch upper bound = 12
	Best(rotr) linear branch upper bound       = 12
	Best(rotr) combined branch upper bound     = 12
	Best(pair) combined branch upper bound     = 12

	--- Post-search confirmed upper bounds ---
	Confirmed(rotl) differential upper bound   = 12
	Confirmed(rotl) linear upper bound         = 12
	Confirmed(rotl) combined upper bound       = 12
	Confirmed(rotr) differential upper bound   = 12
	Confirmed(rotr) linear upper bound         = 12
	Confirmed(rotr) combined upper bound       = 12
	Confirmed(pair) combined upper bound       = 12

	Threshold check (confirmed combined >= 12) = PASS

	Quality: ACCEPTED (safe to forward to heuristic decomposer)
	Next step: run linear_box_heuristic_decomposer --verify <hex> for strict validation (do this for BOTH matrices).

	*/
	std::uint32_t generate_dynamic_diffusion_mask0( std::uint32_t X ) noexcept
	{
		std::uint32_t Y = 0;
		std::uint32_t v0 = X;

		std::uint32_t v1 = v0 ^ NeoAlzetteCore::rotl(v0,2);
		std::uint32_t v2 = v0 ^ NeoAlzetteCore::rotl(v1,17);
		std::uint32_t v3 = v0 ^ NeoAlzetteCore::rotl(v2,4);
		std::uint32_t v4 = v3 ^ NeoAlzetteCore::rotl(v3,24);
		Y = v2 ^ NeoAlzetteCore::rotl(v4,7);

		return Y;
	}

	std::uint32_t generate_dynamic_diffusion_mask1( std::uint32_t X ) noexcept
	{
		std::uint32_t Y = 0;
		std::uint32_t v0 = X;

		std::uint32_t v1 = v0 ^ NeoAlzetteCore::rotr(v0,2);
		std::uint32_t v2 = v0 ^ NeoAlzetteCore::rotr(v1,17);
		std::uint32_t v3 = v0 ^ NeoAlzetteCore::rotr(v2,4);
		std::uint32_t v4 = v3 ^ NeoAlzetteCore::rotr(v3,24);
		Y = v2 ^ NeoAlzetteCore::rotr(v4,7);

		return Y;
	}

	// ============================================================================
	// Cross-branch injection (value domain with constants)
	// ============================================================================

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_B( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		const std::uint32_t mask0 = generate_dynamic_diffusion_mask0( B );
		std::uint32_t s_box_in_B = ( B ^ RC[ 2 ] ) ^ ( ~( B & mask0 ) );

		// Performance: drop L1/L2 here; reuse the dynamic diffusion mask as the linear pre-mix.
		std::uint32_t c = B;
		std::uint32_t d = mask0 ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_B;
		d ^= NeoAlzetteCore::rotr( t, 16 ) ^ rc1;
		return { c, d };
	}

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_A( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		const std::uint32_t mask1 = generate_dynamic_diffusion_mask1( A );
		std::uint32_t s_box_in_A = ( A ^ RC[ 7 ] ) ^ ( ~( A | mask1 ) );

		// Performance: drop L1/L2 here; reuse the dynamic diffusion mask as the linear pre-mix.
		std::uint32_t c = A;
		std::uint32_t d = mask1 ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_A;
		d ^= NeoAlzetteCore::rotl( t, 16 ) ^ rc1;
		return { c, d };
	}

	// ============================================================================
	// Main ARX-box transformations
	// ============================================================================

	void NeoAlzetteCore::forward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// First subround
		B += ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );
		A -= RC[ 1 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
		}

		// Second subround
		A += ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );
		B -= RC[ 6 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
		}

		// Final constant addition
		A ^= RC[ 10 ];
		B ^= RC[ 11 ];
		a = A;
		b = B;
	}

	void NeoAlzetteCore::backward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// Reverse final constant addition
		B ^= RC[ 11 ];
		A ^= RC[ 10 ];

		// Reverse second subround
		{
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
		}
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B += RC[ 6 ];
		A -= ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );

		// Reverse first subround
		{
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
		}
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A += RC[ 1 ];
		B -= ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );

		a = A;
		b = B;
	}

	// ============================================================================
	// Convenience methods
	// ============================================================================

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::encrypt( std::uint32_t a, std::uint32_t b ) noexcept
	{
		forward( a, b );
		return { a, b };
	}

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::decrypt( std::uint32_t a, std::uint32_t b ) noexcept
	{
		backward( a, b );
		return { a, b };
	}

}  // namespace TwilightDream