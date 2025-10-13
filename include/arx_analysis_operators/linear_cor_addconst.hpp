#pragma once
/**
 * @file linear_cor_addconst.hpp
 * @brief Exact linear correlation for modular addition (constant & variable cases)
 *
 * This header provides a **reference-correct** implementation of linear
 * correlation for ARX additions using **per-bit 2×2 carry-state transfer
 * matrices** (Wallén-style). It supports:
 *
 *   1) var-const addition:   z = x ⊞ a           (or subtraction x ⊟ a)
 *      => averaging factor  = 1/2 = 0.5  (only x_i ∈ {0,1} varies)
 *
 *   2) var-var   addition:   z = x ⊞ y
 *      => averaging factor  = 1/4 = 0.25 (x_i,y_i ∈ {0,1}^2 take 4 cases)
 *
 * Both compute the **exact correlation**
 *     corr = E[ (-1)^{ α·x  ⊕  β·z } ]              (var-const)
 *     corr = E[ (-1)^{ α·x  ⊕  γ·y  ⊕  β·z } ]      (var-var)
 * with z = x ⊞ a or z = x ⊞ y under **uniform** input bits, and
 * z_i = x_i ⊕ a_i ⊕ c_i  (or x_i ⊕ y_i ⊕ c_i) with carries c_i.
 *
 * We follow the classic **carry Markov chain** of two states per bit:
 *   state index = carry-in c_{i-1} ∈ {0,1}
 * each bit contributes a 2×2 matrix Mi (rows = cin, cols = cout) whose
 * entries are local **bias averages** over the bit(s) that are random.
 *
 * Accumulation:
 *   v_{i+1} = v_i · Mi   with v_0 = [1, 0]
 *   corr    = sum(v_n)   (final carry not constrained)
 *
 * Linear weight (for trail cost):
 *   Lw = -log2( |corr| ),  with |corr| ∈ [0,1],  Lw=+∞ if corr=0.
 *
 * IMPORTANT — The 0.5 vs 0.25 difference:
 *   - var-const bit: average over x_i only  (two cases)        ⇒ 1/2
 *   - var-var   bit: average over x_i,y_i   (four cases)       ⇒ 1/4
 *
 * References (algorithmic idea):
 *   - J. Wallén, "Linear Approximations of Addition Modulo 2^n", FSE 2003.
 *
 * This implementation is **deterministic, exact, and auditable**; it is O(n)
 * per correlation evaluation and suitable as a correctness baseline as well as
 * for small/medium size automated searches.
 */

#include <cstdint>
#include <cmath>
#include <limits>
#include <type_traits>

namespace neoalz
{
	namespace arx_operators
	{

		// ============================================================================
		//                              Utility helpers
		// ============================================================================

		/**
		 * @brief Get i-th bit (LSB=bit0)
		 */
		static inline int bit_u64( uint64_t v, int i ) noexcept
		{
			return static_cast<int>( ( v >> i ) & 1ULL );
		}

		/**
		 * @brief Full-adder carry out 1-bit: cout = MAJ(x, y, cin)
		 *        cout = (x & y) | (x & cin) | (y & cin)
		 */
		static inline int carry_out_bit( int x, int y, int cin ) noexcept
		{
			return ( x & y ) | ( x & cin ) | ( y & cin );
		}

		/**
		 * @brief (-1)^(mask_bit * value_bit)
		 *        If mask_bit==0 => +1, else => (-1)^(value_bit).
		 *        This is the building block for linear bias contributions.
		 */
		static inline double signed_bit( int mask_bit, int value_bit ) noexcept
		{
			// (mask_bit ? (value_bit ? -1.0 : 1.0) : 1.0)
			return mask_bit ? ( value_bit ? -1.0 : 1.0 ) : 1.0;
		}

		/**
		 * @brief Safe conversion from correlation to linear weight
		 *        Lw = -log2(|corr|), with Lw = +inf for corr == 0.
		 */
		static inline double linear_weight_from_corr( double corr ) noexcept
		{
			const double a = std::fabs( corr );
			if ( a <= 0.0 )
				return std::numeric_limits<double>::infinity();
			return -std::log2( a );
		}

