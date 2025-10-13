#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <climits>

#include "neoalzette/neoalzette_core.hpp"

// 差分算子（变量-变量 / 变量-常量）
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"

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

    DiffRoundResult diff_one_round_xdp_32( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept;

	/**
	 * @brief 便捷函数：同时返回近似概率（double）
	 */
    std::pair<DiffRoundResult, double> diff_one_round_xdp_32_with_prob( std::uint32_t dA_in, std::uint32_t dB_in ) noexcept;

}  // namespace neoalz
