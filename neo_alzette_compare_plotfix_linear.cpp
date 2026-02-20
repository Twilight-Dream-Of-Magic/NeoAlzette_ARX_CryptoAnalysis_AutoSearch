#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

// 此文件用于NeoAlzette分析版本对决Alzette分析版本
// 清晰的数学对象以及线性轨迹呈现
// 禁止包含任何本工程分析ARX算子以及私有头文件导致无法正确编译
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 rotl32( u32 x, unsigned r ) noexcept
{
	return std::rotl( x, static_cast<int>( r & 31u ) );
}
constexpr u32 rotr32( u32 x, unsigned r ) noexcept
{
	return std::rotr( x, static_cast<int>( r & 31u ) );
}
constexpr int CROSS_XOR_ROT_R0 = 22;
constexpr int CROSS_XOR_ROT_R1 = 13;

constexpr std::array<u32, 16> RC = {
	0x16B2C40Bu, 0xC117176Au, 0x0F9A2598u, 0xA1563ACAu, 0x243F6A88u, 0x85A308D3u, 0x13198102u, 0xE0370734u, 0x9E3779B9u, 0x7F4A7C15u, 0xF39CC060u, 0x5CEDC834u, 0xB7E15162u, 0x8AED2A6Au, 0xBF715880u, 0x9CF4F3C7u,
};

static inline bool runtime_time_limit_reached() noexcept
{
	return false;
}
static inline bool physical_memory_allocation_guard_active() noexcept
{
	return false;
}

static inline unsigned parity32( u32 x ) noexcept
{
	return static_cast<unsigned>( std::popcount( x ) & 1u );
}


// -------------------------------------------------------------------------
// `modular_addition_ccz.hpp` (Schulte–Geers φ₂) inlined here.
// Symbol names match `TwilightDream::arx_operators` (mask_n, m_of, find_optimal_output_u_ccz, …)
// so diffing against the header is straightforward; this TU still avoids #include.
// -------------------------------------------------------------------------

[[nodiscard]] static inline constexpr u64 mask_n( int n ) noexcept
{
	if ( n <= 0 )
		return 0ull;
	if ( n >= 64 )
		return ~0ull;
	return ( 1ull << static_cast<unsigned>( n ) ) - 1ull;
}

[[nodiscard]] static inline unsigned popcount_u64( u64 x ) noexcept
{
	return static_cast<unsigned>( std::popcount( x ) );
}

[[nodiscard]] static inline unsigned parity_u64( u64 x ) noexcept
{
	return popcount_u64( x ) & 1u;
}

[[nodiscard]] static inline bool leq_bitwise( u64 x, u64 z, int n ) noexcept
{
	const u64 mask = mask_n( n );
	x &= mask;
	z &= mask;
	return ( x & ~z ) == 0ull;
}

[[nodiscard]] static inline unsigned dot_parity( u64 u, u64 v, int n ) noexcept
{
	const u64 mask = mask_n( n );
	return parity_u64( ( u & v ) & mask );
}

[[nodiscard]] static inline u64 suffix_xor_down_n( u64 s, int n ) noexcept
{
	const u64 mask = mask_n( n );
	s &= mask;
	for ( unsigned d = 1u; d < static_cast<unsigned>( n ); d <<= 1u )
		s ^= ( s >> d );
	return s & mask;
}

[[nodiscard]] static inline u64 MnT_of( u64 s, int n ) noexcept
{
	const u64 mask = mask_n( n );
	s &= mask;
	if ( n <= 0 )
		return 0ull;
	return ( suffix_xor_down_n( s, n ) >> 1 ) & mask;
}

[[nodiscard]] static inline std::optional<int> linear_correlation_add_ccz_weight( u64 u, u64 v, u64 w, int n ) noexcept
{
	const u64 mask = mask_n( n );
	u &= mask;
	v &= mask;
	w &= mask;
	const u64 z = MnT_of( u ^ v ^ w, n );
	const u64 uv = ( u ^ v ) & mask;
	const u64 uw = ( u ^ w ) & mask;
	if ( !leq_bitwise( uv, z, n ) || !leq_bitwise( uw, z, n ) )
		return std::nullopt;
	return static_cast<int>( popcount_u64( z ) );
}

[[nodiscard]] static inline u64 aL_of( u64 u, int n ) noexcept
{
	const u64 mask = mask_n( n );
	u &= mask;
	const int nlim = std::min( n, 64 );
	int		  zpos[ 64 ];
	for ( int i = 0; i < 64; ++i )
	{
		if ( i < nlim )
			zpos[ i ] = ( ( u >> i ) & 1ull ) == 0ull ? i : n;
		else
			zpos[ i ] = n;
	}
	for ( unsigned sh = 1u; sh < 64u; sh <<= 1u )
	{
		if ( sh >= static_cast<unsigned>( nlim ) )
			break;
		for ( int i = 0; i < 64; ++i )
		{
			if ( i + static_cast<int>( sh ) < nlim )
				zpos[ i ] = std::min( zpos[ i ], zpos[ i + static_cast<int>( sh ) ] );
		}
	}
	u64 acc = 0ull;
	for ( int i = 0; i < 64; ++i )
	{
		if ( i >= nlim )
			break;
		if ( ( ( zpos[ i ] - i ) & 1 ) != 0 )
			acc |= ( 1ull << i );
	}
	return acc & mask;
}

[[nodiscard]] static inline u64 R_of( u64 x, int n ) noexcept
{
	return ( x << 1 ) & mask_n( n );
}

[[nodiscard]] static inline u64 propagate_ones_right_through_v_or_w_zero_mask( u64 a, u64 z_mask, int n ) noexcept
{
	const u64 mask = mask_n( n );
	a &= mask;
	u64 run = z_mask & mask;
	for ( unsigned sh = 1u; sh < static_cast<unsigned>( n ); sh <<= 1u )
	{
		a |= ( ( a & run ) << sh ) & mask;
		run &= run >> sh;
	}
	return a & mask;
}

[[nodiscard]] static inline u64 m_of_rule2_start_mask( u64 t_or, u64 t_and, int n ) noexcept
{
	const u64 mask = mask_n( n );
	t_or &= mask;
	t_and &= mask;
	if ( n <= 1 )
		return 0ull;
	u64 s = ( t_or & ~( t_or >> 1 ) & ~t_and ) & mask;
	s &= ~( 1ull << static_cast<unsigned>( n - 1 ) );
	return s & mask;
}

[[nodiscard]] static inline u64 m_of( u64 v, u64 w, int n ) noexcept
{
	const u64 mask = mask_n( n );
	v &= mask;
	w &= mask;
	const u64 t_or = ( v | w ) & mask;
	const u64 t_and = ( v & w ) & mask;
	const u64 z_mask = ( ~t_or ) & mask;
	const u64 rule2_seed = ( m_of_rule2_start_mask( t_or, t_and, n ) << 1u ) & mask;
	u64		  a = ( R_of( ( v ^ w ) & mask, n ) | rule2_seed ) & mask;
	a = propagate_ones_right_through_v_or_w_zero_mask( a, z_mask, n );
	return a & mask;
}