		// ============================================================================
		//                           2×2 transfer matrix
		// ============================================================================

		/**
		 * @brief 2×2 matrix with row-vector left multiply.
		 * Rows   : cin ∈ {0,1}.
		 * Columns: cout ∈ {0,1}.
		 *
		 * We maintain row-major as four doubles.
		 */
		struct Mat2
		{
			double m00, m01, m10, m11;	// rows: 0,1 ; cols: 0,1

			constexpr Mat2() : m00( 0 ), m01( 0 ), m10( 0 ), m11( 0 ) {}
			constexpr Mat2( double a, double b, double c, double d ) : m00( a ), m01( b ), m10( c ), m11( d ) {}

			/**
			 * @brief Row-vector × Matrix
			 *        [v0 v1] × [m00 m01; m10 m11] = [v0*m00+v1*m10, v0*m01+v1*m11]
			 */
			static inline void mul_row( double v0, double v1, const Mat2& M, double& out0, double& out1 ) noexcept
			{
				out0 = v0 * M.m00 + v1 * M.m10;
				out1 = v0 * M.m01 + v1 * M.m11;
			}

			/**
			 * @brief Quick sanity: row sums (not required to be 1 in general
			 *        because these are correlation-transfer, not probabilities).
			 */
			inline double row_sum0() const noexcept
			{
				return m00 + m01;
			}
			inline double row_sum1() const noexcept
			{
				return m10 + m11;
			}
		};

		// ============================================================================
		//                Bitwise local matrices — exact per-bit formulas
		// ============================================================================

		/**
		 * @section Mathematics summary (bit i)
		 *
		 *  Let cin = c_{i-1}, cout = c_i, z_i = x_i ⊕ y_i ⊕ cin  (or x_i ⊕ a_i ⊕ cin).
		 *
		 *  For a given bit i, the local contribution to correlation is the average
		 *  of (-1)^(⋯) over the random bit(s) on that position, **conditioned** on
		 *  the transition cin -> cout. This gives a 2×2 matrix Mi whose entry is:
		 *
		 *   (var-var)  Mi(cin,cout) = (1/4) ∑_{x,y∈{0,1}} 1_{carry(x,y,cin)=cout}
		 *                                · (-1)^{ α_i x  ⊕  γ_i y  ⊕  β_i (x⊕y⊕cin) }
		 *
		 *   (var-const) Mi(cin,cout) = (1/2) ∑_{x∈{0,1}} 1_{carry(x,a_i,cin)=cout}
		 *                                · (-1)^{ α_i x  ⊕  β_i (x⊕a_i⊕cin) }
		 *
		 *  NOTE the **averaging factor**: 1/4 for var-var, 1/2 for var-const.
		 *  This is exactly why you must not use 0.25 in var-const code.
		 */

		// -- var-var: z = x ⊞ y ------------------------------------------------------

		/**
		 * @brief Build Mi for var-var addition (x ⊞ y) at bit i.
		 * @param alpha_i mask bit for x
		 * @param gamma_i mask bit for y   (we call it gamma to avoid clash with β)
		 * @param beta_i  mask bit for z
		 * @return 2×2 local transfer matrix with averaging factor 1/4.
		 */
		static inline Mat2 make_Mi_add_varvar_bit( int alpha_i, int gamma_i, int beta_i ) noexcept
		{
			Mat2 M;
			for ( int cin = 0; cin <= 1; ++cin )
			{
				for ( int cout = 0; cout <= 1; ++cout )
				{
					double acc = 0.0;
					// Average over all (x,y) ∈ {0,1}^2 ⇒ 4 terms ⇒ factor 1/4.
					for ( int x = 0; x <= 1; ++x )
					{
						for ( int y = 0; y <= 1; ++y )
						{
							if ( carry_out_bit( x, y, cin ) != cout )
								continue;
							const int zi = x ^ y ^ cin;
							double	  s = 1.0;
							// (-1)^(α_i x)
							s *= signed_bit( alpha_i, x );
							// (-1)^(γ_i y)
							s *= signed_bit( gamma_i, y );
							// (-1)^(β_i z_i)
							s *= signed_bit( beta_i, zi );
							acc += s;
						}
					}
					const double val = acc * 0.25;	// 1/4
					if ( cin == 0 && cout == 0 )
						M.m00 = val;
					if ( cin == 0 && cout == 1 )
						M.m01 = val;
					if ( cin == 1 && cout == 0 )
						M.m10 = val;
					if ( cin == 1 && cout == 1 )
						M.m11 = val;
				}
			}
			return M;
		}

