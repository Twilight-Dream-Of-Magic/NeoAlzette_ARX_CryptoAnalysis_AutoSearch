#pragma once
/**
 * @file differential_probability/constant_optimal_input_alpha.hpp
 * @brief Fixed-input-delta public API for exact var-const Q2 differential probability.
 *
 * This header owns the fixed-input-delta generator problem:
 * - input  : delta_x
 * - output : optimal delta_y
 *
 * The current implementation is an exact reference generator backed by
 * suffix response-vector Pareto frontiers and a Q1 verifier (`exact_count`).
 * Future candidate algorithms for this direction can diverge freely without
 * coupling to the fixed-output direction.
 */

#include <array>
#include <bit>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "constant_optimal_q2_common.hpp"
#include "arx_analysis_operators/differential_probability/constant_weight_evaluation.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

		struct DiffConstFixedInputQ2Stats
		{
			std::size_t transition_evaluations { 0 };
			std::size_t frontier_generated_entries { 0 };
			std::size_t frontier_kept_entries { 0 };
			std::size_t frontier_pruned_entries { 0 };
			std::size_t frontier_peak_entries { 0 };
			std::size_t prototype_transition_proposals { 0 };
			std::size_t prototype_transition_hits { 0 };
			std::size_t prototype_transition_fallbacks { 0 };
			bool used_phase8_banded_support_prototype { false };
		};

		struct DiffConstFixedInputPrototypeSupportConfig
		{
			int support_max_suffix { 15 };
			std::uint32_t suffix10_plus_upgrade_mask { 0xEEu };
			bool suffix10_plus_include_carry1_full { true };
			int suffix10_plus_anchor_bits { 3 };
			int suffix10_plus_residue_high_bits_floor { 8 };
		};

		struct DiffConstOptimalOutputDeltaResult
		{
			std::uint64_t delta_y { 0 };
			std::uint64_t carry_difference { 0 };
			DiffConstExactCount exact_count { 0 };
			double exact_probability { 0.0 };
			double exact_weight { std::numeric_limits<double>::infinity() };
			SearchWeight exact_weight_ceil_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
			bool is_possible { false };
		};

		namespace detail_diffconst_fixed_input_q2
		{
			using detail_diffconst_q2_common::FrontierCandidate;

			struct SupportNormalizedCandidate
			{
				DiffConstExactCount count_if_carry0 {};
				DiffConstExactCount count_if_carry1 {};
				std::uint64_t relative_delta { 0 };
			};

			using SupportFrontierPair = std::array<std::vector<FrontierCandidate>, 2>;
			using SupportNormalizedFrontierPair = std::array<std::vector<SupportNormalizedCandidate>, 2>;

			struct SupportExactLayer
			{
				std::unordered_map<std::string, int> state_index_by_signature {};
				std::vector<SupportNormalizedFrontierPair> states {};
				std::vector<std::array<int, 4>> next_state_ids {};
			};

			struct SupportTransitionValue
			{
				SupportNormalizedFrontierPair next_frontier_by_carry_diff {};
			};

			struct SupportTransitionSlot
			{
				bool ambiguous { false };
				SupportNormalizedFrontierPair next_frontier_by_carry_diff {};
			};

			struct SupportCache
			{
				DiffConstFixedInputPrototypeSupportConfig config {};
				std::unordered_map<std::string, SupportTransitionValue> transitions {};
			};

			[[nodiscard]] inline constexpr int support_context_index(
				std::uint32_t input_bit,
				std::uint32_t constant_bit ) noexcept
			{
				return static_cast<int>( ( input_bit << 1 ) | constant_bit );
			}

			[[nodiscard]] inline DiffConstExactCount support_gcd_exact_count(
				DiffConstExactCount lhs,
				DiffConstExactCount rhs ) noexcept
			{
				while ( rhs != DiffConstExactCount {} )
				{
					const DiffConstExactCount remainder = lhs % rhs;
					lhs = rhs;
					rhs = remainder;
				}
				return lhs;
			}

			[[nodiscard]] inline std::string support_exact_count_to_text( const DiffConstExactCount& exact_count )
			{
				if ( exact_count.high64() == 0ull )
					return std::to_string( exact_count.low64() );
				std::string text = "0x";
				text += [] ( std::uint64_t high, std::uint64_t low ) {
					char buffer[ 40 ] {};
					std::snprintf(
						buffer,
						sizeof( buffer ),
						"%llx_%016llx",
						static_cast<unsigned long long>( high ),
						static_cast<unsigned long long>( low ) );
					return std::string( buffer );
				}( exact_count.high64(), exact_count.low64() );
				return text;
			}

			[[nodiscard]] inline DiffConstExactCount support_frontier_scale_gcd(
				const std::vector<SupportNormalizedCandidate>& frontier ) noexcept
			{
				DiffConstExactCount scale {};
				for ( const SupportNormalizedCandidate& candidate : frontier )
				{
					if ( candidate.count_if_carry0 != DiffConstExactCount {} )
					{
						scale =
							( scale == DiffConstExactCount {} )
								? candidate.count_if_carry0
								: support_gcd_exact_count( scale, candidate.count_if_carry0 );
					}
					if ( candidate.count_if_carry1 != DiffConstExactCount {} )
					{
						scale =
							( scale == DiffConstExactCount {} )
								? candidate.count_if_carry1
								: support_gcd_exact_count( scale, candidate.count_if_carry1 );
					}
				}
				if ( scale == DiffConstExactCount {} )
					scale = DiffConstExactCount { 1u };
				return scale;
			}

			[[nodiscard]] inline std::string support_exact_frontier_signature(
				const std::vector<SupportNormalizedCandidate>& frontier )
			{
				if ( frontier.empty() )
					return "empty";

				std::string signature = std::to_string( frontier.size() ) + "|";
				for ( std::size_t index = 0; index < frontier.size(); ++index )
				{
					if ( index != 0u )
						signature += ";";
					signature += support_exact_count_to_text( frontier[ index ].count_if_carry0 );
					signature += ":";
					signature += support_exact_count_to_text( frontier[ index ].count_if_carry1 );
					signature += "@0x";
					char buffer[ 17 ] {};
					std::snprintf( buffer, sizeof( buffer ), "%llx",
						static_cast<unsigned long long>( frontier[ index ].relative_delta ) );
					signature += buffer;
				}
				return signature;
			}

			[[nodiscard]] inline std::string support_exact_pair_signature(
				const SupportNormalizedFrontierPair& frontier_by_carry_diff )
			{
				return support_exact_frontier_signature( frontier_by_carry_diff[ 0u ] ) +
					   " || " +
					   support_exact_frontier_signature( frontier_by_carry_diff[ 1u ] );
			}

			[[nodiscard]] inline SupportFrontierPair make_initial_support_frontier_pair(
				std::uint64_t fixed_input_msb ) noexcept
			{
				SupportFrontierPair frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					FrontierCandidate candidate {};
					candidate.count_if_carry0 = 2ull;
					candidate.count_if_carry1 = 2ull;
					candidate.variable_delta =
						( fixed_input_msb ^ current_carry_difference_bit ) << 0;
					frontier_by_current_carry_difference[ current_carry_difference_bit ].push_back( candidate );
				}
				return frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair normalize_support_frontier_pair_relative(
				const SupportFrontierPair& frontier_by_carry_difference )
			{
				SupportNormalizedFrontierPair out {};
				for ( std::uint32_t carry_difference = 0u; carry_difference <= 1u; ++carry_difference )
				{
					out[ carry_difference ].reserve( frontier_by_carry_difference[ carry_difference ].size() );
					for ( const FrontierCandidate& candidate : frontier_by_carry_difference[ carry_difference ] )
					{
						SupportNormalizedCandidate normalized {};
						normalized.count_if_carry0 = candidate.count_if_carry0;
						normalized.count_if_carry1 = candidate.count_if_carry1;
						normalized.relative_delta = candidate.variable_delta;
						out[ carry_difference ].push_back( normalized );
					}
				}
				return out;
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair normalize_support_frontier_pair_absolute(
				const SupportFrontierPair& frontier_by_carry_difference,
				int total_width,
				int suffix_length )
			{
				SupportNormalizedFrontierPair out {};
				const int shift = total_width - suffix_length;
				for ( std::uint32_t carry_difference = 0u; carry_difference <= 1u; ++carry_difference )
				{
					out[ carry_difference ].reserve( frontier_by_carry_difference[ carry_difference ].size() );
					for ( const FrontierCandidate& candidate : frontier_by_carry_difference[ carry_difference ] )
					{
						SupportNormalizedCandidate normalized {};
						normalized.count_if_carry0 = candidate.count_if_carry0;
						normalized.count_if_carry1 = candidate.count_if_carry1;
						normalized.relative_delta =
							( shift <= 0 ) ? candidate.variable_delta : ( candidate.variable_delta >> shift );
						out[ carry_difference ].push_back( normalized );
					}
				}
				return out;
			}

			[[nodiscard]] inline SupportFrontierPair materialize_support_frontier_pair_relative(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference )
			{
				SupportFrontierPair out {};
				for ( std::uint32_t carry_difference = 0u; carry_difference <= 1u; ++carry_difference )
				{
					out[ carry_difference ].reserve( frontier_by_carry_difference[ carry_difference ].size() );
					for ( const SupportNormalizedCandidate& candidate : frontier_by_carry_difference[ carry_difference ] )
					{
						FrontierCandidate materialized {};
						materialized.count_if_carry0 = candidate.count_if_carry0;
						materialized.count_if_carry1 = candidate.count_if_carry1;
						materialized.variable_delta = candidate.relative_delta << 1;
						out[ carry_difference ].push_back( materialized );
					}
				}
				return out;
			}

			[[nodiscard]] inline SupportFrontierPair materialize_support_frontier_pair_absolute(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int total_width,
				int suffix_length )
			{
				SupportFrontierPair out {};
				const int shift = total_width - suffix_length;
				for ( std::uint32_t carry_difference = 0u; carry_difference <= 1u; ++carry_difference )
				{
					out[ carry_difference ].reserve( frontier_by_carry_difference[ carry_difference ].size() );
					for ( const SupportNormalizedCandidate& candidate : frontier_by_carry_difference[ carry_difference ] )
					{
						FrontierCandidate materialized {};
						materialized.count_if_carry0 = candidate.count_if_carry0;
						materialized.count_if_carry1 = candidate.count_if_carry1;
						materialized.variable_delta =
							( shift <= 0 ) ? candidate.relative_delta : ( candidate.relative_delta << shift );
						out[ carry_difference ].push_back( materialized );
					}
				}
				return out;
			}

			[[nodiscard]] inline bool support_frontier_pairs_equal(
				const SupportNormalizedFrontierPair& lhs,
				const SupportNormalizedFrontierPair& rhs )
			{
				return support_exact_pair_signature( lhs ) == support_exact_pair_signature( rhs );
			}

			[[nodiscard]] inline SupportFrontierPair exact_next_frontier_relative(
				const SupportFrontierPair& frontier_by_current_carry_difference,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit ) noexcept
			{
				SupportFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<FrontierCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const std::uint64_t output_difference_bit =
						input_difference_bit ^ current_carry_difference_bit;

					for ( std::uint32_t next_carry_difference_bit = 0u; next_carry_difference_bit <= 1u; ++next_carry_difference_bit )
					{
						const detail::AddConstCarryEdgeMatrix2 edge_matrix =
							detail::make_addconst_carry_edge_matrix(
								input_difference_bit,
								constant_bit,
								current_carry_difference_bit,
								next_carry_difference_bit );
						for ( const FrontierCandidate& child_candidate : frontier_by_current_carry_difference[ next_carry_difference_bit ] )
						{
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
								child_candidate.variable_delta | ( output_difference_bit << 0 );
							next_frontier.push_back( candidate );
						}
					}

					detail_diffconst_q2_common::prune_pareto_frontier(
						next_frontier,
						static_cast<DiffConstFixedInputQ2Stats*>( nullptr ) );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportFrontierPair exact_next_frontier_absolute(
				const SupportFrontierPair& frontier_by_current_carry_difference,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				int bit_index,
				DiffConstFixedInputQ2Stats* stats ) noexcept
			{
				SupportFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<FrontierCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const std::uint64_t output_difference_bit =
						input_difference_bit ^ current_carry_difference_bit;

					for ( std::uint32_t next_carry_difference_bit = 0u; next_carry_difference_bit <= 1u; ++next_carry_difference_bit )
					{
						const detail::AddConstCarryEdgeMatrix2 edge_matrix =
							detail::make_addconst_carry_edge_matrix(
								input_difference_bit,
								constant_bit,
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
								child_candidate.variable_delta | ( output_difference_bit << bit_index );
							next_frontier.push_back( candidate );
						}
					}

					detail_diffconst_q2_common::prune_pareto_frontier( next_frontier, stats );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline bool same_prototype_support_config(
				const DiffConstFixedInputPrototypeSupportConfig& lhs,
				const DiffConstFixedInputPrototypeSupportConfig& rhs ) noexcept
			{
				return lhs.support_max_suffix == rhs.support_max_suffix &&
					   lhs.suffix10_plus_upgrade_mask == rhs.suffix10_plus_upgrade_mask &&
					   lhs.suffix10_plus_include_carry1_full == rhs.suffix10_plus_include_carry1_full &&
					   lhs.suffix10_plus_anchor_bits == rhs.suffix10_plus_anchor_bits &&
					   lhs.suffix10_plus_residue_high_bits_floor == rhs.suffix10_plus_residue_high_bits_floor;
			}

			[[nodiscard]] inline DiffConstFixedInputPrototypeSupportConfig normalized_prototype_support_config(
				DiffConstFixedInputPrototypeSupportConfig config ) noexcept
			{
				if ( config.support_max_suffix < 1 )
					config.support_max_suffix = 1;
				if ( config.support_max_suffix > 15 )
					config.support_max_suffix = 15;
				config.suffix10_plus_upgrade_mask &= 0xFFu;
				if ( config.suffix10_plus_anchor_bits < 1 )
					config.suffix10_plus_anchor_bits = 1;
				if ( config.suffix10_plus_anchor_bits > 16 )
					config.suffix10_plus_anchor_bits = 16;
				if ( config.suffix10_plus_residue_high_bits_floor < 1 )
					config.suffix10_plus_residue_high_bits_floor = 1;
				if ( config.suffix10_plus_residue_high_bits_floor > 16 )
					config.suffix10_plus_residue_high_bits_floor = 16;
				return config;
			}

			[[nodiscard]] inline int support_residue_high_bits_for_suffix(
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config ) noexcept
			{
				if ( suffix_length <= 9 )
					return 6;
				return ( std::max )( config.suffix10_plus_residue_high_bits_floor, suffix_length - 3 );
			}

			[[nodiscard]] inline std::uint32_t support_residue_upgrade_mask_for_suffix(
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config ) noexcept
			{
				if ( suffix_length <= 7 )
					return 0x00u;
				if ( suffix_length == 8 )
					return 0xE2u;
				if ( suffix_length == 9 )
					return 0xEEu;
				return config.suffix10_plus_upgrade_mask & 0xFFu;
			}

			[[nodiscard]] inline std::string support_projection_signature(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				const std::vector<SupportNormalizedCandidate>& frontier = frontier_by_carry_difference[ 0u ];
				if ( frontier.empty() )
					return "empty##scale=1";

				std::array<std::vector<SupportNormalizedCandidate>, 8> buckets {};
				for ( const SupportNormalizedCandidate& candidate : frontier )
					buckets[ static_cast<std::size_t>( candidate.relative_delta & 0x7ull ) ].push_back( candidate );

				std::uint64_t global_min_delta = frontier.front().relative_delta;
				for ( const SupportNormalizedCandidate& candidate : frontier )
				{
					if ( candidate.relative_delta < global_min_delta )
						global_min_delta = candidate.relative_delta;
				}
				const std::uint64_t global_base_q = global_min_delta >> 3;
				const int phase_bits = 3;
				const int global_baseq_bits = suffix_length;
				const std::uint64_t global_baseq_mask =
					( global_baseq_bits >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << global_baseq_bits ) - 1ull );

				std::ostringstream out;
				const int anchor_bits = ( suffix_length >= 10 ) ? config.suffix10_plus_anchor_bits : 3;
				const std::uint64_t anchor_mask =
					( anchor_bits >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << anchor_bits ) - 1ull );
				out << "##anchor_low" << anchor_bits
					<< "=0x" << std::hex << ( global_min_delta & anchor_mask ) << std::dec;
				out << "##gq" << global_baseq_bits
					<< "=0x" << std::hex << ( global_base_q & global_baseq_mask ) << std::dec;

				const std::uint32_t upgrade_mask =
					support_residue_upgrade_mask_for_suffix( suffix_length, config );
				const int residue_high_bits =
					support_residue_high_bits_for_suffix( suffix_length, config );

				for ( std::size_t residue = 0; residue < buckets.size(); ++residue )
				{
					auto& bucket = buckets[ residue ];
					out << "|r" << residue << ":";
					if ( bucket.empty() )
					{
						out << "empty";
						continue;
					}

					DiffConstExactCount common_divisor {};
					for ( const SupportNormalizedCandidate& candidate : bucket )
					{
						if ( candidate.count_if_carry0 != DiffConstExactCount {} )
						{
							common_divisor =
								( common_divisor == DiffConstExactCount {} )
									? candidate.count_if_carry0
									: support_gcd_exact_count( common_divisor, candidate.count_if_carry0 );
						}
						if ( candidate.count_if_carry1 != DiffConstExactCount {} )
						{
							common_divisor =
								( common_divisor == DiffConstExactCount {} )
									? candidate.count_if_carry1
									: support_gcd_exact_count( common_divisor, candidate.count_if_carry1 );
						}
					}
					if ( common_divisor == DiffConstExactCount {} )
						common_divisor = DiffConstExactCount { 1u };

					std::sort(
						bucket.begin(),
						bucket.end(),
						[]( const SupportNormalizedCandidate& lhs, const SupportNormalizedCandidate& rhs ) noexcept
						{
							if ( lhs.relative_delta != rhs.relative_delta )
								return lhs.relative_delta < rhs.relative_delta;
							if ( lhs.count_if_carry0 != rhs.count_if_carry0 )
								return lhs.count_if_carry0 > rhs.count_if_carry0;
							return lhs.count_if_carry1 > rhs.count_if_carry1;
						} );

					const std::uint64_t bucket_min_q = bucket.front().relative_delta >> phase_bits;
					const std::uint64_t base_offset = bucket_min_q - global_base_q;
					int bits = 4;
					if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
						bits = residue_high_bits;
					const std::uint64_t mask =
						( bits >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << bits ) - 1ull );

					out << bucket.size();
					out << "##scale=" << support_exact_count_to_text( common_divisor );
					out << "##bqlow" << bits
						<< "=0x" << std::hex << ( base_offset & mask ) << std::dec;
					out << "|";
					for ( std::size_t index = 0; index < bucket.size(); ++index )
					{
						if ( index != 0u )
							out << ";";
						out
							<< support_exact_count_to_text( bucket[ index ].count_if_carry0 / common_divisor )
							<< ":"
							<< support_exact_count_to_text( bucket[ index ].count_if_carry1 / common_divisor )
							<< "@q0x" << std::hex
							<< ( ( bucket[ index ].relative_delta >> phase_bits ) - bucket_min_q )
							<< std::dec;
					}
				}

				return out.str();
			}

			[[nodiscard]] inline std::string support_class_key(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				std::string key =
					"s=" + std::to_string( suffix_length ) +
					"|" + support_projection_signature( frontier_by_carry_difference, suffix_length, config );
				if ( config.suffix10_plus_include_carry1_full && suffix_length >= 10 )
					key += "|carry1=" + support_exact_frontier_signature( frontier_by_carry_difference[ 1u ] );
				return key;
			}

			[[nodiscard]] inline std::string support_transition_key(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				return support_class_key( frontier_by_carry_difference, suffix_length, config ) +
					   "|input=" + std::to_string( input_difference_bit ) +
					   "|const=" + std::to_string( constant_bit );
			}

			inline void build_support_exact_layers(
				int max_precompute_suffix,
				std::vector<SupportExactLayer>& exact_layers )
			{
				exact_layers.assign( max_precompute_suffix + 1, SupportExactLayer {} );

				std::vector<SupportNormalizedFrontierPair> current_frontiers {};
				current_frontiers.reserve( 2 );
				for ( std::uint64_t fixed_input_msb = 0; fixed_input_msb <= 1; ++fixed_input_msb )
				{
					const SupportFrontierPair initial_frontier =
						make_initial_support_frontier_pair( fixed_input_msb );
					const SupportNormalizedFrontierPair normalized_initial =
						normalize_support_frontier_pair_relative( initial_frontier );
					const std::string signature =
						support_exact_pair_signature( normalized_initial );
					exact_layers[ 1 ].state_index_by_signature.emplace(
						signature,
						static_cast<int>( current_frontiers.size() ) );
					current_frontiers.push_back( normalized_initial );
				}
				exact_layers[ 1 ].states = current_frontiers;
				exact_layers[ 1 ].next_state_ids.resize( current_frontiers.size() );

				for ( int suffix_length = 1; suffix_length < max_precompute_suffix; ++suffix_length )
				{
					std::vector<SupportNormalizedFrontierPair> next_frontiers {};
					next_frontiers.reserve( current_frontiers.size() * 2u );
					exact_layers[ suffix_length ].next_state_ids.resize( current_frontiers.size() );

					for ( std::size_t state_index = 0; state_index < current_frontiers.size(); ++state_index )
					{
						const SupportFrontierPair current_frontier =
							materialize_support_frontier_pair_relative( current_frontiers[ state_index ] );
						for ( std::uint32_t input_difference_bit = 0u; input_difference_bit <= 1u; ++input_difference_bit )
						{
							for ( std::uint32_t constant_bit = 0u; constant_bit <= 1u; ++constant_bit )
							{
								const SupportFrontierPair next_frontier =
									exact_next_frontier_relative(
										current_frontier,
										input_difference_bit,
										constant_bit );
								const SupportNormalizedFrontierPair normalized_next =
									normalize_support_frontier_pair_relative( next_frontier );
								const std::string signature =
									support_exact_pair_signature( normalized_next );
								auto [ it, inserted ] =
									exact_layers[ suffix_length + 1 ].state_index_by_signature.emplace(
										signature,
										static_cast<int>( next_frontiers.size() ) );
								if ( inserted )
									next_frontiers.push_back( normalized_next );
								exact_layers[ suffix_length ].next_state_ids[ state_index ][ support_context_index( input_difference_bit, constant_bit ) ] =
									it->second;
							}
						}
					}

					exact_layers[ suffix_length + 1 ].states = next_frontiers;
					if ( suffix_length + 1 < max_precompute_suffix )
						exact_layers[ suffix_length + 1 ].next_state_ids.resize( next_frontiers.size() );
					current_frontiers.swap( next_frontiers );
				}
			}

			inline std::unordered_map<std::string, SupportTransitionValue> build_phase8_banded_support_cache_transitions(
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				std::vector<SupportExactLayer> exact_layers {};
				build_support_exact_layers( config.support_max_suffix + 1, exact_layers );

				std::unordered_map<std::string, SupportTransitionSlot> slots {};
				for ( int suffix_length = 1; suffix_length <= config.support_max_suffix; ++suffix_length )
				{
					const SupportExactLayer& layer = exact_layers[ suffix_length ];
					for ( std::size_t state_index = 0; state_index < layer.states.size(); ++state_index )
					{
						const std::string class_key =
							support_class_key( layer.states[ state_index ], suffix_length, config );
						for ( std::uint32_t input_difference_bit = 0u; input_difference_bit <= 1u; ++input_difference_bit )
						{
							for ( std::uint32_t constant_bit = 0u; constant_bit <= 1u; ++constant_bit )
							{
								const std::string key =
									class_key +
									"|input=" + std::to_string( input_difference_bit ) +
									"|const=" + std::to_string( constant_bit );
								const int next_state =
									layer.next_state_ids[ state_index ][ support_context_index( input_difference_bit, constant_bit ) ];
								const SupportNormalizedFrontierPair& normalized_next =
									exact_layers[ suffix_length + 1 ].states[ next_state ];
								auto [ it, inserted ] = slots.emplace( key, SupportTransitionSlot {} );
								if ( inserted )
								{
									it->second.next_frontier_by_carry_diff = normalized_next;
									continue;
								}
								if ( it->second.ambiguous )
									continue;
								if ( !support_frontier_pairs_equal( it->second.next_frontier_by_carry_diff, normalized_next ) )
									it->second.ambiguous = true;
							}
						}
					}
				}

				std::unordered_map<std::string, SupportTransitionValue> transitions {};
				transitions.reserve( slots.size() );
				for ( auto& [ key, slot ] : slots )
				{
					if ( slot.ambiguous )
						continue;
					SupportTransitionValue value {};
					value.next_frontier_by_carry_diff = slot.next_frontier_by_carry_diff;
					transitions.emplace( key, std::move( value ) );
				}
				return transitions;
			}

			[[nodiscard]] inline const SupportCache& get_or_build_phase8_banded_support_cache(
				DiffConstFixedInputPrototypeSupportConfig config )
			{
				config = normalized_prototype_support_config( config );
				static std::mutex cache_mutex {};
				static std::vector<SupportCache> caches {};

				std::lock_guard<std::mutex> guard { cache_mutex };
				for ( const SupportCache& cache : caches )
				{
					if ( same_prototype_support_config( cache.config, config ) )
						return cache;
				}

				SupportCache cache {};
				cache.config = config;
				cache.transitions = build_phase8_banded_support_cache_transitions( config );
				caches.push_back( std::move( cache ) );
				return caches.back();
			}

			[[nodiscard]] inline DiffConstOptimalOutputDeltaResult make_verified_output_result(
				std::uint64_t delta_x,
				std::uint64_t constant,
				std::uint64_t delta_y,
				int n,
				DiffConstQ2Operation operation ) noexcept
			{
				DiffConstOptimalOutputDeltaResult result {};
				if ( !detail_diffconst_q2_common::is_supported_width( n ) )
					return result;

				const std::uint64_t mask = detail_diffconst_q2_common::mask_n( n );
				const std::uint64_t input_difference = delta_x & mask;
				const std::uint64_t output_difference = delta_y & mask;
				const std::uint64_t add_constant =
					detail_diffconst_q2_common::normalize_add_constant_for_q2( operation, constant, n );
				const DiffConstExactCount exact_count =
					diff_addconst_exact_count_n( input_difference, add_constant, output_difference, n );

				result.delta_y = output_difference;
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

			[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_exact_reference(
				std::uint64_t delta_x,
				std::uint64_t constant,
				int n,
				DiffConstQ2Operation operation,
				DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
			{
				detail_diffconst_q2_common::clear_stats( stats );
				DiffConstOptimalOutputDeltaResult result {};
				if ( !detail_diffconst_q2_common::is_supported_width( n ) )
					return result;

				const std::uint64_t mask = detail_diffconst_q2_common::mask_n( n );
				const std::uint64_t fixed_input_difference = delta_x & mask;
				const std::uint64_t add_constant =
					detail_diffconst_q2_common::normalize_add_constant_for_q2( operation, constant, n );

				std::array<std::vector<FrontierCandidate>, 2> frontier_by_current_carry_difference {};
				const int most_significant_bit = n - 1;
				const std::uint64_t fixed_input_msb = ( fixed_input_difference >> most_significant_bit ) & 1ull;
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					FrontierCandidate candidate {};
					candidate.count_if_carry0 = 2ull;
					candidate.count_if_carry1 = 2ull;
					candidate.variable_delta =
						( fixed_input_msb ^ current_carry_difference_bit ) << most_significant_bit;
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
					const std::uint64_t input_difference_bit =
						( fixed_input_difference >> bit_index ) & 1ull;
					const std::uint64_t constant_bit = ( add_constant >> bit_index ) & 1ull;

					for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
					{
						std::vector<FrontierCandidate>& next_frontier =
							next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
						const std::uint64_t output_difference_bit =
							input_difference_bit ^ current_carry_difference_bit;

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
									( output_difference_bit << bit_index );
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
				return make_verified_output_result(
					fixed_input_difference,
					constant,
					best_candidate->variable_delta,
					n,
					operation );
			}

			[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_phase8_banded_support_prototype(
				std::uint64_t delta_x,
				std::uint64_t constant,
				int n,
				DiffConstQ2Operation operation,
				DiffConstFixedInputPrototypeSupportConfig prototype_config,
				DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
			{
				detail_diffconst_q2_common::clear_stats( stats );
				DiffConstOptimalOutputDeltaResult result {};
				if ( !detail_diffconst_q2_common::is_supported_width( n ) )
					return result;

				const DiffConstFixedInputPrototypeSupportConfig normalized_config =
					normalized_prototype_support_config( prototype_config );
				DiffConstFixedInputPrototypeSupportConfig effective_cache_config = normalized_config;
				if ( n <= 1 )
					effective_cache_config.support_max_suffix = 0;
				else
					effective_cache_config.support_max_suffix =
						( std::min )( effective_cache_config.support_max_suffix, n - 1 );

				if ( stats != nullptr )
					stats->used_phase8_banded_support_prototype = true;

				const std::uint64_t mask = detail_diffconst_q2_common::mask_n( n );
				const std::uint64_t fixed_input_difference = delta_x & mask;
				const std::uint64_t add_constant =
					detail_diffconst_q2_common::normalize_add_constant_for_q2( operation, constant, n );

				SupportFrontierPair frontier_by_current_carry_difference {};
				const int most_significant_bit = n - 1;
				const std::uint64_t fixed_input_msb = ( fixed_input_difference >> most_significant_bit ) & 1ull;
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					FrontierCandidate candidate {};
					candidate.count_if_carry0 = 2ull;
					candidate.count_if_carry1 = 2ull;
					candidate.variable_delta =
						( fixed_input_msb ^ current_carry_difference_bit ) << most_significant_bit;
					frontier_by_current_carry_difference[ current_carry_difference_bit ].push_back( candidate );
				}
				if ( stats != nullptr )
				{
					stats->frontier_generated_entries = 2u;
					stats->frontier_kept_entries = 2u;
					stats->frontier_peak_entries = 1u;
				}

				const SupportCache* cache = nullptr;
				if ( effective_cache_config.support_max_suffix >= 1 )
					cache = &get_or_build_phase8_banded_support_cache( effective_cache_config );

				for ( int bit_index = n - 2; bit_index >= 0; --bit_index )
				{
					const int child_suffix_length = n - ( bit_index + 1 );
					const std::uint32_t input_difference_bit =
						static_cast<std::uint32_t>( ( fixed_input_difference >> bit_index ) & 1ull );
					const std::uint32_t constant_bit =
						static_cast<std::uint32_t>( ( add_constant >> bit_index ) & 1ull );

					bool support_hit = false;
					if ( cache != nullptr && child_suffix_length <= effective_cache_config.support_max_suffix )
					{
						if ( stats != nullptr )
							++stats->prototype_transition_proposals;
						const SupportNormalizedFrontierPair normalized_current =
							normalize_support_frontier_pair_absolute(
								frontier_by_current_carry_difference,
								n,
								child_suffix_length );
						const std::string key =
							support_transition_key(
								normalized_current,
								child_suffix_length,
								input_difference_bit,
								constant_bit,
								effective_cache_config );
						const auto it = cache->transitions.find( key );
						if ( it != cache->transitions.end() )
						{
							frontier_by_current_carry_difference =
								materialize_support_frontier_pair_absolute(
									it->second.next_frontier_by_carry_diff,
									n,
									child_suffix_length + 1 );
							if ( stats != nullptr )
								++stats->prototype_transition_hits;
							support_hit = true;
						}
					}

					if ( support_hit )
						continue;

					if ( stats != nullptr && cache != nullptr && child_suffix_length <= effective_cache_config.support_max_suffix )
						++stats->prototype_transition_fallbacks;
					frontier_by_current_carry_difference =
						exact_next_frontier_absolute(
							frontier_by_current_carry_difference,
							input_difference_bit,
							constant_bit,
							bit_index,
							stats );
				}

				const std::vector<FrontierCandidate>& root_frontier =
					frontier_by_current_carry_difference[ 0u ];
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
				return make_verified_output_result(
					fixed_input_difference,
					constant,
					best_candidate->variable_delta,
					n,
					operation );
			}
		}  // namespace detail_diffconst_fixed_input_q2

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_addconst_exact_reference(
			std::uint64_t delta_x,
			std::uint64_t add_constant_K,
			int n,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_input_q2::find_optimal_output_delta_exact_reference(
				delta_x,
				add_constant_K,
				n,
				DiffConstQ2Operation::modular_add,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_subconst_exact_reference(
			std::uint64_t delta_x,
			std::uint64_t sub_constant_C,
			int n,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_input_q2::find_optimal_output_delta_exact_reference(
				delta_x,
				sub_constant_C,
				n,
				DiffConstQ2Operation::modular_sub,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_addconst_phase8_banded_support_prototype(
			std::uint64_t delta_x,
			std::uint64_t add_constant_K,
			int n,
			const DiffConstFixedInputPrototypeSupportConfig& prototype_config,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_input_q2::find_optimal_output_delta_phase8_banded_support_prototype(
				delta_x,
				add_constant_K,
				n,
				DiffConstQ2Operation::modular_add,
				prototype_config,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_subconst_phase8_banded_support_prototype(
			std::uint64_t delta_x,
			std::uint64_t sub_constant_C,
			int n,
			const DiffConstFixedInputPrototypeSupportConfig& prototype_config,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return detail_diffconst_fixed_input_q2::find_optimal_output_delta_phase8_banded_support_prototype(
				delta_x,
				sub_constant_C,
				n,
				DiffConstQ2Operation::modular_sub,
				prototype_config,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_addconst(
			std::uint64_t delta_x,
			std::uint64_t add_constant_K,
			int n,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return find_optimal_output_delta_addconst_exact_reference(
				delta_x,
				add_constant_K,
				n,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_addconst(
			std::uint64_t delta_x,
			std::uint64_t add_constant_K,
			int n,
			const DiffConstFixedInputPrototypeSupportConfig& prototype_config,
			DiffConstFixedInputQ2Stats* stats ) noexcept
		{
			return find_optimal_output_delta_addconst_phase8_banded_support_prototype(
				delta_x,
				add_constant_K,
				n,
				prototype_config,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_subconst(
			std::uint64_t delta_x,
			std::uint64_t sub_constant_C,
			int n,
			DiffConstFixedInputQ2Stats* stats = nullptr ) noexcept
		{
			return find_optimal_output_delta_subconst_exact_reference(
				delta_x,
				sub_constant_C,
				n,
				stats );
		}

		[[nodiscard]] inline DiffConstOptimalOutputDeltaResult find_optimal_output_delta_subconst(
			std::uint64_t delta_x,
			std::uint64_t sub_constant_C,
			int n,
			const DiffConstFixedInputPrototypeSupportConfig& prototype_config,
			DiffConstFixedInputQ2Stats* stats ) noexcept
		{
			return find_optimal_output_delta_subconst_phase8_banded_support_prototype(
				delta_x,
				sub_constant_C,
				n,
				prototype_config,
				stats );
		}
	}  // namespace arx_operators
}  // namespace TwilightDream
