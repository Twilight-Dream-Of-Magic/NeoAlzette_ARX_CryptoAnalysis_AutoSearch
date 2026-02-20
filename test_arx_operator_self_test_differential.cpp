#include <bit>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>
#include <array>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include "arx_analysis_operators/differential_probability/weight_evaluation.hpp"
#include "arx_analysis_operators/differential_probability/optimal_gamma.hpp"
#include "arx_analysis_operators/differential_probability/constant_optimal_input_alpha.hpp"
#include "arx_analysis_operators/differential_probability/constant_optimal_output_beta.hpp"
#include "arx_analysis_operators/differential_probability/constant_weight_evaluation.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"



using TwilightDream::arx_operators::diff_addconst_bvweight_q4_n;
using TwilightDream::arx_operators::DiffConstExactCount;
using TwilightDream::arx_operators::find_optimal_gamma;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::xdp_add_lm2001_n;
using TwilightDream::AutoSearchFrameDefine::SearchWeight;
using TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
using TwilightDream::AutoSearchFrameDefine::display_search_weight;
using TwilightDream::AutoSearchFrameDefine::saturating_add_search_weight;

namespace
{

	struct SelfTestTemporaryFile
	{
		std::filesystem::path path;

		~SelfTestTemporaryFile()
		{
			if ( path.empty() )
			{
				return;
			}
			std::error_code ec {};
			std::filesystem::remove( path, ec );
		}
	};

	static std::filesystem::path resolve_self_test_temp_directory()
	{
		namespace fs = std::filesystem;

		std::error_code ec {};
		fs::path		temp_dir = fs::temp_directory_path( ec );
		if ( !ec && !temp_dir.empty() )
		{
			return temp_dir;
		}

#if defined( _WIN32 )
		if ( const char* temp = std::getenv( "TEMP" ); temp && *temp )
		{
			return fs::path( temp );
		}
		if ( const char* tmp = std::getenv( "TMP" ); tmp && *tmp )
		{
			return fs::path( tmp );
		}
#else
		if ( const char* tmpdir = std::getenv( "TMPDIR" ); tmpdir && *tmpdir )
		{
			return fs::path( tmpdir );
		}
		return fs::path( "/tmp" );
#endif

		return fs::current_path( ec );
	}

	static inline int floor_log2_uint64( std::uint64_t value )
	{
		// C++20 portable floor(log2(value)), returns -1 for value==0.
		return value ? ( static_cast<int>( std::bit_width( value ) ) - 1 ) : -1;
	}

	static bool is_power_of_two_uint64( std::uint64_t value )
	{
		return std::has_single_bit( value );
	}

	static std::pair<std::uint32_t, SearchWeight> brute_force_best_gamma_for_xdp_add_n( std::uint32_t alpha, std::uint32_t beta, int n )
	{
		if ( n <= 0 )
			return { 0u, 0 };
		const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
		alpha &= mask;
		beta &= mask;

		std::uint32_t best_gamma = 0u;
		SearchWeight best_w = INFINITE_WEIGHT;

		for ( std::uint32_t gamma = 0; gamma <= mask; ++gamma )
		{
			const SearchWeight w = xdp_add_lm2001_n( alpha, beta, gamma, n );
			if ( w >= INFINITE_WEIGHT )
				continue;
			if ( w < best_w )
			{
				best_w = w;
				best_gamma = gamma;
			}
		}

		// For any fixed (alpha,beta), DP+ over all gamma sums to 1 => at least one gamma is possible.
		if ( best_w >= INFINITE_WEIGHT )
			return { 0u, INFINITE_WEIGHT };
		return { best_gamma, best_w };
	}