		// -- var-const: z = x ⊞ a ----------------------------------------------------

		/**
		 * @brief Build Mi for var-const addition (x ⊞ a) at bit i.
		 * @param alpha_i mask bit for x
		 * @param beta_i  mask bit for z
		 * @param a_i     constant bit a_i ∈ {0,1}
		 * @return 2×2 local transfer matrix with averaging factor 1/2.
		 */
		static inline Mat2 make_Mi_add_const_bit( int alpha_i, int beta_i, int a_i ) noexcept
		{
			Mat2 M;
			for ( int cin = 0; cin <= 1; ++cin )
			{
				for ( int cout = 0; cout <= 1; ++cout )
				{
					double acc = 0.0;
					// Only x varies ⇒ 2 terms ⇒ factor 1/2.
					for ( int x = 0; x <= 1; ++x )
					{
						if ( carry_out_bit( x, a_i, cin ) != cout )
							continue;
						const int zi = x ^ a_i ^ cin;
						double	  s = 1.0;
						s *= signed_bit( alpha_i, x );	// (-1)^(α_i x)
						s *= signed_bit( beta_i, zi );	// (-1)^(β_i z_i)
						acc += s;
					}
					const double val = acc * 0.5;  // 1/2
					if ( cin == 0 && cout == 0 )
						M.m00 = val;
					if ( cin == 0 && cout == 1 )
						M.m01 = val;
					if ( cin == 1 && cout == 0 )
						M.m10 = val;
					if ( cin == 1 && cout == 1 )
						M.m11 = val;
				}
			}
			return M;
		}

		// ============================================================================
		//                    Correlation drivers (exact, O(n))
		// ============================================================================

		/**
		 * @brief Exact correlation for z = x ⊞ y (var-var).
		 *
		 * API contract:
		 *   - alpha: mask for x
		 *   - gamma: mask for y
		 *   - beta : mask for z
		 *   - n    : word size (1..64), LSB = bit 0
		 *
		 * Returns corr ∈ [-1, 1].
		 */
		static inline double corr_add_varvar( uint64_t alpha, uint64_t gamma, uint64_t beta, int n ) noexcept
		{
			double v0 = 1.0, v1 = 0.0;	// v_0 = [1,0]
			for ( int i = 0; i < n; ++i )
			{
				const Mat2 Mi = make_Mi_add_varvar_bit( bit_u64( alpha, i ), bit_u64( gamma, i ), bit_u64( beta, i ) );
				double	   nv0, nv1;
				Mat2::mul_row( v0, v1, Mi, nv0, nv1 );
				v0 = nv0;
				v1 = nv1;
			}
			// sum over final carry
			return ( v0 + v1 );
		}

		/**
		 * @brief Exact correlation for z = x ⊞ a (var-const).
		 *
		 * API contract:
		 *   - alpha: mask for x
		 *   - beta : mask for z
		 *   - a    : n-bit constant (will be masked to n bits)
		 *   - n    : word size (1..64), LSB = bit 0
		 *
		 * Returns corr ∈ [-1, 1].
		 */
		static inline double corr_add_const( uint64_t alpha, uint64_t beta, uint64_t a, int n ) noexcept
		{
			const uint64_t MASK = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			a &= MASK;
			double v0 = 1.0, v1 = 0.0;	// v_0 = [1,0]
			for ( int i = 0; i < n; ++i )
			{
				const Mat2 Mi = make_Mi_add_const_bit( bit_u64( alpha, i ), bit_u64( beta, i ), bit_u64( a, i ) );
				double	   nv0, nv1;
				Mat2::mul_row( v0, v1, Mi, nv0, nv1 );
				v0 = nv0;
				v1 = nv1;
			}
			return ( v0 + v1 );
		}

