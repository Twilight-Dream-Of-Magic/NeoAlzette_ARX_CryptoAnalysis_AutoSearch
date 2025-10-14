#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <climits>
#include <bit>

#include "neoalzette/neoalzette_core.hpp"

// —— 只用现成线性算子（不再额外封装）—— //
#include "arx_analysis_operators/linear_correlation_add_logn.hpp"  // linear_cor_add_wallen_logn / linear_cor_add_value_logn
#include "arx_analysis_operators/linear_correlation_addconst.hpp"  // corr_add_x_plus_const32
#include "arx_analysis_operators/math_util.hpp"			   // neg_mod_2n

namespace TwilightDream
{

	// ============ 小工具：两补求 −K (mod 2^n) ============
	using arx_operators::neg_mod_2n;

	// ============ 结果结构 ============
	struct LinRoundResult
	{
		std::uint32_t a_in_mask, b_in_mask;	 // 入轮输入掩码
		int			  weight;				 // 累计线性权重 = sum(-log2|corr|)
		int			  parity;				 // 线性相位 bit（0=正，1=负）；整体符号 = (-1)^parity
		std::uint16_t used_rc_mask;			 // 哪些 R[i] 被使用（逐步记账）
		int			  used_count;
		int			  missing_count;  // = 16 - used_count
	};

	// ============ 一轮线性近似（按 backward 顺序回溯掩码并累权）===========
	LinRoundResult linear_one_round_backward_analysis( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept;

	// 便捷函数：返回 |corr| 近似值（2^{-W}）
	std::pair<LinRoundResult, double> linear_one_round_backward_analysis_with_prob( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept;

	// cLAT + SLR 论文的搜索框架需要使用
	struct BetaHints
	{
		std::uint32_t beta_for_B_plus_TA = 0; // 用于 B += (T(A)^R0)
		std::uint32_t beta_for_A_plus_TB = 0; // 用于 A += (T(B)^R5)
	};

    // ====== NeoAlzette算法一轮线性近似（Backward 回溯；使用所有常量并计相位）cLAT+SLR 论文搜索框架专用 ======
    LinRoundResult linear_one_round_backward_analysis( std::uint32_t a_mask_out, std::uint32_t b_mask_out, BetaHints* beta_hints ) noexcept;

}  // namespace TwilightDream
