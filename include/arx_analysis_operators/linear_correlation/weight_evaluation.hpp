#pragma once
/**
 * @file linear_correlation/weight_evaluation.hpp
 * @brief Modular-addition linear correlation — var-var side only
 *
 * 这份档案只负责：
 * - Wallén-style CPM / Θ(log n) route for fixed (u,v,w)
 * - var-var addition:      z = x ⊞ y
 * - var-var subtraction:   z = x ⊟ y
 * - exact sequential matrix-chain reference
 * - default public var-var path = log-depth tree reduction
 *
 * 这份档案**不再承担** var-const 公开算子。
 * 常量-变量情况请看 `linear_correlation_addconst_refactored.hpp`。
 *
 * 注意：
 * - `linear_correlation_add_value_logn()` 是 Wallén 那条「固定三个掩码，直接算一个值」的 Θ(log n) 路线。
 * - `correlation_add_varvar_logdepth()` / `correlation_sub_varvar_logdepth()` 是你原本 2×2 矩阵链的
 *   Θ(log n) 深度版本。
 * - 两条路线都属於 var-var，但责任不同，现在放在同一个 var-var 专属档里。
 */

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

#include "arx_analysis_operators/DefineSearchWeight.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

#ifndef TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED
#define TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED

		/**
		 * @brief Get bit_i(value), with bit 0 = LSB.
		 *
		 * 这是所有逐 bit 线性/差分算子的最底层约定：
		 * - bit 0 是最低位
		 * - carry 也是从 bit 0 往高位推
		 *
		 * 後面所有 per-bit 矩阵、CPM、AOP、gamma* 等，都默认这个 bit 编号。
		 */
		static inline int bit_u64( std::uint64_t value, int bit_index ) noexcept
		{
			return static_cast<int>( ( value >> bit_index ) & 1ULL );
		}

		static inline int carry_out_bit( int x, int y, int carry_in ) noexcept
		{
			return ( x & y ) | ( x & carry_in ) | ( y & carry_in );
		}

		static inline double signed_bit( int mask_bit, int value_bit ) noexcept
		{
			return mask_bit ? ( value_bit ? -1.0 : 1.0 ) : 1.0;
		}

		static inline double weight_from_linear_correlation( double correlation ) noexcept
		{
			const double absolute_correlation = std::fabs( correlation );
			if ( absolute_correlation <= 0.0 )
			{
				return std::numeric_limits<double>::infinity();
			}
			return -std::log2( absolute_correlation );
		}

		/**
		 * @brief 2×2 carry-state transfer matrix.
		 *
		 * 行向量左乘约定：
		 *
		 *     [v0 v1] · M
		 *
		 * 其中：
		 * - row    = carry-in  ∈ {0,1}
		 * - column = carry-out ∈ {0,1}
		 *
		 * 所以：
		 *
		 *     M = [ m00  m01 ]
		 *         [ m10  m11 ]
		 *
		 * 表示：
		 * - m00 : cin=0 -> cout=0 的局部平均符号和
		 * - m01 : cin=0 -> cout=1 的局部平均符号和
		 * - m10 : cin=1 -> cout=0 的局部平均符号和
		 * - m11 : cin=1 -> cout=1 的局部平均符号和
		 *
		 * 若逐 bit 累乘：
		 *
		 *     v_{i+1} = v_i · M_i
		 *
		 * 最终对未约束的终止 carry 求和：
		 *
		 *     corr = v_n[0] + v_n[1]
		 */
		struct Matrix2D
		{
			double m00;
			double m01;
			double m10;
			double m11;

			constexpr Matrix2D() noexcept
				: m00( 0.0 ), m01( 0.0 ), m10( 0.0 ), m11( 0.0 )
			{
			}

			constexpr Matrix2D( double a, double b, double c, double d ) noexcept
				: m00( a ), m01( b ), m10( c ), m11( d )
			{
			}

			static inline void multiply_row( double row0, double row1, const Matrix2D& matrix, double& out0, double& out1 ) noexcept
			{
				out0 = row0 * matrix.m00 + row1 * matrix.m10;
				out1 = row0 * matrix.m01 + row1 * matrix.m11;
			}

			[[nodiscard]] static inline Matrix2D multiply_mm( const Matrix2D& left, const Matrix2D& right ) noexcept
			{
				return Matrix2D {
					left.m00 * right.m00 + left.m01 * right.m10,
					left.m00 * right.m01 + left.m01 * right.m11,
					left.m10 * right.m00 + left.m11 * right.m10,
					left.m10 * right.m01 + left.m11 * right.m11 };
			}
		};

