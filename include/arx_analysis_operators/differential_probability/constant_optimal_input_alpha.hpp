#pragma once
/**
 * @file differential_probability/constant_optimal_input_alpha.hpp
 * @brief Fixed-input-delta public API for exact var-const Q2 differential probability.
 *
 * This header owns the fixed-input-delta generator problem:
 * - input  : delta_x
 * - output : optimal delta_y
 *
 * The current implementation is an exact fixed-input prototype backed by
 * phase8 banded support caching plus exact fallback and a Q1 verifier (`exact_count`).
 * Future candidate algorithms for this direction can diverge freely without
 * coupling to the fixed-output direction.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
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
			static constexpr std::size_t support_suffix_stat_slots = 65u;
			std::size_t transition_evaluations { 0 };
			std::size_t frontier_generated_entries { 0 };
			std::size_t frontier_kept_entries { 0 };
			std::size_t frontier_pruned_entries { 0 };
			std::size_t frontier_peak_entries { 0 };
			std::size_t prototype_transition_proposals { 0 };
			std::size_t prototype_transition_hits { 0 };
			std::size_t prototype_transition_fallbacks { 0 };
			std::size_t prototype_transition_key_misses { 0 };
			std::size_t prototype_transition_guard_misses { 0 };
			std::size_t prototype_transition_cache_inserts { 0 };
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_proposals_by_suffix {};
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_hits_by_suffix {};
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_fallbacks_by_suffix {};
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_key_misses_by_suffix {};
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_guard_misses_by_suffix {};
			std::array<std::size_t, support_suffix_stat_slots> prototype_transition_cache_inserts_by_suffix {};
			bool used_phase8_banded_support_prototype { false };
		};

		struct DiffConstFixedInputPrototypeSupportConfig
		{
			int support_max_suffix { 18 };
			std::uint32_t suffix10_plus_upgrade_mask { 0xEEu };
			// Retained for CLI/config compatibility; the current descriptor uses
			// carry1 projection instead of full carry1 frontier text.
			bool suffix10_plus_include_carry1_full { true };
			// Retained for CLI/config compatibility. The promoted descriptor no
			// longer uses an anchor field; residue buckets plus bqlow offsets
			// carry the transition-relevant delta information.
			int suffix10_plus_anchor_bits { 3 };
			// Default upgraded-residue bqlow width follows the exact quotient
			// audit threshold max(4, suffix-3); callers may raise this floor for
			// compatibility/probing, but the mainline no longer needs floor=8.
			int suffix10_plus_residue_high_bits_floor { 4 };
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

			struct SupportTaggedNormalizedCandidate
			{
				DiffConstExactCount count_if_carry0 {};
				DiffConstExactCount count_if_carry1 {};
				std::uint64_t relative_delta { 0 };
				std::uint8_t source_carry_difference { 0u };
			};

			struct SupportTransitionCandidateCore
			{
				std::uint64_t packed { 0ull };
			};

			using SupportFrontierPair = std::array<std::vector<FrontierCandidate>, 2>;
			using SupportNormalizedFrontierPair = std::array<std::vector<SupportNormalizedCandidate>, 2>;
			using SupportTaggedFrontierPair = std::array<std::vector<SupportTaggedNormalizedCandidate>, 2>;
			using SupportTransitionCandidatePool = std::array<std::vector<SupportTransitionCandidateCore>, 2>;
			using SupportTransitionPackedIndex = std::uint8_t;
			using SupportTransitionPackedRun = std::uint16_t;

			inline constexpr std::uint32_t support_transition_core_delta_bits = 19u;
			inline constexpr std::uint32_t support_transition_core_count_bits = 21u;
			inline constexpr std::uint32_t support_transition_core_delta_shift = 0u;
			inline constexpr std::uint32_t support_transition_core_count1_shift =
				support_transition_core_delta_bits;
			inline constexpr std::uint32_t support_transition_core_count0_shift =
				support_transition_core_delta_bits + support_transition_core_count_bits;
			inline constexpr std::uint64_t support_transition_core_delta_mask =
				( 1ull << support_transition_core_delta_bits ) - 1ull;
			inline constexpr std::uint64_t support_transition_core_count_mask =
				( 1ull << support_transition_core_count_bits ) - 1ull;
			static_assert(
				support_transition_core_delta_bits +
					2u * support_transition_core_count_bits <=
				64u );

			[[nodiscard]] inline std::uint64_t support_transition_core_pack(
				std::uint32_t count_if_carry0,
				std::uint32_t count_if_carry1,
				std::uint32_t relative_delta_without_output_bit )
			{
				if ( static_cast<std::uint64_t>( count_if_carry0 ) >
						 support_transition_core_count_mask ||
					 static_cast<std::uint64_t>( count_if_carry1 ) >
						 support_transition_core_count_mask ||
					 static_cast<std::uint64_t>( relative_delta_without_output_bit ) >
						 support_transition_core_delta_mask )
				{
					std::abort();
				}
				return
					( static_cast<std::uint64_t>( count_if_carry0 )
					  << support_transition_core_count0_shift ) |
					( static_cast<std::uint64_t>( count_if_carry1 )
					  << support_transition_core_count1_shift ) |
					( static_cast<std::uint64_t>( relative_delta_without_output_bit )
					  << support_transition_core_delta_shift );
			}

			[[nodiscard]] inline SupportTransitionCandidateCore support_transition_candidate_core_from_u32(
				std::uint32_t count_if_carry0,
				std::uint32_t count_if_carry1,
				std::uint32_t relative_delta_without_output_bit )
			{
				SupportTransitionCandidateCore core {};
				core.packed =
					support_transition_core_pack(
						count_if_carry0,
						count_if_carry1,
						relative_delta_without_output_bit );
				return core;
			}

			[[nodiscard]] inline std::uint32_t support_transition_core_count_if_carry0(
				const SupportTransitionCandidateCore& core ) noexcept
			{
				return static_cast<std::uint32_t>(
					( core.packed >> support_transition_core_count0_shift ) &
					support_transition_core_count_mask );
			}

			[[nodiscard]] inline std::uint32_t support_transition_core_count_if_carry1(
				const SupportTransitionCandidateCore& core ) noexcept
			{
				return static_cast<std::uint32_t>(
					( core.packed >> support_transition_core_count1_shift ) &
					support_transition_core_count_mask );
			}

			[[nodiscard]] inline std::uint32_t
			support_transition_core_relative_delta_without_output_bit(
				const SupportTransitionCandidateCore& core ) noexcept
			{
				return static_cast<std::uint32_t>(
					( core.packed >> support_transition_core_delta_shift ) &
					support_transition_core_delta_mask );
			}

			inline constexpr SupportTransitionPackedIndex support_transition_index_source_bit =
				static_cast<SupportTransitionPackedIndex>( 0x80u );
			inline constexpr SupportTransitionPackedIndex support_transition_index_local_mask =
				static_cast<SupportTransitionPackedIndex>( 0x7Fu );

			[[nodiscard]] inline SupportTransitionPackedIndex support_transition_pack_index(
				std::uint32_t source_carry_difference,
				std::uint32_t local_index )
			{
				if ( local_index >
					 static_cast<std::uint32_t>( support_transition_index_local_mask ) )
				{
					std::abort();
				}
				const std::uint32_t source_bit =
					( source_carry_difference & 1u ) == 0u
						? 0u
						: static_cast<std::uint32_t>( support_transition_index_source_bit );
				return static_cast<SupportTransitionPackedIndex>( source_bit | local_index );
			}

			[[nodiscard]] inline std::uint32_t support_transition_pool_source_carry_difference(
				SupportTransitionPackedIndex packed_index ) noexcept
			{
				return ( packed_index & support_transition_index_source_bit ) == 0u ? 0u : 1u;
			}

			[[nodiscard]] inline std::uint32_t support_transition_pool_local_index(
				SupportTransitionPackedIndex packed_index ) noexcept
			{
				return static_cast<std::uint32_t>(
					packed_index & support_transition_index_local_mask );
			}

			struct SupportTransitionIndexRun
			{
				std::array<std::uint8_t, 2> start_by_source {};
				std::array<std::uint8_t, 2> length_by_source {};
			};

			struct SupportExactLayer
			{
				std::unordered_map<std::uint64_t, std::vector<int>> state_indices_by_hash {};
				std::vector<SupportNormalizedFrontierPair> states {};
				std::vector<std::array<int, 4>> next_state_ids {};
			};

			struct SupportTransitionValue
			{
				std::array<SupportTransitionPackedRun, 2> run_by_carry_diff {};
			};

			struct SupportTransitionGroup
			{
				std::uint32_t candidate_core_offset { 0u };
				std::array<std::uint16_t, 2> source_global_base_q {};
				std::array<SupportTransitionValue, 4> by_context {};
				std::array<std::uint8_t, 2> candidate_core_lengths {};
			};

			struct SupportTransitionPendingGroup
			{
				SupportTransitionGroup group {};
				SupportTransitionCandidatePool candidate_pool {};
			};

			struct SupportCanonicalDescriptor
			{
				std::string key {};
				std::string projection_key {};
				std::uint16_t suffix_length { 0u };
				std::uint64_t hash { 0u };
				SupportNormalizedFrontierPair frontier {};
				bool swapped_count_orientation { false };
			};

			struct SupportDescriptorKeyView
			{
				std::string_view projection_key {};
				std::uint16_t suffix_length { 0u };
				bool structured { false };
			};

			struct SupportTransitionCacheKey
			{
				std::string projection_key {};
				std::uint16_t suffix_length { 0u };
			};

			struct SupportTransitionCacheEntry
			{
				std::uint32_t key_id { 0u };
				SupportTransitionGroup group {};
			};

			struct SupportCache
			{
				DiffConstFixedInputPrototypeSupportConfig config {};
				int descriptor_prewarm_max_suffix { 0 };
				int descriptor_compressed_prewarm_max_suffix { 0 };
				mutable std::mutex transitions_mutex {};
				mutable std::unordered_multimap<std::uint64_t, SupportTransitionCacheEntry> transitions {};
				mutable std::vector<SupportTransitionCacheKey> transition_keys {};
				mutable std::vector<SupportTransitionCandidateCore> transition_candidate_core_arena {};
			};

			[[nodiscard]] inline std::uint32_t support_transition_group_candidate_bucket_size(
				const SupportTransitionGroup& group,
				std::uint32_t source_carry_difference ) noexcept
			{
				return static_cast<std::uint32_t>(
					group.candidate_core_lengths[ source_carry_difference & 1u ] );
			}

			[[nodiscard]] inline const SupportTransitionCandidateCore& support_transition_group_candidate(
				const SupportCache& cache,
				const SupportTransitionGroup& group,
				SupportTransitionPackedIndex packed_index )
			{
				const std::uint32_t source_carry_difference =
					support_transition_pool_source_carry_difference( packed_index );
				const std::uint32_t candidate_index =
					support_transition_pool_local_index( packed_index );
				const std::uint32_t bucket_size =
					support_transition_group_candidate_bucket_size(
						group,
						source_carry_difference );
				if ( candidate_index >= bucket_size )
					std::abort();
				const std::uint32_t bucket_offset =
					source_carry_difference == 0u
						? 0u
						: support_transition_group_candidate_bucket_size( group, 0u );
				const std::uint64_t arena_index =
					static_cast<std::uint64_t>( group.candidate_core_offset ) +
					bucket_offset +
					candidate_index;
				if ( arena_index >= cache.transition_candidate_core_arena.size() )
					std::abort();
				return cache.transition_candidate_core_arena[
					static_cast<std::size_t>( arena_index ) ];
			}

			[[nodiscard]] inline constexpr int support_context_index(
				std::uint32_t input_bit,
				std::uint32_t constant_bit ) noexcept
			{
				return static_cast<int>( ( input_bit << 1 ) | constant_bit );
			}

			inline void record_support_suffix_stat(
				std::array<std::size_t, DiffConstFixedInputQ2Stats::support_suffix_stat_slots>& counters,
				int suffix_length ) noexcept
			{
				if ( suffix_length < 0 )
					return;
				const std::size_t index = static_cast<std::size_t>( suffix_length );
				if ( index < counters.size() )
					++counters[ index ];
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

			inline void append_support_exact_count_text(
				std::ostringstream& out,
				const DiffConstExactCount& exact_count )
			{
				char buffer[ 48 ] {};
				if ( exact_count.high64() == 0ull )
				{
					const auto result =
						std::to_chars(
							buffer,
							buffer + sizeof( buffer ),
							exact_count.low64() );
					out.write( buffer, result.ptr - buffer );
					return;
				}

				const int length =
					std::snprintf(
						buffer,
						sizeof( buffer ),
						"0x%llx_%016llx",
						static_cast<unsigned long long>( exact_count.high64() ),
						static_cast<unsigned long long>( exact_count.low64() ) );
				out.write( buffer, length );
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

			[[nodiscard]] inline constexpr std::uint64_t support_mix_hash64(
				std::uint64_t seed,
				std::uint64_t value ) noexcept
			{
				seed ^= value + 0x9E3779B97F4A7C15ull + ( seed << 6 ) + ( seed >> 2 );
				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_exact_count(
				const DiffConstExactCount& exact_count ) noexcept
			{
				std::uint64_t seed = 0xD6E8FEB86659FD93ull;
				seed = support_mix_hash64( seed, exact_count.high64() );
				seed = support_mix_hash64( seed, exact_count.low64() );
				return seed;
			}

			[[nodiscard]] inline bool support_normalized_candidate_equal(
				const SupportNormalizedCandidate& lhs,
				const SupportNormalizedCandidate& rhs ) noexcept
			{
				return lhs.relative_delta == rhs.relative_delta &&
					   lhs.count_if_carry0 == rhs.count_if_carry0 &&
					   lhs.count_if_carry1 == rhs.count_if_carry1;
			}

			[[nodiscard]] inline bool support_normalized_frontier_equal(
				const std::vector<SupportNormalizedCandidate>& lhs,
				const std::vector<SupportNormalizedCandidate>& rhs ) noexcept
			{
				if ( lhs.size() != rhs.size() )
					return false;
				for ( std::size_t index = 0; index < lhs.size(); ++index )
				{
					if ( !support_normalized_candidate_equal( lhs[ index ], rhs[ index ] ) )
						return false;
				}
				return true;
			}

			[[nodiscard]] inline std::uint64_t support_hash_normalized_candidate(
				const SupportNormalizedCandidate& candidate ) noexcept
			{
				std::uint64_t seed = 0x243F6A8885A308D3ull;
				seed = support_mix_hash64( seed, support_hash_exact_count( candidate.count_if_carry0 ) );
				seed = support_mix_hash64( seed, support_hash_exact_count( candidate.count_if_carry1 ) );
				seed = support_mix_hash64( seed, candidate.relative_delta );
				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_normalized_frontier(
				const std::vector<SupportNormalizedCandidate>& frontier ) noexcept
			{
				std::uint64_t seed = support_mix_hash64( 0x13198A2E03707344ull, frontier.size() );
				for ( const SupportNormalizedCandidate& candidate : frontier )
					seed = support_mix_hash64( seed, support_hash_normalized_candidate( candidate ) );
				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_frontier_pair(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference ) noexcept
			{
				std::uint64_t seed = 0xA4093822299F31D0ull;
				seed = support_mix_hash64(
					seed,
					support_hash_normalized_frontier( frontier_by_carry_difference[ 0u ] ) );
				seed = support_mix_hash64(
					seed,
					support_hash_normalized_frontier( frontier_by_carry_difference[ 1u ] ) );
				return seed;
			}

			[[nodiscard]] inline bool support_frontier_pairs_equal(
				const SupportNormalizedFrontierPair& lhs,
				const SupportNormalizedFrontierPair& rhs ) noexcept
			{
				return support_normalized_frontier_equal( lhs[ 0u ], rhs[ 0u ] ) &&
					   support_normalized_frontier_equal( lhs[ 1u ], rhs[ 1u ] );
			}

			[[nodiscard]] inline std::uint64_t support_frontier_global_base_q(
				const std::vector<SupportNormalizedCandidate>& frontier ) noexcept
			{
				if ( frontier.empty() )
					return 0ull;
				std::uint64_t global_min_delta = frontier.front().relative_delta;
				for ( const SupportNormalizedCandidate& candidate : frontier )
				{
					if ( candidate.relative_delta < global_min_delta )
						global_min_delta = candidate.relative_delta;
				}
				return global_min_delta >> 3;
			}

			[[nodiscard]] inline std::array<std::uint64_t, 2> support_frontier_global_base_q_pair(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference ) noexcept
			{
				return {
					support_frontier_global_base_q( frontier_by_carry_difference[ 0u ] ),
					support_frontier_global_base_q( frontier_by_carry_difference[ 1u ] )
				};
			}

			[[nodiscard]] inline std::uint32_t support_u32_from_exact_count(
				const DiffConstExactCount& count )
			{
				if ( count.high64() != 0u ||
					 count.low64() > std::numeric_limits<std::uint32_t>::max() )
				{
					std::abort();
				}
				return static_cast<std::uint32_t>( count.low64() );
			}

			[[nodiscard]] inline std::uint32_t support_u32_from_u64(
				std::uint64_t value )
			{
				if ( value > std::numeric_limits<std::uint32_t>::max() )
					std::abort();
				return static_cast<std::uint32_t>( value );
			}

			[[nodiscard]] inline std::uint16_t support_u16_from_u64(
				std::uint64_t value )
			{
				if ( value > std::numeric_limits<std::uint16_t>::max() )
					std::abort();
				return static_cast<std::uint16_t>( value );
			}

			[[nodiscard]] inline std::array<std::uint16_t, 2> support_frontier_global_base_q_pair_u16(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference )
			{
				const std::array<std::uint64_t, 2> base_q =
					support_frontier_global_base_q_pair( frontier_by_carry_difference );
				return {
					support_u16_from_u64( base_q[ 0u ] ),
					support_u16_from_u64( base_q[ 1u ] )
				};
			}

			[[nodiscard]] inline int find_or_insert_support_exact_state(
				SupportExactLayer& layer,
				const SupportNormalizedFrontierPair& normalized_state,
				std::vector<SupportNormalizedFrontierPair>& state_store )
			{
				const std::uint64_t state_hash = support_hash_frontier_pair( normalized_state );
				std::vector<int>& bucket = layer.state_indices_by_hash[ state_hash ];
				for ( int state_id : bucket )
				{
					if ( support_frontier_pairs_equal(
							 state_store[ static_cast<std::size_t>( state_id ) ],
							 normalized_state ) )
					{
						return state_id;
					}
				}

				const int state_id = static_cast<int>( state_store.size() );
				state_store.push_back( normalized_state );
				bucket.push_back( state_id );
				return state_id;
			}

			inline void prune_support_normalized_frontier_in_place(
				std::vector<SupportNormalizedCandidate>& candidates ) noexcept
			{
				if ( candidates.empty() )
					return;

				std::sort(
					candidates.begin(),
					candidates.end(),
					[]( const SupportNormalizedCandidate& lhs, const SupportNormalizedCandidate& rhs ) noexcept
					{
						if ( lhs.count_if_carry0 != rhs.count_if_carry0 )
							return lhs.count_if_carry0 > rhs.count_if_carry0;
						if ( lhs.count_if_carry1 != rhs.count_if_carry1 )
							return lhs.count_if_carry1 > rhs.count_if_carry1;
						return lhs.relative_delta < rhs.relative_delta;
					} );

				std::vector<SupportNormalizedCandidate> deduplicated {};
				deduplicated.reserve( candidates.size() );
				for ( const SupportNormalizedCandidate& candidate : candidates )
				{
					if ( !deduplicated.empty() &&
						 deduplicated.back().count_if_carry0 == candidate.count_if_carry0 &&
						 deduplicated.back().count_if_carry1 == candidate.count_if_carry1 )
					{
						if ( candidate.relative_delta < deduplicated.back().relative_delta )
							deduplicated.back() = candidate;
						continue;
					}
					deduplicated.push_back( candidate );
				}

				std::vector<SupportNormalizedCandidate> pruned {};
				pruned.reserve( deduplicated.size() );
				DiffConstExactCount best_count_if_carry1_from_strictly_greater_count_if_carry0 { 0 };
				bool has_strictly_greater_group = false;

				std::size_t group_begin = 0u;
				while ( group_begin < deduplicated.size() )
				{
					std::size_t group_end = group_begin + 1u;
					while ( group_end < deduplicated.size() &&
							deduplicated[ group_end ].count_if_carry0 == deduplicated[ group_begin ].count_if_carry0 )
					{
						++group_end;
					}

					DiffConstExactCount group_max_count_if_carry1 { 0 };
					for ( std::size_t index = group_begin; index < group_end; ++index )
					{
						const SupportNormalizedCandidate& candidate = deduplicated[ index ];
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
			}

			[[nodiscard]] inline std::uint64_t translate_support_relative_delta_by_base_q_delta(
				std::uint64_t relative_delta,
				std::int64_t base_q_delta ) noexcept
			{
				if ( base_q_delta >= 0 )
					return relative_delta + ( static_cast<std::uint64_t>( base_q_delta ) << 4 );
				return relative_delta - ( static_cast<std::uint64_t>( -base_q_delta ) << 4 );
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair swap_support_count_coordinates(
				const SupportNormalizedFrontierPair& input )
			{
				SupportNormalizedFrontierPair output {};
				for ( std::uint32_t carry_difference = 0u; carry_difference <= 1u; ++carry_difference )
				{
					output[ carry_difference ].reserve( input[ carry_difference ].size() );
					for ( const SupportNormalizedCandidate& candidate : input[ carry_difference ] )
					{
						SupportNormalizedCandidate swapped {};
						swapped.count_if_carry0 = candidate.count_if_carry1;
						swapped.count_if_carry1 = candidate.count_if_carry0;
						swapped.relative_delta = candidate.relative_delta;
						output[ carry_difference ].push_back( swapped );
					}
					prune_support_normalized_frontier_in_place( output[ carry_difference ] );
				}
				return output;
			}

			[[nodiscard]] inline SupportTaggedFrontierPair make_tagged_raw_next_frontier_relative(
				const SupportNormalizedFrontierPair& frontier_by_current_carry_difference,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit ) noexcept
			{
				SupportTaggedFrontierPair raw_next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportTaggedNormalizedCandidate>& raw_next_frontier =
						raw_next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					raw_next_frontier.reserve(
						frontier_by_current_carry_difference[ 0u ].size() +
						frontier_by_current_carry_difference[ 1u ].size() );
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
						for ( const SupportNormalizedCandidate& child_candidate : frontier_by_current_carry_difference[ next_carry_difference_bit ] )
						{
							SupportTaggedNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 0 ] ) * child_candidate.count_if_carry0 +
								static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 1 ] ) * child_candidate.count_if_carry1;
							candidate.count_if_carry1 =
								static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 0 ] ) * child_candidate.count_if_carry0 +
								static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 1 ] ) * child_candidate.count_if_carry1;
							if ( ( candidate.count_if_carry0 | candidate.count_if_carry1 ) == 0ull )
								continue;
							candidate.relative_delta =
								( child_candidate.relative_delta << 1 ) | output_difference_bit;
							candidate.source_carry_difference =
								static_cast<std::uint8_t>( next_carry_difference_bit );
							raw_next_frontier.push_back( candidate );
						}
					}
				}
				return raw_next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline bool support_transition_candidate_core_equal(
				const SupportTransitionCandidateCore& lhs,
				const SupportTransitionCandidateCore& rhs ) noexcept
			{
				return lhs.packed == rhs.packed;
			}

			[[nodiscard]] inline SupportTransitionPackedIndex support_transition_pool_index(
				SupportTransitionCandidatePool& pool,
				const SupportTransitionCandidateCore& core,
				std::uint32_t source_carry_difference )
			{
				std::vector<SupportTransitionCandidateCore>& bucket =
					pool[ source_carry_difference & 1u ];
				for ( std::uint32_t index = 0u;
					  index < static_cast<std::uint32_t>( bucket.size() );
					  ++index )
				{
					if ( support_transition_candidate_core_equal( bucket[ index ], core ) )
						return support_transition_pack_index(
							source_carry_difference,
							index );
				}
				bucket.push_back( core );
				return support_transition_pack_index(
					source_carry_difference,
					static_cast<std::uint32_t>( bucket.size() - 1u ) );
			}

			[[nodiscard]] inline const SupportTransitionCandidateCore& support_transition_pool_candidate(
				const SupportTransitionCandidatePool& pool,
				SupportTransitionPackedIndex packed_index ) noexcept
			{
				const std::uint32_t source_carry_difference =
					support_transition_pool_source_carry_difference( packed_index );
				const std::uint32_t candidate_index =
					support_transition_pool_local_index( packed_index );
				return pool[ source_carry_difference ][ static_cast<std::size_t>( candidate_index ) ];
			}

			[[nodiscard]] inline std::uint32_t support_transition_checked_arena_offset(
				std::size_t offset )
			{
				if ( offset >
					 static_cast<std::size_t>( std::numeric_limits<std::uint32_t>::max() ) )
				{
					std::abort();
				}
				return static_cast<std::uint32_t>( offset );
			}

			[[nodiscard]] inline std::uint8_t support_transition_checked_arena_length(
				std::size_t length )
			{
				if ( length >
					 static_cast<std::size_t>( std::numeric_limits<std::uint8_t>::max() ) )
				{
					std::abort();
				}
				return static_cast<std::uint8_t>( length );
			}

			inline constexpr std::uint32_t support_transition_run_start_bits = 5u;
			inline constexpr std::uint32_t support_transition_run_length_bits = 3u;
			inline constexpr std::uint32_t support_transition_run_start_mask =
				( 1u << support_transition_run_start_bits ) - 1u;
			inline constexpr std::uint32_t support_transition_run_length_mask =
				( 1u << support_transition_run_length_bits ) - 1u;

			[[nodiscard]] inline SupportTransitionPackedRun support_transition_pack_run(
				const SupportTransitionIndexRun& run )
			{
				std::uint32_t packed = 0u;
				for ( std::uint32_t source_carry_difference = 0u;
					  source_carry_difference <= 1u;
					  ++source_carry_difference )
				{
					const std::uint32_t start =
						run.start_by_source[ source_carry_difference ];
					const std::uint32_t length =
						run.length_by_source[ source_carry_difference ];
					if ( start > support_transition_run_start_mask ||
						 length > support_transition_run_length_mask )
					{
						std::abort();
					}
					if ( length != 0u &&
						 start + length - 1u > support_transition_run_start_mask )
					{
						std::abort();
					}
					const std::uint32_t shift =
						source_carry_difference *
						( support_transition_run_start_bits +
						  support_transition_run_length_bits );
					packed |= start << shift;
					packed |= length << ( shift + support_transition_run_start_bits );
				}
				return static_cast<SupportTransitionPackedRun>( packed );
			}

			[[nodiscard]] inline SupportTransitionIndexRun support_transition_unpack_run(
				SupportTransitionPackedRun packed_run ) noexcept
			{
				SupportTransitionIndexRun run {};
				for ( std::uint32_t source_carry_difference = 0u;
					  source_carry_difference <= 1u;
					  ++source_carry_difference )
				{
					const std::uint32_t shift =
						source_carry_difference *
						( support_transition_run_start_bits +
						  support_transition_run_length_bits );
					run.start_by_source[ source_carry_difference ] =
						static_cast<std::uint8_t>(
							( packed_run >> shift ) &
							support_transition_run_start_mask );
					run.length_by_source[ source_carry_difference ] =
						static_cast<std::uint8_t>(
							( packed_run >> ( shift + support_transition_run_start_bits ) ) &
							support_transition_run_length_mask );
				}
				return run;
			}

			[[nodiscard]] inline std::uint32_t support_transition_run_length(
				const SupportTransitionIndexRun& run ) noexcept
			{
				return static_cast<std::uint32_t>( run.length_by_source[ 0u ] ) +
					   static_cast<std::uint32_t>( run.length_by_source[ 1u ] );
			}

			[[nodiscard]] inline SupportTransitionIndexRun support_transition_run_from_indices(
				const std::vector<SupportTransitionPackedIndex>& indices )
			{
				SupportTransitionIndexRun run {};
				std::array<bool, 2> seen_source {};
				std::uint32_t previous_source = 0u;
				bool has_previous_source = false;
				for ( const SupportTransitionPackedIndex packed_index : indices )
				{
					const std::uint32_t source_carry_difference =
						support_transition_pool_source_carry_difference( packed_index );
					const std::uint32_t local_index =
						support_transition_pool_local_index( packed_index );
					if ( has_previous_source && source_carry_difference < previous_source )
						std::abort();
					has_previous_source = true;
					previous_source = source_carry_difference;
					if ( !seen_source[ source_carry_difference ] )
					{
						if ( local_index > support_transition_run_start_mask )
							std::abort();
						run.start_by_source[ source_carry_difference ] =
							static_cast<std::uint8_t>( local_index );
						run.length_by_source[ source_carry_difference ] = 1u;
						seen_source[ source_carry_difference ] = true;
						continue;
					}
					const std::uint32_t expected_local_index =
						static_cast<std::uint32_t>(
							run.start_by_source[ source_carry_difference ] ) +
						run.length_by_source[ source_carry_difference ];
					if ( local_index != expected_local_index )
						std::abort();
					if ( run.length_by_source[ source_carry_difference ] >=
						 support_transition_run_length_mask )
					{
						std::abort();
					}
					++run.length_by_source[ source_carry_difference ];
				}
				( void )support_transition_pack_run( run );
				return run;
			}

			[[nodiscard]] inline SupportTransitionIndexRun support_transition_value_index_run(
				const SupportTransitionValue& value,
				std::uint32_t current_carry_difference_bit ) noexcept
			{
				return support_transition_unpack_run(
					value.run_by_carry_diff[ current_carry_difference_bit & 1u ] );
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair materialize_support_tagged_transition_relative(
				const SupportTransitionValue& transition_value,
				const SupportTransitionCandidatePool& pool,
				std::uint32_t input_difference_bit,
				const std::array<std::uint16_t, 2>& transition_source_global_base_q,
				const std::array<std::uint64_t, 2>& current_source_global_base_q )
			{
				SupportNormalizedFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportNormalizedCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const SupportTransitionIndexRun raw_next_indices =
						support_transition_value_index_run(
							transition_value,
							current_carry_difference_bit );
					next_frontier.reserve( support_transition_run_length( raw_next_indices ) );
					const std::uint64_t output_difference_bit =
						static_cast<std::uint64_t>( input_difference_bit & 1u ) ^
						current_carry_difference_bit;
					for ( std::uint32_t source_carry_difference = 0u;
						  source_carry_difference <= 1u;
						  ++source_carry_difference )
					{
						for ( std::uint32_t i = 0u;
							  i < raw_next_indices.length_by_source[ source_carry_difference ];
							  ++i )
						{
							const SupportTransitionPackedIndex candidate_index =
								support_transition_pack_index(
									source_carry_difference,
									static_cast<std::uint32_t>(
										raw_next_indices.start_by_source[ source_carry_difference ] ) +
										i );
							const SupportTransitionCandidateCore& tagged_candidate =
								support_transition_pool_candidate( pool, candidate_index );
							const std::size_t source_carry_index =
								static_cast<std::size_t>( source_carry_difference );
							const std::int64_t source_base_q_delta =
								static_cast<std::int64_t>( current_source_global_base_q[ source_carry_index ] ) -
								static_cast<std::int64_t>( transition_source_global_base_q[ source_carry_index ] );
							SupportNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								support_transition_core_count_if_carry0( tagged_candidate );
							candidate.count_if_carry1 =
								support_transition_core_count_if_carry1( tagged_candidate );
							candidate.relative_delta =
								translate_support_relative_delta_by_base_q_delta(
									( static_cast<std::uint64_t>(
										  support_transition_core_relative_delta_without_output_bit(
											  tagged_candidate ) ) << 1 ) |
										output_difference_bit,
									source_base_q_delta );
							next_frontier.push_back( candidate );
						}
					}
					prune_support_normalized_frontier_in_place( next_frontier );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair materialize_support_tagged_transition_relative_no_shift(
				const SupportTransitionValue& transition_value,
				const SupportTransitionCandidatePool& pool,
				std::uint32_t input_difference_bit )
			{
				SupportNormalizedFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportNormalizedCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const SupportTransitionIndexRun raw_next_indices =
						support_transition_value_index_run(
							transition_value,
							current_carry_difference_bit );
					next_frontier.reserve( support_transition_run_length( raw_next_indices ) );
					const std::uint64_t output_difference_bit =
						static_cast<std::uint64_t>( input_difference_bit & 1u ) ^
						current_carry_difference_bit;
					for ( std::uint32_t source_carry_difference = 0u;
						  source_carry_difference <= 1u;
						  ++source_carry_difference )
					{
						for ( std::uint32_t i = 0u;
							  i < raw_next_indices.length_by_source[ source_carry_difference ];
							  ++i )
						{
							const SupportTransitionPackedIndex candidate_index =
								support_transition_pack_index(
									source_carry_difference,
									static_cast<std::uint32_t>(
										raw_next_indices.start_by_source[ source_carry_difference ] ) +
										i );
							const SupportTransitionCandidateCore& tagged_candidate =
								support_transition_pool_candidate( pool, candidate_index );
							SupportNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								support_transition_core_count_if_carry0( tagged_candidate );
							candidate.count_if_carry1 =
								support_transition_core_count_if_carry1( tagged_candidate );
							candidate.relative_delta =
								( static_cast<std::uint64_t>(
									  support_transition_core_relative_delta_without_output_bit(
										  tagged_candidate ) ) << 1 ) |
								output_difference_bit;
							next_frontier.push_back( candidate );
						}
					}
					prune_support_normalized_frontier_in_place( next_frontier );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair materialize_support_tagged_transition_relative(
				const SupportCache& cache,
				const SupportTransitionGroup& transition_group,
				const SupportTransitionValue& transition_value,
				std::uint32_t input_difference_bit,
				const std::array<std::uint16_t, 2>& transition_source_global_base_q,
				const std::array<std::uint64_t, 2>& current_source_global_base_q )
			{
				SupportNormalizedFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportNormalizedCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const SupportTransitionIndexRun raw_next_indices =
						support_transition_value_index_run(
							transition_value,
							current_carry_difference_bit );
					next_frontier.reserve( support_transition_run_length( raw_next_indices ) );
					const std::uint64_t output_difference_bit =
						static_cast<std::uint64_t>( input_difference_bit & 1u ) ^
						current_carry_difference_bit;
					for ( std::uint32_t source_carry_difference = 0u;
						  source_carry_difference <= 1u;
						  ++source_carry_difference )
					{
						for ( std::uint32_t i = 0u;
							  i < raw_next_indices.length_by_source[ source_carry_difference ];
							  ++i )
						{
							const SupportTransitionPackedIndex candidate_index =
								support_transition_pack_index(
									source_carry_difference,
									static_cast<std::uint32_t>(
										raw_next_indices.start_by_source[ source_carry_difference ] ) +
										i );
							const SupportTransitionCandidateCore& tagged_candidate =
								support_transition_group_candidate(
									cache,
									transition_group,
									candidate_index );
							const std::size_t source_carry_index =
								static_cast<std::size_t>( source_carry_difference );
							const std::int64_t source_base_q_delta =
								static_cast<std::int64_t>( current_source_global_base_q[ source_carry_index ] ) -
								static_cast<std::int64_t>( transition_source_global_base_q[ source_carry_index ] );
							SupportNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								support_transition_core_count_if_carry0( tagged_candidate );
							candidate.count_if_carry1 =
								support_transition_core_count_if_carry1( tagged_candidate );
							candidate.relative_delta =
								translate_support_relative_delta_by_base_q_delta(
									( static_cast<std::uint64_t>(
										  support_transition_core_relative_delta_without_output_bit(
											  tagged_candidate ) ) << 1 ) |
										output_difference_bit,
									source_base_q_delta );
							next_frontier.push_back( candidate );
						}
					}
					prune_support_normalized_frontier_in_place( next_frontier );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair materialize_support_tagged_transition_relative_no_shift(
				const SupportCache& cache,
				const SupportTransitionGroup& transition_group,
				const SupportTransitionValue& transition_value,
				std::uint32_t input_difference_bit )
			{
				SupportNormalizedFrontierPair next_frontier_by_current_carry_difference {};
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportNormalizedCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					const SupportTransitionIndexRun raw_next_indices =
						support_transition_value_index_run(
							transition_value,
							current_carry_difference_bit );
					next_frontier.reserve( support_transition_run_length( raw_next_indices ) );
					const std::uint64_t output_difference_bit =
						static_cast<std::uint64_t>( input_difference_bit & 1u ) ^
						current_carry_difference_bit;
					for ( std::uint32_t source_carry_difference = 0u;
						  source_carry_difference <= 1u;
						  ++source_carry_difference )
					{
						for ( std::uint32_t i = 0u;
							  i < raw_next_indices.length_by_source[ source_carry_difference ];
							  ++i )
						{
							const SupportTransitionPackedIndex candidate_index =
								support_transition_pack_index(
									source_carry_difference,
									static_cast<std::uint32_t>(
										raw_next_indices.start_by_source[ source_carry_difference ] ) +
										i );
							const SupportTransitionCandidateCore& tagged_candidate =
								support_transition_group_candidate(
									cache,
									transition_group,
									candidate_index );
							SupportNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								support_transition_core_count_if_carry0( tagged_candidate );
							candidate.count_if_carry1 =
								support_transition_core_count_if_carry1( tagged_candidate );
							candidate.relative_delta =
								( static_cast<std::uint64_t>(
									  support_transition_core_relative_delta_without_output_bit(
										  tagged_candidate ) ) << 1 ) |
								output_difference_bit;
							next_frontier.push_back( candidate );
						}
					}
					prune_support_normalized_frontier_in_place( next_frontier );
				}
				return next_frontier_by_current_carry_difference;
			}

			[[nodiscard]] inline SupportTransitionValue make_support_transition_value_from_descriptor_source(
				const SupportNormalizedFrontierPair& source_frontier,
				SupportTransitionCandidatePool& candidate_pool,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit )
			{
				SupportTransitionValue value {};
				const SupportTaggedFrontierPair raw_next =
					make_tagged_raw_next_frontier_relative(
						source_frontier,
						input_difference_bit,
						constant_bit );
				for ( std::uint32_t current_carry_difference_bit = 0u;
					  current_carry_difference_bit <= 1u;
					  ++current_carry_difference_bit )
				{
					const auto& raw_frontier = raw_next[ current_carry_difference_bit ];
					std::vector<SupportTransitionPackedIndex> range_indices {};
					range_indices.reserve( raw_frontier.size() );
					const std::uint64_t output_difference_bit =
						input_difference_bit ^ current_carry_difference_bit;
					for ( const SupportTaggedNormalizedCandidate& candidate : raw_frontier )
					{
						const SupportTransitionCandidateCore core =
							support_transition_candidate_core_from_u32(
								support_u32_from_exact_count( candidate.count_if_carry0 ),
								support_u32_from_exact_count( candidate.count_if_carry1 ),
								support_u32_from_u64(
									( candidate.relative_delta ^ output_difference_bit ) >> 1 ) );
						range_indices.push_back(
							support_transition_pool_index(
								candidate_pool,
								core,
								candidate.source_carry_difference ) );
					}
					value.run_by_carry_diff[ current_carry_difference_bit ] =
						support_transition_pack_run(
							support_transition_run_from_indices( range_indices ) );
				}
				return value;
			}

			[[nodiscard]] inline SupportTransitionPendingGroup make_support_transition_group_from_descriptor_source(
				const SupportNormalizedFrontierPair& source_frontier )
			{
				SupportTransitionPendingGroup pending {};
				pending.group.source_global_base_q =
					support_frontier_global_base_q_pair_u16( source_frontier );
				for ( std::uint32_t input_difference_bit = 0u; input_difference_bit <= 1u; ++input_difference_bit )
				{
					for ( std::uint32_t constant_bit = 0u; constant_bit <= 1u; ++constant_bit )
					{
						pending.group.by_context[ static_cast<std::size_t>( support_context_index( input_difference_bit, constant_bit ) ) ] =
							make_support_transition_value_from_descriptor_source(
								source_frontier,
								pending.candidate_pool,
								input_difference_bit,
								constant_bit );
					}
				}
				return pending;
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
					next_frontier.reserve(
						frontier_by_current_carry_difference[ 0u ].size() +
						frontier_by_current_carry_difference[ 1u ].size() );
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

			inline void exact_next_normalized_frontier_relative_inplace(
				const SupportNormalizedFrontierPair& frontier_by_current_carry_difference,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				SupportNormalizedFrontierPair& next_frontier_by_current_carry_difference ) noexcept
			{
				for ( std::uint32_t current_carry_difference_bit = 0u; current_carry_difference_bit <= 1u; ++current_carry_difference_bit )
				{
					std::vector<SupportNormalizedCandidate>& next_frontier =
						next_frontier_by_current_carry_difference[ current_carry_difference_bit ];
					next_frontier.clear();
					next_frontier.reserve(
						frontier_by_current_carry_difference[ 0u ].size() +
						frontier_by_current_carry_difference[ 1u ].size() );
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
						for ( const SupportNormalizedCandidate& child_candidate : frontier_by_current_carry_difference[ next_carry_difference_bit ] )
						{
							SupportNormalizedCandidate candidate {};
							candidate.count_if_carry0 =
								static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 0 ] ) * child_candidate.count_if_carry0 +
								static_cast<std::uint64_t>( edge_matrix.entry[ 0 ][ 1 ] ) * child_candidate.count_if_carry1;
							candidate.count_if_carry1 =
								static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 0 ] ) * child_candidate.count_if_carry0 +
								static_cast<std::uint64_t>( edge_matrix.entry[ 1 ][ 1 ] ) * child_candidate.count_if_carry1;
							if ( ( candidate.count_if_carry0 | candidate.count_if_carry1 ) == 0ull )
								continue;
							candidate.relative_delta =
								( child_candidate.relative_delta << 1 ) | output_difference_bit;
							next_frontier.push_back( candidate );
						}
					}

					prune_support_normalized_frontier_in_place( next_frontier );
				}
			}

			[[nodiscard]] inline SupportNormalizedFrontierPair exact_next_normalized_frontier_relative(
				const SupportNormalizedFrontierPair& frontier_by_current_carry_difference,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit ) noexcept
			{
				SupportNormalizedFrontierPair next_frontier_by_current_carry_difference {};
				exact_next_normalized_frontier_relative_inplace(
					frontier_by_current_carry_difference,
					input_difference_bit,
					constant_bit,
					next_frontier_by_current_carry_difference );
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
					next_frontier.reserve(
						frontier_by_current_carry_difference[ 0u ].size() +
						frontier_by_current_carry_difference[ 1u ].size() );
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
					   lhs.suffix10_plus_anchor_bits == rhs.suffix10_plus_anchor_bits &&
					   lhs.suffix10_plus_residue_high_bits_floor == rhs.suffix10_plus_residue_high_bits_floor;
			}

			[[nodiscard]] inline DiffConstFixedInputPrototypeSupportConfig normalized_prototype_support_config(
				DiffConstFixedInputPrototypeSupportConfig config ) noexcept
			{
				if ( config.support_max_suffix < 1 )
					config.support_max_suffix = 1;
				if ( config.support_max_suffix > 19 )
					config.support_max_suffix = 19;
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
				const int floor = ( std::max )( 4, config.suffix10_plus_residue_high_bits_floor );
				return ( std::max )( floor, suffix_length - 3 );
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

			[[nodiscard]] inline std::string support_projection_signature_frontier(
				const std::vector<SupportNormalizedCandidate>& frontier,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config,
				bool include_global_baseq = false )
			{
				if ( frontier.empty() )
					return "empty##scale=1";

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
				if ( include_global_baseq )
				{
					out << "##gq" << global_baseq_bits
						<< "=0x" << std::hex << ( global_base_q & global_baseq_mask ) << std::dec;
				}

				const std::uint32_t upgrade_mask =
					support_residue_upgrade_mask_for_suffix( suffix_length, config );
				const int residue_high_bits =
					support_residue_high_bits_for_suffix( suffix_length, config );

				if ( frontier.size() == 1u )
				{
					const SupportNormalizedCandidate& candidate = frontier.front();
					DiffConstExactCount common_divisor {};
					if ( candidate.count_if_carry0 != DiffConstExactCount {} )
						common_divisor = candidate.count_if_carry0;
					if ( candidate.count_if_carry1 != DiffConstExactCount {} )
					{
						common_divisor =
							( common_divisor == DiffConstExactCount {} )
								? candidate.count_if_carry1
								: support_gcd_exact_count( common_divisor, candidate.count_if_carry1 );
					}
					if ( common_divisor == DiffConstExactCount {} )
						common_divisor = DiffConstExactCount { 1u };

					const std::size_t candidate_residue =
						static_cast<std::size_t>( candidate.relative_delta & 0x7ull );
					for ( std::size_t residue = 0; residue < 8u; ++residue )
					{
						out << "|r" << residue << ":";
						if ( residue != candidate_residue )
						{
							out << "empty";
							continue;
						}

						int bits = 4;
						if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
							bits = residue_high_bits;

						out << 1u;
						out << "##scale=";
						append_support_exact_count_text( out, common_divisor );
						out << "##bqlow" << bits << "=0x0|";
						append_support_exact_count_text( out, candidate.count_if_carry0 / common_divisor );
						out << ":";
						append_support_exact_count_text( out, candidate.count_if_carry1 / common_divisor );
						out << "@q0x0";
					}

					return out.str();
				}

				if ( frontier.size() <= 16u )
				{
					auto candidate_less =
						[]( const SupportNormalizedCandidate& lhs, const SupportNormalizedCandidate& rhs ) noexcept
						{
							if ( lhs.relative_delta != rhs.relative_delta )
								return lhs.relative_delta < rhs.relative_delta;
							if ( lhs.count_if_carry0 != rhs.count_if_carry0 )
								return lhs.count_if_carry0 > rhs.count_if_carry0;
							return lhs.count_if_carry1 > rhs.count_if_carry1;
						};

					for ( std::size_t residue = 0; residue < 8u; ++residue )
					{
						out << "|r" << residue << ":";

						std::size_t bucket_size = 0u;
						DiffConstExactCount common_divisor {};
						std::uint64_t bucket_min_q = 0ull;
						bool has_bucket_min_q = false;
						for ( const SupportNormalizedCandidate& candidate : frontier )
						{
							if ( static_cast<std::size_t>( candidate.relative_delta & 0x7ull ) != residue )
								continue;
							++bucket_size;
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
							const std::uint64_t candidate_q = candidate.relative_delta >> phase_bits;
							if ( !has_bucket_min_q || candidate_q < bucket_min_q )
							{
								bucket_min_q = candidate_q;
								has_bucket_min_q = true;
							}
						}

						if ( bucket_size == 0u )
						{
							out << "empty";
							continue;
						}
						if ( common_divisor == DiffConstExactCount {} )
							common_divisor = DiffConstExactCount { 1u };

						const std::uint64_t base_offset = bucket_min_q - global_base_q;
						int bits = 4;
						if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
							bits = residue_high_bits;
						const std::uint64_t mask =
							( bits >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << bits ) - 1ull );

						out << bucket_size;
						out << "##scale=";
						append_support_exact_count_text( out, common_divisor );
						out << "##bqlow" << bits
							<< "=0x" << std::hex << ( base_offset & mask ) << std::dec;
						out << "|";

						std::array<bool, 16> emitted {};
						for ( std::size_t output_index = 0u; output_index < bucket_size; ++output_index )
						{
							std::size_t best_index = frontier.size();
							for ( std::size_t index = 0u; index < frontier.size(); ++index )
							{
								if ( emitted[ index ] ||
									 static_cast<std::size_t>( frontier[ index ].relative_delta & 0x7ull ) != residue )
								{
									continue;
								}
								if ( best_index == frontier.size() ||
									 candidate_less( frontier[ index ], frontier[ best_index ] ) )
								{
									best_index = index;
								}
							}

							if ( output_index != 0u )
								out << ";";
							const SupportNormalizedCandidate& candidate = frontier[ best_index ];
							append_support_exact_count_text( out, candidate.count_if_carry0 / common_divisor );
							out << ":";
							append_support_exact_count_text( out, candidate.count_if_carry1 / common_divisor );
							out << "@q0x" << std::hex
								<< ( ( candidate.relative_delta >> phase_bits ) - bucket_min_q )
								<< std::dec;
							emitted[ best_index ] = true;
						}
					}

					return out.str();
				}

				std::array<std::vector<SupportNormalizedCandidate>, 8> buckets {};
				for ( const SupportNormalizedCandidate& candidate : frontier )
					buckets[ static_cast<std::size_t>( candidate.relative_delta & 0x7ull ) ].push_back( candidate );

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
					out << "##scale=";
					append_support_exact_count_text( out, common_divisor );
					out << "##bqlow" << bits
						<< "=0x" << std::hex << ( base_offset & mask ) << std::dec;
					out << "|";
					for ( std::size_t index = 0; index < bucket.size(); ++index )
					{
						if ( index != 0u )
							out << ";";
						append_support_exact_count_text( out, bucket[ index ].count_if_carry0 / common_divisor );
						out << ":";
						append_support_exact_count_text( out, bucket[ index ].count_if_carry1 / common_divisor );
						out << "@q0x" << std::hex
							<< ( ( bucket[ index ].relative_delta >> phase_bits ) - bucket_min_q )
							<< std::dec;
					}
				}

				return out.str();
			}

			[[nodiscard]] inline std::string support_projection_signature(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config,
				bool include_global_baseq = false )
			{
				return support_projection_signature_frontier(
					frontier_by_carry_difference[ 0u ],
					suffix_length,
					config,
					include_global_baseq );
			}

			[[nodiscard]] inline std::uint64_t support_hash_projection_signature_frontier(
				const std::vector<SupportNormalizedCandidate>& frontier,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config,
				bool include_global_baseq = false )
			{
				std::uint64_t seed = support_mix_hash64( 0xDE5FB9D2630458E9ull, static_cast<std::uint64_t>( suffix_length ) );
				if ( frontier.empty() )
					return support_mix_hash64( seed, 0ull );

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

				if ( include_global_baseq )
				{
					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( global_baseq_bits ) );
					seed = support_mix_hash64( seed, global_base_q & global_baseq_mask );
				}

				const std::uint32_t upgrade_mask =
					support_residue_upgrade_mask_for_suffix( suffix_length, config );
				const int residue_high_bits =
					support_residue_high_bits_for_suffix( suffix_length, config );

				if ( frontier.size() == 1u )
				{
					const SupportNormalizedCandidate& candidate = frontier.front();
					DiffConstExactCount common_divisor {};
					if ( candidate.count_if_carry0 != DiffConstExactCount {} )
						common_divisor = candidate.count_if_carry0;
					if ( candidate.count_if_carry1 != DiffConstExactCount {} )
					{
						common_divisor =
							( common_divisor == DiffConstExactCount {} )
								? candidate.count_if_carry1
								: support_gcd_exact_count( common_divisor, candidate.count_if_carry1 );
					}
					if ( common_divisor == DiffConstExactCount {} )
						common_divisor = DiffConstExactCount { 1u };

					const std::size_t candidate_residue =
						static_cast<std::size_t>( candidate.relative_delta & 0x7ull );
					for ( std::size_t residue = 0u; residue < 8u; ++residue )
					{
						seed = support_mix_hash64( seed, residue );
						if ( residue != candidate_residue )
						{
							seed = support_mix_hash64( seed, 0xFFFFFFFFFFFFFFFFull );
							continue;
						}

						int bits = 4;
						if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
							bits = residue_high_bits;

						std::uint64_t item_hash = 0xB7E151628AED2A6Bull;
						item_hash = support_mix_hash64(
							item_hash,
							support_hash_exact_count( candidate.count_if_carry0 / common_divisor ) );
						item_hash = support_mix_hash64(
							item_hash,
							support_hash_exact_count( candidate.count_if_carry1 / common_divisor ) );
						item_hash = support_mix_hash64( item_hash, 0ull );

						seed = support_mix_hash64( seed, 1ull );
						seed = support_mix_hash64( seed, support_hash_exact_count( common_divisor ) );
						seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bits ) );
						seed = support_mix_hash64( seed, 0ull );
						seed = support_mix_hash64( seed, 1ull );
						seed = support_mix_hash64( seed, item_hash );
						seed = support_mix_hash64( seed, item_hash );
						seed = support_mix_hash64( seed, item_hash ^ 0x9E3779B97F4A7C15ull );
					}

					return seed;
				}

				std::array<std::vector<SupportNormalizedCandidate>, 8> buckets {};
				for ( const SupportNormalizedCandidate& candidate : frontier )
					buckets[ static_cast<std::size_t>( candidate.relative_delta & 0x7ull ) ].push_back( candidate );

				for ( std::size_t residue = 0; residue < buckets.size(); ++residue )
				{
					auto& bucket = buckets[ residue ];
					seed = support_mix_hash64( seed, residue );
					if ( bucket.empty() )
					{
						seed = support_mix_hash64( seed, 0xFFFFFFFFFFFFFFFFull );
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

					std::uint64_t bucket_min_q = bucket.front().relative_delta >> phase_bits;
					for ( const SupportNormalizedCandidate& candidate : bucket )
					{
						const std::uint64_t candidate_q = candidate.relative_delta >> phase_bits;
						if ( candidate_q < bucket_min_q )
							bucket_min_q = candidate_q;
					}
					const std::uint64_t base_offset = bucket_min_q - global_base_q;
					int bits = 4;
					if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
						bits = residue_high_bits;
					const std::uint64_t mask =
						( bits >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << bits ) - 1ull );

					std::uint64_t item_xor = 0ull;
					std::uint64_t item_sum = 0ull;
					std::uint64_t item_sum2 = 0ull;
					std::uint64_t item_count = 0ull;
					for ( const SupportNormalizedCandidate& candidate : bucket )
					{
						std::uint64_t item_hash = 0xB7E151628AED2A6Bull;
						item_hash = support_mix_hash64(
							item_hash,
							support_hash_exact_count( candidate.count_if_carry0 / common_divisor ) );
						item_hash = support_mix_hash64(
							item_hash,
							support_hash_exact_count( candidate.count_if_carry1 / common_divisor ) );
						item_hash = support_mix_hash64(
							item_hash,
							( candidate.relative_delta >> phase_bits ) - bucket_min_q );
						item_xor ^= item_hash;
						item_sum += item_hash;
						item_sum2 += ( item_hash ^ 0x9E3779B97F4A7C15ull );
						++item_count;
					}

					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bucket.size() ) );
					seed = support_mix_hash64( seed, support_hash_exact_count( common_divisor ) );
					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bits ) );
					seed = support_mix_hash64( seed, base_offset & mask );
					seed = support_mix_hash64( seed, item_count );
					seed = support_mix_hash64( seed, item_xor );
					seed = support_mix_hash64( seed, item_sum );
					seed = support_mix_hash64( seed, item_sum2 );
				}

				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_projection_signature(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config,
				bool include_global_baseq = false )
			{
				return support_hash_projection_signature_frontier(
					frontier_by_carry_difference[ 0u ],
					suffix_length,
					config,
					include_global_baseq );
			}

			[[nodiscard]] inline constexpr std::string_view support_class_key_descriptor_tag() noexcept
			{
				return "|q2desc=no_gq_no_anchor_no_c1proj_bqlow_sminus3|";
			}

			[[nodiscard]] inline std::string support_class_projection_key(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				// Current promoted Fixed-input Q2 descriptor projection:
				// main projection without global baseq, without anchor, and
				// without carry1 projection. Exact quotient audits showed bqlow
				// and scale are necessary. Upgraded-residue bqlow now uses the
				// exact audit-supported max(4, suffix-3) width by default; smaller
				// caps create ambiguous quotient slots.
				return support_projection_signature(
					frontier_by_carry_difference,
					suffix_length,
					config,
					false );
			}

			[[nodiscard]] inline std::string support_class_key_from_projection(
				int suffix_length,
				const std::string& projection_key )
			{
				const std::string suffix_text = std::to_string( suffix_length );
				const std::string_view tag = support_class_key_descriptor_tag();
				std::string key {};
				key.reserve(
					2u + suffix_text.size() +
					tag.size() + projection_key.size() );
				key += "s=";
				key += suffix_text;
				key.append( tag.data(), tag.size() );
				key += projection_key;
				return key;
			}

			[[nodiscard]] inline std::string support_class_key(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				const std::string projection_key =
					support_class_projection_key(
						frontier_by_carry_difference,
						suffix_length,
						config );
				return support_class_key_from_projection( suffix_length, projection_key );
			}

			[[nodiscard]] inline std::uint64_t support_hash_class_key(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config ) noexcept
			{
				std::uint64_t seed = support_hash_projection_signature(
					frontier_by_carry_difference,
					suffix_length,
					config,
					false );
				seed = support_mix_hash64( seed, static_cast<std::uint64_t>( suffix_length ) );
				seed = support_mix_hash64( seed, 0xC1A551F00D9B5E9ull );
				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_projection_signature_ordered(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config,
				bool include_global_baseq = false )
			{
				const std::vector<SupportNormalizedCandidate>& frontier = frontier_by_carry_difference[ 0u ];
				std::uint64_t seed = support_mix_hash64( 0xA5A5F00D1234BEEFull, static_cast<std::uint64_t>( suffix_length ) );
				if ( frontier.empty() )
					return support_mix_hash64( seed, 0ull );

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

				if ( include_global_baseq )
				{
					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( global_baseq_bits ) );
					seed = support_mix_hash64( seed, global_base_q & global_baseq_mask );
				}

				const std::uint32_t upgrade_mask =
					support_residue_upgrade_mask_for_suffix( suffix_length, config );
				const int residue_high_bits =
					support_residue_high_bits_for_suffix( suffix_length, config );

				if ( frontier.size() == 1u )
				{
					const SupportNormalizedCandidate& candidate = frontier.front();
					DiffConstExactCount common_divisor {};
					if ( candidate.count_if_carry0 != DiffConstExactCount {} )
						common_divisor = candidate.count_if_carry0;
					if ( candidate.count_if_carry1 != DiffConstExactCount {} )
					{
						common_divisor =
							( common_divisor == DiffConstExactCount {} )
								? candidate.count_if_carry1
								: support_gcd_exact_count( common_divisor, candidate.count_if_carry1 );
					}
					if ( common_divisor == DiffConstExactCount {} )
						common_divisor = DiffConstExactCount { 1u };

					const std::size_t candidate_residue =
						static_cast<std::size_t>( candidate.relative_delta & 0x7ull );
					for ( std::size_t residue = 0u; residue < 8u; ++residue )
					{
						seed = support_mix_hash64( seed, residue );
						if ( residue != candidate_residue )
						{
							seed = support_mix_hash64( seed, 0xFFFFFFFFFFFFFFFFull );
							continue;
						}

						int bits = 4;
						if ( ( upgrade_mask & ( std::uint32_t( 1 ) << residue ) ) != 0u )
							bits = residue_high_bits;

						seed = support_mix_hash64( seed, 1ull );
						seed = support_mix_hash64( seed, support_hash_exact_count( common_divisor ) );
						seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bits ) );
						seed = support_mix_hash64( seed, 0ull );
						seed = support_mix_hash64(
							seed,
							support_hash_exact_count( candidate.count_if_carry0 / common_divisor ) );
						seed = support_mix_hash64(
							seed,
							support_hash_exact_count( candidate.count_if_carry1 / common_divisor ) );
						seed = support_mix_hash64( seed, 0ull );
					}

					return seed;
				}

				std::array<std::vector<SupportNormalizedCandidate>, 8> buckets {};
				for ( const SupportNormalizedCandidate& candidate : frontier )
					buckets[ static_cast<std::size_t>( candidate.relative_delta & 0x7ull ) ].push_back( candidate );

				for ( std::size_t residue = 0; residue < buckets.size(); ++residue )
				{
					auto& bucket = buckets[ residue ];
					seed = support_mix_hash64( seed, residue );
					if ( bucket.empty() )
					{
						seed = support_mix_hash64( seed, 0xFFFFFFFFFFFFFFFFull );
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

					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bucket.size() ) );
					seed = support_mix_hash64( seed, support_hash_exact_count( common_divisor ) );
					seed = support_mix_hash64( seed, static_cast<std::uint64_t>( bits ) );
					seed = support_mix_hash64( seed, base_offset & mask );
					for ( const SupportNormalizedCandidate& candidate : bucket )
					{
						seed = support_mix_hash64(
							seed,
							support_hash_exact_count( candidate.count_if_carry0 / common_divisor ) );
						seed = support_mix_hash64(
							seed,
							support_hash_exact_count( candidate.count_if_carry1 / common_divisor ) );
						seed = support_mix_hash64(
							seed,
							( candidate.relative_delta >> phase_bits ) - bucket_min_q );
					}
				}

				return seed;
			}

			[[nodiscard]] inline std::uint64_t support_hash_class_key_ordered(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				std::uint64_t seed = support_hash_projection_signature_ordered(
					frontier_by_carry_difference,
					suffix_length,
					config,
					false );
				seed = support_mix_hash64( seed, static_cast<std::uint64_t>( suffix_length ) );
				seed = support_mix_hash64( seed, 0xC1A551F00D9B5E9ull );
				return seed;
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

			[[nodiscard]] inline SupportCanonicalDescriptor canonical_support_descriptor(
				const SupportNormalizedFrontierPair& frontier_by_carry_difference,
				int suffix_length,
				const DiffConstFixedInputPrototypeSupportConfig& config )
			{
				SupportCanonicalDescriptor descriptor {};
				descriptor.suffix_length = static_cast<std::uint16_t>( suffix_length );
				const std::uint64_t direct_hash =
					support_hash_class_key_ordered(
						frontier_by_carry_difference,
						suffix_length,
						config );
				SupportNormalizedFrontierPair swapped_frontier =
					swap_support_count_coordinates( frontier_by_carry_difference );
				const std::uint64_t swapped_hash =
					support_hash_class_key_ordered(
						swapped_frontier,
						suffix_length,
						config );
				if ( swapped_hash < direct_hash )
				{
					descriptor.projection_key =
						support_class_projection_key(
							swapped_frontier,
							suffix_length,
							config );
					descriptor.key =
						support_class_key_from_projection(
							suffix_length,
							descriptor.projection_key );
					descriptor.frontier = std::move( swapped_frontier );
					descriptor.swapped_count_orientation = true;
					descriptor.hash = swapped_hash;
					return descriptor;
				}
				if ( direct_hash < swapped_hash )
				{
					descriptor.projection_key =
						support_class_projection_key(
							frontier_by_carry_difference,
							suffix_length,
							config );
					descriptor.key =
						support_class_key_from_projection(
							suffix_length,
							descriptor.projection_key );
					descriptor.frontier = frontier_by_carry_difference;
					descriptor.swapped_count_orientation = false;
					descriptor.hash = direct_hash;
					return descriptor;
				}

				std::string direct_projection_key =
					support_class_projection_key(
						frontier_by_carry_difference,
						suffix_length,
						config );
				std::string swapped_projection_key =
					support_class_projection_key(
						swapped_frontier,
						suffix_length,
						config );
				std::string direct_key =
					support_class_key_from_projection(
						suffix_length,
						direct_projection_key );
				std::string swapped_key =
					support_class_key_from_projection(
						suffix_length,
						swapped_projection_key );
				if ( swapped_key < direct_key )
				{
					descriptor.key = std::move( swapped_key );
					descriptor.projection_key = std::move( swapped_projection_key );
					descriptor.frontier = std::move( swapped_frontier );
					descriptor.swapped_count_orientation = true;
					descriptor.hash = swapped_hash;
					return descriptor;
				}
				descriptor.key = std::move( direct_key );
				descriptor.projection_key = std::move( direct_projection_key );
				descriptor.frontier = frontier_by_carry_difference;
				descriptor.swapped_count_orientation = false;
				descriptor.hash = direct_hash;
				return descriptor;
			}

			[[nodiscard]] inline std::uint64_t support_transition_cache_hash(
				const SupportCanonicalDescriptor& descriptor ) noexcept
			{
				return descriptor.hash;
			}

			[[nodiscard]] inline constexpr std::uint16_t support_transition_cache_raw_key_sentinel() noexcept
			{
				return std::numeric_limits<std::uint16_t>::max();
			}

			[[nodiscard]] inline constexpr std::uint16_t support_transition_cache_elided_key_sentinel() noexcept
			{
				return static_cast<std::uint16_t>(
					support_transition_cache_raw_key_sentinel() - 1u );
			}

			[[nodiscard]] inline bool support_transition_cache_key_is_elided(
				const SupportTransitionCacheKey& key ) noexcept
			{
				return key.suffix_length == support_transition_cache_elided_key_sentinel();
			}

			[[nodiscard]] inline bool support_parse_class_key_view(
				const std::string& key,
				SupportDescriptorKeyView& view ) noexcept
			{
				view = SupportDescriptorKeyView {};
				if ( key.size() < 3u || key[ 0u ] != 's' || key[ 1u ] != '=' )
					return false;

				std::size_t offset = 2u;
				if ( offset >= key.size() || key[ offset ] < '0' || key[ offset ] > '9' )
					return false;
				if ( key[ offset ] == '0' &&
					 offset + 1u < key.size() &&
					 key[ offset + 1u ] >= '0' &&
					 key[ offset + 1u ] <= '9' )
				{
					return false;
				}

				std::uint32_t suffix_length = 0u;
				while ( offset < key.size() &&
						key[ offset ] >= '0' &&
						key[ offset ] <= '9' )
				{
					suffix_length =
						suffix_length * 10u +
						static_cast<std::uint32_t>( key[ offset ] - '0' );
					if ( suffix_length >= support_transition_cache_elided_key_sentinel() )
						return false;
					++offset;
				}

				const std::string_view tag = support_class_key_descriptor_tag();
				if ( offset + tag.size() > key.size() ||
					 key.compare( offset, tag.size(), tag.data(), tag.size() ) != 0 )
				{
					return false;
				}
				offset += tag.size();
				view.projection_key =
					std::string_view { key.data() + offset, key.size() - offset };
				view.suffix_length = static_cast<std::uint16_t>( suffix_length );
				view.structured = true;
				return true;
			}

			[[nodiscard]] inline SupportDescriptorKeyView support_descriptor_key_view(
				const SupportCanonicalDescriptor& descriptor ) noexcept
			{
				return SupportDescriptorKeyView {
					std::string_view { descriptor.projection_key.data(), descriptor.projection_key.size() },
					descriptor.suffix_length,
					true };
			}

			[[nodiscard]] inline bool support_string_equals_view(
				const std::string& lhs,
				std::string_view rhs ) noexcept
			{
				if ( lhs.size() != rhs.size() )
					return false;
				if ( rhs.empty() )
					return true;
				return lhs.compare( 0u, lhs.size(), rhs.data(), rhs.size() ) == 0;
			}

			[[nodiscard]] inline SupportTransitionCacheKey support_transition_cache_key_from_descriptor(
				const SupportCanonicalDescriptor& descriptor )
			{
				return SupportTransitionCacheKey {
					descriptor.projection_key,
					descriptor.suffix_length };
			}

			[[nodiscard]] inline SupportTransitionCacheKey support_transition_cache_key_from_text(
				const std::string& descriptor_key )
			{
				SupportDescriptorKeyView key_view {};
				if ( support_parse_class_key_view( descriptor_key, key_view ) )
				{
					return SupportTransitionCacheKey {
						std::string { key_view.projection_key },
						key_view.suffix_length };
				}

				return SupportTransitionCacheKey {
					descriptor_key,
					support_transition_cache_raw_key_sentinel() };
			}

			[[nodiscard]] inline bool support_transition_cache_key_matches(
				const SupportTransitionCacheKey& stored_key,
				const SupportDescriptorKeyView& descriptor_key_view,
				const std::string& descriptor_key ) noexcept
			{
				if ( support_transition_cache_key_is_elided( stored_key ) )
					return false;
				if ( stored_key.suffix_length == support_transition_cache_raw_key_sentinel() )
					return stored_key.projection_key == descriptor_key;
				if ( !descriptor_key_view.structured )
					return false;
				return stored_key.suffix_length == descriptor_key_view.suffix_length &&
					   support_string_equals_view(
						   stored_key.projection_key,
						   descriptor_key_view.projection_key );
			}

			[[nodiscard]] inline const SupportTransitionCacheKey& support_transition_cache_entry_key(
				const SupportCache& cache,
				const SupportTransitionCacheEntry& entry ) noexcept
			{
				return cache.transition_keys[ static_cast<std::size_t>( entry.key_id ) ];
			}

			[[nodiscard]] inline SupportTransitionCacheEntry* find_support_transition_cache_entry(
				SupportCache& cache,
				std::uint64_t hash_key,
				const std::string& descriptor_key ) noexcept
			{
				SupportDescriptorKeyView descriptor_key_view {};
				( void )support_parse_class_key_view( descriptor_key, descriptor_key_view );
				auto range = cache.transitions.equal_range( hash_key );
				std::size_t bucket_size = 0u;
				for ( auto it = range.first; it != range.second; ++it )
					++bucket_size;
				for ( auto it = range.first; it != range.second; ++it )
				{
					const SupportTransitionCacheKey& stored_key =
						support_transition_cache_entry_key( cache, it->second );
					if ( support_transition_cache_key_is_elided( stored_key ) )
					{
						if ( bucket_size == 1u )
							return &it->second;
						continue;
					}
					if ( support_transition_cache_key_matches(
							 stored_key,
							 descriptor_key_view,
							 descriptor_key ) )
					{
						return &it->second;
					}
				}
				return nullptr;
			}

			[[nodiscard]] inline SupportTransitionCacheEntry* find_support_transition_cache_entry(
				SupportCache& cache,
				std::uint64_t hash_key,
				const SupportDescriptorKeyView& descriptor_key_view,
				const std::string& descriptor_key ) noexcept
			{
				auto range = cache.transitions.equal_range( hash_key );
				std::size_t bucket_size = 0u;
				for ( auto it = range.first; it != range.second; ++it )
					++bucket_size;
				for ( auto it = range.first; it != range.second; ++it )
				{
					const SupportTransitionCacheKey& stored_key =
						support_transition_cache_entry_key( cache, it->second );
					if ( support_transition_cache_key_is_elided( stored_key ) )
					{
						if ( bucket_size == 1u )
							return &it->second;
						continue;
					}
					if ( support_transition_cache_key_matches(
							 stored_key,
							 descriptor_key_view,
							 descriptor_key ) )
					{
						return &it->second;
					}
				}
				return nullptr;
			}

			[[nodiscard]] inline const SupportTransitionCacheEntry* find_support_transition_cache_entry(
				const SupportCache& cache,
				std::uint64_t hash_key,
				const std::string& descriptor_key ) noexcept
			{
				SupportDescriptorKeyView descriptor_key_view {};
				( void )support_parse_class_key_view( descriptor_key, descriptor_key_view );
				auto range = cache.transitions.equal_range( hash_key );
				std::size_t bucket_size = 0u;
				for ( auto it = range.first; it != range.second; ++it )
					++bucket_size;
				for ( auto it = range.first; it != range.second; ++it )
				{
					const SupportTransitionCacheKey& stored_key =
						support_transition_cache_entry_key( cache, it->second );
					if ( support_transition_cache_key_is_elided( stored_key ) )
					{
						if ( bucket_size == 1u )
							return &it->second;
						continue;
					}
					if ( support_transition_cache_key_matches(
							 stored_key,
							 descriptor_key_view,
							 descriptor_key ) )
					{
						return &it->second;
					}
				}
				return nullptr;
			}

			[[nodiscard]] inline const SupportTransitionCacheEntry* find_support_transition_cache_entry(
				const SupportCache& cache,
				std::uint64_t hash_key,
				const SupportDescriptorKeyView& descriptor_key_view,
				const std::string& descriptor_key ) noexcept
			{
				auto range = cache.transitions.equal_range( hash_key );
				std::size_t bucket_size = 0u;
				for ( auto it = range.first; it != range.second; ++it )
					++bucket_size;
				for ( auto it = range.first; it != range.second; ++it )
				{
					const SupportTransitionCacheKey& stored_key =
						support_transition_cache_entry_key( cache, it->second );
					if ( support_transition_cache_key_is_elided( stored_key ) )
					{
						if ( bucket_size == 1u )
							return &it->second;
						continue;
					}
					if ( support_transition_cache_key_matches(
							 stored_key,
							 descriptor_key_view,
							 descriptor_key ) )
					{
						return &it->second;
					}
				}
				return nullptr;
			}

			[[nodiscard]] inline bool support_transition_hash_bucket_exists(
				const std::unordered_multimap<std::uint64_t, SupportTransitionCacheEntry>& transitions,
				std::uint64_t hash_key ) noexcept
			{
				return transitions.find( hash_key ) != transitions.end();
			}

			inline void elide_support_transition_cache_singleton_keys( SupportCache& cache )
			{
				std::vector<std::uint64_t> hash_keys {};
				hash_keys.reserve( cache.transitions.size() );
				for ( const auto& entry : cache.transitions )
					hash_keys.push_back( entry.first );
				std::sort( hash_keys.begin(), hash_keys.end() );
				hash_keys.erase(
					std::unique( hash_keys.begin(), hash_keys.end() ),
					hash_keys.end() );

				for ( const std::uint64_t hash_key : hash_keys )
				{
					auto range = cache.transitions.equal_range( hash_key );
					std::size_t bucket_size = 0u;
					for ( auto it = range.first; it != range.second; ++it )
						++bucket_size;
					if ( bucket_size != 1u )
						continue;

					const std::uint32_t key_id = range.first->second.key_id;
					if ( static_cast<std::size_t>( key_id ) >= cache.transition_keys.size() )
						std::abort();
					SupportTransitionCacheKey& key =
						cache.transition_keys[ static_cast<std::size_t>( key_id ) ];
					if ( key.suffix_length == support_transition_cache_raw_key_sentinel() )
						continue;
					std::string {}.swap( key.projection_key );
					key.suffix_length = support_transition_cache_elided_key_sentinel();
				}
			}

			inline void attach_support_transition_pending_group_to_cache(
				const SupportCache& cache,
				SupportTransitionPendingGroup& pending )
			{
				SupportTransitionGroup& group = pending.group;
				group.candidate_core_offset =
					support_transition_checked_arena_offset(
						cache.transition_candidate_core_arena.size() );
				for ( std::uint32_t source_carry_difference = 0u;
					  source_carry_difference <= 1u;
					  ++source_carry_difference )
				{
					const std::vector<SupportTransitionCandidateCore>& bucket =
						pending.candidate_pool[ source_carry_difference ];
					group.candidate_core_lengths[ source_carry_difference ] =
						support_transition_checked_arena_length( bucket.size() );
					cache.transition_candidate_core_arena.insert(
						cache.transition_candidate_core_arena.end(),
						bucket.begin(),
						bucket.end() );
				}
			}

			[[nodiscard]] inline SupportTransitionCacheEntry& get_or_insert_support_transition_cache_entry(
				SupportCache& cache,
				const SupportCanonicalDescriptor& descriptor,
				SupportTransitionPendingGroup group )
			{
				auto existing =
					find_support_transition_cache_entry(
						cache,
						support_transition_cache_hash( descriptor ),
						support_descriptor_key_view( descriptor ),
						descriptor.key );
				if ( existing != nullptr )
					return *existing;

				const std::uint64_t hash_key = support_transition_cache_hash( descriptor );
				const std::uint32_t key_id =
					static_cast<std::uint32_t>( cache.transition_keys.size() );
				cache.transition_keys.push_back(
					support_transition_cache_key_from_descriptor( descriptor ) );
				attach_support_transition_pending_group_to_cache( cache, group );
				auto inserted =
					cache.transitions.emplace(
						hash_key,
						SupportTransitionCacheEntry { key_id, std::move( group.group ) } );
				return inserted->second;
			}

			[[nodiscard]] inline SupportTransitionCacheEntry& get_or_insert_support_transition_cache_entry(
				const SupportCache& cache,
				const SupportCanonicalDescriptor& descriptor,
				SupportTransitionPendingGroup group )
			{
				auto existing =
					find_support_transition_cache_entry(
						cache,
						support_transition_cache_hash( descriptor ),
						support_descriptor_key_view( descriptor ),
						descriptor.key );
				if ( existing != nullptr )
					return const_cast<SupportTransitionCacheEntry&>( *existing );

				const std::uint64_t hash_key = support_transition_cache_hash( descriptor );
				const std::uint32_t key_id =
					static_cast<std::uint32_t>( cache.transition_keys.size() );
				cache.transition_keys.push_back(
					support_transition_cache_key_from_descriptor( descriptor ) );
				attach_support_transition_pending_group_to_cache( cache, group );
				auto inserted =
					cache.transitions.emplace(
						hash_key,
						SupportTransitionCacheEntry { key_id, std::move( group.group ) } );
				return const_cast<SupportTransitionCacheEntry&>( inserted->second );
			}

			[[nodiscard]] inline SupportTransitionCacheEntry& get_or_insert_support_transition_cache_entry(
				const SupportCache& cache,
				const std::string& descriptor_key,
				std::uint64_t hash_key,
				SupportTransitionPendingGroup group )
			{
				auto existing =
					find_support_transition_cache_entry(
						cache,
						hash_key,
						descriptor_key );
				if ( existing != nullptr )
					return const_cast<SupportTransitionCacheEntry&>( *existing );

				const std::uint32_t key_id =
					static_cast<std::uint32_t>( cache.transition_keys.size() );
				cache.transition_keys.push_back(
					support_transition_cache_key_from_text( descriptor_key ) );
				attach_support_transition_pending_group_to_cache( cache, group );
				auto inserted =
					cache.transitions.emplace(
						hash_key,
						SupportTransitionCacheEntry { key_id, std::move( group.group ) } );
				return const_cast<SupportTransitionCacheEntry&>( inserted->second );
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
					( void )find_or_insert_support_exact_state(
						exact_layers[ 1 ],
						normalized_initial,
						current_frontiers );
				}
				exact_layers[ 1 ].states = current_frontiers;
				exact_layers[ 1 ].next_state_ids.resize( current_frontiers.size() );

				for ( int suffix_length = 1; suffix_length < max_precompute_suffix; ++suffix_length )
				{
					std::vector<SupportNormalizedFrontierPair> next_frontiers {};
					next_frontiers.reserve( current_frontiers.size() * 3u );
					exact_layers[ suffix_length + 1 ].state_indices_by_hash.reserve( current_frontiers.size() * 3u );
					exact_layers[ suffix_length ].next_state_ids.resize( current_frontiers.size() );

					for ( std::size_t state_index = 0; state_index < current_frontiers.size(); ++state_index )
					{
						for ( std::uint32_t input_difference_bit = 0u; input_difference_bit <= 1u; ++input_difference_bit )
						{
							for ( std::uint32_t constant_bit = 0u; constant_bit <= 1u; ++constant_bit )
							{
								SupportNormalizedFrontierPair normalized_next =
									exact_next_normalized_frontier_relative(
										current_frontiers[ state_index ],
										input_difference_bit,
										constant_bit );
								const int next_state_id =
									find_or_insert_support_exact_state(
										exact_layers[ suffix_length + 1 ],
										std::move( normalized_next ),
										next_frontiers );
								exact_layers[ suffix_length ].next_state_ids[ state_index ][ support_context_index( input_difference_bit, constant_bit ) ] =
									next_state_id;
							}
						}
					}

					exact_layers[ suffix_length + 1 ].states = next_frontiers;
					if ( suffix_length + 1 < max_precompute_suffix )
						exact_layers[ suffix_length + 1 ].next_state_ids.resize( next_frontiers.size() );
					current_frontiers.swap( next_frontiers );
				}
			}

			[[nodiscard]] inline int support_descriptor_prewarm_suffix_cap(
				const DiffConstFixedInputPrototypeSupportConfig& config ) noexcept
			{
				return ( std::min )( config.support_max_suffix, 10 );
			}

			[[nodiscard]] inline int support_descriptor_compressed_prewarm_suffix_cap(
				const DiffConstFixedInputPrototypeSupportConfig& config ) noexcept
			{
				return ( std::min )( config.support_max_suffix, 19 );
			}

			inline void insert_support_descriptor_transition(
				SupportCache& cache,
				const SupportNormalizedFrontierPair& source_frontier,
				int suffix_length,
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit )
			{
				const SupportCanonicalDescriptor canonical =
					canonical_support_descriptor(
						source_frontier,
						suffix_length,
						cache.config );
				( void )input_difference_bit;
				( void )constant_bit;
				SupportTransitionPendingGroup group =
					make_support_transition_group_from_descriptor_source( canonical.frontier );
				( void )get_or_insert_support_transition_cache_entry(
					cache,
					canonical,
					std::move( group ) );
			}

			inline void insert_support_descriptor_group(
				SupportCache& cache,
				const SupportNormalizedFrontierPair& source_frontier,
				int suffix_length,
				SupportTransitionPendingGroup group )
			{
				const SupportCanonicalDescriptor canonical =
					canonical_support_descriptor(
						source_frontier,
						suffix_length,
						cache.config );
				( void )get_or_insert_support_transition_cache_entry(
					cache,
					canonical,
					std::move( group ) );
			}

			inline void insert_support_descriptor_group_with_key(
				SupportCache& cache,
				const std::string& source_key,
				std::uint64_t source_hash,
				SupportTransitionPendingGroup group )
			{
				( void )get_or_insert_support_transition_cache_entry(
					cache,
					source_key,
					source_hash,
					std::move( group ) );
			}

			inline void prewarm_support_descriptor_transitions( SupportCache& cache )
			{
				const int prewarm_max_suffix =
					support_descriptor_prewarm_suffix_cap( cache.config );
				cache.descriptor_prewarm_max_suffix = prewarm_max_suffix;
				cache.descriptor_compressed_prewarm_max_suffix =
					( std::max )(
						prewarm_max_suffix,
						support_descriptor_compressed_prewarm_suffix_cap( cache.config ) );
				if ( prewarm_max_suffix <= 0 )
					return;

				const int compressed_prewarm_max_suffix =
					cache.descriptor_compressed_prewarm_max_suffix;
				const bool needs_compressed_prewarm =
					compressed_prewarm_max_suffix > prewarm_max_suffix;
				std::unordered_map<std::string, SupportNormalizedFrontierPair> current_descriptors {};

				std::vector<SupportExactLayer> exact_layers {};
				build_support_exact_layers( prewarm_max_suffix + 1, exact_layers );
				std::size_t exact_transition_capacity = 0u;
				for ( int suffix_length = 1; suffix_length <= prewarm_max_suffix; ++suffix_length )
					exact_transition_capacity += exact_layers[ suffix_length ].states.size();
				cache.transitions.reserve( cache.transitions.size() + exact_transition_capacity );
				cache.transition_keys.reserve( cache.transition_keys.size() + exact_transition_capacity );
				cache.transition_candidate_core_arena.reserve(
					cache.transition_candidate_core_arena.size() +
					exact_transition_capacity * 32u );
				if ( needs_compressed_prewarm )
					current_descriptors.reserve( exact_layers[ prewarm_max_suffix ].states.size() );
				for ( int suffix_length = 1; suffix_length <= prewarm_max_suffix; ++suffix_length )
				{
					const SupportExactLayer& layer = exact_layers[ suffix_length ];
					for ( const SupportNormalizedFrontierPair& source_frontier : layer.states )
					{
						SupportCanonicalDescriptor canonical =
							canonical_support_descriptor(
								source_frontier,
								suffix_length,
								cache.config );
						SupportTransitionPendingGroup source_group =
							make_support_transition_group_from_descriptor_source( canonical.frontier );
						if ( needs_compressed_prewarm && suffix_length == prewarm_max_suffix )
						{
							( void )get_or_insert_support_transition_cache_entry(
								cache,
								canonical,
								std::move( source_group ) );
							current_descriptors.emplace(
								std::move( canonical.key ),
								std::move( canonical.frontier ) );
						}
						else
						{
							( void )get_or_insert_support_transition_cache_entry(
								cache,
								canonical,
								std::move( source_group ) );
						}
					}
				}

				if ( !needs_compressed_prewarm )
				{
					elide_support_transition_cache_singleton_keys( cache );
					return;
				}

				for ( int suffix_length = prewarm_max_suffix; suffix_length <= compressed_prewarm_max_suffix; ++suffix_length )
				{
					cache.transitions.reserve( cache.transitions.size() + current_descriptors.size() );
					cache.transition_keys.reserve( cache.transition_keys.size() + current_descriptors.size() );
					cache.transition_candidate_core_arena.reserve(
						cache.transition_candidate_core_arena.size() +
						current_descriptors.size() * 32u );
					std::unordered_map<std::string, SupportNormalizedFrontierPair> next_descriptors {};
					if ( suffix_length < compressed_prewarm_max_suffix )
						next_descriptors.reserve( current_descriptors.size() * 3u );

					for ( const auto& descriptor_entry : current_descriptors )
					{
						const SupportNormalizedFrontierPair& source_frontier = descriptor_entry.second;
						const SupportTransitionGroup* group_ptr = nullptr;
						const std::uint64_t source_hash =
							support_hash_class_key_ordered(
								source_frontier,
								suffix_length,
								cache.config );
						auto* cached_entry =
							find_support_transition_cache_entry(
								cache,
								source_hash,
								descriptor_entry.first );
						if ( cached_entry != nullptr )
						{
							group_ptr = &cached_entry->group;
						}
						else
						{
							SupportTransitionPendingGroup group =
								make_support_transition_group_from_descriptor_source( source_frontier );
							SupportTransitionCacheEntry& inserted_group =
								get_or_insert_support_transition_cache_entry(
									cache,
									descriptor_entry.first,
									source_hash,
									std::move( group ) );
							group_ptr = &inserted_group.group;
						}
						const SupportTransitionGroup& group = *group_ptr;
						if ( suffix_length >= compressed_prewarm_max_suffix )
							continue;

						for ( std::uint32_t input_difference_bit = 0u; input_difference_bit <= 1u; ++input_difference_bit )
						{
							for ( std::uint32_t constant_bit = 0u; constant_bit <= 1u; ++constant_bit )
							{
								const std::size_t context =
									static_cast<std::size_t>( support_context_index( input_difference_bit, constant_bit ) );
								const SupportTransitionValue& transition_value = group.by_context[ context ];
								SupportNormalizedFrontierPair next_frontier =
									materialize_support_tagged_transition_relative_no_shift(
										cache,
										group,
										transition_value,
										input_difference_bit );
								SupportCanonicalDescriptor canonical_next =
									canonical_support_descriptor(
										next_frontier,
										suffix_length + 1,
										cache.config );
								next_descriptors.emplace(
									std::move( canonical_next.key ),
									std::move( canonical_next.frontier ) );
							}
						}
					}

					current_descriptors = std::move( next_descriptors );
				}
				elide_support_transition_cache_singleton_keys( cache );
			}

			[[nodiscard]] inline const SupportCache& get_or_build_phase8_banded_support_cache(
				DiffConstFixedInputPrototypeSupportConfig config )
			{
				config = normalized_prototype_support_config( config );
				static std::mutex cache_mutex {};
				static std::vector<std::unique_ptr<SupportCache>> caches {};

				std::lock_guard<std::mutex> guard { cache_mutex };
				for ( const auto& cache : caches )
				{
					if ( same_prototype_support_config( cache->config, config ) )
						return *cache;
				}

				auto cache = std::make_unique<SupportCache>();
				cache->config = config;
				prewarm_support_descriptor_transitions( *cache );
				caches.push_back( std::move( cache ) );
				return *caches.back();
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
					SupportNormalizedFrontierPair normalized_current {};
					if ( cache != nullptr && child_suffix_length <= effective_cache_config.support_max_suffix )
					{
						if ( stats != nullptr )
						{
							++stats->prototype_transition_proposals;
							record_support_suffix_stat(
								stats->prototype_transition_proposals_by_suffix,
								child_suffix_length );
						}
						normalized_current =
							normalize_support_frontier_pair_absolute(
								frontier_by_current_carry_difference,
								n,
								child_suffix_length );
						SupportCanonicalDescriptor canonical_current =
							canonical_support_descriptor(
								normalized_current,
								child_suffix_length,
								effective_cache_config );
						const std::uint32_t canonical_constant_bit =
							constant_bit ^ ( canonical_current.swapped_count_orientation ? 1u : 0u );
						const int support_context =
							support_context_index(
								input_difference_bit,
								canonical_constant_bit );
						const std::array<std::uint64_t, 2> current_source_global_base_q =
							support_frontier_global_base_q_pair( canonical_current.frontier );
						SupportNormalizedFrontierPair normalized_next {};
						bool has_support_transition_value = false;
						{
							std::lock_guard<std::mutex> guard { cache->transitions_mutex };
							const SupportTransitionCacheEntry* entry =
								find_support_transition_cache_entry(
									*cache,
									support_transition_cache_hash( canonical_current ),
									support_descriptor_key_view( canonical_current ),
									canonical_current.key );
							if ( entry != nullptr )
							{
								const SupportTransitionValue& transition_value =
									entry->group.by_context[ static_cast<std::size_t>( support_context ) ];
								normalized_next =
									materialize_support_tagged_transition_relative(
										*cache,
										entry->group,
										transition_value,
										input_difference_bit,
										entry->group.source_global_base_q,
										current_source_global_base_q );
								has_support_transition_value = true;
							}
							else if ( stats != nullptr )
							{
								if ( support_transition_hash_bucket_exists(
										 cache->transitions,
										 support_transition_cache_hash( canonical_current ) ) )
								{
									++stats->prototype_transition_guard_misses;
									record_support_suffix_stat(
										stats->prototype_transition_guard_misses_by_suffix,
										child_suffix_length );
								}
								else
								{
									++stats->prototype_transition_key_misses;
									record_support_suffix_stat(
										stats->prototype_transition_key_misses_by_suffix,
										child_suffix_length );
								}
							}
						}

						if ( !has_support_transition_value )
						{
							SupportTransitionPendingGroup transition_group =
								make_support_transition_group_from_descriptor_source(
									canonical_current.frontier );

							std::lock_guard<std::mutex> guard { cache->transitions_mutex };
							const SupportTransitionCacheEntry* active_entry =
								find_support_transition_cache_entry(
									*cache,
									support_transition_cache_hash( canonical_current ),
									support_descriptor_key_view( canonical_current ),
									canonical_current.key );
							const bool existed_before = active_entry != nullptr;
							if ( active_entry == nullptr )
							{
								active_entry =
									&get_or_insert_support_transition_cache_entry(
										*cache,
										canonical_current,
										std::move( transition_group ) );
							}
							const SupportTransitionValue& transition_value =
								active_entry->group.by_context[ static_cast<std::size_t>( support_context ) ];
							normalized_next =
								materialize_support_tagged_transition_relative(
									*cache,
									active_entry->group,
									transition_value,
									input_difference_bit,
									active_entry->group.source_global_base_q,
									current_source_global_base_q );
							has_support_transition_value = true;
							if ( !existed_before )
							{
								if ( stats != nullptr )
								{
									++stats->prototype_transition_cache_inserts;
									record_support_suffix_stat(
										stats->prototype_transition_cache_inserts_by_suffix,
										child_suffix_length );
								}
							}
						}

						if ( canonical_current.swapped_count_orientation )
							normalized_next =
								swap_support_count_coordinates( normalized_next );
						frontier_by_current_carry_difference =
							materialize_support_frontier_pair_absolute(
								normalized_next,
								n,
								child_suffix_length + 1 );
						if ( stats != nullptr )
						{
							++stats->prototype_transition_hits;
							record_support_suffix_stat(
								stats->prototype_transition_hits_by_suffix,
								child_suffix_length );
						}
						support_hit = true;
					}

					if ( support_hit )
						continue;

					if ( stats != nullptr && cache != nullptr && child_suffix_length <= effective_cache_config.support_max_suffix )
					{
						++stats->prototype_transition_fallbacks;
						record_support_suffix_stat(
							stats->prototype_transition_fallbacks_by_suffix,
							child_suffix_length );
					}
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
