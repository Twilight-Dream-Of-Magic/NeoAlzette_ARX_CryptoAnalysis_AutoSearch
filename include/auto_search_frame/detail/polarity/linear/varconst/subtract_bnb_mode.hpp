#pragma once

#include <cstdint>

namespace TwilightDream::auto_search_linear
{
	// =============================================================================
	// Var-const subtraction (`y = x ⊟ C`): four Q2 outer-search polarities
	// =============================================================================
	//
	// Search-frame Q2 polarities:
	// - fixed output mask beta on y -> choose / enumerate input mask alpha on x
	// - fixed input mask alpha on x -> choose / enumerate output mask beta on y
	//
	// 1) FixedOutputMaskBeta_EnumerateInputAlpha
	//    - existing exact search-frame path
	//    - enumerates all alpha candidates within a weight cap
	//    - used by strict best-search / strict hull today
	//
	// 2) FixedOutputMaskBeta_ColumnOptimalInputAlpha
	//    - one-shot Gap B Q2 root operator:
	//         fixed (beta, constant) -> exact alpha*
	//    - collapses the fixed-beta shell to the numerically-smallest exact local argmax
	//    - useful as a fast Q2-collapsed outer-search / subspace policy
	//    - NOT a full trail-space reduction theorem: see
	//      `research_gap_b_two_stage_subconst_reduction_audit.cpp` for a concrete
	//      two-stage counterexample where local alpha* loses the global chained optimum
	//
	// 3) FixedInputMaskAlpha_EnumerateOutputBeta
	//    - first-class fixed-alpha polarity / API surface
	//    - enumerates beta candidates for a fixed alpha
	//    - accepted research line is pinned to
	//      `v116 tailtheorempath24_uclass4_root256_final512`
	//
	// 4) FixedInputMaskAlpha_ColumnOptimalOutputBeta
	//    - one-shot fixed-alpha Gap B Q2 root operator:
	//         fixed (alpha, constant) -> exact beta*
	//    - canonical fixed-alpha column-optimal route used where stage semantics permit it
	//    - accepted research line is pinned to
	//      `v116 tailtheorempath24_uclass4_root256_final512`
	//
	enum class LinearVarConstSubBnbMode : std::uint8_t
	{
		FixedOutputMaskBeta_EnumerateInputAlpha = 0,
		FixedOutputMaskBeta_ColumnOptimalInputAlpha = 1,
		FixedInputMaskAlpha_EnumerateOutputBeta = 2,
		FixedInputMaskAlpha_ColumnOptimalOutputBeta = 3,
	};

	static inline const char* linear_varconst_sub_bnb_mode_to_string( LinearVarConstSubBnbMode mode ) noexcept
	{
		switch ( mode )
		{
		case LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha:
			return "fixed_beta_enumerate_alpha";
		case LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha:
			return "fixed_beta_column_optimal_alpha";
		case LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta:
			return "fixed_alpha_enumerate_beta";
		case LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta:
			return "fixed_alpha_column_optimal_beta";
		default:
			return "unknown_varconst_sub_bnb_mode";
		}
	}

	// NOTE:
	// - The fixed-alpha Q2 operator core is implemented and self-tested.
	// - In the current reverse-stage topology, fixed-beta remains the natural source of
	//   sub-const lower bounds and alpha candidates. Fixed-alpha modes are consumed once
	//   alpha becomes concrete on a branch (or both sides are fixed), so runtime semantics
	//   no longer silently fall back to fixed-beta.
	static inline bool linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_search(
		LinearVarConstSubBnbMode mode ) noexcept
	{
		return mode == LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha ||
			mode == LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha ||
			mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta ||
			mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
	}

	static inline bool linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_best_engine(
		LinearVarConstSubBnbMode mode ) noexcept
	{
		return mode == LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha ||
			mode == LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha ||
			mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta ||
			mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
	}
}