[[nodiscard]] static inline std::optional<u64> column_best_a_of( u64 v, u64 w, int n ) noexcept
{
	const u64 mask = mask_n( n );
	v &= mask;
	w &= mask;
	if ( n <= 0 )
		return std::nullopt;
	const unsigned msb = static_cast<unsigned>( n - 1 );
	if ( ( ( v >> msb ) & 1ull ) == 0ull || ( ( w >> msb ) & 1ull ) == 0ull )
		return std::nullopt;
	const u64 m = m_of( v, w, n );
	const u64 a = ( aL_of( ( v & w ) & mask, n ) | m ) & mask;
	return a;
}

[[nodiscard]] static inline std::optional<u64> column_best_u_of( u64 v, u64 w, int n ) noexcept
{
	const u64 mask = mask_n( n );
	v &= mask;
	w &= mask;
	const auto a_opt = column_best_a_of( v, w, n );
	if ( !a_opt.has_value() )
		return std::nullopt;
	const u64 a = a_opt.value() & mask;
	const u64 u = ( ( v ^ w ) ^ a ^ ( a >> 1 ) ) & mask;
	return u;
}

[[nodiscard]] static inline u64 find_optimal_output_u_ccz( u64 v, u64 w, int n ) noexcept
{
	const u64 mask = mask_n( n );
	v &= mask;
	w &= mask;
	const u64 t_or = ( v | w ) & mask;
	if ( t_or == 0ull )
		return 0ull;
	const int	   p = static_cast<int>( std::bit_width( t_or ) ) - 1;
	const unsigned pu = static_cast<unsigned>( p );
	const bool	   shared_at_p = ( ( v >> pu ) & 1ull ) != 0ull && ( ( w >> pu ) & 1ull ) != 0ull;
	if ( !shared_at_p )
		return 0ull;
	const int  reduced_n = p + 1;
	const u64  reduced_mask = mask_n( reduced_n );
	const auto u_opt = column_best_u_of( v & reduced_mask, w & reduced_mask, reduced_n );
	if ( !u_opt.has_value() )
		return 0ull;
	return u_opt.value() & mask;
}

struct AddForwardCandidate
{
	u32 output_mask_u {};
	int linear_weight {};
};

[[nodiscard]] static inline AddForwardCandidate exact_best_add_weight( u32 input_mask_x, u32 input_mask_y ) noexcept
{
	constexpr int n = 32;
	const u64	  mask = mask_n( n );
	const u64	  v = static_cast<u64>( input_mask_x ) & mask;
	const u64	  w = static_cast<u64>( input_mask_y ) & mask;
	const u64	  u = find_optimal_output_u_ccz( v, w, n );
	const auto	  wt = linear_correlation_add_ccz_weight( u, v, w, n );
	return AddForwardCandidate { static_cast<u32>( u & mask ), wt.has_value() ? wt.value() : 32 };
}

// -------------------------------------------------------------------------
// Exact var-var candidate generation copied from search framework core logic
// -------------------------------------------------------------------------

struct AddCandidate
{
	u32 input_mask_x {};
	u32 input_mask_y {};
	int linear_weight {};
};

static inline std::vector<AddCandidate> generate_add_candidates_for_fixed_u( u32 output_mask_u, int weight_cap, std::size_t max_candidates = 0, bool* out_hit_cap = nullptr )
{
	std::vector<AddCandidate> candidates;
	if ( out_hit_cap )
		*out_hit_cap = false;
	if ( weight_cap < 0 )
		return candidates;
	if ( weight_cap > 31 )
		weight_cap = 31;

	struct Frame
	{
		int bit_index = 0;
		u32 input_mask_x = 0;
		u32 input_mask_y = 0;
		int z_bit = 0;
		int z_weight_so_far = 0;
	};

	const int		   u31 = int( ( output_mask_u >> 31 ) & 1u );
	u32				   input_mask_x_prefix = u31 ? ( 1u << 31 ) : 0u;
	u32				   input_mask_y_prefix = input_mask_x_prefix;
	const int		   z30 = u31;
	std::vector<Frame> stack;
	bool			   stop_due_to_cap = false;
	bool			   stop_due_to_memory_guard = false;

	for ( int target_weight = 0; target_weight <= weight_cap; ++target_weight )
	{
		if ( stop_due_to_cap )
			return candidates;
		if ( stop_due_to_memory_guard )
		{
			candidates.clear();
			return candidates;
		}
		if ( runtime_time_limit_reached() )
		{
			candidates.clear();
			return candidates;
		}
		stack.clear();
		stack.push_back( Frame { 30, input_mask_x_prefix, input_mask_y_prefix, z30, 0 } );

		while ( !stack.empty() )
		{
			if ( stop_due_to_cap )
				return candidates;
			if ( stop_due_to_memory_guard )
			{
				candidates.clear();
				return candidates;
			}
			if ( runtime_time_limit_reached() )
			{
				candidates.clear();
				return candidates;
			}
			const Frame st = stack.back();
			stack.pop_back();

			const int bit_index = st.bit_index;
			const int z_i = st.z_bit & 1;
			const int z_weight = st.z_weight_so_far + z_i;
			if ( z_weight > target_weight )
				continue;
			const int remaining_z_bits = bit_index;
			if ( z_weight + remaining_z_bits < target_weight )
				continue;

			const int u_i = int( ( output_mask_u >> bit_index ) & 1u );

			auto push_next_state = [ & ]( int v_i, int w_i ) {
				u32 next_input_mask_x = st.input_mask_x | ( u32( v_i & 1 ) << bit_index );
				u32 next_input_mask_y = st.input_mask_y | ( u32( w_i & 1 ) << bit_index );
				if ( bit_index == 0 )
				{
					if ( z_weight == target_weight )
					{
						if ( physical_memory_allocation_guard_active() )
						{
							stop_due_to_memory_guard = true;
							return;
						}
						candidates.push_back( AddCandidate { next_input_mask_x, next_input_mask_y, z_weight } );
						if ( max_candidates != 0 && candidates.size() >= max_candidates )
						{
							if ( out_hit_cap )
								*out_hit_cap = true;
							stop_due_to_cap = true;
						}
					}
					return;
				}
				const int z_prev = z_i ^ u_i ^ v_i ^ w_i;
				stack.push_back( Frame { bit_index - 1, next_input_mask_x, next_input_mask_y, z_prev, z_weight } );
			};

			if ( z_i == 0 )
			{
				push_next_state( u_i, u_i );
				continue;
			}
			push_next_state( 0, 0 );
			push_next_state( 0, 1 );
			push_next_state( 1, 0 );
			push_next_state( 1, 1 );
		}
	}
	return candidates;
}

