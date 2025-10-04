/**
 * @file test_theorem2_correctness.cpp
 * @brief 驗證Theorem 2實現的正確性
 * 
 * 測試：
 * 1. 論文Example 1的精確值
 * 2. 對比LM簡化方法的錯誤
 * 3. 多個隨機測試
 */

#include <iostream>
#include <iomanip>
#include <bitset>
#include <cstdint>
#include <cmath>
#include "neoalzette_differential_model.hpp"

using namespace neoalz;

void test_paper_example() {
    std::cout << "=== Test 1: 論文Example 1 ===" << std::endl;
    
    // 論文Example 1 (10位)
    uint32_t u = 0b1010001110;
    uint32_t v = 0b1010001010;
    uint32_t a = 0b1000101110;
    
    // 通過compute_diff_weight_addconst來測試
    int weight_int = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(u, a, v);
    // 手動計算prob（逆推）
    double prob = (weight_int == -1) ? 0.0 : std::pow(2.0, -static_cast<double>(weight_int));
    double weight = -std::log2(prob);
    
    std::cout << "輸入差分 u: 0b" << std::bitset<10>(u) << std::endl;
    std::cout << "輸出差分 v: 0b" << std::bitset<10>(v) << std::endl;
    std::cout << "常量     a: 0b" << std::bitset<10>(a) << std::endl;
    std::cout << "概率: " << prob << std::endl;
    std::cout << "權重: " << weight << std::endl;
    std::cout << "論文期望: prob = 5/16 = 0.3125, weight ≈ 1.678" << std::endl;
    
    double expected_prob = 5.0 / 16.0;
    double expected_weight = -std::log2(expected_prob);
    
    std::cout << "誤差: prob差 = " << std::abs(prob - expected_prob) 
              << ", weight差 = " << std::abs(weight - expected_weight) << std::endl;
    
    if (std::abs(prob - expected_prob) < 0.001) {
        std::cout << "✅ 通過！" << std::endl;
    } else {
        std::cout << "❌ 失敗！" << std::endl;
    }
    std::cout << std::endl;
}

void test_lm_vs_theorem2() {
    std::cout << "=== Test 2: 對比LM簡化方法的錯誤 ===" << std::endl;
    
    // 32位隨機測試
    uint32_t test_cases[][3] = {
        {0x12345678, 0xABCDEF00, 0x11111111},
        {0x00000001, 0x00000001, 0xFFFFFFFF},
        {0xFFFFFFFF, 0x00000000, 0x12345678},
        {0x80000000, 0x80000000, 0x80000000},
        {0x00000F00, 0x00000F00, 0x00000AAA},
    };
    
    int correct_count = 0;
    int wrong_count = 0;
    
    for (const auto& tc : test_cases) {
        uint32_t u = tc[0];
        uint32_t v = tc[1];
        uint32_t a = tc[2];
        
        // 正確方法（Theorem 2）
        int w_correct = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(u, a, v);
        
        // 錯誤方法（LM簡化，設β=0）
        int w_wrong = NeoAlzetteDifferentialModel::compute_diff_weight_add(u, 0, v);
        
        std::cout << std::hex << std::setfill('0');
        std::cout << "u=0x" << std::setw(8) << u 
                  << ", v=0x" << std::setw(8) << v
                  << ", a=0x" << std::setw(8) << a << std::dec << std::endl;
        std::cout << "  Theorem 2權重: " << w_correct << std::endl;
        std::cout << "  LM簡化權重:    " << w_wrong << std::endl;
        
        if (w_correct != w_wrong) {
            std::cout << "  ⚠️ 差異: " << (w_correct - w_wrong) << std::endl;
            wrong_count++;
        } else {
            std::cout << "  相同" << std::endl;
            correct_count++;
        }
    }
    
    std::cout << "\n統計：相同=" << correct_count << ", 不同=" << wrong_count << std::endl;
    std::cout << "論文預測：對8位常量，~50%不同；對32位可能更高" << std::endl;
    std::cout << std::endl;
}

void test_subtraction_constant() {
    std::cout << "=== Test 3: 模減常量測試 ===" << std::endl;
    
    uint32_t u = 0x12345678;
    uint32_t c = 0xABCDEF00;
    uint32_t v = 0x87654321;
    
    // 模減常量應該轉換為模加補數
    int w_sub = NeoAlzetteDifferentialModel::compute_diff_weight_subconst(u, c, v);
    
    // 手動驗證：X - C = X + (~C + 1)
    uint32_t addend = (~c + 1) & 0xFFFFFFFF;
    int w_add = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(u, addend, v);
    
    std::cout << std::hex << std::setfill('0');
    std::cout << "u=0x" << std::setw(8) << u << std::endl;
    std::cout << "c=0x" << std::setw(8) << c << std::endl;
    std::cout << "v=0x" << std::setw(8) << v << std::endl;
    std::cout << "補數=0x" << std::setw(8) << addend << std::dec << std::endl;
    std::cout << "模減權重: " << w_sub << std::endl;
    std::cout << "模加權重: " << w_add << std::endl;
    
    if (w_sub == w_add) {
        std::cout << "✅ 模減=模加補數，正確！" << std::endl;
    } else {
        std::cout << "❌ 錯誤！" << std::endl;
    }
    std::cout << std::endl;
}

void test_zero_constant() {
    std::cout << "=== Test 4: 常量=0的特殊情況 ===" << std::endl;
    
    uint32_t u = 0x12345678;
    uint32_t v = 0x87654321;
    uint32_t a = 0x00000000;
    
    // 當常量=0時，應該退化為恆等映射
    int w_theorem2 = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(u, a, v);
    
    std::cout << std::hex << std::setfill('0');
    std::cout << "u=0x" << std::setw(8) << u << std::endl;
    std::cout << "v=0x" << std::setw(8) << v << std::endl;
    std::cout << "a=0x00000000" << std::dec << std::endl;
    std::cout << "權重: " << w_theorem2 << std::endl;
    
    if (u == v && w_theorem2 == 0) {
        std::cout << "✅ u=v時權重=0，正確！" << std::endl;
    } else if (u != v && w_theorem2 == -1) {
        std::cout << "✅ u≠v時不可行，正確！" << std::endl;
    } else {
        std::cout << "結果: " << (w_theorem2 == -1 ? "不可行" : "可行") << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  Theorem 2 正確性驗證測試                             ║\n";
    std::cout << "║  驗證Bit-Vector論文的精確實現                         ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_paper_example();
    test_lm_vs_theorem2();
    test_subtraction_constant();
    test_zero_constant();
    
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║  測試完成                                             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
    
    return 0;
}