	static std::uint64_t brute_force_addconst_count_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n )
	{
		if ( n <= 0 )
			return ( delta_x == 0u && delta_y == 0u ) ? 1ull : 0ull;
		if ( n > 16 )
		{
			// This is a brute-force helper; keep it small.
			std::cerr << "brute_force_addconst_count_n only supports n<=16\n";
			return 0ull;
		}

		const std::uint32_t mask = ( 1u << n ) - 1u;
		delta_x &= mask;
		delta_y &= mask;
		constant &= mask;

		const std::uint32_t domain_size = ( 1u << n );
		std::uint64_t		count = 0;
		for ( std::uint32_t x = 0; x < domain_size; ++x )
		{
			const std::uint32_t y0 = ( x + constant ) & mask;
			const std::uint32_t x1 = ( x ^ delta_x ) & mask;
			const std::uint32_t y1 = ( x1 + constant ) & mask;
			if ( ( ( y0 ^ y1 ) & mask ) == delta_y )
				++count;
		}
		return count;
	}

	[[maybe_unused]] static inline int parity_u32( std::uint32_t v ) noexcept
	{
		return static_cast<int>( std::popcount( v ) & 1u );
	}

	static inline int msb_index_u32( std::uint32_t v ) noexcept
	{
		return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
	}

	[[maybe_unused]] static inline std::uint32_t strip_msb_once_u32( std::uint32_t v ) noexcept
	{
		const int msb = msb_index_u32( v );
		if ( msb < 0 )
			return 0u;
		return v & ~( 1u << msb );
	}

	static int run_addconst_exact_sanity_n8_random()
	{
		using TwilightDream::arx_operators::diff_addconst_bvweight_q4_n;
		using TwilightDream::arx_operators::diff_addconst_exact_count_n;
		using TwilightDream::arx_operators::diff_addconst_exact_weight_ceil_int_n;
		using TwilightDream::arx_operators::diff_addconst_exact_weight_n;
		using TwilightDream::arx_operators::diff_addconst_weight_log2pi_n;

		constexpr int			n = 8;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		std::mt19937								 rng( 0xC0FFEEu );
		std::uniform_int_distribution<std::uint32_t> dist( 0u, mask );

		int			failures = 0;
		long double max_abs_closed_form_err = 0.0L;
		long double max_abs_q4_weight_err = 0.0L;

		// Random sampling (brute-force cost per sample is 2^n = 256 here).
		for ( int t = 0; t < 5000; ++t )
		{
			const std::uint32_t dx = dist( rng );
			const std::uint32_t k = dist( rng );
			const std::uint32_t dy = dist( rng );

			const std::uint64_t brute = brute_force_addconst_count_n( dx, k, dy, n );
			const std::uint64_t dp = diff_addconst_exact_count_n( dx, k, dy, n );
			if ( brute != dp )
			{
				std::cerr << "  [AddConstExact8] COUNT MISMATCH n=" << n << " dx=0x" << std::hex << dx << " k=0x" << k << " dy=0x" << dy << std::dec << " brute=" << brute << " dp=" << dp << "\n";
				++failures;
				if ( failures > 20 )
					break;
			}

			// Exact weight from DP count (double)
			const double w_dp = diff_addconst_exact_weight_n( dx, k, dy, n );
			const double w_log2pi = diff_addconst_weight_log2pi_n( dx, k, dy, n );
			if ( brute == 0ull )
			{
				if ( !std::isinf( w_dp ) || !std::isinf( w_log2pi ) )
				{
					std::cerr << "ARX Differential Analysis: [AddConstExact8] WEIGHT EXPECT INF but got finite: "
							  << " exact_weight_from_count=" << w_dp << " exact_weight_log2pi_closed_form=" << w_log2pi << "\n";
					++failures;
				}
			}
			else
			{
				const long double closed_form_err = std::fabs( static_cast<long double>( w_dp - w_log2pi ) );
				if ( closed_form_err > max_abs_closed_form_err )
					max_abs_closed_form_err = closed_form_err;

				// Integer ceiling weight should match the no-float formula.
				const SearchWeight w_int = diff_addconst_exact_weight_ceil_int_n( dx, k, dy, n );
				const SearchWeight expected_w_int = static_cast<SearchWeight>( n - ( static_cast<int>( std::bit_width( brute ) ) - 1 ) );
				if ( w_int != expected_w_int )
				{
					std::cerr << "ARX Differential Analysis: [AddConstExact8] INT WEIGHT MISMATCH: w_int=" << w_int << " expected=" << expected_w_int << "\n";
					++failures;
				}

				// Q4 BvWeight error (informational)
				const std::uint32_t q4 = diff_addconst_bvweight_q4_n( dx, k, dy, n );
				if ( q4 != 0xFFFFFFFFu )
				{
					const long double w_q4 = static_cast<long double>( q4 ) / 16.0L;
					const long double q4_err = std::fabs( w_q4 - static_cast<long double>( w_dp ) );
					if ( q4_err > max_abs_q4_weight_err )
						max_abs_q4_weight_err = q4_err;
				}
			}
		}

		if ( failures != 0 )
			return 1;

		std::cout << "ARX Differential Analysis: [AddConstExact8] OK: addconst exact DP matches brute-force for n=" << n << "\n";
		std::cout << "  [AddConstExact8] Max |exact_weight_from_count - exact_weight_log2pi_closed_form| = " << std::setprecision( 17 ) << static_cast<double>( max_abs_closed_form_err ) << "\n";
		std::cout << "  [AddConstExact8] Max |exact_weight_from_count - approx_weight_q4|     = " << std::setprecision( 17 ) << static_cast<double>( max_abs_q4_weight_err ) << " (informational, Q4 is approximate)\n";
		return 0;
	}

	struct Segment
	{
		int i;	// MSB index of the segment (inclusive)
		int j;	// LSB index of the segment (inclusive), with y[j]=0 as delimiter
	};

	static std::vector<Segment> make_random_segments( int n, std::mt19937& rng )
	{
		std::uniform_int_distribution<int> seg_count_dist( 1, 6 );
		std::uniform_int_distribution<int> width_dist( 1, 10 );
		std::uniform_int_distribution<int> gap_dist( 0, 2 );

		const int			 seg_count = seg_count_dist( rng );
		std::vector<Segment> segments;
		segments.reserve( static_cast<std::size_t>( seg_count ) );

		int pos = n - 1;
		for ( int t = 0; t < seg_count && pos >= 1; ++t )
		{
			const int						   max_width = ( pos >= 1 ) ? std::min( width_dist.max(), pos ) : 1;
			std::uniform_int_distribution<int> wdist( 1, max_width );
			const int						   width = wdist( rng );  // number of '1' bits in y for this segment

			const int i = pos;
			const int j = i - width;  // delimiter bit at j (y[j]=0), ones are (j+1..i)
			if ( j < 0 )
				break;

			segments.push_back( Segment { i, j } );

			pos = j - 1 - gap_dist( rng );
		}

		return segments;
	}

	static std::uint32_t build_y_from_segments( const std::vector<Segment>& segments )
	{
		std::uint32_t y = 0u;
		for ( const auto& s : segments )
		{
			for ( int bit = s.j + 1; bit <= s.i; ++bit )
				y |= ( 1u << bit );
			// s.j is delimiter => keep 0
		}
		return y;
	}

	static std::uint32_t build_x_from_segments( const std::vector<Segment>& segments, std::mt19937& rng )
	{
		std::uint32_t x = 0u;
		for ( const auto& s : segments )
		{
			const int data_width = s.i - s.j;  // bits (s.j+1 .. s.i)
			if ( data_width <= 0 )
				continue;

			const std::uint32_t							 max_val = ( data_width >= 31 ) ? 0x7FFFFFFFu : ( ( 1u << data_width ) - 1u );
			std::uniform_int_distribution<std::uint32_t> val_dist( 1u, max_val );
			const std::uint32_t							 val = val_dist( rng );

			for ( int k = 0; k < data_width; ++k )
			{
				if ( ( val >> k ) & 1u )
					x |= ( 1u << ( s.j + 1 + k ) );
			}
			// Keep delimiter bit 0 for clarity.
		}
		return x;
	}

	static std::uint32_t direct_sum_floor_log2_segments( std::uint32_t x, const std::vector<Segment>& segments )
	{
		std::uint32_t sum = 0u;
		for ( const auto& s : segments )
		{
			// Find MSB within [j..i], then floor(log2(x[i,j])) = msb_pos - j
			int msb = -1;
			for ( int bit = s.i; bit >= s.j; --bit )
			{
				if ( ( x >> bit ) & 1u )
				{
					msb = bit;
					break;
				}
			}
			if ( msb < 0 )
				return 0xFFFFFFFFu;	 // invalid (should not happen due to generation)
			sum += static_cast<std::uint32_t>( msb - s.j );
		}
		return sum;
	}

	static std::uint32_t direct_sum_truncate_segments( std::uint32_t x, const std::vector<Segment>& segments )
	{
		std::uint32_t sum = 0u;
		for ( const auto& s : segments )
		{
			// Truncate(x[i_t, j_t+1]) returns the 4 MSB bits of the sub-vector (padding zeros if width<4).
			std::uint32_t t = 0u;
			for ( int lambda = 0; lambda < 4; ++lambda )
			{
				const int			bit = s.i - lambda;
				const std::uint32_t b = ( bit > s.j ) ? ( ( x >> bit ) & 1u ) : 0u;
				t |= b << ( 3 - lambda );
			}
			sum += t;
		}
		return sum;
	}

	static int parallel_log_trunc_run_once( int n, std::mt19937& rng )
	{
		const auto			segments = make_random_segments( n, rng );
		const std::uint32_t y = build_y_from_segments( segments );
		const std::uint32_t x = build_x_from_segments( segments, rng );

		const std::uint32_t direct_log = direct_sum_floor_log2_segments( x, segments );
		const std::uint32_t direct_trunc = direct_sum_truncate_segments( x, segments );
		if ( direct_log == 0xFFFFFFFFu )
			return 0;  // skip (should be rare)

		const std::uint32_t par_log = ( n == 32 ) ? TwilightDream::bitvector::ParallelLog( x, y ) : TwilightDream::bitvector::ParallelLog_n( x, y, n );
		const std::uint32_t par_trunc = ( n == 32 ) ? TwilightDream::bitvector::ParallelTrunc( x, y ) : TwilightDream::bitvector::ParallelTrunc_n( x, y, n );

		if ( par_log != direct_log || par_trunc != direct_trunc )
		{
			std::printf( "ARX Differential Analysis: [ParallelLog/Trunc] Mismatch (n=%d)\n", n );
			std::printf( "    x=0x%08X y=0x%08X\n", x, y );
			std::printf( "    ParallelLog=%u direct_log=%u\n", par_log, direct_log );
			std::printf( "    ParallelTrunc=%u direct_trunc=%u\n", par_trunc, direct_trunc );
			std::printf( "    segments:\n" );
			for ( const auto& s : segments )
				std::printf( "      [i=%d, j=%d]\n", s.i, s.j );
			return 1;
		}

		return 0;
	}

	static int run_parallel_log_trunc_sanity()
	{
		std::mt19937 rng( 0xA11CEu );
		for ( int t = 0; t < 20000; ++t )
		{
			// Mix n=32 (native) and a smaller n (wrapper)
			if ( parallel_log_trunc_run_once( 32, rng ) != 0 )
				return 1;
			if ( parallel_log_trunc_run_once( 13, rng ) != 0 )
				return 1;
		}
		return 0;
	}

	static std::uint64_t diffconst_mask_n( int n ) noexcept
	{
		return ( n >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << n ) - 1ull );
	}

	static std::string diffconst_exact_count_to_text( const DiffConstExactCount& exact_count )
	{
		if ( exact_count.high64() == 0ull )
			return std::to_string( exact_count.low64() );

		std::ostringstream out;
		out << "0x" << std::hex << exact_count.high64() << "_" << std::setw( 16 ) << std::setfill( '0' ) << exact_count.low64() << std::dec;
		return out.str();
	}

	static bool diffconst_probability_close( double lhs, double rhs ) noexcept
	{
		return std::abs( lhs - rhs ) <= 1e-15;
	}

	static bool diffconst_weight_close( double lhs, double rhs ) noexcept
	{
		if ( std::isinf( lhs ) || std::isinf( rhs ) )
			return std::isinf( lhs ) && std::isinf( rhs );
		return std::abs( lhs - rhs ) <= 1e-12;
	}

	static TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult make_q1_verified_fixed_input_result(
		bool is_sub,
		std::uint64_t delta_x,
		std::uint64_t constant,
		std::uint64_t delta_y,
		int n ) noexcept
	{
		TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult result {};
		if ( n <= 0 || n > 64 )
			return result;

		const std::uint64_t mask = diffconst_mask_n( n );
		const std::uint64_t input_difference = delta_x & mask;
		const std::uint64_t output_difference = delta_y & mask;
		const DiffConstExactCount exact_count =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_count_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_count_n( input_difference, constant, output_difference, n );

		result.delta_y = output_difference;
		result.carry_difference = ( input_difference ^ output_difference ) & mask;
		result.exact_count = exact_count;
		result.is_possible = ( exact_count != 0ull );
		if ( exact_count == 0ull )
		{
			result.exact_probability = 0.0;
			result.exact_weight = std::numeric_limits<double>::infinity();
			result.exact_weight_ceil_int = INFINITE_WEIGHT;
			return result;
		}

		result.exact_probability =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_probability_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_probability_n( input_difference, constant, output_difference, n );
		result.exact_weight =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_weight_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_weight_n( input_difference, constant, output_difference, n );
		result.exact_weight_ceil_int =
			static_cast<SearchWeight>( n - ( exact_count.bit_width() - 1 ) );
		return result;
	}

	static TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult make_q1_verified_fixed_output_result(
		bool is_sub,
		std::uint64_t delta_y,
		std::uint64_t constant,
		std::uint64_t delta_x,
		int n ) noexcept
	{
		TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult result {};
		if ( n <= 0 || n > 64 )
			return result;

		const std::uint64_t mask = diffconst_mask_n( n );
		const std::uint64_t input_difference = delta_x & mask;
		const std::uint64_t output_difference = delta_y & mask;
		const DiffConstExactCount exact_count =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_count_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_count_n( input_difference, constant, output_difference, n );

		result.delta_x = input_difference;
		result.carry_difference = ( input_difference ^ output_difference ) & mask;
		result.exact_count = exact_count;
		result.is_possible = ( exact_count != 0ull );
		if ( exact_count == 0ull )
		{
			result.exact_probability = 0.0;
			result.exact_weight = std::numeric_limits<double>::infinity();
			result.exact_weight_ceil_int = INFINITE_WEIGHT;
			return result;
		}

		result.exact_probability =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_probability_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_probability_n( input_difference, constant, output_difference, n );
		result.exact_weight =
			is_sub
				? TwilightDream::arx_operators::diff_subconst_exact_weight_n( input_difference, constant, output_difference, n )
				: TwilightDream::arx_operators::diff_addconst_exact_weight_n( input_difference, constant, output_difference, n );
		result.exact_weight_ceil_int =
			static_cast<SearchWeight>( n - ( exact_count.bit_width() - 1 ) );
		return result;
	}

	static TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult brute_force_best_fixed_input_result(
		bool is_sub,
		std::uint64_t delta_x,
		std::uint64_t constant,
		int n ) noexcept
	{
		TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult best {};
		if ( n <= 0 || n > 64 )
			return best;

		const std::uint64_t mask = diffconst_mask_n( n );
		const std::uint64_t limit = mask + 1ull;
		bool has_best = false;
		DiffConstExactCount best_exact_count {};
		std::uint64_t best_delta_y = 0ull;
		for ( std::uint64_t delta_y = 0; delta_y < limit; ++delta_y )
		{
			const DiffConstExactCount exact_count =
				is_sub
					? TwilightDream::arx_operators::diff_subconst_exact_count_n( delta_x, constant, delta_y, n )
					: TwilightDream::arx_operators::diff_addconst_exact_count_n( delta_x, constant, delta_y, n );
			if ( exact_count == 0ull )
				continue;
			if ( !has_best || exact_count > best_exact_count || ( exact_count == best_exact_count && delta_y < best_delta_y ) )
			{
				has_best = true;
				best_exact_count = exact_count;
				best_delta_y = delta_y;
			}
		}

		if ( !has_best )
			return best;
		return make_q1_verified_fixed_input_result( is_sub, delta_x, constant, best_delta_y, n );
	}

	static TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult brute_force_best_fixed_output_result(
		bool is_sub,
		std::uint64_t delta_y,
		std::uint64_t constant,
		int n ) noexcept
	{
		TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult best {};
		if ( n <= 0 || n > 64 )
			return best;

		const std::uint64_t mask = diffconst_mask_n( n );
		const std::uint64_t limit = mask + 1ull;
		bool has_best = false;
		DiffConstExactCount best_exact_count {};
		std::uint64_t best_delta_x = 0ull;
		for ( std::uint64_t delta_x = 0; delta_x < limit; ++delta_x )
		{
			const DiffConstExactCount exact_count =
				is_sub
					? TwilightDream::arx_operators::diff_subconst_exact_count_n( delta_x, constant, delta_y, n )
					: TwilightDream::arx_operators::diff_addconst_exact_count_n( delta_x, constant, delta_y, n );
			if ( exact_count == 0ull )
				continue;
			if ( !has_best || exact_count > best_exact_count || ( exact_count == best_exact_count && delta_x < best_delta_x ) )
			{
				has_best = true;
				best_exact_count = exact_count;
				best_delta_x = delta_x;
			}
		}

		if ( !has_best )
			return best;
		return make_q1_verified_fixed_output_result( is_sub, delta_y, constant, best_delta_x, n );
	}

	static bool fixed_input_results_match(
		const TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult& lhs,
		const TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult& rhs ) noexcept
	{
		return lhs.delta_y == rhs.delta_y &&
			   lhs.carry_difference == rhs.carry_difference &&
			   lhs.exact_count == rhs.exact_count &&
			   diffconst_probability_close( lhs.exact_probability, rhs.exact_probability ) &&
			   diffconst_weight_close( lhs.exact_weight, rhs.exact_weight ) &&
			   lhs.exact_weight_ceil_int == rhs.exact_weight_ceil_int &&
			   lhs.is_possible == rhs.is_possible;
	}

	static bool fixed_output_results_match(
		const TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult& lhs,
		const TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult& rhs ) noexcept
	{
		return lhs.delta_x == rhs.delta_x &&
			   lhs.carry_difference == rhs.carry_difference &&
			   lhs.exact_count == rhs.exact_count &&
			   diffconst_probability_close( lhs.exact_probability, rhs.exact_probability ) &&
			   diffconst_weight_close( lhs.exact_weight, rhs.exact_weight ) &&
			   lhs.exact_weight_ceil_int == rhs.exact_weight_ceil_int &&
			   lhs.is_possible == rhs.is_possible;
	}

	static void print_fixed_input_result_debug(
		const char* label,
		const TwilightDream::arx_operators::DiffConstOptimalOutputDeltaResult& result )
	{
		std::cerr
			<< "  " << label
			<< " delta_y=0x" << std::hex << result.delta_y
			<< " carry_difference=0x" << result.carry_difference
			<< " exact_count=" << diffconst_exact_count_to_text( result.exact_count )
			<< std::dec
			<< " exact_probability=" << result.exact_probability
			<< " exact_weight=" << result.exact_weight
			<< " exact_weight_ceil_int=" << result.exact_weight_ceil_int
			<< " is_possible=" << ( result.is_possible ? "true" : "false" )
			<< "\n";
	}

	static void print_fixed_output_result_debug(
		const char* label,
		const TwilightDream::arx_operators::DiffConstOptimalInputDeltaResult& result )
	{
		std::cerr
			<< "  " << label
			<< " delta_x=0x" << std::hex << result.delta_x
			<< " carry_difference=0x" << result.carry_difference
			<< " exact_count=" << diffconst_exact_count_to_text( result.exact_count )
			<< std::dec
			<< " exact_probability=" << result.exact_probability
			<< " exact_weight=" << result.exact_weight
			<< " exact_weight_ceil_int=" << result.exact_weight_ceil_int
			<< " is_possible=" << ( result.is_possible ? "true" : "false" )
			<< "\n";
	}

	static int run_diffconst_varconst_q2_closure_self_test()
	{
		using TwilightDream::arx_operators::DiffConstFixedInputPrototypeSupportConfig;
		using TwilightDream::arx_operators::DiffConstFixedInputQ2Stats;
		using TwilightDream::arx_operators::DiffConstFixedOutputQ2Stats;
		using TwilightDream::arx_operators::find_optimal_input_delta_addconst_exact_reference;
		using TwilightDream::arx_operators::find_optimal_input_delta_subconst_exact_reference;
		using TwilightDream::arx_operators::find_optimal_output_delta_addconst;
		using TwilightDream::arx_operators::find_optimal_output_delta_addconst_exact_reference;
		using TwilightDream::arx_operators::find_optimal_output_delta_subconst;
		using TwilightDream::arx_operators::find_optimal_output_delta_subconst_exact_reference;

		DiffConstFixedInputPrototypeSupportConfig prototype_config {};
		std::uint64_t fixed_input_cases = 0ull;
		std::uint64_t fixed_output_cases = 0ull;

		for ( int n = 1; n <= 6; ++n )
		{
			const std::uint64_t mask = diffconst_mask_n( n );
			const std::uint64_t limit = mask + 1ull;

			for ( std::uint64_t constant = 0; constant < limit; ++constant )
			{
				for ( std::uint64_t delta_x = 0; delta_x < limit; ++delta_x )
				{
					for ( int op_index = 0; op_index < 2; ++op_index )
					{
						const bool is_sub = ( op_index != 0 );
						DiffConstFixedInputQ2Stats reference_stats {};
						DiffConstFixedInputQ2Stats prototype_stats {};
						const auto reference_result =
							is_sub
								? find_optimal_output_delta_subconst_exact_reference( delta_x, constant, n, &reference_stats )
								: find_optimal_output_delta_addconst_exact_reference( delta_x, constant, n, &reference_stats );
						const auto prototype_result =
							is_sub
								? find_optimal_output_delta_subconst( delta_x, constant, n, prototype_config, &prototype_stats )
								: find_optimal_output_delta_addconst( delta_x, constant, n, prototype_config, &prototype_stats );
						const auto reference_q1_verified =
							make_q1_verified_fixed_input_result( is_sub, delta_x, constant, reference_result.delta_y, n );
						const auto prototype_q1_verified =
							make_q1_verified_fixed_input_result( is_sub, delta_x, constant, prototype_result.delta_y, n );
						const auto brute_force_gold =
							brute_force_best_fixed_input_result( is_sub, delta_x, constant, n );

						if ( !fixed_input_results_match( reference_result, reference_q1_verified ) ||
							 !fixed_input_results_match( prototype_result, prototype_q1_verified ) ||
							 !fixed_input_results_match( reference_result, brute_force_gold ) ||
							 !fixed_input_results_match( prototype_result, brute_force_gold ) ||
							 !prototype_stats.used_phase8_banded_support_prototype )
						{
							std::cerr
								<< "ARX Differential Analysis: [ConstQ2ExactClosure][fixed-input] FAIL"
								<< " op=" << ( is_sub ? "sub" : "add" )
								<< " n=" << n
								<< " constant=0x" << std::hex << constant
								<< " delta_x=0x" << delta_x
								<< std::dec
								<< " support_used=" << ( prototype_stats.used_phase8_banded_support_prototype ? 1 : 0 )
								<< " ref_peak=" << reference_stats.frontier_peak_entries
								<< " proto_peak=" << prototype_stats.frontier_peak_entries
								<< " proto_proposals=" << prototype_stats.prototype_transition_proposals
								<< " proto_hits=" << prototype_stats.prototype_transition_hits
								<< " proto_fallbacks=" << prototype_stats.prototype_transition_fallbacks
								<< "\n";
							print_fixed_input_result_debug( "gold", brute_force_gold );
							print_fixed_input_result_debug( "reference", reference_result );
							print_fixed_input_result_debug( "reference_q1", reference_q1_verified );
							print_fixed_input_result_debug( "prototype", prototype_result );
							print_fixed_input_result_debug( "prototype_q1", prototype_q1_verified );
							return 15;
						}

						++fixed_input_cases;
					}
				}

				for ( std::uint64_t delta_y = 0; delta_y < limit; ++delta_y )
				{
					for ( int op_index = 0; op_index < 2; ++op_index )
					{
						const bool is_sub = ( op_index != 0 );
						DiffConstFixedOutputQ2Stats stats {};
						const auto reference_result =
							is_sub
								? find_optimal_input_delta_subconst_exact_reference( delta_y, constant, n, &stats )
								: find_optimal_input_delta_addconst_exact_reference( delta_y, constant, n, &stats );
						const auto reference_q1_verified =
							make_q1_verified_fixed_output_result( is_sub, delta_y, constant, reference_result.delta_x, n );
						const auto brute_force_gold =
							brute_force_best_fixed_output_result( is_sub, delta_y, constant, n );

						if ( !fixed_output_results_match( reference_result, reference_q1_verified ) ||
							 !fixed_output_results_match( reference_result, brute_force_gold ) )
						{
							std::cerr
								<< "ARX Differential Analysis: [ConstQ2ExactClosure][fixed-output] FAIL"
								<< " op=" << ( is_sub ? "sub" : "add" )
								<< " n=" << n
								<< " constant=0x" << std::hex << constant
								<< " delta_y=0x" << delta_y
								<< std::dec
								<< " peak_frontier=" << stats.frontier_peak_entries
								<< " transition_evaluations=" << stats.transition_evaluations
								<< "\n";
							print_fixed_output_result_debug( "gold", brute_force_gold );
							print_fixed_output_result_debug( "reference", reference_result );
							print_fixed_output_result_debug( "reference_q1", reference_q1_verified );
							return 16;
						}

						++fixed_output_cases;
					}
				}
			}
		}

		std::cout
			<< "ARX Differential Analysis: [ConstQ2ExactClosure] PASS"
			<< " fixed_input_cases=" << fixed_input_cases
			<< " fixed_output_cases=" << fixed_output_cases
			<< " max_n=6"
			<< "\n";
		return 0;
	}

}  // namespace


