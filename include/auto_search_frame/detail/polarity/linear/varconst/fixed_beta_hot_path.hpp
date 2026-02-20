#pragma once

#include <optional>
#include <vector>

#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_strict_witness.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"

namespace TwilightDream::auto_search_linear
{
	struct LinearFixedBetaOuterHotPathStageBinding
	{
		std::uint32_t output_mask_beta;
		std::uint32_t constant;
		SearchWeight& weight_cap;
		SearchWeight& weight_floor;
		SubConstStreamingCursor& streaming_cursor;
		std::vector<SubConstCandidate>& materialized_candidates;
	};

	[[nodiscard]] std::vector<SubConstCandidate> materialize_fixed_beta_subconst_hot_path_candidates(
		LinearFixedBetaHotPathSubconstMode hot_path_mode,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap );

	[[nodiscard]] std::vector<SubConstCandidate> materialize_fixed_beta_subconst_hot_path_candidates(
		const LinearFixedBetaOuterHotPathGate& hot_path_gate,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap );

	[[nodiscard]] std::optional<VarConstSubColumnOptimalOnOutputWire> compute_fixed_beta_outer_hot_path_column_floor(
		LinearFixedBetaOuterHotPathStageBinding stage ) noexcept;

	[[nodiscard]] SearchWeight ensure_fixed_beta_outer_hot_path_weight_floor(
		LinearFixedBetaOuterHotPathStageBinding stage ) noexcept;
}  // namespace TwilightDream::auto_search_linear