		/**
		 * @brief Exact correlation for z = x ⊟ a (var-const subtraction).
		 *        Implemented as z = x ⊞ ((-a) mod 2^n).
		 */
		static inline double corr_sub_const( uint64_t alpha, uint64_t beta, uint64_t a, int n ) noexcept
		{
			const uint64_t MASK = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const uint64_t a_neg = ( ( ~a ) + 1ULL ) & MASK;  // two's complement mod 2^n
			return corr_add_const( alpha, beta, a_neg, n );
		}

		// ============================================================================
		//         High-level wrappers (32/64-bit, weight, convenience structs)
		// ============================================================================

		/**
		 * @brief Result pair: correlation and linear weight.
		 */
		struct LinearCorrelation
		{
			double correlation { 0.0 };									// corr ∈ [-1,1]
			double weight { std::numeric_limits<double>::infinity() };	// Lw = -log2(|corr|)

			constexpr LinearCorrelation() = default;
			constexpr LinearCorrelation( double c, double w ) : correlation( c ), weight( w ) {}

			inline bool is_feasible() const noexcept
			{
				return ( correlation != 0.0 ) && !std::isinf( weight );
			}
		};

		/**
		 * @brief Helper to wrap corr → LinearCorrelation.
		 */
		static inline LinearCorrelation make_lc( double corr ) noexcept
		{
			const double w = linear_weight_from_corr( corr );
			return LinearCorrelation { corr, w };
		}

		// -- var-const: 32/64 --------------------------------------------------------

		static inline LinearCorrelation corr_add_x_plus_const32( uint32_t alpha, uint32_t beta, uint32_t K, int nbits = 32 ) noexcept
		{
			const double c = corr_add_const( alpha, beta, K, nbits );
			return make_lc( c );
		}

		static inline LinearCorrelation corr_add_x_minus_const32( uint32_t alpha, uint32_t beta, uint32_t C, int nbits = 32 ) noexcept
		{
			const double c = corr_sub_const( alpha, beta, C, nbits );
			return make_lc( c );
		}

		static inline LinearCorrelation corr_add_x_plus_const64( uint64_t alpha, uint64_t beta, uint64_t K, int nbits = 64 ) noexcept
		{
			const double c = corr_add_const( alpha, beta, K, nbits );
			return make_lc( c );
		}

		static inline LinearCorrelation corr_add_x_minus_const64( uint64_t alpha, uint64_t beta, uint64_t C, int nbits = 64 ) noexcept
		{
			const double c = corr_sub_const( alpha, beta, C, nbits );
			return make_lc( c );
		}

		// -- var-var: 32/64 ----------------------------------------------------------

		static inline LinearCorrelation corr_add_varvar32( uint32_t alpha, uint32_t gamma, uint32_t beta, int nbits = 32 ) noexcept
		{
			const double c = corr_add_varvar( alpha, gamma, beta, nbits );
			return make_lc( c );
		}

		static inline LinearCorrelation corr_add_varvar64( uint64_t alpha, uint64_t gamma, uint64_t beta, int nbits = 64 ) noexcept
		{
			const double c = corr_add_varvar( alpha, gamma, beta, nbits );
			return make_lc( c );
		}

		// ============================================================================
		//                    Programmer-friendly documentation
		// ============================================================================

		/**
		 * @section Quick how-to (for typical ARX round code)
		 *
		 * Example: one round does
		 *    t = ROTL(x, r1);
		 *    u = t ⊞ a;                // add constant
		 *    v = u ⊕ y;                // XOR (no weight)
		 *    z = ROTR(v, r2);
		 *
		 * To compute the linear correlation for a single approx with input/output
		 * masks (α for x, β for z and maybe γ for y), push masks through transforms:
		 *
		 * 1) Rotation: only reindex masks, no weight:
		 *       α_t = rotl_n(α_x, r1, n)
		 * 2) Add-const: call exact var-const operator:
		 *       LC lc = corr_add_x_plus_const32(α_t, β_u, a, n);
		 *       // accumulate weight += lc.weight
		 * 3) XOR: linear ⇒ no extra weight, just combine masks accordingly.
		 * 4) Final rotation: reindex to the cipher API's output mask domain.
		 *
		 * For var-var addition (x ⊞ y), call corr_add_varvar32(α_x, γ_y, β_z, n).
		 */