#endif  // TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED

#ifndef TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED
#define TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED

		struct LinearCorrelation
		{
			double correlation { 0.0 };
			double weight { std::numeric_limits<double>::infinity() };

			constexpr LinearCorrelation() noexcept = default;
			constexpr LinearCorrelation( double correlation_value, double weight_value ) noexcept
				: correlation( correlation_value ), weight( weight_value )
			{
			}

			[[nodiscard]] inline bool is_feasible() const noexcept
			{
				return ( correlation != 0.0 ) && !std::isinf( weight );
			}
		};

		static inline LinearCorrelation make_linear_correlation( double correlation ) noexcept
		{
			return LinearCorrelation { correlation, weight_from_linear_correlation( correlation ) };
		}

#endif  // TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED

		inline constexpr std::uint32_t HammingWeight( std::uint32_t x ) noexcept
		{
			return static_cast<std::uint32_t>( std::popcount( x ) );
		}

		// =========================================================================
		// Wallén / CPM route — fixed (u, v, w), Θ(log n)
		// =========================================================================

		/**
		 * @brief Compute z* = M_n^T v used in Wallén-style feasibility / weight checks.
		 *
		 * 这里的 `MnT_of(v)` 可以理解成：
		 * - 从 MSB 往 LSB 扫
		 * - 维护 suffix parity
		 * - 当 suffix parity = 1 时，对应 bit 进入 z*
		 *
		 * 直观上，z* 是「进位可影响区域」的闭包型掩码。
		 * 在 `wallen_weight()` 里，它配合：
		 *
		 *     a = mu ^ omega
		 *     b = nu ^ omega
		 *
		 * 去检查 `a`、`b` 是否完全落在 z* 支撑内；
		 * 若不在，该线性逼近不可行。
		 */
		inline std::uint32_t MnT_of( std::uint32_t v ) noexcept
		{
			std::uint32_t z = 0;
			std::uint32_t suffix = 0;

			for ( int bit_index = 31; bit_index >= 0; --bit_index )
			{
				if ( suffix & 1u )
				{
					z |= ( 1u << bit_index );
				}
				suffix ^= ( v >> bit_index ) & 1u;
			}

			return z;
		}

		/**
		 * @brief Wallén-style feasibility + weight helper.
		 *
		 * 令：
		 *     v      = mu ^ nu ^ omega
		 *     z_star = M_n^T v
		 *     a      = mu ^ omega
		 *     b      = nu ^ omega
		 *
		 * 若：
		 *     a ⊄ z_star   或   b ⊄ z_star
		 * 则该近似不可行，返回 nullopt。
		 *
		 * 否则其权重就是：
		 *     wt(z_star)
		 *
		 * 注意：这个 helper 是 Wallén 论文那套 support / feasibility 的辅助视角，
		 * 跟下面的 `compute_cpm_*()` 属於同一世界观，但不是矩阵链那条实作路线。
		 */
		inline std::optional<SearchWeight> wallen_weight( std::uint32_t mu, std::uint32_t nu, std::uint32_t omega, int n )
		{
			( void )n;

			const std::uint32_t v = mu ^ nu ^ omega;
			const std::uint32_t z_star = MnT_of( v );
			const std::uint32_t a = mu ^ omega;
			const std::uint32_t b = nu ^ omega;

			if ( ( a & ~z_star ) != 0u || ( b & ~z_star ) != 0u )
			{
				return std::nullopt;
			}

			return static_cast<int>( std::popcount( z_star ) );
		}

		/**
		 * @brief Reference-correct recursive CPM implementation.
		 *
		 * 对照 Wallén Definition 3 / Definition 6：
		 *
		 * - `cpm_k^i(y)`：
		 *      在窗口 [k, k+i-1] 内，从上往下看「上方是否全为 1」
		 * - `cpm(x, y)`：
		 *      取 x 的最高 1-bit、次高 1-bit 之间的 gap，
		 *      构造一段 z = cpm_k^i(y)，再递归剥去 x 的最高 bit
		 *
		 * 论文型写法可以概括成：
		 *
		 *     cpm(0, y) = 0
		 *
		 *     若 x != 0：
		 *         j = msb(x)
		 *         k = msb(strip(x))，若 strip(x)=0 则按定义落回 0
		 *         i = j - k
		 *         z = cpm_k^i(y)
		 *
		 *         若 z ⊆ y，则下一步 strip^2(x)
		 *         否则         下一步 strip^1(x)
		 *
		 *         cpm(x, y) = z ⊕ cpm(next_x, y)
		 *
		 * 这个版本虽然不是 Θ(log n)，但它是最可靠的定义版 / 对拍版。
		 * 後面的 bit-sliced `compute_cpm_logn_bitsliced()` 若有任何歧义，
		 * 都应该回到这个 reference 版本核对。
		 */
		inline std::uint64_t compute_cpm_recursive( std::uint64_t x, std::uint64_t y, int n = 32 ) noexcept
		{
			if ( n <= 0 )
			{
				return 0ULL;
			}
			if ( n > 64 )
			{
				n = 64;
			}

			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			x &= mask;
			y &= mask;

			auto msb_index_u64 = []( std::uint64_t value ) noexcept -> int
			{
				return value ? static_cast<int>( std::bit_width( value ) - 1 ) : -1;
			};

			auto strip_msb_once_u64 = [&]( std::uint64_t value ) noexcept -> std::uint64_t
			{
				const int msb_index = msb_index_u64( value );
				if ( msb_index < 0 )
				{
					return 0ULL;
				}
				return value & ~( 1ULL << msb_index );
			};

			auto cpm_k_i = [&]( std::uint64_t vec, int k, int width ) noexcept -> std::uint64_t
			{
				if ( width <= 0 || k < 0 )
				{
					return 0ULL;
				}

				const int end = k + width - 1;
				if ( k >= n || end < 0 || end >= n )
				{
					return 0ULL;
				}

				std::uint64_t out = 0ULL;
				bool all_ones_above = true;

				for ( int j = end; j >= k; --j )
				{
					if ( all_ones_above )
					{
						out |= ( 1ULL << j );
					}
					all_ones_above = all_ones_above && ( ( ( vec >> j ) & 1ULL ) != 0ULL );
				}

				return out & mask;
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
			std::function<std::uint64_t( std::uint64_t, std::uint64_t )> recurse;
			recurse = [&]( std::uint64_t xx, std::uint64_t yy ) noexcept -> std::uint64_t
			{
				xx &= mask;
				yy &= mask;

				if ( xx == 0ULL )
				{
					return 0ULL;
				}

				const int msb_index = msb_index_u64( xx );
				const std::uint64_t stripped_once = strip_msb_once_u64( xx );
				const int second_msb_index = ( stripped_once != 0ULL ) ? msb_index_u64( stripped_once ) : 0;
				const int gap = msb_index - second_msb_index;

				const std::uint64_t z = cpm_k_i( yy, second_msb_index, gap );
				const bool z_is_subset_of_y = ( ( z & yy ) == z );

				// strip^b(x): strip once if b=1, strip twice if b=2.
				std::uint64_t next_x = strip_msb_once_u64( xx );
				if ( z_is_subset_of_y )
				{
					next_x = strip_msb_once_u64( next_x );
				}

				return ( z ^ recurse( next_x, yy ) ) & mask;
			};

			return recurse( x, y ) & mask;
		}

		/**
		 * @brief Wallén Theorem 2: compute cpm(x,y) in Θ(log n) time.
		 *
		 * 这是 bit-sliced / power-of-two 长度版本。
		 *
		 * Theorem 2 的精神是：
		 * - 用 α(i) 这类「2^i 个 1、2^i 个 0」的重复块做位级分区
		 * - 维护两个并行分支 z0, z1
		 * - 每一轮把宽度翻倍，直到覆盖整个字
		 *
		 * 在工程实作里最容易出错的不是公式本身，而是：
		 * - α(i) 的 1-block 到底从哪边开始
		 * - β / γ 的 shift 方向
		 * - 条件式写法和展开式写法是否对应同一 bit 编号约定
		 *
		 * 因此本函式保留了一个很偏执、但很适合工程保命的做法：
		 * - 先枚举少量 ambiguity flags
		 * - 在 n=8 全域 exhaustive 对拍 `compute_cpm_recursive()`
		 * - 只接受唯一匹配 Definition 6 的实作分支
		 *
		 * 这样做的好处是：
		 * - 不靠 PDF 抽取品质赌命
		 * - 不会因为 overbar / bit-order 误读，导致表面看起来像 Theorem 2，
		 *   实际却和 Definition 6 不一致
		 */
		inline std::uint64_t compute_cpm_logn_bitsliced( std::uint64_t x, std::uint64_t y, int n = 32 ) noexcept
		{
			if ( n <= 0 )
			{
				return 0ULL;
			}
			if ( n > 64 )
			{
				n = 64;
			}

			// Theorem 2 assumes n is a power of two. If not, fall back to the reference version.
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			x &= mask;
			y &= mask;

			if ( x == 0ULL )
			{
				return 0ULL;
			}

			const bool is_power_of_two = ( ( std::uint64_t( n ) & ( std::uint64_t( n ) - 1ULL ) ) == 0ULL );
			if ( !is_power_of_two )
			{
				return compute_cpm_recursive( x, y, n );
			}

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
				// 0: alpha_ones_first     : α(i) blocks start with ones at LSB end (else start with zeros)
				// 1: beta_not_alpha0      : β init is ¬α(0) (else β=α(0))
				// 2: gamma_shift_right    : step 2(b) uses (γ >> 2^i) (else γ << 2^i)
				// 3: beta_shift_right     : step 2(e) uses (β >> 2^i) (else β << 2^i)
				// 4: gamma_use_cond_form  : step 2(a) uses conditional form ((y∧z_b)→x,¬x)
				//                           (else uses expanded form with y&z_b / y&¬z_b)
				std::uint8_t flags { 0 };
			};

			auto run_variant_general = [&]( int n_local, std::uint64_t xx, std::uint64_t yy, std::uint8_t flags ) noexcept -> std::uint64_t
			{
				if ( n_local <= 0 )
				{
					return 0ULL;
				}
				if ( n_local > 64 )
				{
					n_local = 64;
				}

				const std::uint64_t mask_local = ( n_local == 64 ) ? ~0ULL : ( ( 1ULL << n_local ) - 1ULL );
				xx &= mask_local;
				yy &= mask_local;

				int log2_local = 0;
#if defined( __GNUC__ ) || defined( __clang__ )
				log2_local = ( n_local == 64 ) ? 6 : __builtin_ctz( static_cast<unsigned>( n_local ) );
#else
				while ( ( 1u << log2_local ) < static_cast<unsigned>( n_local ) )
				{
					++log2_local;
				}
#endif

				const bool alpha_ones_first = ( flags & 0x01 ) != 0;
				const bool beta_not_alpha0 = ( flags & 0x02 ) != 0;
				const bool gamma_shift_right = ( flags & 0x04 ) != 0;
				const bool beta_shift_right = ( flags & 0x08 ) != 0;
				const bool gamma_use_cond = ( flags & 0x10 ) != 0;

				auto alpha_local = [&]( int i ) noexcept -> std::uint64_t
				{
					if ( i < 0 )
					{
						return 0ULL;
					}

					const unsigned block_length = 1u << i;
					if ( block_length >= static_cast<unsigned>( n_local ) )
					{
						return mask_local;
					}

					std::uint64_t pattern = 0ULL;
					const std::uint64_t period = std::uint64_t( block_length ) * 2ULL;

					for ( std::uint64_t position = 0; position < static_cast<std::uint64_t>( n_local ); position += period )
					{
						const std::uint64_t ones_position = alpha_ones_first ? position : ( position + block_length );
						const std::uint64_t block = ( block_length >= 64u ) ? ~0ULL : ( ( 1ULL << block_length ) - 1ULL );
						pattern |= ( block << ones_position );
					}

					return pattern & mask_local;
				};

				// Theorem 2, Step 1:
				// Initialise β, z0=0, z1=1⃗.
				const std::uint64_t alpha0 = alpha_local( 0 );
				std::uint64_t beta = beta_not_alpha0 ? ( ( ~alpha0 ) & mask_local ) : ( alpha0 & mask_local );
				std::uint64_t z0 = 0ULL;
				std::uint64_t z1 = mask_local;

				for ( int i = 0; i < log2_local; ++i )
				{
					const std::uint64_t Ai = alpha_local( i );
					const std::uint64_t not_Ai = ( ~Ai ) & mask_local;
					const unsigned shift = 1u << i;

					const std::uint64_t not_x = ( ~xx ) & mask_local;
					std::uint64_t gamma0 = 0ULL;
					std::uint64_t gamma1 = 0ULL;


					// Theorem 2, Step 2(a):
					// γ_b = ((y ∧ z_b) → x, ¬x) ∧ β, for b∈{0,1}
					// where (p → a,b) = (p∧a) ∨ (¬p∧b).
					if ( gamma_use_cond )
					{
						const std::uint64_t cond0 = ( yy & z0 ) & mask_local;
						const std::uint64_t cond1 = ( yy & z1 ) & mask_local;

						gamma0 = ( ( cond0 & xx ) | ( ( ( ~cond0 ) & mask_local ) & not_x ) ) & beta;
						gamma1 = ( ( cond1 & xx ) | ( ( ( ~cond1 ) & mask_local ) & not_x ) ) & beta;
					}
					else
					{
						// Expanded (equivalent) form:
						// ((y ∧ z_b) ∧ x) ∨ ((y ∧ ¬z_b) ∧ ¬x)
						const std::uint64_t not_z0 = ( ~z0 ) & mask_local;
						const std::uint64_t not_z1 = ( ~z1 ) & mask_local;

						gamma0 = ( ( yy & z0 & xx ) | ( yy & not_z0 & not_x ) ) & beta;
						gamma1 = ( ( yy & z1 & xx ) | ( yy & not_z1 & not_x ) ) & beta;
					}

					gamma0 &= mask_local;
					gamma1 &= mask_local;


					// Theorem 2, Step 2(b):
					// γ_b ← γ_b ∨ (γ_b >> 2^i)  (or <<, depending on bit-order interpretation)
					if ( gamma_shift_right )
					{
						gamma0 = ( gamma0 | ( gamma0 >> shift ) ) & mask_local;
						gamma1 = ( gamma1 | ( gamma1 >> shift ) ) & mask_local;
					}
					else
					{
						gamma0 = ( gamma0 | ( gamma0 << shift ) ) & mask_local;
						gamma1 = ( gamma1 | ( gamma1 << shift ) ) & mask_local;
					}

					// Theorem 2, Step 2(c):
					//  t_b = (z_b ∧ α(i)) ∨ (z0 ∧ γ_b ∧ ¬α(i)) ∨ (z1 ∧ ¬γ_b)
					const std::uint64_t t0 =
						( z0 & Ai ) |
						( z0 & gamma0 & not_Ai ) |
						( z1 & ( ( ~gamma0 ) & mask_local ) );

					const std::uint64_t t1 =
						( z1 & Ai ) |
						( z0 & gamma1 & not_Ai ) |
						( z1 & ( ( ~gamma1 ) & mask_local ) );

					z0 = t0 & mask_local;
					z1 = t1 & mask_local;

					const std::uint64_t next_alpha = alpha_local( i + 1 );

					// Theorem 2, Step 2(e):
					// β ← (β >> 2^i) ∧ α(i+1)      (or <<, depending on bit-order interpretation)
					if ( beta_shift_right )
					{
						beta = ( ( beta >> shift ) & next_alpha ) & mask_local;
					}
					else
					{
						beta = ( ( beta << shift ) & next_alpha ) & mask_local;
					}
				}

				return z0 & mask_local;
			};

			static const CommonPrefixMaskConfig config = [&]() -> CommonPrefixMaskConfig
			{
				CommonPrefixMaskConfig out;

				// Discover on n=8 only (fast, exhaustive). Use the same interpretation for all n=2^k.
				const int n_test = 8;
				const std::uint64_t mask_test = ( 1ULL << n_test ) - 1ULL;

				int found = 0;
				std::uint8_t found_flags = 0;

				// Search all 2^5 ambiguity combinations and pick the *unique* one that matches
				// Definition 6 on the complete n=8 domain.

				for ( std::uint8_t flags = 0; flags < 32; ++flags )
				{
					bool ok = true;

					for ( std::uint64_t tx = 0; tx <= mask_test && ok; ++tx )
					{
						for ( std::uint64_t ty = 0; ty <= mask_test; ++ty )
						{
							const std::uint64_t ref = compute_cpm_recursive( tx, ty, n_test ) & mask_test;
							const std::uint64_t got = run_variant_general( n_test, tx, ty, flags ) & mask_test;

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
						{
							break;
						}
					}
				}
				
				// Exactly one variant should match (otherwise the mapping is still ambiguous and we
				// must refuse to "claim Theorem 2 is implemented correctly").
				if ( found == 1 )
				{
					out.valid = true;
					out.flags = found_flags;
				}

				return out;
			}();

			if ( !config.valid )
			{
				return compute_cpm_recursive( x, y, n );
			}

			return run_variant_general( n, x, y, config.flags ) & mask;
		}

		inline std::uint64_t compute_cpm_logn( std::uint64_t x, std::uint64_t y, int n = 32 ) noexcept
		{
			return compute_cpm_logn_bitsliced( x, y, n );
		}

		/**
		 * @brief eq(x,y) = ~(x ^ y)
		 *
		 * 含义：
		 *     eq(x,y)_i = 1   iff   x_i = y_i
		 *
		 * 在 Wallén 路线里它拿来构造：
		 *
		 *     z = cpm(u, eq(v', w'))
		 *
		 * 也就是先找出 v' 与 w' 哪些 bit 相等，再在那个条件上做 common-prefix 结构。
		 */
		inline std::uint32_t eq( std::uint32_t x, std::uint32_t y ) noexcept
		{
			return ~( x ^ y );
		}

		/**
		 * @brief Fixed-(u,v,w) Wallén route for modular addition.
		 *
		 * 先用 Lemma 7 把加法转成 carry：
		 *
		 *     C_add(u ← v,w) = C_carry(u ← v⊕u, w⊕u)
		 *
		 * 设：
		 *     v' = v ⊕ u
		 *     w' = w ⊕ u
		 *
		 * 再计算：
		 *     eq_vw = eq(v', w')
		 *     z     = cpm(u, eq_vw)
		 *
		 * Theorem 1 的 feasibility 条件可写成：
		 *
		 *     v'_{\bar z} = 0   且   w'_{\bar z} = 0
		 *
		 * 工程上就是：
		 *
		 *     (v' & ~z) == 0
		 *     (w' & ~z) == 0
		 *
		 * 若不可行，返回 `INFINITE_WEIGHT`。
		 * 若可行，相关度绝对值为：
		 *
		 *     |C| = 2^{-HW(z)}
		 *
		 * 所以这里直接返回：
		 *     weight = HW(z)
		 */
		inline SearchWeight internal_addition_wallen_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			// Lemma 7 (Wallén): reduce addition to carry by **vector addition in IF_2^n**.
			// In code, this is bitwise XOR (NOT integer modular addition):
			// z = x ⊞ y = x ⊕ y ⊕ carry(x,y)
			// u·z ⊕ v·x ⊕ w·y = u·carry ⊕ (u⊕v)·x ⊕ (u⊕w)·y
			// So: C_add(u <- v,w) = C_carry(u <- (u⊕v), (u⊕w))
			const std::uint32_t v_prime = ( v ^ u );
			const std::uint32_t w_prime = ( w ^ u );

			const std::uint32_t eq_vw = eq( v_prime, w_prime );
			const std::uint32_t z = static_cast<std::uint32_t>( compute_cpm_logn( u, eq_vw ) );

			
			// Theorem 1: feasibility for carry-approx correlation.
			//
			// In the PDF, the condition is commonly stated as:
			// C = 0  if  v_{\bar z} != 0  or  w_{\bar z} != 0
			// i.e. v and w must not have 1-bits outside z.
			// (Some text extractions lose the "bar" and become ambiguous; we use the correct condition.)
			if ( ( v_prime & ~z ) != 0u || ( w_prime & ~z ) != 0u )
			{
				return TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
			}

			return static_cast<SearchWeight>( HammingWeight( z ) );
		}

		/**
		 * @brief Return |C(u ← v,w)| for modular addition via the Wallén log-n route.
		 *
		 * 若 `internal_addition_wallen_logn()` 返回 `INFINITE_WEIGHT`，表示不可行，相关度为 0。
		 * 否则：
		 *
		 *     |corr| = 2^{-weight}
		 *
		 * 注意这里返回的是**绝对值**，不含符号。
		 * 若你需要符号，得另外从更细致的推导或局部传播里补。
		 */
		inline double linear_correlation_add_value_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			const SearchWeight weight = internal_addition_wallen_logn( u, v, w );
			if ( weight >= TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT )
			{
				return 0.0;
			}

			return std::pow( 2.0, -double( weight ) );
		}

		static inline LinearCorrelation linear_correlation_add_logn32( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			return make_linear_correlation( linear_correlation_add_value_logn( u, v, w ) );
		}

		// =========================================================================
		// var-var transfer-matrix route
		// =========================================================================

		/**
		 * @brief Build the exact local 2×2 matrix for z = x ⊞ y at one bit.
		 *
		 * M_i(cin, cout)
		 *   = 1/4 * Σ_{x_i,y_i} 1_{carry(x_i,y_i,cin)=cout}
		 *            (-1)^(α_i x_i ⊕ β_i y_i ⊕ γ_i z_i)
		 *
		 * Both x_i and y_i vary, so the averaging factor is 1/4.
		 */
		/**
		 * @brief Build the exact per-bit 2×2 transfer matrix for z = x ⊞ y.
		 *
		 * 目标相关度：
		 *
		 *     corr = E[ (-1)^{ α·x ⊕ β·y ⊕ γ·z } ]
		 *
		 * 对於单一 bit i，在固定 carry-in / carry-out 下，局部矩阵 entry 为：
		 *
		 *     M_i(cin, cout)
		 *       = (1/4) * Σ_{x_i,y_i ∈ {0,1}}
		 *           1_{ carry(x_i,y_i,cin)=cout }
		 *           · (-1)^{ α_i x_i ⊕ β_i y_i ⊕ γ_i z_i }
		 *
		 * 其中：
		 *     z_i = x_i ⊕ y_i ⊕ cin
		 *
		 * 为什麽是 1/4？
		 * 因为 var-var 每个 bit 有 4 种输入组合：
		 *     (x_i, y_i) ∈ {0,1}²
		 *
		 * 这就是你原本 exact per-bit operator 的核心，不应该被删，只应该放回 var-var 档。
		 */
		static inline Matrix2D make_Mi_add_varvar_bit( int alpha_i, int beta_i, int gamma_i ) noexcept
		{
			Matrix2D matrix;

			for ( int carry_in = 0; carry_in <= 1; ++carry_in )
			{
				for ( int carry_out = 0; carry_out <= 1; ++carry_out )
				{
					double sum = 0.0;

					for ( int x_i = 0; x_i <= 1; ++x_i )
					{
						for ( int y_i = 0; y_i <= 1; ++y_i )
						{
							if ( carry_out_bit( x_i, y_i, carry_in ) != carry_out )
							{
								continue;
							}

							const int z_i = x_i ^ y_i ^ carry_in;

							double sign = 1.0;
							sign *= signed_bit( alpha_i, x_i );
							sign *= signed_bit( beta_i, y_i );
							sign *= signed_bit( gamma_i, z_i );
							sum += sign;
						}
					}

					const double value = sum * 0.25;

					if ( carry_in == 0 && carry_out == 0 ) matrix.m00 = value;
					if ( carry_in == 0 && carry_out == 1 ) matrix.m01 = value;
					if ( carry_in == 1 && carry_out == 0 ) matrix.m10 = value;
					if ( carry_in == 1 && carry_out == 1 ) matrix.m11 = value;
				}
			}

			return matrix;
		}

		/**
		 * @brief Reference-correct sequential exact correlation for z = x ⊞ y.
		 *
		 * 留作 audit / 对拍基准。公开主路径则交给 log-depth 版本。
		 */
		/**
		 * @brief Exact sequential reference chain for z = x ⊞ y.
		 *
		 * 逐 bit 左乘：
		 *
		 *     v_0 = [1, 0]
		 *     v_{i+1} = v_i · M_i
		 *
		 * 最终：
		 *
		 *     corr = v_n[0] + v_n[1]
		 *
		 * 这个版本的价值不是「最快」，
		 * 而是它最适合做：
		 * - correctness baseline
		 * - 对拍 log-depth 版本
		 * - 小 n / debug 场景下的 reference
		 */
		static inline double correlation_add_varvar_reference( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return 1.0;
			}

			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );

			alpha &= mask;
			beta &= mask;
			gamma &= mask;

			double row0 = 1.0;
			double row1 = 0.0;

			for ( int bit_index = 0; bit_index < n; ++bit_index )
			{
				const Matrix2D matrix = make_Mi_add_varvar_bit(
					bit_u64( alpha, bit_index ),
					bit_u64( beta, bit_index ),
					bit_u64( gamma, bit_index ) );

				double next_row0 = 0.0;
				double next_row1 = 0.0;
				Matrix2D::multiply_row( row0, row1, matrix, next_row0, next_row1 );
				row0 = next_row0;
				row1 = next_row1;
			}

			return row0 + row1;
		}

		/**
		 * @brief Exact var-var correlation with Θ(log n) multiplication depth and O(n) work.
		 */
		/**
		 * @brief Log-depth tree reduction for z = x ⊞ y.
		 *
		 * 逻辑上仍然是：
		 *     M_total = M_0 M_1 ... M_{n-1}
		 *
		 * 只是乘法次序改成二叉树合并：
		 *
		 *     ((M_0 M_1) (M_2 M_3)) ...
		 *
		 * 这样：
		 * - work 仍是 O(n)
		 * - circuit depth 变成 Θ(log n)
		 *
		 * 注意它和顺序链在浮点乘法次序上不同，
		 * 所以若你用 double，比特级数学等价 ≠ 浮点最後一位必然完全一样。
		 * 工程上应该允许极小量 roundoff 差异。
		 */
		static inline double correlation_add_varvar_logdepth( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return 1.0;
			}

			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );

			alpha &= mask;
			beta &= mask;
			gamma &= mask;

			std::array<Matrix2D, 64> chain {};
			for ( int bit_index = 0; bit_index < n; ++bit_index )
			{
				chain[ static_cast<std::size_t>( bit_index ) ] = make_Mi_add_varvar_bit(
					bit_u64( alpha, bit_index ),
					bit_u64( beta, bit_index ),
					bit_u64( gamma, bit_index ) );
			}

			int active_length = n;
			while ( active_length > 1 )
			{
				const int pair_count = active_length / 2;

				for ( int pair_index = 0; pair_index < pair_count; ++pair_index )
				{
					chain[ static_cast<std::size_t>( pair_index ) ] = Matrix2D::multiply_mm(
						chain[ static_cast<std::size_t>( 2 * pair_index ) ],
						chain[ static_cast<std::size_t>( 2 * pair_index + 1 ) ] );
				}

				if ( ( active_length & 1 ) != 0 )
				{
					chain[ static_cast<std::size_t>( pair_count ) ] =
						chain[ static_cast<std::size_t>( active_length - 1 ) ];
				}

				active_length = pair_count + ( ( active_length & 1 ) != 0 ? 1 : 0 );
			}

			double row0 = 0.0;
			double row1 = 0.0;
			Matrix2D::multiply_row( 1.0, 0.0, chain[0], row0, row1 );
			return row0 + row1;
		}

		/**
		 * @brief Public default alias: var-var now defaults to the log-depth path.
		 */
		static inline double correlation_add_varvar( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			return correlation_add_varvar_logdepth( alpha, beta, gamma, n );
		}

		/**
		 * @brief Reference-correct sequential exact correlation for z = x ⊟ y.
		 *
		 * Historical parameter order is preserved:
		 *   (alpha for x, gamma for z, beta for y)
		 */
		/**
		 * @brief Exact sequential reference chain for z = x ⊟ y.
		 *
		 * 用 two's complement 改写：
		 *
		 *     x ⊟ y = x ⊞ (-y mod 2^n)
		 *
		 * 所以这里不是另起一套新算子，而是把：
		 *
		 *     neg_beta = (~beta + 1) mod 2^n
		 *
		 * 喂回 `correlation_add_varvar_reference()`。
		 */
		static inline double correlation_sub_varvar_reference( std::uint64_t alpha, std::uint64_t gamma, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_beta = ( ( ~beta ) + 1ULL ) & mask;
			return correlation_add_varvar_reference( alpha, neg_beta, gamma, n );
		}

		/**
		 * @brief Exact var-var subtraction with Θ(log n) multiplication depth.
		 *
		 * Historical parameter order is preserved:
		 *   (alpha for x, gamma for z, beta for y)
		 */
		/**
		 * @brief Log-depth tree reduction for z = x ⊟ y.
		 *
		 * 一样透过：
		 *
		 *     x ⊟ y = x ⊞ (-y mod 2^n)
		 *
		 * 去重用 var-var addition 的 log-depth 核心。
		 */
		static inline double correlation_sub_varvar_logdepth( std::uint64_t alpha, std::uint64_t gamma, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_beta = ( ( ~beta ) + 1ULL ) & mask;
			return correlation_add_varvar_logdepth( alpha, neg_beta, gamma, n );
		}

		/**
		 * @brief Public default alias: var-var subtraction now defaults to the log-depth path.
		 *
		 * Historical parameter order is preserved:
		 *   (alpha for x, gamma for z, beta for y)
		 */
		static inline double correlation_sub_varvar( std::uint64_t alpha, std::uint64_t gamma, std::uint64_t beta, int n ) noexcept
		{
			return correlation_sub_varvar_logdepth( alpha, gamma, beta, n );
		}

		// =========================================================================
		// convenience wrappers — var-var only
		// =========================================================================

		static inline LinearCorrelation linear_add_varvar32( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_add_varvar( alpha, beta, gamma, nbits ) );
		}

		static inline LinearCorrelation linear_add_varvar64( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_add_varvar( alpha, beta, gamma, nbits ) );
		}

		static inline LinearCorrelation linear_sub_varvar32( std::uint32_t alpha, std::uint32_t gamma, std::uint32_t beta, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_sub_varvar( alpha, gamma, beta, nbits ) );
		}

		static inline LinearCorrelation linear_sub_varvar64( std::uint64_t alpha, std::uint64_t gamma, std::uint64_t beta, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_sub_varvar( alpha, gamma, beta, nbits ) );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
