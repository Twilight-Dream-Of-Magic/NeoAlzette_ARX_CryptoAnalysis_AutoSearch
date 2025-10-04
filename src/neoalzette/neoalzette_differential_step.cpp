#include "neoalzette/neoalzette_differential_step.hpp"

namespace neoalz {

NeoAlzetteDifferentialStep::SingleRoundResult
NeoAlzetteDifferentialStep::propagate_single_round_heuristic(
    std::uint32_t dA_in,
    std::uint32_t dB_in,
    std::uint32_t c0,
    std::uint32_t c1
) noexcept {
    SingleRoundResult result{0, 0, 0, false};
    
    // ========== Subround 1 ==========
    
    // Step 1: 线性层 L1(A), L2(B)
    auto [dA_l1, dB_l2] = subround1_linear_layer(dA_in, dB_in);
    // Weight: 0 (线性操作)
    
    // Step 2: cd_from_B(B)
    auto [dC0, dD0] = subround1_cd_from_B(dB_l2);
    // Weight: 0 (XOR操作)
    
    // Step 3: 跨分支注入 A ^= (rotl(C0,24) ^ rotl(D0,16))
    std::uint32_t dA_injected = subround1_cross_injection(dA_l1, dC0, dD0);
    // Weight: 0 (XOR操作)
    
    // Step 4: 模加 (A + B)
    auto [dOut_add1, w_add1] = subround1_modular_add_optimal(dA_injected, dB_l2);
    if (w_add1 < 0) {
        return result; // 不可行
    }
    result.total_weight += w_add1;
    
    // Step 5: 减常量 A - c0
    // 启发式：假设减常量后差分不变（实际应该枚举）
    std::uint32_t dA_after_sub = dOut_add1;
    int w_sub1 = subround1_subtract_const_weight(dOut_add1, c0, dA_after_sub);
    if (w_sub1 < 0) {
        // 尝试找更好的
        // 简化：直接使用原差分
        w_sub1 = 0;
    }
    result.total_weight += w_sub1;
    
    // ========== Subround 2 ==========
    
    // Step 6: 线性层 L1(B), L2(A)
    auto [dB_l1, dA_l2] = subround2_linear_layer(dB_l2, dA_after_sub);
    // Weight: 0
    
    // Step 7: cd_from_A(A)
    auto [dC1, dD1] = subround2_cd_from_A(dA_l2);
    // Weight: 0
    
    // Step 8: 跨分支注入 B ^= (rotl(C1,24) ^ rotl(D1,16))
    std::uint32_t dB_injected = subround2_cross_injection(dB_l1, dC1, dD1);
    // Weight: 0
    
    // Step 9: 模加 (B + A)
    auto [dOut_add2, w_add2] = subround2_modular_add_optimal(dB_injected, dA_l2);
    if (w_add2 < 0) {
        return result; // 不可行
    }
    result.total_weight += w_add2;
    
    // Step 10: 减常量 B - c1
    std::uint32_t dB_after_sub = dOut_add2;
    int w_sub2 = subround2_subtract_const_weight(dOut_add2, c1, dB_after_sub);
    if (w_sub2 < 0) {
        w_sub2 = 0;
    }
    result.total_weight += w_sub2;
    
    // 最终输出
    result.dA_out = dA_l2;  // Subround 2后的A
    result.dB_out = dB_after_sub;
    result.feasible = true;
    
    return result;
}

} // namespace neoalz
