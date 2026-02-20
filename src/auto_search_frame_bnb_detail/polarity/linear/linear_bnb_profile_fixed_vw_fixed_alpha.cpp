#include "auto_search_frame/detail/polarity/linear/linear_bnb_profile_ops.hpp"
#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_hot_path.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		std::optional<SearchWeight> resolve_subconst_fixed_alpha(
			const LinearBestSearchConfiguration& configuration,
			std::uint32_t input_mask_alpha,
			std::uint32_t output_mask_beta,
			std::uint32_t constant,
			SearchWeight fallback_exact_weight,
			SearchWeight weight_cap,
			SearchWeight* fixed_alpha_weight_floor,
			int n_bits ) noexcept
		{
			return resolve_varconst_sub_candidate_weight_for_runtime(
				configuration,
				input_mask_alpha,
				output_mask_beta,
				constant,
				fallback_exact_weight,
				weight_cap,
				fixed_alpha_weight_floor,
				n_bits );
		}
	}

	const LinearBnbProfileOps& linear_bnb_profile_fixed_vw_fixed_alpha() noexcept
	{
		static const LinearBnbProfileOps kProfile {
			"fixed_vw_fixed_alpha",
			LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU,
			LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta,
			&resolve_subconst_fixed_alpha };
		return kProfile;
	}
}
