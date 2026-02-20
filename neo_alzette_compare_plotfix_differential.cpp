#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

// 此文件用于NeoAlzette分析版本对决Alzette分析版本
// 清晰的数学对象以及差分轨迹呈现
// 禁止包含任何本工程分析ARX算子以及私有头文件导致无法正确编译
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 MASK32 = 0xFFFFFFFFu;

constexpr u32 rotl32( u32 x, unsigned r ) noexcept
{
	return std::rotl( x, static_cast<int>( r & 31u ) );
}
constexpr u32 rotr32( u32 x, unsigned r ) noexcept
{
	return std::rotr( x, static_cast<int>( r & 31u ) );
}

constexpr std::array<u32, 16> RC = {
	0x16B2C40Bu, 0xC117176Au, 0x0F9A2598u, 0xA1563ACAu, 0x243F6A88u, 0x85A308D3u, 0x13198102u, 0xE0370734u, 0x9E3779B9u, 0x7F4A7C15u, 0xF39CC060u, 0x5CEDC834u, 0xB7E15162u, 0x8AED2A6Au, 0xBF715880u, 0x9CF4F3C7u,
};

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

struct InjectionTables
{
	u32									inj_f0_b {};
	u32									inj_f0_a {};
	std::array<std::array<u32, 32>, 32> lin_cols_b {};
	std::array<std::array<u32, 32>, 32> lin_cols_a {};

	static constexpr int rank_from_cols( const std::array<u32, 32>& cols ) noexcept
	{
		std::array<u32, 32> basis {};
		int					basis_size = 0;
		for ( u32 v : cols )
		{
			u32 x = v;
			for ( int i = 0; i < basis_size; ++i )
			{
				x = std::min( x, static_cast<u32>( x ^ basis[ i ] ) );
			}
			if ( x != 0 )
			{
				basis[ basis_size++ ] = x;
			}
		}
		return basis_size;
	}

	template <typename Func>
	static constexpr std::array<std::array<u32, 32>, 32> build_lin_cols( Func&& f, u32 f0 )
	{
		std::array<std::array<u32, 32>, 32> all {};
		for ( int i = 0; i < 32; ++i )
		{
			const u32			di = u32 { 1 } << i;
			const u32			g0 = f0 ^ f( di );
			std::array<u32, 32> cols {};
			for ( int j = 0; j < 32; ++j )
			{
				const u32 ej = u32 { 1 } << j;
				const u32 gij = f( ej ) ^ f( ej ^ di );
				cols[ j ] = gij ^ g0;
			}
			all[ i ] = cols;
		}
		return all;
	}

	constexpr InjectionTables()
	{
		inj_f0_b = injected_xor_term_from_branch_b( 0 );
		inj_f0_a = injected_xor_term_from_branch_a( 0 );
		lin_cols_b = build_lin_cols( []( u32 x ) noexcept { return injected_xor_term_from_branch_b( x ); }, inj_f0_b );
		lin_cols_a = build_lin_cols( []( u32 x ) noexcept { return injected_xor_term_from_branch_a( x ); }, inj_f0_a );
	}

	constexpr u32 injection_f_B( u32 delta ) const noexcept
	{
		return inj_f0_b ^ injected_xor_term_from_branch_b( delta );
	}
	constexpr u32 injection_f_A( u32 delta ) const noexcept
	{
		return inj_f0_a ^ injected_xor_term_from_branch_a( delta );
	}

	constexpr int injection_weight_branch_b( u32 delta ) const noexcept
	{
		if ( delta == 0 )
			return 0;
		std::array<u32, 32> cols {};
		while ( delta != 0 )
		{
			const u32	lsb = delta & ( 0u - delta );
			const int	idx = std::countr_zero( lsb );
			const auto& ci = lin_cols_b[ idx ];
			for ( int j = 0; j < 32; ++j )
				cols[ j ] ^= ci[ j ];
			delta ^= lsb;
		}
		return rank_from_cols( cols );
	}

