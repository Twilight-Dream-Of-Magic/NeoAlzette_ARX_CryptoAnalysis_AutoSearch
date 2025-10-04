#include "neoalzette/neoalzette_linear_step.hpp"

namespace neoalz {

NeoAlzetteLinearStep::SingleRoundLinearResult
NeoAlzetteLinearStep::propagate_single_round_backward_heuristic(
    std::uint32_t mask_A_out,
    std::uint32_t mask_B_out,
    std::uint32_t c0,
    std::uint32_t c1
) noexcept {
    SingleRoundLinearResult result{0, 0, 0, false};
    
    // ========== Subround 2 逆向（Step 10 → 6）==========
    
    // Step 10逆向: B_final = B ⊕ A_L2
    // 启发式：假设mask_B = mask_B_final, mask_A_L2 = 0
    std::uint32_t mask_B_before_xor = mask_B_out;
    std::uint32_t mask_A_L2 = 0;
    
    // Step 9逆向: L1(B), L2(A) 转置
    auto [mask_B_s2, mask_A_s2] = subround2_step9_linear_transpose(
        mask_B_before_xor, mask_A_L2
    );
    // Weight: 0
    
    // Step 8逆向: B - c1
    // 启发式：假设mask不变
    std::uint32_t mask_B_before_sub = mask_B_s2;
    auto corr_sub2 = subround2_step8_subtract_const(mask_B_s2, mask_B_before_sub, c1);
    if (!corr_sub2.feasible) {
        return result;
    }
    result.total_weight += corr_sub2.weight;
    
    // Step 7逆向: B + A 模加
    // 启发式：假设mask_A_s2作为输入
    std::uint32_t mask_B_before_add = mask_B_before_sub;
    std::uint32_t mask_A_for_add = mask_A_s2;
    auto corr_add2 = subround2_step7_modular_add(
        mask_B_before_add, mask_A_for_add, mask_B_before_sub
    );
    if (!corr_add2.feasible) {
        return result;
    }
    result.total_weight += corr_add2.weight;
    
    // Step 6逆向: cd_from_A
    // 启发式：假设C和D各取一半掩码
    std::uint32_t mask_C1 = mask_A_for_add & 0xFFFF;
    std::uint32_t mask_D1 = mask_A_for_add & 0xFFFF0000;
    auto [mask_A_from_c1, mask_A_from_d1] = subround2_step6_cd_from_A_transpose(
        mask_C1, mask_D1
    );
    std::uint32_t mask_A_after_cd = mask_A_from_c1 ^ mask_A_from_d1;
    // Weight: 0
    
    // ========== Subround 1 逆向（Step 5 → 1）==========
    
    // Step 5逆向: B ⊕ A_L1
    // 启发式：假设mask_B不变
    std::uint32_t mask_B_s1 = mask_B_before_add;
    std::uint32_t mask_A_L1 = 0;
    
    // Step 4逆向: L1(A) 转置
    std::uint32_t mask_A_before_L1 = subround1_step4_linear_transpose(mask_A_L1);
    // Weight: 0
    
    // Step 3逆向: A - c0
    std::uint32_t mask_A_before_sub = mask_A_before_L1;
    auto corr_sub1 = subround1_step3_subtract_const(
        mask_A_before_L1, mask_A_before_sub, c0
    );
    if (!corr_sub1.feasible) {
        return result;
    }
    result.total_weight += corr_sub1.weight;
    
    // Step 2逆向: A + tmp1 模加
    std::uint32_t mask_A_before_add = mask_A_before_sub;
    std::uint32_t mask_tmp1 = 0;  // 启发式
    auto corr_add1 = subround1_step2_modular_add(
        mask_A_before_add, mask_tmp1, mask_A_before_sub
    );
    if (!corr_add1.feasible) {
        return result;
    }
    result.total_weight += corr_add1.weight;
    
    // Step 1逆向: cd_from_B
    std::uint32_t mask_C0 = 0;
    std::uint32_t mask_D0 = 0;
    auto [mask_B_from_c0, mask_B_from_d0] = subround1_step1_cd_from_B_transpose(
        mask_C0, mask_D0
    );
    std::uint32_t mask_B_after_cd = mask_B_from_c0 ^ mask_B_from_d0;
    // Weight: 0
    
    // 最终输入掩码
    result.mask_A_in = mask_A_before_add;
    result.mask_B_in = mask_B_s1;
    result.feasible = true;
    
    return result;
}

} // namespace neoalz
