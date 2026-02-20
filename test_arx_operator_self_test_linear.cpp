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


#include "arx_analysis_operators/linear_correlation/constant_optimal_alpha.hpp"
#include "arx_analysis_operators/linear_correlation/constant_optimal_beta.hpp"
#include "arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp"
#include "arx_analysis_operators/linear_correlation/constant_weight_evaluation_flat.hpp"
#include "arx_analysis_operators/linear_correlation/weight_evaluation.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_hot_path.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_hot_path.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/subtract_bnb_mode.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"


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

	[[maybe_unused]] static std::filesystem::path resolve_self_test_temp_directory()
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

	[[maybe_unused]] static bool is_power_of_two_uint64( std::uint64_t value )
	{
		return std::has_single_bit( value );
	}

	static inline int parity_u32( std::uint32_t v ) noexcept
	{
		return static_cast<int>( std::popcount( v ) & 1u );
	}

	static inline int msb_index_u32( std::uint32_t v ) noexcept
	{
		return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
	}

	static inline std::uint32_t strip_msb_once_u32( std::uint32_t v ) noexcept
	{
		const int msb = msb_index_u32( v );
		if ( msb < 0 )
			return 0u;
		return v & ~( 1u << msb );
	}

	static double brute_force_corr_add_const_n( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n );
	static double brute_force_corr_sub_const_n( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n );

	// Definition 3 (Wallén 2003): cpm_k^i(x) on n-bit domain.
	// For each j, cpm_k^i(x)_j = 1 iff k <= j < k+i and x_ℓ = 1 for all j < ℓ < k+i.
	static std::uint32_t cpmki_naive( std::uint32_t x, int k, int i, int n ) noexcept
	{
		if ( n <= 0 || i <= 0 )
			return 0u;
		if ( k < 0 )
			return 0u;

		const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
		x &= mask;

		const int end = k + i - 1;
		if ( k >= n || end < 0 )
			return 0u;
		if ( end >= n )
			return 0u;	// undefined by definition if window exceeds domain

		std::uint32_t out = 0u;
		bool		  all_ones_above = true;
		for ( int j = end; j >= k; --j )
		{
			if ( all_ones_above )
				out |= ( 1u << j );
			all_ones_above = all_ones_above && ( ( ( x >> j ) & 1u ) != 0u );
		}
		return out & mask;
	}

	// Definition 6 (Wallén 2003): cpm(x,y) on n-bit domain.
	// NOTE: `strip_b(x)` in the paper corresponds to stripping 1 or 2 highest '1' bits (strip applied b times).
	static std::uint32_t cpm_naive( std::uint32_t x, std::uint32_t y, int n ) noexcept
	{
		if ( n <= 0 )
			return 0u;
		const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
		x &= mask;
		y &= mask;

		if ( x == 0u )
			return 0u;

		const int			j = msb_index_u32( x );
		const std::uint32_t x_stripped1 = strip_msb_once_u32( x );
		const int			k = ( x_stripped1 != 0u ) ? msb_index_u32( x_stripped1 ) : 0;
		const int			i = j - k;

		const std::uint32_t z = cpmki_naive( y, k, i, n );
		const bool			z_subset_y = ( ( z & y ) == z );

		std::uint32_t next_x = strip_msb_once_u32( x );
		if ( z_subset_y )
			next_x = strip_msb_once_u32( next_x );	// b=2, else b=1

		return ( z ^ cpm_naive( next_x, y, n ) ) & mask;
	}

	static int run_cpm_logn_sanity_n8_exhaustive()
	{
		using TwilightDream::arx_operators::compute_cpm_logn_bitsliced;

		constexpr int			n = 8;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t x = 0; x <= mask; ++x )
		{
			for ( std::uint32_t y = 0; y <= mask; ++y )
			{
				const std::uint32_t ref = cpm_naive( x, y, n ) & mask;
				const std::uint32_t got = static_cast<std::uint32_t>( compute_cpm_logn_bitsliced( x, y, n ) ) & mask;
				if ( ref != got )
				{
					std::cout << "ARX Linear Analysis: [cpm(logn)] FAIL n=" << n << " x=0x" << std::hex << x << " y=0x" << y << " ref=0x" << ref << " got=0x" << got << std::dec << "\n";
					return 1;
				}
			}
		}

		std::cout << "ARX Linear Analysis: [cpm(logn)] PASS (n=8 exhaustive)\n";
		return 0;
	}

	static int run_subconst_exact_enum_sanity_n8()
	{
		using TwilightDream::arx_operators::linear_x_modulo_minus_const32;
		using TwilightDream::auto_search_linear::generate_subconst_candidates_for_fixed_beta;

		constexpr int			n = 8;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;
		const std::uint32_t		beta = 0x5Au & mask;
		const std::uint32_t		constant = 0x3Cu & mask;
		const SearchWeight		weight_cap = static_cast<SearchWeight>( n );

		const auto							   candidates = generate_subconst_candidates_for_fixed_beta( beta, constant, weight_cap );
		std::unordered_map<std::uint32_t, SearchWeight> got;
		got.reserve( candidates.size() );
		SearchWeight last_weight = 0;
		bool has_last_weight = false;
		for ( const auto& c : candidates )
		{
			if ( has_last_weight && c.linear_weight < last_weight )
			{
				std::cout << "ARX Linear Analysis: [subconst-exact] FAIL: non-monotone weights\n";
				return 1;
			}
			has_last_weight = true;
			last_weight = c.linear_weight;
			got[ c.input_mask_on_x ] = c.linear_weight;
		}

		auto weight_from_corr = [ & ]( double corr ) -> SearchWeight {
			const double a = std::fabs( corr );
			if ( !( a > 0.0 ) || !std::isfinite( a ) )
				return INFINITE_WEIGHT;
			const double		scale = std::ldexp( 1.0, n );  // 2^n
			const std::uint64_t abs_w = static_cast<std::uint64_t>( std::llround( a * scale ) );
			if ( abs_w == 0 )
				return INFINITE_WEIGHT;
			const int msb = floor_log2_uint64( abs_w );
			return static_cast<SearchWeight>( n - msb );
		};

		for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
		{
			const auto lc = linear_x_modulo_minus_const32( alpha, constant, beta, n );
			const SearchWeight w = weight_from_corr( lc.correlation );
			if ( w >= INFINITE_WEIGHT || w > weight_cap )
			{
				if ( got.find( alpha ) != got.end() )
				{
					std::cout << "ARX Linear Analysis: [subconst-exact] FAIL: unexpected candidate alpha=0x" << std::hex << alpha << std::dec << "\n";
					return 1;
				}
				continue;
			}
			const auto it = got.find( alpha );
			if ( it == got.end() )
			{
				std::cout << "ARX Linear Analysis: [subconst-exact] FAIL: missing alpha=0x" << std::hex << alpha << std::dec << " w=" << w << "\n";
				return 1;
			}
			if ( it->second != w )
			{
				std::cout << "ARX Linear Analysis: [subconst-exact] FAIL: weight mismatch alpha=0x" << std::hex << alpha << std::dec << " got=" << it->second << " ref=" << w << "\n";
				return 1;
			}
		}

		std::cout << "ARX Linear Analysis: [subconst-exact] PASS (n=8 exhaustive)\n";
		return 0;
	}

	static int run_subconst_optimal_alpha_sanity_n6()
	{
		using TwilightDream::arx_operators::find_optimal_alpha_varconst_mod_sub;
		using TwilightDream::arx_operators::linear_x_modulo_minus_const32;

		constexpr int			n = 6;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		auto weight_from_corr = [ & ]( double corr ) -> SearchWeight {
			const double a = std::fabs( corr );
			if ( !( a > 0.0 ) || !std::isfinite( a ) )
				return INFINITE_WEIGHT;
			const double		scale = std::ldexp( 1.0, n );
			const std::uint64_t abs_w = static_cast<std::uint64_t>( std::llround( a * scale ) );
			if ( abs_w == 0 )
				return INFINITE_WEIGHT;
			const int msb = floor_log2_uint64( abs_w );
			return static_cast<SearchWeight>( n - msb );
		};

		for ( std::uint32_t constant = 0; constant <= mask; ++constant )
		{
			for ( std::uint32_t beta = 0; beta <= mask; ++beta )
			{
				std::uint64_t expected_abs_weight = 0;
				SearchWeight  expected_weight = INFINITE_WEIGHT;
				std::uint32_t expected_alpha = 0;
				for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
				{
					const auto			lc = linear_x_modulo_minus_const32( alpha, constant, beta, n );
					const double		abs_corr = std::fabs( lc.correlation );
					const std::uint64_t abs_weight = ( abs_corr > 0.0 && std::isfinite( abs_corr ) ) ? static_cast<std::uint64_t>( std::llround( abs_corr * std::ldexp( 1.0, n ) ) ) : 0u;
					const SearchWeight w = weight_from_corr( lc.correlation );
					if ( abs_weight > expected_abs_weight || ( abs_weight == expected_abs_weight && alpha < expected_alpha ) )
					{
						expected_abs_weight = abs_weight;
						expected_weight = w;
						expected_alpha = alpha;
					}
				}

				const auto got = find_optimal_alpha_varconst_mod_sub( beta, constant, n );
				if ( ( got.alpha & mask ) != expected_alpha || got.abs_weight != expected_abs_weight || got.ceil_linear_weight_int != expected_weight )
				{
					std::cout << "ARX Linear Analysis: [subconst-optimal-alpha] FAIL"
							  << " beta=0x" << std::hex << beta << " constant=0x" << constant << " expected_alpha=0x" << expected_alpha << " got_alpha=0x" << ( got.alpha & mask ) << std::dec << " expected_abs_weight=" << expected_abs_weight << " got_abs_weight=" << got.abs_weight << " expected_weight=" << expected_weight << " got_weight=" << got.ceil_linear_weight_int << "\n";
					return 1;
				}
			}
		}

		std::cout << "ARX Linear Analysis: [subconst-optimal-alpha] PASS (n=6 exhaustive)\n";
		return 0;
	}

	static int run_subconst_optimal_beta_sanity_n6()
	{
		using TwilightDream::arx_operators::find_optimal_beta_varconst_mod_add;
		using TwilightDream::arx_operators::find_optimal_beta_varconst_mod_sub;
		using TwilightDream::arx_operators::solve_fixed_alpha_q2_canonical;
		using TwilightDream::arx_operators::solve_fixed_alpha_q2_exact_transition_reference;
		using TwilightDream::arx_operators::solve_fixed_alpha_q2_raw_reference;
		using TwilightDream::arx_operators::VarConstQ2Direction;
		using TwilightDream::arx_operators::VarConstQ2MainlineRequest;
		using TwilightDream::arx_operators::VarConstQ2Operation;

		constexpr int			n = 6;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		auto weight_from_corr = [ & ]( double corr ) -> SearchWeight {
			const double a = std::fabs( corr );
			if ( !( a > 0.0 ) || !std::isfinite( a ) )
				return INFINITE_WEIGHT;
			const double		scale = std::ldexp( 1.0, n );
			const std::uint64_t abs_w = static_cast<std::uint64_t>( std::llround( a * scale ) );
			if ( abs_w == 0 )
				return INFINITE_WEIGHT;
			const int msb = floor_log2_uint64( abs_w );
			return static_cast<SearchWeight>( n - msb );
		};

		for ( int op_index = 0; op_index < 2; ++op_index )
		{
			const bool				  is_sub = ( op_index != 0 );
			const VarConstQ2Operation operation = is_sub ? VarConstQ2Operation::modular_sub : VarConstQ2Operation::modular_add;

			for ( std::uint32_t constant = 0; constant <= mask; ++constant )
			{
				for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
				{
					std::uint64_t expected_abs_weight = 0;
					SearchWeight expected_weight = INFINITE_WEIGHT;
					std::uint32_t expected_beta = 0;

					for ( std::uint32_t beta = 0; beta <= mask; ++beta )
					{
						const double		corr = is_sub ? brute_force_corr_sub_const_n( alpha, constant, beta, n ) : brute_force_corr_add_const_n( alpha, constant, beta, n );
						const double		abs_corr = std::fabs( corr );
						const std::uint64_t abs_weight = ( abs_corr > 0.0 && std::isfinite( abs_corr ) ) ? static_cast<std::uint64_t>( std::llround( abs_corr * std::ldexp( 1.0, n ) ) ) : 0u;
						const SearchWeight weight = weight_from_corr( corr );
						if ( abs_weight > expected_abs_weight || ( abs_weight == expected_abs_weight && beta < expected_beta ) )
						{
							expected_abs_weight = abs_weight;
							expected_weight = weight;
							expected_beta = beta;
						}
					}

					const auto						public_result = is_sub ? find_optimal_beta_varconst_mod_sub( alpha, constant, n ) : find_optimal_beta_varconst_mod_add( alpha, constant, n );
					const VarConstQ2MainlineRequest request { alpha, constant, n, VarConstQ2Direction::fixed_alpha_to_beta, operation };
					const auto						canonical_result = solve_fixed_alpha_q2_canonical( request, nullptr );
					const auto						transition_result = solve_fixed_alpha_q2_exact_transition_reference( request, nullptr );
					const auto						raw_result = solve_fixed_alpha_q2_raw_reference( request );

					const bool public_ok = ( ( public_result.beta & mask ) == expected_beta ) && public_result.abs_weight == expected_abs_weight && public_result.ceil_linear_weight_int == expected_weight;
					const bool canonical_ok = ( ( canonical_result.optimal_mask & mask ) == expected_beta ) && canonical_result.abs_weight == expected_abs_weight && canonical_result.ceil_linear_weight_int == expected_weight;
					const bool transition_ok = ( ( transition_result.optimal_mask & mask ) == expected_beta ) && transition_result.abs_weight == expected_abs_weight && transition_result.ceil_linear_weight_int == expected_weight;
					const bool raw_ok = ( ( raw_result.optimal_mask & mask ) == expected_beta ) && raw_result.abs_weight == expected_abs_weight && raw_result.ceil_linear_weight_int == expected_weight;

					if ( !( public_ok && canonical_ok && transition_ok && raw_ok ) )
					{
						std::cout << "ARX Linear Analysis: [subconst-optimal-beta] FAIL"
								  << " op=" << ( is_sub ? "sub" : "add" ) << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " expected_beta=0x" << expected_beta << " public_beta=0x" << ( public_result.beta & mask ) << " canonical_beta=0x" << ( canonical_result.optimal_mask & mask ) << " transition_beta=0x" << ( transition_result.optimal_mask & mask ) << " raw_beta=0x" << ( raw_result.optimal_mask & mask ) << std::dec << " expected_abs_weight=" << expected_abs_weight << " public_abs_weight=" << public_result.abs_weight << " canonical_abs_weight=" << canonical_result.abs_weight << " transition_abs_weight=" << transition_result.abs_weight << " raw_abs_weight=" << raw_result.abs_weight << " expected_weight=" << expected_weight << " public_weight=" << public_result.ceil_linear_weight_int << " canonical_weight=" << canonical_result.ceil_linear_weight_int << " transition_weight=" << transition_result.ceil_linear_weight_int << " raw_weight=" << raw_result.ceil_linear_weight_int << "\n";
						return 1;
					}
				}
			}
		}

		std::cout << "ARX Linear Analysis: [subconst-optimal-beta] PASS (n=6 exhaustive)\n";
		return 0;
	}

	static int run_linear_varconst_bnb_mode_sanity()
	{
		using TwilightDream::auto_search_linear::linear_configuration_requests_fixed_alpha_outer_hot_path_collapsed_branch;
		using TwilightDream::auto_search_linear::linear_configuration_requests_fixed_beta_outer_hot_path_collapsed_branch;
		using TwilightDream::auto_search_linear::linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_best_engine;
		using TwilightDream::auto_search_linear::linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_search;
		using TwilightDream::auto_search_linear::linear_configuration_uses_column_optimal_subconst_mode;
		using TwilightDream::auto_search_linear::linear_configuration_uses_fixed_alpha_column_optimal_subconst_mode;
		using TwilightDream::auto_search_linear::linear_configuration_uses_fixed_alpha_subconst_mode;
		using TwilightDream::auto_search_linear::linear_configuration_uses_fixed_beta_column_optimal_subconst_mode;
		using TwilightDream::auto_search_linear::linear_configuration_uses_fixed_beta_subconst_mode;
		using TwilightDream::auto_search_linear::linear_varconst_sub_bnb_mode_to_string;
		using TwilightDream::auto_search_linear::LinearBestSearchConfiguration;
		using TwilightDream::auto_search_linear::LinearVarConstSubBnbMode;

		struct ExpectedCase
		{
			LinearVarConstSubBnbMode mode;
			const char*				 name;
			bool					 uses_fixed_beta;
			bool					 uses_fixed_alpha;
			bool					 beta_column;
			bool					 alpha_column;
			bool					 integrated_in_linear_search;
			bool					 integrated_in_best_engine;
		};

		const ExpectedCase cases[] = {
			{ LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha, "fixed_beta_enumerate_alpha", true, false, false, false, true, true },
			{ LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha, "fixed_beta_column_optimal_alpha", true, false, true, false, true, true },
			{ LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta, "fixed_alpha_enumerate_beta", false, true, false, false, true, true },
			{ LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta, "fixed_alpha_column_optimal_beta", false, true, false, true, true, true },
		};

		for ( const auto& tc : cases )
		{
			const char* name = linear_varconst_sub_bnb_mode_to_string( tc.mode );
			if ( std::string( name ) != tc.name )
			{
				std::cout << "ARX Linear CLI: [varconst-bnb-mode] FAIL"
						  << " mode=" << static_cast<int>( tc.mode ) << " expected_name=" << tc.name << " got_name=" << name << "\n";
				return 1;
			}

			LinearBestSearchConfiguration config {};
			config.varconst_sub_bnb_mode = tc.mode;
			config.enable_fixed_beta_outer_hot_path_gate = tc.beta_column;
			config.enable_fixed_alpha_outer_hot_path_gate = tc.alpha_column;

			if ( linear_configuration_uses_fixed_beta_subconst_mode( config ) != tc.uses_fixed_beta ||
				 linear_configuration_uses_fixed_alpha_subconst_mode( config ) != tc.uses_fixed_alpha ||
				 linear_configuration_uses_fixed_beta_column_optimal_subconst_mode( config ) != tc.beta_column ||
				 linear_configuration_uses_fixed_alpha_column_optimal_subconst_mode( config ) != tc.alpha_column ||
				 linear_configuration_uses_column_optimal_subconst_mode( config ) != ( tc.beta_column || tc.alpha_column ) ||
				 linear_configuration_requests_fixed_beta_outer_hot_path_collapsed_branch( config ) != tc.beta_column ||
				 linear_configuration_requests_fixed_alpha_outer_hot_path_collapsed_branch( config ) != tc.alpha_column ||
				 linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_search( tc.mode ) != tc.integrated_in_linear_search ||
				 linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_best_engine( tc.mode ) != tc.integrated_in_best_engine )
			{
				std::cout << "ARX Linear CLI: [varconst-bnb-mode] FAIL semantic mismatch"
						  << " mode=" << tc.name << "\n";
				return 1;
			}
		}

		std::cout << "ARX Linear CLI: [varconst-bnb-mode] PASS\n";
		return 0;
	}

	static double brute_force_corr_add_varvar_n( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma, int n )
	{
		if ( n <= 0 )
			return 1.0;
		if ( n > 16 )
		{
			// Keep brute force small and auditable.
			std::cerr << "brute_force_corr_add_varvar_n only supports n<=16\n";
			return 0.0;
		}

		const std::uint32_t mask = ( 1u << n ) - 1u;
		alpha &= mask;
		beta &= mask;
		gamma &= mask;

		const std::uint32_t domain_size = ( 1u << n );
		std::int64_t		sum = 0;
		for ( std::uint32_t x = 0; x < domain_size; ++x )
		{
			for ( std::uint32_t y = 0; y < domain_size; ++y )
			{
				const std::uint32_t z = ( x + y ) & mask;
				const int			e = parity_u32( alpha & x ) ^ parity_u32( beta & y ) ^ parity_u32( gamma & z );
				sum += ( e ? -1 : 1 );
			}
		}

		// Denominator is 2^(2n), exact as dyadic rational.
		const double denom = std::ldexp( 1.0, 2 * n );
		return static_cast<double>( sum ) / denom;
	}

	static std::uint32_t eq_mask_n( std::uint32_t x, std::uint32_t y, int n )
	{
		if ( n <= 0 )
			return 0u;
		const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
		return ( ~( x ^ y ) ) & mask;
	}

	static std::uint32_t cpm_naive_u32( std::uint32_t x, std::uint32_t y, int n )
	{
		if ( n <= 0 )
			return 0u;
		const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
		x &= mask;
		y &= mask;
		if ( x == 0u )
			return 0u;

		auto msb = []( std::uint32_t v ) -> int {
			return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
		};
		auto strip1 = [ & ]( std::uint32_t v ) -> std::uint32_t {
			const int m = msb( v );
			return ( m < 0 ) ? 0u : ( v & ~( 1u << m ) );
		};
		auto cpmki = [ & ]( std::uint32_t vec, int k, int i ) -> std::uint32_t {
			if ( i <= 0 || k < 0 )
				return 0u;
			const int end = k + i - 1;
			if ( k >= n || end < 0 || end >= n )
				return 0u;
			std::uint32_t out = 0u;
			bool		  all_ones_above = true;
			for ( int j = end; j >= k; --j )
			{
				if ( all_ones_above )
					out |= ( 1u << j );
				all_ones_above = all_ones_above && ( ( ( vec >> j ) & 1u ) != 0u );
			}
			return out & mask;
		};

		std::function<std::uint32_t( std::uint32_t )> rec = [ & ]( std::uint32_t xx ) -> std::uint32_t {
			xx &= mask;
			if ( xx == 0u )
				return 0u;
			const int			j = msb( xx );
			const std::uint32_t xs1 = strip1( xx );
			const int			k = ( xs1 != 0u ) ? msb( xs1 ) : 0;
			const int			i = j - k;
			const std::uint32_t z = cpmki( y, k, i );
			const bool			z_subset_y = ( ( z & y ) == z );
			std::uint32_t		next = strip1( xx );
			if ( z_subset_y )
				next = strip1( next );
			return ( z ^ rec( next ) ) & mask;
		};

		return rec( x ) & mask;
	}

	static double brute_force_corr_add_const_n( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n )
	{
		if ( n <= 0 )
			return 1.0;
		if ( n > 16 )
		{
			std::cerr << "brute_force_corr_add_const_n only supports n<=16\n";
			return 0.0;
		}

		const std::uint32_t mask = ( 1u << n ) - 1u;
		alpha &= mask;
		constant &= mask;
		beta &= mask;

		const std::uint32_t domain_size = ( 1u << n );
		std::int64_t		sum = 0;
		for ( std::uint32_t x = 0; x < domain_size; ++x )
		{
			const std::uint32_t z = ( x + constant ) & mask;
			const int			e = parity_u32( alpha & x ) ^ parity_u32( beta & z );
			sum += ( e ? -1 : 1 );
		}

		const double denom = std::ldexp( 1.0, n );
		return static_cast<double>( sum ) / denom;
	}

	static double brute_force_corr_sub_const_n( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int n )
	{
		if ( n <= 0 )
			return 1.0;
		if ( n > 16 )
		{
			std::cerr << "brute_force_corr_sub_const_n only supports n<=16\n";
			return 0.0;
		}

		const std::uint32_t mask = ( 1u << n ) - 1u;
		alpha &= mask;
		constant &= mask;
		beta &= mask;

		const std::uint32_t domain_size = ( 1u << n );
		std::int64_t		sum = 0;
		for ( std::uint32_t x = 0; x < domain_size; ++x )
		{
			const std::uint32_t z = ( x - constant ) & mask;
			const int			e = parity_u32( alpha & x ) ^ parity_u32( beta & z );
			sum += ( e ? -1 : 1 );
		}

		const double denom = std::ldexp( 1.0, n );
		return static_cast<double>( sum ) / denom;
	}

}  // namespace

