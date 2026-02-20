#pragma once

#include <cstdint>
#include <vector>

#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"

namespace TwilightDream::auto_search_linear
{
	struct FixedBetaSubColumnStrictWitnessRequest
	{
		std::uint32_t output_mask_beta = 0;
		std::uint32_t sub_constant = 0;
		SearchWeight weight_cap = 0;
		int n_bits = 32;
	};

	struct FixedBetaSubColumnStrictWitness
	{
		std::uint32_t input_mask_alpha = 0;
		SearchWeight linear_weight = 0;
	};

	using FixedBetaSubColumnStrictWitnessStreamingCursor = SubConstStreamingCursor;

	[[nodiscard]] static inline FixedBetaSubColumnStrictWitnessRequest make_fixed_beta_sub_column_strict_witness_request(
		std::uint32_t output_mask_beta,
		std::uint32_t sub_constant,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept
	{
		return FixedBetaSubColumnStrictWitnessRequest {
			output_mask_beta,
			sub_constant,
			weight_cap,
			n_bits };
	}

	[[nodiscard]] static inline FixedBetaSubColumnRootTheoremRequest make_fixed_beta_sub_column_root_theorem_request(
		FixedBetaSubColumnStrictWitnessRequest request ) noexcept
	{
		return make_fixed_beta_sub_column_root_theorem_request(
			request.output_mask_beta,
			request.sub_constant,
			request.n_bits );
	}

	[[nodiscard]] static inline FixedBetaSubColumnStrictWitness make_fixed_beta_sub_column_strict_witness(
		std::uint32_t input_mask_alpha,
		SearchWeight linear_weight ) noexcept
	{
		return FixedBetaSubColumnStrictWitness {
			input_mask_alpha,
			linear_weight };
	}

	[[nodiscard]] static inline FixedBetaSubColumnStrictWitness project_subconst_candidate_to_fixed_beta_sub_column_strict_witness(
		const SubConstCandidate& candidate ) noexcept
	{
		return make_fixed_beta_sub_column_strict_witness(
			candidate.input_mask_on_x,
			candidate.linear_weight );
	}

	[[nodiscard]] static inline SubConstCandidate project_fixed_beta_sub_column_strict_witness_to_subconst_candidate(
		const FixedBetaSubColumnStrictWitness& witness ) noexcept
	{
		return SubConstCandidate {
			witness.input_mask_alpha,
			witness.linear_weight };
	}

	[[nodiscard]] static inline FixedBetaSubColumnStrictWitness project_fixed_beta_sub_column_root_theorem_answer_to_strict_witness(
		const FixedBetaSubColumnRootTheoremAnswer& answer ) noexcept
	{
		return make_fixed_beta_sub_column_strict_witness(
			answer.optimal_input_mask_alpha,
			answer.ceil_linear_weight );
	}

	[[nodiscard]] static inline std::optional<FixedBetaSubColumnStrictWitness> try_project_fixed_beta_sub_column_root_theorem_answer_to_strict_witness(
		const std::optional<FixedBetaSubColumnRootTheoremAnswer>& answer ) noexcept
	{
		if ( !answer.has_value() )
			return std::nullopt;
		return project_fixed_beta_sub_column_root_theorem_answer_to_strict_witness( *answer );
	}

	[[nodiscard]] static inline std::vector<SubConstCandidate> project_fixed_beta_sub_column_strict_witnesses_to_subconst_candidates(
		const std::vector<FixedBetaSubColumnStrictWitness>& witnesses )
	{
		std::vector<SubConstCandidate> out {};
		out.reserve( witnesses.size() );
		for ( const auto& witness : witnesses )
			out.push_back( project_fixed_beta_sub_column_strict_witness_to_subconst_candidate( witness ) );
		return out;
	}

	void rebuild_fixed_beta_sub_column_strict_witness_cursor_runtime_state(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor );

	void restore_fixed_beta_sub_column_strict_witness_cursor_heap_snapshot(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		const std::vector<SubConstCandidate>& heap_snapshot );

	void reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedBetaSubColumnStrictWitnessRequest request );

	bool next_fixed_beta_sub_column_strict_witness(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedBetaSubColumnStrictWitness& out_witness );

	std::vector<FixedBetaSubColumnStrictWitness> materialize_fixed_beta_sub_column_strict_witnesses_exact(
		FixedBetaSubColumnStrictWitnessRequest request );

	std::vector<FixedBetaSubColumnStrictWitness> materialize_fixed_beta_sub_column_strict_witnesses(
		FixedBetaSubColumnStrictWitnessRequest request );
}  // namespace TwilightDream::auto_search_linear
