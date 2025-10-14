#include "neoalzette/neoalzette_differential_step.hpp"

namespace TwilightDream
{

	// --- 审计工具 ---
	inline void mark_rc_used( std::uint16_t& mask, int idx ) noexcept
	{
		mask |= ( 1u << idx );
	}

	// Δ(const) = 0，但必须显式“使用”常量并做记录
	inline std::uint32_t delta_of_const( std::uint32_t /*rc*/, std::uint16_t& mask, int idx ) noexcept
	{
		mark_rc_used( mask, idx );
		return 0u;
	}

	// ==== 注入路径（Δ版，严格镜像你的 value-domain，且显式“使用”rc0/rc1） ====

	inline std::pair<std::uint32_t, std::uint32_t> cd_injection_from_B_analysis( std::uint32_t dB, std::uint32_t rc0, int rc0_idx, std::uint32_t rc1, int rc1_idx, std::uint16_t& used ) noexcept
	{
		using N = TwilightDream::NeoAlzetteCore;

		// value: c = L2(B ^ rc0)            → delta: dc = L2(dB ^ Δ(rc0))
		std::uint32_t dc = N::l2_forward( dB ^ delta_of_const( rc0, used, rc0_idx ) );
		// value: d = L1(rotr(B,3) ^ rc1)    → delta: dd = L1(rotr(dB,3) ^ Δ(rc1))
		std::uint32_t dd = N::l1_forward( N::rotr<std::uint32_t>( dB, 3 ) ^ delta_of_const( rc1, used, rc1_idx ) );

		// value: t = rotl(c ^ d, 31)        → delta: dt = rotl(dc ^ dd, 31)
		std::uint32_t dt = N::rotl<std::uint32_t>( dc ^ dd, 31 );
		// value: c ^= rotl(d,17)            → delta: dc ^= rotl(dd,17)
		dc ^= N::rotl<std::uint32_t>( dd, 17 );
		// value: d ^= rotr(t,16)            → delta: dd ^= rotr(dt,16)
		dd ^= N::rotr<std::uint32_t>( dt, 16 );

		return { dc, dd };
	}

	inline std::pair<std::uint32_t, std::uint32_t> cd_injection_from_A_analysis( std::uint32_t dA, std::uint32_t rc0, int rc0_idx, std::uint32_t rc1, int rc1_idx, std::uint16_t& used ) noexcept
	{
		using N = TwilightDream::NeoAlzetteCore;

		// value: c = L1(A ^ rc0)            → delta: dc = L1(dA ^ Δ(rc0))
		std::uint32_t dc = N::l1_forward( dA ^ delta_of_const( rc0, used, rc0_idx ) );
		// value: d = L2(rotl(A,24) ^ rc1)   → delta: dd = L2(rotl(dA,24) ^ Δ(rc1))
		std::uint32_t dd = N::l2_forward( N::rotl<std::uint32_t>( dA, 24 ) ^ delta_of_const( rc1, used, rc1_idx ) );

		// value: t = rotr(c ^ d, 31)        → delta: dt = rotr(dc ^ dd, 31)
		std::uint32_t dt = N::rotr<std::uint32_t>( dc ^ dd, 31 );
		// value: c ^= rotr(d,17)            → delta: dc ^= rotr(dd,17)
		dc ^= N::rotr<std::uint32_t>( dd, 17 );
		// value: d ^= rotl(t,16)            → delta: dd ^= rotl(dt,16)
		dd ^= N::rotl<std::uint32_t>( dt, 16 );

		return { dc, dd };
	}


	/**
	 * @brief 一轮 NeoAlzette ARX 盒的 XOR-差分传播（32-bit）
	 *
	 * 说明：
	 *  - 仅做“差分”分析；“transpose”只用于线性近似，不在本函数中使用。
	 *  - 模加（var-var）用 LM-2001：对每次加法，使用 Algorithm 4 贪心选取最优 γ，并用 Algorithm 2 计权。
	 *  - 模加（var-const）用 BvWeight：默认取 Δout = Δin（身份映射），并计入对应权重（近似 O(log^2 n)）。
	 *  - 其它运算（XOR / rotl / rotr / L1/L2 线性层 / 注入-delta版）为确定传播，权重 0。
	 */
	// DiffRoundResult 結構已在 header 定義

