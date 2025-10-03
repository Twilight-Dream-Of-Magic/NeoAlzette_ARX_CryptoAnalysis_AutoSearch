#include "neoalzette_linear_model.hpp"
#include <algorithm>
#include <cstring>

namespace neoalz {

// ============================================================================
// 相關性矩陣構建
// ============================================================================

NeoAlzetteLinearModel::CorrelationMatrix<2> 
NeoAlzetteLinearModel::build_round_correlation_matrix(int round) {
    CorrelationMatrix<2> M;
    
    // NeoAlzette單輪的線性相關性矩陣
    // 這是一個簡化版本，完整版本需要詳細分析每個操作的線性性質
    
    // 對於ARX密碼，2×2矩陣的典型形式：
    // M = [c00  c01]
    //     [c10  c11]
    // 
    // 其中c_ij表示特定掩碼配置下的相關性
    
    // 啟發式值（基於Alzette論文的典型值）
    // 需要通過實驗確定精確值
    double base_correlation = std::pow(2.0, -2);  // 單輪典型相關性 ≈ 2^{-2}
    
    M(0, 0) = base_correlation;
    M(0, 1) = base_correlation * 0.5;
    M(1, 0) = base_correlation * 0.5;
    M(1, 1) = base_correlation;
    
    return M;
}

// ============================================================================
// 多輪MELCC計算（矩陣乘法鏈）
// ============================================================================

double NeoAlzetteLinearModel::compute_MELCC_via_matrix_chain(int rounds) {
    if (rounds <= 0) return 1.0;
    
    // 構建第一輪的相關性矩陣
    CorrelationMatrix<2> M_total = build_round_correlation_matrix(0);
    
    // 矩陣乘法鏈：M_total = M_1 ⊗ M_2 ⊗ ... ⊗ M_R
    for (int r = 1; r < rounds; ++r) {
        CorrelationMatrix<2> M_r = build_round_correlation_matrix(r);
        M_total = M_total * M_r;
    }
    
    // 返回最大絕對相關性
    return M_total.max_abs_correlation();
}

} // namespace neoalz