	constexpr int injection_weight_branch_a( u32 delta ) const noexcept
	{
		if ( delta == 0 )
			return 0;
		std::array<u32, 32> cols {};
		while ( delta != 0 )
		{
			const u32	lsb = delta & ( 0u - delta );
			const int	idx = std::countr_zero( lsb );
			const auto& ci = lin_cols_a[ idx ];
			for ( int j = 0; j < 32; ++j )
				cols[ j ] ^= ci[ j ];
			delta ^= lsb;
		}
		return rank_from_cols( cols );
	}
};

inline constinit InjectionTables g_injection_tables {};

inline u32 psi( u32 alpha, u32 beta, u32 gamma ) noexcept
{
	const u32 not_alpha = ~alpha;
	return ( not_alpha ^ beta ) & ( not_alpha ^ gamma );
}

inline int xdp_add_lm2001( u32 alpha, u32 beta, u32 gamma ) noexcept
{
	const u32 beta_shifted = beta << 1;
	const u32 psi_shifted = psi( alpha << 1, beta_shifted, gamma << 1 );
	const u32 xor_val = alpha ^ beta ^ gamma;
	const u32 xor_condition = xor_val ^ beta_shifted;
	if ( ( psi_shifted & xor_condition ) != 0 )
		return -1;
	const u32 eq_val = psi( alpha, beta, gamma );
	const u32 masked_bad = ( ~eq_val ) & 0x7FFFFFFFu;
	return std::popcount( masked_bad );
}

inline u32 aopr( u32 x ) noexcept
{
	u32 p = 0;
	u32 carry = 0;
	for ( int i = 31; i >= 0; --i )
	{
		const u32 bit = ( x >> i ) & 1u;
		carry ^= bit;
		if ( carry )
			p |= ( u32 { 1 } << i );
	}
	return p;
}

inline u32 find_optimal_gamma( u32 alpha, u32 beta ) noexcept
{
	constexpr u32 mask = 0xFFFFFFFFu;
	const u32	  r = alpha & 1u;
	u32			  e = ( ~( alpha ^ beta ) & ( ~r ) ) & mask;
	u32			  a = e & ( e << 1 ) & ( alpha ^ ( alpha << 1 ) );
	const u32	  p = aopr( a & mask );
	a = ( a | ( a >> 1 ) ) & ( ~r );
	const u32 b = ( a | e ) << 1;
	u32		  gamma = ( ( ( alpha ^ p ) & a ) | ( ( alpha ^ beta ^ ( alpha << 1 ) ) & ( ~a ) & b ) | ( alpha & ( ~a ) & ( ~b ) ) ) & mask;
	gamma = ( gamma & ~1u ) | ( ( alpha ^ beta ) & 1u );
	return gamma;
}

inline std::pair<u32, int> find_optimal_gamma_with_weight( u32 alpha, u32 beta ) noexcept
{
	const u32 gamma = find_optimal_gamma( alpha, beta );
	const int weight = xdp_add_lm2001( alpha, beta, gamma );
	return { gamma, weight };
}

using Mat4 = std::array<std::array<u32, 4>, 4>;
struct StepMatrices
{
	Mat4 M0 {};
	Mat4 M1 {};
};

constexpr StepMatrices step_matrices( unsigned u_bit, unsigned a_bit ) noexcept
{
	StepMatrices out {};
	for ( int s = 0; s < 4; ++s )
	{
		const unsigned c = s & 1;
		const unsigned cp = ( s >> 1 ) & 1;
		for ( unsigned x0 = 0; x0 <= 1; ++x0 )
		{
			const unsigned x1 = x0 ^ u_bit;
			const unsigned y0 = x0 ^ a_bit ^ c;
			const unsigned y1 = x1 ^ a_bit ^ cp;
			const unsigned bit_out = y0 ^ y1;
			const unsigned nc = ( x0 & a_bit ) | ( x0 & c ) | ( a_bit & c );
			const unsigned ncp = ( x1 & a_bit ) | ( x1 & cp ) | ( a_bit & cp );
			const unsigned ns = nc | ( ncp << 1 );
			if ( bit_out == 0 )
			{
				out.M0[ s ][ ns ] += 1;
			}
			else
			{
				out.M1[ s ][ ns ] += 1;
			}
		}
	}
	return out;
}

