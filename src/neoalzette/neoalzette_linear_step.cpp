#include "neoalzette/neoalzette_linear_step.hpp"

namespace TwilightDream
{
	// ====== 小工具 ======
	static inline void mark_rc( std::uint16_t& mask, int idx )
	{
		mask |= ( 1u << idx );
	}
	static inline uint8_t dot_parity( std::uint32_t a, std::uint32_t b )
	{
		std::uint32_t x = a & b;
		x ^= x >> 16;
		x ^= x >> 8;
		x ^= x >> 4;
		x ^= x >> 2;
		x ^= x >> 1;
		return static_cast<uint8_t>( x & 1u );
	}

	// ============ T0/T1 的转置（来自 rotl(.,31) ^ rotl(.,17)） ============
	static inline std::uint32_t T_xy_transpose( std::uint32_t m ) noexcept
	{
		using NA = NeoAlzetteCore;
		return NA::rotr<std::uint32_t>( m, 31 ) ^ NA::rotr<std::uint32_t>( m, 17 );
	}

	// ====== 注入（来自 A）的转置回溯 —— 显式计入 R[7], R[8], R[9] 的“使用”与相位 ======
	// 前向：
	//   c1 = l1_forward(A ^ rc7);
	//   d1 = l2_forward(rotl(A,24) ^ rc8);
	//   t  = rotr(c1 ^ d1, 31);
	//   c2 = c1 ^ rotr(d1, 17);
	//   d2 = d1 ^ rotl(t, 16);
	//   B ^= rotl(c2,24) ^ rotl(d2,16) ^ rc9;
	//
	// 反向（线性掩码回溯）：给定 mB，累加到 mA。
	static inline void backprop_injection_from_A_analysis( std::uint32_t mB, std::uint32_t& mA, std::uint16_t& used, int& phase ) noexcept
	{
		using NA = NeoAlzetteCore;

		// 前向：B ^= rotl(c2,24) ^ rotl(d2,16) ^ rc9
		mark_rc( used, 9 );
		phase ^= dot_parity( mB, NA::ROUND_CONSTANTS[ 9 ] );

		// y1=rotl(c2,24), y2=rotl(d2,16)
		std::uint32_t m_c2 = NA::rotr<std::uint32_t>( mB, 24 );
		std::uint32_t m_d2 = NA::rotr<std::uint32_t>( mB, 16 );

		std::uint32_t m_c1 = 0, m_d1 = 0, m_t = 0;

		// d2 = d1 ^ rotl(t,16)
		m_d1 ^= m_d2;
		m_t ^= NA::rotr<std::uint32_t>( m_d2, 16 );

		// t = rotr(c1 ^ d1,31)
		std::uint32_t add = NA::rotl<std::uint32_t>( m_t, 31 );
		m_c1 ^= add;
		m_d1 ^= add;

		// c2 = c1 ^ rotr(d1,17)
		m_c1 ^= m_c2;
		m_d1 ^= NA::rotl<std::uint32_t>( m_c2, 17 );

		// 回到 A 的两支
		// c1 = L1(A ^ rc7)
		mark_rc( used, 7 );
		std::uint32_t mc_pre = NA::l1_transpose( m_c1 );
		phase ^= dot_parity( mc_pre, NA::ROUND_CONSTANTS[ 7 ] );

		// d1 = L2(rotl(A,24) ^ rc8)
		mark_rc( used, 8 );
		std::uint32_t md_pre = NA::l2_transpose( m_d1 );
		phase ^= dot_parity( md_pre, NA::ROUND_CONSTANTS[ 8 ] );
		md_pre = NA::rotr<std::uint32_t>( md_pre, 24 );

		mA ^= ( mc_pre ^ md_pre );
	}


	// ====== 注入（来自 B）的转置回溯 —— 显式计入 R[2], R[3], R[4] ======
	// 前向：
	//   c1 = l2_forward(B ^ rc2);
	//   d1 = l1_forward(rotr(B,3) ^ rc3);
	//   t  = rotl(c1 ^ d1, 31);
	//   c2 = c1 ^ rotl(d1, 17);
	//   d2 = d1 ^ rotr(t, 16);
	//   A ^= rotl(c2,24) ^ rotl(d2,16) ^ rc4;
	//
	// 反向：给定 mA，累加到 mB。
	static inline void backprop_injection_from_B_analysis( std::uint32_t mA, std::uint32_t& mB, std::uint16_t& used, int& phase ) noexcept
	{
		using NA = NeoAlzetteCore;

		// 前向：A ^= rotl(c2,24) ^ rotl(d2,16) ^ rc4
		mark_rc( used, 4 );
		phase ^= dot_parity( mA, NA::ROUND_CONSTANTS[ 4 ] );

		std::uint32_t m_c2 = NA::rotr<std::uint32_t>( mA, 24 );
		std::uint32_t m_d2 = NA::rotr<std::uint32_t>( mA, 16 );

		std::uint32_t m_c1 = 0, m_d1 = 0, m_t = 0;

		// d2 = d1 ^ rotr(t,16)
		m_d1 ^= m_d2;
		m_t ^= NA::rotl<std::uint32_t>( m_d2, 16 );

		// t = rotl(c1 ^ d1,31)
		std::uint32_t add = NA::rotr<std::uint32_t>( m_t, 31 );
		m_c1 ^= add;
		m_d1 ^= add;

		// c2 = c1 ^ rotl(d1,17)
		m_c1 ^= m_c2;
		m_d1 ^= NA::rotr<std::uint32_t>( m_c2, 17 );

		// 回到 B 的两支
		// c1 = L2(B ^ rc2)
		mark_rc( used, 2 );
		std::uint32_t mc_pre = NA::l2_transpose( m_c1 );
		phase ^= dot_parity( mc_pre, NA::ROUND_CONSTANTS[ 2 ] );

		// d1 = L1(rotr(B,3) ^ rc3)
		mark_rc( used, 3 );
		std::uint32_t md_pre = NA::l1_transpose( m_d1 );
		phase ^= dot_parity( md_pre, NA::ROUND_CONSTANTS[ 3 ] );
		md_pre = NA::rotl<std::uint32_t>( md_pre, 3 );

		mB ^= ( mc_pre ^ md_pre );
	}

