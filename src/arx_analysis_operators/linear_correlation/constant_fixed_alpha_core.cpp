/**
 * @file linear_varconst_fixed_alpha_core_impl.hpp
 * @brief Internal Fixed-alpha exact Q2 core implementation extracted from the public facade.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
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
			std::uint64_t		fixed_mask { 0 };
			std::uint64_t		constant { 0 };
			int					n { 0 };
			VarConstQ2Direction direction { VarConstQ2Direction::fixed_alpha_to_beta };
			VarConstQ2Operation operation { VarConstQ2Operation::modular_add };
		};

		struct VarConstQ2MainlineResult
		{
			std::uint64_t optimal_mask { 0 };
			std::uint64_t abs_weight { 0 };
			bool		  abs_weight_is_exact_2pow64 { false };
			double		  abs_correlation { 0.0 };
			SearchWeight  ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

		struct VarConstOptimalOutputMaskResult
		{
			std::uint64_t beta { 0 };
			std::uint64_t abs_weight { 0 };
			bool		  abs_weight_is_exact_2pow64 { false };
			double		  abs_correlation { 0.0 };
			SearchWeight  ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

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

			struct ProjectiveSegmentSummaryHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveSegmentSummary& summary ) const noexcept;
			};

			struct ProjectiveSegmentSummaryEqual
			{
				[[nodiscard]] bool operator()( const ProjectiveSegmentSummary& lhs, const ProjectiveSegmentSummary& rhs ) const noexcept;
			};

			[[nodiscard]] constexpr std::uint64_t mask_n( int n ) noexcept
			{
				if ( n <= 0 )
					return 0ull;
				if ( n >= 64 )
					return ~0ull;
				return ( std::uint64_t( 1 ) << n ) - 1ull;
			}

			[[nodiscard]] constexpr int floor_log2_u64( std::uint64_t value ) noexcept
			{
				return ( value == 0ull ) ? -1 : ( 63 - static_cast<int>( std::countl_zero( value ) ) );
			}

			[[nodiscard]] constexpr std::uint64_t packed_exact_2pow64_weight() noexcept
			{
				return 0ull;
			}

			[[nodiscard]] inline CarryWide carry_wide_zero() noexcept
			{
				return CarryWide {};
			}

			[[nodiscard]] inline CarryWide carry_wide_from_i64( std::int64_t value ) noexcept
			{
				return CarryWide { value };
			}

			[[nodiscard]] inline CarryWide carry_wide_from_u128_bits( uint128_t bits ) noexcept
			{
				return CarryWide::from_words( bits.high64(), bits.low64() );
			}

			[[nodiscard]] inline uint128_t carry_wide_abs_to_u128( const CarryWide& value ) noexcept
			{
				return value.magnitude_bits();
			}

			[[nodiscard]] inline uint128_t uabs128_twos( const CarryWide& value ) noexcept
			{
				return value.magnitude_bits();
			}

			[[nodiscard]] inline std::size_t hash_carry_wide( const CarryWide& value ) noexcept
			{
				return std::hash<CarryWide> {}( value );
			}

			[[nodiscard]] inline std::size_t hash_u128( uint128_t value ) noexcept
			{
				const std::uint64_t lo = value.low64();
				const std::uint64_t hi = value.high64();
				const std::size_t	h0 = std::hash<std::uint64_t> {}( lo );
				const std::size_t	h1 = std::hash<std::uint64_t> {}( hi );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ull + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			[[nodiscard]] static inline bool exact_abs_weight_is_zero( const ExactAbsWeight& value ) noexcept
			{
				return !value.is_exact_2pow64 && value.packed_weight == 0ull;
			}

			[[nodiscard]] inline uint128_t exact_abs_weight_to_u128( const ExactAbsWeight& value ) noexcept
			{
				if ( value.is_exact_2pow64 )
					return uint128_t { 1 } << 64;
				return uint128_t { value.packed_weight };
			}

			[[nodiscard]] static inline bool exact_abs_weight_equal( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept
			{
				return lhs.is_exact_2pow64 == rhs.is_exact_2pow64 && lhs.packed_weight == rhs.packed_weight;
			}

			[[nodiscard]] static inline bool exact_abs_weight_less( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept
			{
				if ( lhs.is_exact_2pow64 != rhs.is_exact_2pow64 )
					return !lhs.is_exact_2pow64 && rhs.is_exact_2pow64;
				return lhs.packed_weight < rhs.packed_weight;
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_weight_from_u128( uint128_t value ) noexcept
			{
				if ( value == 0 )
					return {};
				if ( value == ( uint128_t { 1 } << 64 ) )
					return { packed_exact_2pow64_weight(), true };
				return { static_cast<std::uint64_t>( value.low64() ), false };
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_weight_from_u128_projective( uint128_t value ) noexcept
			{
				return exact_abs_weight_from_u128( value );
			}

			[[nodiscard]] static inline double exact_abs_weight_to_correlation( const ExactAbsWeight& value, int n ) noexcept
			{
				if ( exact_abs_weight_is_zero( value ) )
					return 0.0;
				if ( value.is_exact_2pow64 )
					return 1.0;
				return std::ldexp( static_cast<double>( value.packed_weight ), -n );
			}

			[[nodiscard]] static inline SearchWeight exact_abs_weight_to_ceil_linear_weight_int( const ExactAbsWeight& value, int n ) noexcept
			{
				if ( exact_abs_weight_is_zero( value ) )
					return TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
				if ( value.is_exact_2pow64 )
					return 0;
				return static_cast<SearchWeight>( n - floor_log2_u64( value.packed_weight ) );
			}

			[[nodiscard]] inline VarConstOptimalOutputMaskResult make_beta_result( std::uint64_t beta, const ExactAbsWeight& abs_weight, int n ) noexcept
			{
				VarConstOptimalOutputMaskResult out {};
				out.beta = beta;
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

			[[nodiscard]] inline VarConstOptimalOutputMaskResult layer_dp_summary_to_beta( const VarConstLayerDpSummary& summary, int n ) noexcept
			{
				return make_beta_result( summary.optimal_mask, summary.abs_weight, n );
			}

			[[nodiscard]] static inline VarConstQ2MainlineResult layer_dp_summary_to_q2_mainline( const VarConstLayerDpSummary& summary, int n ) noexcept
			{
				return make_q2_mainline_result( summary.optimal_mask, summary.abs_weight, n );
			}

			[[nodiscard]] static inline VarConstOptimalOutputMaskResult q2_mainline_to_beta_result( const VarConstQ2MainlineResult& result ) noexcept
			{
				VarConstOptimalOutputMaskResult out {};
				out.beta = result.optimal_mask;
				out.abs_weight = result.abs_weight;
				out.abs_weight_is_exact_2pow64 = result.abs_weight_is_exact_2pow64;
				out.abs_correlation = result.abs_correlation;
				out.ceil_linear_weight_int = result.ceil_linear_weight_int;
				return out;
			}

			[[nodiscard]] inline bool bit_matrix_equal( const BitMatrix& lhs, const BitMatrix& rhs ) noexcept
			{
				return lhs.m00 == rhs.m00 && lhs.m01 == rhs.m01 && lhs.m10 == rhs.m10 && lhs.m11 == rhs.m11;
			}

			[[nodiscard]] inline std::size_t hash_bit_matrix( const BitMatrix& matrix ) noexcept
			{
				std::size_t h = std::hash<int> {}( matrix.m00 );
				h ^= std::hash<int> {}( matrix.m01 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( matrix.m10 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( matrix.m11 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t DirectionKeyHash::operator()( const DirectionKey& key ) const noexcept
			{
				return hash_carry_wide( key.x ) ^ ( hash_carry_wide( key.y ) + 0x9e3779b97f4a7c15ull );
			}

			inline std::size_t ProjectiveMatrixKeyHash::operator()( const ProjectiveMatrixKey& key ) const noexcept
			{
				std::size_t h = hash_carry_wide( key.m00 );
				h ^= hash_carry_wide( key.m01 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				h ^= hash_carry_wide( key.m10 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				h ^= hash_carry_wide( key.m11 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t ProjectiveSupportVectorKeyHash::operator()( const ProjectiveSupportVectorKey& key ) const noexcept
			{
				const std::size_t h0 = hash_carry_wide( key.x );
				const std::size_t h1 = hash_carry_wide( key.y );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ull + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			inline bool HomogeneousQ2ChoiceBlockDescriptor::operator==( const HomogeneousQ2ChoiceBlockDescriptor& other ) const noexcept
			{
				return len == other.len && bit_matrix_equal( choose_mask0, other.choose_mask0 ) && bit_matrix_equal( choose_mask1, other.choose_mask1 );
			}

			inline std::size_t HomogeneousQ2ChoiceBlockDescriptorHash::operator()( const HomogeneousQ2ChoiceBlockDescriptor& descriptor ) const noexcept
			{
				std::size_t h = hash_bit_matrix( descriptor.choose_mask0 );
				h ^= hash_bit_matrix( descriptor.choose_mask1 ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				h ^= std::hash<int> {}( descriptor.len ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
				return h;
			}

			inline std::size_t StateKeyHash::operator()( const StateKey& key ) const noexcept
			{
				const std::size_t h0 = hash_carry_wide( key.v0 );
				const std::size_t h1 = hash_carry_wide( key.v1 );
				return h0 ^ ( h1 + 0x9e3779b97f4a7c15ull + ( h0 << 6 ) + ( h0 >> 2 ) );
			}

			inline std::size_t ProjectiveSegmentSummaryHash::operator()( const ProjectiveSegmentSummary& summary ) const noexcept
			{
				std::size_t sum_hash = 0u;
				std::size_t xor_hash = 0u;
				for ( const auto& [ key, value ] : summary.table )
				{
					std::size_t h = ProjectiveMatrixKeyHash {}( key );
					h ^= hash_u128( value.scale ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
					h ^= std::hash<std::uint64_t> {}( value.mask ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
					sum_hash += h;
					xor_hash ^= h + 0x9e3779b97f4a7c15ull + ( xor_hash << 6 ) + ( xor_hash >> 2 );
				}

				std::size_t out = std::hash<int> {}( summary.bit_length );
				out ^= std::hash<std::size_t> {}( summary.table.size() ) + 0x9e3779b97f4a7c15ull + ( out << 6 ) + ( out >> 2 );
				out ^= sum_hash + 0x9e3779b97f4a7c15ull + ( out << 6 ) + ( out >> 2 );
				out ^= xor_hash + 0x9e3779b97f4a7c15ull + ( out << 6 ) + ( out >> 2 );
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

			[[nodiscard]] static inline BitMatrix build_matrix_for_beta_bit( int alpha_i, int constant_i, int beta_i ) noexcept
			{
				BitMatrix matrix {};
				for ( int carry_in = 0; carry_in <= 1; ++carry_in )
				{
					for ( int x = 0; x <= 1; ++x )
					{
						const int carry_out = arx_operators::carry_out_bit( x, constant_i, carry_in );
						const int z_i = x ^ constant_i ^ carry_in;
						const int exponent = ( alpha_i & x ) ^ ( beta_i & z_i );
						const int sign = exponent ? -1 : 1;
						if ( carry_in == 0 && carry_out == 0 )
							matrix.m00 += sign;
						else if ( carry_in == 0 && carry_out == 1 )
							matrix.m01 += sign;
						else if ( carry_in == 1 && carry_out == 0 )
							matrix.m10 += sign;
						else
							matrix.m11 += sign;
					}
				}
				return matrix;
			}

			[[nodiscard]] inline VarConstQ2BitModel build_output_mask_q2_bit_model( std::uint64_t alpha, std::uint64_t add_constant, int n ) noexcept
			{
				VarConstQ2BitModel model {};
				model.n = n;
				for ( int bit = 0; bit < n; ++bit )
				{
					const int alpha_i = static_cast<int>( ( alpha >> bit ) & 1ull );
					const int constant_i = static_cast<int>( ( add_constant >> bit ) & 1ull );
					model.choose_mask0[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( alpha_i, constant_i, 0 );
					model.choose_mask1[ static_cast<std::size_t>( bit ) ] = build_matrix_for_beta_bit( alpha_i, constant_i, 1 );
				}
				return model;
			}

			inline void apply_row_step( const BitMatrix& matrix, CarryWide v0, CarryWide v1, CarryWide& o0, CarryWide& o1 ) noexcept
			{
				o0 = v0 * carry_wide_from_i64( matrix.m00 ) + v1 * carry_wide_from_i64( matrix.m10 );
				o1 = v0 * carry_wide_from_i64( matrix.m01 ) + v1 * carry_wide_from_i64( matrix.m11 );
			}

			[[nodiscard]] static inline ExactAbsWeight exact_abs_sum_v0_v1( CarryWide v0, CarryWide v1 ) noexcept
			{
				const CarryWide sum = v0 + v1;
				const uint128_t magnitude = carry_wide_abs_to_u128( sum );
				if ( magnitude == 0 )
					return {};
				if ( magnitude == ( uint128_t { 1 } << 64 ) )
					return { packed_exact_2pow64_weight(), true };
				return { magnitude.low64(), false };
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

				const bool		negative = ( value < 0 );
				const uint128_t abs_value = carry_wide_abs_to_u128( value );
				const CarryWide quotient = carry_wide_from_u128_bits( abs_value / divisor );
				return negative ? -quotient : quotient;
			}

			[[nodiscard]] static inline std::pair<DirectionKey, uint128_t> normalize_projective_direction( CarryWide v0, CarryWide v1 ) noexcept
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

			[[nodiscard]] static inline ExactAbsWeight scale_exact_abs_weight_projective( const ExactAbsWeight& weight, uint128_t scale ) noexcept
			{
				if ( scale == 0 || exact_abs_weight_is_zero( weight ) )
					return {};
				if ( weight.is_exact_2pow64 )
				{
					if ( scale == 1 )
						return { packed_exact_2pow64_weight(), true };
					return { packed_exact_2pow64_weight(), false };
				}
				return exact_abs_weight_from_u128_projective( uint128_t { weight.packed_weight } * scale );
			}

			inline void relax_edge( CarryLayer& next, const StateKey& out, std::uint64_t candidate_prefix, std::uint64_t& transitions ) noexcept
			{
				++transitions;
				const auto [ it, inserted ] = next.try_emplace( out, candidate_prefix );
				if ( !inserted && candidate_prefix < it->second )
					it->second = candidate_prefix;
			}

			[[nodiscard]] inline ExactWideMatrix exact_wide_matrix_from_bit( const BitMatrix& matrix ) noexcept
			{
				return ExactWideMatrix { carry_wide_from_i64( matrix.m00 ), carry_wide_from_i64( matrix.m01 ), carry_wide_from_i64( matrix.m10 ), carry_wide_from_i64( matrix.m11 ) };
			}

			[[nodiscard]] inline ExactWideMatrix projective_matrix_key_to_exact_wide( const ProjectiveMatrixKey& key ) noexcept
			{
				return ExactWideMatrix { key.m00, key.m01, key.m10, key.m11 };
			}

			[[nodiscard]] inline ExactWideMatrix multiply_exact_wide_mm( const ExactWideMatrix& lhs, const ExactWideMatrix& rhs ) noexcept
			{
				return ExactWideMatrix { lhs.m00 * rhs.m00 + lhs.m01 * rhs.m10, lhs.m00 * rhs.m01 + lhs.m01 * rhs.m11, lhs.m10 * rhs.m00 + lhs.m11 * rhs.m10, lhs.m10 * rhs.m01 + lhs.m11 * rhs.m11 };
			}

			[[nodiscard]] inline std::pair<ProjectiveMatrixKey, uint128_t> normalize_projective_matrix( ExactWideMatrix matrix ) noexcept
			{
				const uint128_t a0 = carry_wide_abs_to_u128( matrix.m00 );
				const uint128_t a1 = carry_wide_abs_to_u128( matrix.m01 );
				const uint128_t a2 = carry_wide_abs_to_u128( matrix.m10 );
				const uint128_t a3 = carry_wide_abs_to_u128( matrix.m11 );
				uint128_t		g = gcd_u128( a0, a1 );
				g = gcd_u128( g, a2 );
				g = gcd_u128( g, a3 );
				if ( g == 0 )
					return { {}, 0 };

				matrix.m00 = carry_wide_div_exact_by_u128( matrix.m00, g );
				matrix.m01 = carry_wide_div_exact_by_u128( matrix.m01, g );
				matrix.m10 = carry_wide_div_exact_by_u128( matrix.m10, g );
				matrix.m11 = carry_wide_div_exact_by_u128( matrix.m11, g );

				const bool flip_sign = ( matrix.m00 < 0 ) || ( matrix.m00 == 0 && matrix.m01 < 0 ) || ( matrix.m00 == 0 && matrix.m01 == 0 && matrix.m10 < 0 ) || ( matrix.m00 == 0 && matrix.m01 == 0 && matrix.m10 == 0 && matrix.m11 < 0 );
				if ( flip_sign )
				{
					matrix.m00 = -matrix.m00;
					matrix.m01 = -matrix.m01;
					matrix.m10 = -matrix.m10;
					matrix.m11 = -matrix.m11;
				}

				return { ProjectiveMatrixKey { matrix.m00, matrix.m01, matrix.m10, matrix.m11 }, g };
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

				for ( const auto& [ left_key, left_value ] : left.table )
				{
					const ExactWideMatrix left_matrix = projective_matrix_key_to_exact_wide( left_key );
					for ( const auto& [ right_key, right_value ] : right.table )
					{
						const ExactWideMatrix product = multiply_exact_wide_mm( left_matrix, projective_matrix_key_to_exact_wide( right_key ) );
						const auto [ product_key, product_scale ] = normalize_projective_matrix( product );
						const uint128_t		total_scale = left_value.scale * right_value.scale * product_scale;
						const std::uint64_t shifted_right = ( left.bit_length >= 64 ) ? 0ull : ( right_value.mask << left.bit_length );
						relax_projective_segment_entry( out, product_key, total_scale, left_value.mask | shifted_right );
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

					[[nodiscard]] std::size_t operator()( const PoweredDescriptor& descriptor ) const noexcept
					{
						std::size_t h = base_hash( descriptor.descriptor );
						h ^= std::hash<std::size_t> {}( descriptor.repeat_count ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
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
						h ^= std::hash<std::size_t> {}( key.right_id ) + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
						return h;
					}
				};

				std::vector<PoweredDescriptor> leaves {};
				leaves.reserve( descriptors.size() );
				std::size_t local_power_runs = 0u;
				std::size_t local_power_collapsed_blocks = 0u;
				for ( std::size_t i = 0u; i < descriptors.size(); )
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

				const auto intern_summary = [ & ]( ProjectiveSegmentSummary summary, bool is_leaf ) noexcept -> std::size_t {
					const auto found = summary_cache.find( summary );
					if ( found != summary_cache.end() )
					{
						++local_tree_reuse_hits;
						return found->second;
					}
					local_stats.max_segment_entries = ( std::max )( local_stats.max_segment_entries, summary.table.size() );
					const std::size_t id = node_summaries.size();
					node_summaries.push_back( std::move( summary ) );
					summary_cache.emplace( node_summaries.back(), id );
					if ( is_leaf )
						++local_unique_leaf_nodes;
					return id;
				};

				const auto intern_leaf = [ & ]( const PoweredDescriptor& leaf ) noexcept -> std::size_t {
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

				const auto build_tree = [ & ]( auto&& self, std::size_t begin, std::size_t end ) noexcept -> std::size_t {
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

			[[nodiscard]] inline ProjectiveSegmentSummary build_homogeneous_q2_choice_block_summary( const HomogeneousQ2ChoiceBlockDescriptor& descriptor ) noexcept
			{
				ProjectiveSegmentSummary single_bit {};
				single_bit.bit_length = 1;
				single_bit.table.reserve( 2u );
				relax_projective_segment_entry_from_exact_matrix( single_bit, exact_wide_matrix_from_bit( descriptor.choose_mask0 ), 0ull );
				relax_projective_segment_entry_from_exact_matrix( single_bit, exact_wide_matrix_from_bit( descriptor.choose_mask1 ), 1ull );
				return pow_projective_segment_summary( single_bit, static_cast<std::size_t>( descriptor.len ), nullptr );
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
					const auto [ direction, row_scale ] = normalize_projective_direction( key.m00, key.m01 );
					if ( row_scale == 0 )
						continue;
					relax_projective_root_query_entry( out, direction, value.scale * row_scale, value.mask );
				}
				return out;
			}

			[[nodiscard]] inline ExactAbsWeight exact_abs_dot_projective_direction_support( const DirectionKey& direction, const ProjectiveSupportVectorKey& vector ) noexcept
			{
				return exact_abs_sum_v0_v1( direction.x * vector.x, direction.y * vector.y );
			}

			[[nodiscard]] inline ProjectiveSuffixChoice solve_projective_support_summary_query( const ProjectiveSupportSummary& support, const DirectionKey& direction ) noexcept
			{
				ProjectiveSuffixChoice out {};
				for ( const auto& [ key, value ] : support.table )
				{
					const ExactAbsWeight unit_abs = exact_abs_dot_projective_direction_support( direction, key );
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
				for ( const auto& [ direction, value ] : root.table )
				{
					const ProjectiveSuffixChoice suffix = solve_projective_support_summary_query( support, direction );
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

			[[nodiscard]] inline ExactAbsWeight evaluate_exact_abs_weight_for_alpha_beta( std::uint64_t alpha, std::uint64_t add_constant, std::uint64_t beta, int n ) noexcept
			{
				if ( n <= 0 || n > 64 )
					return {};

				const std::uint64_t m = mask_n( n );
				alpha &= m;
				add_constant &= m;
				beta &= m;

				std::array<ExactWideMatrix, 64> matrices {};
				for ( int bit = 0; bit < n; ++bit )
				{
					const int alpha_i = static_cast<int>( ( alpha >> bit ) & 1ull );
					const int constant_i = static_cast<int>( ( add_constant >> bit ) & 1ull );
					const int beta_i = static_cast<int>( ( beta >> bit ) & 1ull );
					matrices[ static_cast<std::size_t>( bit ) ] = exact_wide_matrix_from_bit( build_matrix_for_beta_bit( alpha_i, constant_i, beta_i ) );
				}

				std::size_t active = static_cast<std::size_t>( n );
				while ( active > 1u )
				{
					std::size_t next = 0u;
					std::size_t i = 0u;
					for ( ; i + 1u < active; i += 2u )
						matrices[ next++ ] = multiply_exact_wide_mm( matrices[ i ], matrices[ i + 1u ] );
					if ( i < active )
						matrices[ next++ ] = matrices[ i ];
					active = next;
				}
				return exact_abs_sum_v0_v1( matrices[ 0 ].m00, matrices[ 0 ].m01 );
			}

			[[nodiscard]] inline VarConstLayerDpSummary run_raw_carry_layer_dp_from_model( const VarConstQ2BitModel& model ) noexcept
			{
				CarryLayer		  current {};
				CarryLayer		  next {};
				const std::size_t reserve_hint = static_cast<std::size_t>( 1 ) << static_cast<std::size_t>( ( std::min )( model.n, 16 ) );
				current.reserve( reserve_hint );
				next.reserve( reserve_hint );

				current.emplace( StateKey { carry_wide_from_i64( 1 ), carry_wide_zero() }, 0ull );
				std::uint64_t transitions = 0;

				for ( int bit = 0; bit < model.n; ++bit )
				{
					next.clear();
					const BitMatrix& m0 = model.choose_mask0[ static_cast<std::size_t>( bit ) ];
					const BitMatrix& m1 = model.choose_mask1[ static_cast<std::size_t>( bit ) ];
					for ( const auto& [ state, prefix ] : current )
					{
						CarryWide o0 = carry_wide_zero();
						CarryWide o1 = carry_wide_zero();
						apply_row_step( m0, state.v0, state.v1, o0, o1 );
						relax_edge( next, StateKey { o0, o1 }, prefix, transitions );

						apply_row_step( m1, state.v0, state.v1, o0, o1 );
						const std::uint64_t prefix1 = prefix | ( std::uint64_t( 1 ) << bit );
						relax_edge( next, StateKey { o0, o1 }, prefix1, transitions );
					}
					current.swap( next );
				}

				ExactAbsWeight best_abs {};
				std::uint64_t  best_mask = std::numeric_limits<std::uint64_t>::max();
				for ( const auto& [ state, prefix ] : current )
				{
					const ExactAbsWeight abs_weight = exact_abs_sum_v0_v1( state.v0, state.v1 );
					if ( exact_abs_weight_less( best_abs, abs_weight ) || ( exact_abs_weight_equal( abs_weight, best_abs ) && prefix < best_mask ) )
					{
						best_abs = abs_weight;
						best_mask = prefix;
					}
				}
				return { best_mask, best_abs };
			}

			[[nodiscard]] static inline std::uint64_t normalize_add_constant_for_q2( VarConstQ2Operation operation, std::uint64_t constant, int n ) noexcept
			{
				const std::uint64_t m = mask_n( n );
				if ( operation == VarConstQ2Operation::modular_sub )
					return ( ( ~constant ) + 1ull ) & m;
				return constant & m;
			}

			[[nodiscard]] inline VarConstQ2BitModel build_q2_bit_model_from_request( const VarConstQ2MainlineRequest& request ) noexcept
			{
				const std::uint64_t m = mask_n( request.n );
				const std::uint64_t fixed_mask = request.fixed_mask & m;
				const std::uint64_t add_constant = normalize_add_constant_for_q2( request.operation, request.constant, request.n );
				return build_output_mask_q2_bit_model( fixed_mask, add_constant, request.n );
			}
		}  // namespace detail_varconst_carry_dp
	}  // namespace arx_operators
}  // namespace TwilightDream

namespace TwilightDream
{
	namespace arx_operators
	{
		namespace detail_varconst_carry_dp
		{
			[[nodiscard]] inline ProjectiveSegmentSummary build_exact_segment_summary_from_model( const VarConstQ2BitModel& model, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( stats != nullptr )
					*stats = {};

				std::vector<HomogeneousQ2ChoiceBlockDescriptor> descriptors {};
				descriptors.reserve( static_cast<std::size_t>( model.n ) );
				const std::size_t raw_block_count = static_cast<std::size_t>( model.n );

				for ( int bit = 0; bit < model.n; )
				{
					int end = bit + 1;
					while ( end < model.n && bit_matrix_equal( model.choose_mask0[ static_cast<std::size_t>( bit ) ], model.choose_mask0[ static_cast<std::size_t>( end ) ] ) && bit_matrix_equal( model.choose_mask1[ static_cast<std::size_t>( bit ) ], model.choose_mask1[ static_cast<std::size_t>( end ) ] ) )
					{
						++end;
					}

					descriptors.push_back( HomogeneousQ2ChoiceBlockDescriptor { model.choose_mask0[ static_cast<std::size_t>( bit ) ], model.choose_mask1[ static_cast<std::size_t>( bit ) ], end - bit } );
					bit = end;
				}

				ProjectiveSegmentSummaryStats segment_stats {};
				std::size_t					  power_runs = 0u;
				std::size_t					  power_collapsed_blocks = 0u;
				std::size_t					  leaf_classes = 0u;
				std::size_t					  unique_leaf_nodes = 0u;
				std::size_t					  unique_nodes = 0u;
				std::size_t					  reuse_hits = 0u;
				ProjectiveSegmentSummary	  summary = build_projective_segment_summary_from_powered_descriptor_chain( descriptors, []( const HomogeneousQ2ChoiceBlockDescriptor& descriptor ) noexcept { return build_homogeneous_q2_choice_block_summary( descriptor ); }, HomogeneousQ2ChoiceBlockDescriptorHash {}, &segment_stats, &power_runs, &power_collapsed_blocks, &leaf_classes, &unique_leaf_nodes, &unique_nodes, &reuse_hits );

				if ( stats != nullptr )
				{
					stats->segment_root_entries = segment_stats.root_entries;
					stats->segment_max_entries = segment_stats.max_segment_entries;
					stats->segment_combine_pairs = segment_stats.combine_pairs;
					stats->segment_raw_block_count = raw_block_count;
					stats->segment_powered_leaf_count = descriptors.size();
					stats->segment_power_runs = power_runs;
					stats->segment_power_collapsed_blocks = power_collapsed_blocks;
					stats->segment_descriptor_tree_leaf_classes = leaf_classes;
					stats->segment_descriptor_tree_unique_leaf_nodes = unique_leaf_nodes;
					stats->segment_descriptor_tree_unique_nodes = unique_nodes;
					stats->segment_descriptor_tree_reuse_hits = reuse_hits;
				}
				return summary;
			}

			[[nodiscard]] inline bool try_run_fixed_alpha_homogeneous_block_mainline( const VarConstQ2MainlineRequest& request, VarConstLayerDpSummary& out, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( request.direction != VarConstQ2Direction::fixed_alpha_to_beta )
					return false;
				if ( request.n <= 0 || request.n > 64 )
					return false;
				if ( stats != nullptr )
					*stats = {};

				const VarConstQ2BitModel	   model = build_q2_bit_model_from_request( request );
				const ProjectiveSegmentSummary summary = build_exact_segment_summary_from_model( model, stats );
				out = summarize_projective_segment_summary_best( summary );
				return true;
			}

			[[nodiscard]] inline bool try_run_projective_periodic_block_power_mainline( const VarConstQ2BitModel& model, VarConstLayerDpSummary& out, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( model.n <= 0 || model.n > 64 )
					return false;
				if ( stats != nullptr )
					*stats = {};

				const ProjectiveSegmentSummary summary = build_exact_segment_summary_from_model( model, stats );
				out = summarize_projective_segment_summary_best( summary );
				return true;
			}

			[[nodiscard]] inline VarConstLayerDpSummary run_q2_mainline_from_model( const VarConstQ2BitModel& model, VarConstQ2MainlineMethod method, VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				if ( stats != nullptr )
					*stats = {};

				VarConstLayerDpSummary out {};
				if ( method == VarConstQ2MainlineMethod::projective_support_summary && try_run_projective_periodic_block_power_mainline( model, out, stats ) )
				{
					return out;
				}
				return run_raw_carry_layer_dp_from_model( model );
			}
		}  // namespace detail_varconst_carry_dp
	}  // namespace arx_operators
}  // namespace TwilightDream

namespace fixed_alpha_v116_canonical
{
	namespace ao = TwilightDream::arx_operators;
	namespace dv = TwilightDream::arx_operators::detail_varconst_carry_dp;

	namespace detail
	{
		constexpr std::size_t kBeamEntries = 733u;
		constexpr std::size_t kAdaptiveExactSlackEntries = 64u;
		constexpr int kShortTailBlockSupportExactWidth = 64;
		constexpr std::size_t kShortTailBlockSupportMaxTailBlocks = 8u;
		constexpr std::size_t kShortTailBlockSupportMaxRootEntries = 64u;
		constexpr std::size_t kShortTailBlockSupportMaxSupportEntries = 16u;

		struct LaneArtifacts
		{
			dv::ProjectiveSegmentSummary summary {};
			ao::VarConstQ2MainlineResult result {};
			std::size_t peak_entries { 0u };
			std::size_t final_entries { 0u };
			std::size_t compress_steps { 0u };
			std::size_t precompress_total_entries { 0u };
		};

		struct CandidateEntry
		{
			dv::ProjectiveMatrixKey key {};
			dv::ProjectiveMatrixSummaryValue value {};
			dv::ExactAbsWeight total_abs {};
		};

		using SummaryTable = std::unordered_map<
			dv::ProjectiveMatrixKey,
			dv::ProjectiveMatrixSummaryValue,
			dv::ProjectiveMatrixKeyHash>;
		using SummaryTableIterator = SummaryTable::iterator;

		struct SummaryCandidateRef
		{
			SummaryTableIterator it {};
			dv::ExactAbsWeight total_abs {};
		};

		struct HomogeneousBlockChain
		{
			std::vector<dv::ProjectiveSegmentSummary> owned_summaries {};
			std::vector<const dv::ProjectiveSegmentSummary*> blocks {};
			std::vector<dv::HomogeneousQ2ChoiceBlockDescriptor> block_descriptors {};
		};

		struct ExactPreludeSeed
		{
			bool available { false };
			dv::ProjectiveSegmentSummary summary {};
			std::size_t absorbed_blocks { 0u };
			std::vector<const dv::ProjectiveSegmentSummary*> tail_blocks {};
		};

		struct ShortTailBlockSupportClosure
		{
			bool available { false };
			ao::VarConstQ2MainlineResult result {};
			std::size_t prefix_entries { 0u };
			std::size_t root_entries { 0u };
			dv::ProjectiveSupportSummaryStats support_stats {};
		};

		struct TailExactTheoremPathClosure
		{
			bool available { false };
			ao::VarConstQ2MainlineResult result {};
			std::size_t prefix_entries { 0u };
			std::size_t tail_leaf_classes { 0u };
			std::size_t tail_unique_leaf_nodes { 0u };
			std::size_t tail_unique_tree_nodes { 0u };
			std::size_t tail_reuse_hits { 0u };
			dv::ProjectiveSegmentSummaryStats segment_stats {};
		};

		[[nodiscard]] inline dv::ExactAbsWeight compute_projective_entry_total_abs(
			const dv::ProjectiveMatrixKey& key,
			const dv::ProjectiveMatrixSummaryValue& value ) noexcept
		{
			const dv::ExactAbsWeight unit_abs =
				dv::exact_abs_sum_v0_v1( key.m00, key.m01 );
			return dv::scale_exact_abs_weight_projective( unit_abs, value.scale );
		}

		[[nodiscard]] inline CandidateEntry make_candidate_entry(
			const dv::ProjectiveMatrixKey& key,
			const dv::ProjectiveMatrixSummaryValue& value ) noexcept
		{
			return CandidateEntry {
				key,
				value,
				compute_projective_entry_total_abs( key, value ) };
		}

		[[nodiscard]] inline bool projective_matrix_key_lex_less(
			const dv::ProjectiveMatrixKey& lhs,
			const dv::ProjectiveMatrixKey& rhs ) noexcept
		{
			if ( lhs.m00 != rhs.m00 )
				return lhs.m00 < rhs.m00;
			if ( lhs.m01 != rhs.m01 )
				return lhs.m01 < rhs.m01;
			if ( lhs.m10 != rhs.m10 )
				return lhs.m10 < rhs.m10;
			return lhs.m11 < rhs.m11;
		}

		[[nodiscard]] inline bool candidate_better(
			const CandidateEntry& lhs,
			const CandidateEntry& rhs ) noexcept
		{
			if ( dv::exact_abs_weight_less( lhs.total_abs, rhs.total_abs ) )
				return false;
			if ( dv::exact_abs_weight_less( rhs.total_abs, lhs.total_abs ) )
				return true;
			if ( lhs.value.mask != rhs.value.mask )
				return lhs.value.mask < rhs.value.mask;
			return projective_matrix_key_lex_less( lhs.key, rhs.key );
		}

		[[nodiscard]] inline bool summary_candidate_better(
			const SummaryCandidateRef& lhs,
			const SummaryCandidateRef& rhs ) noexcept
		{
			if ( dv::exact_abs_weight_less( lhs.total_abs, rhs.total_abs ) )
				return false;
			if ( dv::exact_abs_weight_less( rhs.total_abs, lhs.total_abs ) )
				return true;
			if ( lhs.it->second.mask != rhs.it->second.mask )
				return lhs.it->second.mask < rhs.it->second.mask;
			return projective_matrix_key_lex_less( lhs.it->first, rhs.it->first );
		}

		[[nodiscard]] inline std::vector<SummaryCandidateRef> take_top_candidates_from_summary(
			dv::ProjectiveSegmentSummary& summary,
			std::size_t limit ) noexcept
		{
			if ( limit == 0u )
				return {};

			std::vector<SummaryCandidateRef> selected {};
			selected.reserve( ( std::min )( limit, summary.table.size() ) );
			bool heap_initialized = false;
			const auto better =
				[]( const SummaryCandidateRef& lhs, const SummaryCandidateRef& rhs ) noexcept
				{
					return summary_candidate_better( lhs, rhs );
				};

			for ( auto it = summary.table.begin(); it != summary.table.end(); ++it )
			{
				SummaryCandidateRef candidate {
					it,
					compute_projective_entry_total_abs( it->first, it->second ) };
				if ( selected.size() < limit )
				{
					selected.push_back( std::move( candidate ) );
					continue;
				}
				if ( !heap_initialized )
				{
					std::make_heap( selected.begin(), selected.end(), better );
					heap_initialized = true;
				}
				if ( !summary_candidate_better( candidate, selected.front() ) )
					continue;
				std::pop_heap( selected.begin(), selected.end(), better );
				selected.back() = std::move( candidate );
				std::push_heap( selected.begin(), selected.end(), better );
			}
			return selected;
		}

		[[nodiscard]] inline dv::ProjectiveSegmentSummary compress_summary_by_global_elite(
			dv::ProjectiveSegmentSummary summary,
			std::size_t global_elite_entries,
			std::size_t exact_passthrough_limit = kBeamEntries + kAdaptiveExactSlackEntries ) noexcept
		{
			if ( summary.table.size() <= exact_passthrough_limit )
				return summary;

			const std::size_t retained_entries =
				( std::min )( global_elite_entries, kBeamEntries );
			std::vector<SummaryCandidateRef> selected =
				take_top_candidates_from_summary( summary, retained_entries );

			dv::ProjectiveSegmentSummary out {};
			out.bit_length = summary.bit_length;
			out.table.reserve( selected.size() + 8u );
			for ( const auto& candidate : selected )
				out.table.insert( summary.table.extract( candidate.it ) );
			return out;
		}

		[[nodiscard]] inline dv::ProjectiveSegmentSummary compress_summary_by_beam_cap(
			dv::ProjectiveSegmentSummary summary,
			std::size_t exact_passthrough_limit = kBeamEntries + kAdaptiveExactSlackEntries ) noexcept
		{
			return compress_summary_by_global_elite(
				std::move( summary ),
				kBeamEntries,
				exact_passthrough_limit );
		}

		[[nodiscard]] inline dv::ProjectiveSegmentSummary compress_summary_by_forced_topk_kernel(
			dv::ProjectiveSegmentSummary summary ) noexcept
		{
			if ( summary.table.size() <= kBeamEntries )
				return summary;
			return compress_summary_by_beam_cap( std::move( summary ), 0u );
		}

		[[nodiscard]] inline HomogeneousBlockChain build_homogeneous_block_chain(
			const dv::VarConstQ2BitModel& model,
			ao::VarConstQ2MainlineStats& stats ) noexcept
		{
			HomogeneousBlockChain out {};
			out.owned_summaries.reserve( static_cast<std::size_t>( model.n ) );
			out.blocks.reserve( static_cast<std::size_t>( model.n ) );
			out.block_descriptors.reserve( static_cast<std::size_t>( model.n ) );
			std::unordered_map<
				dv::HomogeneousQ2ChoiceBlockDescriptor,
				std::size_t,
				dv::HomogeneousQ2ChoiceBlockDescriptorHash>
				summary_cache {};
			summary_cache.reserve( static_cast<std::size_t>( model.n ) );

			for ( int bit = 0; bit < model.n; )
			{
				int end = bit + 1;
				while ( end < model.n &&
						dv::bit_matrix_equal(
							model.choose_mask0[ static_cast<std::size_t>( bit ) ],
							model.choose_mask0[ static_cast<std::size_t>( end ) ] ) &&
						dv::bit_matrix_equal(
							model.choose_mask1[ static_cast<std::size_t>( bit ) ],
							model.choose_mask1[ static_cast<std::size_t>( end ) ] ) )
				{
					++end;
				}

				const int len = end - bit;
				const dv::HomogeneousQ2ChoiceBlockDescriptor descriptor {
					model.choose_mask0[ static_cast<std::size_t>( bit ) ],
					model.choose_mask1[ static_cast<std::size_t>( bit ) ],
					len };
				const auto cached = summary_cache.find( descriptor );
				if ( cached == summary_cache.end() )
				{
					const std::size_t index = out.owned_summaries.size();
					out.owned_summaries.push_back(
						dv::build_homogeneous_q2_choice_block_summary( descriptor ) );
					summary_cache.emplace( descriptor, index );
					out.blocks.push_back( &out.owned_summaries.back() );
				}
				else
				{
					out.blocks.push_back( &out.owned_summaries[ cached->second ] );
				}
				out.block_descriptors.push_back( descriptor );
				if ( len > 1 )
				{
					++stats.segment_power_runs;
					stats.segment_power_collapsed_blocks += static_cast<std::size_t>( len - 1 );
				}
				bit = end;
			}

			stats.segment_raw_block_count = static_cast<std::size_t>( model.n );
			stats.segment_powered_leaf_count = out.blocks.size();
			return out;
		}

		[[nodiscard]] inline ExactPreludeSeed build_exact_prefix_prelude_seed(
			const std::vector<const dv::ProjectiveSegmentSummary*>& blocks,
			std::size_t* combine_pairs_accumulator = nullptr ) noexcept
		{
			ExactPreludeSeed out {};
			out.tail_blocks = blocks;
			if ( blocks.empty() )
				return out;

			dv::ProjectiveSegmentSummary current = *blocks.front();
			std::size_t absorbed = 1u;
			for ( ; absorbed < blocks.size(); ++absorbed )
			{
				dv::ProjectiveSegmentSummaryStats combine_stats {};
				dv::ProjectiveSegmentSummary combined =
					dv::combine_projective_segment_summaries(
						current,
						*blocks[ absorbed ],
						&combine_stats );
				if ( combined.table.size() > kBeamEntries )
					break;
				if ( combine_pairs_accumulator != nullptr )
					*combine_pairs_accumulator += combine_stats.combine_pairs;
				current = std::move( combined );
			}

			out.available = true;
			out.summary = std::move( current );
			out.absorbed_blocks = absorbed;
			out.tail_blocks.assign(
				blocks.begin() + static_cast<std::ptrdiff_t>( absorbed ),
				blocks.end() );
			return out;
		}

		[[nodiscard]] inline dv::ProjectiveSupportSummary make_identity_projective_support_summary() noexcept
		{
			dv::ProjectiveSupportSummary out {};
			out.bit_length = 0;
			out.table.reserve( 1u );
			dv::relax_projective_support_entry(
				out,
				dv::ProjectiveSupportVectorKey {
					dv::carry_wide_from_i64( 1 ),
					dv::carry_wide_from_i64( 1 ) },
				1,
				0ull );
			return out;
		}

		[[nodiscard]] inline dv::ProjectiveSupportSummary append_projective_support_summary_with_segment_summary(
			const dv::ProjectiveSegmentSummary& left,
			const dv::ProjectiveSupportSummary& right,
			dv::ProjectiveSupportSummaryStats* stats = nullptr ) noexcept
		{
			dv::ProjectiveSupportSummary out {};
			out.bit_length = left.bit_length + right.bit_length;
			out.table.reserve( left.table.size() * right.table.size() + 8u );

			for ( const auto& [ lkey, lval ] : left.table )
			{
				const dv::ExactWideMatrix left_matrix =
					dv::projective_matrix_key_to_exact_wide( lkey );
				for ( const auto& [ rkey, rval ] : right.table )
				{
					const dv::CarryWide ox =
						left_matrix.m00 * rkey.x +
						left_matrix.m01 * rkey.y;
					const dv::CarryWide oy =
						left_matrix.m10 * rkey.x +
						left_matrix.m11 * rkey.y;
					const auto [ out_key, out_scale ] =
						dv::normalize_projective_support_vector( ox, oy );
					const std::uint64_t shifted_right =
						( left.bit_length >= 64 ) ? 0ull : ( rval.mask << left.bit_length );
					dv::relax_projective_support_entry(
						out,
						out_key,
						lval.scale * rval.scale * out_scale,
						lval.mask | shifted_right );
					if ( stats != nullptr )
						++stats->frontier_transitions;
				}
			}

			if ( stats != nullptr )
				stats->frontier_peak_entries =
					( std::max )( stats->frontier_peak_entries, out.table.size() );
			return out;
		}

		[[nodiscard]] inline dv::ProjectiveSupportSummary build_exact_support_summary_from_tail_blocks(
			const std::vector<const dv::ProjectiveSegmentSummary*>& blocks,
			dv::ProjectiveSupportSummaryStats* stats = nullptr ) noexcept
		{
			dv::ProjectiveSupportSummary out =
				make_identity_projective_support_summary();
			dv::ProjectiveSupportSummaryStats local_stats {};
			local_stats.frontier_peak_entries = out.table.size();

			for ( auto it = blocks.rbegin(); it != blocks.rend(); ++it )
			{
				out =
					append_projective_support_summary_with_segment_summary(
						**it,
						out,
						&local_stats );
			}

			local_stats.root_entries = out.table.size();
			local_stats.max_entries = out.table.size();
			if ( stats != nullptr )
				*stats = local_stats;
			return out;
		}

		[[nodiscard]] inline ShortTailBlockSupportClosure try_run_short_tail_exact_block_support_closure(
			const ao::VarConstQ2MainlineRequest& request,
			const ExactPreludeSeed& prelude ) noexcept
		{
			ShortTailBlockSupportClosure out {};
			out.prefix_entries = prelude.summary.table.size();
			if ( request.n != kShortTailBlockSupportExactWidth ||
				 !prelude.available ||
				 prelude.tail_blocks.empty() ||
				 prelude.tail_blocks.size() > kShortTailBlockSupportMaxTailBlocks )
			{
				return out;
			}

			const dv::ProjectiveRootQuerySummary root =
				dv::build_projective_root_query_summary_from_segment_summary(
					prelude.summary );
			out.root_entries = root.table.size();
			if ( out.root_entries == 0u ||
				 out.root_entries > kShortTailBlockSupportMaxRootEntries )
			{
				return out;
			}

			const dv::ProjectiveSupportSummary support =
				build_exact_support_summary_from_tail_blocks(
					prelude.tail_blocks,
					&out.support_stats );
			if ( out.support_stats.root_entries == 0u ||
				 out.support_stats.root_entries > kShortTailBlockSupportMaxSupportEntries )
			{
				return out;
			}

			const dv::VarConstLayerDpSummary exact =
				dv::solve_projective_root_query_summary_against_support_summary(
					root,
					support );
			out.result =
				dv::layer_dp_summary_to_q2_mainline(
					exact,
					request.n );
			out.available = true;
			return out;
		}

		[[nodiscard]] inline TailExactTheoremPathClosure try_run_tail_exact_theorem_path_closure(
			const ao::VarConstQ2MainlineRequest& request,
			const ExactPreludeSeed& prelude,
			const std::vector<dv::HomogeneousQ2ChoiceBlockDescriptor>& block_descriptors ) noexcept
		{
			TailExactTheoremPathClosure out {};
			out.prefix_entries = prelude.summary.table.size();
			if ( request.n != kShortTailBlockSupportExactWidth ||
				 !prelude.available ||
				 prelude.tail_blocks.size() <= kShortTailBlockSupportMaxTailBlocks ||
				 prelude.absorbed_blocks >= block_descriptors.size() )
			{
				return out;
			}

			const std::size_t tail_block_count =
				block_descriptors.size() - prelude.absorbed_blocks;
			if ( tail_block_count != prelude.tail_blocks.size() ||
				 tail_block_count > 24u )
			{
				return out;
			}

			std::vector<dv::HomogeneousQ2ChoiceBlockDescriptor> tail_descriptors {};
			tail_descriptors.reserve( tail_block_count );
			for ( std::size_t i = prelude.absorbed_blocks; i < block_descriptors.size(); ++i )
				tail_descriptors.push_back( block_descriptors[ i ] );

			std::unordered_set<
				dv::HomogeneousQ2ChoiceBlockDescriptor,
				dv::HomogeneousQ2ChoiceBlockDescriptorHash>
				descriptor_classes {};
			descriptor_classes.reserve( tail_descriptors.size() );
			for ( const auto& descriptor : tail_descriptors )
				descriptor_classes.insert( descriptor );
			if ( descriptor_classes.size() > 4u )
				return out;

			const dv::ProjectiveSegmentSummary tail_summary =
				dv::build_projective_segment_summary_from_powered_descriptor_chain(
					tail_descriptors,
					[]( const dv::HomogeneousQ2ChoiceBlockDescriptor& desc ) noexcept
					{
						return dv::build_homogeneous_q2_choice_block_summary( desc );
					},
					dv::HomogeneousQ2ChoiceBlockDescriptorHash {},
					&out.segment_stats,
					nullptr,
					nullptr,
					&out.tail_leaf_classes,
					&out.tail_unique_leaf_nodes,
					&out.tail_unique_tree_nodes,
					&out.tail_reuse_hits );
			if ( out.segment_stats.root_entries == 0u ||
				 out.segment_stats.root_entries > 256u )
			{
				return out;
			}

			dv::ProjectiveSegmentSummaryStats combine_stats {};
			const dv::ProjectiveSegmentSummary exact_summary =
				dv::combine_projective_segment_summaries(
					prelude.summary,
					tail_summary,
					&combine_stats );
			out.segment_stats.combine_pairs += combine_stats.combine_pairs;
			out.segment_stats.root_entries = exact_summary.table.size();
			out.segment_stats.max_segment_entries =
				( std::max )( out.segment_stats.max_segment_entries, exact_summary.table.size() );
			if ( exact_summary.table.size() > 512u )
				return out;

			out.result =
				dv::layer_dp_summary_to_q2_mainline(
					dv::summarize_projective_segment_summary_best( exact_summary ),
					request.n );
			out.available = true;
			return out;
		}

		[[nodiscard]] inline LaneArtifacts run_forced_beam_cap_lane(
			dv::ProjectiveSegmentSummary seed,
			const std::vector<const dv::ProjectiveSegmentSummary*>& blocks,
			std::size_t* combine_pairs_accumulator = nullptr ) noexcept
		{
			LaneArtifacts out {};
			out.summary = std::move( seed );
			out.peak_entries = out.summary.table.size();
			out.final_entries = out.summary.table.size();
			out.precompress_total_entries = out.summary.table.size();

			for ( const dv::ProjectiveSegmentSummary* block_ptr : blocks )
			{
				const auto& block = *block_ptr;
				dv::ProjectiveSegmentSummaryStats combine_stats {};
				out.summary =
					dv::combine_projective_segment_summaries(
						out.summary,
						block,
						&combine_stats );
				if ( combine_pairs_accumulator != nullptr )
					*combine_pairs_accumulator += combine_stats.combine_pairs;
				out.peak_entries = ( std::max )( out.peak_entries, out.summary.table.size() );
				out.precompress_total_entries += out.summary.table.size();
				out.summary =
					compress_summary_by_forced_topk_kernel( std::move( out.summary ) );
				++out.compress_steps;
				out.peak_entries = ( std::max )( out.peak_entries, out.summary.table.size() );
			}

			out.final_entries = out.summary.table.size();
			return out;
		}

		inline void assign_rescue_only_topk_stats(
			ao::VarConstQ2MainlineStats& stats,
			const LaneArtifacts& lane,
			std::size_t transitions ) noexcept
		{
			stats.memo_states = 0u;
			stats.queue_pops = 1u;
			stats.queue_pushes = 1u;
			stats.segment_descriptor_tree_leaf_classes = 0u;
			stats.segment_descriptor_tree_unique_leaf_nodes = lane.compress_steps;
			stats.segment_descriptor_tree_unique_nodes = lane.compress_steps;
			stats.segment_descriptor_tree_reuse_hits = lane.final_entries;
			stats.segment_root_entries = lane.final_entries;
			stats.segment_max_entries = lane.peak_entries;
			stats.support_root_entries = 0u;
			stats.support_max_entries = 0u;
			stats.support_frontier_peak_entries = lane.peak_entries;
			stats.support_frontier_transitions = transitions;
			stats.recursive_total_frontier_entries = 0u;
			stats.recursive_max_frontier_entries = lane.precompress_total_entries;
		}

		inline void assign_short_tail_block_support_stats(
			ao::VarConstQ2MainlineStats& stats,
			const ShortTailBlockSupportClosure& closure ) noexcept
		{
			stats.memo_states = 0u;
			stats.queue_pops = 1u;
			stats.queue_pushes = 1u;
			stats.segment_descriptor_tree_leaf_classes = 0u;
			stats.segment_descriptor_tree_unique_leaf_nodes = 0u;
			stats.segment_descriptor_tree_unique_nodes = 1u;
			stats.segment_descriptor_tree_reuse_hits = closure.root_entries;
			stats.segment_root_entries = closure.root_entries;
			stats.segment_max_entries = closure.prefix_entries;
			stats.support_root_entries = closure.support_stats.root_entries;
			stats.support_max_entries = closure.support_stats.max_entries;
			stats.support_frontier_peak_entries = closure.support_stats.frontier_peak_entries;
			stats.support_frontier_transitions = closure.support_stats.frontier_transitions;
			stats.recursive_total_frontier_entries = 0u;
			stats.recursive_max_frontier_entries =
				closure.root_entries + closure.support_stats.frontier_peak_entries;
		}

		inline void assign_tail_exact_theorem_path_stats(
			ao::VarConstQ2MainlineStats& stats,
			const TailExactTheoremPathClosure& closure,
			std::size_t raw_tail_block_count ) noexcept
		{
			stats.memo_states = 0u;
			stats.queue_pops = 1u;
			stats.queue_pushes = 1u;
			stats.segment_root_entries = closure.segment_stats.root_entries;
			stats.segment_max_entries = closure.segment_stats.max_segment_entries;
			stats.segment_combine_pairs = closure.segment_stats.combine_pairs;
			stats.segment_raw_block_count = raw_tail_block_count;
			stats.segment_powered_leaf_count = raw_tail_block_count;
			stats.segment_power_runs = 0u;
			stats.segment_power_collapsed_blocks = 0u;
			stats.segment_descriptor_tree_leaf_classes = closure.tail_leaf_classes;
			stats.segment_descriptor_tree_unique_leaf_nodes = closure.tail_unique_leaf_nodes;
			stats.segment_descriptor_tree_unique_nodes = closure.tail_unique_tree_nodes;
			stats.segment_descriptor_tree_reuse_hits = closure.tail_reuse_hits;
			stats.support_root_entries = closure.segment_stats.root_entries;
			stats.support_max_entries = closure.segment_stats.max_segment_entries;
			stats.support_frontier_peak_entries = raw_tail_block_count;
			stats.support_frontier_transitions = 0u;
			stats.recursive_total_frontier_entries = 0u;
			stats.recursive_max_frontier_entries = closure.prefix_entries;
		}

		[[nodiscard]] inline ao::VarConstQ2MainlineResult run_fixed_alpha_q2_rescue_only_forced_topk_kernel(
			const ao::VarConstQ2MainlineRequest& request,
			ao::VarConstQ2MainlineStats* stats ) noexcept
		{
			ao::VarConstQ2MainlineStats local_stats {};

			const dv::VarConstQ2BitModel model =
				dv::build_q2_bit_model_from_request( request );
			HomogeneousBlockChain blocks =
				build_homogeneous_block_chain( model, local_stats );
			ExactPreludeSeed prelude =
				build_exact_prefix_prelude_seed(
					blocks.blocks,
					&local_stats.segment_combine_pairs );

			const ShortTailBlockSupportClosure short_tail_block_support =
				try_run_short_tail_exact_block_support_closure(
					request,
					prelude );
			if ( short_tail_block_support.available )
			{
				assign_short_tail_block_support_stats(
					local_stats,
					short_tail_block_support );
				if ( stats != nullptr )
					*stats = local_stats;
				return short_tail_block_support.result;
			}

			const TailExactTheoremPathClosure tail_exact_theorem_path =
				try_run_tail_exact_theorem_path_closure(
					request,
					prelude,
					blocks.block_descriptors );
			if ( tail_exact_theorem_path.available )
			{
				assign_tail_exact_theorem_path_stats(
					local_stats,
					tail_exact_theorem_path,
					prelude.tail_blocks.size() );
				if ( stats != nullptr )
					*stats = local_stats;
				return tail_exact_theorem_path.result;
			}

			LaneArtifacts pre_lane =
				run_forced_beam_cap_lane(
					prelude.summary,
					prelude.tail_blocks,
					&local_stats.segment_combine_pairs );
			pre_lane.result =
				dv::layer_dp_summary_to_q2_mainline(
					dv::summarize_projective_segment_summary_best( pre_lane.summary ),
					request.n );

			assign_rescue_only_topk_stats(
				local_stats,
				pre_lane,
				prelude.available ? ( prelude.tail_blocks.size() + 1u ) : blocks.blocks.size() );
			if ( stats != nullptr )
				*stats = local_stats;
			return pre_lane.result;
		}
	}  // namespace detail

	[[nodiscard]] inline bool run_fixed_alpha_q2_canonical_algorithm(
		const ao::VarConstQ2MainlineRequest& request,
		ao::VarConstQ2MainlineResult& out,
		ao::VarConstQ2MainlineStats* stats = nullptr ) noexcept
	{
		if ( request.direction != ao::VarConstQ2Direction::fixed_alpha_to_beta ||
			 request.n != 64 )
			return false;
		out = detail::run_fixed_alpha_q2_rescue_only_forced_topk_kernel( request, stats );
		return true;
	}
}  // namespace fixed_alpha_v116_canonical

namespace TwilightDream
{
	namespace arx_operators
	{
		namespace detail_varconst_carry_dp
		{
			[[nodiscard]] inline bool try_run_support_summary_request_fastpath(
				const VarConstQ2MainlineRequest& request,
				VarConstQ2MainlineResult& out,
				VarConstQ2MainlineStats* stats = nullptr ) noexcept
			{
				return fixed_alpha_v116_canonical::run_fixed_alpha_q2_canonical_algorithm(
					request,
					out,
					stats );
			}

			[[nodiscard]] static VarConstOptimalOutputMaskResult solve_fixed_alpha_public_root(
				std::uint64_t alpha,
				std::uint64_t constant,
				int n,
				VarConstQ2Operation operation ) noexcept
			{
				if ( n <= 0 || n > 64 )
					return {};

				const std::uint64_t mask = mask_n( n );
				const VarConstQ2MainlineRequest request {
					alpha & mask,
					constant & mask,
					n,
					VarConstQ2Direction::fixed_alpha_to_beta,
					operation };

				VarConstQ2MainlineResult result {};
				if ( !try_run_support_summary_request_fastpath(
						 request,
						 result,
						 nullptr ) )
				{
					const VarConstQ2BitModel model =
						build_q2_bit_model_from_request( request );
					const VarConstLayerDpSummary summary =
						run_q2_mainline_from_model(
							model,
							VarConstQ2MainlineMethod::projective_support_summary,
							nullptr );
					result = layer_dp_summary_to_q2_mainline( summary, request.n );
				}
				return q2_mainline_to_beta_result( result );
			}
		}  // namespace detail_varconst_carry_dp

		VarConstQ2MainlineResult solve_fixed_alpha_q2_exact_transition_reference(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineStats* stats ) noexcept
		{
			if ( request.direction != VarConstQ2Direction::fixed_alpha_to_beta ||
				 request.n <= 0 || request.n > 64 )
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
					VarConstQ2MainlineMethod::projective_support_summary,
					stats );
			return detail_varconst_carry_dp::layer_dp_summary_to_q2_mainline( summary, request.n );
		}

		VarConstQ2MainlineResult solve_fixed_alpha_q2_raw_reference(
			const VarConstQ2MainlineRequest& request ) noexcept
		{
			if ( request.direction != VarConstQ2Direction::fixed_alpha_to_beta ||
				 request.n <= 0 || request.n > 64 )
				return {};

			const detail_varconst_carry_dp::VarConstQ2BitModel model =
				detail_varconst_carry_dp::build_q2_bit_model_from_request( request );
			return detail_varconst_carry_dp::layer_dp_summary_to_q2_mainline(
				detail_varconst_carry_dp::run_raw_carry_layer_dp_from_model( model ),
				request.n );
		}

		VarConstOptimalOutputMaskResult find_optimal_beta_varconst_mod_sub(
			std::uint64_t alpha,
			std::uint64_t sub_constant_C,
			int n ) noexcept
		{
			return detail_varconst_carry_dp::solve_fixed_alpha_public_root(
				alpha,
				sub_constant_C,
				n,
				VarConstQ2Operation::modular_sub );
		}

		VarConstOptimalOutputMaskResult find_optimal_beta_varconst_mod_add(
			std::uint64_t alpha,
			std::uint64_t add_constant_K,
			int n ) noexcept
		{
			return detail_varconst_carry_dp::solve_fixed_alpha_public_root(
				alpha,
				add_constant_K,
				n,
				VarConstQ2Operation::modular_add );
		}

		VarConstQ2MainlineResult solve_fixed_alpha_q2_canonical(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineStats* stats ) noexcept
		{
			VarConstQ2MainlineResult result {};
			if ( fixed_alpha_v116_canonical::run_fixed_alpha_q2_canonical_algorithm(
					 request,
					 result,
					 stats ) )
			{
				return result;
			}
			return solve_fixed_alpha_q2_exact_transition_reference( request, stats );
		}
	}  // namespace arx_operators
}  // namespace TwilightDream
