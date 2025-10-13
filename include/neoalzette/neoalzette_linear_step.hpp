#pragma once
#include <cstdint>
#include <utility>
#include <cmath>
#include <climits>

#include "neoalzette/neoalzette_core.hpp"

// —— 只用现成线性算子（不再额外封装）—— //
#include "arx_analysis_operators/linear_cor_add_logn.hpp"  // linear_cor_add_wallen_logn / linear_cor_add_value_logn
#include "arx_analysis_operators/linear_cor_addconst.hpp"  // corr_add_x_plus_const32

namespace neoalz
{

	// ============ 小工具：两补求 −K (mod 2^n) ============
	template <class T>
	static constexpr T neg_mod_2n( T k, int n ) noexcept;

	// ============ T0/T1 的转置（来自 rotl(.,31) ^ rotl(.,17)） ============
	static inline std::uint32_t T_xy_transpose( std::uint32_t m ) noexcept;

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
	static inline void backprop_injection_from_A( std::uint32_t mB, std::uint32_t& mA ) noexcept;

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
	static inline void backprop_injection_from_B( std::uint32_t mA, std::uint32_t& mB ) noexcept;

	// ============ 结果结构 ============
	// 仅返回“输入掩码 + 总权重”；|corr|≈2^{-weight}
	struct LinRoundResult
	{
		std::uint32_t a_in_mask;
		std::uint32_t b_in_mask;
		int			  weight;  // = −log2 |corr_total|
	};

	// ============ 一轮线性近似（按 backward 顺序回溯掩码并累权）===========
	inline LinRoundResult linear_one_round_backward_32( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept;

	// 便捷函数：返回 |corr| 近似值（2^{-W}）
	inline std::pair<LinRoundResult, double> linear_one_round_backward_32_with_prob( std::uint32_t a_mask_out, std::uint32_t b_mask_out ) noexcept;

}  // namespace neoalz