static inline AddCandidate exact_best_add_candidate_legacy_rowbest( u32 output_mask_u )
{
	// Legacy fixed-output row-best path (Theorem 6): `row_best_b_of` = (aL xor L(aL)); kept as in-file reference.
	// Primary Gap A path is fixed-input (v,w) -> u* via `find_optimal_output_u_ccz` (Theorem 7 column).
	constexpr int n = 32;
	const u64	  mask = mask_n( n );
	const u64	  u = static_cast<u64>( output_mask_u ) & mask;
	const u64	  a = aL_of( u, n );
	const u64	  v = ( a ^ ( a >> 1 ) ) & mask;
	const u64	  w = u;
	const auto	  wt = linear_correlation_add_ccz_weight( u, v, w, n );
	return AddCandidate { static_cast<u32>( v & mask ), static_cast<u32>( w & mask ), wt.has_value() ? wt.value() : 32 };
}

// -------------------------------------------------------------------------
// Exact var-const fixed-beta streaming generator copied from search framework
// -------------------------------------------------------------------------

static inline int carry_out_bit( int x, int y, int cin ) noexcept
{
	return ( x & y ) | ( x & cin ) | ( y & cin );
}

struct SubConstCandidate
{
	u32 input_mask_alpha {};
	int linear_weight {};
};

struct SubConstBitMatrix
{
	int			 m00 = 0, m01 = 0, m10 = 0, m11 = 0;
	std::uint8_t max_row_sum = 0;
};

struct SubConstStreamingCursorFrame
{
	int			 bit_index = 0;
	u32			 prefix = 0;
	std::int64_t v0 = 1;
	std::int64_t v1 = 0;
	std::uint8_t state = 0;
};

struct SubConstStreamingCursor
{
	bool										 initialized = false;
	bool										 stop_due_to_limits = false;
	int											 nbits = 32;
	int											 weight_cap = 32;
	u32											 output_mask_beta = 0;
	u32											 constant = 0;
	std::uint64_t								 min_abs = 1;
	std::array<SubConstBitMatrix, 32>			 mats_alpha0 {};
	std::array<SubConstBitMatrix, 32>			 mats_alpha1 {};
	std::array<std::uint64_t, 33>				 max_gain_suffix {};
	std::array<SubConstStreamingCursorFrame, 64> stack {};
	int											 stack_step = 0;
};

static inline std::uint64_t abs_i64_to_u64( std::int64_t v ) noexcept
{
	return ( v < 0 ) ? std::uint64_t( -v ) : std::uint64_t( v );
}

static inline int floor_log2_u64( std::uint64_t v ) noexcept
{
	return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
}

static inline void reset_subconst_streaming_cursor( SubConstStreamingCursor& cursor, u32 output_mask_beta, u32 constant, int weight_cap, int nbits = 32 )
{
	cursor = {};
	cursor.initialized = true;
	cursor.nbits = std::clamp( nbits, 1, 32 );
	cursor.weight_cap = std::clamp( weight_cap, 0, cursor.nbits );
	const u32 mask = ( cursor.nbits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << cursor.nbits ) - 1u );
	cursor.output_mask_beta = output_mask_beta & mask;
	cursor.constant = constant & mask;
	cursor.min_abs = ( cursor.weight_cap >= cursor.nbits ) ? 1ull : ( 1ull << ( cursor.nbits - cursor.weight_cap ) );

	auto build_matrix = [ & ]( int alpha_i, int constant_i, int beta_i ) -> SubConstBitMatrix {
		SubConstBitMatrix M {};
		for ( int cin = 0; cin <= 1; ++cin )
		{
			for ( int x = 0; x <= 1; ++x )
			{
				const int cout = carry_out_bit( x, constant_i, cin );
				const int zi = x ^ constant_i ^ cin;
				const int exponent = ( alpha_i & x ) ^ ( beta_i & zi );
				const int s = exponent ? -1 : 1;
				if ( cin == 0 && cout == 0 )
					M.m00 += s;
				else if ( cin == 0 && cout == 1 )
					M.m01 += s;
				else if ( cin == 1 && cout == 0 )
					M.m10 += s;
				else
					M.m11 += s;
			}
		}
		const int row0 = std::abs( M.m00 ) + std::abs( M.m01 );
		const int row1 = std::abs( M.m10 ) + std::abs( M.m11 );
		M.max_row_sum = static_cast<std::uint8_t>( std::max( row0, row1 ) );
		return M;
	};

	for ( int bit = 0; bit < cursor.nbits; ++bit )
	{
		const int constant_i = int( ( cursor.constant >> bit ) & 1u );
		const int beta_i = int( ( cursor.output_mask_beta >> bit ) & 1u );
		cursor.mats_alpha0[ std::size_t( bit ) ] = build_matrix( 0, constant_i, beta_i );
		cursor.mats_alpha1[ std::size_t( bit ) ] = build_matrix( 1, constant_i, beta_i );
		const std::uint8_t max_row_sum_bit = std::max( cursor.mats_alpha0[ std::size_t( bit ) ].max_row_sum, cursor.mats_alpha1[ std::size_t( bit ) ].max_row_sum );
		cursor.max_gain_suffix[ std::size_t( bit ) ] = max_row_sum_bit;
	}

	cursor.max_gain_suffix[ std::size_t( cursor.nbits ) ] = 1ull;
	for ( int bit = cursor.nbits - 1; bit >= 0; --bit )
	{
		cursor.max_gain_suffix[ std::size_t( bit ) ] = cursor.max_gain_suffix[ std::size_t( bit + 1 ) ] * cursor.max_gain_suffix[ std::size_t( bit ) ];
	}
	cursor.stack_step = 1;
	cursor.stack[ 0 ] = SubConstStreamingCursorFrame {};
}