int run_differential_search_self_test( std::uint64_t seed, std::size_t extra_cases );

int run_arx_operator_self_test()
{
    std::cout << "[SelfTest] ARX analysis operators validation\n";
	std::cout << "[SelfTest][Section] Operator Layer\n";

	// ------------------------------------------------------------
	// 1) Azimi et al. (DCC 2022) Example 3:
	//    - BvWeight(u,v,a) = 28 (Q4)  => apxweight = 28/16 = 1.75
	//    - exact DP = 5/16            => weight = -log2(5/16) ≈ 1.678
	// From papers_txt/... line ~2091.
	// ------------------------------------------------------------
	{
		constexpr int		n = 10;
		const std::uint32_t u = 0x28Eu;	 // delta_x
		const std::uint32_t v = 0x28Au;	 // delta_y
		const std::uint32_t a = 0x22Eu;	 // constant

		const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( u, a, v, n );
		std::cout << "  [BvWeight] expected_q4=28, got_q4=" << bvweight_q4 << "\n";
		if ( bvweight_q4 != 28u )
		{
			// If this fails, print a small diagnostic by exploring a few plausible OCR-decoding variants
			// for Lemma 6 (w-construction). This makes it easier to pin down symbol/shift ambiguities.
			const std::uint32_t mask = ( 1u << n ) - 1u;
			const std::uint32_t s000 = ( ~( u << 1 ) & ~( v << 1 ) ) & mask;
			const std::uint32_t s000_hat = s000 & ~TwilightDream::bitvector::LeadingZeros_n( ( ~s000 ) & mask, n );
			const std::uint32_t t0 = ( ~s000_hat & ( ( s000 << 1 ) & mask ) ) & mask;
			const std::uint32_t t1 = ( s000_hat & ~( ( s000 << 1 ) & mask ) ) & mask;

			const std::uint32_t s1 = ( ( a << 1 ) & mask ) & t0;
			const std::uint32_t s2 = a & ( ( s000_hat << 1 ) & mask );
			const std::uint32_t qbase = ( ~( ( ( ( a << 1 ) & mask ) ^ ( u & mask ) ^ ( v & mask ) ) ) ) & mask;

			// Baseline reconstruction using the current header formula (for easy comparison).
			{
				const std::uint32_t s_header = ( ( ( a << 1 ) & t0 ) ^ ( a & ( ( s000 << 1 ) & mask ) ) ) & mask;
				std::uint32_t		q_header = qbase & t1;
				const std::uint32_t d_header = ( TwilightDream::bitvector::RevCarry_n( ( ( ( s000_hat << 1 ) & t1 ) & mask ), q_header, n ) | q_header ) & mask;
				const std::uint32_t w_header = ( ( q_header - ( s_header & d_header ) ) | ( s_header & ~d_header ) ) & mask;
				std::cout << "  [BvWeight] baseline(header-like): w=0x" << std::hex << w_header << std::dec << "\n";
			}

			struct CombineOp
			{
				const char* name;
				std::uint32_t ( *fn )( std::uint32_t, std::uint32_t, std::uint32_t );
			};

			struct MaskVariant
			{
				const char*	  name;
				std::uint32_t value;
			};
			const MaskVariant q_masks[] = {
				{ "(none)", mask }, { "t0", t0 }, { "t1", t1 }, { "t0>>1", ( t0 >> 1 ) & mask }, { "t1>>1", ( t1 >> 1 ) & mask }, { "t0<<1", ( t0 << 1 ) & mask }, { "t1<<1", ( t1 << 1 ) & mask },
			};

			const CombineOp s_ops[] = {
				{ "xor",
				  +[]( std::uint32_t x, std::uint32_t y, std::uint32_t m ) {
					  return ( x ^ y ) & m;
				  } },
				{ "add",
				  +[]( std::uint32_t x, std::uint32_t y, std::uint32_t m ) {
					  return ( x + y ) & m;
				  } },
			};

			auto compute_bvweight_from_w = [ & ]( std::uint32_t w_value ) -> std::uint32_t {
				const std::uint32_t hw_uv = TwilightDream::bitvector::HammingWeight( ( ( u ^ v ) << 1 ) & mask );
				const std::uint32_t hw_s000hat = TwilightDream::bitvector::HammingWeight( s000_hat );
				const std::uint32_t parlog = TwilightDream::bitvector::ParallelLog_n( ( ( w_value & s000_hat ) << 1 ) & mask, ( s000_hat << 1 ) & mask, n );
				const std::uint32_t int_part = ( hw_uv + hw_s000hat ) - parlog;
				const std::uint32_t revcarry = TwilightDream::bitvector::RevCarry_n( ( ( w_value & s000_hat ) << 1 ) & mask, ( s000_hat << 1 ) & mask, n );
				const std::uint32_t frac = TwilightDream::bitvector::ParallelTrunc_n( ( w_value << 1 ) & mask, revcarry & mask, n );
				return ( int_part << 4 ) - ( frac & 0xFu );
			};

			// Sanity: Table 4 states w = 0001010010 (0x052) and BvWeight = 28.
			{
				const std::uint32_t w_known = 0x052u;
				const std::uint32_t hw_uv = TwilightDream::bitvector::HammingWeight( ( ( u ^ v ) << 1 ) & mask );
				const std::uint32_t hw_s000hat = TwilightDream::bitvector::HammingWeight( s000_hat );
				const std::uint32_t parlog = TwilightDream::bitvector::ParallelLog_n( ( ( w_known & s000_hat ) << 1 ) & mask, ( s000_hat << 1 ) & mask, n );
				const std::uint32_t int_part = ( hw_uv + hw_s000hat ) - parlog;
				const std::uint32_t revcarry = TwilightDream::bitvector::RevCarry_n( ( ( w_known & s000_hat ) << 1 ) & mask, ( s000_hat << 1 ) & mask, n );
				const std::uint32_t frac = TwilightDream::bitvector::ParallelTrunc_n( ( w_known << 1 ) & mask, revcarry & mask, n );
				const std::uint32_t bv_q4 = ( int_part << 4 ) - ( frac & 0xFu );
				std::cout << "  [BvWeight] diagnostic(known w=0x052): hw_uv=" << hw_uv << " hw_s000hat=" << hw_s000hat << " parlog=" << parlog << " int=" << int_part << " frac=" << ( frac & 0xFu ) << " bv_q4=" << bv_q4 << "\n";
				std::cout << "  [BvWeight] s000=0x" << std::hex << s000 << " s000_hat=0x" << s000_hat << " (w&s000_hat)<<1=0x" << ( ( ( w_known & s000_hat ) << 1 ) & mask ) << " s000_hat<<1=0x" << ( ( s000_hat << 1 ) & mask ) << " RevCarry=0x" << ( revcarry & mask ) << std::dec << "\n";

				// Detailed fraction components for debugging Proposition 1(b) mapping.
				const std::uint32_t x_val = ( w_known << 1 ) & mask;
				const std::uint32_t y_val = revcarry & mask;
				const std::uint32_t y1 = ( y_val << 1 ) & mask;
				const std::uint32_t y2 = ( y_val << 2 ) & mask;
				const std::uint32_t y3 = ( y_val << 3 ) & mask;
				const std::uint32_t y4 = ( y_val << 4 ) & mask;
				const std::uint32_t z0 = x_val & y_val & ~y1;
				const std::uint32_t z1 = x_val & y1 & ~y2;
				const std::uint32_t z2 = x_val & y2 & ~y3;
				const std::uint32_t z3 = x_val & y3 & ~y4;
				const std::uint32_t hw0 = TwilightDream::bitvector::HammingWeight( z0 );
				const std::uint32_t hw1 = TwilightDream::bitvector::HammingWeight( z1 );
				const std::uint32_t hw2 = TwilightDream::bitvector::HammingWeight( z2 );
				const std::uint32_t hw3 = TwilightDream::bitvector::HammingWeight( z3 );
				const std::uint32_t frac_xor = ( hw0 ) ^ ( hw1 << 2 ) ^ ( hw2 << 1 ) ^ ( hw3 );
				const std::uint32_t frac_add = ( hw0 ) + ( hw1 << 2 ) + ( hw2 << 1 ) + ( hw3 );
				std::cout << "  [BvWeight] frac-debug hex: x=0x" << std::hex << x_val << " y=0x" << y_val << " z0=0x" << z0 << " z1=0x" << z1 << " z2=0x" << z2 << " z3=0x" << z3 << std::dec << "\n";
				std::cout << "  [BvWeight] frac-components: hw0=" << hw0 << " hw1=" << hw1 << " hw2=" << hw2 << " hw3=" << hw3 << " frac_xor=" << ( frac_xor & 0xFu ) << " frac_add=" << ( frac_add & 0xFu ) << "\n";
			}

			bool found = false;
			for ( const auto& s_op : s_ops )
			{
				const std::uint32_t s_value = s_op.fn( s1, s2, mask );
				for ( const auto& q_mask : q_masks )
				{
					for ( int q_shift = 0; q_shift <= 1; ++q_shift )
					{
						const std::uint32_t q_value = q_shift ? ( ( ( qbase << 1 ) & mask ) & q_mask.value ) : ( qbase & q_mask.value );

						// d variants: try a small set of plausible first-arg masks around s000_hat and t0/t1
						const std::uint32_t d_inputs[] = {
							( ( s000_hat << 1 ) & t0 ) & mask, ( ( s000_hat << 1 ) & t1 ) & mask, ( ( s000_hat << 1 ) ) & mask, ( t0 )&mask, ( t1 )&mask,
						};
						const char* d_input_names[] = {
							"(s000_hat<<1)&t0", "(s000_hat<<1)&t1", "(s000_hat<<1)", "t0", "t1",
						};

						for ( int di = 0; di < 5; ++di )
						{
							const std::uint32_t d_input = d_inputs[ di ];
							const std::uint32_t d_value = ( TwilightDream::bitvector::RevCarry_n( d_input, q_value, n ) | q_value ) & mask;

							const std::uint32_t w_value = ( ( q_value - ( s_value & d_value ) ) | ( s_value & ~d_value ) ) & mask;
							const std::uint32_t bv_q4 = compute_bvweight_from_w( w_value );
							if ( w_value == 0x052u && bv_q4 == 28u )
							{
								std::cout << "  [BvWeight] Found matching variant:\n";
								std::cout << "    s_op=" << s_op.name << "  q_mask=" << q_mask.name << "  q_shift=" << ( q_shift ? "left1" : "none" ) << "  d_input=" << d_input_names[ di ] << "\n";
								found = true;
								break;
							}
						}
						if ( found )
							break;
					}
					if ( found )
						break;
				}
				if ( found )
					break;
			}
			if ( !found )
			{
				std::cout << "ARX Differential Analysis: [BvWeight] No matching variant found in the small search set.\n";
			}

			std::cout << "ARX Differential Analysis: [BvWeight] FAIL\n";
			return 1;
		}

		// Exact reference: DP = 5/16, hence count = (5/16)*2^10 = 320.
		const std::uint64_t exact_count = TwilightDream::arx_operators::diff_addconst_exact_count_n( u, a, v, n );
		std::cout << " [ExactDP] expected_count=320, got_count=" << exact_count << "\n";
		if ( exact_count != 320ull )
		{
			std::cout << "ARX Differential Analysis: [ExactDP] FAIL\n";
			return 1;
		}

		const double exact_probability = TwilightDream::arx_operators::diff_addconst_exact_probability_n( u, a, v, n );
		std::cout << " [ExactDP] expected_DP=0.3125, got_DP=" << std::setprecision( 16 ) << exact_probability << "\n";
		if ( exact_probability != 0.3125 )
		{
			std::cout << "ARX Differential Analysis: [ExactDP] FAIL\n";
			return 1;
		}

		const double expected_weight = -std::log2( 0.3125 );
		const double exact_weight = TwilightDream::arx_operators::diff_addconst_exact_weight_n( u, a, v, n );
		const double exact_weight_log2pi = TwilightDream::arx_operators::diff_addconst_weight_log2pi_n( u, a, v, n );
		std::cout << " [ExactWeight] exact_weight=" << std::setprecision( 16 ) << exact_weight << " exact_weight_log2pi_closed_form=" << exact_weight_log2pi << " expected=" << expected_weight << "\n";
		if ( std::abs( exact_weight - expected_weight ) > 1e-12 )
		{
			std::cout << "ARX Differential Analysis: [ExactWeight] FAIL (count-DP)\n";
			return 1;
		}
		if ( std::abs( exact_weight_log2pi - expected_weight ) > 1e-12 )
		{
			std::cout << "ARX Differential Analysis: [ExactWeight] FAIL (Azimi closed-form log2pi)\n";
			return 1;
		}

		std::cout << "ARX Differential Analysis: [BvWeight] PASS\n";
	}

	// ------------------------------------------------------------
	// 2) Machado exact DP baseline: exhaustive check for n=4
	// Verify diff_addconst_exact_count_n against brute force enumeration of x, for all (Δx, K, Δy).
	// Also cross-check:
	//   - is_diff_addconst_possible_n  <=>  count != 0
	//   - exact_weight(count) == Azimi exact closed-form weight_log2pi when count != 0
	// Reference:
	//   - Machado, "Differential Probability of Modular Addition with a Constant Operand" (ePrint 2001/052)
	//   - Azimi et al. restate an equivalent recursion as Theorem 2 (DCC 2022 / ASIACRYPT 2020 extended)
	// ------------------------------------------------------------
	{
		constexpr int			n = 4;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t constant = 0; constant <= mask; ++constant )
		{
			for ( std::uint32_t input_difference = 0; input_difference <= mask; ++input_difference )
			{
				std::array<std::uint64_t, 16> brute_counts {};
				brute_counts.fill( 0 );

				for ( std::uint32_t x = 0; x <= mask; ++x )
				{
					const std::uint32_t y = ( x + constant ) & mask;
					const std::uint32_t y_star = ( ( x ^ input_difference ) + constant ) & mask;
					const std::uint32_t output_difference = ( y ^ y_star ) & mask;
					brute_counts[ size_t( output_difference ) ]++;
				}

				for ( std::uint32_t output_difference = 0; output_difference <= mask; ++output_difference )
				{
					const std::uint64_t dp_count = TwilightDream::arx_operators::diff_addconst_exact_count_n( input_difference, constant, output_difference, n );
					const std::uint64_t brute_count = brute_counts[ size_t( output_difference ) ];
					if ( dp_count != brute_count )
					{
						std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 input_difference=" << input_difference << " constant=" << constant << " output_difference=" << output_difference << " expected_count=" << brute_count << " got_count=" << dp_count << "\n";
						return 5;
					}

					const bool possible = TwilightDream::arx_operators::is_diff_addconst_possible_n( input_difference, constant, output_difference, n );
					if ( possible != ( dp_count != 0ull ) )
					{
						std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 input_difference=" << input_difference << " constant=" << constant << " output_difference=" << output_difference << " possible=" << ( possible ? 1 : 0 ) << " count=" << dp_count << "\n";
						return 6;
					}

					const double exact_weight = TwilightDream::arx_operators::diff_addconst_exact_weight_n( input_difference, constant, output_difference, n );
					const double weight_log2pi = TwilightDream::arx_operators::diff_addconst_weight_log2pi_n( input_difference, constant, output_difference, n );
					if ( dp_count == 0ull )
					{
						if ( !std::isinf( exact_weight ) || !std::isinf( weight_log2pi ) )
						{
							std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 expected +inf exact weights for impossible case\n";
							return 7;
						}
					}
					else
					{
						if ( std::abs( exact_weight - weight_log2pi ) > 1e-12 )
						{
							std::cout << "ARX Differential Analysis: [AddConstExact] FAIL n=4 weight mismatch input_difference=" << input_difference << " constant=" << constant << " output_difference=" << output_difference << " exact_weight_from_count=" << std::setprecision( 16 ) << exact_weight << " exact_weight_log2pi_closed_form=" << weight_log2pi << "\n";
							return 8;
						}
					}
				}
			}
		}

		std::cout << "ARX Differential Analysis: [AddConstExact] PASS (n=4 exhaustive)\n";
	}

	// ------------------------------------------------------------
	// 3) LM2001 xdp_add_lm2001_n brute-force check for n=4
	// Verify: count == 0 <=> weight == INFINITE_WEIGHT, else weight == 2n - log2(count).
	// ------------------------------------------------------------
	{
		constexpr int			n = 4;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
		{
			for ( std::uint32_t beta = 0; beta <= mask; ++beta )
			{
				std::array<std::uint64_t, 16> counts {};
				counts.fill( 0 );

				for ( std::uint32_t x = 0; x <= mask; ++x )
				{
					const std::uint32_t x_star = x ^ alpha;
					for ( std::uint32_t y = 0; y <= mask; ++y )
					{
						const std::uint32_t y_star = y ^ beta;
						const std::uint32_t z = ( x + y ) & mask;
						const std::uint32_t z_star = ( x_star + y_star ) & mask;
						const std::uint32_t gamma = ( z ^ z_star ) & mask;
						counts[ size_t( gamma ) ]++;
					}
				}

				for ( std::uint32_t gamma = 0; gamma <= mask; ++gamma )
				{
					const std::uint64_t count = counts[ size_t( gamma ) ];
					const SearchWeight computed_weight = xdp_add_lm2001_n( alpha, beta, gamma, n );

					if ( count == 0 )
					{
						if ( computed_weight != INFINITE_WEIGHT )
						{
							std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma << " expected impossible, got weight=" << computed_weight << "\n";
							return 2;
						}
						continue;
					}

					if ( !is_power_of_two_uint64( count ) )
					{
						std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma << " count=" << count << " is not a power of two\n";
						return 3;
					}

					const SearchWeight expected_weight = static_cast<SearchWeight>( 2 * n - floor_log2_uint64( count ) );
					if ( computed_weight != expected_weight )
					{
						std::cout << "ARX Differential Analysis: [LM2001] FAIL alpha=" << alpha << " beta=" << beta << " gamma=" << gamma << " expected_weight=" << expected_weight << " got_weight=" << computed_weight << "\n";
						return 4;
					}
				}
			}
		}

		std::cout << "ARX Differential Analysis: [LM2001] PASS (n=4 exhaustive)\n";
	}

	// ------------------------------------------------------------
	// 3.5) LM2001 Algorithm 4: find_optimal_gamma() correctness check
	// Verify: gamma returned by Algorithm 4 achieves the MIN weight among all gamma (small n brute-force).
	// ------------------------------------------------------------
	{
		// Exhaustive (small n) check: n=6 => 64*64 pairs, each brute-forces 64 gammas (fast).
		constexpr int			n = 6;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
		{
			for ( std::uint32_t beta = 0; beta <= mask; ++beta )
			{
				const auto [ brute_gamma, brute_w ] = brute_force_best_gamma_for_xdp_add_n( alpha, beta, n );
				if ( brute_w >= INFINITE_WEIGHT )
				{
					std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n << " alpha=" << alpha << " beta=" << beta << " no feasible gamma found (unexpected)\n";
					return 11;
				}

				const std::uint32_t opt_gamma = find_optimal_gamma( alpha, beta, n ) & mask;
				const SearchWeight opt_w = xdp_add_lm2001_n( alpha, beta, opt_gamma, n );
				if ( opt_w != brute_w )
				{
					std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n << " alpha=" << alpha << " beta=" << beta << " opt_gamma=" << opt_gamma << " opt_w=" << opt_w << " brute_gamma=" << brute_gamma << " brute_w=" << brute_w << "\n";
					return 12;
				}

				const auto [ opt_gamma2, opt_w2 ] = find_optimal_gamma_with_weight( alpha, beta, n );
				if ( ( ( opt_gamma2 & mask ) != opt_gamma ) || ( opt_w2 != opt_w ) )
				{
					std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n << " alpha=" << alpha << " beta=" << beta << " wrapper_gamma=" << ( opt_gamma2 & mask ) << " wrapper_w=" << opt_w2 << " direct_gamma=" << opt_gamma << " direct_w=" << opt_w << "\n";
					return 13;
				}
			}
		}
		std::cout << "ARX Differential Analysis: [LM2001-Alg4] PASS (n=6 exhaustive)\n";

		// Random (medium n) check: n=10 => brute-force 1024 gammas per sample.
		// This is still cheap but catches masking/bit-reverse mistakes that only show up beyond tiny widths.
		{
			constexpr int								 n2 = 10;
			constexpr std::uint32_t						 mask2 = ( 1u << n2 ) - 1u;
			std::mt19937								 rng( 0xB16B00B5u );
			std::uniform_int_distribution<std::uint32_t> dist( 0u, mask2 );

			for ( int t = 0; t < 2000; ++t )
			{
				const std::uint32_t alpha = dist( rng );
				const std::uint32_t beta = dist( rng );
				const auto [ brute_gamma, brute_w ] = brute_force_best_gamma_for_xdp_add_n( alpha, beta, n2 );
				const std::uint32_t opt_gamma = find_optimal_gamma( alpha, beta, n2 ) & mask2;
				const SearchWeight opt_w = xdp_add_lm2001_n( alpha, beta, opt_gamma, n2 );
				if ( opt_w != brute_w )
				{
					std::cout << "ARX Differential Analysis: [LM2001-Alg4] FAIL n=" << n2 << " alpha=" << alpha << " beta=" << beta << " opt_gamma=" << opt_gamma << " opt_w=" << opt_w << " brute_gamma=" << brute_gamma << " brute_w=" << brute_w << "\n";
					return 14;
				}
			}
			std::cout << "ARX Differential Analysis: [LM2001-Alg4] PASS (n=10 random)\n";
		}
	}

	// ------------------------------------------------------------
	// 4) differential_addconst exact sanity: random sampling for n=8
	// Validate exact DP count/weight against brute force, and check log2pi closed-form.
	// ------------------------------------------------------------
	{
		if ( run_addconst_exact_sanity_n8_random() != 0 )
		{
			std::cout << "ARX Differential Analysis: [AddConstExact8] FAIL\n";
			return 9;
		}
		std::cout << "ARX Differential Analysis: [AddConstExact8] PASS\n";
	}

	// ------------------------------------------------------------
	// 5) Azimi Proposition 1 sanity: ParallelLog / ParallelTrunc consistency
	// ------------------------------------------------------------
	{
		if ( run_parallel_log_trunc_sanity() != 0 )
		{
			std::cout << "ARX Differential Analysis: [ParallelLog/Trunc] FAIL\n";
			return 10;
		}
		std::cout << "ARX Differential Analysis: [ParallelLog/Trunc] PASS\n";
	}

	// ------------------------------------------------------------
	// 6) var-const exact Q2 closure: fixed-input exact/prototype and fixed-output exact reference
	// Verify both directions use the same XOR-differential Q1 semantics and preserve lexicographic tie handling.
	// ------------------------------------------------------------
	{
		if ( run_diffconst_varconst_q2_closure_self_test() != 0 )
		{
			std::cout << "ARX Differential Analysis: [ConstQ2ExactClosure] FAIL\n";
			return 15;
		}
	}

    std::cout << "[SelfTest][Section] PASS Operator Layer\n";

    std::cout << "[SelfTest][Section] Differential BNB Math Layer\n";
    if ( run_differential_search_self_test( 0x0FEDCBA987654321ull, 0u ) != 0 )
    {
        std::cout << "[SelfTest][Section] FAIL Differential BNB Math Layer\n";
        return 34;
    }
    std::cout << "[SelfTest][Section] PASS Differential BNB Math Layer\n";

    std::cout << "[SelfTest][Summary] operator_layer=PASS differential_bnb_math=PASS\n";
    std::cout << "[SelfTest] PASS\n\n";
    return 0;
}


