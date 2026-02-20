#pragma once

#include <optional>
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_theorem.hpp"

namespace TwilightDream::auto_search_linear
{
	struct LinearBestSearchConfiguration;

	// Fixed-alpha does not currently supply an outer-Q2 shell source for the
	// reverse search frame. This binding is therefore only for root-floor /
	// runtime-exact helper queries once alpha is already concrete.
	struct LinearFixedAlphaOuterHotPathStageBinding
	{
		std::uint32_t input_mask_alpha = 0;
		std::uint32_t constant = 0;
		SearchWeight* weight_cap = nullptr;
		SearchWeight* weight_floor = nullptr;
	};

	// The fixed-alpha helper only exposes the theorem/root optimum beta* on the
	// already-fixed input wire. It is not a cap-complete outer candidate stream.
	[[nodiscard]] std::optional<VarConstSubColumnOptimalOnInputWire> compute_fixed_alpha_outer_hot_path_column_floor(
		LinearFixedAlphaOuterHotPathStageBinding stage ) noexcept;

	[[nodiscard]] SearchWeight ensure_fixed_alpha_outer_hot_path_weight_floor(
		LinearFixedAlphaOuterHotPathStageBinding stage ) noexcept;

	[[nodiscard]] bool fixed_alpha_dual_fixed_pair_within_cap(
		std::uint32_t input_mask_alpha,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] std::optional<SearchWeight> resolve_varconst_sub_candidate_weight_for_runtime(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t input_mask_alpha,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight fallback_exact_weight,
		SearchWeight weight_cap,
		SearchWeight* fixed_alpha_weight_floor = nullptr,
		int n_bits = 32 ) noexcept;
}  // namespace TwilightDream::auto_search_linear
