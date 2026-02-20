#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"

namespace TwilightDream::auto_search_linear
{
	struct FixedBetaSubColumnRootTheoremRequest
	{
		std::uint32_t output_mask_beta = 0;
		std::uint32_t sub_constant = 0;
		int n_bits = 32;
	};

	struct FixedBetaSubColumnRootTheoremAnswer
	{
		std::uint32_t optimal_input_mask_alpha = 0;
		std::uint64_t exact_abs_weight = 0;
		bool exact_abs_weight_is_exact_2pow64 = false;
		SearchWeight ceil_linear_weight = INFINITE_WEIGHT;

		[[nodiscard]] bool fits_within_cap( SearchWeight weight_cap ) const noexcept
		{
			return ceil_linear_weight <= weight_cap;
		}
	};

	// Search-frame projection of the fixed-beta root theorem onto the outer
	// fixed-output var-const wire contract consumed by engine / collector.
	struct VarConstSubColumnOptimalOnOutputWire
	{
		std::uint32_t input_mask_alpha = 0;
		SearchWeight linear_weight = INFINITE_WEIGHT;
	};

	/**
	 * Fixed-beta objects on the search-frame side:
	 * 1. theorem-backed local root operator:
	 *    `solve_fixed_beta_sub_column_root_theorem_u32(...)`
	 *    + `try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(...)`
	 * 2. strict witness / ordered candidate enumeration:
	 *    lives in `linear_varconst/fixed_beta_strict_witness.hpp`
	 * 3. outer hot-path wiring / materialization policy:
	 *    lives in `linear_varconst/fixed_beta_hot_path.hpp`
	 *
	 * This file owns only (1): the theorem-facing fixed-output root operator exposed
	 * to the search frame. It does not own strict witness generation or gate wiring.
	 *
	 * Active continuation frontier pinned by the research log:
	 * `kterm4t0olent1hookprofneps`.
	 */
	[[nodiscard]] FixedBetaSubColumnRootTheoremRequest make_fixed_beta_sub_column_root_theorem_request(
		std::uint32_t output_mask_beta,
		std::uint32_t sub_constant,
		int n_bits = 32 ) noexcept;

	/**
	 * @brief Fixed-output Gap B theorem-facing root operator:
	 *        given `(beta, C)`, return the exact column-optimal input mask `alpha*`
	 *        together with the exact root numerator weight object used by the
	 *        fixed-beta theorem.
	 *
	 *        For the fixed-beta direction this is backed by the FLAT
	 *        run-synchronized block theorem inside
	 *        `arx_analysis_operators/linear_correlation/constant_optimal_alpha.hpp`:
	 *        beta=0 intervals collapse to a constant-size quotient before the beta=1
	 *        synchronization points are composed.
	 */
	[[nodiscard]] FixedBetaSubColumnRootTheoremAnswer solve_fixed_beta_sub_column_root_theorem_u32(
		FixedBetaSubColumnRootTheoremRequest request ) noexcept;

	[[nodiscard]] std::optional<FixedBetaSubColumnRootTheoremAnswer> try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
		FixedBetaSubColumnRootTheoremRequest request,
		SearchWeight weight_cap ) noexcept;

	[[nodiscard]] VarConstSubColumnOptimalOnOutputWire project_fixed_beta_sub_column_root_theorem_to_output_wire(
		const FixedBetaSubColumnRootTheoremAnswer& answer ) noexcept;

	[[nodiscard]] std::optional<VarConstSubColumnOptimalOnOutputWire> try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
		const std::optional<FixedBetaSubColumnRootTheoremAnswer>& answer ) noexcept;
}  // namespace TwilightDream::auto_search_linear
