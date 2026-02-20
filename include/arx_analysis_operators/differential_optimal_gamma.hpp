/**
 * @file differential_optimal_gamma.hpp
 * @brief 给定输入差分 (α,β) 快速构造最优输出差分 γ（LM-2001 Algorithm 4）
 *
 * 参考 / Ref:
 * - H. Lipmaa, S. Moriai, "Efficient Algorithms for Computing Differential Properties of Addition"
 *   FSE 2001 (LNCS 2355), 2002.（本项目 `papers/` 已收录）
 *
 * ----------------------------------------------------------------------------
 * 0) 背景：XOR 差分下的模加法 DP⁺
 * ----------------------------------------------------------------------------
 * 设位宽为 n（bit 0 = LSB），加法为模 \(2^n\)：
 *
 *   z  = x ⊞ y
 *   z' = (x ⊕ α) ⊞ (y ⊕ β)
 *   γ  = z ⊕ z'
 *
 * 定义 XOR differential probability：
 *
 *   DP⁺(α,β ↦ γ) := Pr_{x,y}[ (x+y) ⊕ ((x⊕α)+(y⊕β)) = γ ].
 *
 * 对固定 (α,β) 的最大差分概率定义为：
 *
 *   DP⁺_max(α,β) := max_γ DP⁺(α,β ↦ γ).
 *
 * ----------------------------------------------------------------------------
 * 1) 本文件解决的问题
 * ----------------------------------------------------------------------------
 * 论文 Algorithm 4 给出 Θ(log n) 的构造：输入 (α,β)，直接返回使 DP⁺ 最大的 γ（即 argmax）。
 * 这对“自动化差分特征搜索”（BnB/MILP/SAT 提示）很关键：避免枚举 2^n 个 γ。
 *
 * 在本项目中：
 * - `find_optimal_gamma(α,β,n)` 给出最优 γ
 * - 若还需要最优概率的指数形式，可用 `xdp_add_lm2001(_n)` 计算权重 w，并得 DP⁺ = 2^{-w}
 *
 * ----------------------------------------------------------------------------
 * 2) 工程化位宽约定 / Engineering notes
 * ----------------------------------------------------------------------------
 * - 以 `uint32_t` 承载 n-bit 向量；当 n<32 时，所有输入与中间量都显式 `& mask(n)` 以避免高位污染。
 * - 论文复杂度按 unit-cost RAM 记为 Θ(log n)。对固定 32-bit 机字而言，本实现等价于常数成本。
 * - `aopr` 需要 bit-reverse：对 n=32 采用 SWAR 常数实现；对 n!=32 给出 O(n) fallback（n<=32 时足够快）。
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
		 * @brief AOP(α,β,γ)：由 (α,β,γ) 推导“进位可能发生的位置”掩码
		 *
		 * 注意：这里的 AOP(α,β,γ) **不是** 论文 Algorithm 1 的 aop(x)（all-one parity），
		 * 只是名字相近，含义不同。为了避免混淆，本函数命名为 `carry_aop`。
		 *
		 * 常见写法（与论文中的按位推导等价）：
		 *
		 *   AOP(α,β,γ) = α ⊕ β ⊕ γ ⊕ ( ((α∧β) ⊕ ((α⊕β)∧γ)) << 1 )
		 *
		 * 直观含义：AOP[i]=1 表示在第 i 位可能出现“影响输出差分的进位状态”。
		 *
		 * @param alpha 输入差分 α
		 * @param beta  输入差分 β
		 * @param gamma 输出差分 γ
		 * @return AOP 掩码（32-bit）
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
		 * @brief LM-2001 Algorithm 1：aop(x) / all-one parity（32-bit）
		 *
		 * 论文定义（把 x 看作 n-bit bitstring）：
		 *
		 *   aop(x)_i = 1  ⇔  从第 i 位开始的连续 1 段（run of ones）长度为奇数。
		 *
		 * 该原语用于 Algorithm 4 的 `aopr`（从 MSB 方向看 run-parity）。
		 *
		 * 工程化说明：
		 * - 本实现固定 n=32，因此循环次数是常数（log2(32)=5），在实际代码里属于 O(1)。
		 *
		 * @param x 输入 32 位整数（视为 32-bit bitstring）
		 * @return aop(x) 的 32 位掩码
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
		 * @brief bit-reverse（仅 reverse 低 n 位）
		 *
		 * 论文中用于定义 aopr：令 x' 为 x 的位反转（x'_i := x_{n-1-i}）。
		 *
		 * 工程注意：
		 * - 当 n==32：使用 SWAR 常数步骤实现完整 32-bit reverse。
		 * - 当 n!=32：仅 reverse 低 n 位，并保证高位保持为 0（O(n) fallback）。
		 *
		 * @param x 输入 32 位整数
		 * @param n 位宽（默认 32；建议 1..32）
		 * @return 低 n 位反转后的结果
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
		 * @brief aopr(x,n)：all-one parity from MSB side（通过 bit-reverse 实现）
		 *
		 * 论文定义：
		 *
		 *   aopr(x) = aop(x'), 其中 x' 为 x 的 n-bit 位反转。
		 *
		 * 直观：aop(x) 是“从 LSB 方向”看 run-of-ones 的奇偶；aopr 则等价于“从 MSB 方向”看。
		 *
		 * @param x 输入（仅使用低 n 位）
		 * @param n 位宽（默认 32；建议 1..32）
		 * @return aopr(x,n) 掩码
		 */
		inline std::uint32_t aopr( std::uint32_t x, int n = 32 ) noexcept
		{
			std::uint32_t x_rev = bitreverse_n( x, n );
			std::uint32_t result = aop( x_rev );
			return bitreverse_n( result, n );
		}

		/**
		 * @brief LM-2001 Algorithm 4：find_optimal_gamma(α,β)（返回使 DP⁺ 最大的 γ）
		 *
		 * 这段逻辑是论文给出的“闭式构造”。代码基本逐行对应 Algorithm 4 的位向量操作。
		 *
		 * 变量语义（对照论文的 r/e/a/p/b）：
		 * - r = α ∧ 1：仅包含最低位的掩码（处理 bit0 的特殊约束）
		 * - e = ¬(α ⊕ β) ∧ ¬r：标出 α 与 β 相等的位置（并排除 bit0 的 r 影响）
		 * - a：从 e 与 α 的“边界变化”中提取需要处理的区段起点
		 * - p = aopr(a)：利用 all-one parity（从 MSB 方向）为区段选择“翻转/不翻转”的模式
		 * - b：辅助 mask，用于区分不同 bit-case 的输出 γ 公式分支
		 *
		 * 工程注意：
		 * - n<32 时，必须在每一步显式 `&mask`，否则高位垃圾会破坏位向量约束（这也是很多实现出错的点）。
		 * - 预期位宽：1 <= n <= 32。
		 *
		 * @param alpha 输入差分 α（仅使用低 n 位）
		 * @param beta  输入差分 β（仅使用低 n 位）
		 * @param n 位宽（默认 32）
		 * @return 最优输出差分 γ（仅低 n 位有效）
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