namespace
{

	namespace linear_search_self_test
	{

		static int xor_basis_add_32_selftest( std::array<std::uint32_t, 32>& basis_by_msb, std::uint32_t v ) noexcept
		{
			while ( v != 0u )
			{
				const unsigned		bit = 31u - std::countl_zero( v );
				const std::uint32_t basis = basis_by_msb[ std::size_t( bit ) ];
				if ( basis != 0u )
				{
					v ^= basis;
				}
				else
				{
					basis_by_msb[ std::size_t( bit ) ] = v;
					return 1;
				}
			}
			return 0;
		}

		static std::array<std::uint32_t, 32> pack_basis_vectors_desc( const std::array<std::uint32_t, 32>& basis_by_msb )
		{
			std::array<std::uint32_t, 32> out {};
			int							  packed = 0;
			for ( int bit = 31; bit >= 0; --bit )
			{
				const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
				if ( v != 0u )
				{
					out[ std::size_t( packed++ ) ] = v;
				}
			}
			return out;
		}

		static bool basis_span_contains( const std::array<std::uint32_t, 32>& basis_by_msb, std::uint32_t v ) noexcept
		{
			while ( v != 0u )
			{
				const unsigned		bit = 31u - std::countl_zero( v );
				const std::uint32_t basis = basis_by_msb[ std::size_t( bit ) ];
				if ( basis == 0u )
				{
					return false;
				}
				v ^= basis;
			}
			return true;
		}

	}  // namespace linear_search_self_test

