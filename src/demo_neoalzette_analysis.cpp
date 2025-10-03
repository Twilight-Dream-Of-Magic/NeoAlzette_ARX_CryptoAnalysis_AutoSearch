/**
 * @file demo_neoalzette_analysis.cpp
 * @brief NeoAlzette MEDCP/MELCC 分析演示程序
 * 
 * 本程序演示如何使用專門為NeoAlzette設計的差分和線性分析器
 * 計算Maximum Expected Differential Characteristic Probability (MEDCP)
 * 和Maximum Expected Linear Characteristic Correlation (MELCC)
 * 
 * 注意：本程序使用控制執行時間的方式，避免計算密集操作長時間運行
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include "neoalzette_medcp_analyzer.hpp"
#include "neoalzette_melcc_analyzer.hpp"
#include "neoalzette_differential_model.hpp"
#include "neoalzette_linear_model.hpp"

using namespace neoalz;

// ============================================================================
// 演示函數
// ============================================================================

/**
 * @brief 演示NeoAlzette單輪差分模型
 */
void demo_single_round_differential() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示1: NeoAlzette單輪差分模型\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    // 測試案例：簡單的單bit差分
    std::uint32_t delta_A_in = 0x00000001;
    std::uint32_t delta_B_in = 0x00000000;
    std::uint32_t delta_A_out = 0x12345678;
    std::uint32_t delta_B_out = 0xABCDEF00;
    
    std::cout << "輸入差分:\n";
    std::cout << "  ΔA_in  = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << delta_A_in << std::dec << "\n";
    std::cout << "  ΔB_in  = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << delta_B_in << std::dec << "\n";
    
    // 計算詳細差分
    auto detailed = NeoAlzetteDifferentialModel::compute_round_diff_detailed(
        delta_A_in, delta_B_in,
        delta_A_out, delta_B_out
    );
    
    std::cout << "\n單輪差分分析結果:\n";
    std::cout << "  ΔA_out = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << detailed.delta_A_out << std::dec << "\n";
    std::cout << "  ΔB_out = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << detailed.delta_B_out << std::dec << "\n";
    std::cout << "  總權重 = " << detailed.total_weight << "\n";
    std::cout << "  總概率 = 2^{-" << detailed.total_weight << "} ≈ " 
              << detailed.total_probability << "\n";
    std::cout << "  操作數 = " << detailed.operation_diffs.size() << "\n";
    
    // 顯示中間操作
    std::cout << "\n中間操作詳情:\n";
    for (size_t i = 0; i < detailed.operation_diffs.size(); ++i) {
        const auto& op = detailed.operation_diffs[i];
        std::cout << "  操作 " << (i+1) << ": weight=" << op.weight 
                  << ", prob=2^{-" << op.weight << "}"
                  << ", feasible=" << (op.feasible ? "是" : "否") << "\n";
    }
}

/**
 * @brief 演示bit-vector模型：模加常量差分分析
 */
void demo_addconst_differential() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示2: 模加常量差分分析（Bit-Vector模型）\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::uint32_t delta_x = 0x00000001;
    std::uint32_t constant = 0xDEADBEEF;
    std::uint32_t delta_y = 0x00000001;  // 期望輸出差分
    
    std::cout << "測試: X + C → Y 的差分\n";
    std::cout << "  ΔX = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << delta_x << std::dec << "\n";
    std::cout << "  C  = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << constant << std::dec << " (常量)\n";
    std::cout << "  ΔY = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << delta_y << std::dec << "\n";
    
    int weight = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(
        delta_x, constant, delta_y
    );
    
    std::cout << "\n分析結果:\n";
    if (weight >= 0) {
        std::cout << "  可行性: 是\n";
        std::cout << "  權重: " << weight << "\n";
        std::cout << "  概率: 2^{-" << weight << "} ≈ " << std::pow(2.0, -weight) << "\n";
    } else {
        std::cout << "  可行性: 否（不可能的差分）\n";
    }
    
    // 測試模減常量
    std::cout << "\n測試: X - C → Y 的差分\n";
    std::cout << "  ΔX = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << delta_x << std::dec << "\n";
    std::cout << "  C  = 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << constant << std::dec << " (常量)\n";
    
    int weight_sub = NeoAlzetteDifferentialModel::compute_diff_weight_subconst(
        delta_x, constant, delta_y
    );
    
    std::cout << "\n模減分析結果:\n";
    if (weight_sub >= 0) {
        std::cout << "  可行性: 是\n";
        std::cout << "  權重: " << weight_sub << "\n";
        std::cout << "  說明: 模減常量不改變差分（常量的差分為0）\n";
    } else {
        std::cout << "  可行性: 否\n";
    }
}

