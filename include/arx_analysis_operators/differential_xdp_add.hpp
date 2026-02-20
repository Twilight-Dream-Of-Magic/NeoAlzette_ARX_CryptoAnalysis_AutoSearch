/**
 * @file differential_xdp_add.hpp
 * @brief XOR 差分下的模加法（变量-变量）差分概率 DP⁺ / 权重 w 计算
 *
 * 参考 / Ref:
 * - H. Lipmaa, S. Moriai, "Efficient Algorithms for Computing Differential Properties of Addition"
 *   FSE 2001 (LNCS 2355), 2002.（本项目 `papers/` 已收录）
 *
 * ----------------------------------------------------------------------------
 * 0) 问题定义（XOR differential of modular addition）
 * ----------------------------------------------------------------------------
 * 设位宽为 n（bit 0 = LSB），所有加法均为模 \(2^n\)：
 *
 *   z  = x ⊞ y
 *   z' = (x ⊕ α) ⊞ (y ⊕ β)
 *   γ  = z ⊕ z'
 *
 * 论文中的 XOR differential probability 定义为：
 *
 *   DP⁺(α,β ↦ γ) := Pr_{x,y}[ (x+y) ⊕ ((x⊕α)+(y⊕β)) = γ ].
 *
 * ----------------------------------------------------------------------------
 * 1) 输出量：weight w = -log2(DP⁺)
 * ----------------------------------------------------------------------------
 * Lipmaa–Moriai 证明：对固定 (α,β,γ)，DP⁺ 要么为 0（不可能），要么是 2 的负整数次幂：
 *
 *   DP⁺(α,β ↦ γ) = 2^{-w},  w ∈ Z_{\ge 0}.
 *
 * 其中 w 可由 Algorithm 2 的位并行约束直接计算：
 *
 *   w = HW( ¬eq(α,β,γ) ∧ mask(n-1) ).
 *
 * mask(n-1) 仅保留低 (n-1) 位（最高位不再产生“向更高位传播”的进位约束）。
 *
 * ----------------------------------------------------------------------------
 * 2) eq / ψ（psi-constraint）的工程化实现
 * ----------------------------------------------------------------------------
 * 论文里使用 eq(α,β,γ) 来表达逐位约束。本项目采用等价的 ψ 形式实现（无分支 + 易向量化）：
 *
 *   ψ(α,β,γ) = (¬α ⊕ β) ∧ (¬α ⊕ γ).
 *
 * 对单比特 i 而言：ψ_i = 1 当且仅当 α_i = β_i = γ_i（否则为 0）。
 * 因此 ¬ψ 表示“该位三者不全相等”，每出现一次会使 DP⁺ 额外乘上 1/2。
 *
 * ----------------------------------------------------------------------------
 * 3) API 约定
 * ----------------------------------------------------------------------------
 * - `xdp_add_lm2001` / `xdp_add_lm2001_n`：返回权重 w；若不可能则返回 -1。
 * - `xdp_add_probability`：返回 DP⁺（double），不可能时返回 0。
 *
 * ----------------------------------------------------------------------------
 * 4) 复杂度
 * ----------------------------------------------------------------------------
 * 对固定机字宽（如 32-bit）而言，本实现为常数成本：若干 XOR/AND/SHIFT + 1 次 popcount。
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <concepts>
#include <type_traits>

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
		template <class T>
		concept BitWord = std::unsigned_integral<T> && ( !std::same_as<T, bool> );

		/**
		 * @brief ψ(α,β,γ) / eq(α,β,γ) 位约束（LM-2001 的核心 bit-mask）
		 *
		 * 论文符号常写作 eq(α,β,γ)，这里用 ψ函数（psi-constraint）实现：
		 *
		 *   ψ(α,β,γ) = (¬α ⊕ β) ∧ (¬α ⊕ γ)
		 *
		 * 逐位含义：ψ_i = 1 ⇔ α_i = β_i = γ_i。
		 *
		 * @param alpha 输入差分 α（n-bit packed in uint32_t）
		 * @param beta  输入差分 β（n-bit packed in uint32_t）
		 * @param gamma 输出差分 γ（n-bit packed in uint32_t）
		 * @return ψ 的 bitmask（每一位给出约束是否满足）
		 */
		template <BitWord T>
		[[nodiscard]] constexpr T psi(T alpha, T beta, T gamma) noexcept
		{
			const T not_alpha = ~alpha;
			return (not_alpha ^ beta) & (not_alpha ^ gamma);
		}

		template <BitWord T>
		[[nodiscard]] constexpr T psi_with_mask(T alpha, T beta, T gamma, T mask) noexcept
		{
			const T not_alpha = (~alpha) & mask;
			return ((not_alpha ^ beta) & (not_alpha ^ gamma)) & mask;
		}

		/**
		 * @brief LM-2001 Algorithm 2：计算 XOR 差分模加的权重 w（32-bit word）
		 *
		 * 该函数对应论文 Algorithm 2（位并行检查 + popcount 计数）。
		 *
		 * - **可行性检查（good/psi check）**
		 *
		 *   若满足：
		 *     ψ(α<<1,β<<1,γ<<1) ∧ ( (α⊕β⊕γ) ⊕ (β<<1) ) != 0
		 *   则差分不可能（DP⁺=0）。
		 *
		 * - **权重（w = -log2 DP⁺）**
		 *
		 *   w = HW( ¬ψ(α,β,γ) ∧ mask(31) )，
		 *   其中 mask(31)=0x7fffffff（低 31 位）。
		 *
		 * @param alpha 输入差分 α（32-bit）
		 * @param beta  输入差分 β（32-bit）
		 * @param gamma 输出差分 γ（32-bit）
		 * @return 权重 w；若差分不可能则返回 -1
		 */
		inline int xdp_add_lm2001( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma ) noexcept
		{
			// Step 1: 可行性检查（good check，使用 ψ/eq 约束）
			std::uint32_t beta_shifted = beta << 1;

			//make shifted
			std::uint32_t psi_shifted = psi<std::uint32_t>( alpha << 1, beta_shifted, gamma << 1 );
			std::uint32_t xor_val = alpha ^ beta ^ gamma;
			std::uint32_t xor_condition = xor_val ^ beta_shifted;

			// If (psi_shifted & xor_condition) != 0, differential is impossible
			if ( ( psi_shifted & xor_condition ) != 0 )
			{
				return -1;	// Impossible differential
			}

			// Step 2: 计算权重 w（w = HW(¬eq ∧ mask(n-1))）
			// 论文里的 eq(α,β,γ) 在本实现中用 ψ(α,β,γ) 表示（见 psi_32）。
			std::uint32_t eq_val = psi<std::uint32_t>( alpha, beta, gamma );

			// mask(n-1) = 0x7FFFFFFF (低31位)
			constexpr std::uint32_t mask_lower = 0x7FFFFFFF;

			std::uint32_t masked_bad = ( ~eq_val ) & mask_lower;

			// weight = popcount(¬eq(α,β,γ) & mask(n-1))
			#if __cpp_lib_bitops >= 201907L
				int weight = std::popcount(masked_bad);
			#elif defined(_MSC_VER)
				int weight = __popcnt( masked_bad );
			#else
				int weight = __builtin_popcount( masked_bad );
			#endif

			return weight;
		}

		/**
		 * @brief 由权重返回 DP⁺（DP⁺ = 2^{-w}）
		 *
		 * @return 若不可能则返回 0；否则返回 2^{-w}
		 */
		inline double xdp_add_probability( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma ) noexcept
		{
			int weight = xdp_add_lm2001( alpha, beta, gamma );
			if ( weight < 0 )
				return 0.0;
			return std::pow( 2.0, -weight );
		}

		/**
		 * @brief 检查差分是否可能（DP⁺>0）
		 */
		inline bool is_xdp_add_possible( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma ) noexcept
		{
			return xdp_add_lm2001( alpha, beta, gamma ) >= 0;
		}

		/**
		 * @brief LM-2001 Algorithm 2：支持任意位宽 n（1..32）的权重计算
		 *
		 * 工程注意点：
		 * - 输入 α/β/γ 会被截断到低 n 位（mask(n)）。
		 * - 所有中间量也会显式 &mask，避免 n<32 时高位污染约束。
		 *
		 * @param n 位宽，建议 1 <= n <= 32
		 */
		inline int xdp_add_lm2001_n( std::uint32_t alpha, std::uint32_t beta, std::uint32_t gamma, int n ) noexcept
		{
			std::uint32_t mask = ( n == 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1 );
			alpha &= mask;
			beta &= mask;
			gamma &= mask;

			std::uint32_t alpha_shifted = ( alpha << 1 ) & mask;
			std::uint32_t beta_shifted = ( beta << 1 ) & mask;
			std::uint32_t gamma_shifted = ( gamma << 1 ) & mask;

			// psi_shifted
			std::uint32_t psi_shifted = psi<std::uint32_t>( alpha_shifted, beta_shifted, gamma_shifted );

			std::uint32_t xor_val = ( alpha ^ beta ^ gamma ) & mask;
			std::uint32_t xor_condition = ( xor_val ^ beta_shifted ) & mask;

			if ( ( psi_shifted & xor_condition ) != 0 )
			{
				return -1;
			}

			// eq_val (psi)
			std::uint32_t psi_val = psi_with_mask<std::uint32_t>( alpha, beta, gamma, mask );

			std::uint32_t mask_lower = ( n == 32 ) ? 0x7FFFFFFFu : ( ( 1u << ( n - 1 ) ) - 1 );
			std::uint32_t masked_bad = ( ~psi_val ) & mask_lower;

			#if __cpp_lib_bitops >= 201907L
				int weight = std::popcount( masked_bad );
			#elif defined(_MSC_VER)
				int weight = __popcnt( masked_bad );
			#else
				int weight = __builtin_popcount( masked_bad );
			#endif

			return weight;
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
