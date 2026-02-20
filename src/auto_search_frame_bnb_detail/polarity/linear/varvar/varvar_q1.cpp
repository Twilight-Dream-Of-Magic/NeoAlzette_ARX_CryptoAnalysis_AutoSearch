#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"

#include "arx_analysis_operators/linear_correlation/weight_evaluation_ccz.hpp"

namespace TwilightDream::auto_search_linear
{
	LinearOrderedQ2StreamContract linear_varvar_q2_ordered_stream_contract(
		const LinearBestSearchConfiguration& configuration ) noexcept
	{
		const auto row_q2_contract =
			linear_varvar_row_q2_contract( configuration );
		return LinearOrderedQ2StreamContract {
			.exact_weight_monotone_non_decreasing =
				!row_q2_contract.used_as_candidate_source_for_column_mode,
			.safe_break_when_cap_exceeded =
				!row_q2_contract.used_as_candidate_source_for_column_mode,
			.accelerator_is_index_only = row_q2_contract.accelerator_enabled };
	}

	LinearVarVarQ2Q1Evaluation evaluate_varvar_add_q2_q1_candidate(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t fixed_output_mask_u,
		const AddCandidate& candidate,
		SearchWeight weight_cap,
		int n_bits ) noexcept
	{
		const auto resolved =
			resolve_varvar_add_candidate_for_mode(
				configuration.varvar_modular_add_bnb_mode,
				fixed_output_mask_u,
				candidate,
				weight_cap,
				n_bits );
		return LinearVarVarQ2Q1Evaluation {
			.kind = resolved.kind,
			.candidate = resolved.candidate,
			.ordered_stream =
				linear_varvar_q2_ordered_stream_contract( configuration ) };
	}

	std::optional<SearchWeight> evaluate_varvar_q1_exact_weight(
		const LinearVarVarQ2Candidate& candidate,
		int n_bits ) noexcept
	{
		if ( candidate.exact_weight_hint < INFINITE_WEIGHT )
			return candidate.exact_weight_hint;

		const auto weight =
			TwilightDream::arx_operators::linear_correlation_add_ccz_weight(
				static_cast<std::uint64_t>( candidate.sum_wire_u ),
				static_cast<std::uint64_t>( candidate.q1_input.input_mask_x ),
				static_cast<std::uint64_t>( candidate.q1_input.input_mask_y ),
				n_bits );
		if ( !weight.has_value() )
			return std::nullopt;
		return weight.value();
	}
}  // namespace TwilightDream::auto_search_linear
