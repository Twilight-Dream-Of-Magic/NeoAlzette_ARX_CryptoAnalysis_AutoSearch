#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_hot_path.hpp"
#include "arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp"

namespace TwilightDream::auto_search_linear
{
	std::optional<SearchWeight> weight_sub_const_ceil_int(
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant,
		std::uint32_t output_mask_beta ) noexcept
	{
		using TwilightDream::arx_operators::LinearCorrelation;

		constexpr int kWordBitSize = 32;
		const LinearCorrelation linear_correlation =
			TwilightDream::arx_operators::linear_x_modulo_minus_const32(
				input_mask_alpha,
				subtrahend_constant,
				output_mask_beta,
				kWordBitSize );
		if ( !linear_correlation.is_feasible() )
			return std::nullopt;

		const double abs_correlation = std::abs( linear_correlation.correlation );
		if ( !( abs_correlation > 0.0 ) || !std::isfinite( abs_correlation ) )
			return std::nullopt;

		constexpr double kScale2Pow32 = 4294967296.0;
		const std::uint64_t abs_correlation_numerator =
			static_cast<std::uint64_t>(
				std::llround( abs_correlation * kScale2Pow32 ) );
		if ( abs_correlation_numerator == 0u )
			return std::nullopt;

		const unsigned msb_index =
			std::bit_width( abs_correlation_numerator ) - 1u;
		const SearchWeight weight =
			static_cast<SearchWeight>( kWordBitSize - msb_index );
		return std::min<SearchWeight>( weight, kWordBitSize );
	}

	std::optional<SearchWeight> evaluate_varconst_sub_q1_exact_weight(
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant,
		std::uint32_t output_mask_beta ) noexcept
	{
		return weight_sub_const_ceil_int(
			input_mask_alpha,
			subtrahend_constant,
			output_mask_beta );
	}

	LinearOrderedQ2StreamContract linear_varconst_fixed_beta_q2_ordered_stream_contract(
		const LinearFixedBetaOuterHotPathGate& /*hot_path_gate*/ ) noexcept
	{
		return LinearOrderedQ2StreamContract {
			.exact_weight_monotone_non_decreasing = true,
			.safe_break_when_cap_exceeded = true,
			.accelerator_is_index_only = true };
	}

	LinearVarConstQ2Q1Evaluation evaluate_varconst_sub_q2_q1_candidate(
		const LinearBestSearchConfiguration& configuration,
		const SubConstCandidate& candidate,
		std::uint32_t output_mask_beta,
		std::uint32_t subtrahend_constant,
		SearchWeight weight_cap,
		const LinearFixedBetaOuterHotPathGate& hot_path_gate,
		SearchWeight* fixed_alpha_weight_floor ) noexcept
	{
		( void )configuration;
		const auto exact_weight =
			evaluate_varconst_q1_exact_weight(
				LinearVarConstQ2Candidate {
					candidate.input_mask_on_x,
					output_mask_beta,
					candidate.linear_weight,
					linear_varconst_fixed_beta_q2_ordered_stream_contract( hot_path_gate ) },
				subtrahend_constant,
				weight_cap,
				fixed_alpha_weight_floor,
				32 );

		return LinearVarConstQ2Q1Evaluation {
			.accepted = exact_weight.has_value(),
			.exact_local_weight = exact_weight.value_or( INFINITE_WEIGHT ),
			.ordered_stream =
				linear_varconst_fixed_beta_q2_ordered_stream_contract(
					hot_path_gate ) };
	}

	std::optional<SearchWeight> evaluate_varconst_q1_exact_weight(
		const LinearVarConstQ2Candidate& candidate,
		std::uint32_t subtrahend_constant,
		SearchWeight weight_cap,
		SearchWeight* fixed_alpha_weight_floor,
		int n_bits ) noexcept
	{
		if ( fixed_alpha_weight_floor != nullptr &&
			 *fixed_alpha_weight_floor == INFINITE_WEIGHT )
		{
			const auto answer =
				try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
					make_fixed_alpha_sub_column_root_theorem_request(
						candidate.input_mask_alpha,
						subtrahend_constant,
						n_bits ),
					weight_cap );
			*fixed_alpha_weight_floor =
				answer.has_value()
					? answer->ceil_linear_weight
					: MAX_FINITE_SEARCH_WEIGHT;
		}

		if ( candidate.exact_weight_hint < INFINITE_WEIGHT &&
			 candidate.exact_weight_hint <= weight_cap )
		{
			return candidate.exact_weight_hint;
		}

		const SearchWeight exact_weight =
			TwilightDream::arx_operators::correlation_sub_const_weight_ceil_int_logdepth(
				static_cast<std::uint64_t>( candidate.input_mask_alpha ),
				static_cast<std::uint64_t>( subtrahend_constant ),
				static_cast<std::uint64_t>( candidate.output_mask_beta ),
				n_bits );
		if ( exact_weight > weight_cap )
			return std::nullopt;
		return exact_weight;
	}

	LinearVarConstQ2Q1Evaluation evaluate_fixed_alpha_subconst_q2_q1_candidate(
		const FixedAlphaSubConstCandidate& candidate,
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant ) noexcept
	{
		const auto exact_weight =
			evaluate_varconst_sub_q1_exact_weight(
				input_mask_alpha,
				subtrahend_constant,
				candidate.output_mask_beta );
		return LinearVarConstQ2Q1Evaluation {
			.accepted = exact_weight.has_value(),
			.exact_local_weight = exact_weight.value_or( INFINITE_WEIGHT ),
			.ordered_stream = LinearOrderedQ2StreamContract {
				.exact_weight_monotone_non_decreasing = true,
				.safe_break_when_cap_exceeded = true,
				.accelerator_is_index_only = true } };
	}

}  // namespace TwilightDream::auto_search_linear
