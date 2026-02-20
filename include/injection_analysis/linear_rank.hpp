#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
#include "injection_analysis/function_constexpr.hpp"
#include "arx_analysis_operators/DefineSearchWeight.hpp"

namespace TwilightDream::auto_search_linear
{
	// ============================================================================
	// Exact linear-rank model for the current injected XOR maps
	// ----------------------------------------------------------------------------
	// The analyzed objects are the exact 32 → 32 maps
	//   F_B(B) = injected_xor_term_from_branch_b(B),
	//   F_A(A) = injected_xor_term_from_branch_a(A),
	// i.e. the actual XOR words injected into the opposite branch by the current core.
	//
	// Exact current-core semantics:
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
	// - The variable `B` / `A` here is the branch word before the internal constant pre-whitening.
	// - So the linear rank analysis below is for the exact implemented maps F_B / F_A,
	//   not for an older abstract “ELL(C) ⊕ ERR(D) ⊕ RC” comment model.
	//
	// Because F_B and F_A are vector quadratic Boolean maps, for a fixed output mask u the scalar objects
	//   g_u^B(B) = ⟨u, F_B(B)⟩,
	//   g_u^A(A) = ⟨u, F_A(A)⟩
	// are quadratic Boolean functions.
	//
	// Their Walsh spectrum is controlled by the bilinear matrix S(u), and every non-zero correlation has
	// magnitude
	//   |corr| = 2^{-rank(S(u))/2},
	// hence the exact local linear weight is rank(S(u)) / 2.
	//
	// Mask-set support (no approximation):
	// - The correlated input masks v form the affine subspace
	//     v ∈ ℓ(u) ⊕ im(S(u)),
	//   where ℓ(u) is the linear coefficient vector of g_u.
	// - We compute ℓ(u) exactly by
	//     ℓ_i(u) = g_u(e_i) ⊕ g_u(0) = parity(u ∧ F(e_i)) ⊕ parity(u ∧ F(0)).
	// - Every v in that affine subspace has the same absolute correlation magnitude
	//     2^{-rank(S(u))/2}.
	// ============================================================================

	struct InjectionCorrelationTransition
	{
		// Correlated input masks form the affine subspace
		//   v ∈ ℓ(u) ⊕ span(basis_vectors[0..rank-1]),
		// where `offset_mask` stores ℓ(u).
		std::uint32_t				  offset_mask = 0;
		std::array<std::uint32_t, 32> basis_vectors {};
		std::uint64_t					rank = 0;	   // rank(S(u))
		SearchWeight				  weight = 0;  // ⌈rank(S(u))/2⌉ under integer weight semantics
	};

	namespace injection_rank_detail
	{
		static inline unsigned parity32( std::uint32_t x ) noexcept
		{
			return static_cast<unsigned>( std::popcount( x ) & 1u );
		}
	}  // namespace injection_rank_detail

