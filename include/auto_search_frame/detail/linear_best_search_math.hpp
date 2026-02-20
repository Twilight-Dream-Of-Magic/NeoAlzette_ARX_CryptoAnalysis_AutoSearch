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
#include <optional>
#include <source_location>
#include <bit>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <ctime>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_injection_constexpr.hpp"
#include "arx_analysis_operators/linear_correlation_addconst.hpp"
// Keep the generic Wallen logn route nearby as a fallback / regression backend.
// The default fast path below still uses the Schulte-Geers / CCZ weight backend.
#include "arx_analysis_operators/linear_correlation_add_logn.hpp"
#include "arx_analysis_operators/modular_addition_ccz.hpp"

// self-test harness is compiled from test_arx_operator_self_test.cpp
int run_arx_operator_self_test();
int run_linear_search_self_test( std::uint64_t seed, std::size_t extra_cases );

namespace TwilightDream::auto_search_linear
{
	using ::TwilightDream::NeoAlzetteCore;

	extern std::atomic<bool> g_disable_linear_tls_caches;

	// ============================================================================
	// Small utilities
	// ============================================================================

	static inline unsigned parity32( std::uint32_t x ) noexcept
	{
		return static_cast<unsigned>( std::popcount( x ) & 1u );
	}

	// ============================================================================
	// Mask propagation for deterministic linear transport used in the cipher
	//
	// We traverse the cipher in reverse (output mask -> input mask).
	// For y = L(x), we need x_mask = L^T(y_mask).
	//
	// In the current V6 core there are no standalone outer l1/l2 wrapper layers anymore.
	// The relevant linear transport here is the fixed cross-branch XOR/rotation bridge, and
	// its transpose is obtained by flipping rotl <-> rotr with the same rotation constants.
	// ============================================================================

	/** Transpose transformations for linear cryptanalysis **/
	// Transpose: replace every rotl with rotr.
	// For the dynamic diffusion masks M0 / M1 used inside the injection gadget,
	// transpose is realized by reversing the rotation orientation, i.e.
	// NeoAlzetteCore::generate_dynamic_diffusion_mask0_transpose() /
	// NeoAlzetteCore::generate_dynamic_diffusion_mask1_transpose().

	// ============================================================================
	// Injection model for linear correlations (quadratic -> rank/2 weight)
	//
	// The real injected term is a 32->32 nonlinear function:
	//   f_B(B) = rotl24(C(B)) ⊕ rotl16(D(B)) ⊕ rotl((C(B) >> 1) ⊕ (D(B) << 1), 8) ⊕ RC[4]
	//   f_A(A) = rotr24(C(A)) ⊕ rotr16(D(A)) ⊕ rotr((C(A) << 3) ⊕ (D(A) >> 3), 8) ⊕ RC[9]
	// as returned by neoalzette_injection_constexpr.hpp
	//
	// However, f is built only from XOR/ROT/NOT/AND/OR with linear masks => f is (vector) quadratic.
	// For a fixed output mask u, define the scalar boolean function:
	//   g_u(x) = parity(u & f(x))  =  <u, f(x)>
	// Then g_u is a quadratic boolean function. Its Walsh spectrum is determined by the rank of the
	// associated bilinear form matrix S(u), and any non-zero correlation has magnitude:
	//   |corr| = 2^{-rank(S(u))/2}    => weight = rank(S(u))/2.
	//
	// Mask-set support (no more "linearization" approximation):
	// - The correlated input masks v for a fixed output mask u form an affine subspace:
	//     v ∈ l(u) ⊕ im(S(u))
	//   where l(u) is the linear coefficient vector of g_u and S(u) is the bilinear matrix.
	// - We compute l(u) exactly via:
	//     l_i(u) = g_u(e_i) ⊕ g_u(0) = parity(u & f(e_i)) ⊕ parity(u & f(0)).
	// - Every v in that affine subspace has the same absolute correlation magnitude:
	//     |corr| = 2^{-rank(S(u))/2}  => weight = rank(S(u))/2.
	//
	// ============================================================================

