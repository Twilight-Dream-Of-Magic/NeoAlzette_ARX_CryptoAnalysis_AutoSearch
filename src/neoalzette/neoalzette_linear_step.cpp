#include "neoalzette/neoalzette_linear_step.hpp"

namespace neoalz
{

	// ============ 小工具：两补求 −K (mod 2^n) ============
    // neg_mod_2n moved to arx_analysis_operators/math_util.hpp

	// ============ T0/T1 的转置（来自 rotl(.,31) ^ rotl(.,17)） ============
	static inline std::uint32_t T_xy_transpose( std::uint32_t m ) noexcept
	{
		// transpose(rotl(.,r)) = rotr(mask, r)；XOR 的转置仍是 XOR
		return NeoAlzetteCore::rotr<std::uint32_t>( m, 31 ) ^ NeoAlzetteCore::rotr<std::uint32_t>( m, 17 );
	}

	// ============ 注入 cd_from_A 的转置回溯 ============
	// 前向：
	//   c1 = l1_forward(A ^ rc7);
	//   d1 = l2_forward(rotl(A,24) ^ rc8);
	//   t  = rotr(c1 ^ d1, 31);
	//   c2 = c1 ^ rotr(d1, 17);
	//   d2 = d1 ^ rotl(t, 16);
	//   B ^= rotl(c2,24) ^ rotl(d2,16) ^ R9;
	//
	// 反向（线性掩码回溯）：给定 mB，累加到 mA。
	static inline void backprop_injection_from_A( std::uint32_t mB, std::uint32_t& mA ) noexcept
	{
		using NA = NeoAlzetteCore;
		// 从 B 的异或注入处，y1=rotl(c2,24), y2=rotl(d2,16) 均承载 mB
		std::uint32_t m_c2 = NA::rotr<std::uint32_t>( mB, 24 );
		std::uint32_t m_d2 = NA::rotr<std::uint32_t>( mB, 16 );

		// 逐步回溯 c2,d2,t,c1,d1
		std::uint32_t m_c1 = 0, m_d1 = 0, m_t = 0;

		// d2 = d1 ^ rotl(t,16)
		m_d1 ^= m_d2;
		m_t ^= NA::rotr<std::uint32_t>( m_d2, 16 );	 // transpose(rotl(.,16)) = rotr(.,16)

		// t  = rotr(c1 ^ d1,31)
		std::uint32_t add = NA::rotl<std::uint32_t>( m_t, 31 );	 // transpose(rotr(.,31)) = rotl(.,31)
		m_c1 ^= add;
		m_d1 ^= add;

		// c2 = c1 ^ rotr(d1,17)
		m_c1 ^= m_c2;
		m_d1 ^= NA::rotl<std::uint32_t>( m_c2, 17 );  // transpose(rotr(.,17)) = rotl(.,17)

		// 回到 A：c1 = l1_forward(A ^ rc7)
		std::uint32_t m_x = NA::l1_transpose( m_c1 );  // 到 A^rc7，再到 A（常量不影响幅值/掩码）
		// d1 = l2_forward(rotl(A,24) ^ rc8)
		std::uint32_t m_y = NA::l2_transpose( m_d1 );
		m_y = NA::rotr<std::uint32_t>( m_y, 24 );  // 通过 rotl(A,24) 的转置

		mA ^= ( m_x ^ m_y );
	}

	// ============ 注入 cd_from_B 的转置回溯 ============
	// 前向：
	//   c1 = l2_forward(B ^ rc2);
	//   d1 = l1_forward(rotr(B,3) ^ rc3);
	//   t  = rotl(c1 ^ d1, 31);
	//   c2 = c1 ^ rotl(d1, 17);
	//   d2 = d1 ^ rotr(t, 16);
	//   A ^= rotl(c2,24) ^ rotl(d2,16) ^ R4;
	//
	// 反向：给定 mA，累加到 mB。
	static inline void backprop_injection_from_B( std::uint32_t mA, std::uint32_t& mB ) noexcept
	{
		using NA = NeoAlzetteCore;

		std::uint32_t m_c2 = NA::rotr<std::uint32_t>( mA, 24 );
		std::uint32_t m_d2 = NA::rotr<std::uint32_t>( mA, 16 );

		std::uint32_t m_c1 = 0, m_d1 = 0, m_t = 0;

		// d2 = d1 ^ rotr(t,16)
		m_d1 ^= m_d2;
		m_t ^= NA::rotl<std::uint32_t>( m_d2, 16 );	 // transpose(rotr(.,16)) = rotl(.,16)

		// t  = rotl(c1 ^ d1,31)
		std::uint32_t add = NA::rotr<std::uint32_t>( m_t, 31 );	 // transpose(rotl(.,31)) = rotr(.,31)
		m_c1 ^= add;
		m_d1 ^= add;

		// c2 = c1 ^ rotl(d1,17)
		m_c1 ^= m_c2;
		m_d1 ^= NA::rotr<std::uint32_t>( m_c2, 17 );  // transpose(rotl(.,17)) = rotr(.,17)

		// 回到 B：c1 = l2_forward(B ^ rc2)
		std::uint32_t m_x = NA::l2_transpose( m_c1 );
		// d1 = l1_forward(rotr(B,3) ^ rc3)
		std::uint32_t m_y = NA::l1_transpose( m_d1 );
		m_y = NA::rotl<std::uint32_t>( m_y, 3 );  // 通过 rotr(B,3) 的转置 → rotl(.,3)

		mB ^= ( m_x ^ m_y );
	}