	// ====== 一轮线性近似（Backward 回溯；使用所有常量并计相位）======
	LinRoundResult linear_one_round_backward_analysis( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept
	{
		using NA = NeoAlzetteCore;
		using arx_operators::linear_x_modulo_plus_const32;
		using arx_operators::linear_cor_add_wallen_logn;
		using arx_operators::neg_mod_2n;

		constexpr int n = 32;
		const auto&	  R = NA::ROUND_CONSTANTS;

		std::uint32_t mA = a_mask_out, mB = b_mask_out;
		int			  SumWeight = 0;
		int			  phase = 0;
		std::uint16_t used = 0;

		// 末尾 XOR 常量：A ^= R10; B ^= R11
		mark_rc( used, 10 );
		phase ^= dot_parity( mA, R[ 10 ] );
		mark_rc( used, 11 );
		phase ^= dot_parity( mB, R[ 11 ] );

		// —— Second subround 回溯 ——（注意：参数顺序！第三个是 used，第四个是 phase）
		backprop_injection_from_A_analysis( mB, mA, used, phase );

		mB = NA::l1_transpose( mB );
		mA = NA::l2_transpose( mA );

		mB ^= NA::rotr<std::uint32_t>( mA, 16 );  // A ^= rotl(B,16)
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );  // B ^= rotl(A,24)

		// B -= R6  变-常模加
		{
			mark_rc( used, 6 );
			// var-const subtraction: use both input/output masks = mB as a standard choice
			auto lc = arx_operators::linear_x_modulo_minus_const32( mB, R[ 6 ], mB, n );
			SumWeight += static_cast<int>( std::ceil( lc.weight ) );
			// 若有 lc.parity：phase ^= lc.parity;
		}

		// A += (rotl(B,31) ^ rotl(B,17) ^ R5)
		{
			const std::uint32_t w = mA;							   // 加法输出掩码 γ
			const std::uint32_t alpha = w;						   // 左输入（A）掩码 α：常用取法
			const std::uint32_t beta = /* 若无 hints 就先用 */ w;  // 右输入（T(B)^R5）掩码 β

			mark_rc( used, 5 );

			// ✅ 相位：用“经过 ^R5 的那根线”的掩码 β 来翻相位
			phase ^= dot_parity( beta, R[ 5 ] );

			// ✅ 相关度：三元 (α, β, γ)
			const int lw = linear_cor_add_wallen_logn( alpha, beta, w );
			SumWeight += lw;

			// ✅ 把 β 过 T 的转置推回到 B（异或常量不改掩码，只改相位）
			mB ^= T_xy_transpose( beta );
		}

		// —— First subround 回溯 ——
		backprop_injection_from_B_analysis( mA, mB, used, phase );

		mA = NA::l1_transpose( mA );
		mB = NA::l2_transpose( mB );

		mB ^= NA::rotr<std::uint32_t>( mA, 16 );
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );

		// A -= R1
		{
			mark_rc( used, 1 );
			auto lc = arx_operators::linear_x_modulo_minus_const32( mA, R[ 1 ], mA, n );
			SumWeight += static_cast<int>( std::ceil( lc.weight ) );
			// 若有 lc.parity：phase ^= lc.parity;
		}

		// B += (rotl(A,31) ^ rotl(A,17) ^ R0)
		{
			const std::uint32_t w = mB;							   // 加法输出掩码 γ
			const std::uint32_t alpha = w;						   // 左输入（B）掩码 α
			const std::uint32_t beta = /* 若无 hints 就先用 */ w;  // 右输入（T(A)^R0）掩码 β

			mark_rc( used, 0 );

			// ✅ 相位：用“经过 ^R0 的那根线”的掩码 β
			phase ^= dot_parity( beta, R[ 0 ] );

			// ✅ 相关度：三元 (α, β, γ)
			const int lw = linear_cor_add_wallen_logn( alpha, beta, w );
			SumWeight += lw;

			// ✅ 把 β 过 T 的转置推回到 A
			mA ^= T_xy_transpose( beta );
		}

