#pragma once
/**
 * @file linear_correlation/constant_weight_evaluation.hpp
 * @brief Modular-addition linear correlation — var-const side only
 *
 * 这份档案只处理：
 *
 *     z = x ⊞ a
 *     z = x ⊟ a
 *
 * 也就是「一边是变量，一边是常量」的模加 / 模减线性相关度。
 *
 * -----------------------------------------------------------------------------
 * 0) 本档要算的数学对象
 * -----------------------------------------------------------------------------
 *
 * 对於加常量：
 *
 *     corr = E_x[ (-1)^{ α·x ⊕ β·z } ],     z = x ⊞ a
 *
 * 对於减常量：
 *
 *     corr = E_x[ (-1)^{ α·x ⊕ β·z } ],     z = x ⊟ a
 *
 * 注意：
 * - 这里只有 x 是随机变量
 * - a / constant 是固定常量
 * - 所以逐 bit 局部平均时，每一位只需平均 2 种情况，而不是 4 种
 *
 * 这就是为什麽 var-const 的 per-bit local matrix 系数是：
 *
 *     1/2
 *
 * 而不是 var-var 的：
 *
 *     1/4
 *
 * -----------------------------------------------------------------------------
 * 1) 工程实作的两条路
 * -----------------------------------------------------------------------------
 *
 * A. exact sequential chain
 *
 *     v_0 = [1,0]
 *     v_{i+1} = v_i · M_i
 *     corr = sum(v_n)
 *
 * 这是 reference-correct / O(n) 的版本。
 *
 * B. exact run-event evaluator
 *
 *     corr = N / 2^n
 *
 * where the exact numerator `N` is built from the same per-bit 2x2 operator,
 * but regrouped into exact events:
 * - constant-run / beta=0 intervals collapse to commuting macro steps
 * - beta=1 positions stay as singleton synchronization events
 *
 * Public API names keep the historical `*_logdepth` spelling, but the optimized
 * implementation here is the event-semilattice / exact-continuation-friendly Q1 kernel.
 *
 * -----------------------------------------------------------------------------
 * 2) 本档不再承担的事
 * -----------------------------------------------------------------------------
 *
 * - 不再承担 var-var
 * - 不再同时放 x⊞y 的公共 wrapper
 * - 不再让档案职责和 `linear_correlation_add_logn_refactored_verbose.hpp` 混住
 *
 */

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "../SignedInteger128Bit.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;


