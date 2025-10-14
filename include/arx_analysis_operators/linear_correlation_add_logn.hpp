/**
 * @file linear_correlation_add_logn.hpp
 * @brief 模加法線性相關度 - Θ(log n)對數算法（變量-變量）
 * 
 * 論文：Wallén (2003), "Linear Approximations of Addition Modulo 2^n", FSE 2003
 * 
 * **關鍵發現**：Wallén Theorem 2 + Corollary 1
 * 
 * "The correlation coefficients C(u ← v, w) can be computed in time Θ(log n)"
 * 
 *  Common Prefix Mask = CPM
 *
 * 算法核心：
 * 1. 計算cpm(x, y) - Common Prefix Mask - Θ(log n)時間
 * 2. 使用Theorem 1計算相關度
 * 3. 總複雜度：Θ(log n)
 * 
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include <optional>
#include <functional>

#if defined(__cpp_lib_bitops) && (__cpp_lib_bitops >= 201907L)
#include <bit>      // std::popcount
#endif

#if defined(_MSC_VER)
#include <intrin.h> // _BitScanReverse64
#endif

namespace TwilightDream
{
	namespace arx_operators
	{

		inline constexpr std::uint32_t HammingWeight( std::uint32_t x ) noexcept
		{
			#if defined( __cpp_lib_bitops ) && ( __cpp_lib_bitops >= 201907L )
						return static_cast<std::uint32_t>( std::popcount( x ) );
			#elif defined( _MSC_VER )
						return static_cast<std::uint32_t>( __popcnt( static_cast<unsigned long>( x ) ) );
			#else
						return static_cast<std::uint32_t>( __builtin_popcount( x ) );
			#endif
		}

		// ============================================================================
		// MnT operator implementation (core of Wallén algorithm)
		// ============================================================================

		inline std::uint32_t MnT_of( std::uint32_t v ) noexcept
		{
			// Compute z* = M_n^T v (carry support vector) for 32-bit via prefix XOR trick
			std::uint32_t z = 0;
			std::uint32_t suffix = 0;

			for ( int i = 31; i >= 0; --i )
			{
				if ( suffix & 1u )
					z |= ( 1u << i );
				suffix ^= ( v >> i ) & 1u;
			}

			return z;
		}

		inline std::optional<int> wallen_weight( std::uint32_t mu, std::uint32_t nu, std::uint32_t omega, int n )
		{
			( void )n;	// reserved for future n-bit generalization; keep signature stable
			std::uint32_t v = mu ^ nu ^ omega;
			std::uint32_t z_star = MnT_of( v );
			std::uint32_t a = mu ^ omega;
			std::uint32_t b = nu ^ omega;

			if ( ( a & ~z_star ) != 0 || ( b & ~z_star ) != 0 )
			{
				return std::nullopt;  // Not feasible
			}

			return __builtin_popcount( z_star );
		}

		/**
		 * @brief cpm(x, y) — reference-correct recursive implementation (Definition 3 + Definition 6, Wallén 2003)
		 *
		 * 说明：
		 * - 这里先以“严格按论文定义”的递归写法实现，保证 correctness（用于算子/搜索/单测）。
		 * - 作为 Theorem 2 bit-sliced 版本的对照/回归基准。
		 *
		 * @param x 第一個向量（仅低 n 位有效）
		 * @param y 第二個向量（仅低 n 位有效）
		 * @param n 位宽（1..64）
		 * @return cpm(x, y)（仅低 n 位有效）
		 */
		inline uint64_t compute_cpm_recursive( uint64_t x, uint64_t y, int n = 32 ) noexcept
		{
			if ( n <= 0 )
				return 0ull;
			if ( n > 64 )
				n = 64;

			const uint64_t MASK = ( n == 64 ) ? ~0ull : ( ( 1ull << n ) - 1ull );
			x &= MASK;
			y &= MASK;

			// Bit numbering convention used in this project:
			// - bit 0 = LSB, bit (n-1) = MSB (standard integer bit indices).
			//
			// Wallén's paper writes vectors as (x_{n-1},...,x_0)^t; our uint64_t stores x_i at bit i.
			auto msb_index_u64 = []( std::uint64_t v ) noexcept -> int
			{
#if defined(__cpp_lib_bitops) && (__cpp_lib_bitops >= 201907L)
				// C++20: bit_width(v) = floor(log2(v)) + 1 (for v != 0)
				return v ? static_cast<int>(std::bit_width(v) - 1) : -1;

#elif defined(_MSC_VER)
				unsigned long idx = 0;
				return _BitScanReverse64(&idx, v) ? static_cast<int>(idx) : -1;

#elif defined(__GNUC__) || defined(__clang__)
				// __builtin_clzll(0) is UB, so guard.
				constexpr int kUllBits = static_cast<int>(8 * sizeof(unsigned long long));
				return v ? (kUllBits - 1 - static_cast<int>(__builtin_clzll(static_cast<unsigned long long>(v)))) : -1;

#else
				// Portable fallback
				if ( v == 0 )
					return -1;
				int idx = 0;
				while ( ( v >>= 1 ) != 0 )
					++idx;
				return idx;
#endif
			};

			// Definition 5 (strip): strip(x) clears the highest 1-bit (if any).
			auto strip_msb_once_u64 = [ & ]( uint64_t v ) noexcept -> uint64_t
			{
				const int msb = msb_index_u64( v );
				if ( msb < 0 )
					return 0ull;
				return v & ~( 1ull << msb );
			};

			// -------------------------------------------------------------------------
			// Definition 3 (Wallén 2003): cpm_k^i(y)
			//
			// Paper:
			//   cpm_k^i(y)_j = 1  if  k <= j < k+i  and  y_ℓ = 1 for all j < ℓ < k+i.
			//
			// Engineering interpretation:
			// - Consider the window [k .. k+i-1]. We scan from the top of the window down.
			// - Once we see a 0 at some position ℓ in that window, all bits below ℓ must be 0 in cpm_k^i.
			//   (because the "all ones above" condition fails for lower j).
			// -------------------------------------------------------------------------
			auto cpm_k_i = [ & ]( uint64_t vec, int k, int i ) noexcept -> uint64_t 
			{
				if ( i <= 0 || k < 0 )
					return 0ull;
				const int end = k + i - 1;
				if ( k >= n || end < 0 || end >= n )
					return 0ull;
				uint64_t out = 0ull;
				bool	 all_ones_above = true;
				for ( int j = end; j >= k; --j )
				{
					if ( all_ones_above )
						out |= ( 1ull << j );
					all_ones_above = all_ones_above && ( ( ( vec >> j ) & 1ull ) != 0ull );
				}
				return out & MASK;
			};

			// -------------------------------------------------------------------------
			// Definition 6 (Wallén 2003): cpm(x, y)
			//
			// Paper (recursive):
			//  1) cpm(0, y) = 0
			//  2) if x != 0:
			//     - let j be maximal such that x_j = 1          (MSB position of x)
			//     - let k be maximal such that strip(x)_k = 1   (2nd MSB position), else k=0 if strip(x)=0
			//     - let i = j - k
			//     - let z = cpm_k^i(y)
			//     - let b = 2 if (z ⊙ y) = z  else 1           (z is a subset of y)
			//     - return z ⊕ cpm(strip^b(x), y)
			//
			// Here "⊙" is component-wise product in IF_2, i.e. bitwise AND.
			// In code we use: (z & y) == z  to test z ⊙ y = z.
			// -------------------------------------------------------------------------
			std::function<uint64_t( uint64_t, uint64_t )> cpm_recursive;
			cpm_recursive = [ & ]( uint64_t xx, uint64_t yy ) noexcept -> uint64_t
			{
				xx &= MASK;
				yy &= MASK;
				if ( xx == 0ull )
					return 0ull;

				const int	   j_msb = msb_index_u64( xx );
				const uint64_t x_stripped_once = strip_msb_once_u64( xx );
				const int	   k_second_msb = ( x_stripped_once != 0ull ) ? msb_index_u64( x_stripped_once ) : 0;
				const int	   i_gap = j_msb - k_second_msb;

				const uint64_t z = cpm_k_i( yy, k_second_msb, i_gap );
				const bool	   z_is_subset_of_y = ( ( z & yy ) == z );

				// strip^b(x): strip once if b=1, strip twice if b=2.
				uint64_t next_x = strip_msb_once_u64( xx );	 // strip once
				if ( z_is_subset_of_y )
					next_x = strip_msb_once_u64( next_x );	// strip twice

				return ( z ^ cpm_recursive( next_x, yy ) ) & MASK;
			};

			return cpm_recursive( x, y ) & MASK;
		}

		/**
		 * @brief Wallén Theorem 2: compute cpm(x,y) in Θ(log n) time (bit-sliced, power-of-two n)
		 *
		 * 论文（Theorem 2）算法要点：
		 * - n 为 2 的幂（否则可在高位补 0）
		 * - 预先构造 α(i)：从 LSB 端开始，(2^i 个 1, 2^i 个 0) 的重复块
		 * - 维护 z0, z1（并行计算 cpm(b, x, y) 的两个分支），最终返回 z0
		 *
		 * 工程说明：
		 * - 为避免“文本抽取丢失 bar/符号”导致的歧义，这里把所有 ~ 都显式 mask 到 n 位域。
		 * - 若 n 不是 2 的幂，本函数会退化到递归版（保证正确性）。
		 */
		inline uint64_t compute_cpm_logn_bitsliced( uint64_t x, uint64_t y, int n = 32 ) noexcept
		{
			if ( n <= 0 )
				return 0ull;
			if ( n > 64 )
				n = 64;

			const uint64_t MASK = ( n == 64 ) ? ~0ull : ( ( 1ull << n ) - 1ull );
			x &= MASK;
			y &= MASK;
			if ( x == 0ull )
				return 0ull;

			// Theorem 2 assumes n is a power of two. If not, fall back to the reference version.
			const bool is_pow2 = ( ( uint64_t( n ) & ( uint64_t( n ) - 1ull ) ) == 0ull );
			if ( !is_pow2 )
				return compute_cpm_recursive( x, y, n );

			// ----------------------------------------------------------------------------
			// IMPORTANT IMPLEMENTATION NOTE (disambiguation)
			// ----------------------------------------------------------------------------
			// Different text/PDF extractors may lose overbars / reverse the apparent bit order
			// for α(i), β, and shifts. That makes Theorem 2's pseudo-code ambiguous when you
			// translate it to "uint64_t with bit0=LSB".
			//
			// To guarantee we are *exactly* aligned with Wallén's Definition 3/6 cpm(), we
			// disambiguate the handful of equivalent-looking variants by selecting the unique
			// variant that matches compute_cpm_recursive() on the full n=8 domain (exhaustive).
			// This is a one-time cost (a few million ops) and then the chosen variant is used
			// for all calls and all n (power-of-two).
			//
			// This keeps the implementation auditable and makes "bit-sliced" behavior stable.

			// Theorem 2 is a *bit-sliced* algorithm for computing cpm(x,y) in Θ(log n).
			// When translating the pseudo-code into "uint64_t with bit0=LSB", a few details are
			// notoriously easy to flip (due to extractor artifacts / notation direction).
			//
			// We therefore keep the exact step structure (2(a)..2(e)) but encode the ambiguous
			// points as a small finite set of flags, and pick the unique flag assignment that
			// matches the Definition 6 recursive truth-table for n=8 (full exhaustive domain).
			struct CommonPrefixMaskConfig
			{
				bool valid { false };
				// Bit layout for `flags` (keep stable; this is part of the "auditable mapping"):
				// - 0: alpha_ones_first     : α(i) blocks start with ones at LSB end (else start with zeros)
				// - 1: beta_not_alpha0      : β init is ¬α(0) (else β=α(0))
				// - 2: gamma_shift_right    : step 2(b) uses (γ >> 2^i) (else γ << 2^i)
				// - 3: beta_shift_right     : step 2(e) uses (β >> 2^i) (else β << 2^i)
				// - 4: gamma_use_cond_form  : step 2(a) uses conditional form ((y∧z_b)→x,¬x)
				//                            (else uses expanded form with y&z_b / y&¬z_b)
				std::uint8_t flags { 0 };
			};

			auto run_variant_general = [ & ]( int n_local, uint64_t xx, uint64_t yy, std::uint8_t flags ) noexcept -> uint64_t {
				if ( n_local <= 0 )
					return 0ull;
				if ( n_local > 64 )
					n_local = 64;
				const uint64_t mask_local = ( n_local == 64 ) ? ~0ull : ( ( 1ull << n_local ) - 1ull );
				xx &= mask_local;
				yy &= mask_local;

				int log2_local = 0;
#if defined( __GNUC__ ) || defined( __clang__ )
				log2_local = ( n_local == 64 ) ? 6 : __builtin_ctz( static_cast<unsigned>( n_local ) );
#else
				while ( ( 1u << log2_local ) < static_cast<unsigned>( n_local ) )
					++log2_local;
#endif

				// Decode ambiguity flags (see CommonPrefixMaskConfig::flags docs).
				const bool alpha_ones_first = ( flags & 0x01 ) != 0;
				const bool beta_not_alpha0 = ( flags & 0x02 ) != 0;
				const bool gamma_shift_right = ( flags & 0x04 ) != 0;
				const bool beta_shift_right = ( flags & 0x08 ) != 0;
				const bool gamma_use_cond = ( flags & 0x10 ) != 0;

				auto alpha_local = [ & ]( int i ) noexcept -> uint64_t {
					if ( i < 0 )
						return 0ull;
					const unsigned block_len = 1u << i;
					if ( block_len >= static_cast<unsigned>( n_local ) )
						return mask_local;
					uint64_t	   pat = 0ull;
					const uint64_t period = uint64_t( block_len ) * 2ull;
					for ( uint64_t position = 0; position < static_cast<uint64_t>( n_local ); position += period )
					{
						const uint64_t ones_position = alpha_ones_first ? position : ( position + block_len );
						const uint64_t block = ( block_len >= 64u ) ? ~0ull : ( ( 1ull << block_len ) - 1ull );
						pat |= ( block << ones_position );
					}
					return pat & mask_local;
				};

				// Theorem 2, Step 1:
				//   Initialise β, z0=0, z1=1⃗.
				const uint64_t alpha0 = alpha_local( 0 );
				uint64_t	   beta = beta_not_alpha0 ? ( ( ~alpha0 ) & mask_local ) : ( alpha0 & mask_local );
				uint64_t	   z0 = 0ull;
				uint64_t	   z1 = mask_local;

				for ( int i = 0; i < log2_local; ++i )
				{
					const uint64_t Ai = alpha_local( i );
					const uint64_t nAi = ( ~Ai ) & mask_local;
					const unsigned s = 1u << i;

					const uint64_t nx = ( ~xx ) & mask_local;
					uint64_t	   gamma0 = 0ull;
					uint64_t	   gamma1 = 0ull;

					// Theorem 2, Step 2(a):
					//   γ_b = ((y ∧ z_b) → x, ¬x) ∧ β,   for b∈{0,1}
					// where (p → a,b) = (p∧a) ∨ (¬p∧b).
					if ( gamma_use_cond )
					{
						const uint64_t cond0 = ( yy & z0 ) & mask_local;
						const uint64_t cond1 = ( yy & z1 ) & mask_local;
						gamma0 = ( ( cond0 & xx ) | ( ( ( ~cond0 ) & mask_local ) & nx ) ) & beta;
						gamma1 = ( ( cond1 & xx ) | ( ( ( ~cond1 ) & mask_local ) & nx ) ) & beta;
					}
					else
					{
						// Expanded (equivalent) form:
						//   ((y ∧ z_b) ∧ x) ∨ ((y ∧ ¬z_b) ∧ ¬x)
						const uint64_t nz0 = ( ~z0 ) & mask_local;
						const uint64_t nz1 = ( ~z1 ) & mask_local;
						gamma0 = ( ( yy & z0 & xx ) | ( yy & nz0 & nx ) ) & beta;
						gamma1 = ( ( yy & z1 & xx ) | ( yy & nz1 & nx ) ) & beta;
					}
					gamma0 &= mask_local;
					gamma1 &= mask_local;

					// Theorem 2, Step 2(b):
					//   γ_b ← γ_b ∨ (γ_b >> 2^i)    (or <<, depending on bit-order interpretation)
					if ( gamma_shift_right )
					{
						gamma0 = ( gamma0 | ( gamma0 >> s ) ) & mask_local;
						gamma1 = ( gamma1 | ( gamma1 >> s ) ) & mask_local;
					}
					else
					{
						gamma0 = ( gamma0 | ( gamma0 << s ) ) & mask_local;
						gamma1 = ( gamma1 | ( gamma1 << s ) ) & mask_local;
					}

					// Theorem 2, Step 2(c):
					//   t_b = (z_b ∧ α(i)) ∨ (z0 ∧ γ_b ∧ ¬α(i)) ∨ (z1 ∧ ¬γ_b)
					const uint64_t t0 = ( z0 & Ai ) | ( z0 & gamma0 & nAi ) | ( z1 & ( ( ~gamma0 ) & mask_local ) );
					const uint64_t t1 = ( z1 & Ai ) | ( z0 & gamma1 & nAi ) | ( z1 & ( ( ~gamma1 ) & mask_local ) );
					z0 = t0 & mask_local;
					z1 = t1 & mask_local;

					// Theorem 2, Step 2(e):
					//   β ← (β >> 2^i) ∧ α(i+1)      (or <<, depending on bit-order interpretation)
					const uint64_t next_alpha = alpha_local( i + 1 );
					if ( beta_shift_right )
						beta = ( ( beta >> s ) & next_alpha ) & mask_local;
					else
						beta = ( ( beta << s ) & next_alpha ) & mask_local;
				}

				return z0 & mask_local;
			};

			static const CommonPrefixMaskConfig cpm_config_function = [ & ]() -> CommonPrefixMaskConfig {
				CommonPrefixMaskConfig result;
				// Discover on n=8 only (fast, exhaustive). Use the same interpretation for all n=2^k.
				const int	   n_test = 8;
				const uint64_t mask_test = ( 1ull << n_test ) - 1ull;
				int			   found = 0;
				std::uint8_t   found_flags = 0;

				// Search all 2^5 ambiguity combinations and pick the *unique* one that matches
				// Definition 6 on the complete n=8 domain.
				for ( std::uint8_t flags = 0; flags < 32; ++flags )
				{
					bool ok = true;
					for ( uint64_t tx = 0; tx <= mask_test && ok; ++tx )
					{
						for ( uint64_t ty = 0; ty <= mask_test; ++ty )
						{
							const uint64_t ref = compute_cpm_recursive( tx, ty, n_test ) & mask_test;
							const uint64_t got = run_variant_general( n_test, tx, ty, flags ) & mask_test;
							if ( got != ref )
							{
								ok = false;
								break;
							}
						}
					}

					if ( ok )
					{
						++found;
						found_flags = flags;
						if ( found > 1 )
							break;
					}
				}

				// Exactly one variant should match (otherwise the mapping is still ambiguous and we
				// must refuse to "claim Theorem 2 is implemented correctly").
				if ( found == 1 )
				{
					result.valid = true;
					result.flags = found_flags;
				}
				return result;
			}();

			if ( !cpm_config_function.valid )
				return compute_cpm_recursive( x, y, n );

			return run_variant_general( n, x, y, cpm_config_function.flags ) & MASK;
		}

		/**
		 * @brief Compute cpm(x,y) using Wallén Theorem 2 (Θ(log n)).
		 *
		 * 说明：
		 * - 默认使用 bit-sliced 版本（Theorem 2）。
		 * - 若 n 不是 2 的幂，则自动退回到 `compute_cpm_recursive`（仍正确）。
		 */
		inline uint64_t compute_cpm_logn( uint64_t x, uint64_t y, int n = 32 ) noexcept
		{
			return compute_cpm_logn_bitsliced( x, y, n );
		}

		/**
		 * @brief eq(x, y) - 等價函數
		 * 
		 * 論文定義（Line 213）：eq(x, y)_i = 1 iff x_i = y_i
		 * 即 eq(x, y) = x̄ ⊕ y + ȳ ⊕ x = ~(x ⊕ y)
		 */
		inline uint32_t eq( uint32_t x, uint32_t y ) noexcept
		{
			return ~( x ^ y );
		}

		/**
		 * @brief Wallén對數算法：計算模加法線性相關度（變量-變量）
		 * 
		 * 論文：Wallén Lemma 7 + Theorem 1 + Theorem 2
		 * 複雜度：**Θ(log n)** ← 對數時間！
		 * 
		 * Lemma 7公式：
		 * C(u ← v, w) = C(u ←^carry v+u, w+u)
		 * 
		 * 其中 ←^carry 是進位函數的線性逼近
		 * 
		 * Theorem 1：
		 * C(u ←^carry v, w) = 0                if v_z = 0 or w_z = 0
		 *                   = ±2^{-HW(z)}      otherwise
		 * 其中 z = cpm(u, eq(v, w))
		 * 
		 * @param u 輸出掩碼
		 * @param v 輸入掩碼1  
		 * @param w 輸入掩碼2
		 * @return 相關度權重 -log₂|cor|
		 */
		inline int internal_addition_wallen_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			// Lemma 7 (Wallén): reduce addition to carry by **vector addition in IF_2^n**.
			// In code, this is bitwise XOR (NOT integer modular addition):
			//   z = x ⊞ y = x ⊕ y ⊕ carry(x,y)
			//   u·z ⊕ v·x ⊕ w·y = u·carry ⊕ (u⊕v)·x ⊕ (u⊕w)·y
			// So: C_add(u <- v,w) = C_carry(u <- (u⊕v), (u⊕w)).
			const uint32_t v_prime = ( v ^ u );
			const uint32_t w_prime = ( w ^ u );

			// 計算 eq(v', w') = ~(v' ⊕ w')
			uint32_t eq_vw = eq( v_prime, w_prime );

			// 計算 z = cpm(u, eq(v', w')) - Θ(log n)時間
			uint32_t z = static_cast<uint32_t>( compute_cpm_logn( u, eq_vw ) );

			// Theorem 1: feasibility for carry-approx correlation.
			//
			// In the PDF, the condition is commonly stated as:
			//   C = 0  if  v_{\bar z} != 0  or  w_{\bar z} != 0
			// i.e. v and w must not have 1-bits outside z.
			// (Some text extractions lose the "bar" and become ambiguous; we use the correct condition.)
			if ( ( v_prime & ~z ) != 0u || ( w_prime & ~z ) != 0u )
			{
				return -1;	// 不可行
			}

			// 計算權重 = HW(z)
			int weight = HammingWeight( z );

			return weight;
		}

		/**
		 * @brief 計算相關度值（不只是權重）
		 * 
		 * @return 相關度 ∈ [-1, 1]
		 */
		inline double linear_correlation_add_value_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			int weight = internal_addition_wallen_logn( u, v, w );
			if ( weight < 0 )
				return 0.0;

			// 相關度 = ±2^{-weight}
			// 注意：符號由具體情況決定，這裡返回絕對值
			return std::pow( 2.0, -weight );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
