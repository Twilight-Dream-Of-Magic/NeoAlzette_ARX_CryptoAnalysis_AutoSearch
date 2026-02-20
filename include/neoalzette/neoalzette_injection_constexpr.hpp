#pragma once
#include <array>
#include <cstdint>
#include <utility>

#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream::neoalzette_constexpr
{
	// These helpers replicate the logic in `src/neoalzette/neoalzette_core.cpp` in a constexpr-friendly form.
	// We intentionally keep them out of `neoalzette_core.*` as requested, so analysis tools can precompute
	// constants at compile-time under C++20.

	constexpr std::pair<std::uint32_t, std::uint32_t>
	cd_injection_from_B_constexpr(std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1) noexcept {
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		// XOR with NOT-AND and NOT-OR is balance of boolean logic
		const std::uint32_t s_box_in_B = (B ^ RC[2]) ^ (~(B & generate_dynamic_diffusion_mask0(B)));

		std::uint32_t c = NeoAlzetteCore::l2_forward(B);
		std::uint32_t d = NeoAlzetteCore::l1_forward(B) ^ rc0;

		const std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_B;
		d ^= NeoAlzetteCore::rotr<std::uint32_t>(t, 16) ^ rc1;
		return {c, d};
	}

	constexpr std::pair<std::uint32_t, std::uint32_t>
	cd_injection_from_A_constexpr(std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1) noexcept {
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		// XOR with NOT-AND and NOT-OR is balance of boolean logic
		const std::uint32_t s_box_in_A = (A ^ RC[7]) ^ (~(A | generate_dynamic_diffusion_mask1(A)));

		std::uint32_t c = NeoAlzetteCore::l1_forward(A);
		std::uint32_t d = NeoAlzetteCore::l2_forward(A) ^ rc0;

		const std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_A;
		d ^= NeoAlzetteCore::rotl<std::uint32_t>(t, 16) ^ rc1;
		return {c, d};
	}

	// f_B(B) = rotl24(C(B)) ⊕ rotl16(D(B))
	constexpr std::uint32_t injected_xor_term_from_branch_b(std::uint32_t B) noexcept {
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		const auto [C, D] = cd_injection_from_B_constexpr(B, (RC[2] | RC[3]), RC[3]);
		return NeoAlzetteCore::rotl<std::uint32_t>(C, 24) ^ NeoAlzetteCore::rotl<std::uint32_t>(D, 16);
	}

	// f_A(A) = rotl24(C(A)) ⊕ rotl16(D(A))
	constexpr std::uint32_t injected_xor_term_from_branch_a(std::uint32_t A) noexcept {
		const auto& RC = NeoAlzetteCore::ROUND_CONSTANTS;
		const auto [C, D] = cd_injection_from_A_constexpr(A, (RC[7] & RC[8]), RC[8]);
		return NeoAlzetteCore::rotl<std::uint32_t>(C, 24) ^ NeoAlzetteCore::rotl<std::uint32_t>(D, 16);
	}

}  // namespace TwilightDream::neoalzette_constexpr