#ifndef TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED
#define TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED

		/**
		 * @brief Get i-th bit (LSB = bit 0)
		 *
		 * 这是整个 carry-state 框架最基本的 bit 编号约定：
		 * - bit 0 = LSB
		 * - carry 由低位往高位传
		 *
		 * 只要这个约定不动，後面的 2×2 matrix、逐 bit 累乘、two's complement
		 * 重写减法，都能保持一致。
		 */
		static inline int bit_u64( std::uint64_t value, int bit_index ) noexcept
		{
			return static_cast<int>( ( value >> bit_index ) & 1ULL );
		}

		[[nodiscard]] static inline std::uint64_t addconst_event_lowbit_u64( std::uint64_t value ) noexcept
		{
			return value & ( ~value + 1ULL );
		}

		/**
		 * @brief 1-bit full-adder carry out:
		 *        cout = MAJ(x, y, cin)
		 */
		static inline int carry_out_bit( int x, int y, int carry_in ) noexcept
		{
			return ( x & y ) | ( x & carry_in ) | ( y & carry_in );
		}

		/**
		 * @brief Compute (-1)^(mask_bit * value_bit)
		 */
		static inline double signed_bit( int mask_bit, int value_bit ) noexcept
		{
			return mask_bit ? ( value_bit ? -1.0 : 1.0 ) : 1.0;
		}

		/**
		 * @brief Convert correlation to linear weight.
		 */
		static inline double weight_from_linear_correlation( double correlation ) noexcept
		{
			const double absolute_correlation = std::fabs( correlation );
			if ( absolute_correlation <= 0.0 )
			{
				return std::numeric_limits<double>::infinity();
			}
			return -std::log2( absolute_correlation );
		}

		/**
		 * @brief 2×2 matrix with row-vector left multiplication.
		 *
		 * Rows    : carry-in  ∈ {0,1}
		 * Columns : carry-out ∈ {0,1}
		 */
		/**
		 * @brief 2×2 carry-state transfer matrix for one bit.
		 *
		 * 这里的 2×2 不是装饰，它正好对应 full-adder 只有两个 carry 状态：
		 *
		 *     carry-in  ∈ {0,1}
		 *     carry-out ∈ {0,1}
		 *
		 * 因此每一位的局部演化都能写成：
		 *
		 *     M_i(cin, cout)
		 *
		 * 全部 bit 串起来就是：
		 *
		 *     v_{i+1} = v_i · M_i
		 *
		 * 其中 v_i 是长度 2 的 row vector，表示到 bit i 为止、对应两个 carry 状态的
		 * 符号平均累积值。
		 */
		struct Matrix2D
		{
			double m00;
			double m01;
			double m10;
			double m11;

			constexpr Matrix2D() noexcept
				: m00( 0.0 ), m01( 0.0 ), m10( 0.0 ), m11( 0.0 )
			{
			}

			constexpr Matrix2D( double a, double b, double c, double d ) noexcept
				: m00( a ), m01( b ), m10( c ), m11( d )
			{
			}

			static inline void multiply_row( double row0, double row1, const Matrix2D& matrix, double& out0, double& out1 ) noexcept
			{
				out0 = row0 * matrix.m00 + row1 * matrix.m10;
				out1 = row0 * matrix.m01 + row1 * matrix.m11;
			}

			[[nodiscard]] static inline Matrix2D multiply_mm( const Matrix2D& left, const Matrix2D& right ) noexcept
			{
				return Matrix2D {
					left.m00 * right.m00 + left.m01 * right.m10,
					left.m00 * right.m01 + left.m01 * right.m11,
					left.m10 * right.m00 + left.m11 * right.m10,
					left.m10 * right.m01 + left.m11 * right.m11 };
			}
		};

		struct EventRunInfo
		{
			int			 start { 0 };
			int			 length { 0 };
			int			 bit { 0 };
			std::uint64_t rel_mask { 0 };
			std::uint64_t abs_mask { 0 };
		};

		struct EventRunTable
		{
			std::uint64_t		 constant { 0 };
			int				 n { 0 };
			int				 run_count { 0 };
			std::array<EventRunInfo, 64> runs {};
			std::array<std::uint8_t, 64> bit_to_run {};
		};

		[[nodiscard]] static inline EventRunTable build_event_run_table( std::uint64_t constant, int n ) noexcept
		{
			EventRunTable table {};
			if ( n <= 0 )
			{
				return table;
			}
			if ( n > 64 )
			{
				n = 64;
			}

			const std::uint64_t masked_constant =
				( n == 64 ) ? constant : ( constant & ( ( 1ULL << n ) - 1ULL ) );
			table.constant = masked_constant;
			table.n = n;

			for ( int start = 0; start < n; )
			{
				const int run_bit = bit_u64( masked_constant, start );
				int		  end = start;
				while ( ( end + 1 ) < n && bit_u64( masked_constant, end + 1 ) == run_bit )
				{
					++end;
				}

				const int run_length = end - start + 1;
				const std::uint64_t rel_mask =
					( run_length >= 64 ) ? ~0ULL : ( ( 1ULL << run_length ) - 1ULL );
				const std::uint64_t abs_mask =
					( run_length >= 64 ) ? ~0ULL : ( rel_mask << start );

				table.runs[ table.run_count ] =
					EventRunInfo { start, run_length, run_bit, rel_mask, abs_mask };
				for ( int bit = start; bit <= end; ++bit )
				{
					table.bit_to_run[ static_cast<std::size_t>( bit ) ] =
						static_cast<std::uint8_t>( table.run_count );
				}
				++table.run_count;
				start = end + 1;
			}

			return table;
		}

		struct EventRunCacheSlot
		{
			bool		  valid { false };
			std::uint64_t constant { 0 };
			int			  n { 0 };
			EventRunTable table {};
		};

		[[nodiscard]] static inline const EventRunTable& get_event_run_table_cached( std::uint64_t constant, int n ) noexcept
		{
			static const EventRunTable empty_table {};
			if ( n <= 0 )
			{
				return empty_table;
			}
			if ( n > 64 )
			{
				n = 64;
			}

			const std::uint64_t masked_constant =
				( n == 64 ) ? constant : ( constant & ( ( 1ULL << n ) - 1ULL ) );

			thread_local EventRunCacheSlot cache {};
			if ( !cache.valid || cache.constant != masked_constant || cache.n != n )
			{
				cache.table = build_event_run_table( masked_constant, n );
				cache.constant = masked_constant;
				cache.n = n;
				cache.valid = true;
			}
			return cache.table;
		}

		struct ExactEventRow
		{
			SignedInteger128Bit v0 { 1 };
			SignedInteger128Bit v1 { 0 };
			int				 scale { 0 };
		};

		static inline void apply_0run_t0_pow( ExactEventRow& row, int count ) noexcept
		{
			if ( count <= 0 )
			{
				return;
			}

			const SignedInteger128Bit v0 = row.v0;
			const SignedInteger128Bit v1 = row.v1;
			row.v0 = ( v0 + v1 ).shl( count ) - v1;
			row.v1 = v1;
			row.scale += count;
		}

		static inline void apply_0run_t1_pow( ExactEventRow& row, int count ) noexcept
		{
			if ( count <= 0 )
			{
				return;
			}

			const SignedInteger128Bit v1 = row.v1;
			if ( ( count & 1 ) != 0 )
			{
				row.v0 = v1;
				row.v1 = -v1;
			}
			else
			{
				row.v0 = -v1;
				row.v1 = v1;
			}
			row.scale += count;
		}

		static inline void apply_1run_t0_pow( ExactEventRow& row, int count ) noexcept
		{
			if ( count <= 0 )
			{
				return;
			}

			const SignedInteger128Bit v0 = row.v0;
			const SignedInteger128Bit v1 = row.v1;
			row.v0 = v0;
			row.v1 = ( v0 + v1 ).shl( count ) - v0;
			row.scale += count;
		}

		static inline void apply_1run_t1_pow( ExactEventRow& row, int count ) noexcept
		{
			if ( count <= 0 )
			{
				return;
			}

			const SignedInteger128Bit v0 = row.v0;
			row.v0 = v0;
			row.v1 = -v0;
			row.scale += count;
		}

		static inline void step_beta1_0run( ExactEventRow& row, int tbit ) noexcept
		{
			row.v1 = -row.v1;

			const SignedInteger128Bit v0 = row.v0;
			const SignedInteger128Bit v1 = row.v1;
			if ( tbit == 0 )
			{
				row.v0 = v0 + v0 + v1;
				row.v1 = v1;
			}
			else
			{
				row.v0 = v1;
				row.v1 = -v1;
			}
			++row.scale;
		}

		static inline void step_beta1_1run( ExactEventRow& row, int tbit ) noexcept
		{
			row.v1 = -row.v1;

			const SignedInteger128Bit v0 = row.v0;
			const SignedInteger128Bit v1 = row.v1;
			if ( tbit == 0 )
			{
				row.v0 = v0;
				row.v1 = v0 + v1 + v1;
			}
			else
			{
				row.v0 = v0;
				row.v1 = -v0;
			}
			++row.scale;
		}

		static inline void apply_run_counts( ExactEventRow& row, int run_bit, int t0_count, int t1_count ) noexcept
		{
			if ( run_bit == 0 )
			{
				apply_0run_t0_pow( row, t0_count );
				apply_0run_t1_pow( row, t1_count );
			}
			else
			{
				apply_1run_t0_pow( row, t0_count );
				apply_1run_t1_pow( row, t1_count );
			}
		}

		static inline void process_run_exact_span(
			ExactEventRow&	 row,
			int			 run_bit,
			std::uint64_t t_rel,
			std::uint64_t beta_rel,
			int			 run_length ) noexcept
		{
			int segment_start = 0;
			while ( true )
			{
				const int beta_pos = ( beta_rel != 0 ) ? static_cast<int>( std::countr_zero( beta_rel ) ) : run_length;
				const int segment_length = beta_pos - segment_start;
				if ( segment_length > 0 )
				{
					const std::uint64_t segment_mask =
						( segment_length >= 64 ) ? ~0ULL : ( ( 1ULL << segment_length ) - 1ULL );
					const int t1_count =
						static_cast<int>( std::popcount( ( t_rel >> segment_start ) & segment_mask ) );
					apply_run_counts( row, run_bit, segment_length - t1_count, t1_count );
				}

				if ( beta_rel == 0 )
				{
					break;
				}

				const int tbit = bit_u64( t_rel, beta_pos );
				if ( run_bit == 0 )
				{
					step_beta1_0run( row, tbit );
				}
				else
				{
					step_beta1_1run( row, tbit );
				}

				beta_rel &= ( beta_rel - 1 );
				segment_start = beta_pos + 1;
				if ( segment_start >= run_length )
				{
					break;
				}
			}
		}

