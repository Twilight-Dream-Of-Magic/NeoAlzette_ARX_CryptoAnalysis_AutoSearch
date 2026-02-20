#pragma once
#if !defined( TEST_NEOALZETTE_LINEAR_BEST_SEARCH_HPP )
#define TEST_NEOALZETTE_LINEAR_BEST_SEARCH_HPP

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
#include <optional>
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
#include "arx_analysis_operators/linear_correlation_addconst.hpp"
#include "arx_analysis_operators/linear_correlation_add_logn.hpp"
#include "arx_analysis_operators/modular_addition_ccz.hpp"

// self-test harness is compiled from test_arx_operator_self_test.cpp
int run_arx_operator_self_test();

namespace TwilightDream::auto_search_linear
{
	using ::TwilightDream::NeoAlzetteCore;

	// Integer weight used by this linear best-search:
	//   weight_int = ceil( -log2(|corr|) )
	// This matches the differential best-search style (integer weights), avoids float drift,
	// and preserves a safe lower-bound interpretation: |corr| >= 2^{-weight_int}.
	constexpr int INFINITE_WEIGHT = 1'000'000;

	// ============================================================================
	// Shared runtime components (moved to `common/runtime_component.*`)
	// ============================================================================

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
	using TwilightDream::runtime_component::pmr_suggest_limit_bytes;
	using TwilightDream::runtime_component::print_word32_hex;

	// ============================================================================
	// Small utilities
	// ============================================================================

	static inline unsigned parity32( std::uint32_t x ) noexcept
	{
		#if __cpp_lib_bitops >= 201907L
			std::uint32_t popcount_value = static_cast<unsigned>( std::popcount( x ) );
		#elif defined( _MSC_VER )
			std::uint32_t popcount_value = static_cast<unsigned>( __popcnt( x ) );
		#else
			std::uint32_t popcount_value = static_cast<unsigned>( __builtin_popcount( x ) );
		#endif

		return popcount_value & 1u;
	}

	// ============================================================================
	// Mask propagation for linear layers used in the cipher
	//
	// We traverse the cipher in reverse (output mask -> input mask).
	// For y = L(x), we need x_mask = L^T(y_mask).
	//
	// In the real cipher:
	//   B = l1_backward(B)
	//   A = l2_backward(A)
	// These are XOR-of-rotations linear maps, so the transpose is obtained by
	// flipping rotr <-> rotl with the same rotation constants.
	// ============================================================================

	/** Transpose transformations for linear cryptanalysis **/
	// Transpose：把所有 rotl 改成 rotr

	// ============================================================================
	// Injection model for linear correlations (quadratic -> rank/2 weight)
	//
	// The real injected term is a 32->32 nonlinear function:
	//   f_B(B) = rotl24(C(B)) ⊕ rotl16(D(B))
	//   f_A(A) = rotl24(C(A)) ⊕ rotl16(D(A))
	// from neoalzette_injection_constexpr.hpp
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

	// --------------------------------------------------------------------------
	// Exact quadratic weight for g_u(x)=<u,f(x)> via bilinear rank
	//
	// For i<j, the quadratic (bilinear) coefficient vector of f for (x_i * x_j) is:
	//   Δ2_{i,j} f = f(0) ⊕ f(e_i) ⊕ f(e_j) ⊕ f(e_i ⊕ e_j)     (vector in {0,1}^{32})
	// Then the scalar quadratic coefficient q_{i,j}(u) of g_u is:
	//   q_{i,j}(u) = parity( u & Δ2_{i,j} f )
	//
	// We build S(u) from these q_{i,j}(u) and compute rank(S(u)) over GF(2).
	// --------------------------------------------------------------------------

	static constexpr std::size_t injection_pair_count_32 = ( 32u * 31u ) / 2u;	// 496