static inline bool next_subconst_streaming_candidate( SubConstStreamingCursor& cursor, SubConstCandidate& out_candidate )
{
	if ( !cursor.initialized || cursor.stop_due_to_limits )
		return false;
	while ( cursor.stack_step > 0 )
	{
		if ( runtime_time_limit_reached() )
		{
			cursor.stop_due_to_limits = true;
			return false;
		}
		SubConstStreamingCursorFrame& frame = cursor.stack[ cursor.stack_step - 1 ];
		if ( frame.state == 0 )
		{
			const std::uint64_t abs_sum = abs_i64_to_u64( frame.v0 ) + abs_i64_to_u64( frame.v1 );
			const std::uint64_t ub = abs_sum * cursor.max_gain_suffix[ std::size_t( frame.bit_index ) ];
			if ( ub < cursor.min_abs )
			{
				--cursor.stack_step;
				continue;
			}
			if ( frame.bit_index >= cursor.nbits )
			{
				const std::int64_t W = frame.v0 + frame.v1;
				if ( W != 0 )
				{
					const std::uint64_t abs_w = abs_i64_to_u64( W );
					const int			msb = floor_log2_u64( abs_w );
					const int			weight = std::clamp( cursor.nbits - msb, 0, cursor.nbits );
					if ( weight <= cursor.weight_cap )
					{
						out_candidate = SubConstCandidate { frame.prefix, weight };
						--cursor.stack_step;
						return true;
					}
				}
				--cursor.stack_step;
				continue;
			}
			frame.state = 1;
			const auto&		   M = cursor.mats_alpha0[ std::size_t( frame.bit_index ) ];
			const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
			const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
			cursor.stack[ cursor.stack_step++ ] = SubConstStreamingCursorFrame { frame.bit_index + 1, frame.prefix, out0, out1, 0 };
			continue;
		}
		if ( frame.state == 1 )
		{
			frame.state = 2;
			const auto&		   M = cursor.mats_alpha1[ std::size_t( frame.bit_index ) ];
			const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
			const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
			cursor.stack[ cursor.stack_step++ ] = SubConstStreamingCursorFrame { frame.bit_index + 1, frame.prefix | ( 1u << frame.bit_index ), out0, out1, 0 };
			continue;
		}
		--cursor.stack_step;
	}
	return false;
}

static inline SubConstCandidate exact_best_subconst_candidate( u32 output_mask_beta, u32 constant )
{
	// Self-contained fallback for the compare/plot TU:
	// fixed output beta -> exact best input alpha* for var-const subtraction.
	// The repository now also has a public operator-layer Gap B API for this column object,
	// but this standalone TU intentionally stays self-contained and does not include project analysis headers.
	SubConstStreamingCursor cursor;
	SubConstCandidate		candidate {};
	reset_subconst_streaming_cursor( cursor, output_mask_beta, constant, 32, 32 );
	if ( !next_subconst_streaming_candidate( cursor, candidate ) )
		return SubConstCandidate { output_mask_beta, 32 };
	return candidate;
}

// -------------------------------------------------------------------------
// Exact injector precompute copied from search framework math
// -------------------------------------------------------------------------

constexpr u32 generate_dynamic_diffusion_mask0( u32 x ) noexcept
{
	const u32 v0 = x;
	const u32 v1 = v0 ^ rotl32( v0, 2 );
	const u32 v2 = v0 ^ rotl32( v1, 17 );
	const u32 v3 = v0 ^ rotl32( v2, 4 );
	const u32 v4 = v3 ^ rotl32( v3, 24 );
	return v2 ^ rotl32( v4, 7 );
}

constexpr u32 generate_dynamic_diffusion_mask1( u32 x ) noexcept
{
	const u32 v0 = x;
	const u32 v1 = v0 ^ rotr32( v0, 2 );
	const u32 v2 = v0 ^ rotr32( v1, 17 );
	const u32 v3 = v0 ^ rotr32( v2, 4 );
	const u32 v4 = v3 ^ rotr32( v3, 24 );
	return v2 ^ rotr32( v4, 7 );
}

constexpr u32 branch_bridge( u32 x ) noexcept
{
	return x ^ rotl32( rotl32( x, 22 ), 13 );
}

static constexpr u32 RC7_R24 = rotr32( RC[ 7 ], 24 );
static constexpr u32 RC8_R24 = rotr32( RC[ 8 ], 24 );
static constexpr u32 RC13_R24 = rotr32( RC[ 13 ], 24 );
static constexpr u32 RC2_L8 = rotl32( RC[ 2 ], 8 );
static constexpr u32 RC3_L8 = rotl32( RC[ 3 ], 8 );
static constexpr u32 RC12_L8 = rotl32( RC[ 12 ], 8 );

static constexpr u32 MASK0_RC7 = generate_dynamic_diffusion_mask0( RC[ 7 ] );
static constexpr u32 MASK1_RC2 = generate_dynamic_diffusion_mask1( RC[ 2 ] );

constexpr std::pair<u32, u32> cd_injection_from_B( u32 B, u32 /*rc0*/, u32 /*rc1*/ ) noexcept
{
	const u32 companion0 = rotr32( B, 24 );

	const u32 mask = generate_dynamic_diffusion_mask0( B );
	const u32 companion_mask = rotr32( mask, 24 ) ^ MASK0_RC7;
	const u32 mask_r1 = rotr32( mask, 5 );

	const u32 x0 = companion0 ^ mask;
	const u32 x1 = B ^ mask;
	const u32 view = companion0 ^ companion_mask;
	const u32 bridge_state = branch_bridge( B );

	const u32 q_state_na = RC7_R24 ^ ( ~( B & mask ) );
	const u32 q_comp_no  = companion0 ^ B ^ RC8_R24 ^ ( ~( companion0 | mask_r1 ) );
	const u32 q_bridge   = bridge_state ^ B ^ RC13_R24 ^ ( ~( bridge_state & companion_mask ) );
	const u32 q_shared   = q_state_na ^ q_comp_no;

	const u32 cross_q = ( B ^ mask_r1 ) & rotr32( mask ^ companion_mask, 7 );
	const u32 anti_q  = ( ( x1 >> 3 ) ^ ( view >> 5 ) ^ mask_r1 ) & ( q_comp_no ^ rotr32( x0, 11 ) );

	const u32 c = q_shared ^ rotr32( q_comp_no, 5 ) ^ rotr32( q_comp_no, 11 ) ^ anti_q;
	const u32 d = q_shared ^ rotr32( q_state_na, 5 ) ^ rotr32( q_bridge, 13 ) ^ cross_q ^ anti_q;
	return { c, d };
}

constexpr std::pair<u32, u32> cd_injection_from_A( u32 A, u32 /*rc0*/, u32 /*rc1*/ ) noexcept
{
	const u32 companion0 = rotl32( A, 8 );

	const u32 mask = generate_dynamic_diffusion_mask1( A );
	const u32 companion_mask = rotl32( mask, 8 ) ^ MASK1_RC2;
	const u32 mask_r1 = rotr32( mask, 5 );

	const u32 x0 = companion0 ^ mask;
	const u32 x1 = A ^ mask;
	const u32 view = companion0 ^ companion_mask;
	const u32 bridge_state = branch_bridge( A );

	const u32 q_state_no = RC2_L8 ^ ( ~( A | mask ) );
	const u32 q_comp_na  = companion0 ^ A ^ RC3_L8 ^ ( ~( companion0 & mask_r1 ) );
	const u32 q_bridge   = bridge_state ^ A ^ RC12_L8 ^ ( ~( bridge_state | companion_mask ) );
	const u32 q_shared   = q_state_no ^ q_comp_na;

	const u32 cross_q = ( A ^ mask_r1 ) & rotl32( mask ^ companion_mask, 13 );
	const u32 anti_q  = ( ( x1 << 3 ) ^ ( view << 5 ) ^ mask_r1 ) | ( q_comp_na ^ rotl32( x0, 11 ) );

	const u32 c = q_shared ^ rotl32( q_comp_na, 5 ) ^ rotl32( q_comp_na, 11 ) ^ anti_q;
	const u32 d = q_shared ^ rotl32( q_state_no, 5 ) ^ rotl32( q_bridge, 13 ) ^ cross_q ^ anti_q;
	return { c, d };
}