#endif  // TWILIGHT_DREAM_LINEAR_CORRELATION_TRANSFER_MATRIX_CORE_DEFINED

#ifndef TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED
#define TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED

		/**
		 * @brief Result pair: exact correlation and linear weight.
		 */
		struct LinearCorrelation
		{
			double correlation { 0.0 };
			double weight { std::numeric_limits<double>::infinity() };

			constexpr LinearCorrelation() noexcept = default;
			constexpr LinearCorrelation( double correlation_value, double weight_value ) noexcept
				: correlation( correlation_value ), weight( weight_value )
			{
			}

			[[nodiscard]] inline bool is_feasible() const noexcept
			{
				return ( correlation != 0.0 ) && !std::isinf( weight );
			}
		};

		static inline LinearCorrelation make_linear_correlation( double correlation ) noexcept
		{
			return LinearCorrelation { correlation, weight_from_linear_correlation( correlation ) };
		}

#endif  // TWILIGHT_DREAM_LINEAR_CORRELATION_RESULT_DEFINED

		// =========================================================================
		// var-const local matrix
		// =========================================================================

		/**
		 * @brief Build the exact local 2×2 matrix for z = x ⊞ a at one bit.
		 *
		 * M_i(cin, cout)
		 *   = 1/2 * Σ_x 1_{carry(x,a_i,cin)=cout} (-1)^(α_i x ⊕ β_i z_i)
		 *
		 * Only x_i varies, so the averaging factor is 1/2.
		 */
		/**
		 * @brief Build the exact per-bit transfer matrix for z = x ⊞ a.
		 *
		 * 对单 bit i，有：
		 *
		 *     z_i = x_i ⊕ a_i ⊕ cin
		 *     cout = carry(x_i, a_i, cin)
		 *
		 * 所以局部矩阵 entry 是：
		 *
		 *     M_i(cin, cout)
		 *       = (1/2) * Σ_{x_i ∈ {0,1}}
		 *           1_{ carry(x_i,a_i,cin)=cout }
		 *           · (-1)^{ α_i x_i ⊕ β_i z_i }
		 *
		 * 为什麽是 1/2？
		 * 因为 var-const 每一位只有 x_i 这一个随机 bit；
		 * 常量位 a_i 已经固定，所以只平均两种情况。
		 *
		 * 这一点和 var-var 的 1/4 是本质区别，不是写法差异。
		 */
		static inline Matrix2D make_Mi_add_const_bit( int alpha_i, int constant_i, int beta_i ) noexcept
		{
			Matrix2D matrix;

			for ( int carry_in = 0; carry_in <= 1; ++carry_in )
			{
				for ( int carry_out = 0; carry_out <= 1; ++carry_out )
				{
					double sum = 0.0;

					for ( int x_i = 0; x_i <= 1; ++x_i )
					{
						if ( carry_out_bit( x_i, constant_i, carry_in ) != carry_out )
						{
							continue;
						}

						const int z_i = x_i ^ constant_i ^ carry_in;

						double sign = 1.0;
						sign *= signed_bit( alpha_i, x_i );
						sign *= signed_bit( beta_i, z_i );
						sum += sign;
					}

					const double value = sum * 0.5;

					if ( carry_in == 0 && carry_out == 0 ) matrix.m00 = value;
					if ( carry_in == 0 && carry_out == 1 ) matrix.m01 = value;
					if ( carry_in == 1 && carry_out == 0 ) matrix.m10 = value;
					if ( carry_in == 1 && carry_out == 1 ) matrix.m11 = value;
				}
			}

			return matrix;
		}

		// =========================================================================
		// var-const exact correlation — sequential O(n)
		// =========================================================================

		/**
		 * @brief Exact correlation for z = x ⊞ a.
		 *
		 * This is the reference-correct sequential chain:
		 *   v_{i+1} = v_i · M_i,  v_0 = [1, 0]
		 *   corr    = sum(v_n)
		 */
		/**
		 * @brief Exact sequential chain for z = x ⊞ a.
		 *
		 * 累乘公式：
		 *
		 *     v_0 = [1, 0]
		 *     v_{i+1} = v_i · M_i
		 *     corr = v_n[0] + v_n[1]
		 *
		 * 这里 `v_0 = [1,0]` 的含义是：
		 * - 在最低位之前，初始 carry 固定为 0
		 * - 不存在 carry-in = 1 的起始情况
		 *
		 * 最後把两个终止 carry 状态都加总，是因为最终 carry 没有限制。
		 */
		static inline double correlation_add_const_exact( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return 1.0;
			}

			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );

			alpha &= mask;
			beta &= mask;
			constant &= mask;

			double row0 = 1.0;
			double row1 = 0.0;

			for ( int bit_index = 0; bit_index < n; ++bit_index )
			{
				const Matrix2D matrix = make_Mi_add_const_bit(
					bit_u64( alpha, bit_index ),
					bit_u64( constant, bit_index ),
					bit_u64( beta, bit_index ) );

				double next_row0 = 0.0;
				double next_row1 = 0.0;
				Matrix2D::multiply_row( row0, row1, matrix, next_row0, next_row1 );
				row0 = next_row0;
				row1 = next_row1;
			}

			return row0 + row1;
		}

		/**
		 * @brief Backward-compatible alias.
		 */
		static inline double correlation_add_const( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			return correlation_add_const_exact( alpha, constant, beta, n );
		}

		/**
		 * @brief Exact correlation for z = x ⊟ a = x ⊞ (-a mod 2^n)
		 */
		/**
		 * @brief Exact sequential chain for z = x ⊟ a.
		 *
		 * 这里不重新发明 subtraction operator，而是直接用：
		 *
		 *     x ⊟ a = x ⊞ (-a mod 2^n)
		 *
		 * 其中：
		 *
		 *     -a mod 2^n = (~a + 1) mod 2^n
		 *
		 * 然後回调 `correlation_add_const_exact()`。
		 * 也就是说，减常量其实只是「加上 two's complement 常量」。
		 */
		static inline double correlation_sub_const_exact( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_constant = ( ( ~constant ) + 1ULL ) & mask;
			return correlation_add_const_exact( alpha, neg_constant, beta, n );
		}

		/**
		 * @brief Backward-compatible alias.
		 */
		static inline double correlation_sub_const( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			return correlation_sub_const_exact( alpha, constant, beta, n );
		}

		// =========================================================================
		// var-const exact correlation — event-driven exact numerator
		// =========================================================================

		/**
		 * @brief Exact numerator `numer = 2^n · corr` for z = x ⊞ a.
		 *
		 * The optimization comes from exact event regrouping, not from per-bit greedy
		 * choice and not from calling the flat solver:
		 * - inside a constant run, every maximal `beta=0` interval collapses to a small
		 *   exact macro family
		 * - every `beta=1` bit remains a singleton synchronization event
		 *
		 * This keeps the public Q1 entry name while making the implementation match the
		 * run-event semigroup that later one-sided Bellman / continuation code needs.
		 */
		[[nodiscard]] static inline SignedInteger128Bit correlation_add_const_exact_numerator_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return SignedInteger128Bit { 1 };
			}
			if ( n > 64 )
			{
				n = 64;
			}

			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			alpha &= mask;
			beta &= mask;
			constant &= mask;

			const EventRunTable& run_table = get_event_run_table_cached( constant, n );
			const int parity_bc = static_cast<int>( std::popcount( beta & run_table.constant ) & 1U );
			const std::uint64_t t = ( alpha ^ beta ) & mask;

			ExactEventRow row {};
			for ( int run_index = 0; run_index < run_table.run_count; ++run_index )
			{
				const EventRunInfo& run = run_table.runs[ static_cast<std::size_t>( run_index ) ];
				const std::uint64_t beta_rel = ( beta >> run.start ) & run.rel_mask;
				const std::uint64_t t_rel = ( t >> run.start ) & run.rel_mask;
				process_run_exact_span( row, run.bit, t_rel, beta_rel, run.length );
			}

			SignedInteger128Bit numerator = row.v0 + row.v1;
			if ( parity_bc != 0 )
			{
				numerator = -numerator;
			}
			return numerator;
		}

		/**
		 * @brief Decision-oriented ceil linear weight for z = x ⊞ a.
		 *
		 * If `corr = numer / 2^n` and `numer != 0`, then
		 *
		 *     ceil( -log2 |corr| ) = n - floor( log2 |numer| ).
		 *
		 * This avoids any floating-point conversion.
		 */
		[[nodiscard]] static inline SearchWeight correlation_add_const_weight_ceil_int_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return 0;
			}

			const SignedInteger128Bit numerator = correlation_add_const_exact_numerator_logdepth( alpha, constant, beta, n );
			if ( numerator.is_zero() )
			{
				return TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
			}

			return static_cast<SearchWeight>( n - ( numerator.bit_width_abs() - 1 ) );
		}

		/**
		 * @brief Exact zero/nonzero correlation decision for z = x ⊞ a.
		 */
		[[nodiscard]] static inline bool correlation_add_const_has_nonzero_correlation_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			return !correlation_add_const_exact_numerator_logdepth( alpha, constant, beta, n ).is_zero();
		}

		/**
		 * @brief Exact Q1 correlation for z = x ⊞ a.
		 *
		 * The public name is preserved, but the implementation is now the exact
		 * run-event evaluator described above.
		 */
		static inline double correlation_add_const_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			if ( n <= 0 )
			{
				return 1.0;
			}

			const SignedInteger128Bit numerator = correlation_add_const_exact_numerator_logdepth( alpha, constant, beta, n );
			return std::ldexp( numerator.to_double(), -n );
		}

		/**
		 * @brief Exact Q1 correlation for z = x ⊟ a.
		 */
		/**
		 * @brief Tree-reduced log-depth version for z = x ⊟ a.
		 *
		 * 同理：
		 *
		 *     x ⊟ a = x ⊞ (-a mod 2^n)
		 *
		 * 所以这里仍然只是先做 two's complement，再调用 add-const 的 log-depth 核心。
		 */
		static inline double correlation_sub_const_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_constant = ( ( ~constant ) + 1ULL ) & mask;
			return correlation_add_const_logdepth( alpha, neg_constant, beta, n );
		}

		/**
		 * @brief Exact numerator `numer = 2^n · corr` for z = x ⊟ a.
		 */
		[[nodiscard]] static inline SignedInteger128Bit correlation_sub_const_exact_numerator_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_constant = ( ( ~constant ) + 1ULL ) & mask;
			return correlation_add_const_exact_numerator_logdepth( alpha, neg_constant, beta, n );
		}

		/**
		 * @brief Decision-oriented ceil linear weight for z = x ⊟ a.
		 */
		[[nodiscard]] static inline SearchWeight correlation_sub_const_weight_ceil_int_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_constant = ( ( ~constant ) + 1ULL ) & mask;
			return correlation_add_const_weight_ceil_int_logdepth( alpha, neg_constant, beta, n );
		}

		/**
		 * @brief Exact zero/nonzero correlation decision for z = x ⊟ a.
		 */
		[[nodiscard]] static inline bool correlation_sub_const_has_nonzero_correlation_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int n ) noexcept
		{
			const std::uint64_t mask = ( n == 64 ) ? ~0ULL : ( ( 1ULL << n ) - 1ULL );
			const std::uint64_t neg_constant = ( ( ~constant ) + 1ULL ) & mask;
			return correlation_add_const_has_nonzero_correlation_logdepth( alpha, neg_constant, beta, n );
		}

		// =========================================================================
		// convenience wrappers — var-const only
		// =========================================================================

		static inline LinearCorrelation linear_x_modulo_plus_const32( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_add_const( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_minus_const32( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_sub_const( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_plus_const64( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_add_const( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_minus_const64( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_sub_const( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_plus_const32_logdepth( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_add_const_logdepth( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_minus_const32_logdepth( std::uint32_t alpha, std::uint32_t constant, std::uint32_t beta, int nbits = 32 ) noexcept
		{
			return make_linear_correlation( correlation_sub_const_logdepth( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_plus_const64_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_add_const_logdepth( alpha, constant, beta, nbits ) );
		}

		static inline LinearCorrelation linear_x_modulo_minus_const64_logdepth( std::uint64_t alpha, std::uint64_t constant, std::uint64_t beta, int nbits = 64 ) noexcept
		{
			return make_linear_correlation( correlation_sub_const_logdepth( alpha, constant, beta, nbits ) );
		}

	}  // namespace arx_operators
}  // namespace TwilightDream