int run_linear_search_self_test( std::uint64_t seed, std::size_t extra_cases );

int run_arx_operator_self_test()
{
    std::cout << "[SelfTest] ARX analysis operators validation\n";
    std::cout << "[SelfTest][Section] Operator Layer\n";

	// ------------------------------------------------------------
	// 6) Linear correlations of modular addition (exact, small-n brute-force)
	//    + Wallén Θ(log n) weight (32-bit) vs exact DP (random)
	// ------------------------------------------------------------
	{
		if ( run_cpm_logn_sanity_n8_exhaustive() != 0 )
		{
			std::cout << "ARX Linear Analysis: [cpm(logn)] FAIL\n";
			return 22;
		}
	}

	{
		if ( run_subconst_exact_enum_sanity_n8() != 0 )
		{
			std::cout << "ARX Linear Analysis: [subconst-exact] FAIL\n";
			return 23;
		}
	}

	{
		if ( run_subconst_optimal_alpha_sanity_n6() != 0 )
		{
			std::cout << "ARX Linear Analysis: [subconst-optimal-alpha] FAIL\n";
			return 24;
		}
	}

	{
		if ( run_subconst_optimal_beta_sanity_n6() != 0 )
		{
			std::cout << "ARX Linear Analysis: [subconst-optimal-beta] FAIL\n";
			return 25;
		}
	}

	{
		if ( run_linear_varconst_bnb_mode_sanity() != 0 )
		{
			std::cout << "ARX Linear CLI: [varconst-bnb-mode] FAIL\n";
			return 26;
		}
	}

	{
		constexpr int			n = 4;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
		{
			for ( std::uint32_t beta = 0; beta <= mask; ++beta )
			{
				for ( std::uint32_t gamma = 0; gamma <= mask; ++gamma )
				{
					const double brute = brute_force_corr_add_varvar_n( alpha, beta, gamma, n );
					const auto	 lc = TwilightDream::arx_operators::linear_add_varvar32( alpha, beta, gamma, n );
					if ( lc.correlation != brute )
					{
						std::cout << "ARX Linear Analysis: [AddVarVarLinear] FAIL n=" << n << " alpha=0x" << std::hex << alpha << " beta=0x" << beta << " gamma=0x" << gamma << std::dec << " brute=" << std::setprecision( 17 ) << brute << " dp=" << lc.correlation << "\n";
						return 15;
					}
				}
			}
		}
		std::cout << "ARX Linear Analysis: [AddVarVarLinear] PASS (n=4 exhaustive)\n";
	}

	{
		constexpr int			n = 4;
		constexpr std::uint32_t mask = ( 1u << n ) - 1u;

		for ( std::uint32_t constant = 0; constant <= mask; ++constant )
		{
			for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
			{
				for ( std::uint32_t beta = 0; beta <= mask; ++beta )
				{
					const double brute_add = brute_force_corr_add_const_n( alpha, constant, beta, n );
					const auto	 lc_add = TwilightDream::arx_operators::linear_x_modulo_plus_const32( alpha, constant, beta, n );
					if ( lc_add.correlation != brute_add )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinear] FAIL (add) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute=" << std::setprecision( 17 ) << brute_add << " dp=" << lc_add.correlation << "\n";
						return 16;
					}

					const double flat_add = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat( alpha, constant, beta, n );
					if ( flat_add != brute_add )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearFlat] FAIL (add) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute=" << std::setprecision( 17 ) << brute_add << " flat=" << flat_add << "\n";
						return 23;
					}

					const auto flat_add_dyadic = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat_dyadic( alpha, constant, beta, n );
					const auto numer_add = TwilightDream::arx_operators::correlation_add_const_exact_numerator_logdepth( alpha, constant, beta, n );
					if ( !( numer_add == flat_add_dyadic.numerator ) )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthNumerator] FAIL (add) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << "\n";
						return 27;
					}

					const SearchWeight ceil_add_logdepth = TwilightDream::arx_operators::correlation_add_const_weight_ceil_int_logdepth( alpha, constant, beta, n );
					const SearchWeight ceil_add_flat = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat_weight_ceil_int( alpha, constant, beta, n );
					if ( ceil_add_logdepth != ceil_add_flat )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthWeight] FAIL (add) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " logdepth=" << ceil_add_logdepth << " flat=" << ceil_add_flat << "\n";
						return 28;
					}

					const bool nz_add_logdepth = TwilightDream::arx_operators::correlation_add_const_has_nonzero_correlation_logdepth( alpha, constant, beta, n );
					if ( nz_add_logdepth != !flat_add_dyadic.numerator.is_zero() )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthDecision] FAIL (add) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << "\n";
						return 29;
					}

					// Windowed estimator: must satisfy certified bound (Theorem 5.3 style).
					for ( int L = 0; L <= n; ++L )
					{
						const auto	 rep = TwilightDream::arx_operators::linear_correlation_add_const_flat_bin_report( alpha, constant, beta, n, L );
						const double chat = rep.corr_hat.as_double();
						const double diff = std::fabs( chat - brute_add );
						const double delta = ( double )rep.delta_bound;
						if ( diff > delta + 1e-12 )
						{
							std::cout << "ARX Linear Analysis: [AddConstLinearFlatBinBound] FAIL n=" << n << " L=" << L << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute=" << std::setprecision( 17 ) << brute_add << " chat=" << chat << " diff=" << diff << " delta=" << delta << "\n";
							return 25;
						}
					}

					// Binary-Lift + window: uses beta_res, enforces L>=2; must satisfy bound too.
					for ( int L = 0; L <= n; ++L )
					{
						const auto			liftm = TwilightDream::arx_operators::binary_lift_addconst_masks( alpha, beta, constant, n );
						const std::uint32_t alpha_res = ( std::uint32_t )( ( liftm.t_res ^ liftm.beta_res ) & ( std::uint64_t )mask );
						const std::uint32_t beta_res = ( std::uint32_t )( liftm.beta_res & ( std::uint64_t )mask );
						const double		brute_lifted = brute_force_corr_add_const_n( alpha_res, constant, beta_res, n );

						const auto	 lifted = TwilightDream::arx_operators::corr_add_const_binary_lifted_report( alpha, beta, constant, n, L );
						const double chat = lifted.residual.corr_hat.as_double();
						const double diff = std::fabs( chat - brute_lifted );
						const double delta = ( double )lifted.residual.delta_bound;
						if ( diff > delta + 1e-12 )
						{
							std::cout << "ARX Linear Analysis: [AddConstLinearLiftedBound] FAIL n=" << n << " L=" << L << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute_lifted=" << std::setprecision( 17 ) << brute_lifted << " chat=" << chat << " diff=" << diff << " delta=" << delta << "\n";
							return 26;
						}
					}

					const double brute_sub = brute_force_corr_sub_const_n( alpha, constant, beta, n );
					const auto	 lc_sub = TwilightDream::arx_operators::linear_x_modulo_minus_const32( alpha, constant, beta, n );
					if ( lc_sub.correlation != brute_sub )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinear] FAIL (sub) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute=" << std::setprecision( 17 ) << brute_sub << " dp=" << lc_sub.correlation << "\n";
						return 17;
					}

					// Flat evaluator uses addition by two's complement.
					const std::uint32_t neg_constant = ( ( ~constant ) + 1u ) & mask;
					const double		flat_sub = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat( alpha, neg_constant, beta, n );
					if ( flat_sub != brute_sub )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearFlat] FAIL (sub) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " brute=" << std::setprecision( 17 ) << brute_sub << " flat=" << flat_sub << "\n";
						return 24;
					}

					const auto flat_sub_dyadic = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat_dyadic( alpha, neg_constant, beta, n );
					const auto numer_sub = TwilightDream::arx_operators::correlation_sub_const_exact_numerator_logdepth( alpha, constant, beta, n );
					if ( !( numer_sub == flat_sub_dyadic.numerator ) )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthNumerator] FAIL (sub) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << "\n";
						return 30;
					}

					const SearchWeight ceil_sub_logdepth = TwilightDream::arx_operators::correlation_sub_const_weight_ceil_int_logdepth( alpha, constant, beta, n );
					const SearchWeight ceil_sub_flat = TwilightDream::arx_operators::linear_correlation_add_const_exact_flat_weight_ceil_int( alpha, neg_constant, beta, n );
					if ( ceil_sub_logdepth != ceil_sub_flat )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthWeight] FAIL (sub) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << " logdepth=" << ceil_sub_logdepth << " flat=" << ceil_sub_flat << "\n";
						return 31;
					}

					const bool nz_sub_logdepth = TwilightDream::arx_operators::correlation_sub_const_has_nonzero_correlation_logdepth( alpha, constant, beta, n );
					if ( nz_sub_logdepth != !flat_sub_dyadic.numerator.is_zero() )
					{
						std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthDecision] FAIL (sub) n=" << n << " alpha=0x" << std::hex << alpha << " constant=0x" << constant << " beta=0x" << beta << std::dec << "\n";
						return 32;
					}
				}
			}
		}
		std::cout << "ARX Linear Analysis: [AddConstLinear] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearFlat] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthNumerator] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthWeight] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearLogdepthDecision] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearFlatBinBound] PASS (n=4 exhaustive)\n";
		std::cout << "ARX Linear Analysis: [AddConstLinearLiftedBound] PASS (n=4 exhaustive)\n";
	}

	{
		std::mt19937								 rng( 0x1A2B3C4Du );
		std::uniform_int_distribution<std::uint32_t> dist( 0u, 0xFFFFFFFFu );

		for ( int t = 0; t < 10000; ++t )
		{
			const std::uint32_t u = dist( rng );  // output mask
			const std::uint32_t v = dist( rng );  // input mask x
			const std::uint32_t w = dist( rng );  // input mask y

			const SearchWeight wallen_w = TwilightDream::arx_operators::internal_addition_wallen_logn( u, v, w );
			const auto	 lc = TwilightDream::arx_operators::linear_add_varvar32( v, w, u, 32 );
			const double corr = lc.correlation;

			// Extra sanity: check cpm computed inside Wallén formula against a direct Definition 6 implementation.
			const std::uint32_t vprime = v ^ u;
			const std::uint32_t wprime = w ^ u;
			const std::uint32_t eqvw = eq_mask_n( vprime, wprime, 32 );
			const std::uint32_t z_ref = cpm_naive_u32( u, eqvw, 32 );
			const int			expected_feasible = ( ( vprime & ~z_ref ) == 0u && ( wprime & ~z_ref ) == 0u ) ? 1 : 0;

			SearchWeight expected_w = INFINITE_WEIGHT;
			if ( corr != 0.0 )
			{
				int			 exp = 0;
				const double m = std::frexp( std::fabs( corr ), &exp );
				if ( m != 0.5 )
				{
					std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL (corr not power-of-two)"
							  << " corr=" << std::setprecision( 17 ) << corr << "\n";
					return 18;
				}
				expected_w = static_cast<SearchWeight>( 1 - exp );  // because frexp(|corr|)=0.5*2^exp
			}

			if ( wallen_w != expected_w )
			{
				std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL weight mismatch"
						  << " u=0x" << std::hex << u << " v=0x" << v << " w=0x" << w << std::dec
						  << " wallen=" << display_search_weight( wallen_w )
						  << " expected=" << display_search_weight( expected_w )
						  << " corr=" << std::setprecision( 17 ) << corr << " expected_feasible=" << expected_feasible << " z_ref=0x" << std::hex << z_ref << std::dec << "\n";
				return 19;
			}

			if ( wallen_w < INFINITE_WEIGHT )
			{
				const double abs_corr = std::fabs( corr );
				const double abs_expected = std::ldexp( 1.0, -wallen_w );
				if ( abs_corr != abs_expected )
				{
					std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL |corr| mismatch"
							  << " wallen=" << display_search_weight( wallen_w ) << " |corr|=" << std::setprecision( 17 ) << abs_corr << " expected=" << abs_expected << "\n";
					return 20;
				}
			}
			else
			{
				if ( corr != 0.0 )
				{
					std::cout << "ARX Linear Analysis: [WallenLogn32] FAIL expected corr=0 for infeasible case\n";
					return 21;
				}
			}
		}
		std::cout << "ARX Linear Analysis: [WallenLogn32] PASS (n=32 random)\n";
	}

	std::cout << "[SelfTest][Section] PASS Operator Layer\n";

	std::cout << "[SelfTest][Section] Linear BNB Math Layer\n";
	if ( run_linear_search_self_test( 0x123456789ABCDEF0ull, 0u ) != 0 )
	{
		std::cout << "[SelfTest][Section] FAIL Linear BNB Math Layer\n";
		return 33;
	}
	std::cout << "[SelfTest][Section] PASS Linear BNB Math Layer\n";
    std::cout << "[SelfTest][Summary] operator_layer=PASS linear_bnb_math=PASS\n";
    std::cout << "[SelfTest] PASS\n\n";
    return 0;
}