	namespace differential_search_self_test
	{

		using namespace TwilightDream::auto_search_differential;

		using ModAddSequence = std::vector<std::pair<std::uint32_t, SearchWeight>>;

		struct DiffToyCase
		{
			std::uint32_t alpha = 0;
			std::uint32_t beta = 0;
			std::uint32_t output_hint = 0;
			SearchWeight  weight_cap = 0;
			int			  word_bits = 0;
		};

		static std::string format_word32_hex( std::uint32_t v )
		{
			std::ostringstream oss;
			oss << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << v << std::dec;
			return oss.str();
		}

		static void print_diff_case( std::ostream& os, const DiffToyCase& c )
		{
			os << "alpha=" << format_word32_hex( c.alpha ) << " beta=" << format_word32_hex( c.beta ) << " output_hint=" << format_word32_hex( c.output_hint ) << " weight_cap=" << c.weight_cap << " word_bits=" << c.word_bits;
		}

		struct ModAddPairKey
		{
			std::uint32_t gamma = 0;
			SearchWeight weight = 0;
		};

		struct ModAddPairKeyHash
		{
			std::size_t operator()( const ModAddPairKey& key ) const noexcept
			{
				const std::size_t h1 = std::hash<std::uint32_t> {}( key.gamma );
				const std::size_t h2 = std::hash<SearchWeight> {}( key.weight );
				return ( h1 * 1315423911u ) ^ ( h2 + 0x9e3779b97f4a7c15ull );
			}
		};

