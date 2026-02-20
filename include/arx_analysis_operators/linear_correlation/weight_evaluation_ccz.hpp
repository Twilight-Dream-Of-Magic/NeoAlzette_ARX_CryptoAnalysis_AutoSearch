#pragma once

/**
 * @file linear_correlation/weight_evaluation_ccz.hpp
 * @brief 模加法 (mod 2^n) 的 CCZ 等价构造 + 线性分析（Walsh/相关系数）显式算子
 *
 * 参考论文：
 * - Ernst Schulte-Geers, "On CCZ-equivalence of Addition mod 2^n"
 *   (下文注释中的公式/符号均按该文的 Definition / Theorem 编号逐条对应)
 *
 * -----------------------------------------------------------------------------
 * 与差分侧的对位（固定输入掩码 → 最优输出掩码）
 * -----------------------------------------------------------------------------
 * **方向对齐**（便於与 Lipmaa–Moriai 差分对照）：
 * - 差分模加：`differential_optimal_gamma.hpp` 中固定输入 (α,β) 构造使 DP⁺ 最大的输出 γ*。
 * - 线性模加（本论文 φ₂）：固定输入掩码 (v,w)，求使 |φ₂| 最大的输出掩码 u*。
 *   → **`find_optimal_output_u_ccz(v,w,n)`**：显式构造（下文引理 + Theorem 7），不对 u 做全枚举。
 *
 * **常见混淆**：
 * 1) `linear_correlation_add_logn.hpp`（Wallén）的 Θ(log n) 是 **已知 (u,v,w)** 时计算单个相关值，不是 argmax u。
 * 2) **Theorem 6** 处理的是固定 **u** 时对 (v,w) 的**行**最优，与「固定 (v,w) 求 u*」方向相反。
 *
 * **复杂度**：LM-2001 对 γ* 给出 Θ(log n) 叙述。本档中 **`MnT_of`**、**`aL_of`**（连续 1 长度奇偶 + 後缀 min）、**`m_of`**（规则 (1)(2)）在 **n≤64** 固定槽下皆 **Θ(log n) 深度**。**`find_optimal_output_u_ccz`** 主路径同阶 **Θ(log n)**（并行前缀/後缀与 zero-run 闭包）。现行 fast path 与 Θ(n) 逐位扫描之教科书式构造可逐枚举对照，以验证 bitwise 等价；**`m_of`** 之 rule (2) 亦可并入与 rule (1) 相同之 zero-run closure，简化结构与常数。在 bounded-fan-in 电路深度模型下，对角输入族 `(x,x)` 已承载 `OR_n` / `msb(x)` 类子问题，故 exact 路径有 **Ω(log n)** 下界，与上界同阶即该模型下**阶数最优**。**模减**线性（var-const 等）见同目录 `linear_correlation_addconst.hpp`，
 * 与此处 var-var 模加 φ₂ 接口不同。
 *
 * -----------------------------------------------------------------------------
 * 位序约定（非常关键，和论文的向量写法要对齐）
 * -----------------------------------------------------------------------------
 * 论文把向量写成 x = (x0, x1, ..., x_{n-1}) ∈ F_2^n。
 *
 * 本工程用 uint64_t 存储 n-bit 向量，约定：
 * - bit 0  表示 x0  (LSB)
 * - bit i  表示 xi
 * - bit n-1 表示 x_{n-1} (MSB)
 *
 * 因此，论文中的线性移位算子：
 * - L : (x0,...,x_{n-1}) ↦ (x1,...,x_{n-1},0)   —— 在 uint64_t 上等价于右移 1 位
 * - R : (x0,...,x_{n-1}) ↦ (0,x0,...,x_{n-2})   —— 在 uint64_t 上等价于左移 1 位（并 mask 掉溢出）
 *
 * 该约定与用户给出的 demo 代码一致，也与本仓库其它 ARX 头文件的 bit0=LSB 约定一致。
 */

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "arx_analysis_operators/DefineSearchWeight.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

		// ============================================================================
		// 基本位运算工具（mask / popcount / parity / 偏序 ）
		// ============================================================================

		/// @brief n-bit 掩码：低 n 位为 1（n ∈ [0,64]）
		[[nodiscard]] inline constexpr std::uint64_t mask_n( int n ) noexcept
		{
			if ( n <= 0 )
				return 0ull;
			if ( n >= 64 )
				return ~0ull;
			return ( 1ull << static_cast<unsigned>( n ) ) - 1ull;
		}

		/// @brief Hamming weight |x|
		[[nodiscard]] inline unsigned popcount_u64( std::uint64_t x ) noexcept
		{
			return static_cast<unsigned>( std::popcount( x ) );
		}

		/// @brief parity(x) = |x| mod 2
		[[nodiscard]] inline unsigned parity_u64( std::uint64_t x ) noexcept
		{
			return popcount_u64( x ) & 1u;
		}

		/**
		 * @brief 论文中的偏序 "x  z" ：逐位比较 x_i <= z_i
		 *
		 * 等价实现：x 在 z 的 0 位上不能为 1，即 (x & ~z) == 0
		 */
		[[nodiscard]] inline bool leq_bitwise( std::uint64_t x, std::uint64_t z, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			x &= MASK;
			z &= MASK;
			return ( x & ~z ) == 0ull;
		}

		/// @brief 内积 (u·v) mod 2 = parity( u & v )
		[[nodiscard]] inline unsigned dot_parity( std::uint64_t u, std::uint64_t v, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			return parity_u64( ( u & v ) & MASK );
		}

		// ============================================================================
		// 论文 2. Notation: L, R, M, M^t
		// ============================================================================

		/**
		 * @brief 论文的 L : (x0,...,x_{n-1}) ↦ (x1,...,x_{n-1},0)
		 *
		 * bit0=LSB 约定下：L(x) == (x >> 1)
		 */
		[[nodiscard]] inline std::uint64_t L_of( std::uint64_t x, int n ) noexcept
		{
			( void )n;
			return ( x >> 1 );
		}

		/**
		 * @brief 论文的 R : (x0,...,x_{n-1}) ↦ (0,x0,...,x_{n-2})
		 *
		 * bit0=LSB 约定下：R(x) == ((x << 1) & mask_n(n))
		 */
		[[nodiscard]] inline std::uint64_t R_of( std::uint64_t x, int n ) noexcept
		{
			return ( x << 1 ) & mask_n( n );
		}

		/**
		 * @brief 论文的 (I ⊕ R)(x) = x ⊕ R(x)
		 *
		 * Theorem 3 (Lipmaa–Moriai) 的可行性条件会直接用到它。
		 */
		[[nodiscard]] inline std::uint64_t I_xor_R_of( std::uint64_t x, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			return ( x ^ R_of( x, n ) ) & MASK;
		}

		/**
		 * @brief prefix XOR（工程化工具）：p_i = x0 ⊕ x1 ⊕ ... ⊕ xi
		 *
		 * 说明：这里是 bit-sliced 的 xor-scan（对 XOR 来说就是前缀和）。
		 * 对 n<=64 的 uint64_t 足够。
		 */
		[[nodiscard]] inline std::uint64_t prefix_xor_inclusive( std::uint64_t x, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			x &= MASK;
			x ^= ( x << 1 ) & MASK;
			x ^= ( x << 2 ) & MASK;
			x ^= ( x << 4 ) & MASK;
			x ^= ( x << 8 ) & MASK;
			x ^= ( x << 16 ) & MASK;
			x ^= ( x << 32 ) & MASK;
			return x & MASK;
		}

		/**
		 * @brief 论文中的 M : (x0,...,x_{n-1}) ↦ (0, x0, x0⊕x1, ..., x0⊕...⊕x_{n-2})
		 *
		 * 论文原文（Notation）：
		 *   M(x) = (0, x0, x0⊕x1, ..., x0⊕...⊕x_{n-2})
		 *
		 * 工程实现（和公式一一对应）：
		 * - 先算前缀 XOR: p_i = x0⊕...⊕xi
		 * - 再右移 1 个坐标（在 uint64_t 上是左移 1 位）：
		 *   M(x) = R(p) = (p << 1) & mask
		 */
		[[nodiscard]] inline std::uint64_t M_of( std::uint64_t x, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			const std::uint64_t p = prefix_xor_inclusive( x, n );
			return ( p << 1 ) & MASK;
		}

		/**
		 * @brief 论文中的 M^t（transpose）
		 *
		 * 论文 Remark after Theorem 4 给出的显式形式：
		 *   令 s := u ⊕ v ⊕ w，则 z = M^t(s) =
		 *     (s1⊕...⊕s_{n-1}, s2⊕...⊕s_{n-1}, ..., s_{n-1}, 0)
		 *
		 * 工程实现：
		 * - 从 MSB 往下做 suffix XOR
		 * - 令 z_{n-1} = 0，并为 i=1..n-1 设置 z_{i-1} = s_i ⊕ ... ⊕ s_{n-1}
		 *
		 * **并行实作（Θ(log n) 轮字运算）**：先算
		 *   Suffix_i = s_i ⊕ s_{i+1} ⊕ … ⊕ s_{n-1}（仅低 n 位），再 z = (Suffix >> 1) & mask。
		 * 与上式逐位扫描等价；与 `prefix_xor_inclusive`（M 的前缀 XOR）互为对偶。
		 */
		[[nodiscard]] inline std::uint64_t suffix_xor_down_n( std::uint64_t s, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			s &= MASK;
			for ( unsigned d = 1u; d < static_cast<unsigned>( n ); d <<= 1u )
				s ^= ( s >> d );
			return s & MASK;
		}

		[[nodiscard]] inline std::uint64_t MnT_of( std::uint64_t s, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			s &= MASK;
			if ( n <= 0 )
				return 0ull;
			const std::uint64_t suf = suffix_xor_down_n( s, n );
			return ( suf >> 1 ) & MASK;
		}

		// ============================================================================
		// Theorem 3: 差分分析算子（DEA / Differential Probability）—— 显式公式
		// ============================================================================

		/**
		 * @brief Theorem 3 (Lipmaa–Moriai): 模加法差分概率的显式公式
		 *
		 * 论文原文（Theorem 3）：
		 *   令 (X,Y) 在 F_2^n × F_2^n 上均匀分布，则
		 *
		 *   P( (X ⊞ Y) ⊕ ((X ⊕ α) ⊞ (Y ⊕ β)) = γ)
		 *     = 1{ (I⊕R)(a⊕b⊕d)  R((a⊕d) | (b⊕d)) } · 2^{ - | R((a⊕d) | (b⊕d)) | }
		 *
		 * 其中：
		 * - “⊞” 为 mod 2^n 加法（论文用 Addition mod 2^n）
		 * - “⊕” 为逐位 XOR
		 * - “|” 为逐位 OR
		 * - “R” 为右移算子（论文定义 R(x)=(0,x0,...,x_{n-2})；bit0=LSB 下对应 left-shift）
		 * - “” 为逐位偏序（x_i <= z_i）
		 * - |·| 为 Hamming weight
		 *
		 * 工程实现说明：
		 * - 按本文件的位序约定，R_of(x,n) == (x<<1)&mask_n(n)
		 * - (I⊕R)(x) == x ⊕ R_of(x,n)
		 *
		 * 返回值：
		 * - 若指示条件不满足，则概率为 0（返回 0.0）
		 * - 否则返回精确形态为 2^{-k} 的 double 值（k 为权重）
		 */
		[[nodiscard]] inline double differential_probability_add_ccz_value( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			alpha &= MASK;
			beta &= MASK;
			gamma &= MASK;

			// rhs := R((a⊕d) | (b⊕d))
			const std::uint64_t rhs = R_of( ( ( alpha ^ gamma ) | ( beta ^ gamma ) ) & MASK, n );

			// lhs := (I⊕R)(a⊕b⊕d)
			const std::uint64_t lhs = I_xor_R_of( ( alpha ^ beta ^ gamma ) & MASK, n );

			// feasibility indicator 1{ lhs  rhs }
			if ( !leq_bitwise( lhs, rhs, n ) )
				return 0.0;

			// probability magnitude: 2^{-|rhs|}
			const int k = static_cast<int>( popcount_u64( rhs ) );
			return std::ldexp( 1.0, -k );
		}

		/**
		 * @brief Theorem 3 的权重形式：返回 -log2(P)（若 P=0 则返回 std::nullopt）
		 *
		 * 按 Theorem 3：
		 * - 可行时 P = 2^{-k}，其中 k = | R((α⊕γ)|(β⊕γ)) |
		 */
		[[nodiscard]] inline std::optional<SearchWeight> differential_probability_add_ccz_weight( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			alpha &= MASK;
			beta &= MASK;
			gamma &= MASK;

			const std::uint64_t rhs = R_of( ( ( alpha ^ gamma ) | ( beta ^ gamma ) ) & MASK, n );
			const std::uint64_t lhs = I_xor_R_of( ( alpha ^ beta ^ gamma ) & MASK, n );
			if ( !leq_bitwise( lhs, rhs, n ) )
				return std::nullopt;

			return static_cast<SearchWeight>( popcount_u64( rhs ) );
		}

		/**
		 * @brief Corollary 1 / Theorem 3: 判断单个 DEA 是否有解（只看指示条件）
		 *
		 * 对应论文：
		 *   (I⊕R)(a⊕β⊕γ)  R((α⊕γ)|(β⊕γ))
		 */
		[[nodiscard]] inline bool differential_equation_add_ccz_solvable( std::uint64_t a, std::uint64_t b, std::uint64_t d, int n ) noexcept
		{
			return differential_probability_add_ccz_value( a, b, d, n ) != 0.0;
		}

		// ============================================================================
		// Theorem 4: 线性相关系数 φ2(u,v,w) —— “线性分析算子”
		// ============================================================================

		/**
		 * @brief Theorem 4 (Walsh transform of addition mod 2^n)
		 *
		 * 论文公式（逐字对应）：
		 *   z := M^t(u ⊕ v ⊕ w)
		 *   φ2(u,v,w) = 1{u⊕v  z} · 1{u⊕w  z} · (-1)^{(u⊕w)·(u⊕v)} · 2^{-|z|}
		 *
		 * 返回：
		 * - 若不可行（任一指示函数为 0），则返回 std::nullopt
		 * - 否则返回相关系数的 exact 值（形如 ± 2^{-k}）
		 */
		[[nodiscard]] inline std::optional<double> linear_correlation_add_ccz_value( std::uint64_t u, std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			u &= MASK;
			v &= MASK;
			w &= MASK;

			// z := M^t(u ⊕ v ⊕ w)
			const std::uint64_t z = MnT_of( u ^ v ^ w, n );
			const std::uint64_t uv = ( u ^ v ) & MASK;
			const std::uint64_t uw = ( u ^ w ) & MASK;

			// 1{u⊕v  z} 1{u⊕w  z}
			if ( !leq_bitwise( uv, z, n ) || !leq_bitwise( uw, z, n ) )
				return std::nullopt;

			// (-1)^{(u⊕w)·(u⊕v)}
			const unsigned sign = dot_parity( uw, uv, n );
			const int	   k = static_cast<int>( popcount_u64( z ) );  // |z|
			const double   mag = std::ldexp( 1.0, -k );				   // 2^{-|z|}
			return sign ? -mag : mag;
		}

		/**
		 * @brief Theorem 4 的“权重形式”：返回 -log2(|φ|) = |z|
		 *
		 * 若 φ2(u,v,w)=0（不可行），返回 std::nullopt。
		 */
		[[nodiscard]] inline std::optional<SearchWeight> linear_correlation_add_ccz_weight( std::uint64_t u, std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			u &= MASK;
			v &= MASK;
			w &= MASK;

			const std::uint64_t z = MnT_of( u ^ v ^ w, n );
			const std::uint64_t uv = ( u ^ v ) & MASK;
			const std::uint64_t uw = ( u ^ w ) & MASK;

			if ( !leq_bitwise( uv, z, n ) || !leq_bitwise( uw, z, n ) )
				return std::nullopt;

			return static_cast<SearchWeight>( popcount_u64( z ) );
		}

		// ============================================================================
		// Theorem 5/6: row maximum（给定 u，找使 |φ| 最大的 v,w 之一）
		// ============================================================================

		/**
		 * @brief Theorem 5: aL(u)（论文称其为 L-minimal vector）
		 *
		 * 论文定义（逐字对应）：
		 *   aL(u) := u ⊕ (u ⋆ L(u)) ⊕ ... ⊕ (u ⋆ L(u) ⋆ L^2(u) ⋆ ... ⋆ L^{n-1}(u))
		 *
		 * 其中 "⋆" 为逐位乘（在 F2 上等价于 bitwise AND）；bit0=LSB 下 $L^t(u)=u\gg t$。
		 *
		 * **闭式推导**：令 $T_k = u\wedge (u\gg1)\wedge\cdots\wedge(u\gg k)$。则
		 * $(T_k)_i=1$ 若且唯若 $u_i=\cdots=u_{i+k}=1$（在 $i+k<n$ 意义下）。故对固定 $i$，
		 * 使 $(T_k)_i=1$ 的 $k$ 恰为 $0,\ldots,L_i-1$，其中 $L_i$ 为从 $i$ 起连续 1 的长度；
		 * $aL_i=\bigoplus_k (T_k)_i = L_i \bmod 2$。若 $z(i)=\min\{j\ge i: u_j=0\}$（无则取 $n$），则
		 * $L_i=z(i)-i$，即 $aL_i=(z(i)-i)\bmod 2$（$u_i=0$ 时 $z(i)=i$，亦得 0）。
		 *
		 * **实作**：`val[j]=j` 若 $u_j=0$，否则 $n$；并行後缀 min 得 $z(i)$；再组字。**深度 Θ(log n)**（固定 64 槽，同 `m_of_rule2` 之 Hillis–Steele）。
		 */
		[[nodiscard]] inline std::uint64_t aL_of( std::uint64_t u, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			u &= MASK;
			const int nlim = std::min( n, 64 );

			int zpos[64];
			for ( int i = 0; i < 64; ++i )
			{
				if ( i < nlim )
					zpos[i] = ( ( u >> i ) & 1ull ) == 0ull ? i : n;
				else
					zpos[i] = n;
			}

			for ( unsigned sh = 1u; sh < 64u; sh <<= 1u )
			{
				if ( sh >= static_cast<unsigned>( nlim ) )
					break;
				for ( int i = 0; i < 64; ++i )
				{
					if ( i + static_cast<int>( sh ) < nlim )
						zpos[i] = std::min( zpos[i], zpos[i + static_cast<int>( sh )] );
				}
			}

			std::uint64_t acc = 0ull;
			for ( int i = 0; i < 64; ++i )
			{
				if ( i >= nlim )
					break;
				if ( ( ( zpos[i] - i ) & 1 ) != 0 )
					acc |= ( 1ull << i );
			}
			return acc & MASK;
		}

		/**
		 * @brief Theorem 6: b(u) := (I ⊕ L)(aL(u))
		 *
		 * bit0=LSB 下 L(x) == x>>1，因此：
		 *   b(u) = aL(u) ⊕ (aL(u) >> 1)
		 */
		[[nodiscard]] inline std::uint64_t row_best_b_of( std::uint64_t u, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			const std::uint64_t a = aL_of( u, n );
			return ( a ^ ( a >> 1 ) ) & MASK;
		}

		/**
		 * @brief Theorem 6: 行最大相关的指数 d(u) = |L(aL(u))|
		 *
		 * 论文写法：
		 *   φ2(u, b(u), u) = 2^{-d(u)}
		 *   d(u) := |L(aL(u))|
		 */
		[[nodiscard]] inline int row_best_d_of( std::uint64_t u, int n ) noexcept
		{
			const std::uint64_t a = aL_of( u, n );
			return static_cast<int>( popcount_u64( a >> 1 ) );	// |L(a)|, 其中 L==>>1
		}

		/// @brief Theorem 6: φ2(u, b(u), u) = 2^{-d(u)}（总为正）
		[[nodiscard]] inline double row_best_correlation_value( std::uint64_t u, int n ) noexcept
		{
			const int d = row_best_d_of( u, n );
			return std::ldexp( 1.0, -d );
		}

		/**
		 * @brief Theorem 6（Schulte–Geers 行最大）：固定输出掩码 u 於 s，取 (v,w) = (b(u), u) 使 |φ₂(u,v,w)| 最大。
		 *
		 * 论文已有构造；并非超越原界的全新上下界。与「固定 (v,w) 求列最大 u*」方向相反（後者见 `find_optimal_output_u_ccz`）。
		 *
		 * 在 Liu/Wang/Rijmen ACNS 2016 的 Schulte–Geers 显式式下，权重为命题 1 的 |z|；此处之 `z_weight` 等於
		 * 对固定 u 按 |z| 非降枚举时的**最小**可达权重（与搜寻框架中 `generate_add_candidates_for_fixed_u` 的首项一致）。
		 * 以 O(n) 位运算实作（`row_best_b_of` + `linear_correlation_add_ccz_weight`），无需对 (v,w) 做堆叠 DFS。
		 */
		struct Phi2RowMax
		{
			std::uint64_t v = 0;
			std::uint64_t w = 0;
			SearchWeight  z_weight = 0;
		};

		// Theorem 6（行最大）：固定 u 时可取 (v,w)=(b(u),u)；|z| 见 `linear_correlation_add_ccz_weight(u,b(u),u,n)`。

		[[nodiscard]] inline Phi2RowMax linear_correlation_add_phi2_row_max( std::uint64_t u, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			u &= MASK;
			const std::uint64_t v = row_best_b_of( u, n );
			const std::uint64_t w = u;
			const auto			wt = linear_correlation_add_ccz_weight( u, v, w, n );
			const SearchWeight zw = wt.has_value() ? wt.value() : static_cast<SearchWeight>( n );
			return Phi2RowMax { v, w, zw };
		}

		// ============================================================================
		// Theorem 7: column maximum（给定 (v,w)，找使 |φ2(u,v,w)| 最大的 u）
		// ============================================================================

		/**
		 * @brief Theorem 7 前的约束问题（论文 5.2, (∗)）
		 *
		 * 论文中令 s = u ⊕ v ⊕ w，并令 s = a ⊕ L(a)。
		 * 则要找“列 (v,w) 的最大相关”，可转化为找 a 使得 |L(a)| 最小，并满足：
		 *
		 *   (∗)  a ⊕ v  L(a)    且    a ⊕ w  L(a)
		 *
		 * 其中  为逐位偏序，L(a) 为左移算子（本工程 bit0=LSB 下 L(a)=a>>1）。
		 *
		 * Theorem 7 给出一个显式构造：
		 *   m(v,w) 由 R(v⊕w) 按论文文字规则“补齐”得到
		 *   a(v,w) := aL(v⋆w) | m(v,w)
		 *   u(v,w) := (v ⊕ w) ⊕ (I ⊕ L)(a(v,w))
		 *
		 * 这里的 “|” 表示逐位 OR（集合并），“⋆” 表示逐位乘（bitwise AND）。
		 *
		 * 注意：论文 Theorem 7 额外假设 v_{n-1}=w_{n-1}=1（MSB 位置为 1）。
		 */

		/**
		 * @brief 计算论文 Theorem 7 中的 m(v,w)
		 *
		 * 论文（5.2 Column maxima）描述：
		 * - 先从 R(v ⊕ w) 开始；
		 * - 然后做两类“强制修改”：
		 *   1) 若 v_i = w_i = 0 且 a_i = 1，则必须有 a_{i+1} = 1（因此在 0-run 内会向高位传播 1）
		 *   2) 若 (v|w) 存在一段形如 1 0^k 的区段，且该区段在 (v⋆w) 上对应为 0^{k+1}，
		 *      则 L(a) 在该区段必须为 1^{k+1}（等价于把 a 的对应高一位区段置 1）
		 *
		 * 说明（工程化落地）：
		 * - 在 1 0^k 区段中，0^k 部分意味着 v=w=0，因此 v⋆w 也必为 0；
		 * - 因此“对应为 0^{k+1}”的关键在于区段起点那一位：v⋆w=0（即不是 v=w=1）。
		 */

		/// @brief 低位连续掩码：位 `lo..hi`（含）全 1，再与 `word_mask` 相交（用于 Theorem 7 的 `m` 段 OR）
		[[nodiscard]] inline std::uint64_t mask_bit_range_inclusive(
			unsigned lo, unsigned hi, std::uint64_t word_mask ) noexcept
		{
			if ( lo > hi )
				return 0ull;
			std::uint64_t upper = ( hi >= 63u ) ? ~std::uint64_t { 0 } : ( ( 1ull << ( hi + 1u ) ) - 1ull );
			const std::uint64_t lower = ( lo == 0u ) ? 0ull : ( ( 1ull << lo ) - 1ull );
			return ( upper - lower ) & word_mask;
		}

		/**
		 * @brief Theorem 7 之 `m`：规则 (1) —— 在 $v_i=w_i=0$ 时，若 $a_i=1$ 则 $a_{i+1}=1$，低位向高位单趟闭包。
		 *
		 * `z_mask` 须为 $(v|w)$ 的补（仅低 n 位）：$z_i=1 \Leftrightarrow v_i=w_i=0$。
		 *
		 * **并行闭包（Θ(log n) 轮）**：令 `run` 表示连续 $z$ 的 AND 窗宽倍增；每轮
		 * `a |= ((a & run) << sh) & mask`，再 `run &= run >> sh`。与「`i=0..n-2` 顺序一次传递」逐位等价
		 *（在极长 $z=1$ 区间内，最低索引之 1 经 $\sum 2^k$ 步覆盖到右端）。
		 */
		[[nodiscard]] inline std::uint64_t propagate_ones_right_through_v_or_w_zero_mask(
			std::uint64_t a, std::uint64_t z_mask, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			a &= MASK;
			std::uint64_t run = z_mask & MASK;
			for ( unsigned sh = 1u; sh < static_cast<unsigned>( n ); sh <<= 1u )
			{
				a |= ( ( a & run ) << sh ) & MASK;
				run &= run >> sh;
			}
			return a & MASK;
		}

		// Rule (2) start positions:
		// S = { i : (v|w)_i = 1, (v|w)_{i+1} = 0, (v&w)_i = 0, i < n-1 }.
		[[nodiscard]] inline std::uint64_t m_of_rule2_start_mask(
			std::uint64_t t_or, std::uint64_t t_and, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			t_or &= MASK;
			t_and &= MASK;
			if ( n <= 1 )
				return 0ull;

			std::uint64_t s = ( t_or & ~( t_or >> 1 ) & ~t_and ) & MASK;
			s &= ~( 1ull << static_cast<unsigned>( n - 1 ) );
			return s & MASK;
		}

		/**
		 * @brief Theorem 7 之 `m`：规则 (2) 的 **OR 填充掩码**（不含 `R(v⊕w)`、不含规则 (1)）。
		 *
		 * 令 $S=\{i : (v|w)_i=1,\ (v|w)_{i+1}=0,\ (v\wedge w)_i=0,\ i<n-1\}$。对 $i\in S$ 令
		 * $\mathrm{nxt}(m)=\min\{k\ge m:(v|w)_k=1\}$（若不存在则取 $n$），原实作 OR 区间 $[i+1,\mathrm{nxt}(i+1)]\cap\mathbb Z$。
		 * 则位 $j\ge1$ 被置 1 若且唯若 $p:=\max(S\cap[0,j-1])$ 存在且 $j\le \mathrm{nxt}(p+1)$。
		 *
		 * `suf[m]` = $\mathrm{nxt}(m)$ 用 **并行後缀 min**（Θ(log n) 轮）；`pm[j]` = $\max(S\cap[0,j])$ 用 **并行前缀 max**
		 *（Θ(log n) 轮）。最後对 $j=1..n-1$ 组字（$n\le64$ 时为常数层）。
		 */
		[[nodiscard]] inline std::uint64_t m_of_rule2_fill_mask_parallel(
			std::uint64_t t_or, std::uint64_t t_and, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			t_or &= MASK;
			t_and &= MASK;
			if ( n <= 1 )
				return 0ull;

			const std::uint64_t seed = ( m_of_rule2_start_mask( t_or, t_and, n ) << 1u ) & MASK;
			const std::uint64_t z_mask = ( ~t_or ) & MASK;
			return propagate_ones_right_through_v_or_w_zero_mask( seed, z_mask, n );
		}

		[[nodiscard]] inline std::uint64_t m_of( std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			v &= MASK;
			w &= MASK;

			const std::uint64_t t_or = ( v | w ) & MASK;   // v|w
			const std::uint64_t t_and = ( v & w ) & MASK;  // v⋆w

			// Start from R(v ⊕ w)  (paper: "starting from R(v ⊕ w)")
			const std::uint64_t z_mask = ( ~t_or ) & MASK;
			const std::uint64_t rule2_seed =
				( m_of_rule2_start_mask( t_or, t_and, n ) << 1u ) & MASK;
			std::uint64_t a = ( R_of( ( v ^ w ) & MASK, n ) | rule2_seed ) & MASK;

			// (2) "1 0^k" 区段：并行後缀 next + 前缀 max(S) 组装（见 `m_of_rule2_fill_mask_parallel`）。
			// rule (2) is already injected via rule2_seed above.

			// (1) propagation rule inside regions where v_i=w_i=0:
			// z_i=1 ⇔ (v|w)_i=0 ⇔ v_i=w_i=0；与「单趟 i=0..n-2 顺序传递」逐位等价的 Θ(log n) 并行闭包。
			// One zero-run closure suffices for both rule (1) and rule (2):
			// closure(x union closure(y)) = closure(x union y).
			a = propagate_ones_right_through_v_or_w_zero_mask( a, z_mask, n );

			return a & MASK;
		}

		/**
		 * @brief Theorem 7: a(v,w) := aL(v⋆w) | m(v,w)
		 *
		 * Theorem 7 假设：v_{n-1} = w_{n-1} = 1，否则返回 std::nullopt。
		 */
		[[nodiscard]] inline std::optional<std::uint64_t> column_best_a_of( std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			v &= MASK;
			w &= MASK;
			if ( n <= 0 )
				return std::nullopt;

			// Theorem 7 assumption: v_{n-1} = w_{n-1} = 1
			const unsigned msb = static_cast<unsigned>( n - 1 );
			if ( ( ( v >> msb ) & 1ull ) == 0ull || ( ( w >> msb ) & 1ull ) == 0ull )
				return std::nullopt;

			const std::uint64_t m = m_of( v, w, n );
			const std::uint64_t a = ( aL_of( ( v & w ) & MASK, n ) | m ) & MASK;
			return a;
		}

		/**
		 * @brief Theorem 7: u(v,w) := (v ⊕ w) ⊕ (I ⊕ L)(a(v,w))
		 *
		 * 其中 (I ⊕ L)(a) = a ⊕ L(a) = a ⊕ (a >> 1)（bit0=LSB 约定）。
		 */
		[[nodiscard]] inline std::optional<std::uint64_t> column_best_u_of( std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			const auto			a_opt = column_best_a_of( v, w, n );
			if ( !a_opt.has_value() )
				return std::nullopt;

			const std::uint64_t a = a_opt.value() & MASK;
			const std::uint64_t u = ( ( v ^ w ) ^ a ^ ( a >> 1 ) ) & MASK;
			return u;
		}

		/**
		 * @brief Theorem 7: 返回列 (v,w) 的最大相关系数 φ2(u(v,w), v, w)
		 *
		 * 注意：Theorem 7 的结论是“绝对值最大”，符号由 Theorem 4 的 (-1)^{...} 决定；
		 * 此函数返回包含符号的 φ 值（可能为负）。
		 */
		[[nodiscard]] inline std::optional<double> column_best_correlation_value( std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const auto u_opt = column_best_u_of( v, w, n );
			if ( !u_opt.has_value() )
				return std::nullopt;

			return linear_correlation_add_ccz_value( u_opt.value(), v, w, n );
		}

		/**
		 * @brief 全域 fixed-(v,w) 的 optimal output mask u*（在 |φ2(u,v,w)| 意义下取任一个 argmax）
		 *
		 * 数学归约（与 Theorem 4/7 及列最大构造对齐；完整全域最优性请以论文为准）

			文献：Ernst Schulte–Geers，*On CCZ-equivalence of Addition mod $2^n$*；$\phi_2$ 为 Theorem 4，列构造为 Theorem 7。位序与程式一致：bit0 = LSB。

			引理 A（Theorem 4 可行时之最高位约束）  
			设 $\phi_2(u,v,w)\neq 0$，$s=u\oplus v\oplus w$，$z=M_n^\top(s)$，$h=\mathrm{msb}(u\vee v\vee w)$。则 $u_h=v_h=w_h$。  
			*证明要点*：$k>h$ 时 $u_k=v_k=w_k=0$ $\Rightarrow$ $s_k=0$ $\Rightarrow$ $z_h=0$；可行性要求 $(u\oplus v)_h,(u\oplus w)_h\le z_h$（偏序分量）$\Rightarrow$ $u_h=v_h=w_h$。

			引理 B（顶端不共享 1 $\Rightarrow$ 整列为 0）
			令 $p=\mathrm{msb}(v\vee w)$。若 $(v\wedge w)_p=0$，则 $\forall u,\ \phi_2(u,v,w)=0$。  
			*证明要点*：反设存在非零可行，则引理 A 给出某 $h$ 使 $u_h=v_h=w_h=1$；但 $v\vee w$ 之最高活跃位为 $p$ 且该位 $v,w$ 不同为 1，或 $h>p$ 时 $v_h=w_h=0$，皆矛盾。  
			→ 对应程式 `!shared_at_p` 时 `return 0`：**硬引理**，非经验剪枝。

			引理 C（顶端共享 1 $\Rightarrow$ 列最大可裁宽至 $p+1$）
			若 $(v\wedge w)_p=1$，则任意满足 $\phi_2(u,v,w)\neq 0$ 的 $u$ 必满足 $u_i=0$（$i>p$）。故  
			$\max_{u\in\mathbb F_2^n}|\phi_2(u,v,w,n)|=\max_{u<2^{p+1}}|\phi_2(u,v,w,n)|$。

			引理 D（低宽与 full 宽之 $\phi_2$ 一致）
			若 $u,v,w<2^m$ 且 $m\le n$，则 $\phi_2(u,v,w,n)=\phi_2(u,v,w,m)$。  
			*证明要点*：$s_k=0$（$k\ge m$）$\Rightarrow$ $M_n^\top(s)$ 与 $M_m^\top(s)$ 在低 $m$ 位相同；可行性与 $|z|$ 一致。

			主定理
			定义 $\mathcal U(v,w,n)$ 与 `find_optimal_output_u_ccz` 同语义（$v|w=0$ 或顶端不共享时返回 $0$；否则裁宽至 $p+1$ 并取 Theorem 7 之 `column_best_u_of` 结果，再以 $n$ 位掩码返回）。若 **Theorem 7** 在截断宽度上给出该宽度之**列绝对值最大**，则对所有 $n,v,w$，  
			$|\phi_2(\mathcal U(v,w,n),v,w,n)|=\max_{u\in\mathbb F_2^n}|\phi_2(u,v,w,n)|$。  
			*证明*：分情形：$v|w=0$；引理 B；引理 C + Theorem 7 + 引理 D。

			**实作校验**：对小 $n$ 可穷举全部 $u$，以 Theorem 4 计算 $|\phi_2(u,v,w)|$ 并与本函数输出比对列极大，作工程回归与论文归约之一致性检查；不替代上述纸面证明，亦不承担「证明 Theorem 7」之义务。

		 * **实作前提**：`m_of` / `aL_of` 应与 Schulte–Geers 文中 Theorem 7 前之 `m`、Theorem 5 之 L-minimal 向量叙述逐位一致（可由小 $n$ 全体检验支持）；**Θ(log n) 深度**（n≤64 槽）仅影响成本，与上述命题无关。
		 */
		[[nodiscard]] inline std::uint64_t find_optimal_output_u_ccz(
			std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			v &= MASK;
			w &= MASK;
			const std::uint64_t t_or = ( v | w ) & MASK;
			if ( t_or == 0ull )
				return 0ull;

			const int p = static_cast<int>( std::bit_width( t_or ) ) - 1;
			const unsigned pu = static_cast<unsigned>( p );
			const bool shared_at_p =
				( ( v >> pu ) & 1ull ) != 0ull && ( ( w >> pu ) & 1ull ) != 0ull;
			if ( !shared_at_p )
				return 0ull;

			const int reduced_n = p + 1;
			const std::uint64_t reduced_mask = mask_n( reduced_n );
			const auto u_opt = column_best_u_of( v & reduced_mask, w & reduced_mask, reduced_n );
			if ( !u_opt.has_value() )
				return 0ull;

			return u_opt.value() & MASK;
		}

		/**
		 * @brief Theorem 7（列最大）+ Theorem 4 的**一条流水线**：固定 (v,w)，先取 u* = `find_optimal_output_u_ccz`，
		 *        再以 `linear_correlation_add_ccz_weight(u*,v,w)` 得精确 w_lin = |z|。
		 *
		 * 与差分侧 `find_optimal_gamma` + `xdp_add_lm2001_n` 同构：γ* 对 u*，DP+ 权对 |z|。
		 * NeoAlzette **反向**一轮若已固定和线掩码 u，则须走 z-shell / cLAT 枚举 (v,w) 并对**该固定 u** 呼叫
		 * `linear_correlation_add_ccz_weight(u,v,w)` —— **不得**用此结构覆写轮上的 u。
		 */
		struct Phi2ColumnMax
		{
			std::uint64_t u = 0;
			SearchWeight  z_weight = 0;
		};

		[[nodiscard]] inline Phi2ColumnMax linear_correlation_add_phi2_column_max(
			std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			v &= MASK;
			w &= MASK;
			const std::uint64_t u = find_optimal_output_u_ccz( v, w, n );
			const auto			wt = linear_correlation_add_ccz_weight( u, v, w, n );
			const SearchWeight zw = wt.has_value() ? wt.value() : static_cast<SearchWeight>( n );
			return Phi2ColumnMax { u & MASK, zw };
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