inline constexpr std::array<std::array<StepMatrices, 2>, 2> g_step_addconstant = [] {
	std::array<std::array<StepMatrices, 2>, 2> table {};
	for ( int u = 0; u <= 1; ++u )
	{
		for ( int a = 0; a <= 1; ++a )
			table[ u ][ a ] = step_matrices( static_cast<unsigned>( u ), static_cast<unsigned>( a ) );
	}
	return table;
}();

struct Vec4
{
	u64	 x0 {}, x1 {}, x2 {}, x3 {};
	bool operator==( const Vec4& ) const = default;
};

struct Vec4Hash
{
	std::size_t operator()( const Vec4& v ) const noexcept
	{
		std::size_t h = static_cast<std::size_t>( v.x0 * 0x9E3779B185EBCA87ull );
		h ^= static_cast<std::size_t>( v.x1 + 0x9E3779B97F4A7C15ull + ( h << 6 ) + ( h >> 2 ) );
		h ^= static_cast<std::size_t>( v.x2 + 0xBF58476D1CE4E5B9ull + ( h << 6 ) + ( h >> 2 ) );
		h ^= static_cast<std::size_t>( v.x3 + 0x94D049BB133111EBull + ( h << 6 ) + ( h >> 2 ) );
		return h;
	}
};

constexpr bool dominates( const Vec4& a, const Vec4& b ) noexcept
{
	return a.x0 >= b.x0 && a.x1 >= b.x1 && a.x2 >= b.x2 && a.x3 >= b.x3;
}

constexpr Vec4 apply( const Vec4& vec, const Mat4& M ) noexcept
{
	return {
		vec.x0 * M[ 0 ][ 0 ] + vec.x1 * M[ 1 ][ 0 ] + vec.x2 * M[ 2 ][ 0 ] + vec.x3 * M[ 3 ][ 0 ],
		vec.x0 * M[ 0 ][ 1 ] + vec.x1 * M[ 1 ][ 1 ] + vec.x2 * M[ 2 ][ 1 ] + vec.x3 * M[ 3 ][ 1 ],
		vec.x0 * M[ 0 ][ 2 ] + vec.x1 * M[ 1 ][ 2 ] + vec.x2 * M[ 2 ][ 2 ] + vec.x3 * M[ 3 ][ 2 ],
		vec.x0 * M[ 0 ][ 3 ] + vec.x1 * M[ 1 ][ 3 ] + vec.x2 * M[ 2 ][ 3 ] + vec.x3 * M[ 3 ][ 3 ],
	};
}

struct DiffAddConstResult
{
	u32 best_delta {};
	int wc_int {};
	u64 best_count {};
};

struct ConstCacheKey
{
	u32	 alpha {};
	u32	 constant {};
	bool operator==( const ConstCacheKey& ) const = default;
};

struct ConstCacheKeyHash
{
	std::size_t operator()( const ConstCacheKey& k ) const noexcept
	{
		return ( static_cast<std::size_t>( k.alpha ) << 32 ) ^ static_cast<std::size_t>( k.constant );
	}
};

class ConstDiffCache
{
public:
	explicit ConstDiffCache( std::size_t cap = 200000 ) : capacity_( cap ) {}

	std::optional<DiffAddConstResult> get( u32 alpha, u32 constant ) const
	{
		if ( capacity_ == 0 )
			return std::nullopt;
		auto it = map_.find( { alpha, constant } );
		if ( it == map_.end() )
			return std::nullopt;
		return it->second;
	}

