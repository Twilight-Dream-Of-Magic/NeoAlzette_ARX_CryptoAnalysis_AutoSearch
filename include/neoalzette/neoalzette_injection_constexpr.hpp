#pragma once
#include <array>
#include <cstdint>
#include <utility>

#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream::neoalzette_constexpr
{
	// These constexpr helpers reuse the formulas exposed by `NeoAlzetteCore` so analysis code can
	// precompute injected XOR terms at compile-time under C++20.

	constexpr std::pair<std::uint32_t, std::uint32_t> cd_injection_from_B_constexpr( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;

		const std::uint32_t B2 = NeoAlzetteCore::rotr<std::uint32_t>(B, 16);
		const std::uint32_t mask0 = NeoAlzetteCore::generate_dynamic_diffusion_mask0(B);

		const std::uint32_t s_box_in_B  = (B  ^ RC[2]) ^ (~(B  & mask0));
		const std::uint32_t s_box_in_B2 = (B2) ^ (~(B2 | mask0));

		const std::uint32_t x0 = B2 ^ rc0 ^ mask0;
		const std::uint32_t x1 = B  ^ rc1 ^ mask0;

		const std::uint32_t cross = B & NeoAlzetteCore::rotr<std::uint32_t>(x0, 13);
		const std::uint32_t anti_shift = ((x1 >> 3) ^ (x1 >> 1)) & (B ^ NeoAlzetteCore::rotr<std::uint32_t>(x0, 9));

		const std::uint32_t c = B  ^ s_box_in_B  ^ anti_shift;
		const std::uint32_t d = B2 ^ s_box_in_B2 ^ cross ^ anti_shift;
		return {c, d};
	}

	constexpr std::pair<std::uint32_t, std::uint32_t> cd_injection_from_A_constexpr( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		// XOR with NOT-AND and NOT-OR is balance of boolean logic
		const std::uint32_t A2 = NeoAlzetteCore::rotl<std::uint32_t>(A, 16);
		const std::uint32_t mask1 = NeoAlzetteCore::generate_dynamic_diffusion_mask1(A);

		const std::uint32_t s_box_in_A  = (A  ^ RC[7]) ^ (~(A  | mask1));
		const std::uint32_t s_box_in_A2 = (A2) ^ (~(A2 & mask1));

		const std::uint32_t x0 = A2 ^ rc0 ^ mask1;
		const std::uint32_t x1 = A  ^ rc1 ^ mask1;

		const std::uint32_t cross = A & NeoAlzetteCore::rotl<std::uint32_t>(x0, 19);
		const std::uint32_t anti_shift = ((x1 << 3) ^ (x1 << 1)) | (A ^ NeoAlzetteCore::rotl<std::uint32_t>(x0, 9));

		const std::uint32_t c = A  ^ s_box_in_A  ^ anti_shift;
		const std::uint32_t d = A2 ^ s_box_in_A2 ^ cross ^ anti_shift;
		return {c, d};
	}

	// Exact round-level XOR term injected by the B -> A update:
	//   rotl24(C(B)) ⊕ rotl16(D(B)) ⊕ rotl((C(B) >> 1) ⊕ (D(B) << 1), 8) ⊕ RC[4]
	constexpr std::uint32_t injected_xor_term_from_branch_b( std::uint32_t B ) noexcept
	{
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		const auto [ C, D ] = cd_injection_from_B_constexpr( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
		return NeoAlzetteCore::rotl<std::uint32_t>( C, 24 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( D, 16 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( (C >> 1) ^ (D << 1), 8 ) ^ RC[ 4 ];
	}

	// Exact round-level XOR term injected by the A -> B update:
	//   rotr24(C(A)) ⊕ rotr16(D(A)) ⊕ rotr((C(A) << 3) ⊕ (D(A) >> 3), 8) ⊕ RC[9]
	constexpr std::uint32_t injected_xor_term_from_branch_a( std::uint32_t A ) noexcept
	{
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		const auto [ C, D ] = cd_injection_from_A_constexpr( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
		return NeoAlzetteCore::rotr<std::uint32_t>( C, 24 ) ^ NeoAlzetteCore::rotr<std::uint32_t>( D, 16 ) ^ NeoAlzetteCore::rotr<std::uint32_t>( (C << 3) ^ (D >> 3), 8 ) ^ RC[ 9 ];
	}

}  // namespace TwilightDream::neoalzette_constexpr
