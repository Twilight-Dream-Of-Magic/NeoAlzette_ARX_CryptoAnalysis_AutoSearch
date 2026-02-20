#pragma once
/**
 * @file linear_correlation/constant_optimal_beta.hpp
 * @brief Fixed-alpha public declarations for exact var-const Q2 linear correlation.
 *
 * All implementation lives in
 * `src/arx_analysis_operators/linear_correlation/constant_fixed_alpha_core.cpp`.
 */

#include "constant_optimal_alpha.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		// Canonical Fixed-alpha operator entry.
		// For n=64 this dispatches to the accepted Round-372 frontier
		// `v116 tailtheorempath24_uclass4_root256_final512`; for smaller widths it
		// preserves exact semantics via the exact transition reference path.
		[[nodiscard]] VarConstQ2MainlineResult solve_fixed_alpha_q2_canonical(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineStats* stats = nullptr ) noexcept;

		[[nodiscard]] VarConstQ2MainlineResult solve_fixed_alpha_q2_exact_transition_reference(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineStats* stats = nullptr ) noexcept;

		[[nodiscard]] VarConstQ2MainlineResult solve_fixed_alpha_q2_raw_reference(
			const VarConstQ2MainlineRequest& request ) noexcept;

		[[nodiscard]] VarConstOptimalOutputMaskResult find_optimal_beta_varconst_mod_sub(
			std::uint64_t alpha,
			std::uint64_t sub_constant_C,
			int n ) noexcept;

		[[nodiscard]] VarConstOptimalOutputMaskResult find_optimal_beta_varconst_mod_add(
			std::uint64_t alpha,
			std::uint64_t add_constant_K,
			int n ) noexcept;
	}  // namespace arx_operators
}  // namespace TwilightDream