	void put( u32 alpha, u32 constant, const DiffAddConstResult& value )
	{
		if ( capacity_ == 0 )
			return;
		if ( map_.size() >= capacity_ )
		{
			map_.clear();
		}
		map_[ { alpha, constant } ] = value;
	}

private:
	std::size_t																 capacity_;
	std::unordered_map<ConstCacheKey, DiffAddConstResult, ConstCacheKeyHash> map_;
};

inline DiffAddConstResult diff_addconst_exact_weight_impl( u32 alpha, u32 constant )
{
	std::unordered_map<Vec4, u32, Vec4Hash> candidates;
	candidates.reserve( 64 );
	candidates[ { 1, 0, 0, 0 } ] = 0;

	for ( int i = 0; i < 32; ++i )
	{
		const unsigned u_bit = ( alpha >> i ) & 1u;
		const unsigned a_bit = ( constant >> i ) & 1u;
		const auto&	   step = g_step_addconstant[ u_bit ][ a_bit ];

		std::unordered_map<Vec4, u32, Vec4Hash> next;
		next.reserve( candidates.size() * 2 + 8 );

		for ( const auto& [ vec, delta_prefix ] : candidates )
		{
			const Vec4 v0 = apply( vec, step.M0 );
			if ( ( v0.x0 | v0.x1 | v0.x2 | v0.x3 ) != 0 )
			{
				const u32 d0 = delta_prefix;
				auto	  it = next.find( v0 );
				if ( it == next.end() || d0 < it->second )
					next[ v0 ] = d0;
			}

			const Vec4 v1 = apply( vec, step.M1 );
			if ( ( v1.x0 | v1.x1 | v1.x2 | v1.x3 ) != 0 )
			{
				const u32 d1 = delta_prefix | ( u32 { 1 } << i );
				auto	  it = next.find( v1 );
				if ( it == next.end() || d1 < it->second )
					next[ v1 ] = d1;
			}
		}

		if ( next.empty() )
			return { alpha, 1000000000, 0 };

		std::vector<std::pair<Vec4, u32>> items;
		items.reserve( next.size() );
		for ( const auto& kv : next )
			items.push_back( kv );

		std::sort( items.begin(), items.end(), []( const auto& lhs, const auto& rhs ) {
			const auto& a = lhs.first;
			const auto& b = rhs.first;
			if ( a.x0 != b.x0 )
				return a.x0 > b.x0;
			if ( a.x1 != b.x1 )
				return a.x1 > b.x1;
			if ( a.x2 != b.x2 )
				return a.x2 > b.x2;
			if ( a.x3 != b.x3 )
				return a.x3 > b.x3;
			return lhs.second < rhs.second;	 // python sorted reverse on -delta_prefix
		} );

		std::vector<std::pair<Vec4, u32>> kept;
		kept.reserve( items.size() );
		for ( const auto& item : items )
		{
			bool dominated = false;
			for ( const auto& kept_item : kept )
			{
				if ( dominates( kept_item.first, item.first ) )
				{
					dominated = true;
					break;
				}
			}
			if ( dominated )
				continue;

			std::vector<std::pair<Vec4, u32>> new_kept;
			new_kept.reserve( kept.size() + 1 );
			for ( const auto& kept_item : kept )
			{
				if ( !dominates( item.first, kept_item.first ) )
					new_kept.push_back( kept_item );
			}
			new_kept.push_back( item );
			kept.swap( new_kept );
		}

		candidates.clear();
		candidates.reserve( kept.size() );
		for ( const auto& kv : kept )
			candidates.emplace( kv );
	}

	u32	 best_delta = 0;
	u64	 best_count = 0;
	bool first = true;
	for ( const auto& [ vec, delta_out ] : candidates )
	{
		const u64 total = vec.x0 + vec.x1 + vec.x2 + vec.x3;
		if ( first || total > best_count || ( total == best_count && delta_out < best_delta ) )
		{
			first = false;
			best_count = total;
			best_delta = delta_out;
		}
	}

	if ( best_count == 0 )
		return { alpha, 1000000000, 0 };
	const int floor_log2 = 63 - std::countl_zero( best_count );
	const int wc_int = 32 - floor_log2;
	return { best_delta, wc_int, best_count };
}

