/**
 * @file differential_probability/constant_weight_evaluation.hpp
 * @brief 常量加法 XOR 差分分析（变量-常量）：y = x ⊞ a (mod 2^n)
 * 
 * 参考论文：
 * - Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
 *   (ASIACRYPT 2020, LNCS 12491)。
 * - 扩展版本（含应用与 impossible-differential）："A Bit-Vector Differential Model for the Modular Addition by a Constant
 *   and its Applications to Differential and Impossible-Differential Cryptanalysis"（你专案 `papers/` 中亦有收录）。
 * - 精确 DP（递回/逐位形式）最早可追溯到：
 *   Machado, "Differential Probability of Modular Addition with a Constant Operand", IACR ePrint 2001/052。
 *
 * ----------------------------------------------------------------------------
 * 0) 符号 / 位序约定（务必先对齐，避免「看起来对、其实差一位」）
 * ----------------------------------------------------------------------------
 * - 位元索引：bit 0 = LSB，bit (n-1) = MSB。
 * - 论文约定：对所有 i<0，视 u[i]=v[i]=a[i]=0（本档所有涉及 i-λ 之类的索引也遵守此约定）。
 * - XOR 差分（也是本专案差分的预设）：给定输入差分 u=Δx 与输出差分 v=Δy：
 *
 *   y  = x ⊞ a                 (mod 2^n)
 *   y' = (x ⊕ u) ⊞ a           (mod 2^n)
 *   v  = y ⊕ y'
 *
 *   我们关心：Pr_x[ v ]，以及 weight^a(u,v) = -log2(Pr_x[v])。
 *
 * - 参数命名对照（避免缩写，但保留论文符号以便对照）：
 *   - delta_x  ↔ u（输入 XOR 差分）
 *   - delta_y  ↔ v（输出 XOR 差分）
 *   - constant ↔ a（加法常量）
 *
 * - 本档**只**讨论差分（differential）中的
 *      y = x ⊞ a
 *   这一类「一边变量、一边常量」的 Q1 权重/概率评估器。
 *   不涉及线性相关（linear correlation）那一支，也不与 var-var `x ⊞ y` 混写。
 * 
 * ----------------------------------------------------------------------------
 * 1) 你会在这个档案看到的「三种」量（每个都对应明确的论文公式）
 * ----------------------------------------------------------------------------
 * 1) 精确 DP / count（Machado 2001/052；Azimi DCC 2022 Theorem 2 等价叙述）
 *    - count(u,a,v) = #{ x ∈ {0,1}^n | (x ⊞ a) ⊕ ((x ⊕ u) ⊞ a) = v }
 *    - DP = Pr[u -> v] = count / 2^n
 *    - exact_weight = -log2(DP) = n - log2(count)
 *    对应 API：
 *      diff_addconst_exact_count_n / diff_addconst_exact_probability_n / diff_addconst_exact_weight_n
 *
 * 2) 精确 weight（Azimi Lemma 3/4/5 的闭式拆解；不做 Qκ 截断）
 *    论文给出（把 -Σ log2(ϕ_i) 拆成「整数项」+「链项」+「π_i 的 log」）：
 *
 *      weight^a(u,v)
 *        = HW(((u ⊕ v) << 1))                       (Lemma 3: i∉I 的整数部分)
 *        + Σ_{i∈I} (λ_i - 1)                         (Lemma 5: 链长贡献)
 *        - Σ_{i∈I} log2(π_i)                         (Lemma 4: ϕ_i = π_i / 2^{λ_i-1})
 *
 *    对应 API：
 *      diff_addconst_weight_log2pi_n
 *
 * 3) 近似 weight：BvWeight^κ（Azimi Algorithm 1；κ=4 即论文预设 Q4）
 *    - 目的：把 log2(π_i) 的小数部分用 κ 个 bits 的 fixed-point 近似（SMT/bit-vector 好处理）。
 *    - 输出：BvWeight^κ(u,v,a) 是 Qκ（低 κ bits 为小数）；代表
 *        apxweight^a(u,v) ≈ 2^{-κ} * BvWeight^κ(u,v,a)
 *    对应 API：
 *      diff_addconst_bvweight_fixed_point_n  (通用 κ)
 *      diff_addconst_bvweight_q4_n           (κ=4 的便捷封装)
 * 
 * 本档工程化实作说明：
 * - 对「C++ 搜寻框架」而言，固定 32-bit 时 O(n)=32 几乎等同常数成本；
 *   因此我们在 `diff_addconst_bvweight_fixed_point_n` / `diff_addconst_weight_log2pi_n` 中
 *   **直接依论文的数学定义展开计算**（仍然输出同一个近似量 BvWeight^κ / 精确 weight），
 *   让每个中间量（链长 λᵢ、πᵢ、Truncate(πᵢ)）都可被列印/单元测试审计。
 * - 若你的目标是 SMT 编码，则更偏好使用论文的「位向量原语」写法；本档仍保留 HW/LZ/RevCarry/ParallelLog/ParallelTrunc
 *   等基元与对应注解，便於未来再切回纯 bit-vector constraints。
 *
 * ----------------------------------------------------------------------------
 * 2) 工程分层（2026-04-11 之后的模板化版本）
 * ----------------------------------------------------------------------------
 * - `bitvector::...`
 *   论文中的位向量基元 / O(1) 或 O(log n) 小工具。
 *
 * - `detail::..._impl<WordT>(...)`
 *   这是**唯一一套**核心实现。
 *   数学公式不变，只在编译期根据 `WordT` 选择：
 *   - `WordT = uint32_t`  : n <= 32
 *   - `WordT = uint64_t`  : n <= 64
 *
 *   对应的 exact count 类型也在编译期决定：
 *   - `uint32_t -> uint64_t count`
 *   - `uint64_t -> UnsignedInteger128Bit count`
 *
 *   原因很简单：
 *   - count 的数学定义是一样的
 *   - 只是 n=64 时，最大 count 可能到 2^64，不能再放在 `uint64_t`
 *
 * - public overloads（下方对外 API）
 *   仍然保留 32/64 两组明确定义的入口与论文注释。
 *   这样做是为了：
 *   1) 对外介面一眼能看懂
 *   2) 内部不再复制两份只差位宽的逻辑
 *   3) 不删你已经写好的完整论文对照说明
 */

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "arx_analysis_operators/UnsignedInteger128Bit.hpp"
#include "arx_analysis_operators/math_util.hpp"

namespace TwilightDream
{
	namespace bitvector
	{

		// ============================================================================
		// 基础位向量操作（O(1) 或 O(log n)）
		// ============================================================================

		/**
		 * @brief HW(x) - Hamming Weight（汉明重量）
		 * 
		 * 计算x中1的个数
		 * 复杂度：O(1)（硬件指令）
		 * 
		 * 论文：第209-213行
		 */
		inline constexpr std::uint32_t HammingWeight( std::uint32_t x ) noexcept
		{
			return static_cast<std::uint32_t>( std::popcount( x ) );
		}

		inline constexpr std::uint32_t HammingWeight( std::uint64_t x ) noexcept
		{
			return static_cast<std::uint32_t>( std::popcount( x ) );
		}

		// Forward declaration (Rev uses BitReverse)
		inline constexpr std::uint32_t BitReverse( std::uint32_t x ) noexcept;