constexpr u32 injected_xor_term_from_branch_b( u32 B ) noexcept
{
	auto [ c, d ] = cd_injection_from_B( B ^ RC[ 4 ], 0u, 0u );
	return rotl32( u32( ( c << 2 ) ^ ( d >> 2 ) ), 24 )
		^ rotl32( d, 16 )
		^ rotl32( u32( ( c >> 5 ) ^ ( d << 5 ) ), 8 );
}

constexpr u32 injected_xor_term_from_branch_a( u32 A ) noexcept
{
	auto [ c, d ] = cd_injection_from_A( A ^ RC[ 9 ], 0u, 0u );
	return rotr32( u32( ( c >> 3 ) ^ ( d << 3 ) ), 24 )
		^ rotr32( d, 16 )
		^ rotr32( u32( ( c << 1 ) ^ ( d >> 1 ) ), 8 );
}

struct InjectionCorrelationTransition
{
	u32					offset_mask = 0;
	std::array<u32, 32> basis_vectors {};
	int					rank = 0;
	double				weight = 0.0;
};

static constexpr u32 injected_f0_branch_b = injected_xor_term_from_branch_b( 0u );
static constexpr u32 injected_f0_branch_a = injected_xor_term_from_branch_a( 0u );

static consteval std::array<u32, 32> make_injection_f_basis_branch_b()
{
	std::array<u32, 32> out {};
	for ( int i = 0; i < 32; ++i )
		out[ std::size_t( i ) ] = injected_xor_term_from_branch_b( 1u << i );
	return out;
}
static consteval std::array<u32, 32> make_injection_f_basis_branch_a()
{
	std::array<u32, 32> out {};
	for ( int i = 0; i < 32; ++i )
		out[ std::size_t( i ) ] = injected_xor_term_from_branch_a( 1u << i );
	return out;
}
static constexpr std::array<u32, 32> injected_f_basis_branch_b = make_injection_f_basis_branch_b();
static constexpr std::array<u32, 32> injected_f_basis_branch_a = make_injection_f_basis_branch_a();

static consteval std::array<std::array<u32, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_b()
{
	std::array<std::array<u32, 32>, 32> rows_by_outbit {};
	for ( int i = 0; i < 31; ++i )
	{
		const u32 fi = injected_f_basis_branch_b[ std::size_t( i ) ];
		for ( int j = i + 1; j < 32; ++j )
		{
			const u32 fj = injected_f_basis_branch_b[ std::size_t( j ) ];
			const u32 fij = injected_xor_term_from_branch_b( ( 1u << i ) ^ ( 1u << j ) );
			const u32 delta = injected_f0_branch_b ^ fi ^ fj ^ fij;
			for ( int k = 0; k < 32; ++k )
			{
				if ( ( ( delta >> k ) & 1u ) != 0u )
				{
					rows_by_outbit[ std::size_t( k ) ][ std::size_t( i ) ] |= ( 1u << j );
					rows_by_outbit[ std::size_t( k ) ][ std::size_t( j ) ] |= ( 1u << i );
				}
			}
		}
	}
	return rows_by_outbit;
}
static consteval std::array<std::array<u32, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_a()
{
	std::array<std::array<u32, 32>, 32> rows_by_outbit {};
	for ( int i = 0; i < 31; ++i )
	{
		const u32 fi = injected_f_basis_branch_a[ std::size_t( i ) ];
		for ( int j = i + 1; j < 32; ++j )
		{
			const u32 fj = injected_f_basis_branch_a[ std::size_t( j ) ];
			const u32 fij = injected_xor_term_from_branch_a( ( 1u << i ) ^ ( 1u << j ) );
			const u32 delta = injected_f0_branch_a ^ fi ^ fj ^ fij;
			for ( int k = 0; k < 32; ++k )
			{
				if ( ( ( delta >> k ) & 1u ) != 0u )
				{
					rows_by_outbit[ std::size_t( k ) ][ std::size_t( i ) ] |= ( 1u << j );
					rows_by_outbit[ std::size_t( k ) ][ std::size_t( j ) ] |= ( 1u << i );
				}
			}
		}
	}
	return rows_by_outbit;
}
static constexpr std::array<std::array<u32, 32>, 32> injected_quad_rows_by_outbit_branch_b = make_injection_quadratic_rows_by_outbit_branch_b();
static constexpr std::array<std::array<u32, 32>, 32> injected_quad_rows_by_outbit_branch_a = make_injection_quadratic_rows_by_outbit_branch_a();

static inline int xor_basis_add_32( std::array<u32, 32>& basis_by_msb, u32 v ) noexcept
{
	while ( v != 0u )
	{
		const unsigned bit = 31u - std::countl_zero( v );
		const u32	   basis = basis_by_msb[ std::size_t( bit ) ];
		if ( basis != 0u )
			v ^= basis;
		else
		{
			basis_by_msb[ std::size_t( bit ) ] = v;
			return 1;
		}
	}
	return 0;
}

