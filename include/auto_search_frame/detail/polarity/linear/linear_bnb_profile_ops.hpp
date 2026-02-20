#pragma once

#include "auto_search_frame/detail/linear_best_search_types.hpp"

#include <limits>
#include <optional>

namespace TwilightDream::auto_search_linear
{
	using LinearSubConstCandidateWeightResolver = std::optional<SearchWeight> ( * )(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t input_mask_alpha,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight fallback_exact_weight,
		SearchWeight weight_cap,
		SearchWeight* fixed_alpha_weight_floor,
		int n_bits ) noexcept;

	struct LinearBnbProfileOps
	{
		const char* name = "unknown";
		LinearVarVarModularAddBnBMode varvar_mode =
			LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW;
		LinearVarConstSubBnbMode varconst_mode =
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha;
		LinearSubConstCandidateWeightResolver resolve_subconst_weight = nullptr;
	};

	const LinearBnbProfileOps& linear_bnb_profile_fixed_u_fixed_beta() noexcept;
	const LinearBnbProfileOps& linear_bnb_profile_fixed_u_fixed_alpha() noexcept;
	const LinearBnbProfileOps& linear_bnb_profile_fixed_vw_fixed_beta() noexcept;
	const LinearBnbProfileOps& linear_bnb_profile_fixed_vw_fixed_alpha() noexcept;

	static inline const LinearBnbProfileOps& resolve_linear_bnb_profile_ops(
		const LinearBestSearchConfiguration& configuration ) noexcept
	{
		switch ( configuration.varvar_modular_add_bnb_mode )
		{
		case LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU:
			switch ( configuration.varconst_sub_bnb_mode )
			{
			case LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta:
			case LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta:
				return linear_bnb_profile_fixed_vw_fixed_alpha();
			case LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha:
			case LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha:
			default:
				return linear_bnb_profile_fixed_vw_fixed_beta();
			}
		case LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW:
		default:
			switch ( configuration.varconst_sub_bnb_mode )
			{
			case LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta:
			case LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta:
				return linear_bnb_profile_fixed_u_fixed_alpha();
			case LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha:
			case LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha:
			default:
				return linear_bnb_profile_fixed_u_fixed_beta();
			}
		}
	}
}
