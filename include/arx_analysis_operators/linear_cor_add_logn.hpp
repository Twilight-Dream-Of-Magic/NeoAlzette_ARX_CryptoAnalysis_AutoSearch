/**
 * @file linear_cor_add_logn.hpp
 * @brief 模加法線性相關度 - Θ(log n)對數算法（變量-變量）
 * 
 * 論文：Wallén (2003), "Linear Approximations of Addition Modulo 2^n", FSE 2003
 * 
 * **關鍵發現**：Wallén Theorem 2 + Corollary 1
 * 
 * "The correlation coefficients C(u ← v, w) can be computed in time Θ(log n)"
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

namespace neoalz
{
	namespace arx_operators
	{

		/**
		 * @brief Wallén Theorem 2: 計算cpm(x, y) - Θ(log n)時間
		 * 
		 * 論文Algorithm (Theorem 2, Lines 495-506):
		 * 
		 * 1. Initialise β = 1010...1010, z0 = 0, z1 = 1
		 * 2. For i = 0 to log2(n) - 1:  ← 只循環log(n)次！
		 *    (a) γb = ((y ∧ zb ∧ x) ∨ (y ∧ z̄b ∧ x̄)) ∧ β
		 *    (b) γb ← γb ∨ (γb >> 2^i)
		 *    (c) tb = (zb ∧ α[i]) ∨ (z0 ∧ γb ∧ ᾱ[i]) ∨ (z1 ∧ γ̄b)
		 *    (d) zb ← tb
		 *    (e) β ← (β >> 2^i) ∧ α[i+1]
		 * 3. Return z0
		 * 
		 * @param x 第一個向量
		 * @param y 第二個向量
		 * @return cpm(x, y)
		 */
		inline uint64_t compute_cpm_logn( uint64_t x, uint64_t y, int n = 32 ) noexcept
		{
			// n 必须是 2 的幂：8/16/32/64 皆可
			// 生成 α^(i) ：从 LSB 开始，每 2^i 位为一块的 1/0 掩码（LSB 侧先是 1 块）
			auto alpha_i = [ & ]( int i ) -> uint64_t {
				uint64_t mask = ( n == 64 ) ? ~0ull : ( ( 1ull << n ) - 1ull );
				uint64_t block = ( i == 63 ? ~0ull : ( ( 1ull << ( 1u << i ) ) - 1ull ) );
				uint64_t pat = 0, period = ( 1ull << ( i + 1 ) );
				for ( uint64_t pos = 0; pos < ( uint64_t )n; pos += period )
				{
					pat |= ( block << pos );
				}
				return pat & mask;
			};

			auto log2_n = [ & ]() -> int {
#if defined( __GNUC__ ) || defined( __clang__ )
				return ( n == 64 ) ? 6 : __builtin_ctz( ( unsigned )n );
#else
				int t = 0;
				while ( ( 1u << t ) < ( unsigned )n )
					++t;
				return t;
#endif
			}();

			const uint64_t MASK = ( n == 64 ) ? ~0ull : ( ( 1ull << n ) - 1ull );

			// Step 1: Initialise β = 1010… , z0 = 0, z1 = 1⃗
			uint64_t beta = ( ~alpha_i( 0 ) ) & MASK;  // 1010… (与 α^(0)=0101… 互补)
			uint64_t z0 = 0ull;
			uint64_t z1 = MASK;

			// Step 2: for i = 0 .. log2(n)-1
			for ( int i = 0; i < log2_n; ++i )
			{
				const uint64_t Ai = alpha_i( i );
				const unsigned s = ( 1u << i );

				// 2(a): γb = ((y ∧ zb ∧ x) ∨ (y ∧ ¬zb ∧ ¬x)) ∧ β
				uint64_t gamma0 = ( ( y & z0 & x ) | ( y & ( ~z0 ) & ( ~x ) ) ) & beta;
				uint64_t gamma1 = ( ( y & z1 & x ) | ( y & ( ~z1 ) & ( ~x ) ) ) & beta;

				// 2(b): γb ← γb ∨ (γb >> 2^i)   ← 注意是“右移”
				gamma0 = ( gamma0 | ( gamma0 >> s ) ) & MASK;
				gamma1 = ( gamma1 | ( gamma1 >> s ) ) & MASK;

				// 2(c): tb = (zb ∧ α^(i)) ∨ (z0 ∧ γb ∧ ¬α^(i)) ∨ (z1 ∧ ¬γb)
				uint64_t t0 = ( z0 & Ai ) | ( z0 & gamma0 & ( ~Ai ) ) | ( z1 & ( ~gamma0 ) );
				uint64_t t1 = ( z1 & Ai ) | ( z0 & gamma1 & ( ~Ai ) ) | ( z1 & ( ~gamma1 ) );

				// 2(d): zb ← tb
				z0 = t0 & MASK;
				z1 = t1 & MASK;

				// 2(e): β ← (β >> 2^i) ∧ α^(i+1)  ← 注意是“右移”
				beta = ( ( beta >> s ) & alpha_i( i + 1 ) ) & MASK;
			}

			// 3: Return z0
			return z0 & MASK;
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
		inline int linear_cor_add_wallen_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			// Lemma 7: C(u ← v, w) = C(u ←^carry v+u, w+u)
			uint32_t v_prime = ( v + u ) & 0xFFFFFFFF;
			uint32_t w_prime = ( w + u ) & 0xFFFFFFFF;

			// 計算 eq(v', w') = ~(v' ⊕ w')
			uint32_t eq_vw = eq( v_prime, w_prime );

			// 計算 z = cpm(u, eq(v', w')) - Θ(log n)時間
			uint32_t z = static_cast<uint32_t>( compute_cpm_logn( u, eq_vw ) );

			// Theorem 1: 檢查可行性
			// 如果 v'_z = 0 或 w'_z = 0，則相關度為0
			if ( ( v_prime & z ) == 0 || ( w_prime & z ) == 0 )
			{
				return -1;	// 不可行
			}

			// 計算權重 = HW(z)
			int weight = __builtin_popcount( z );

			return weight;
		}

		/**
		 * @brief 計算相關度值（不只是權重）
		 * 
		 * @return 相關度 ∈ [-1, 1]
		 */
		inline double linear_cor_add_value_logn( std::uint32_t u, std::uint32_t v, std::uint32_t w ) noexcept
		{
			int weight = linear_cor_add_wallen_logn( u, v, w );
			if ( weight < 0 )
				return 0.0;

			// 相關度 = ±2^{-weight}
			// 注意：符號由具體情況決定，這裡返回絕對值
			return std::pow( 2.0, -weight );
		}

	}  // namespace arx_operators
}  // namespace neoalz