		static inline bool operator==( const ModAddPairKey& a, const ModAddPairKey& b ) noexcept
		{
			return a.gamma == b.gamma && a.weight == b.weight;
		}

		static ModAddPairKey make_mod_add_pair_key( std::uint32_t gamma, SearchWeight weight )
		{
			return ModAddPairKey { gamma, weight };
		}

		static bool find_duplicate_mod_add_pair( const ModAddSequence& seq, std::size_t& index_out, std::uint32_t& gamma_out, SearchWeight& weight_out )
		{
			std::unordered_set<ModAddPairKey, ModAddPairKeyHash> seen;
			seen.reserve( seq.size() * 2u + 1u );
			for ( std::size_t i = 0; i < seq.size(); ++i )
			{
				const std::uint32_t gamma = seq[ i ].first;
				const SearchWeight	weight = seq[ i ].second;
				const ModAddPairKey key = make_mod_add_pair_key( gamma, weight );
				if ( !seen.insert( key ).second )
				{
					index_out = i;
					gamma_out = gamma;
					weight_out = weight;
					return true;
				}
			}
			return false;
		}

		static std::unordered_map<SearchWeight, std::size_t> build_weight_multiset( const ModAddSequence& seq )
		{
			std::unordered_map<SearchWeight, std::size_t> counts;
			counts.reserve( seq.size() + 1u );
			for ( const auto& entry : seq )
			{
				++counts[ entry.second ];
			}
			return counts;
		}

		static bool compare_weight_multiset( const std::unordered_map<SearchWeight, std::size_t>& expected, const std::unordered_map<SearchWeight, std::size_t>& actual, SearchWeight& weight_out, std::size_t& expected_count, std::size_t& actual_count )
		{
			for ( const auto& kv : expected )
			{
				const SearchWeight w = kv.first;
				const std::size_t count = kv.second;
				const auto		  it = actual.find( w );
				const std::size_t actual_value = ( it == actual.end() ) ? 0u : it->second;
				if ( count != actual_value )
				{
					weight_out = w;
					expected_count = count;
					actual_count = actual_value;
					return false;
				}
			}
			for ( const auto& kv : actual )
			{
				if ( expected.find( kv.first ) == expected.end() )
				{
					weight_out = kv.first;
					expected_count = 0u;
					actual_count = kv.second;
					return false;
				}
			}
			return true;
		}

		static bool compare_mod_add_sequences( const DiffToyCase& tc, const ModAddSequence& expected, const ModAddSequence& actual, const char* expected_label, const char* actual_label, const char* test_label )
		{
			bool ok = true;
			bool header_printed = false;
			auto print_header = [ & ]() {
				if ( header_printed )
				{
					return;
				}
				std::cerr << "[SelfTest][Differential] " << test_label << " mismatch: ";
				print_diff_case( std::cerr, tc );
				std::cerr << "\n";
				header_printed = true;
			};

			if ( expected.size() != actual.size() )
			{
				print_header();
				std::cerr << "  count mismatch expected=" << expected.size() << " actual=" << actual.size() << "\n";
				ok = false;
			}

			{
				std::size_t	  dup_index = 0;
				std::uint32_t dup_gamma = 0;
				SearchWeight dup_weight = 0;
				if ( find_duplicate_mod_add_pair( expected, dup_index, dup_gamma, dup_weight ) )
				{
					print_header();
					std::cerr << "  duplicate in " << expected_label << " at index " << dup_index << " gamma=" << format_word32_hex( dup_gamma ) << " weight=" << dup_weight << "\n";
					ok = false;
				}
				if ( find_duplicate_mod_add_pair( actual, dup_index, dup_gamma, dup_weight ) )
				{
					print_header();
					std::cerr << "  duplicate in " << actual_label << " at index " << dup_index << " gamma=" << format_word32_hex( dup_gamma ) << " weight=" << dup_weight << "\n";
					ok = false;
				}
			}

			{
				const auto	expected_weights = build_weight_multiset( expected );
				const auto	actual_weights = build_weight_multiset( actual );
				SearchWeight w = 0;
				std::size_t expected_count = 0;
				std::size_t actual_count = 0;
				if ( !compare_weight_multiset( expected_weights, actual_weights, w, expected_count, actual_count ) )
				{
					print_header();
					std::cerr << "  weight multiset mismatch weight=" << w << " expected_count=" << expected_count << " actual_count=" << actual_count << "\n";
					ok = false;
				}
			}

			{
				std::unordered_set<ModAddPairKey, ModAddPairKeyHash> expected_set;
				std::unordered_set<ModAddPairKey, ModAddPairKeyHash> actual_set;
				expected_set.reserve( expected.size() * 2u + 1u );
				actual_set.reserve( actual.size() * 2u + 1u );
				for ( const auto& entry : expected )
				{
					expected_set.insert( make_mod_add_pair_key( entry.first, entry.second ) );
				}
				for ( const auto& entry : actual )
				{
					actual_set.insert( make_mod_add_pair_key( entry.first, entry.second ) );
				}

				for ( const auto& entry : expected )
				{
					const ModAddPairKey key = make_mod_add_pair_key( entry.first, entry.second );
					if ( actual_set.find( key ) == actual_set.end() )
					{
						print_header();
						std::cerr << "  missing candidate gamma=" << format_word32_hex( entry.first ) << " weight=" << entry.second << "\n";
						ok = false;
						break;
					}
				}
				for ( const auto& entry : actual )
				{
					const ModAddPairKey key = make_mod_add_pair_key( entry.first, entry.second );
					if ( expected_set.find( key ) == expected_set.end() )
					{
						print_header();
						std::cerr << "  unexpected candidate gamma=" << format_word32_hex( entry.first ) << " weight=" << entry.second << "\n";
						ok = false;
						break;
					}
				}
			}

			if ( expected != actual )
			{
				const std::size_t min_size = std::min( expected.size(), actual.size() );
				std::size_t		  mismatch_index = min_size;
				for ( std::size_t i = 0; i < min_size; ++i )
				{
					if ( expected[ i ] != actual[ i ] )
					{
						mismatch_index = i;
						break;
					}
				}
				print_header();
				if ( mismatch_index < min_size )
				{
					const auto& exp_entry = expected[ mismatch_index ];
					const auto& act_entry = actual[ mismatch_index ];
					std::cerr << "  first mismatch at index " << mismatch_index << " expected_gamma=" << format_word32_hex( exp_entry.first ) << " expected_weight=" << exp_entry.second << " actual_gamma=" << format_word32_hex( act_entry.first ) << " actual_weight=" << act_entry.second << "\n";
				}
				else
				{
					std::cerr << "  sequence differs after prefix length " << min_size << "\n";
				}
				ok = false;
			}

			return ok;
		}

		static std::filesystem::path make_unique_checkpoint_path( std::mt19937_64& rng, const char* prefix )
		{
			const std::filesystem::path temp_dir = resolve_self_test_temp_directory();
			const std::uint64_t			a = rng();
			const std::uint64_t			b = rng();
			std::ostringstream			oss;
			oss << prefix << "_" << std::hex << a << "_" << b << ".ckpt";
			return temp_dir / oss.str();
		}