		// ============================================================================
		//                          Optional: tiny self-checks
		// ============================================================================

		/**
		 * These checks are O(n) per call and safe. Define the macro below to enable.
		 *
		 *  - Zero masks ⇒ corr = 1.
		 *  - Subtraction vs addition of two's complement constant.
		 *  - Bound: |corr| ∈ [0,1].
		 *
		 * NOTE: Keep disabled by default to avoid any runtime in hot paths.
		 */
// #define NEOALZ_ENABLE_SMALL_SELFTESTS 1
#ifdef NEOALZ_ENABLE_SMALL_SELFTESTS
		static inline void _selftest_linear_cor_add()
		{
			// 32-bit simple probes
			{
				const int n = 32;
				// All-zero masks ⇒ corr = 1
				double c1 = corr_add_const( 0, 0, 0x12345678u, n );
				double c2 = corr_add_varvar( 0, 0, 0, n );
				if ( std::fabs( c1 - 1.0 ) > 1e-12 )
					__builtin_trap();
				if ( std::fabs( c2 - 1.0 ) > 1e-12 )
					__builtin_trap();
			}
			{
				const int n = 32;
				// Subtraction equals addition of two's complement
				const uint32_t A = 0xDEADBEEFu;
				const uint32_t alpha = 0xA5A5A5A5u;
				const uint32_t beta = 0x5A5A5A5Au;
				const double   c_sub = corr_sub_const( alpha, beta, A, n );
				const double   c_add = corr_add_const( alpha, beta, ( ~uint64_t( A ) + 1ULL ) & 0xFFFFFFFFULL, n );
				if ( std::fabs( c_sub - c_add ) > 1e-12 )
					__builtin_trap();
				if ( !( std::fabs( c_sub ) <= 1.0 + 1e-12 ) )
					__builtin_trap();
			}
		}
#endif	// NEOALZ_ENABLE_SMALL_SELFTESTS

		// ============================================================================
		//                 Explanation (math → code) — long notes
		// ============================================================================

		/*
		===============================================================================

		Why 0.5 for var-const and 0.25 for var-var?
		-------------------------------------------
		We model the correlation as an expectation over all **random** input bits.

		  var-const (z = x ⊞ a):
			  corr = E_x[ (-1)^{ α·x ⊕ β·z } ],  z_i = x_i ⊕ a_i ⊕ c_i
			  Per bit i, **only x_i** is random ⇒ two cases ⇒ average factor 1/2.
			  The local 2×2 entry Mi(cin,cout) is the conditional average of the
			  sign (-1)^(...) over x_i ∈ {0,1}, given carry(x_i, a_i, cin) = cout.

		  var-var   (z = x ⊞ y):
			  corr = E_{x,y}[ (-1)^{ α·x ⊕ γ·y ⊕ β·z } ],  z_i = x_i ⊕ y_i ⊕ c_i
			  Per bit i, **both x_i,y_i** are random ⇒ four cases ⇒ 1/4.
			  Mi(cin,cout) averages (-1)^(...) over the four pairs subject to cout.

		The carry-out relation per bit is the standard full-adder majority:
			  cout = MAJ(x, rhs_bit, cin)

		We multiply these matrices from LSB up because carry flows from LSB→MSB.

		On feasibility vs exactness
		---------------------------
		We do *not* need any feasibility flags here: correlation is exactly computed
		and can be 0 if destructive interference occurs. The linear weight
		Lw = -log2(|corr|) is ∞ (represented by +infinity) when corr=0.

		Rotations and XOR
		-----------------
		Rotations only change bit indices of masks; XOR is a linear operation and
		adds **no** new weight; only additions contribute to weight.

		Templates & types
		-----------------
		We accept 32/64-bit via uint64_t and a parameter n (1..64). The functions
		mask constants to n bits so you can safely pass any 64-bit literal.

		===============================================================================
		*/

	}  // namespace arx_operators
}  // namespace neoalz