/**
 * @brief 演示線性層的掩碼傳播
 */
void demo_linear_layer_mask_propagation() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示3: 線性層的掩碼傳播\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::uint32_t mask_in = 0x00000001;
    
    std::cout << "輸入掩碼: 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << mask_in << std::dec << "\n";
    
    // l1_forward掩碼傳播
    std::uint32_t mask_out_l1 = NeoAlzetteLinearModel::mask_through_l1(mask_in);
    std::cout << "\nl1_forward掩碼傳播:\n";
    std::cout << "  l1(x) = x ^ rotl(x,2) ^ rotl(x,10) ^ rotl(x,18) ^ rotl(x,24)\n";
    std::cout << "  輸出掩碼: 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << mask_out_l1 << std::dec << "\n";
    std::cout << "  Hamming weight: " << __builtin_popcount(mask_out_l1) << "\n";
    
    // l2_forward掩碼傳播
    std::uint32_t mask_out_l2 = NeoAlzetteLinearModel::mask_through_l2(mask_in);
    std::cout << "\nl2_forward掩碼傳播:\n";
    std::cout << "  l2(x) = x ^ rotl(x,8) ^ rotl(x,14) ^ rotl(x,22) ^ rotl(x,30)\n";
    std::cout << "  輸出掩碼: 0x" << std::hex << std::setw(8) << std::setfill('0') 
              << mask_out_l2 << std::dec << "\n";
    std::cout << "  Hamming weight: " << __builtin_popcount(mask_out_l2) << "\n";
    
    std::cout << "\n說明:\n";
    std::cout << "  - 線性層的掩碼傳播是確定性的（無概率損失）\n";
    std::cout << "  - 輸出掩碼是輸入掩碼的線性變換\n";
    std::cout << "  - 線性相關性 = 1.0（不減弱）\n";
}

/**
 * @brief 演示MEDCP計算（限制執行時間）
 */
void demo_medcp_computation() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示4: MEDCP計算（NeoAlzette專用分析器）\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    // 配置：限制輪數和權重，避免計算密集
    NeoAlzetteMEDCPAnalyzer::Config config;
    config.num_rounds = 2;        // 只分析2輪（避免計算量過大）
    config.weight_cap = 15;       // 較低的權重上限
    config.initial_dA = 0x00000001;
    config.initial_dB = 0x00000000;
    config.use_highway = false;   // 不使用highway表（簡化）
    config.verbose = true;
    
    std::cout << "配置:\n";
    std::cout << "  輪數: " << config.num_rounds << "\n";
    std::cout << "  權重上限: " << config.weight_cap << "\n";
    std::cout << "  初始差分: ΔA=0x" << std::hex << config.initial_dA 
              << " ΔB=0x" << config.initial_dB << std::dec << "\n";
    std::cout << "\n開始MEDCP搜索...\n";
    
    // 執行分析
    auto result = NeoAlzetteMEDCPAnalyzer::compute_MEDCP(config);
    
    std::cout << "\n=== MEDCP分析結果 ===\n";
    std::cout << "最佳權重: " << result.best_weight << "\n";
    std::cout << "MEDCP: 2^{-" << result.best_weight << "} ≈ " << result.MEDCP << "\n";
    std::cout << "訪問節點數: " << result.nodes_visited << "\n";
    std::cout << "執行時間: " << result.time_ms << " ms\n";
    std::cout << "搜索完成: " << (result.search_complete ? "是" : "否") << "\n";
    
    // 顯示最佳軌道
    if (!result.best_trail.elements.empty()) {
        std::cout << "\n最佳差分軌道:\n";
        for (const auto& elem : result.best_trail.elements) {
            std::cout << "  Round " << elem.round << ": "
                      << "ΔA=0x" << std::hex << std::setw(8) << std::setfill('0') << elem.delta_A
                      << " ΔB=0x" << std::setw(8) << std::setfill('0') << elem.delta_B
                      << std::dec << " weight=" << elem.weight << "\n";
        }
    }
}

/**
 * @brief 演示MELCC計算（矩陣乘法鏈方法）
 */