namespace
{

	namespace linear_search_self_test
	{

		using namespace TwilightDream::auto_search_linear;

		struct LinearTestCase
		{
			std::uint32_t output_mask_u = 0;
			SearchWeight  weight_cap = 0;
			int			  word_bits = 32;
		};

		struct CandidateKey
		{
			std::uint32_t x = 0;
			std::uint32_t y = 0;
			SearchWeight  w = 0;
		};

		struct CandidateKeyHash
		{
			std::size_t operator()( const CandidateKey& k ) const noexcept
			{
				const std::size_t h1 = std::hash<std::uint32_t> {}( k.x );
				const std::size_t h2 = std::hash<std::uint32_t> {}( k.y );
				const std::size_t h3 = std::hash<SearchWeight> {}( k.w );
				return ( h1 * 1315423911u ) ^ ( h2 + 0x9e3779b97f4a7c15ull ) ^ ( h3 << 1 );
			}
		};

		struct ExactMaskOrderEntry
		{
			std::uint32_t mask = 0;
			std::uint64_t abs_weight = 0;
			SearchWeight  ceil_weight = INFINITE_WEIGHT;
		};

		static inline bool operator==( const CandidateKey& a, const CandidateKey& b )
		{
			return a.x == b.x && a.y == b.y && a.w == b.w;
		}