	static constexpr std::uint32_t injected_f0_branch_b = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 0u );
	static constexpr std::uint32_t injected_f0_branch_a = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 0u );

	// We keep the full injected maps f_B / f_A intact when extracting the exact
	// linear coefficient vector l(u). Pure rotate/XOR sublayers still admit
	// transpose by reversing the rotation orientation (and the corresponding
	// zero-fill shift direction), but we do not split mask0 / mask1 into
	// standalone pseudo-components here.

	static consteval std::array<std::uint32_t, 32> make_injection_f_basis_branch_b()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ std::size_t( i ) ] = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 1u << i );
		return out;
	}

	static consteval std::array<std::uint32_t, 32> make_injection_f_basis_branch_a()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ std::size_t( i ) ] = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 1u << i );
		return out;
	}

	static constexpr std::array<std::uint32_t, 32> injected_f_basis_branch_b = make_injection_f_basis_branch_b();
	static constexpr std::array<std::uint32_t, 32> injected_f_basis_branch_a = make_injection_f_basis_branch_a();

	// Formula sanity: precomputed f(0) and f(e_i) must match direct neoalzette_constexpr (same as differential search).
	static_assert( injected_f0_branch_b == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 0u ), "f(0) branch_b: precomputed vs direct" );
	static_assert( injected_f0_branch_a == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 0u ), "f(0) branch_a: precomputed vs direct" );
	static_assert( injected_f_basis_branch_b[ 0 ] == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 1u ), "f(e_0) branch_b: precomputed vs direct" );
	static_assert( injected_f_basis_branch_a[ 0 ] == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 1u ), "f(e_0) branch_a: precomputed vs direct" );
	static_assert( injected_f_basis_branch_b[ 31 ] == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( 1u << 31 ), "f(e_31) branch_b: precomputed vs direct" );
	static_assert( injected_f_basis_branch_a[ 31 ] == TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( 1u << 31 ), "f(e_31) branch_a: precomputed vs direct" );

	// --------------------------------------------------------------------------
	// Exact quadratic weight for g_u(x)=<u,f(x)> via bilinear rank
	//
	// For i<j, the quadratic (bilinear) coefficient vector of f for (x_i * x_j) is:
	//   Δ²_{i,j} f = f(0) ⊕ f(e_i) ⊕ f(e_j) ⊕ f(e_i ⊕ e_j)     (vector in {0,1}^{32})
	// Then the scalar quadratic coefficient q_{i,j}(u) of g_u is:
	//   q_{i,j}(u) = parity( u & Δ²_{i,j} f )
	//
	// We build S(u) from these q_{i,j}(u) and compute rank(S(u)) over GF(2).
	// --------------------------------------------------------------------------

	// Fused: compute Δ²_{i,j} f on the fly and fill rows_by_outbit; no separate stored deltas array.
	//
	// Formula check (must match comment above):
	//   e_i = 1<<i, e_j = 1<<j
	//   Δ²_{i,j} f = f(0) ⊕ f(e_i) ⊕ f(e_j) ⊕ f(e_i ⊕ e_j)
	//   fi = f(e_i), fj = f(e_j), fij = f(e_i ⊕ e_j) => delta = f0 ⊕ fi ⊕ fj ⊕ fij
	//   q_{i,j}(u) = parity(u & Δ²_{i,j} f); for u=1<<k => q_{i,j} = (delta>>k)&1; fill S(u) at (i,j),(j,i).
	static consteval std::array<std::array<std::uint32_t, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_b()
	{
		std::array<std::array<std::uint32_t, 32>, 32> rows_by_outbit {};
		for ( int i = 0; i < 31; ++i )
		{
			const std::uint32_t fi = injected_f_basis_branch_b[ std::size_t( i ) ];   // f(e_i), e_i = 1<<i
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t fj = injected_f_basis_branch_b[ std::size_t( j ) ];   // f(e_j)
				const std::uint32_t fij = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( ( 1u << i ) ^ ( 1u << j ) );   // f(e_i ⊕ e_j)
				const std::uint32_t delta = injected_f0_branch_b ^ fi ^ fj ^ fij;   // Δ²_{i,j} f = f(0) ⊕ f(e_i) ⊕ f(e_j) ⊕ f(e_i ⊕ e_j)
				for ( int k = 0; k < 32; ++k )
				{
					if ( ( ( delta >> k ) & 1u ) != 0u )   // q_{i,j}(u) for u=1<<k
					{
						rows_by_outbit[ std::size_t( k ) ][ std::size_t( i ) ] |= ( 1u << j );
						rows_by_outbit[ std::size_t( k ) ][ std::size_t( j ) ] |= ( 1u << i );
					}
				}
			}
		}
		return rows_by_outbit;
	}

	static consteval std::array<std::array<std::uint32_t, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_a()
	{
		std::array<std::array<std::uint32_t, 32>, 32> rows_by_outbit {};
		for ( int i = 0; i < 31; ++i )
		{
			const std::uint32_t fi = injected_f_basis_branch_a[ std::size_t( i ) ];
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t fj = injected_f_basis_branch_a[ std::size_t( j ) ];
				const std::uint32_t fij = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( ( 1u << i ) ^ ( 1u << j ) );
				const std::uint32_t delta = injected_f0_branch_a ^ fi ^ fj ^ fij;
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

	static constexpr std::array<std::array<std::uint32_t, 32>, 32> injected_quad_rows_by_outbit_branch_b = make_injection_quadratic_rows_by_outbit_branch_b();
	static constexpr std::array<std::array<std::uint32_t, 32>, 32> injected_quad_rows_by_outbit_branch_a = make_injection_quadratic_rows_by_outbit_branch_a();

	static inline int xor_basis_add_32( std::array<std::uint32_t, 32>& basis_by_msb, std::uint32_t v ) noexcept
	{
		// classic GF(2) linear basis insertion; returns 1 if v increased rank, 0 otherwise
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

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_b( std::uint32_t output_mask_u ) noexcept
	{
		InjectionCorrelationTransition transition {};
		if ( output_mask_u == 0u )
			return transition;

		// offset_mask = exact linear coefficient vector l(u), computed directly
		// from the full injected map basis values:
		//   l_i(u) = parity(u & f(e_i)) ^ parity(u & f(0)).
		{
			const unsigned g0 = parity32( output_mask_u & injected_f0_branch_b );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_b[ std::size_t( i ) ] );
				if ( ( gi ^ g0 ) != 0u )
					offset_mask ^= ( 1u << i );
			}
			transition.offset_mask = offset_mask;
		}

		// Build bilinear matrix rows S(u) by XORing per-output-bit precomputed matrices.
		std::array<std::uint32_t, 32> rows {};
		for ( int k = 0; k < 32; ++k )
		{
			if ( ( ( output_mask_u >> k ) & 1u ) == 0u )
				continue;
			for ( int i = 0; i < 32; ++i )
				rows[ std::size_t( i ) ] ^= injected_quad_rows_by_outbit_branch_b[ std::size_t( k ) ][ std::size_t( i ) ];
		}

		// rank and basis of im(S(u)) (== row space for symmetric/alternating matrices)
		int							  rank = 0;
		std::array<std::uint32_t, 32> basis_by_msb {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t row = rows[ std::size_t( i ) ];
			if ( row != 0u )
				rank += xor_basis_add_32( basis_by_msb, row );
		}
		transition.rank = rank;
		transition.weight = ( rank + 1 ) / 2;
		int packed = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
			if ( v != 0u )
				transition.basis_vectors[ std::size_t( packed++ ) ] = v;
		}
		return transition;
	}

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_a( std::uint32_t output_mask_u ) noexcept
	{
		InjectionCorrelationTransition transition {};
		if ( output_mask_u == 0u )
			return transition;

		// Same exact l(u) construction for branch A:
		//   l_i(u) = parity(u & f(e_i)) ^ parity(u & f(0)).
		{
			const unsigned g0 = parity32( output_mask_u & injected_f0_branch_a );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_a[ std::size_t( i ) ] );
				if ( ( gi ^ g0 ) != 0u )
					offset_mask ^= ( 1u << i );
			}
			transition.offset_mask = offset_mask;
		}

		std::array<std::uint32_t, 32> rows {};
		for ( int k = 0; k < 32; ++k )
		{
			if ( ( ( output_mask_u >> k ) & 1u ) == 0u )
				continue;
			for ( int i = 0; i < 32; ++i )
				rows[ std::size_t( i ) ] ^= injected_quad_rows_by_outbit_branch_a[ std::size_t( k ) ][ std::size_t( i ) ];
		}

		int							  rank = 0;
		std::array<std::uint32_t, 32> basis_by_msb {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t row = rows[ std::size_t( i ) ];
			if ( row != 0u )
				rank += xor_basis_add_32( basis_by_msb, row );
		}
		transition.rank = rank;
		transition.weight = ( rank + 1 ) / 2;
		int packed = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
			if ( v != 0u )
				transition.basis_vectors[ std::size_t( packed++ ) ] = v;
		}
		return transition;
	}

	// ============================================================================
	// Local cost evaluation helpers
	// ============================================================================

	static inline std::optional<int> weight_add_varvar_ccz( std::uint32_t u_out, std::uint32_t v_in_x, std::uint32_t w_in_y ) noexcept
	{
		return TwilightDream::arx_operators::linear_correlation_add_ccz_weight( u_out, v_in_x, w_in_y, 32 );
	}

	[[nodiscard]] static inline std::optional<int> weight_sub_const_ceil_int( std::uint32_t input_mask_alpha, std::uint32_t subtrahend_constant, std::uint32_t output_mask_beta ) noexcept
	{
		using TwilightDream::arx_operators::LinearCorrelation;

		constexpr int WordBitSize = 32;

		const LinearCorrelation linear_correlation = TwilightDream::arx_operators::linear_x_modulo_minus_const32( input_mask_alpha, subtrahend_constant, output_mask_beta, WordBitSize );

		if ( !linear_correlation.is_feasible() )
			return std::nullopt;

		// lc.correlation is an exact binary rational with denominator 2^32 under this operator stack.
		// Let |corr| = |W| / 2^32 with integer |W|. Then:
		//   ceil(-log2(|corr|)) = ceil(32 - log2(|W|)) = 32 - floor(log2(|W|)).
		const double abs_correlation = std::abs( linear_correlation.correlation );
		if ( !( abs_correlation > 0.0 ) || !std::isfinite( abs_correlation ) )
			return std::nullopt;

		constexpr double	SCALE_2_POW_32 = 4294967296.0;	// 2^32 (exact in double)
		const std::uint64_t abs_weight = static_cast<std::uint64_t>( std::llround( abs_correlation * SCALE_2_POW_32 ) );

		if ( abs_weight == 0u )
			return std::nullopt;

		// C++20: bit_width(x) == floor(log2(x)) + 1 for x > 0
		const int msb_index = static_cast<int>( std::bit_width( abs_weight ) ) - 1;
		const int weight = WordBitSize - msb_index;

		return std::clamp( weight, 0, WordBitSize );
	}

	// ============================================================================
	// Candidate generation (exact, deterministic)
	// ============================================================================

	/**
	 * @brief Enumerate feasible input masks for modular addition (var-var).
	 *
	 * We analyze the linear approximation of 32-bit modular addition:
	 *   s = x ⊞ y   (addition modulo 2^32)
	 *
	 * Notation (consistent with the paper and this project):
	 * - u: output mask on s
	 * - v: input mask on x
	 * - w: input mask on y
	 * - bit i: i=0 is LSB, i=31 is MSB
	 *
	 * Paper reference:
	 *   Liu/Wang/Rijmen, "Automatic Search of Linear Trails in ARX with Applications to SPECK and Chaskey"
	 *   (ACNS 2016) — Section 3.1, Proposition 1 / Eq.(1) (Schulte-Geers explicit formula)
	 *
	 * Schulte-Geers explicit constraints (Eq.(1)):
	 *   z_{n-1} = 0
	 *   z_{n-2} = u_{n-1} ⊕ v_{n-1} ⊕ w_{n-1}
	 *   z_j     = z_{j+1} ⊕ u_{j+1} ⊕ v_{j+1} ⊕ w_{j+1}     (0 ≤ j ≤ n-3)
	 *   z_i ≥ u_i ⊕ v_i,    z_i ≥ u_i ⊕ w_i                 (0 ≤ i ≤ n-1)
	 *
	 * Correlation magnitude (Proposition 1):
	 *   |cor(u,v,w)| = 2^{-|z|}    if the constraints hold, else 0
	 * Therefore the linear weight is:
	 *   weight = -log2(|cor|) = |z|
	 *
	 * Engineering interpretation of the inequalities:
	 * - If z_i == 0, then u_i ⊕ v_i must be 0 AND u_i ⊕ w_i must be 0  =>  v_i = w_i = u_i.
	 * - If z_i == 1, the inequalities impose no restriction on (v_i, w_i) (4 choices).
	 *
	 * This function enumerates candidate pairs (v,w) in nondecreasing weight |z| and returns up to
	 * max_candidates results (0 means "no cap"). If out_hit_cap is non-null, it is set to true when
	 * enumeration stops early due to max_candidates. Only the absolute correlation is used here; the
	 * sign term from Proposition 1 is irrelevant for weight-based best-trail search.
	 */
	static inline std::vector<AddCandidate> generate_add_candidates_for_fixed_u(
		std::uint32_t output_mask_u,
		int weight_cap,
		std::size_t max_candidates = 0,
		bool* out_hit_cap = nullptr )
	{
		std::vector<AddCandidate> candidates;
		if ( out_hit_cap )
			*out_hit_cap = false;

		// Clamp to the meaningful range for 32-bit (z_31 is forced to 0).
		if ( weight_cap < 0 )
			return candidates;
		if ( weight_cap > 31 )
			weight_cap = 31;

		struct Frame
		{
			int			  bit_index = 0;		// current bit i (30..0)
			std::uint32_t input_mask_x = 0;		// assigned bits above i (mask on x)
			std::uint32_t input_mask_y = 0;		// assigned bits above i (mask on y)
			int			  z_bit = 0;			// current z_i (auxiliary "carry-support" bit from Eq.(1))
			int			  z_weight_so_far = 0;	// popcount(z_{i+1..30}) accumulated so far
		};

		// MSB constraint: z_31 = 0 => v_31 = w_31 = u_31.
		const int		   u31 = int( ( output_mask_u >> 31 ) & 1u );
		std::uint32_t	   input_mask_x_prefix = u31 ? ( 1u << 31 ) : 0u;
		std::uint32_t	   input_mask_y_prefix = input_mask_x_prefix;
		const int		   z30 = u31;  // with v_31 = w_31 = u_31: z_30 = u_31 ⊕ v_31 ⊕ w_31 = u_31
		std::vector<Frame> stack;
		bool stop_due_to_cap = false;
		bool stop_due_to_memory_guard = false;

		// Enumerate in nondecreasing weight (paper-style objective is to minimize |z|).
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

				// Prune by exact target weight.
				if ( z_weight > target_weight )
					continue;
				const int remaining_z_bits = bit_index;	 // after consuming z_i, remaining are z_{i-1}..z_0 (count=bit_index)
				if ( z_weight + remaining_z_bits < target_weight )
					continue;

				const int u_i = int( ( output_mask_u >> bit_index ) & 1u );

				auto push_next_state = [ & ]( int v_i, int w_i ) {
					std::uint32_t next_input_mask_x = st.input_mask_x | ( std::uint32_t( v_i & 1 ) << bit_index );
					std::uint32_t next_input_mask_y = st.input_mask_y | ( std::uint32_t( w_i & 1 ) << bit_index );

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
								return;
							}
						}
						return;
					}

					// Eq.(1) recursion: z_{i-1} = z_i ⊕ u_i ⊕ v_i ⊕ w_i.
					const int sum_i = u_i ^ v_i ^ w_i;
					const int z_prev = z_i ^ sum_i;
					stack.push_back( Frame { bit_index - 1, next_input_mask_x, next_input_mask_y, z_prev, z_weight } );
				};

				// Inequalities (Eq. (1)): if z_i == 0 => v_i = w_i = u_i, else free.
				if ( z_i == 0 )
				{
					// Forced assignment: v_i=w_i=u_i (otherwise constraints would be violated).
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

	void reset_weight_sliced_clat_streaming_cursor(
		WeightSlicedClatStreamingCursor& cursor,
		std::uint32_t output_mask_u,
		int weight_cap_requested );

	bool next_weight_sliced_clat_streaming_candidate(
		WeightSlicedClatStreamingCursor& cursor,
		AddCandidate& out_candidate );

	// ============================================================================
	// Split-Lookup-Recombine (SLR) enumerator for var-var addition (z-weight shells).
	//
	// Mathematical basis:
	//   Schulte-Geers explicit correlation constraints for modular addition:
	//     z = M^T(u ^ v ^ w), |Cor(u,v,w)| = 2^{-|z|}
	//   so the exact search objective is the z-shell weight |z|.
	//
	// Paper lineage:
	//   Huang/Wang 2019 first constructs the input-output mask space of a given
	//   correlation weight, then uses an improved cLAT with
	//   split/lookup/recombine to enumerate the remaining masks efficiently.
	//
	// What we keep from that line:
	// - mathematical object: exact z-shells under a fixed output mask u
	// - engineering structure: split/lookup/recombine on small 8-bit blocks
	//
	// Therefore this is NOT a full classical cLAT table. It is a z-shell based
	// Weight-Sliced cLAT enumerator implemented with a compact split-8 SLR path.
	// ============================================================================

	// ----------------------------------------------------------------------------
	// Exact sub-const enumeration (strict mode)
	// ----------------------------------------------------------------------------

	static inline std::uint64_t abs_i64_to_u64( std::int64_t v ) noexcept
	{
		return ( v < 0 ) ? std::uint64_t( -v ) : std::uint64_t( v );
	}

	static inline int floor_log2_u64( std::uint64_t v ) noexcept
	{
		return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
	}

	void reset_subconst_streaming_cursor( SubConstStreamingCursor& cursor, std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap, int nbits = 32 );

	bool next_subconst_streaming_candidate( SubConstStreamingCursor& cursor, SubConstCandidate& out_candidate );

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta_exact( std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap, int nbits = 32 );

	/**
	 * @brief Exact enumeration of input masks for a fixed var-const subtraction mask.
	 *
	 * We analyze:
	 *   y = x ⊟ C   (mod 2^32)
	 * with:
	 *   beta  = output mask on y (given),
	 *   alpha = input  mask on x (to be enumerated).
	 *
	 * Weight is computed exactly from the integer carry-state transfer model; enumeration is strict
	 * and ordered by nondecreasing weight. No candidate cap is applied.
	 */
	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta( std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap );

	// ============================================================================
	// One-round reverse transition enumeration for NeoAlzette (linear trails)
	// ============================================================================
	//
	// We search for linear trails (mask propagation) through one "round" of the NeoAlzette core.
	// This file traverses the cipher BACKWARDS (ciphertext-side masks -> plaintext-side masks).
	//
	// -----------------------------------------------------------------------------
	// Forward round structure (encryption direction) — simplified view
	// -----------------------------------------------------------------------------
	//
	// Let (A0,B0) be the round input (two 32-bit branches). One round in `NeoAlzetteCore::forward()`
	// can be summarized (naming matches the code in src/neoalzette/neoalzette_core.cpp):
	//
	//   // First subround (near round input):
	//   B1 = B0 ⊞ (rotl(A0,31) ⊕ rotl(A0,17) ⊕ RC[0])        (var-var addition; weight from |z|)
	//   A1 = A0 ⊟ RC[1]                                       (var-const subtraction; exact weight)
	//   A2 = A1 ⊕ rotl(B1, R0)                                (linear, weight 0)
	//   B2 = B1 ⊕ rotl(A2, R1)                                (linear, weight 0)
	//   A3 = A2 ⊕ f_B(B2) ⊕ RC[4]                             (injection, quadratic; weight = rank/2)
	//
	//   // Second subround (near round output):
	//   A4 = A3 ⊞ (rotl(B2,31) ⊕ rotl(B2,17) ⊕ RC[5])        (var-var addition; weight from |z|)
	//   B3 = B2 ⊟ RC[6]                                       (var-const subtraction; exact weight)
	//   B4 = B3 ⊕ rotl(A4, R0)                                (linear, weight 0)
	//   A5 = A4 ⊕ rotl(B4, R1)                                (linear, weight 0)
	//   B5 = B4 ⊕ f_A(A5) ⊕ RC[9]                             (injection, quadratic; weight = rank/2)
	//   A_out = A5 ⊕ RC[10],  B_out = B5 ⊕ RC[11]             (XOR with constants; weight 0)
	//
	// Notes about constants (engineering reality):
	// - XOR with a constant does NOT change |correlation|, it only flips the sign. Since this best-search
	//   optimizes weight = -log2(|cor|), we ignore those XOR constants in the reverse propagation.
	// - The constants in "termA/termB" appear as XOR on the second operand before addition; that also only
	//   flips correlation sign by (-1)^{<w,const>} and does not change weight, so it is ignored here too.
	//
	// Notes about injection modeling:
	// - For a fixed output mask u on the injected XOR term, the set of *correlated* input masks is an
	//   affine subspace  v ∈ l(u) ⊕ im(S(u))  (see injection section above). We enumerate masks in that
	//   affine subspace (capped by search_configuration.maximum_injection_input_masks) and charge weight = rank(S(u))/2.


}  // namespace TwilightDream::auto_search_linear