		int			  used_cnt = std::popcount( used );
		constexpr int NEED_RC_COUNT = 12;
		const int	  USED_RC_COUNT = std::popcount( used );
		return { mA, mB, SumWeight, phase, used, USED_RC_COUNT, 16 - USED_RC_COUNT };
	}

	// 便捷函数：返回 |corr| 近似值（2^{-W}）
	std::pair<LinRoundResult, double> linear_one_round_backward_analysis_with_prob( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept
	{
		auto		 r = linear_one_round_backward_analysis( a_mask_out, b_mask_out );
		const double p = ( r.weight >= ( 1 << 31 ) ) ? 0.0 : std::exp2(-r.weight);	// 2^{-W}
		return { r, p };
	}

	// ====== 一轮线性近似（Backward 回溯；使用所有常量并计相位）cLAT+SLR 论文搜索框架专用 ======
	LinRoundResult linear_one_round_backward_analysis( std::uint32_t a_mask_out, std::uint32_t b_mask_out, BetaHints* beta_hints ) noexcept
	{
		using NA = NeoAlzetteCore;
		using arx_operators::linear_x_modulo_plus_const32;
		using arx_operators::linear_cor_add_wallen_logn;
		using arx_operators::neg_mod_2n;

		constexpr int n = 32;
		const auto&	  R = NA::ROUND_CONSTANTS;

		std::uint32_t mA = a_mask_out, mB = b_mask_out;
		int			  SumWeight = 0;
		int			  phase = 0;
		std::uint16_t used = 0;

		// 末尾 XOR 常量：A ^= R10; B ^= R11
		mark_rc( used, 10 );
		phase ^= dot_parity( mA, R[ 10 ] );
		mark_rc( used, 11 );
		phase ^= dot_parity( mB, R[ 11 ] );

		// —— Second subround 回溯 ——（注意：参数顺序！第三个是 used，第四个是 phase）
		backprop_injection_from_A_analysis( mB, mA, used, phase );

		mB = NA::l1_transpose( mB );
		mA = NA::l2_transpose( mA );

		mB ^= NA::rotr<std::uint32_t>( mA, 16 );  // A ^= rotl(B,16)
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );  // B ^= rotl(A,24)

		// B -= R6  变-常模加
		{
			mark_rc( used, 6 );
			// var-const subtraction: use both input/output masks = mB as a standard choice
			auto lc = arx_operators::linear_x_modulo_minus_const32( mB, R[ 6 ], mB, n );
			SumWeight += static_cast<int>( std::ceil( lc.weight ) );
			// 若有 lc.parity：phase ^= lc.parity;
		}

		// A += (rotl(B,31) ^ rotl(B,17) ^ R5)
		{
			const std::uint32_t w = mA;													 // γ
			const std::uint32_t alpha = w;												 // α
			const std::uint32_t beta = beta_hints ? beta_hints->beta_for_A_plus_TB : w;	 // β

			mark_rc( used, 5 );
			phase ^= dot_parity( beta, R[ 5 ] );  // 相位用β·R5
			SumWeight += linear_cor_add_wallen_logn( alpha, beta, w );
			mB ^= T_xy_transpose( beta );  // β 过转置回 B
		}

		// —— First subround 回溯 ——
		backprop_injection_from_B_analysis( mA, mB, used, phase );

		mA = NA::l1_transpose( mA );
		mB = NA::l2_transpose( mB );

		mB ^= NA::rotr<std::uint32_t>( mA, 16 );
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );

		// A -= R1
		{
			mark_rc( used, 1 );
			auto lc = arx_operators::linear_x_modulo_minus_const32( mA, R[ 1 ], mA, n );
			SumWeight += static_cast<int>( std::ceil( lc.weight ) );
			// 若有 lc.parity：phase ^= lc.parity;
		}

		// B += (rotl(A,31) ^ rotl(A,17) ^ R0)
		{
			const std::uint32_t w = mB;		// γ
			const std::uint32_t alpha = w;	// α
			const std::uint32_t beta = beta_hints ? beta_hints->beta_for_B_plus_TA : w;

			mark_rc( used, 0 );
			phase ^= dot_parity( beta, R[ 0 ] );  // 相位用β·R0
			SumWeight += linear_cor_add_wallen_logn( alpha, beta, w );
			mA ^= T_xy_transpose( beta );  // β 过转置回 A
		}

		int			  used_cnt = std::popcount( used );
		constexpr int NEED_RC_COUNT = 12;
		const int	  USED_RC_COUNT = std::popcount( used );
		return { mA, mB, SumWeight, phase, used, USED_RC_COUNT, 16 - USED_RC_COUNT };
	}

}  // namespace TwilightDream