		/**
		 * @brief Bit-reverse (n bits) - fallback for n != 32.
		 */
		inline constexpr std::uint32_t BitReverse_n( std::uint32_t x, int n ) noexcept
		{
			if ( n <= 0 )
				return 0u;
			if ( n >= 32 )
				return BitReverse( x );

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

		/**
		 * @brief Carry(x,y) modulo 2^n.
		 */
		inline constexpr std::uint32_t Carry_n( std::uint32_t x, std::uint32_t y, int n ) noexcept
		{
			if ( n <= 0 )
				return 0u;

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			x &= mask;
			y &= mask;
			return ( x ^ y ^ ( ( x + y ) & mask ) ) & mask;
		}

		/**
		 * @brief RevCarry(x,y) for n bits (uses BitReverse + Carry_n).
		 */
		inline constexpr std::uint32_t RevCarry_n( std::uint32_t x, std::uint32_t y, int n ) noexcept
		{
			if ( n <= 0 )
				return 0u;

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			x &= mask;
			y &= mask;
			return BitReverse_n( Carry_n( BitReverse_n( x, n ), BitReverse_n( y, n ), n ), n ) & mask;
		}

		/**
		 * @brief BitReverse(x) - Bit Reversal（位反转）
		 * 
		 * 反转x的位顺序：Rev(x) = (x[0], x[1], ..., x[n-1])
		 * 复杂度：O(log n)
		 * 
		 * 论文：第204-206行
		 * 参考：Hacker's Delight, Fig. 7-1
		 */
		inline constexpr std::uint32_t BitReverse( std::uint32_t x ) noexcept
		{
			// 交换相邻位
			x = ( ( x & 0x55555555u ) << 1 ) | ( ( x >> 1 ) & 0x55555555u );
			// 交换相邻2位组
			x = ( ( x & 0x33333333u ) << 2 ) | ( ( x >> 2 ) & 0x33333333u );
			// 交换相邻4位组
			x = ( ( x & 0x0F0F0F0Fu ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0Fu );
			// 交换字节
			x = ( ( x & 0x00FF00FFu ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FFu );
			// 交换半字
			x = ( x << 16 ) | ( x >> 16 );
			return x;
		}


		inline uint32_t CountLeftZeros( uint32_t x ) noexcept
		{
			if ( x == 0 )
				return 32u;
			return static_cast<uint32_t>( std::countl_zero( x ) );
		}

		inline uint32_t CountLeftZeros( std::uint64_t x ) noexcept
		{
			if ( x == 0 )
				return 64u;
			return static_cast<uint32_t>( std::countl_zero( x ) );
		}

		/**
		 * @brief LZ(x) - Leading Zeros（前导零标记）
		 * 
		 * 标记x的前导零位
		 * 定义：LZ(x)[i] = 1 ⟺ x[n-1, i] = 0
		 * 即：从最高位到第i位都是0
		 * 复杂度：O(1)（使用硬件指令）
		 * 
		 * 论文：第214-218行
		 */
		inline uint32_t LeadingZeros( uint32_t x ) noexcept
		{
			if ( x == 0 )
				return 0xFFFFFFFFu;

			int clz = CountLeftZeros( x );

			if ( clz == 0 )
				return 0u;	// 避免 <<32
			return 0xFFFFFFFFu << ( 32 - clz );
		}

		/**
		 * @brief LeadingZeros for n-bit domain.
		 *
		 * LZ(x)[i] = 1  ⟺  x[n-1,i] == 0
		 */
		inline uint32_t LeadingZeros_n( uint32_t x, int n ) noexcept
		{
			if ( n <= 0 )
				return 0u;
			const uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			x &= mask;
			if ( x == 0 )
				return mask;

			// Find MSB position within [0..n-1]
			int msb = 31 - CountLeftZeros( x );
			// Mark bits above msb within n bits
			const uint32_t above_msb_mask = ( msb >= 31 ) ? 0u : ( ~( ( 1u << ( msb + 1 ) ) - 1u ) );
			return above_msb_mask & mask;
		}

		inline std::uint64_t LeadingZeros_n( std::uint64_t x, int n ) noexcept
		{
			if ( n <= 0 )
				return 0ull;
			const std::uint64_t mask = ( n >= 64 ) ? 0xFFFFFFFFFFFFFFFFull : ( ( std::uint64_t( 1 ) << n ) - 1ull );
			x &= mask;
			if ( x == 0ull )
				return mask;

			const int msb = 63 - static_cast<int>( CountLeftZeros( x ) );
			const std::uint64_t above_msb_mask = ( msb >= 63 ) ? 0ull : ( ~( ( std::uint64_t( 1 ) << ( msb + 1 ) ) - 1ull ) );
			return above_msb_mask & mask;
		}

		/**
		 * @brief Carry(x, y) - 进位链
		 * 
		 * 计算x + y的进位链
		 * 公式：Carry(x, y) = x ⊕ y ⊕ (x ⊞ y)
		 * 复杂度：O(1)
		 * 
		 * 论文：第198-200行
		 */
		inline uint32_t Carry( uint32_t x, uint32_t y ) noexcept
		{
			return x ^ y ^ ( ( x + y ) & 0xFFFFFFFF );
		}

		/**
		 * @brief RevCarry(x, y) - 反向进位
		 * 
		 * 从右到左的进位传播
		 * 公式：RevCarry(x, y) = Rev(Carry(Rev(x), Rev(y)))
		 * 复杂度：O(log n)
		 * 
		 * 论文：第207-208行
		 */
		inline uint32_t BitReverseCarry( uint32_t x, uint32_t y ) noexcept
		{
			return BitReverse( Carry( BitReverse( x ), BitReverse( y ) ) );
		}

		// ============================================================================
		// 高级位向量操作（Algorithm 1核心）
		// ============================================================================

		/**
		 * @brief ParallelLog(x, y) - 并行对数
		 * 
		 * 对於y分隔的子向量，并行计算x的对数（整数部分）
		 * 公式：ParallelLog(x, y) = HW(RevCarry(x ∧ y, y))
		 * 复杂度：O(log n)
		 * 
		 * 论文：第1479行，Proposition 1(a)
		 * 
		 * @param x 数据向量
		 * @param y 分隔向量（每个子向量为 (1,1,...,1,0)）
		 * @return 所有子向量的 log₂ 之和
 */
		inline uint32_t ParallelLog( uint32_t x, uint32_t y ) noexcept
		{
			return HammingWeight( BitReverseCarry( x & y, y ) );
		}

		/**
		 * @brief ParallelTrunc(x, y) - 并行截断
		 * 
		 * 对於 y 分隔的子向量，并行提取 x 的「Truncate(...)」值（4 bits）。
		 *
		 * 论文 Proposition 1(b)：
		 * - 对每个 delimited sub-vector x[i_t, j_t]（由 y[i_t, j_t]=(1,1,...,1,0) 指示），
		 *   其对应的 Truncate 作用在 x[i_t, j_t+1]（注意：j_t 那一位是分隔用的 0，不属於有效资料位）。
		 * - Truncate(z) 取的是 z 的 **4 个最高位**（不足 4 则右侧补 0），并把它当成 4-bit 整数。
		 *
		 * 工程化理解：
		 * - 在 BvWeight/ apxlog2 的使用情境中，这 4 bits 会对应到「MSB 之後的 4 个 bits」（Eq.(4)），
		 *   也就是 apxlog2 的 fraction bits。
		 *
		 * 实作方式（Proposition 1(b) 的位运算展开，等价於论文中的 HW(z_λ) 组合）：
		 * - 在 bit0=LSB 的索引约定下，要选出每个子向量的「MSB 往下数第 λ 位」，
		 *   需同时保证该位上方连续为 1（避免短链造成错位），因此 z_λ 需要把 y 的多个右移版本做交集：
		 *     z_λ = x ∧ (y >> 0) ∧ (y >> 1) ∧ ... ∧ (y >> λ) ∧ ¬(y >> (λ+1))，λ=0..3
		 * - 则 ParallelTrunc(x,y) = (HW(z0)<<3) + (HW(z1)<<2) + (HW(z2)<<1) + HW(z3)
		 *
		 * 复杂度：O(log n)
		 * 
		 * 论文：第1480-1492行，Proposition 1(b)
		 * 
		 * @param x 数据向量
		 * @param y 分隔向量
		 * @return 所有子向量的截断小数部分之和
		 */
		inline uint32_t ParallelTrunc( uint32_t x, uint32_t y ) noexcept
		{
			// z_λ = x ∧ (y >> 0) ∧ ... ∧ (y >> λ) ∧ ¬(y >> (λ+1))
			const uint32_t y0 = y;
			const uint32_t y1 = ( y >> 1 );
			const uint32_t y2 = ( y >> 2 );
			const uint32_t y3 = ( y >> 3 );
			const uint32_t y4 = ( y >> 4 );

			const uint32_t z0 = x & y0 & ~y1;
			const uint32_t z1 = x & y0 & y1 & ~y2;
			const uint32_t z2 = x & y0 & y1 & y2 & ~y3;
			const uint32_t z3 = x & y0 & y1 & y2 & y3 & ~y4;

			// Proposition 1(b): (HW(z0)<<3) + (HW(z1)<<2) + (HW(z2)<<1) + HW(z3)
			uint32_t result = ( HammingWeight( z0 ) << 3 );
			result += ( HammingWeight( z1 ) << 2 );
			result += ( HammingWeight( z2 ) << 1 );
			result += HammingWeight( z3 );
			return result;
		}

		/**
		 * @brief ParallelLog for n-bit domain.
		 *
		 * 论文语意（Azimi bit-vector primitives）：
		 * - ParallelLog(x, sep) 会把 x 中由 sep 指示的「区段」做并行的 log/层级聚合，
		 *   在 Algorithm 1 里用来取得 floor(log2(π_i)) 等整数部分。
		 *
		 * 工程化说明（本档的定位）：
		 * - 我们的 BvWeight^κ 实作采用「逐链计算 π_i」的可审计写法，
		 *   因此并不强依赖 ParallelLog/ParallelTrunc 的黑盒位运算；
		 *   但仍保留这些 primitives 以及 n-bit wrapper，方便：
		 *   1) 日後回切成纯 bit-vector constraints（SMT/MILP/SAT）
		 *   2) 对照论文 pseudo-code 的原始结构
		 */
		inline uint32_t ParallelLog_n( uint32_t x, uint32_t y, int n ) noexcept
		{
			const uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			x &= mask;
			y &= mask;
			if ( n == 32 )
				return ParallelLog( x, y );
			return HammingWeight( RevCarry_n( x & y, y, n ) & mask );
		}

		/**
		 * @brief ParallelTrunc for n-bit domain.
		 *
		 * 论文语意（Azimi Eq.(4) 的 Truncate）：
		 * - κ=4 时，Truncate(π_i[m-1,0]) 取的是 π_i 的 MSB 右侧 4 个 bits（不足补 0），
		 *   用来构造 apxlog2(π_i) 的小数部分。
		 *
		 * 同 ParallelLog_n，本工程主要采用逐链可审计版本；此 wrapper 主要是保留可对照性。
		 */
		inline uint32_t ParallelTrunc_n( uint32_t x, uint32_t y, int n ) noexcept
		{
			const uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			x &= mask;
			y &= mask;
			if ( n == 32 )
				return ParallelTrunc( x, y );

			const uint32_t y0 = y & mask;
			const uint32_t y1 = ( y >> 1 ) & mask;
			const uint32_t y2 = ( y >> 2 ) & mask;
			const uint32_t y3 = ( y >> 3 ) & mask;
			const uint32_t y4 = ( y >> 4 ) & mask;

			const uint32_t z0 = x & y0 & ~y1;
			const uint32_t z1 = x & y0 & y1 & ~y2;
			const uint32_t z2 = x & y0 & y1 & y2 & ~y3;
			const uint32_t z3 = x & y0 & y1 & y2 & y3 & ~y4;

			uint32_t result = ( HammingWeight( z0 ) << 3 );
			result += ( HammingWeight( z1 ) << 2 );
			result += ( HammingWeight( z2 ) << 1 );
			result += HammingWeight( z3 );
			return result;
		}

	}  // namespace bitvector

	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;
		using TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
		using uint128_t = UnsignedInteger128Bit;

		inline constexpr bool MachadoDiffAddConstExactUseFast2State = true;

		namespace detail
		{

			struct AddConstCarryEdgeMatrix2
			{
				std::uint8_t entry[ 2 ][ 2 ] {};
			};

			[[nodiscard]] inline constexpr AddConstCarryEdgeMatrix2 make_addconst_carry_edge_matrix(
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				std::uint32_t carry_difference_bit,
				std::uint32_t next_carry_difference_bit ) noexcept
			{
				AddConstCarryEdgeMatrix2 matrix {};
				for ( std::uint32_t carry0 = 0u; carry0 <= 1u; ++carry0 )
				{
					const std::uint32_t carry1 = carry0 ^ carry_difference_bit;
					for ( std::uint32_t x0_bit = 0u; x0_bit <= 1u; ++x0_bit )
					{
						const std::uint32_t x1_bit = x0_bit ^ input_difference_bit;
						const std::uint32_t next_carry0 =
							( x0_bit & constant_bit ) | ( x0_bit & carry0 ) | ( constant_bit & carry0 );
						const std::uint32_t next_carry1 =
							( x1_bit & constant_bit ) | ( x1_bit & carry1 ) | ( constant_bit & carry1 );
						if ( ( next_carry0 ^ next_carry1 ) != next_carry_difference_bit )
							continue;

						matrix.entry[ carry0 ][ next_carry0 ] += 1u;
					}
				}
				return matrix;
			}

			inline void multiply_addconst_carry_row_vector_2state(
				const std::uint64_t in[ 2 ],
				const AddConstCarryEdgeMatrix2& matrix,
				std::uint64_t out[ 2 ] ) noexcept
			{
				out[ 0 ] = 0ull;
				out[ 1 ] = 0ull;

				for ( int row = 0; row < 2; ++row )
				{
					const std::uint64_t row_value = in[ row ];
					if ( row_value == 0ull )
						continue;

					for ( int column = 0; column < 2; ++column )
					{
						const std::uint64_t weight = matrix.entry[ row ][ column ];
						if ( weight == 0ull )
							continue;
						out[ column ] += row_value * weight;
					}
				}
			}

			[[nodiscard]] inline constexpr std::uint32_t addconst_carry_edge_pattern_id(
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				std::uint32_t carry_difference_bit,
				std::uint32_t next_carry_difference_bit ) noexcept
			{
				return input_difference_bit |
					   ( constant_bit << 1 ) |
					   ( carry_difference_bit << 2 ) |
					   ( next_carry_difference_bit << 3 );
			}

			[[nodiscard]] inline const std::array<AddConstCarryEdgeMatrix2, 16>& addconst_carry_edge_matrices() noexcept
			{
				static const std::array<AddConstCarryEdgeMatrix2, 16> matrices = []() constexpr noexcept
				{
					std::array<AddConstCarryEdgeMatrix2, 16> table {};
					for ( std::uint32_t pattern = 0; pattern < 16u; ++pattern )
					{
						table[ pattern ] = make_addconst_carry_edge_matrix(
							pattern & 1u,
							( pattern >> 1 ) & 1u,
							( pattern >> 2 ) & 1u,
							( pattern >> 3 ) & 1u );
					}
					return table;
				}();
				return matrices;
			}

			[[nodiscard]] inline std::uint8_t addconst_carry_edge_next_mask( std::uint8_t current_state_mask, std::uint32_t pattern_id ) noexcept
			{
				static const std::array<std::array<std::uint8_t, 4>, 16> transition_masks = []() noexcept
				{
					std::array<std::array<std::uint8_t, 4>, 16> table {};
					const auto&								   matrices = addconst_carry_edge_matrices();

					for ( std::uint32_t pattern = 0; pattern < 16u; ++pattern )
					{
						for ( std::uint32_t current_mask = 0; current_mask < 4u; ++current_mask )
						{
							std::uint8_t next_mask = 0u;
							for ( int state = 0; state < 2; ++state )
							{
								if ( ( current_mask & ( 1u << state ) ) == 0u )
									continue;

								for ( int next_state = 0; next_state < 2; ++next_state )
								{
									if ( matrices[ pattern ].entry[ state ][ next_state ] != 0u )
									{
										next_mask |= std::uint8_t( 1u << next_state );
									}
								}
							}
							table[ pattern ][ current_mask ] = next_mask;
						}
					}
					return table;
				}();
				return transition_masks[ pattern_id & 15u ][ current_state_mask & 0x3u ];
			}

			[[nodiscard]] inline constexpr std::uint32_t addconst_low_bit_mask( int bit_count ) noexcept
			{
				if ( bit_count <= 0 )
					return 0u;
				return ( bit_count >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << bit_count ) - 1u );
			}

			[[nodiscard]] inline constexpr std::uint32_t addconst_extract_low_bits( std::uint32_t value, int bit_count ) noexcept
			{
				return value & addconst_low_bit_mask( bit_count );
			}

			[[nodiscard]] inline int addconst_lsb_run_length(
				std::uint32_t shifted_bits,
				std::uint32_t bit_value,
				int max_length ) noexcept
			{
				if ( max_length <= 0 )
					return 0;

				const std::uint32_t transition_bits = ( bit_value == 0u ) ? shifted_bits : ~shifted_bits;
				const int raw_run_length = static_cast<int>( std::countr_zero( transition_bits ) );
				return ( raw_run_length < max_length ) ? raw_run_length : max_length;
			}

			inline void apply_addconst_zero_run_count_segment(
				std::uint64_t& count_with_carry0,
				std::uint64_t& count_with_carry1,
				std::uint32_t constant_bit,
				std::uint32_t segment_input_bits,
				int segment_length ) noexcept
			{
				if ( segment_length <= 0 )
					return;

				const std::uint64_t previous_count_with_carry0 = count_with_carry0;
				const std::uint64_t previous_count_with_carry1 = count_with_carry1;
				const std::uint64_t scale = ( 1ull << segment_length );

				if ( segment_input_bits == 0u )
				{
					if ( constant_bit == 0u )
					{
						count_with_carry0 = ( scale * previous_count_with_carry0 ) + ( ( scale - 1ull ) * previous_count_with_carry1 );
						count_with_carry1 = previous_count_with_carry1;
					}
					else
					{
						count_with_carry0 = previous_count_with_carry0;
						count_with_carry1 = ( ( scale - 1ull ) * previous_count_with_carry0 ) + ( scale * previous_count_with_carry1 );
					}
					return;
				}

				const int first_input_one_offset = static_cast<int>( std::countr_zero( segment_input_bits ) );
				const std::uint64_t projected_suffix_weight = scale - ( 1ull << ( segment_length - first_input_one_offset ) );
				if ( constant_bit == 0u )
				{
					count_with_carry0 = ( scale * previous_count_with_carry0 ) + ( projected_suffix_weight * previous_count_with_carry1 );
					count_with_carry1 = 0ull;
				}
				else
				{
					count_with_carry0 = 0ull;
					count_with_carry1 = ( projected_suffix_weight * previous_count_with_carry0 ) + ( scale * previous_count_with_carry1 );
				}
			}

			template<typename WordT>
			[[nodiscard]] inline constexpr WordT addconst_low_bit_mask_generic( int bit_count ) noexcept
			{
				if ( bit_count <= 0 )
					return WordT { 0 };
				constexpr int word_bits = int( sizeof( WordT ) * 8 );
				return ( bit_count >= word_bits ) ? WordT( ~WordT { 0 } ) : ( ( WordT { 1 } << bit_count ) - WordT { 1 } );
			}

			template<typename WordT>
			[[nodiscard]] inline constexpr WordT addconst_extract_low_bits_generic( WordT value, int bit_count ) noexcept
			{
				return value & addconst_low_bit_mask_generic<WordT>( bit_count );
			}

			template<typename WordT>
			[[nodiscard]] inline int addconst_lsb_run_length_generic(
				WordT shifted_bits,
				WordT bit_value,
				int max_length ) noexcept
			{
				if ( max_length <= 0 )
					return 0;

				const WordT transition_bits = ( bit_value == WordT { 0 } ) ? shifted_bits : ~shifted_bits;
				const int raw_run_length = static_cast<int>( std::countr_zero( transition_bits ) );
				return ( raw_run_length < max_length ) ? raw_run_length : max_length;
			}

			template<typename CountT, typename WordT>
			inline void apply_addconst_zero_run_count_segment_generic(
				CountT& count_with_carry0,
				CountT& count_with_carry1,
				WordT constant_bit,
				WordT segment_input_bits,
				int segment_length ) noexcept
			{
				if ( segment_length <= 0 )
					return;

				const CountT previous_count_with_carry0 = count_with_carry0;
				const CountT previous_count_with_carry1 = count_with_carry1;
				const CountT scale = CountT { 1 } << segment_length;

				if ( segment_input_bits == WordT { 0 } )
				{
					if ( constant_bit == WordT { 0 } )
					{
						count_with_carry0 = ( scale * previous_count_with_carry0 ) + ( ( scale - CountT { 1 } ) * previous_count_with_carry1 );
						count_with_carry1 = previous_count_with_carry1;
					}
					else
					{
						count_with_carry0 = previous_count_with_carry0;
						count_with_carry1 = ( ( scale - CountT { 1 } ) * previous_count_with_carry0 ) + ( scale * previous_count_with_carry1 );
					}
					return;
				}

				const int first_input_one_offset = static_cast<int>( std::countr_zero( segment_input_bits ) );
				const CountT projected_suffix_weight =
					scale - ( CountT { 1 } << ( segment_length - first_input_one_offset ) );
				if ( constant_bit == WordT { 0 } )
				{
					count_with_carry0 = ( scale * previous_count_with_carry0 ) + ( projected_suffix_weight * previous_count_with_carry1 );
					count_with_carry1 = CountT {};
				}
				else
				{
					count_with_carry0 = CountT {};
					count_with_carry1 = ( projected_suffix_weight * previous_count_with_carry0 ) + ( scale * previous_count_with_carry1 );
				}
			}

			[[nodiscard]] inline long double unsigned128_to_long_double( const uint128_t& value ) noexcept
			{
				long double out = std::scalbn( static_cast<long double>( value.high64() ), 64 );
				out += static_cast<long double>( value.low64() );
				return out;
			}

			[[nodiscard]] inline std::uint8_t apply_addconst_zero_run_support_segment(
				std::uint8_t possible_state_mask,
				std::uint32_t constant_bit,
				std::uint32_t segment_input_bits ) noexcept
			{
				if ( possible_state_mask == 0u )
					return 0u;

				if ( segment_input_bits == 0u )
				{
					return ( constant_bit == 0u )
							   ? ( ( possible_state_mask & 0x2u ) != 0u ? 0x3u : ( possible_state_mask & 0x1u ) )
							   : ( ( possible_state_mask & 0x1u ) != 0u ? 0x3u : ( possible_state_mask & 0x2u ) );
				}

				const int first_input_one_offset = static_cast<int>( std::countr_zero( segment_input_bits ) );
				if ( constant_bit == 0u )
				{
					return ( first_input_one_offset == 0 ) ? ( possible_state_mask & 0x1u ) : 0x1u;
				}
				return ( first_input_one_offset == 0 ) ? ( possible_state_mask & 0x2u ) : 0x2u;
			}

			template<typename WordT>
			[[nodiscard]] inline std::uint8_t apply_addconst_zero_run_support_segment_generic(
				std::uint8_t possible_state_mask,
				WordT constant_bit,
				WordT segment_input_bits ) noexcept
			{
				if ( possible_state_mask == 0u )
					return 0u;

				if ( segment_input_bits == WordT { 0 } )
				{
					return ( constant_bit == WordT { 0 } )
							   ? ( ( possible_state_mask & 0x2u ) != 0u ? 0x3u : ( possible_state_mask & 0x1u ) )
							   : ( ( possible_state_mask & 0x1u ) != 0u ? 0x3u : ( possible_state_mask & 0x2u ) );
				}

				const int first_input_one_offset = static_cast<int>( std::countr_zero( segment_input_bits ) );
				if ( constant_bit == WordT { 0 } )
					return ( first_input_one_offset == 0 ) ? ( possible_state_mask & 0x1u ) : 0x1u;
				return ( first_input_one_offset == 0 ) ? ( possible_state_mask & 0x2u ) : 0x2u;
			}

			struct AddConstCarryPatternMatrix4
			{
				std::uint64_t entry[ 4 ][ 4 ] {};
			};

			[[nodiscard]] inline AddConstCarryPatternMatrix4 make_addconst_carry_pattern_matrix(
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				std::uint32_t output_difference_bit ) noexcept
			{
				AddConstCarryPatternMatrix4 matrix {};
				for ( int state = 0; state < 4; ++state )
				{
					const std::uint32_t carry0 = std::uint32_t( state & 1 );
					const std::uint32_t carry1 = std::uint32_t( ( state >> 1 ) & 1 );

					for ( std::uint32_t x0_bit = 0; x0_bit <= 1u; ++x0_bit )
					{
						const std::uint32_t x1_bit = x0_bit ^ input_difference_bit;
						const std::uint32_t y0_bit = x0_bit ^ constant_bit ^ carry0;
						const std::uint32_t y1_bit = x1_bit ^ constant_bit ^ carry1;
						if ( ( y0_bit ^ y1_bit ) != output_difference_bit )
							continue;

						const std::uint32_t next_carry0 = ( x0_bit & constant_bit ) | ( x0_bit & carry0 ) | ( constant_bit & carry0 );
						const std::uint32_t next_carry1 = ( x1_bit & constant_bit ) | ( x1_bit & carry1 ) | ( constant_bit & carry1 );
						const int			  next_state = int( next_carry0 | ( next_carry1 << 1 ) );
						matrix.entry[ state ][ next_state ] += 1ull;
					}
				}
				return matrix;
			}

			[[nodiscard]] inline AddConstCarryPatternMatrix4 addconst_carry_pattern_matrix_identity() noexcept
			{
				AddConstCarryPatternMatrix4 matrix {};
				for ( int state = 0; state < 4; ++state )
				{
					matrix.entry[ state ][ state ] = 1ull;
				}
				return matrix;
			}

			[[nodiscard]] inline AddConstCarryPatternMatrix4 multiply_addconst_carry_pattern_matrices(
				const AddConstCarryPatternMatrix4& left,
				const AddConstCarryPatternMatrix4& right ) noexcept
			{
				AddConstCarryPatternMatrix4 out {};
				for ( int row = 0; row < 4; ++row )
				{
					for ( int middle = 0; middle < 4; ++middle )
					{
						const std::uint64_t left_value = left.entry[ row ][ middle ];
						if ( left_value == 0ull )
							continue;

						for ( int column = 0; column < 4; ++column )
						{
							const std::uint64_t right_value = right.entry[ middle ][ column ];
							if ( right_value == 0ull )
								continue;
							out.entry[ row ][ column ] += left_value * right_value;
						}
					}
				}
				return out;
			}

			inline void multiply_addconst_carry_row_vector(
				const std::uint64_t in[ 4 ],
				const AddConstCarryPatternMatrix4& matrix,
				std::uint64_t out[ 4 ] ) noexcept
			{
				out[ 0 ] = 0ull;
				out[ 1 ] = 0ull;
				out[ 2 ] = 0ull;
				out[ 3 ] = 0ull;

				for ( int row = 0; row < 4; ++row )
				{
					const std::uint64_t row_value = in[ row ];
					if ( row_value == 0ull )
						continue;

					for ( int column = 0; column < 4; ++column )
					{
						const std::uint64_t weight = matrix.entry[ row ][ column ];
						if ( weight == 0ull )
							continue;
						out[ column ] += row_value * weight;
					}
				}
			}

			[[nodiscard]] inline std::uint32_t addconst_carry_pattern_id(
				std::uint32_t input_difference_bit,
				std::uint32_t constant_bit,
				std::uint32_t output_difference_bit ) noexcept
			{
				return input_difference_bit | ( constant_bit << 1 ) | ( output_difference_bit << 2 );
			}

			[[nodiscard]] inline const std::array<AddConstCarryPatternMatrix4, 8>& addconst_carry_pattern_matrices() noexcept
			{
				static const std::array<AddConstCarryPatternMatrix4, 8> matrices = []() noexcept
				{
					std::array<AddConstCarryPatternMatrix4, 8> table {};
					for ( std::uint32_t pattern = 0; pattern < 8u; ++pattern )
					{
						table[ pattern ] = make_addconst_carry_pattern_matrix(
							pattern & 1u,
							( pattern >> 1 ) & 1u,
							( pattern >> 2 ) & 1u );
					}
					return table;
				}();
				return matrices;
			}

			[[nodiscard]] inline const std::array<std::array<AddConstCarryPatternMatrix4, 33>, 8>& addconst_carry_pattern_matrix_powers() noexcept
			{
				static const std::array<std::array<AddConstCarryPatternMatrix4, 33>, 8> powers = []() noexcept
				{
					std::array<std::array<AddConstCarryPatternMatrix4, 33>, 8> table {};
					const auto&												 base_matrices = addconst_carry_pattern_matrices();

					for ( std::uint32_t pattern = 0; pattern < 8u; ++pattern )
					{
						table[ pattern ][ 0 ] = addconst_carry_pattern_matrix_identity();
						for ( int run_length = 1; run_length <= 32; ++run_length )
						{
							table[ pattern ][ run_length ] =
								multiply_addconst_carry_pattern_matrices(
									table[ pattern ][ run_length - 1 ],
									base_matrices[ pattern ] );
						}
					}
					return table;
				}();
				return powers;
			}

			[[nodiscard]] inline std::uint8_t addconst_carry_pattern_next_mask( std::uint8_t current_state_mask, std::uint32_t pattern_id ) noexcept
			{
				static const std::array<std::array<std::uint8_t, 16>, 8> transition_masks = []() noexcept
				{
					std::array<std::array<std::uint8_t, 16>, 8> table {};
					const auto&								 matrices = addconst_carry_pattern_matrices();

					for ( std::uint32_t pattern = 0; pattern < 8u; ++pattern )
					{
						for ( std::uint32_t current_mask = 0; current_mask < 16u; ++current_mask )
						{
							std::uint8_t next_mask = 0u;
							for ( int state = 0; state < 4; ++state )
							{
								if ( ( current_mask & ( 1u << state ) ) == 0u )
									continue;

								for ( int next_state = 0; next_state < 4; ++next_state )
								{
									if ( matrices[ pattern ].entry[ state ][ next_state ] != 0ull )
									{
										next_mask |= std::uint8_t( 1u << next_state );
									}
								}
							}
							table[ pattern ][ current_mask ] = next_mask;
						}
					}
					return table;
				}();
				return transition_masks[ pattern_id & 7u ][ current_state_mask & 0xFu ];
			}

			// --------------------------------------------------------------------
			// Paper-to-Code Map for the template helpers below
			// --------------------------------------------------------------------
			//
			// 1) Machado 2001/052 + Azimi Theorem 2
			//    -> `is_diff_addconst_possible_n_impl`
			//    -> `diff_addconst_exact_count_n_impl`
			//    -> `diff_addconst_exact_probability_n_impl`
			//    -> `diff_addconst_exact_weight_n_impl`
			//
			//    数学对象：
			//      Pr^a[u -> v] = ϕ0 * ... * ϕ_{n-1}
			//      count(u,a,v) = #{ x | (x⊞a) ⊕ ((x⊕u)⊞a) = v }
			//
			//    工程实现：
			//    - 不直接显式保存论文的每个 ϕ_i / δ_i
			//    - 而是用与其等价的 carry-DP / count-DP 来计算 exact count 与可行性
			//
			// 2) Azimi Eq.(3) + Lemma 3 / 4 / 5
			//    -> `diff_addconst_weight_log2pi_n_impl`
			//
			//      weight^a(u,v)
			//        = HW(((u ⊕ v) << 1))
			//          + Σ_{i∈I}(λ_i - 1)
			//          - Σ_{i∈I} log2(π_i)
			//
			// 3) Azimi Eq.(5)(6) + Algorithm 1
			//    -> `diff_addconst_bvweight_fixed_point_n_impl`
			//
			//      apxlog2 / apxweight / BvWeight^κ
			//
			//    工程实现：
			//    - 论文用 ParallelLog / ParallelTrunc 组合成 bit-vector expression
			//    - 这里则直接逐链抽取 π_i 的 integer part / κ-bit fraction
			//    - 语义保持一致，主要是为了让 C++ 路径更可审计

			template<typename WordT>
			struct DiffAddConstExactCountType;

			template<>
			struct DiffAddConstExactCountType<std::uint32_t>
			{
				using type = std::uint64_t;
			};

			template<>
			struct DiffAddConstExactCountType<std::uint64_t>
			{
				using type = uint128_t;
			};

			template<typename WordT>
			using DiffAddConstExactCountType_t = typename DiffAddConstExactCountType<WordT>::type;

			/**
			 * @brief Supported machine-word widths for this differential Q1 implementation.
			 *
			 * 数学上我们只需要 n-bit words；
			 * 工程上目前明确支持：
			 * - `uint32_t` : 1 <= n <= 32
			 * - `uint64_t` : 1 <= n <= 64
			 *
			 * 若未来要再扩到 128-bit，应先检查：
			 * - carry DP 的局部位运算 helper
			 * - exact count 所需整数宽度
			 * - BvWeight / log2(pi) 中的位索引与截断逻辑
			 */
			template<typename WordT>
			inline constexpr bool is_supported_diff_addconst_word_v =
				std::is_same_v<WordT, std::uint32_t> || std::is_same_v<WordT, std::uint64_t>;

			/**
			 * @brief Convert exact count to floating form for DP / weight display.
			 *
			 * 数学上：
			 *   DP = count / 2^n
			 *   weight = n - log2(count)
			 *
			 * 所以这里不是新公式，只是把 exact count 变成可喂给
			 * `ldexp` / `log2` 的浮点值。
			 */
			[[nodiscard]] inline long double diff_addconst_count_to_long_double( std::uint64_t count ) noexcept
			{
				return static_cast<long double>( count );
			}

			[[nodiscard]] inline long double diff_addconst_count_to_long_double( const uint128_t& count ) noexcept
			{
				return unsigned128_to_long_double( count );
			}

			[[nodiscard]] inline int diff_addconst_count_bit_width( std::uint64_t count ) noexcept
			{
				return static_cast<int>( std::bit_width( count ) );
			}

			[[nodiscard]] inline int diff_addconst_count_bit_width( const uint128_t& count ) noexcept
			{
				return count.bit_width();
			}

			/**
			 * @brief Word-generic exact feasibility check.
			 *
			 * 这对应 public `is_diff_addconst_possible_n(...)` 的模板化核心。
			 * 数学语义完全不变：
			 * - 若存在 x 使 `(x⊞a) ⊕ ((x⊕u)⊞a) = v`，则回传 true
			 * - 否则回传 false
			 *
			 * 与 32/64 的唯一区别只是：
			 * - mask 的位宽
			 * - shift / run-length 发生在 32-bit 还是 64-bit machine word 上
			 */
			template<typename WordT>
			[[nodiscard]] inline bool is_diff_addconst_possible_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				static_assert( is_supported_diff_addconst_word_v<WordT>, "Unsupported word size for diff_addconst exact APIs." );

				if ( n <= 0 )
					return ( delta_x == WordT {} && delta_y == WordT {} );

				// u ← Δx[n−1:0], v ← Δy[n−1:0], a ← constant[n−1:0]
				const WordT mask = addconst_low_bit_mask_generic<WordT>( n );
				delta_x &= mask;  // u := u mod 2ⁿ
				delta_y &= mask;  // v := v mod 2ⁿ
				constant &= mask; // a := a mod 2ⁿ
				// d := u ⊕ v
				const WordT carry_difference = ( delta_x ^ delta_y ) & mask;

				if constexpr ( MachadoDiffAddConstExactUseFast2State )
				{
					// d₀ = c₀ ⊕ c'₀，而 c₀ = c'₀ = 0，所以必须 d₀ = 0
					if ( ( carry_difference & WordT { 1 } ) != WordT {} )
						return false;

					// reachable mask over cᵢ ∈ {0,1}；初始只有 c₀ = 0
					std::uint8_t possible_state_mask = 0x1u;
					for ( int bit_index = 0; bit_index + 1 < n; )
					{
						// 当前 edge 处理的是 i → i+1，对应 dᵢ 与 d_{i+1}
						const WordT shifted_carry_difference = ( carry_difference >> bit_index );
						const std::uint32_t carry_difference_edge_pattern =
							static_cast<std::uint32_t>( shifted_carry_difference & WordT { 3 } );

						if ( carry_difference_edge_pattern == 0x3u )
						{
							// (d_{i},d_{i+1}) = (1,1) ⇒ 2×2 edge kernel = I
							// 数学上这段 edge 不改变 reachable carry-set
							const int one_run_bits =
								addconst_lsb_run_length_generic( shifted_carry_difference, WordT { 1 }, n - bit_index );
							bit_index += ( one_run_bits - 1 );
							continue;
						}

						if ( carry_difference_edge_pattern == 0x0u )
						{
							// (d_{i},d_{i+1}) = (0,0) 的连续 run：
							// 用 Machado/Azimi 等价的 zero-run 闭式，而不是逐 edge 展开
							const int zero_run_bits =
								addconst_lsb_run_length_generic( shifted_carry_difference, WordT { 0 }, n - bit_index );
							const int zero_run_end = bit_index + zero_run_bits - 1;
							while ( bit_index < zero_run_end )
							{
								const WordT shifted_constant = ( constant >> bit_index );
								const WordT constant_bit = shifted_constant & WordT { 1 }; // aᵢ
								const int segment_length =
									addconst_lsb_run_length_generic( shifted_constant, constant_bit, zero_run_end - bit_index );
								const WordT segment_input_bits =
									addconst_extract_low_bits_generic<WordT>( delta_x >> bit_index, segment_length ); // u[i+ℓ−1:i]

								// 按 zero-run support 闭式更新：
								// reachable(cᵢ)  ↦  reachable(cᵢ₊ℓ)
								possible_state_mask = apply_addconst_zero_run_support_segment_generic(
									possible_state_mask,
									constant_bit,
									segment_input_bits );
								if ( possible_state_mask == 0u )
									return false;
								bit_index += segment_length;
							}
							continue;
						}

						const WordT input_difference_bit = ( delta_x >> bit_index ) & WordT { 1 }; // uᵢ
						const WordT constant_bit = ( constant >> bit_index ) & WordT { 1 };         // aᵢ
						if ( carry_difference_edge_pattern == 0x1u )
						{
							// (d_{i},d_{i+1}) = (1,0)
							// 根据局部 2×2 转移矩阵，所有 reachable state 收缩到单一下一状态
							possible_state_mask =
								( possible_state_mask == 0u ) ? 0u : ( constant_bit != WordT {} ? 0x2u : 0x1u );
						}
						else
						{
							// (d_{i},d_{i+1}) = (0,1)
							// 若 uᵢ = 0，则该局部模式不可能；
							// 若 uᵢ = 1，则由 aᵢ 决定哪一个当前 carry 可分裂到两条下一状态
							if ( input_difference_bit == WordT {} )
								return false;
							possible_state_mask =
								( constant_bit == WordT {} )
									? ( ( possible_state_mask & 0x2u ) != 0u ? 0x3u : 0u )
									: ( ( possible_state_mask & 0x1u ) != 0u ? 0x3u : 0u );
						}
						if ( possible_state_mask == 0u )
							return false;
						++bit_index;
					}
					return possible_state_mask != 0u;
				}

				return false;
			}

			/**
			 * @brief Word-generic exact count implementation.
			 *
			 * 这就是 Machado exact count 的模板化核心：
			 *
			 *   count(u,a,v)
			 *     = #{ x | (x⊞a) ⊕ ((x⊕u)⊞a) = v }
			 *
			 * 公式与 32-bit 版**完全一样**，只是：
			 * - `WordT` 决定我们在几位 machine word 上做局部 bit 操作
			 * - `CountT` 决定精确计数不会因 n=64 而溢位
			 */
			template<typename WordT>
			[[nodiscard]] inline DiffAddConstExactCountType_t<WordT> diff_addconst_exact_count_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				static_assert( is_supported_diff_addconst_word_v<WordT>, "Unsupported word size for diff_addconst exact APIs." );

				using CountT = DiffAddConstExactCountType_t<WordT>;

				if ( n <= 0 )
					return ( delta_x == WordT {} && delta_y == WordT {} ) ? CountT { 1 } : CountT {};

				// u ← Δx[n−1:0], v ← Δy[n−1:0], a ← constant[n−1:0]
				const WordT mask = addconst_low_bit_mask_generic<WordT>( n );
				delta_x &= mask;
				delta_y &= mask;
				constant &= mask;

				// d := u ⊕ v；这是 2-state recurrence 里的 cᵢ ⊕ c'ᵢ
				const WordT carry_difference = ( delta_x ^ delta_y ) & mask;

				if constexpr ( MachadoDiffAddConstExactUseFast2State )
				{
					// d₀ = 0 是必要条件，否则不存在任何 x
					if ( ( carry_difference & WordT { 1 } ) != WordT {} )
						return CountT {};

					// Nᵢ(0), Nᵢ(1):
					// 到达当前 bit i 时，第一条加法 carry cᵢ 为 0/1 的精确路径计数
					CountT count_with_carry0 { 1 };
					CountT count_with_carry1 { 0 };
					for ( int bit_index = 0; bit_index + 1 < n; )
					{
						// edge pattern = (d_{i},d_{i+1})
						const WordT shifted_carry_difference = ( carry_difference >> bit_index );
						const std::uint32_t carry_difference_edge_pattern =
							static_cast<std::uint32_t>( shifted_carry_difference & WordT { 3 } );

						if ( carry_difference_edge_pattern == 0x3u )
						{
							// (1,1) ⇒ edge kernel = I
							// 所以计数向量 [Nᵢ(0),Nᵢ(1)] 不变
							const int one_run_bits =
								addconst_lsb_run_length_generic( shifted_carry_difference, WordT { 1 }, n - bit_index );
							bit_index += ( one_run_bits - 1 );
							continue;
						}

						if ( carry_difference_edge_pattern == 0x0u )
						{
							// (0,0) 的 zero-run：
							// 用闭式一次吞掉长度 ℓ 的 run，
							// 对应把连续 edge matrix 相乘后的结果直接写成显式公式
							const int zero_run_bits =
								addconst_lsb_run_length_generic( shifted_carry_difference, WordT { 0 }, n - bit_index );
							const int zero_run_end = bit_index + zero_run_bits - 1;
							while ( bit_index < zero_run_end )
							{
								const WordT shifted_constant = ( constant >> bit_index );
								const WordT constant_bit = shifted_constant & WordT { 1 }; // aᵢ
								const int segment_length =
									addconst_lsb_run_length_generic( shifted_constant, constant_bit, zero_run_end - bit_index );
								const WordT segment_input_bits =
									addconst_extract_low_bits_generic<WordT>( delta_x >> bit_index, segment_length ); // u[i+ℓ−1:i]

								// [Nᵢ(0),Nᵢ(1)]  ←  [Nᵢ₊ℓ(0),Nᵢ₊ℓ(1)]
								// 用 Machado 递推在 zero-run 上的闭式计算
								apply_addconst_zero_run_count_segment_generic(
									count_with_carry0,
									count_with_carry1,
									constant_bit,
									segment_input_bits,
									segment_length );
								if ( count_with_carry0 == CountT {} && count_with_carry1 == CountT {} )
									return CountT {};

								bit_index += segment_length;
							}
							continue;
						}

						const WordT input_difference_bit = ( delta_x >> bit_index ) & WordT { 1 }; // uᵢ
						const WordT constant_bit = ( constant >> bit_index ) & WordT { 1 };         // aᵢ
						const CountT previous_count_with_carry0 = count_with_carry0;
						const CountT previous_count_with_carry1 = count_with_carry1;
						if ( carry_difference_edge_pattern == 0x1u )
						{
							// (d_{i},d_{i+1}) = (1,0)
							// edge matrix:
							//   aᵢ=0 : [[1,0],[1,0]]
							//   aᵢ=1 : [[0,1],[0,1]]
							const CountT sum = previous_count_with_carry0 + previous_count_with_carry1;
							if ( constant_bit == WordT {} )
							{
								count_with_carry0 = sum;
								count_with_carry1 = CountT {};
							}
							else
							{
								count_with_carry0 = CountT {};
								count_with_carry1 = sum;
							}
						}
						else
						{
							// (d_{i},d_{i+1}) = (0,1)
							// edge matrix:
							//   uᵢ=0 : impossible
							//   uᵢ=1,aᵢ=0 : [[0,0],[1,1]]
							//   uᵢ=1,aᵢ=1 : [[1,1],[0,0]]
							if ( input_difference_bit == WordT {} )
								return CountT {};
							if ( constant_bit == WordT {} )
							{
								count_with_carry0 = previous_count_with_carry1;
								count_with_carry1 = previous_count_with_carry1;
							}
							else
							{
								count_with_carry0 = previous_count_with_carry0;
								count_with_carry1 = previous_count_with_carry0;
							}
						}
						if ( count_with_carry0 == CountT {} && count_with_carry1 == CountT {} )
							return CountT {};
						++bit_index;
					}

					// 最终 carry-out 不参与 mod 2ⁿ 输出，因此要把 cₙ=0/1 两种尾状态都计入，
					// 并乘上末位未约束的 2 倍因子
					return CountT { 2 } * ( count_with_carry0 + count_with_carry1 );
				}

				return CountT {};
			}

