/**
 * @file differential_optimal_gamma.hpp
 * @brief 查找最优输出差分γ - Lipmaa & Moriai (2001) Algorithm 4
 * 
 * 论文："Efficient Algorithms for Computing Differential Properties of Addition"
 *       Lipmaa & Moriai, FSE 2001
 * 
 * 功能：给定输入差分(α, β)，在Θ(log n)时间内找到最优输出差分γ
 *       使得 DP+(α, β → γ) = DP+_max(α, β)
 * 
 * 依赖算法：
 * - Algorithm 1: aop(x) - All-one parity, Θ(log n)
 * - Algorithm 4: find_optimal_gamma(α, β) - Θ(log n)
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include "arx_analysis_operators/differential_xdp_add.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		/**
		* Compute AOP (All Output Positions) function
		* 
		* Mathematical formula from Lipmaa-Moriai:
		* AOP(α, β, γ) = α ⊕ β ⊕ γ ⊕ ((α∧β) ⊕ ((α⊕β)∧γ)) << 1
		* 
		* Components:
		* 1. α ⊕ β ⊕ γ: XOR of all three differences
		* 2. α ∧ β: Both inputs have difference (both 1)
		* 3. (α⊕β) ∧ γ: XOR of inputs matches output difference
		* 4. << 1: Shift left (carry propagation)
		* 
		* Interpretation:
		* AOP[i] = 1 means bit position i can have non-zero carry
		* hw(AOP) = number of positions with possible carry = weight
		* 
		* @param alpha α
		* @param beta β
		* @param gamma γ
		* @return AOP value
		*/
		inline std::uint32_t carry_aop( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma )
		{
			std::uint32_t xor_part = alpha ^ beta ^ gamma;
			std::uint32_t alpha_and_beta = alpha & beta;
			std::uint32_t alpha_xor_beta = alpha ^ beta;
			std::uint32_t xor_and_gamma = alpha_xor_beta & gamma;
			std::uint32_t carry_part = alpha_and_beta ^ xor_and_gamma;

			std::uint32_t aop = xor_part ^ ( carry_part << 1 );

			return aop;
		}

		/**
		 * @brief Algorithm 1: 计算All-One Parity - Θ(log n)时间
		 * 
		 * 论文Algorithm 1 (Lines 292-299):
		 * 
		 * aop(x)_i = 1 iff x的第i位开始的连续1序列长度为奇数
		 * 
		 * 算法步骤（n = 32）：
		 * 1. x[1] = x ∧ (x >> 1)        ← 检测相邻的1对
		 * 2. 循环: x[i] = x[i-1] ∧ (x[i-1] >> 2^(i-1))  ← 倍增
		 * 3. y[1] = x ∧ ¬x[1]           ← 找单独的1
		 * 4. 循环: y[i] = y[i-1] ∨ ((y[i-1] >> 2^(i-1)) ∧ x[i-1])
		 * 5. Return y[log2(n)]
		 * 
		 * @param x 输入32位整数
		 * @return aop(x)
		 */
		inline std::uint32_t aop( std::uint32_t x ) noexcept
		{
			constexpr int log2_n = 5;  // log2(32) = 5

			// Step 1: x[1] = x ∧ (x >> 1)
			std::uint32_t x_arr[ 6 ];  // x[0] to x[5]
			x_arr[ 0 ] = x;
			x_arr[ 1 ] = x & ( x >> 1 );

			// Step 2: For i = 2 to log2(n) - 1
			for ( int i = 2; i < log2_n; ++i )
			{
				int shift = 1 << ( i - 1 );	 // 2^(i-1)
				x_arr[ i ] = x_arr[ i - 1 ] & ( x_arr[ i - 1 ] >> shift );
			}

			// Step 3: y[1] = x ∧ ¬x[1]
			std::uint32_t y_arr[ 6 ];  // y[0] to y[5]
			y_arr[ 0 ] = 0;
			y_arr[ 1 ] = x & ~x_arr[ 1 ];

			// Step 4: For i = 2 to log2(n)
			for ( int i = 2; i <= log2_n; ++i )
			{
				int shift = 1 << ( i - 1 );	 // 2^(i-1)
				y_arr[ i ] = y_arr[ i - 1 ] | ( ( y_arr[ i - 1 ] >> shift ) & x_arr[ i - 1 ] );
			}

			// Step 5: Return y[log2(n)]
			return y_arr[ log2_n ];
		}

		/**
		 * @brief Bit-reverse函数（n位）
		 * 
		 * 论文Line 302: x'_i := x_{n-i}
		 * 
		 * ⚠️ 关键：只reverse低n位，高位保持为0
		 * 
		 * @param x 输入32位整数
		 * @param n 位宽（默认32）
		 * @return bit-reversed的低n位
		 */
		inline std::uint32_t bitreverse_n( std::uint32_t x, int n = 32 ) noexcept
		{
			if ( n == 32 )
			{
				// 完整32位reverse
				x = ( ( x & 0x55555555 ) << 1 ) | ( ( x >> 1 ) & 0x55555555 );
				x = ( ( x & 0x33333333 ) << 2 ) | ( ( x >> 2 ) & 0x33333333 );
				x = ( ( x & 0x0F0F0F0F ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0F );
				x = ( ( x & 0x00FF00FF ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FF );
				x = ( x << 16 ) | ( x >> 16 );
				return x;
			}
			else
			{
				// 只reverse低n位
				std::uint32_t result = 0;
				for ( int i = 0; i < n; ++i )
				{
					if ( x & ( 1u << i ) )
					{
						result |= 1u << ( n - 1 - i );
					}
				}
				return result;
			}
		}

		/**
		 * @brief 32位完整bit-reverse（向后兼容）
		 */
		inline std::uint32_t bitreverse32( std::uint32_t x ) noexcept
		{
			return bitreverse_n( x, 32 );
		}

		/**
		 * @brief aopr(x, n) - All-One Parity Reverse（n位）
		 * 
		 * 论文Line 301-302:
		 * aopr(x) = aop(x'), where x'_i := x_{n-i}
		 * 
		 * ⚠️ 关键：只对低n位进行bit-reverse
		 * 
		 * @param x 输入32位整数
		 * @param n 位宽（默认32）
		 * @return aopr(x)
		 */
		inline std::uint32_t aopr( std::uint32_t x, int n = 32 ) noexcept
		{
			std::uint32_t x_rev = bitreverse_n( x, n );
			std::uint32_t result = aop( x_rev );
			return bitreverse_n( result, n );
		}

		/**
		 * @brief Algorithm 4: 查找最优输出差分γ - Θ(log n)时间
		 * 
		 * 论文Algorithm 4 (Lines 653-664):
		 * 
		 * 给定输入差分(α, β)，找到使得DP+(α, β → γ)最大的γ
		 * 
		 * ⚠️ 重要：支持任意位宽n（默认32）
		 * 
		 * 算法步骤：
		 * 1. r ← α ∧ 1;
		 * 2. e ← ¬(α ⊕ β) ∧ ¬r;
		 * 3. a ← e ∧ (e << 1) ∧ (α ⊕ (α << 1));
		 * 4. p ← aopr(a, n);  ← 注意n！
		 * 5. a ← (a ∨ (a >> 1)) ∧ ¬r;
		 * 6. b ← (a ∨ e) << 1;
		 * 7. γ ← ((α ⊕ p) ∧ a) ∨ ((α ⊕ β ⊕ (α << 1)) ∧ ¬a ∧ b) ∨ (α ∧ ¬a ∧ ¬b);
		 * 8. γ ← (γ ∧ ¬1) ∨ ((α ⊕ β) ∧ 1);
		 * 9. Return γ & mask(n)
		 * 
		 * @param alpha 输入差分α
		 * @param beta 输入差分β
		 * @param n 位宽（默认32）
		 * @return 最优输出差分γ
		 */
		inline std::uint32_t find_optimal_gamma( std::uint32_t alpha, std::uint32_t beta, int n = 32 ) noexcept
		{
			// Mask for n bits
			std::uint32_t mask = ( n == 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1 );
			alpha &= mask;
			beta &= mask;
			// Step 1: r ← α ∧ 1
			std::uint32_t r = alpha & 1;

			// Step 2: e ← ¬(α ⊕ β) ∧ ¬r
			std::uint32_t e = ( ~( alpha ^ beta ) ) & ( ~r ) & mask;  // ← Apply mask!

			// Step 3: a ← e ∧ (e << 1) ∧ (α ⊕ (α << 1))
			std::uint32_t a = e & ( e << 1 ) & ( alpha ^ ( alpha << 1 ) ) & mask;  // ← Apply mask!

			// Step 4: p ← aopr(a, n)  ← 重要：传入位宽！
			std::uint32_t p = aopr( a & mask, n );	// ← Apply mask!

			// Step 5: a ← (a ∨ (a >> 1)) ∧ ¬r
			a = ( ( a | ( a >> 1 ) ) & ( ~r ) ) & mask;	 // ← Apply mask!

			// Step 6: b ← (a ∨ e) << 1
			std::uint32_t b = ( ( a | e ) << 1 ) & mask;  // ← Apply mask!

			// Step 7: γ ← ((α ⊕ p) ∧ a) ∨ ((α ⊕ β ⊕ (α << 1)) ∧ ¬a ∧ b) ∨ (α ∧ ¬a ∧ ¬b)
			std::uint32_t gamma = ( ( ( alpha ^ p ) & a ) | ( ( alpha ^ beta ^ ( alpha << 1 ) ) & ~a & b ) | ( alpha & ~a & ~b ) ) & mask;	// ← Apply mask!

			// Step 8: γ ← (γ ∧ ¬1) ∨ ((α ⊕ β) ∧ 1)
			gamma = ( ( gamma & ~1u ) | ( ( alpha ^ beta ) & 1 ) ) & mask;	// ← Apply mask!

			// Step 9: Return γ
			return gamma;
		}

		/**
		 * @brief 包装函数：查找最优γ并计算其权重
		 * 
		 * 用于差分搜索框架，直接返回(最优γ, 权重)
		 * 
		 * @param alpha 输入差分α
		 * @param beta 输入差分β
		 * @param n 位宽（默认32）
		 * @return pair<最优γ, 权重>
		 */
		inline std::pair<std::uint32_t, int> find_optimal_gamma_with_weight( std::uint32_t alpha, std::uint32_t beta, int n = 32 ) noexcept
		{
			// Step 1: 使用Algorithm 4找到最优γ
			std::uint32_t gamma = find_optimal_gamma( alpha, beta, n );

			// Step 2: 使用Algorithm 2计算权重
			int weight = ( n == 32 ) ? xdp_add_lm2001( alpha, beta, gamma ) : xdp_add_lm2001_n( alpha, beta, gamma, n );

			return { gamma, weight };
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
