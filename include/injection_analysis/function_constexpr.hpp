#pragma once
#include <array>
#include <cstdint>
#include <utility>

#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream::analysis_constexpr
{
	// These constexpr helpers define the exact injected XOR maps analyzed by
	// `differential_rank.hpp` and `linear_rank.hpp`.
	//
	// The mathematical objects are the two 32 → 32 maps
	//   F_B(B) = injected_xor_term_from_branch_b(B),
	//   F_A(A) = injected_xor_term_from_branch_a(A),
	// where the argument `B` / `A` is now the branch word AFTER the explicit pre-injection
	// XOR-by-constant step has already been applied by the caller / BnB stage machine.
	//
	// Exact current-core split semantics:
	//   B_pre = B_raw ⊕ RC[4]          // explicit deterministic BnB bridge
	//   (C_B(B_pre), D_B(B_pre)) = cd_injection_from_B_constexpr(B_pre)
	//   F_B(B_pre) = ROTL24((C_B(B_pre) ≪ 2) ⊕ (D_B(B_pre) ≫ 2))
	//              ⊕ ROTL16(D_B(B_pre))
	//              ⊕ ROTL8((C_B(B_pre) ≫ 5) ⊕ (D_B(B_pre) ≪ 5))
	//
	//   A_pre = A_raw ⊕ RC[9]          // explicit deterministic BnB bridge
	//   (C_A(A_pre), D_A(A_pre)) = cd_injection_from_A_constexpr(A_pre)
	//   F_A(A_pre) = ROTR24((C_A(A_pre) ≫ 3) ⊕ (D_A(A_pre) ≪ 3))
	//              ⊕ ROTR16(D_A(A_pre))
	//              ⊕ ROTR8((C_A(A_pre) ≪ 1) ⊕ (D_A(A_pre) ≫ 1))
	//
	// The comments below intentionally use these exact objects so the analysis text matches the implementation.

	constexpr std::uint32_t branch_bridge( std::uint32_t x ) noexcept
	{
		return x ^ NeoAlzetteCore::rotl( NeoAlzetteCore::rotl( x, NeoAlzetteCore::CROSS_XOR_ROT_R0 ), NeoAlzetteCore::CROSS_XOR_ROT_R1 );
	}

	static constexpr std::uint32_t RC7_R24 = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 7 ], 24 );
	static constexpr std::uint32_t RC8_R24 = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 8 ], 24 );
	static constexpr std::uint32_t RC13_R24 = NeoAlzetteCore::rotr( NeoAlzetteCore::ROUND_CONSTANTS[ 13 ], 24 );
	static constexpr std::uint32_t RC2_L8 = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 2 ], 8 );
	static constexpr std::uint32_t RC3_L8 = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 3 ], 8 );
	static constexpr std::uint32_t RC12_L8 = NeoAlzetteCore::rotl( NeoAlzetteCore::ROUND_CONSTANTS[ 12 ], 8 );

	static constexpr std::uint32_t MASK0_RC7 = NeoAlzetteCore::generate_dynamic_diffusion_mask0( NeoAlzetteCore::ROUND_CONSTANTS[ 7 ] );
	static constexpr std::uint32_t MASK1_RC2 = NeoAlzetteCore::generate_dynamic_diffusion_mask1( NeoAlzetteCore::ROUND_CONSTANTS[ 2 ] );

	// Core value-domain injection kernel C_B,D_B evaluated at B_pre = B ⊕ RC[4].
	// In other words, the argument here is already the pre-whitened word entering the B → A injection gadget.
	constexpr std::pair<std::uint32_t, std::uint32_t> cd_injection_from_B_constexpr( std::uint32_t B ) noexcept
	{
		const std::uint32_t companion0 = NeoAlzetteCore::rotr<std::uint32_t>( B, 24 );
		const std::uint32_t mask = NeoAlzetteCore::generate_dynamic_diffusion_mask0( B );
		const std::uint32_t companion_mask = NeoAlzetteCore::rotr<std::uint32_t>( mask, 24 ) ^ MASK0_RC7;
		const std::uint32_t mask_r1 = NeoAlzetteCore::rotr<std::uint32_t>( mask, 5 );
		const std::uint32_t x0 = companion0 ^ mask;
		const std::uint32_t x1 = B ^ mask;
		const std::uint32_t view = companion0 ^ companion_mask;
		const std::uint32_t bridge_state = branch_bridge( B );
		const std::uint32_t q_state_na = RC7_R24 ^ ( ~( B & mask ) );
		const std::uint32_t q_comp_no = companion0 ^ B ^ RC8_R24 ^ ( ~( companion0 | mask_r1 ) );
		const std::uint32_t q_bridge = bridge_state ^ B ^ RC13_R24 ^ ( ~( bridge_state & companion_mask ) );
		const std::uint32_t q_shared = q_state_na ^ q_comp_no;
		const std::uint32_t cross_q = ( B ^ mask_r1 ) & NeoAlzetteCore::rotr<std::uint32_t>( mask ^ companion_mask, 7 );
		const std::uint32_t anti_q = ( ( x1 >> 3 ) ^ ( view >> 5 ) ^ mask_r1 ) & ( q_comp_no ^ NeoAlzetteCore::rotr<std::uint32_t>( x0, 11 ) );
		const std::uint32_t c = q_shared ^ NeoAlzetteCore::rotr<std::uint32_t>( q_comp_no, 5 ) ^ NeoAlzetteCore::rotr<std::uint32_t>( q_comp_no, 11 ) ^ anti_q;
		const std::uint32_t d = q_shared ^ NeoAlzetteCore::rotr<std::uint32_t>( q_state_na, 5 ) ^ NeoAlzetteCore::rotr<std::uint32_t>( q_bridge, 13 ) ^ cross_q ^ anti_q;
		return { c, d };
	}

	// Core value-domain injection kernel C_A,D_A evaluated at A_pre = A ⊕ RC[9].
	// The argument here is already the pre-whitened word entering the A → B injection gadget.
	constexpr std::pair<std::uint32_t, std::uint32_t> cd_injection_from_A_constexpr( std::uint32_t A ) noexcept
	{
		const std::uint32_t companion0 = NeoAlzetteCore::rotl<std::uint32_t>( A, 8 );
		const std::uint32_t mask = NeoAlzetteCore::generate_dynamic_diffusion_mask1( A );
		const std::uint32_t companion_mask = NeoAlzetteCore::rotl<std::uint32_t>( mask, 8 ) ^ MASK1_RC2;
		const std::uint32_t mask_r1 = NeoAlzetteCore::rotr<std::uint32_t>( mask, 5 );
		const std::uint32_t x0 = companion0 ^ mask;
		const std::uint32_t x1 = A ^ mask;
		const std::uint32_t view = companion0 ^ companion_mask;
		const std::uint32_t bridge_state = branch_bridge( A );
		const std::uint32_t q_state_no = RC2_L8 ^ ( ~( A | mask ) );
		const std::uint32_t q_comp_na = companion0 ^ A ^ RC3_L8 ^ ( ~( companion0 & mask_r1 ) );
		const std::uint32_t q_bridge = bridge_state ^ A ^ RC12_L8 ^ ( ~( bridge_state | companion_mask ) );
		const std::uint32_t q_shared = q_state_no ^ q_comp_na;
		const std::uint32_t cross_q = ( A ^ mask_r1 ) & NeoAlzetteCore::rotl<std::uint32_t>( mask ^ companion_mask, 13 );
		const std::uint32_t anti_q = ( std::uint32_t( x1 << 3 ) ^ std::uint32_t( view << 5 ) ^ mask_r1 ) | ( q_comp_na ^ NeoAlzetteCore::rotl<std::uint32_t>( x0, 11 ) );
		const std::uint32_t c = q_shared ^ NeoAlzetteCore::rotl<std::uint32_t>( q_comp_na, 5 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( q_comp_na, 11 ) ^ anti_q;
		const std::uint32_t d = q_shared ^ NeoAlzetteCore::rotl<std::uint32_t>( q_state_no, 5 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( q_bridge, 13 ) ^ cross_q ^ anti_q;
		return { c, d };
	}

	// Exact injected XOR map F_B : {0,1}^32 → {0,1}^32 used by the B → A update.
	// Input  : branch-B word AFTER the explicit BnB pre-whitening step `B_pre = B_raw ⊕ RC[4]`.
	// Output : exact 32-bit XOR word injected into branch A.
	constexpr std::uint32_t injected_xor_term_from_branch_b( std::uint32_t B ) noexcept
	{
		const auto [ C, D ] = cd_injection_from_B_constexpr( B );
		return NeoAlzetteCore::rotl( (C << 2) ^ (D >> 2), 24 ) ^ NeoAlzetteCore::rotl( D, 16 ) ^ NeoAlzetteCore::rotl( (C >> 5) ^ (D << 5), 8 );
	}

	// Exact injected XOR map F_A : {0,1}^32 → {0,1}^32 used by the A → B update.
	// Input  : branch-A word AFTER the explicit BnB pre-whitening step `A_pre = A_raw ⊕ RC[9]`.
	// Output : exact 32-bit XOR word injected into branch B.
	constexpr std::uint32_t injected_xor_term_from_branch_a( std::uint32_t A ) noexcept
	{
		const auto [ C, D ] = cd_injection_from_A_constexpr( A );
		return NeoAlzetteCore::rotr( (C >> 3) ^ (D << 3), 24 ) ^ NeoAlzetteCore::rotr( D, 16 ) ^ NeoAlzetteCore::rotr( (C << 1) ^ (D >> 1), 8 );
	}

}  // namespace TwilightDream::analysis_constexpr