inline std::pair<u32, int> diff_subconst_best( u32 delta_x, u32 sub_constant, ConstDiffCache& cache )
{
	const u32 add_constant = ( ~sub_constant ) + 1u;
	if ( auto cached = cache.get( delta_x, add_constant ); cached.has_value() )
	{
		return { cached->best_delta, cached->wc_int };
	}
	const auto res = diff_addconst_exact_weight_impl( delta_x, add_constant );
	cache.put( delta_x, add_constant, res );
	return { res.best_delta, res.wc_int };
}

constexpr int CROSS_XOR_ROT_R0 = 22;
constexpr int CROSS_XOR_ROT_R1 = 13;

inline int circular_distance( int left_rotation, int right_rotation, int modulus = 32 ) noexcept
{
	return std::min( ( right_rotation - left_rotation + modulus ) % modulus, ( left_rotation - right_rotation + modulus ) % modulus );
}

inline void validate_bridge_constants( int r0, int r1, int modulus = 32 )
{
	const int bridge_sum = ( r0 + r1 ) % modulus;
	const int distance = circular_distance( r0, r1, modulus );
	if ( std::gcd( bridge_sum, modulus ) != 1 )
		throw std::runtime_error( "Invalid bridge constants: gcd != 1" );
	if ( r0 == 8 || r0 == 16 || r0 == 24 || r1 == 8 || r1 == 16 || r1 == 24 )
		throw std::runtime_error( "Invalid bridge constants: forbidden positions" );
	if ( distance < 5 || distance > 11 )
		throw std::runtime_error( "Invalid bridge constants: circular distance out of range" );
	const bool span_halves = ( ( 1 <= r0 && r0 <= 15 ) && ( 17 <= r1 && r1 <= 31 ) ) || ( ( 1 <= r1 && r1 <= 15 ) && ( 17 <= r0 && r0 <= 31 ) );
	if ( !span_halves )
		throw std::runtime_error( "Invalid bridge constants: do not span halves" );
}

struct NeoRoundResult
{
	u32 A {};
	u32 B {};
	int weight_add {};
	int weight_const {};
	int weight_inj {};
};

struct AlzetteRoundResult
{
	u32 x {};
	u32 y {};
	int weight_add {};
};

inline NeoRoundResult neoalzette_round_delta( u32 delta_a, u32 delta_b, ConstDiffCache& cache )
{
	u32 A = delta_a;
	u32 B = delta_b;
	int weight_add = 0;
	int weight_const = 0;
	int weight_inj = 0;

	auto [ gamma0, w0 ] = find_optimal_gamma_with_weight( B, rotl32( A, 31 ) ^ rotl32( A, 17 ) );
	B = gamma0;
	weight_add += w0;

	auto [ new_A, wc0 ] = diff_subconst_best( A, RC[ 1 ], cache );
	A = new_A;
	weight_const += wc0;

	A ^= rotl32( B, CROSS_XOR_ROT_R0 );
	B ^= rotl32( A, CROSS_XOR_ROT_R1 );

	const u32 inj_b = g_injection_tables.injection_f_B( B );
	const int rank_b = g_injection_tables.injection_weight_branch_b( B );
	A ^= inj_b;
	weight_inj += rank_b;

	auto [ gamma1, w1 ] = find_optimal_gamma_with_weight( A, rotl32( B, 31 ) ^ rotl32( B, 17 ) );
	A = gamma1;
	weight_add += w1;

	auto [ new_B, wc1 ] = diff_subconst_best( B, RC[ 6 ], cache );
	B = new_B;
	weight_const += wc1;

	B ^= rotl32( A, CROSS_XOR_ROT_R0 );
	A ^= rotl32( B, CROSS_XOR_ROT_R1 );

	const u32 inj_a = g_injection_tables.injection_f_A( A );
	const int rank_a = g_injection_tables.injection_weight_branch_a( A );
	B ^= inj_a;
	weight_inj += rank_a;

	return { A, B, weight_add, weight_const, weight_inj };
}