		static CandidateKey make_key( const AddCandidate& c )
		{
			return CandidateKey { c.input_mask_x, c.input_mask_y, c.linear_weight };
		}

		static std::string format_word32_hex( std::uint32_t v )
		{
			std::ostringstream oss;
			oss << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << v << std::dec;
			return oss.str();
		}

		static std::uint64_t exact_abs_weight_from_corr_small_n( double corr, int n )
		{
			const double a = std::fabs( corr );
			if ( !( a > 0.0 ) || !std::isfinite( a ) )
				return 0;
			return static_cast<std::uint64_t>( std::llround( a * std::ldexp( 1.0, n ) ) );
		}

		static std::vector<ExactMaskOrderEntry> build_fixed_beta_varconst_sub_oracle_order(
			std::uint32_t beta,
			std::uint32_t constant,
			int n_bits,
			SearchWeight weight_cap )
		{
			std::vector<ExactMaskOrderEntry> out;
			if ( n_bits <= 0 || n_bits > 16 )
				return out;
			const std::uint32_t mask = ( 1u << n_bits ) - 1u;
			beta &= mask;
			constant &= mask;
			out.reserve( std::size_t( 1u ) << n_bits );

			for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
			{
				const double corr =
					brute_force_corr_sub_const_n( alpha, constant, beta, n_bits );
				const std::uint64_t abs_weight =
					exact_abs_weight_from_corr_small_n( corr, n_bits );
				if ( abs_weight == 0 )
					continue;
				const SearchWeight ceil_weight = static_cast<SearchWeight>( n_bits - floor_log2_uint64( abs_weight ) );
				if ( ceil_weight > weight_cap )
					continue;
				out.push_back( ExactMaskOrderEntry { alpha, abs_weight, ceil_weight } );
			}

			std::sort( out.begin(), out.end(), []( const ExactMaskOrderEntry& a, const ExactMaskOrderEntry& b ) {
				if ( a.abs_weight != b.abs_weight )
					return a.abs_weight > b.abs_weight;
				return a.mask < b.mask;
			} );
			return out;
		}

