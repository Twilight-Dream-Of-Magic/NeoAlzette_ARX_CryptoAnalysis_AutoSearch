#include "auto_search_frame/detail/polarity/linear/linear_bnb_profile_ops.hpp"
#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		std::optional<SearchWeight> resolve_subconst_fixed_beta(
			const LinearBestSearchConfiguration& /*configuration*/,
			std::uint32_t input_mask_alpha,
			std::uint32_t output_mask_beta,
			std::uint32_t constant,
			SearchWeight fallback_exact_weight,
			SearchWeight weight_cap,
			SearchWeight* fixed_alpha_weight_floor,
			int n_bits ) noexcept
		{
			( void )n_bits;
			const auto exact_weight =
				evaluate_varconst_sub_q1_exact_weight(
					input_mask_alpha,
					constant,
					output_mask_beta );
			if ( !exact_weight.has_value() )
			{
				if ( fixed_alpha_weight_floor != nullptr )
					*fixed_alpha_weight_floor = MAX_FINITE_SEARCH_WEIGHT;
				return std::nullopt;
			}
			if ( fixed_alpha_weight_floor != nullptr )
				*fixed_alpha_weight_floor = std::min( fallback_exact_weight, exact_weight.value() );
			if ( exact_weight.value() > weight_cap )
				return std::nullopt;
			return exact_weight.value();
		}
	}

	const LinearBnbProfileOps& linear_bnb_profile_fixed_u_fixed_beta() noexcept
	{
		static const LinearBnbProfileOps kProfile {
			"fixed_u_fixed_beta",
			LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW,
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha,
			&resolve_subconst_fixed_beta };
		return kProfile;
	}
}
