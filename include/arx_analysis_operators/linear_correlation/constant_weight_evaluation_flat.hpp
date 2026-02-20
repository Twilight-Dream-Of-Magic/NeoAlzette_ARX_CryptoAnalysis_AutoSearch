#pragma once
/**
 * @file linear_correlation/constant_weight_evaluation_flat.hpp
 * @brief Run-flattened exact and windowed linear correlation for $y = x \boxplus c$ on $n \le 64$ bits.
 *
 * This header implements the BvCorr-FLAT engineering model for the fixed-constant branch
 * of modular addition. The public API stays one-shot and header-only, while the internal
 * execution model is explicitly split into:
 *
 * - an offline structural phase on the fixed constant $c$ (run decomposition), and
 * - an online algebraic phase driven by run events and $\beta$-synchronization points.
 *
 * Core algebra:
 * - Let $t = \alpha \oplus \beta$ and pull out the constant sign $(-1)^{\beta \cdot c}$.
 * - Inside a fixed 0-run or 1-run of $c$, the single-bit carry kernels commute.
 * - Any $\beta=0$ interval can therefore be collapsed to a closed-form power update that
 *   depends only on counts of $t$ bits inside that interval.
 * - At positions with $\beta_i = 1$, the sign flip $D = \mathrm{diag}(1,-1)$ is genuinely
 *   non-commuting and must be applied in place before the one-bit kernel step.
 *
 * Exact evaluator:
 * - Result type is a dyadic rational `DyadicCorrelation` with denominator $2^n$.
 * - With the internal run table cached, the natural online complexity is
 *     $O(\#\mathrm{runs}(c) + \mathrm{wt}(\beta))$.
 *
 * Windowed evaluator:
 * - Restrict the exact kernel to the working set
 *     $S = \mathrm{supp}(t) \cup \mathrm{LeftBand}(\beta,L)$.
 * - `LeftBand` is assembled by an exact doubling-style builder, so its word-level cost is
 *     $B(L,n) = O(\log \min\{L+1,n\})$.
 * - Internally, the windowed path is event-driven twice: first by active runs, then by a
 *   per-run dispatch among exact-span, count-only, sparse-event, and segmented kernels.
 * - The coarse engineering bound is therefore read as
 *     $O(B(L,n) + \#\mathrm{runs}(c|_S) + \mathrm{wt}(\beta|_S))$,
 *   and in the worst coarse form as
 *     $O(\log n + \#\mathrm{runs}(c) + \mathrm{wt}(\beta))$.
 *
 * Integer backend (portability):
 * - Uses the built-in fixed-width `SignedInteger128Bit`.
 * - No compiler-specific native 128-bit integer backend is required.
 *
 * Public API (implemented in this header):
 * - Exact evaluators: `linear_correlation_add_const_exact_flat_*`
 * - Windowed evaluators: `linear_correlation_add_const_flat_bin_*`
 * - Paper-order aliases: `corr_add_const_exact_flat_*`, `corr_add_const_flat_bin_report`
 * - Binary-lift / cascade helpers for engineering experiments
 *
 * Internal engineering notes:
 * - One-shot calls reuse a thread-local cached run table keyed by `(constant, n)`.
 * - No outer API change is required to benefit from cached runs or active-run dispatch.
 * - This file targets the var-const case as a structural alternative to the per-bit 2x2 chain
 *   in `linear_correlation_addconst.hpp`.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "arx_analysis_operators/SignedInteger128Bit.hpp"
#include "arx_analysis_operators/UnsignedInteger128Bit.hpp"

namespace TwilightDream::arx_operators
{
	using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

	// ============================================================================
	// Small helpers (n <= 64)
	// ============================================================================

	static inline std::uint64_t mask_n_u64( int n ) noexcept
	{
		return ( n >= 64 ) ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << n ) - 1 );
	}

	static inline std::uint64_t mask_range_u64( int lo, int hi, int n ) noexcept
	{
		if ( n <= 0 )
			return 0;
		if ( lo > hi )
			return 0;
		if ( hi < 0 || lo >= n )
			return 0;
		if ( lo < 0 )
			lo = 0;
		if ( hi >= n )
			hi = n - 1;

		const std::uint64_t m = mask_n_u64( n );

		const std::uint64_t low = ( lo == 0 ) ? m : ( m & ~( ( std::uint64_t( 1 ) << lo ) - 1 ) );
		const std::uint64_t high =
			( hi >= 63 ) ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << ( hi + 1 ) ) - 1 );
		return ( low & high ) & m;
	}

	static inline std::uint64_t lowbit_u64( std::uint64_t x ) noexcept
	{
		return x & ( ~x + 1 );	// x & -x (two's complement)
	}

	// LeftBand(beta,L) = ⋃_{i:β_i=1}[i-L,i]∩[0..n-1], for n<=64.
	// Exact doubling-style builder: grow the covered distance from 1 to L+1 by
	// repeatedly OR-ing a right-shifted copy of the already-covered band.
	// This yields the exact union with O(log min(L+1,n)) shifts instead of O(L).
	static inline std::uint64_t left_band_u64( std::uint64_t beta, int L, int n ) noexcept
	{
		if ( n <= 0 )
			return 0;
		if ( n > 64 )
			n = 64;
		const std::uint64_t m = mask_n_u64( n );
		beta &= m;
		if ( L <= 0 )
			return beta;

		int Lcap = L;
		if ( Lcap > ( n - 1 ) )
			Lcap = ( n - 1 );

		std::uint64_t band = beta;
		int covered = 1;  // currently covers shifts {0,1,...,covered-1}
		while ( covered <= Lcap )
		{
			const int step = std::min( covered, ( Lcap + 1 ) - covered );
			band |= ( band >> step );
			band &= m;
			covered += step;
		}
		return band;
	}

	namespace detail
	{
		struct FlatRunInfo
		{
			int			 start { 0 };
			int			 length { 0 };
			int			 bit { 0 };
			std::uint64_t rel_mask { 0 };
			std::uint64_t abs_mask { 0 };
		};

		struct FlatRunTable
		{
			std::uint64_t		 constant { 0 };
			int				 n { 0 };
			int				 run_count { 0 };
			std::array<FlatRunInfo, 64> runs {};
			std::array<std::uint8_t, 64> bit_to_run {};
		};

		[[nodiscard]] static inline FlatRunTable build_flat_run_table( std::uint64_t constant, int n ) noexcept
		{
			FlatRunTable table {};
			if ( n <= 0 )
				return table;
			if ( n > 64 )
				n = 64;

			const std::uint64_t masked_constant = constant & mask_n_u64( n );
			table.constant = masked_constant;
			table.n = n;

			for ( int i = 0; i < n; )
			{
				const int run_bit = int( ( masked_constant >> i ) & 1ULL );
				int	  j = i;
				while ( ( j + 1 ) < n && int( ( masked_constant >> ( j + 1 ) ) & 1ULL ) == run_bit )
					++j;

				const int run_len = ( j - i + 1 );
				const std::uint64_t rel_mask =
					( run_len >= 64 ) ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << run_len ) - 1 );
				const std::uint64_t abs_mask = mask_range_u64( i, j, n );

				table.runs[table.run_count] = FlatRunInfo { i, run_len, run_bit, rel_mask, abs_mask };
				for ( int bit = i; bit <= j; ++bit )
					table.bit_to_run[bit] = static_cast<std::uint8_t>( table.run_count );
				++table.run_count;
				i = j + 1;
			}
			return table;
		}

		struct FlatRunCacheSlot
		{
			bool		 valid { false };
			std::uint64_t constant { 0 };
			int			 n { 0 };
			FlatRunTable table {};
		};

		[[nodiscard]] static inline const FlatRunTable& get_flat_run_table_cached( std::uint64_t constant, int n ) noexcept
		{
			if ( n <= 0 )
			{
				static const FlatRunTable empty_table {};
				return empty_table;
			}
			if ( n > 64 )
				n = 64;
			const std::uint64_t masked_constant = constant & mask_n_u64( n );

			thread_local FlatRunCacheSlot cache {};
			if ( !cache.valid || cache.constant != masked_constant || cache.n != n )
			{
				cache.table = build_flat_run_table( masked_constant, n );
				cache.constant = masked_constant;
				cache.n = n;
				cache.valid = true;
			}
			return cache.table;
		}
	}  // namespace detail

	// ============================================================================
	// Dyadic correlation report (exact numerator / power-of-two denominator)
	// ============================================================================

	namespace detail
	{
		using WideInt = SignedInteger128Bit;

		static inline SignedInteger128Bit shl_wide( const SignedInteger128Bit& x, int k ) noexcept
		{
			return x.shl( k );
		}

		[[nodiscard]] static inline bool is_zero_wide( const SignedInteger128Bit& x ) noexcept
		{
			return x.is_zero();
		}

		[[nodiscard]] static inline long double to_long_double_wide( const SignedInteger128Bit& x ) noexcept
		{
			return x.to_long_double();
		}

		[[nodiscard]] static inline double to_double_wide( const SignedInteger128Bit& x ) noexcept
		{
			return x.to_double();
		}

		// floor(log2(|x|)) as a 0-based bit index, for x != 0.
		[[nodiscard]] static inline int abs_msb_index_wide( const SignedInteger128Bit& x ) noexcept
		{
			const int bw = x.bit_width_abs();
			return bw - 1;
		}
	}  // namespace detail

	struct DyadicCorrelation
	{
		// correlation = numerator / 2^{denom_log2}
		detail::WideInt numerator { 0 };
		int				denom_log2 { 0 };

		[[nodiscard]] inline long double as_long_double() const noexcept
		{
			// NOTE: On MSVC, long double == double (precision loss possible for n=64).
			return std::scalbn( detail::to_long_double_wide( numerator ), -denom_log2 );
		}

		[[nodiscard]] inline double as_double() const noexcept
		{
			return std::ldexp( detail::to_double_wide( numerator ), -denom_log2 );
		}
	};

	// ============================================================================
	// Internal: exact flattened evaluator (row-vector kernels, scaled integers)
	// ============================================================================

	namespace detail
	{
		struct Acc2Row
		{
			// Represents a scaled row vector:
			//   v = (v0, v1) / 2^{scale}
			// where v0/v1 correspond to carry state s ∈ {0,1}.
			WideInt v0 { 1 };
			WideInt v1 { 0 };
			int		 scale { 0 };
		};

		// ---- 0-run (c_i=0): kernels for t=0/1 commute within the run ----
		// K0 = [[1,0],[1/2,1/2]], K1 = [[0,0],[1/2,-1/2]]

		static inline void apply_0run_t0_pow( Acc2Row& a, int k ) noexcept
		{
			if ( k <= 0 )
				return;
			const WideInt v0 = a.v0;
			const WideInt v1 = a.v1;
			// (v0,v1) * K0^k, scaled by 2^k:
			//   W0 = (v0+v1)*2^k - v1
			//   W1 = v1
			a.v0 = shl_wide( ( v0 + v1 ), k ) - v1;
			a.v1 = v1;
			a.scale += k;
		}

		static inline void apply_0run_t1_pow( Acc2Row& a, int k ) noexcept
		{
			if ( k <= 0 )
				return;
			const WideInt v1 = a.v1;
			// (v0,v1) * K1^k, scaled by 2^k:
			//   depends only on v1; (v0 is annihilated)
			if ( k & 1 )
			{
				a.v0 = v1;
				a.v1 = -v1;
			}
			else
			{
				a.v0 = -v1;
				a.v1 = v1;
			}
			a.scale += k;
		}

		// ---- 1-run (c_i=1): kernels for t=0/1 commute within the run ----
		// K0' = [[1/2,1/2],[0,1]], K1' = [[1/2,-1/2],[0,0]]

		static inline void apply_1run_t0_pow( Acc2Row& a, int k ) noexcept
		{
			if ( k <= 0 )
				return;
			const WideInt v0 = a.v0;
			const WideInt v1 = a.v1;
			// (v0,v1) * (K0')^k, scaled by 2^k:
			//   W0 = v0
			//   W1 = (v0+v1)*2^k - v0
			a.v0 = v0;
			a.v1 = shl_wide( ( v0 + v1 ), k ) - v0;
			a.scale += k;
		}

		static inline void apply_1run_t1_pow( Acc2Row& a, int k ) noexcept
		{
			if ( k <= 0 )
				return;
			const WideInt v0 = a.v0;
			// (v0,v1) * (K1')^k, scaled by 2^k:
			//   depends only on v0; v1 is annihilated
			a.v0 = v0;
			a.v1 = -v0;
			a.scale += k;
		}

		// ---- β=1 singleton steps (must apply D=diag(1,-1) in place) ----

		static inline void step_beta1_0run( Acc2Row& a, int tbit ) noexcept
		{
			// Apply D at β=1: (v0,v1) -> (v0,-v1)
			a.v1 = -a.v1;
			const WideInt v0 = a.v0;
			const WideInt v1 = a.v1;

			if ( tbit == 0 )
			{
				// (v0,v1) * K0, scaled by 2:
				//   W0 = 2*v0 + v1
				//   W1 = v1
				a.v0 = ( v0 + v0 ) + v1;
				a.v1 = v1;
			}
			else
			{
				// (v0,v1) * K1, scaled by 2:
				//   W0 = v1
				//   W1 = -v1
				a.v0 = v1;
				a.v1 = -v1;
			}
			a.scale += 1;
		}

		static inline void step_beta1_1run( Acc2Row& a, int tbit ) noexcept
		{
			// Apply D at β=1: (v0,v1) -> (v0,-v1)
			a.v1 = -a.v1;
			const WideInt v0 = a.v0;
			const WideInt v1 = a.v1;

			if ( tbit == 0 )
			{
				// (v0,v1) * K0', scaled by 2:
				//   W0 = v0
				//   W1 = v0 + 2*v1
				a.v0 = v0;
				a.v1 = v0 + ( v1 + v1 );
			}
			else
			{
				// (v0,v1) * K1', scaled by 2:
				//   W0 = v0
				//   W1 = -v0
				a.v0 = v0;
				a.v1 = -v0;
			}
			a.scale += 1;
		}

		static inline void apply_run_counts( Acc2Row& acc, int run_bit, int k0, int k1 ) noexcept
		{
			if ( run_bit == 0 )
			{
				apply_0run_t0_pow( acc, k0 );
				apply_0run_t1_pow( acc, k1 );
			}
			else
			{
				apply_1run_t0_pow( acc, k0 );
				apply_1run_t1_pow( acc, k1 );
			}
		}

		static inline void process_run_exact_span(
			Acc2Row&		 acc,
			int			 run_bit,
			std::uint64_t t_rel,
			std::uint64_t beta_rel,
			int			 run_len
		) noexcept
		{
			int seg = 0;
			while ( true )
			{
				const int bpos = ( beta_rel != 0 ) ? std::countr_zero( beta_rel ) : run_len;
				const int len = bpos - seg;
				if ( len > 0 )
				{
					const std::uint64_t seg_mask_rel =
						( len >= 64 ) ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << len ) - 1 );
					const int k1 = std::popcount( ( t_rel >> seg ) & seg_mask_rel );
					apply_run_counts( acc, run_bit, len - k1, k1 );
				}

				if ( beta_rel == 0 )
					break;

				const int tbit = int( ( t_rel >> bpos ) & 1ULL );
				if ( run_bit == 0 )
					step_beta1_0run( acc, tbit );
				else
					step_beta1_1run( acc, tbit );

				beta_rel &= ( beta_rel - 1 );
				seg = bpos + 1;
				if ( seg >= run_len )
					break;
			}
		}

		static inline void process_run_window_segmented(
			Acc2Row&		 acc,
			int			 run_bit,
			std::uint64_t t_rel,
			std::uint64_t beta_rel,
			std::uint64_t active_rel,
			int			 run_len
		) noexcept
		{
			int seg = 0;
			while ( true )
			{
				const int bpos = ( beta_rel != 0 ) ? std::countr_zero( beta_rel ) : run_len;
				const int len = bpos - seg;
				if ( len > 0 )
				{
					const std::uint64_t seg_mask_rel =
						( len >= 64 ) ? ~std::uint64_t( 0 ) : ( ( std::uint64_t( 1 ) << len ) - 1 );
					const std::uint64_t active_seg = ( active_rel >> seg ) & seg_mask_rel;
					const int kS = std::popcount( active_seg );
					if ( kS > 0 )
					{
						const int k1 = std::popcount( active_seg & ( ( t_rel >> seg ) & seg_mask_rel ) );
						apply_run_counts( acc, run_bit, kS - k1, k1 );
					}
				}

				if ( beta_rel == 0 )
					break;

				const int tbit = int( ( t_rel >> bpos ) & 1ULL );
				if ( run_bit == 0 )
					step_beta1_0run( acc, tbit );
				else
					step_beta1_1run( acc, tbit );

				beta_rel &= ( beta_rel - 1 );
				seg = bpos + 1;
				if ( seg >= run_len )
					break;
			}
		}

		static inline void process_run_window_sparse_events(
			Acc2Row&		 acc,
			int			 run_bit,
			std::uint64_t t_rel,
			std::uint64_t beta_rel,
			std::uint64_t active_rel
		) noexcept
		{
			std::uint64_t events = active_rel;
			int k0 = 0;
			int k1 = 0;
			while ( events != 0 )
			{
				const std::uint64_t bit = lowbit_u64( events );
				events ^= bit;
				const bool is_beta = ( beta_rel & bit ) != 0;
				const bool tbit = ( t_rel & bit ) != 0;

				if ( is_beta )
				{
					if ( k0 != 0 || k1 != 0 )
					{
						apply_run_counts( acc, run_bit, k0, k1 );
						k0 = 0;
						k1 = 0;
					}
					if ( run_bit == 0 )
						step_beta1_0run( acc, tbit ? 1 : 0 );
					else
						step_beta1_1run( acc, tbit ? 1 : 0 );
				}
				else
				{
					if ( tbit )
						++k1;
					else
						++k0;
				}
			}

			if ( k0 != 0 || k1 != 0 )
				apply_run_counts( acc, run_bit, k0, k1 );
		}

		[[nodiscard]] static inline std::uint64_t build_active_run_mask(
			const FlatRunTable& run_table,
			std::uint64_t		 active_mask
		) noexcept
		{
			std::uint64_t active_runs = 0;
			while ( active_mask != 0 )
			{
				const int bit_index = std::countr_zero( active_mask );
				const std::uint8_t run_index = run_table.bit_to_run[bit_index];
				const FlatRunInfo& run = run_table.runs[run_index];
				active_runs |= ( std::uint64_t( 1 ) << run_index );
				active_mask &= ~run.abs_mask;
			}
			return active_runs;
		}

	}  // namespace detail

	/**
	 * @brief Exact flattened correlation for y = x ⊞ constant (n <= 64).
	 *
	 * Returns an exact dyadic rational (numerator / 2^{denom_log2}).
	 * For exact evaluation over all n bits, denom_log2 == n.
	 */
	[[nodiscard]] static inline DyadicCorrelation linear_correlation_add_const_exact_flat_dyadic(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n
	) noexcept
	{
		if ( n <= 0 )
			return DyadicCorrelation { detail::WideInt( 1 ), 0 };
		if ( n > 64 )
			n = 64;

		const std::uint64_t m = mask_n_u64( n );
		alpha &= m;
		beta &= m;
		constant &= m;

		const detail::FlatRunTable& run_table = detail::get_flat_run_table_cached( constant, n );

		// Pull out constant sign: (-1)^{β·c}
		const int parity_bc = std::popcount( beta & run_table.constant ) & 1;

		// Residual input mask: t = α ⊕ β
		const std::uint64_t t = ( alpha ^ beta ) & m;

		detail::Acc2Row acc;
		for ( int run_index = 0; run_index < run_table.run_count; ++run_index )
		{
			const detail::FlatRunInfo& run = run_table.runs[run_index];
			const std::uint64_t beta_rel = ( beta >> run.start ) & run.rel_mask;
			const std::uint64_t t_rel = ( t >> run.start ) & run.rel_mask;
			detail::process_run_exact_span( acc, run.bit, t_rel, beta_rel, run.length );
		}

		// Sum over final carry state (s_n ∈ {0,1})
		detail::WideInt num = acc.v0 + acc.v1;
		if ( parity_bc )
			num = -num;

		return DyadicCorrelation { num, acc.scale };
	}

	// ============================================================================
	// Internal: restricted working-set evaluator (windowed variant)
	// ============================================================================

	namespace detail
	{
		// Evaluate using the same flattened machinery, but only apply kernel updates on the
		// active mask. Bits outside `active_mask` are treated as "ignored for carry influence".
		// The output is normalized to denom_log2 == n (so it is comparable to the exact correlation).
		// Internally this is event-driven twice: first by active runs, then by a sparse-vs-segmented
		// per-run dispatch so sparse windows do not pay for dense interval logic.
		[[nodiscard]] static inline DyadicCorrelation linear_correlation_add_const_flat_on_mask_dyadic(
			std::uint64_t alpha,
			std::uint64_t constant,
			std::uint64_t beta,
			std::uint64_t active_mask,
			int			  n
		) noexcept
		{
			if ( n <= 0 )
				return DyadicCorrelation { detail::WideInt( 1 ), 0 };
			if ( n > 64 )
				n = 64;

			const std::uint64_t m = mask_n_u64( n );
			alpha &= m;
			beta &= m;
			constant &= m;
			active_mask &= m;

			// Ensure β singletons are always processed (LeftBand includes β, but keep API safe).
			active_mask |= beta;
			const detail::FlatRunTable& run_table = detail::get_flat_run_table_cached( constant, n );

			// Pull out constant sign: (-1)^{β·c}
			const int parity_bc = std::popcount( beta & run_table.constant ) & 1;

			// Residual input mask: t = α ⊕ β
			const std::uint64_t t = ( alpha ^ beta ) & m;

			detail::Acc2Row acc;
			std::uint64_t active_runs = detail::build_active_run_mask( run_table, active_mask );
			while ( active_runs != 0 )
			{
				const std::uint64_t active_run_bit = lowbit_u64( active_runs );
				active_runs ^= active_run_bit;
				const int run_index = std::countr_zero( active_run_bit );
				const detail::FlatRunInfo& run = run_table.runs[run_index];

				const std::uint64_t active_rel = ( active_mask >> run.start ) & run.rel_mask;
				const std::uint64_t beta_rel = ( beta >> run.start ) & run.rel_mask;
				const std::uint64_t t_rel = ( t >> run.start ) & run.rel_mask;

				if ( active_rel == run.rel_mask )
				{
					detail::process_run_exact_span( acc, run.bit, t_rel, beta_rel, run.length );
					continue;
				}

				const int active_count = std::popcount( active_rel );
				const int beta_count = std::popcount( beta_rel );
				const int event_count = active_count + beta_count;

				if ( beta_count == 0 )
				{
					const int k1 = std::popcount( active_rel & t_rel );
					detail::apply_run_counts( acc, run.bit, active_count - k1, k1 );
				}
				else if ( event_count <= 8 || active_count <= 4 )
				{
					detail::process_run_window_sparse_events( acc, run.bit, t_rel, beta_rel, active_rel );
				}
				else
				{
					detail::process_run_window_segmented( acc, run.bit, t_rel, beta_rel, active_rel, run.length );
				}
			}

			// Sum over final carry state (s_n ∈ {0,1})
			detail::WideInt num = acc.v0 + acc.v1;
			if ( parity_bc )
				num = -num;

			// Normalize to n-bit denominator to match C(α,β) definition.
			const int missing = n - acc.scale;
			if ( missing > 0 )
				num = detail::shl_wide( num, missing );

			return DyadicCorrelation { num, n };
		}
	}  // namespace detail

	[[nodiscard]] static inline long double linear_correlation_add_const_exact_flat_ld(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat_dyadic( alpha, constant, beta, n ).as_long_double();
	}

	[[nodiscard]] static inline double linear_correlation_add_const_exact_flat(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat_dyadic( alpha, constant, beta, n ).as_double();
	}

	// Exact integer ceil-weight: ceil(-log2(|corr|)).
	// For corr = num / 2^d, this equals: d - floor(log2(|num|)), for num != 0.
	[[nodiscard]] static inline SearchWeight linear_correlation_add_const_exact_flat_weight_ceil_int(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n
	) noexcept
	{
		const DyadicCorrelation r = linear_correlation_add_const_exact_flat_dyadic( alpha, constant, beta, n );
		if ( detail::is_zero_wide( r.numerator ) )
			return TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;

		const int msb_index = detail::abs_msb_index_wide( r.numerator );

		return static_cast<SearchWeight>( r.denom_log2 - msb_index );
	}

	// ============================================================================
	// Paper-order aliases: (alpha, beta, constant, n)
	// ============================================================================

	[[nodiscard]] static inline DyadicCorrelation corr_add_const_exact_flat_dyadic(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat_dyadic( alpha, constant, beta, n );
	}

	[[nodiscard]] static inline long double corr_add_const_exact_flat_ld(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat_ld( alpha, constant, beta, n );
	}

	[[nodiscard]] static inline double corr_add_const_exact_flat(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat( alpha, constant, beta, n );
	}

	[[nodiscard]] static inline SearchWeight corr_add_const_exact_flat_weight_ceil_int(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n
	) noexcept
	{
		return linear_correlation_add_const_exact_flat_weight_ceil_int( alpha, constant, beta, n );
	}

	// ============================================================================
	// Windowed estimator (BvCorr-FLAT): working-set restriction + certified bound
	// ============================================================================

	struct WindowedCorrelationReport
	{
		DyadicCorrelation corr_hat;		   // Ĉ_L as dyadic with denom_log2 == n
		long double		  delta_bound { 0 };  // certified absolute bound: 2 * wt(beta) * 2^{-L}
		long double		  weight_conservative { 0 };
		std::uint64_t	  working_set_mask { 0 };	// S = supp(alpha^beta) ∪ LeftBand(beta,L)
		int				  n { 0 };
		int				  L_used { 0 };
	};

	[[nodiscard]] static inline WindowedCorrelationReport linear_correlation_add_const_flat_bin_report(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n,
		int			  L
	) noexcept
	{
		if ( n <= 0 )
			return WindowedCorrelationReport { DyadicCorrelation { detail::WideInt( 1 ), 0 }, 0.0L, 0.0L, 0, 0, L };
		if ( n > 64 )
			n = 64;
		if ( L < 0 )
			L = 0;

		const std::uint64_t m = mask_n_u64( n );
		alpha &= m;
		beta &= m;
		constant &= m;

		const std::uint64_t t = ( alpha ^ beta ) & m;
		const std::uint64_t S = ( t | left_band_u64( beta, L, n ) ) & m;

		const DyadicCorrelation chat = detail::linear_correlation_add_const_flat_on_mask_dyadic( alpha, constant, beta, S, n );

		const int wt_beta = std::popcount( beta );
		// Certified bound on |C-Ĉ_L| for this truncation model:
		// |E[f]-E[g]| <= E[|f-g|] = 2·Pr[f!=g], hence the factor 2.
		const long double delta = 2.0L * static_cast<long double>( wt_beta ) * std::ldexp( 1.0L, -L );

		const long double abs_hat = std::fabsl( chat.as_long_double() );
		const long double floor_mag = std::ldexp( 1.0L, -n );
		const long double lower_mag = std::max( abs_hat - delta, floor_mag );
		const long double w = -std::log2( lower_mag );

		return WindowedCorrelationReport { chat, delta, w, S, n, L };
	}

	[[nodiscard]] static inline DyadicCorrelation linear_correlation_add_const_flat_bin_dyadic(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n,
		int			  L
	) noexcept
	{
		return linear_correlation_add_const_flat_bin_report( alpha, constant, beta, n, L ).corr_hat;
	}

	[[nodiscard]] static inline long double linear_correlation_add_const_flat_bin_ld(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n,
		int			  L
	) noexcept
	{
		return linear_correlation_add_const_flat_bin_dyadic( alpha, constant, beta, n, L ).as_long_double();
	}

	[[nodiscard]] static inline double linear_correlation_add_const_flat_bin(
		std::uint64_t alpha,
		std::uint64_t constant,
		std::uint64_t beta,
		int			  n,
		int			  L
	) noexcept
	{
		return linear_correlation_add_const_flat_bin_dyadic( alpha, constant, beta, n, L ).as_double();
	}

	// Paper-order alias: (alpha, beta, constant, n, L)
	[[nodiscard]] static inline WindowedCorrelationReport corr_add_const_flat_bin_report(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n,
		int			  L
	) noexcept
	{
		return linear_correlation_add_const_flat_bin_report( alpha, constant, beta, n, L );
	}

	// ============================================================================
	// Binary-Lift (k=1 layer) + windowed residual estimator
	// ============================================================================

	struct BinaryLiftMasks
	{
		std::uint64_t u { 0 }, v { 0 }, w { 0 };
		std::uint64_t beta_res { 0 };
		std::uint64_t t_res { 0 };
	};

	[[nodiscard]] static inline BinaryLiftMasks binary_lift_addconst_masks(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n
	) noexcept
	{
		if ( n <= 0 )
			return BinaryLiftMasks {};
		if ( n > 64 )
			n = 64;
		const std::uint64_t m = mask_n_u64( n );
		alpha &= m;
		beta &= m;
		constant &= m;

		const std::uint64_t carry1 = ( constant & ( beta << 1 ) ) & m;
		const std::uint64_t u = ( beta ^ carry1 ) & m;
		const std::uint64_t v = beta;
		const std::uint64_t w = beta;
		const std::uint64_t beta_res = ( beta & ~carry1 ) & m;	// = beta & ~(c<<1) within n bits
		const std::uint64_t t_res = ( ( alpha ^ beta ) ^ carry1 ) & m;

		return BinaryLiftMasks { u, v, w, beta_res, t_res };
	}

	struct BinaryLiftedWindowedReport
	{
		std::uint64_t			  u { 0 }, v { 0 }, w { 0 };  // display-layer masks
		WindowedCorrelationReport residual;          // residual certified estimator on (t_res, beta_res)
	};

	[[nodiscard]] static inline BinaryLiftedWindowedReport corr_add_const_binary_lifted_report(
		std::uint64_t alpha,
		std::uint64_t beta,
		std::uint64_t constant,
		int			  n,
		int			  L
	) noexcept
	{
		if ( L < 2 )
			L = 2;
		const BinaryLiftMasks lift = binary_lift_addconst_masks( alpha, beta, constant, n );
		// Make internal t = alpha'^beta' equal to t_res.
		const std::uint64_t alpha_res = lift.t_res ^ lift.beta_res;
		const WindowedCorrelationReport r = linear_correlation_add_const_flat_bin_report( alpha_res, constant, lift.beta_res, n, L );
		return BinaryLiftedWindowedReport { lift.u, lift.v, lift.w, r };
	}

	// ============================================================================
	// Cascades (product estimator, additive error accounting)
	// ============================================================================

	struct CascadeRound
	{
		std::uint64_t alpha { 0 };
		std::uint64_t beta { 0 };
		std::uint64_t constant { 0 };
		int			  n { 0 };
		int			  L { 0 };
		bool		  lift { false };
	};

	struct CascadeReport
	{
		long double corr_hat { 1.0L };
		long double delta_sum { 0.0L };            // Σ_j δ_j under the product-estimator model
		long double weight_conservative { 0.0L };
	};

	[[nodiscard]] static inline CascadeReport corr_add_const_cascade( std::span<const CascadeRound> rounds ) noexcept
	{
		if ( rounds.empty() )
			return CascadeReport {};

		int n = rounds[0].n;
		if ( n <= 0 )
			return CascadeReport { 1.0L, 0.0L, 0.0L };
		if ( n > 64 )
			n = 64;

		long double chat = 1.0L;
		long double delta = 0.0L;

		for ( const CascadeRound& rd : rounds )
		{
			const int nrd = ( rd.n <= 0 ) ? n : ( rd.n > 64 ? 64 : rd.n );
			const int Lrd = ( rd.L < 0 ) ? 0 : rd.L;

			if ( rd.lift )
			{
				const int Luse = ( Lrd < 2 ) ? 2 : Lrd;
				const BinaryLiftMasks liftm = binary_lift_addconst_masks( rd.alpha, rd.beta, rd.constant, nrd );
				const std::uint64_t alpha_res = liftm.t_res ^ liftm.beta_res;
				const WindowedCorrelationReport r = linear_correlation_add_const_flat_bin_report( alpha_res, rd.constant, liftm.beta_res, nrd, Luse );
				chat *= r.corr_hat.as_long_double();
				delta += 2.0L * static_cast<long double>( std::popcount( liftm.beta_res & mask_n_u64( nrd ) ) ) * std::ldexp( 1.0L, -Luse );
			}
			else
			{
				const WindowedCorrelationReport r = linear_correlation_add_const_flat_bin_report( rd.alpha, rd.constant, rd.beta, nrd, Lrd );
				chat *= r.corr_hat.as_long_double();
				delta += 2.0L * static_cast<long double>( std::popcount( rd.beta & mask_n_u64( nrd ) ) ) * std::ldexp( 1.0L, -Lrd );
			}
		}

		const long double floor_mag = std::ldexp( 1.0L, -n );
		const long double lower_mag = std::max( std::fabsl( chat ) - delta, floor_mag );
		const long double w = -std::log2( lower_mag );

		return CascadeReport { chat, delta, w };
	}

}  // namespace TwilightDream::arx_operators