	static consteval std::array<std::uint32_t, injection_pair_count_32> make_injection_quadratic_second_diffs_branch_b()
	{
		std::array<std::uint32_t, injection_pair_count_32> deltas {};
		std::size_t										   delta_index = 0;
		for ( int i = 0; i < 31; ++i )
		{
			const std::uint32_t ei = ( 1u << i );
			const std::uint32_t fi = injected_f_basis_branch_b[ std::size_t( i ) ];
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t ej = ( 1u << j );
				const std::uint32_t fj = injected_f_basis_branch_b[ std::size_t( j ) ];
				const std::uint32_t fij = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b( ei ^ ej );
				deltas[ delta_index++ ] = injected_f0_branch_b ^ fi ^ fj ^ fij;	// Δ2_{i,j} f
			}
		}
		return deltas;
	}

	static consteval std::array<std::uint32_t, injection_pair_count_32> make_injection_quadratic_second_diffs_branch_a()
	{
		std::array<std::uint32_t, injection_pair_count_32> deltas {};
		std::size_t										   delta_index = 0;
		for ( int i = 0; i < 31; ++i )
		{
			const std::uint32_t ei = ( 1u << i );
			const std::uint32_t fi = injected_f_basis_branch_a[ std::size_t( i ) ];
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t ej = ( 1u << j );
				const std::uint32_t fj = injected_f_basis_branch_a[ std::size_t( j ) ];
				const std::uint32_t fij = TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a( ei ^ ej );
				deltas[ delta_index++ ] = injected_f0_branch_a ^ fi ^ fj ^ fij;	// Δ2_{i,j} f
			}
		}
		return deltas;
	}

	static constexpr std::array<std::uint32_t, injection_pair_count_32> injected_quad_d2_branch_b = make_injection_quadratic_second_diffs_branch_b();
	static constexpr std::array<std::uint32_t, injection_pair_count_32> injected_quad_d2_branch_a = make_injection_quadratic_second_diffs_branch_a();

	static consteval std::array<std::array<std::uint32_t, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_b()
	{
		// rows_by_outbit[k][i] = row i (bitset) of the bilinear matrix for the single-bit mask u=(1<<k).
		std::array<std::array<std::uint32_t, 32>, 32> rows_by_outbit {};
		std::size_t									  idx = 0;
		for ( int i = 0; i < 31; ++i )
		{
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t delta = injected_quad_d2_branch_b[ idx++ ];
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

	static consteval std::array<std::array<std::uint32_t, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_a()
	{
		std::array<std::array<std::uint32_t, 32>, 32> rows_by_outbit {};
		std::size_t									  idx = 0;
		for ( int i = 0; i < 31; ++i )
		{
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t delta = injected_quad_d2_branch_a[ idx++ ];
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

	struct InjectionCorrelationTransition
	{
		// correlated input masks v form an affine subspace:
		//   v ∈ offset_mask ⊕ span(basis_vectors[0..rank-1])
		std::uint32_t				  offset_mask = 0;
		std::array<std::uint32_t, 32> basis_vectors {};
		int							  rank = 0;	   // rank(S(u))
		int							  weight = 0;  // ceil(rank/2) for weight_int semantics
	};

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_b( std::uint32_t output_mask_u ) noexcept
	{
		InjectionCorrelationTransition transition {};
		if ( output_mask_u == 0u )
			return transition;

		// offset_mask = linear coefficient vector l(u), computed exactly from g_u(e_i) ⊕ g_u(0)
		{
			const unsigned g0 = parity32( output_mask_u & injected_f0_branch_b );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_b[ std::size_t( i ) ] );
				if ( ( gi ^ g0 ) != 0u )
					offset_mask |= ( 1u << i );
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

		{
			const unsigned g0 = parity32( output_mask_u & injected_f0_branch_a );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = parity32( output_mask_u & injected_f_basis_branch_a[ std::size_t( i ) ] );
				if ( ( gi ^ g0 ) != 0u )
					offset_mask |= ( 1u << i );
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
	// Candidate generation (small, heuristic, deterministic)
	// ============================================================================

	struct AddCandidate
	{
		// Input masks for: s = x ⊞ y
		// - input_mask_x: mask on x
		// - input_mask_y: mask on y
		std::uint32_t input_mask_x = 0;
		std::uint32_t input_mask_y = 0;

		// Linear weight contribution of this modular addition (weight = -log2(|cor|)).
		// For Schulte‑Geers (Proposition 1), this equals |z|.
		int linear_weight = 0;
	};

	struct SubConstCandidate
	{
		// Input mask on x for: y = x ⊟ C
		std::uint32_t input_mask_on_x = 0;

		// Linear weight contribution (exact, via carry transfer matrix operator).
		int linear_weight = 0;
	};

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
	 *   (ACNS 2016) — Section 3.1, Proposition 1 / Eq.(1) (Schulte‑Geers explicit formula)
	 *
	 * Schulte‑Geers explicit constraints (Eq.(1)):
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
	 * - If z_i == 0, then u_i⊕v_i must be 0 AND u_i⊕w_i must be 0  =>  v_i = w_i = u_i.
	 * - If z_i == 1, the inequalities impose no restriction on (v_i, w_i) (4 choices).
	 *
	 * This function enumerates candidate pairs (v,w) in nondecreasing weight |z| and returns up to
	 * max_candidates results (0 means "no cap"). Only the absolute correlation is used here; the
	 * sign term from Proposition 1 is irrelevant for weight-based best-trail search.
	 */
	static inline std::vector<AddCandidate> generate_add_candidates_for_fixed_u( std::uint32_t output_mask_u, int weight_cap, std::size_t max_candidates )
	{
		std::vector<AddCandidate> candidates;
		const std::size_t		  candidate_limit = ( max_candidates == 0 ) ? std::numeric_limits<std::size_t>::max() : max_candidates;
		candidates.reserve( std::min<std::size_t>( 256, candidate_limit ) );

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
		const int		   z30 = u31;  // with v_31=w_31=u_31: z_30 = u_31 ⊕ v_31 ⊕ w_31 = u_31
		std::vector<Frame> stack;
		stack.reserve( 256 );

		// Enumerate in nondecreasing weight (paper-style objective is to minimize |z|).
		for ( int target_weight = 0; target_weight <= weight_cap && candidates.size() < candidate_limit; ++target_weight )
		{
			stack.clear();
			stack.push_back( Frame { 30, input_mask_x_prefix, input_mask_y_prefix, z30, 0 } );

			while ( !stack.empty() && candidates.size() < candidate_limit )
			{
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
							candidates.push_back( AddCandidate { next_input_mask_x, next_input_mask_y, z_weight } );
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

	// ============================================================================
	// "Highway-style" acceleration for var-var addition (paper-inspired Splitting‑Lookup‑Recombination)
	//
	// What the paper does (Algorithm 2 + SLR):
	// - build a large cLAT indexed by one fixed input mask v and a 1-bit "connection status",
	//   then enumerate (u,w) in increasing weight.
	//
	// What we do here (adapted to our reverse search that FIXES the output mask u):
	// - build a tiny per-byte lookup (8-bit blocks) indexed by (u_byte, connection_bit_in),
	//   then recombine blocks (MSB->LSB) to enumerate feasible (v,w) candidates in increasing weight.
	//
	// This is "Highway-like" (split + lookup + recombination + pruning by lower bounds),
	// but it is NOT the full 1.2GB cLAT from the paper.
	// ============================================================================

	#ifndef AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_HIGHWAY_SPLIT8
	#define AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_HIGHWAY_SPLIT8 1
	#endif

	class AddVarVarCandidateHighway32
	{
	public:
		static inline const std::vector<AddCandidate>& get_candidates_for_output_mask_u( std::uint32_t output_mask_u, int weight_cap_requested, std::size_t max_candidates_requested )
		{
			// Only cache the common bounded case. Unbounded enumeration (max_candidates==0) can explode.
			const bool enable_cache = ( max_candidates_requested != 0 ) && ( max_candidates_requested <= kMaxCachedCandidates );

			static thread_local CandidateCache cache;

			if ( !enable_cache )
			{
				cache.scratch_index = ( cache.scratch_index + 1u ) & 3u;
				auto& scratch = cache.scratch_ring[ std::size_t( cache.scratch_index ) ];
				scratch = generate_add_candidates_for_fixed_u( output_mask_u, weight_cap_requested, max_candidates_requested );
				return scratch;
			}

			if ( auto it = cache.by_output_mask_u.find( output_mask_u ); it != cache.by_output_mask_u.end() )
				return it->second;

			if ( cache.by_output_mask_u.size() >= kMaxCandidateCacheEntries )
				cache.by_output_mask_u.clear();

			std::vector<AddCandidate> generated;
			#if AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_HIGHWAY_SPLIT8
			generated = generate_candidates_split8_highway( output_mask_u, 31, kMaxCachedCandidates );
			#else
			generated = generate_add_candidates_for_fixed_u( output_mask_u, 31, kMaxCachedCandidates );
			#endif

			auto [ ins_it, _ ] = cache.by_output_mask_u.emplace( output_mask_u, std::move( generated ) );
			return ins_it->second;
		}

	private:
		static constexpr bool kEnableSplit8Highway = ( AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_HIGHWAY_SPLIT8 != 0 );
		static constexpr std::size_t kMaxCachedCandidates = 4096;
		static constexpr std::size_t kMaxCandidateCacheEntries = 256;
		static constexpr std::size_t kMaxBlockOptionCacheEntries = 512;

		struct BlockOption8
		{
			// Local (8-bit) input masks for: s = x ⊞ y, with fixed 8-bit output mask u_byte.
			std::uint8_t input_mask_x_byte = 0;
			std::uint8_t input_mask_y_byte = 0;
			// Connection status passed to the next (less significant) block.
			std::uint8_t next_connection_bit = 0;	// 0/1
			// Local weight contribution (sum of z bits inside this block).
			std::uint8_t block_weight = 0;  // 0..8
		};

		struct CandidateCache
		{
			std::unordered_map<std::uint32_t, std::vector<AddCandidate>> by_output_mask_u;
			std::array<std::vector<AddCandidate>, 4>					 scratch_ring {};
			std::uint32_t												 scratch_index = 0;
		};

		static inline const std::vector<BlockOption8>& get_block_options_for_u_byte( std::uint8_t u_byte, int connection_bit_in, bool exclude_top_z31_weight )
		{
			const std::uint16_t cache_key = std::uint16_t( std::uint16_t( u_byte ) << 2 ) | std::uint16_t( ( connection_bit_in & 1 ) << 1 ) | std::uint16_t( exclude_top_z31_weight ? 1 : 0 );
			static thread_local std::unordered_map<std::uint16_t, std::vector<BlockOption8>> cache;

			if ( auto it = cache.find( cache_key ); it != cache.end() )
				return it->second;

			if ( cache.size() >= kMaxBlockOptionCacheEntries )
				cache.clear();

			std::vector<BlockOption8> options;
			options.reserve( 2048 );

			struct DfsState
			{
				int			 bit_index = 7;	 // 7..0 (MSB->LSB within the byte)
				int			 z_bit = 0;
				std::uint8_t input_mask_x = 0;
				std::uint8_t input_mask_y = 0;
				int			 weight_sum = 0;
			};

			static thread_local std::vector<DfsState> stack;
			stack.clear();
			stack.reserve( 1u << 16 );  // worst-case 4^8 states
			stack.push_back( DfsState { 7, ( connection_bit_in & 1 ), 0u, 0u, 0 } );

			while ( !stack.empty() )
			{
				const DfsState st = stack.back();
				stack.pop_back();

				if ( st.bit_index < 0 )
				{
					options.push_back( BlockOption8 { st.input_mask_x, st.input_mask_y, std::uint8_t( st.z_bit & 1 ), std::uint8_t( st.weight_sum ) } );
					continue;
				}

				const int bit_index = st.bit_index;
				const int z = st.z_bit & 1;
				const int u_i = int( ( u_byte >> bit_index ) & 1u );

				// Do NOT count z31 in the global weight (z31 is fixed to 0).
				const int weight_add = ( exclude_top_z31_weight && bit_index == 7 ) ? 0 : z;
				const int next_weight_sum = st.weight_sum + weight_add;

				auto push_next = [ & ]( int v_i, int w_i ) {
					DfsState nx = st;
					nx.bit_index = bit_index - 1;
					nx.input_mask_x = std::uint8_t( nx.input_mask_x | ( std::uint8_t( ( v_i & 1 ) << bit_index ) ) );
					nx.input_mask_y = std::uint8_t( nx.input_mask_y | ( std::uint8_t( ( w_i & 1 ) << bit_index ) ) );
					// recursion: z_{i-1} = z_i ⊕ u_i ⊕ v_i ⊕ w_i
					nx.z_bit = z ^ u_i ^ ( v_i & 1 ) ^ ( w_i & 1 );
					nx.weight_sum = next_weight_sum;
					stack.push_back( nx );
				};

				if ( z == 0 )
				{
					// Forced (Schulte-Geers constraints): if z_i==0 then v_i=w_i=u_i.
					push_next( u_i, u_i );
				}
				else
				{
					push_next( 0, 0 );
					push_next( 0, 1 );
					push_next( 1, 0 );
					push_next( 1, 1 );
				}
			}

			std::sort( options.begin(), options.end(), []( const BlockOption8& a, const BlockOption8& b ) {
				if ( a.block_weight != b.block_weight )
					return a.block_weight < b.block_weight;
				if ( a.next_connection_bit != b.next_connection_bit )
					return a.next_connection_bit < b.next_connection_bit;
				if ( a.input_mask_x_byte != b.input_mask_x_byte )
					return a.input_mask_x_byte < b.input_mask_x_byte;
				return a.input_mask_y_byte < b.input_mask_y_byte;
			} );

			auto [ ins_it, _ ] = cache.emplace( cache_key, std::move( options ) );
			return ins_it->second;
		}

		static inline std::vector<AddCandidate> generate_candidates_split8_highway( std::uint32_t output_mask_u, int weight_cap, std::size_t max_candidates )
		{
			std::vector<AddCandidate> candidates;
			if ( weight_cap < 0 )
				return candidates;
			weight_cap = std::clamp( weight_cap, 0, 31 );
			if ( max_candidates == 0 )
			{
				// Unbounded enumeration can explode; use the reference 32-bit enumerator.
				return generate_add_candidates_for_fixed_u( output_mask_u, weight_cap, max_candidates );
			}

			const std::size_t candidate_limit = max_candidates;
			candidates.reserve( std::min<std::size_t>( 512, candidate_limit ) );

			const std::uint8_t u_bytes[ 4 ] = { std::uint8_t( output_mask_u >> 24 ), std::uint8_t( output_mask_u >> 16 ), std::uint8_t( output_mask_u >> 8 ), std::uint8_t( output_mask_u ) };

			// Lower bound DP: minimum achievable remaining weight from (block_index, connection_bit_in).
			int min_remaining_weight[ 5 ][ 2 ];
			min_remaining_weight[ 4 ][ 0 ] = 0;
			min_remaining_weight[ 4 ][ 1 ] = 0;

			for ( int block_index = 3; block_index >= 0; --block_index )
			{
				for ( int connection_in = 0; connection_in <= 1; ++connection_in )
				{
					int best = 1'000'000;
					const bool top_block = ( block_index == 0 );
					const auto& block_options = get_block_options_for_u_byte( u_bytes[ block_index ], connection_in, top_block );
					for ( const auto& opt : block_options )
					{
						const int tail = ( block_index == 3 ) ? 0 : min_remaining_weight[ block_index + 1 ][ int( opt.next_connection_bit & 1u ) ];
						best = std::min( best, int( opt.block_weight ) + tail );
						if ( best == 0 )
							break;
					}
					min_remaining_weight[ block_index ][ connection_in ] = best;
				}
			}

			const auto enumerate = [ & ]( auto&& self,
										  int		 target_weight,
										  int		 block_index,
										  int		 connection_in,
										  std::uint32_t input_mask_x_acc,
										  std::uint32_t input_mask_y_acc,
										  int		 remaining_weight ) -> void {
				if ( candidates.size() >= candidate_limit )
					return;
				if ( remaining_weight < 0 )
					return;
				if ( min_remaining_weight[ block_index ][ connection_in ] > remaining_weight )
					return;

				if ( block_index == 4 )
				{
					if ( remaining_weight == 0 )
						candidates.push_back( AddCandidate { input_mask_x_acc, input_mask_y_acc, target_weight } );
					return;
				}

				const bool top_block = ( block_index == 0 );
				const auto& block_options = get_block_options_for_u_byte( u_bytes[ block_index ], connection_in, top_block );
				const int   shift = ( 3 - block_index ) * 8;

				for ( const auto& opt : block_options )
				{
					const int local_w = int( opt.block_weight );
					if ( local_w > remaining_weight )
						break;	// sorted by weight
					const int next_remaining = remaining_weight - local_w;
					const int next_connection = int( opt.next_connection_bit & 1u );
					if ( block_index != 3 && min_remaining_weight[ block_index + 1 ][ next_connection ] > next_remaining )
						continue;

					const std::uint32_t x2 = input_mask_x_acc | ( std::uint32_t( opt.input_mask_x_byte ) << shift );
					const std::uint32_t y2 = input_mask_y_acc | ( std::uint32_t( opt.input_mask_y_byte ) << shift );
					self( self, target_weight, block_index + 1, next_connection, x2, y2, next_remaining );
					if ( candidates.size() >= candidate_limit )
						return;
				}
			};

			for ( int target = 0; target <= weight_cap && candidates.size() < candidate_limit; ++target )
			{
				if ( min_remaining_weight[ 0 ][ 0 ] > target )
					continue;
				enumerate( enumerate, target, 0, 0, 0u, 0u, target );
			}

			std::sort( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) {
				if ( a.linear_weight != b.linear_weight )
					return a.linear_weight < b.linear_weight;
				if ( a.input_mask_x != b.input_mask_x )
					return a.input_mask_x < b.input_mask_x;
				return a.input_mask_y < b.input_mask_y;
			} );
			candidates.erase( std::unique( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) { return a.input_mask_x == b.input_mask_x && a.input_mask_y == b.input_mask_y; } ), candidates.end() );
			if ( candidates.size() > candidate_limit )
				candidates.resize( candidate_limit );
			return candidates;
		}
	};

	/**
	 * @brief Heuristic enumeration of input masks for a fixed var-const subtraction mask.
	 *
	 * We analyze:
	 *   y = x ⊟ C   (mod 2^32)
	 * with:
	 *   beta  = output mask on y (given),
	 *   alpha = input  mask on x (to be enumerated).
	 *
	 * Weight is computed by the exact per-bit 2×2 carry transfer operator
	 * (`linear_x_modulo_minus_const32`). This helper only proposes a small set of plausible alpha
	 * candidates and keeps the best few (by weight) to limit branching.
	 */
	static inline std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta( std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap, std::size_t max_candidates )
	{
		std::vector<SubConstCandidate> candidates;
		candidates.reserve( std::max<std::size_t>( 8, max_candidates ) );
		weight_cap = std::clamp( weight_cap, 0, 32 );

		const auto try_add_candidate = [ & ]( std::uint32_t candidate_input_mask_on_x ) {
			const auto wopt = weight_sub_const_ceil_int( candidate_input_mask_on_x, constant, output_mask_beta );
			if ( !wopt.has_value() )
				return;
			if ( wopt.value() > weight_cap )
				return;
			candidates.push_back( SubConstCandidate { candidate_input_mask_on_x, wopt.value() } );
		};

		// Primary: identity masks
		try_add_candidate( output_mask_beta );
		try_add_candidate( 0u );
		try_add_candidate( 0xFFFFFFFFu );

		// Small neighborhood
		for ( int bit = 0; bit < 12; ++bit )
			try_add_candidate( output_mask_beta ^ ( 1u << bit ) );

		std::sort( candidates.begin(), candidates.end(), []( const SubConstCandidate& a, const SubConstCandidate& b ) {
			if ( a.linear_weight != b.linear_weight )
				return a.linear_weight < b.linear_weight;
			return a.input_mask_on_x < b.input_mask_on_x;
		} );
		candidates.erase( std::unique( candidates.begin(), candidates.end(), []( const SubConstCandidate& a, const SubConstCandidate& b ) { return a.input_mask_on_x == b.input_mask_on_x; } ), candidates.end() );
		if ( max_candidates != 0 && candidates.size() > max_candidates )
			candidates.resize( max_candidates );
		return candidates;
	}

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
	//   B3 = l1_backward(B2)                                  (linear, weight 0)
	//
	//   // Second subround (near round output):
	//   A4 = A3 ⊞ (rotl(B3,31) ⊕ rotl(B3,17) ⊕ RC[5])        (var-var addition; weight from |z|)
	//   B4 = B3 ⊟ RC[6]                                       (var-const subtraction; exact weight)
	//   B5 = B4 ⊕ rotl(A4, R0)                                (linear, weight 0)
	//   A5 = A4 ⊕ rotl(B5, R1)                                (linear, weight 0)
	//   B6 = B5 ⊕ f_A(A5) ⊕ RC[9]                             (injection, quadratic; weight = rank/2)
	//   A6 = l2_backward(A5)                                  (linear, weight 0)
	//   A_out = A6 ⊕ RC[10],  B_out = B6 ⊕ RC[11]             (XOR with constants; weight 0)
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

	struct LinearTrailStepRecord
	{
		// Human-friendly round number (1..round_count).
		// NOTE: The internal search traverses backwards, but we still label rounds in forward order.
		int round_index = 0;

		// Round boundary masks (output -> input, since we traverse backwards)
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;

		// Gate decisions (reverse direction):
		// For each nonlinear gate we store:
		// - the fixed output mask (given by propagation),
		// - one chosen input mask assignment (picked by candidate enumeration),
		// - the local weight contribution.

		// ---------------------------
		// Second subround (near output)
		// ---------------------------

		// Constant subtraction on branch B (second subround):
		//   B4 = B3 ⊟ RC[6]
		std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;  // mask on B4
		std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;  // chosen mask on B3
		int			  weight_second_constant_subtraction = 0;

		// Var-var add on branch A:
		//   A4 = A3 ⊞ termB(B3),   termB(B3) = rotl(B3,31) ⊕ rotl(B3,17) ⊕ RC[5]
		// (RC[5] is ignored for weight; see notes above.)
		std::uint32_t output_branch_a_mask_after_second_addition = 0;  // mask on A4
		std::uint32_t input_branch_a_mask_before_second_addition = 0;  // chosen mask on A3 (the "x" operand)
		std::uint32_t second_addition_term_mask_from_branch_b = 0;	   // chosen mask on termB(B3) (the "y" operand)
		int			  weight_second_addition = 0;

		// Injection weights (quadratic exact rank/2 model for the injected PRF term)
		// - Injection A -> B: B6 = B5 ⊕ f_A(A5) ⊕ RC[9]
		// - Injection B -> A: A3 = A2 ⊕ f_B(B2) ⊕ RC[4]
		int weight_injection_from_branch_a = 0;
		int weight_injection_from_branch_b = 0;

		// Chosen correlated input masks for the injection gates (one element of the affine subspace)
		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;	 // chosen correlated input mask on A5 (A->B injection)
		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;	 // chosen correlated input mask on B2 (B->A injection)

		// ---------------------------
		// First subround (near input)
		// ---------------------------

		// Constant subtraction on branch A (first subround):
		//   A1 = A0 ⊟ RC[1]
		std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;  // mask on A1
		std::uint32_t input_branch_a_mask_before_first_constant_subtraction = 0;  // chosen mask on A0
		int			  weight_first_constant_subtraction = 0;

		// Var-var add on branch B:
		//   B1 = B0 ⊞ termA(A0),   termA(A0) = rotl(A0,31) ⊕ rotl(A0,17) ⊕ RC[0]
		std::uint32_t output_branch_b_mask_after_first_addition = 0;  // mask on B1
		std::uint32_t input_branch_b_mask_before_first_addition = 0;  // chosen mask on B0 (the "x" operand)
		std::uint32_t first_addition_term_mask_from_branch_a = 0;	  // chosen mask on termA(A0) (the "y" operand)
		int			  weight_first_addition = 0;

		// Total weight contribution of this round step (sum of the six local weights above).
		int round_weight = 0;
	};

	struct LinearBestSearchConfiguration
	{
		int			  round_count = 1;
		int			  addition_weight_cap = 31;			   // per modular-addition (var-var) weight cap (0..31). 31 = no extra cap for 32-bit
		int			  constant_subtraction_weight_cap = 32;  // per sub-const weight cap (0..32). 32 = no extra cap.
		std::size_t	  maximum_addition_candidates = 4096;  // cap enumerated (v,w) pairs per addition (0 = no cap)
		std::size_t	  maximum_constant_subtraction_candidates = 8;
		std::size_t	  maximum_injection_input_masks = 64;  // cap enumerated masks per injection affine subspace (0 = enumerate all; may explode)
		std::size_t	  maximum_round_predecessors = 256;	   // cap the number of generated predecessor states per round boundary
		std::uint64_t maximum_search_nodes = 1000000;
		std::uint64_t maximum_search_seconds = 0;		// 0 = unlimited
		int			  target_best_weight = -1;		// if >=0: stop early once best_weight <= target
		bool		  enable_state_memoization = true;	// prune revisits: (depth, mask_a, mask_b) -> best weight so far (best-effort on OOM)
		// `remaining_round_min_weight[k]` must be a LOWER bound on the minimal possible weight of ANY k-round continuation.
		// This is the "Bcr-k" style bound used in Matsui-style pruning.
		bool		  enable_remaining_round_lower_bound = false;
		std::vector<int> remaining_round_min_weight {};	// index: rounds_left (0..round_count). Must satisfy remaining_round_min_weight[0]==0.
		// Auto-generate remaining_round_min_weight (best-effort). If enabled and the table is empty,
		// we run short searches for k=1..round_count. Bounds are only strict if those searches are exhaustive.
		bool		  auto_generate_remaining_round_lower_bound = false;
		std::uint64_t remaining_round_lower_bound_generation_nodes = 0;	   // 0 = reuse maximum_search_nodes
		std::uint64_t remaining_round_lower_bound_generation_seconds = 0;  // 0 = reuse maximum_search_seconds
		// If true, disable remaining-round pruning when the auto-generated table is not guaranteed strict.
		bool		  strict_remaining_round_lower_bound = true;
		bool		  enable_verbose_output = false;
	};

	struct MatsuiSearchRunLinearResult
	{
		bool							   found = false;
		int								   best_weight = INFINITE_WEIGHT;
		std::uint64_t					   nodes_visited = 0;
		bool							   hit_maximum_search_nodes = false;
		bool							   hit_time_limit = false;
		bool							   hit_target_best_weight = false;
		std::uint32_t					   best_input_branch_a_mask = 0;
		std::uint32_t					   best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail;  // in reverse order (last round -> first round)
	};

	struct BestWeightCheckpointWriter;	// forward

	struct LinearBestSearchContext
	{
		LinearBestSearchConfiguration configuration;
		std::uint32_t				  start_output_branch_a_mask = 0;
		std::uint32_t				  start_output_branch_b_mask = 0;

		std::uint64_t					   visited_node_count = 0;
		int								   best_weight = INFINITE_WEIGHT;
		std::uint32_t					   best_input_branch_a_mask = 0;
		std::uint32_t					   best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail;	  // in reverse order (last round -> first round)
		std::vector<LinearTrailStepRecord> current_linear_trail;  // working stack (same order as best_linear_trail)

		// Best-effort memoization: prune revisits at round boundaries.
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int> memoization;

		// Run start time (used for progress/checkpoints even when maximum_search_seconds==0).
		std::chrono::steady_clock::time_point run_start_time {};

		// Single-run progress reporting (optional; auto deep uses this).
		std::uint64_t						  progress_every_seconds = 0;				// 0 = disable
		std::uint64_t						  progress_node_mask = ( 1ull << 18 ) - 1;	// check clock every ~262k nodes to reduce overhead
		std::chrono::steady_clock::time_point progress_start_time {};
		std::chrono::steady_clock::time_point progress_last_print_time {};
		std::uint64_t						  progress_last_print_nodes = 0;
		bool								  progress_print_masks = false;  // if enabled, print (maskA,maskB) snapshots on progress lines

		// Time limit (single-run). Implemented as a global stop flag to avoid deep unwinding costs.
		std::chrono::steady_clock::time_point time_limit_start_time {};
		bool								  stop_due_to_time_limit = false;
		bool								  stop_due_to_target = false;

		// Optional: checkpoint writer (e.g., auto mode) to persist best-weight changes.
		BestWeightCheckpointWriter* checkpoint = nullptr;
	};

	struct BestWeightCheckpointWriter
	{
		std::ofstream out {};
		int			  last_written_weight = INFINITE_WEIGHT;

		static std::string default_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b )
		{
			std::ostringstream oss;
			oss << "auto_checkpoint_linear_R" << round_count << "_MaskA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_a << "_MaskB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_b << std::dec << ".log";
			return oss.str();
		}

		bool open_append( const std::string& path )
		{
			out.open( path, std::ios::out | std::ios::app );
			return bool( out );
		}

		void maybe_write( const LinearBestSearchContext& context, const char* reason )
		{
			if ( !out )
				return;
			if ( context.best_weight >= INFINITE_WEIGHT )
				return;
			if ( last_written_weight < INFINITE_WEIGHT && context.best_weight == last_written_weight )
				return;

			const auto	 now = std::chrono::steady_clock::now();
			const double elapsed_sec = ( context.run_start_time.time_since_epoch().count() == 0 ) ? 0.0 : std::chrono::duration<double>( now - context.run_start_time ).count();

			out << "=== checkpoint ===\n";
			out << "timestamp_local=" << format_local_time_now() << "\n";
			out << "reason=" << ( reason ? reason : "best_changed" ) << "\n";
			out << "rounds=" << context.configuration.round_count << "\n";
			out << "start_mask_a=" << hex8( context.start_output_branch_a_mask ) << "\n";
			out << "start_mask_b=" << hex8( context.start_output_branch_b_mask ) << "\n";
			out << "best_weight=" << context.best_weight << "\n";
			out << "nodes_visited=" << context.visited_node_count << "\n";
			out << "elapsed_sec=" << std::fixed << std::setprecision( 3 ) << elapsed_sec << "\n";
			out << "best_input_mask_a=" << hex8( context.best_input_branch_a_mask ) << "\n";
			out << "best_input_mask_b=" << hex8( context.best_input_branch_b_mask ) << "\n";
			out << "trail_steps=" << context.best_linear_trail.size() << "\n";
			for ( const auto& s : context.best_linear_trail )
			{
				out << "R" << s.round_index << " round_w=" << s.round_weight << " out_MaskA=" << hex8( s.output_branch_a_mask ) << " out_MaskB=" << hex8( s.output_branch_b_mask ) << " in_MaskA=" << hex8( s.input_branch_a_mask ) << " in_MaskB=" << hex8( s.input_branch_b_mask ) << "\n";
			}
			out << "\n";
			out.flush();
			last_written_weight = context.best_weight;
		}
	};

	static inline void maybe_print_single_run_progress( LinearBestSearchContext& ctx, int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask )
	{
		if ( ctx.progress_every_seconds == 0 )
			return;
		if ( ( ctx.visited_node_count & ctx.progress_node_mask ) != 0 )
			return;

		const auto now = std::chrono::steady_clock::now();
		memory_governor_poll_if_needed( now );
		if ( ctx.progress_last_print_time.time_since_epoch().count() != 0 )
		{
			const double since_last = std::chrono::duration<double>( now - ctx.progress_last_print_time ).count();
			if ( since_last < double( ctx.progress_every_seconds ) )
				return;
		}

		const double		elapsed = std::chrono::duration<double>( now - ctx.progress_start_time ).count();
		const double		window = ( ctx.progress_last_print_time.time_since_epoch().count() == 0 ) ? elapsed : std::chrono::duration<double>( now - ctx.progress_last_print_time ).count();
		const std::uint64_t delta_nodes = ( ctx.visited_node_count >= ctx.progress_last_print_nodes ) ? ( ctx.visited_node_count - ctx.progress_last_print_nodes ) : 0;
		const double		rate = ( window > 1e-9 ) ? ( double( delta_nodes ) / window ) : 0.0;

		std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );

		// Save/restore formatting (avoid perturbing later prints).
		const auto old_flags = std::cout.flags();
		const auto old_prec = std::cout.precision();
		const auto old_fill = std::cout.fill();

		TwilightDream::runtime_component::print_progress_prefix( std::cout );
		std::cout << "[Progress] nodes=" << ctx.visited_node_count << "  nodes_per_sec=" << std::fixed << std::setprecision( 2 ) << rate << "  elapsed_sec=" << std::fixed << std::setprecision( 2 ) << elapsed << "  best_weight=" << ( ( ctx.best_weight >= INFINITE_WEIGHT ) ? -1 : ctx.best_weight );
		std::cout << "  depth=" << depth << "/" << ctx.configuration.round_count;
		if ( ctx.progress_print_masks )
		{
			std::cout << "  ";
			print_word32_hex( "start_mask_a=", ctx.start_output_branch_a_mask );
			std::cout << " ";
			print_word32_hex( "start_mask_b=", ctx.start_output_branch_b_mask );
			std::cout << " ";
			print_word32_hex( "cur_mask_a=", current_round_output_branch_a_mask );
			std::cout << " ";
			print_word32_hex( "cur_mask_b=", current_round_output_branch_b_mask );
		}
		std::cout << "\n";

		std::cout.flags( old_flags );
		std::cout.precision( old_prec );
		std::cout.fill( old_fill );

		ctx.progress_last_print_time = now;
		ctx.progress_last_print_nodes = ctx.visited_node_count;
	}

	template <class OnInputMaskFn>
	static inline void enumerate_affine_subspace_input_masks( LinearBestSearchContext& context, const InjectionCorrelationTransition& transition, std::size_t maximum_input_mask_count, OnInputMaskFn&& on_input_mask )
	{
		const auto&   search_configuration = context.configuration;
		std::uint64_t& inout_node_budget = context.visited_node_count;

		// Enumerate all elements of an affine subspace over GF(2):
		//
		//   V = offset_mask ⊕ span( basis_vectors[0..rank-1] )
		//
		// i.e. every element is:
		//   v = offset_mask ⊕ (⊕_{j in S} basis_vectors[j])   for some subset S.
		//
		// This is exactly the mask-set for injection correlations:
		//   v ∈ l(u) ⊕ im(S(u))  (see injection section), where basis_vectors is a basis of im(S(u)).
		//
		// WARNING: |V| = 2^{rank}. This can explode, so we cap enumeration via maximum_input_mask_count and
		// also honor the global node budget (search_configuration.maximum_search_nodes).
		std::size_t produced_count = 0;
		if ( transition.rank <= 0 )
		{
			if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
				return;
			++inout_node_budget;
			on_input_mask( transition.offset_mask );
			return;
		}

		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_mask = 0;
			std::uint8_t  branch_state = 0;	 // 0=enter, 1=after "exclude" branch (about to do "include")
		};

		std::array<Frame, 64> stack {};
		int					  stack_step = 0;
		stack[ stack_step++ ] = Frame { 0, transition.offset_mask, 0 };

		while ( stack_step > 0 )
		{
			if ( maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count )
				return;
			if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
				return;

			Frame& frame = stack[ stack_step - 1 ];
			if ( frame.branch_state == 0 )
			{
				if ( frame.basis_index >= transition.rank )
				{
					if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
						return;
					++inout_node_budget;
					on_input_mask( frame.current_mask );
					++produced_count;
					--stack_step;
					continue;
				}

				// Branch 0: exclude basis_vectors[basis_index]
				frame.branch_state = 1;
				stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_mask, 0 };
				continue;
			}

			// Branch 1: include basis_vectors[basis_index]
			const std::uint32_t next_mask = frame.current_mask ^ transition.basis_vectors[ std::size_t( frame.basis_index ) ];
			const int			next_index = frame.basis_index + 1;
			--stack_step;
			stack[ stack_step++ ] = Frame { next_index, next_mask, 0 };
		}
	}

	// ARX Automatic Search Frame - Linear Analysis Paper:
	// [eprint-iacr-org-2019-1319] Automatic Search for the Linear (hull) Characteristics of ARX Ciphers - Applied to SPECK, SPARX, Chaskey and CHAM-64
	// Is applied to NeoAlzette ARX-Box Algorithm every step of the round
	class LinearBestTrailSearcher final
	{
	public:

		// Linear round model (reverse, mask propagation):
		//   We traverse masks backward from (A_out,B_out) to (A_in,B_in).
		//   For y = L(x):         x_mask = L^T(y_mask).
		//   For y = x XOR rotl(z,r):
		//     mask(x) ^= mask(y),  mask(z) ^= rotr(mask(y), r).
		//   For modular addition s = x ⊞ y with output mask u:
		//     |corr(u,v,w)| = 2^{-|z|}  (Schulte-Geers), so weight = |z|.
		//   For injection f: for fixed output mask u,
		//     correlated input masks v ∈ l(u) ⊕ im(S(u)), and weight = rank(S(u)) / 2.
		//
		// Engineering:
		//   DFS over round boundaries with Matsui-style pruning:
		//     accumulated_weight + round_weight < best_weight.
		//   Candidate counts are capped, node budget is enforced, and we keep top-k
		//   predecessors per round for deterministic branching control.
		explicit LinearBestTrailSearcher( LinearBestSearchContext& context_in )
			: context( context_in ), search_configuration( context_in.configuration )
		{
		}

		// Entry: start from round output masks and walk backward.
		void search_from_start( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask )
		{
			round_state_stack.clear();
			const int reserve_rounds = std::max( 1, search_configuration.round_count );
			round_state_stack.reserve( std::size_t( reserve_rounds ) + 1u );
			search_recursive( 0, output_branch_a_mask, output_branch_b_mask, 0 );
		}

	private:
		// Per-round shared state for nested enumeration callbacks.
		// Engineering: every lambda captures only `this`, all shared variables live here.
		struct RoundSearchState
		{
			int round_boundary_depth = 0;
			int accumulated_weight_so_far = 0;
			int round_index = 0;
			int round_weight_cap = 0;
			int remaining_round_weight_lower_bound_after_this_round = 0;

			std::uint32_t round_output_branch_a_mask = 0;
			std::uint32_t round_output_branch_b_mask = 0;

			std::uint32_t branch_a_mask_before_linear_layer_two_backward = 0;
			std::uint32_t branch_b_mask_before_injection_from_branch_a = 0;

			InjectionCorrelationTransition injection_from_branch_a_transition {};
			int							   weight_injection_from_branch_a = 0;
			int							   remaining_after_inj_a = 0;
			int							   second_subconst_weight_cap = 0;
			int							   second_add_weight_cap = 0;

			std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;

			std::uint32_t output_branch_a_mask_after_second_addition = 0;
			std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;

			std::vector<SubConstCandidate>	  second_constant_subtraction_candidates_for_branch_b;
			const std::vector<AddCandidate>* second_addition_candidates_for_branch_a = nullptr;

			std::uint32_t input_branch_a_mask_before_second_addition = 0;
			std::uint32_t second_addition_term_mask_from_branch_b = 0;
			int			  weight_second_addition = 0;
			std::uint32_t branch_b_mask_contribution_from_second_addition_term = 0;

			InjectionCorrelationTransition injection_from_branch_b_transition {};
			int							   weight_injection_from_branch_b = 0;
			int							   base_weight_after_inj_b = 0;

			std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;

			std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;
			int			  weight_second_constant_subtraction = 0;
			std::uint32_t branch_b_mask_after_linear_layer_one_backward = 0;
			std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a_base = 0;

			int base_weight_after_second_subconst = 0;
			int first_subconst_weight_cap = 0;
			int first_add_weight_cap = 0;

			std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;
			std::uint32_t output_branch_b_mask_after_first_addition = 0;

			std::vector<LinearTrailStepRecord> round_predecessors;
		};

		LinearBestSearchContext&			   context;
		const LinearBestSearchConfiguration& search_configuration;
		std::vector<RoundSearchState>		   round_state_stack;

		RoundSearchState& current_round_state()
		{
			return round_state_stack.back();
		}

		const RoundSearchState& current_round_state() const
		{
			return round_state_stack.back();
		}

		// DFS at a round boundary (A_out,B_out). Split into stages to keep the recursion short.
		void search_recursive( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( should_stop_search( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_weight ) )
				return;
			if ( handle_round_end_if_needed( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_weight ) )
				return;
			if ( should_prune_state_memoization( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_weight ) )
				return;

			round_state_stack.emplace_back();
			if ( !prepare_round_state( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_weight ) )
			{
				round_state_stack.pop_back();
				return;
			}

			enumerate_round_predecessors();
			recurse_over_round_predecessors();

			round_state_stack.pop_back();
		}

		// Global stop conditions, node/time budget, and trivial weight pruning.
		bool should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( context.stop_due_to_time_limit || context.stop_due_to_target )
				return true;

			if ( search_configuration.maximum_search_nodes != 0 && context.visited_node_count >= search_configuration.maximum_search_nodes )
				return true;

			++context.visited_node_count;

			// Governor hook: very low overhead (one clock read every ~262k nodes).
			if ( ( context.visited_node_count & context.progress_node_mask ) == 0 )
			{
				memory_governor_poll_if_needed( std::chrono::steady_clock::now() );
			}

			// Time limit check (low overhead): evaluate clock only every ~262k nodes.
			if ( search_configuration.maximum_search_seconds != 0 )
			{
				if ( ( context.visited_node_count & context.progress_node_mask ) == 0 )
				{
					const auto now = std::chrono::steady_clock::now();
					memory_governor_poll_if_needed( now );
					const double elapsed = std::chrono::duration<double>( now - context.time_limit_start_time ).count();
					if ( elapsed >= double( search_configuration.maximum_search_seconds ) )
					{
						context.stop_due_to_time_limit = true;
						return true;
					}
				}
			}

			maybe_print_single_run_progress( context, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask );

			if ( accumulated_weight >= context.best_weight )
				return true;

			if ( should_prune_remaining_round_lower_bound( depth, accumulated_weight ) )
				return true;

			return false;
		}

		// Remaining-round lower bound pruning (Matsui-style).
		bool should_prune_remaining_round_lower_bound( int depth, int accumulated_weight ) const
		{
			if ( context.best_weight >= INFINITE_WEIGHT )
				return false;
			if ( !search_configuration.enable_remaining_round_lower_bound )
				return false;

			const int rounds_left = search_configuration.round_count - depth;
			if ( rounds_left < 0 )
				return false;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t table_index = std::size_t( rounds_left );
			if ( table_index >= remaining_round_min_weight_table.size() )
				return false;
			const int weight_lower_bound = remaining_round_min_weight_table[ table_index ];
			return accumulated_weight + weight_lower_bound >= context.best_weight;
		}

		// Reached final depth: update best trail (reverse order).
		bool handle_round_end_if_needed( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( depth != search_configuration.round_count )
				return false;

			context.best_weight = accumulated_weight;
			context.best_linear_trail = context.current_linear_trail;
			context.best_input_branch_a_mask = current_round_output_branch_a_mask;
			context.best_input_branch_b_mask = current_round_output_branch_b_mask;
			if ( context.checkpoint )
			{
				context.checkpoint->maybe_write( context, "improved" );
			}
			if ( search_configuration.target_best_weight >= 0 && context.best_weight <= search_configuration.target_best_weight )
			{
				context.stop_due_to_target = true;
			}
			return true;
		}

		// Memoization: prune revisits at round boundaries.
		bool should_prune_state_memoization( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( !search_configuration.enable_state_memoization )
				return false;

			// revisits pruning (state at round boundary)
			const std::size_t rc = std::size_t( std::max( 1, search_configuration.round_count ) );
			std::size_t		  hint = ( rc == 0 ) ? 0 : std::size_t( search_configuration.maximum_search_nodes / rc );
			hint = std::clamp<std::size_t>( hint, 4096u, 1'000'000u );

			const std::uint64_t key = ( std::uint64_t( current_round_output_branch_a_mask ) << 32 ) | std::uint64_t( current_round_output_branch_b_mask );
			return context.memoization.should_prune_and_update( std::size_t( depth ), key, accumulated_weight, true, true, hint, 192ull, "linear_memo.reserve", "linear_memo.try_emplace" );
		}

		// Prepare per-round fixed terms and caps (reverse direction).
		// Math: round_weight_cap = best_weight - accumulated_weight (exclusive).
		bool prepare_round_state( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			auto& state = current_round_state();
			state.round_boundary_depth = depth;
			state.accumulated_weight_so_far = accumulated_weight;
			state.round_index = search_configuration.round_count - depth;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( depth );
			const int base_cap = ( context.best_weight >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : ( context.best_weight - accumulated_weight );
			state.round_weight_cap = ( base_cap >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : ( base_cap - state.remaining_round_weight_lower_bound_after_this_round );
			if ( state.round_weight_cap <= 0 )
				return false;

			state.round_output_branch_a_mask = current_round_output_branch_a_mask;
			state.round_output_branch_b_mask = current_round_output_branch_b_mask;

			// Linear layer on branch A removed (treat as identity in reverse propagation)
			state.branch_a_mask_before_linear_layer_two_backward = current_round_output_branch_a_mask;
			state.branch_b_mask_before_injection_from_branch_a = current_round_output_branch_b_mask;

			state.injection_from_branch_a_transition = compute_injection_transition_from_branch_a( current_round_output_branch_b_mask );
			state.weight_injection_from_branch_a = state.injection_from_branch_a_transition.weight;
			if ( state.weight_injection_from_branch_a >= state.round_weight_cap )
				return false;

			state.remaining_after_inj_a = state.round_weight_cap - state.weight_injection_from_branch_a;
			if ( state.remaining_after_inj_a <= 0 )
				return false;

			state.second_subconst_weight_cap = std::min( search_configuration.constant_subtraction_weight_cap, state.remaining_after_inj_a - 1 );
			state.second_add_weight_cap = std::min( search_configuration.addition_weight_cap, state.remaining_after_inj_a - 1 );
			if ( state.second_add_weight_cap < 0 )
				return false;

			state.second_addition_candidates_for_branch_a = nullptr;
			reset_round_predecessor_buffer();

			return true;
		}

		int compute_remaining_round_weight_lower_bound_after_this_round( int depth ) const
		{
			if ( !search_configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left_after = search_configuration.round_count - ( depth + 1 );
			if ( rounds_left_after < 0 )
				return 0;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t( rounds_left_after );
			if ( idx >= remaining_round_min_weight_table.size() )
				return 0;
			return std::max( 0, remaining_round_min_weight_table[ idx ] );
		}

		void reset_round_predecessor_buffer()
		{
			auto& state = current_round_state();
			state.round_predecessors.clear();
			state.round_predecessors.reserve( std::min<std::size_t>( search_configuration.maximum_round_predecessors ? search_configuration.maximum_round_predecessors : 256, 4096 ) );
		}

		void trim_round_predecessors( bool force )
		{
			if ( search_configuration.maximum_round_predecessors == 0 )
				return;

			auto& state = current_round_state();
			const std::size_t cap = search_configuration.maximum_round_predecessors;
			if ( cap == 0 )
				return;
			const std::size_t threshold = std::min<std::size_t>( cap * 8u, 16'384u );
			if ( !force && state.round_predecessors.size() <= threshold )
				return;

			std::sort( state.round_predecessors.begin(), state.round_predecessors.end(), []( const LinearTrailStepRecord& a, const LinearTrailStepRecord& b ) {
				if ( a.round_weight != b.round_weight )
					return a.round_weight < b.round_weight;
				if ( a.input_branch_a_mask != b.input_branch_a_mask )
					return a.input_branch_a_mask < b.input_branch_a_mask;
				return a.input_branch_b_mask < b.input_branch_b_mask;
			} );
			state.round_predecessors.erase( std::unique( state.round_predecessors.begin(), state.round_predecessors.end(), []( const LinearTrailStepRecord& a, const LinearTrailStepRecord& b ) {
				return a.input_branch_a_mask == b.input_branch_a_mask && a.input_branch_b_mask == b.input_branch_b_mask;
			} ), state.round_predecessors.end() );
			if ( state.round_predecessors.size() > cap )
				state.round_predecessors.resize( cap );
		}

		// Enumerate all predecessors for the current round and keep top-k.
		void enumerate_round_predecessors()
		{
			enumerate_injection_from_branch_a_masks();
			trim_round_predecessors( true );
		}

		// Injection A -> B (reverse): correlated A5 masks form an affine subspace.
		// v ∈ l(u) ⊕ im(S(u)), weight = rank(S(u))/2.
		void enumerate_injection_from_branch_a_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_a_transition,
				search_configuration.maximum_injection_input_masks,
				[ this ]( std::uint32_t m ) { handle_injection_from_branch_a_mask( m ); } );
		}

		// Reverse XOR/ROT pair (second subround) then branch on ADD2/SUB2 candidates.
		void handle_injection_from_branch_a_mask( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a )
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_correlated_input_mask_for_injection_from_branch_a;

			const std::uint32_t branch_a_mask_before_linear_layer_two_backward_with_injection_choice =
				state.branch_a_mask_before_linear_layer_two_backward ^ chosen_correlated_input_mask_for_injection_from_branch_a;
			const std::uint32_t branch_b_mask_before_injection_from_branch_a_for_this_choice = state.branch_b_mask_before_injection_from_branch_a;

			state.output_branch_a_mask_after_second_addition = branch_a_mask_before_linear_layer_two_backward_with_injection_choice;
			const std::uint32_t branch_b_mask_after_second_xor_with_rotated_branch_a =
				branch_b_mask_before_injection_from_branch_a_for_this_choice ^ NeoAlzetteCore::rotr( branch_a_mask_before_linear_layer_two_backward_with_injection_choice, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
			state.output_branch_b_mask_after_second_constant_subtraction = branch_b_mask_after_second_xor_with_rotated_branch_a;
			state.output_branch_a_mask_after_second_addition ^=
				NeoAlzetteCore::rotr( branch_b_mask_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R0 );

			state.second_constant_subtraction_candidates_for_branch_b =
				generate_subconst_candidates_for_fixed_beta(
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					state.second_subconst_weight_cap,
					search_configuration.maximum_constant_subtraction_candidates );
			state.second_addition_candidates_for_branch_a =
				&AddVarVarCandidateHighway32::get_candidates_for_output_mask_u(
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap,
					search_configuration.maximum_addition_candidates );

			enumerate_second_addition_candidates();
		}

		// ADD2: A4 = A3 ⊞ termB(B3). Enumerate (maskA3, maskTermB) candidates in increasing weight.
		void enumerate_second_addition_candidates()
		{
			auto& state = current_round_state();
			if ( !state.second_addition_candidates_for_branch_a )
				return;

			std::size_t second_addition_candidate_count = 0;
			std::uint64_t& inout_node_budget = context.visited_node_count;

			for ( const auto& second_addition_candidate_for_branch_a : *state.second_addition_candidates_for_branch_a )
			{
				if ( second_addition_candidate_for_branch_a.linear_weight > state.second_add_weight_cap )
					break;
				if ( search_configuration.maximum_addition_candidates != 0 && second_addition_candidate_count >= search_configuration.maximum_addition_candidates )
					break;
				++second_addition_candidate_count;

				if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
					break;
				++inout_node_budget;

				state.weight_second_addition = second_addition_candidate_for_branch_a.linear_weight;
				if ( state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap )
					break;  // candidates are in nondecreasing weight order

				state.input_branch_a_mask_before_second_addition = second_addition_candidate_for_branch_a.input_mask_x;
				state.second_addition_term_mask_from_branch_b = second_addition_candidate_for_branch_a.input_mask_y;

				handle_second_addition_candidate();
			}
		}

		// After ADD2, inject B -> A: correlated B2 masks form an affine subspace.
		void handle_second_addition_candidate()
		{
			auto& state = current_round_state();
			state.branch_b_mask_contribution_from_second_addition_term =
				NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 31 ) ^ NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 17 );

			state.injection_from_branch_b_transition = compute_injection_transition_from_branch_b( state.input_branch_a_mask_before_second_addition );
			state.weight_injection_from_branch_b = state.injection_from_branch_b_transition.weight;
			state.base_weight_after_inj_b = state.weight_injection_from_branch_a + state.weight_second_addition + state.weight_injection_from_branch_b;
			if ( state.base_weight_after_inj_b >= state.round_weight_cap )
				return;

			enumerate_injection_from_branch_b_masks();
		}

		// Injection B -> A (reverse): enumerate correlated input masks.
		void enumerate_injection_from_branch_b_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_b_transition,
				search_configuration.maximum_injection_input_masks,
				[ this ]( std::uint32_t m ) { handle_injection_from_branch_b_mask( m ); } );
		}

		void handle_injection_from_branch_b_mask( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b )
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_correlated_input_mask_for_injection_from_branch_b;
			enumerate_second_constant_subtraction_candidates();
		}

		// SUB2: B4 = B3 ⊟ RC6, then reverse L1 and XOR/ROT pair.
		void enumerate_second_constant_subtraction_candidates()
		{
			auto& state = current_round_state();
			std::uint64_t& inout_node_budget = context.visited_node_count;

			for ( const auto& second_constant_subtraction_candidate_for_branch_b : state.second_constant_subtraction_candidates_for_branch_b )
			{
				if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
					break;
				++inout_node_budget;

				state.weight_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.linear_weight;
				if ( state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap )
					break;  // candidates are sorted by weight

				state.input_branch_b_mask_before_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.input_mask_on_x;
				state.branch_b_mask_after_linear_layer_one_backward =
					state.input_branch_b_mask_before_second_constant_subtraction ^ state.branch_b_mask_contribution_from_second_addition_term;
				// Linear layer on branch B removed (treat as identity in reverse propagation)
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base = state.branch_b_mask_after_linear_layer_one_backward;

				state.base_weight_after_second_subconst = state.base_weight_after_inj_b + state.weight_second_constant_subtraction;
				if ( state.base_weight_after_second_subconst >= state.round_weight_cap )
					continue;

				const int remaining_after_second_subconst = state.round_weight_cap - state.base_weight_after_second_subconst;
				if ( remaining_after_second_subconst <= 0 )
					continue;
				state.first_subconst_weight_cap = std::min( search_configuration.constant_subtraction_weight_cap, remaining_after_second_subconst - 1 );
				state.first_add_weight_cap = std::min( search_configuration.addition_weight_cap, remaining_after_second_subconst - 1 );
				if ( state.first_add_weight_cap < 0 )
					continue;

				const std::uint32_t output_branch_a_mask_after_injection_from_branch_b = state.input_branch_a_mask_before_second_addition;
				const std::uint32_t branch_a_mask_after_first_xor_with_rotated_branch_b = output_branch_a_mask_after_injection_from_branch_b;
				const std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a =
					state.branch_b_mask_after_first_xor_with_rotated_branch_a_base ^ state.chosen_correlated_input_mask_for_injection_from_branch_b;

				state.output_branch_a_mask_after_first_constant_subtraction =
					branch_a_mask_after_first_xor_with_rotated_branch_b ^ NeoAlzetteCore::rotr( branch_b_mask_after_first_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
				state.output_branch_b_mask_after_first_addition =
					branch_b_mask_after_first_xor_with_rotated_branch_a ^ NeoAlzetteCore::rotr( state.output_branch_a_mask_after_first_constant_subtraction, NeoAlzetteCore::CROSS_XOR_ROT_R0 );

				enumerate_first_subround_candidates();
			}
		}

		// SUB1/ADD1 (near input): enumerate candidates and build LinearTrailStepRecord.
		void enumerate_first_subround_candidates()
		{
			auto& state = current_round_state();
			const auto first_constant_subtraction_candidates_for_branch_a =
				generate_subconst_candidates_for_fixed_beta(
					state.output_branch_a_mask_after_first_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					state.first_subconst_weight_cap,
					search_configuration.maximum_constant_subtraction_candidates );
			const auto& first_addition_candidates_for_branch_b =
				AddVarVarCandidateHighway32::get_candidates_for_output_mask_u(
					state.output_branch_b_mask_after_first_addition,
					state.first_add_weight_cap,
					search_configuration.maximum_addition_candidates );

			std::uint64_t& inout_node_budget = context.visited_node_count;

			for ( const auto& first_constant_subtraction_candidate_for_branch_a : first_constant_subtraction_candidates_for_branch_a )
			{
				if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
					break;
				++inout_node_budget;

				const std::uint32_t input_branch_a_mask_before_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.input_mask_on_x;
				const int			base_weight_after_first_subconst = state.base_weight_after_second_subconst + first_constant_subtraction_candidate_for_branch_a.linear_weight;
				if ( base_weight_after_first_subconst >= state.round_weight_cap )
					break;  // candidates are sorted by weight

				std::size_t first_addition_candidate_count = 0;
				for ( const auto& first_addition_candidate_for_branch_b : first_addition_candidates_for_branch_b )
				{
					if ( first_addition_candidate_for_branch_b.linear_weight > state.first_add_weight_cap )
						break;
					if ( search_configuration.maximum_addition_candidates != 0 && first_addition_candidate_count >= search_configuration.maximum_addition_candidates )
						break;
					++first_addition_candidate_count;

					if ( search_configuration.maximum_search_nodes != 0 && inout_node_budget >= search_configuration.maximum_search_nodes )
						break;
					++inout_node_budget;

					const std::uint32_t input_branch_b_mask_before_first_addition = first_addition_candidate_for_branch_b.input_mask_x;
					const std::uint32_t first_addition_term_mask_from_branch_a = first_addition_candidate_for_branch_b.input_mask_y;

					const std::uint32_t branch_a_mask_contribution_from_first_addition_term =
						NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 31 ) ^ NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 17 );
					const std::uint32_t input_branch_a_mask_at_round_input =
						input_branch_a_mask_before_first_constant_subtraction ^ branch_a_mask_contribution_from_first_addition_term;

					const int total_w = base_weight_after_first_subconst + first_addition_candidate_for_branch_b.linear_weight;
					if ( total_w >= state.round_weight_cap )
						break;  // candidates are in nondecreasing weight order

					LinearTrailStepRecord step {};
					step.round_index = state.round_index;
					step.output_branch_a_mask = state.round_output_branch_a_mask;
					step.output_branch_b_mask = state.round_output_branch_b_mask;
					step.input_branch_a_mask = input_branch_a_mask_at_round_input;
					step.input_branch_b_mask = input_branch_b_mask_before_first_addition;

					step.output_branch_b_mask_after_second_constant_subtraction = state.output_branch_b_mask_after_second_constant_subtraction;
					step.input_branch_b_mask_before_second_constant_subtraction = state.input_branch_b_mask_before_second_constant_subtraction;
					step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;

					step.output_branch_a_mask_after_second_addition = state.output_branch_a_mask_after_second_addition;
					step.input_branch_a_mask_before_second_addition = state.input_branch_a_mask_before_second_addition;
					step.second_addition_term_mask_from_branch_b = state.second_addition_term_mask_from_branch_b;
					step.weight_second_addition = state.weight_second_addition;

					step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
					step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
					step.chosen_correlated_input_mask_for_injection_from_branch_a = state.chosen_correlated_input_mask_for_injection_from_branch_a;
					step.chosen_correlated_input_mask_for_injection_from_branch_b = state.chosen_correlated_input_mask_for_injection_from_branch_b;

					step.output_branch_a_mask_after_first_constant_subtraction = state.output_branch_a_mask_after_first_constant_subtraction;
					step.input_branch_a_mask_before_first_constant_subtraction = input_branch_a_mask_before_first_constant_subtraction;
					step.weight_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.linear_weight;

					step.output_branch_b_mask_after_first_addition = state.output_branch_b_mask_after_first_addition;
					step.input_branch_b_mask_before_first_addition = input_branch_b_mask_before_first_addition;
					step.first_addition_term_mask_from_branch_a = first_addition_term_mask_from_branch_a;
					step.weight_first_addition = first_addition_candidate_for_branch_b.linear_weight;

					step.round_weight = total_w;

					state.round_predecessors.push_back( step );
					trim_round_predecessors( false );
				}
			}
		}

		// DFS over predecessors in increasing weight order.
		void recurse_over_round_predecessors()
		{
			auto& state = current_round_state();
			for ( const auto& step : state.round_predecessors )
			{
				if ( context.stop_due_to_time_limit || context.stop_due_to_target )
					return;
				if ( state.accumulated_weight_so_far + step.round_weight >= context.best_weight )
					continue;

				context.current_linear_trail.push_back( step );
				search_recursive( state.round_boundary_depth + 1, step.input_branch_a_mask, step.input_branch_b_mask, state.accumulated_weight_so_far + step.round_weight );
				context.current_linear_trail.pop_back();
			}
		}
	};

	static inline std::vector<int> auto_generate_remaining_round_lower_bound_table( const LinearBestSearchConfiguration& base_configuration, std::uint32_t start_output_branch_a_mask, std::uint32_t start_output_branch_b_mask )
	{
		const int round_count = std::max( 0, base_configuration.round_count );
		std::vector<int> table( std::size_t( round_count ) + 1u, 0 );
		if ( round_count <= 0 )
			return table;

		for ( int k = 1; k <= round_count; ++k )
		{
			LinearBestSearchConfiguration config = base_configuration;
			config.round_count = k;
			config.enable_remaining_round_lower_bound = false;
			config.remaining_round_min_weight.clear();
			config.auto_generate_remaining_round_lower_bound = false;
			config.target_best_weight = -1;
			// Make the lower-bound search as complete as possible by removing heuristic caps.
			config.maximum_round_predecessors = 0;
			config.maximum_addition_candidates = 0;
			config.maximum_constant_subtraction_candidates = 0;
			config.maximum_injection_input_masks = 0;
			config.maximum_search_nodes = 0;
			config.maximum_search_seconds = 0;

			if ( base_configuration.remaining_round_lower_bound_generation_nodes != 0 )
				config.maximum_search_nodes = base_configuration.remaining_round_lower_bound_generation_nodes;
			if ( base_configuration.remaining_round_lower_bound_generation_seconds != 0 )
				config.maximum_search_seconds = base_configuration.remaining_round_lower_bound_generation_seconds;

			LinearBestSearchContext tmp_context {};
			tmp_context.configuration = config;
			tmp_context.start_output_branch_a_mask = start_output_branch_a_mask;
			tmp_context.start_output_branch_b_mask = start_output_branch_b_mask;
			tmp_context.run_start_time = std::chrono::steady_clock::now();
			tmp_context.memoization.initialize( std::size_t( config.round_count ) + 1u, config.enable_state_memoization, "linear_memo.lb.init" );

			if ( config.maximum_search_seconds != 0 )
			{
				tmp_context.time_limit_start_time = tmp_context.run_start_time;
			}

			LinearBestTrailSearcher searcher( tmp_context );
			searcher.search_from_start( start_output_branch_a_mask, start_output_branch_b_mask );

			if ( tmp_context.best_weight < INFINITE_WEIGHT )
				table[ std::size_t( k ) ] = tmp_context.best_weight;
		}

		return table;
	}

	static inline MatsuiSearchRunLinearResult run_linear_best_search( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask, const LinearBestSearchConfiguration& search_configuration, bool print_output = false, std::uint64_t single_run_progress_every_seconds = 0, bool progress_print_masks = false, int seeded_upper_bound_weight = INFINITE_WEIGHT, const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail = nullptr, BestWeightCheckpointWriter* checkpoint = nullptr )
	{
		MatsuiSearchRunLinearResult result {};
		if ( search_configuration.round_count <= 0 )
			return result;

		LinearBestSearchContext context {};
		context.configuration = search_configuration;
		context.start_output_branch_a_mask = output_branch_a_mask;
		context.start_output_branch_b_mask = output_branch_b_mask;
		context.checkpoint = checkpoint;

		int&								best = context.best_weight;
		std::uint32_t&						best_input_branch_a_mask = context.best_input_branch_a_mask;
		std::uint32_t&						best_input_branch_b_mask = context.best_input_branch_b_mask;
		std::vector<LinearTrailStepRecord>& best_linear_trail = context.best_linear_trail;
		std::vector<LinearTrailStepRecord>& current = context.current_linear_trail;

		current.reserve( std::size_t( search_configuration.round_count ) );

		// Runtime init (single-run).
		context.run_start_time = std::chrono::steady_clock::now();
		context.memoization.initialize( std::size_t( search_configuration.round_count ) + 1u, search_configuration.enable_state_memoization, "linear_memo.init" );

		bool remaining_round_lower_bound_disabled_due_to_strict = false;
		bool remaining_round_lower_bound_autogenerated = false;

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		if ( context.configuration.enable_remaining_round_lower_bound )
		{
			const bool generation_limited =
				context.configuration.auto_generate_remaining_round_lower_bound &&
				( context.configuration.remaining_round_lower_bound_generation_nodes != 0 ||
				  context.configuration.remaining_round_lower_bound_generation_seconds != 0 );
			if ( generation_limited && context.configuration.strict_remaining_round_lower_bound )
			{
				context.configuration.enable_remaining_round_lower_bound = false;
				context.configuration.remaining_round_min_weight.clear();
				remaining_round_lower_bound_disabled_due_to_strict = true;
			}

			auto& remaining_round_min_weight_table = context.configuration.remaining_round_min_weight;
			if ( remaining_round_min_weight_table.empty() && context.configuration.auto_generate_remaining_round_lower_bound )
			{
				remaining_round_min_weight_table =
					auto_generate_remaining_round_lower_bound_table(
						context.configuration,
						output_branch_a_mask,
						output_branch_b_mask );
				remaining_round_lower_bound_autogenerated = true;
			}

			if ( remaining_round_min_weight_table.empty() )
			{
				remaining_round_min_weight_table.assign( std::size_t( std::max( 0, search_configuration.round_count ) ) + 1u, 0 );
			}
			else
			{
				// Ensure remaining_round_min_weight_table[0] exists and is 0.
				if ( remaining_round_min_weight_table.size() < 1u )
					remaining_round_min_weight_table.resize( 1u, 0 );
				remaining_round_min_weight_table[ 0 ] = 0;
				// Pad to round_count+1 with 0 (safe lower bound).
				const std::size_t need = std::size_t( std::max( 0, search_configuration.round_count ) ) + 1u;
				if ( remaining_round_min_weight_table.size() < need )
					remaining_round_min_weight_table.resize( need, 0 );
				for ( int& round_min_weight : remaining_round_min_weight_table )
				{
					if ( round_min_weight < 0 )
						round_min_weight = 0;
				}
			}
		}

		// Initialize time limit start time (even if progress is disabled).
		if ( search_configuration.maximum_search_seconds != 0 )
		{
			context.time_limit_start_time = context.run_start_time;
			// IMPORTANT: For small smoke tests, the original ~262k-node time-check granularity can
			// effectively disable the wall-clock limit. Tighten it when a time limit is requested.
			//
			// This affects:
			// - time limit checks
			// - memory governor polling
			// - progress time checks (if enabled)
			if ( search_configuration.maximum_search_seconds <= 2 )
				context.progress_node_mask = ( 1ull << 8 ) - 1;	 // check ~every 256 nodes
			else if ( search_configuration.maximum_search_seconds <= 10 )
				context.progress_node_mask = ( 1ull << 10 ) - 1;  // ~every 1024 nodes
			else
				context.progress_node_mask = ( 1ull << 12 ) - 1;  // ~every 4096 nodes
		}

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < best )
		{
			best = seeded_upper_bound_weight;
			if ( seeded_upper_bound_trail && !seeded_upper_bound_trail->empty() )
			{
				best_linear_trail = *seeded_upper_bound_trail;
				best_input_branch_a_mask = best_linear_trail.back().input_branch_a_mask;
				best_input_branch_b_mask = best_linear_trail.back().input_branch_b_mask;
			}
		}

		// Optional: persist initial best (seeded) once.
		if ( checkpoint && best < INFINITE_WEIGHT && !best_linear_trail.empty() )
		{
			checkpoint->maybe_write( context, "init" );
		}

		if ( print_output )
		{
			std::cout << "[BestSearch] mode=matsui(injection-affine)(reverse)\n";
			std::cout << "  rounds=" << search_configuration.round_count << "  maximum_search_nodes=" << search_configuration.maximum_search_nodes << "  maximum_search_seconds=" << search_configuration.maximum_search_seconds << "  memo=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n";
			std::cout << "  max_add_candidates=" << search_configuration.maximum_addition_candidates << "  max_subconst_candidates=" << search_configuration.maximum_constant_subtraction_candidates << "  max_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  max_round_predecessors=" << search_configuration.maximum_round_predecessors << "\n";
			if ( remaining_round_lower_bound_disabled_due_to_strict )
			{
				std::cout << "  remaining_round_lower_bound=off  reason=strict_mode_non_exhaustive_generation\n";
			}
			else if ( context.configuration.enable_remaining_round_lower_bound )
			{
				std::cout << "  remaining_round_lower_bound=on";
				if ( remaining_round_lower_bound_autogenerated )
				{
					std::cout << "  source=auto_generated";
					if ( context.configuration.remaining_round_lower_bound_generation_nodes != 0 )
						std::cout << "  gen_nodes=" << context.configuration.remaining_round_lower_bound_generation_nodes;
					if ( context.configuration.remaining_round_lower_bound_generation_seconds != 0 )
						std::cout << "  gen_seconds=" << context.configuration.remaining_round_lower_bound_generation_seconds;
				}
				std::cout << "\n";
			}
			if ( best < INFINITE_WEIGHT )
			{
				std::cout << "  seeded_upper_bound_weight=" << best << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if ( print_output && single_run_progress_every_seconds != 0 )
		{
			context.progress_every_seconds = single_run_progress_every_seconds;
			context.progress_start_time = context.run_start_time;
			context.progress_last_print_time = {};
			context.progress_last_print_nodes = 0;
			context.progress_print_masks = progress_print_masks;
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << context.progress_every_seconds << " seconds (time-check granularity ~" << ( context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		LinearBestTrailSearcher searcher( context );
		searcher.search_from_start( output_branch_a_mask, output_branch_b_mask );

		result.nodes_visited = context.visited_node_count;
		result.hit_maximum_search_nodes = ( search_configuration.maximum_search_nodes != 0 ) && ( context.visited_node_count >= search_configuration.maximum_search_nodes );
		result.hit_time_limit = context.stop_due_to_time_limit;
		result.hit_target_best_weight = context.stop_due_to_target;
		if ( best < INFINITE_WEIGHT && !best_linear_trail.empty() )
		{
			result.found = true;
			result.best_weight = best;
			result.best_linear_trail = std::move( best_linear_trail );
			result.best_input_branch_a_mask = best_input_branch_a_mask;
			result.best_input_branch_b_mask = best_input_branch_b_mask;
		}
		return result;
	}

}  // namespace TwilightDream::auto_search_linear


#endif