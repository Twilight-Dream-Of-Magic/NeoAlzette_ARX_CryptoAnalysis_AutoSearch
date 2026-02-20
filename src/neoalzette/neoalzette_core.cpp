#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream
{
	// ========================================================================
	// Linear diffusion layers
	// https://github.com/Twilight-Dream-Of-Magic/linear-box-search
	// ========================================================================

	/*****
	
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

	*****/

	// ============================================================================
	// Cross-branch injection (value domain with constants)
	//
	// Design rationale:
	// - add a second nonlinearity source beyond the carry/borrow effects of the main ARX path;
	// - keep the injector lightweight;
	// - preserve reversibility at the round level via cross-branch XOR-style injection,
	//   so the local function itself does not need to be invertible.
	// ============================================================================

	constexpr std::uint32_t branch_bridge( std::uint32_t x ) noexcept
	{
		return x ^ NeoAlzetteCore::rotl( NeoAlzetteCore::rotl( x, NeoAlzetteCore::CROSS_XOR_ROT_R0 ), NeoAlzetteCore::CROSS_XOR_ROT_R1 );
	}

	// precompute once
	static constexpr std::uint32_t RC7_R24   = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 7 ], 24 );
	static constexpr std::uint32_t RC8_R24   = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 8 ], 24 );
	static constexpr std::uint32_t RC13_R24  = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 13 ], 24 );
	static constexpr std::uint32_t RC2_L8    = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 2 ], 8 );
	static constexpr std::uint32_t RC3_L8    = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 3 ], 8 );
	static constexpr std::uint32_t RC12_L8   = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 12 ], 8 );

	static constexpr std::uint32_t MASK0_RC7 = NeoAlzetteCore::generate_dynamic_diffusion_mask0( NeoAlzetteCore::ROUND_CONSTANTS[ 7 ] );
	static constexpr std::uint32_t MASK1_RC2 = NeoAlzetteCore::generate_dynamic_diffusion_mask1( NeoAlzetteCore::ROUND_CONSTANTS[ 2 ] );

	// Feistel-like nonlinear branch injection: B -> A
	// Local nonlinear mixing function from B into A (PRF-like role, not a formal PRF claim)
	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_B( std::uint32_t B ) noexcept
	{
		const std::uint32_t companion0 = rotr( B, 24 );

		const std::uint32_t mask =  generate_dynamic_diffusion_mask0( B );
		const std::uint32_t companion_mask =  rotr( mask, 24 ) ^ MASK0_RC7;
		const std::uint32_t mask_r1 =  rotr( mask, 5 );

		const std::uint32_t x0 = companion0 ^ mask;
		const std::uint32_t x1 = B ^ mask;
		const std::uint32_t view = companion0 ^ companion_mask;
		const std::uint32_t bridge_state = branch_bridge( B );

		const std::uint32_t q_state_na = RC7_R24 ^ ( ~( B & mask ) );
		const std::uint32_t q_comp_no  = companion0 ^ B ^ RC8_R24 ^ ( ~( companion0 | mask_r1 ) );
		const std::uint32_t q_bridge   = bridge_state ^ B ^ RC13_R24 ^ ( ~( bridge_state & companion_mask ) );
		const std::uint32_t q_shared   = q_state_na ^ q_comp_no;

		const std::uint32_t cross_q = ( B ^ mask_r1 ) & rotr( mask ^ companion_mask, 7 );
		const std::uint32_t anti_q  = ( ( x1 >> 3 ) ^ ( view >> 5 ) ^ mask_r1 ) & ( q_comp_no ^ rotr( x0, 11 ) );

		const std::uint32_t c = q_shared ^ rotr( q_comp_no, 5 ) ^ rotr( q_comp_no, 11 ) ^ anti_q;
		const std::uint32_t d = q_shared ^ rotr( q_state_na, 5 ) ^ rotr( q_bridge, 13 ) ^ cross_q ^ anti_q;
		return { c, d };
	}

	// Feistel-like nonlinear branch injection: A -> B
	// Local nonlinear mixing function from A into B (PRF-like role, not a formal PRF claim)
	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_A( std::uint32_t A ) noexcept
	{
		const std::uint32_t companion0 = rotl( A, 8 );

		const std::uint32_t mask = generate_dynamic_diffusion_mask1( A );
		const std::uint32_t companion_mask = rotl( mask, 8 ) ^ MASK1_RC2;
		const std::uint32_t mask_r1 = rotr( mask, 5 );

		const std::uint32_t x0 = companion0 ^ mask;
		const std::uint32_t x1 = A ^ mask;
		const std::uint32_t view = companion0 ^ companion_mask;
		const std::uint32_t bridge_state = branch_bridge( A );

		const std::uint32_t q_state_no = RC2_L8 ^ ( ~( A | mask ) );
		const std::uint32_t q_comp_na  = companion0 ^ A ^ RC3_L8 ^ ( ~( companion0 & mask_r1 ) );
		const std::uint32_t q_bridge   = bridge_state ^ A ^ RC12_L8 ^ ( ~( bridge_state | companion_mask ) );
		const std::uint32_t q_shared   = q_state_no ^ q_comp_na;

		const std::uint32_t cross_q = ( A ^ mask_r1 ) & rotl( mask ^ companion_mask, 13 );
		const std::uint32_t anti_q  = ( ( x1 << 3 ) ^ ( view << 5 ) ^ mask_r1 ) | ( q_comp_na ^ rotl( x0, 11 ) );

		const std::uint32_t c = q_shared ^ rotl( q_comp_na, 5 ) ^ rotl( q_comp_na, 11 ) ^ anti_q;
		const std::uint32_t d = q_shared ^ rotl( q_state_no, 5 ) ^ rotl( q_bridge, 13 ) ^ cross_q ^ anti_q;
		return { c, d };
	}

	// ============================================================================
	// Main ARX-box transformations
	//
	// Design rationale:
	// the nonlinear branch injector is placed in a cross-branch XOR-injection role,
	// so the local mixing function itself does not need to be invertible.
	// This avoids "self-locking" updates while preserving reversibility of the whole round structure.
	// ============================================================================

	void NeoAlzetteCore::forward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// First subround
		B += ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );
		
		// This is hardcore.
		// Constant addition/subtraction inside an ARX-style trail is still costly to model precisely.
		// Existing differential treatments are possible, but practical low-complexity and broadly reusable
		// linear/correlation-oriented models are still awkward for this kind of construction.
		A -= RC[ 1 ];

		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			B ^= RC[ 4 ];
			auto [ C0, D0 ] = cd_injection_from_B( B );
			A ^= NeoAlzetteCore::rotl( (C0 << 2) ^ (D0 >> 2), 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ NeoAlzetteCore::rotl( (C0 >> 5) ^ (D0 << 5), 8 );
		}

		// Second subround
		A += ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );
		
		// This is hardcore.
		// Constant addition/subtraction inside an ARX-style trail is still costly to model precisely.
		// Existing differential treatments are possible, but practical low-complexity and broadly reusable
		// linear/correlation-oriented models are still awkward for this kind of construction.
		B -= RC[ 6 ];
		
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			A ^= RC[ 9 ];
			auto [ C1, D1 ] = cd_injection_from_A( A );
			B ^= NeoAlzetteCore::rotr( (C1 >> 3) ^ (D1 << 3), 24 ) ^ NeoAlzetteCore::rotr( D1, 16 ) ^ NeoAlzetteCore::rotr( (C1 << 1) ^ (D1 >> 1), 8 );
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
			A ^= RC[ 9 ];
			auto [ C1, D1 ] = cd_injection_from_A( A );
			B ^= NeoAlzetteCore::rotr( (C1 >> 3) ^ (D1 << 3), 24 ) ^ NeoAlzetteCore::rotr( D1, 16 ) ^ NeoAlzetteCore::rotr( (C1 << 1) ^ (D1 >> 1), 8 );
		}
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B += RC[ 6 ];
		A -= ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );

		// Reverse first subround
		{
			B ^= RC[ 4 ];
			auto [ C0, D0 ] = cd_injection_from_B( B );
			A ^= NeoAlzetteCore::rotl( (C0 << 2) ^ (D0 >> 2), 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ NeoAlzetteCore::rotl( (C0 >> 5) ^ (D0 << 5), 8 );
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