		static DiffToyCase make_feasible_diff_case(
			std::uint32_t alpha,
			std::uint32_t beta,
			int word_bits,
			SearchWeight requested_weight_cap )
		{
			const std::uint32_t mask = ( word_bits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			DiffToyCase			c {};
			c.word_bits = word_bits;
			c.alpha = alpha & mask;
			c.beta = beta & mask;
			if ( c.alpha == 0u && c.beta == 0u )
			{
				c.alpha = 1u;
			}
			const auto [ optimal_gamma, optimal_weight ] = find_optimal_gamma_with_weight( c.alpha, c.beta, word_bits );
			c.output_hint = optimal_gamma & mask;
			c.weight_cap = std::max( requested_weight_cap, optimal_weight );
			return c;
		}

		static std::vector<DiffToyCase> make_fixed_diff_toy_cases()
		{
			return { make_feasible_diff_case( 0x00000015u, 0x0000002Bu, 8, 6 ), make_feasible_diff_case( 0x0000003Cu, 0x000000A1u, 8, 7 ), make_feasible_diff_case( 0x0000012Du, 0x0000007Fu, 9, 7 ), make_feasible_diff_case( 0x00000155u, 0x000000AAu, 10, 8 ), make_feasible_diff_case( 0x00000345u, 0x000001A7u, 12, 8 ) };
		}

		static DiffToyCase make_random_diff_case( std::mt19937_64& rng )
		{
			const int			min_word_bits = 7;
			const int			max_word_bits = 12;
			const int			word_bits = min_word_bits + static_cast<int>( rng() % std::uint64_t( max_word_bits - min_word_bits + 1 ) );
			const std::uint32_t mask = ( word_bits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			auto				pick_masked = [ & ]() {
				   return static_cast<std::uint32_t>( rng() ) & mask;
			};

			DiffToyCase c {};
			c.word_bits = word_bits;
			c.alpha = pick_masked();
			c.beta = pick_masked();
			if ( c.alpha == 0 && c.beta == 0 )
			{
				c.alpha = 1u;
			}
			const auto [ optimal_gamma, optimal_weight ] = find_optimal_gamma_with_weight( c.alpha, c.beta, word_bits );
			c.output_hint = optimal_gamma & mask;

			const SearchWeight min_cap = std::max<SearchWeight>( 4, optimal_weight );
			SearchWeight max_cap = std::max<SearchWeight>( min_cap, static_cast<SearchWeight>( std::min( 8, word_bits ) ) );
			c.weight_cap = min_cap + static_cast<SearchWeight>( rng() % std::uint64_t( max_cap - min_cap + 1 ) );
			return c;
		}

		static void init_mod_add_test_context( DifferentialBestSearchContext& ctx, bool enable_weight_sliced_pddt, SearchWeight max_weight )
		{
			ctx.configuration.enable_weight_sliced_pddt = enable_weight_sliced_pddt;
			ctx.configuration.weight_sliced_pddt_max_weight = max_weight;
			ctx.runtime_controls.maximum_search_nodes = 0;
		}

		static void weight_sliced_pddt_rebuildable_cleanup_callback_for_self_test()
		{
			g_weight_sliced_pddt_cache.clear_keep_enabled( "rebuildable_release" );
		}

		static bool collect_mod_add_sequence( DifferentialBestSearchContext& ctx, ModularAdditionEnumerator& e, ModAddSequence& out )
		{
			out.clear();
			ctx.visited_node_count = 0;
			std::uint32_t gamma = 0;
			SearchWeight	  weight = 0;
			while ( e.next( ctx, gamma, weight ) )
			{
				out.emplace_back( gamma, weight );
			}
			return !e.stop_due_to_limits;
		}

		static bool build_mod_add_baseline( const DiffToyCase& tc, ModAddSequence& baseline )
		{
			g_weight_sliced_pddt_cache.configure( false, 0 );
			g_weight_sliced_pddt_cache.clear_keep_enabled();

			DifferentialBestSearchContext ctx {};
			init_mod_add_test_context( ctx, false, 0 );
			ModularAdditionEnumerator e {};
			e.reset( tc.alpha, tc.beta, tc.output_hint, tc.weight_cap, 0, 0u, tc.word_bits );

			const bool ok = collect_mod_add_sequence( ctx, e, baseline );
			g_weight_sliced_pddt_cache.configure( false, 0 );
			return ok && !baseline.empty();
		}

		static bool run_mod_add_strict_consistency_case( const DiffToyCase& tc, const ModAddSequence& baseline )
		{
			DifferentialBestSearchContext ctx_cache {};
			init_mod_add_test_context( ctx_cache, true, tc.weight_cap );

			g_weight_sliced_pddt_cache.configure( true, tc.weight_cap );
			g_weight_sliced_pddt_cache.clear_keep_enabled();

			ModularAdditionEnumerator e_cache {};
			e_cache.reset( tc.alpha, tc.beta, tc.output_hint, tc.weight_cap, 0, 0u, tc.word_bits );

			ModAddSequence seq_cache;
			const bool	   ok_cache = collect_mod_add_sequence( ctx_cache, e_cache, seq_cache );

			g_weight_sliced_pddt_cache.clear_keep_enabled();
			g_weight_sliced_pddt_cache.configure( false, 0 );

			if ( !ok_cache )
			{
				return false;
			}
			return compare_mod_add_sequences( tc, baseline, seq_cache, "baseline", "pddt", "strict-consistency" );
		}

		static bool run_mod_add_checkpoint_resume_case( const DiffToyCase& tc, const ModAddSequence& baseline, std::mt19937_64& rng )
		{
			DifferentialBestSearchContext ctx {};
			init_mod_add_test_context( ctx, true, tc.weight_cap );
			g_weight_sliced_pddt_cache.configure( true, tc.weight_cap );
			g_weight_sliced_pddt_cache.clear_keep_enabled();

			ModularAdditionEnumerator e {};
			e.reset( tc.alpha, tc.beta, tc.output_hint, tc.weight_cap, 0, 0u, tc.word_bits );

			std::uint32_t gamma = 0;
			SearchWeight	  weight = 0;
			bool		  checkpointed = false;
			std::size_t	  consumed = 0;
			for ( std::size_t guard = 0; guard < 100000; ++guard )
			{
				if ( !e.next( ctx, gamma, weight ) )
				{
					break;
				}
				++consumed;
				if ( e.using_cached_shell && e.shell_index > 0 )
				{
					checkpointed = true;
					break;
				}
			}
			if ( !checkpointed )
			{
				std::cerr << "[SelfTest][Differential] checkpoint guard failed: ";
				print_diff_case( std::cerr, tc );
				std::cerr << " (did not enter cached shell)\n";
				g_weight_sliced_pddt_cache.clear_keep_enabled();
				g_weight_sliced_pddt_cache.configure( false, 0 );
				return false;
			}

			SelfTestTemporaryFile checkpoint_file { make_unique_checkpoint_path( rng, "tmp_mod_add_enum" ) };
			{
				TwilightDream::auto_search_checkpoint::BinaryWriter w( checkpoint_file.path.string() );
				if ( !w.ok() )
				{
					std::cerr << "[SelfTest][Differential] checkpoint writer failed: " << checkpoint_file.path.string() << "\n";
					g_weight_sliced_pddt_cache.clear_keep_enabled();
					g_weight_sliced_pddt_cache.configure( false, 0 );
					return false;
				}
				write_mod_add_enum( w, e );
				if ( !w.ok() )
				{
					std::cerr << "[SelfTest][Differential] checkpoint writer failed (write_mod_add_enum)\n";
					g_weight_sliced_pddt_cache.clear_keep_enabled();
					g_weight_sliced_pddt_cache.configure( false, 0 );
					return false;
				}
			}

			ModularAdditionEnumerator e_resume {};
			{
				TwilightDream::auto_search_checkpoint::BinaryReader r( checkpoint_file.path.string() );
				if ( !r.ok() )
				{
					std::cerr << "[SelfTest][Differential] checkpoint reader failed: " << checkpoint_file.path.string() << "\n";
					g_weight_sliced_pddt_cache.clear_keep_enabled();
					g_weight_sliced_pddt_cache.configure( false, 0 );
					return false;
				}
				if ( !read_mod_add_enum( r, e_resume ) )
				{
					std::cerr << "[SelfTest][Differential] checkpoint read failed (read_mod_add_enum)\n";
					g_weight_sliced_pddt_cache.clear_keep_enabled();
					g_weight_sliced_pddt_cache.configure( false, 0 );
					return false;
				}
			}

			if ( !e_resume.shell_cache.empty() )
			{
				std::cerr << "[SelfTest][Differential] shell_cache should not be serialized; resume copy must start rebuildable-only\n";
				g_weight_sliced_pddt_cache.clear_keep_enabled();
				g_weight_sliced_pddt_cache.configure( false, 0 );
				return false;
			}
			if ( e_resume.using_cached_shell &&
				 !rebuild_modular_addition_enumerator_shell_cache( ctx.configuration, e_resume ) )
			{
				std::cerr << "[SelfTest][Differential] failed to rebuild shell_cache after checkpoint round-trip\n";
				g_weight_sliced_pddt_cache.clear_keep_enabled();
				g_weight_sliced_pddt_cache.configure( false, 0 );
				return false;
			}

			ModAddSequence seq_direct;
			ModAddSequence seq_resume;
			const bool	   ok_direct = collect_mod_add_sequence( ctx, e, seq_direct );
			ctx.visited_node_count = 0;
			const bool ok_resume = collect_mod_add_sequence( ctx, e_resume, seq_resume );

			bool ok = true;
			if ( !ok_direct || !ok_resume )
			{
				ok = false;
			}
			if ( consumed > baseline.size() )
			{
				std::cerr << "[SelfTest][Differential] checkpoint consumed beyond baseline: consumed=" << consumed << " baseline=" << baseline.size() << "\n";
				ok = false;
			}

			if ( ok )
			{
				const ModAddSequence baseline_remaining( baseline.begin() + static_cast<std::ptrdiff_t>( consumed ), baseline.end() );
				ok = compare_mod_add_sequences( tc, baseline_remaining, seq_direct, "baseline_tail", "direct", "checkpoint-direct" ) && ok;
				ok = compare_mod_add_sequences( tc, baseline_remaining, seq_resume, "baseline_tail", "resume", "checkpoint-resume" ) && ok;
				ok = compare_mod_add_sequences( tc, seq_direct, seq_resume, "direct", "resume", "checkpoint-compare" ) && ok;
			}

			g_weight_sliced_pddt_cache.clear_keep_enabled();
			g_weight_sliced_pddt_cache.configure( false, 0 );

			return ok;
		}

		static bool run_mod_add_memory_pressure_case( const DiffToyCase& tc, const ModAddSequence& baseline )
		{
			g_weight_sliced_pddt_cache.clear_keep_enabled();
			g_weight_sliced_pddt_cache.configure( true, tc.weight_cap );
			TwilightDream::runtime_component::rebuildable_set_cleanup_fn( &weight_sliced_pddt_rebuildable_cleanup_callback_for_self_test );

			DifferentialBestSearchContext ctx {};
			init_mod_add_test_context( ctx, true, tc.weight_cap );
			ModularAdditionEnumerator e {};
			e.reset( tc.alpha, tc.beta, tc.output_hint, tc.weight_cap, 0, 0u, tc.word_bits );

			std::uint32_t gamma = 0;
			SearchWeight	  weight = 0;
			bool		  checkpointed = false;
			std::size_t	  consumed = 0;
			for ( std::size_t guard = 0; guard < 100000; ++guard )
			{
				if ( !e.next( ctx, gamma, weight ) )
				{
					break;
				}
				++consumed;
				if ( e.using_cached_shell && e.shell_index > 0 )
				{
					checkpointed = true;
					break;
				}
			}
			if ( !checkpointed )
			{
				std::cerr << "[SelfTest][Differential] memory-pressure guard failed: ";
				print_diff_case( std::cerr, tc );
				std::cerr << " (did not enter cached shell)\n";
				TwilightDream::runtime_component::rebuildable_set_cleanup_fn( nullptr );
				g_weight_sliced_pddt_cache.clear_keep_enabled();
				g_weight_sliced_pddt_cache.configure( false, 0 );
				return false;
			}

			TwilightDream::runtime_component::on_memory_pressure();
			e.shell_cache.clear();

			ModAddSequence remaining;
			const bool	   ok_remaining = collect_mod_add_sequence( ctx, e, remaining );

			TwilightDream::runtime_component::rebuildable_set_cleanup_fn( nullptr );
			g_weight_sliced_pddt_cache.clear_keep_enabled();
			g_weight_sliced_pddt_cache.configure( false, 0 );

			if ( !ok_remaining )
			{
				return false;
			}
			if ( consumed > baseline.size() )
			{
				std::cerr << "[SelfTest][Differential] memory-pressure consumed beyond baseline: consumed=" << consumed << " baseline=" << baseline.size() << "\n";
				return false;
			}
			const ModAddSequence baseline_remaining( baseline.begin() + static_cast<std::ptrdiff_t>( consumed ), baseline.end() );
			return compare_mod_add_sequences( tc, baseline_remaining, remaining, "baseline_tail", "pressure_resume", "memory-pressure" );
		}

		static std::array<std::uint32_t, 32> canonical_affine_basis_by_msb( const InjectionAffineTransition& transition )
		{
			std::array<std::uint32_t, 32> basis_by_msb {};
			for ( const std::uint32_t v : transition.basis_vectors )
			{
				if ( v != 0u )
				{
					linear_search_self_test::xor_basis_add_32_selftest( basis_by_msb, v );
				}
			}
			return basis_by_msb;
		}

		static InjectionAffineTransition build_direct_injection_affine_oracle_branch_b( std::uint32_t input_difference )
		{
			InjectionAffineTransition transition {};

			const std::uint32_t f0 = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 0u );
			const std::uint32_t f_delta = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( input_difference );
			transition.offset = f0 ^ f_delta;  // D_Δ f(0)

			std::array<std::uint32_t, 32> basis_by_msb {};
			int							  rank = 0;
			for ( int i = 0; i < 32; ++i )
			{
				const std::uint32_t ei = ( 1u << i );
				const std::uint32_t f_ei = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( ei );
				const std::uint32_t f_ei_delta = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( ei ^ input_difference );
				const std::uint32_t column = f_ei ^ f_ei_delta ^ transition.offset;
				if ( column != 0u )
				{
					rank += linear_search_self_test::xor_basis_add_32_selftest( basis_by_msb, column );
				}
			}
			transition.rank_weight = rank;
			transition.basis_vectors = linear_search_self_test::pack_basis_vectors_desc( basis_by_msb );
			return transition;
		}

		static InjectionAffineTransition build_direct_injection_affine_oracle_branch_a( std::uint32_t input_difference )
		{
			InjectionAffineTransition transition {};

			const std::uint32_t f0 = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 0u );
			const std::uint32_t f_delta = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( input_difference );
			transition.offset = f0 ^ f_delta;  // D_Δ f(0)

			std::array<std::uint32_t, 32> basis_by_msb {};
			int							  rank = 0;
			for ( int i = 0; i < 32; ++i )
			{
				const std::uint32_t ei = ( 1u << i );
				const std::uint32_t f_ei = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( ei );
				const std::uint32_t f_ei_delta = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( ei ^ input_difference );
				const std::uint32_t column = f_ei ^ f_ei_delta ^ transition.offset;
				if ( column != 0u )
				{
					rank += linear_search_self_test::xor_basis_add_32_selftest( basis_by_msb, column );
				}
			}
			transition.rank_weight = rank;
			transition.basis_vectors = linear_search_self_test::pack_basis_vectors_desc( basis_by_msb );
			return transition;
		}

