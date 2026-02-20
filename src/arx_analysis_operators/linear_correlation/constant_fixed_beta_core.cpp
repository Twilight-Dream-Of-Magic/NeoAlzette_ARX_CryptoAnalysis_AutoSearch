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