		static std::vector<ExactMaskOrderEntry> build_fixed_alpha_varconst_sub_oracle_order(
			std::uint32_t alpha,
			std::uint32_t constant,
			int n_bits,
			SearchWeight weight_cap )
		{
			std::vector<ExactMaskOrderEntry> out;
			if ( n_bits <= 0 || n_bits > 16 )
				return out;
			const std::uint32_t mask = ( 1u << n_bits ) - 1u;
			alpha &= mask;
			constant &= mask;
			out.reserve( std::size_t( 1u ) << n_bits );

			for ( std::uint32_t beta = 0; beta <= mask; ++beta )
			{
				const double corr =
					brute_force_corr_sub_const_n( alpha, constant, beta, n_bits );
				const std::uint64_t abs_weight =
					exact_abs_weight_from_corr_small_n( corr, n_bits );
				if ( abs_weight == 0 )
					continue;
				const SearchWeight ceil_weight = static_cast<SearchWeight>( n_bits - floor_log2_uint64( abs_weight ) );
				if ( ceil_weight > weight_cap )
					continue;
				out.push_back( ExactMaskOrderEntry { beta, abs_weight, ceil_weight } );
			}

			std::sort( out.begin(), out.end(), []( const ExactMaskOrderEntry& a, const ExactMaskOrderEntry& b ) {
				if ( a.abs_weight != b.abs_weight )
					return a.abs_weight > b.abs_weight;
				return a.mask < b.mask;
			} );
			return out;
		}

		static bool compare_exact_mask_order(
			const char* label,
			std::uint32_t fixed_mask,
			std::uint32_t constant,
			const std::vector<ExactMaskOrderEntry>& expected,
			const std::vector<ExactMaskOrderEntry>& actual )
		{
			if ( expected.size() != actual.size() )
			{
				std::cerr << "[SelfTest][Linear][VarConstQ2] " << label
						  << " count mismatch fixed_mask=" << format_word32_hex( fixed_mask )
						  << " constant=" << format_word32_hex( constant )
						  << " expected=" << expected.size()
						  << " actual=" << actual.size() << "\n";
				return false;
			}

			for ( std::size_t i = 0; i < expected.size(); ++i )
			{
				if ( expected[ i ].mask != actual[ i ].mask ||
					 expected[ i ].abs_weight != actual[ i ].abs_weight ||
					 expected[ i ].ceil_weight != actual[ i ].ceil_weight )
				{
					std::cerr << "[SelfTest][Linear][VarConstQ2] " << label
							  << " mismatch fixed_mask=" << format_word32_hex( fixed_mask )
							  << " constant=" << format_word32_hex( constant )
							  << " index=" << i
							  << " expected_mask=" << format_word32_hex( expected[ i ].mask )
							  << " actual_mask=" << format_word32_hex( actual[ i ].mask )
							  << " expected_abs=" << expected[ i ].abs_weight
							  << " actual_abs=" << actual[ i ].abs_weight
							  << " expected_w=" << expected[ i ].ceil_weight
							  << " actual_w=" << actual[ i ].ceil_weight
							  << "\n";
					return false;
				}
			}
			return true;
		}

		static void print_linear_case( std::ostream& os, const LinearTestCase& c )
		{
			os << "output_mask_u=" << format_word32_hex( c.output_mask_u ) << " weight_cap=" << c.weight_cap << " word_bits=" << c.word_bits;
		}

		static bool find_duplicate_candidate( const std::vector<AddCandidate>& candidates, std::size_t& index_out, CandidateKey& key_out )
		{
			std::unordered_set<CandidateKey, CandidateKeyHash> seen;
			seen.reserve( candidates.size() * 2u + 1u );
			for ( std::size_t i = 0; i < candidates.size(); ++i )
			{
				const CandidateKey key = make_key( candidates[ i ] );
				if ( !seen.insert( key ).second )
				{
					index_out = i;
					key_out = key;
					return true;
				}
			}
			return false;
		}

