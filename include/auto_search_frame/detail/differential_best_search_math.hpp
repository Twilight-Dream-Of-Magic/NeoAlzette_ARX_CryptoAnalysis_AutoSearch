#pragma once


#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <memory>
#include <memory_resource>
#include <new>
#include <limits>
#include <source_location>
#include <bit>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <ctime>

#include "auto_search_frame/detail/differential_best_search_primitives.hpp"
#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_injection_constexpr.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"

using TwilightDream::arx_operators::diff_subconst_exact_weight_ceil_int;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::xdp_add_lm2001;
using TwilightDream::arx_operators::xdp_add_lm2001_n;

// Implemented in `test_arx_operator_self_test.cpp` (linked into this executable).
int run_arx_operator_self_test();
int run_differential_search_self_test( std::uint64_t seed, std::size_t extra_cases );

namespace TwilightDream::auto_search_differential
{
	struct DifferentialBestSearchConfiguration;
	struct ModularAdditionEnumerator;

	using ::TwilightDream::NeoAlzetteCore;

	struct DifferentialTrailStepRecord;
	struct DifferentialBestSearchConfiguration;

	// Injection transition caches can grow extremely large in batch mode (random inputs),
	// which can make thread-exit teardown very slow (looks like "hang" after 100% progress).
	// We bound the cache size per-thread; `0` disables caching.
	// Keep the default conservative to avoid large memory footprints in multi-thread batch runs.
	extern std::size_t g_injection_cache_max_entries_per_thread;  // default: 2^16

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
	// Weight-Sliced pDDT (w-pDDT) accelerator
	//
	// Paper lineage:
	//   Biryukov/Velichkov threshold search uses a pDDT that stores
	//   (alpha, beta, gamma) whenever DP(alpha,beta->gamma) >= p_thres.
	//   Our search keeps the same Matsui/pDDT spirit, but upgrades the threshold
	//   axis to exact LM2001 integer-weight shells:
	//     S_t(alpha,beta) = { gamma | w_diff(alpha,beta->gamma) = t }.
	//
	// Engineering contract:
	//   This cache is rebuildable accelerator state only; it never defines truth.
	//   Cache miss / disable must fall back to exact generation of the same shell.
	// ============================================================================

	struct WeightSlicedPddtKey
	{
		std::uint32_t alpha = 0;
		std::uint32_t beta = 0;
		std::uint8_t	weight = 0;
		std::uint8_t	word_bits = 32;

		friend bool operator==( const WeightSlicedPddtKey& a, const WeightSlicedPddtKey& b ) noexcept
		{
			return a.alpha == b.alpha && a.beta == b.beta && a.weight == b.weight && a.word_bits == b.word_bits;
		}
	};

	struct WeightSlicedPddtKeyHash
	{
		std::size_t operator()( const WeightSlicedPddtKey& k ) const noexcept
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

	struct WeightSlicedPddtShell
	{
		std::uint32_t* data = nullptr;
		std::uint32_t  size = 0;
	};

	class WeightSlicedPddtCache
	{
	public:
		void configure( bool enable, int max_weight );
		bool enabled() const;
		int max_weight() const;
		bool try_get_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, std::vector<std::uint32_t>& out );
		void maybe_put_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, const std::vector<std::uint32_t>& shell );
		void clear_and_disable( const char* reason = nullptr );
		void clear_keep_enabled( const char* reason = nullptr );

