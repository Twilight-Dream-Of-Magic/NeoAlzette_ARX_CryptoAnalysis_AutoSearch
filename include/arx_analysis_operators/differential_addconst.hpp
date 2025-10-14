/**
 * @file differential_addconst.hpp
 * @brief 常量加法差分分析（變量-常量）
 * 
 * 論文："A Bit-Vector Differential Model for the Modular Addition by a Constant" (2022)
 * 
 * 兩種算法：
 * 1. Theorem 2 (Machado 2015): 精確浮点数 迭代方法 O(n)
 * 2. Algorithm 1 (BvWeight): 最优近似 位向量方法 O(log n)
 * 
 * 當前實現：Algorithm 1 (BvWeight) - 對數複雜度
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <bit>
#include "arx_analysis_operators/math_util.hpp"

namespace TwilightDream
{
	namespace bitvector
	{

		// ============================================================================
		// 基礎位向量操作（O(1) 或 O(log n)）
		// ============================================================================

		/**
		 * @brief HW(x) - Hamming Weight（漢明重量）
		 * 
		 * 計算x中1的個數
		 * 複雜度：O(1)（硬件指令）
		 * 
		 * 論文：第209-213行
		 */
		inline uint32_t HammingWeight( uint32_t x ) noexcept
		{
			return __builtin_popcount( x );
		}

		/**
		 * @brief Rev(x) - Bit Reversal（位反轉）
		 * 
		 * 反轉x的位順序：Rev(x) = (x[0], x[1], ..., x[n-1])
		 * 複雜度：O(log n)
		 * 
		 * 論文：第204-206行
		 * 參考：Hacker's Delight, Fig. 7-1
		 */
		inline uint32_t Rev( uint32_t x ) noexcept
		{
			// 交換相鄰位
			x = ( ( x & 0x55555555 ) << 1 ) | ( ( x >> 1 ) & 0x55555555 );
			// 交換相鄰2位組
			x = ( ( x & 0x33333333 ) << 2 ) | ( ( x >> 2 ) & 0x33333333 );
			// 交換相鄰4位組
			x = ( ( x & 0x0F0F0F0F ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0F );
			// 交換字節
			x = ( ( x & 0x00FF00FF ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FF );
			// 交換半字
			x = ( x << 16 ) | ( x >> 16 );
			return x;
		}

		/**
		 * @brief Carry(x, y) - 進位鏈
		 * 
		 * 計算x + y的進位鏈
		 * 公式：Carry(x, y) = x ⊕ y ⊕ (x ⊞ y)
		 * 複雜度：O(1)
		 * 
		 * 論文：第198-200行
		 */
		inline uint32_t Carry( uint32_t x, uint32_t y ) noexcept
		{
			return x ^ y ^ ( ( x + y ) & 0xFFFFFFFF );
		}

		/**
		 * @brief RevCarry(x, y) - 反向進位
		 * 
		 * 從右到左的進位傳播
		 * 公式：RevCarry(x, y) = Rev(Carry(Rev(x), Rev(y)))
		 * 複雜度：O(log n)
		 * 
		 * 論文：第207-208行
		 */
		inline uint32_t RevCarry( uint32_t x, uint32_t y ) noexcept
		{
			return Rev( Carry( Rev( x ), Rev( y ) ) );
		}

		/**
		 * @brief LZ(x) - Leading Zeros（前導零標記）
		 * 
		 * 標記x的前導零位
		 * 定義：LZ(x)[i] = 1 ⟺ x[n-1, i] = 0
		 * 即：從最高位到第i位都是0
		 * 複雜度：O(1)（使用硬件指令）
		 * 
		 * 論文：第214-218行
		 */
		inline uint32_t LeadingZeros( uint32_t x ) noexcept
		{
			if ( x == 0 )
				return 0xFFFFFFFF;

			// 使用CLZ（Count Leading Zeros）
			int clz = __builtin_clz( x );  // 計算前導零個數

			// 構造標記向量：從最高位到clz位都設為1
			return ( clz == 32 ) ? 0xFFFFFFFF : ( 0xFFFFFFFF << ( 32 - clz ) );
		}

		// ============================================================================
		// 高級位向量操作（Algorithm 1核心）
		// ============================================================================

		/**
		 * @brief ParallelLog(x, y) - 並行對數
		 * 
		 * 對於y分隔的子向量，並行計算x的對數（整數部分）
		 * 公式：ParallelLog(x, y) = HW(RevCarry(x ∧ y, y))
		 * 複雜度：O(log n)
		 * 
		 * 論文：第1479行，Proposition 1(a)
		 * 
		 * @param x 數據向量
		 * @param y 分隔向量（每個子向量為 (1,1,...,1,0)）
		 * @return 所有子向量的 log₂ 之和
 */
		inline uint32_t ParallelLog( uint32_t x, uint32_t y ) noexcept
		{
			return HammingWeight( RevCarry( x & y, y ) );
		}

		/**
		 * @brief ParallelTrunc(x, y) - 並行截斷
		 * 
		 * 對於y分隔的子向量，並行提取x的小數部分（最高4位）
		 * 公式：ParallelTrunc(x, y) = (HW(z₀) << 0) ⊕ (HW(z₁) << 2) ⊕ (HW(z₂) << 1) ⊕ HW(z₃)
		 * 其中 z_λ = x ∧ (y << λ) ∧ ¬(y << (λ+1))
		 * 複雜度：O(log n)
		 * 
		 * 論文：第1480-1492行，Proposition 1(b)
		 * 
		 * @param x 數據向量
		 * @param y 分隔向量
		 * @return 所有子向量的截斷小數部分之和
		 */
		inline uint32_t ParallelTrunc( uint32_t x, uint32_t y ) noexcept
		{
			// z_λ = x ∧ (y << λ) ∧ ¬(y << (λ+1))
			uint32_t z0 = x & y & ~( y << 1 );
			uint32_t z1 = x & ( y << 1 ) & ~( y << 2 );
			uint32_t z2 = x & ( y << 2 ) & ~( y << 3 );
			uint32_t z3 = x & ( y << 3 );

			// ParallelTrunc = (HW(z₀) << 0) ⊕ (HW(z₁) << 2) ⊕ (HW(z₂) << 1) ⊕ HW(z₃)
			uint32_t result = HammingWeight( z0 );
			result ^= ( HammingWeight( z1 ) << 2 );
			result ^= ( HammingWeight( z2 ) << 1 );
			result ^= HammingWeight( z3 );

			return result;
		}

	}  // namespace bitvector

	namespace arx_operators
	{

		/**
		 * @brief Algorithm 1: BvWeight - 計算常量加法差分權重
		 * 
		 * 論文Algorithm 1, Lines 1701-1749
		 * 複雜度：O(log²n)
		 * 
		 * @param delta_x 輸入差分
		 * @param constant 常量K
		 * @param delta_y 輸出差分
		 * @return 近似權重，0表示權重為0，-1表示不可能
		 */
		inline int diff_addconst_bvweight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			using namespace bitvector;

			const std::uint32_t u = delta_x;
			const std::uint32_t v = delta_y;
			const std::uint32_t a = constant;

			// Algorithm 1, Lines 1704-1709
			uint32_t s000 = ~( u << 1 ) & ~( v << 1 );
			uint32_t s000_prime = s000 & ~LeadingZeros( ~s000 );

			// Lines 1712-1720
			uint32_t t = ~s000_prime & ( s000 << 1 );
			uint32_t t_prime = s000_prime & ~( s000 << 1 );

			// Lines 1722-1723
			uint32_t s = ( ( a << 1 ) & t ) ^ ( a & ( s000 << 1 ) );

			// Lines 1726-1730
			uint32_t q = ~( ( a << 1 ) ^ u ^ v );
			uint32_t d = RevCarry( ( s000_prime << 1 ) & t_prime, q ) | q;

			// Line 1731
			uint32_t w = ( q << ( s & d ) ) | ( s & ~d );

			// Lines 1733-1735
			uint32_t int_part = HammingWeight( ( u ^ v ) << 1 ) ^ HammingWeight( s000_prime ) ^ ParallelLog( ( w & s000_prime ) << 1, s000_prime << 1 );

			// Lines 1738-1742
			uint32_t frac = ParallelTrunc( w << 1, RevCarry( ( w & s000_prime ) << 1, s000_prime << 1 ) );

			// Line 1743: bvweight = int_part || frac（4位小數）
			uint32_t bvweight = ( int_part << 4 ) | frac;

			// 轉換為權重
			if ( bvweight == 0 )
				return 0;
			double approx_weight = static_cast<double>( bvweight ) / 16.0;
			return static_cast<int>( std::ceil( approx_weight ) );
		}

		// 使用公共 math_util.hpp 的 neg_mod_2n

		/**
		 * @brief 常量減法差分權重
		 * 
		 * X ⊟ C = X ⊞ (~C + 1)
		 * 
		 * @param delta_x 輸入差分
		 * @param constant 常量C
		 * @param delta_y 輸出差分
		 * @return 近似權重
		 */
		inline int diff_subconst_bvweight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			// X - C = X + ((~C) + 1)
			std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<uint32_t>( constant, 32 );
			return diff_addconst_bvweight( delta_x, neg_constant, delta_y );
		}

		/**
		 * @brief 計算常量加法差分概率
		 * 
		 * @param delta_x 輸入差分
		 * @param constant 常量K
		 * @param delta_y 輸出差分
		 * @return 近似概率
		 */
		inline double diff_addconst_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			int weight = diff_addconst_bvweight( delta_x, constant, delta_y );
			if ( weight < 0 )
				return 0.0;
			return std::pow( 2.0, -weight );
		}

		/**
		 * @brief 計算常量减法差分概率
		 * 
		 * @param delta_x 輸入差分
		 * @param constant 常量K
		 * @param delta_y 輸出差分
		 * @return 近似概率
		 */
		inline double diff_subconst_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			int weight = diff_subconst_bvweight( delta_x, constant, delta_y );
			if ( weight < 0 )
				return 0.0;
			return std::pow( 2.0, -weight );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
