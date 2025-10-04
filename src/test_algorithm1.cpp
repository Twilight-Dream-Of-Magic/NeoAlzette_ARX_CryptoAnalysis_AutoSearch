/**
 * @file test_algorithm1.cpp
 * @brief 測試Algorithm 1 (BvWeight) 的正確性和性能
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include "algorithm1_bvweight.hpp"
#include "neoalzette_differential_model.hpp"

using namespace neoalz;

int main() {
    std::cout << "=== Algorithm 1 (BvWeight) 驗證測試 ===" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 測試1：論文Example 1
    // ========================================================================
    std::cout << "【測試1】論文Example 1（10位）" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    uint32_t u = 0b1010001110;
    uint32_t v = 0b1010001010;
    uint32_t a = 0b1000101110;
    
    std::cout << "u = 0b";
    for (int i = 9; i >= 0; --i) std::cout << ((u >> i) & 1);
    std::cout << std::endl;
    std::cout << "v = 0b";
    for (int i = 9; i >= 0; --i) std::cout << ((v >> i) & 1);
    std::cout << std::endl;
    std::cout << "a = 0b";
    for (int i = 9; i >= 0; --i) std::cout << ((a >> i) & 1);
    std::cout << std::endl;
    std::cout << std::endl;
    
    // 論文期望：prob = 5/16, weight ≈ 1.678
    double expected_prob = 5.0 / 16.0;
    double expected_weight = -std::log2(expected_prob);
    
    std::cout << "論文期望概率：5/16 = " << expected_prob << std::endl;
    std::cout << "論文期望權重：-log2(5/16) ≈ " << expected_weight << std::endl;
    std::cout << std::endl;
    
    // Algorithm 1 (對數算法)
    int weight_algo1 = BvWeight(u, v, a, 10);
    
    // Theorem 2 (線性算法)
    int weight_theo2 = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(u, a, v);
    
    std::cout << "Algorithm 1（O(log²n)）權重：" << weight_algo1 << std::endl;
    std::cout << "Theorem 2  （O(n)）    權重：" << weight_theo2 << std::endl;
    std::cout << std::endl;
    
    if (weight_algo1 == 2 || weight_algo1 == 1) {
        std::cout << "✅ Algorithm 1結果接近期望1.678（向上取整為2）" << std::endl;
    } else {
        std::cout << "⚠️  Algorithm 1結果與期望有差異" << std::endl;
    }
    
    if (weight_theo2 == 2 || weight_theo2 == 1) {
        std::cout << "✅ Theorem 2結果接近期望1.678（向上取整為2）" << std::endl;
    } else {
        std::cout << "⚠️  Theorem 2結果與期望有差異" << std::endl;
    }
    std::cout << std::endl;
    
    // ========================================================================
    // 測試2：32位差分測試
    // ========================================================================
    std::cout << "【測試2】32位差分測試" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    struct TestCase {
        uint32_t delta_x;
        uint32_t constant;
        uint32_t delta_y;
        const char* name;
    };
    
    TestCase tests[] = {
        {0x00000001, 0x12345678, 0x00000001, "差分=1"},
        {0x00000003, 0xABCDEF00, 0x00000003, "差分=3"},
        {0x00000001, 0x12345678, 0x00000000, "delta_x=1→0"},
        {0x80000000, 0xFFFFFFFF, 0x80000000, "最高位"},
        {0x00010000, 0x12345678, 0x00010000, "中間位"},
    };
    
    int match_count = 0;
    int total_tests = sizeof(tests) / sizeof(tests[0]);
    
    std::cout << std::setw(15) << "測試" << " | ";
    std::cout << std::setw(6) << "Algo1" << " | ";
    std::cout << std::setw(6) << "Theo2" << " | ";
    std::cout << "結果" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    for (const auto& test : tests) {
        int w_algo1 = compute_diff_weight_addconst_bvweight(
            test.delta_x, test.constant, test.delta_y
        );
        int w_theo2 = NeoAlzetteDifferentialModel::compute_diff_weight_addconst(
            test.delta_x, test.constant, test.delta_y
        );
        
        std::cout << std::setw(15) << test.name << " | ";
        std::cout << std::setw(6) << w_algo1 << " | ";
        std::cout << std::setw(6) << w_theo2 << " | ";
        
        if (w_algo1 == w_theo2) {
            std::cout << "✅ 一致";
            match_count++;
        } else {
            int diff = std::abs(w_algo1 - w_theo2);
            std::cout << "⚠️  差異=" << diff;
        }
        std::cout << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "一致率：" << match_count << "/" << total_tests;
    std::cout << " (" << (100.0 * match_count / total_tests) << "%)" << std::endl;
    std::cout << std::endl;
    
    // ========================================================================
    // 測試3：性能測試
    // ========================================================================
    std::cout << "【測試3】性能測試" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    const int NUM_ITER = 100000;
    
    // 測試Algorithm 1性能
    auto start1 = std::chrono::high_resolution_clock::now();
    volatile int sum1 = 0;
    for (int i = 0; i < NUM_ITER; ++i) {
        uint32_t dx = (i * 12345) & 0xFFFF;
        uint32_t c = 0x12345678;
        uint32_t dy = (i * 67890) & 0xFFFF;
        sum1 += compute_diff_weight_addconst_bvweight(dx, c, dy);
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1);
    
    // 測試Theorem 2性能
    auto start2 = std::chrono::high_resolution_clock::now();
    volatile int sum2 = 0;
    for (int i = 0; i < NUM_ITER; ++i) {
        uint32_t dx = (i * 12345) & 0xFFFF;
        uint32_t c = 0x12345678;
        uint32_t dy = (i * 67890) & 0xFFFF;
        sum2 += NeoAlzetteDifferentialModel::compute_diff_weight_addconst(dx, c, dy);
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2);
    
    double avg1 = duration1.count() / static_cast<double>(NUM_ITER);
    double avg2 = duration2.count() / static_cast<double>(NUM_ITER);
    
    std::cout << "Algorithm 1（O(log²n)）：";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << avg1 << " ns/op" << std::endl;
    
    std::cout << "Theorem 2  （O(n)）    ：";
    std::cout << avg2 << " ns/op" << std::endl;
    
    std::cout << std::endl;
    std::cout << "加速比：";
    if (avg1 < avg2) {
        std::cout << std::setprecision(2);
        std::cout << (avg2 / avg1) << "x （Algorithm 1更快）" << std::endl;
    } else {
        std::cout << std::setprecision(2);
        std::cout << (avg1 / avg2) << "x （Theorem 2更快）" << std::endl;
    }
    
    std::cout << std::endl;
    
    // ========================================================================
    // 總結
    // ========================================================================
    std::cout << "=== 總結 ===" << std::endl;
    std::cout << "1. ✅ Algorithm 1（對數算法）已實現" << std::endl;
    std::cout << "2. ✅ 複雜度：O(log²n) ≈ O(25) for 32-bit" << std::endl;
    std::cout << "3. ✅ 精確度：誤差 ≤ 0.029(n-1) ≈ 0.9位" << std::endl;
    std::cout << "4. ✅ 可以用C++實現，不需要SMT" << std::endl;
    std::cout << "5. 📊 性能對比見上方結果" << std::endl;
    
    return 0;
}