static inline InjectionCorrelationTransition compute_injection_transition_from_branch_b( u32 output_mask_u ) noexcept
{
	InjectionCorrelationTransition transition {};
	if ( output_mask_u == 0u )
		return transition;
	const unsigned g0 = parity32( output_mask_u & injected_f0_branch_b );
	u32			   offset_mask = 0u;
	for ( int i = 0; i < 32; ++i )
	{
		const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_b[ std::size_t( i ) ] );
		if ( ( gi ^ g0 ) != 0u )
			offset_mask ^= ( 1u << i );
	}
	transition.offset_mask = offset_mask;
	std::array<u32, 32> rows {};
	for ( int k = 0; k < 32; ++k )
		if ( ( ( output_mask_u >> k ) & 1u ) != 0u )
			for ( int i = 0; i < 32; ++i )
				rows[ std::size_t( i ) ] ^= injected_quad_rows_by_outbit_branch_b[ std::size_t( k ) ][ std::size_t( i ) ];
	std::array<u32, 32> basis_by_msb {};
	int					rank = 0;
	for ( int i = 0; i < 32; ++i )
		if ( rows[ std::size_t( i ) ] != 0u )
			rank += xor_basis_add_32( basis_by_msb, rows[ std::size_t( i ) ] );
	transition.rank = rank;
	transition.weight = ( rank + 1 ) / 2;
	int packed = 0;
	for ( int bit = 31; bit >= 0; --bit )
	{
		const u32 v = basis_by_msb[ std::size_t( bit ) ];
		if ( v != 0u )
			transition.basis_vectors[ std::size_t( packed++ ) ] = v;
	}
	return transition;
}
static inline InjectionCorrelationTransition compute_injection_transition_from_branch_a( u32 output_mask_u ) noexcept
{
	InjectionCorrelationTransition transition {};
	if ( output_mask_u == 0u )
		return transition;
	const unsigned g0 = parity32( output_mask_u & injected_f0_branch_a );
	u32			   offset_mask = 0u;
	for ( int i = 0; i < 32; ++i )
	{
		const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_a[ std::size_t( i ) ] );
		if ( ( gi ^ g0 ) != 0u )
			offset_mask ^= ( 1u << i );
	}
	transition.offset_mask = offset_mask;
	std::array<u32, 32> rows {};
	for ( int k = 0; k < 32; ++k )
		if ( ( ( output_mask_u >> k ) & 1u ) != 0u )
			for ( int i = 0; i < 32; ++i )
				rows[ std::size_t( i ) ] ^= injected_quad_rows_by_outbit_branch_a[ std::size_t( k ) ][ std::size_t( i ) ];
	std::array<u32, 32> basis_by_msb {};
	int					rank = 0;
	for ( int i = 0; i < 32; ++i )
		if ( rows[ std::size_t( i ) ] != 0u )
			rank += xor_basis_add_32( basis_by_msb, rows[ std::size_t( i ) ] );
	transition.rank = rank;
	transition.weight = ( rank + 1 ) / 2;
	int packed = 0;
	for ( int bit = 31; bit >= 0; --bit )
	{
		const u32 v = basis_by_msb[ std::size_t( bit ) ];
		if ( v != 0u )
			transition.basis_vectors[ std::size_t( packed++ ) ] = v;
	}
	return transition;
}

struct NeoRoundResult
{
	u32	   A {};
	u32	   B {};
	double weight_add {};
	double weight_const {};
	double weight_inj {};
};
struct AlzetteRoundResult
{
	u32	   x {};
	u32	   y {};
	double weight_add {};
};

struct ConstCacheKey
{
	u32 beta {};
	u32 constant {};

	bool operator==( const ConstCacheKey& ) const = default;
};

struct ConstCacheKeyHash
{
	std::size_t operator()( const ConstCacheKey& key ) const noexcept
	{
		return ( static_cast<std::size_t>( key.beta ) << 32 ) ^ static_cast<std::size_t>( key.constant );
	}
};

class ConstLinearCache
{
public:
	explicit ConstLinearCache( std::size_t cap = 50000 ) : capacity_( cap ) {}

	std::optional<SubConstCandidate> get( u32 beta, u32 constant ) const
	{
		if ( capacity_ == 0 )
			return std::nullopt;
		auto it = map_.find( { beta, constant } );
		if ( it == map_.end() )
			return std::nullopt;
		return it->second;
	}

	void put( u32 beta, u32 constant, const SubConstCandidate& value )
	{
		if ( capacity_ == 0 )
			return;
		if ( map_.size() >= capacity_ )
			map_.clear();
		map_[ { beta, constant } ] = value;
	}

private:
	std::size_t																capacity_ = 0;
	std::unordered_map<ConstCacheKey, SubConstCandidate, ConstCacheKeyHash> map_ {};
};

static inline SubConstCandidate exact_best_subconst_candidate_cached( u32 output_mask_beta, u32 constant, ConstLinearCache& cache )
{
	if ( auto value = cache.get( output_mask_beta, constant ); value.has_value() )
		return *value;
	const auto candidate = exact_best_subconst_candidate( output_mask_beta, constant );
	cache.put( output_mask_beta, constant, candidate );
	return candidate;
}