		static std::unordered_map<SearchWeight, std::size_t> build_weight_multiset( const std::vector<AddCandidate>& candidates )
		{
			std::unordered_map<SearchWeight, std::size_t> counts;
			counts.reserve( candidates.size() + 1u );
			for ( const auto& c : candidates )
			{
				++counts[ c.linear_weight ];
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

		static bool check_non_decreasing_weight( const LinearTestCase& tc, const std::vector<AddCandidate>& candidates, const char* label, const char* test_label )
		{
			if ( candidates.empty() )
			{
				return true;
			}
			for ( std::size_t i = 1; i < candidates.size(); ++i )
			{
				if ( candidates[ i ].linear_weight < candidates[ i - 1 ].linear_weight )
				{
					std::cerr << "[SelfTest][Linear] " << test_label << " ordering violation in " << label << ": ";
					print_linear_case( std::cerr, tc );
					std::cerr << " at index " << i << " prev_w=" << candidates[ i - 1 ].linear_weight << " curr_w=" << candidates[ i ].linear_weight << "\n";
					return false;
				}
			}
			return true;
		}

		static bool compare_candidate_sets( const LinearTestCase& tc, const std::vector<AddCandidate>& expected, const std::vector<AddCandidate>& actual, const char* expected_label, const char* actual_label, const char* test_label )
		{
			bool ok = true;
			bool header_printed = false;
			auto print_header = [ & ]() {
				if ( header_printed )
				{
					return;
				}
				std::cerr << "[SelfTest][Linear] " << test_label << " mismatch: ";
				print_linear_case( std::cerr, tc );
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
				std::size_t	 dup_index = 0;
				CandidateKey dup_key {};
				if ( find_duplicate_candidate( expected, dup_index, dup_key ) )
				{
					print_header();
					std::cerr << "  duplicate in " << expected_label << " at index " << dup_index << " x=" << format_word32_hex( dup_key.x ) << " y=" << format_word32_hex( dup_key.y ) << " w=" << dup_key.w << "\n";
					ok = false;
				}
				if ( find_duplicate_candidate( actual, dup_index, dup_key ) )
				{
					print_header();
					std::cerr << "  duplicate in " << actual_label << " at index " << dup_index << " x=" << format_word32_hex( dup_key.x ) << " y=" << format_word32_hex( dup_key.y ) << " w=" << dup_key.w << "\n";
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
				std::unordered_set<CandidateKey, CandidateKeyHash> expected_set;
				std::unordered_set<CandidateKey, CandidateKeyHash> actual_set;
				expected_set.reserve( expected.size() * 2u + 1u );
				actual_set.reserve( actual.size() * 2u + 1u );
				for ( const auto& c : expected )
				{
					expected_set.insert( make_key( c ) );
				}
				for ( const auto& c : actual )
				{
					actual_set.insert( make_key( c ) );
				}

				for ( const auto& c : expected )
				{
					if ( actual_set.find( make_key( c ) ) == actual_set.end() )
					{
						print_header();
						std::cerr << "  missing candidate x=" << format_word32_hex( c.input_mask_x ) << " y=" << format_word32_hex( c.input_mask_y ) << " w=" << c.linear_weight << "\n";
						ok = false;
						break;
					}
				}
				for ( const auto& c : actual )
				{
					if ( expected_set.find( make_key( c ) ) == expected_set.end() )
					{
						print_header();
						std::cerr << "  unexpected candidate x=" << format_word32_hex( c.input_mask_x ) << " y=" << format_word32_hex( c.input_mask_y ) << " w=" << c.linear_weight << "\n";
						ok = false;
						break;
					}
				}
			}

			return ok;
		}

		static std::vector<AddCandidate> generate_oracle_candidates_small_n( std::uint32_t output_mask_u, int word_bits )
		{
			std::vector<AddCandidate> out;
			if ( word_bits <= 0 || word_bits > 16 )
			{
				return out;
			}
			const std::uint32_t mask = ( word_bits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			output_mask_u &= mask;
			const std::uint32_t limit = ( word_bits >= 32 ) ? 0u : ( 1u << word_bits );
			out.reserve( std::size_t( limit ) * std::size_t( limit ) );

			for ( std::uint32_t v = 0; v < limit; ++v )
			{
				for ( std::uint32_t w = 0; w < limit; ++w )
				{
					std::uint32_t z_mask = 0;
					int			  z_next = 0;
					for ( int i = word_bits - 2; i >= 0; --i )
					{
						const int u_ip1 = int( ( output_mask_u >> ( i + 1 ) ) & 1u );
						const int v_ip1 = int( ( v >> ( i + 1 ) ) & 1u );
						const int w_ip1 = int( ( w >> ( i + 1 ) ) & 1u );
						const int z_i = z_next ^ u_ip1 ^ v_ip1 ^ w_ip1;
						if ( z_i )
						{
							z_mask |= ( 1u << i );
						}
						z_next = z_i;
					}

					bool ok = true;
					for ( int i = 0; i < word_bits; ++i )
					{
						const int z_i = ( i == word_bits - 1 ) ? 0 : int( ( z_mask >> i ) & 1u );
						const int u_i = int( ( output_mask_u >> i ) & 1u );
						const int v_i = int( ( v >> i ) & 1u );
						const int w_i = int( ( w >> i ) & 1u );
						if ( z_i == 0 && ( ( u_i ^ v_i ) != 0 || ( u_i ^ w_i ) != 0 ) )
						{
							ok = false;
							break;
						}
					}

					if ( ok )
					{
						const SearchWeight weight = static_cast<SearchWeight>( std::popcount( z_mask ) );
						out.push_back( AddCandidate { v, w, weight } );
					}
				}
			}

			return out;
		}

		static std::vector<AddCandidate> filter_candidates_by_word_bits( const std::vector<AddCandidate>& candidates, int word_bits )
		{
			std::vector<AddCandidate> out;
			if ( word_bits <= 0 || word_bits > 32 )
			{
				return out;
			}
			const std::uint32_t mask = ( word_bits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			out.reserve( candidates.size() );
			for ( const auto& c : candidates )
			{
				if ( ( c.input_mask_x & ~mask ) != 0u || ( c.input_mask_y & ~mask ) != 0u )
				{
					continue;
				}
				out.push_back( AddCandidate { c.input_mask_x & mask, c.input_mask_y & mask, c.linear_weight } );
			}
			return out;
		}

		static std::vector<LinearTestCase> make_fixed_linear_equivalence_cases()
		{
			const SearchWeight weight_cap = 5;
			return { LinearTestCase { 0x00000000u, weight_cap, 32 }, LinearTestCase { 0x00000001u, weight_cap, 32 }, LinearTestCase { 0x00000003u, weight_cap, 32 }, LinearTestCase { 0x0000000Fu, weight_cap, 32 }, LinearTestCase { 0x00000080u, weight_cap, 32 }, LinearTestCase { 0x00008000u, weight_cap, 32 }, LinearTestCase { 0x00010001u, weight_cap, 32 }, LinearTestCase { 0x00FF00FFu, weight_cap, 32 }, LinearTestCase { 0x80000000u, weight_cap, 32 }, LinearTestCase { 0x40000000u, weight_cap, 32 } };
		}

		static std::vector<LinearTestCase> make_fixed_linear_oracle_cases()
		{
			const int word_bits = 8;
			const SearchWeight weight_cap = 8;
			return { LinearTestCase { 0x00000000u, weight_cap, word_bits }, LinearTestCase { 0x00000001u, weight_cap, word_bits }, LinearTestCase { 0x00000003u, weight_cap, word_bits }, LinearTestCase { 0x00000005u, weight_cap, word_bits }, LinearTestCase { 0x0000000Fu, weight_cap, word_bits }, LinearTestCase { 0x00000033u, weight_cap, word_bits }, LinearTestCase { 0x00000055u, weight_cap, word_bits }, LinearTestCase { 0x00000080u, weight_cap, word_bits }, LinearTestCase { 0x000000A5u, weight_cap, word_bits }, LinearTestCase { 0x000000FFu, weight_cap, word_bits } };
		}

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

		static std::array<std::uint32_t, 32> canonical_basis_by_msb( const InjectionCorrelationTransition& transition )
		{
			std::array<std::uint32_t, 32> basis_by_msb {};
			for ( const std::uint32_t v : transition.basis_vectors )
			{
				if ( v != 0u )
				{
					xor_basis_add_32_selftest( basis_by_msb, v );
				}
			}
			return basis_by_msb;
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

		static InjectionCorrelationTransition build_direct_injection_transition_oracle_branch_b( std::uint32_t output_mask_u )
		{
			InjectionCorrelationTransition transition {};
			if ( output_mask_u == 0u )
			{
				return transition;
			}

			const std::uint32_t f0 = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 0u );

			{
				const unsigned g0 = static_cast<unsigned>( parity_u32( output_mask_u & f0 ) );
				std::uint32_t  offset_mask = 0u;
				for ( int i = 0; i < 32; ++i )
				{
					const std::uint32_t fi = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << i );
					const unsigned		gi = static_cast<unsigned>( parity_u32( output_mask_u & fi ) );
					if ( ( gi ^ g0 ) != 0u )
					{
						offset_mask ^= ( 1u << i );
					}
				}
				transition.offset_mask = offset_mask;
			}

			std::array<std::uint32_t, 32> rows {};
			for ( int i = 0; i < 31; ++i )
			{
				const std::uint32_t fi = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << i );
				for ( int j = i + 1; j < 32; ++j )
				{
					const std::uint32_t fj = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << j );
					const std::uint32_t fij = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( ( 1u << i ) ^ ( 1u << j ) );
					const std::uint32_t delta = f0 ^ fi ^ fj ^ fij;
					if ( parity_u32( output_mask_u & delta ) != 0 )
					{
						rows[ std::size_t( i ) ] ^= ( 1u << j );
						rows[ std::size_t( j ) ] ^= ( 1u << i );
					}
				}
			}

			std::array<std::uint32_t, 32> basis_by_msb {};
			int							  rank = 0;
			for ( const std::uint32_t row : rows )
			{
				if ( row != 0u )
				{
					rank += xor_basis_add_32_selftest( basis_by_msb, row );
				}
			}
			transition.rank = rank;
			transition.weight = ( rank + 1 ) / 2;
			transition.basis_vectors = pack_basis_vectors_desc( basis_by_msb );
			return transition;
		}

		static InjectionCorrelationTransition build_direct_injection_transition_oracle_branch_a( std::uint32_t output_mask_u )
		{
			InjectionCorrelationTransition transition {};
			if ( output_mask_u == 0u )
			{
				return transition;
			}

			const std::uint32_t f0 = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 0u );

			{
				const unsigned g0 = static_cast<unsigned>( parity_u32( output_mask_u & f0 ) );
				std::uint32_t  offset_mask = 0u;
				for ( int i = 0; i < 32; ++i )
				{
					const std::uint32_t fi = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << i );
					const unsigned		gi = static_cast<unsigned>( parity_u32( output_mask_u & fi ) );
					if ( ( gi ^ g0 ) != 0u )
					{
						offset_mask ^= ( 1u << i );
					}
				}
				transition.offset_mask = offset_mask;
			}

			std::array<std::uint32_t, 32> rows {};
			for ( int i = 0; i < 31; ++i )
			{
				const std::uint32_t fi = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << i );
				for ( int j = i + 1; j < 32; ++j )
				{
					const std::uint32_t fj = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << j );
					const std::uint32_t fij = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( ( 1u << i ) ^ ( 1u << j ) );
					const std::uint32_t delta = f0 ^ fi ^ fj ^ fij;
					if ( parity_u32( output_mask_u & delta ) != 0 )
					{
						rows[ std::size_t( i ) ] ^= ( 1u << j );
						rows[ std::size_t( j ) ] ^= ( 1u << i );
					}
				}
			}

