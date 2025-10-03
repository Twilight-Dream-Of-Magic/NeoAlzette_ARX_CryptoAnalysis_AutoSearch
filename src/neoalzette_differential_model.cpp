#include "neoalzette_differential_model.hpp"
#include <algorithm>
#include <cstring>

namespace neoalz {

// ============================================================================
// RoundDiff詳細計算實現
// ============================================================================

NeoAlzetteDifferentialModel::RoundDiff 
NeoAlzetteDifferentialModel::compute_round_diff_detailed(
    std::uint32_t delta_A_in,
    std::uint32_t delta_B_in,
    std::uint32_t delta_A_out,
    std::uint32_t delta_B_out
) {
    RoundDiff result;
    result.delta_A_in = delta_A_in;
    result.delta_B_in = delta_B_in;
    result.delta_A_out = delta_A_out;
    result.delta_B_out = delta_B_out;
    result.total_weight = 0;
    result.total_probability = 1.0;
    
    // 追踪中間狀態
    std::uint32_t dA = delta_A_in;
    std::uint32_t dB = delta_B_in;
    
    // ========== First subround ==========
    
    // Op1: B += (rotl(A, 31) ^ rotl(A, 17) ^ R[0])
    {
        std::uint32_t beta = NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17);
        
        // 嘗試找到最佳的dB_after
        std::uint32_t best_dB_after = dB;  // 默認：差分不變
        int best_weight = std::numeric_limits<int>::max();
        
        // 啟發式搜索
        for (std::uint32_t candidate : {dB, dB ^ beta, NeoAlzetteCore::rotl(dB, 1)}) {
            int w = compute_diff_weight_add(dB, beta, candidate);
            if (w >= 0 && w < best_weight) {
                best_weight = w;
                best_dB_after = candidate;
            }
        }
        
        dB = best_dB_after;
        if (best_weight > 0) {
            result.total_weight += best_weight;
            result.total_probability *= std::pow(2.0, -best_weight);
        }
        
        OperationDiff op1;
        op1.delta_out_A = dA;
        op1.delta_out_B = dB;
        op1.weight = best_weight;
        op1.probability = std::pow(2.0, -best_weight);
        op1.feasible = (best_weight >= 0);
        result.operation_diffs.push_back(op1);
    }
    
    // Op2: A -= R[1] (差分不變)
    // dA 不變
    
    // Op3: A ^= rotl(B, 24)
    dA ^= NeoAlzetteCore::rotl(dB, 24);
    
    // Op4: B ^= rotl(A, 16)
    dB ^= NeoAlzetteCore::rotl(dA, 16);
    
    // Op5-6: 線性層
    dA = diff_through_l1(dA);
    dB = diff_through_l2(dB);
    
    // Op7: 交叉分支注入
    auto [dC0, dD0] = diff_through_cd_from_B(dB);
    dA ^= (NeoAlzetteCore::rotl(dC0, 24) ^ NeoAlzetteCore::rotl(dD0, 16));
    
    // ========== Second subround ==========
    
    // Op8: A += (rotl(B, 31) ^ rotl(B, 17) ^ R[5])
    {
        std::uint32_t beta2 = NeoAlzetteCore::rotl(dB, 31) ^ NeoAlzetteCore::rotl(dB, 17);
        
        std::uint32_t best_dA_after = dA;
        int best_weight = std::numeric_limits<int>::max();
        
        for (std::uint32_t candidate : {dA, dA ^ beta2, NeoAlzetteCore::rotl(dA, 1)}) {
            int w = compute_diff_weight_add(dA, beta2, candidate);
            if (w >= 0 && w < best_weight) {
                best_weight = w;
                best_dA_after = candidate;
            }
        }
        
        dA = best_dA_after;
        if (best_weight > 0) {
            result.total_weight += best_weight;
            result.total_probability *= std::pow(2.0, -best_weight);
        }
        
        OperationDiff op8;
        op8.delta_out_A = dA;
        op8.delta_out_B = dB;
        op8.weight = best_weight;
        op8.probability = std::pow(2.0, -best_weight);
        op8.feasible = (best_weight >= 0);
        result.operation_diffs.push_back(op8);
    }
    
    // Op9: B -= R[6] (差分不變)
    
    // Op10: B ^= rotl(A, 24)
    dB ^= NeoAlzetteCore::rotl(dA, 24);
    
    // Op11: A ^= rotl(B, 16)
    dA ^= NeoAlzetteCore::rotl(dB, 16);
    
    // Op12-13: 線性層
    dB = diff_through_l1(dB);
    dA = diff_through_l2(dA);
    
    // Op14: 交叉分支注入
    auto [dC1, dD1] = diff_through_cd_from_A(dA);
    dB ^= (NeoAlzetteCore::rotl(dC1, 24) ^ NeoAlzetteCore::rotl(dD1, 16));
    
    // Final: 常量XOR (差分不變)
    
    // 設置最終輸出
    result.delta_A_out = dA;
    result.delta_B_out = dB;
    
    return result;
}

// ============================================================================
// 可行性檢查實現
// ============================================================================

bool NeoAlzetteDifferentialModel::is_diff_feasible(
    std::uint32_t delta_in_A,
    std::uint32_t delta_in_B,
    std::uint32_t delta_out_A,
    std::uint32_t delta_out_B,
    int weight_cap
) {
    // 快速檢查：計算詳細差分並驗證權重
    auto detailed = compute_round_diff_detailed(
        delta_in_A, delta_in_B,
        delta_out_A, delta_out_B
    );
    
    // 檢查輸出是否匹配
    bool output_match = (detailed.delta_A_out == delta_out_A) &&
                        (detailed.delta_B_out == delta_out_B);
    
    // 檢查權重是否在限制內
    bool weight_ok = detailed.total_weight < weight_cap;
    
    return output_match && weight_ok;
}

} // namespace neoalz
