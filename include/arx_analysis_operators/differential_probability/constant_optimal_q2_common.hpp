#pragma once
/**
 * @file differential_probability/constant_optimal_q2_common.hpp
 * @brief Neutral low-level helpers for exact Q2 reference / verifier code.
 *
 * This file is intentionally not a direction-unified Q2 framework.
 * The two public directional APIs:
 * - fixed-input  -> `constant_optimal_input_alpha.hpp`
 * - fixed-output -> `constant_optimal_output_beta.hpp`
 * may evolve into different generators later.
 *
 * What stays here is only the shared low-level substrate:
 * - add/sub normalization
 * - frontier candidate container
 * - Pareto pruning
 * - small stats helpers
 */

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "arx_analysis_operators/math_util.hpp"
#include "arx_analysis_operators/UnsignedInteger128Bit.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using DiffConstExactCount = UnsignedInteger128Bit;

		enum class DiffConstQ2Operation : std::uint8_t
		{
			modular_add,
			modular_sub,
		};

		namespace detail_diffconst_q2_common
		{
			struct FrontierCandidate
			{
				DiffConstExactCount count_if_carry0 { 0 };
				DiffConstExactCount count_if_carry1 { 0 };
				std::uint64_t variable_delta { 0 };
			};

			[[nodiscard]] inline constexpr std::uint64_t mask_n( int n ) noexcept
			{
				if ( n <= 0 )
					return 0ull;
				return ( n >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << n ) - 1ull );
			}

			[[nodiscard]] inline constexpr bool is_supported_width( int n ) noexcept
			{
				return n > 0 && n <= 64;
			}

			[[nodiscard]] inline constexpr std::uint64_t normalize_add_constant_for_q2(
				DiffConstQ2Operation operation,
				std::uint64_t constant,
				int n ) noexcept
			{
				if ( operation == DiffConstQ2Operation::modular_sub )
					return TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, n );
				return constant & mask_n( n );
			}

			[[nodiscard]] inline long double exact_count_to_long_double( const DiffConstExactCount& count ) noexcept
			{
				long double value = std::scalbn( static_cast<long double>( count.high64() ), 64 );
				value += static_cast<long double>( count.low64() );
				return value;
			}

			[[nodiscard]] inline double exact_count_to_double( const DiffConstExactCount& count ) noexcept
			{
				double value = std::ldexp( static_cast<double>( count.high64() ), 64 );
				value += static_cast<double>( count.low64() );
				return value;
			}

			[[nodiscard]] inline int exact_count_bit_width( const DiffConstExactCount& count ) noexcept
			{
				return count.bit_width();
			}

			template<typename StatsT>
			inline void clear_stats( StatsT* stats ) noexcept
			{
				if ( stats != nullptr )
					*stats = {};
			}

			template<typename StatsT>
			inline void update_frontier_stats(
				std::size_t before_prune_size,
				std::size_t after_prune_size,
				StatsT* stats ) noexcept
			{
				if ( stats == nullptr )
					return;
				stats->frontier_generated_entries += before_prune_size;
				stats->frontier_kept_entries += after_prune_size;
				stats->frontier_pruned_entries += before_prune_size - after_prune_size;
				if ( after_prune_size > stats->frontier_peak_entries )
					stats->frontier_peak_entries = after_prune_size;
			}

			template<typename StatsT>
			inline void prune_pareto_frontier(
				std::vector<FrontierCandidate>& candidates,
				StatsT* stats ) noexcept
			{
				if ( candidates.empty() )
				{
					update_frontier_stats( 0u, 0u, stats );
					return;
				}

				const std::size_t before_prune_size = candidates.size();
				std::sort(
					candidates.begin(),
					candidates.end(),
					[]( const FrontierCandidate& lhs, const FrontierCandidate& rhs ) noexcept
					{
						if ( lhs.count_if_carry0 != rhs.count_if_carry0 )
							return lhs.count_if_carry0 > rhs.count_if_carry0;
						if ( lhs.count_if_carry1 != rhs.count_if_carry1 )
							return lhs.count_if_carry1 > rhs.count_if_carry1;
						return lhs.variable_delta < rhs.variable_delta;
					} );

				std::vector<FrontierCandidate> deduplicated {};
				deduplicated.reserve( candidates.size() );
				for ( const FrontierCandidate& candidate : candidates )
				{
					if ( !deduplicated.empty() &&
						 deduplicated.back().count_if_carry0 == candidate.count_if_carry0 &&
						 deduplicated.back().count_if_carry1 == candidate.count_if_carry1 )
					{
						if ( candidate.variable_delta < deduplicated.back().variable_delta )
							deduplicated.back() = candidate;
						continue;
					}
					deduplicated.push_back( candidate );
				}

				std::vector<FrontierCandidate> pruned {};
				pruned.reserve( deduplicated.size() );
				DiffConstExactCount best_count_if_carry1_from_strictly_greater_count_if_carry0 { 0 };
				bool has_strictly_greater_group = false;

				std::size_t group_begin = 0;
				while ( group_begin < deduplicated.size() )
				{
					std::size_t group_end = group_begin + 1;
					while ( group_end < deduplicated.size() &&
							deduplicated[ group_end ].count_if_carry0 == deduplicated[ group_begin ].count_if_carry0 )
					{
						++group_end;
					}

					DiffConstExactCount group_max_count_if_carry1 { 0 };
					for ( std::size_t index = group_begin; index < group_end; ++index )
					{
						const FrontierCandidate& candidate = deduplicated[ index ];
						if ( !has_strictly_greater_group ||
							 best_count_if_carry1_from_strictly_greater_count_if_carry0 <= candidate.count_if_carry1 )
						{
							pruned.push_back( candidate );
						}
						if ( candidate.count_if_carry1 > group_max_count_if_carry1 )
							group_max_count_if_carry1 = candidate.count_if_carry1;
					}

					if ( !has_strictly_greater_group ||
						 group_max_count_if_carry1 > best_count_if_carry1_from_strictly_greater_count_if_carry0 )
					{
						best_count_if_carry1_from_strictly_greater_count_if_carry0 = group_max_count_if_carry1;
						has_strictly_greater_group = true;
					}
					group_begin = group_end;
				}

				candidates.swap( pruned );
				update_frontier_stats( before_prune_size, candidates.size(), stats );
			}
		}  // namespace detail_diffconst_q2_common
	}  // namespace arx_operators
}  // namespace TwilightDream