inline AlzetteRoundResult alzette_round_delta( u32 delta_x, u32 delta_y )
{
	u32 x = delta_x;
	u32 y = delta_y;
	int weight_add = 0;

	auto [ g0, w0 ] = find_optimal_gamma_with_weight( x, rotr32( y, 31 ) );
	x = g0;
	weight_add += w0;
	y ^= rotr32( x, 24 );

	auto [ g1, w1 ] = find_optimal_gamma_with_weight( x, rotr32( y, 17 ) );
	x = g1;
	weight_add += w1;
	y ^= rotr32( x, 17 );

	auto [ g2, w2 ] = find_optimal_gamma_with_weight( x, y );
	x = g2;
	weight_add += w2;
	y ^= rotr32( x, 31 );

	auto [ g3, w3 ] = find_optimal_gamma_with_weight( x, rotr32( y, 24 ) );
	x = g3;
	weight_add += w3;
	y ^= rotr32( x, 16 );

	return { x, y, weight_add };
}

constexpr u64 pack_pair_key( u32 a, u32 b ) noexcept
{
	return ( static_cast<u64>( a ) << 32 ) | static_cast<u64>( b );
}

struct RoundCacheNeoValue
{
	u32 A;
	u32 B;
	int wa;
	int wc;
	int wi;
};
struct RoundCacheAlzValue
{
	u32 x;
	u32 y;
	int w;
};

void write_round_csv( const std::string& output_csv, const std::vector<std::tuple<int, double, double, double>>& rows )
{
	std::ofstream out( output_csv );
	out << "rounds,mean_weight_alzette,mean_weight_neoalzette,mean_weight_difference\n";
	out << std::fixed << std::setprecision( 15 );
	for ( const auto& [ r, ma, mn, md ] : rows )
	{
		out << r << ',' << ma << ',' << mn << ',' << md << '\n';
	}
}

