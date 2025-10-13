#include "neoalzette/neoalzette_differential_step.hpp"

namespace neoalz
{

	/**
	 * @brief 一轮 NeoAlzette ARX 盒的 XOR-差分传播（32-bit）
	 *
	 * 说明：
	 *  - 仅做“差分”分析；“transpose”只用于线性近似，不在本函数中使用。
	 *  - 模加（var-var）用 LM-2001：对每次加法，使用 Algorithm 4 贪心选取最优 γ，并用 Algorithm 2 计权。
	 *  - 模加（var-const）用 BvWeight：默认取 Δout = Δin（身份映射），并计入对应权重（近似 O(log^2 n)）。
	 *  - 其它运算（XOR / rotl / rotr / L1/L2 线性层 / 注入-delta版）为确定传播，权重 0。
	 */
	struct DiffRoundResult
	{
		std::uint32_t dA_out;
		std::uint32_t dB_out;
		int			  weight;  // = −log2 DP_total（整型；BvWeight已做4bit小数→整数）
	};

	inline DiffRoundResult diff_one_round_xdp_32( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept
	{
		using neoalz::NeoAlzetteCore;
		using neoalz::arx_operators::diff_addconst_bvweight;
		using neoalz::arx_operators::diff_subconst_bvweight;
		using neoalz::arx_operators::find_optimal_gamma_with_weight;

		constexpr int n = 32;
		const auto&	  R = NeoAlzetteCore::ROUND_CONSTANTS;

		std::uint32_t dA = dA_in;
		std::uint32_t dB = dB_in;
		int			  W = 0;

		// -----------------------------
		// First subround（与 forward 顺序一致）
		// -----------------------------

		// (1) B += (rotl(A,31) ^ rotl(A,17) ^ R[0])  —— var-var 加法（R[0]在差分域消失）
		{
			std::uint32_t dT0 = NeoAlzetteCore::rotl<std::uint32_t>( dA, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( dA, 17 );
			auto [ gammaB, w ] = find_optimal_gamma_with_weight( dB, dT0, n );
			// LM-2001 Alg.4 给的 γ 一定是“可行”的；若你想指定 γ，请改为 xdp_add_lm2001 验证权重。
			dB = gammaB;
			if ( w < 0 )
			{  // 理论上不会发生
				// 标记成“几乎不可能”，你也可以抛出特定错误码
				return { 0, 0, INT_MAX / 2 };
			}
			W += w;
		}

		// (2) A -= R[1] —— var-const 加法
		// 默认路径：Δout = Δin（身份映射），并将该选择的权重计入；
		// 若要搜索/指定其它 Δout，只需改“dA1”并用同一 diff_addconst_bvweight 计权。
		{
			const std::uint32_t dA1 = dA;  // 身份映射（可替换为你的候选 ΔA'）
			int					w = diff_subconst_bvweight( dA, R[ 1 ], dA1 );
			if ( w < 0 )
			{  // 当前BvWeight实现通常不会给-1，这里留兜底
				return { 0, 0, INT_MAX / 2 };
			}
			W += w;
			dA = dA1;
		}

		// (3) A ^= rotl(B,24) —— 确定传播
		dA ^= NeoAlzetteCore::rotl<std::uint32_t>( dB, 24 );

		// (4) B ^= rotl(A,16) —— 确定传播
		dB ^= NeoAlzetteCore::rotl<std::uint32_t>( dA, 16 );

		// (5) A = L1(A) —— 线性层（确定传播）
		dA = NeoAlzetteCore::l1_forward( dA );

		// (6) B = L2(B) —— 线性层（确定传播）
		dB = NeoAlzetteCore::l2_forward( dB );

		// (7) 注入（来自 B）：A ^= rotl(C0,24) ^ rotl(D0,16)
		//     使用 delta 版本，常量在差分域消失
		{
			auto [ dC0, dD0 ] = NeoAlzetteCore::cd_from_B_delta( dB );
			dA ^= NeoAlzetteCore::rotl<std::uint32_t>( dC0, 24 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( dD0, 16 );
		}

		// -----------------------------
		// Second subround
		// -----------------------------

		// (8) A += (rotl(B,31) ^ rotl(B,17) ^ R[5]) —— var-var 加法
		{
			std::uint32_t dT1 = NeoAlzetteCore::rotl<std::uint32_t>( dB, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( dB, 17 );
			auto [ gammaA, w ] = find_optimal_gamma_with_weight( dA, dT1, n );
			dA = gammaA;
			if ( w < 0 )
			{
				return { 0, 0, INT_MAX / 2 };
			}
			W += w;
		}

		// (9) B -= R[6] —— var-const 加法
		{
			const std::uint32_t dB1 = dB;  // 身份映射（可替换为你的候选 ΔB'）
			int					w = diff_subconst_bvweight( dB, R[ 6 ], dB1 );
			if ( w < 0 )
			{
				return { 0, 0, INT_MAX / 2 };
			}
			W += w;
			dB = dB1;
		}

		// (10) B ^= rotl(A,24) —— 确定传播
		dB ^= NeoAlzetteCore::rotl<std::uint32_t>( dA, 24 );

		// (11) A ^= rotl(B,16) —— 确定传播
		dA ^= NeoAlzetteCore::rotl<std::uint32_t>( dB, 16 );

		// (12) B = L1(B) —— 线性层（确定传播）
		dB = NeoAlzetteCore::l1_forward( dB );

		// (13) A = L2(A) —— 线性层（确定传播）
		dA = NeoAlzetteCore::l2_forward( dA );

		// (14) 注入（来自 A）：B ^= rotl(C1,24) ^ rotl(D1,16)
		{
			auto [ dC1, dD1 ] = NeoAlzetteCore::cd_from_A_delta( dA );
			dB ^= NeoAlzetteCore::rotl<std::uint32_t>( dC1, 24 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( dD1, 16 );
		}

		// (15) 末尾 XOR 常量 —— 在差分域消失（无需处理）

		return { dA, dB, W };
	}

	/**
	 * @brief 便捷函数：同时返回近似概率（double）
	 */
	inline std::pair<DiffRoundResult, double> diff_one_round_xdp_32_with_prob( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept
	{
		auto   r = diff_one_round_xdp_32( dA_in, dB_in );
		double p = ( r.weight >= ( INT_MAX / 4 ) ) ? 0.0 : std::pow( 2.0, -static_cast<double>( r.weight ) );
		return { r, p };
	}

}  // namespace neoalz
