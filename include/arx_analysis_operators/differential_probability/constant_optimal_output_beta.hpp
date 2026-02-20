#pragma once
/**
 * @file differential_probability/constant_optimal_output_beta.hpp
 * @brief Fixed-output-delta public API for exact var-const Q2 differential probability.
 *
 * This header owns the fixed-output-delta generator problem:
 * - input  : delta_y
 * - output : optimal delta_x
 *
 * The current implementation is an exact reference generator backed by
 * suffix response-vector Pareto frontiers and a Q1 verifier (`exact_count`).
 * Future candidate algorithms for this direction can diverge freely without
 * coupling to the fixed-input direction.
 */

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "constant_optimal_q2_common.hpp"
#include "arx_analysis_operators/differential_probability/constant_weight_evaluation.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

		struct DiffConstFixedOutputQ2Stats
		{
			std::size_t transition_evaluations { 0 };
			std::size_t frontier_generated_entries { 0 };
			std::size_t frontier_kept_entries { 0 };
			std::size_t frontier_pruned_entries { 0 };
			std::size_t frontier_peak_entries { 0 };
		};

		struct DiffConstOptimalInputDeltaResult
		{
			std::uint64_t delta_x { 0 };
			std::uint64_t carry_difference { 0 };
			DiffConstExactCount exact_count { 0 };
			double exact_probability { 0.0 };
			double exact_weight { std::numeric_limits<double>::infinity() };
			SearchWeight exact_weight_ceil_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
			bool is_possible { false };
		};

		namespace detail_diffconst_fixed_output_q2
		{
			using detail_diffconst_q2_common::FrontierCandidate;

			[[nodiscard]] inline DiffConstOptimalInputDeltaResult make_verified_input_result(
				std::uint64_t delta_y,
				std::uint64_t constant,
				std::uint64_t delta_x,
				int n,
				DiffConstQ2Operation operation ) noexcept
			{
				DiffConstOptimalInputDeltaResult result {};
				if ( !detail_diffconst_q2_common::is_supported_width( n ) )
					return result;

				const std::uint64_t mask = detail_diffconst_q2_common::mask_n( n );
				const std::uint64_t input_difference = delta_x & mask;
				const std::uint64_t output_difference = delta_y & mask;
				const std::uint64_t add_constant =
					detail_diffconst_q2_common::normalize_add_constant_for_q2( operation, constant, n );
				const DiffConstExactCount exact_count =
					diff_addconst_exact_count_n( input_difference, add_constant, output_difference, n );

				result.delta_x = input_difference;
				result.carry_difference = ( input_difference ^ output_difference ) & mask;
				result.exact_count = exact_count;
				result.is_possible = ( exact_count != 0ull );
				if ( exact_count == 0ull )
					return result;

				result.exact_probability = std::ldexp(
					static_cast<double>( detail_diffconst_q2_common::exact_count_to_long_double( exact_count ) ),
					-n );
				result.exact_weight =
					static_cast<double>( n ) -
					std::log2( static_cast<double>( detail_diffconst_q2_common::exact_count_to_long_double( exact_count ) ) );
				const SearchWeight exact_weight_ceil_int =
					static_cast<SearchWeight>( n - ( detail_diffconst_q2_common::exact_count_bit_width( exact_count ) - 1 ) );
				result.exact_weight_ceil_int = exact_weight_ceil_int;
				return result;
			}

			[[nodiscard]] inline DiffConstOptimalInputDeltaResult find_optimal_input_delta_exact_reference(
				std::uint64_t delta_y,
				std::uint64_t constant,
				int n,
				DiffConstQ2Operation operation,
				DiffConstFixedOutputQ2Stats* stats = nullptr ) noexcept
			{
				detail_diffconst_q2_common::clear_stats( stats );
				DiffConstOptimalInputDeltaResult result {};
				if ( !detail_diffconst_q2_common::is_supported_width( n ) )
					return result;

				const std::uint64_t mask = detail_diffconst_q2_common::mask_n( n );
				const std::uint64_t fixed_output_difference = delta_y & mask;
				const std::uint64_t add_constant =
					detail_diffconst_q2_common::normalize_add_constant_for_q2( operation, constant, n );

				std::array<std::vector<FrontierCandidate>, 2> frontier_by_current_carry_difference {};
				const int most_significant_bit = n - 1;
				const std::uint64_t fixed_output_msb = ( fixed_output_difference >> most_significant_bit ) & 1ull;
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					FrontierCandidate candidate {};
					candidate.count_if_carry0 = 2ull;
					candidate.count_if_carry1 = 2ull;
					candidate.variable_delta =
						( fixed_output_msb ^ current_carry_difference_bit ) << most_significant_bit;
					frontier_by_current_carry_difference[ current_carry_difference_bit ].push_back( candidate );
				}
				if ( stats != nullptr )
				{
					stats->frontier_generated_entries = 2u;
					stats->frontier_kept_entries = 2u;
					stats->frontier_peak_entries = 1u;
				}

				for ( int bit_index = n - 2; bit_index >= 0; --bit_index )
				{
					std::array<std::vector<FrontierCandidate>, 2> next_frontier_by_current_carry_difference {};
					const std::uint64_t output_difference_bit =
						( fixed_output_difference >> bit_index ) & 1ull;
					const std::uint64_t constant_bit = ( add_constant >> bit_index ) & 1ull;

					for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
					{
						std::vector<FrontierCandidate>& next_frontier =
							next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
						const std::uint64_t input_difference_bit =
							output_difference_bit ^ current_carry_difference_bit;

						for ( std::uint32_t next_carry_difference_bit = 0u; next_carry_difference_bit <= 1u; ++next_carry_difference_bit )
						{
							const detail::AddConstCarryEdgeMatrix2 edge_matrix =
								detail::make_addconst_carry_edge_matrix(
									static_cast<std::uint32_t>( input_difference_bit ),
									static_cast<std::uint32_t>( constant_bit ),
									current_carry_difference_bit,
									next_carry_difference_bit );

							for ( const FrontierCandidate& child_candidate : frontier_by_current_carry_difference[ next_carry_difference_bit ] )
							{
								if ( stats != nullptr )
									++stats->transition_evaluations;
								FrontierCandidate candidate {};
								candidate.count_if_carry0 =
									static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 0 ] ) * child_candidate.count_if_carry0 +
									static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 1 ] ) * child_candidate.count_if_carry1;
								candidate.count_if_carry1 =
									static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 0 ] ) * child_candidate.count_if_carry0 +
									static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 1 ] ) * child_candidate.count_if_carry1;
								if ( ( candidate.count_if_carry0 | candidate.count_if_carry1 ) == 0ull )
									continue;

								candidate.variable_delta =
									child_candidate.variable_delta |
									( input_difference_bit << bit_index );
								next_frontier.push_back( candidate );
							}
						}

						detail_diffconst_q2_common::prune_pareto_frontier( next_frontier, stats );
					}

					frontier_by_current_carry_difference.swap( next_frontier_by_current_carry_difference );
				}

				const std::vector<FrontierCandidate>& root_frontier = frontier_by_current_carry_difference[ 0u ];
				if ( root_frontier.empty() )
					return result;

				const FrontierCandidate* best_candidate = nullptr;
				for ( const FrontierCandidate& candidate : root_frontier )
				{
					if ( best_candidate == nullptr ||
						 candidate.count_if_carry0 > best_candidate->count_if_carry0 ||
						 ( candidate.count_if_carry0 == best_candidate->count_if_carry0 &&
						   candidate.variable_delta < best_candidate->variable_delta ) )
					{
						best_candidate = &candidate;
					}
				}

				if ( best_candidate == nullptr )
					return result;
				return make_verified_input_result(
					fixed_output_difference,
					constant,
					best_candidate->variable_delta,
					n,
					operation );
			}
		}  // namespace detail_diffconst_fixed_output_q2

		[[nodiscard]] inline DiffConstOptimalInputDeltaResult find_optimal_input_delta_addconst_exact_reference(
			std::uint64_t delta_y,
			std::uint64_t add_constant_K,
			int n,
			DiffConstFixedOutputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_output_q2::find_optimal_input_delta_exact_reference(
				delta_y,
				add_constant_K,
				n,
				DiffConstQ2Operation::modular_add,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalInputDeltaResult find_optimal_input_delta_subconst_exact_reference(
			std::uint64_t delta_y,
			std::uint64_t sub_constant_C,
			int n,
			DiffConstFixedOutputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_output_q2::find_optimal_input_delta_exact_reference(
				delta_y,
				sub_constant_C,
				n,
				DiffConstQ2Operation::modular_sub,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalInputDeltaResult find_optimal_input_delta_addconst(
			std::uint64_t delta_y,
			std::uint64_t add_constant_K,
			int n,
			DiffConstFixedOutputQ2Stats* stats = nullptr ) noexcept
		{
			return find_optimal_input_delta_addconst_exact_reference(
				delta_y,
				add_constant_K,
				n,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalInputDeltaResult find_optimal_input_delta_subconst(
			std::uint64_t delta_y,
			std::uint64_t sub_constant_C,
			int n,
			DiffConstFixedOutputQ2Stats* stats = nullptr ) noexcept
		{
			return find_optimal_input_delta_subconst_exact_reference(
				delta_y,
				sub_constant_C,
				n,
				stats );
		}
	}  // namespace arx_operators
}  // namespace TwilightDream