		static bool compare_injection_affine_transition( const char* branch_label, const char* cache_mode_label, std::uint32_t input_difference, const InjectionAffineTransition& oracle, const InjectionAffineTransition& actual )
		{
			bool ok = true;
			bool header_printed = false;
			auto print_header = [ & ]() {
				if ( header_printed )
				{
					return;
				}
				std::cerr << "[SelfTest][Differential][Injection] " << branch_label << " mismatch (" << cache_mode_label << ") for input_difference=" << format_word32_hex( input_difference ) << "\n";
				header_printed = true;
			};

			if ( oracle.offset != actual.offset )
			{
				print_header();
				std::cerr << "  offset mismatch oracle=" << format_word32_hex( oracle.offset ) << " actual=" << format_word32_hex( actual.offset ) << "\n";
				ok = false;
			}
			if ( oracle.rank_weight != actual.rank_weight )
			{
				print_header();
				std::cerr << "  rank mismatch oracle=" << oracle.rank_weight << " actual=" << actual.rank_weight << "\n";
				ok = false;
			}

			const auto oracle_basis = canonical_affine_basis_by_msb( oracle );
			const auto actual_basis = canonical_affine_basis_by_msb( actual );

			int oracle_basis_count = 0;
			int actual_basis_count = 0;
			for ( const std::uint32_t v : oracle.basis_vectors )
			{
				if ( v != 0u )
				{
					++oracle_basis_count;
					if ( !linear_search_self_test::basis_span_contains( actual_basis, v ) )
					{
						print_header();
						std::cerr << "  oracle basis vector missing from actual span: " << format_word32_hex( v ) << "\n";
						ok = false;
					}
				}
			}
			for ( const std::uint32_t v : actual.basis_vectors )
			{
				if ( v != 0u )
				{
					++actual_basis_count;
					if ( !linear_search_self_test::basis_span_contains( oracle_basis, v ) )
					{
						print_header();
						std::cerr << "  actual basis vector missing from oracle span: " << format_word32_hex( v ) << "\n";
						ok = false;
					}
				}
			}

			if ( static_cast<std::uint64_t>( oracle_basis_count ) != oracle.rank_weight )
			{
				print_header();
				std::cerr << "  oracle basis count mismatch count=" << oracle_basis_count << " rank=" << oracle.rank_weight << "\n";
				ok = false;
			}
			if ( static_cast<std::uint64_t>( actual_basis_count ) != actual.rank_weight )
			{
				print_header();
				std::cerr << "  actual basis count mismatch count=" << actual_basis_count << " rank=" << actual.rank_weight << "\n";
				ok = false;
			}

			return ok;
		}

		static bool run_injection_affine_transition_self_tests( std::uint64_t seed, std::size_t extra_cases )
		{
			std::mt19937_64			   rng( seed ^ 0xD1FF3A77E5B19C4Bull );
			std::vector<std::uint32_t> cases { 0x00000000u, 0x00000001u, 0x00000003u, 0x00000005u, 0x0000000Fu, 0x00000080u, 0x00008000u, 0x00010001u, 0x00FF00FFu, 0xA5A5A5A5u, 0x80000000u, 0xFFFFFFFFu };
			cases.reserve( cases.size() + extra_cases );
			for ( std::size_t i = 0; i < extra_cases; ++i )
			{
				cases.push_back( static_cast<std::uint32_t>( rng() ) );
			}

			const std::size_t previous_cache_cap = g_injection_cache_max_entries_per_thread;
			bool			  ok = true;

			auto run_mode = [ & ]( const char* cache_mode_label, std::size_t cache_cap ) {
				g_injection_cache_max_entries_per_thread = cache_cap;
				for ( const std::uint32_t input_difference : cases )
				{
					const auto oracle_b = build_direct_injection_affine_oracle_branch_b( input_difference );
					const auto actual_b_first = compute_injection_transition_from_branch_b( input_difference );
					const auto actual_b_second = compute_injection_transition_from_branch_b( input_difference );
					if ( !compare_injection_affine_transition( "branch_b", cache_mode_label, input_difference, oracle_b, actual_b_first ) )
					{
						ok = false;
					}
					if ( !compare_injection_affine_transition( "branch_b", cache_mode_label, input_difference, oracle_b, actual_b_second ) )
					{
						ok = false;
					}

					const auto oracle_a = build_direct_injection_affine_oracle_branch_a( input_difference );
					const auto actual_a_first = compute_injection_transition_from_branch_a( input_difference );
					const auto actual_a_second = compute_injection_transition_from_branch_a( input_difference );
					if ( !compare_injection_affine_transition( "branch_a", cache_mode_label, input_difference, oracle_a, actual_a_first ) )
					{
						ok = false;
					}
					if ( !compare_injection_affine_transition( "branch_a", cache_mode_label, input_difference, oracle_a, actual_a_second ) )
					{
						ok = false;
					}
				}
			};

			run_mode( "cache_off", 0 );
			run_mode( "cache_on", 64 );

			g_injection_cache_max_entries_per_thread = previous_cache_cap;

			if ( ok )
			{
				std::cout << "[SelfTest][Differential][Injection] exact affine transition checks passed"
						  << " cases=" << cases.size() << " seed=0x" << std::hex << seed << std::dec << "\n";
			}
			return ok;
		}

		static bool run_differential_search_self_test_impl( std::uint64_t seed, std::size_t extra_cases )
		{
			std::mt19937_64 rng( seed );

			std::vector<DiffToyCase> cases = make_fixed_diff_toy_cases();
			const std::size_t		 fixed_case_count = cases.size();
			for ( std::size_t i = 0; i < extra_cases; ++i )
			{
				cases.push_back( make_random_diff_case( rng ) );
			}

			std::cout << "[SelfTest][Differential] fixed_cases=" << fixed_case_count << " random_cases=" << extra_cases << " seed=0x" << std::hex << seed << std::dec << "\n";

			bool ok = true;
			for ( std::size_t case_index = 0; case_index < cases.size(); ++case_index )
			{
				const DiffToyCase& tc = cases[ case_index ];
				ModAddSequence	   baseline;
				if ( !build_mod_add_baseline( tc, baseline ) )
				{
					std::cerr << "[SelfTest][Differential] baseline build failed for case #" << case_index << ": ";
					print_diff_case( std::cerr, tc );
					std::cerr << "\n";
					ok = false;
					continue;
				}

				if ( !run_mod_add_strict_consistency_case( tc, baseline ) )
				{
					ok = false;
				}
				if ( !run_mod_add_checkpoint_resume_case( tc, baseline, rng ) )
				{
					ok = false;
				}
				if ( !run_mod_add_memory_pressure_case( tc, baseline ) )
				{
					ok = false;
				}
			}
			if ( !run_injection_affine_transition_self_tests( seed, extra_cases ) )
			{
				ok = false;
			}
			if ( ok )
			{
				std::cout << "[SelfTest][Differential] regression tests passed\n";
			}
			return ok;
		}

	}  // namespace differential_search_self_test

}  // namespace

int run_differential_search_self_test( std::uint64_t seed, std::size_t extra_cases )
{
	return differential_search_self_test::run_differential_search_self_test_impl( seed, extra_cases ) ? 0 : 1;
}
