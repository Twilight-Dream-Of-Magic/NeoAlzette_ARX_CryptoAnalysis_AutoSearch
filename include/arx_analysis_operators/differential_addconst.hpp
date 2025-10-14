/**
 * @file differential_addconst.hpp
 * @brief 常量加法 XOR 差分分析（變量-常量）：y = x ⊞ a (mod 2^n)
 * 
 * 參考論文：
 * - Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
 *   (ASIACRYPT 2020, LNCS 12491)。
 * - 擴展版本（含應用與 impossible-differential）："A Bit-Vector Differential Model for the Modular Addition by a Constant
 *   and its Applications to Differential and Impossible-Differential Cryptanalysis"（你專案 `papers/` 中亦有收錄）。
 * - 精確 DP（遞迴/逐位形式）最早可追溯到：
 *   Machado, "Differential Probability of Modular Addition with a Constant Operand", IACR ePrint 2001/052。
 *
 * ----------------------------------------------------------------------------
 * 0) 符號 / 位序約定（務必先對齊，避免「看起來對、其實差一位」）
 * ----------------------------------------------------------------------------
 * - 位元索引：bit 0 = LSB，bit (n-1) = MSB。
 * - 論文約定：對所有 i<0，視 u[i]=v[i]=a[i]=0（本檔所有涉及 i-λ 之類的索引也遵守此約定）。
 * - XOR 差分（也是本專案差分的預設）：給定輸入差分 u=Δx 與輸出差分 v=Δy：
 *
 *   y  = x ⊞ a                 (mod 2^n)
 *   y' = (x ⊕ u) ⊞ a           (mod 2^n)
 *   v  = y ⊕ y'
 *
 *   我們關心：Pr_x[ v ]，以及 weight^a(u,v) = -log2(Pr_x[v])。
 *
 * - 參數命名對照（避免縮寫，但保留論文符號以便對照）：
 *   - delta_x  ↔ u（輸入 XOR 差分）
 *   - delta_y  ↔ v（輸出 XOR 差分）
 *   - constant ↔ a（加法常量）
 * 
 * ----------------------------------------------------------------------------
 * 1) 你會在這個檔案看到的「三種」量（每個都對應明確的論文公式）
 * ----------------------------------------------------------------------------
 * 1) 精確 DP / count（Machado 2001/052；Azimi DCC 2022 Theorem 2 等價敘述）
 *    - count(u,a,v) = #{ x ∈ {0,1}^n | (x ⊞ a) ⊕ ((x ⊕ u) ⊞ a) = v }
 *    - DP = Pr[u -> v] = count / 2^n
 *    - exact_weight = -log2(DP) = n - log2(count)
 *    對應 API：
 *      diff_addconst_exact_count_n / diff_addconst_exact_probability_n / diff_addconst_exact_weight_n
 *
 * 2) 精確 weight（Azimi Lemma 3/4/5 的閉式拆解；不做 Qκ 截斷）
 *    論文給出（把 -Σ log2(ϕ_i) 拆成「整數項」+「鏈項」+「π_i 的 log」）：
 *
 *      weight^a(u,v)
 *        = HW(((u ⊕ v) << 1))                       (Lemma 3: i∉I 的整數部分)
 *        + Σ_{i∈I} (λ_i - 1)                         (Lemma 5: 鏈長貢獻)
 *        - Σ_{i∈I} log2(π_i)                         (Lemma 4: ϕ_i = π_i / 2^{λ_i-1})
 *
 *    對應 API：
 *      diff_addconst_weight_log2pi_n
 *
 * 3) 近似 weight：BvWeight^κ（Azimi Algorithm 1；κ=4 即論文預設 Q4）
 *    - 目的：把 log2(π_i) 的小數部分用 κ 個 bits 的 fixed-point 近似（SMT/bit-vector 好處理）。
 *    - 輸出：BvWeight^κ(u,v,a) 是 Qκ（低 κ bits 為小數）；代表
 *        apxweight^a(u,v) ≈ 2^{-κ} * BvWeight^κ(u,v,a)
 *    對應 API：
 *      diff_addconst_bvweight_fixed_point_n  (通用 κ)
 *      diff_addconst_bvweight_q4_n           (κ=4 的便捷封裝)
 * 
 * 本檔工程化實作說明：
 * - 對「C++ 搜尋框架」而言，固定 32-bit 時 O(n)=32 幾乎等同常數成本；
 *   因此我們在 `diff_addconst_bvweight_fixed_point_n` / `diff_addconst_weight_log2pi_n` 中
 *   **直接依論文的數學定義展開計算**（仍然輸出同一個近似量 BvWeight^κ / 精確 weight），
 *   讓每個中間量（鏈長 λᵢ、πᵢ、Truncate(πᵢ)）都可被列印/單元測試審計。
 * - 若你的目標是 SMT 編碼，則更偏好使用論文的「位向量原語」寫法；本檔仍保留 HW/LZ/RevCarry/ParallelLog/ParallelTrunc
 *   等基元與對應註解，便於未來再切回純 bit-vector constraints。
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <bit>
#include <utility>
#include "arx_analysis_operators/math_util.hpp"

#if defined(__cpp_lib_bitops) && (__cpp_lib_bitops >= 201907L)
  #include <bit>      // std::bit_width
#endif

#if defined(_MSC_VER)
  #include <intrin.h> // _BitScanReverse64
#endif

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
		inline constexpr std::uint32_t HammingWeight( std::uint32_t x ) noexcept
		{
			#if defined( __cpp_lib_bitops ) && ( __cpp_lib_bitops >= 201907L )
						return static_cast<std::uint32_t>( std::popcount( x ) );
			#elif defined( _MSC_VER )
						return static_cast<std::uint32_t>( __popcnt( static_cast<unsigned long>( x ) ) );
			#else
						return static_cast<std::uint32_t>( __builtin_popcount( x ) );
			#endif
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
		 * @brief BitReverse(x) - Bit Reversal（位反轉）
		 * 
		 * 反轉x的位順序：Rev(x) = (x[0], x[1], ..., x[n-1])
		 * 複雜度：O(log n)
		 * 
		 * 論文：第204-206行
		 * 參考：Hacker's Delight, Fig. 7-1
		 */
		inline constexpr std::uint32_t BitReverse( std::uint32_t x ) noexcept
		{
			// 交換相鄰位
			x = ( ( x & 0x55555555u ) << 1 ) | ( ( x >> 1 ) & 0x55555555u );
			// 交換相鄰2位組
			x = ( ( x & 0x33333333u ) << 2 ) | ( ( x >> 2 ) & 0x33333333u );
			// 交換相鄰4位組
			x = ( ( x & 0x0F0F0F0Fu ) << 4 ) | ( ( x >> 4 ) & 0x0F0F0F0Fu );
			// 交換字節
			x = ( ( x & 0x00FF00FFu ) << 8 ) | ( ( x >> 8 ) & 0x00FF00FFu );
			// 交換半字
			x = ( x << 16 ) | ( x >> 16 );
			return x;
		}


		inline uint32_t CountLeftZeros( uint32_t x ) noexcept
		{
			if ( x == 0 )
				return 32u;

			#if __cpp_lib_bitops >= 201907L
						return static_cast<uint32_t>( std::countl_zero( x ) );
			#elif defined( _MSC_VER )
						return static_cast<uint32_t>( __lzcnt( x ) );
			#else
						return static_cast<uint32_t>( __builtin_clz( x ) );
			#endif
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
		inline uint32_t BitReverseCarry( uint32_t x, uint32_t y ) noexcept
		{
			return BitReverse( Carry( BitReverse( x ), BitReverse( y ) ) );
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
			return HammingWeight( BitReverseCarry( x & y, y ) );
		}

		/**
		 * @brief ParallelTrunc(x, y) - 並行截斷
		 * 
		 * 對於 y 分隔的子向量，並行提取 x 的「Truncate(...)」值（4 bits）。
		 *
		 * 論文 Proposition 1(b)：
		 * - 對每個 delimited sub-vector x[i_t, j_t]（由 y[i_t, j_t]=(1,1,...,1,0) 指示），
		 *   其對應的 Truncate 作用在 x[i_t, j_t+1]（注意：j_t 那一位是分隔用的 0，不屬於有效資料位）。
		 * - Truncate(z) 取的是 z 的 **4 個最高位**（不足 4 則右側補 0），並把它當成 4-bit 整數。
		 *
		 * 工程化理解：
		 * - 在 BvWeight/ apxlog2 的使用情境中，這 4 bits 會對應到「MSB 之後的 4 個 bits」（Eq.(4)），
		 *   也就是 apxlog2 的 fraction bits。
		 *
		 * 實作方式（Proposition 1(b) 的位運算展開，等價於論文中的 HW(z_λ) 組合）：
		 * - 在 bit0=LSB 的索引約定下，要選出每個子向量的「MSB 往下數第 λ 位」，
		 *   需同時保證該位上方連續為 1（避免短鏈造成錯位），因此 z_λ 需要把 y 的多個右移版本做交集：
		 *     z_λ = x ∧ (y >> 0) ∧ (y >> 1) ∧ ... ∧ (y >> λ) ∧ ¬(y >> (λ+1))，λ=0..3
		 * - 則 ParallelTrunc(x,y) = (HW(z0)<<3) + (HW(z1)<<2) + (HW(z2)<<1) + HW(z3)
		 *
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
		 * 論文語意（Azimi bit-vector primitives）：
		 * - ParallelLog(x, sep) 會把 x 中由 sep 指示的「區段」做並行的 log/層級聚合，
		 *   在 Algorithm 1 裡用來取得 floor(log2(π_i)) 等整數部分。
		 *
		 * 工程化說明（本檔的定位）：
		 * - 我們的 BvWeight^κ 實作採用「逐鏈計算 π_i」的可審計寫法，
		 *   因此並不強依賴 ParallelLog/ParallelTrunc 的黑盒位運算；
		 *   但仍保留這些 primitives 以及 n-bit wrapper，方便：
		 *   1) 日後回切成純 bit-vector constraints（SMT/MILP/SAT）
		 *   2) 對照論文 pseudo-code 的原始結構
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
		 * 論文語意（Azimi Eq.(4) 的 Truncate）：
		 * - κ=4 時，Truncate(π_i[m-1,0]) 取的是 π_i 的 MSB 右側 4 個 bits（不足補 0），
		 *   用來構造 apxlog2(π_i) 的小數部分。
		 *
		 * 同 ParallelLog_n，本工程主要採用逐鏈可審計版本；此 wrapper 主要是保留可對照性。
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

		/**
		 * @brief Exact feasibility check for XOR-differential of (x ⊞ constant) in n bits.
		 *
		 * This checks whether there exists x such that:
		 *   (x ⊞ a) ⊕ ((x ⊕ Δx) ⊞ a) == Δy   (mod 2^n)
		 *
		 * 工程化說明（對照 Azimi 的「valid/invalid」位向量模型）：
		 * - 論文使用位向量約束去描述 differential validity（避免狀態 001 等不合法情形）
		 *   以便直接丟給 SMT solver。
		 * - 在 C++ 自動化搜尋裡，我們改用更直觀/可審計的 **逐 bit DP**：
		 *   只需追蹤兩條加法的 carry（c, c'），因此狀態空間僅 4 個（00/01/10/11）。
		 * - 這個檢查是 **精確的可行性判定**（existential），不計算概率/權重，只用於：
		 *   1) 早期剪枝（impossible differential 直接淘汰）
		 *   2) 保護後續 πᵢ 計算避免出現 0/未定義情況
		 *
		 * 等價關係（便於理解）：
		 * - 若 `diff_addconst_exact_count_n(...) == 0`，則必然不可行。
		 * - 本函式相當於「把 count 的 DP 遞推改成 bool OR」，因此更快、也更適合用作 guard。
		 */
		inline bool is_diff_addconst_possible_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			if ( n <= 0 )
				return ( delta_x == 0u && delta_y == 0u );

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			delta_x &= mask;
			delta_y &= mask;
			constant &= mask;

			// possible states for (carry, carry') in the two evaluations (x, x⊕Δx)
			std::uint8_t possible[ 4 ] = { 1, 0, 0, 0 };

			for ( int bit_index = 0; bit_index < n; ++bit_index )
			{
				const std::uint32_t input_difference_bit = ( delta_x >> bit_index ) & 1u;
				const std::uint32_t output_difference_bit = ( delta_y >> bit_index ) & 1u;
				const std::uint32_t constant_bit = ( constant >> bit_index ) & 1u;

				std::uint8_t next_possible[ 4 ] = { 0, 0, 0, 0 };
				for ( int state = 0; state < 4; ++state )
				{
					if ( !possible[ state ] )
						continue;

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

						const int next_state = int( next_carry0 | ( next_carry1 << 1 ) );
						next_possible[ next_state ] = 1;
					}
				}

				possible[ 0 ] = next_possible[ 0 ];
				possible[ 1 ] = next_possible[ 1 ];
				possible[ 2 ] = next_possible[ 2 ];
				possible[ 3 ] = next_possible[ 3 ];
				if ( !( possible[ 0 ] | possible[ 1 ] | possible[ 2 ] | possible[ 3 ] ) )
					return false;
			}

			return ( possible[ 0 ] | possible[ 1 ] | possible[ 2 ] | possible[ 3 ] ) != 0;
		}

		/**
		 * @brief 精確 DP（count/2^n）: 計算常量加法 XOR 差分的解數（存在多少 x 使得差分成立）
		 *
		 * 定義：
		 *   y  = x ⊞ a   (mod 2^n)
		 *   y' = (x ⊕ u) ⊞ a
		 *   v  = y ⊕ y'
		 *
		 * 則 DP = Pr_x[ v ] = count(u,a,v) / 2^n，其中 count 是滿足條件的 x 個數。
		 *
		 * 參考/出處：
		 * - Machado, "Differential probability of modular addition with a constant operand",
		 *   IACR ePrint 2001/052.
		 * - Azimi et al., "A bit-vector differential model for the modular addition by a constant"
		 *   (DCC 2022 / ASIACRYPT 2020 extended), Theorem 2 亦給出等價的逐位遞推（以 δ_i, ϕ_i 表示）。
		 *
		 * Theorem 2（Azimi）如何對上本實作？
		 * - Theorem 2 用 (δ_i, ϕ_i) 表示每一位對 DP 的乘法因子/遞推。
		 * - 在工程實作裡，我們改用更直接的 carry-pair 狀態：
		 *     state_i = (c_i, c'_i) ∈ {0,1}^2
		 *   其中 c_i / c'_i 分別是計算 (x ⊞ a) 與 ((x⊕u) ⊞ a) 在 bit i 的 carry-in。
		 * - 這兩種表述是等價的：δ_i/ϕ_i 本質上在描述「哪些 carry 轉移允許」以及「允許轉移的數量」，
		 *   而 carry-pair DP 直接把它展開成 4-state 的有限狀態自動機累加計數。
		 *
		 * 工程化實作（本函式）：
		 * - 使用 4 個 carry-pair 狀態 (c, c') ∈ {0,1}^2 的逐位 DP，精確計數。
		 * - 時間 O(n)（每 bit 最多 4*2 分支），對固定 32-bit 幾乎可視為常數成本。
		 */
		inline std::uint64_t diff_addconst_exact_count_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			// ------------------------------------------------------------
			// 精確計數（Machado 2001/052 的 N；亦等價於 Azimi DCC 2022 Theorem 2 的逐位遞推）
			//
			// 觀察：對固定常量 a 與輸入 XOR 差分 u，
			//       (x ⊞ a) 與 ((x ⊕ u) ⊞ a) 之間的關係完全由「兩條加法的 carry」決定。
			//
			// 令 c_i, c'_i 分別是第 i bit（從 LSB 起）對應兩條加法的 carry-in（c_0=c'_0=0）。
			// 對每個 bit，我們枚舉 x_i ∈ {0,1}，並檢查輸出差分 bit v_i 是否成立：
			//
			//   y_i  = x_i      ⊕ a_i ⊕ c_i
			//   y'_i = (x_i⊕u_i) ⊕ a_i ⊕ c'_i
			//   需要：v_i = y_i ⊕ y'_i
			//
			// 同時更新下一個 carry：
			//   c_{i+1}  = maj(x_i, a_i, c_i)
			//   c'_{i+1} = maj(x_i⊕u_i, a_i, c'_i)
			//
			// 因此只需 DP 在 4 個狀態 (c_i, c'_i) ∈ {0,1}^2 上累加「可行 x 前綴數量」，
			// 最後總和即為 count(u,a,v)。
			// ------------------------------------------------------------
			if ( n <= 0 )
			{
				// 0-bit domain: only one input value exists.
				return ( delta_x == 0u && delta_y == 0u ) ? 1ull : 0ull;
			}

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			delta_x &= mask;
			delta_y &= mask;
			constant &= mask;

			std::uint64_t counts_by_carry_pair_state[ 4 ] = { 1ull, 0ull, 0ull, 0ull };	 // (carry, carry') = (0,0)

			for ( int bit_index = 0; bit_index < n; ++bit_index )
			{
				const std::uint32_t input_difference_bit = ( delta_x >> bit_index ) & 1u;
				const std::uint32_t output_difference_bit = ( delta_y >> bit_index ) & 1u;
				const std::uint32_t constant_bit = ( constant >> bit_index ) & 1u;

				std::uint64_t next_counts_by_carry_pair_state[ 4 ] = { 0ull, 0ull, 0ull, 0ull };
				for ( int state = 0; state < 4; ++state )
				{
					const std::uint64_t state_count = counts_by_carry_pair_state[ state ];
					if ( state_count == 0ull )
						continue;

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

						const int next_state = int( next_carry0 | ( next_carry1 << 1 ) );
						next_counts_by_carry_pair_state[ next_state ] += state_count;
					}
				}

				counts_by_carry_pair_state[ 0 ] = next_counts_by_carry_pair_state[ 0 ];
				counts_by_carry_pair_state[ 1 ] = next_counts_by_carry_pair_state[ 1 ];
				counts_by_carry_pair_state[ 2 ] = next_counts_by_carry_pair_state[ 2 ];
				counts_by_carry_pair_state[ 3 ] = next_counts_by_carry_pair_state[ 3 ];
			}

			return counts_by_carry_pair_state[ 0 ] + counts_by_carry_pair_state[ 1 ] + counts_by_carry_pair_state[ 2 ] + counts_by_carry_pair_state[ 3 ];
		}

		/**
		 * @brief 精確 DP（double）
		 */
		inline double diff_addconst_exact_probability_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint64_t count = diff_addconst_exact_count_n( delta_x, constant, delta_y, n );
			if ( count == 0ull )
				return 0.0;
			const double denominator = std::ldexp( 1.0, n );  // 2^n (exact for n<=53)
			return static_cast<double>( count ) / denominator;
		}

		/**
		 * @brief 精確 differential weight（double）：w = -log2(DP)
		 */
		inline double diff_addconst_exact_weight_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint64_t count = diff_addconst_exact_count_n( delta_x, constant, delta_y, n );
			if ( count == 0ull )
				return std::numeric_limits<double>::infinity();
			// DP = count / 2^n  =>  w = -log2(DP) = n - log2(count)
			return static_cast<double>( n ) - std::log2( static_cast<double>( count ) );
		}

		/**
		 * @brief 精確 differential weight（整數，上取整）：w_int = ceil(-log2(DP))
		 *
		 * 這個版本是給「只吃整數權重」的 branch-and-bound / trail search 用的。
		 *
		 * 核心等式（避免浮點；也避免「double 很難精確表示 log2(count)」的工程坑）：
		 *   DP = count / 2^n
		 *   w  = -log2(DP) = n - log2(count)
		 *   ceil(w) = n - floor(log2(count))                 (count > 0)
		 *
		 * 小證明（為什麼上式成立）：
		 * - 令 count = 2^k * t，其中 k = floor(log2(count))，且 t ∈ [1,2)。
		 * - 則 w = n - log2(count) = n - (k + log2(t))，其中 log2(t) ∈ [0,1)。
		 * - 因此 ceil(w) = n - k。
		 *
		 * 工程含義：
		 * - 這個整數權重是「精確 weight 的上界（上取整）」；
		 *   用於 BnB/trail search 的剪枝時不會低估（安全）。
		 *
		 * 出處/思路：
		 * - 計數 count 本身由 `diff_addconst_exact_count_n` 的 carry-pair 逐位 DP 得到，
		 *   可對照 Machado ePrint 2001/052（亦等價於 Azimi DCC 2022 Theorem 2 的逐位遞推）。
		 *
		 * @return -1 表示不可能（count=0），否則回傳 ceil(weight) 的整數值。
		 */
		inline int diff_addconst_exact_weight_ceil_int_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint64_t count = diff_addconst_exact_count_n( delta_x, constant, delta_y, n );
			if ( count == 0ull )
				return -1;

			// floor(log2(count)) for count>0
			const int floor_log2_count = static_cast<int>( std::bit_width( count ) ) - 1;
			const int w_int = n - floor_log2_count;	 // equals ceil(exact_weight)
			return ( w_int < 0 ) ? 0 : w_int;
		}

		/**
		 * @brief 精確 differential weight（不做 Qκ 截斷）：用論文 Lemma 3/4/5 的閉式計算 Σ log2(pi)
		 *
		 * 參考：
		 * - Azimi et al., DCC 2022 / ASIACRYPT 2020 extended，Lemma 4（ϕ_i = p_i/2^{λ_i-1}）與
		 *   Eq.(3)(5)（weight = HW((u⊕v)<<1) + Σ(λ_i-1) - Σ log2(p_i)）。
		 *
		 * 注意：
		 * - 本函式回傳的是實數（double）權重；與 `diff_addconst_exact_weight_n` 理論上應一致（僅有浮點誤差）。
		 */
		inline double diff_addconst_weight_log2pi_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			using namespace bitvector;

			// ------------------------------------------------------------
			// Azimi (ASIACRYPT 2020 / DCC 2022) 的閉式拆解（不做 Qκ 截斷）：
			//
			//   weight^a(u,v) = HW(((u ⊕ v) << 1)) + Σ_{i∈I}(λ_i-1) - Σ_{i∈I} log2(π_i)
			//
			// 其中：
			// - 狀態：S_i = (u[i-1], v[i-1], u[i] ⊕ v[i])
			// - I = { 1 ≤ i ≤ n-1 | S_i = 11* 且 λ_i > 1 }
			// - λ_i：鏈長（chain length），可由 s000' 的 run 長度得到（Lemma 5）
			// - π_i：Lemma 4 定義的正整數，使得對 i∈I 有 ϕ_i = π_i / 2^{λ_i-1}
			//
			// 工程化提醒：
			// - 這裡的目標是「精確 weight」，因此直接用 double 計算 log2(π_i)。
			// - 若你要 SMT/bit-vector 友善的近似，請用 BvWeight^κ（見下方 diff_addconst_bvweight_fixed_point_n）。
			// ------------------------------------------------------------
			if ( n <= 0 )
				return 0.0;
			if ( !is_diff_addconst_possible_n( delta_x, constant, delta_y, n ) )
				return std::numeric_limits<double>::infinity();

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			const std::uint32_t input_difference = delta_x & mask;
			const std::uint32_t output_difference = delta_y & mask;
			const std::uint32_t additive_constant = constant & mask;

			// Lemma 3:  - Σ_{i∉I} log2(ϕ_i)  = HW(((u ⊕ v) << 1))
			const std::uint32_t hamming_weight_of_u_xor_v_shifted_one = HammingWeight( ( ( input_difference ^ output_difference ) << 1 ) & mask );

			// Lemma 5: s000 與 s000'
			//
			// 論文定義（直覺版）：
			// - 在狀態序列 S_i 中，某些位置會形成 carry chain（鏈）。
			// - s000 用來找出「狀態為 000 的位置」；s000' 則把 prefix set 𝒫 排除後，
			//   只留下真正貢獻鏈長的區段。
			// - 並且有：Σ_{i∈I}(λ_i-1) = HW(s000')（Lemma 5）
			const std::uint32_t s000 = ( ~( input_difference << 1 ) & ~( output_difference << 1 ) ) & mask;
			const std::uint32_t s000_prime = s000 & ~LeadingZeros_n( ( ~s000 ) & mask, n );
			const std::uint32_t sum_chain_lengths_minus_one = HammingWeight( s000_prime );

			long double	  sum_log2_pi = 0.0L;
			std::uint32_t remaining_chain_mask = s000_prime;
			while ( remaining_chain_mask != 0u )
			{
				// s000' 中每一段連續 1 的 run 對應一條鏈（Lemma 5）：
				// - run_length = λ_i - 1
				// - run 的 MSB index = i-1  =>  i = run_msb + 1
				//
				// 工程注意：
				// - 我們用「從 MSB 向下掃到 run 的 LSB」的方式抽取每條 run，避免 O(n^2)。
				const int run_most_significant_bit_index = 31 - CountLeftZeros( remaining_chain_mask );
				int		  run_least_significant_bit_index = run_most_significant_bit_index;
				while ( run_least_significant_bit_index > 0 && ( ( remaining_chain_mask >> ( run_least_significant_bit_index - 1 ) ) & 1u ) == 1u )
				{
					--run_least_significant_bit_index;
				}

				const int run_length = run_most_significant_bit_index - run_least_significant_bit_index + 1;
				const int state_index_i = run_most_significant_bit_index + 1;
				const int carry_chain_length_lambda_i = run_length + 1;

				const std::uint32_t run_mask = ( ( 1u << run_length ) - 1u ) << run_least_significant_bit_index;
				remaining_chain_mask &= ~run_mask;

				// i ∈ [1, n-1] 才是論文的有效索引；其他（例如 prefix set 𝒫）直接略過。
				if ( state_index_i <= 0 || state_index_i >= n )
					continue;

				// Lemma 4 的 π_i（這是整個模型最容易「看錯位」的地方，所以這裡寫死對照）：
				//
				// 記：
				//   λ = λ_i
				//   i = state_index_i
				//   a[...] 是常量 a 的 bit（bit0=LSB），且對所有負索引視為 0。
				//
				// 論文把一段常量 bits 視為整數（低位在右）：
				//   a[i-2, i-λ]  表示 bits: a[i-2] ... a[i-λ]
				// 我們在程式中把它 pack 成：
				//   constant_window_value = Σ_{j=0..λ-2} a[i-λ+j] * 2^j
				// 再加上額外位 a[i-λ-1]（仍按論文定義是 +1bit，不是 shift）：
				//   t = constant_window_value + a[i-λ-1]
				//
				// 最後：
				//   if (u[i] ⊕ v[i] ⊕ a[i-1]) == 1:  π_i = t
				//   else                           :  π_i = 2^{λ-1} - t
				std::uint32_t constant_window_value = 0;
				for ( int bit_offset = 0; bit_offset <= carry_chain_length_lambda_i - 2; ++bit_offset )
				{
					const int			bit_index = ( state_index_i - carry_chain_length_lambda_i ) + bit_offset;
					const std::uint32_t bit = ( bit_index >= 0 && bit_index < n ) ? ( ( additive_constant >> bit_index ) & 1u ) : 0u;
					constant_window_value |= bit << bit_offset;
				}
				const int			extra_bit_index = state_index_i - carry_chain_length_lambda_i - 1;
				const std::uint32_t extra_bit = ( extra_bit_index >= 0 && extra_bit_index < n ) ? ( ( additive_constant >> extra_bit_index ) & 1u ) : 0u;
				const std::uint32_t sum_value = constant_window_value + extra_bit;

				const std::uint32_t condition_bit = ( ( ( input_difference >> state_index_i ) ^ ( output_difference >> state_index_i ) ^ ( additive_constant >> ( state_index_i - 1 ) ) ) & 1u );
				const std::uint32_t denominator = 1u << ( carry_chain_length_lambda_i - 1 );  // 2^{λ_i-1}
				const std::uint32_t pi_value = ( condition_bit == 1u ) ? sum_value : ( denominator - sum_value );
				if ( pi_value == 0u )
					return std::numeric_limits<double>::infinity();

				sum_log2_pi += std::log2( static_cast<long double>( pi_value ) );
			}

			const long double exact_weight = static_cast<long double>( hamming_weight_of_u_xor_v_shifted_one + sum_chain_lengths_minus_one ) - sum_log2_pi;
			return static_cast<double>( exact_weight );
		}

		/**
		 * @brief Algorithm 1 (generalized): BvWeight^κ - 計算常量加法差分近似權重（Qκ fixed-point）
		 *
		 * 參考：
		 * - Azimi et al., DCC 2022 / ASIACRYPT 2020 extended
		 *   - Algorithm 1: BvWeight（κ=4）
		 *   - Section 3.3: apxlog2^κ 的一般化（κ 可調）
		 *
		 * 論文對照（κ=4，論文預設）：
		 * - Lemma 8：在足夠的 bit-width（避免溢位）下，
		 *     BvWeight(u,v,a) = 2^4 * apxweight^a(u,v)
		 *   其中 apxweight^a(u,v) 使用 Eq.(4) 的 apxlog2 / Truncate 近似。
		 * - Theorem 4：近似誤差
		 *     E = weight^a(u,v) - apxweight^a(u,v) = weight^a(u,v) - 2^{-4}*BvWeight(u,v,a)
		 *   有界：
		 *     -0.029*(n-1) ≤ E ≤ 0
		 *   因此 apxweight^a(u,v) 是 **精確 weight^a(u,v) 的上界**（通常略偏大）。
		 *
		 * 工程使用提醒（很重要）：
		 * - 由於 BvWeight/ apxweight 是 **上界**（可能高估 weight），
		 *   在「尋找最小 weight」的 BnB/搜尋中，不能把它當作安全的下界來做剪枝，
		 *   否則可能錯誤剪掉實際更優的分支。
		 * - 它更適合用於：
		 *   1) SMT/bit-vector 編碼（避免浮點）
		 *   2) 相似性/近似篩選（搭配閾值）
		 *   3) heuristic ordering（排序/啟發式），而非作為最終精確權重
		 *
		 * 本工程的實作策略：
		 * - 論文 Algorithm 1 以 ParallelLog/ParallelTrunc 等 primitives 構造 bit-vector expression。
		 * - 本檔為了可審計/可單測，改用「逐鏈」計算 π_i + 直接抽取 MSB 右側 κ bits 的方式，
		 *   但其數學意義仍然是 Eq.(4) / Lemma 4/5/7/8 所定義的同一個近似量。
		 *
		 * 注意：Theorem 4 的常數界是針對 κ=4 的 apxlog2；若 κ != 4，誤差界會改變。
		 *
		 * @param fraction_bit_count κ（小數位精度）。κ=4 對應論文/舊版 `diff_addconst_bvweight_q4_n`。
		 * @return BvWeight^κ(u,v,a)（低 κ bits 為小數位），不可能則回傳 0xFFFFFFFF。
		 */
		inline std::uint32_t diff_addconst_bvweight_fixed_point_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n, int fraction_bit_count ) noexcept
		{
			using namespace bitvector;

			// ------------------------------------------------------------
			// Azimi Algorithm 1 的工程化（可審計）實作：BvWeight^κ
			//
			// 目標：回傳 Qκ fixed-point 的近似權重：
			//   BvWeight^κ(u,v,a) ≈ 2^κ * weight^a(u,v)
			//
			// 對應論文（κ=4 時）：
			//   apxlog2(π_i) = floor(log2(π_i)) + Truncate(π_i[m-1,0]) / 16     (Eq.(4))
			//   BvWeight(u,v,a) = 16 * apxweight^a(u,v)
			//
			// 這裡做 κ 的一般化（論文在 error analysis 章節亦給出 apxlog2^κ 的定義）：
			// - 對每個 π_i，取 MSB 右邊的 κ 個 bits 當作 fraction（不足 κ 則右側補 0）
			// - 最後：
			//     BvWeight^κ = (int_part << κ) - Σ fraction_bits(π_i)
			//
			// 其中 int_part 仍然對應：
			//   int_part = HW(((u ⊕ v) << 1)) + HW(s000') - Σ floor(log2(π_i))
			//
			// 這個版本刻意「逐鏈」計算 π_i，讓每個中間量可被列印/單測核對。
			// ------------------------------------------------------------
			if ( n <= 0 )
				return 0u;
			if ( fraction_bit_count < 0 || fraction_bit_count > 24 )
				return 0xFFFFFFFFu;

			if ( !is_diff_addconst_possible_n( delta_x, constant, delta_y, n ) )
				return 0xFFFFFFFFu;

			const std::uint32_t mask = ( n >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << n ) - 1u );
			const std::uint32_t input_difference = delta_x & mask;
			const std::uint32_t output_difference = delta_y & mask;
			const std::uint32_t additive_constant = constant & mask;

			// Lemma 5: s000 與 s000'（排除 prefix set 𝒫）
			const std::uint32_t s000 = ( ~( input_difference << 1 ) & ~( output_difference << 1 ) ) & mask;
			const std::uint32_t s000_prime = s000 & ~LeadingZeros_n( ( ~s000 ) & mask, n );

			// Lemma 3:  -Σ_{i∉I} log2(ϕ_i) = HW(((u ⊕ v) << 1))
			const std::uint32_t hamming_weight_of_u_xor_v_shifted_one = HammingWeight( ( ( input_difference ^ output_difference ) << 1 ) & mask );
			// Lemma 5: Σ_{i∈I}(λ_i-1) = HW(s000')
			const std::uint32_t sum_chain_lengths_minus_one = HammingWeight( s000_prime );

			// Σ floor(log2(π_i)) 與 Σ fraction_bits(π_i)
			//
			// 對照 Azimi：
			// - floor(log2(π_i)) 對應 apxlog2 的整數部分
			// - fraction_bits(π_i) 對應 Eq.(4) 的 Truncate(...)（κ bits）
			//
			// 工程化實作：
			// - floor(log2(π_i)) 直接用 msb index（31-clz）取得
			// - fraction_bits 用「緊貼 MSB 右側」取 κ bits，不足補 0（與論文一致）
			std::uint32_t sum_floor_log2_pi = 0;
			std::uint64_t sum_fraction_bits_pi = 0;

			std::uint32_t remaining_chain_mask = s000_prime;
			while ( remaining_chain_mask != 0u )
			{
				// 逐條鏈處理：s000' 的每段連續 1 表示一條鏈（run_length = λ_i-1）
				const int run_most_significant_bit_index = 31 - CountLeftZeros( remaining_chain_mask );
				int		  run_least_significant_bit_index = run_most_significant_bit_index;
				while ( run_least_significant_bit_index > 0 && ( ( remaining_chain_mask >> ( run_least_significant_bit_index - 1 ) ) & 1u ) == 1u )
				{
					--run_least_significant_bit_index;
				}

				const int run_length = run_most_significant_bit_index - run_least_significant_bit_index + 1;
				const int state_index_i = run_most_significant_bit_index + 1;
				const int carry_chain_length_lambda_i = run_length + 1;

				const std::uint32_t run_mask = ( ( 1u << run_length ) - 1u ) << run_least_significant_bit_index;
				remaining_chain_mask &= ~run_mask;

				if ( state_index_i <= 0 || state_index_i >= n )
					continue;

				std::uint32_t constant_window_value = 0;
				for ( int bit_offset = 0; bit_offset <= carry_chain_length_lambda_i - 2; ++bit_offset )
				{
					const int			bit_index = ( state_index_i - carry_chain_length_lambda_i ) + bit_offset;
					const std::uint32_t bit = ( bit_index >= 0 && bit_index < n ) ? ( ( additive_constant >> bit_index ) & 1u ) : 0u;
					constant_window_value |= bit << bit_offset;
				}
				const int			extra_bit_index = state_index_i - carry_chain_length_lambda_i - 1;
				const std::uint32_t extra_bit = ( extra_bit_index >= 0 && extra_bit_index < n ) ? ( ( additive_constant >> extra_bit_index ) & 1u ) : 0u;
				const std::uint32_t sum_value = constant_window_value + extra_bit;

				const std::uint32_t condition_bit = ( ( ( input_difference >> state_index_i ) ^ ( output_difference >> state_index_i ) ^ ( additive_constant >> ( state_index_i - 1 ) ) ) & 1u );
				const std::uint32_t denominator = 1u << ( carry_chain_length_lambda_i - 1 );  // 2^{λ_i-1}
				const std::uint32_t pi_value = ( condition_bit == 1u ) ? sum_value : ( denominator - sum_value );
				if ( pi_value == 0u )
					return 0xFFFFFFFFu;

				const int floor_log2_pi = 31 - CountLeftZeros( pi_value );
				sum_floor_log2_pi += std::uint32_t( floor_log2_pi );

				// apxlog2^κ: take κ bits right to MSB (pad with zeros if m<κ)
				//
				// 具體而言：
				// - 若 floor_log2_pi = m（π 的 MSB index），則 MSB 右側第一個 bit 的 index 是 m-1。
				// - 取 bits: (m-1), (m-2), ... (m-κ)，不足（<0）視為 0。
				std::uint32_t fraction_bits = 0;
				for ( int k = 0; k < fraction_bit_count; ++k )
				{
					const int			bit_index = floor_log2_pi - 1 - k;
					const std::uint32_t bit = ( bit_index >= 0 ) ? ( ( pi_value >> bit_index ) & 1u ) : 0u;
					fraction_bits = ( fraction_bits << 1 ) | bit;
				}
				sum_fraction_bits_pi += static_cast<std::uint64_t>( fraction_bits );
			}

			const std::uint32_t int_part = ( hamming_weight_of_u_xor_v_shifted_one + sum_chain_lengths_minus_one ) - sum_floor_log2_pi;

			const std::uint64_t scaled_int_part = ( static_cast<std::uint64_t>( int_part ) << fraction_bit_count );
			if ( scaled_int_part < sum_fraction_bits_pi )
				return 0u;
			const std::uint64_t result = scaled_int_part - sum_fraction_bits_pi;
			return ( result > 0xFFFFFFFFull ) ? 0xFFFFFFFFu : static_cast<std::uint32_t>( result );
		}

		/**
		 * @brief Algorithm 1: BvWeight - 計算常量加法差分權重（Q4 fixed-point output）
		 *
		 * 參考：Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
		 * Algorithm 1 (BvWeight)。
		 *
		 * 輸入/輸出對應：
		 * - delta_x = u：輸入 XOR 差分（Δx）
		 * - delta_y = v：輸出 XOR 差分（Δy），其中 y = x ⊞ a (mod 2^n)
		 * - constant = a：常量加數
		 *
		 * 近似量的定義（論文 Section 3.2, Eq.(3)(4) 與 Algorithm 1）：
		 * - 精確差分權重：weight^a(u,v) = -log2(Pr[u -> v])，一般為無理數
		 * - 論文用 4 個 fraction bits 近似 log2(pi)：
		 *     apxlog2(pi) = floor(log2(pi)) + Truncate(pi[m-1,0]) / 16
		 *   其中 Truncate 取的是「MSB 後的 4 個 bit」（Eq.(4)）
		 * - BvWeight(u,v,a) 以 Q4 fixed-point 表示 apxweight^a(u,v)：
		 *     BvWeight = 16 * apxweight^a(u,v)
		 *
		 * 論文誤差界（κ=4）：
		 * - Theorem 4：E = weight^a(u,v) - apxweight^a(u,v) 滿足 -0.029*(n-1) ≤ E ≤ 0，
		 *   因此 apxweight（也就是 BvWeight/16）是 **精確 weight 的上界**（略偏大）。
		 *   這也是你在做「相似性判定」時需要用閾值的原因：它不是精確值。
		 *
		 * 工程化實作策略：
		 * - 論文的 Algorithm 1 目標是「可用 bit-vector primitives 表達」；
		 *   本工程在固定 32-bit 搜尋時，直接按照 Lemma 4/5 的定義逐鏈計算 πᵢ，
		 *   讓每個中間量可列印/可單測，並避免 OCR 對符號（如  / ）的歧義。
		 * - 我們仍然輸出同一個近似量（BvWeight），因此可用於「相似性/近似剪枝」：
		 *   若要與精確權重比對，請先用枚舉/DP 算精確 Pr，再看 |apxweight - exact_weight| 是否小於閾值。
		 *
		 * @return BvWeight(u,v,a) in Q4 (low 4 bits are fraction), or 0xFFFFFFFF for impossible.
		 */
		inline std::uint32_t diff_addconst_bvweight_q4_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			// κ=4 的便捷封裝：等價於 Azimi Algorithm 1 的原始設定（Q4 fixed-point）。
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, n, 4 );
		}

		/**
		 * @brief 32-bit convenience wrapper returning Q4 bvweight.
		 */
		inline std::uint32_t diff_addconst_bvweight_q4( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 32-bit convenience wrapper: exact count.
		 */
		inline std::uint64_t diff_addconst_exact_count( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_count_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 32-bit convenience wrapper: exact probability.
		 */
		inline double diff_addconst_exact_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_probability_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 32-bit convenience wrapper: exact weight.
		 */
		inline double diff_addconst_exact_weight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 32-bit convenience wrapper: exact weight from Lemma 3/4/5 (log2(pi)).
		 */
		inline double diff_addconst_weight_log2pi( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_weight_log2pi_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 32-bit convenience wrapper returning Qκ bvweight.
		 */
		inline std::uint32_t diff_addconst_bvweight_fixed_point( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int fraction_bit_count ) noexcept
		{
			return diff_addconst_bvweight_fixed_point_n( delta_x, constant, delta_y, 32, fraction_bit_count );
		}

		/**
		 * @brief 精確 weight（整數，上取整）：給現有搜尋框架的「整數權重」介面
		 * 
		 * 重要：為了不去大改你的搜尋框架（它目前吃 `int` 權重），
		 * 我們在這個函式回傳 **精確 weight 的上取整**（不低估），而不是論文 Q4 的 BvWeight。
		 * 
		 * 出處/對照：
		 * - count/DP 的逐位 DP：Machado, IACR ePrint 2001/052（也可視為 Azimi DCC 2022 Theorem 2 的等價形式）。
		 * - 若你要 Azimi 的位向量近似（Algorithm 1 / Q4），請改用：
		 *     `diff_addconst_bvweight_q4_n`（Q4 fixed-point）
		 *     `diff_addconst_bvweight_fixed_point_n`（Qκ fixed-point）
		 *     `diff_addconst_bvweight_q4_int_ceil`（Q4→int 的對照封裝）
		 *
		 * 若你要做「相似性判定/閾值」比較，請用 Q4（或 double 精確 weight）版本，不要用這個整數上取整版本。
		 * 
		 * @param delta_x 輸入差分
		 * @param constant 常量K
		 * @param delta_y 輸出差分
		 * @return 整數權重（精確 weight 的上取整），0表示權重為0，-1表示不可能
		 */
		inline int diff_addconst_bvweight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_addconst_exact_weight_ceil_int_n( delta_x, constant, delta_y, 32 );
		}

		/**
		 * @brief 對照/演示：把 Azimi Algorithm 1 的 Q4 BvWeight 轉成整數（ceil）
		 *
		 * - 本函式保留給「對照實驗」或「近似剪枝」用途（例如：你想檢驗 BVWeight 與精確 weight 的相似性）。
		 * - 不建議在你目前的 differential trail 搜尋裡當作最終權重（它是近似的）。
		 *
		 * 數學含義：
		 * - Q4 的 BvWeight 代表：BvWeight ≈ 16 * apxweight
		 * - 這裡做 ceil(BvWeight / 16) 只是把 Q4 近似權重轉成整數（仍然是近似，不保證等於 ceil(exact_weight)）。
		 *
		 * Ref:
		 * - Azimi et al., "A Bit-Vector Differential Model for the Modular Addition by a Constant"
		 *   (ASIACRYPT 2020 / DCC 2022 extended), Algorithm 1 (BvWeight, κ=4).
		 */
		inline int diff_addconst_bvweight_q4_int_ceil( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return -1;
			if ( bvweight_q4 == 0u )
				return 0;
			// Convert Q4 -> integer weight (round up).
			return int( ( bvweight_q4 + 15u ) >> 4 );
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
		 * @brief 精確 DP: subtraction-by-constant（透過 x ⊟ c == x ⊞ (-c mod 2^n) 轉換）
		 */
		inline std::uint64_t diff_subconst_exact_count_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_count_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_probability_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_probability_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_exact_weight_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_exact_weight_n( delta_x, neg_constant, delta_y, n );
		}

		inline double diff_subconst_weight_log2pi_n( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y, int n ) noexcept
		{
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<std::uint32_t>( constant, n );
			return diff_addconst_weight_log2pi_n( delta_x, neg_constant, delta_y, n );
		}

		inline std::uint64_t diff_subconst_exact_count( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_count_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_exact_probability( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_probability_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_exact_weight( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_exact_weight_n( delta_x, constant, delta_y, 32 );
		}

		inline double diff_subconst_weight_log2pi( std::uint32_t delta_x, std::uint32_t constant, std::uint32_t delta_y ) noexcept
		{
			return diff_subconst_weight_log2pi_n( delta_x, constant, delta_y, 32 );
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
			// 工程約定：
			// - 這裡回傳的是「由 BvWeight 近似權重換算」得到的近似 DP
			// - 如果你要精確 DP/weight，請用：
			//     diff_addconst_exact_probability(_n) / diff_addconst_exact_weight(_n)
			const std::uint32_t bvweight_q4 = diff_addconst_bvweight_q4_n( delta_x, constant, delta_y, 32 );
			if ( bvweight_q4 == 0xFFFFFFFFu )
				return 0.0;
			return std::pow( 2.0, -static_cast<double>( bvweight_q4 ) / 16.0 );
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
			// X - C = X + (-C) (mod 2^n)
			const std::uint32_t neg_constant = TwilightDream::arx_operators::neg_mod_2n<uint32_t>( constant, 32 );
			return diff_addconst_probability( delta_x, neg_constant, delta_y );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