	private:
		mutable std::mutex																	 mutex_ {};
		bool																				 enabled_ = false;
		int																					 max_weight_ = 0;
		std::unordered_map<WeightSlicedPddtKey, WeightSlicedPddtShell, WeightSlicedPddtKeyHash> map_ {};
	};

	extern WeightSlicedPddtCache g_weight_sliced_pddt_cache;
	inline WeightSlicedPddtCache& g_weight_shell_cache = g_weight_sliced_pddt_cache;

	bool rebuild_modular_addition_enumerator_shell_cache(
		const DifferentialBestSearchConfiguration& configuration,
		ModularAdditionEnumerator& enumerator );

	std::uint64_t estimate_weight_sliced_pddt_bytes( int weight, int word_bits ) noexcept;
	int compute_weight_sliced_pddt_max_weight_from_budget( std::uint64_t budget_bytes, int word_bits ) noexcept;
	void configure_weight_sliced_pddt_cache_for_run( DifferentialBestSearchConfiguration& configuration, std::uint64_t rebuildable_budget_bytes ) noexcept;

	// ============================================================================
	// Exact carry-DP helpers for y = x - constant (mod 2^32) under XOR differences.
	// These helpers are shared by the resumable sub-const enumerator.
	// ============================================================================

	std::array<std::uint64_t, 4> compute_next_prefix_counts_for_addition_by_constant_at_bit(
		const std::array<std::uint64_t, 4>& prefix_counts_by_carry_pair_state,
		std::uint32_t input_difference_bit,
		std::uint32_t additive_constant_bit,
		std::uint32_t output_difference_bit );

	std::uint32_t compute_greedy_output_difference_for_addition_by_constant(
		std::uint32_t input_difference,
		std::uint32_t additive_constant );

	// ============================================================================
	// Injection affine model (XOR with NOT-AND / NOT-OR): InjectionAffineTransition{ basis_vectors, offset, rank_weight }
	//
	// We must preserve the exact call structure of the current V6 core:
	//   - (C0, D0) = cd_injection_from_B(B, (RC[2]|RC[3]), RC[3])
	//     then A ^= rotl(C0,24) ^ rotl(D0,16) ^ rotl((C0 >> 1) ^ (D0 << 1), 8) ^ RC[4]
	//   - (C1, D1) = cd_injection_from_A(A, (RC[7]&RC[8]), RC[8])
	//     then B ^= rotr(C1,24) ^ rotr(D1,16) ^ rotr((C1 << 3) ^ (D1 >> 3), 8) ^ RC[9]
	//
	// In XOR-difference terms, the injected constants RC[4], RC[9] cancel out, but the non-linear
	// cd_injection_* still changes the difference. Here we build a rigorous (auditable) XOR-differential
	// transition model under the assumption that the base input x is uniform:
	//
	//   Let f be the full injected XOR term (the exact value XORed into the other branch):
	//     f_B(B) = rotl24(C(B)) ⊕ rotl16(D(B)) ⊕ rotl((C(B) >> 1) ⊕ (D(B) << 1), 8) ⊕ RC[4]
	//              where (C,D)=cd_injection_from_B(B,...)
	//     f_A(A) = rotr24(C(A)) ⊕ rotr16(D(A)) ⊕ rotr((C(A) << 3) ⊕ (D(A) >> 3), 8) ⊕ RC[9]
	//              where (C,D)=cd_injection_from_A(A,...)
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

	using ShardedInjectionCache32 = TwilightDream::runtime_component::ShardedSharedCache<std::uint32_t, InjectionAffineTransition>;

	// Optional shared (cross-thread) caches. These are configured at run start (single/batch).
	extern ShardedInjectionCache32 g_shared_injection_cache_branch_a;
	extern ShardedInjectionCache32 g_shared_injection_cache_branch_b;

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

	// Formula sanity: f(0) and f(e_i) must match wrapper so D_Δ f(0) = f(0) ⊕ f(Δ) and column_i = D_Δ f(e_i) ⊕ D_Δ f(0) are unchanged.
	static_assert( g_injected_xor_term_f0_branch_b == compute_injected_xor_term_from_branch_b( 0u ), "f(0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f0_branch_a == compute_injected_xor_term_from_branch_a( 0u ), "f(0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 0 ] == compute_injected_xor_term_from_branch_b( 1u ), "f(e_0) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 0 ] == compute_injected_xor_term_from_branch_a( 1u ), "f(e_0) branch_a: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_b[ 31 ] == compute_injected_xor_term_from_branch_b( 1u << 31 ), "f(e_31) branch_b: direct vs wrapper" );
	static_assert( g_injected_xor_term_f_basis_branch_a[ 31 ] == compute_injected_xor_term_from_branch_a( 1u << 31 ), "f(e_31) branch_a: direct vs wrapper" );

	InjectionAffineTransition compute_injection_transition_from_branch_b( std::uint32_t branch_b_input_difference );

	InjectionAffineTransition compute_injection_transition_from_branch_a( std::uint32_t branch_a_input_difference );

	int compute_greedy_upper_bound_weight(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference );

	std::vector<DifferentialTrailStepRecord> construct_greedy_initial_differential_trail(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		int& output_total_weight );

	// ============================================================================
	// Matsui / best-first (round-level BnB + bit-level recursion)
	// ----------------------------------------------------------------------------
	// Notation (32-bit words):
	//   - ⊞ : addition modulo 2^32        (x ⊞ y) = (x + y) mod 2^32
	//   - ⊟ : subtraction modulo 2^32     (x ⊟ c) = (x - c) mod 2^32
	//   - ⊕ : bitwise XOR
	//   - ROTL_r(x), ROTR_r(x): rotations
	//   - The current V6 core has no standalone outer L1/L2 wrapper layers.
	//     Some state names below still retain older "linear_layer_*" wording for trace/checkpoint
	//     compatibility, but the carried value is an identity passthrough in the current core.
	//
	// Differential model conventions:
	//   - XOR-difference: Δx = x ⊕ x'
	//   - For modular add/sub, we DO NOT force identity transitions.
	//     We enumerate feasible output differences and score with exact integer-weight backends:
	//       - xdp_add_lm2001_n : (Δx, Δy) -> Δ(x ⊞ y)
	//       - diff_subconst_exact_weight_ceil_int(): Δx -> Δ(x ⊟ c)
	//   - Cross-branch injection cd_injection_from_* is kept exactly (structure preserved).
	//     We propagate differences through its internal ops, and additionally attach the
	//     exact InjectionAffineTransition rank weight from the affine-derivative model.
	//
	// IMPORTANT (must match the real cipher structure):
	//   - We DO NOT remove cd_injection_from_* from the cipher implementation.
	//   - We DO propagate differences through cd_injection_from_* (structure preserved),
	//     and we also include InjectionAffineTransition-based rank_weight.
	//
	// One forward round (as implemented):
	//   Input: (A0, B0)
	//   (1)  B1 = B0 ⊞ (ROTL_31(A0) ⊕ ROTL_17(A0) ⊕ RC[0])
	//        A1 = A0 ⊟ RC1
	//        A2 = A1 ⊕ ROTL_{R0}(B1)
	//        B2 = B1 ⊕ ROTL_{R1}(A2)
	//        (C0, D0) = cd_injection_from_B(B2; rc0, rc1)
	//        A3 = A2 ⊕ (ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ ROTL_8((C0 >> 1) ⊕ (D0 << 1)) ⊕ RC4)
	//
	//   (2)  A4 = A3 ⊞ (ROTL_31(B2) ⊕ ROTL_17(B2) ⊕ RC5)
	//        B3 = B2 ⊟ RC6      // no extra standalone linear layer between the two subrounds in V6
	//        B4 = B3 ⊕ ROTL_{R0}(A4)
	//        A5 = A4 ⊕ ROTL_{R1}(B4)
	//        (C1, D1) = cd_injection_from_A(A5; rc0, rc1)
	//        B5 = B4 ⊕ (ROTR_24(C1) ⊕ ROTR_16(D1) ⊕ ROTR_8((C1 << 3) ⊕ (D1 >> 3)) ⊕ RC9)
	//
	//   (3)  A_out = A5 ⊕ RC10
	//        B_out = B5 ⊕ RC11
	//
	// Record fields below store Δ-values at each boundary and the per-step weight.
	// ============================================================================


}  // namespace TwilightDream::auto_search_differential
