#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
#include "arx_analysis_operators/linear_correlation/constant_optimal_beta.hpp"

namespace TwilightDream::auto_search_linear
{
	// Active fixed-alpha theorem/root source pinned by the research log:
	// live `v116` tail-theorem-path support.

	struct FixedAlphaSubColumnRootTheoremRequest
	{
		std::uint32_t input_mask_alpha = 0;
		std::uint32_t sub_constant = 0;
		int n_bits = 32;
	};

	struct FixedAlphaSubColumnRootTheoremAnswer
	{
		std::uint32_t optimal_output_mask_beta = 0;
		std::uint64_t exact_abs_weight = 0;
		bool exact_abs_weight_is_exact_2pow64 = false;
		SearchWeight ceil_linear_weight = INFINITE_WEIGHT;
	};

	struct VarConstSubColumnOptimalOnInputWire
	{
		std::uint32_t output_mask_beta = 0;
		SearchWeight linear_weight = INFINITE_WEIGHT;
	};

	[[nodiscard]] FixedAlphaSubColumnRootTheoremRequest make_fixed_alpha_sub_column_root_theorem_request(
		std::uint32_t input_mask_alpha,
		std::uint32_t sub_constant,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] FixedAlphaSubColumnRootTheoremAnswer solve_fixed_alpha_sub_column_root_theorem_u32(
		FixedAlphaSubColumnRootTheoremRequest request ) noexcept;

	[[nodiscard]] std::optional<FixedAlphaSubColumnRootTheoremAnswer> try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
		FixedAlphaSubColumnRootTheoremRequest request,
		SearchWeight weight_cap ) noexcept;

	[[nodiscard]] VarConstSubColumnOptimalOnInputWire project_fixed_alpha_sub_column_root_theorem_to_input_wire(
		const FixedAlphaSubColumnRootTheoremAnswer& answer ) noexcept;

	[[nodiscard]] std::optional<VarConstSubColumnOptimalOnInputWire> try_project_fixed_alpha_sub_column_root_theorem_to_input_wire(
		const std::optional<FixedAlphaSubColumnRootTheoremAnswer>& answer ) noexcept;
}  // namespace TwilightDream::auto_search_linear