void run_round_experiment( int max_rounds, u64 num_samples, u32 seed, const std::string& output_csv, bool reuse_samples, bool use_transition_cache, std::size_t const_cache_cap )
{
	std::mt19937   rng( seed );
	ConstDiffCache const_cache( const_cache_cap );

	std::vector<std::tuple<int, double, double, double>> rows;
	rows.reserve( max_rounds );

	if ( reuse_samples )
	{
		std::vector<std::pair<u32, u32>> samples;
		samples.reserve( static_cast<std::size_t>( num_samples ) );
		for ( u64 i = 0; i < num_samples; ++i )
			samples.emplace_back( rng(), rng() );

		std::vector<u64> sum_alz( max_rounds, 0 );
		std::vector<u64> sum_neo( max_rounds, 0 );

		std::unordered_map<u64, RoundCacheAlzValue> cache_alz;
		std::unordered_map<u64, RoundCacheNeoValue> cache_neo;
		if ( use_transition_cache )
		{
			cache_alz.reserve( samples.size() / 8 + 16 );
			cache_neo.reserve( samples.size() / 8 + 16 );
		}

		for ( const auto& [ da0, db0 ] : samples )
		{
			u32 da_alz = da0, db_alz = db0;
			u32 da_neo = da0, db_neo = db0;
			u64 cum_alz = 0;
			u64 cum_neo = 0;

			for ( int r = 0; r < max_rounds; ++r )
			{
				if ( use_transition_cache )
				{
					const u64 key = pack_pair_key( da_alz, db_alz );
					auto	  it = cache_alz.find( key );
					if ( it == cache_alz.end() )
					{
						const auto v = alzette_round_delta( da_alz, db_alz );
						it = cache_alz.emplace( key, RoundCacheAlzValue { v.x, v.y, v.weight_add } ).first;
					}
					da_alz = it->second.x;
					db_alz = it->second.y;
					cum_alz += it->second.w;
				}
				else
				{
					const auto v = alzette_round_delta( da_alz, db_alz );
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
						const auto v = neoalzette_round_delta( da_neo, db_neo, const_cache );
						it = cache_neo.emplace( key, RoundCacheNeoValue { v.A, v.B, v.weight_add, v.weight_const, v.weight_inj } ).first;
					}
					da_neo = it->second.A;
					db_neo = it->second.B;
					cum_neo += it->second.wa + it->second.wc + it->second.wi;
				}
				else
				{
					const auto v = neoalzette_round_delta( da_neo, db_neo, const_cache );
					da_neo = v.A;
					db_neo = v.B;
					cum_neo += v.weight_add + v.weight_const + v.weight_inj;
				}

				sum_alz[ r ] += cum_alz;
				sum_neo[ r ] += cum_neo;
			}
		}

		for ( int r = 0; r < max_rounds; ++r )
		{
			const double mean_alz = static_cast<double>( sum_alz[ r ] ) / static_cast<double>( num_samples );
			const double mean_neo = static_cast<double>( sum_neo[ r ] ) / static_cast<double>( num_samples );
			const double mean_diff = mean_neo - mean_alz;
			rows.emplace_back( r + 1, mean_alz, mean_neo, mean_diff );
			std::cout << std::fixed << std::setprecision( 3 ) << "[rounds=" << ( r + 1 ) << "] mean_alz=" << mean_alz << ", mean_neo=" << mean_neo << ", mean_diff=" << mean_diff << '\n';
		}
	}
	else
	{
		std::unordered_map<u64, RoundCacheAlzValue> cache_alz;
		std::unordered_map<u64, RoundCacheNeoValue> cache_neo;

		for ( int r = 1; r <= max_rounds; ++r )
		{
			u64 sum_alz = 0;
			u64 sum_neo = 0;
			for ( u64 i = 0; i < num_samples; ++i )
			{
				u32 da_alz = rng(), db_alz = rng();
				u32 da_neo = da_alz, db_neo = db_alz;
				u64 w_alz = 0, w_neo = 0;
				for ( int k = 0; k < r; ++k )
				{
					if ( use_transition_cache )
					{
						const u64 key = pack_pair_key( da_alz, db_alz );
						auto	  it = cache_alz.find( key );
						if ( it == cache_alz.end() )
						{
							const auto v = alzette_round_delta( da_alz, db_alz );
							it = cache_alz.emplace( key, RoundCacheAlzValue { v.x, v.y, v.weight_add } ).first;
						}
						da_alz = it->second.x;
						db_alz = it->second.y;
						w_alz += it->second.w;
					}
					else
					{
						const auto v = alzette_round_delta( da_alz, db_alz );
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
							const auto v = neoalzette_round_delta( da_neo, db_neo, const_cache );
							it = cache_neo.emplace( key, RoundCacheNeoValue { v.A, v.B, v.weight_add, v.weight_const, v.weight_inj } ).first;
						}
						da_neo = it->second.A;
						db_neo = it->second.B;
						w_neo += it->second.wa + it->second.wc + it->second.wi;
					}
					else
					{
						const auto v = neoalzette_round_delta( da_neo, db_neo, const_cache );
						da_neo = v.A;
						db_neo = v.B;
						w_neo += v.weight_add + v.weight_const + v.weight_inj;
					}
				}
				sum_alz += w_alz;
				sum_neo += w_neo;
			}
			const double mean_alz = static_cast<double>( sum_alz ) / static_cast<double>( num_samples );
			const double mean_neo = static_cast<double>( sum_neo ) / static_cast<double>( num_samples );
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

void run_experiment( u64 num_samples, u32 seed, const std::string& output_csv, bool write_csv, std::size_t const_cache_cap )
{
	std::mt19937   rng( seed );
	ConstDiffCache const_cache( const_cache_cap );
	std::ofstream  out;
	if ( write_csv )
	{
		out.open( output_csv );
		out << "delta_a,delta_b,weight_alzette,weight_neoalzette,weight_difference,weight_additions,weight_constant,weight_injection\n";
	}

	long long sum_diff = 0;
	int		  min_diff = std::numeric_limits<int>::max();
	int		  max_diff = std::numeric_limits<int>::min();

	for ( u64 i = 0; i < num_samples; ++i )
	{
		const u32  delta_a = rng();
		const u32  delta_b = rng();
		const auto alz = alzette_round_delta( delta_a, delta_b );
		const auto neo = neoalzette_round_delta( delta_a, delta_b, const_cache );
		const int  w_alz = alz.weight_add;
		const int  w_neo = neo.weight_add + neo.weight_const + neo.weight_inj;
		const int  diff = w_neo - w_alz;
		sum_diff += diff;
		min_diff = std::min( min_diff, diff );
		max_diff = std::max( max_diff, diff );

		if ( write_csv )
		{
			out << "0x" << std::hex << std::nouppercase << delta_a << ",0x" << delta_b << std::dec << ',' << std::fixed << std::setprecision( 15 ) << w_alz << ',' << w_neo << ',' << diff << ',' << neo.weight_add << ',' << neo.weight_const << ',' << neo.weight_inj << '\n';
		}
	}

	const double mean_diff = static_cast<double>( sum_diff ) / static_cast<double>( num_samples );
	std::cout << "Experiment completed for " << num_samples << " samples.\n";
	std::cout << std::fixed << std::setprecision( 3 ) << "Average weight difference (NeoAlzette - Alzette): " << mean_diff << " bits\n"
			  << "Min difference: " << min_diff << " bits, Max difference: " << max_diff << " bits\n"
			  << "matplotlib not available; skipping plot generation.\n";
	if ( out.is_open() )
	{
		out.close();
	}
}

struct Options
{
	int			max_rounds = 8;
	u64			num_samples = 80000;
	u32			seed = 0;
	bool		reuse_samples = true;
	bool		use_transition_cache = false;
	bool		skip_round = false;
	bool		skip_single = false;
	bool		write_single_csv = true;
	std::string round_csv = "results_by_round.csv";
	std::string single_csv = "results.csv";
	std::size_t const_cache_cap = 200000;
};

Options parse_args( int argc, char** argv )
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
		else if ( arg == "--help" )
		{
			std::cout << "neo_alzette_compare_plotfix_cpp\n"
					  << "  --rounds N               default 8\n"
					  << "  --samples N              default 80000\n"
					  << "  --seed S                 default 0\n"
					  << "  --reuse-samples          default ON\n"
					  << "  --fresh-samples          use fresh random samples per r\n"
					  << "  --use-transition-cache   cache state->transition for round chaining\n"
					  << "  --const-cache-cap N      default 200000 (0 disables const-diff cache)\n"
					  << "  --round-csv PATH         default results_by_round.csv\n"
					  << "  --single-csv PATH        default results.csv\n"
					  << "  --skip-round             skip multi-round trend experiment\n"
					  << "  --skip-single            skip single-box experiment\n"
					  << "  --no-single-csv          do not write per-sample CSV for single-box run\n";
			std::exit( 0 );
		}
		else
		{
			throw std::runtime_error( "Unknown argument: " + arg );
		}
	}
	return opt;
}

int main( int argc, char** argv )
{
	try
	{
		validate_bridge_constants( CROSS_XOR_ROT_R0, CROSS_XOR_ROT_R1 );
		const Options opt = parse_args( argc, argv );
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
	catch ( const std::exception& ex )
	{
		std::cerr << "[ERROR] " << ex.what() << '\n';
		return 1;
	}
}
