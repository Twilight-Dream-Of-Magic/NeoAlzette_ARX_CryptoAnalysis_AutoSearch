#pragma once

#include <array>
#include <bit>
#include <cmath>
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
	// - The correlated input masks v form one exact affine coset of im(S(u)).
	// - `offset_mask` stores a support representative that satisfies the radical constraints
	//   q_u(z) ⊕ q_u(0) = <offset_mask, z> on ker(S(u)); this is stricter than blindly
	//   reusing the raw linear coefficient vector when the quadratic form has a non-trivial
	//   radical offset.
	// - Every v in that affine subspace has the same absolute correlation magnitude
	//     2^{-rank(S(u))/2}.
	// ============================================================================

	struct InjectionCorrelationTransition
	{
		// Correlated input masks form the affine subspace
		//   v ∈ offset_mask ⊕ span(basis_vectors[0..rank-1]),
		// where `offset_mask` stores one exact support-coset representative.
		std::uint32_t				  offset_mask = 0;
		std::array<std::uint32_t, 32> basis_vectors {};
		std::array<std::uint8_t, 32>  basis_pivot_bits {};
		std::uint64_t					rank = 0;	   // rank(S(u))
		SearchWeight				  weight = 0;  // ⌈rank(S(u))/2⌉ under integer weight semantics
		std::uint8_t				  phase_constant = 0;
		std::uint32_t				  phase_linear_mask = 0;
		std::array<std::uint32_t, 32> phase_quadratic_rows {};
	};

	namespace injection_rank_detail
	{
		static inline unsigned parity32( std::uint32_t x ) noexcept
		{
			return static_cast<unsigned>( std::popcount( x ) & 1u );
		}

		static inline unsigned evaluate_phase_quadratic_bit(
			std::uint32_t coefficients,
			const std::array<std::uint32_t, 32>& phase_quadratic_rows ) noexcept
		{
			unsigned parity = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				if ( ( ( coefficients >> i ) & 1u ) == 0u )
					continue;
				parity ^= parity32( coefficients & phase_quadratic_rows[ std::size_t( i ) ] );
			}
			return parity;
		}

		static inline std::uint32_t apply_matrix_rows_32(
			const std::array<std::uint32_t, 32>& rows,
			std::uint32_t vector ) noexcept
		{
			std::uint32_t image = 0u;
			for ( int i = 0; i < 32; ++i )
			{
				if ( ( ( vector >> i ) & 1u ) == 0u )
					continue;
				image ^= rows[ std::size_t( i ) ];
			}
			return image;
		}

		static inline unsigned bilinear_form_32(
			const std::array<std::uint32_t, 32>& rows,
			std::uint32_t lhs,
			std::uint32_t rhs ) noexcept
		{
			return parity32( apply_matrix_rows_32( rows, lhs ) & rhs );
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

		static inline bool solve_linear_system_rows_32(
			const std::array<std::uint32_t, 32>& rows_in,
			std::uint32_t rhs_bits,
			std::uint32_t& solution ) noexcept
		{
			std::array<std::uint32_t, 32> rows = rows_in;
			std::array<std::uint8_t, 32>  rhs {};
			std::array<int, 32>			  pivot_col_for_row {};
			for ( int i = 0; i < 32; ++i )
			{
				rhs[ std::size_t( i ) ] = static_cast<std::uint8_t>( ( rhs_bits >> i ) & 1u );
				pivot_col_for_row[ std::size_t( i ) ] = -1;
			}

			int pivot_row = 0;
			for ( int col = 0; col < 32 && pivot_row < 32; ++col )
			{
				int selected = -1;
				for ( int row = pivot_row; row < 32; ++row )
				{
					if ( ( ( rows[ std::size_t( row ) ] >> col ) & 1u ) != 0u )
					{
						selected = row;
						break;
					}
				}
				if ( selected < 0 )
					continue;

				if ( selected != pivot_row )
				{
					std::swap( rows[ std::size_t( selected ) ], rows[ std::size_t( pivot_row ) ] );
					std::swap( rhs[ std::size_t( selected ) ], rhs[ std::size_t( pivot_row ) ] );
				}

				for ( int row = 0; row < 32; ++row )
				{
					if ( row == pivot_row )
						continue;
					if ( ( ( rows[ std::size_t( row ) ] >> col ) & 1u ) == 0u )
						continue;
					rows[ std::size_t( row ) ] ^= rows[ std::size_t( pivot_row ) ];
					rhs[ std::size_t( row ) ] ^= rhs[ std::size_t( pivot_row ) ];
				}

				pivot_col_for_row[ std::size_t( pivot_row ) ] = col;
				++pivot_row;
			}

			for ( int row = pivot_row; row < 32; ++row )
			{
				if ( rhs[ std::size_t( row ) ] != 0u )
					return false;
			}

			solution = 0u;
			for ( int row = 0; row < pivot_row; ++row )
			{
				if ( rhs[ std::size_t( row ) ] == 0u )
					continue;
				const int pivot_col = pivot_col_for_row[ std::size_t( row ) ];
				if ( pivot_col < 0 )
					return false;
				solution ^= ( 1u << pivot_col );
			}
			return true;
		}

		static inline int compute_nullspace_basis_32(
			const std::array<std::uint32_t, 32>& rows_in,
			std::array<std::uint32_t, 32>& nullspace_basis ) noexcept
		{
			std::array<std::uint32_t, 32> rows = rows_in;
			std::array<int, 32>			  pivot_cols {};
			pivot_cols.fill( -1 );

			int rank = 0;
			for ( int col = 0; col < 32 && rank < 32; ++col )
			{
				int selected = -1;
				for ( int row = rank; row < 32; ++row )
				{
					if ( ( ( rows[ std::size_t( row ) ] >> col ) & 1u ) != 0u )
					{
						selected = row;
						break;
					}
				}
				if ( selected < 0 )
					continue;
				if ( selected != rank )
					std::swap( rows[ std::size_t( selected ) ], rows[ std::size_t( rank ) ] );
				for ( int row = 0; row < 32; ++row )
				{
					if ( row == rank )
						continue;
					if ( ( ( rows[ std::size_t( row ) ] >> col ) & 1u ) == 0u )
						continue;
					rows[ std::size_t( row ) ] ^= rows[ std::size_t( rank ) ];
				}
				pivot_cols[ std::size_t( rank ) ] = col;
				++rank;
			}

			nullspace_basis.fill( 0u );
			int nullity = 0;
			for ( int free_col = 0; free_col < 32; ++free_col )
			{
				bool is_pivot = false;
				for ( int row = 0; row < rank; ++row )
				{
					if ( pivot_cols[ std::size_t( row ) ] == free_col )
					{
						is_pivot = true;
						break;
					}
				}
				if ( is_pivot )
					continue;

				std::uint32_t vector = ( 1u << free_col );
				for ( int row = rank - 1; row >= 0; --row )
				{
					const int pivot_col = pivot_cols[ std::size_t( row ) ];
					if ( pivot_col < 0 )
						continue;
					if ( parity32( rows[ std::size_t( row ) ] & vector ) != 0u )
						vector ^= ( 1u << pivot_col );
				}
				nullspace_basis[ std::size_t( nullity++ ) ] = vector;
			}
			return nullity;
		}

		static inline void pack_basis_vectors_and_pivots(
			InjectionCorrelationTransition& transition,
			const std::array<std::uint32_t, 32>& basis_by_msb ) noexcept
		{
			transition.basis_vectors.fill( 0u );
			transition.basis_pivot_bits.fill( 0xFFu );

			std::size_t packed = 0;
			for ( int bit = 31; bit >= 0; --bit )
			{
				const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
				if ( v == 0u )
					continue;
				transition.basis_vectors[ packed ] = v;
				transition.basis_pivot_bits[ packed ] = static_cast<std::uint8_t>( bit );
				++packed;
			}
		}

		static inline int repack_basis_from_vectors(
			const std::array<std::uint32_t, 32>& input_vectors,
			int input_count,
			std::array<std::uint32_t, 32>& packed_vectors ) noexcept
		{
			std::array<std::uint32_t, 32> basis_by_msb {};
			for ( int i = 0; i < input_count; ++i )
			{
				const std::uint32_t v = input_vectors[ std::size_t( i ) ];
				if ( v != 0u )
					xor_basis_add_32( basis_by_msb, v );
			}

			packed_vectors.fill( 0u );
			int packed_count = 0;
			for ( int bit = 31; bit >= 0; --bit )
			{
				const std::uint32_t v = basis_by_msb[ std::size_t( bit ) ];
				if ( v != 0u )
					packed_vectors[ std::size_t( packed_count++ ) ] = v;
			}
			return packed_count;
		}

		template <typename EvalMapFn>
		static inline unsigned compute_base_phase_sign_bit(
			const std::array<std::uint32_t, 32>& rows,
			std::uint32_t output_mask_u,
			std::uint32_t support_offset,
			unsigned q0,
			EvalMapFn&& eval_map ) noexcept
		{
			auto reduced_quadratic_value = [&]( std::uint32_t x ) noexcept -> unsigned {
				const unsigned qx =
					parity32(
						output_mask_u &
						eval_map( x ) );
				return static_cast<unsigned>( ( qx ^ q0 ^ parity32( support_offset & x ) ) & 1u );
			};

			std::array<std::uint32_t, 32> current_basis {};
			for ( int i = 0; i < 32; ++i )
				current_basis[ std::size_t( i ) ] = ( 1u << i );
			int current_count = 32;
			unsigned arf_bit = 0u;

			while ( true )
			{
				int p_index = -1;
				int q_index = -1;
				for ( int i = 0; i < current_count; ++i )
				{
					const std::uint32_t p = current_basis[ std::size_t( i ) ];
					if ( apply_matrix_rows_32( rows, p ) == 0u )
						continue;
					for ( int j = 0; j < current_count; ++j )
					{
						if ( i == j )
							continue;
						if ( bilinear_form_32( rows, p, current_basis[ std::size_t( j ) ] ) == 0u )
							continue;
						p_index = i;
						q_index = j;
						break;
					}
					if ( p_index >= 0 )
						break;
				}

				if ( p_index < 0 || q_index < 0 )
					break;

				const std::uint32_t p = current_basis[ std::size_t( p_index ) ];
				const std::uint32_t q = current_basis[ std::size_t( q_index ) ];
				arf_bit ^= ( reduced_quadratic_value( p ) & reduced_quadratic_value( q ) );

				std::array<std::uint32_t, 32> next_vectors {};
				int next_count = 0;
				for ( int i = 0; i < current_count; ++i )
				{
					if ( i == p_index || i == q_index )
						continue;
					std::uint32_t v = current_basis[ std::size_t( i ) ];
					if ( bilinear_form_32( rows, v, q ) != 0u )
						v ^= p;
					if ( bilinear_form_32( rows, v, p ) != 0u )
						v ^= q;
					next_vectors[ std::size_t( next_count++ ) ] = v;
				}
				current_count = repack_basis_from_vectors( next_vectors, next_count, current_basis );
			}

			return static_cast<unsigned>( ( q0 ^ arf_bit ) & 1u );
		}

		template <typename EvalMapFn>
		static inline void build_phase_descriptor(
			InjectionCorrelationTransition& transition,
			const std::array<std::uint32_t, 32>& rows,
			std::uint32_t output_mask_u,
			unsigned q0,
			EvalMapFn&& eval_map ) noexcept
		{
			transition.phase_constant =
				static_cast<std::uint8_t>(
					compute_base_phase_sign_bit(
						rows,
						output_mask_u,
						transition.offset_mask,
						q0,
						std::forward<EvalMapFn>( eval_map ) ) );
			transition.phase_linear_mask = 0u;
			transition.phase_quadratic_rows.fill( 0u );

			auto relative_phase_sample = [&]( std::uint32_t section_preimage ) noexcept -> unsigned {
				const std::uint32_t q_value =
					static_cast<std::uint32_t>(
						parity32(
							output_mask_u &
							eval_map( section_preimage ) ) );
				const std::uint32_t offset_dot_y =
					static_cast<std::uint32_t>(
						parity32( transition.offset_mask & section_preimage ) );
				return static_cast<unsigned>( ( q_value ^ q0 ^ offset_dot_y ) & 1u );
			};

			if ( transition.rank == 0u )
				return;

			std::array<std::uint32_t, 32> section_basis_preimages {};
			for ( std::size_t i = 0; i < static_cast<std::size_t>( transition.rank ); ++i )
			{
				std::uint32_t preimage = 0u;
				if ( !solve_linear_system_rows_32( rows, transition.basis_vectors[ i ], preimage ) )
					continue;
				section_basis_preimages[ i ] = preimage;
			}

			for ( std::size_t i = 0; i < static_cast<std::size_t>( transition.rank ); ++i )
			{
				const unsigned sample_single = relative_phase_sample( section_basis_preimages[ i ] );
				if ( sample_single != 0u )
					transition.phase_linear_mask ^= ( 1u << i );
			}

			for ( std::size_t i = 0; i < static_cast<std::size_t>( transition.rank ); ++i )
			{
				const unsigned phase_i = relative_phase_sample( section_basis_preimages[ i ] );
				for ( std::size_t j = i + 1; j < static_cast<std::size_t>( transition.rank ); ++j )
				{
					const unsigned phase_j = relative_phase_sample( section_basis_preimages[ j ] );
					const unsigned phase_ij =
						relative_phase_sample( section_basis_preimages[ i ] ^ section_basis_preimages[ j ] );
					if ( ( phase_ij ^ phase_i ^ phase_j ) != 0u )
						transition.phase_quadratic_rows[ i ] |= ( 1u << j );
				}
			}
		}

		template <typename EvalMapFn>
		static inline InjectionCorrelationTransition build_linear_correlation_transition_from_exact_map(
			std::uint32_t output_mask_u,
			std::uint32_t f0,
			const std::array<std::array<std::uint32_t, 32>, 32>& quad_rows_by_outbit,
			EvalMapFn&& eval_map ) noexcept
		{
			InjectionCorrelationTransition transition {};
			transition.basis_pivot_bits.fill( 0xFFu );
			if ( output_mask_u == 0u )
			{
				transition.phase_constant = 0u;
				return transition;
			}

			std::array<std::uint32_t, 32> rows {};
			for ( int k = 0; k < 32; ++k )
			{
				if ( ( ( output_mask_u >> k ) & 1u ) == 0u )
					continue;
				for ( int i = 0; i < 32; ++i )
					rows[ std::size_t( i ) ] ^= quad_rows_by_outbit[ std::size_t( k ) ][ std::size_t( i ) ];
			}

			std::uint64_t				  rank = 0;
			std::array<std::uint32_t, 32> basis_by_msb {};
			for ( int i = 0; i < 32; ++i )
			{
				const std::uint32_t row = rows[ std::size_t( i ) ];
				if ( row != 0u )
					rank += xor_basis_add_32( basis_by_msb, row );
			}
			transition.rank = rank;
			transition.weight = static_cast<SearchWeight>( ( rank + 1 ) / 2 );
			pack_basis_vectors_and_pivots( transition, basis_by_msb );

			const unsigned q0 = parity32( output_mask_u & f0 );
			{
				std::array<std::uint32_t, 32> kernel_basis {};
				const int kernel_dim = compute_nullspace_basis_32( rows, kernel_basis );
				std::array<std::uint32_t, 32> kernel_rows {};
				std::uint32_t kernel_rhs_bits = 0u;
				for ( int i = 0; i < kernel_dim; ++i )
				{
					kernel_rows[ std::size_t( i ) ] = kernel_basis[ std::size_t( i ) ];
					const unsigned qz =
						parity32(
							output_mask_u &
							eval_map( kernel_basis[ std::size_t( i ) ] ) );
					if ( ( qz ^ q0 ) != 0u )
						kernel_rhs_bits |= ( 1u << i );
				}
				std::uint32_t support_offset = 0u;
				if ( !solve_linear_system_rows_32( kernel_rows, kernel_rhs_bits, support_offset ) )
					support_offset = 0u;
				transition.offset_mask = support_offset;
			}
			build_phase_descriptor(
				transition,
				rows,
				output_mask_u,
				q0,
				std::forward<EvalMapFn>( eval_map ) );
			return transition;
		}
	}  // namespace injection_rank_detail

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_b( std::uint32_t output_mask_u ) noexcept
	{
		return injection_rank_detail::build_linear_correlation_transition_from_exact_map(
			output_mask_u,
			injected_f0_branch_b,
			injected_quad_rows_by_outbit_branch_b,
			[]( std::uint32_t input_word ) noexcept {
				return TwilightDream::analysis_constexpr::injected_xor_term_from_branch_b( input_word );
			} );
	}

	static inline InjectionCorrelationTransition compute_injection_transition_from_branch_a( std::uint32_t output_mask_u ) noexcept
	{
		return injection_rank_detail::build_linear_correlation_transition_from_exact_map(
			output_mask_u,
			injected_f0_branch_a,
			injected_quad_rows_by_outbit_branch_a,
			[]( std::uint32_t input_word ) noexcept {
				return TwilightDream::analysis_constexpr::injected_xor_term_from_branch_a( input_word );
			} );
	}

	static inline bool solve_injection_support_coefficients(
		const InjectionCorrelationTransition& transition,
		std::uint32_t chosen_mask,
		std::uint32_t& coefficients ) noexcept
	{
		std::uint32_t residual = chosen_mask ^ transition.offset_mask;
		coefficients = 0u;
		for ( std::size_t i = 0; i < static_cast<std::size_t>( transition.rank ); ++i )
		{
			const std::uint8_t pivot_bit = transition.basis_pivot_bits[ i ];
			if ( pivot_bit >= 32u )
				return false;
			if ( ( ( residual >> pivot_bit ) & 1u ) == 0u )
				continue;
			residual ^= transition.basis_vectors[ i ];
			coefficients ^= ( 1u << i );
		}
		return residual == 0u;
	}

	static inline unsigned evaluate_injection_phase_bit(
		const InjectionCorrelationTransition& transition,
		std::uint32_t coefficients ) noexcept
	{
		return
			static_cast<unsigned>( transition.phase_constant & 1u ) ^
			injection_rank_detail::parity32( coefficients & transition.phase_linear_mask ) ^
			injection_rank_detail::evaluate_phase_quadratic_bit(
				coefficients,
				transition.phase_quadratic_rows );
	}

	static inline long double exact_signed_injection_correlation(
		const InjectionCorrelationTransition& transition,
		std::uint32_t chosen_mask ) noexcept
	{
		std::uint32_t coefficients = 0u;
		if ( !solve_injection_support_coefficients( transition, chosen_mask, coefficients ) )
			return 0.0L;
		const long double magnitude =
			std::pow(
				2.0L,
				-static_cast<long double>( transition.rank ) / 2.0L );
		return ( evaluate_injection_phase_bit( transition, coefficients ) != 0u ) ? -magnitude : magnitude;
	}

}  // namespace TwilightDream::auto_search_linear
