#pragma once

/**
 * @file modular_addition_ccz.hpp
 * @brief 模加法 (mod 2^n) 的 CCZ 等价构造 + 线性分析（Walsh/相关系数）显式算子
 *
 * 参考论文：
 * - Ernst Schulte-Geers, "On CCZ-equivalence of Addition mod 2^n"
 *   (下文注释中的公式/符号均按该文的 Definition / Theorem 编号逐条对应)
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

#include <cstdint>
#include <cstddef>
#include <bit>
#include <cmath>
#include <optional>

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
#if __cpp_lib_bitops >= 201907L
			return static_cast<unsigned>( std::popcount( x ) );
#elif defined( _MSC_VER )
			return static_cast<unsigned>( __popcnt64( x ) );
#else
			return static_cast<unsigned>( __builtin_popcountll( x ) );
#endif
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
		 */
		[[nodiscard]] inline std::uint64_t MnT_of( std::uint64_t s, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			s &= MASK;
			if ( n <= 0 )
				return 0ull;

			std::uint64_t z = 0ull;
			unsigned	  acc = 0u;	 // acc = s_i ⊕ s_{i+1} ⊕ ... ⊕ s_{n-1}
			for ( int i = n - 1; i >= 1; --i )
			{
				acc ^= static_cast<unsigned>( ( s >> i ) & 1ull );
				if ( acc )
					z |= ( 1ull << static_cast<unsigned>( i - 1 ) );
			}
			return z & MASK;
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
		[[nodiscard]] inline std::optional<int> differential_probability_add_ccz_weight( std::uint64_t alpha, std::uint64_t beta, std::uint64_t gamma, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			alpha &= MASK;
			beta &= MASK;
			gamma &= MASK;

			const std::uint64_t rhs = R_of( ( ( alpha ^ gamma ) | ( beta ^ gamma ) ) & MASK, n );
			const std::uint64_t lhs = I_xor_R_of( ( alpha ^ beta ^ gamma ) & MASK, n );
			if ( !leq_bitwise( lhs, rhs, n ) )
				return std::nullopt;

			return static_cast<int>( popcount_u64( rhs ) );
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
		[[nodiscard]] inline std::optional<int> linear_correlation_add_ccz_weight( std::uint64_t u, std::uint64_t v, std::uint64_t w, int n ) noexcept
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

			return static_cast<int>( popcount_u64( z ) );
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
		 * 其中 "⋆" 为逐位乘（在 F2 上等价于 bitwise AND）。
		 */
		[[nodiscard]] inline std::uint64_t aL_of( std::uint64_t u, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			u &= MASK;

			std::uint64_t acc = 0ull;
			std::uint64_t term = u;	 // term_k = u ⋆ L(u) ⋆ ... ⋆ L^k(u)
			for ( int k = 0; k < n; ++k )
			{
				acc ^= term;
				// 下一个因子为 L^{k+1}(u)；bit0=LSB 下 L^{t}(u) == (u >> t)
				const std::uint64_t shifted = ( k + 1 >= 64 ) ? 0ull : ( u >> static_cast<unsigned>( k + 1 ) );
				term &= shifted;
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
		[[nodiscard]] inline std::uint64_t m_of( std::uint64_t v, std::uint64_t w, int n ) noexcept
		{
			const std::uint64_t MASK = mask_n( n );
			v &= MASK;
			w &= MASK;

			const std::uint64_t t_or = ( v | w ) & MASK;   // v|w
			const std::uint64_t t_and = ( v & w ) & MASK;  // v⋆w

			// Start from R(v ⊕ w)  (paper: "starting from R(v ⊕ w)")
			std::uint64_t a = R_of( ( v ^ w ) & MASK, n );

			// (2) handle segments "1 0^k" in (v|w) with (v⋆w)=0 at the leading 1-bit.
			// For such segment starting at i, set L(a) on that segment to all ones,
			// which is equivalent to setting a_{i+1..end} = 1 (end = i+k+1).
			for ( int i = 0; i + 1 < n; ++i )
			{
				const unsigned ti = static_cast<unsigned>( ( t_or >> i ) & 1ull );
				if ( ti == 0u )
					continue;

				const unsigned next_t = static_cast<unsigned>( ( t_or >> ( i + 1 ) ) & 1ull );
				if ( next_t != 0u )
					continue;  // not a "1 0^k" start

				const unsigned gi = static_cast<unsigned>( ( t_and >> i ) & 1ull );
				if ( gi != 0u )
					continue;  // v⋆w is 1 at the leading 1-bit => not the "0^{k+1}" case

				int j = i + 1;
				while ( j < n && ( ( t_or >> j ) & 1ull ) == 0ull )
					++j;

				// Now (v|w) has pattern: [i]=1, [i+1 .. j-1]=0.
				// Force a_{i+1 .. j} = 1  (note: j can be n-1 or n)
				for ( int k = i + 1; k <= j && k < n; ++k )
					a |= ( 1ull << static_cast<unsigned>( k ) );
			}

			// (1) propagation rule inside regions where v_i=w_i=0:
			// if a_i==1 and v_i=w_i=0 then enforce a_{i+1}=1
			for ( int i = 0; i + 1 < n; ++i )
			{
				const unsigned vi = static_cast<unsigned>( ( v >> i ) & 1ull );
				const unsigned wi = static_cast<unsigned>( ( w >> i ) & 1ull );
				if ( vi == 0u && wi == 0u )
				{
					if ( ( ( a >> i ) & 1ull ) != 0ull )
						a |= ( 1ull << static_cast<unsigned>( i + 1 ) );
				}
			}

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

	}  // namespace arx_operators
}  // namespace TwilightDream