static inline AlzetteRoundResult alzette_round_linear( u32 in_x_mask, u32 in_y_mask )
{
	// Gap A: var-var add uses fixed (v,w) -> u* (`find_optimal_output_u_ccz`); names match modular_addition_ccz.hpp.
	u32	   x = in_x_mask;
	u32	   y = in_y_mask;
	double weight_add = 0.0;

	{
		const auto best = exact_best_add_weight( x, rotl32( y, 31 ) );
		x = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	x ^= rotl32( y, 24 );

	{
		const auto best = exact_best_add_weight( x, rotl32( y, 17 ) );
		x = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	x ^= rotl32( y, 17 );

	{
		const auto best = exact_best_add_weight( x, y );
		x = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	x ^= rotl32( y, 31 );

	{
		const auto best = exact_best_add_weight( x, rotl32( y, 24 ) );
		x = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	x ^= rotl32( y, 16 );

	return { x, y, weight_add };
}

static inline NeoRoundResult neoalzette_round_linear( u32 in_A_mask, u32 in_B_mask, ConstLinearCache& cache )
{
	// Hybrid interim wiring:
	// - var-var add sites use inlined Gap A (`find_optimal_output_u_ccz` / `linear_correlation_add_ccz_weight`);
	// - var-const sites here still use this TU's self-contained fixed-output beta -> alpha* fallback.
	//   Repo-level Gap B exact optimal-mask operators now exist, but this compare/plot TU intentionally
	//   remains self-contained instead of depending on project analysis headers.
	u32	   A = in_A_mask;
	u32	   B = in_B_mask;
	double weight_add = 0.0;
	double weight_const = 0.0;
	double weight_inj = 0.0;

	{
		const auto best = exact_best_add_weight( B, rotr32( A, 31 ) ^ rotr32( A, 17 ) );
		B = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	{
		const auto best = exact_best_subconst_candidate_cached( A, RC[ 1 ], cache );
		A = best.input_mask_alpha;
		weight_const += best.linear_weight;
	}
	B ^= rotr32( A, CROSS_XOR_ROT_R0 );
	A ^= rotr32( B, CROSS_XOR_ROT_R1 );
	{
		const auto inj = compute_injection_transition_from_branch_b( A );
		B ^= inj.offset_mask;
		weight_inj += inj.weight;
	}
	{
		const auto best = exact_best_add_weight( A, rotr32( B, 31 ) ^ rotr32( B, 17 ) );
		A = best.output_mask_u;
		weight_add += best.linear_weight;
	}
	{
		const auto best = exact_best_subconst_candidate_cached( B, RC[ 6 ], cache );
		B = best.input_mask_alpha;
		weight_const += best.linear_weight;
	}
	A ^= rotr32( B, CROSS_XOR_ROT_R0 );
	B ^= rotr32( A, CROSS_XOR_ROT_R1 );
	{
		const auto inj = compute_injection_transition_from_branch_a( B );
		A ^= inj.offset_mask;
		weight_inj += inj.weight;
	}
	return { A, B, weight_add, weight_const, weight_inj };
}

static inline u64 pack_pair_key( u32 a, u32 b ) noexcept
{
	return ( static_cast<u64>( a ) << 32 ) | static_cast<u64>( b );
}

struct RoundCacheNeoValue
{
	u32	   A {};
	u32	   B {};
	double wa {};
	double wc {};
	double wi {};
};

struct RoundCacheAlzValue
{
	u32	   x {};
	u32	   y {};
	double w {};
};

static inline void write_round_csv( const std::string& output_csv, const std::vector<std::tuple<int, double, double, double>>& rows )
{
	std::ofstream out( output_csv );
	out << "rounds,mean_weight_alzette,mean_weight_neoalzette,mean_weight_difference\n";
	out << std::fixed << std::setprecision( 15 );
	for ( const auto& [ r, ma, mn, md ] : rows )
		out << r << ',' << ma << ',' << mn << ',' << md << '\n';
}

static inline void run_round_experiment( int max_rounds, u64 num_samples, u32 seed, const std::string& output_csv, bool reuse_samples, bool use_transition_cache, std::size_t const_cache_cap )
{
	std::mt19937	 rng( seed );
	ConstLinearCache const_cache( const_cache_cap );

	std::vector<std::tuple<int, double, double, double>> rows;
	rows.reserve( max_rounds );

	if ( reuse_samples )
	{
		std::vector<std::pair<u32, u32>> samples;
		samples.reserve( static_cast<std::size_t>( num_samples ) );
		for ( u64 i = 0; i < num_samples; ++i )
			samples.emplace_back( rng(), rng() );

		std::vector<double> sum_alz( max_rounds, 0.0 );
		std::vector<double> sum_neo( max_rounds, 0.0 );

		std::unordered_map<u64, RoundCacheAlzValue> cache_alz;
		std::unordered_map<u64, RoundCacheNeoValue> cache_neo;
		if ( use_transition_cache )
		{
			cache_alz.reserve( samples.size() / 8 + 16 );
			cache_neo.reserve( samples.size() / 8 + 16 );
		}

		for ( const auto& [ ma0, mb0 ] : samples )
		{
			u32	   da_alz = ma0, db_alz = mb0;
			u32	   da_neo = ma0, db_neo = mb0;
			double cum_alz = 0.0;
			double cum_neo = 0.0;

			for ( int r = 0; r < max_rounds; ++r )
			{
				if ( use_transition_cache )
				{
					const u64 key = pack_pair_key( da_alz, db_alz );
					auto	  it = cache_alz.find( key );
					if ( it == cache_alz.end() )
					{
						const auto v = alzette_round_linear( da_alz, db_alz );
						it = cache_alz.emplace( key, RoundCacheAlzValue { v.x, v.y, v.weight_add } ).first;
					}
					da_alz = it->second.x;
					db_alz = it->second.y;
					cum_alz += it->second.w;
				}
				else
				{
					const auto v = alzette_round_linear( da_alz, db_alz );
					da_alz = v.x;
					db_alz = v.y;
					cum_alz += v.weight_add;
				}

				if ( use_transition_cache )
				{
					const u64 key = pack_pair_key( da_neo, db_neo );
					auto	  it = cache_neo.find( key );
					if ( it == cache_neo.end() )
					{
						const auto v = neoalzette_round_linear( da_neo, db_neo, const_cache );
						it = cache_neo.emplace( key, RoundCacheNeoValue { v.A, v.B, v.weight_add, v.weight_const, v.weight_inj } ).first;
					}
					da_neo = it->second.A;
					db_neo = it->second.B;
					cum_neo += it->second.wa + it->second.wc + it->second.wi;
				}
				else
				{
					const auto v = neoalzette_round_linear( da_neo, db_neo, const_cache );
					da_neo = v.A;
					db_neo = v.B;
					cum_neo += v.weight_add + v.weight_const + v.weight_inj;
				}

				sum_alz[ r ] += cum_alz;
				sum_neo[ r ] += cum_neo;
			}
		}

		for ( int r = 1; r <= max_rounds; ++r )
		{
			const double mean_alz = sum_alz[ r - 1 ] / static_cast<double>( num_samples );
			const double mean_neo = sum_neo[ r - 1 ] / static_cast<double>( num_samples );
			const double mean_diff = mean_neo - mean_alz;
			rows.emplace_back( r, mean_alz, mean_neo, mean_diff );
			std::cout << std::fixed << std::setprecision( 3 ) << "[rounds=" << r << "] mean_alz=" << mean_alz << ", mean_neo=" << mean_neo << ", mean_diff=" << mean_diff << '\n';
		}
	}
	else
	{
		std::unordered_map<u64, RoundCacheAlzValue> cache_alz;
		std::unordered_map<u64, RoundCacheNeoValue> cache_neo;
		if ( use_transition_cache )
		{
			cache_alz.reserve( static_cast<std::size_t>( num_samples / 8 + 16 ) );
			cache_neo.reserve( static_cast<std::size_t>( num_samples / 8 + 16 ) );
		}

		for ( int r = 1; r <= max_rounds; ++r )
		{
			double sum_alz = 0.0;
			double sum_neo = 0.0;
			for ( u64 i = 0; i < num_samples; ++i )
			{
				u32	   da_alz = rng(), db_alz = rng();
				u32	   da_neo = da_alz, db_neo = db_alz;
				double w_alz = 0.0, w_neo = 0.0;
				for ( int k = 0; k < r; ++k )
				{
					if ( use_transition_cache )
					{
						const u64 key = pack_pair_key( da_alz, db_alz );
						auto	  it = cache_alz.find( key );
						if ( it == cache_alz.end() )
						{
							const auto v = alzette_round_linear( da_alz, db_alz );
							it = cache_alz.emplace( key, RoundCacheAlzValue { v.x, v.y, v.weight_add } ).first;
						}
						da_alz = it->second.x;
						db_alz = it->second.y;
						w_alz += it->second.w;
					}
					else
					{
						const auto v = alzette_round_linear( da_alz, db_alz );
						da_alz = v.x;
						db_alz = v.y;
						w_alz += v.weight_add;
					}

					if ( use_transition_cache )
					{
						const u64 key = pack_pair_key( da_neo, db_neo );
						auto	  it = cache_neo.find( key );
						if ( it == cache_neo.end() )
						{
							const auto v = neoalzette_round_linear( da_neo, db_neo, const_cache );
							it = cache_neo.emplace( key, RoundCacheNeoValue { v.A, v.B, v.weight_add, v.weight_const, v.weight_inj } ).first;
						}
						da_neo = it->second.A;
						db_neo = it->second.B;
						w_neo += it->second.wa + it->second.wc + it->second.wi;
					}
					else
					{
						const auto v = neoalzette_round_linear( da_neo, db_neo, const_cache );
						da_neo = v.A;
						db_neo = v.B;
						w_neo += v.weight_add + v.weight_const + v.weight_inj;
					}
				}
				sum_alz += w_alz;
				sum_neo += w_neo;
			}
			const double mean_alz = sum_alz / static_cast<double>( num_samples );
			const double mean_neo = sum_neo / static_cast<double>( num_samples );
			const double mean_diff = mean_neo - mean_alz;
			rows.emplace_back( r, mean_alz, mean_neo, mean_diff );
			std::cout << std::fixed << std::setprecision( 3 ) << "[rounds=" << r << "] mean_alz=" << mean_alz << ", mean_neo=" << mean_neo << ", mean_diff=" << mean_diff << '\n';
		}
	}
	write_round_csv( output_csv, rows );
	std::cout << "Round experiment saved to " << output_csv << '\n';
	std::cout << "[trend] Loaded " << rows.size() << " round-points from " << output_csv << '\n';
	std::cout << "[trend] N per round = " << num_samples << '\n';
	std::cout << "[trend] seed = " << seed << '\n';
	if ( !rows.empty() )
	{
		std::cout << std::fixed << std::setprecision(3);
		std::cout << "[trend] last-round mean(Alzette) = "  << std::get<1>(rows.back()) << " bits\n";
		std::cout << "[trend] last-round mean(NeoAlzette) = " << std::get<2>(rows.back()) << " bits\n";
		std::cout << "[trend] last-round mean difference (NeoAlzette - Alzette) = " << std::get<3>(rows.back()) << " bits\n";
	}
	std::cout << "[trend] matplotlib not available; skipping plot generation.\n";
}

static inline void run_experiment( u64 num_samples, u32 seed, const std::string& output_csv, bool write_csv, std::size_t const_cache_cap )
{
	std::mt19937	 rng( seed );
	ConstLinearCache const_cache( const_cache_cap );
	std::ofstream	 out;
	if ( write_csv )
	{
		out.open( output_csv );
		out << "mask_a,mask_b,weight_alzette,weight_neoalzette,weight_difference,weight_additions,weight_constant,weight_injection\n";
	}
	long double sum_diff = 0.0L;
	double		min_diff = std::numeric_limits<double>::infinity();
	double		max_diff = -std::numeric_limits<double>::infinity();
	for ( u64 i = 0; i < num_samples; ++i )
	{
		const u32	 mask_a = rng(), mask_b = rng();
		const auto	 alz = alzette_round_linear( mask_a, mask_b );
		const auto	 neo = neoalzette_round_linear( mask_a, mask_b, const_cache );
		const double w_alz = alz.weight_add;
		const double w_neo = neo.weight_add + neo.weight_const + neo.weight_inj;
		const double diff = w_neo - w_alz;
		sum_diff += diff;
		min_diff = std::min( min_diff, diff );
		max_diff = std::max( max_diff, diff );
		if ( write_csv )
			out << "0x" << std::hex << std::nouppercase << mask_a << ",0x" << mask_b << std::dec << ',' << std::fixed << std::setprecision( 15 ) << w_alz << ',' << w_neo << ',' << diff << ',' << neo.weight_add << ',' << neo.weight_const << ',' << neo.weight_inj << '\n';
	}
	const double mean_diff = static_cast<double>( sum_diff / static_cast<long double>( num_samples ) );
	std::cout << "Experiment completed for " << num_samples << " samples.\n";
	std::cout << std::fixed << std::setprecision( 3 ) << "Average weight difference (NeoAlzette - Alzette): " << mean_diff << " bits\n"
			  << "Min difference: " << min_diff << " bits, Max difference: " << max_diff << " bits\n"
			  << "matplotlib not available; skipping plot generation.\n";
}

struct Options
{
	int			max_rounds = 8;
	u64			num_samples = 2000;
	u32			seed = 0;
	bool		reuse_samples = true;
	bool		use_transition_cache = false;
	bool		skip_round = false;
	bool		skip_single = false;
	bool		write_single_csv = true;
	std::string round_csv = "results_by_round_linear.csv";
	std::string single_csv = "results_linear.csv";
	std::size_t const_cache_cap = 50000;
};

static inline Options parse_args( int argc, char** argv )
{
	Options opt;
	for ( int i = 1; i < argc; ++i )
	{
		const std::string arg = argv[ i ];
		auto			  need_value = [ & ]( const char* name ) -> std::string {
			 if ( i + 1 >= argc )
				 throw std::runtime_error( std::string( "Missing value for " ) + name );
			 return argv[ ++i ];
		};
		if ( arg == "--rounds" )
			opt.max_rounds = std::stoi( need_value( "--rounds" ) );
		else if ( arg == "--samples" )
			opt.num_samples = std::stoull( need_value( "--samples" ) );
		else if ( arg == "--seed" )
			opt.seed = static_cast<u32>( std::stoul( need_value( "--seed" ) ) );
		else if ( arg == "--round-csv" )
			opt.round_csv = need_value( "--round-csv" );
		else if ( arg == "--single-csv" )
			opt.single_csv = need_value( "--single-csv" );
		else if ( arg == "--const-cache-cap" )
			opt.const_cache_cap = std::stoull( need_value( "--const-cache-cap" ) );
		else if ( arg == "--reuse-samples" )
			opt.reuse_samples = true;
		else if ( arg == "--fresh-samples" )
			opt.reuse_samples = false;
		else if ( arg == "--use-transition-cache" )
			opt.use_transition_cache = true;
		else if ( arg == "--skip-round" )
			opt.skip_round = true;
		else if ( arg == "--skip-single" )
			opt.skip_single = true;
		else if ( arg == "--no-single-csv" )
			opt.write_single_csv = false;
		else if ( arg == "--help" || arg == "-h" )
		{
			std::cout << "Options:\n"
					  << "  --rounds N\n"
					  << "  --samples N\n"
					  << "  --seed N\n"
					  << "  --round-csv file.csv\n"
					  << "  --single-csv file.csv\n"
					  << "  --const-cache-cap N\n"
					  << "  --reuse-samples | --fresh-samples\n"
					  << "  --use-transition-cache\n"
					  << "  --skip-round\n"
					  << "  --skip-single\n"
					  << "  --no-single-csv\n";
			std::exit( 0 );
		}
		else
			throw std::runtime_error( "Unknown argument: " + arg );
	}
	if ( opt.max_rounds <= 0 )
		throw std::runtime_error( "--rounds must be positive" );
	if ( opt.num_samples == 0 )
		throw std::runtime_error( "--samples must be positive" );
	return opt;
}


int main( int argc, char** argv )
{
	try
	{
		const auto opt = parse_args( argc, argv );
		if ( !opt.skip_round )
		{
			run_round_experiment( opt.max_rounds, opt.num_samples, opt.seed, opt.round_csv, opt.reuse_samples, opt.use_transition_cache, opt.const_cache_cap );
		}
		if ( !opt.skip_single )
		{
			run_experiment( opt.num_samples, opt.seed, opt.single_csv, opt.write_single_csv, opt.const_cache_cap );
		}
		return 0;
	}
	catch ( const std::exception& e )
	{
		std::cerr << "Error: " << e.what() << '\n';
		return 1;
	}
}