	DiffRoundResult diff_one_round_analysis( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept
	{
		using NA = TwilightDream::NeoAlzetteCore;
		using TwilightDream::arx_operators::diff_subconst_bvweight;		  // var-const
		using TwilightDream::arx_operators::find_optimal_gamma_with_weight;  // var-var (LM-2001)

		constexpr int n = 32;
		const auto&	  R = NA::ROUND_CONSTANTS;

		std::uint32_t dA = dA_in, dB = dB_in;
		int			  SumWeight = 0;
		std::uint16_t used = 0;

		// ---- First subround ----

		// 1) B += (rotl(A,31) ^ rotl(A,17) ^ R[0]) 
		// (var-var)
		{
			// 在 Δ 域显式“用”R[0]（Δ=0）
			std::uint32_t dT0 = ( NA::rotl<std::uint32_t>( dA, 31 ) ^ NA::rotl<std::uint32_t>( dA, 17 ) ) ^ delta_of_const( R[ 0 ], used, 0 );
			// 这里我们不用真实的计算，阿尔法 模加 贝塔等于伽马。(γ = α + β mod 2^{N})
			// 因为我们直接通过LM-2001 Algorithm 4 评估器算出最优伽马
			// ARX 差分分析里面这种模加减运算，我们本来就是为了求伽马 才需要真实的算一步 Z = X + Y mod 2^{N}，如果没有最后伽马就要把Z作为伽马候选。
			auto [ gammaB, possible_weight ] = find_optimal_gamma_with_weight( dB, dT0, n );
			if ( possible_weight < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dB = gammaB;
			SumWeight += possible_weight;
		}

		// 2) A -= R[1]  (var-const)
		{
			mark_rc_used( used, 1 );
			const std::uint32_t dA1 = dA - R[ 1 ]; // 先算“伽马”γ
			int					possible_weight = diff_subconst_bvweight( dA, R[ 1 ], dA1 ); // 再使用模加模减差分算子评估 (α, β, γ)
			if ( possible_weight < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dA = dA1;
			SumWeight += possible_weight;
		}

		// 3) A ^= rotl(B,24)
		dA ^= NA::rotl<std::uint32_t>( dB, 24 );

		// 4) B ^= rotl(A,16)
		dB ^= NA::rotl<std::uint32_t>( dA, 16 );

		// 5) A = L1(A)
		dA = NA::l1_forward( dA );

		// 6) B = L2(B)
		dB = NA::l2_forward( dB );

		// 7) A ^= (rotl(C0,24) ^ rotl(D0,16) ^ R[4]), with (C0,D0)=cd_from_B(B,R[2],R[3])
		{
			auto [ dC0, dD0 ] = cd_injection_from_B_analysis( dB, R[ 2 ], 2, R[ 3 ], 3, used );
			std::uint32_t dK = delta_of_const( R[ 4 ], used, 4 );
			dA ^= ( NA::rotl<std::uint32_t>( dC0, 24 ) ^ NA::rotl<std::uint32_t>( dD0, 16 ) ^ dK );
		}

		// ---- Second subround ----

		// 8) A += (rotl(B,31) ^ rotl(B,17) ^ R[5])
		// (var-var)
		{
			std::uint32_t dT1 = ( NA::rotl<std::uint32_t>( dB, 31 ) ^ NA::rotl<std::uint32_t>( dB, 17 ) ) ^ delta_of_const( R[ 5 ], used, 5 );
			// 这里我们不用真实的计算，阿尔法 模加 贝塔等于伽马。(γ = α + β mod 2^{N})
			// 因为我们直接通过LM-2001 Algorithm 4 评估器算出最优伽马
			// ARX 差分分析里面这种模加减运算，我们本来就是为了求伽马 才需要真实的算一步 Z = X + Y mod 2^{N}，如果没有最后伽马就要把Z作为伽马候选。
			auto [ gammaA, possible_weight ] = find_optimal_gamma_with_weight( dA, dT1, n );
			if ( possible_weight < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dA = gammaA;
			SumWeight += possible_weight;
		}

		// 9) B -= R[6]  (var-const)
		{
			mark_rc_used( used, 6 );
			const std::uint32_t dB1 = dB - R[ 6 ]; // 先算“伽马”γ
			int					possible_weight = diff_subconst_bvweight( dB, R[ 6 ], dB1 ); // 再使用模加模减差分算子评估 (α, β, γ)
			if ( possible_weight < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dB = dB1;
			SumWeight += possible_weight;
		}

		// 10) B ^= rotl(A,24)
		dB ^= NA::rotl<std::uint32_t>( dA, 24 );

		// 11) A ^= rotl(B,16)
		dA ^= NA::rotl<std::uint32_t>( dB, 16 );

		// 12) B = L1(B)
		dB = NA::l1_forward( dB );

		// 13) A = L2(A)
		dA = NA::l2_forward( dA );

		// 14) B ^= (rotl(C1,24) ^ rotl(D1,16) ^ R[9]), with (C1,D1)=cd_from_A(A,R[7],R[8])
		{
			auto [ dC1, dD1 ] = cd_injection_from_A_analysis( dA, R[ 7 ], 7, R[ 8 ], 8, used );
			std::uint32_t dK = delta_of_const( R[ 9 ], used, 9 );
			dB ^= ( NA::rotl<std::uint32_t>( dC1, 24 ) ^ NA::rotl<std::uint32_t>( dD1, 16 ) ^ dK );
		}

		// 15) A ^= R[10]; B ^= R[11]  (末尾 XOR 常量)
		{
			std::uint32_t dK10 = delta_of_const( R[ 10 ], used, 10 );
			std::uint32_t dK11 = delta_of_const( R[ 11 ], used, 11 );
			dA ^= dK10;
			dB ^= dK11;	 // Δ=0，但显式“使用”并记账
		}
		constexpr int NEED_RC_COUNT = 12;
		const int USED_RC_COUNT = std::popcount( used );
		return { dA, dB, SumWeight, used, USED_RC_COUNT, NEED_RC_COUNT - USED_RC_COUNT };
	}

	/**
	 * @brief 便捷函数：同时返回近似概率（double）
	 */
	std::pair<DiffRoundResult, double> diff_one_round_analysis_with_prob( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept
	{
		auto   r = diff_one_round_analysis( dA_in, dB_in );
		double p = ( r.weight >= ( 1 << 31 ) ) ? 0.0 : std::pow( 2.0, -static_cast<double>( r.weight ) );
		return { r, p };
	}

	/* NeoAlzette算法一轮差分传播 pDDT+Matsui 论文搜索框架专用 */
	static inline void push_step( MatsuiSearchNeed* need, MatsuiSearchNeed::Step::Kind k, int rc_idx, std::uint32_t dA_in, std::uint32_t dB_in, std::uint32_t dA_out, std::uint32_t dB_out, int w ) noexcept
	{
		if ( !need || !need->record_trace )
			return;
		auto& s = need->steps[ need->step_count++ ];
		s.kind = k;
		s.rc_idx = static_cast<std::int16_t>( rc_idx );
		s.dA_in = dA_in;
		s.dB_in = dB_in;
		s.dA_out = dA_out;
		s.dB_out = dB_out;
		s.w = w;
	}

	/* NeoAlzette算法一轮差分传播 pDDT+Matsui 论文搜索框架专用 */
	DiffRoundResult diff_one_round_analysis( std::uint32_t dA_in, std::uint32_t dB_in, MatsuiSearchNeed* need0, PDDTNeed* need1 ) noexcept
	{
		using NA = TwilightDream::NeoAlzetteCore;
		using TwilightDream::arx_operators::diff_subconst_bvweight;		  // var-const
		using TwilightDream::arx_operators::find_optimal_gamma_with_weight;  // var-var (LM-2001)

		constexpr int n = 32;
		const auto&	  R = NA::ROUND_CONSTANTS;

		std::uint32_t dA = dA_in, dB = dB_in;
		int			  SumWeight = 0;
		std::uint16_t used = 0;

		// ==== First subround ====

		// 1) B += (rotl(A,31) ^ rotl(A,17) ^ R[0])  —— var-var（LM-2001-Alg.4 + Alg.2）
		{
			const std::uint32_t inA = dA, inB = dB;
			std::uint32_t		dT0 = ( NA::rotl<std::uint32_t>( dA, 31 ) ^ NA::rotl<std::uint32_t>( dA, 17 ) ) ^ delta_of_const( R[ 0 ], used, 0 );
			auto [ gammaB, w ] = find_optimal_gamma_with_weight( dB, dT0, n );
			if ( w < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dB = gammaB;
			SumWeight += w;
			push_step( need0, MatsuiSearchNeed::Step::AddVV, -1, inA, inB, dA, dB, w );
		}

		// 2) A -= R[1] —— var-const（BvWeight；Δout=Δin 或按你的实现：γ = dA - R[1]）
		{
			const std::uint32_t inA = dA, inB = dB;
			mark_rc_used( used, 1 );
			const std::uint32_t dA1 = dA - R[ 1 ];	// 保持与你现有实现完全一致
			int					w = diff_subconst_bvweight( dA, R[ 1 ], dA1 );
			if ( w < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dA = dA1;
			SumWeight += w;
			push_step( need0, MatsuiSearchNeed::Step::AddVC, 1, inA, inB, dA, dB, w );
		}

		// 3) A ^= rotl(B,24)
		{
			const std::uint32_t inA = dA, inB = dB;
			dA ^= NA::rotl<std::uint32_t>( dB, 24 );
			push_step( need0, MatsuiSearchNeed::Step::Xor, -1, inA, inB, dA, dB, 0 );
		}

		// 4) B ^= rotl(A,16)
		{
			const std::uint32_t inA = dA, inB = dB;
			dB ^= NA::rotl<std::uint32_t>( dA, 16 );
			push_step( need0, MatsuiSearchNeed::Step::Xor, -1, inA, inB, dA, dB, 0 );
		}

		// 5) A = L1(A)
		{
			const std::uint32_t inA = dA, inB = dB;
			dA = NA::l1_forward( dA );
			push_step( need0, MatsuiSearchNeed::Step::L1, -1, inA, inB, dA, dB, 0 );
		}

		// 6) B = L2(B)
		{
			const std::uint32_t inA = dA, inB = dB;
			dB = NA::l2_forward( dB );
			push_step( need0, MatsuiSearchNeed::Step::L2, -1, inA, inB, dA, dB, 0 );
		}

		// 7) A ^= (rotl(C0,24) ^ rotl(D0,16) ^ R[4]), with (C0,D0)=cd_from_B(B,R[2],R[3])
		{
			const std::uint32_t inA = dA, inB = dB;
			auto [ dC0, dD0 ] = cd_injection_from_B_analysis( dB, R[ 2 ], 2, R[ 3 ], 3, used );
			std::uint32_t dK = delta_of_const( R[ 4 ], used, 4 );
			dA ^= ( NA::rotl<std::uint32_t>( dC0, 24 ) ^ NA::rotl<std::uint32_t>( dD0, 16 ) ^ dK );
			push_step( need0, MatsuiSearchNeed::Step::InjectB, 2, inA, inB, dA, dB, 0 );
			// 注：R[3], R[4] 也已显式使用并计入 mask
		}

		// ==== Second subround ====

		// 8) A += (rotl(B,31) ^ rotl(B,17) ^ R[5]) —— var-var
		{
			const std::uint32_t inA = dA, inB = dB;
			std::uint32_t		dT1 = ( NA::rotl<std::uint32_t>( dB, 31 ) ^ NA::rotl<std::uint32_t>( dB, 17 ) ) ^ delta_of_const( R[ 5 ], used, 5 );
			auto [ gammaA, w ] = find_optimal_gamma_with_weight( dA, dT1, n );
			if ( w < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dA = gammaA;
			SumWeight += w;
			push_step( need0, MatsuiSearchNeed::Step::AddVV, -1, inA, inB, dA, dB, w );
		}

		// 9) B -= R[6] —— var-const
		{
			const std::uint32_t inA = dA, inB = dB;
			mark_rc_used( used, 6 );
			const std::uint32_t dB1 = dB - R[ 6 ];	// 保持与你现有实现完全一致
			int					w = diff_subconst_bvweight( dB, R[ 6 ], dB1 );
			if ( w < 0 )
				return { 0, 0, INT_MAX / 2, used, 0, 16 };
			dB = dB1;
			SumWeight += w;
			push_step( need0, MatsuiSearchNeed::Step::AddVC, 6, inA, inB, dA, dB, w );
		}

		// 10) B ^= rotl(A,24)
		{
			const std::uint32_t inA = dA, inB = dB;
			dB ^= NA::rotl<std::uint32_t>( dA, 24 );
			push_step( need0, MatsuiSearchNeed::Step::Xor, -1, inA, inB, dA, dB, 0 );
		}

		// 11) A ^= rotl(B,16)
		{
			const std::uint32_t inA = dA, inB = dB;
			dA ^= NA::rotl<std::uint32_t>( dB, 16 );
			push_step( need0, MatsuiSearchNeed::Step::Xor, -1, inA, inB, dA, dB, 0 );
		}

		// 12) B = L1(B)
		{
			const std::uint32_t inA = dA, inB = dB;
			dB = NA::l1_forward( dB );
			push_step( need0, MatsuiSearchNeed::Step::L1, -1, inA, inB, dA, dB, 0 );
		}

		// 13) A = L2(A)
		{
			const std::uint32_t inA = dA, inB = dB;
			dA = NA::l2_forward( dA );
			push_step( need0, MatsuiSearchNeed::Step::L2, -1, inA, inB, dA, dB, 0 );
		}

		// 14) B ^= (rotl(C1,24) ^ rotl(D1,16) ^ R[9]), with (C1,D1)=cd_from_A(A,R[7],R[8])
		{
			const std::uint32_t inA = dA, inB = dB;
			auto [ dC1, dD1 ] = cd_injection_from_A_analysis( dA, R[ 7 ], 7, R[ 8 ], 8, used );
			std::uint32_t dK = delta_of_const( R[ 9 ], used, 9 );
			dB ^= ( NA::rotl<std::uint32_t>( dC1, 24 ) ^ NA::rotl<std::uint32_t>( dD1, 16 ) ^ dK );
			push_step( need0, MatsuiSearchNeed::Step::InjectA, 7, inA, inB, dA, dB, 0 );
			// 注：R[8], R[9] 也已显式使用并计入 mask
		}

		// 15) A ^= R[10]; B ^= R[11]  —— 末尾 XOR 常量（Δ=0，但显式“使用”）
		{
			const std::uint32_t inA = dA, inB = dB;
			std::uint32_t		dK10 = delta_of_const( R[ 10 ], used, 10 );
			std::uint32_t		dK11 = delta_of_const( R[ 11 ], used, 11 );
			dA ^= dK10;
			dB ^= dK11;
			push_step( need0, MatsuiSearchNeed::Step::XorRC, 10, inA, inB, dA, dB, 0 );
		}

		const int		NEED_RC_COUNT = 12;
		const int		USED_RC_COUNT = std::popcount( used );
		DiffRoundResult out { dA, dB, SumWeight, used, USED_RC_COUNT, NEED_RC_COUNT - USED_RC_COUNT };

		// —— 回填 MatsuiSearchNeed（单条最佳，与旧行为一致）——
		if ( need0 )
		{
			need0->best.dA_out = dA;
			need0->best.dB_out = dB;
			need0->best.weight = SumWeight;
			need0->best.prob = ( SumWeight >= ( 1 << 31 ) ) ? 0.0 : std::exp2( -SumWeight );
			need0->best.used_rc_mask = used;
			// caps 已在构造时给出（Single + Trace）
		}

		// —— 回填 PDDTNeed（可选；满足阈值再收）——
		if ( need1 && need1->capture_edge )
		{
			const bool ok_w = ( need1->weight_threshold < 0 ) || ( SumWeight <= need1->weight_threshold );
			const bool ok_p = ( need1->prob_threshold <= 0.0 ) || ( std::exp2( -SumWeight ) >= need1->prob_threshold );
			if ( ok_w && ok_p )
			{
				need1->edge = { dA_in, dB_in, dA, dB, SumWeight };
				need1->has_edge = true;
			}
		}

		return out;
	}
}  // namespace TwilightDream
