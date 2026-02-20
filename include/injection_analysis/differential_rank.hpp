#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <unordered_map>

#include "common/runtime_component.hpp"
#include "injection_analysis/function_constexpr.hpp"
#include "arx_analysis_operators/DefineSearchWeight.hpp"

namespace TwilightDream::auto_search_differential
{
	// Injection transition caches can grow extremely large in batch mode (random inputs),
	// which can make thread-exit teardown very slow (looks like "hang" after 100% progress).
	// We bound the cache size per-thread; `0` disables caching.
	// Keep the default conservative to avoid large memory footprints in multi-thread batch runs.
	inline std::size_t g_injection_cache_max_entries_per_thread = 65536;	 // default: 2^16

	// ============================================================================
	// Exact affine-derivative rank model for the current injected XOR maps
	// ----------------------------------------------------------------------------
	// The analyzed objects are the exact 32 → 32 maps
	//   F_B(B) = injected_xor_term_from_branch_b(B),
	//   F_A(A) = injected_xor_term_from_branch_a(A),
	// i.e. the actual XOR words injected into the opposite branch by the current core.
	//
	// Current exact round semantics:
	//   B_pre = B ⊕ RC[4]
	//   (C_B(B_pre), D_B(B_pre)) = cd_injection_from_B(B_pre)
	//   F_B(B) = ROTL24((C_B(B_pre) ≪ 2) ⊕ (D_B(B_pre) ≫ 2))
	//          ⊕ ROTL16(D_B(B_pre))
	//          ⊕ ROTL8((C_B(B_pre) ≫ 5) ⊕ (D_B(B_pre) ≪ 5))
	//
	//   A_pre = A ⊕ RC[9]
	//   (C_A(A_pre), D_A(A_pre)) = cd_injection_from_A(A_pre)
	//   F_A(A) = ROTR24((C_A(A_pre) ≫ 3) ⊕ (D_A(A_pre) ≪ 3))
	//          ⊕ ROTR16(D_A(A_pre))
	//          ⊕ ROTR8((C_A(A_pre) ≪ 1) ⊕ (D_A(A_pre) ≫ 1))
	//
	// Important:
	// - The variable `B` / `A` above is the branch word before the internal constant pre-whitening.
	// - So the differential rank analysis below is for the exact implemented maps F_B / F_A,
	//   not for an older “external XOR with RC[4]/RC[9]” comment model.
	//
	// For an input XOR-difference Δ, define the derivative
	//   D_ΔF(x) = F(x) ⊕ F(x ⊕ Δ).
	//
	// Because F_B and F_A are vector quadratic Boolean maps, D_ΔF is affine for each fixed Δ:
	//   D_ΔF(x) = M_Δ x ⊕ c_Δ.
	//
	// Therefore the reachable output-difference set is exactly the affine subspace
	//   c_Δ ⊕ im(M_Δ),
	// and for uniform x every reachable output difference occurs with probability 2^{-rank(M_Δ)}.
	//
	// We encode that object as
	//   InjectionAffineTransition{ offset=c_Δ, basis_vectors=basis(im(M_Δ)), rank_weight=rank(M_Δ) }.
	// ============================================================================

	struct InjectionAffineTransition
	{
		std::uint32_t				  offset = 0;		 // c_Δ in D_ΔF(x) = M_Δ x ⊕ c_Δ
		std::array<std::uint32_t, 32> basis_vectors {};	 // packed basis of im(M_Δ), stored in [0..rank_weight-1]
		std::uint64_t				  rank_weight = 0;	 // rank(M_Δ) = -log2(probability of each reachable output difference)
	};

	using ShardedInjectionCache32 = TwilightDream::runtime_component::ShardedSharedCache<std::uint32_t, InjectionAffineTransition>;

	// Optional shared (cross-thread) caches. These are configured at run start (single/batch).
	inline ShardedInjectionCache32 g_shared_injection_cache_branch_a {};
	inline ShardedInjectionCache32 g_shared_injection_cache_branch_b {};

	inline void configure_shared_injection_caches( std::size_t total_entries, std::size_t shard_count )
	{
		g_shared_injection_cache_branch_a.configure( total_entries, shard_count );
		g_shared_injection_cache_branch_b.configure( total_entries, shard_count );
	}

	inline void clear_shared_injection_caches_with_progress()
	{
		g_shared_injection_cache_branch_a.clear_and_release_with_progress( "shared_cache.branch_a" );
		g_shared_injection_cache_branch_b.clear_and_release_with_progress( "shared_cache.branch_b" );
	}

	inline void apply_injection_cache_configuration(
		std::size_t cache_entries_per_thread,
		std::size_t shared_total_entries,
		std::size_t shared_shard_count )
	{
		g_injection_cache_max_entries_per_thread = cache_entries_per_thread;
		configure_shared_injection_caches( shared_total_entries, shared_shard_count );
	}

	// Wrapper for the exact analyzed map F_B on the raw branch-B word.
	static constexpr std::uint32_t compute_injected_xor_term_from_branch_b( std::uint32_t branch_b_value ) noexcept
	{
		return TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( branch_b_value );
	}

