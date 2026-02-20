#pragma once
#if !defined( TEST_NEOALZETTE_DIFFERENTIAL_BEST_SEARCH_HPP )
#define TEST_NEOALZETTE_DIFFERENTIAL_BEST_SEARCH_HPP

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <memory>
#include <memory_resource>
#include <new>
#include <limits>
#include <source_location>
#include <bit>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <ctime>

#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_injection_constexpr.hpp"
#include "common/runtime_component.hpp"
#include "auto_search_frame/search_checkpoint.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"

using TwilightDream::arx_operators::diff_subconst_bvweight;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::xdp_add_lm2001;
using TwilightDream::arx_operators::xdp_add_lm2001_n;

// Implemented in `test_arx_operator_self_test.cpp` (linked into this executable).
int run_arx_operator_self_test();

namespace TwilightDream::auto_search_differential
{
	using ::TwilightDream::NeoAlzetteCore;

	constexpr int INFINITE_WEIGHT = 1'000'000;

	// Injection transition caches can grow extremely large in batch mode (random inputs),
	// which can make thread-exit teardown very slow (looks like "hang" after 100% progress).
	// We bound the cache size per-thread; `0` disables caching.
	// Keep the default conservative to avoid large memory footprints in multi-thread batch runs.
	static std::size_t g_injection_cache_max_entries_per_thread = 65536;  // default: 2^16

	// ============================================================================
	// Shared runtime components (moved to `common/runtime_component.*`)
	// ============================================================================

	using TwilightDream::runtime_component::budgeted_reserve_target;
	using TwilightDream::runtime_component::compute_initial_cache_reserve_hint;
	using TwilightDream::runtime_component::compute_initial_memo_reserve_hint;
	using TwilightDream::runtime_component::compute_next_cache_reserve_hint;
	using TwilightDream::runtime_component::format_local_time_now;
	using TwilightDream::runtime_component::hex8;
	using TwilightDream::runtime_component::memory_governor_enable_for_run;
	using TwilightDream::runtime_component::memory_governor_disable_for_run;
	using TwilightDream::runtime_component::memory_governor_in_pressure;
	using TwilightDream::runtime_component::memory_governor_poll_if_needed;
	using TwilightDream::runtime_component::memory_governor_set_poll_fn;
	using TwilightDream::runtime_component::memory_governor_update_from_system_sample;
	using TwilightDream::runtime_component::pmr_bounded_resource;
	using TwilightDream::runtime_component::pmr_configure_for_run;
	using TwilightDream::runtime_component::pmr_report_oom_once;
	using TwilightDream::runtime_component::pmr_run_epoch;
	using TwilightDream::runtime_component::pmr_suggest_limit_bytes;
	using TwilightDream::runtime_component::print_word32_hex;
	using TwilightDream::runtime_component::round_up_power_of_two;

	static inline double weight_to_probability( int weight )
	{
		return ( weight >= INFINITE_WEIGHT ) ? 0.0 : std::pow( 2.0, -double( weight ) );
	}

	static inline std::uint64_t pack_two_word32_differences( std::uint32_t first_difference, std::uint32_t second_difference )
	{
		return ( std::uint64_t( first_difference ) << 32 ) | std::uint64_t( second_difference );
	}

	static inline int floor_log2_uint64( std::uint64_t value )
	{
		// C++20 portable floor(log2(value)), returns -1 for value==0.
		return value ? ( static_cast<int>( std::bit_width( value ) ) - 1 ) : -1;
	}

	// ============================================================================
	// Weight-Sliced Partial DDT (w-pDDT) cache
	// Exact weight-shells for XOR differential probability of modular addition
	// (LM2001 xdp_add model). Rebuildable accelerator only; never defines truth.
	// ============================================================================

	struct WeightShellKey
	{
		std::uint32_t alpha = 0;
		std::uint32_t beta = 0;
		std::uint8_t	weight = 0;
		std::uint8_t	word_bits = 32;

		friend bool operator==( const WeightShellKey& a, const WeightShellKey& b ) noexcept
		{
			return a.alpha == b.alpha && a.beta == b.beta && a.weight == b.weight && a.word_bits == b.word_bits;
		}
	};

	struct WeightShellKeyHash
	{
		std::size_t operator()( const WeightShellKey& k ) const noexcept
		{
			std::uint64_t h = ( std::uint64_t( k.alpha ) << 32 ) ^ std::uint64_t( k.beta );
			h ^= ( std::uint64_t( k.weight ) << 7 );
			h ^= ( std::uint64_t( k.word_bits ) << 17 );
			h ^= ( h >> 33 );
			h *= 0xff51afd7ed558ccdULL;
			h ^= ( h >> 33 );
			h *= 0xc4ceb9fe1a85ec53ULL;
			h ^= ( h >> 33 );
			return static_cast<std::size_t>( h );
		}
	};

	struct WeightShell
	{
		std::uint32_t* data = nullptr;
		std::uint32_t  size = 0;
	};

	class WeightShellCache
	{
	public:
		void configure( bool enable, int max_weight )
		{
			std::scoped_lock lk( mutex_ );
			enabled_ = enable;
			max_weight_ = std::clamp( max_weight, 0, 31 );
			if ( !enabled_ )
				map_.clear();
		}

		bool enabled() const
		{
			std::scoped_lock lk( mutex_ );
			return enabled_;
		}

		int max_weight() const
		{
			std::scoped_lock lk( mutex_ );
			return max_weight_;
		}

		bool try_get_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, std::vector<std::uint32_t>& out )
		{
			if ( weight < 0 )
				return false;
			const WeightShellKey key { alpha, beta, static_cast<std::uint8_t>( weight ), static_cast<std::uint8_t>( word_bits ) };
			std::scoped_lock	 lk( mutex_ );
			if ( !enabled_ )
				return false;
			auto it = map_.find( key );
			if ( it == map_.end() )
				return false;
			const WeightShell& shell = it->second;
			out.clear();
			out.reserve( shell.size );
			for ( std::uint32_t i = 0; i < shell.size; ++i )
				out.push_back( shell.data[ i ] );
			return true;
		}