	// 結構 LinRoundResult 於 header 定義

	// ============ 一轮线性近似（按 backward 顺序回溯掩码并累权）===========
	inline LinRoundResult linear_one_round_backward_32( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept
	{
		using NA = NeoAlzetteCore;
		using arx_operators::corr_add_x_plus_const32;
		using arx_operators::linear_cor_add_wallen_logn;

		constexpr int n = 32;
		const auto&	  R = NA::ROUND_CONSTANTS;

		std::uint32_t mA = a_mask_out;
		std::uint32_t mB = b_mask_out;
		int			  W = 0;  // 总线性权重

		// --------------------------------------------------------------------
		// 末尾 XOR 常量（R10, R11）：仅相位翻转；对 |corr| 无影响 → 掩码不变
		// --------------------------------------------------------------------

		// ===================== 反向 · Second subround ======================

		// 注入（来自 A）：B ^= rotl(C1,24) ^ rotl(D1,16) ^ R9
		// 掩码从 B 分流到 (C1,D1) 并回溯至 A
		backprop_injection_from_A( mB, mA );

		// B = l1_forward(B)；A = l2_forward(A) —— 线性层用 transpose 回溯
		mB = NA::l1_transpose( mB );
		mA = NA::l2_transpose( mA );

		// A ^= rotl(B,16)；掩码分流：B += rotr(mA,16)
		mB ^= NA::rotr<std::uint32_t>( mA, 16 );

		// B ^= rotl(A,24)；掩码分流：A += rotr(mB,24)
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );

		// B -= R6 —— 变-常模加（用 + (−R6) 计权；恒等掩码路径）
		{
			const std::uint32_t Kneg = neg_mod_2n<std::uint32_t>( R[ 6 ], n );
			// API: corr_add_x_plus_const32(alpha_in_mask, w_out_mask, K, n)
			auto lc = corr_add_x_plus_const32( mB, mB, Kneg, n );
			W += static_cast<int>( std::ceil( lc.weight ) );  // weight 已是 −log2|corr|；保持整权
			// 掩码恒等穿过该节点（我们不做择优）：
			// mB 保持不变
		}

		// A += (rotl(B,31) ^ rotl(B,17) ^ R5) —— 变-变模加（Wallén）
		{
			const std::uint32_t w = mA;	 // 输出掩码
			const std::uint32_t u = w;	 // 左输入（恒等路径）
			const std::uint32_t v = w;	 // 右输入（恒等路径，对应 T1(B) 的掩码）

			// 计权
			int lw = linear_cor_add_wallen_logn( u, v, w );
			W += lw;

			// 把 v 回溯到 B：v_B = T1^T(v) = rotr(v,31) ^ rotr(v,17)
			mB ^= T_xy_transpose( v );
			// 左输入到 A：u_A = u（恒等），即 mA 不变
		}

		// ===================== 反向 · First subround =======================

		// 注入（来自 B）：A ^= rotl(C0,24) ^ rotl(D0,16) ^ R4
		backprop_injection_from_B( mA, mB );

		// A = l1_forward(A)；B = l2_forward(B)
		mA = NA::l1_transpose( mA );
		mB = NA::l2_transpose( mB );

		// B ^= rotl(A,16)；掩码分流：B += rotr(mA,16)
		mB ^= NA::rotr<std::uint32_t>( mA, 16 );

		// A ^= rotl(B,24)；掩码分流：A += rotr(mB,24)
		mA ^= NA::rotr<std::uint32_t>( mB, 24 );

		// A -= R1 —— 变-常模加（用 + (−R1) 计权；恒等掩码路径）
		{
			const std::uint32_t Kneg = neg_mod_2n<std::uint32_t>( R[ 1 ], n );
			auto				lc = corr_add_x_plus_const32( mA, mA, Kneg, n );
			W += static_cast<int>( std::ceil( lc.weight ) );
			// mA 恒等穿过
		}

		// B += (rotl(A,31) ^ rotl(A,17) ^ R0) —— 变-变模加（Wallén）
		{
			const std::uint32_t w = mB;
			const std::uint32_t u = w;
			const std::uint32_t v = w;

			int lw = linear_cor_add_wallen_logn( u, v, w );
			W += lw;

			// 把 v 回溯到 A：T0^T(v)
			mA ^= T_xy_transpose( v );
			// mB 作为 w 保持
		}

		// —— 回溯完成：mA/mB 为入轮输入掩码 —— //
		return { mA, mB, W };
	}

	// 便捷函数：返回 |corr| 近似值（2^{-W}）
	inline std::pair<LinRoundResult, double> linear_one_round_backward_32_with_prob( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept
	{
		auto		 r = linear_one_round_backward_32( a_mask_out, b_mask_out );
		const double p = ( r.weight >= ( 1 << 28 ) ) ? 0.0 : std::ldexp( 1.0, -r.weight );	// 2^{-W}
		return { r, p };
	}

}  // namespace neoalz