	// Wrapper for the exact analyzed map F_A on the raw branch-A word.
	static constexpr std::uint32_t compute_injected_xor_term_from_branch_a( std::uint32_t branch_a_value ) noexcept
	{
		return TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( branch_a_value );
	}

	// Exact speed-up for the current maps F_B / F_A:
	// in the transition builder we need D_ΔF(0) and D_ΔF(e_i) for i = 0..31, where
	//   D_ΔF(x) = F(x) ⊕ F(x ⊕ Δ).
	// Precomputing F(0) and F(e_i) removes half the exact-map evaluations per Δ without changing the model.
	// Use `analysis_constexpr` directly in consteval so the compile-time object is exactly the same function.
	static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_b()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ static_cast<std::size_t>( i ) ] = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << i );
		return out;
	}

	static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_a()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ static_cast<std::size_t>( i ) ] = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << i );
		return out;
	}

	static constexpr std::uint32_t				   g_injected_xor_term_f0_branch_b = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 0u );
	static constexpr std::uint32_t				   g_injected_xor_term_f0_branch_a = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 0u );
	static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_b = make_injected_xor_term_basis_branch_b();
	static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_a = make_injected_xor_term_basis_branch_a();

	// Formula sanity for the exact analyzed objects F_B / F_A:
	// F(0) and F(e_i) must match the runtime wrapper so D_ΔF(0) and every column of M_Δ are unchanged.
	static_assert( g_injected_xor_term_f0_branch_b == compute_injected_xor_term_from_branch_b( 0u ), "f(0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f0_branch_a == compute_injected_xor_term_from_branch_a( 0u ), "f(0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 0 ] == compute_injected_xor_term_from_branch_b( 1u ), "f(e_0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 0 ] == compute_injected_xor_term_from_branch_a( 1u ), "f(e_0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 31 ] == compute_injected_xor_term_from_branch_b( 1u << 31 ), "f(e_31) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 31 ] == compute_injected_xor_term_from_branch_a( 1u << 31 ), "f(e_31) branch_a: direct vs wrapper" );

	namespace injection_rank_detail
	{
		static inline int xor_basis_add( std::array<std::uint32_t, 32>& basis_by_bit, std::uint32_t v ) noexcept
		{
			// classic GF(2) linear basis insertion; returns 1 if v increased rank, 0 otherwise
			while ( v != 0u )
			{
				const unsigned		bit = 31u - std::countl_zero( v );
				const std::uint32_t basis = basis_by_bit[ std::size_t( bit ) ];
				if ( basis != 0u )
				{
					v ^= basis;
				}
				else
				{
					basis_by_bit[ std::size_t( bit ) ] = v;
					return 1;
				}
			}
			return 0;
		}
	}  // namespace injection_rank_detail

	inline InjectionAffineTransition compute_injection_transition_from_branch_b( std::uint32_t branch_b_input_difference )
	{
		// Thread-safe for batch search: each thread gets its own cache to avoid data races.
		static thread_local bool tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t tls_epoch = 0;
		static thread_local std::size_t cache_reserved_hint = 0;
		static thread_local std::size_t last_configured_cache_cap = std::size_t( -1 );
		// Reset thread-local state on each new "run" (so a prior OOM doesn't permanently disable caching).
		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				// Do not call tls_pool.release() while `cache` is still alive:
				// MSVC's pmr::unordered_map may still retain allocator-owned internal state
				// after rehash(0), and releasing the pool here can make the next find()/emplace()
				// observe freed storage.
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
				cache.max_load_factor( 0.7f );
				cache_reserved_hint = 0;
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
		const std::uint32_t f_delta = compute_injected_xor_term_from_branch_b( branch_b_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_b ^ f_delta;	 // D_Δ f(0)

		// Build the column space of M_Δ via
		//   column_i(M_Δ) = D_ΔF(e_i) ⊕ D_ΔF(0).
		std::uint64_t rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_b[ std::size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_b( basis_input_vector ^ branch_b_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;	 // D_ΔF(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += injection_rank_detail::xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		// pack basis vectors deterministically (high-bit first)
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ std::size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ std::size_t( packed_index++ ) ] = vector_value;
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

	inline InjectionAffineTransition compute_injection_transition_from_branch_a( std::uint32_t branch_a_input_difference )
	{
		static thread_local bool tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t tls_epoch = 0;
		static thread_local std::size_t cache_reserved_hint = 0;
		static thread_local std::size_t last_configured_cache_cap = std::size_t( -1 );

		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				// Same lifetime rule as branch-B cache above: keep the pool alive while the
				// container object exists, otherwise the next lookup can hit released storage.
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
				cache.max_load_factor( 0.7f );
				cache_reserved_hint = 0;
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
		const std::uint32_t f_delta = compute_injected_xor_term_from_branch_a( branch_a_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_a ^ f_delta;	 // D_ΔF(0)

		std::uint64_t rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_a[ std::size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_a( basis_input_vector ^ branch_a_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;	 // D_ΔF(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += injection_rank_detail::xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ std::size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ std::size_t( packed_index++ ) ] = vector_value;
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

}  // namespace TwilightDream::auto_search_differential
