/**
 * @file linear_varconst_fixed_beta_core_impl.hpp
 * @brief Internal exact Fixed-beta var-const Q2 carry/segment core.
 *
 * Included by linear_varconst_optimal_mask.hpp after the public
 * request/result surface is declared.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/arx_analysis_operators/DefineSearchWeight.hpp"
#include "include/arx_analysis_operators/SignedInteger128Bit.hpp"
#include "include/arx_analysis_operators/UnsignedInteger128Bit.hpp"
#include "include/arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

		enum class VarConstQ2Direction : std::uint8_t
		{
			fixed_alpha_to_beta,
			fixed_beta_to_alpha,
		};

		enum class VarConstQ2Operation : std::uint8_t
		{
			modular_add,
			modular_sub,
		};

		enum class VarConstQ2MainlineMethod : std::uint8_t
		{
			projective_support_summary,
		};

		struct VarConstQ2MainlineStats
		{
			std::size_t memo_states { 0 };
			std::size_t queue_pops { 0 };
			std::size_t queue_pushes { 0 };
			std::size_t segment_root_entries { 0 };
			std::size_t segment_max_entries { 0 };
			std::size_t segment_combine_pairs { 0 };
			std::size_t segment_raw_block_count { 0 };
			std::size_t segment_powered_leaf_count { 0 };
			std::size_t segment_power_runs { 0 };
			std::size_t segment_power_collapsed_blocks { 0 };
			std::size_t segment_descriptor_tree_leaf_classes { 0 };
			std::size_t segment_descriptor_tree_unique_leaf_nodes { 0 };
			std::size_t segment_descriptor_tree_unique_nodes { 0 };
			std::size_t segment_descriptor_tree_reuse_hits { 0 };
			std::size_t support_root_entries { 0 };
			std::size_t support_max_entries { 0 };
			std::size_t support_frontier_peak_entries { 0 };
			std::size_t support_frontier_transitions { 0 };
			std::size_t recursive_total_frontier_entries { 0 };
			std::size_t recursive_max_frontier_entries { 0 };
		};

		struct VarConstQ2MainlineRequest
		{
			std::uint64_t fixed_mask { 0 };
			std::uint64_t constant { 0 };
			int n { 0 };
			VarConstQ2Direction direction { VarConstQ2Direction::fixed_beta_to_alpha };
			VarConstQ2Operation operation { VarConstQ2Operation::modular_add };
		};

		struct VarConstQ2MainlineResult
		{
			std::uint64_t optimal_mask { 0 };
			std::uint64_t abs_weight { 0 };
			bool abs_weight_is_exact_2pow64 { false };
			double abs_correlation { 0.0 };
			SearchWeight ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

		struct VarConstOptimalInputMaskResult
		{
			std::uint64_t alpha { 0 };
			std::uint64_t abs_weight { 0 };
			bool abs_weight_is_exact_2pow64 { false };
			double abs_correlation { 0.0 };
			SearchWeight ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

}  // namespace arx_operators
}  // namespace TwilightDream


namespace TwilightDream
{
	namespace arx_operators
	{
		namespace detail_varconst_carry_dp
		{
			using uint128_t = UnsignedInteger128Bit;
			using CarryWide = SignedInteger128Bit;

			struct ExactAbsWeight
			{
				std::uint64_t packed_weight { 0 };
				bool		  is_exact_2pow64 { false };
			};

			struct VarConstLayerDpSummary
			{
				std::uint64_t  optimal_mask { 0 };
				ExactAbsWeight abs_weight {};
			};

			struct BitMatrix
			{
				int m00 { 0 };
				int m01 { 0 };
				int m10 { 0 };
				int m11 { 0 };
			};

			struct VarConstQ2BitModel
			{
				std::array<BitMatrix, 64> choose_mask0 {};
				std::array<BitMatrix, 64> choose_mask1 {};
				int						  n { 0 };
			};

			struct ExactWideMatrix
			{
				CarryWide m00 { 0 };
				CarryWide m01 { 0 };
				CarryWide m10 { 0 };
				CarryWide m11 { 0 };
			};

			struct DirectionKey
			{
				CarryWide x { 0 };
				CarryWide y { 0 };

				[[nodiscard]] bool operator==( const DirectionKey& other ) const noexcept
				{
					return x == other.x && y == other.y;
				}
			};

			struct DirectionKeyHash
			{
				[[nodiscard]] std::size_t operator()( const DirectionKey& key ) const noexcept;
			};

			struct ProjectiveMatrixKey
			{
				CarryWide m00 { 0 };
				CarryWide m01 { 0 };
				CarryWide m10 { 0 };
				CarryWide m11 { 0 };

				[[nodiscard]] bool operator==( const ProjectiveMatrixKey& other ) const noexcept
				{
					return m00 == other.m00 && m01 == other.m01 && m10 == other.m10 && m11 == other.m11;
				}
			};

			struct ProjectiveMatrixKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveMatrixKey& key ) const noexcept;
			};

			struct ProjectiveMatrixSummaryValue
			{
				uint128_t	  scale { 0 };
				std::uint64_t mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveMatrixSummaryValue& other ) const noexcept
				{
					return scale == other.scale && mask == other.mask;
				}
			};

			struct ProjectiveSegmentSummary
			{
				int																							   bit_length { 0 };
				std::unordered_map<ProjectiveMatrixKey, ProjectiveMatrixSummaryValue, ProjectiveMatrixKeyHash> table {};
			};

			struct ProjectiveSegmentSummaryStats
			{
				std::size_t root_entries { 0 };
				std::size_t max_segment_entries { 0 };
				std::size_t combine_pairs { 0 };
			};

			struct ProjectiveRootQueryValue
			{
				uint128_t	  scale { 0 };
				std::uint64_t mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootQueryValue& other ) const noexcept
				{
					return scale == other.scale && mask == other.mask;
				}
			};

			struct ProjectiveRootQuerySummary
			{
				int																			 bit_length { 0 };
				std::unordered_map<DirectionKey, ProjectiveRootQueryValue, DirectionKeyHash> table {};
			};

			enum class ProjectiveSlopeKind : std::uint8_t
			{
				finite,
				infinity,
			};

			struct ProjectiveSlopeKey
			{
				ProjectiveSlopeKind kind { ProjectiveSlopeKind::finite };
				CarryWide			num { 0 };
				CarryWide			den { 1 };

				[[nodiscard]] bool operator==( const ProjectiveSlopeKey& other ) const noexcept
				{
					return kind == other.kind && num == other.num && den == other.den;
				}
			};

			struct ProjectiveSlopeKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveSlopeKey& key ) const noexcept;
			};

			struct ProjectiveRootSlopeRelativeProfileValue
			{
				uint128_t	  relative_scale { 0 };
				std::uint64_t relative_mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootSlopeRelativeProfileValue& other ) const noexcept
				{
					return relative_scale == other.relative_scale && relative_mask == other.relative_mask;
				}
			};

			struct ProjectiveRootSlopeSummary
			{
				int																						 bit_length { 0 };
				std::unordered_map<ProjectiveSlopeKey, ProjectiveRootQueryValue, ProjectiveSlopeKeyHash> table {};
			};

			struct ProjectiveRootSlopeRelativeProfileSummary
			{
				int																										bit_length { 0 };
				std::unordered_map<ProjectiveSlopeKey, ProjectiveRootSlopeRelativeProfileValue, ProjectiveSlopeKeyHash> table {};
			};

			struct ProjectiveRootSlopeSyncCompetitionKey
			{
				int				   run_bit { 0 };
				ProjectiveSlopeKey child_slope {};
				uint128_t		   relative_scale { 0 };
				std::uint64_t	   shifted_suffix_mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootSlopeSyncCompetitionKey& other ) const noexcept
				{
					return run_bit == other.run_bit && child_slope == other.child_slope && relative_scale == other.relative_scale && shifted_suffix_mask == other.shifted_suffix_mask;
				}
			};

			struct ProjectiveRootSlopeSyncCompetitionKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveRootSlopeSyncCompetitionKey& key ) const noexcept;
			};

			using ProjectiveRootSlopeSyncCompetitionGroupMap = std::unordered_map<ProjectiveRootSlopeSyncCompetitionKey, std::vector<std::uint64_t>, ProjectiveRootSlopeSyncCompetitionKeyHash>;

			struct ProjectiveRootSlopeLiveSyncShadowSummary
			{
				int										  bit_length { 0 };
				ProjectiveRootSlopeRelativeProfileSummary profile {};
				std::uint64_t							  live_bit_support { 0 };
				std::uint64_t							  live_anchor_mask { 0 };
			};

			struct ProjectiveRootSlopeSyncWinnerControlEntry
			{
				ProjectiveRootSlopeSyncCompetitionKey competition {};
				std::uint64_t						  winner_relative_mask { 0 };
			};

			struct ProjectiveRootSlopeSyncWinnerControlSummary
			{
				int													   bit_length { 0 };
				ProjectiveRootSlopeRelativeProfileSummary			   profile {};
				std::vector<ProjectiveRootSlopeSyncWinnerControlEntry> winners {};
			};

			struct ProjectiveSupportVectorKey
			{
				CarryWide x { 0 };
				CarryWide y { 0 };

				[[nodiscard]] bool operator==( const ProjectiveSupportVectorKey& other ) const noexcept
				{
					return x == other.x && y == other.y;
				}
			};

			struct ProjectiveSupportVectorKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveSupportVectorKey& key ) const noexcept;
			};

			struct ProjectiveSupportVectorValue
			{
				uint128_t	  scale { 0 };
				std::uint64_t mask { 0 };
			};

			struct ProjectiveSupportSummary
			{
				int																											 bit_length { 0 };
				std::unordered_map<ProjectiveSupportVectorKey, ProjectiveSupportVectorValue, ProjectiveSupportVectorKeyHash> table {};
			};

			struct ProjectiveSupportSummaryStats
			{
				std::size_t root_entries { 0 };
				std::size_t max_entries { 0 };
				std::size_t frontier_peak_entries { 0 };
				std::size_t frontier_transitions { 0 };
			};

			struct HomogeneousQ2ChoiceBlockDescriptor
			{
				BitMatrix choose_mask0 {};
				BitMatrix choose_mask1 {};
				int		  len { 0 };

				[[nodiscard]] bool operator==( const HomogeneousQ2ChoiceBlockDescriptor& other ) const noexcept;
			};

			struct HomogeneousQ2ChoiceBlockDescriptorHash
			{
				[[nodiscard]] std::size_t operator()( const HomogeneousQ2ChoiceBlockDescriptor& descriptor ) const noexcept;
			};

			struct ProjectiveSuffixChoice
			{
				ExactAbsWeight best_abs {};
				std::uint64_t  best_suffix_mask { std::numeric_limits<std::uint64_t>::max() };
			};

			struct ProjectiveState
			{
				DirectionKey dir {};
				uint128_t scale { 0 };
			};

			struct ProjectiveBellmanKey
			{
				int bit_index { 0 };
				DirectionKey dir {};

				[[nodiscard]] bool operator==( const ProjectiveBellmanKey& other ) const noexcept
				{
					return bit_index == other.bit_index && dir == other.dir;
				}
			};

			struct ProjectiveBellmanKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveBellmanKey& key ) const noexcept;
			};

			using ProjectiveBellmanMemo =
				std::unordered_map<ProjectiveBellmanKey, ProjectiveSuffixChoice, ProjectiveBellmanKeyHash>;

			struct StateKey
			{
				CarryWide v0 { 0 };
				CarryWide v1 { 0 };

				[[nodiscard]] bool operator==( const StateKey& other ) const noexcept
				{
					return v0 == other.v0 && v1 == other.v1;
				}
			};

			struct StateKeyHash
			{
				[[nodiscard]] std::size_t operator()( const StateKey& key ) const noexcept;
			};

			using CarryLayer = std::unordered_map<StateKey, std::uint64_t, StateKeyHash>;

			struct ProjectiveRootSlopeSyncWinnerPathSummary
			{
				std::uint64_t winner_relative_mask { 0 };
				std::uint8_t  query_depth { 0 };
				std::uint64_t winner_trace_word { 0 };
				std::uint64_t queried_bit_support { 0 };
			};

			struct FixedBetaFlatTheoremArtifacts
			{
				ProjectiveSegmentSummary	  summary {};
				ProjectiveSegmentSummaryStats segment_stats {};
				std::size_t					  raw_block_count { 0 };
				std::size_t					  powered_leaf_count { 0 };
				std::size_t					  power_runs { 0 };
				std::size_t					  power_collapsed_blocks { 0 };
				std::size_t					  descriptor_tree_leaf_classes { 0 };
				std::size_t					  descriptor_tree_unique_leaf_nodes { 0 };
				std::size_t					  descriptor_tree_unique_nodes { 0 };
				std::size_t					  descriptor_tree_reuse_hits { 0 };
			};

			[[nodiscard]] constexpr std::uint64_t mask_n( int n ) noexcept
			{
				if ( n <= 0 )
					return 0ull;
				if ( n >= 64 )
					return ~std::uint64_t( 0 );
				return ( std::uint64_t( 1 ) << n ) - 1ull;
			}

			[[nodiscard]] constexpr int floor_log2_u64( std::uint64_t x ) noexcept
			{
				return ( x == 0ull ) ? -1 : static_cast<int>( std::bit_width( x ) - 1u );
			}

			[[nodiscard]] constexpr std::uint64_t packed_exact_2pow64_weight() noexcept
			{
				return 0ull;
			}

			[[nodiscard]] inline CarryWide carry_wide_zero() noexcept
			{
				return {};
			}

			[[nodiscard]] inline CarryWide carry_wide_from_i64( std::int64_t value ) noexcept
			{
				return CarryWide( value );
			}

			[[nodiscard]] inline CarryWide carry_wide_from_u128_bits( uint128_t value ) noexcept
			{
				return CarryWide::from_words( value.high64(), value.low64() );
			}

			[[nodiscard]] inline uint128_t carry_wide_abs_to_u128( CarryWide value ) noexcept
			{
				return value.magnitude_bits();
			}

			[[nodiscard]] inline uint128_t uabs128_twos( CarryWide value ) noexcept
			{
				return value.magnitude_bits();
			}

			[[nodiscard]] inline std::size_t hash_carry_wide( CarryWide value ) noexcept
			{
				return std::hash<CarryWide> {}( value );
			}

			[[nodiscard]] inline std::size_t hash_u128( uint128_t x ) noexcept
			{
				const std::uint64_t lo = static_cast<std::uint64_t>( x );
				const std::uint64_t hi = x.high64();
				const std::size_t	h0 = std::hash<std::uint64_t> {}( lo );
				const std::size_t	h1 = std::hash<std::uint64_t> {}( hi );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ULL + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			[[nodiscard]] bool exact_abs_weight_is_zero( const ExactAbsWeight& w ) noexcept
			{
				return !w.is_exact_2pow64 && w.packed_weight == 0ull;
			}

			[[nodiscard]] inline uint128_t exact_abs_weight_to_u128( const ExactAbsWeight& w ) noexcept
			{
				if ( exact_abs_weight_is_zero( w ) )
					return {};
				if ( w.is_exact_2pow64 )
					return uint128_t { 1 } << 64;
				return uint128_t { w.packed_weight };
			}

			[[nodiscard]] bool exact_abs_weight_equal( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept
			{
				return exact_abs_weight_to_u128( lhs ) == exact_abs_weight_to_u128( rhs );
			}

			[[nodiscard]] bool exact_abs_weight_less( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept
			{
				return exact_abs_weight_to_u128( lhs ) < exact_abs_weight_to_u128( rhs );
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_weight_from_u128( uint128_t value ) noexcept
			{
				if ( value == 0 )
					return {};
				if ( value == ( uint128_t { 1 } << 64 ) )
					return { packed_exact_2pow64_weight(), true };
				return { static_cast<std::uint64_t>( value ), false };
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_weight_from_u128_projective( uint128_t value ) noexcept
			{
				if ( value == 0 )
					return {};
				if ( value == ( uint128_t { 1 } << 64 ) )
					return { packed_exact_2pow64_weight(), true };
				if ( value > ( uint128_t { 1 } << 64 ) )
					return { std::numeric_limits<std::uint64_t>::max(), false };
				return { static_cast<std::uint64_t>( value ), false };
			}

			[[nodiscard]] double exact_abs_weight_to_correlation( const ExactAbsWeight& w, int n ) noexcept
			{
				if ( exact_abs_weight_is_zero( w ) )
					return 0.0;
				if ( w.is_exact_2pow64 )
					return 1.0;
				return std::ldexp( static_cast<double>( w.packed_weight ), -n );
			}

			[[nodiscard]] SearchWeight exact_abs_weight_to_ceil_linear_weight_int( const ExactAbsWeight& w, int n ) noexcept
			{
				if ( exact_abs_weight_is_zero( w ) )
					return TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
				if ( w.is_exact_2pow64 )
					return 0;
				return static_cast<SearchWeight>( n - floor_log2_u64( w.packed_weight ) );
			}

			[[nodiscard]] inline VarConstOptimalInputMaskResult make_alpha_result( std::uint64_t alpha, const ExactAbsWeight& abs_weight, int n ) noexcept
			{
				VarConstOptimalInputMaskResult out {};
				out.alpha = alpha;
				out.abs_weight = abs_weight.packed_weight;
				out.abs_weight_is_exact_2pow64 = abs_weight.is_exact_2pow64;
				out.abs_correlation = exact_abs_weight_to_correlation( abs_weight, n );
				out.ceil_linear_weight_int = exact_abs_weight_to_ceil_linear_weight_int( abs_weight, n );
				return out;
			}

			[[nodiscard]] inline VarConstQ2MainlineResult make_q2_mainline_result( std::uint64_t optimal_mask, const ExactAbsWeight& abs_weight, int n ) noexcept
			{
				VarConstQ2MainlineResult out {};
				out.optimal_mask = optimal_mask;
				out.abs_weight = abs_weight.packed_weight;
				out.abs_weight_is_exact_2pow64 = abs_weight.is_exact_2pow64;
				out.abs_correlation = exact_abs_weight_to_correlation( abs_weight, n );
				out.ceil_linear_weight_int = exact_abs_weight_to_ceil_linear_weight_int( abs_weight, n );
				return out;
			}

			[[nodiscard]] inline VarConstOptimalInputMaskResult layer_dp_summary_to_alpha( const VarConstLayerDpSummary& s, int n ) noexcept
			{
				return make_alpha_result( s.optimal_mask, s.abs_weight, n );
			}

			[[nodiscard]] VarConstQ2MainlineResult layer_dp_summary_to_q2_mainline( const VarConstLayerDpSummary& s, int n ) noexcept
			{
				return make_q2_mainline_result( s.optimal_mask, s.abs_weight, n );
			}

			[[nodiscard]] VarConstOptimalInputMaskResult q2_mainline_to_alpha_result( const VarConstQ2MainlineResult& r ) noexcept
			{
				VarConstOptimalInputMaskResult out {};
				out.alpha = r.optimal_mask;
				out.abs_weight = r.abs_weight;
				out.abs_weight_is_exact_2pow64 = r.abs_weight_is_exact_2pow64;
				out.abs_correlation = r.abs_correlation;
				out.ceil_linear_weight_int = r.ceil_linear_weight_int;
				return out;
			}

			[[nodiscard]] inline bool bit_matrix_equal( const BitMatrix& lhs, const BitMatrix& rhs ) noexcept
			{
				return lhs.m00 == rhs.m00 && lhs.m01 == rhs.m01 && lhs.m10 == rhs.m10 && lhs.m11 == rhs.m11;
			}

			inline std::size_t DirectionKeyHash::operator()( const DirectionKey& key ) const noexcept
			{
				return hash_carry_wide( key.x ) ^ ( hash_carry_wide( key.y ) + 0x9e3779b97f4a7c15ULL );
			}

			inline std::size_t ProjectiveBellmanKeyHash::operator()( const ProjectiveBellmanKey& key ) const noexcept
			{
				std::size_t h = std::hash<int> {}( key.bit_index );
				h ^= DirectionKeyHash {}( key.dir ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t ProjectiveSlopeKeyHash::operator()( const ProjectiveSlopeKey& key ) const noexcept
			{
				std::size_t h = std::hash<int> {}( static_cast<int>( key.kind ) );
				h ^= hash_carry_wide( key.num ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= hash_carry_wide( key.den ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			[[nodiscard]] inline bool projective_slope_key_less( const ProjectiveSlopeKey& lhs, const ProjectiveSlopeKey& rhs ) noexcept
			{
				if ( lhs.kind != rhs.kind )
					return static_cast<int>( lhs.kind ) < static_cast<int>( rhs.kind );
				if ( lhs.num != rhs.num )
					return lhs.num < rhs.num;
				return lhs.den < rhs.den;
			}

			inline std::size_t ProjectiveMatrixKeyHash::operator()( const ProjectiveMatrixKey& key ) const noexcept
			{
				std::size_t h = hash_carry_wide( key.m00 );
				h ^= hash_carry_wide( key.m01 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= hash_carry_wide( key.m10 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= hash_carry_wide( key.m11 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t ProjectiveSupportVectorKeyHash::operator()( const ProjectiveSupportVectorKey& key ) const noexcept
			{
				const std::size_t h0 = hash_carry_wide( key.x );
				const std::size_t h1 = hash_carry_wide( key.y );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ULL + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			inline std::size_t ProjectiveRootSlopeSyncCompetitionKeyHash::operator()( const ProjectiveRootSlopeSyncCompetitionKey& key ) const noexcept
			{
				std::size_t		  h = std::hash<int> {}( key.run_bit );
				const std::size_t slope_hash = ProjectiveSlopeKeyHash {}( key.child_slope );
				h ^= slope_hash + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<std::uint64_t> {}( static_cast<std::uint64_t>( key.relative_scale >> 64 ) ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<std::uint64_t> {}( static_cast<std::uint64_t>( key.relative_scale ) ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<std::uint64_t> {}( key.shifted_suffix_mask ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline bool HomogeneousQ2ChoiceBlockDescriptor::operator==( const HomogeneousQ2ChoiceBlockDescriptor& other ) const noexcept
			{
				return len == other.len && bit_matrix_equal( choose_mask0, other.choose_mask0 ) && bit_matrix_equal( choose_mask1, other.choose_mask1 );
			}

			[[nodiscard]] inline std::size_t hash_bit_matrix( const BitMatrix& m ) noexcept
			{
				std::size_t h = std::hash<int> {}( m.m00 );
				h ^= std::hash<int> {}( m.m01 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( m.m10 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( m.m11 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t HomogeneousQ2ChoiceBlockDescriptorHash::operator()( const HomogeneousQ2ChoiceBlockDescriptor& descriptor ) const noexcept
			{
				std::size_t h = hash_bit_matrix( descriptor.choose_mask0 );
				h ^= hash_bit_matrix( descriptor.choose_mask1 ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( descriptor.len ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t StateKeyHash::operator()( const StateKey& key ) const noexcept
			{
				const std::size_t h0 = hash_carry_wide( key.v0 );
				const std::size_t h1 = hash_carry_wide( key.v1 );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ULL + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			struct ProjectiveSegmentSummaryHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveSegmentSummary& summary ) const noexcept;
			};

			struct ProjectiveSegmentSummaryEqual
			{
				[[nodiscard]] bool operator()( const ProjectiveSegmentSummary& lhs, const ProjectiveSegmentSummary& rhs ) const noexcept;
			};

			[[nodiscard]] inline int carry_out_bit( int x, int constant_i, int carry_in ) noexcept
			{
				return ( x + constant_i + carry_in ) >> 1;
			}

			[[nodiscard]] BitMatrix build_matrix_for_beta_bit( int alpha_i, int constant_i, int beta_i ) noexcept
			{
				BitMatrix M {};
				for ( int cin = 0; cin <= 1; ++cin )
				{
					for ( int x = 0; x <= 1; ++x )
					{
						const int cout = carry_out_bit( x, constant_i, cin );
						const int zi = x ^ constant_i ^ cin;
						const int exponent = ( alpha_i & x ) ^ ( beta_i & zi );
						const int sign = exponent ? -1 : 1;
						if ( cin == 0 && cout == 0 )
							M.m00 += sign;
						else if ( cin == 0 && cout == 1 )
							M.m01 += sign;
						else if ( cin == 1 && cout == 0 )
							M.m10 += sign;
						else
							M.m11 += sign;
					}
				}
				return M;
			}

			[[nodiscard]] inline VarConstQ2BitModel build_q2_bit_model_from_fixed_side( std::uint64_t fixed_mask, std::uint64_t add_constant, int n, bool fixed_mask_is_alpha ) noexcept
			{
				VarConstQ2BitModel model {};
				model.n = n;
				for ( int bit = 0; bit < n; ++bit )
				{
					const int fixed_i = static_cast<int>( ( fixed_mask >> bit ) & 1ull );
					const int constant_i = static_cast<int>( ( add_constant >> bit ) & 1ull );
					if ( fixed_mask_is_alpha )
					{
						model.choose_mask0[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( fixed_i, constant_i, 0 );
						model.choose_mask1[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( fixed_i, constant_i, 1 );
					}
					else
					{
						model.choose_mask0[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( 0, constant_i, fixed_i );
						model.choose_mask1[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( 1, constant_i, fixed_i );
					}
				}
				return model;
			}

			[[nodiscard]] inline VarConstQ2BitModel build_output_mask_q2_bit_model( std::uint64_t alpha, std::uint64_t add_constant, int n ) noexcept
			{
				return build_q2_bit_model_from_fixed_side( alpha, add_constant, n, true );
			}

			[[nodiscard]] inline VarConstQ2BitModel build_input_mask_q2_bit_model( std::uint64_t beta, std::uint64_t add_constant, int n ) noexcept
			{
				return build_q2_bit_model_from_fixed_side( beta, add_constant, n, false );
			}

			inline std::size_t ProjectiveSegmentSummaryHash::operator()( const ProjectiveSegmentSummary& summary ) const noexcept
			{
				std::size_t sum_hash = 0u;
				std::size_t xor_hash = 0u;
				for ( const auto& [ key, value ] : summary.table )
				{
					std::size_t h = ProjectiveMatrixKeyHash {}( key );
					h ^= hash_u128( value.scale ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
					h ^= std::hash<std::uint64_t> {}( value.mask ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
					sum_hash += h;
					xor_hash ^= h + 0x9e3779b97f4a7c15ULL + ( xor_hash << 6 ) + ( xor_hash >> 2 );
				}

				std::size_t out = std::hash<int> {}( summary.bit_length );
				out ^= std::hash<std::size_t> {}( summary.table.size() ) + 0x9e3779b97f4a7c15ULL + ( out << 6 ) + ( out >> 2 );
				out ^= sum_hash + 0x9e3779b97f4a7c15ULL + ( out << 6 ) + ( out >> 2 );
				out ^= xor_hash + 0x9e3779b97f4a7c15ULL + ( out << 6 ) + ( out >> 2 );
				return out;
			}

			inline bool ProjectiveSegmentSummaryEqual::operator()( const ProjectiveSegmentSummary& lhs, const ProjectiveSegmentSummary& rhs ) const noexcept
			{
				if ( lhs.bit_length != rhs.bit_length || lhs.table.size() != rhs.table.size() )
				{
					return false;
				}

				for ( const auto& [ key, value ] : lhs.table )
				{
					const auto it = rhs.table.find( key );
					if ( it == rhs.table.end() || !( it->second == value ) )
						return false;
				}
				return true;
			}

			inline void apply_row_step( const BitMatrix& M, CarryWide v0, CarryWide v1, CarryWide& o0, CarryWide& o1 ) noexcept
			{
				o0 = v0 * carry_wide_from_i64( M.m00 ) + v1 * carry_wide_from_i64( M.m10 );
				o1 = v0 * carry_wide_from_i64( M.m01 ) + v1 * carry_wide_from_i64( M.m11 );
			}

			[[nodiscard]] ExactAbsWeight exact_abs_sum_v0_v1( CarryWide v0, CarryWide v1 ) noexcept
			{
				const CarryWide sum = v0 + v1;
				const uint128_t mag = carry_wide_abs_to_u128( sum );
				if ( mag == 0 )
					return {};
				if ( mag == ( uint128_t { 1 } << 64 ) )
					return { packed_exact_2pow64_weight(), true };
				return { static_cast<std::uint64_t>( mag ), false };
			}

			[[nodiscard]] inline uint128_t gcd_u128( uint128_t a, uint128_t b ) noexcept
			{
				while ( b != 0 )
				{
					const uint128_t t = a % b;
					a = b;
					b = t;
				}
				return a;
			}

			[[nodiscard]] inline CarryWide carry_wide_div_exact_by_u128( CarryWide value, uint128_t divisor ) noexcept
			{
				if ( divisor == 0 )
					return carry_wide_zero();

				const bool		neg = value < 0;
				const uint128_t abs_q = carry_wide_abs_to_u128( value ) / divisor;
				const CarryWide q = carry_wide_from_u128_bits( abs_q );
				return neg ? -q : q;
			}

			[[nodiscard]] std::pair<DirectionKey, uint128_t> normalize_projective_direction( CarryWide v0, CarryWide v1 ) noexcept
			{
				if ( v0 == 0 && v1 == 0 )
					return { DirectionKey {}, 0 };

				const uint128_t a = uabs128_twos( v0 );
				const uint128_t b = uabs128_twos( v1 );
				const uint128_t g = gcd_u128( a, b );
				if ( g == 0 )
					return { DirectionKey {}, 0 };

				CarryWide x = carry_wide_div_exact_by_u128( v0, g );
				CarryWide y = carry_wide_div_exact_by_u128( v1, g );
				if ( x < 0 || ( x == 0 && y < 0 ) )
				{
					x = -x;
					y = -y;
				}
				return { DirectionKey { x, y }, g };
			}

			[[nodiscard]] inline ProjectiveSlopeKey make_infinite_projective_slope() noexcept
			{
				return ProjectiveSlopeKey { ProjectiveSlopeKind::infinity, carry_wide_zero(), carry_wide_zero() };
			}

			[[nodiscard]] inline ProjectiveSlopeKey normalize_projective_slope( CarryWide num, CarryWide den ) noexcept
			{
				if ( den == 0 )
					return make_infinite_projective_slope();
				if ( num == 0 )
				{
					return ProjectiveSlopeKey { ProjectiveSlopeKind::finite, carry_wide_zero(), carry_wide_from_i64( 1 ) };
				}

				uint128_t g = gcd_u128( carry_wide_abs_to_u128( num ), carry_wide_abs_to_u128( den ) );
				if ( g != 0 )
				{
					num = carry_wide_div_exact_by_u128( num, g );
					den = carry_wide_div_exact_by_u128( den, g );
				}
				if ( den < 0 )
				{
					num = -num;
					den = -den;
				}
				return ProjectiveSlopeKey { ProjectiveSlopeKind::finite, num, den };
			}

			[[nodiscard]] inline ProjectiveSlopeKey projective_slope_from_direction( const DirectionKey& dir ) noexcept
			{
				if ( dir.x == 0 )
					return make_infinite_projective_slope();
				return normalize_projective_slope( dir.y, dir.x );
			}

			[[nodiscard]] inline DirectionKey direction_from_projective_slope( const ProjectiveSlopeKey& slope ) noexcept
			{
				if ( slope.kind == ProjectiveSlopeKind::infinity )
					return DirectionKey { carry_wide_zero(), carry_wide_from_i64( 1 ) };
				return DirectionKey { slope.den, slope.num };
			}

			[[nodiscard]] inline std::pair<ProjectiveSupportVectorKey, uint128_t> normalize_projective_support_vector( CarryWide x, CarryWide y ) noexcept
			{
				if ( x == 0 && y == 0 )
					return { {}, 0 };

				const uint128_t ax = uabs128_twos( x );
				const uint128_t ay = uabs128_twos( y );
				const uint128_t g = gcd_u128( ax, ay );
				if ( g == 0 )
					return { {}, 0 };

				x = carry_wide_div_exact_by_u128( x, g );
				y = carry_wide_div_exact_by_u128( y, g );
				if ( x < 0 || ( x == 0 && y < 0 ) )
				{
					x = -x;
					y = -y;
				}
				return { ProjectiveSupportVectorKey { x, y }, g };
			}

			[[nodiscard]] ExactAbsWeight scale_exact_abs_weight_projective( const ExactAbsWeight& w, uint128_t scale ) noexcept
			{
				if ( scale == 0 || exact_abs_weight_is_zero( w ) )
					return {};
				if ( w.is_exact_2pow64 )
					return exact_abs_weight_from_u128_projective( ( uint128_t { 1 } << 64 ) * scale );
				return exact_abs_weight_from_u128_projective( uint128_t { w.packed_weight } * scale );
			}

			[[nodiscard]] ProjectiveState apply_projective_step( const BitMatrix& matrix, const DirectionKey& dir ) noexcept
			{
				CarryWide out0 = carry_wide_zero();
				CarryWide out1 = carry_wide_zero();
				apply_row_step( matrix, dir.x, dir.y, out0, out1 );
				const auto [ next_dir, scale ] = normalize_projective_direction( out0, out1 );
				return ProjectiveState { next_dir, scale };
			}

			[[nodiscard]] ProjectiveSuffixChoice solve_projective_bellman_suffix_choice(
				int bit_index,
				const DirectionKey& dir,
				const std::array<BitMatrix, 64>& ordered_mats_alpha0,
				const std::array<BitMatrix, 64>& ordered_mats_alpha1,
				int nbits,
				ProjectiveBellmanMemo& memo ) noexcept
			{
				if ( bit_index >= nbits )
				{
					return ProjectiveSuffixChoice {
						exact_abs_sum_v0_v1( dir.x, dir.y ),
						0ull };
				}

				const ProjectiveBellmanKey key { bit_index, dir };
				if ( const auto it = memo.find( key ); it != memo.end() )
					return it->second;

				ProjectiveSuffixChoice best {};
				bool have_best = false;
				for ( int choice = 0; choice <= 1; ++choice )
				{
					const BitMatrix& matrix =
						choice == 0 ?
							ordered_mats_alpha0[ static_cast<std::size_t>( bit_index ) ] :
							ordered_mats_alpha1[ static_cast<std::size_t>( bit_index ) ];
					const ProjectiveState child = apply_projective_step( matrix, dir );
					if ( child.scale == 0 )
						continue;

					ProjectiveSuffixChoice suffix =
						solve_projective_bellman_suffix_choice(
							bit_index + 1,
							child.dir,
							ordered_mats_alpha0,
							ordered_mats_alpha1,
							nbits,
							memo );
					const ExactAbsWeight total_abs =
						scale_exact_abs_weight_projective( suffix.best_abs, child.scale );
					const std::uint64_t total_mask =
						suffix.best_suffix_mask |
						( static_cast<std::uint64_t>( choice ) << bit_index );

					if ( !have_best ||
						 exact_abs_weight_less( best.best_abs, total_abs ) ||
						 ( exact_abs_weight_equal( total_abs, best.best_abs ) && total_mask < best.best_suffix_mask ) )
					{
						best.best_abs = total_abs;
						best.best_suffix_mask = total_mask;
						have_best = true;
					}
				}

				memo.emplace( key, best );
				return best;
			}

			inline void relax_edge( CarryLayer& next, const StateKey& out, std::uint64_t cand_prefix, std::uint64_t& transitions ) noexcept
			{
				++transitions;
				const auto [ it, inserted ] = next.try_emplace( out, cand_prefix );
				if ( !inserted && cand_prefix < it->second )
					it->second = cand_prefix;
			}

			[[nodiscard]] inline ExactWideMatrix exact_wide_matrix_from_bit( const BitMatrix& M ) noexcept
			{
				return ExactWideMatrix { carry_wide_from_i64( M.m00 ), carry_wide_from_i64( M.m01 ), carry_wide_from_i64( M.m10 ), carry_wide_from_i64( M.m11 ) };
			}

			[[nodiscard]] inline ExactWideMatrix projective_matrix_key_to_exact_wide( const ProjectiveMatrixKey& key ) noexcept
			{
				return ExactWideMatrix { key.m00, key.m01, key.m10, key.m11 };
			}

			[[nodiscard]] inline ExactWideMatrix multiply_exact_wide_mm( const ExactWideMatrix& A, const ExactWideMatrix& B ) noexcept
			{
				return ExactWideMatrix { A.m00 * B.m00 + A.m01 * B.m10, A.m00 * B.m01 + A.m01 * B.m11, A.m10 * B.m00 + A.m11 * B.m10, A.m10 * B.m01 + A.m11 * B.m11 };
			}

			[[nodiscard]] inline std::pair<ProjectiveMatrixKey, uint128_t> normalize_projective_matrix( ExactWideMatrix M ) noexcept
			{
				const uint128_t a0 = carry_wide_abs_to_u128( M.m00 );
				const uint128_t a1 = carry_wide_abs_to_u128( M.m01 );
				const uint128_t a2 = carry_wide_abs_to_u128( M.m10 );
				const uint128_t a3 = carry_wide_abs_to_u128( M.m11 );
				uint128_t		g = gcd_u128( a0, a1 );
				g = gcd_u128( g, a2 );
				g = gcd_u128( g, a3 );
				if ( g == 0 )
					return { {}, 0 };

				M.m00 = carry_wide_div_exact_by_u128( M.m00, g );
				M.m01 = carry_wide_div_exact_by_u128( M.m01, g );
				M.m10 = carry_wide_div_exact_by_u128( M.m10, g );
				M.m11 = carry_wide_div_exact_by_u128( M.m11, g );

				const bool flip_sign = ( M.m00 < 0 ) || ( M.m00 == 0 && M.m01 < 0 ) || ( M.m00 == 0 && M.m01 == 0 && M.m10 < 0 ) || ( M.m00 == 0 && M.m01 == 0 && M.m10 == 0 && M.m11 < 0 );
				if ( flip_sign )
				{
					M.m00 = -M.m00;
					M.m01 = -M.m01;
					M.m10 = -M.m10;
					M.m11 = -M.m11;
				}
				return { ProjectiveMatrixKey { M.m00, M.m01, M.m10, M.m11 }, g };
			}

			inline void relax_projective_segment_entry( ProjectiveSegmentSummary& summary, const ProjectiveMatrixKey& key, uint128_t scale, std::uint64_t mask ) noexcept
			{
				const auto [ it, inserted ] = summary.table.try_emplace( key, ProjectiveMatrixSummaryValue { scale, mask } );
				if ( inserted )
					return;
				if ( scale > it->second.scale || ( scale == it->second.scale && mask < it->second.mask ) )
				{
					it->second = { scale, mask };
				}
			}

			inline void relax_projective_support_entry( ProjectiveSupportSummary& summary, const ProjectiveSupportVectorKey& key, uint128_t scale, std::uint64_t mask ) noexcept
			{
				const auto [ it, inserted ] = summary.table.try_emplace( key, ProjectiveSupportVectorValue { scale, mask } );
				if ( inserted )
					return;
				if ( scale > it->second.scale || ( scale == it->second.scale && mask < it->second.mask ) )
				{
					it->second = { scale, mask };
				}
			}

			inline void relax_projective_root_query_entry( ProjectiveRootQuerySummary& summary, const DirectionKey& key, uint128_t scale, std::uint64_t mask ) noexcept
			{
				const auto [ it, inserted ] = summary.table.try_emplace( key, ProjectiveRootQueryValue { scale, mask } );
				if ( inserted )
					return;
				if ( scale > it->second.scale || ( scale == it->second.scale && mask < it->second.mask ) )
				{
					it->second = { scale, mask };
				}
			}

			[[nodiscard]] inline ProjectiveSegmentSummary make_identity_projective_segment_summary() noexcept
			{
				ProjectiveSegmentSummary out {};
				out.bit_length = 0;
				out.table.reserve( 1u );
				out.table.emplace( ProjectiveMatrixKey { carry_wide_from_i64( 1 ), carry_wide_zero(), carry_wide_zero(), carry_wide_from_i64( 1 ) }, ProjectiveMatrixSummaryValue { 1, 0ull } );
				return out;
			}

			inline void relax_projective_segment_entry_from_exact_matrix( ProjectiveSegmentSummary& summary, const ExactWideMatrix& matrix, std::uint64_t mask ) noexcept
			{
				const auto [ key, scale ] = normalize_projective_matrix( matrix );
				relax_projective_segment_entry( summary, key, scale, mask );
			}

			[[nodiscard]] inline ProjectiveSegmentSummary combine_projective_segment_summaries( const ProjectiveSegmentSummary& left, const ProjectiveSegmentSummary& right, ProjectiveSegmentSummaryStats* stats = nullptr ) noexcept
			{
				ProjectiveSegmentSummary out {};
				out.bit_length = left.bit_length + right.bit_length;
				out.table.reserve( left.table.size() * right.table.size() + 8u );
				if ( stats != nullptr )
					stats->combine_pairs += left.table.size() * right.table.size();

				for ( const auto& [ lkey, lval ] : left.table )
				{
					const ExactWideMatrix left_matrix = projective_matrix_key_to_exact_wide( lkey );
					for ( const auto& [ rkey, rval ] : right.table )
					{
						const ExactWideMatrix prod = multiply_exact_wide_mm( left_matrix, projective_matrix_key_to_exact_wide( rkey ) );
						const auto [ pkey, pscale ] = normalize_projective_matrix( prod );
						const uint128_t		total_scale = lval.scale * rval.scale * pscale;
						const std::uint64_t shifted_right = ( left.bit_length >= 64 ) ? 0ull : ( rval.mask << left.bit_length );
						relax_projective_segment_entry( out, pkey, total_scale, lval.mask | shifted_right );
					}
				}
				if ( stats != nullptr )
					stats->max_segment_entries = ( std::max )( stats->max_segment_entries, out.table.size() );
				return out;
			}

			[[nodiscard]] inline ProjectiveSegmentSummary pow_projective_segment_summary( const ProjectiveSegmentSummary& base, std::size_t exponent, ProjectiveSegmentSummaryStats* stats = nullptr ) noexcept
			{
				if ( exponent == 0u )
					return make_identity_projective_segment_summary();
				if ( exponent == 1u )
				{
					if ( stats != nullptr )
						stats->max_segment_entries = ( std::max )( stats->max_segment_entries, base.table.size() );
					return base;
				}

				ProjectiveSegmentSummary result = make_identity_projective_segment_summary();
				ProjectiveSegmentSummary power = base;
				if ( stats != nullptr )
				{
					stats->max_segment_entries = ( std::max )( stats->max_segment_entries, result.table.size() );
					stats->max_segment_entries = ( std::max )( stats->max_segment_entries, power.table.size() );
				}

				std::size_t e = exponent;
				while ( e > 0u )
				{
					if ( ( e & 1u ) != 0u )
						result = combine_projective_segment_summaries( result, power, stats );
					e >>= 1u;
					if ( e != 0u )
						power = combine_projective_segment_summaries( power, power, stats );
				}
				return result;
			}

			[[nodiscard]] inline ProjectiveSegmentSummary build_homogeneous_q2_choice_block_summary( const HomogeneousQ2ChoiceBlockDescriptor& desc ) noexcept
			{
				ProjectiveSegmentSummary single_bit {};
				single_bit.bit_length = 1;
				single_bit.table.reserve( 2u );
				relax_projective_segment_entry_from_exact_matrix( single_bit, exact_wide_matrix_from_bit( desc.choose_mask0 ), 0ull );
				relax_projective_segment_entry_from_exact_matrix( single_bit, exact_wide_matrix_from_bit( desc.choose_mask1 ), 1ull );
				return pow_projective_segment_summary( single_bit, static_cast<std::size_t>( desc.len ), nullptr );
			}

			template <typename Descriptor, typename SummaryBuilder, typename DescriptorHash>
			[[nodiscard]] inline ProjectiveSegmentSummary build_projective_segment_summary_from_powered_descriptor_chain( const std::vector<Descriptor>& descriptors, SummaryBuilder&& build_summary, DescriptorHash descriptor_hash, ProjectiveSegmentSummaryStats* stats = nullptr, std::size_t* power_runs = nullptr, std::size_t* power_collapsed_blocks = nullptr, std::size_t* unique_leaf_classes = nullptr, std::size_t* unique_leaf_nodes = nullptr, std::size_t* unique_tree_nodes = nullptr, std::size_t* descriptor_tree_reuse_hits = nullptr ) noexcept
			{
				struct PoweredDescriptor
				{
					Descriptor	descriptor {};
					std::size_t repeat_count { 0 };

					[[nodiscard]] bool operator==( const PoweredDescriptor& other ) const noexcept
					{
						return repeat_count == other.repeat_count && descriptor == other.descriptor;
					}
				};

				struct PoweredDescriptorHash
				{
					DescriptorHash base_hash {};

					[[nodiscard]] std::size_t operator()( const PoweredDescriptor& d ) const noexcept
					{
						std::size_t h = base_hash( d.descriptor );
						h ^= std::hash<std::size_t> {}( d.repeat_count ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
						return h;
					}
				};

				struct PairKey
				{
					std::size_t left_id { 0 };
					std::size_t right_id { 0 };

					[[nodiscard]] bool operator==( const PairKey& other ) const noexcept
					{
						return left_id == other.left_id && right_id == other.right_id;
					}
				};

				struct PairKeyHash
				{
					[[nodiscard]] std::size_t operator()( const PairKey& key ) const noexcept
					{
						std::size_t h = std::hash<std::size_t> {}( key.left_id );
						h ^= std::hash<std::size_t> {}( key.right_id ) + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 );
						return h;
					}
				};

				std::vector<PoweredDescriptor> leaves {};
				leaves.reserve( descriptors.size() );

				std::size_t local_power_runs = 0u;
				std::size_t local_power_collapsed_blocks = 0u;
				for ( std::size_t i = 0; i < descriptors.size(); )
				{
					std::size_t j = i + 1u;
					while ( j < descriptors.size() && descriptors[ j ] == descriptors[ i ] )
						++j;

					const std::size_t repeat_count = j - i;
					leaves.push_back( PoweredDescriptor { descriptors[ i ], repeat_count } );
					if ( repeat_count != 1u )
						++local_power_runs;
					local_power_collapsed_blocks += ( repeat_count - 1u );
					i = j;
				}

				if ( leaves.empty() )
				{
					ProjectiveSegmentSummary	  summary = make_identity_projective_segment_summary();
					ProjectiveSegmentSummaryStats local_stats {};
					local_stats.root_entries = summary.table.size();
					local_stats.max_segment_entries = summary.table.size();
					if ( stats != nullptr )
						*stats = local_stats;
					if ( power_runs != nullptr )
						*power_runs = local_power_runs;
					if ( power_collapsed_blocks != nullptr )
						*power_collapsed_blocks = local_power_collapsed_blocks;
					if ( unique_leaf_classes != nullptr )
						*unique_leaf_classes = 0u;
					if ( unique_leaf_nodes != nullptr )
						*unique_leaf_nodes = 0u;
					if ( unique_tree_nodes != nullptr )
						*unique_tree_nodes = 1u;
					if ( descriptor_tree_reuse_hits != nullptr )
						*descriptor_tree_reuse_hits = 0u;
					return summary;
				}

				ProjectiveSegmentSummaryStats		  local_stats {};
				std::vector<ProjectiveSegmentSummary> node_summaries {};
				node_summaries.reserve( leaves.size() * 2u + 1u );
				node_summaries.push_back( make_identity_projective_segment_summary() );
				local_stats.max_segment_entries = ( std::max )( local_stats.max_segment_entries, node_summaries[ 0 ].table.size() );

				std::unordered_map<PoweredDescriptor, std::size_t, PoweredDescriptorHash>											   leaf_cache( 0u, PoweredDescriptorHash { descriptor_hash } );
				std::unordered_map<PairKey, std::size_t, PairKeyHash>																   pair_cache {};
				std::unordered_map<ProjectiveSegmentSummary, std::size_t, ProjectiveSegmentSummaryHash, ProjectiveSegmentSummaryEqual> summary_cache {};
				summary_cache.reserve( leaves.size() * 2u + 1u );
				summary_cache.emplace( node_summaries[ 0 ], 0u );
				std::size_t local_tree_reuse_hits = 0u;
				std::size_t local_unique_leaf_nodes = 0u;

				auto intern_summary = [ & ]( ProjectiveSegmentSummary summary, bool is_leaf ) noexcept -> std::size_t {
					const auto found = summary_cache.find( summary );
					if ( found != summary_cache.end() )
					{
						++local_tree_reuse_hits;
						return found->second;
					}

					local_stats.max_segment_entries = ( std::max )( local_stats.max_segment_entries, summary.table.size() );
					const std::size_t id = node_summaries.size();
					node_summaries.push_back( summary );
					summary_cache.emplace( node_summaries.back(), id );
					if ( is_leaf )
						++local_unique_leaf_nodes;
					return id;
				};

				auto intern_leaf = [ & ]( const PoweredDescriptor& leaf ) noexcept -> std::size_t {
					const auto it = leaf_cache.find( leaf );
					if ( it != leaf_cache.end() )
					{
						++local_tree_reuse_hits;
						return it->second;
					}

					const ProjectiveSegmentSummary base = build_summary( leaf.descriptor );
					ProjectiveSegmentSummary	   summary = ( leaf.repeat_count == 1u ) ? base : pow_projective_segment_summary( base, leaf.repeat_count, &local_stats );
					const std::size_t			   id = intern_summary( std::move( summary ), true );
					leaf_cache.emplace( leaf, id );
					return id;
				};

				auto build_tree = [ & ]( auto&& self, std::size_t begin, std::size_t end ) noexcept -> std::size_t {
					if ( begin >= end )
						return 0u;
					if ( begin + 1u == end )
						return intern_leaf( leaves[ begin ] );

					const std::size_t mid = begin + ( ( end - begin ) / 2u );
					const std::size_t left_id = self( self, begin, mid );
					const std::size_t right_id = self( self, mid, end );
					const PairKey	  key { left_id, right_id };
					const auto		  it = pair_cache.find( key );
					if ( it != pair_cache.end() )
					{
						++local_tree_reuse_hits;
						return it->second;
					}

					ProjectiveSegmentSummary summary = combine_projective_segment_summaries( node_summaries[ left_id ], node_summaries[ right_id ], &local_stats );
					const std::size_t		 id = intern_summary( std::move( summary ), false );
					pair_cache.emplace( key, id );
					return id;
				};

				const std::size_t		 root_id = build_tree( build_tree, 0u, leaves.size() );
				ProjectiveSegmentSummary summary = node_summaries[ root_id ];
				local_stats.root_entries = summary.table.size();
				local_stats.max_segment_entries = ( std::max )( local_stats.max_segment_entries, summary.table.size() );

				if ( stats != nullptr )
					*stats = local_stats;
				if ( power_runs != nullptr )
					*power_runs = local_power_runs;
				if ( power_collapsed_blocks != nullptr )
					*power_collapsed_blocks = local_power_collapsed_blocks;
				if ( unique_leaf_classes != nullptr )
					*unique_leaf_classes = leaf_cache.size();
				if ( unique_leaf_nodes != nullptr )
					*unique_leaf_nodes = local_unique_leaf_nodes;
				if ( unique_tree_nodes != nullptr )
					*unique_tree_nodes = node_summaries.size();
				if ( descriptor_tree_reuse_hits != nullptr )
					*descriptor_tree_reuse_hits = local_tree_reuse_hits;
				return summary;
			}

			[[nodiscard]] inline VarConstLayerDpSummary summarize_projective_segment_summary_best( const ProjectiveSegmentSummary& summary ) noexcept
			{
				ExactAbsWeight best_abs {};
				std::uint64_t  best_mask = std::numeric_limits<std::uint64_t>::max();
				for ( const auto& [ key, value ] : summary.table )
				{
					const ExactAbsWeight unit_abs = exact_abs_sum_v0_v1( key.m00, key.m01 );
					const ExactAbsWeight total_abs = scale_exact_abs_weight_projective( unit_abs, value.scale );
					if ( exact_abs_weight_less( best_abs, total_abs ) || ( exact_abs_weight_equal( total_abs, best_abs ) && value.mask < best_mask ) )
					{
						best_abs = total_abs;
						best_mask = value.mask;
					}
				}
				return { best_mask, best_abs };
			}

			[[nodiscard]] inline ProjectiveRootQuerySummary build_projective_root_query_summary_from_segment_summary( const ProjectiveSegmentSummary& segment ) noexcept
			{
				ProjectiveRootQuerySummary out {};
				out.bit_length = segment.bit_length;
				out.table.reserve( segment.table.size() + 8u );

				for ( const auto& [ key, value ] : segment.table )
				{
					const auto [ dir, row_scale ] = normalize_projective_direction( key.m00, key.m01 );
					if ( row_scale == 0 )
						continue;
					relax_projective_root_query_entry( out, dir, value.scale * row_scale, value.mask );
				}
				return out;
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_dot_projective_direction_support( const DirectionKey& dir, const ProjectiveSupportVectorKey& vec ) noexcept
			{
				return exact_abs_sum_v0_v1( dir.x * vec.x, dir.y * vec.y );
			}

			[[nodiscard]] inline ProjectiveSuffixChoice solve_projective_support_summary_query( const ProjectiveSupportSummary& support, const DirectionKey& dir ) noexcept
			{
				ProjectiveSuffixChoice out {};
				for ( const auto& [ key, value ] : support.table )
				{
					const ExactAbsWeight unit_abs = exact_abs_dot_projective_direction_support( dir, key );
					const ExactAbsWeight total_abs = scale_exact_abs_weight_projective( unit_abs, value.scale );
					if ( exact_abs_weight_less( out.best_abs, total_abs ) || ( exact_abs_weight_equal( total_abs, out.best_abs ) && value.mask < out.best_suffix_mask ) )
					{
						out.best_abs = total_abs;
						out.best_suffix_mask = value.mask;
					}
				}
				return out;
			}

			[[nodiscard]] inline VarConstLayerDpSummary solve_projective_root_query_summary_against_support_summary( const ProjectiveRootQuerySummary& root, const ProjectiveSupportSummary& support ) noexcept
			{
				VarConstLayerDpSummary out {};
				for ( const auto& [ dir, value ] : root.table )
				{
					const ProjectiveSuffixChoice suffix = solve_projective_support_summary_query( support, dir );
					const ExactAbsWeight		 total_abs = scale_exact_abs_weight_projective( suffix.best_abs, value.scale );
					const std::uint64_t			 shifted_right = ( root.bit_length >= 64 ) ? 0ull : ( suffix.best_suffix_mask << root.bit_length );
					const std::uint64_t			 total_mask = value.mask | shifted_right;
					if ( exact_abs_weight_less( out.abs_weight, total_abs ) || ( exact_abs_weight_equal( total_abs, out.abs_weight ) && total_mask < out.optimal_mask ) )
					{
						out.abs_weight = total_abs;
						out.optimal_mask = total_mask;
					}
				}
				return out;
			}

			[[nodiscard]] inline ExactAbsWeight evaluate_fixed_beta_exact_abs_weight( std::uint64_t alpha, std::uint64_t add_constant, std::uint64_t beta, int n ) noexcept
			{
				if ( n <= 0 || n > 64 )
					return {};

				const std::uint64_t m = mask_n( n );
				alpha &= m;
				add_constant &= m;
				beta &= m;

				std::array<ExactWideMatrix, 64> mats {};
				for ( int bit = 0; bit < n; ++bit )
				{
					const int alpha_i = static_cast<int>( ( alpha >> bit ) & 1ull );
					const int constant_i = static_cast<int>( ( add_constant >> bit ) & 1ull );
					const int beta_i = static_cast<int>( ( beta >> bit ) & 1ull );
					mats[ static_cast<std::size_t>( bit ) ] = exact_wide_matrix_from_bit( build_matrix_for_beta_bit( alpha_i, constant_i, beta_i ) );
				}

				std::size_t active = static_cast<std::size_t>( n );
				while ( active > 1u )
				{
					std::size_t next = 0u;
					std::size_t i = 0u;
					for ( ; i + 1u < active; i += 2u )
						mats[ next++ ] = multiply_exact_wide_mm( mats[ i ], mats[ i + 1u ] );
					if ( i < active )
						mats[ next++ ] = mats[ i ];
					active = next;
				}
				return exact_abs_sum_v0_v1( mats[ 0 ].m00, mats[ 0 ].m01 );
			}

			[[nodiscard]] inline VarConstLayerDpSummary run_raw_carry_layer_dp_from_model( const VarConstQ2BitModel& model ) noexcept
			{
				CarryLayer		  cur;
				CarryLayer		  nxt;
				const std::size_t reserve_hint = static_cast<std::size_t>( 1 ) << static_cast<std::size_t>( ( std::min )( model.n, 16 ) );
				cur.reserve( reserve_hint );
				nxt.reserve( reserve_hint );

				cur.emplace( StateKey { carry_wide_from_i64( 1 ), carry_wide_zero() }, 0ull );
				std::uint64_t transitions = 0;

				for ( int bit = 0; bit < model.n; ++bit )
				{
					nxt.clear();
					const BitMatrix& M0 = model.choose_mask0[ static_cast<std::size_t>( bit ) ];
					const BitMatrix& M1 = model.choose_mask1[ static_cast<std::size_t>( bit ) ];

					for ( const auto& [ st, pref ] : cur )
					{
						CarryWide o0 = carry_wide_zero();
						CarryWide o1 = carry_wide_zero();
						apply_row_step( M0, st.v0, st.v1, o0, o1 );
						relax_edge( nxt, StateKey { o0, o1 }, pref, transitions );

						apply_row_step( M1, st.v0, st.v1, o0, o1 );
						const std::uint64_t pref1 = pref | ( std::uint64_t( 1 ) << bit );
						relax_edge( nxt, StateKey { o0, o1 }, pref1, transitions );
					}
					cur.swap( nxt );
				}

				ExactAbsWeight best_abs {};
				std::uint64_t  best_mask = std::numeric_limits<std::uint64_t>::max();
				for ( const auto& [ st, pref ] : cur )
				{
					const ExactAbsWeight aw = exact_abs_sum_v0_v1( st.v0, st.v1 );
					if ( exact_abs_weight_less( best_abs, aw ) || ( exact_abs_weight_equal( aw, best_abs ) && pref < best_mask ) )
					{
						best_abs = aw;
						best_mask = pref;
					}
				}
				return { best_mask, best_abs };
			}

			[[nodiscard]] std::uint64_t normalize_add_constant_for_q2( VarConstQ2Operation operation, std::uint64_t constant, int n ) noexcept
			{
				const std::uint64_t m = mask_n( n );
				if ( operation == VarConstQ2Operation::modular_sub )
					return ( ( ~constant ) + 1ull ) & m;
				return constant & m;
			}

			[[nodiscard]] inline VarConstQ2BitModel build_q2_bit_model_from_request( const VarConstQ2MainlineRequest& request ) noexcept
			{
				if ( request.direction != VarConstQ2Direction::fixed_beta_to_alpha )
					return {};
				const std::uint64_t m = mask_n( request.n );
				const std::uint64_t fixed_mask = request.fixed_mask & m;
				const std::uint64_t add_constant = normalize_add_constant_for_q2( request.operation, request.constant, request.n );
				return build_input_mask_q2_bit_model( fixed_mask, add_constant, request.n );
			}

			[[nodiscard]] inline std::vector<HomogeneousQ2ChoiceBlockDescriptor> build_homogeneous_block_descriptors_from_model( const VarConstQ2BitModel& model ) noexcept
			{
				std::vector<HomogeneousQ2ChoiceBlockDescriptor> out {};
				if ( model.n <= 0 )
					return out;

				HomogeneousQ2ChoiceBlockDescriptor current {};
				current.choose_mask0 = model.choose_mask0[ 0 ];
				current.choose_mask1 = model.choose_mask1[ 0 ];
				current.len = 1;

				for ( int bit = 1; bit < model.n; ++bit )
				{
					const BitMatrix& next0 = model.choose_mask0[ static_cast<std::size_t>( bit ) ];
					const BitMatrix& next1 = model.choose_mask1[ static_cast<std::size_t>( bit ) ];
					if ( bit_matrix_equal( current.choose_mask0, next0 ) && bit_matrix_equal( current.choose_mask1, next1 ) )
					{
						++current.len;
						continue;
					}

					out.push_back( current );
					current.choose_mask0 = next0;
					current.choose_mask1 = next1;
					current.len = 1;
				}
				out.push_back( current );
				return out;
			}

			[[nodiscard]] inline ProjectiveSegmentSummary build_exact_segment_summary_from_model( const VarConstQ2BitModel& model, ProjectiveSegmentSummaryStats* segment_stats = nullptr, std::size_t* raw_block_count = nullptr, std::size_t* powered_leaf_count = nullptr, std::size_t* power_runs = nullptr, std::size_t* power_collapsed_blocks = nullptr, std::size_t* unique_leaf_classes = nullptr, std::size_t* unique_leaf_nodes = nullptr, std::size_t* unique_tree_nodes = nullptr, std::size_t* descriptor_tree_reuse_hits = nullptr ) noexcept
			{
				const std::vector<HomogeneousQ2ChoiceBlockDescriptor> descriptors = build_homogeneous_block_descriptors_from_model( model );
				if ( raw_block_count != nullptr )
					*raw_block_count = descriptors.size();
				if ( powered_leaf_count != nullptr )
					*powered_leaf_count = descriptors.size();

				return build_projective_segment_summary_from_powered_descriptor_chain( descriptors, []( const HomogeneousQ2ChoiceBlockDescriptor& desc ) noexcept { return build_homogeneous_q2_choice_block_summary( desc ); }, HomogeneousQ2ChoiceBlockDescriptorHash {}, segment_stats, power_runs, power_collapsed_blocks, unique_leaf_classes, unique_leaf_nodes, unique_tree_nodes, descriptor_tree_reuse_hits );
			}

			[[nodiscard]] inline bool try_run_projective_periodic_block_power_mainline( const VarConstQ2BitModel& model, VarConstLayerDpSummary& out, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				( void )model;
				( void )out;
				if ( stats != nullptr )
					*stats = {};
				return false;
			}

			[[nodiscard]] inline VarConstLayerDpSummary run_q2_mainline_from_model( const VarConstQ2BitModel& model, VarConstQ2MainlineMethod method, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( stats != nullptr )
					*stats = {};

#if defined( TWILIGHTDREAM_VARCONST_RAW_CARRY_DP )
				( void )method;
				return run_raw_carry_layer_dp_from_model( model );
#else
				( void )method;
				ProjectiveSegmentSummaryStats  segment_stats {};
				std::size_t					   raw_block_count = 0u;
				std::size_t					   powered_leaf_count = 0u;
				std::size_t					   power_runs = 0u;
				std::size_t					   power_collapsed_blocks = 0u;
				std::size_t					   unique_leaf_classes = 0u;
				std::size_t					   unique_leaf_nodes = 0u;
				std::size_t					   unique_tree_nodes = 0u;
				std::size_t					   reuse_hits = 0u;
				const ProjectiveSegmentSummary summary = build_exact_segment_summary_from_model( model, &segment_stats, &raw_block_count, &powered_leaf_count, &power_runs, &power_collapsed_blocks, &unique_leaf_classes, &unique_leaf_nodes, &unique_tree_nodes, &reuse_hits );
				if ( stats != nullptr )
				{
					stats->segment_root_entries = segment_stats.root_entries;
					stats->segment_max_entries = segment_stats.max_segment_entries;
					stats->segment_combine_pairs = segment_stats.combine_pairs;
					stats->segment_raw_block_count = raw_block_count;
					stats->segment_powered_leaf_count = powered_leaf_count;
					stats->segment_power_runs = power_runs;
					stats->segment_power_collapsed_blocks = power_collapsed_blocks;
					stats->segment_descriptor_tree_leaf_classes = unique_leaf_classes;
					stats->segment_descriptor_tree_unique_leaf_nodes = unique_leaf_nodes;
					stats->segment_descriptor_tree_unique_nodes = unique_tree_nodes;
					stats->segment_descriptor_tree_reuse_hits = reuse_hits;
				}
				return summarize_projective_segment_summary_best( summary );
#endif
			}

			[[nodiscard]] inline FixedBetaFlatTheoremArtifacts build_fixed_beta_flat_theorem_artifacts( std::uint64_t beta, std::uint64_t add_constant, int n ) noexcept
			{
				FixedBetaFlatTheoremArtifacts out {};
				if ( n <= 0 || n > 64 )
					return out;

				const VarConstQ2BitModel model = build_input_mask_q2_bit_model( beta & mask_n( n ), add_constant & mask_n( n ), n );
				out.summary = build_exact_segment_summary_from_model( model, &out.segment_stats, &out.raw_block_count, &out.powered_leaf_count, &out.power_runs, &out.power_collapsed_blocks, &out.descriptor_tree_leaf_classes, &out.descriptor_tree_unique_leaf_nodes, &out.descriptor_tree_unique_nodes, &out.descriptor_tree_reuse_hits );
				return out;
			}

			inline void populate_fixed_beta_flat_mainline_stats( const FixedBetaFlatTheoremArtifacts& artifacts, VarConstQ2MainlineStats& stats ) noexcept
			{
				stats.segment_root_entries = artifacts.segment_stats.root_entries;
				stats.segment_max_entries = artifacts.segment_stats.max_segment_entries;
				stats.segment_combine_pairs = artifacts.segment_stats.combine_pairs;
				stats.segment_raw_block_count = artifacts.raw_block_count;
				stats.segment_powered_leaf_count = artifacts.powered_leaf_count;
				stats.segment_power_runs = artifacts.power_runs;
				stats.segment_power_collapsed_blocks = artifacts.power_collapsed_blocks;
				stats.segment_descriptor_tree_leaf_classes = artifacts.descriptor_tree_leaf_classes;
				stats.segment_descriptor_tree_unique_leaf_nodes = artifacts.descriptor_tree_unique_leaf_nodes;
				stats.segment_descriptor_tree_unique_nodes = artifacts.descriptor_tree_unique_nodes;
				stats.segment_descriptor_tree_reuse_hits = artifacts.descriptor_tree_reuse_hits;
			}

			inline void relax_projective_root_slope_entry( ProjectiveRootSlopeSummary& summary, const ProjectiveSlopeKey& key, uint128_t scale, std::uint64_t mask ) noexcept
			{
				const auto [ it, inserted ] = summary.table.try_emplace( key, ProjectiveRootQueryValue { scale, mask } );
				if ( inserted )
					return;
				if ( scale > it->second.scale || ( scale == it->second.scale && mask < it->second.mask ) )
				{
					it->second = { scale, mask };
				}
			}

			[[nodiscard]] inline uint128_t projective_root_slope_scale_gcd( const ProjectiveRootSlopeSummary& summary ) noexcept
			{
				uint128_t g { 0 };
				for ( const auto& [ slope, value ] : summary.table )
				{
					( void )slope;
					g = ( g == uint128_t { 0 } ) ? value.scale : gcd_u128( g, value.scale );
				}
				return ( g == uint128_t { 0 } ) ? uint128_t { 1 } : g;
			}

			[[nodiscard]] inline ProjectiveRootSlopeSummary build_projective_root_slope_summary_from_root_query_summary( const ProjectiveRootQuerySummary& root ) noexcept
			{
				ProjectiveRootSlopeSummary out {};
				out.bit_length = root.bit_length;
				out.table.reserve( root.table.size() + 8u );
				for ( const auto& [ dir, value ] : root.table )
				{
					relax_projective_root_slope_entry( out, projective_slope_from_direction( dir ), value.scale, value.mask );
				}
				return out;
			}

			[[nodiscard]] inline ProjectiveRootSlopeSummary build_projective_root_slope_summary_from_segment_summary( const ProjectiveSegmentSummary& summary ) noexcept
			{
				return build_projective_root_slope_summary_from_root_query_summary( build_projective_root_query_summary_from_segment_summary( summary ) );
			}

			[[nodiscard]] inline ProjectiveRootSlopeSummary append_projective_root_slope_summary_with_segment_summary( const ProjectiveRootSlopeSummary& left, const ProjectiveSegmentSummary& right ) noexcept
			{
				ProjectiveRootSlopeSummary out {};
				out.bit_length = left.bit_length + right.bit_length;
				out.table.reserve( left.table.size() * right.table.size() + 8u );

				for ( const auto& [ lslope, lval ] : left.table )
				{
					const DirectionKey ldir = direction_from_projective_slope( lslope );
					for ( const auto& [ rkey, rval ] : right.table )
					{
						const CarryWide o0 = ldir.x * rkey.m00 + ldir.y * rkey.m10;
						const CarryWide o1 = ldir.x * rkey.m01 + ldir.y * rkey.m11;
						const auto [ out_dir, out_scale ] = normalize_projective_direction( o0, o1 );
						if ( out_scale == 0 )
							continue;
						const std::uint64_t shifted_right = ( left.bit_length >= 64 ) ? 0ull : ( rval.mask << left.bit_length );
						relax_projective_root_slope_entry( out, projective_slope_from_direction( out_dir ), lval.scale * rval.scale * out_scale, lval.mask | shifted_right );
					}
				}

				return out;
			}

			[[nodiscard]] std::uint64_t projective_root_slope_anchor_mask( const ProjectiveRootSlopeSummary& summary ) noexcept
			{
				bool			   have_anchor = false;
				ProjectiveSlopeKey anchor_slope {};
				std::uint64_t	   anchor_mask = 0ull;
				for ( const auto& [ slope, value ] : summary.table )
				{
					if ( !have_anchor || projective_slope_key_less( slope, anchor_slope ) )
					{
						have_anchor = true;
						anchor_slope = slope;
						anchor_mask = value.mask;
					}
				}
				return anchor_mask;
			}

			[[nodiscard]] inline ProjectiveRootSlopeRelativeProfileSummary build_projective_root_slope_relative_profile_summary( const ProjectiveRootSlopeSummary& summary ) noexcept
			{
				ProjectiveRootSlopeRelativeProfileSummary out {};
				out.bit_length = summary.bit_length;
				out.table.reserve( summary.table.size() + 8u );

				const uint128_t		g = projective_root_slope_scale_gcd( summary );
				const std::uint64_t anchor_mask = projective_root_slope_anchor_mask( summary );

				for ( const auto& [ slope, value ] : summary.table )
				{
					out.table.emplace( slope, ProjectiveRootSlopeRelativeProfileValue { value.scale / g, value.mask ^ anchor_mask } );
				}
				return out;
			}

			[[nodiscard]] inline ProjectiveSegmentSummary build_fixed_beta_flat_sync_summary( int constant_i ) noexcept
			{
				ProjectiveSegmentSummary out {};
				out.bit_length = 1;
				out.table.reserve( 2u );
				for ( int alpha_i = 0; alpha_i <= 1; ++alpha_i )
				{
					relax_projective_segment_entry_from_exact_matrix( out, exact_wide_matrix_from_bit( build_matrix_for_beta_bit( alpha_i, constant_i, 1 ) ), static_cast<std::uint64_t>( alpha_i ) );
				}
				return out;
			}

			[[nodiscard]] inline ProjectiveRootSlopeSyncCompetitionGroupMap build_projective_root_slope_sync_competition_groups( const ProjectiveRootSlopeRelativeProfileSummary& profile ) noexcept
			{
				ProjectiveRootSlopeSyncCompetitionGroupMap groups {};

				for ( int run_bit = 0; run_bit <= 1; ++run_bit )
				{
					const ProjectiveSegmentSummary sync_summary = build_fixed_beta_flat_sync_summary( run_bit );

					for ( const auto& [ parent_slope, parent_value ] : profile.table )
					{
						const DirectionKey ldir = direction_from_projective_slope( parent_slope );
						for ( const auto& [ rkey, rval ] : sync_summary.table )
						{
							const CarryWide o0 = ldir.x * rkey.m00 + ldir.y * rkey.m10;
							const CarryWide o1 = ldir.x * rkey.m01 + ldir.y * rkey.m11;
							const auto [ out_dir, out_scale ] = normalize_projective_direction( o0, o1 );
							if ( out_scale == 0 )
								continue;

							const std::uint64_t shifted_suffix_mask = ( profile.bit_length >= 64 ) ? 0ull : ( rval.mask << profile.bit_length );
							groups[ ProjectiveRootSlopeSyncCompetitionKey { run_bit, projective_slope_from_direction( out_dir ), parent_value.relative_scale * rval.scale * out_scale, shifted_suffix_mask } ].push_back( parent_value.relative_mask );
						}
					}
				}

				return groups;
			}

			[[nodiscard]] inline bool projective_root_slope_sync_competition_key_less( const ProjectiveRootSlopeSyncCompetitionKey& lhs, const ProjectiveRootSlopeSyncCompetitionKey& rhs ) noexcept
			{
				if ( lhs.run_bit != rhs.run_bit )
					return lhs.run_bit < rhs.run_bit;
				if ( lhs.child_slope != rhs.child_slope )
					return projective_slope_key_less( lhs.child_slope, rhs.child_slope );
				if ( lhs.relative_scale != rhs.relative_scale )
					return lhs.relative_scale < rhs.relative_scale;
				return lhs.shifted_suffix_mask < rhs.shifted_suffix_mask;
			}

			[[nodiscard]] inline ProjectiveRootSlopeSyncWinnerPathSummary build_projective_root_slope_sync_winner_path_summary( std::vector<std::uint64_t> rel_masks, std::uint64_t anchor_mask ) noexcept
			{
				ProjectiveRootSlopeSyncWinnerPathSummary out {};
				std::sort( rel_masks.begin(), rel_masks.end() );
				rel_masks.erase( std::unique( rel_masks.begin(), rel_masks.end() ), rel_masks.end() );
				if ( rel_masks.empty() )
					return out;
				if ( rel_masks.size() == 1u )
				{
					out.winner_relative_mask = rel_masks.front();
					return out;
				}

				while ( rel_masks.size() > 1u )
				{
					int best_bit = -1;
					for ( int bit = 63; bit >= 0; --bit )
					{
						const std::uint64_t bit_mask = ( 1ull << bit );
						bool				have0 = false;
						bool				have1 = false;
						for ( const std::uint64_t mask : rel_masks )
						{
							if ( ( mask & bit_mask ) == 0ull )
								have0 = true;
							else
								have1 = true;
							if ( have0 && have1 )
							{
								best_bit = bit;
								break;
							}
						}
						if ( best_bit >= 0 )
							break;
					}

					if ( best_bit < 0 )
						break;

					const std::uint64_t bit_mask = ( 1ull << best_bit );
					const bool			prefer_one = ( anchor_mask & bit_mask ) != 0ull;
					bool				chosen_one = prefer_one;

					std::vector<std::uint64_t> next_masks {};
					next_masks.reserve( rel_masks.size() );
					for ( const std::uint64_t mask : rel_masks )
					{
						const bool bit_is_one = ( mask & bit_mask ) != 0ull;
						if ( bit_is_one == prefer_one )
							next_masks.push_back( mask );
					}
					if ( next_masks.empty() )
					{
						chosen_one = !prefer_one;
						for ( const std::uint64_t mask : rel_masks )
						{
							const bool bit_is_one = ( mask & bit_mask ) != 0ull;
							if ( bit_is_one == chosen_one )
								next_masks.push_back( mask );
						}
					}

					out.winner_trace_word = ( out.winner_trace_word << 1u ) | ( chosen_one ? 1ull : 0ull );
					out.queried_bit_support |= bit_mask;
					++out.query_depth;
					rel_masks = std::move( next_masks );
				}

				if ( !rel_masks.empty() )
					out.winner_relative_mask = rel_masks.front();
				return out;
			}

			[[nodiscard]] inline ProjectiveRootSlopeLiveSyncShadowSummary build_projective_root_slope_live_sync_shadow_summary( const ProjectiveRootSlopeSummary& summary ) noexcept
			{
				ProjectiveRootSlopeLiveSyncShadowSummary out {};
				out.bit_length = summary.bit_length;
				out.profile = build_projective_root_slope_relative_profile_summary( summary );
				const std::uint64_t						   anchor_mask = projective_root_slope_anchor_mask( summary );
				ProjectiveRootSlopeSyncCompetitionGroupMap groups = build_projective_root_slope_sync_competition_groups( out.profile );

				for ( auto& [ unused_key, rel_masks ] : groups )
				{
					( void )unused_key;
					const ProjectiveRootSlopeSyncWinnerPathSummary winner_path = build_projective_root_slope_sync_winner_path_summary( rel_masks, anchor_mask );
					out.live_bit_support |= winner_path.queried_bit_support;
				}

				out.live_anchor_mask = anchor_mask & out.live_bit_support;
				return out;
			}

			[[nodiscard]] inline ProjectiveRootSlopeSyncWinnerControlSummary build_projective_root_slope_sync_winner_control_summary( const ProjectiveRootSlopeSummary& summary ) noexcept
			{
				ProjectiveRootSlopeSyncWinnerControlSummary out {};
				out.bit_length = summary.bit_length;
				out.profile = build_projective_root_slope_relative_profile_summary( summary );
				const std::uint64_t						   anchor_mask = projective_root_slope_anchor_mask( summary );
				ProjectiveRootSlopeSyncCompetitionGroupMap groups = build_projective_root_slope_sync_competition_groups( out.profile );
				out.winners.reserve( groups.size() );

				for ( auto& [ key, rel_masks ] : groups )
				{
					const ProjectiveRootSlopeSyncWinnerPathSummary winner_path = build_projective_root_slope_sync_winner_path_summary( rel_masks, anchor_mask );
					if ( winner_path.query_depth == 0u )
						continue;

					out.winners.push_back( ProjectiveRootSlopeSyncWinnerControlEntry { key, winner_path.winner_relative_mask } );
				}

				std::sort( out.winners.begin(), out.winners.end(), []( const ProjectiveRootSlopeSyncWinnerControlEntry& lhs, const ProjectiveRootSlopeSyncWinnerControlEntry& rhs ) noexcept {
					if ( lhs.competition.run_bit != rhs.competition.run_bit )
						return lhs.competition.run_bit < rhs.competition.run_bit;
					if ( lhs.competition.child_slope != rhs.competition.child_slope )
					{
						return projective_slope_key_less( lhs.competition.child_slope, rhs.competition.child_slope );
					}
					if ( lhs.competition.relative_scale != rhs.competition.relative_scale )
						return lhs.competition.relative_scale < rhs.competition.relative_scale;
					if ( lhs.competition.shifted_suffix_mask != rhs.competition.shifted_suffix_mask )
					{
						return lhs.competition.shifted_suffix_mask < rhs.competition.shifted_suffix_mask;
					}
					return lhs.winner_relative_mask < rhs.winner_relative_mask;
				} );

				return out;
			}

			[[nodiscard]] inline bool try_run_support_summary_request_fastpath( const VarConstQ2MainlineRequest& request, VarConstQ2MainlineResult& out, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( request.n <= 0 || request.n > 64 )
					return false;
				if ( request.direction != VarConstQ2Direction::fixed_beta_to_alpha )
					return false;

				if ( stats != nullptr )
					*stats = {};
				const std::uint64_t					request_mask = mask_n( request.n );
				const std::uint64_t					beta = request.fixed_mask & request_mask;
				const std::uint64_t					add_constant = normalize_add_constant_for_q2( request.operation, request.constant, request.n );
				const FixedBetaFlatTheoremArtifacts artifacts = build_fixed_beta_flat_theorem_artifacts( beta, add_constant, request.n );
				const VarConstLayerDpSummary		layer_summary = summarize_projective_segment_summary_best( artifacts.summary );
				out = layer_dp_summary_to_q2_mainline( layer_summary, request.n );
				if ( stats != nullptr )
					populate_fixed_beta_flat_mainline_stats( artifacts, *stats );
				return true;
			}
		}  // namespace detail_varconst_carry_dp
	}  // namespace arx_operators
}  // namespace TwilightDream

#define TWILIGHTDREAM_ARX_LINEAR_CONSTANT_Q2_SHARED_DECLS
#include "include/arx_analysis_operators/linear_correlation/fixed_beta_research_debug.hpp"
#undef TWILIGHTDREAM_ARX_LINEAR_CONSTANT_Q2_SHARED_DECLS

namespace TwilightDream
{
	namespace arx_operators
	{
		VarConstQ2MainlineResult solve_varconst_q2_mainline(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineMethod method,
			VarConstQ2MainlineStats* stats ) noexcept
		{
			if ( request.n <= 0 || request.n > 64 )
			{
				if ( stats != nullptr )
					*stats = {};
				return {};
			}

			VarConstQ2MainlineResult out {};
			if ( method == VarConstQ2MainlineMethod::projective_support_summary &&
				 detail_varconst_carry_dp::try_run_support_summary_request_fastpath( request, out, stats ) )
			{
				return out;
			}

			if ( request.direction != VarConstQ2Direction::fixed_beta_to_alpha )
			{
				if ( stats != nullptr )
					*stats = {};
				return {};
			}

			const detail_varconst_carry_dp::VarConstQ2BitModel model =
				detail_varconst_carry_dp::build_q2_bit_model_from_request( request );
			const detail_varconst_carry_dp::VarConstLayerDpSummary summary =
				detail_varconst_carry_dp::run_q2_mainline_from_model(
					model,
					method,
					stats );
			return detail_varconst_carry_dp::layer_dp_summary_to_q2_mainline( summary, request.n );
		}

		VarConstOptimalInputMaskResult find_optimal_alpha_varconst_mod_sub(
			std::uint64_t beta,
			std::uint64_t sub_constant_C,
			int n ) noexcept
		{
			if ( n <= 0 || n > 64 )
				return {};
			const std::uint64_t mask = detail_varconst_carry_dp::mask_n( n );
			const VarConstQ2MainlineRequest request {
				beta & mask,
				sub_constant_C & mask,
				n,
				VarConstQ2Direction::fixed_beta_to_alpha,
				VarConstQ2Operation::modular_sub };
			return detail_varconst_carry_dp::q2_mainline_to_alpha_result(
				solve_varconst_q2_mainline(
					request,
					VarConstQ2MainlineMethod::projective_support_summary,
					nullptr ) );
		}

		VarConstOptimalInputMaskResult find_optimal_alpha_varconst_mod_add(
			std::uint64_t beta,
			std::uint64_t add_constant_K,
			int n ) noexcept
		{
			if ( n <= 0 || n > 64 )
				return {};
			const std::uint64_t mask = detail_varconst_carry_dp::mask_n( n );
			const VarConstQ2MainlineRequest request {
				beta & mask,
				add_constant_K & mask,
				n,
				VarConstQ2Direction::fixed_beta_to_alpha,
				VarConstQ2Operation::modular_add };
			return detail_varconst_carry_dp::q2_mainline_to_alpha_result(
				solve_varconst_q2_mainline(
					request,
					VarConstQ2MainlineMethod::projective_support_summary,
					nullptr ) );
		}
	}  // namespace arx_operators
}  // namespace TwilightDream

namespace gap_b_fixed_beta_research_continuation
{
	namespace ao = TwilightDream::arx_operators;
	namespace dv = TwilightDream::arx_operators::detail_varconst_carry_dp;

	[[nodiscard]] inline const char* coverage_kind_label(
		CoverageKind coverage_kind ) noexcept
	{
		switch ( coverage_kind )
		{
		case CoverageKind::no_active_family: return "no_active_family";
		case CoverageKind::rigid_only: return "rigid_only";
		case CoverageKind::mixed_or_unexpected: return "mixed_or_unexpected";
		case CoverageKind::terminal_non_complement_pair: return "terminal_non_complement_pair";
		case CoverageKind::prototype_noncomplement_one_step: return "prototype_noncomplement_one_step";
		case CoverageKind::structured_pure_terminal:
		default: return "structured_pure_terminal";
		}
	}

	namespace detail
	{
		struct NormalizedMaskFamily
		{
			std::size_t width { 0 };
			std::vector<std::uint64_t> masks {};
			std::uint64_t anchor_projection { 0 };
		};

		[[nodiscard]] inline std::string binary_u64_string(
			std::uint64_t value,
			std::size_t width ) noexcept
		{
			if ( width == 0u )
				return "eps";
			std::string out {};
			out.reserve( width );
			for ( std::size_t i = 0; i < width; ++i )
			{
				const std::size_t shift = width - 1u - i;
				out.push_back( ( ( value >> shift ) & 1ull ) != 0ull ? '1' : '0' );
			}
			return out;
		}

		[[nodiscard]] inline std::vector<std::uint64_t> dedup_sorted_masks(
			std::vector<std::uint64_t> rel_masks ) noexcept
		{
			std::sort( rel_masks.begin(), rel_masks.end() );
			rel_masks.erase( std::unique( rel_masks.begin(), rel_masks.end() ), rel_masks.end() );
			return rel_masks;
		}

		[[nodiscard]] inline std::vector<unsigned int> build_differing_bits_desc(
			const std::vector<std::uint64_t>& masks ) noexcept
		{
			std::vector<unsigned int> differing_bits_desc {};
			if ( masks.size() <= 1u )
				return differing_bits_desc;
			for ( int bit = 63; bit >= 0; --bit )
			{
				const std::uint64_t bit_mask = ( 1ull << bit );
				bool have0 = false;
				bool have1 = false;
				for ( const std::uint64_t mask : masks )
				{
					if ( ( mask & bit_mask ) == 0ull ) have0 = true;
					else have1 = true;
					if ( have0 && have1 )
					{
						differing_bits_desc.push_back( static_cast<unsigned int>( bit ) );
						break;
					}
				}
			}
			return differing_bits_desc;
		}

		[[nodiscard]] inline NormalizedMaskFamily normalize_mask_family(
			const std::vector<std::uint64_t>& masks,
			std::uint64_t anchor_mask ) noexcept
		{
			NormalizedMaskFamily out {};
			const std::vector<unsigned int> differing_bits_desc =
				build_differing_bits_desc( masks );
			out.width = differing_bits_desc.size();
			out.masks.reserve( masks.size() );
			for ( const std::uint64_t mask : masks )
			{
				std::uint64_t normalized = 0ull;
				for ( std::size_t i = 0; i < differing_bits_desc.size(); ++i )
				{
					const unsigned int bit = differing_bits_desc[ i ];
					const std::size_t normalized_shift = differing_bits_desc.size() - 1u - i;
					if ( ( mask & ( 1ull << bit ) ) != 0ull )
						normalized |= ( 1ull << normalized_shift );
					if ( ( anchor_mask & ( 1ull << bit ) ) != 0ull )
						out.anchor_projection |= ( 1ull << normalized_shift );
				}
				out.masks.push_back( normalized );
			}
			std::sort( out.masks.begin(), out.masks.end() );
			out.masks.erase( std::unique( out.masks.begin(), out.masks.end() ), out.masks.end() );
			return out;
		}

		[[nodiscard]] inline int find_best_query_bit(
			const std::vector<std::uint64_t>& masks,
			std::size_t width ) noexcept
		{
			for ( int bit = static_cast<int>( width ) - 1; bit >= 0; --bit )
			{
				const std::uint64_t bit_mask = ( 1ull << bit );
				bool have0 = false;
				bool have1 = false;
				for ( const std::uint64_t mask : masks )
				{
					if ( ( mask & bit_mask ) == 0ull ) have0 = true;
					else have1 = true;
					if ( have0 && have1 )
						return bit;
				}
			}
			return -1;
		}

		[[nodiscard]] inline std::uint64_t width_mask(
			std::size_t width ) noexcept
		{
			if ( width >= 64u ) return ~0ull;
			if ( width == 0u ) return 0ull;
			return ( 1ull << width ) - 1ull;
		}

		[[nodiscard]] inline FirstStepResidualExplanation explain_first_step_residual(
			std::vector<std::uint64_t> rel_masks,
			std::uint64_t anchor_mask ) noexcept
		{
			FirstStepResidualExplanation out {};
			rel_masks = dedup_sorted_masks( std::move( rel_masks ) );
			out.family_size = rel_masks.size();
			if ( rel_masks.size() <= 1u )
				return out;

			const NormalizedMaskFamily normalized = normalize_mask_family( rel_masks, anchor_mask );
			out.normalized_width = normalized.width;
			out.normalized_masks = normalized.masks;
			out.anchor_projection = normalized.anchor_projection;

			const dv::ProjectiveRootSlopeSyncWinnerPathSummary full_path =
				dv::build_projective_root_slope_sync_winner_path_summary(
					out.normalized_masks,
					out.anchor_projection );
			out.full_query_depth = full_path.query_depth;
			out.full_winner_trace_word = full_path.winner_trace_word;

			const int best_bit = find_best_query_bit( out.normalized_masks, out.normalized_width );
			if ( best_bit < 0 )
				return out;

			out.first_query_bit_desc = static_cast<unsigned int>( best_bit );
			const std::uint64_t bit_mask = ( 1ull << best_bit );
			out.first_anchor_bit = ( out.anchor_projection & bit_mask ) != 0ull;
			bool chosen_one = out.first_anchor_bit;
			for ( const std::uint64_t mask : out.normalized_masks )
			{
				const bool bit_is_one = ( mask & bit_mask ) != 0ull;
				if ( bit_is_one == out.first_anchor_bit )
					out.first_step_survivors.push_back( mask );
			}
			if ( out.first_step_survivors.empty() )
			{
				chosen_one = !out.first_anchor_bit;
				for ( const std::uint64_t mask : out.normalized_masks )
				{
					const bool bit_is_one = ( mask & bit_mask ) != 0ull;
					if ( bit_is_one == chosen_one )
						out.first_step_survivors.push_back( mask );
				}
			}
			out.first_chosen_bit = chosen_one;
			if ( out.first_step_survivors.size() <= 1u )
				return out;

			const NormalizedMaskFamily residual =
				normalize_mask_family( out.first_step_survivors, out.anchor_projection );
			out.residual_width = residual.width;
			out.residual_masks = residual.masks;
			out.residual_anchor_projection = residual.anchor_projection;

			const dv::ProjectiveRootSlopeSyncWinnerPathSummary residual_path =
				dv::build_projective_root_slope_sync_winner_path_summary(
					out.residual_masks,
					out.residual_anchor_projection );
			out.residual_query_depth = residual_path.query_depth;
			out.residual_winner_trace_word = residual_path.winner_trace_word;
			return out;
		}

		[[nodiscard]] inline bool is_expected_nontrivial_shape(
			const FirstStepResidualExplanation& explanation ) noexcept
		{
			static const std::vector<std::uint64_t> expected_pair { 0ull, 3ull };
			return explanation.first_chosen_bit &&
				   explanation.first_step_survivors.size() == 2u &&
				   explanation.residual_width == 2u &&
				   explanation.residual_masks == expected_pair &&
				   explanation.residual_anchor_projection == 0ull &&
				   explanation.residual_query_depth == 1u &&
				   explanation.residual_winner_trace_word == 0ull &&
				   explanation.full_query_depth == 2u &&
				   explanation.full_winner_trace_word == 2ull;
		}

		[[nodiscard]] inline bool is_complement_pair_one_step_family(
			const FirstStepResidualExplanation& explanation ) noexcept
		{
			return explanation.full_query_depth == 1u &&
				   explanation.first_step_survivors.size() == 1u &&
				   explanation.normalized_masks.size() == 2u &&
				   ( explanation.normalized_masks[ 0 ] ^ explanation.normalized_masks[ 1 ] ) ==
					   width_mask( explanation.normalized_width );
		}

		[[nodiscard]] inline bool build_tail_profile_from_first_step(
			const FirstStepResidualExplanation& explanation,
			TailProfile& out ) noexcept
		{
			if ( !is_complement_pair_one_step_family( explanation ) )
				return false;
			const unsigned int query_bit = explanation.first_query_bit_desc;
			const std::uint64_t query_mask = ( query_bit >= 64u ) ? 0ull : ( 1ull << query_bit );
			bool found_winner = false;
			std::uint64_t winner_mask = 0ull;
			for ( const std::uint64_t mask : explanation.normalized_masks )
			{
				if ( ( ( mask & query_mask ) != 0ull ) == explanation.first_chosen_bit )
				{
					winner_mask = mask;
					found_winner = true;
					break;
				}
			}
			if ( !found_winner )
				return false;

			const std::uint64_t tail_mask = width_mask( query_bit );
			const std::uint64_t anchor_tail =
				( explanation.anchor_projection ^ winner_mask ) & tail_mask;
			if ( query_bit == 0u )
			{
				out.kind = "eps";
				out.exact_tail = "eps";
				return true;
			}

			const std::string exact_tail = binary_u64_string( anchor_tail, query_bit );
			std::size_t split = 0u;
			while ( split < exact_tail.size() && exact_tail[ split ] == '0' ) ++split;
			for ( std::size_t i = split; i < exact_tail.size(); ++i )
				if ( exact_tail[ i ] != '1' )
					return false;

			out.zero_len = split;
			out.one_len = exact_tail.size() - split;
			out.total_len = exact_tail.size();
			out.exact_tail = exact_tail;
			out.kind = "hook";
			if ( out.one_len == 0u ) out.kind = "zero";
			else if ( out.zero_len == 0u ) out.kind = "one";
			return true;
		}

		[[nodiscard]] inline std::string multiset_label(
			std::vector<std::string> labels ) noexcept
		{
			std::sort( labels.begin(), labels.end() );
			std::string out {};
			for ( std::size_t i = 0; i < labels.size(); ++i )
			{
				if ( i != 0u ) out += " || ";
				out += labels[ i ];
			}
			return out;
		}

		[[nodiscard]] inline std::string counted_plus_multiset_label(
			std::vector<std::string> labels ) noexcept
		{
			std::sort( labels.begin(), labels.end() );
			std::string out {};
			for ( std::size_t i = 0u; i < labels.size(); )
			{
				std::size_t j = i + 1u;
				while ( j < labels.size() && labels[ j ] == labels[ i ] ) ++j;
				if ( !out.empty() ) out += " + ";
				out += labels[ i ];
				if ( j - i > 1u ) out += "^" + std::to_string( j - i );
				i = j;
			}
			return out;
		}

		[[nodiscard]] inline std::string lower_ascii( std::string text ) noexcept
		{
			for ( char& ch : text )
				if ( ch >= 'A' && ch <= 'Z' )
					ch = static_cast<char>( ch - 'A' + 'a' );
			return text;
		}

		[[nodiscard]] inline std::string head_safe_ascii_slug( std::string text ) noexcept
		{
			text = lower_ascii( std::move( text ) );
			std::string out {};
			bool last_was_sep = false;
			for ( const char ch : text )
			{
				if ( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) )
				{
					out.push_back( ch );
					last_was_sep = false;
				}
				else if ( !out.empty() && !last_was_sep )
				{
					out.push_back( '_' );
					last_was_sep = true;
				}
			}
			if ( !out.empty() && out.back() == '_' ) out.pop_back();
			return out.empty() ? std::string { "empty" } : out;
		}

		[[nodiscard]] inline std::string tail_profile_exact_signature(
			const TailProfile& tail ) noexcept
		{
			return "T(Z:" + std::to_string( tail.zero_len ) +
				   ";O:" + std::to_string( tail.one_len ) + ")";
		}

		[[nodiscard]] inline std::string tail_terminal_signature_no_eps_len(
			const TailProfile& tail ) noexcept
		{
			if ( tail.kind == "eps" ) return "S(K:eps)";
			return "S(K:" + tail.kind + ";L:" + std::to_string( tail.total_len ) + ")";
		}

		[[nodiscard]] inline std::string trace0_length_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace0_tails )
				labels.push_back( "L=" + std::to_string( tail.total_len ) );
			return multiset_label( std::move( labels ) );
		}

		[[nodiscard]] inline std::string trace1_kind_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace1_tails )
				labels.push_back( "K=" + tail.kind );
			return multiset_label( std::move( labels ) );
		}

		[[nodiscard]] inline std::string trace0_exact_tail_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace0_tails )
				labels.push_back( "t=" + tail.exact_tail );
			return multiset_label( std::move( labels ) );
		}

		[[nodiscard]] inline std::string trace1_exact_tail_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace1_tails )
				labels.push_back( "t=" + tail.exact_tail );
			return multiset_label( std::move( labels ) );
		}

		[[nodiscard]] inline std::string trace0_one_length_selective_signature_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace0_tails )
			{
				if ( tail.kind == "one" )
					labels.push_back( "K:one:L:" + std::to_string( tail.total_len ) );
				else
					labels.push_back( "K:" + tail.kind );
			}
			const std::string out = counted_plus_multiset_label( std::move( labels ) );
			return out.empty() ? std::string { "eps" } : out;
		}

		[[nodiscard]] inline std::string trace1_nonhook_terminal_hook_profile_no_eps_signature_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const auto& tail : record.trace1_tails )
			{
				if ( tail.kind == "hook" )
					labels.push_back( tail_profile_exact_signature( tail ) );
				else
					labels.push_back( tail_terminal_signature_no_eps_len( tail ) );
			}
			const std::string out = counted_plus_multiset_label( std::move( labels ) );
			return out.empty() ? std::string { "eps" } : out;
		}

		[[nodiscard]] inline std::string pure_terminal_projection_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			return "trace0{" + trace0_length_reference_label( record ) +
				   "}|trace1{" + trace1_kind_reference_label( record ) + "}";
		}

		[[nodiscard]] inline std::string pure_terminal_exact_tail_reference_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			return "trace0{" + trace0_exact_tail_reference_label( record ) +
				   "}|trace1{" + trace1_exact_tail_reference_label( record ) + "}";
		}

		[[nodiscard]] inline std::string pure_terminal_kterm4t0olent1hookprofneps_label(
			const StructuredPureTerminalRecord& record ) noexcept
		{
			const std::string trace0sel = trace0_one_length_selective_signature_label( record );
			const std::string trace1mix = trace1_nonhook_terminal_hook_profile_no_eps_signature_label( record );
			return "kterm4t0olent1hookprofneps[o1_" + head_safe_ascii_slug( trace0sel ) +
				   ";m1_" + head_safe_ascii_slug( trace1mix ) + "]" +
				   "{trace0sel={" + trace0sel + "}|trace1mix={" + trace1mix + "}}";
		}

		[[nodiscard]] inline std::string raw_terminal_one_step_signature(
			const FirstStepResidualExplanation& explanation ) noexcept
		{
			std::vector<std::string> labels {};
			for ( const std::uint64_t mask : explanation.normalized_masks )
				labels.push_back( binary_u64_string( mask, explanation.normalized_width ) );
			return "width=" + std::to_string( explanation.normalized_width ) +
				   "|q=" + std::to_string( explanation.first_query_bit_desc ) +
				   "|anchor=" + binary_u64_string( explanation.anchor_projection, explanation.normalized_width ) +
				   "|masks={" + multiset_label( std::move( labels ) ) + "}";
		}

		[[nodiscard]] inline std::string reduced_terminal_one_step_signature(
			const FirstStepResidualExplanation& explanation ) noexcept
		{
			const char family = ( explanation.full_winner_trace_word == 0u ) ? '0' : '1';
			return std::string { "os1t" } + family +
				   "[q=" + std::to_string( explanation.first_query_bit_desc ) +
				   ";w=" + std::to_string( explanation.normalized_width ) + "]";
		}

		[[nodiscard]] inline std::string maybe_with_raw_sidecar(
			const std::string& base_label,
			const char* raw_prefix,
			const std::vector<std::string>& raw_signatures ) noexcept
		{
			if ( raw_signatures.empty() ) return base_label;
			return base_label + "|" + raw_prefix + "{" + multiset_label( raw_signatures ) + "}";
		}

		[[nodiscard]] inline std::string terminal_noncomplement_one_step_label(
			const std::vector<std::string>& raw_trace0_signatures,
			const std::vector<std::string>& raw_trace1_signatures,
			const char* raw0_prefix,
			const char* raw1_prefix ) noexcept
		{
			std::string out {};
			if ( !raw_trace0_signatures.empty() )
				out = std::string { raw0_prefix } + "{" + multiset_label( raw_trace0_signatures ) + "}";
			if ( !raw_trace1_signatures.empty() )
			{
				if ( !out.empty() ) out += "|";
				out += std::string { raw1_prefix } + "{" + multiset_label( raw_trace1_signatures ) + "}";
			}
			return out.empty() ? std::string { "eps" } : out;
		}

		inline void accumulate_noncomplement_one_step_candidate_families(
			ContinuationAuditStats& stats,
			const std::string& candidate_label ) noexcept
		{
			const std::string family = candidate_label;
			++stats.noncomplement_one_step_candidate_family_counts[ family ];
			if ( family == "os1t0" )
			{
				++stats.noncomplement_one_step_os1t0_profile_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t0_compact_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t0_base_signature_counts[ candidate_label ];
			}
			else if ( family == "os1t1" )
			{
				++stats.noncomplement_one_step_os1t1_tail_head_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_tail_signature_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_base_head_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_base_signature_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_chain_atom_head_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_atom_signature_counts[ candidate_label ];
				++stats.noncomplement_one_step_os1t1_chain_mode_counts[ "flat" ];
				++stats.noncomplement_one_step_os1t1_compact_payload_counts[ candidate_label ];
			}
		}

		inline void accumulate_kterm4t0olent1hookprofneps_label_stats(
			ContinuationAuditStats& stats,
			const std::string& candidate_label ) noexcept
		{
			const std::string family = candidate_label;
			++stats.kterm4t0olent1hookprofneps_candidate_family_counts[ family ];
			++stats.kterm4t0olent1hookprofneps_trace0_selective_signature_counts[ candidate_label ];
			++stats.kterm4t0olent1hookprofneps_trace1_mixed_signature_counts[ candidate_label ];
		}

		inline void capture_split_witness(
			ContinuationSplitWitness& witness,
			const std::string& common_label,
			const std::string& first_candidate_label,
			const std::string& first_reference_label,
			int first_n,
			std::uint64_t first_beta,
			std::uint64_t first_sub_constant,
			const std::string& second_candidate_label,
			const std::string& second_reference_label,
			int second_n,
			std::uint64_t second_beta,
			std::uint64_t second_sub_constant ) noexcept
		{
			if ( witness.found ) return;
			witness.found = true;
			witness.common_label = common_label;
			witness.first_candidate_label = first_candidate_label;
			witness.first_reference_label = first_reference_label;
			witness.first_n = first_n;
			witness.first_beta = first_beta;
			witness.first_sub_constant = first_sub_constant;
			witness.second_candidate_label = second_candidate_label;
			witness.second_reference_label = second_reference_label;
			witness.second_n = second_n;
			witness.second_beta = second_beta;
			witness.second_sub_constant = second_sub_constant;
		}

		inline void accumulate_partition_case(
			ContinuationPartitionStats& stats,
			const ao::VarConstQ2MainlineRequest& request,
			const std::string& candidate_label,
			const std::string& reference_label ) noexcept
		{
			++stats.covered_cases;
			++stats.reference_label_occurrences[ reference_label ];
			const std::uint64_t request_mask = dv::mask_n( request.n );
			const std::uint64_t beta = request.fixed_mask & request_mask;
			const std::uint64_t sub_constant = request.constant & request_mask;
			auto& bucket = stats.buckets[ candidate_label ];
			if ( bucket.occurrences == 0u )
			{
				bucket.first_n = request.n;
				bucket.first_beta = beta;
				bucket.first_sub_constant = sub_constant;
				bucket.first_reference_label = reference_label;
			}
			else if ( !bucket.candidate_collision && bucket.first_reference_label != reference_label )
			{
				bucket.candidate_collision = true;
				++stats.labels_with_candidate_collision;
				capture_split_witness(
					stats.first_candidate_collision_witness,
					candidate_label,
					candidate_label,
					bucket.first_reference_label,
					bucket.first_n,
					bucket.first_beta,
					bucket.first_sub_constant,
					candidate_label,
					reference_label,
					request.n,
					beta,
					sub_constant );
			}
			++bucket.occurrences;

			auto& reference = stats.references[ reference_label ];
			if ( reference.first_candidate_label.empty() )
			{
				reference.first_candidate_label = candidate_label;
				reference.first_n = request.n;
				reference.first_beta = beta;
				reference.first_sub_constant = sub_constant;
				return;
			}
			if ( !reference.split_seen && reference.first_candidate_label != candidate_label )
			{
				reference.split_seen = true;
				++stats.references_with_candidate_split;
				capture_split_witness(
					stats.first_reference_split_witness,
					reference_label,
					reference.first_candidate_label,
					reference_label,
					reference.first_n,
					reference.first_beta,
					reference.first_sub_constant,
					candidate_label,
					reference_label,
					request.n,
					beta,
					sub_constant );
			}
		}
	}  // namespace detail

	[[nodiscard]] inline ContinuationStateSnapshot analyze_projective_root_slope_summary(
		const dv::ProjectiveRootSlopeSummary& summary ) noexcept
	{
		ContinuationStateSnapshot out {};
		const auto profile = dv::build_projective_root_slope_relative_profile_summary( summary );
		const std::uint64_t anchor_mask = dv::projective_root_slope_anchor_mask( summary );
		dv::ProjectiveRootSlopeSyncCompetitionGroupMap groups =
			dv::build_projective_root_slope_sync_competition_groups( profile );

		StructuredPureTerminalRecord record {};
		bool terminal_non_complement_pair = false;
		std::vector<std::string> raw_trace0_reference_signatures {};
		std::vector<std::string> raw_trace1_reference_signatures {};
		std::vector<std::string> raw_trace0_candidate_signatures {};
		std::vector<std::string> raw_trace1_candidate_signatures {};

		for ( auto& [ unused_key, rel_masks ] : groups )
		{
			( void )unused_key;
			const FirstStepResidualExplanation explanation =
				detail::explain_first_step_residual( std::move( rel_masks ), anchor_mask );
			if ( explanation.full_query_depth == 0u ) continue;
			if ( explanation.first_step_survivors.size() > 1u )
			{
				if ( detail::is_expected_nontrivial_shape( explanation ) ) ++out.grammar.r_count;
				else ++out.grammar.u_count;
				continue;
			}
			if ( explanation.full_query_depth == 1u &&
				 ( explanation.full_winner_trace_word == 0u || explanation.full_winner_trace_word == 1u ) )
			{
				TailProfile tail {};
				const bool is_complement_pair = detail::build_tail_profile_from_first_step( explanation, tail );
				if ( explanation.full_winner_trace_word == 0u )
				{
					++out.grammar.t0_count;
					if ( is_complement_pair ) record.trace0_tails.push_back( std::move( tail ) );
					else
					{
						terminal_non_complement_pair = true;
						raw_trace0_reference_signatures.push_back( detail::raw_terminal_one_step_signature( explanation ) );
						raw_trace0_candidate_signatures.push_back( detail::reduced_terminal_one_step_signature( explanation ) );
					}
				}
				else
				{
					++out.grammar.t1_count;
					if ( is_complement_pair ) record.trace1_tails.push_back( std::move( tail ) );
					else
					{
						terminal_non_complement_pair = true;
						raw_trace1_reference_signatures.push_back( detail::raw_terminal_one_step_signature( explanation ) );
						raw_trace1_candidate_signatures.push_back( detail::reduced_terminal_one_step_signature( explanation ) );
					}
				}
				continue;
			}
			++out.grammar.u_count;
		}

		if ( out.grammar.t0_count == 0u && out.grammar.t1_count == 0u &&
			 out.grammar.r_count == 0u && out.grammar.u_count == 0u )
		{
			out.gamma_kind = GammaKind::epsilon;
			out.coverage_kind = CoverageKind::no_active_family;
			out.coverage_reason = coverage_kind_label( out.coverage_kind );
			return out;
		}
		if ( out.grammar.r_count == 1u && out.grammar.t0_count == 0u &&
			 out.grammar.t1_count == 0u && out.grammar.u_count == 0u )
		{
			out.gamma_kind = GammaKind::rigid;
			out.coverage_kind = CoverageKind::rigid_only;
			out.coverage_reason = coverage_kind_label( out.coverage_kind );
			return out;
		}
		if ( out.grammar.r_count == 0u && out.grammar.u_count == 0u )
		{
			out.gamma_kind = GammaKind::terminal;
			out.projected_tail_reference_label = detail::maybe_with_raw_sidecar(
				detail::maybe_with_raw_sidecar(
					detail::pure_terminal_projection_reference_label( record ),
					"raw0",
					raw_trace0_reference_signatures ),
				"raw1",
				raw_trace1_reference_signatures );
			out.exact_tail_reference_label = detail::maybe_with_raw_sidecar(
				detail::maybe_with_raw_sidecar(
					detail::pure_terminal_exact_tail_reference_label( record ),
					"raw0",
					raw_trace0_reference_signatures ),
				"raw1",
				raw_trace1_reference_signatures );
			out.kterm4t0olent1hookprofneps_label = detail::maybe_with_raw_sidecar(
				detail::maybe_with_raw_sidecar(
					detail::pure_terminal_kterm4t0olent1hookprofneps_label( record ),
					"raw0",
					raw_trace0_candidate_signatures ),
				"raw1",
				raw_trace1_candidate_signatures );
			if ( terminal_non_complement_pair )
			{
				out.noncomplement_one_step_reference_label =
					detail::terminal_noncomplement_one_step_label(
						raw_trace0_reference_signatures,
						raw_trace1_reference_signatures,
						"raw0",
						"raw1" );
				out.noncomplement_one_step_candidate_label =
					detail::terminal_noncomplement_one_step_label(
						raw_trace0_candidate_signatures,
						raw_trace1_candidate_signatures,
						"raw0",
						"raw1" );
				if ( !out.noncomplement_one_step_reference_label.empty() &&
					 !out.noncomplement_one_step_candidate_label.empty() )
					out.coverage_kind = CoverageKind::prototype_noncomplement_one_step;
				else
					out.coverage_kind = CoverageKind::terminal_non_complement_pair;
				out.coverage_reason = coverage_kind_label( out.coverage_kind );
				return out;
			}
			out.coverage_kind = CoverageKind::structured_pure_terminal;
			out.coverage_reason = coverage_kind_label( out.coverage_kind );
			out.structured_pure_terminal = true;
			return out;
		}
		out.gamma_kind = GammaKind::mixed_or_unexpected;
		out.coverage_kind = CoverageKind::mixed_or_unexpected;
		out.coverage_reason = coverage_kind_label( out.coverage_kind );
		return out;
	}

	FixedBetaFrozenContinuationSnapshot operator_run_frozen_fixed_beta_flat_with_continuation(
		const ao::VarConstQ2MainlineRequest& request ) noexcept
	{
		FixedBetaFrozenContinuationSnapshot out {};
		if ( request.direction != ao::VarConstQ2Direction::fixed_beta_to_alpha ||
			 request.n <= 0 || request.n > 64 )
			return out;
		const std::uint64_t request_mask = dv::mask_n( request.n );
		const std::uint64_t beta = request.fixed_mask & request_mask;
		const std::uint64_t add_constant =
			dv::normalize_add_constant_for_q2( request.operation, request.constant, request.n );
		const dv::FixedBetaFlatTheoremArtifacts artifacts =
			dv::build_fixed_beta_flat_theorem_artifacts( beta, add_constant, request.n );
		const dv::VarConstLayerDpSummary layer_summary =
			dv::summarize_projective_segment_summary_best( artifacts.summary );
		const dv::ProjectiveRootSlopeSummary root_slope =
			dv::build_projective_root_slope_summary_from_segment_summary( artifacts.summary );
		out.available = true;
		out.result = dv::layer_dp_summary_to_q2_mainline( layer_summary, request.n );
		dv::populate_fixed_beta_flat_mainline_stats( artifacts, out.stats );
		out.continuation = analyze_projective_root_slope_summary( root_slope );
		return out;
	}

	void operator_accumulate_continuation_case(
		ContinuationAuditStats& stats,
		const ao::VarConstQ2MainlineRequest& request,
		const ContinuationStateSnapshot& snapshot ) noexcept
	{
		++stats.total_cases;
		switch ( snapshot.gamma_kind )
		{
		case GammaKind::epsilon: ++stats.gamma_epsilon_cases; break;
		case GammaKind::rigid: ++stats.gamma_rigid_cases; break;
		case GammaKind::terminal: ++stats.gamma_terminal_cases; break;
		case GammaKind::mixed_or_unexpected:
		default: ++stats.gamma_mixed_or_unexpected_cases; break;
		}
		switch ( snapshot.coverage_kind )
		{
		case CoverageKind::no_active_family: ++stats.no_active_family_cases; return;
		case CoverageKind::rigid_only: ++stats.rigid_only_cases; return;
		case CoverageKind::mixed_or_unexpected: ++stats.mixed_or_unexpected_cases; return;
		case CoverageKind::terminal_non_complement_pair: ++stats.terminal_non_complement_pair_cases; break;
		case CoverageKind::prototype_noncomplement_one_step: ++stats.prototype_noncomplement_one_step_cases; break;
		case CoverageKind::structured_pure_terminal:
		default: ++stats.structured_pure_terminal_cases; break;
		}
		if ( !snapshot.noncomplement_one_step_reference_label.empty() &&
			 !snapshot.noncomplement_one_step_candidate_label.empty() )
		{
			detail::accumulate_noncomplement_one_step_candidate_families(
				stats,
				snapshot.noncomplement_one_step_candidate_label );
			detail::accumulate_partition_case(
				stats.noncomplement_one_step,
				request,
				snapshot.noncomplement_one_step_candidate_label,
				snapshot.noncomplement_one_step_reference_label );
		}
		if ( !snapshot.exact_tail_reference_label.empty() &&
			 !snapshot.kterm4t0olent1hookprofneps_label.empty() )
		{
			detail::accumulate_kterm4t0olent1hookprofneps_label_stats(
				stats,
				snapshot.kterm4t0olent1hookprofneps_label );
			detail::accumulate_partition_case(
				stats.kterm4t0olent1hookprofneps_exact_tail,
				request,
				snapshot.kterm4t0olent1hookprofneps_label,
				snapshot.exact_tail_reference_label );
		}
	}

	void operator_finalize_continuation_stats(
		ContinuationAuditStats& stats ) noexcept
	{
		stats.noncomplement_one_step.unique_candidate_labels =
			stats.noncomplement_one_step.buckets.size();
		stats.noncomplement_one_step.unique_reference_labels =
			stats.noncomplement_one_step.reference_label_occurrences.size();
		stats.kterm4t0olent1hookprofneps_exact_tail.unique_candidate_labels =
			stats.kterm4t0olent1hookprofneps_exact_tail.buckets.size();
		stats.kterm4t0olent1hookprofneps_exact_tail.unique_reference_labels =
			stats.kterm4t0olent1hookprofneps_exact_tail.reference_label_occurrences.size();
	}
}  // namespace gap_b_fixed_beta_research_continuation

namespace gap_b_fixed_beta_q2_prototype
{
	namespace ao = TwilightDream::arx_operators;
	namespace rc = gap_b_fixed_beta_research_continuation;

	namespace detail
	{
		[[nodiscard]] constexpr PrototypeCapabilities current_fixed_beta_q2_prototype_capabilities() noexcept
		{
			return PrototypeCapabilities {
				"fixed_beta_root_exact_plus_continuation_or2lrcbdfjem_noncomp1sup_kterm4t0olent1hookprofneps_live",
				"real_width_root_exact_delegate_1_to_64_with_live_continuation_labels",
				"continuation_support_over_exact_root_result",
				"delegated_fixed_beta_flat_theorem_direct",
				"projective_root_slope_summary_partition_labels_plus_prototype_noncomplement_one_step_plus_live_exact_tail_kterm4t0olent1hookprofneps",
				true,
				false,
				1,
				64,
				false,
				true,
				true };
		}

		[[nodiscard]] constexpr bool prototype_supports_request(
			const ao::VarConstQ2MainlineRequest& request ) noexcept
		{
			const PrototypeCapabilities caps = current_fixed_beta_q2_prototype_capabilities();
			return request.direction == ao::VarConstQ2Direction::fixed_beta_to_alpha &&
				   request.n >= caps.min_supported_n &&
				   request.n <= caps.max_supported_n;
		}
	}  // namespace detail

	PrototypeCapabilities operator_fixed_beta_q2_prototype_capabilities() noexcept
	{
		return detail::current_fixed_beta_q2_prototype_capabilities();
	}

	bool operator_fixed_beta_q2_prototype_supports_request(
		const ao::VarConstQ2MainlineRequest& request ) noexcept
	{
		return detail::prototype_supports_request( request );
	}

	PrototypeRun operator_run_fixed_beta_q2_prototype(
		const ao::VarConstQ2MainlineRequest& request ) noexcept
	{
		PrototypeRun out {};
		if ( !detail::prototype_supports_request( request ) ) return out;
		const rc::FixedBetaFrozenContinuationSnapshot snapshot =
			rc::operator_run_frozen_fixed_beta_flat_with_continuation( request );
		out.available = snapshot.available;
		out.result = snapshot.result;
		out.stats = snapshot.stats;
		out.continuation = snapshot.continuation;
		return out;
	}
}  // namespace gap_b_fixed_beta_q2_prototype