			/**
			 * @brief Word-generic exact probability helper.
			 *
			 * 只是在 exact count 之上套：
			 *   DP = count / 2^n
			 */
			template<typename WordT>
			[[nodiscard]] inline double diff_addconst_exact_probability_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				const auto count = diff_addconst_exact_count_n_impl<WordT>( delta_x, constant, delta_y, n );
				if ( count == DiffAddConstExactCountType_t<WordT> {} )
					return 0.0;
				const long double probability = std::ldexp( diff_addconst_count_to_long_double( count ), -n );
				return static_cast<double>( probability );
			}

			/**
			 * @brief Word-generic exact weight helper.
			 *
			 * 直接对应论文里的：
			 *   weight = -log2(DP) = n - log2(count)
			 */
			template<typename WordT>
			[[nodiscard]] inline double diff_addconst_exact_weight_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				const auto count = diff_addconst_exact_count_n_impl<WordT>( delta_x, constant, delta_y, n );
				if ( count == DiffAddConstExactCountType_t<WordT> {} )
					return std::numeric_limits<double>::infinity();
				return static_cast<double>( n ) - std::log2( static_cast<double>( diff_addconst_count_to_long_double( count ) ) );
			}

			/**
			 * @brief Word-generic ceil(exact weight) helper.
			 *
			 * 仍然用同一个恒等式：
			 *   ceil(weight) = n - floor(log2(count))
			 */
			template<typename WordT>
			[[nodiscard]] inline SearchWeight diff_addconst_exact_weight_ceil_int_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				const auto count = diff_addconst_exact_count_n_impl<WordT>( delta_x, constant, delta_y, n );
				if ( count == DiffAddConstExactCountType_t<WordT> {} )
					return INFINITE_WEIGHT;

				const int floor_log2_count = diff_addconst_count_bit_width( count ) - 1;
				return static_cast<SearchWeight>( n - floor_log2_count );
			}

			/**
			 * @brief Word-generic exact `log2(pi)` weight helper.
			 *
			 * 这对应 Azimi Lemma 3/4/5 的闭式：
			 *
			 *   weight^a(u,v)
			 *     = HW(((u ⊕ v) << 1))
			 *       + Σ_{i∈I}(λ_i - 1)
			 *       - Σ_{i∈I} log2(π_i)
			 *
			 * 模板化之后依然是这一条公式，
			 * 没有换数学，只是把 `u,v,a` 的 machine-word 类型参数化了。
			 */
			template<typename WordT>
			[[nodiscard]] inline double diff_addconst_weight_log2pi_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n ) noexcept
			{
				using namespace bitvector;
				static_assert( is_supported_diff_addconst_word_v<WordT>, "Unsupported word size for diff_addconst exact APIs." );

				if ( n <= 0 )
					return 0.0;
				if ( !is_diff_addconst_possible_n_impl<WordT>( delta_x, constant, delta_y, n ) )
					return std::numeric_limits<double>::infinity();

				const WordT mask = addconst_low_bit_mask_generic<WordT>( n );
				const WordT input_difference = delta_x & mask;    // u
				const WordT output_difference = delta_y & mask;   // v
				const WordT additive_constant = constant & mask;  // a

				// Lemma 3:
				//   − Σ_{i∉I} log₂(ϕᵢ) = HW(((u ⊕ v) << 1))
				const std::uint32_t hamming_weight_of_u_xor_v_shifted_one =
					HammingWeight( ( ( input_difference ^ output_difference ) << 1 ) & mask );
				// Lemma 5:
				//   s₀₀₀ = ¬(u << 1) ∧ ¬(v << 1)
				//   s'₀₀₀ = s₀₀₀ ∧ ¬LZ(¬s₀₀₀)
				//   Σ_{i∈I}(λᵢ − 1) = HW(s'₀₀₀)
				const WordT s000 = ( ~( input_difference << 1 ) & ~( output_difference << 1 ) ) & mask;
				const WordT s000_prime = s000 & ~LeadingZeros_n( ( ~s000 ) & mask, n );
				const std::uint32_t sum_chain_lengths_minus_one = HammingWeight( s000_prime );

				long double sum_log2_pi = 0.0L;
				WordT	   remaining_chain_mask = s000_prime;
				constexpr int word_bits = int( sizeof( WordT ) * 8 );
				while ( remaining_chain_mask != WordT {} )
				{
					const int run_most_significant_bit_index =
						( word_bits - 1 ) - static_cast<int>( CountLeftZeros( remaining_chain_mask ) );
					int run_least_significant_bit_index = run_most_significant_bit_index;
					while ( run_least_significant_bit_index > 0 &&
							( ( remaining_chain_mask >> ( run_least_significant_bit_index - 1 ) ) & WordT { 1 } ) == WordT { 1 } )
					{
						--run_least_significant_bit_index;
					}

					const int run_length = run_most_significant_bit_index - run_least_significant_bit_index + 1;
					const int state_index_i = run_most_significant_bit_index + 1;
					const int carry_chain_length_lambda_i = run_length + 1;
					const WordT run_mask =
						( run_length >= word_bits )
							? ~WordT {}
							: ( ( ( WordT { 1 } << run_length ) - WordT { 1 } ) << run_least_significant_bit_index );
					remaining_chain_mask &= ~run_mask;

					if ( state_index_i <= 0 || state_index_i >= n )
						continue;

					// 这里在逐条 carry-chain Γᵢ 上恢复 Lemma 4 的 πᵢ：
					//   若 u[i] ⊕ v[i] ⊕ a[i−1] = 1，则
					//      πᵢ = a[i−2, i−λᵢ] + a[i−λᵢ−1]
					//   否则
					//      πᵢ = 2^{λᵢ−1} − (a[i−2, i−λᵢ] + a[i−λᵢ−1])
					WordT constant_window_value = WordT {};
					for ( int bit_offset = 0; bit_offset <= carry_chain_length_lambda_i - 2; ++bit_offset )
					{
						const int bit_index = ( state_index_i - carry_chain_length_lambda_i ) + bit_offset;
						const WordT bit =
							( bit_index >= 0 && bit_index < n ) ? ( ( additive_constant >> bit_index ) & WordT { 1 } ) : WordT {};
						constant_window_value |= bit << bit_offset;
					}
					const int extra_bit_index = state_index_i - carry_chain_length_lambda_i - 1;
					const WordT extra_bit =
						( extra_bit_index >= 0 && extra_bit_index < n ) ? ( ( additive_constant >> extra_bit_index ) & WordT { 1 } ) : WordT {};
					const WordT sum_value = constant_window_value + extra_bit;

					const WordT condition_bit =
						( ( ( input_difference >> state_index_i ) ^ ( output_difference >> state_index_i ) ^ ( additive_constant >> ( state_index_i - 1 ) ) ) & WordT { 1 } );
					const WordT denominator = WordT { 1 } << ( carry_chain_length_lambda_i - 1 );
					const WordT pi_value = ( condition_bit == WordT { 1 } ) ? sum_value : ( denominator - sum_value );
					if ( pi_value == WordT {} )
						return std::numeric_limits<double>::infinity();

					// Eq.(3) 中对 i∈I 的那一部分：
					//   − Σ_{i∈I} log₂(ϕᵢ)
					// = Σ_{i∈I}(λᵢ−1) − Σ_{i∈I}log₂(πᵢ)
					sum_log2_pi += std::log2( static_cast<long double>( pi_value ) );
				}

				// 算法：
				// weightᵃ(u,v)
				//   = HW(((u ⊕ v) << 1))
				//     + Σ_{i∈I}(λᵢ − 1)
				//     − Σ_{i∈I} log₂(πᵢ)
				const long double exact_weight =
					static_cast<long double>( hamming_weight_of_u_xor_v_shifted_one + sum_chain_lengths_minus_one ) - sum_log2_pi;
				return static_cast<double>( exact_weight );
			}

			/**
			 * @brief Word-generic BvWeight^κ helper.
			 *
			 * 这对应 Azimi Algorithm 1 / apxlog2^κ 的工程化版本：
			 * - 公式不变
			 * - 只是 `WordT` 变成编译期参数
			 *
			 * 其中回传型别仍保留 `uint32_t`，因为本工程的 Qκ fixed-point
			 * 输出本来就是小整数编码，而不是 exact count 那种会长到 128-bit 的量。
			 */
			template<typename WordT>
			[[nodiscard]] inline std::uint32_t diff_addconst_bvweight_fixed_point_n_impl(
				WordT delta_x,
				WordT constant,
				WordT delta_y,
				int n,
				int fraction_bit_count ) noexcept
			{
				using namespace bitvector;
				static_assert( is_supported_diff_addconst_word_v<WordT>, "Unsupported word size for diff_addconst exact APIs." );

				if ( n <= 0 )
					return 0u;
				if ( fraction_bit_count < 0 || fraction_bit_count > 24 )
					return 0xFFFFFFFFu;
				if ( !is_diff_addconst_possible_n_impl<WordT>( delta_x, constant, delta_y, n ) )
					return 0xFFFFFFFFu;

				const WordT mask = addconst_low_bit_mask_generic<WordT>( n );
				const WordT input_difference = delta_x & mask;   // u
				const WordT output_difference = delta_y & mask;  // v
				const WordT additive_constant = constant & mask; // a

				// Algorithm 1 / Lemma 5 的公共骨架：
				const WordT s000 = ( ~( input_difference << 1 ) & ~( output_difference << 1 ) ) & mask;
				const WordT s000_prime = s000 & ~LeadingZeros_n( ( ~s000 ) & mask, n );
				// int-part 的前两项：
				//   HW(((u ⊕ v) << 1)) + HW(s'₀₀₀)
				const std::uint32_t hamming_weight_of_u_xor_v_shifted_one =
					HammingWeight( ( ( input_difference ^ output_difference ) << 1 ) & mask );
				const std::uint32_t sum_chain_lengths_minus_one = HammingWeight( s000_prime );

				std::uint32_t sum_floor_log2_pi = 0;
				std::uint64_t sum_fraction_bits_pi = 0;
				WordT remaining_chain_mask = s000_prime;
				constexpr int word_bits = int( sizeof( WordT ) * 8 );
				while ( remaining_chain_mask != WordT {} )
				{
					const int run_most_significant_bit_index =
						( word_bits - 1 ) - static_cast<int>( CountLeftZeros( remaining_chain_mask ) );
					int run_least_significant_bit_index = run_most_significant_bit_index;
					while ( run_least_significant_bit_index > 0 &&
							( ( remaining_chain_mask >> ( run_least_significant_bit_index - 1 ) ) & WordT { 1 } ) == WordT { 1 } )
					{
						--run_least_significant_bit_index;
					}

					const int run_length = run_most_significant_bit_index - run_least_significant_bit_index + 1;
					const int state_index_i = run_most_significant_bit_index + 1;
					const int carry_chain_length_lambda_i = run_length + 1;
					const WordT run_mask =
						( run_length >= word_bits )
							? ~WordT {}
							: ( ( ( WordT { 1 } << run_length ) - WordT { 1 } ) << run_least_significant_bit_index );
					remaining_chain_mask &= ~run_mask;

					if ( state_index_i <= 0 || state_index_i >= n )
						continue;

					// 仍然逐链恢复 πᵢ（与精确 `log₂(πᵢ)` 版同一条 Lemma 4）
					WordT constant_window_value = WordT {};
					for ( int bit_offset = 0; bit_offset <= carry_chain_length_lambda_i - 2; ++bit_offset )
					{
						const int bit_index = ( state_index_i - carry_chain_length_lambda_i ) + bit_offset;
						const WordT bit =
							( bit_index >= 0 && bit_index < n ) ? ( ( additive_constant >> bit_index ) & WordT { 1 } ) : WordT {};
						constant_window_value |= bit << bit_offset;
					}
					const int extra_bit_index = state_index_i - carry_chain_length_lambda_i - 1;
					const WordT extra_bit =
						( extra_bit_index >= 0 && extra_bit_index < n ) ? ( ( additive_constant >> extra_bit_index ) & WordT { 1 } ) : WordT {};
					const WordT sum_value = constant_window_value + extra_bit;

					const WordT condition_bit =
						( ( ( input_difference >> state_index_i ) ^ ( output_difference >> state_index_i ) ^ ( additive_constant >> ( state_index_i - 1 ) ) ) & WordT { 1 } );
					const WordT denominator = WordT { 1 } << ( carry_chain_length_lambda_i - 1 );
					const WordT pi_value = ( condition_bit == WordT { 1 } ) ? sum_value : ( denominator - sum_value );
					if ( pi_value == WordT {} )
						return 0xFFFFFFFFu;

					// floor(log₂(πᵢ)) = πᵢ 的最高位索引
					const int floor_log2_pi =
						( word_bits - 1 ) - static_cast<int>( CountLeftZeros( pi_value ) );
					sum_floor_log2_pi += static_cast<std::uint32_t>( floor_log2_pi );

					std::uint32_t fraction_bits = 0;
					for ( int k = 0; k < fraction_bit_count; ++k )
					{
						// κ-bit fraction:
						// 取 πᵢ 的 MSB 右侧 κ 个 bits，不足补 0
						const int bit_index = floor_log2_pi - 1 - k;
						const WordT bit = ( bit_index >= 0 ) ? ( ( pi_value >> bit_index ) & WordT { 1 } ) : WordT {};
						fraction_bits = ( fraction_bits << 1 ) | static_cast<std::uint32_t>( bit );
					}
					sum_fraction_bits_pi += static_cast<std::uint64_t>( fraction_bits );
				}

				// 因而：
				//   BvWeight^κ
				//   = ( HW(((u ⊕ v)<<1)) + HW(s'₀₀₀) − Σ floor(log₂(πᵢ)) ) · 2^κ
				//     − Σ frac_κ(πᵢ)
				const std::uint32_t int_part =
					( hamming_weight_of_u_xor_v_shifted_one + sum_chain_lengths_minus_one ) - sum_floor_log2_pi;
				const std::uint64_t scaled_int_part = ( static_cast<std::uint64_t>( int_part ) << fraction_bit_count );
				if ( scaled_int_part < sum_fraction_bits_pi )
					return 0u;
				const std::uint64_t result = scaled_int_part - sum_fraction_bits_pi;
				return ( result > 0xFFFFFFFFull ) ? 0xFFFFFFFFu : static_cast<std::uint32_t>( result );
			}

		}  // namespace detail

		/**
		 * @brief Exact feasibility check for XOR-differential of (x ⊞ constant) in n bits.
		 *
		 * This checks whether there exists x such that:
		 *   (x ⊞ a) ⊕ ((x ⊕ Δx) ⊞ a) == Δy   (mod 2^n)
		 *
		 * 工程化说明（对照 Azimi 的「valid/invalid」位向量模型）：
		 * - 论文使用位向量约束去描述 differential validity（避免状态 001 等不合法情形）
		 *   以便直接丢给 SMT solver。
		 * - 在 C++ 自动化搜寻里，我们把 Machado 的 4-state carry DP 写成
		 *   「8 种局部 bit-pattern -> 4-state reachable-mask 转移」。
		 *   状态仍是两条加法的 carry（c, c'），但每一位不再枚举 x_i，而是直接查表更新。
		 * - 这个检查是 **精确的可行性判定**（existential），不计算概率/权重，只用於：
		 *   1) 早期剪枝（impossible differential 直接淘汰）
		 *   2) 保护後续 πᵢ 计算避免出现 0/未定义情况
		 *
		 * 等价关系（便於理解）：
		 * - 若 `diff_addconst_exact_count_n(...) == 0`，则必然不可行。
		 * - 本函式相当於「把 count 的 DP 递推改成 bool OR」，因此更快、也更适合用作 guard。
		 */
		inline bool is_diff_addconst_possible_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::is_diff_addconst_possible_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 精确 DP（count/2^n）: 计算常量加法 XOR 差分的解数（存在多少 x 使得差分成立）
		 *
		 * 定义：
		 *   y  = x ⊞ a   (mod 2^n)
		 *   y' = (x ⊕ u) ⊞ a
		 *   v  = y ⊕ y'
		 *
		 * 则 DP = Pr_x[ v ] = count(u,a,v) / 2^n，其中 count 是满足条件的 x 个数。
		 *
		 * 参考/出处：
		 * - Machado, "Differential probability of modular addition with a constant operand",
		 *   IACR ePrint 2001/052.
		 * - Azimi et al., "A bit-vector differential model for the modular addition by a constant"
		 *   (DCC 2022 / ASIACRYPT 2020 extended), Theorem 2 亦给出等价的逐位递推（以 δ_i, ϕ_i 表示）。
		 *
		 * Theorem 2（Azimi）如何对上本实作？
		 * - Theorem 2 用 (δ_i, ϕ_i) 表示每一位对 DP 的乘法因子/递推。
		 * - 在工程实作里，我们改用更直接的 carry-pair 状态：
		 *     state_i = (c_i, c'_i) ∈ {0,1}^2
		 *   其中 c_i / c'_i 分别是计算 (x ⊞ a) 与 ((x⊕u) ⊞ a) 在 bit i 的 carry-in。
		 * - 这两种表述是等价的：δ_i/ϕ_i 本质上在描述「哪些 carry 转移允许」以及「允许转移的数量」，
		 *   而 carry-pair DP 直接把它展开成 4-state 的有限状态自动机累加计数。
		 *
		 * 工程化实作（本函式）：
		 * - 把 Machado 的逐位递推写成 4×4 整数 transfer matrix：
		 *     T_i[s,t] = #{ x_i ∈ {0,1} | 从 carry-pair state s 走到 t，且 v_i 成立 }
		 * - 因为 T_i 只依赖局部三元组 (u_i, a_i, v_i) ∈ {0,1}^3，所以一共只有 8 种 pattern。
		 * - 整体精确计数就是：
		 *     count(u,a,v) = e_(00) · (Π_i T_{u_i,a_i,v_i}) · 1
		 *   也就是说，count 只和这串 bit-pattern word 有关，不再需要显式枚举 x。
		 * - 连续相同 pattern 的区段可进一步压成 T_pattern^ℓ，一次吞掉整个 run。
		 * - 这条 exact-count / exact-DP 路径完全独立於下方两类 weight 接口：
		 *   1) `diff_addconst_weight_log2pi_n`：Azimi Lemma 3/4/5 的闭式精确 weight
		 *   2) `diff_addconst_bvweight_fixed_point_n`：Qκ fixed-point 近似 weight
		 *   这里只做 Machado carry recursion 的精确重写，不把任何 weight 公式当作 count 的实现来源。
		 */
		inline std::uint64_t diff_addconst_exact_count_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_count_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 精确 DP（double）
		 */
		inline double diff_addconst_exact_probability_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_probability_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 精确 differential weight（double）：w = -log2(DP)
		 */
		inline double diff_addconst_exact_weight_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_weight_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 精确 differential weight（整数，上取整）：w_int = ceil(-log2(DP))
		 *
		 * 这个版本是给「只吃整数权重」的 branch-and-bound / trail search 用的。
		 *
		 * 核心等式（避免浮点；也避免「double 很难精确表示 log2(count)」的工程坑）：
		 *   DP = count / 2^n
		 *   w  = -log2(DP) = n - log2(count)
		 *   ceil(w) = n - floor(log2(count))                 (count > 0)
		 *
		 * 小证明（为什麽上式成立）：
		 * - 令 count = 2^k * t，其中 k = floor(log2(count))，且 t ∈ [1,2)。
		 * - 则 w = n - log2(count) = n - (k + log2(t))，其中 log2(t) ∈ [0,1)。
		 * - 因此 ceil(w) = n - k。
		 *
		 * 工程含义：
		 * - 这个整数权重是「精确 weight 的上界（上取整）」；
		 *   用於 BnB/trail search 的剪枝时不会低估（安全）。
		 *
		 * 出处/思路：
		 * - 计数 count 本身由 `diff_addconst_exact_count_n` 的 carry-pair 逐位 DP 得到，
		 *   可对照 Machado ePrint 2001/052（亦等价於 Azimi DCC 2022 Theorem 2 的逐位递推）。
		 *
		 * @return -1 表示不可能（count=0），否则回传 ceil(weight) 的整数值。
		 */
		inline SearchWeight diff_addconst_exact_weight_ceil_int_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_weight_ceil_int_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 精确 differential weight（32-bit convenience wrapper）
		 *
		 * 回传 `ceil(exact_weight)`；不可能时回传 `-1`。
		 */
		inline SearchWeight diff_addconst_exact_weight_ceil_int( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_ceil_int_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 64-bit exact feasibility check for XOR-differential of (x ⊞ constant).
		 *
		 * This is the 64-bit sibling of the 32-bit exact guard above and is used by
		 * the 64-bit Q2 exact verifier path.
		 */
		inline bool is_diff_addconst_possible_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::is_diff_addconst_possible_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief 64-bit exact count for XOR-differential of (x ⊞ constant).
		 *
		 * Returns a 128-bit exact count because for n=64 the maximum count can reach 2^64.
		 */
		inline uint128_t diff_addconst_exact_count_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_count_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		inline double diff_addconst_exact_probability_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_probability_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		inline double diff_addconst_exact_weight_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_weight_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		inline SearchWeight diff_addconst_exact_weight_ceil_int_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_exact_weight_ceil_int_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		inline uint128_t diff_addconst_exact_count( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_exact_count_n( delta_x, constant, delta_y, 64 );
		}

		inline double diff_addconst_exact_probability( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_exact_probability_n( delta_x, constant, delta_y, 64 );
		}

		inline double diff_addconst_exact_weight( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_n( delta_x, constant, delta_y, 64 );
		}

		inline SearchWeight diff_addconst_exact_weight_ceil_int( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_ceil_int_n( delta_x, constant, delta_y, 64 );
		}

		/**
		 * @brief 精确 differential weight（不做 Qκ 截断）：用论文 Lemma 3/4/5 的闭式计算 Σ log2(pi)
		 *
		 * 参考：
		 * - Azimi et al., DCC 2022 / ASIACRYPT 2020 extended，Lemma 4（ϕ_i = p_i/2^{λ_i-1}）与
		 *   Eq.(3)(5)（weight = HW((u⊕v)<<1) + Σ(λ_i-1) - Σ log2(p_i)）。
		 *
		 * 注意：
		 * - 本函式回传的是实数（double）权重；与 `diff_addconst_exact_weight_n` 理论上应一致（仅有浮点误差）。
		 */
		inline double diff_addconst_weight_log2pi_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_weight_log2pi_n_impl<std::uint32_t>( delta_x, constant, delta_y, n );
		}

		inline double diff_addconst_weight_log2pi_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return detail::diff_addconst_weight_log2pi_n_impl<std::uint64_t>( delta_x, constant, delta_y, n );
		}

		/**
		 * @brief Algorithm 1 (generalized): BvWeight^κ - 计算常量加法差分近似权重（Qκ fixed-point）
		 *
		 * 参考：
		 * - Azimi et al., DCC 2022 / ASIACRYPT 2020 extended
		 *   - Algorithm 1: BvWeight（κ=4）
		 *   - Section 3.3: apxlog2^κ 的一般化（κ 可调）
		 *
		 * 论文对照（κ=4，论文预设）：
		 * - Lemma 8：在足够的 bit-width（避免溢位）下，
		 *     BvWeight(u,v,a) = 2^4 * apxweight^a(u,v)
		 *   其中 apxweight^a(u,v) 使用 Eq.(4) 的 apxlog2 / Truncate 近似。
		 * - Theorem 4：近似误差
		 *     E = weight^a(u,v) - apxweight^a(u,v) = weight^a(u,v) - 2^{-4}*BvWeight(u,v,a)
		 *   有界：
		 *     -0.029*(n-1) ≤ E ≤ 0
		 *   因此 apxweight^a(u,v) 是 **精确 weight^a(u,v) 的上界**（通常略偏大）。
		 *
		 * 工程使用提醒（很重要）：
		 * - 由於 BvWeight/ apxweight 是 **上界**（可能高估 weight），
		 *   在「寻找最小 weight」的 BnB/搜寻中，不能把它当作安全的下界来做剪枝，
		 *   否则可能错误剪掉实际更优的分支。
		 * - 它更适合用於：
		 *   1) SMT/bit-vector 编码（避免浮点）
		 *   2) 相似性/近似筛选（搭配阈值）
		 *   3) heuristic ordering（排序/启发式），而非作为最终精确权重
		 *
		 * 本工程的实作策略：
		 * - 论文 Algorithm 1 以 ParallelLog/ParallelTrunc 等 primitives 构造 bit-vector expression。
		 * - 本档为了可审计/可单测，改用「逐链」计算 π_i + 直接抽取 MSB 右侧 κ bits 的方式，
		 *   但其数学意义仍然是 Eq.(4) / Lemma 4/5/7/8 所定义的同一个近似量。
		 *
		 * 注意：Theorem 4 的常数界是针对 κ=4 的 apxlog2；若 κ != 4，误差界会改变。
		 *
		 * @param fraction_bit_count κ（小数位精度）。κ=4 对应论文/旧版 `diff_addconst_bvweight_q4_n`。
		 * @return BvWeight^κ(u,v,a)（低 κ bits 为小数位），不可能则回传 0xFFFFFFFF。
		 */
		inline std::uint32_t diff_addconst_bvweight_fixed_point_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n, int fraction_bit_count ) noexcept
		{
			return detail::diff_addconst_bvweight_fixed_point_n_impl<std::uint32_t>(
				delta_x,
				constant,
				delta_y,
				n,
				fraction_bit_count );
		}

		inline std::uint32_t diff_addconst_bvweight_fixed_point_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n, int fraction_bit_count ) noexcept
		{
			return detail::diff_addconst_bvweight_fixed_point_n_impl<std::uint64_t>(
				delta_x,
				constant,
				delta_y,
				n,
				fraction_bit_count );
		}

		/**
		 * @brief Algorithm 1: BvWeight - 计算常量加法差分权重（Q4 fixed-point output）
		 *
		 * 参考：Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
		 * Algorithm 1 (BvWeight)。
		 *
		 * 输入/输出对应：
		 * - delta_x = u：输入 XOR 差分（Δx）
		 * - delta_y = v：输出 XOR 差分（Δy），其中 y = x ⊞ a (mod 2^n)
		 * - constant = a：常量加数
		 *
		 * 近似量的定义（论文 Section 3.2, Eq.(3)(4) 与 Algorithm 1）：
		 * - 精确差分权重：weight^a(u,v) = -log2(Pr[u -> v])，一般为无理数
		 * - 论文用 4 个 fraction bits 近似 log2(pi)：
		 *     apxlog2(pi) = floor(log2(pi)) + Truncate(pi[m-1,0]) / 16
		 *   其中 Truncate 取的是「MSB 後的 4 个 bit」（Eq.(4)）
		 * - BvWeight(u,v,a) 以 Q4 fixed-point 表示 apxweight^a(u,v)：
		 *     BvWeight = 16 * apxweight^a(u,v)
		 *
		 * 论文误差界（κ=4）：
		 * - Theorem 4：E = weight^a(u,v) - apxweight^a(u,v) 满足 -0.029*(n-1) ≤ E ≤ 0，
		 *   因此 apxweight（也就是 BvWeight/16）是 **精确 weight 的上界**（略偏大）。
		 *   这也是你在做「相似性判定」时需要用阈值的原因：它不是精确值。
		 *
		 * 工程化实作策略：
		 * - 论文的 Algorithm 1 目标是「可用 bit-vector primitives 表达」；
		 *   本工程在固定 32-bit 搜寻时，直接按照 Lemma 4/5 的定义逐链计算 πᵢ，

		 * - 我们仍然输出同一个近似量（BvWeight），因此可用於「相似性/近似剪枝」：
		 *   若要与精确权重比对，请先用枚举/DP 算精确 Pr，再看 |apxweight - exact_weight| 是否小於阈值。
		 *
		 * @return BvWeight(u,v,a) in Q4 (low 4 bits are fraction), or 0xFFFFFFFF for impossible.
		 */
		inline std::uint32_t diff_addconst_bvweight_q4_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			// κ=4 的便捷封装：等价於 Azimi Algorithm 1 的原始设定（Q4 fixed-point）。
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, n, 4 );
		}

		inline std::uint32_t diff_addconst_bvweight_q4_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, n, 4 );
		}

		/**
		 * @brief Convenience wrapper returning Q4 bvweight.
		 *
		 * 32/64 两个 overload 都只是把默认位宽设成整字宽：
		 * - `uint32_t` -> n=32
		 * - `uint64_t` -> n=64
		 */
		inline std::uint32_t diff_addconst_bvweight_q4( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
		}

		inline std::uint32_t diff_addconst_bvweight_q4( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 64 );
		}

		/**
		 * @brief Convenience wrapper: exact count.
		 */
		inline std::uint64_t diff_addconst_exact_count( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_count_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief Convenience wrapper: exact probability.
		 */
		inline double diff_addconst_exact_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_probability_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief Convenience wrapper: exact weight.
		 */
		inline double diff_addconst_exact_weight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief Convenience wrapper: exact weight from Lemma 3/4/5 (log2(pi)).
		 */
		inline double diff_addconst_weight_log2pi( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_weight_log2pi_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_addconst_weight_log2pi( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_addconst_weight_log2pi_n( delta_x, constant, delta_y, 64 );
		}

		/**
		 * @brief Convenience wrapper returning Qκ bvweight.
		 */
		inline std::uint32_t diff_addconst_bvweight_fixed_point( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int fraction_bit_count ) noexcept
		{
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, 32, fraction_bit_count );
		}

		inline std::uint32_t diff_addconst_bvweight_fixed_point( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int fraction_bit_count ) noexcept
		{
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, 64, fraction_bit_count );
		}

		/**
		 * @brief 对照/演示：把 Azimi Algorithm 1 的 Q4 BvWeight 转成整数（ceil）
		 *
		 * - 本函式保留给「对照实验」或「近似剪枝」用途（例如：你想检验 BVWeight 与精确 weight 的相似性）。
		 * - 不建议在你目前的 differential trail 搜寻里当作最终权重（它是近似的）。
		 *
		 * 数学含义：
		 * - Q4 的 BvWeight 代表：BvWeight ≈ 16 * apxweight
		 * - 这里做 ceil(BvWeight / 16) 只是把 Q4 近似权重转成整数（仍然是近似，不保证等於 ceil(exact_weight)）。
		 *
		 * Ref:
		 * - Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
		 *   (ASIACRYPT 2020 / DCC 2022 extended), Algorithm 1 (BvWeight, κ=4).
		 */
		inline SearchWeight diff_addconst_bvweight_q4_int_ceil( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return INFINITE_WEIGHT;
			if ( bvweight_q4 == 0u )
				return SearchWeight( 0 );
			// Convert Q4 -> integer weight (round up).
			return static_cast<SearchWeight>( ( bvweight_q4 + 15u ) >> 4 );
		}

		inline SearchWeight diff_addconst_bvweight_q4_int_ceil( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 64 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return INFINITE_WEIGHT;
			return static_cast<SearchWeight>( ( bvweight_q4 + 15u ) >> 4 );
		}

		// 使用公共 math_util.hpp 的 neg_mod_2n

		/**
		 * @brief 精确 subtraction-by-constant weight（32-bit convenience wrapper）
		 *
		 * 透过 `x ⊟ c == x ⊞ (-c mod 2^n)` 转换，回传 `ceil(exact_weight)`；
		 * 不可能时回传 `-1`。
		 */
		inline SearchWeight diff_subconst_exact_weight_ceil_int( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<uint32_t>( constant, 32 );
			return diff_addconst_exact_weight_ceil_int( delta_x, neg_constant, delta_y );
		}

		/**
		 * @brief 精确 DP: subtraction-by-constant（透过 x ⊟ c == x ⊞ (-c mod 2^n) 转换）
		 */
		inline std::uint64_t diff_subconst_exact_count_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_count_n( delta_x, neg_constant, delta_y, n );
		}

		inline uint128_t diff_subconst_exact_count_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, n );
			return diff_addconst_exact_count_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_probability_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_probability_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_probability_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, n );
			return diff_addconst_exact_probability_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_weight_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_weight_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_weight_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, n );
			return diff_addconst_exact_weight_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_weight_log2pi_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_weight_log2pi_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_weight_log2pi_n( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y, int n ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, n );
			return diff_addconst_weight_log2pi_n( delta_x, neg_constant, delta_y, n );
		}

		inline std::uint64_t diff_subconst_exact_count( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_count_n( delta_x, constant, delta_y, 32 );
		}

		inline uint128_t diff_subconst_exact_count( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_subconst_exact_count_n( delta_x, constant, delta_y, 64 );
		}

		inline double diff_subconst_exact_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_probability_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_exact_probability( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_subconst_exact_probability_n( delta_x, constant, delta_y, 64 );
		}

		inline double diff_subconst_exact_weight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_weight_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_exact_weight( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_subconst_exact_weight_n( delta_x, constant, delta_y, 64 );
		}

		inline double diff_subconst_weight_log2pi( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_weight_log2pi_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_weight_log2pi( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			return diff_subconst_weight_log2pi_n( delta_x, constant, delta_y, 64 );
		}

		/**
		 * @brief 计算常量加法差分概率
		 * 
		 * @param delta_x 输入差分
		 * @param constant 常量K
		 * @param delta_y 输出差分
		 * @return 近似概率
		 */
		inline double diff_addconst_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			// 工程约定：
			// - 这里回传的是「由 BvWeight 近似权重换算」得到的近似 DP
			// - 如果你要精确 DP/weight，请用：
			//     diff_addconst_exact_probability(_n) / diff_addconst_exact_weight(_n)
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return 0.0;
			return std::pow( 2.0, -static_cast<double>( bvweight_q4 ) / 16.0 );
		}

		inline double diff_addconst_probability( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 64 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return 0.0;
			return std::pow( 2.0, -static_cast<double>( bvweight_q4 ) / 16.0 );
		}

		/**
		 * @brief 计算常量减法差分概率
		 * 
		 * @param delta_x 输入差分
		 * @param constant 常量K
		 * @param delta_y 输出差分
		 * @return 近似概率
		 */
		inline double diff_subconst_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			// X - C = X + (-C) (mod 2^n)
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<uint32_t>( constant, 32 );
			return diff_addconst_probability( delta_x, neg_constant, delta_y );
		}

		inline double diff_subconst_probability( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, 64 );
			return diff_addconst_probability( delta_x, neg_constant, delta_y );
		}

		inline SearchWeight diff_subconst_exact_weight_ceil_int( std::uint64_t delta_x, std::uint64_t constant, std::uint64_t delta_y ) noexcept
		{
			const std::uint64_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint64_t>( constant, 64 );
			return diff_addconst_exact_weight_ceil_int( delta_x, neg_constant, delta_y );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