	static constexpr std::uint32_t injected_f0_branch_b = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 0u );
	static constexpr std::uint32_t injected_f0_branch_a = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 0u );

	// We keep the full exact maps F_B / F_A intact when extracting the exact
	// linear coefficient vector ℓ(u). Pure rotate/XOR sublayers still admit
	// transpose by reversing the rotation orientation (and the corresponding
	// zero-fill shift direction), but we do not split mask0 / mask1 into
	// standalone pseudo-components here.

	static consteval std::array<std::uint32_t, 32> make_injection_f_basis_branch_b()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ std::size_t( i ) ] = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << i );
		return out;
	}

	static consteval std::array<std::uint32_t, 32> make_injection_f_basis_branch_a()
	{
		std::array<std::uint32_t, 32> out {};
		for ( int i = 0; i < 32; ++i )
			out[ std::size_t( i ) ] = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << i );
		return out;
	}

	static constexpr std::array<std::uint32_t, 32> injected_f_basis_branch_b = make_injection_f_basis_branch_b();
	static constexpr std::array<std::uint32_t, 32> injected_f_basis_branch_a = make_injection_f_basis_branch_a();

	// Formula sanity for the exact analyzed objects F_B / F_A:
	// precomputed F(0) and F(e_i) must match direct `analysis_constexpr`.
	static_assert( injected_f0_branch_b == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 0u ), "f(0) branch_b: precomputed vs direct" );
	static_assert( injected_f0_branch_a == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 0u ), "f(0) branch_a: precomputed vs direct" );
	static_assert( injected_f_basis_branch_b[ 0 ] == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u ), "f(e_0) branch_b: precomputed vs direct" );
	static_assert( injected_f_basis_branch_a[ 0 ] == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u ), "f(e_0) branch_a: precomputed vs direct" );
	static_assert( injected_f_basis_branch_b[ 31 ] == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( 1u << 31 ), "f(e_31) branch_b: precomputed vs direct" );
	static_assert( injected_f_basis_branch_a[ 31 ] == TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( 1u << 31 ), "f(e_31) branch_a: precomputed vs direct" );

	// --------------------------------------------------------------------------
	// Exact quadratic weight for g_u(x) = ⟨u, F(x)⟩ via bilinear rank
	//
	// For i < j, the quadratic coefficient vector of F for x_i x_j is
	//   Δ²_{i,j}F = F(0) ⊕ F(e_i) ⊕ F(e_j) ⊕ F(e_i ⊕ e_j)  ∈ {0,1}^{32}.
	// Then the scalar quadratic coefficient q_{i,j}(u) of g_u is
	//   q_{i,j}(u) = parity(u ∧ Δ²_{i,j}F).
	//
	// We build S(u) from these q_{i,j}(u) and compute rank(S(u)) over GF(2).
	// --------------------------------------------------------------------------

	// Fused: compute Δ²_{i,j}F on the fly and fill rows_by_outbit; no separate stored deltas array.
	//
	// Formula check (must match comment above):
	//   e_i = 1<<i, e_j = 1<<j
	//   Δ²_{i,j}F = F(0) ⊕ F(e_i) ⊕ F(e_j) ⊕ F(e_i ⊕ e_j)
	//   Fi = F(e_i), Fj = F(e_j), Fij = F(e_i ⊕ e_j) => delta = F0 ⊕ Fi ⊕ Fj ⊕ Fij
	//   q_{i,j}(u) = parity(u ∧ Δ²_{i,j}F); for u = 1<<k, q_{i,j} = (delta>>k)&1, placed at (i,j),(j,i).
	static consteval std::array<std::array<std::uint32_t, 32>, 32> make_injection_quadratic_rows_by_outbit_branch_b()
	{
		std::array<std::array<std::uint32_t, 32>, 32> rows_by_outbit {};
		for ( int i = 0; i < 31; ++i )
		{
			const std::uint32_t fi = injected_f_basis_branch_b[ std::size_t( i ) ];	 // F_B(e_i), e_i = 1<<i
			for ( int j = i + 1; j < 32; ++j )
			{
				const std::uint32_t fj = injected_f_basis_branch_b[ std::size_t( j ) ];	 // F_B(e_j)
				const std::uint32_t fij = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( ( 1u << i ) ^ ( 1u << j ) );	 // F_B(e_i ⊕ e_j)
				const std::uint32_t delta = injected_f0_branch_b ^ fi ^ fj ^ fij;	 // Δ²_{i,j}F_B
				for ( int k = 0; k < 32; ++k )
				{
					if ( ( ( delta >> k ) & 1u ) != 0u )	 // q_{i,j}(u) for u = 1<<k
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
				const std::uint32_t fij = TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( ( 1u << i ) ^ ( 1u << j ) );
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

	namespace injection_rank_detail
	{
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
	}  // namespace injection_rank_detail

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_b( std::uint32_t output_mask_u ) noexcept
	{
		InjectionCorrelationTransition transition {};
		if ( output_mask_u == 0u )
			return transition;

		// offset_mask = exact linear coefficient vector ℓ(u), computed directly
		// from the full injected map basis values:
		//   ℓ_i(u) = parity(u ∧ F(e_i)) ⊕ parity(u ∧ F(0)).
		{
			const unsigned g0 = injection_rank_detail::parity32( output_mask_u & injected_f0_branch_b );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = injection_rank_detail::parity32( output_mask_u & injected_f_basis_branch_b[ std::size_t( i ) ] );
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
		std::uint64_t				  rank = 0;
		std::array<std::uint32_t, 32> basis_by_msb {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t row = rows[ std::size_t( i ) ];
			if ( row != 0u )
				rank += injection_rank_detail::xor_basis_add_32( basis_by_msb, row );
		}
		transition.rank = rank;
		transition.weight = static_cast<SearchWeight>( ( rank + 1 ) / 2 );
		std::size_t packed = 0;
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

		// Same exact ℓ(u) construction for branch A:
		//   ℓ_i(u) = parity(u ∧ F(e_i)) ⊕ parity(u ∧ F(0)).
		{
			const unsigned g0 = injection_rank_detail::parity32( output_mask_u & injected_f0_branch_a );
			std::uint32_t  offset_mask = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				const unsigned gi = injection_rank_detail::parity32( output_mask_u & injected_f_basis_branch_a[ std::size_t( i ) ] );
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

		std::uint64_t				  rank = 0;
		std::array<std::uint32_t, 32> basis_by_msb {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t row = rows[ std::size_t( i ) ];
			if ( row != 0u )
				rank += injection_rank_detail::xor_basis_add_32( basis_by_msb, row );
		}
		transition.rank = rank;
		transition.weight = static_cast<SearchWeight>( ( rank + 1 ) / 2 );
		std::size_t packed = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
			if ( v != 0u )
				transition.basis_vectors[ std::size_t( packed++ ) ] = v;
		}
		return transition;
	}

}  // namespace TwilightDream::auto_search_linear
