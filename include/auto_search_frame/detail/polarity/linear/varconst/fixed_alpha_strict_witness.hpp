#pragma once

#include <cstdint>
#include <vector>

#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_theorem.hpp"

namespace TwilightDream::auto_search_linear
{
	struct FixedAlphaSubColumnStrictWitnessRequest
	{
		std::uint32_t input_mask_alpha = 0;
		std::uint32_t sub_constant = 0;
		SearchWeight weight_cap = 0;
		int n_bits = 32;
	};

	struct FixedAlphaSubColumnStrictWitness
	{
		std::uint32_t output_mask_beta = 0;
		SearchWeight linear_weight = 0;
	};

	using FixedAlphaSubColumnStrictWitnessStreamingCursor = FixedAlphaSubConstStreamingCursor;

	[[nodiscard]] FixedAlphaSubColumnStrictWitnessRequest make_fixed_alpha_sub_column_strict_witness_request(
		std::uint32_t input_mask_alpha,
		std::uint32_t sub_constant,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] FixedAlphaSubColumnStrictWitness make_fixed_alpha_sub_column_strict_witness(
		std::uint32_t output_mask_beta,
		SearchWeight linear_weight ) noexcept;

	[[nodiscard]] FixedAlphaSubConstCandidate project_fixed_alpha_sub_column_strict_witness_to_candidate(
		const FixedAlphaSubColumnStrictWitness& witness ) noexcept;

	[[nodiscard]] FixedAlphaSubColumnStrictWitness project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness(
		const FixedAlphaSubColumnRootTheoremAnswer& answer ) noexcept;

	[[nodiscard]] std::optional<FixedAlphaSubColumnStrictWitness> try_project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness(
		const std::optional<FixedAlphaSubColumnRootTheoremAnswer>& answer ) noexcept;

	[[nodiscard]] std::vector<FixedAlphaSubConstCandidate> project_fixed_alpha_sub_column_strict_witnesses_to_candidates(
		const std::vector<FixedAlphaSubColumnStrictWitness>& witnesses );

	void rebuild_fixed_alpha_sub_column_strict_witness_cursor_runtime_state(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor );

	void restore_fixed_alpha_sub_column_strict_witness_cursor_heap_snapshot(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		const std::vector<FixedAlphaSubConstCandidate>& heap_snapshot );

	void reset_fixed_alpha_sub_column_strict_witness_streaming_cursor(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedAlphaSubColumnStrictWitnessRequest request );

	bool next_fixed_alpha_sub_column_strict_witness(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedAlphaSubColumnStrictWitness& out_witness );

	std::vector<FixedAlphaSubColumnStrictWitness> materialize_fixed_alpha_sub_column_strict_witnesses_exact(
		FixedAlphaSubColumnStrictWitnessRequest request );

	std::vector<FixedAlphaSubColumnStrictWitness> materialize_fixed_alpha_sub_column_strict_witnesses(
		FixedAlphaSubColumnStrictWitnessRequest request );
}  // namespace TwilightDream::auto_search_linear