			std::array<std::uint32_t, 32> basis_by_msb {};
			int							  rank = 0;
			for ( const std::uint32_t row : rows )
			{
				if ( row != 0u )
				{
					rank += xor_basis_add_32_selftest( basis_by_msb, row );
				}
			}
			transition.rank = rank;
			transition.weight = ( rank + 1 ) / 2;
			transition.basis_vectors = pack_basis_vectors_desc( basis_by_msb );
			return transition;
		}

		static bool compare_injection_transition( const char* branch_label, std::uint32_t output_mask_u, const InjectionCorrelationTransition& oracle, const InjectionCorrelationTransition& actual )
		{
			bool ok = true;
			bool header_printed = false;
			auto print_header = [ & ]() {
				if ( header_printed )
				{
					return;
				}
				std::cerr << "[SelfTest][Linear][Injection] " << branch_label << " mismatch for output_mask_u=" << format_word32_hex( output_mask_u ) << "\n";
				header_printed = true;
			};

			if ( oracle.offset_mask != actual.offset_mask )
			{
				print_header();
				std::cerr << "  offset mismatch oracle=" << format_word32_hex( oracle.offset_mask ) << " actual=" << format_word32_hex( actual.offset_mask ) << "\n";
				ok = false;
			}
			if ( oracle.rank != actual.rank )
			{
				print_header();
				std::cerr << "  rank mismatch oracle=" << oracle.rank << " actual=" << actual.rank << "\n";
				ok = false;
			}
			if ( oracle.weight != actual.weight )
			{
				print_header();
				std::cerr << "  weight mismatch oracle=" << oracle.weight << " actual=" << actual.weight << "\n";
				ok = false;
			}

			const auto oracle_basis = canonical_basis_by_msb( oracle );
			const auto actual_basis = canonical_basis_by_msb( actual );

			int oracle_basis_count = 0;
			int actual_basis_count = 0;
			for ( const std::uint32_t v : oracle.basis_vectors )
			{
				if ( v != 0u )
				{
					++oracle_basis_count;
					if ( !basis_span_contains( actual_basis, v ) )
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
					if ( !basis_span_contains( oracle_basis, v ) )
					{
						print_header();
						std::cerr << "  actual basis vector missing from oracle span: " << format_word32_hex( v ) << "\n";
						ok = false;
					}
				}
			}

			if ( static_cast<std::uint64_t>( oracle_basis_count ) != oracle.rank )
			{
				print_header();
				std::cerr << "  oracle basis count mismatch count=" << oracle_basis_count << " rank=" << oracle.rank << "\n";
				ok = false;
			}
			if ( static_cast<std::uint64_t>( actual_basis_count ) != actual.rank )
			{
				print_header();
				std::cerr << "  actual basis count mismatch count=" << actual_basis_count << " rank=" << actual.rank << "\n";
				ok = false;
			}
			if ( ( oracle.rank & 1 ) != 0 || ( actual.rank & 1 ) != 0 )
			{
				print_header();
				std::cerr << "  odd polar rank encountered oracle=" << oracle.rank << " actual=" << actual.rank << "\n";
				ok = false;
			}

			return ok;
		}

		static bool run_injection_transition_self_tests( std::uint64_t seed, std::size_t extra_cases )
		{
			std::mt19937_64			   rng( seed ^ 0x9E3779B97F4A7C15ull );
			std::vector<std::uint32_t> cases { 0x00000000u, 0x00000001u, 0x00000003u, 0x00000005u, 0x0000000Fu, 0x00000080u, 0x00008000u, 0x00010001u, 0x00FF00FFu, 0xA5A5A5A5u, 0x80000000u, 0xFFFFFFFFu };
			cases.reserve( cases.size() + extra_cases );
			for ( std::size_t i = 0; i < extra_cases; ++i )
			{
				cases.push_back( static_cast<std::uint32_t>( rng() ) );
			}

			bool ok = true;
			for ( const std::uint32_t output_mask_u : cases )
			{
				const auto oracle_b = build_direct_injection_transition_oracle_branch_b( output_mask_u );
				const auto actual_b = compute_injection_transition_from_branch_b( output_mask_u );
				if ( !compare_injection_transition( "branch_b", output_mask_u, oracle_b, actual_b ) )
				{
					ok = false;
				}

				const auto oracle_a = build_direct_injection_transition_oracle_branch_a( output_mask_u );
				const auto actual_a = compute_injection_transition_from_branch_a( output_mask_u );
				if ( !compare_injection_transition( "branch_a", output_mask_u, oracle_a, actual_a ) )
				{
					ok = false;
				}
			}

			if ( ok )
			{
				std::cout << "[SelfTest][Linear][Injection] exact transition checks passed"
						  << " cases=" << cases.size() << " seed=0x" << std::hex << seed << std::dec << "\n";
			}
			return ok;
		}

		static bool run_linear_z_shell_vs_slr_tests( const std::vector<LinearTestCase>& cases )
		{
			bool ok = true;
			for ( const auto& tc : cases )
			{
				const auto& z_shell_ref = AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u( tc.output_mask_u, tc.weight_cap, SearchMode::Strict, true, 0 );
				const auto& slr_ref = AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u( tc.output_mask_u, tc.weight_cap, SearchMode::Strict, false, 0 );

				const std::vector<AddCandidate> z_shell( z_shell_ref.begin(), z_shell_ref.end() );
				const std::vector<AddCandidate> slr( slr_ref.begin(), slr_ref.end() );

				if ( !compare_candidate_sets( tc, slr, z_shell, "slr", "z-shell", "z-shell-vs-slr" ) )
				{
					ok = false;
				}
				if ( !check_non_decreasing_weight( tc, slr, "slr", "z-shell-vs-slr" ) )
				{
					ok = false;
				}
				if ( !check_non_decreasing_weight( tc, z_shell, "z-shell", "z-shell-vs-slr" ) )
				{
					ok = false;
				}
			}
			return ok;
		}

		static bool collect_weight_sliced_clat_streaming_candidates( const LinearTestCase& tc, std::vector<AddCandidate>& out )
		{
			out.clear();
			WeightSlicedClatStreamingCursor cursor {};
			reset_weight_sliced_clat_streaming_cursor( cursor, tc.output_mask_u, tc.weight_cap );

			AddCandidate candidate {};
			while ( next_weight_sliced_clat_streaming_candidate( cursor, candidate ) )
			{
				out.push_back( candidate );
			}
			return !cursor.stop_due_to_limits;
		}

		static bool run_linear_z_shell_streaming_vs_batch_tests( const std::vector<LinearTestCase>& cases )
		{
			bool ok = true;
			for ( const auto& tc : cases )
			{
				const auto&						z_shell_ref = AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u( tc.output_mask_u, tc.weight_cap, SearchMode::Strict, true, 0 );
				const std::vector<AddCandidate> z_shell_batch( z_shell_ref.begin(), z_shell_ref.end() );

				std::vector<AddCandidate> z_shell_streaming;
				if ( !collect_weight_sliced_clat_streaming_candidates( tc, z_shell_streaming ) )
				{
					std::cerr << "[SelfTest][Linear] z-shell-streaming collection aborted: ";
					print_linear_case( std::cerr, tc );
					std::cerr << "\n";
					ok = false;
					continue;
				}

				if ( !compare_candidate_sets( tc, z_shell_batch, z_shell_streaming, "z-shell-batch", "z-shell-streaming", "z-shell-streaming" ) )
				{
					ok = false;
				}
				if ( !check_non_decreasing_weight( tc, z_shell_streaming, "z-shell-streaming", "z-shell-streaming" ) )
				{
					ok = false;
				}
			}
			return ok;
		}

		static bool run_linear_oracle_tests( const std::vector<LinearTestCase>& cases )
		{
			bool ok = true;
			for ( const auto& tc : cases )
			{
				const std::vector<AddCandidate> oracle = generate_oracle_candidates_small_n( tc.output_mask_u, tc.word_bits );
				const auto&						z_shell_ref = AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u( tc.output_mask_u, tc.weight_cap, SearchMode::Strict, true, 0 );
				const std::vector<AddCandidate> z_shell_filtered = filter_candidates_by_word_bits( z_shell_ref, tc.word_bits );

				if ( !compare_candidate_sets( tc, oracle, z_shell_filtered, "oracle", "z-shell", "z-shell-oracle" ) )
				{
					ok = false;
				}
				if ( !check_non_decreasing_weight( tc, z_shell_filtered, "z-shell", "z-shell-oracle" ) )
				{
					ok = false;
				}
			}
			return ok;
		}

		static bool run_linear_varconst_q2_framework_sanity_n5()
		{
			using TwilightDream::auto_search_linear::FixedAlphaSubColumnStrictWitness;
			using TwilightDream::auto_search_linear::FixedAlphaSubColumnStrictWitnessStreamingCursor;
			using TwilightDream::auto_search_linear::FixedBetaSubColumnStrictWitness;
			using TwilightDream::auto_search_linear::FixedBetaSubColumnStrictWitnessStreamingCursor;
			using TwilightDream::auto_search_linear::LinearFixedAlphaOuterHotPathStageBinding;
			using TwilightDream::auto_search_linear::LinearFixedBetaHotPathSubconstMode;
			using TwilightDream::auto_search_linear::LinearFixedBetaOuterHotPathStageBinding;
			using TwilightDream::auto_search_linear::SubConstCandidate;
			using TwilightDream::auto_search_linear::compute_fixed_alpha_outer_hot_path_column_floor;
			using TwilightDream::auto_search_linear::compute_fixed_beta_outer_hot_path_column_floor;
			using TwilightDream::auto_search_linear::fixed_alpha_dual_fixed_pair_within_cap;
			using TwilightDream::auto_search_linear::make_fixed_alpha_sub_column_root_theorem_request;
			using TwilightDream::auto_search_linear::make_fixed_alpha_sub_column_strict_witness_request;
			using TwilightDream::auto_search_linear::make_fixed_beta_sub_column_root_theorem_request;
			using TwilightDream::auto_search_linear::make_fixed_beta_sub_column_strict_witness_request;
			using TwilightDream::auto_search_linear::materialize_fixed_alpha_sub_column_strict_witnesses_exact;
			using TwilightDream::auto_search_linear::materialize_fixed_beta_sub_column_strict_witnesses_exact;
			using TwilightDream::auto_search_linear::materialize_fixed_beta_subconst_hot_path_candidates;
			using TwilightDream::auto_search_linear::next_fixed_alpha_sub_column_strict_witness;
			using TwilightDream::auto_search_linear::next_fixed_beta_sub_column_strict_witness;
			using TwilightDream::auto_search_linear::reset_fixed_alpha_sub_column_strict_witness_streaming_cursor;
			using TwilightDream::auto_search_linear::reset_fixed_beta_sub_column_strict_witness_streaming_cursor;
			using TwilightDream::auto_search_linear::solve_fixed_alpha_sub_column_root_theorem_u32;
			using TwilightDream::auto_search_linear::solve_fixed_beta_sub_column_root_theorem_u32;
			using TwilightDream::auto_search_linear::try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32;
			using TwilightDream::auto_search_linear::try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32;

			constexpr int n = 5;
			constexpr std::uint32_t mask = ( 1u << n ) - 1u;
			constexpr SearchWeight weight_cap = n;

			for ( std::uint32_t constant = 0; constant <= mask; ++constant )
			{
				for ( std::uint32_t beta = 0; beta <= mask; ++beta )
				{
					const auto oracle =
						build_fixed_beta_varconst_sub_oracle_order(
							beta,
							constant,
							n,
							weight_cap );
					if ( oracle.empty() )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-beta oracle unexpectedly empty\n";
						return false;
					}

					const auto root =
						solve_fixed_beta_sub_column_root_theorem_u32(
							make_fixed_beta_sub_column_root_theorem_request(
								beta,
								constant,
								n ) );
					if ( ( root.optimal_input_mask_alpha & mask ) != oracle.front().mask ||
						 root.exact_abs_weight != oracle.front().abs_weight ||
						 root.ceil_linear_weight != oracle.front().ceil_weight )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-beta root theorem mismatch"
								  << " beta=" << format_word32_hex( beta )
								  << " constant=" << format_word32_hex( constant ) << "\n";
						return false;
					}

					if ( !try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
							 make_fixed_beta_sub_column_root_theorem_request(
								 beta,
								 constant,
								 n ),
							 oracle.front().ceil_weight )
							 .has_value() )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-beta cap acceptance mismatch\n";
						return false;
					}

					const auto materialized =
						materialize_fixed_beta_sub_column_strict_witnesses_exact(
							make_fixed_beta_sub_column_strict_witness_request(
								beta,
								constant,
								weight_cap,
								n ) );
					std::vector<ExactMaskOrderEntry> actual_materialized {};
					actual_materialized.reserve( materialized.size() );
					for ( const auto& witness : materialized )
					{
						const double corr =
							brute_force_corr_sub_const_n(
								witness.input_mask_alpha,
								constant,
								beta,
								n );
						actual_materialized.push_back( ExactMaskOrderEntry {
							witness.input_mask_alpha,
							exact_abs_weight_from_corr_small_n( corr, n ),
							witness.linear_weight } );
					}
					if ( !compare_exact_mask_order(
							 "fixed_beta_strict_materialized",
							 beta,
							 constant,
							 oracle,
							 actual_materialized ) )
						return false;

					FixedBetaSubColumnStrictWitnessStreamingCursor cursor {};
					reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
						cursor,
						make_fixed_beta_sub_column_strict_witness_request(
							beta,
							constant,
							weight_cap,
							n ) );
					std::vector<ExactMaskOrderEntry> actual_streaming {};
					FixedBetaSubColumnStrictWitness witness {};
					while ( next_fixed_beta_sub_column_strict_witness( cursor, witness ) )
					{
						const double corr =
							brute_force_corr_sub_const_n(
								witness.input_mask_alpha,
								constant,
								beta,
								n );
						actual_streaming.push_back( ExactMaskOrderEntry {
							witness.input_mask_alpha,
							exact_abs_weight_from_corr_small_n( corr, n ),
							witness.linear_weight } );
					}
					if ( !compare_exact_mask_order(
							 "fixed_beta_strict_streaming",
							 beta,
							 constant,
							 oracle,
							 actual_streaming ) )
						return false;

					SearchWeight stage_weight_cap = weight_cap;
					SearchWeight stage_weight_floor = INFINITE_WEIGHT;
					SubConstStreamingCursor stage_cursor {};
					std::vector<SubConstCandidate> materialized_candidates {};
					LinearFixedBetaOuterHotPathStageBinding beta_stage {
						beta,
						constant,
						stage_weight_cap,
						stage_weight_floor,
						stage_cursor,
						materialized_candidates };
					const auto floor =
						compute_fixed_beta_outer_hot_path_column_floor( beta_stage );
					if ( !floor.has_value() ||
						 floor->input_mask_alpha != oracle.front().mask ||
						 floor->linear_weight != oracle.front().ceil_weight ||
						 stage_weight_floor != oracle.front().ceil_weight )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-beta outer-hot-path floor mismatch\n";
						return false;
					}

					const auto collapsed =
						materialize_fixed_beta_subconst_hot_path_candidates(
							LinearFixedBetaHotPathSubconstMode::ColumnOptimalCollapsed,
							floor,
							beta,
							constant,
							weight_cap );
					if ( collapsed.size() != 1u ||
						 collapsed.front().input_mask_on_x != oracle.front().mask ||
						 collapsed.front().linear_weight != oracle.front().ceil_weight )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-beta collapsed candidate mismatch\n";
						return false;
					}
				}

				for ( std::uint32_t alpha = 0; alpha <= mask; ++alpha )
				{
					const auto oracle =
						build_fixed_alpha_varconst_sub_oracle_order(
							alpha,
							constant,
							n,
							weight_cap );
					if ( oracle.empty() )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-alpha oracle unexpectedly empty\n";
						return false;
					}

					const auto root =
						solve_fixed_alpha_sub_column_root_theorem_u32(
							make_fixed_alpha_sub_column_root_theorem_request(
								alpha,
								constant,
								n ) );
					if ( ( root.optimal_output_mask_beta & mask ) != oracle.front().mask ||
						 root.exact_abs_weight != oracle.front().abs_weight ||
						 root.ceil_linear_weight != oracle.front().ceil_weight )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-alpha root theorem mismatch"
								  << " alpha=" << format_word32_hex( alpha )
								  << " constant=" << format_word32_hex( constant ) << "\n";
						return false;
					}

					if ( !try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
							 make_fixed_alpha_sub_column_root_theorem_request(
								 alpha,
								 constant,
								 n ),
							 oracle.front().ceil_weight )
							 .has_value() )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-alpha cap acceptance mismatch\n";
						return false;
					}

					const auto materialized =
						materialize_fixed_alpha_sub_column_strict_witnesses_exact(
							make_fixed_alpha_sub_column_strict_witness_request(
								alpha,
								constant,
								weight_cap,
								n ) );
					std::vector<ExactMaskOrderEntry> actual_materialized {};
					actual_materialized.reserve( materialized.size() );
					for ( const auto& witness : materialized )
					{
						const double corr =
							brute_force_corr_sub_const_n(
								alpha,
								constant,
								witness.output_mask_beta,
								n );
						actual_materialized.push_back( ExactMaskOrderEntry {
							witness.output_mask_beta,
							exact_abs_weight_from_corr_small_n( corr, n ),
							witness.linear_weight } );
					}
					if ( !compare_exact_mask_order(
							 "fixed_alpha_strict_materialized",
							 alpha,
							 constant,
							 oracle,
							 actual_materialized ) )
						return false;

					FixedAlphaSubColumnStrictWitnessStreamingCursor cursor {};
					reset_fixed_alpha_sub_column_strict_witness_streaming_cursor(
						cursor,
						make_fixed_alpha_sub_column_strict_witness_request(
							alpha,
							constant,
							weight_cap,
							n ) );
					std::vector<ExactMaskOrderEntry> actual_streaming {};
					FixedAlphaSubColumnStrictWitness witness {};
					while ( next_fixed_alpha_sub_column_strict_witness( cursor, witness ) )
					{
						const double corr =
							brute_force_corr_sub_const_n(
								alpha,
								constant,
								witness.output_mask_beta,
								n );
						actual_streaming.push_back( ExactMaskOrderEntry {
							witness.output_mask_beta,
							exact_abs_weight_from_corr_small_n( corr, n ),
							witness.linear_weight } );
					}
					if ( !compare_exact_mask_order(
							 "fixed_alpha_strict_streaming",
							 alpha,
							 constant,
							 oracle,
							 actual_streaming ) )
						return false;

					SearchWeight stage_weight_cap = weight_cap;
					SearchWeight stage_weight_floor = INFINITE_WEIGHT;
					FixedAlphaSubColumnStrictWitnessStreamingCursor stage_cursor {};
					std::vector<FixedAlphaSubConstCandidate> materialized_candidates {};
					LinearFixedAlphaOuterHotPathStageBinding alpha_stage {
						alpha,
						oracle.front().mask,
						constant,
						&stage_weight_cap,
						&stage_weight_floor,
						&stage_cursor,
						&materialized_candidates };
					const auto floor =
						compute_fixed_alpha_outer_hot_path_column_floor( alpha_stage );
					if ( !floor.has_value() ||
						 floor->output_mask_beta != oracle.front().mask ||
						 floor->linear_weight != oracle.front().ceil_weight ||
						 stage_weight_floor != oracle.front().ceil_weight )
					{
						std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-alpha outer-hot-path floor mismatch\n";
						return false;
					}

					for ( std::uint32_t beta = 0; beta <= mask; ++beta )
					{
						const double corr =
							brute_force_corr_sub_const_n( alpha, constant, beta, n );
						const std::uint64_t abs_weight =
							exact_abs_weight_from_corr_small_n( corr, n );
						const bool expected_within_cap =
							abs_weight != 0 &&
							static_cast<SearchWeight>( n - floor_log2_uint64( abs_weight ) ) <= weight_cap;
						const bool actual_within_cap =
							fixed_alpha_dual_fixed_pair_within_cap(
								alpha,
								beta,
								constant,
								weight_cap,
								n );
						if ( expected_within_cap != actual_within_cap )
						{
							std::cerr << "[SelfTest][Linear][VarConstQ2] fixed-alpha dual-fixed mismatch"
									  << " alpha=" << format_word32_hex( alpha )
									  << " beta=" << format_word32_hex( beta )
									  << " constant=" << format_word32_hex( constant )
									  << " expected=" << expected_within_cap
									  << " actual=" << actual_within_cap << "\n";
							return false;
						}
					}
				}
			}

			std::cout << "[SelfTest][Linear][VarConstQ2] root/strict/hot-path checks passed (n=5 exhaustive)\n";
			return true;
		}

		static bool run_linear_search_self_test_impl( std::uint64_t seed, std::size_t extra_cases )
		{
			std::mt19937_64 rng( seed );

			std::vector<LinearTestCase> equivalence_cases = make_fixed_linear_equivalence_cases();
			const std::size_t			fixed_equivalence_count = equivalence_cases.size();
			for ( std::size_t i = 0; i < extra_cases; ++i )
			{
				LinearTestCase tc {};
				tc.output_mask_u = static_cast<std::uint32_t>( rng() );
				tc.weight_cap = 5;
				tc.word_bits = 32;
				equivalence_cases.push_back( tc );
			}

			std::vector<LinearTestCase> oracle_cases = make_fixed_linear_oracle_cases();
			const std::size_t			fixed_oracle_count = oracle_cases.size();
			for ( std::size_t i = 0; i < extra_cases; ++i )
			{
				LinearTestCase tc {};
				tc.output_mask_u = static_cast<std::uint32_t>( rng() ) & 0xFFu;
				tc.weight_cap = 8;
				tc.word_bits = 8;
				oracle_cases.push_back( tc );
			}

			std::cout << "[SelfTest][Linear] fixed_equivalence_cases=" << fixed_equivalence_count << " fixed_oracle_cases=" << fixed_oracle_count << " random_cases=" << extra_cases << " seed=0x" << std::hex << seed << std::dec << "\n";

			bool ok = true;
			if ( !run_linear_z_shell_vs_slr_tests( equivalence_cases ) )
			{
				ok = false;
			}
			if ( !run_linear_z_shell_streaming_vs_batch_tests( equivalence_cases ) )
			{
				ok = false;
			}
			if ( !run_linear_oracle_tests( oracle_cases ) )
			{
				ok = false;
			}
			if ( !run_linear_varconst_q2_framework_sanity_n5() )
			{
				ok = false;
			}
			if ( !run_injection_transition_self_tests( seed, extra_cases ) )
			{
				ok = false;
			}
			if ( ok )
			{
				std::cout << "[SelfTest][Linear] regression tests passed\n";
			}
			return ok;
		}

	}  // namespace linear_search_self_test

}  // namespace

int run_linear_search_self_test( std::uint64_t seed, std::size_t extra_cases )
{
	return linear_search_self_test::run_linear_search_self_test_impl( seed, extra_cases ) ? 0 : 1;
}
