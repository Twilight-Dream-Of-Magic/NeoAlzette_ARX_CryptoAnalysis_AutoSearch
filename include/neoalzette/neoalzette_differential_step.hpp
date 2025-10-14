#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <climits>
#include <bit>

#include "neoalzette/neoalzette_core.hpp"

// 差分算子（变量-变量 / 变量-常量）
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"

namespace TwilightDream
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
		std::uint32_t dA_out, dB_out;
		int			  weight;		  // = −log2 DP_total（整型；BvWeight已做4bit小数→整数）
		std::uint16_t used_rc_mask;	  // 16 位；第 i 位=1 表示 R[i] 在本轮实现中被“使用过”
		int			  used_count;	  // popcount(used_rc_mask)
		int			  missing_count;  // 16 - used_count
	};

	DiffRoundResult diff_one_round_analysis( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept;

	/**
	 * @brief 便捷函数：同时返回近似概率（double）
	 */
	std::pair<DiffRoundResult, double> diff_one_round_analysis_with_prob( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept;


	// === 外部通信 Need（最小接口，仅新增，不影响原有签名） ===

	struct MatsuiSearchNeed
	{
		// 【输入】是否记录逐步轨迹（用于 Matsui-2 的 Bn/审计）
		bool record_trace = false;

		// 【输出】本轮“最佳后继”（与旧版行为一致；不改变算法）
		struct Candidate
		{
			std::uint32_t dA_out = 0, dB_out = 0;
			int			  weight = INT_MAX;
			double		  prob = 0.0;
			std::uint16_t used_rc_mask = 0;
		} best;

		// 【输出】逐步轨迹（可选）
		struct Step
		{
			enum Kind : std::uint8_t
			{
				AddVV = 0,
				AddVC = 1,
				Xor = 2,
				Rot = 3,
				L1 = 4,
				L2 = 5,
				InjectA = 6,
				InjectB = 7,
				XorRC = 8
			} kind;
			std::int16_t  rc_idx;  // 非 RC 步骤 = -1
			std::uint32_t dA_in, dB_in, dA_out, dB_out;
			int			  w;  // 本步权重贡献（非加法步=0）
		};
		Step steps[ 24 ] {};  // 本轮最多 15~16 步，这里留冗余
		int	 step_count = 0;

		// （扩展位）能力标识：当前仅支持 Single-best + Trace
		enum : std::uint32_t
		{
			CAP_SINGLE = 1u << 0,
			CAP_TRACE = 1u << 1
		};
		std::uint32_t caps = CAP_SINGLE | CAP_TRACE;
	};

	struct PDDTNeed
	{
		// 【输入】是否要把“本轮从 (dA_in,dB_in) 到 (dA_out,dB_out) 的那一条边”
		//        回填到调用方的 pDDT 构建缓存里（命中阈值才回填）
		bool   capture_edge = false;
		int	   weight_threshold = -1;  // ≤w 才收；-1 关闭
		double prob_threshold = 0.0;   // 2^{-w} ≥ τ 才收；0 关闭
		int	   capacity_hint = 8;	   // 调用方可用作桶容量参考（这里只回填 1 条）

		// 【输出】单条边（与 capacity_hint 无关，这里只写 1 条）
		struct Edge
		{
			std::uint32_t inA, inB, outA, outB;
			int			  weight;
		} edge {};
		bool has_edge = false;
	};

	/* NeoAlzette算法一轮差分传播 pDDT+Matsui 论文搜索框架专用 */
	DiffRoundResult diff_one_round_analysis( std::uint32_t dA_in, std::uint32_t dB_in, MatsuiSearchNeed* need0, PDDTNeed* need1 ) noexcept;

}  // namespace TwilightDream
