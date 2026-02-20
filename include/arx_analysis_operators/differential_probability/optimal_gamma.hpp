/**
 * @file differential_probability/optimal_gamma.hpp
 * @brief Given input differences (alpha, beta), construct the optimal output difference gamma
 *        (LM-2001 Algorithm 4), upgraded to generic word types (32 / 64 / 128 bit).
 *
 * Reference:
 * - H. Lipmaa, S. Moriai, "Efficient Algorithms for Computing Differential Properties of Addition",
 *   FSE 2001 (LNCS 2355), 2002.
 *
 * -----------------------------------------------------------------------------
 * 0) 本档要解的问题
 * -----------------------------------------------------------------------------
 *
 * 对 n-bit modular addition：
 *
 *     z  = x ⊞ y
 *     z' = (x ⊕ alpha) ⊞ (y ⊕ beta)
 *     gamma = z ⊕ z'
 *
 * XOR differential probability:
 *
 *     DP+(alpha, beta ↦ gamma)
 *       = Pr_{x,y}[ (x+y) ⊕ ((x⊕alpha)+(y⊕beta)) = gamma ]
 *
 * 现在我们不是要枚举所有 gamma，而是要直接构造：
 *
 *     gamma* = argmax_gamma DP+(alpha, beta ↦ gamma)
 *
 * LM-2001 Algorithm 4 告诉我们：
 * 这个 gamma* 可以用 Θ(log n) 的位运算构造出来。
 *
 * -----------------------------------------------------------------------------
 * 1) 本档的三层结构
 * -----------------------------------------------------------------------------
 *
 * A. 底层 generic bit helpers
 *    - low_mask
 *    - bitreverse_low_bits_generic
 *    - aop_generic
 *
 * B. 论文中的中间对象
 *    - carry_aop
 *    - aop
 *    - aopr
 *
 * C. 最终 argmax 构造
 *    - find_optimal_gamma<WordT>()
 *
 * -----------------------------------------------------------------------------
 * 2) 这次升位宽时特别注意的坑
 * -----------------------------------------------------------------------------
 *
 * 原版 32-bit 写法里，最容易把 64/128 位弄炸的点是：
 * - `uint32_t`
 * - `1u << n`
 * - `log2(32)=5` 写死
 * - `bitreverse32` 只对 32 位 SWAR 常量成立
 *
 * 所以这里不是「把 32 换 64」而已，而是把整套位级 helper 一起泛化。
 *
 * -----------------------------------------------------------------------------
 * 3) 关於 weight wrapper
 * -----------------------------------------------------------------------------
 *
 * `find_optimal_gamma_with_weight(...)` 目前只保留 32-bit 便利版本，
 * 因为这个 workspace 可见的 XDP backend 是：
 *
 *     xdp_add_lm2001(...)
 *     xdp_add_lm2001_n(...)
 *
 * 都是以 32-bit 介面暴露。
 * 若你工程里已有 64 / 128-bit XDP backend，按同样模式补 overload 即可。
 */

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "arx_analysis_operators/UnsignedInteger128Bit.hpp"
#include "arx_analysis_operators/differential_probability/weight_evaluation.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

		using uint128_t = UnsignedInteger128Bit;

		namespace detail
		{
			template<typename WordT>
			struct is_supported_word : std::false_type
			{
			};

			template<>
			struct is_supported_word<std::uint32_t> : std::true_type
			{
			};

			template<>
			struct is_supported_word<std::uint64_t> : std::true_type
			{
			};

			template<>
			struct is_supported_word<uint128_t> : std::true_type
			{
			};

			template<typename WordT>
			inline constexpr bool is_supported_word_v = is_supported_word<WordT>::value;

			template<typename WordT>
			struct word_traits;

			template<>
			struct word_traits<std::uint32_t>
			{
				static constexpr int digits = 32;
			};

			template<>
			struct word_traits<std::uint64_t>
			{
				static constexpr int digits = 64;
			};

			template<>
			struct word_traits<uint128_t>
			{
				static constexpr int digits = 128;
			};

			template<typename WordT>
			inline constexpr int word_bits_v = word_traits<WordT>::digits;

			/**
			 * @brief Return the low-n-bit mask in the given word type.
			 *
			 * 例子：
			 * - n = 5   -> 0b11111
			 * - n = 64  -> all-one 64-bit mask
			 * - n = 128 -> all-one 128-bit mask
			 *
			 * 这个 helper 存在的原因很现实：
			 * 原来 32 位写法常见 `((1u << n) - 1)`，
			 * 但到 64 / 128 位时，这种写法不但不通用，还容易 UB / 溢出。
			 */
			template<typename WordT>
			constexpr WordT low_mask( int n ) noexcept
			{
				static_assert( is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

				if ( n <= 0 )
					return WordT { 0 };

				constexpr int kDigits = word_bits_v<WordT>;
				if ( n >= kDigits )
					return ~WordT { 0 };

				return ( WordT { 1 } << n ) - WordT { 1 };
			}

			/**
			 * @brief ceil(log2(n)) for small positive integers.
			 *
			 * `aop_generic()` 需要做 power-of-two shift stage：
			 *
			 *     1, 2, 4, 8, ...
			 *
			 * 所以要知道总共需要几层。
			 */
			template<typename WordT>
			constexpr int ceil_log2_n( int n ) noexcept
			{
				if ( n <= 1 )
					return 0;

				int levels = 0;
				int power = 1;
				while ( power < n )
				{
					power <<= 1;
					++levels;
				}
				return levels;
			}

			/**
			 * @brief Reverse all bits of a full machine word in O(1) word-ops.
			 *
			 * 这里不用逐 bit for-loop，直接走固定字宽 fast path：
			 * - 32-bit : SWAR bit-reversal
			 * - 64-bit : SWAR bit-reversal
			 * - 128-bit: 拆成两个 64-bit，各自反转後交换高低半部
			 *
			 * 对於本档支援的 32 / 64 / 128 位型别，这都是固定条数的位运算，
			 * 也就是工程语境下的 O(1)。
			 */
			template<typename WordT>
			inline WordT bitreverse_full_word( WordT x ) noexcept
			{
				static_assert( is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

				if constexpr ( std::is_same_v<WordT, std::uint32_t> )
				{
					x = ( ( x & 0x55555555u ) << 1 ) | ( ( x >> 1 ) & 0x55555555u );
					x = ( ( x & 0x33333333u ) << 2 ) | ( ( x >> 2 ) & 0x33333333u );
					x = ( ( x & 0x0F0F0F0Fu ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0Fu );
					x = ( ( x & 0x00FF00FFu ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FFu );
					x = ( x << 16 ) | ( x >> 16 );
					return x;
				}
				else if constexpr ( std::is_same_v<WordT, std::uint64_t> )
				{
					x = ( ( x & 0x5555555555555555ull ) << 1 ) | ( ( x >> 1 ) & 0x5555555555555555ull );
					x = ( ( x & 0x3333333333333333ull ) << 2 ) | ( ( x >> 2 ) & 0x3333333333333333ull );
					x = ( ( x & 0x0F0F0F0F0F0F0F0Full ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0F0F0F0F0Full );
					x = ( ( x & 0x00FF00FF00FF00FFull ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FF00FF00FFull );
					x = ( ( x & 0x0000FFFF0000FFFFull ) << 16 ) | ( ( x >> 16 ) & 0x0000FFFF0000FFFFull );
					x = ( x << 32 ) | ( x >> 32 );
					return x;
				}
				else if constexpr ( std::is_same_v<WordT, uint128_t> )
				{
					const std::uint64_t low64 = static_cast<std::uint64_t>( x );
					const std::uint64_t high64 = static_cast<std::uint64_t>( x >> 64 );

					const uint128_t reversed_low_to_high = static_cast<uint128_t>( bitreverse_full_word<std::uint64_t>( low64 ) );
					const uint128_t reversed_high_to_low = static_cast<uint128_t>( bitreverse_full_word<std::uint64_t>( high64 ) );

					return ( reversed_low_to_high << 64 ) | reversed_high_to_low;
				}
				else
				{
					static_assert( !sizeof( WordT* ), "Unhandled word type in bitreverse_full_word." );
				}
			}

			/**
			 * @brief Reverse only the low n bits of x.
			 *
			 * 论文里 `aopr(x)` 的定义，本质上就是：
			 * - 先把低 n 位反转
			 * - 对反转後的串做 `aop`
			 * - 再反转回去
			 *
			 * 这里不再逐 bit 扫描，而是：
			 * 1. 先把整个 machine word 做 full reverse（固定字宽 O(1)）
			 * 2. 再把「原本低 n 位」对应到的新位置右移回低位
			 *
			 * 若 n == word_bits，这就等同 full-word reverse；
			 * 若 n < word_bits，则相当於“只反转低 n 位，其余清零”。
			 */
			template<typename WordT>
			inline WordT bitreverse_low_bits_generic( WordT x, int n ) noexcept
			{
				static_assert( is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

				const WordT mask = low_mask<WordT>( n );
				x &= mask;

				constexpr int kDigits = word_bits_v<WordT>;
				const WordT reversed_full = bitreverse_full_word<WordT>( x );

				if ( n >= kDigits )
					return reversed_full;

				return ( reversed_full >> ( kDigits - n ) ) & mask;
			}

			/**
			 * @brief Generic implementation of LM-2001 Algorithm 1: aop(x).
			 *
			 * 定义：
			 *
			 *     aop(x)_i = 1
			 *         iff 从 bit i 开始的那段连续 1-run 长度是奇数
			 *
			 * 它不是普通 popcount，也不是简单 prefix/suffix parity，
			 * 而是「每个起点位置往右看的 all-one run parity」。
			 *
			 * Algorithm 1 的结构可以理解成两段：
			 *
			 * 1) 先构造 x_arr[k]
			 *      用来表示长度 2^k 的连续 1 结构是否存在
			 *
			 * 2) 再构造 y_arr[k]
			 *      把“奇数长 run 的起点”一路扩展累积出来
			 *
			 * 所以它的 stage 数是 Θ(log n)，而不是 O(n) 扫一遍每个起点。
			 */
			template<typename WordT>
			inline WordT aop_generic( WordT x, int n ) noexcept
			{
				static_assert( is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

				const WordT mask = low_mask<WordT>( n );
				x &= mask;

				if ( n <= 0 )
					return WordT { 0 };
				if ( n == 1 )
					return x;

				// Enough for 128-bit words: ceil(log2(128)) = 7.
				std::array<WordT, 9> x_arr {};
				std::array<WordT, 9> y_arr {};

				const int levels = ceil_log2_n<WordT>( n );

				x_arr[ 0 ] = x;
				x_arr[ 1 ] = x & ( x >> 1 );

				for ( int i = 2; i < levels; ++i )
				{
					const int shift = 1 << ( i - 1 );
					x_arr[ i ] = x_arr[ i - 1 ] & ( x_arr[ i - 1 ] >> shift );
				}

				y_arr[ 0 ] = 0;
				y_arr[ 1 ] = x & ( ~x_arr[ 1 ] ) & mask;

				for ( int i = 2; i <= levels; ++i )
				{
					const int shift = 1 << ( i - 1 );
					y_arr[ i ] = ( y_arr[ i - 1 ] | ( ( y_arr[ i - 1 ] >> shift ) & x_arr[ i - 1 ] ) ) & mask;
				}

				return y_arr[ levels ] & mask;
			}
		}  // namespace detail

		/**
		 * @brief AOP(alpha, beta, gamma): mask of positions where carries can affect the output XOR differential.
		 *
		 * Common equivalent form:
		 *   AOP(alpha, beta, gamma)
		 *     = alpha ⊕ beta ⊕ gamma ⊕ ( ((alpha∧beta) ⊕ ((alpha⊕beta)∧gamma)) << 1 )
		 *
		 * The result is masked to the low n bits.
		 */
		/**
		 * @brief AOP(alpha, beta, gamma): positions where carry-state can matter.
		 *
		 * 常见等价写法：
		 *
		 *     AOP(alpha,beta,gamma)
		 *       = alpha ⊕ beta ⊕ gamma
		 *         ⊕ ( ((alpha∧beta) ⊕ ((alpha⊕beta)∧gamma)) << 1 )
		 *
		 * 直观理解：
		 * - 若某 bit 在这个 mask 上为 1，
		 *   代表该位置的 differential 行为和 carry 状态有关
		 * - 若为 0，代表这一位相对“平静”，不需要 carry 状态去区分
		 */
		template<typename WordT>
		inline WordT carry_aop( WordT alpha, WordT beta, WordT gamma, int n = detail::word_bits_v<WordT> ) noexcept
		{
			static_assert( detail::is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

			const WordT mask = detail::low_mask<WordT>( n );
			alpha &= mask;
			beta &= mask;
			gamma &= mask;

			const WordT xor_part = alpha ^ beta ^ gamma;
			const WordT alpha_and_beta = alpha & beta;
			const WordT alpha_xor_beta = alpha ^ beta;
			const WordT xor_and_gamma = alpha_xor_beta & gamma;
			const WordT carry_part = alpha_and_beta ^ xor_and_gamma;

			return ( xor_part ^ ( carry_part << 1 ) ) & mask;
		}

		/**
		 * @brief LM-2001 Algorithm 1: aop(x) / all-one parity, generic over 32/64/128-bit words.
		 *
		 * Definition:
		 *   aop(x)_i = 1 iff the run of consecutive 1s starting at bit i has odd length.
		 *
		 * Complexity:
		 *   Θ(log n) stages, using the same power-of-two shift structure as the original 32-bit code.
		 */
		/**
		 * @brief Public wrapper of Algorithm 1.
		 *
		 * 也就是把「odd-length 1-run 起点」的判断，包装成对外可用的 word-generic 版本。
		 */
		template<typename WordT>
		inline WordT aop( WordT x, int n = detail::word_bits_v<WordT> ) noexcept
		{
			return detail::aop_generic( x, n );
		}

		/**
		 * @brief Reverse only the low n bits of x.
		 *
		 * This path is fully generic and therefore works for 32 / 64 / 128-bit words.
		 * It favors portability and auditability over SWAR specialization.
		 */
		/**
		 * @brief Reverse only the low n bits.
		 *
		 * 这个函式单独暴露出来，是因为：
		 * - `aopr()` 要用
		 * - 其他你想 debug / 可视化 low-n reversal 的地方也能直接调
		 */
		template<typename WordT>
		inline WordT bitreverse_n( WordT x, int n = detail::word_bits_v<WordT> ) noexcept
		{
			return detail::bitreverse_low_bits_generic( x, n );
		}

		inline std::uint32_t bitreverse32( std::uint32_t x ) noexcept
		{
			return bitreverse_n<std::uint32_t>( x, 32 );
		}

		inline std::uint64_t bitreverse64( std::uint64_t x ) noexcept
		{
			return bitreverse_n<std::uint64_t>( x, 64 );
		}

		inline uint128_t bitreverse128( uint128_t x ) noexcept
		{
			return bitreverse_n<uint128_t>( x, 128 );
		}

		/**
		 * @brief aopr(x, n): all-one parity from the MSB side (via bit-reverse).
		 */
		/**
		 * @brief aopr(x): aop seen from the MSB side.
		 *
		 * 定义上：
		 *
		 *     aopr(x) = reverse( aop( reverse(x) ) )
		 *
		 * 所以：
		 * - `aop`  看的是从 LSB 方向展开的 1-run parity
		 * - `aopr` 看的是从 MSB 方向展开的 1-run parity
		 *
		 * Algorithm 4 正是依赖这个「从高位方向看 segment parity」的中间量。
		 */
		template<typename WordT>
		inline WordT aopr( WordT x, int n = detail::word_bits_v<WordT> ) noexcept
		{
			const WordT x_rev = bitreverse_n<WordT>( x, n );
			const WordT result = aop<WordT>( x_rev, n );
			return bitreverse_n<WordT>( result, n );
		}

		/**
		 * @brief LM-2001 Algorithm 4: construct gamma that maximizes DP+(alpha, beta ↦ gamma).
		 *
		 * This is the same closed-form construction as the original file, but written against a
		 * generic unsigned word type.
		 *
		 * Supported public widths:
		 *   - std::uint32_t
		 *   - std::uint64_t
		 *   - UnsignedInteger128Bit
		 *
		 * @param alpha input difference alpha (low n bits)
		 * @param beta  input difference beta  (low n bits)
		 * @param n     active word size, 1 <= n <= digits(WordT)
		 * @return      optimal output difference gamma (low n bits)
		 */
		/**
		 * @brief LM-2001 Algorithm 4: construct gamma* maximizing DP+(alpha,beta↦gamma).
		 *
		 * 这里按论文步骤对照来看，最清楚：
		 *
		 * Step 1:
		 *     r = alpha ∧ 1
		 *
		 *     这是最低位特殊处理，因为 bit 0 没有更低位 carry-in。
		 *
		 * Step 2:
		 *     e = ¬(alpha ⊕ beta) ∧ ¬r
		 *
		 *     e 标出 alpha 与 beta 相等、且不碰 bit0 特判的位置。
		 *
		 * Step 3:
		 *     a = e ∧ (e << 1) ∧ (alpha ⊕ (alpha << 1))
		 *
		 *     a 可理解为某些 segment 的“起点/边界标记”。
		 *     它同时要求：
		 *     - 相邻位置都在 e 内
		 *     - alpha 在边界上发生切换
		 *
		 * Step 4:
		 *     p = aopr(a)
		 *
		 *     从 MSB 方向看 segment parity，决定後面 segment 内部要不要翻转。
		 *
		 * Step 5:
		 *     a = (a ∨ (a >> 1)) ∧ ¬r
		 *
		 *     把原本的边界标记扩成真正参与 case split 的区域。
		 *
		 * Step 6:
		 *     b = (a ∨ e) << 1
		 *
		 *     这是另一个 case mask，控制 gamma 构造式里第二项启用的位置。
		 *
		 * Step 7:
		 *     gamma =
		 *         ((alpha ⊕ p) & a)
		 *       ∨ ((alpha ⊕ beta ⊕ (alpha << 1)) & ~a & b)
		 *       ∨ (alpha & ~a & ~b)
		 *
		 *     这一步就是整个 Algorithm 4 的 closed form。
		 *     它把 bit 位分成三大区块：
		 *     - a 区：用 alpha ⊕ p
		 *     - ~a & b 区：用 alpha ⊕ beta ⊕ (alpha << 1)
		 *     - ~a & ~b 区：直接沿用 alpha
		 *
		 * Step 8:
		 *     gamma_0 = (alpha ⊕ beta)_0
		 *
		 *     也就是：
		 *
		 *     gamma = (gamma & ~1) ∨ ((alpha ⊕ beta) & 1)
		 *
		 *     重新把 bit0 特判补回去。
		 *
		 * Step 9:
		 *     return gamma
		 *
		 * 工程注意：
		 * - 每一步都 mask 到 low n bits，不能偷懒
		 * - 尤其在 64 / 128 位时，任何 stray 高位都会污染後面整条公式
		 */
		template<typename WordT>
		inline WordT find_optimal_gamma( WordT alpha, WordT beta, int n = detail::word_bits_v<WordT> ) noexcept
		{
			static_assert( detail::is_supported_word_v<WordT>, "Unsupported word type for differential_optimal_gamma." );

			const WordT mask = detail::low_mask<WordT>( n );
			alpha &= mask;
			beta &= mask;

			// Step 1: r ← alpha ∧ 1
			const WordT r = alpha & WordT { 1 };

			// Step 2: e ← ¬(alpha ⊕ beta) ∧ ¬r
			const WordT e = ( ~( alpha ^ beta ) ) & ( ~r ) & mask;

			// Step 3: a ← e ∧ (e << 1) ∧ (alpha ⊕ (alpha << 1))
			WordT a = e & ( e << 1 ) & ( alpha ^ ( alpha << 1 ) ) & mask;

			// Step 4: p ← aopr(a, n)
			const WordT p = aopr<WordT>( a, n );

			// Step 5: a ← (a ∨ (a >> 1)) ∧ ¬r
			a = ( ( a | ( a >> 1 ) ) & ( ~r ) ) & mask;

			// Step 6: b ← (a ∨ e) << 1
			const WordT b = ( ( a | e ) << 1 ) & mask;

			// Step 7:
			// gamma ← ((alpha ⊕ p) ∧ a)
			//       ∨ ((alpha ⊕ beta ⊕ (alpha << 1)) ∧ ¬a ∧ b)
			//       ∨ (alpha ∧ ¬a ∧ ¬b)
			WordT gamma = ( ( ( alpha ^ p ) & a )
			              | ( ( alpha ^ beta ^ ( alpha << 1 ) ) & ( ~a ) & b )
			              | ( alpha & ( ~a ) & ( ~b ) ) ) & mask;

			// Step 8: gamma ← (gamma ∧ ¬1) ∨ ((alpha ⊕ beta) ∧ 1)
			gamma = ( ( gamma & ( ~WordT { 1 } ) ) | ( ( alpha ^ beta ) & WordT { 1 } ) ) & mask;

			return gamma;
		}

		inline std::uint32_t find_optimal_gamma32( std::uint32_t alpha, std::uint32_t beta, int n = 32 ) noexcept
		{
			return find_optimal_gamma<std::uint32_t>( alpha, beta, n );
		}

		inline std::uint64_t find_optimal_gamma64( std::uint64_t alpha, std::uint64_t beta, int n = 64 ) noexcept
		{
			return find_optimal_gamma<std::uint64_t>( alpha, beta, n );
		}

		inline uint128_t find_optimal_gamma128( uint128_t alpha, uint128_t beta, int n = 128 ) noexcept
		{
			return find_optimal_gamma<uint128_t>( alpha, beta, n );
		}

		/**
		 * @brief Convenience wrapper: return both gamma* and its LM-2001 weight.
		 *
		 * 用法上它相当於：
		 *
		 *     gamma* = find_optimal_gamma(alpha, beta, n)
		 *     w      = xdp_add_lm2001(_n)(alpha, beta, gamma*)
		 *
		 * 也就是：
		 * - 先构造 argmax_gamma
		 * - 再把这个 gamma* 丢给你现有的 XDP weight backend
		 */
		inline std::pair<std::uint32_t, SearchWeight> find_optimal_gamma_with_weight( std::uint32_t alpha, std::uint32_t beta, int n = 32 ) noexcept
		{
			const std::uint32_t gamma = find_optimal_gamma<std::uint32_t>( alpha, beta, n );
			const SearchWeight weight = ( n == 32 ) ? xdp_add_lm2001( alpha, beta, gamma ) : xdp_add_lm2001_n( alpha, beta, gamma, n );
			return { gamma, weight };
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