void demo_melcc_computation() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示5: MELCC計算（矩陣乘法鏈方法）\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    // 配置
    NeoAlzetteMELCCAnalyzer::Config config;
    config.num_rounds = 4;
    config.use_matrix_chain = true;  // 使用精確的矩陣乘法鏈
    config.verbose = true;
    
    std::cout << "配置:\n";
    std::cout << "  輪數: " << config.num_rounds << "\n";
    std::cout << "  方法: 矩陣乘法鏈（精確方法）\n";
    std::cout << "\n開始MELCC計算...\n";
    
    // 執行分析
    auto result = NeoAlzetteMELCCAnalyzer::compute_MELCC(config);
    
    std::cout << "\n=== MELCC分析結果 ===\n";
    std::cout << "MELCC: " << result.MELCC << "\n";
    std::cout << "Log2(MELCC): " << std::log2(result.MELCC) << "\n";
    std::cout << "執行時間: " << result.time_ms << " ms\n";
    
    // 顯示矩陣序列
    std::cout << "\n相關性矩陣序列:\n";
    for (size_t r = 0; r < result.matrices.size(); ++r) {
        std::cout << "  Round " << r << ":\n";
        std::cout << "    [" << result.matrices[r](0, 0) << "  " << result.matrices[r](0, 1) << "]\n";
        std::cout << "    [" << result.matrices[r](1, 0) << "  " << result.matrices[r](1, 1) << "]\n";
        std::cout << "    最大相關性: " << result.matrices[r].max_abs_correlation() << "\n";
    }
}

/**
 * @brief 演示Wallén線性可行性檢查
 */
void demo_wallen_feasibility() {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "演示6: Wallén線性逼近可行性檢查\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::uint32_t mu = 0x00000001;
    std::uint32_t nu = 0x00000001;
    std::uint32_t omega = 0x00000000;
    
    std::cout << "線性逼近三元組 (μ, ν, ω):\n";
    std::cout << "  μ = 0x" << std::hex << std::setw(8) << std::setfill('0') << mu << std::dec << "\n";
    std::cout << "  ν = 0x" << std::hex << std::setw(8) << std::setfill('0') << nu << std::dec << "\n";
    std::cout << "  ω = 0x" << std::hex << std::setw(8) << std::setfill('0') << omega << std::dec << "\n";
    
    // 計算M_n^T
    std::uint32_t v = mu ^ nu ^ omega;
    std::uint32_t z_star = NeoAlzetteLinearModel::compute_MnT(v);
    
    std::cout << "\nWallén算法分析:\n";
    std::cout << "  v = μ⊕ν⊕ω = 0x" << std::hex << std::setw(8) << std::setfill('0') << v << std::dec << "\n";
    std::cout << "  z* = M_n^T(v) = 0x" << std::hex << std::setw(8) << std::setfill('0') << z_star << std::dec << "\n";
    
    // 檢查可行性
    bool feasible = NeoAlzetteLinearModel::is_linear_approx_feasible(mu, nu, omega);
    
    std::cout << "\n可行性檢查:\n";
    std::cout << "  條件: (μ⊕ω) ⪯ z* AND (ν⊕ω) ⪯ z*\n";
    std::cout << "  結果: " << (feasible ? "可行" : "不可行") << "\n";
    
    if (feasible) {
        double corr = NeoAlzetteLinearModel::compute_linear_correlation(mu, nu, omega);
        std::cout << "  相關性: " << corr << "\n";
        std::cout << "  Log2(corr): " << std::log2(std::abs(corr)) << "\n";
    }
}

// ============================================================================
// 主程序
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  NeoAlzette 密碼分析演示程序                                      ║\n";
    std::cout << "║  MEDCP/MELCC 計算 - 基於11篇ARX密碼分析論文                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
    
    try {
        // 演示1: 單輪差分模型
        demo_single_round_differential();
        
        // 演示2: 模加常量差分（bit-vector模型）
        demo_addconst_differential();
        
        // 演示3: 線性層掩碼傳播
        demo_linear_layer_mask_propagation();
        
        // 演示4: MEDCP計算（限制2輪）
        demo_medcp_computation();
        
        // 演示5: MELCC計算（矩陣乘法鏈）
        demo_melcc_computation();
        
        // 演示6: Wallén可行性檢查
        demo_wallen_feasibility();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "所有演示完成！\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "重要說明:\n";
        std::cout << "1. 本演示使用了NeoAlzette專門的差分和線性模型\n";
        std::cout << "2. 差分分析完整處理：模加變量XOR、模減常量、線性層、交叉分支\n";
        std::cout << "3. 線性分析使用Wallén算法和矩陣乘法鏈（MIQCP論文方法）\n";
        std::cout << "4. 計算量已控制在合理範圍（2-4輪，權重上限15）\n";
        std::cout << "5. 完整分析需要更多輪數和更高權重上限\n";
        std::cout << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "錯誤: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