		void maybe_put_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, const std::vector<std::uint32_t>& shell )
		{
			if ( weight < 0 )
				return;
			if ( runtime_component::memory_governor_in_pressure() )
				return;
			const WeightShellKey key { alpha, beta, static_cast<std::uint8_t>( weight ), static_cast<std::uint8_t>( word_bits ) };
			std::scoped_lock	 lk( mutex_ );
			if ( !enabled_ )
				return;
			if ( map_.find( key ) != map_.end() )
				return;

			if ( shell.empty() )
			{
				map_.emplace( key, WeightShell { nullptr, 0 } );
				return;
			}

			const std::uint64_t bytes = static_cast<std::uint64_t>( shell.size() ) * sizeof( std::uint32_t );
			void* p = runtime_component::alloc_rebuildable( bytes, false );
			if ( !p )
				return;
			std::uint32_t* dst = static_cast<std::uint32_t*>( p );
			for ( std::size_t i = 0; i < shell.size(); ++i )
				dst[ i ] = shell[ i ];
			map_.emplace( key, WeightShell { dst, static_cast<std::uint32_t>( shell.size() ) } );
		}

		void clear_and_disable( const char* /*reason*/ = nullptr )
		{
			std::scoped_lock lk( mutex_ );
			map_.clear();
			enabled_ = false;
		}

		void clear_keep_enabled( const char* /*reason*/ = nullptr )
		{
			std::scoped_lock lk( mutex_ );
			map_.clear();
		}

	private:
		mutable std::mutex																	 mutex_ {};
		bool																				 enabled_ = false;
		int																					 max_weight_ = 0;
		std::unordered_map<WeightShellKey, WeightShell, WeightShellKeyHash> map_ {};
	};

	inline WeightShellCache g_weight_shell_cache;

	// ============================================================================
	// Injection affine model (XOR with NOT-AND / NOT-OR): InjectionAffineTransition{ basis_vectors, offset, rank_weight }
	//
	// We must preserve the exact call structure of the cipher:
	//   - cd_injection_from_B(B, (RC[2]|RC[3]), RC[3]) then A ^= rotl(C,24)^rotl(D,16)^RC[4]
	//   - cd_injection_from_A(A, (RC[7]&RC[8]), RC[8]) then B ^= rotl(C,24)^rotl(D,16)^RC[9]
	//
	// In XOR-difference terms, the injected constants RC[4], RC[9] cancel out, but the non-linear
	// cd_injection_* still changes the difference. Here we build a rigorous (auditable) XOR-differential
	// transition model under the assumption that the base input x is uniform:
	//
	//   Let f be the injected XOR term (the value that is XORed into the other branch):
	//     f_B(B) = rotl24(C(B)) ⊕ rotl16(D(B))   where (C,D)=cd_injection_from_B(B,...)
	//     f_A(A) = rotl24(C(A)) ⊕ rotl16(D(A))   where (C,D)=cd_injection_from_A(A,...)
	//
	//   For an input XOR-difference Δ, define the derivative:
	//     D_Δ f(x) = f(x) ⊕ f(x ⊕ Δ)
	//
	//   For quadratic f (our case: AND/OR with linear masks), D_Δ f(x) is affine:
	//     D_Δ f(x) = M x ⊕ c
	//
	//   Therefore the reachable set of output differences is the affine subspace:
	//     { c ⊕ im(M) }
	//   and for uniform x every reachable output occurs with probability 2^{-rank(M)}.
	//
	// We represent this as:
	//   InjectionAffineTransition{ offset=c, basis_vectors=some basis of im(M), rank_weight=rank(M) }.
	// ============================================================================

	struct InjectionAffineTransition
	{
		std::uint32_t				  offset = 0;		 // c in D_Δ f(x) = Mx ⊕ c
		std::array<std::uint32_t, 32> basis_vectors {};	 // basis vectors spanning im(M), packed [0..rank_weight-1]
		int							  rank_weight = 0;	 // rank(M) = -log2(probability of any reachable output)
	};

	using ShardedInjectionCache32 = TwilightDream::runtime_component::ShardedSharedCache<std::uint32_t, InjectionAffineTransition>;

	// Optional shared (cross-thread) caches. These are configured at run start (single/batch).
	static ShardedInjectionCache32 g_shared_injection_cache_branch_a;
	static ShardedInjectionCache32 g_shared_injection_cache_branch_b;

	static constexpr std::uint32_t compute_injected_xor_term_from_branch_b( std::uint32_t branch_b_value ) noexcept
	{
		return TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( branch_b_value );
	}

	static constexpr std::uint32_t compute_injected_xor_term_from_branch_a( std::uint32_t branch_a_value ) noexcept
	{
		return TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( branch_a_value );
	}

	// Exact speed-up:
	// In the transition builder we need D_Δ f(0) and D_Δ f(e_i) for i=0..31, where
	//   D_Δ f(x) = f(x) ⊕ f(x ⊕ Δ).
	// Precomputing f(0) and f(e_i) avoids half the f() evaluations per Δ without changing results.
	// Use neoalzette_constexpr directly in consteval (same as linear search) so compile-time path has no extra indirection.
	static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_b()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ static_cast<std::size_t>( i ) ] = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 1u << i );
		return out;
	}
	static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_a()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ static_cast<std::size_t>( i ) ] = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 1u << i );
		return out;
	}

	static constexpr std::uint32_t				   g_injected_xor_term_f0_branch_b = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 0u );
	static constexpr std::uint32_t				   g_injected_xor_term_f0_branch_a = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 0u );
	static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_b = make_injected_xor_term_basis_branch_b();
	static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_a = make_injected_xor_term_basis_branch_a();

	// Formula sanity: f(0) and f(e_i) must match wrapper so D_Δ f(0)=f(0)⊕f(Δ) and column_i = D_Δ f(e_i)⊕D_Δ f(0) are unchanged.
	static_assert( g_injected_xor_term_f0_branch_b == compute_injected_xor_term_from_branch_b( 0u ), "f(0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f0_branch_a == compute_injected_xor_term_from_branch_a( 0u ), "f(0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 0 ] == compute_injected_xor_term_from_branch_b( 1u ), "f(e_0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 0 ] == compute_injected_xor_term_from_branch_a( 1u ), "f(e_0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 31 ] == compute_injected_xor_term_from_branch_b( 1u << 31 ), "f(e_31) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 31 ] == compute_injected_xor_term_from_branch_a( 1u << 31 ), "f(e_31) branch_a: direct vs wrapper" );

	static int xor_basis_add( std::array<std::uint32_t, 32>& basis, std::uint32_t v )
	{
		// classic GF(2) linear basis insertion; returns 1 if v increased rank, 0 otherwise
		while ( v != 0u )
		{
			const unsigned		bit = 31u - std::countl_zero( v );
			const std::uint32_t b = basis[ bit ];
			if ( b != 0u )
			{
				v ^= b;
			}
			else
			{
				basis[ bit ] = v;
				return 1;
			}
		}
		return 0;
	}

	static InjectionAffineTransition compute_injection_transition_from_branch_b( std::uint32_t branch_b_input_difference )
	{
		// Thread-safe for batch search: each thread gets its own cache to avoid data races.
		static thread_local bool															  tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource							  tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t													  tls_epoch = 0;
		static thread_local std::size_t														  cache_reserved_hint = 0;
		static thread_local std::size_t														  last_configured_cache_cap = std::size_t( -1 );

		// Reset thread-local state on each new "run" (so a prior OOM doesn't permanently disable caching).
		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				tls_pool.release();
				cache_reserved_hint = 0;
				last_configured_cache_cap = std::size_t( -1 );
			}
		}

		// If caching is disabled, bypass the thread-local map entirely (and avoid reusing stale entries).
		const std::size_t cap = g_injection_cache_max_entries_per_thread;
		if ( cap == 0 || tls_cache_disabled || memory_governor_in_pressure() )
		{
			if ( last_configured_cache_cap != 0 )
			{
				cache.clear();
				cache.rehash( 0 );
				tls_pool.release();
				cache_reserved_hint = 0;
				last_configured_cache_cap = 0;
			}
			// Optional shared cache is still valid even when per-thread caching is disabled.
			if ( g_shared_injection_cache_branch_b.enabled() )
			{
				InjectionAffineTransition cached {};
				if ( g_shared_injection_cache_branch_b.try_get( branch_b_input_difference, cached ) )
					return cached;
			}
			// Fall through to compute without caching.
		}
		else
		{
			// (Re)configure reserve if the cache cap changed between stages (e.g., auto breadth -> deep).
			if ( last_configured_cache_cap != cap )
			{
				// Root fix: do NOT reserve "cap" upfront. Reserve a computed initial amount and grow on demand.
				const std::size_t hint = budgeted_reserve_target( cache.size(), compute_initial_cache_reserve_hint( cap ), 256ull );
				cache.max_load_factor( 0.7f );
				if ( cache_reserved_hint < hint )
				{
					try
					{
						cache.reserve( hint );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_b)" );
						cache.clear();
						cache.rehash( 0 );
						tls_pool.release();
						cache_reserved_hint = 0;
						last_configured_cache_cap = 0;
					}
				}
				cache_reserved_hint = hint;
				last_configured_cache_cap = cap;
			}
		}

		if ( cap != 0 && !tls_cache_disabled )
		{
			auto cache_iterator = cache.find( branch_b_input_difference );
			if ( cache_iterator != cache.end() )
				return cache_iterator->second;
		}

		// Optional shared cache (cross-thread). If hit, optionally populate thread-local (lock-free fast path).
		if ( g_shared_injection_cache_branch_b.enabled() )
		{
			InjectionAffineTransition cached {};
			if ( g_shared_injection_cache_branch_b.try_get( branch_b_input_difference, cached ) )
			{
				if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
				{
					// Grow TLS cache gradually (avoid huge upfront bucket allocation).
					if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
					{
						const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
						if ( next_hint > cache_reserved_hint )
						{
							try
							{
								cache.reserve( next_hint );
								cache_reserved_hint = next_hint;
							}
							catch ( const std::bad_alloc& )
							{
								tls_cache_disabled = true;
								pmr_report_oom_once( "tls_cache.reserve(branch_b)(grow)" );
							}
						}
					}
					try
					{
						cache.emplace( branch_b_input_difference, cached );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.emplace(branch_b)(shared_hit)" );
					}
				}
				return cached;
			}
		}

		InjectionAffineTransition transition {};
		const std::uint32_t		  f_delta = compute_injected_xor_term_from_branch_b( branch_b_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_b ^ f_delta;	// D_Δ f(0)

		// Build column space of M by evaluating D_Δ f(e_i) ⊕ D_Δ f(0) = column_i(M)
		int							  rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_b[ size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_b( basis_input_vector ^ branch_b_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;  // D_Δ f(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		// pack basis vectors deterministically (high-bit first)
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ size_t( packed_index++ ) ] = vector_value;
		}

		if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
		{
			if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
			{
				const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
				if ( next_hint > cache_reserved_hint )
				{
					try
					{
						cache.reserve( next_hint );
						cache_reserved_hint = next_hint;
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_b)(grow)" );
					}
				}
			}
			try
			{
				cache.emplace( branch_b_input_difference, transition );
			}
			catch ( const std::bad_alloc& )
			{
				tls_cache_disabled = true;
				pmr_report_oom_once( "tls_cache.emplace(branch_b)" );
			}
		}
		if ( g_shared_injection_cache_branch_b.enabled() )
		{
			g_shared_injection_cache_branch_b.try_put( branch_b_input_difference, transition );
		}
		return transition;
	}

	static InjectionAffineTransition compute_injection_transition_from_branch_a( std::uint32_t branch_a_input_difference )
	{
		static thread_local bool															  tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource							  tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t													  tls_epoch = 0;
		static thread_local std::size_t														  cache_reserved_hint = 0;
		static thread_local std::size_t														  last_configured_cache_cap = std::size_t( -1 );

		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				tls_pool.release();
				cache_reserved_hint = 0;
				last_configured_cache_cap = std::size_t( -1 );
			}
		}

		const std::size_t cap = g_injection_cache_max_entries_per_thread;
		if ( cap == 0 || tls_cache_disabled || memory_governor_in_pressure() )
		{
			if ( last_configured_cache_cap != 0 )
			{
				cache.clear();
				cache.rehash( 0 );
				tls_pool.release();
				cache_reserved_hint = 0;
				last_configured_cache_cap = 0;
			}
			if ( g_shared_injection_cache_branch_a.enabled() )
			{
				InjectionAffineTransition cached {};
				if ( g_shared_injection_cache_branch_a.try_get( branch_a_input_difference, cached ) )
					return cached;
			}
		}
		else
		{
			if ( last_configured_cache_cap != cap )
			{
				// Root fix: do NOT reserve "cap" upfront. Reserve a computed initial amount and grow on demand.
				const std::size_t hint = budgeted_reserve_target( cache.size(), compute_initial_cache_reserve_hint( cap ), 256ull );
				cache.max_load_factor( 0.7f );
				if ( cache_reserved_hint < hint )
				{
					try
					{
						cache.reserve( hint );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_a)" );
						cache.clear();
						cache.rehash( 0 );
						tls_pool.release();
						cache_reserved_hint = 0;
						last_configured_cache_cap = 0;
					}
				}
				cache_reserved_hint = hint;
				last_configured_cache_cap = cap;
			}
		}

		if ( cap != 0 && !tls_cache_disabled )
		{
			auto it = cache.find( branch_a_input_difference );
			if ( it != cache.end() )
				return it->second;
		}

		if ( g_shared_injection_cache_branch_a.enabled() )
		{
			InjectionAffineTransition cached {};
			if ( g_shared_injection_cache_branch_a.try_get( branch_a_input_difference, cached ) )
			{
				if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
				{
					if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
					{
						const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
						if ( next_hint > cache_reserved_hint )
						{
							try
							{
								cache.reserve( next_hint );
								cache_reserved_hint = next_hint;
							}
							catch ( const std::bad_alloc& )
							{
								tls_cache_disabled = true;
								pmr_report_oom_once( "tls_cache.reserve(branch_a)(grow)" );
							}
						}
					}
					try
					{
						cache.emplace( branch_a_input_difference, cached );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.emplace(branch_a)(shared_hit)" );
					}
				}
				return cached;
			}
		}

		InjectionAffineTransition transition {};
		const std::uint32_t		  f_delta = compute_injected_xor_term_from_branch_a( branch_a_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_a ^ f_delta;	// D_Δ f(0)

		int							  rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_a[ size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_a( basis_input_vector ^ branch_a_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;  // D_Δ f(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ size_t( packed_index++ ) ] = vector_value;
		}

		if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
		{
			if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
			{
				const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
				if ( next_hint > cache_reserved_hint )
				{
					try
					{
						cache.reserve( next_hint );
						cache_reserved_hint = next_hint;
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_a)(grow)" );
					}
				}
			}
			try
			{
				cache.emplace( branch_a_input_difference, transition );
			}
			catch ( const std::bad_alloc& )
			{
				tls_cache_disabled = true;
				pmr_report_oom_once( "tls_cache.emplace(branch_a)" );
			}
		}
		if ( g_shared_injection_cache_branch_a.enabled() )
		{
			g_shared_injection_cache_branch_a.try_put( branch_a_input_difference, transition );
		}
		return transition;
	}

	// ============================================================================
	// Matsui / best-first (round-level BnB + bit-level recursion)
	// ----------------------------------------------------------------------------
	// Notation (32-bit words):
	//   - ⊞  : addition modulo 2^32        (x ⊞ y) = (x + y) mod 2^32
	//   - ⊟  : subtraction modulo 2^32     (x ⊟ c) = (x - c) mod 2^32
	//   - ⊕  : bitwise XOR
	//   - ROTL_r(x), ROTR_r(x): rotations
	//   - L1^{-1}, L2^{-1}: backward linear layers (real code: l1_backward / l2_backward)
	//
	// Differential model conventions:
	//   - XOR-difference: Δx = x ⊕ x'
	//   - For modular add/sub, we DO NOT force identity transitions.
	//     We enumerate feasible output differences and score with BV-weight operators:
	//       - xdp_add_lm2001_n : (Δx, Δy) -> Δ(x ⊞ y)
	//       - diff_subconst_bvweight(): Δx -> Δ(x ⊟ c)
	//   - Cross-branch injection cd_injection_from_* is kept exactly (structure preserved).
	//     We propagate differences through its internal ops, and additionally attach an
	//     InjectionAffineTransition-based probability surrogate (rank_weight).
	//
	// IMPORTANT (must match the real cipher structure):
	//   - We DO NOT remove cd_injection_from_* from the cipher implementation.
	//   - We DO propagate differences through cd_injection_from_* (structure preserved),
	//     and we also include InjectionAffineTransition-based rank_weight.
	//
	// One forward round (as implemented):
	//   Input: (A0, B0)
	//   (1)  B1 = B0 ⊞ (ROTL_31(A0) ⊕ ROTL_17(A0) ⊕ RC[0])
	//        A1 = A0 - RC1
	//        A2 = A1 ⊕ ROTL_{R0}(B1)
	//        B2 = B1 ⊕ ROTL_{R1}(A2)
	//        (C0, D0) = cd_injection_from_B(B2; rc0, rc1)
	//        A3 = A2 ⊕ (ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ RC4)
	//
	//   (2)  A4 = A3 ⊞ (ROTL_31(B2) ⊕ ROTL_17(B2) ⊕ RC5)
	//        B3 = B2 - RC6
	//        B4 = B3 ⊕ ROTL_{R0}(A4)
	//        A5 = A4 ⊕ ROTL_{R1}(B4)
	//        (C1, D1) = cd_injection_from_A(A5; rc0, rc1)
	//        B5 = B4 ⊕ (ROTL_24(C1) ⊕ ROTL_16(D1) ⊕ RC9)
	//
	//   (3)  A_out = A5 ⊕ RC10
	//        B_out = B5 ⊕ RC11
	//
	// Record fields below store Δ-values at each boundary and the per-step weight.
	// ============================================================================

	struct DifferentialTrailStepRecord
	{
		int round_index = 0;

		// Round input XOR-differences:
		//   ΔA0 = A0 ⊕ A0' ,  ΔB0 = B0 ⊕ B0'
		std::uint32_t input_branch_a_difference = 0;  // ΔA0
		std::uint32_t input_branch_b_difference = 0;  // ΔB0

		// ------------------------------------------------------------------------
		// (1) B-addition:  B1 = B0 ⊞ T0
		//     where T0 = ROTL_31(A0) ⊕ ROTL_17(A0) ⊕ RC0
		//
		//   β0 = ΔT0,  γ0 = ΔB1,  w_add0 = -log2 Pr[Δ(B0 ⊞ T0)=γ0 | ΔB0, β0]
		std::uint32_t first_addition_term_difference = 0;					// β0 = ΔT0
		std::uint32_t output_branch_b_difference_after_first_addition = 0;	// γ0 = ΔB1
		int			  weight_first_addition = 0;							// w_add0

		// (1) A-const subtraction:  A1 = A0 ⊟ RC1
		//   γA0 = ΔA1,  w_subA = diff_subconst_bvweight(ΔA0 -> γA0; RC1)
		std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;	// ΔA1
		int			  weight_first_constant_subtraction = 0;							// w_subA

		// (1) XOR/ROT mixing:
		//   A2 = A1 ⊕ ROTL_{R0}(B1)
		//   B2 = B1 ⊕ ROTL_{R1}(A2)
		// XOR-diff propagation is deterministic:
		//   ΔA2 = ΔA1 ⊕ ROTL_{R0}(ΔB1)
		//   ΔB2 = ΔB1 ⊕ ROTL_{R1}(ΔA2)
		std::uint32_t branch_a_difference_after_first_xor_with_rotated_branch_b = 0;  // ΔA2
		std::uint32_t branch_b_difference_after_first_xor_with_rotated_branch_a = 0;  // ΔB2

		// (1) Injection B -> A (structure-preserved):
		//   (C0, D0) = cd_injection_from_B(B2; rc0, rc1)
		//   A3 = A2 ⊕ (ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ RC4)
		//
		// We store:
		//   ΔI_B = Δ(ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ RC4)   (the injected XOR mask difference)
		// and attach a surrogate weight via InjectionAffineTransition (rank_weight).
		std::uint32_t injection_from_branch_b_xor_difference = 0;			  // ΔI_B
		int			  weight_injection_from_branch_b = 0;					  // w_injB (rank_weight)
		std::uint32_t branch_a_difference_after_injection_from_branch_b = 0;  // ΔA3 = ΔA2 ⊕ ΔI_B

		// (1) Linear layer on B:
		//   B3 = L1^{-1}(B2)  =>  ΔB3 = L1^{-1}(ΔB2)
		std::uint32_t branch_b_difference_after_linear_layer_one_backward = 0;	// ΔB3

		// ------------------------------------------------------------------------
		// (2) A-addition:  A4 = A3 ⊞ T1
		//     where T1 = ROTL_31(B3) ⊕ ROTL_17(B3) ⊕ RC5
		//
		//   β1 = ΔT1,  γ1 = ΔA4,  w_add1 computed by add BV-weight operator
		std::uint32_t second_addition_term_difference = 0;					 // β1 = ΔT1
		std::uint32_t output_branch_a_difference_after_second_addition = 0;	 // γ1 = ΔA4
		int			  weight_second_addition = 0;							 // w_add1

		// (2) B-const subtraction:  B4 = B3 ⊟ RC6
		//   γB1 = ΔB4, w_subB = diff_subconst_bvweight(ΔB3 -> γB1; RC6)
		std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;	 // ΔB4
		int			  weight_second_constant_subtraction = 0;							 // w_subB

		// (2) XOR/ROT mixing:
		//   B5 = B4 ⊕ ROTL_{R0}(A4)
		//   A5 = A4 ⊕ ROTL_{R1}(B5)
		std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;  // ΔB5
		std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;  // ΔA5

		// (2) Injection A -> B (structure-preserved):
		//   (C1, D1) = cd_injection_from_A(A5; rc0, rc1)
		//   B6 = B5 ⊕ (ROTL_24(C1) ⊕ ROTL_16(D1) ⊕ RC9)
		// Store injected mask difference ΔI_A and rank-weight surrogate.
		std::uint32_t injection_from_branch_a_xor_difference = 0;  // ΔI_A
		int			  weight_injection_from_branch_a = 0;		   // w_injA (rank_weight)

		// End-of-round boundary (after A6 = L2^{-1}(A5)):
		//   ΔA6 = L2^{-1}(ΔA5),  ΔB6 = ΔB6 (post injection)
		std::uint32_t output_branch_a_difference = 0;  // ΔA_out (round boundary)
		std::uint32_t output_branch_b_difference = 0;  // ΔB_out (round boundary)

		// Round aggregate weight:
		//   w_round = w_add0 + w_subA + w_injB + w_add1 + w_subB + w_injA
		int round_weight = 0;
	};

	struct DifferentialBestSearchConfiguration
	{
		int			round_count = 2;
		int			addition_weight_cap = 31;						// extra cap (0..31). default=31 (no extra cap beyond the B&B bound)
		int			constant_subtraction_weight_cap = 32;			// cap for subconst weight (0..32). 32 means no extra cap.
		// Performance knob: branch limiter for enumerating injected transition output differences (0=exact/all).
		// NOTE: This does not change the underlying transition model; it only limits branching during search.
		std::size_t	  maximum_transition_output_differences = 0;
		// safety limit for DFS nodes (round + bit recursion). 0 = unlimited (prefer time limit).
		std::size_t	  maximum_search_nodes = 0;
		int			  target_best_weight = -1;				// if >=0: stop early once best_total_weight <= target_best_weight
		std::uint64_t maximum_search_seconds = 0;			// 0=unlimited; stop early once elapsed >= maximum_search_seconds
		bool		  enable_state_memoization = true;		// prune revisits: (depth, difference_a, difference_b) -> best weight so far
		// Weight-Sliced Partial DDT (w-pDDT) cache: exact weight shells for modular addition.
		bool		  enable_wpddt_cache = true;			// accelerator only; miss => exact fallback
		int			  wpddt_max_cached_weight = -1;			// -1 = auto from budget; otherwise 0..31
		bool		  enable_verbose_output = false;

		// Matsui-style remaining-round lower bound (weight domain):
		// Paper notation uses probabilities B_{n-r} for the remaining (n-r) rounds. In weight form:
		//   W_k = -log2(B_k)
		// and pruning is:
		//   accumulated_weight_so_far + W_{rounds_left} >= best_total_weight  => prune.
		//
		// IMPORTANT correctness rule:
		// `remaining_round_min_weight[k]` must be a LOWER bound on the minimal possible weight of ANY k-round continuation
		// (i.e., it must never overestimate). If unsure, leave this disabled or provide zeros.
		bool			  enable_remaining_round_lower_bound = false;
		std::vector<int>  remaining_round_min_weight {};	// index: rounds_left (0..round_count). Must satisfy remaining_round_min_weight[0]==0.
	};

	struct MatsuiSearchRunDifferentialResult
	{
		bool									 found = false;
		int										 best_weight = INFINITE_WEIGHT;
		std::uint64_t							 nodes_visited = 0;
		bool									 hit_maximum_search_nodes = false;
		bool									 hit_time_limit = false;
		std::vector<DifferentialTrailStepRecord> best_trail;
	};

	struct BestWeightHistory;	// forward
	struct BinaryCheckpointManager;		// forward

	struct DifferentialBestSearchContext
	{
		DifferentialBestSearchConfiguration configuration;
		std::uint32_t			start_difference_a = 0;
		std::uint32_t			start_difference_b = 0;

		std::uint64_t							 visited_node_count = 0;
		int										 best_total_weight = INFINITE_WEIGHT;
		std::vector<DifferentialTrailStepRecord> best_differential_trail;
		std::vector<DifferentialTrailStepRecord> current_differential_trail;

		// Best-effort memoization: prune revisits at round boundaries.
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int> memoization;

		// Run start time (used for progress/checkpoints even when maximum_search_seconds==0).
		std::chrono::steady_clock::time_point run_start_time {};

		// Single-run progress reporting (optional; batch has its own progress monitor).
		// Printed only when enabled by CLI: --progress-every-seconds S (S>0).
		std::uint64_t						  progress_every_seconds = 0;				// 0 = disable
		std::uint64_t						  progress_node_mask = ( 1ull << 18 ) - 1;	// check clock every ~262k nodes to reduce overhead
		std::chrono::steady_clock::time_point progress_start_time {};
		std::chrono::steady_clock::time_point progress_last_print_time {};
		std::uint64_t						  progress_last_print_nodes = 0;
		bool								  progress_print_differences = false;  // if enabled, print (dA,dB) snapshots on progress lines (useful for long/unbounded runs)

		// Time limit (single-run). Implemented as a global stop flag to avoid deep unwinding costs.
		std::chrono::steady_clock::time_point time_limit_start_time {};
		bool								  stop_due_to_time_limit = false;

		// Optional: checkpoint writer (e.g., auto mode) to persist best-weight changes.
		BestWeightHistory* checkpoint = nullptr;

		// Optional: binary checkpoint manager for resumable DFS.
		BinaryCheckpointManager* binary_checkpoint = nullptr;
	};

	struct BestWeightHistory
	{
		std::ofstream out {};
		int			  last_written_weight = INFINITE_WEIGHT;

		static std::string default_path( int round_count, std::uint32_t da, std::uint32_t db )
		{
			std::ostringstream oss;
			oss << "auto_checkpoint_R" << round_count << "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << da << "_DiffB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << db << std::dec << ".log";
			return oss.str();
		}

		bool open_append( const std::string& path )
		{
			out.open( path, std::ios::out | std::ios::app );
			return bool( out );
		}

		void maybe_write( const DifferentialBestSearchContext& context, const char* reason )
		{
			if ( !out )
				return;
			if ( context.best_total_weight >= INFINITE_WEIGHT )
				return;
			if ( context.best_total_weight == last_written_weight )
				return;

			const auto	 now = std::chrono::steady_clock::now();
			const double elapsed_seconds = ( context.run_start_time.time_since_epoch().count() == 0 ) ? 0.0 : std::chrono::duration<double>( now - context.run_start_time ).count();
			const auto&	 config = context.configuration;

			out << "=== checkpoint ===\n";
			out << "timestamp_local=" << format_local_time_now() << "\n";
			out << "checkpoint_reason=" << ( reason ? reason : "best_weight_changed" ) << "\n";
			out << "round_count=" << config.round_count << "\n";
			out << "start_difference_branch_a=" << hex8( context.start_difference_a ) << "\n";
			out << "start_difference_branch_b=" << hex8( context.start_difference_b ) << "\n";
			out << "best_total_weight=" << context.best_total_weight << "\n";
			out << "visited_node_count=" << context.visited_node_count << "\n";
			out << "elapsed_seconds=" << std::fixed << std::setprecision( 3 ) << elapsed_seconds << "\n";
			out << "target_best_weight=" << config.target_best_weight << "\n";
			out << "addition_weight_cap=" << config.addition_weight_cap << "\n";
			out << "constant_subtraction_weight_cap=" << config.constant_subtraction_weight_cap << "\n";
			out << "maximum_transition_output_differences=" << config.maximum_transition_output_differences << "\n";
			out << "maximum_search_nodes=" << config.maximum_search_nodes << "\n";
			out << "maximum_search_seconds=" << config.maximum_search_seconds << "\n";
			out << "enable_state_memoization=" << ( config.enable_state_memoization ? 1 : 0 ) << "\n";
			out << "enable_remaining_round_lower_bound=" << ( config.enable_remaining_round_lower_bound ? 1 : 0 ) << "\n";
			out << "remaining_round_min_weight_count=" << config.remaining_round_min_weight.size() << "\n";
			if ( !config.remaining_round_min_weight.empty() )
			{
				out << "remaining_round_min_weight_values=";
				for ( std::size_t i = 0; i < config.remaining_round_min_weight.size(); ++i )
				{
					if ( i != 0 )
						out << ",";
					out << config.remaining_round_min_weight[ i ];
				}
				out << "\n";
			}
			out << "enable_wpddt_cache=" << ( config.enable_wpddt_cache ? 1 : 0 ) << "\n";
			out << "wpddt_max_cached_weight=" << config.wpddt_max_cached_weight << "\n";
			out << "trail_step_count=" << context.best_differential_trail.size() << "\n";
			for ( const auto& s : context.best_differential_trail )
			{
				out << "round_index=" << s.round_index
					<< " round_weight=" << s.round_weight
					<< " input_difference_branch_a=" << hex8( s.input_branch_a_difference )
					<< " input_difference_branch_b=" << hex8( s.input_branch_b_difference )
					<< " output_difference_branch_a=" << hex8( s.output_branch_a_difference )
					<< " output_difference_branch_b=" << hex8( s.output_branch_b_difference ) << "\n";
			}
			out << "\n";
			out.flush();
			last_written_weight = context.best_total_weight;
		}
	};

	static inline void maybe_print_single_run_progress( DifferentialBestSearchContext& context, int round_boundary_depth_hint )
	{
		if ( context.progress_every_seconds == 0 )
			return;
		if ( ( context.visited_node_count & context.progress_node_mask ) != 0 )
			return;

		const auto now = std::chrono::steady_clock::now();
		memory_governor_poll_if_needed( now );
		if ( context.progress_last_print_time.time_since_epoch().count() != 0 )
		{
			const double since_last = std::chrono::duration<double>( now - context.progress_last_print_time ).count();
			if ( since_last < double( context.progress_every_seconds ) )
				return;
		}

		const double		elapsed = std::chrono::duration<double>( now - context.progress_start_time ).count();
		const double		window = ( context.progress_last_print_time.time_since_epoch().count() == 0 ) ? elapsed : std::chrono::duration<double>( now - context.progress_last_print_time ).count();
		const std::uint64_t delta_nodes = ( context.visited_node_count >= context.progress_last_print_nodes ) ? ( context.visited_node_count - context.progress_last_print_nodes ) : 0;
		const double		rate = ( window > 1e-9 ) ? ( double( delta_nodes ) / window ) : 0.0;

		std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );

		// Save/restore formatting (avoid perturbing later prints).
		const auto old_flags = std::cout.flags();
		const auto old_prec = std::cout.precision();
		const auto old_fill = std::cout.fill();

		TwilightDream::runtime_component::print_progress_prefix( std::cout );
		std::cout << "[Progress] visited_node_count=" << context.visited_node_count
				  << "  visited_nodes_per_second=" << std::fixed << std::setprecision( 2 ) << rate
				  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed
				  << "  best_total_weight=" << ( ( context.best_total_weight >= INFINITE_WEIGHT ) ? -1 : context.best_total_weight );
		if ( round_boundary_depth_hint >= 0 )
		{
			std::cout << "  current_depth=" << round_boundary_depth_hint << "  total_rounds=" << context.configuration.round_count;
		}
		if ( context.progress_print_differences )
		{
			std::cout << "  ";
			print_word32_hex( "start_difference_branch_a=", context.start_difference_a );
			std::cout << " ";
			print_word32_hex( "start_difference_branch_b=", context.start_difference_b );
			if ( !context.best_differential_trail.empty() )
			{
				const auto& r1 = context.best_differential_trail.front();
				const auto& rn = context.best_differential_trail.back();
				std::cout << " ";
				print_word32_hex( "best_trail_round1_output_difference_branch_a=", r1.output_branch_a_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_round1_output_difference_branch_b=", r1.output_branch_b_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_output_difference_branch_a=", rn.output_branch_a_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_output_difference_branch_b=", rn.output_branch_b_difference );
			}
		}
		std::cout << "\n";

		std::cout.flags( old_flags );
		std::cout.precision( old_prec );
		std::cout.fill( old_fill );

		context.progress_last_print_time = now;
		context.progress_last_print_nodes = context.visited_node_count;
	}

	// Per-round shared state for nested enumeration (moved out for checkpointing).
	struct DifferentialRoundSearchState
	{
		int round_boundary_depth = 0;
		int accumulated_weight_so_far = 0;
		int remaining_round_weight_lower_bound_after_this_round = 0;

		std::uint32_t branch_a_input_difference = 0;
		std::uint32_t branch_b_input_difference = 0;

		DifferentialTrailStepRecord base_step {};

		std::uint32_t first_addition_term_difference = 0;
		std::uint32_t optimal_output_branch_b_difference_after_first_addition = 0;
		int			  optimal_weight_first_addition = 0;
		int			  weight_cap_first_addition = 0;

		std::uint32_t output_branch_b_difference_after_first_addition = 0;
		int			  weight_first_addition = 0;
		int			  accumulated_weight_after_first_addition = 0;

		std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
		int			  weight_first_constant_subtraction = 0;
		int			  accumulated_weight_after_first_constant_subtraction = 0;

		std::uint32_t branch_a_difference_after_first_xor_with_rotated_branch_b = 0;
		std::uint32_t branch_b_difference_after_first_xor_with_rotated_branch_a = 0;
		std::uint32_t branch_b_difference_after_linear_layer_one_backward = 0;

		int			  weight_injection_from_branch_b = 0;
		int			  accumulated_weight_before_second_addition = 0;
		std::uint32_t injection_from_branch_b_xor_difference = 0;
		std::uint32_t branch_a_difference_after_injection_from_branch_b = 0;

		std::uint32_t second_addition_term_difference = 0;
		std::uint32_t optimal_output_branch_a_difference_after_second_addition = 0;
		int			  optimal_weight_second_addition = 0;
		int			  weight_cap_second_addition = 0;

		std::uint32_t output_branch_a_difference_after_second_addition = 0;
		int			  weight_second_addition = 0;
		int			  accumulated_weight_after_second_addition = 0;

		std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
		int			  weight_second_constant_subtraction = 0;
		int			  accumulated_weight_after_second_constant_subtraction = 0;

		std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;
		std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;

		int			  weight_injection_from_branch_a = 0;
		int			  accumulated_weight_at_round_end = 0;
		std::uint32_t injection_from_branch_a_xor_difference = 0;

		std::uint32_t output_branch_a_difference = 0;
		std::uint32_t output_branch_b_difference = 0;
	};

	// ============================================================================
	// Resumable enumerators (preserve traversal order)
	// ============================================================================

	static inline bool enumerate_add_shell_exact_collect(
		DifferentialBestSearchContext& context,
		std::uint32_t alpha,
		std::uint32_t beta,
		std::uint32_t output_hint,
		int target_weight,
		int word_bits,
		std::vector<std::uint32_t>& out_shell,
		bool& stop_due_to_limits )
	{
		out_shell.clear();
		stop_due_to_limits = false;
		if ( target_weight < 0 )
			return false;

		if ( word_bits < 1 )
			return true;
		if ( word_bits > 32 )
			word_bits = 32;
		const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
		alpha &= mask;
		beta &= mask;
		output_hint &= mask;

		struct Frame
		{
			int			  bit_position = 0;
			std::uint32_t prefix = 0;
			std::uint32_t prefer = 0;
			std::uint8_t  state = 0;	// 0=enter, 1=after prefer-branch, 2=done
		};

		static constexpr int MAX_STACK = 33;
		std::array<Frame, MAX_STACK> stack {};
		int							 stack_step = 0;
		stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };

		while ( stack_step > 0 )
		{
			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				++context.visited_node_count;
				if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
				{
					stop_due_to_limits = true;
					return false;
				}
				maybe_print_single_run_progress( context, -1 );

				if ( frame.bit_position > 0 )
				{
					const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
					if ( w_prefix < 0 || w_prefix > target_weight )
					{
						--stack_step;
						continue;
					}
					const int remaining_max = word_bits - frame.bit_position;
					if ( w_prefix + remaining_max < target_weight )
					{
						--stack_step;
						continue;
					}
					if ( frame.bit_position == word_bits )
					{
						if ( w_prefix == target_weight )
							out_shell.push_back( frame.prefix );
						--stack_step;
						continue;
					}
				}

				frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
				frame.state = 1;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
				continue;
			}

			if ( frame.state == 1 )
			{
				if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
				{
					stop_due_to_limits = true;
					return false;
				}

				frame.state = 2;
				const std::uint32_t other = 1u - frame.prefer;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
				continue;
			}

			--stack_step;
		}

		return true;
	}

	struct ModularAdditionEnumerator
	{
		struct Frame
		{
			int			  bit_position = 0;
			std::uint32_t prefix = 0;
			std::uint32_t prefer = 0;
			std::uint8_t  state = 0;  // 0=enter, 1=after prefer-branch, 2=done
		};

		static constexpr int MAX_STACK = 33;

		bool initialized = false;
		bool stop_due_to_limits = false;
		bool dfs_active = false;
		bool using_cached_shell = false;
		std::uint32_t alpha = 0;
		std::uint32_t beta = 0;
		std::uint32_t output_hint = 0;
		int			  weight_cap = 0;
		int			  target_weight = 0;
		int			  word_bits = 32;
		std::size_t	  shell_index = 0;
		std::vector<std::uint32_t> shell_cache {};
		int			  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( std::uint32_t a, std::uint32_t b, std::uint32_t hint, int cap, int bit_position = 0, std::uint32_t prefix = 0, int bits = 32 )
		{
			initialized = true;
			stop_due_to_limits = false;
			dfs_active = false;
			using_cached_shell = false;
			alpha = a;
			beta = b;
			word_bits = std::clamp( bits, 1, 32 );
			const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			alpha &= mask;
			beta &= mask;
			output_hint = hint & mask;
			weight_cap = std::min( cap, word_bits - 1 );
			target_weight = 0;
			shell_index = 0;
			shell_cache.clear();
			stack_step = 0;
			stack[ stack_step++ ] = Frame { bit_position, prefix, 0u, 0 };
		}

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_gamma, int& out_weight )
		{
			if ( !initialized || stop_due_to_limits )
				return false;

			while ( true )
			{
				if ( weight_cap < 0 || target_weight > weight_cap )
					return false;

				// If we are iterating a cached shell, emit from it.
				if ( using_cached_shell )
				{
					if ( shell_cache.empty() )
					{
						// Resume path: rebuild the shell cache if possible.
						bool cache_hit = false;
						if ( context.configuration.enable_wpddt_cache && target_weight <= context.configuration.wpddt_max_cached_weight )
						{
							std::vector<std::uint32_t> tmp;
							if ( g_weight_shell_cache.try_get_shell( alpha, beta, target_weight, word_bits, tmp ) )
							{
								cache_hit = true;
								shell_cache = std::move( tmp );
							}
						}
						if ( cache_hit && shell_cache.empty() )
						{
							using_cached_shell = false;
							shell_cache.clear();
							shell_index = 0;
							++target_weight;
							continue;
						}
						if ( shell_cache.empty() )
						{
							bool stop = false;
							if ( !enumerate_add_shell_exact_collect( context, alpha, beta, output_hint, target_weight, word_bits, shell_cache, stop ) )
							{
								stop_due_to_limits = stop;
								return false;
							}
						}
					}

					if ( shell_index < shell_cache.size() )
					{
						out_gamma = shell_cache[ shell_index++ ];
						out_weight = target_weight;
						return true;
					}

					using_cached_shell = false;
					shell_cache.clear();
					shell_index = 0;
					++target_weight;
					continue;
				}

				// Weight-sliced pDDT cached shell path (low weights).
				if ( context.configuration.enable_wpddt_cache && target_weight <= context.configuration.wpddt_max_cached_weight )
				{
					std::vector<std::uint32_t> shell;
					if ( g_weight_shell_cache.try_get_shell( alpha, beta, target_weight, word_bits, shell ) )
					{
						if ( shell.empty() )
						{
							++target_weight;
							continue;
						}
						shell_cache = std::move( shell );
						using_cached_shell = true;
						shell_index = 0;
						continue;
					}

					bool stop = false;
					if ( !enumerate_add_shell_exact_collect( context, alpha, beta, output_hint, target_weight, word_bits, shell, stop ) )
					{
						stop_due_to_limits = stop;
						return false;
					}
					g_weight_shell_cache.maybe_put_shell( alpha, beta, target_weight, word_bits, shell );
					if ( shell.empty() )
					{
						++target_weight;
						continue;
					}
					shell_cache = std::move( shell );
					using_cached_shell = true;
					shell_index = 0;
					continue;
				}

				// DFS shell enumeration (exact, resumable).
				if ( !dfs_active )
				{
					dfs_active = true;
					stack_step = 0;
					stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };
				}

				while ( stack_step > 0 )
				{
					Frame& frame = stack[ stack_step - 1 ];

					if ( frame.state == 0 )
					{
						++context.visited_node_count;
						if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
						{
							stop_due_to_limits = true;
							return false;
						}
						maybe_print_single_run_progress( context, -1 );

						if ( frame.bit_position > 0 )
						{
							const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
							if ( w_prefix < 0 || w_prefix > target_weight )
							{
								--stack_step;
								continue;
							}
							const int remaining_max = word_bits - frame.bit_position;
							if ( w_prefix + remaining_max < target_weight )
							{
								--stack_step;
								continue;
							}
							if ( frame.bit_position == word_bits )
							{
								if ( w_prefix == target_weight )
								{
									out_gamma = frame.prefix;
									out_weight = w_prefix;
									--stack_step;
									return true;
								}
								--stack_step;
								continue;
							}
						}

						frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
						frame.state = 1;
						stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
						continue;
					}

					if ( frame.state == 1 )
					{
						if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
						{
							stop_due_to_limits = true;
							return false;
						}

						frame.state = 2;
						const std::uint32_t other = 1u - frame.prefer;
						stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
						continue;
					}

					--stack_step;
				}

				// Finished this shell; move to next weight.
				dfs_active = false;
				++target_weight;
			}
		}
	};

	struct SubConstEnumerator
	{
		struct Frame
		{
			int							 bit_position = 0;
			std::uint32_t				 prefix = 0;
			std::array<std::uint64_t, 4> prefix_counts {};
			std::uint32_t				 preferred_bit = 0;
			std::uint8_t				 state = 0;  // 0=enter, 1=after preferred-branch, 2=done
		};

		static constexpr int MAX_STACK = 33;
		static constexpr int SLACK = 8;

		bool initialized = false;
		bool stop_due_to_limits = false;
		std::uint32_t input_difference = 0;
		std::uint32_t subtractive_constant = 0;
		std::uint32_t additive_constant = 0;
		std::uint32_t output_hint = 0;
		int						  cap_bitvector = 0;
		int						  cap_dynamic_planning = 0;
		int						  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( std::uint32_t dx, std::uint32_t sub_const, int bvweight_cap )
		{
			initialized = true;
			stop_due_to_limits = false;
			input_difference = dx;
			subtractive_constant = sub_const;
			cap_bitvector = std::clamp( bvweight_cap, 0, 32 );
			cap_dynamic_planning = std::min( 32, cap_bitvector + SLACK );
			additive_constant = std::uint32_t( 0u ) - subtractive_constant;
			output_hint = compute_greedy_output_difference_for_addition_by_constant( input_difference, additive_constant );
			stack_step = 0;
			std::array<std::uint64_t, 4> prefix_counts {};
			prefix_counts[ 0 ] = 1;
			stack[ stack_step++ ] = Frame { 0, 0u, prefix_counts, 0u, 0 };
		}

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_difference, int& out_weight )
		{
			if ( !initialized || stop_due_to_limits )
				return false;

			while ( stack_step > 0 )
			{
				Frame& frame = stack[ stack_step - 1 ];

				if ( frame.state == 0 )
				{
					++context.visited_node_count;
					if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress( context, -1 );

					if ( frame.bit_position > 0 )
					{
						const std::uint64_t total_prefix_count = frame.prefix_counts[ 0 ] + frame.prefix_counts[ 1 ] + frame.prefix_counts[ 2 ] + frame.prefix_counts[ 3 ];
						if ( total_prefix_count == 0 )
						{
							--stack_step;
							continue;
						}
						const int log2_total_prefix_count = floor_log2_uint64( total_prefix_count );
						const int prefix_weight_estimate = frame.bit_position - log2_total_prefix_count;
						if ( prefix_weight_estimate > cap_dynamic_planning )
						{
							--stack_step;
							continue;
						}
					}

					if ( frame.bit_position == 32 )
					{
						const int weight = diff_subconst_bvweight( input_difference, subtractive_constant, frame.prefix );
						if ( weight >= 0 && weight <= cap_bitvector )
						{
							out_difference = frame.prefix;
							out_weight = weight;
							--stack_step;
							return true;
						}
						--stack_step;
						continue;
					}

					const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
					const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
					frame.preferred_bit = ( output_hint >> frame.bit_position ) & 1u;

					const auto next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, frame.preferred_bit );
					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.preferred_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
					continue;
				}

				if ( frame.state == 1 )
				{
					if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
					{
						stop_due_to_limits = ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes );
						return false;
					}

					const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
					const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
					const std::uint32_t other_bit = 1u - frame.preferred_bit;
					const auto			next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, other_bit );

					frame.state = 2;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
					continue;
				}

				--stack_step;
			}

			return false;
		}
	};

	struct AffineSubspaceEnumerator
	{
		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_difference = 0;
			std::uint8_t  state = 0;  // 0=enter, 1=after branch0
		};

		static constexpr int MAX_STACK = 33;

		bool initialized = false;
		bool stop_due_to_limits = false;
		InjectionAffineTransition transition {};
		std::size_t maximum_output_difference_count = 0;
		std::size_t produced_output_difference_count = 0;
		int		  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( const InjectionAffineTransition& t, std::size_t max_outputs )
		{
			initialized = true;
			stop_due_to_limits = false;
			transition = t;
			maximum_output_difference_count = max_outputs;
			produced_output_difference_count = 0;
			stack_step = 0;
			stack[ stack_step++ ] = Frame { 0, transition.offset, 0 };
		}

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_difference )
		{
			if ( !initialized || stop_due_to_limits )
				return false;

			while ( stack_step > 0 )
			{
				if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
					return false;

				Frame& frame = stack[ stack_step - 1 ];

				if ( frame.state == 0 )
				{
					++context.visited_node_count;
					if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress( context, -1 );

					if ( frame.basis_index >= transition.rank_weight )
					{
						out_difference = frame.current_difference;
						++produced_output_difference_count;
						--stack_step;
						return true;
					}

					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_difference, 0 };
					continue;
				}

				if ( context.configuration.maximum_search_nodes != 0 && context.visited_node_count > context.configuration.maximum_search_nodes )
				{
					stop_due_to_limits = true;
					return false;
				}
				if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
					return false;

				frame = Frame { frame.basis_index + 1, frame.current_difference ^ transition.basis_vectors[ size_t( frame.basis_index ) ], 0 };
			}

			return false;
		}
	};

	enum class DifferentialSearchStage : std::uint8_t
	{
		Enter = 0,
		FirstAdd = 1,
		FirstConst = 2,
		InjB = 3,
		SecondAdd = 4,
		SecondConst = 5,
		InjA = 6
	};

	struct DifferentialSearchFrame
	{
		DifferentialSearchStage stage = DifferentialSearchStage::Enter;
		std::size_t			   trail_size_at_entry = 0;
		DifferentialRoundSearchState state {};
		ModularAdditionEnumerator	  enum_first_add {};
		SubConstEnumerator			  enum_first_const {};
		AffineSubspaceEnumerator	  enum_inj_b {};
		ModularAdditionEnumerator	  enum_second_add {};
		SubConstEnumerator			  enum_second_const {};
		AffineSubspaceEnumerator	  enum_inj_a {};
	};

	struct DifferentialSearchCursor
	{
		std::vector<DifferentialSearchFrame> stack;
	};

	struct BinaryCheckpointManager
	{
		std::string path {};
		std::uint64_t every_seconds = 0;
		bool pending_best = false;
		std::chrono::steady_clock::time_point last_write_time {};

		bool enabled() const { return !path.empty(); }
		bool pending_best_change() const { return pending_best; }
		void mark_best_changed() { pending_best = true; }

		// Implemented later (needs serialization helpers).
		void poll( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor );
		bool write_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason );
	};

	static int compute_greedy_upper_bound_weight( const DifferentialBestSearchConfiguration& search_configuration, std::uint32_t initial_branch_a_difference, std::uint32_t initial_branch_b_difference )
	{
		// Greedy initializer: pick per-addition optimal gamma (LM2001 Algorithm 4) and use an identity choice for constants.
		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		long long	  total_weight = 0;

		for ( int round_index = 0; round_index < search_configuration.round_count; ++round_index )
		{
			// First modular addition on branch B
			const std::uint32_t first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 17 );
			auto [ output_branch_b_difference_after_first_addition, weight_first_addition ] = find_optimal_gamma_with_weight( current_branch_b_difference, first_addition_term_difference, 32 );
			if ( weight_first_addition < 0 )
				return INFINITE_WEIGHT;
			current_branch_b_difference = output_branch_b_difference_after_first_addition;
			total_weight += weight_first_addition;

			// A -= ROUND_CONSTANTS[1] (rough greedy: keep output difference equal to input difference, but score it)
			{
				const int weight_constant_subtraction_identity_choice = diff_subconst_bvweight( current_branch_a_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], current_branch_a_difference );
				if ( weight_constant_subtraction_identity_choice < 0 )
					return INFINITE_WEIGHT;
				total_weight += weight_constant_subtraction_identity_choice;
			}

			// First XOR/ROT pair
			current_branch_a_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			current_branch_b_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			// Injection from branch B into branch A (pick one reachable output: offset)
			{
				const InjectionAffineTransition injection_transition = compute_injection_transition_from_branch_b( current_branch_b_difference );
				total_weight += injection_transition.rank_weight;
				current_branch_a_difference ^= injection_transition.offset;
			}

			// Linear layer on branch B (L1 removed)

			// Second modular addition on branch A
			const std::uint32_t second_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] = find_optimal_gamma_with_weight( current_branch_a_difference, second_addition_term_difference, 32 );
			if ( weight_second_addition < 0 )
				return INFINITE_WEIGHT;
			current_branch_a_difference = output_branch_a_difference_after_second_addition;
			total_weight += weight_second_addition;

			// B -= ROUND_CONSTANTS[6] (rough greedy: keep output difference equal to input difference, but score it)
			{
				const int weight_constant_subtraction_identity_choice = diff_subconst_bvweight( current_branch_b_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], current_branch_b_difference );
				if ( weight_constant_subtraction_identity_choice < 0 )
					return INFINITE_WEIGHT;
				total_weight += weight_constant_subtraction_identity_choice;
			}

			// Second XOR/ROT pair
			current_branch_b_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			current_branch_a_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			// Injection from branch A into branch B (pick one reachable output: offset)
			{
				const InjectionAffineTransition injection_transition = compute_injection_transition_from_branch_a( current_branch_a_difference );
				total_weight += injection_transition.rank_weight;
				current_branch_b_difference ^= injection_transition.offset;
			}

			// Linear layer on branch A (L2 removed)
		}
		if ( total_weight > INFINITE_WEIGHT )
			return INFINITE_WEIGHT;
		return int( total_weight );
	}

	static std::vector<DifferentialTrailStepRecord> construct_greedy_initial_differential_trail( const DifferentialBestSearchConfiguration& search_configuration, std::uint32_t initial_branch_a_difference, std::uint32_t initial_branch_b_difference, int& output_total_weight )
	{
		// Construct an explicit greedy trail so we always have a baseline solution to print,
		// even if the main search hits the maximum node budget before reaching any leaf.
		std::vector<DifferentialTrailStepRecord> trail;
		trail.reserve( std::max( 1, search_configuration.round_count ) );

		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		long long	  total_weight = 0;

		for ( int round_index = 0; round_index < search_configuration.round_count; ++round_index )
		{
			DifferentialTrailStepRecord step_record {};
			step_record.round_index = round_index + 1;
			step_record.input_branch_a_difference = current_branch_a_difference;
			step_record.input_branch_b_difference = current_branch_b_difference;

			// First modular addition on branch B
			step_record.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 17 );
			auto [ output_branch_b_difference_after_first_addition, weight_first_addition ] = find_optimal_gamma_with_weight( current_branch_b_difference, step_record.first_addition_term_difference, 32 );
			if ( weight_first_addition < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
			step_record.weight_first_addition = weight_first_addition;
			total_weight += weight_first_addition;

			// Constant subtraction on branch A (greedy identity choice)
			step_record.output_branch_a_difference_after_first_constant_subtraction = current_branch_a_difference;
			step_record.weight_first_constant_subtraction = diff_subconst_bvweight( current_branch_a_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], step_record.output_branch_a_difference_after_first_constant_subtraction );
			if ( step_record.weight_first_constant_subtraction < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight += step_record.weight_first_constant_subtraction;

			// First XOR/ROT pair
			step_record.branch_a_difference_after_first_xor_with_rotated_branch_b = step_record.output_branch_a_difference_after_first_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( step_record.output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			step_record.branch_b_difference_after_first_xor_with_rotated_branch_a = step_record.output_branch_b_difference_after_first_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			// Injection from branch B into branch A (greedy offset choice)
			const InjectionAffineTransition injection_transition_from_branch_b = compute_injection_transition_from_branch_b( step_record.branch_b_difference_after_first_xor_with_rotated_branch_a );
			step_record.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
			step_record.injection_from_branch_b_xor_difference = injection_transition_from_branch_b.offset;
			step_record.branch_a_difference_after_injection_from_branch_b = step_record.branch_a_difference_after_first_xor_with_rotated_branch_b ^ step_record.injection_from_branch_b_xor_difference;
			total_weight += step_record.weight_injection_from_branch_b;

			// Linear layer on branch B (L1 removed)
			step_record.branch_b_difference_after_linear_layer_one_backward = step_record.branch_b_difference_after_first_xor_with_rotated_branch_a;

			// Second modular addition on branch A
			step_record.second_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_linear_layer_one_backward, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_linear_layer_one_backward, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] = find_optimal_gamma_with_weight( step_record.branch_a_difference_after_injection_from_branch_b, step_record.second_addition_term_difference, 32 );
			if ( weight_second_addition < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
			step_record.weight_second_addition = weight_second_addition;
			total_weight += weight_second_addition;

			// Constant subtraction on branch B (greedy identity choice)
			step_record.output_branch_b_difference_after_second_constant_subtraction = step_record.branch_b_difference_after_linear_layer_one_backward;
			step_record.weight_second_constant_subtraction = diff_subconst_bvweight( step_record.branch_b_difference_after_linear_layer_one_backward, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], step_record.output_branch_b_difference_after_second_constant_subtraction );
			if ( step_record.weight_second_constant_subtraction < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight += step_record.weight_second_constant_subtraction;

			// Second XOR/ROT pair
			step_record.branch_b_difference_after_second_xor_with_rotated_branch_a = step_record.output_branch_b_difference_after_second_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( step_record.output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			step_record.branch_a_difference_after_second_xor_with_rotated_branch_b = step_record.output_branch_a_difference_after_second_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			// Injection from branch A into branch B (greedy offset choice)
			const InjectionAffineTransition injection_transition_from_branch_a = compute_injection_transition_from_branch_a( step_record.branch_a_difference_after_second_xor_with_rotated_branch_b );
			step_record.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
			step_record.injection_from_branch_a_xor_difference = injection_transition_from_branch_a.offset;
			total_weight += step_record.weight_injection_from_branch_a;

			// End-of-round boundary differences
			step_record.output_branch_b_difference = step_record.branch_b_difference_after_second_xor_with_rotated_branch_a ^ step_record.injection_from_branch_a_xor_difference;
			step_record.output_branch_a_difference = step_record.branch_a_difference_after_second_xor_with_rotated_branch_b;

			step_record.round_weight = step_record.weight_first_addition + step_record.weight_first_constant_subtraction + step_record.weight_injection_from_branch_b + step_record.weight_second_addition + step_record.weight_second_constant_subtraction + step_record.weight_injection_from_branch_a;

			trail.push_back( step_record );

			current_branch_a_difference = step_record.output_branch_a_difference;
			current_branch_b_difference = step_record.output_branch_b_difference;
		}

		if ( total_weight > INFINITE_WEIGHT )
			total_weight = INFINITE_WEIGHT;
		output_total_weight = int( total_weight );
		return trail;
	}

	template <class OnFullGamma>
	static void enumerate_modular_addition_output_differences_by_bit_recursion( DifferentialBestSearchContext& search_context, std::uint32_t input_difference_alpha, std::uint32_t input_difference_beta,
																				std::uint32_t output_difference_hint,	 // branch ordering hint (try this bit first)
																				int			  bit_position,				 // assigned bits [0..bit_position-1]
																				std::uint32_t output_difference_prefix,	 // assigned bits in [0..bit_position-1]
																				int weight_cap, OnFullGamma&& on_full_gamma )
	{
		// Iterative DFS to avoid recursion overhead.
		// IMPORTANT: preserve the recursive version's:
		// - pre-order budget check per frame (visited_node_count++)
		// - maybe_print call placement
		// - branch order: prefer first, then (1-prefer)
		// - between-branch stop check (max nodes)
		struct Frame
		{
			int			  bit_position = 0;
			std::uint32_t prefix = 0;
			std::uint32_t prefer = 0;
			std::uint8_t  state = 0;  // 0=enter, 1=after prefer-branch, 2=done
		};

		// Hot-path: avoid per-call heap allocations (`std::vector`) by using a fixed stack.
		// Maximum depth is 33 frames (bits 0..32 inclusive).
		constexpr int MAX_STACK = 33;
		std::array<Frame, MAX_STACK> stack;
		int							 stack_step = 0;
		stack[ stack_step++ ] = Frame { bit_position, output_difference_prefix, 0u, 0 };

		while ( stack_step > 0 )
		{
			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				// Always count visited nodes (even when max_nodes=0/unlimited) so progress output
				// does not get stuck at nodes=0 during deep runs.
				++search_context.visited_node_count;
				if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
					return;
				maybe_print_single_run_progress( search_context, -1 );

				if ( frame.bit_position > 0 )
				{
					// Optimistic (prefix) weight using truncated n=f.bit_position bits.
					// By monotonicity, this weight can only stay the same or increase as bit_position grows.
					const int w_prefix = xdp_add_lm2001_n( input_difference_alpha, input_difference_beta, frame.prefix, frame.bit_position );
					if ( w_prefix < 0 )
					{
						--stack_step;
						continue;  // impossible already at prefix
					}
					if ( w_prefix > weight_cap )
					{
						--stack_step;
						continue;  // can't ever go below weight_cap
					}
					if ( frame.bit_position == 32 )
					{
						on_full_gamma( frame.prefix, w_prefix );
						--stack_step;
						continue;
					}
				}

				frame.prefer = ( output_difference_hint >> frame.bit_position ) & 1u;
				frame.state = 1;
				// (sp <= MAX_STACK) always holds by construction (depth <= 33).
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
				continue;
			}

			if ( frame.state == 1 )
			{
				// Between-branch stop check (matches recursive version).
				if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
					return;

				frame.state = 2;
				const std::uint32_t other = 1u - frame.prefer;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
				continue;
			}

			// done
			--stack_step;
		}
	}

	template <class OnOutputDifferenceFn>
	static void enumerate_affine_subspace_output_differences_recursive( DifferentialBestSearchContext& search_context, const InjectionAffineTransition& transition, std::uint32_t accumulated_difference, int basis_index, std::size_t& produced_output_difference_count, std::size_t maximum_output_difference_count, OnOutputDifferenceFn&& on_output_difference )
	{
		// Iterative DFS over the affine subspace to avoid deep recursion overhead.
		// IMPORTANT: keep the exact traversal order and budget checks as the recursive version:
		// - pre-order "node" budget check per frame (visited_node_count++)
		// - branch 0 then branch 1
		// - between branches, re-check stop conditions to preserve behavior under tight limits
		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_difference = 0; //accumulated
			std::uint8_t  state = 0;  // 0=enter, 1=after branch0 (about to do branch1)
		};

		// Hot-path: avoid per-call heap allocations (`std::vector`) by using a fixed stack.
		// Maximum depth is 33 frames (idx 0..32 inclusive).
		constexpr int MAX_STACK = 33;
		std::array<Frame, MAX_STACK> stack;
		int							 stack_step = 0;
		stack[ stack_step++ ] = Frame { basis_index, accumulated_difference, 0 };

		while ( stack_step > 0 )
		{
			if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
				return;

			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				++search_context.visited_node_count;
				if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
					return;
				maybe_print_single_run_progress( search_context, -1 );

				if ( frame.basis_index >= transition.rank_weight )
				{
					on_output_difference( frame.current_difference );
					++produced_output_difference_count;
					--stack_step;
					continue;
				}

				// branch 0: do not use basis_vectors[idx]
				frame.state = 1;
				stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_difference, 0 };
				continue;
			}

			// After branch 0: preserve the recursive version's between-branch stop checks.
			if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
				return;
			if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
				return;

			// branch 1: use basis_vectors[idx]
			// Replace the current frame in-place (same effect as pop+push in the vector version).
			frame = Frame { frame.basis_index + 1, frame.current_difference ^ transition.basis_vectors[ size_t( frame.basis_index ) ], 0 };
		}
	}

	// Enumerate candidate output XOR-differences from an affine subspace model:
	//   offset ⊕ span(basis_vectors)
	//
	// Why is this a template with `OnOutputDifferenceFn&&`?
	// - `OnOutputDifferenceFn` here means a callable type (a function object), NOT "faith/belief".
	// - `on_output_difference` is a callback that will be INVOKED for every generated output difference.
	//   Call sites typically pass a lambda that captures the current search state and immediately scores/prunes.
	// - This design avoids allocating/storing all differences in a container, and avoids `std::function`'s
	//   potential type-erasure overhead. In practice it allows inlining and early-exit checks, which matters
	//   because enumeration can be exponential in `rank_weight`.
	//
	// Here, `on_output_difference` serves as a callback function that processes each enumerated output difference.
	// It does not directly return a vector to conserve memory/avoid allocations and to facilitate pruning and early exit (performance-critical).
	template <class OnOutputDifferenceFn>
	static void enumerate_affine_subspace_output_differences( DifferentialBestSearchContext& search_context, const InjectionAffineTransition& transition, std::size_t maximum_output_difference_count, OnOutputDifferenceFn&& on_output_difference )
	{
		// Enumerate all elements of the affine subspace: offset ⊕ span(basis_vectors).
		// NOTE: This is exact; runtime can be exponential in rank_weight, controlled by search_context.configuration.maximum_search_nodes.
		std::size_t produced_output_difference_count = 0;
		if ( transition.rank_weight <= 0 )
		{
			on_output_difference( transition.offset );
			++produced_output_difference_count;
			return;
		}
		enumerate_affine_subspace_output_differences_recursive( search_context, transition, transition.offset, 0, produced_output_difference_count, maximum_output_difference_count, on_output_difference );
	}

	// ============================================================================
	// Subtraction by a constant (XOR-differential transition enumeration)
	//
	// Goal: avoid forcing Δout == Δin. We enumerate candidate output differences Δy for:
	//   y = x - constant  (mod 2^32)
	// under XOR differences.
	//
	// We generate candidates by exact carry-DP (4 carry-pair states) and prune by a DP-based weight
	// cap (w_dp). We then score candidates using the existing operator:
	//   diff_subconst_bvweight(Δx, cst, Δy)
	//
	// This keeps compatibility with your current ARX operator stack while allowing Δy to vary.
	// ============================================================================

	static std::array<std::uint64_t, 4> compute_next_prefix_counts_for_addition_by_constant_at_bit( const std::array<std::uint64_t, 4>& prefix_counts_by_carry_pair_state, std::uint32_t input_difference_bit, std::uint32_t additive_constant_bit, std::uint32_t output_difference_bit )
	{
		auto carry_out_bit = []( std::uint32_t x_bit, std::uint32_t k_bit, std::uint32_t c_in ) -> std::uint32_t {
			return ( x_bit & k_bit ) | ( x_bit & c_in ) | ( k_bit & c_in );
		};

		// carry-pair state index = (carry_bit<<1) | carry_bit_prime
		std::array<std::uint64_t, 4> next_prefix_counts_by_carry_pair_state {};
		for ( int carry_pair_state_index = 0; carry_pair_state_index < 4; ++carry_pair_state_index )
		{
			const std::uint64_t prefix_count = prefix_counts_by_carry_pair_state[ size_t( carry_pair_state_index ) ];
			if ( prefix_count == 0 )
				continue;
			const std::uint32_t carry_bit = ( std::uint32_t( carry_pair_state_index ) >> 1 ) & 1u;
			const std::uint32_t carry_bit_prime = ( std::uint32_t( carry_pair_state_index ) >> 0 ) & 1u;
			const std::uint32_t required_output_difference_bit = input_difference_bit ^ carry_bit ^ carry_bit_prime;
			if ( required_output_difference_bit != output_difference_bit )
				continue;

			for ( std::uint32_t input_bit = 0; input_bit <= 1; ++input_bit )
			{
				const std::uint32_t input_bit_prime = input_bit ^ input_difference_bit;
				const std::uint32_t next_carry_bit = carry_out_bit( input_bit, additive_constant_bit, carry_bit );
				const std::uint32_t next_carry_bit_prime = carry_out_bit( input_bit_prime, additive_constant_bit, carry_bit_prime );
				const int			next_carry_pair_state_index = int( ( next_carry_bit << 1 ) | next_carry_bit_prime );
				next_prefix_counts_by_carry_pair_state[ size_t( next_carry_pair_state_index ) ] += prefix_count;
			}
		}
		return next_prefix_counts_by_carry_pair_state;
	}

	static std::uint32_t compute_greedy_output_difference_for_addition_by_constant( std::uint32_t input_difference, std::uint32_t additive_constant )
	{
		// Greedy pick output difference bit-by-bit to maximize prefix count mass.
		std::array<std::uint64_t, 4> prefix_counts_by_carry_pair_state {};
		prefix_counts_by_carry_pair_state[ 0 ] = 1;	 // (carry,carry') = (0,0)
		std::uint32_t output_difference = 0;
		for ( int bit_position = 0; bit_position < 32; ++bit_position )
		{
			const std::uint32_t input_difference_bit = ( input_difference >> bit_position ) & 1u;
			const std::uint32_t additive_constant_bit = ( additive_constant >> bit_position ) & 1u;
			const auto			next_prefix_counts_when_output_bit_is_zero = compute_next_prefix_counts_for_addition_by_constant_at_bit( prefix_counts_by_carry_pair_state, input_difference_bit, additive_constant_bit, 0u );
			const auto			next_prefix_counts_when_output_bit_is_one = compute_next_prefix_counts_for_addition_by_constant_at_bit( prefix_counts_by_carry_pair_state, input_difference_bit, additive_constant_bit, 1u );
			const std::uint64_t total_prefix_count_when_output_bit_is_zero = next_prefix_counts_when_output_bit_is_zero[ 0 ] + next_prefix_counts_when_output_bit_is_zero[ 1 ] + next_prefix_counts_when_output_bit_is_zero[ 2 ] + next_prefix_counts_when_output_bit_is_zero[ 3 ];
			const std::uint64_t total_prefix_count_when_output_bit_is_one = next_prefix_counts_when_output_bit_is_one[ 0 ] + next_prefix_counts_when_output_bit_is_one[ 1 ] + next_prefix_counts_when_output_bit_is_one[ 2 ] + next_prefix_counts_when_output_bit_is_one[ 3 ];
			const std::uint32_t chosen_output_difference_bit = ( total_prefix_count_when_output_bit_is_one > total_prefix_count_when_output_bit_is_zero ) ? 1u : 0u;
			output_difference |= ( chosen_output_difference_bit << bit_position );
			prefix_counts_by_carry_pair_state = ( chosen_output_difference_bit ? next_prefix_counts_when_output_bit_is_one : next_prefix_counts_when_output_bit_is_zero );
		}
		return output_difference;
	}

	template <class OnOutputDifference>
	static void enumerate_addition_by_constant_output_differences_by_bit_recursion( DifferentialBestSearchContext& search_context, std::uint32_t input_difference, std::uint32_t additive_constant, std::uint32_t output_difference_hint, int bit_position, std::uint32_t output_difference_prefix, std::array<std::uint64_t, 4> prefix_counts_by_carry_pair_state, int weight_cap_for_prefix_count_pruning, OnOutputDifference&& on_output_difference )
	{
		// Iterative DFS to avoid recursion overhead.
		// IMPORTANT: preserve the recursive version's:
		// - pre-order node budget check per frame (visited_node_count++)
		// - maybe_print placement
		// - branch order: preferred bit first, then (1-preferred)
		// - between-branch stop checks (max nodes)
		struct Frame
		{
			int							 bit_position = 0;
			std::uint32_t				 prefix = 0;
			std::array<std::uint64_t, 4> prefix_counts {};
			std::uint32_t				 preferred_bit = 0;
			std::uint8_t				 state = 0;	 // 0=enter, 1=after preferred-branch, 2=done
		};

		// Hot-path: avoid per-call heap allocations (`std::vector`) by using a fixed stack.
		// Maximum depth is 33 frames (bits 0..32 inclusive).
		constexpr int MAX_STACK = 33;
		std::array<Frame, MAX_STACK> stack;
		int							 stack_step = 0;
		stack[ stack_step++ ] = Frame { bit_position, output_difference_prefix, prefix_counts_by_carry_pair_state, 0u, 0 };

		while ( stack_step > 0 )
		{
			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				++search_context.visited_node_count;
				if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
					return;
				maybe_print_single_run_progress( search_context, -1 );

				if ( frame.bit_position > 0 )
				{
					const std::uint64_t total_prefix_count = frame.prefix_counts[ 0 ] + frame.prefix_counts[ 1 ] + frame.prefix_counts[ 2 ] + frame.prefix_counts[ 3 ];
					if ( total_prefix_count == 0 )
					{
						--stack_step;
						continue;
					}
					const int log2_total_prefix_count = floor_log2_uint64( total_prefix_count );
					const int prefix_weight_estimate = frame.bit_position - log2_total_prefix_count;  // ceil(-log2(total_prefix_count/2^bit_position))
					if ( prefix_weight_estimate > weight_cap_for_prefix_count_pruning )
					{
						--stack_step;
						continue;
					}
					if ( frame.bit_position == 32 )
					{
						on_output_difference( frame.prefix );
						--stack_step;
						continue;
					}
				}

				const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
				const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
				frame.preferred_bit = ( output_difference_hint >> frame.bit_position ) & 1u;

				const auto next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, frame.preferred_bit );
				frame.state = 1;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.preferred_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
				continue;
			}

			if ( frame.state == 1 )
			{
				if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
					return;

				const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
				const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
				const std::uint32_t other_bit = 1u - frame.preferred_bit;
				const auto			next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, other_bit );

				frame.state = 2;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
				continue;
			}

			--stack_step;
		}
	}

	template <class OnOutputDifferenceFn>
	static void enumerate_subtraction_by_constant_output_differences( DifferentialBestSearchContext& search_context, std::uint32_t input_difference, std::uint32_t subtractive_constant, int bvweight_cap, OnOutputDifferenceFn&& on_out )
	{
		// Enumerate Δout for y=x-subtractive_constant using DP enumeration, then score with diff_subconst_bvweight.
		// Exact enumeration (no max-candidate cap). We allow some slack between DP-pruning weight and bvweight
		// to avoid missing candidates due to approximation differences.
		constexpr int SLACK = 8;
		const int	  cap_bitvector = std::clamp( bvweight_cap, 0, 32 );
		const int	  cap_dynamic_planning = std::min( 32, cap_bitvector + SLACK );
	
		const std::uint32_t additive_constant = std::uint32_t( 0u ) - subtractive_constant;  // x - c == x + (-c) (mod 2^32)
		const std::uint32_t output_difference_hint = compute_greedy_output_difference_for_addition_by_constant( input_difference, additive_constant );
	
		std::array<std::uint64_t, 4> prefix_counts_by_carry_pair_state {};
		prefix_counts_by_carry_pair_state[ 0 ] = 1;  // (carry,carry')=(0,0)
	
		enumerate_addition_by_constant_output_differences_by_bit_recursion(
			search_context, input_difference, additive_constant, output_difference_hint, 0, 0u, prefix_counts_by_carry_pair_state, cap_dynamic_planning,
			[ & ]( std::uint32_t output_difference )
			{
				const int weight = diff_subconst_bvweight( input_difference, subtractive_constant, output_difference );
				if ( weight < 0 )
					return;
				if ( weight > cap_bitvector )
					return;
				on_out( output_difference, weight );
			} );
	}

	//ARX Automatic Search Frame - Differential Analysis Paper:
	//Automatic Search for the Best Trails in ARX - Application to Block Cipher Speck
	//Is applied to NeoAlzette ARX-Box Algorithm every step of the round
	class DifferentialBestTrailSearcher final
	{
	public:
		
		// Differential round model (Δ denotes XOR difference; constants do not change Δ directly):

		// Engineering: DFS over reachable Δ values; probabilistic steps use BV-weight (≈ -log2 Pr),
		// with Matsui-style pruning: prune if W_acc + W_rem >= W_best.
		explicit DifferentialBestTrailSearcher( DifferentialBestSearchContext& search_context_in )
			: search_context( search_context_in )
		{
		}

		
		// Entry: start from round input differences and walk forward.
		void search_from_start( std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference )
		{
			// Engineering: per-round shared state lives in a stack to avoid long lambda captures.
			round_state_stack.clear();
			const int reserve_rounds = std::max( 1, search_context.configuration.round_count );
			round_state_stack.reserve( std::size_t( reserve_rounds ) + 1u );
			search_recursive( 0, branch_a_input_difference, branch_b_input_difference, 0 );
		}

	private:
		DifferentialBestSearchContext& search_context;

		// Per-round shared state for nested enumeration callbacks.
		// Engineering: every lambda only captures `this`, all shared variables live here.
		struct RoundSearchState
		{
			int round_boundary_depth = 0;
			int accumulated_weight_so_far = 0;
			int remaining_round_weight_lower_bound_after_this_round = 0;

			std::uint32_t branch_a_input_difference = 0;
			std::uint32_t branch_b_input_difference = 0;

			DifferentialTrailStepRecord base_step {};

			std::uint32_t first_addition_term_difference = 0;
			std::uint32_t optimal_output_branch_b_difference_after_first_addition = 0;
			int			  optimal_weight_first_addition = 0;
			int			  weight_cap_first_addition = 0;

			std::uint32_t output_branch_b_difference_after_first_addition = 0;
			int			  weight_first_addition = 0;
			int			  accumulated_weight_after_first_addition = 0;

			std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
			int			  weight_first_constant_subtraction = 0;
			int			  accumulated_weight_after_first_constant_subtraction = 0;

			std::uint32_t branch_a_difference_after_first_xor_with_rotated_branch_b = 0;
			std::uint32_t branch_b_difference_after_first_xor_with_rotated_branch_a = 0;
			std::uint32_t branch_b_difference_after_linear_layer_one_backward = 0;

			int			  weight_injection_from_branch_b = 0;
			int			  accumulated_weight_before_second_addition = 0;
			std::uint32_t injection_from_branch_b_xor_difference = 0;
			std::uint32_t branch_a_difference_after_injection_from_branch_b = 0;

			std::uint32_t second_addition_term_difference = 0;
			std::uint32_t optimal_output_branch_a_difference_after_second_addition = 0;
			int			  optimal_weight_second_addition = 0;
			int			  weight_cap_second_addition = 0;

			std::uint32_t output_branch_a_difference_after_second_addition = 0;
			int			  weight_second_addition = 0;
			int			  accumulated_weight_after_second_addition = 0;

			std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
			int			  weight_second_constant_subtraction = 0;
			int			  accumulated_weight_after_second_constant_subtraction = 0;

			std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;
			std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;

			int			  weight_injection_from_branch_a = 0;
			int			  accumulated_weight_at_round_end = 0;
			std::uint32_t injection_from_branch_a_xor_difference = 0;

			std::uint32_t output_branch_a_difference = 0;
			std::uint32_t output_branch_b_difference = 0;
		};

		std::vector<RoundSearchState> round_state_stack;

		RoundSearchState& current_round_state()
		{
			return round_state_stack.back();
		}

		const RoundSearchState& current_round_state() const
		{
			return round_state_stack.back();
		}

		// DFS at a round boundary (ΔA, ΔB). Split into stages for clarity and shorter recursion body.
		void search_recursive( int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			if ( should_stop_search( round_boundary_depth, accumulated_weight_so_far ) )
				return;
			if ( handle_round_end_if_needed( round_boundary_depth, accumulated_weight_so_far ) )
				return;
			if ( should_prune_state_memoization( round_boundary_depth, branch_a_input_difference, branch_b_input_difference, accumulated_weight_so_far ) )
				return;

			round_state_stack.emplace_back();
			if ( !prepare_round_state( round_boundary_depth, branch_a_input_difference, branch_b_input_difference, accumulated_weight_so_far ) )
			{
				round_state_stack.pop_back();
				return;
			}

			enumerate_first_addition_candidates();

			round_state_stack.pop_back();
		}

		// Stop conditions and global pruning (budget/time/best bound).
		// Math: if W_acc + W_rem(rounds_left) >= W_best then prune.
		bool should_stop_search( int round_boundary_depth, int accumulated_weight_so_far )
		{
			// Early stop: reached target probability (weight) already.
			if ( search_context.configuration.target_best_weight >= 0 && search_context.best_total_weight <= search_context.configuration.target_best_weight )
				return true;

			if ( search_context.stop_due_to_time_limit )
				return true;

			// Count visited nodes for progress reporting even when maximum_search_nodes is unlimited (0).
			// This was previously only incremented when maximum_search_nodes != 0, which made
			// deep runs (max_nodes=0) misleadingly report nodes=0 and nodes_per_sec=0 forever.
			++search_context.visited_node_count;

			// Governor hook: very low overhead (one clock read every ~262k nodes).
			if ( ( search_context.visited_node_count & search_context.progress_node_mask ) == 0 )
			{
				memory_governor_poll_if_needed( std::chrono::steady_clock::now() );
			}

			// Time limit check (low overhead): evaluate clock only every ~262k nodes.
			if ( search_context.configuration.maximum_search_seconds != 0 )
			{
				if ( ( search_context.visited_node_count & search_context.progress_node_mask ) == 0 )
				{
					const auto now = std::chrono::steady_clock::now();
					memory_governor_poll_if_needed( now );
					const double elapsed = std::chrono::duration<double>( now - search_context.time_limit_start_time ).count();
					if ( elapsed >= double( search_context.configuration.maximum_search_seconds ) )
					{
						search_context.stop_due_to_time_limit = true;
						return true;
					}
				}
			}

			// Node budget check (0 = unlimited). We use '>' because visited_node_count was
			// already incremented above.
			if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
				return true;

			maybe_print_single_run_progress( search_context, round_boundary_depth );
			if ( accumulated_weight_so_far > search_context.best_total_weight )
				return true;

			if ( should_prune_remaining_round_lower_bound( round_boundary_depth, accumulated_weight_so_far ) )
				return true;

			return false;
		}

		// Remaining-round lower bound pruning (Matsui).
		bool should_prune_remaining_round_lower_bound( int round_boundary_depth, int accumulated_weight_so_far ) const
		{
			// Matsui-style remaining-round lower bound pruning (round-level).
			// If we have a valid lower bound W_k for the best possible continuation of k rounds,
			// then any completion from here costs at least:
			//   accumulated_weight_so_far + W_{rounds_left}
			// If that can't beat the current best, prune.
			if ( search_context.best_total_weight < INFINITE_WEIGHT && search_context.configuration.enable_remaining_round_lower_bound )
			{
				const int rounds_left = search_context.configuration.round_count - round_boundary_depth;
				if ( rounds_left >= 0 )
				{
					const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
					const std::size_t table_index = std::size_t( rounds_left );
					if ( table_index < remaining_round_min_weight_table.size() )
					{
						const int weight_lower_bound = remaining_round_min_weight_table[ table_index ];
						if ( accumulated_weight_so_far + weight_lower_bound >= search_context.best_total_weight )
							return true;
					}
				}
			}
			return false;
		}

		// Reached final round boundary: update best trail if improved.
		bool handle_round_end_if_needed( int round_boundary_depth, int accumulated_weight_so_far )
		{
			if ( round_boundary_depth != search_context.configuration.round_count )
				return false;

			if ( accumulated_weight_so_far < search_context.best_total_weight || search_context.best_differential_trail.empty() )
			{
				const int old = search_context.best_total_weight;
				search_context.best_total_weight = accumulated_weight_so_far;
				search_context.best_differential_trail = search_context.current_differential_trail;
				if ( search_context.checkpoint && accumulated_weight_so_far != old )
				{
					search_context.checkpoint->maybe_write( search_context, "improved" );
				}
			}
			return true;
		}

		// Engineering: memoization prunes revisits at round boundaries.
		bool should_prune_state_memoization( int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			if ( !search_context.configuration.enable_state_memoization )
				return false;

			const std::size_t rc = std::size_t( std::max( 1, search_context.configuration.round_count ) );
			std::size_t		  hint = ( rc == 0 ) ? 0 : ( search_context.configuration.maximum_search_nodes / rc );
			hint = std::clamp<std::size_t>( hint, 4096u, 1'000'000u );

			const std::uint64_t key = pack_two_word32_differences( branch_a_input_difference, branch_b_input_difference );
			return search_context.memoization.should_prune_and_update( std::size_t( round_boundary_depth ), key, accumulated_weight_so_far, true, true, hint, 192ull, "memoization.reserve", "memoization.try_emplace" );
		}

		// Lower bound for rounds after finishing current round (tightens intra-round caps).
		int compute_remaining_round_weight_lower_bound_after_this_round( int round_boundary_depth ) const
		{
			if ( !search_context.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left_after = search_context.configuration.round_count - ( round_boundary_depth + 1 );
			if ( rounds_left_after < 0 )
				return 0;
			const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t( rounds_left_after );
			if ( idx >= remaining_round_min_weight_table.size() )
				return 0;
			return std::max( 0, remaining_round_min_weight_table[ idx ] );
		}

		// Fast check for W_acc + W_rem_after >= W_best.
		bool should_prune_with_remaining_round_lower_bound( int accumulated_weight ) const
		{
			const auto& state = current_round_state();
			return accumulated_weight + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight;
		}

		// Prepare per-round fixed terms and caps.
		// Math: ΔT0 = ROTL_31(ΔA0) ⊕ ROTL_17(ΔA0).
		// Engineering: compute optimistic weight caps before enumerating any candidates.
		bool prepare_round_state( int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			auto& state = current_round_state();
			state.round_boundary_depth = round_boundary_depth;
			state.accumulated_weight_so_far = accumulated_weight_so_far;
			state.branch_a_input_difference = branch_a_input_difference;
			state.branch_b_input_difference = branch_b_input_difference;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( round_boundary_depth );

			state.base_step = DifferentialTrailStepRecord {};
			state.base_step.round_index = round_boundary_depth + 1;
			state.base_step.input_branch_a_difference = branch_a_input_difference;
			state.base_step.input_branch_b_difference = branch_b_input_difference;

			state.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			state.base_step.first_addition_term_difference = state.first_addition_term_difference;

			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, state.first_addition_term_difference, 32 );
			state.optimal_output_branch_b_difference_after_first_addition = optimal_output_branch_b_difference_after_first_addition;
			state.optimal_weight_first_addition = optimal_weight_first_addition;
			if ( state.optimal_weight_first_addition < 0 )
				return false;

			int weight_cap_first_addition = search_context.best_total_weight - accumulated_weight_so_far - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_first_addition = std::min( weight_cap_first_addition, 31 );
			weight_cap_first_addition = std::min( weight_cap_first_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
			state.weight_cap_first_addition = weight_cap_first_addition;
			if ( weight_cap_first_addition < 0 || state.optimal_weight_first_addition > weight_cap_first_addition )
				return false;

			return true;
		}

		// Enumerate ΔB1 for B1 = B0 ⊕ T0.
		// Engineering: bit-recursion enumerator yields (ΔB1, w_add0) candidates.
		void enumerate_first_addition_candidates()
		{
			auto& state = current_round_state();
			enumerate_modular_addition_output_differences_by_bit_recursion
			(
				search_context, state.branch_b_input_difference, state.first_addition_term_difference, state.optimal_output_branch_b_difference_after_first_addition, 0, 0u, state.weight_cap_first_addition,
				[ this ]( std::uint32_t output_branch_b_difference_after_first_addition, int weight_first_addition )
				{
					handle_first_addition_candidate( output_branch_b_difference_after_first_addition, weight_first_addition );
				}
			);
		}

		// A1 = A0 ⊟ RC1 (ΔA1 is non-deterministic, weighted by diff_subconst_bvweight).
		// Mathematical note: diff_subconst_bvweight is specifically designed for estimating differential weights in x ⊟ c,
		// not equivalent to x ⊞ y or x ⊟ y (two variables).
		// Engineering Explanation: When constants are involved, the achievable Δ space is smaller, but the constraints are stronger and the distribution is “denser,”
		// resulting in more concentrated weight evaluations. Therefore, a separate enumeration and weight model is required.
		void handle_first_addition_candidate( std::uint32_t output_branch_b_difference_after_first_addition, int weight_first_addition )
		{
			auto& state = current_round_state();
			state.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
			state.weight_first_addition = weight_first_addition;
			state.accumulated_weight_after_first_addition = state.accumulated_weight_so_far + weight_first_addition;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_after_first_addition ) )
				return;

			const std::uint32_t round_constant_for_first_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 1 ];
			const int			weight_cap_first_constant_subtraction =
				std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
					search_context.best_total_weight - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round );
			if ( weight_cap_first_constant_subtraction < 0 )
				return;

			enumerate_subtraction_by_constant_output_differences
			(
				search_context, state.branch_a_input_difference, round_constant_for_first_subtraction, weight_cap_first_constant_subtraction,
				[ this ]( std::uint32_t output_branch_a_difference_after_first_constant_subtraction, int weight_first_constant_subtraction )
				{
					handle_first_constant_subtraction_candidate( output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction );
				}
			);
		}

		// XOR/ROT mixing and injection B -> A.
		// Math: ΔA2 = ΔA1 ⊕ ROTL_{R0}(ΔB1),  ΔB2 = ΔB1 ⊕ ROTL_{R1}(ΔA2).
		// Injection model: D_Δ f(x) = Mx ⊕ c, reachable Δ are c ⊕ im(M),
		// each with probability 2^{-rank(M)} (weight = rank(M)).
		void handle_first_constant_subtraction_candidate( std::uint32_t output_branch_a_difference_after_first_constant_subtraction, int weight_first_constant_subtraction )
		{
			auto& state = current_round_state();
			state.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
			state.weight_first_constant_subtraction = weight_first_constant_subtraction;
			state.accumulated_weight_after_first_constant_subtraction = state.accumulated_weight_after_first_addition + weight_first_constant_subtraction;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_after_first_constant_subtraction ) )
				return;

			state.branch_a_difference_after_first_xor_with_rotated_branch_b =
				output_branch_a_difference_after_first_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			state.branch_b_difference_after_first_xor_with_rotated_branch_a =
				state.output_branch_b_difference_after_first_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
			state.branch_b_difference_after_linear_layer_one_backward = state.branch_b_difference_after_first_xor_with_rotated_branch_a;

			const InjectionAffineTransition injection_transition_from_branch_b = compute_injection_transition_from_branch_b( state.branch_b_difference_after_first_xor_with_rotated_branch_a );
			state.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
			state.accumulated_weight_before_second_addition = state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_before_second_addition ) )
				return;

			enumerate_affine_subspace_output_differences
			(
				search_context, injection_transition_from_branch_b, search_context.configuration.maximum_transition_output_differences,
				[ this ]( std::uint32_t injection_from_branch_b_xor_difference )
				{
					handle_injection_from_branch_b_candidate( injection_from_branch_b_xor_difference );
				}
			);
		}

		// Second addition: A4 = A3 ⊞ T1, with ΔT1 from ΔB3.
		void handle_injection_from_branch_b_candidate( std::uint32_t injection_from_branch_b_xor_difference )
		{
			auto& state = current_round_state();
			state.injection_from_branch_b_xor_difference = injection_from_branch_b_xor_difference;
			state.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b ^ injection_from_branch_b_xor_difference;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_before_second_addition ) )
				return;

			state.second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_linear_layer_one_backward, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_linear_layer_one_backward, 17 );

			const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
				find_optimal_gamma_with_weight( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, 32 );
			state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
			state.optimal_weight_second_addition = optimal_weight_second_addition;
			if ( state.optimal_weight_second_addition < 0 )
				return;
			if ( state.accumulated_weight_before_second_addition + state.optimal_weight_second_addition + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight )
				return;

			int weight_cap_second_addition = search_context.best_total_weight - state.accumulated_weight_before_second_addition - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_second_addition = std::min( weight_cap_second_addition, 31 );
			weight_cap_second_addition = std::min( weight_cap_second_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
			state.weight_cap_second_addition = weight_cap_second_addition;
			if ( weight_cap_second_addition < 0 || state.optimal_weight_second_addition > weight_cap_second_addition )
				return;

			enumerate_modular_addition_output_differences_by_bit_recursion
			(
				search_context, state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, state.optimal_output_branch_a_difference_after_second_addition, 0, 0u, weight_cap_second_addition,
				[ this ]( std::uint32_t output_branch_a_difference_after_second_addition, int weight_second_addition )
				{
					handle_second_addition_candidate( output_branch_a_difference_after_second_addition, weight_second_addition );
				}
			);
		}

		// B4 = B3 ⊟ RC6 (weighted subtraction).
		// Mathematical note: diff_subconst_bvweight is specifically designed for estimating differential weights in x ⊟ c,
		// not equivalent to x ⊞ y or x ⊟ y (two variables).
		// Engineering Explanation: When constants are involved, the achievable Δ space is smaller, but the constraints are stronger and the distribution is “denser,”
		// resulting in more concentrated weight evaluations. Therefore, a separate enumeration and weight model is required.
		void handle_second_addition_candidate( std::uint32_t output_branch_a_difference_after_second_addition, int weight_second_addition )
		{
			auto& state = current_round_state();
			state.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
			state.weight_second_addition = weight_second_addition;
			state.accumulated_weight_after_second_addition = state.accumulated_weight_before_second_addition + weight_second_addition;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_after_second_addition ) )
				return;

			const std::uint32_t round_constant_for_second_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 6 ];
			const int			weight_cap_second_constant_subtraction =
				std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
					search_context.best_total_weight - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round );
			if ( weight_cap_second_constant_subtraction < 0 )
				return;

			enumerate_subtraction_by_constant_output_differences
			(
				search_context, state.branch_b_difference_after_linear_layer_one_backward, round_constant_for_second_subtraction, weight_cap_second_constant_subtraction,
				[ this ]( std::uint32_t output_branch_b_difference_after_second_constant_subtraction, int weight_second_constant_subtraction )
				{
					handle_second_constant_subtraction_candidate( output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction );
				}
			);
		}

		// XOR/ROT mixing and injection A -> B.
		// Math: ΔB5 = ΔB4 ⊕ ROTL_{R0}(ΔA4),  ΔA5 = ΔA4 ⊕ ROTL_{R1}(ΔB5).
		void handle_second_constant_subtraction_candidate( std::uint32_t output_branch_b_difference_after_second_constant_subtraction, int weight_second_constant_subtraction )
		{
			auto& state = current_round_state();
			state.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
			state.weight_second_constant_subtraction = weight_second_constant_subtraction;
			state.accumulated_weight_after_second_constant_subtraction = state.accumulated_weight_after_second_addition + weight_second_constant_subtraction;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_after_second_constant_subtraction ) )
				return;

			state.branch_b_difference_after_second_xor_with_rotated_branch_a =
				output_branch_b_difference_after_second_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			state.branch_a_difference_after_second_xor_with_rotated_branch_b =
				state.output_branch_a_difference_after_second_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			const InjectionAffineTransition injection_transition_from_branch_a = compute_injection_transition_from_branch_a( state.branch_a_difference_after_second_xor_with_rotated_branch_b );
			state.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
			state.accumulated_weight_at_round_end = state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_at_round_end ) )
				return;

			enumerate_affine_subspace_output_differences
			(
				search_context, injection_transition_from_branch_a, search_context.configuration.maximum_transition_output_differences,
				[ this ]( std::uint32_t injection_from_branch_a_xor_difference )
				{
					handle_injection_from_branch_a_candidate( injection_from_branch_a_xor_difference );
				}
			);
		}

		// Round end: ΔA_out = L2^{-1}(ΔA5), ΔB_out = ΔB5 ⊕ ΔI_A.
		// Engineering: push current step, recurse to next round, then pop.
		void handle_injection_from_branch_a_candidate( std::uint32_t injection_from_branch_a_xor_difference )
		{
			auto& state = current_round_state();
			state.injection_from_branch_a_xor_difference = injection_from_branch_a_xor_difference;
			state.output_branch_b_difference = state.branch_b_difference_after_second_xor_with_rotated_branch_a ^ injection_from_branch_a_xor_difference;
			state.output_branch_a_difference = state.branch_a_difference_after_second_xor_with_rotated_branch_b;

			if ( should_prune_with_remaining_round_lower_bound( state.accumulated_weight_at_round_end ) )
				return;

			DifferentialTrailStepRecord step = state.base_step;
			step.output_branch_b_difference_after_first_addition = state.output_branch_b_difference_after_first_addition;
			step.weight_first_addition = state.weight_first_addition;
			step.output_branch_a_difference_after_first_constant_subtraction = state.output_branch_a_difference_after_first_constant_subtraction;
			step.weight_first_constant_subtraction = state.weight_first_constant_subtraction;
			step.branch_a_difference_after_first_xor_with_rotated_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b;
			step.branch_b_difference_after_first_xor_with_rotated_branch_a = state.branch_b_difference_after_first_xor_with_rotated_branch_a;
			step.injection_from_branch_b_xor_difference = state.injection_from_branch_b_xor_difference;
			step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
			step.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_injection_from_branch_b;
			step.branch_b_difference_after_linear_layer_one_backward = state.branch_b_difference_after_linear_layer_one_backward;
			step.second_addition_term_difference = state.second_addition_term_difference;
			step.output_branch_a_difference_after_second_addition = state.output_branch_a_difference_after_second_addition;
			step.weight_second_addition = state.weight_second_addition;
			step.output_branch_b_difference_after_second_constant_subtraction = state.output_branch_b_difference_after_second_constant_subtraction;
			step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
			step.branch_b_difference_after_second_xor_with_rotated_branch_a = state.branch_b_difference_after_second_xor_with_rotated_branch_a;
			step.branch_a_difference_after_second_xor_with_rotated_branch_b = state.branch_a_difference_after_second_xor_with_rotated_branch_b;
			step.injection_from_branch_a_xor_difference = state.injection_from_branch_a_xor_difference;
			step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
			step.output_branch_a_difference = state.output_branch_a_difference;
			step.output_branch_b_difference = state.output_branch_b_difference;
			step.round_weight =
				state.weight_first_addition + state.weight_first_constant_subtraction + state.weight_injection_from_branch_b +
				state.weight_second_addition + state.weight_second_constant_subtraction + state.weight_injection_from_branch_a;

			search_context.current_differential_trail.push_back( step );
			search_recursive( state.round_boundary_depth + 1, state.output_branch_a_difference, state.output_branch_b_difference, state.accumulated_weight_at_round_end );
			search_context.current_differential_trail.pop_back();
		}
	};

	// Resumable DFS searcher (explicit stack, same traversal order).
	class DifferentialBestTrailSearcherCursor final
	{
	public:
		DifferentialBestTrailSearcherCursor( DifferentialBestSearchContext& context_in, DifferentialSearchCursor& cursor_in )
			: search_context( context_in ), cursor( cursor_in )
		{
		}

		void search_from_start( std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference )
		{
			cursor.stack.clear();
			search_context.current_differential_trail.clear();
			DifferentialSearchFrame frame {};
			frame.stage = DifferentialSearchStage::Enter;
			frame.trail_size_at_entry = search_context.current_differential_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.branch_a_input_difference = branch_a_input_difference;
			frame.state.branch_b_input_difference = branch_b_input_difference;
			cursor.stack.push_back( frame );
			run();
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		DifferentialBestSearchContext& search_context;
		DifferentialSearchCursor&	   cursor;

		DifferentialSearchFrame& current_frame()
		{
			return cursor.stack.back();
		}

		DifferentialRoundSearchState& current_round_state()
		{
			return current_frame().state;
		}

		void pop_frame()
		{
			if ( cursor.stack.empty() )
				return;
			const std::size_t target = cursor.stack.back().trail_size_at_entry;
			if ( search_context.current_differential_trail.size() > target )
				search_context.current_differential_trail.resize( target );
			cursor.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if ( !search_context.binary_checkpoint )
				return;
			if ( search_context.binary_checkpoint->pending_best_change() || ( ( search_context.visited_node_count & search_context.progress_node_mask ) == 0 ) )
				search_context.binary_checkpoint->poll( search_context, cursor );
		}

		void run()
		{
			while ( !cursor.stack.empty() )
			{
				DifferentialSearchFrame&	  frame = current_frame();
				DifferentialRoundSearchState& state = frame.state;

				switch ( frame.stage )
				{
				case DifferentialSearchStage::Enter:
				{
					if ( should_stop_search( state.round_boundary_depth, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( handle_round_end_if_needed( state.round_boundary_depth, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( should_prune_state_memoization( state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( !prepare_round_state( state, state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}

					frame.enum_first_add.reset( state.branch_b_input_difference, state.first_addition_term_difference, state.optimal_output_branch_b_difference_after_first_addition, state.weight_cap_first_addition );
					frame.stage = DifferentialSearchStage::FirstAdd;
					break;
				}
				case DifferentialSearchStage::FirstAdd:
				{
					std::uint32_t output_branch_b_difference_after_first_addition = 0;
					int			  weight_first_addition = 0;
					if ( !frame.enum_first_add.next( search_context, output_branch_b_difference_after_first_addition, weight_first_addition ) )
					{
						if ( frame.enum_first_add.stop_due_to_limits )
							cursor.stack.clear();
						else
							pop_frame();
						break;
					}

					state.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
					state.weight_first_addition = weight_first_addition;
					state.accumulated_weight_after_first_addition = state.accumulated_weight_so_far + weight_first_addition;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_first_addition ) )
						break;

					const std::uint32_t round_constant_for_first_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 1 ];
					const int			weight_cap_first_constant_subtraction =
						std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
							search_context.best_total_weight - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round );
					if ( weight_cap_first_constant_subtraction < 0 )
						break;

					frame.enum_first_const.reset( state.branch_a_input_difference, round_constant_for_first_subtraction, weight_cap_first_constant_subtraction );
					frame.stage = DifferentialSearchStage::FirstConst;
					break;
				}
				case DifferentialSearchStage::FirstConst:
				{
					std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
					int			  weight_first_constant_subtraction = 0;
					if ( !frame.enum_first_const.next( search_context, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction ) )
					{
						if ( frame.enum_first_const.stop_due_to_limits )
							cursor.stack.clear();
						else
							frame.stage = DifferentialSearchStage::FirstAdd;
						break;
					}

					state.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
					state.weight_first_constant_subtraction = weight_first_constant_subtraction;
					state.accumulated_weight_after_first_constant_subtraction = state.accumulated_weight_after_first_addition + weight_first_constant_subtraction;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_first_constant_subtraction ) )
						break;

					state.branch_a_difference_after_first_xor_with_rotated_branch_b =
						output_branch_a_difference_after_first_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					state.branch_b_difference_after_first_xor_with_rotated_branch_a =
						state.output_branch_b_difference_after_first_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
					state.branch_b_difference_after_linear_layer_one_backward = state.branch_b_difference_after_first_xor_with_rotated_branch_a;

					const InjectionAffineTransition injection_transition_from_branch_b = compute_injection_transition_from_branch_b( state.branch_b_difference_after_first_xor_with_rotated_branch_a );
					state.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
					state.accumulated_weight_before_second_addition = state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_before_second_addition ) )
						break;

					frame.enum_inj_b.reset( injection_transition_from_branch_b, search_context.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjB;
					break;
				}
				case DifferentialSearchStage::InjB:
				{
					std::uint32_t injection_from_branch_b_xor_difference = 0;
					if ( !frame.enum_inj_b.next( search_context, injection_from_branch_b_xor_difference ) )
					{
						if ( frame.enum_inj_b.stop_due_to_limits )
							cursor.stack.clear();
						else
							frame.stage = DifferentialSearchStage::FirstConst;
						break;
					}

					state.injection_from_branch_b_xor_difference = injection_from_branch_b_xor_difference;
					state.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b ^ injection_from_branch_b_xor_difference;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_before_second_addition ) )
						break;

					state.second_addition_term_difference =
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_linear_layer_one_backward, 31 ) ^
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_linear_layer_one_backward, 17 );

					const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
						find_optimal_gamma_with_weight( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, 32 );
					state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
					state.optimal_weight_second_addition = optimal_weight_second_addition;
					if ( state.optimal_weight_second_addition < 0 )
						break;
					if ( state.accumulated_weight_before_second_addition + state.optimal_weight_second_addition + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight )
						break;

					int weight_cap_second_addition = search_context.best_total_weight - state.accumulated_weight_before_second_addition - state.remaining_round_weight_lower_bound_after_this_round;
					weight_cap_second_addition = std::min( weight_cap_second_addition, 31 );
					weight_cap_second_addition = std::min( weight_cap_second_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
					state.weight_cap_second_addition = weight_cap_second_addition;
					if ( weight_cap_second_addition < 0 || state.optimal_weight_second_addition > weight_cap_second_addition )
						break;

					frame.enum_second_add.reset( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, state.optimal_output_branch_a_difference_after_second_addition, state.weight_cap_second_addition );
					frame.stage = DifferentialSearchStage::SecondAdd;
					break;
				}
				case DifferentialSearchStage::SecondAdd:
				{
					std::uint32_t output_branch_a_difference_after_second_addition = 0;
					int			  weight_second_addition = 0;
					if ( !frame.enum_second_add.next( search_context, output_branch_a_difference_after_second_addition, weight_second_addition ) )
					{
						if ( frame.enum_second_add.stop_due_to_limits )
							cursor.stack.clear();
						else
							frame.stage = DifferentialSearchStage::InjB;
						break;
					}

					state.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
					state.weight_second_addition = weight_second_addition;
					state.accumulated_weight_after_second_addition = state.accumulated_weight_before_second_addition + weight_second_addition;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_second_addition ) )
						break;

					const std::uint32_t round_constant_for_second_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 6 ];
					const int			weight_cap_second_constant_subtraction =
						std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
							search_context.best_total_weight - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round );
					if ( weight_cap_second_constant_subtraction < 0 )
						break;

					frame.enum_second_const.reset( state.branch_b_difference_after_linear_layer_one_backward, round_constant_for_second_subtraction, weight_cap_second_constant_subtraction );
					frame.stage = DifferentialSearchStage::SecondConst;
					break;
				}
				case DifferentialSearchStage::SecondConst:
				{
					std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
					int			  weight_second_constant_subtraction = 0;
					if ( !frame.enum_second_const.next( search_context, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction ) )
					{
						if ( frame.enum_second_const.stop_due_to_limits )
							cursor.stack.clear();
						else
							frame.stage = DifferentialSearchStage::SecondAdd;
						break;
					}

					state.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
					state.weight_second_constant_subtraction = weight_second_constant_subtraction;
					state.accumulated_weight_after_second_constant_subtraction = state.accumulated_weight_after_second_addition + weight_second_constant_subtraction;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_second_constant_subtraction ) )
						break;

					state.branch_b_difference_after_second_xor_with_rotated_branch_a =
						output_branch_b_difference_after_second_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					state.branch_a_difference_after_second_xor_with_rotated_branch_b =
						state.output_branch_a_difference_after_second_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

					const InjectionAffineTransition injection_transition_from_branch_a = compute_injection_transition_from_branch_a( state.branch_a_difference_after_second_xor_with_rotated_branch_b );
					state.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
					state.accumulated_weight_at_round_end = state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_at_round_end ) )
						break;

					frame.enum_inj_a.reset( injection_transition_from_branch_a, search_context.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjA;
					break;
				}
				case DifferentialSearchStage::InjA:
				{
					std::uint32_t injection_from_branch_a_xor_difference = 0;
					if ( !frame.enum_inj_a.next( search_context, injection_from_branch_a_xor_difference ) )
					{
						if ( frame.enum_inj_a.stop_due_to_limits )
							cursor.stack.clear();
						else
							frame.stage = DifferentialSearchStage::SecondConst;
						break;
					}

					state.injection_from_branch_a_xor_difference = injection_from_branch_a_xor_difference;
					state.output_branch_b_difference = state.branch_b_difference_after_second_xor_with_rotated_branch_a ^ injection_from_branch_a_xor_difference;
					state.output_branch_a_difference = state.branch_a_difference_after_second_xor_with_rotated_branch_b;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_at_round_end ) )
						break;

					DifferentialTrailStepRecord step = state.base_step;
					step.output_branch_b_difference_after_first_addition = state.output_branch_b_difference_after_first_addition;
					step.weight_first_addition = state.weight_first_addition;
					step.output_branch_a_difference_after_first_constant_subtraction = state.output_branch_a_difference_after_first_constant_subtraction;
					step.weight_first_constant_subtraction = state.weight_first_constant_subtraction;
					step.branch_a_difference_after_first_xor_with_rotated_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b;
					step.branch_b_difference_after_first_xor_with_rotated_branch_a = state.branch_b_difference_after_first_xor_with_rotated_branch_a;
					step.injection_from_branch_b_xor_difference = state.injection_from_branch_b_xor_difference;
					step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
					step.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_injection_from_branch_b;
					step.branch_b_difference_after_linear_layer_one_backward = state.branch_b_difference_after_linear_layer_one_backward;
					step.second_addition_term_difference = state.second_addition_term_difference;
					step.output_branch_a_difference_after_second_addition = state.output_branch_a_difference_after_second_addition;
					step.weight_second_addition = state.weight_second_addition;
					step.output_branch_b_difference_after_second_constant_subtraction = state.output_branch_b_difference_after_second_constant_subtraction;
					step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
					step.branch_b_difference_after_second_xor_with_rotated_branch_a = state.branch_b_difference_after_second_xor_with_rotated_branch_a;
					step.branch_a_difference_after_second_xor_with_rotated_branch_b = state.branch_a_difference_after_second_xor_with_rotated_branch_b;
					step.injection_from_branch_a_xor_difference = state.injection_from_branch_a_xor_difference;
					step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
					step.output_branch_a_difference = state.output_branch_a_difference;
					step.output_branch_b_difference = state.output_branch_b_difference;
					step.round_weight =
						state.weight_first_addition + state.weight_first_constant_subtraction + state.weight_injection_from_branch_b +
						state.weight_second_addition + state.weight_second_constant_subtraction + state.weight_injection_from_branch_a;

					search_context.current_differential_trail.push_back( step );

					DifferentialSearchFrame child {};
					child.stage = DifferentialSearchStage::Enter;
					// Store the parent trail size (exclude the step we just pushed) so pop_frame() removes it.
					child.trail_size_at_entry = search_context.current_differential_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
					child.state.branch_a_input_difference = state.output_branch_a_difference;
					child.state.branch_b_input_difference = state.output_branch_b_difference;
					cursor.stack.push_back( child );
					break;
				}
				}

				maybe_poll_checkpoint();
			}
		}

		// Stop conditions and global pruning (budget/time/best bound).
		bool should_stop_search( int round_boundary_depth, int accumulated_weight_so_far )
		{
			// Early stop: reached target probability (weight) already.
			if ( search_context.configuration.target_best_weight >= 0 && search_context.best_total_weight <= search_context.configuration.target_best_weight )
				return true;

			if ( search_context.stop_due_to_time_limit )
				return true;

			// Count visited nodes for progress reporting even when maximum_search_nodes is unlimited (0).
			++search_context.visited_node_count;

			// Governor hook: very low overhead (one clock read every ~262k nodes).
			if ( ( search_context.visited_node_count & search_context.progress_node_mask ) == 0 )
			{
				memory_governor_poll_if_needed( std::chrono::steady_clock::now() );
			}

			// Time limit check (low overhead): evaluate clock only every ~262k nodes.
			if ( search_context.configuration.maximum_search_seconds != 0 )
			{
				if ( ( search_context.visited_node_count & search_context.progress_node_mask ) == 0 )
				{
					const auto now = std::chrono::steady_clock::now();
					memory_governor_poll_if_needed( now );
					const double elapsed = std::chrono::duration<double>( now - search_context.time_limit_start_time ).count();
					if ( elapsed >= double( search_context.configuration.maximum_search_seconds ) )
					{
						search_context.stop_due_to_time_limit = true;
						return true;
					}
				}
			}

			// Node budget check (0 = unlimited). We use '>' because visited_node_count was
			// already incremented above.
			if ( search_context.configuration.maximum_search_nodes != 0 && search_context.visited_node_count > search_context.configuration.maximum_search_nodes )
				return true;

			maybe_print_single_run_progress( search_context, round_boundary_depth );
			if ( accumulated_weight_so_far > search_context.best_total_weight )
				return true;

			if ( should_prune_remaining_round_lower_bound( round_boundary_depth, accumulated_weight_so_far ) )
				return true;

			return false;
		}

		bool should_prune_remaining_round_lower_bound( int round_boundary_depth, int accumulated_weight_so_far ) const
		{
			if ( search_context.best_total_weight < INFINITE_WEIGHT && search_context.configuration.enable_remaining_round_lower_bound )
			{
				const int rounds_left = search_context.configuration.round_count - round_boundary_depth;
				if ( rounds_left >= 0 )
				{
					const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
					const std::size_t table_index = std::size_t( rounds_left );
					if ( table_index < remaining_round_min_weight_table.size() )
					{
						const int weight_lower_bound = remaining_round_min_weight_table[ table_index ];
						if ( accumulated_weight_so_far + weight_lower_bound >= search_context.best_total_weight )
							return true;
					}
				}
			}
			return false;
		}

		bool handle_round_end_if_needed( int round_boundary_depth, int accumulated_weight_so_far )
		{
			if ( round_boundary_depth != search_context.configuration.round_count )
				return false;

			if ( accumulated_weight_so_far < search_context.best_total_weight || search_context.best_differential_trail.empty() )
			{
				const int old = search_context.best_total_weight;
				search_context.best_total_weight = accumulated_weight_so_far;
				search_context.best_differential_trail = search_context.current_differential_trail;
				if ( search_context.checkpoint && accumulated_weight_so_far != old )
					search_context.checkpoint->maybe_write( search_context, "improved" );
				if ( search_context.binary_checkpoint && accumulated_weight_so_far != old )
					search_context.binary_checkpoint->mark_best_changed();
			}
			return true;
		}

		bool should_prune_state_memoization( int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			if ( !search_context.configuration.enable_state_memoization )
				return false;

			const std::size_t rc = std::size_t( std::max( 1, search_context.configuration.round_count ) );
			std::size_t		  hint = ( rc == 0 ) ? 0 : ( search_context.configuration.maximum_search_nodes / rc );
			hint = std::clamp<std::size_t>( hint, 4096u, 1'000'000u );

			const std::uint64_t key = pack_two_word32_differences( branch_a_input_difference, branch_b_input_difference );
			return search_context.memoization.should_prune_and_update( std::size_t( round_boundary_depth ), key, accumulated_weight_so_far, true, true, hint, 192ull, "memoization.reserve", "memoization.try_emplace" );
		}

		int compute_remaining_round_weight_lower_bound_after_this_round( int round_boundary_depth ) const
		{
			if ( !search_context.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left_after = search_context.configuration.round_count - ( round_boundary_depth + 1 );
			if ( rounds_left_after < 0 )
				return 0;
			const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t( rounds_left_after );
			if ( idx >= remaining_round_min_weight_table.size() )
				return 0;
			return std::max( 0, remaining_round_min_weight_table[ idx ] );
		}

		bool should_prune_with_remaining_round_lower_bound( const DifferentialRoundSearchState& state, int accumulated_weight ) const
		{
			return accumulated_weight + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight;
		}

		bool prepare_round_state( DifferentialRoundSearchState& state, int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			state.round_boundary_depth = round_boundary_depth;
			state.accumulated_weight_so_far = accumulated_weight_so_far;
			state.branch_a_input_difference = branch_a_input_difference;
			state.branch_b_input_difference = branch_b_input_difference;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( round_boundary_depth );

			state.base_step = DifferentialTrailStepRecord {};
			state.base_step.round_index = round_boundary_depth + 1;
			state.base_step.input_branch_a_difference = branch_a_input_difference;
			state.base_step.input_branch_b_difference = branch_b_input_difference;

			state.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			state.base_step.first_addition_term_difference = state.first_addition_term_difference;

			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, state.first_addition_term_difference, 32 );
			state.optimal_output_branch_b_difference_after_first_addition = optimal_output_branch_b_difference_after_first_addition;
			state.optimal_weight_first_addition = optimal_weight_first_addition;
			if ( state.optimal_weight_first_addition < 0 )
				return false;

			int weight_cap_first_addition = search_context.best_total_weight - accumulated_weight_so_far - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_first_addition = std::min( weight_cap_first_addition, 31 );
			weight_cap_first_addition = std::min( weight_cap_first_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
			state.weight_cap_first_addition = weight_cap_first_addition;
			if ( weight_cap_first_addition < 0 || state.optimal_weight_first_addition > weight_cap_first_addition )
				return false;

			return true;
		}
	};

	static MatsuiSearchRunDifferentialResult run_matsui_best_search_with_injection_internal( int round_count, std::uint32_t initial_branch_a_difference, std::uint32_t initial_branch_b_difference, const DifferentialBestSearchConfiguration& input_search_configuration, bool print_output, std::uint64_t single_run_progress_every_seconds, bool progress_print_differences = false, int seeded_upper_bound_weight = INFINITE_WEIGHT, const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail = nullptr, BestWeightHistory* checkpoint = nullptr, BinaryCheckpointManager* binary_checkpoint = nullptr )
	{
		MatsuiSearchRunDifferentialResult result {};
		DifferentialBestSearchContext				  search_context {};
		search_context.configuration = input_search_configuration;
		search_context.configuration.round_count = round_count;
		search_context.configuration.addition_weight_cap = std::clamp( search_context.configuration.addition_weight_cap, 0, 31 );
		search_context.configuration.constant_subtraction_weight_cap = std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 );
		search_context.start_difference_a = initial_branch_a_difference;
		search_context.start_difference_b = initial_branch_b_difference;
		search_context.run_start_time = std::chrono::steady_clock::now();
		search_context.checkpoint = checkpoint;
		search_context.binary_checkpoint = binary_checkpoint;
		search_context.memoization.initialize( ( round_count > 0 ) ? std::size_t( round_count ) : 0u, search_context.configuration.enable_state_memoization, "memoization.init" );

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		if ( search_context.configuration.enable_remaining_round_lower_bound )
		{
			auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			if ( remaining_round_min_weight_table.empty() )
			{
				remaining_round_min_weight_table.assign( std::size_t( std::max( 0, round_count ) ) + 1u, 0 );
			}
			else
			{
				// Ensure remaining_round_min_weight_table[0] exists and is 0.
				if ( remaining_round_min_weight_table.size() < 1u )
					remaining_round_min_weight_table.resize( 1u, 0 );
				remaining_round_min_weight_table[ 0 ] = 0;
				// Pad to round_count+1 with 0 (safe lower bound).
				const std::size_t need = std::size_t( std::max( 0, round_count ) ) + 1u;
				if ( remaining_round_min_weight_table.size() < need )
					remaining_round_min_weight_table.resize( need, 0 );
				for ( int& round_min_weight : remaining_round_min_weight_table )
				{
					if ( round_min_weight < 0 )
						round_min_weight = 0;
				}
			}
		}

		// initial upper bound (greedy)
		search_context.best_total_weight = compute_greedy_upper_bound_weight( search_context.configuration, initial_branch_a_difference, initial_branch_b_difference );
		if ( search_context.best_total_weight >= INFINITE_WEIGHT )
			search_context.best_total_weight = INFINITE_WEIGHT;

		// Seed best_trail with an explicit greedy construction to avoid false [FAIL] when DFS hits max_nodes early.
		{
			int	 initial_weight = INFINITE_WEIGHT;
			auto gtrail = construct_greedy_initial_differential_trail( search_context.configuration, initial_branch_a_difference, initial_branch_b_difference, initial_weight );
			if ( !gtrail.empty() && initial_weight < INFINITE_WEIGHT )
			{
				search_context.best_total_weight = initial_weight;
				search_context.best_differential_trail = std::move( gtrail );
			}
		}

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < search_context.best_total_weight )
		{
			search_context.best_total_weight = seeded_upper_bound_weight;
			if ( seeded_upper_bound_trail && !seeded_upper_bound_trail->empty() )
			{
				search_context.best_differential_trail = *seeded_upper_bound_trail;
			}
		}

		// Persistence (auto mode): record the initial best (greedy/seeded) once.
		if ( search_context.checkpoint )
		{
			search_context.checkpoint->maybe_write( search_context, "init" );
		}

		if ( print_output )
		{
			std::cout << "[BestSearch] mode=matsui(injection-affine)\n";
			std::cout << "  rounds=" << round_count << "  addition_weight_cap=" << search_context.configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_context.configuration.constant_subtraction_weight_cap << "  maximum_transition_output_differences=" << search_context.configuration.maximum_transition_output_differences << "  maximum_search_nodes=" << search_context.configuration.maximum_search_nodes << "  maximum_search_seconds=" << search_context.configuration.maximum_search_seconds << "  memo=" << ( search_context.configuration.enable_state_memoization ? "on" : "off" ) << "\n";
			std::cout << "  weight_shell_cache=" << ( search_context.configuration.enable_wpddt_cache ? "on" : "off" )
					  << "  weight_shell_max_weight=" << search_context.configuration.wpddt_max_cached_weight << "\n";
			std::cout << "  greedy_upper_bound_weight=" << ( search_context.best_total_weight >= INFINITE_WEIGHT ? -1 : search_context.best_total_weight ) << "\n";
			if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < INFINITE_WEIGHT )
			{
				std::cout << "  seeded_upper_bound_weight=" << seeded_upper_bound_weight << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if ( print_output && single_run_progress_every_seconds != 0 )
		{
			search_context.progress_every_seconds = single_run_progress_every_seconds;
			search_context.progress_start_time = search_context.run_start_time;
			search_context.progress_last_print_time = {};
			search_context.progress_last_print_nodes = 0;
			search_context.progress_print_differences = progress_print_differences;
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << search_context.progress_every_seconds << " seconds (time-check granularity ~" << ( search_context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		// Initialize time limit start time (even if progress is disabled).
		if ( search_context.configuration.maximum_search_seconds != 0 )
		{
			search_context.time_limit_start_time = search_context.run_start_time;
		}

		DifferentialSearchCursor cursor {};
		DifferentialBestTrailSearcherCursor searcher( search_context, cursor );
		searcher.search_from_start( initial_branch_a_difference, initial_branch_b_difference );

		result.nodes_visited = search_context.visited_node_count;
		result.hit_maximum_search_nodes = ( search_context.configuration.maximum_search_nodes != 0 ) && ( search_context.visited_node_count >= search_context.configuration.maximum_search_nodes );
		result.hit_time_limit = ( search_context.configuration.maximum_search_seconds != 0 ) && search_context.stop_due_to_time_limit;

		if ( search_context.best_differential_trail.empty() )
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			if ( print_output )
				std::cout << "[FAIL] No trail found within limits.\n";
			return result;
		}

		result.found = true;
		result.best_weight = search_context.best_total_weight;
		result.best_trail = std::move( search_context.best_differential_trail );

		if ( print_output )
		{
			std::cout << "[OK] best_weight=" << result.best_weight << "  (DP ~= 2^-" << result.best_weight << ")\n";
			std::cout << "  approx_DP=" << std::setprecision( 10 ) << weight_to_probability( result.best_weight ) << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited << ( result.hit_maximum_search_nodes ? "  [HIT maximum_search_nodes]" : "" );
			if ( result.hit_time_limit )
			{
				std::cout << "  [HIT maximum_search_seconds=" << search_context.configuration.maximum_search_seconds << "]";
			}
			if ( search_context.configuration.target_best_weight >= 0 && result.best_weight <= search_context.configuration.target_best_weight )
			{
				std::cout << "  [HIT target_best_weight=" << search_context.configuration.target_best_weight << "]";
			}
			std::cout << "\n\n";

			for ( const auto& s : result.best_trail )
			{
				std::cout << "R" << s.round_index << "  round_weight=" << s.round_weight << "  weight_first_addition=" << s.weight_first_addition << "  weight_first_constant_subtraction=" << s.weight_first_constant_subtraction << "  weight_injection_from_branch_b=" << s.weight_injection_from_branch_b << "  weight_second_addition=" << s.weight_second_addition << "  weight_second_constant_subtraction=" << s.weight_second_constant_subtraction << "  weight_injection_from_branch_a=" << s.weight_injection_from_branch_a << "\n";
				print_word32_hex( "  input_branch_a_difference=", s.input_branch_a_difference );
				std::cout << "  ";
				print_word32_hex( "input_branch_b_difference=", s.input_branch_b_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_b_difference_after_first_addition=", s.output_branch_b_difference_after_first_addition );
				std::cout << "  ";
				print_word32_hex( "first_addition_term_difference=", s.first_addition_term_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_a_difference_after_first_constant_subtraction=", s.output_branch_a_difference_after_first_constant_subtraction );
				std::cout << "  ";
				print_word32_hex( "branch_a_difference_after_first_xor_with_rotated_branch_b=", s.branch_a_difference_after_first_xor_with_rotated_branch_b );
				std::cout << "\n";

				print_word32_hex( "  injection_from_branch_b_xor_difference=", s.injection_from_branch_b_xor_difference );
				std::cout << "  ";
				print_word32_hex( "branch_a_difference_after_injection_from_branch_b=", s.branch_a_difference_after_injection_from_branch_b );
				std::cout << "\n";

				print_word32_hex( "  branch_b_difference_after_linear_layer_one_backward=", s.branch_b_difference_after_linear_layer_one_backward );
				std::cout << "  ";
				print_word32_hex( "second_addition_term_difference=", s.second_addition_term_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_b_difference_after_second_constant_subtraction=", s.output_branch_b_difference_after_second_constant_subtraction );
				std::cout << "  ";
				print_word32_hex( "branch_b_difference_after_second_xor_with_rotated_branch_a=", s.branch_b_difference_after_second_xor_with_rotated_branch_a );
				std::cout << "\n";

				print_word32_hex( "  injection_from_branch_a_xor_difference=", s.injection_from_branch_a_xor_difference );
				std::cout << "  ";
				print_word32_hex( "output_branch_b_difference=", s.output_branch_b_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_a_difference=", s.output_branch_a_difference );
				std::cout << "  ";
				print_word32_hex( "output_branch_b_difference=", s.output_branch_b_difference );
				std::cout << "\n";
			}
		}
		return result;
	}

	// ============================================================================
	// Binary checkpoint serialization (differential)
	// ============================================================================

	static inline std::string default_binary_checkpoint_path( int round_count, std::uint32_t da, std::uint32_t db )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << da << "_DiffB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << db << std::dec << ".ckpt";
		return oss.str();
	}

	static inline void write_config( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialBestSearchConfiguration& c )
	{
		w.write_i32( c.round_count );
		w.write_i32( c.addition_weight_cap );
		w.write_i32( c.constant_subtraction_weight_cap );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_transition_output_differences ) );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_search_nodes ) );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_search_seconds ) );
		w.write_u8( c.enable_state_memoization ? 1u : 0u );
		w.write_u8( c.enable_remaining_round_lower_bound ? 1u : 0u );
		w.write_i32( c.target_best_weight );
		w.write_u64( static_cast<std::uint64_t>( c.remaining_round_min_weight.size() ) );
		for ( const int v : c.remaining_round_min_weight )
			w.write_i32( v );
		// Extension block (required).
		static constexpr std::uint32_t kDiffConfigExtTag = 0x44434658u; // 'XCFD'
		w.write_u32( kDiffConfigExtTag );
		w.write_u8( c.enable_wpddt_cache ? 1u : 0u );
		w.write_i32( c.wpddt_max_cached_weight );
	}

	static inline bool read_config( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialBestSearchConfiguration& c )
	{
		std::int32_t round_count = 0;
		std::int32_t add_cap = 0;
		std::int32_t sub_cap = 0;
		std::uint64_t max_trans = 0;
		std::uint64_t max_nodes = 0;
		std::uint64_t max_seconds = 0;
		std::uint8_t memo = 0;
		std::uint8_t rem = 0;
		std::int32_t target = 0;
		std::uint64_t rem_size = 0;
		if ( !r.read_i32( round_count ) ) return false;
		if ( !r.read_i32( add_cap ) ) return false;
		if ( !r.read_i32( sub_cap ) ) return false;
		if ( !r.read_u64( max_trans ) ) return false;
		if ( !r.read_u64( max_nodes ) ) return false;
		if ( !r.read_u64( max_seconds ) ) return false;
		if ( !r.read_u8( memo ) ) return false;
		if ( !r.read_u8( rem ) ) return false;
		if ( !r.read_i32( target ) ) return false;
		if ( !r.read_u64( rem_size ) ) return false;

		c.round_count = round_count;
		c.addition_weight_cap = add_cap;
		c.constant_subtraction_weight_cap = sub_cap;
		c.maximum_transition_output_differences = static_cast<std::size_t>( max_trans );
		c.maximum_search_nodes = static_cast<std::size_t>( max_nodes );
		c.maximum_search_seconds = static_cast<std::uint64_t>( max_seconds );
		c.enable_state_memoization = ( memo != 0 );
		c.enable_remaining_round_lower_bound = ( rem != 0 );
		c.target_best_weight = target;
		c.remaining_round_min_weight.clear();
		c.remaining_round_min_weight.resize( static_cast<std::size_t>( rem_size ) );
		for ( std::size_t i = 0; i < c.remaining_round_min_weight.size(); ++i )
		{
			std::int32_t v = 0;
			if ( !r.read_i32( v ) ) return false;
			c.remaining_round_min_weight[ i ] = v;
		}
		// Extension block (required).
		{
			std::uint32_t tag = 0;
			if ( !r.read_u32( tag ) ) return false;
			if ( tag != 0x44434658u ) return false;
			std::uint8_t wpddt = 0;
			std::int32_t wpddt_w = -1;
			if ( !r.read_u8( wpddt ) ) return false;
			if ( !r.read_i32( wpddt_w ) ) return false;
			c.enable_wpddt_cache = ( wpddt != 0 );
			c.wpddt_max_cached_weight = wpddt_w;
		}
		return true;
	}

	static inline void write_trail_step( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialTrailStepRecord& s )
	{
		w.write_i32( s.round_index );
		w.write_u32( s.input_branch_a_difference );
		w.write_u32( s.input_branch_b_difference );
		w.write_u32( s.first_addition_term_difference );
		w.write_u32( s.output_branch_b_difference_after_first_addition );
		w.write_i32( s.weight_first_addition );
		w.write_u32( s.output_branch_a_difference_after_first_constant_subtraction );
		w.write_i32( s.weight_first_constant_subtraction );
		w.write_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b );
		w.write_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a );
		w.write_u32( s.injection_from_branch_b_xor_difference );
		w.write_i32( s.weight_injection_from_branch_b );
		w.write_u32( s.branch_a_difference_after_injection_from_branch_b );
		w.write_u32( s.branch_b_difference_after_linear_layer_one_backward );
		w.write_u32( s.second_addition_term_difference );
		w.write_u32( s.output_branch_a_difference_after_second_addition );
		w.write_i32( s.weight_second_addition );
		w.write_u32( s.output_branch_b_difference_after_second_constant_subtraction );
		w.write_i32( s.weight_second_constant_subtraction );
		w.write_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a );
		w.write_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b );
		w.write_u32( s.injection_from_branch_a_xor_difference );
		w.write_i32( s.weight_injection_from_branch_a );
		w.write_u32( s.output_branch_a_difference );
		w.write_u32( s.output_branch_b_difference );
		w.write_i32( s.round_weight );
	}

	static inline bool read_trail_step( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialTrailStepRecord& s )
	{
		if ( !r.read_i32( s.round_index ) ) return false;
		if ( !r.read_u32( s.input_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.input_branch_b_difference ) ) return false;
		if ( !r.read_u32( s.first_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_i32( s.weight_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_first_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_b_xor_difference ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_linear_layer_one_backward ) ) return false;
		if ( !r.read_u32( s.second_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_i32( s.weight_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_second_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_a_xor_difference ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference ) ) return false;
		if ( !r.read_i32( s.round_weight ) ) return false;
		return true;
	}

	static inline void write_round_state( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialRoundSearchState& s )
	{
		w.write_i32( s.round_boundary_depth );
		w.write_i32( s.accumulated_weight_so_far );
		w.write_i32( s.remaining_round_weight_lower_bound_after_this_round );
		w.write_u32( s.branch_a_input_difference );
		w.write_u32( s.branch_b_input_difference );
		write_trail_step( w, s.base_step );
		w.write_u32( s.first_addition_term_difference );
		w.write_u32( s.optimal_output_branch_b_difference_after_first_addition );
		w.write_i32( s.optimal_weight_first_addition );
		w.write_i32( s.weight_cap_first_addition );
		w.write_u32( s.output_branch_b_difference_after_first_addition );
		w.write_i32( s.weight_first_addition );
		w.write_i32( s.accumulated_weight_after_first_addition );
		w.write_u32( s.output_branch_a_difference_after_first_constant_subtraction );
		w.write_i32( s.weight_first_constant_subtraction );
		w.write_i32( s.accumulated_weight_after_first_constant_subtraction );
		w.write_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b );
		w.write_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a );
		w.write_u32( s.branch_b_difference_after_linear_layer_one_backward );
		w.write_i32( s.weight_injection_from_branch_b );
		w.write_i32( s.accumulated_weight_before_second_addition );
		w.write_u32( s.injection_from_branch_b_xor_difference );
		w.write_u32( s.branch_a_difference_after_injection_from_branch_b );
		w.write_u32( s.second_addition_term_difference );
		w.write_u32( s.optimal_output_branch_a_difference_after_second_addition );
		w.write_i32( s.optimal_weight_second_addition );
		w.write_i32( s.weight_cap_second_addition );
		w.write_u32( s.output_branch_a_difference_after_second_addition );
		w.write_i32( s.weight_second_addition );
		w.write_i32( s.accumulated_weight_after_second_addition );
		w.write_u32( s.output_branch_b_difference_after_second_constant_subtraction );
		w.write_i32( s.weight_second_constant_subtraction );
		w.write_i32( s.accumulated_weight_after_second_constant_subtraction );
		w.write_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a );
		w.write_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b );
		w.write_i32( s.weight_injection_from_branch_a );
		w.write_i32( s.accumulated_weight_at_round_end );
		w.write_u32( s.injection_from_branch_a_xor_difference );
		w.write_u32( s.output_branch_a_difference );
		w.write_u32( s.output_branch_b_difference );
	}

	static inline bool read_round_state( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialRoundSearchState& s )
	{
		if ( !r.read_i32( s.round_boundary_depth ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_so_far ) ) return false;
		if ( !r.read_i32( s.remaining_round_weight_lower_bound_after_this_round ) ) return false;
		if ( !r.read_u32( s.branch_a_input_difference ) ) return false;
		if ( !r.read_u32( s.branch_b_input_difference ) ) return false;
		if ( !read_trail_step( r, s.base_step ) ) return false;
		if ( !r.read_u32( s.first_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.optimal_output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_i32( s.optimal_weight_first_addition ) ) return false;
		if ( !r.read_i32( s.weight_cap_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_i32( s.weight_first_addition ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_after_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_first_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_first_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_linear_layer_one_backward ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_before_second_addition ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_b_xor_difference ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.second_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.optimal_output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_i32( s.optimal_weight_second_addition ) ) return false;
		if ( !r.read_i32( s.weight_cap_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_i32( s.weight_second_addition ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_after_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_second_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_after_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_at_round_end ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_a_xor_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference ) ) return false;
		return true;
	}

	static inline void write_injection_transition( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const InjectionAffineTransition& t )
	{
		w.write_u32( t.offset );
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
			w.write_u32( t.basis_vectors[ i ] );
		w.write_i32( t.rank_weight );
	}

	static inline bool read_injection_transition( TwilightDream::auto_search_checkpoint::BinaryReader& r, InjectionAffineTransition& t )
	{
		if ( !r.read_u32( t.offset ) ) return false;
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
		{
			if ( !r.read_u32( t.basis_vectors[ i ] ) ) return false;
		}
		if ( !r.read_i32( t.rank_weight ) ) return false;
		return true;
	}

	static inline void write_mod_add_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const ModularAdditionEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		w.write_u32( e.alpha );
		w.write_u32( e.beta );
		w.write_u32( e.output_hint );
		w.write_i32( e.weight_cap );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.bit_position );
			w.write_u32( f.prefix );
			w.write_u32( f.prefer );
			w.write_u8( f.state );
		}
		// Extension block (required).
		static constexpr std::uint32_t kModAddEnumExtTag = 0x584D4144u; // 'DAMX'
		w.write_u32( kModAddEnumExtTag );
		w.write_u8( e.dfs_active ? 1u : 0u );
		w.write_u8( e.using_cached_shell ? 1u : 0u );
		w.write_i32( e.target_weight );
		w.write_i32( e.word_bits );
		w.write_u64( static_cast<std::uint64_t>( e.shell_index ) );
	}

	static inline bool read_mod_add_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, ModularAdditionEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !r.read_u32( e.alpha ) ) return false;
		if ( !r.read_u32( e.beta ) ) return false;
		if ( !r.read_u32( e.output_hint ) ) return false;
		if ( !r.read_i32( e.weight_cap ) ) return false;
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.bit_position ) ) return false;
			if ( !r.read_u32( f.prefix ) ) return false;
			if ( !r.read_u32( f.prefer ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		// Extension block (required).
		{
			std::uint32_t tag = 0;
			if ( !r.read_u32( tag ) ) return false;
			if ( tag != 0x584D4144u ) return false;
			std::uint8_t dfs = 0;
			std::uint8_t cached = 0;
			std::int32_t target = 0;
			std::int32_t bits = 32;
			std::uint64_t index = 0;
			if ( !r.read_u8( dfs ) ) return false;
			if ( !r.read_u8( cached ) ) return false;
			if ( !r.read_i32( target ) ) return false;
			if ( target < 0 )
				return false;
			if ( !r.read_i32( bits ) ) return false;
			if ( !r.read_u64( index ) ) return false;
			e.dfs_active = ( dfs != 0u );
			e.using_cached_shell = ( cached != 0u );
			e.target_weight = target;
			e.word_bits = std::clamp( bits, 1, 32 );
			e.shell_index = static_cast<std::size_t>( index );
		}
		e.shell_cache.clear();
		return true;
	}

	static inline void write_subconst_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const SubConstEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		w.write_u32( e.input_difference );
		w.write_u32( e.subtractive_constant );
		w.write_u32( e.additive_constant );
		w.write_u32( e.output_hint );
		w.write_i32( e.cap_bitvector );
		w.write_i32( e.cap_dynamic_planning );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.bit_position );
			w.write_u32( f.prefix );
			for ( std::uint64_t v : f.prefix_counts )
				w.write_u64( v );
			w.write_u32( f.preferred_bit );
			w.write_u8( f.state );
		}
	}

	static inline bool read_subconst_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, SubConstEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !r.read_u32( e.input_difference ) ) return false;
		if ( !r.read_u32( e.subtractive_constant ) ) return false;
		if ( !r.read_u32( e.additive_constant ) ) return false;
		if ( !r.read_u32( e.output_hint ) ) return false;
		if ( !r.read_i32( e.cap_bitvector ) ) return false;
		if ( !r.read_i32( e.cap_dynamic_planning ) ) return false;
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.bit_position ) ) return false;
			if ( !r.read_u32( f.prefix ) ) return false;
			for ( std::uint64_t& v : f.prefix_counts )
			{
				if ( !r.read_u64( v ) ) return false;
			}
			if ( !r.read_u32( f.preferred_bit ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		return true;
	}

	static inline void write_affine_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const AffineSubspaceEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		write_injection_transition( w, e.transition );
		w.write_u64( static_cast<std::uint64_t>( e.maximum_output_difference_count ) );
		w.write_u64( static_cast<std::uint64_t>( e.produced_output_difference_count ) );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.basis_index );
			w.write_u32( f.current_difference );
			w.write_u8( f.state );
		}
	}

	static inline bool read_affine_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, AffineSubspaceEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !read_injection_transition( r, e.transition ) ) return false;
		std::uint64_t max_out = 0;
		std::uint64_t prod = 0;
		if ( !r.read_u64( max_out ) ) return false;
		if ( !r.read_u64( prod ) ) return false;
		e.maximum_output_difference_count = static_cast<std::size_t>( max_out );
		e.produced_output_difference_count = static_cast<std::size_t>( prod );
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.basis_index ) ) return false;
			if ( !r.read_u32( f.current_difference ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		return true;
	}

	static inline void write_search_frame( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialSearchFrame& f )
	{
		w.write_u8( static_cast<std::uint8_t>( f.stage ) );
		w.write_u64( static_cast<std::uint64_t>( f.trail_size_at_entry ) );
		write_round_state( w, f.state );
		write_mod_add_enum( w, f.enum_first_add );
		write_subconst_enum( w, f.enum_first_const );
		write_affine_enum( w, f.enum_inj_b );
		write_mod_add_enum( w, f.enum_second_add );
		write_subconst_enum( w, f.enum_second_const );
		write_affine_enum( w, f.enum_inj_a );
	}

	static inline bool read_search_frame( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialSearchFrame& f )
	{
		std::uint8_t stage = 0;
		std::uint64_t trail_size = 0;
		if ( !r.read_u8( stage ) ) return false;
		if ( !r.read_u64( trail_size ) ) return false;
		f.stage = static_cast<DifferentialSearchStage>( stage );
		f.trail_size_at_entry = static_cast<std::size_t>( trail_size );
		if ( !read_round_state( r, f.state ) ) return false;
		if ( !read_mod_add_enum( r, f.enum_first_add ) ) return false;
		if ( !read_subconst_enum( r, f.enum_first_const ) ) return false;
		if ( !read_affine_enum( r, f.enum_inj_b ) ) return false;
		if ( !read_mod_add_enum( r, f.enum_second_add ) ) return false;
		if ( !read_subconst_enum( r, f.enum_second_const ) ) return false;
		if ( !read_affine_enum( r, f.enum_inj_a ) ) return false;
		return true;
	}

	static inline void write_cursor( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialSearchCursor& cursor )
	{
		w.write_u64( static_cast<std::uint64_t>( cursor.stack.size() ) );
		for ( const auto& f : cursor.stack )
			write_search_frame( w, f );
	}

	static inline bool read_cursor( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialSearchCursor& cursor )
	{
		std::uint64_t count = 0;
		if ( !r.read_u64( count ) ) return false;
		cursor.stack.clear();
		cursor.stack.resize( static_cast<std::size_t>( count ) );
		for ( auto& f : cursor.stack )
		{
			if ( !read_search_frame( r, f ) ) return false;
		}
		return true;
	}

	static inline void write_trail_vector( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const std::vector<DifferentialTrailStepRecord>& v )
	{
		w.write_u64( static_cast<std::uint64_t>( v.size() ) );
		for ( const auto& s : v )
			write_trail_step( w, s );
	}

	static inline bool read_trail_vector( TwilightDream::auto_search_checkpoint::BinaryReader& r, std::vector<DifferentialTrailStepRecord>& v )
	{
		std::uint64_t count = 0;
		if ( !r.read_u64( count ) ) return false;
		v.clear();
		v.resize( static_cast<std::size_t>( count ) );
		for ( auto& s : v )
		{
			if ( !read_trail_step( r, s ) ) return false;
		}
		return true;
	}

	struct DifferentialCheckpointLoadResult
	{
		DifferentialBestSearchConfiguration configuration {};
		std::uint32_t start_difference_a = 0;
		std::uint32_t start_difference_b = 0;
		std::uint64_t visited_node_count = 0;
		int best_total_weight = INFINITE_WEIGHT;
		std::vector<DifferentialTrailStepRecord> best_trail;
		std::vector<DifferentialTrailStepRecord> current_trail;
		DifferentialSearchCursor cursor;
		std::uint64_t elapsed_usec = 0;
	};

	static inline bool write_differential_checkpoint( const std::string& path, const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, std::uint64_t elapsed_usec )
	{
		return TwilightDream::auto_search_checkpoint::write_atomic( path, [ & ]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
			if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::Differential ) )
				return false;
			w.write_u64( elapsed_usec );
			write_config( w, context.configuration );
			w.write_u32( context.start_difference_a );
			w.write_u32( context.start_difference_b );
			w.write_u64( context.visited_node_count );
			w.write_i32( context.best_total_weight );
			write_trail_vector( w, context.best_differential_trail );
			write_trail_vector( w, context.current_differential_trail );
			write_cursor( w, cursor );
			context.memoization.serialize( w );
			return w.ok();
		} );
	}

	static inline bool read_differential_checkpoint( const std::string& path, DifferentialCheckpointLoadResult& out, TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int>& memo )
	{
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::Differential )
			return false;
		if ( !r.read_u64( out.elapsed_usec ) )
			return false;
		if ( !read_config( r, out.configuration ) )
			return false;
		if ( !r.read_u32( out.start_difference_a ) )
			return false;
		if ( !r.read_u32( out.start_difference_b ) )
			return false;
		if ( !r.read_u64( out.visited_node_count ) )
			return false;
		if ( !r.read_i32( out.best_total_weight ) )
			return false;
		if ( !read_trail_vector( r, out.best_trail ) )
			return false;
		if ( !read_trail_vector( r, out.current_trail ) )
			return false;
		if ( !read_cursor( r, out.cursor ) )
			return false;
		if ( !memo.deserialize( r ) )
			return false;
		return true;
	}

	inline void BinaryCheckpointManager::poll( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor )
	{
		if ( !enabled() )
			return;
		const auto now = std::chrono::steady_clock::now();
		const bool due_time = ( every_seconds != 0 ) && ( last_write_time.time_since_epoch().count() == 0 || std::chrono::duration<double>( now - last_write_time ).count() >= double( every_seconds ) );
		if ( !pending_best && !due_time )
			return;
		if ( write_now( context, cursor, pending_best ? "best_weight_change" : "periodic_timer" ) )
		{
			pending_best = false;
			last_write_time = now;
		}
	}

	inline bool BinaryCheckpointManager::write_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason )
	{
		const auto now = std::chrono::steady_clock::now();
		const std::uint64_t elapsed_usec = ( context.run_start_time.time_since_epoch().count() == 0 ) ? 0ull : static_cast<std::uint64_t>( std::chrono::duration_cast<std::chrono::microseconds>( now - context.run_start_time ).count() );
		const bool ok = write_differential_checkpoint( path, context, cursor, elapsed_usec );

		{
			std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
			const auto old_flags = std::cout.flags();
			const auto old_prec = std::cout.precision();
			const auto old_fill = std::cout.fill();

			const double elapsed_seconds = static_cast<double>( elapsed_usec ) / 1e6;
			const int best_weight_out = ( context.best_total_weight >= INFINITE_WEIGHT ) ? -1 : context.best_total_weight;

			TwilightDream::runtime_component::print_progress_prefix( std::cout );
			std::cout << "[Checkpoint] search_kind=differential"
					  << "  binary_checkpoint_write_result=" << ( ok ? "success" : "failure" )
					  << "  checkpoint_reason=" << ( reason ? reason : "unspecified" )
					  << "  checkpoint_path=" << path
					  << "  visited_node_count=" << context.visited_node_count
					  << "  best_total_weight=" << best_weight_out
					  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed_seconds
					  << "  cursor_stack_depth=" << cursor.stack.size() << "\n";

			std::cout.flags( old_flags );
			std::cout.precision( old_prec );
			std::cout.fill( old_fill );
		}

		return ok;
	}

}  // namespace TwilightDream::auto_search_differential

#endif

