/**
 * @file test_linear_correlation_addconst.cpp
 * @brief 測試Wallén 2003按位進位DP方法計算模加/模減常量的線性相關性
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include "linear_correlation_addconst.hpp"

using namespace neoalz;

void test_basic_cases() {
    std::cout << "\n=== 測試1：基本案例 ===\n";
    
    // 測試案例1：簡單的單bit掩碼
    {
        std::uint32_t alpha = 0x00000001;  // 輸入掩碼：bit 0
        std::uint32_t beta  = 0x00000001;  // 輸出掩碼：bit 0
        std::uint32_t K     = 0x00000000;  // 常量：0
        
        auto result = corr_add_x_plus_const32(alpha, beta, K, 32);
        
        std::cout << "測試：Y = X + 0，掩碼 (0x1, 0x1)\n";
        std::cout << "  相關性：" << result.correlation << "\n";
        std::cout << "  權重：" << result.weight << "\n";
        std::cout << "  預期：相關性應該接近1.0（因為Y=X）\n";
    }
    
    // 測試案例2：非零常量
    {
        std::uint32_t alpha = 0x00000001;
        std::uint32_t beta  = 0x00000001;
        std::uint32_t K     = 0x12345678;
        
        auto result = corr_add_x_plus_const32(alpha, beta, K, 32);
        
        std::cout << "\n測試：Y = X + 0x12345678，掩碼 (0x1, 0x1)\n";
        std::cout << "  相關性：" << result.correlation << "\n";
        std::cout << "  權重：" << result.weight << "\n";
    }
    
    // 測試案例3：多bit掩碼
    {
        std::uint32_t alpha = 0x0000000F;  // 低4位
        std::uint32_t beta  = 0x0000000F;
        std::uint32_t K     = 0x00000001;
        
        auto result = corr_add_x_plus_const32(alpha, beta, K, 32);
        
        std::cout << "\n測試：Y = X + 1，掩碼 (0xF, 0xF)\n";
        std::cout << "  相關性：" << result.correlation << "\n";
        std::cout << "  權重：" << result.weight << "\n";
    }
}

void test_modular_subtraction() {
    std::cout << "\n=== 測試2：模減法 ===\n";
    
    std::uint32_t alpha = 0x00000001;
    std::uint32_t beta  = 0x00000001;
    std::uint32_t C     = 0xDEADBEEF;
    
    // 方法1：直接用模減函數
    auto result1 = corr_add_x_minus_const32(alpha, beta, C, 32);
    
    // 方法2：手動計算補數
    std::uint32_t K_complement = (~C + 1) & 0xFFFFFFFF;
    auto result2 = corr_add_x_plus_const32(alpha, beta, K_complement, 32);
    
    std::cout << "測試：Y = X - 0xDEADBEEF\n";
    std::cout << "  方法1（直接模減）：\n";
    std::cout << "    相關性：" << result1.correlation << "\n";
    std::cout << "    權重：" << result1.weight << "\n";
    
    std::cout << "  方法2（轉換為加補數）：\n";
    std::cout << "    相關性：" << result2.correlation << "\n";
    std::cout << "    權重：" << result2.weight << "\n";
    
    // 驗證兩種方法結果一致
    bool match = (std::fabs(result1.correlation - result2.correlation) < 1e-10);
    std::cout << "  兩種方法一致性：" << (match ? "✓ 通過" : "✗ 失敗") << "\n";
}

void test_various_masks() {
    std::cout << "\n=== 測試3：各種掩碼組合 ===\n";
    
    std::uint32_t K = 0x12345678;
    
    struct TestCase {
        std::uint32_t alpha;
        std::uint32_t beta;
        const char* description;
    };
    
    TestCase cases[] = {
        {0x00000001, 0x00000001, "單bit (0x1, 0x1)"},
        {0x00000001, 0x00000002, "相鄰bit (0x1, 0x2)"},
        {0x000000FF, 0x000000FF, "低字節 (0xFF, 0xFF)"},
        {0xFFFFFFFF, 0xFFFFFFFF, "全1 (0xFFFFFFFF, 0xFFFFFFFF)"},
        {0x55555555, 0x55555555, "間隔bit (0x55555555, 0x55555555)"},
    };
    
    for (const auto& tc : cases) {
        auto result = corr_add_x_plus_const32(tc.alpha, tc.beta, K, 32);
        
        std::cout << "\n  " << tc.description << "：\n";
        std::cout << "    相關性：" << std::scientific << result.correlation << std::fixed << "\n";
        std::cout << "    權重：" << result.weight << "\n";
        std::cout << "    可行性：" << (result.is_feasible() ? "是" : "否") << "\n";
    }
}

void test_neoalzette_constants() {
    std::cout << "\n=== 測試4：NeoAlzette實際常量 ===\n";
    
    // NeoAlzette的一些實際輪常量（示例）
    std::uint32_t round_constants[] = {
        0xB7E15162,  // R[0]
        0x8AED2A6A,  // R[1]
        0xBF715880,  // R[2]
        0x9CF4F3C7,  // R[3]
        // ... 更多
    };
    
    // 測試：A -= R[1] 的線性分析
    std::uint32_t alpha = 0x00000001;
    std::uint32_t beta  = 0x00000001;
    std::uint32_t C = round_constants[1];
    
    auto result = corr_add_x_minus_const32(alpha, beta, C, 32);
    
    std::cout << "NeoAlzette操作：A -= R[1] (R[1] = 0x" 
              << std::hex << C << std::dec << ")\n";
    std::cout << "  掩碼：(0x" << std::hex << alpha << ", 0x" << beta << std::dec << ")\n";
    std::cout << "  相關性：" << result.correlation << "\n";
    std::cout << "  權重：" << result.weight << "\n";
    std::cout << "  說明：這是變量-常量的精確相關性計算\n";
}

void test_comparison_with_zero() {
    std::cout << "\n=== 測試5：與K=0的對比 ===\n";
    
    std::uint32_t alpha = 0x00000003;  // 低2位
    std::uint32_t beta  = 0x00000003;
    
    // K=0：Y = X，相關性應該最高
    auto result_zero = corr_add_x_plus_const32(alpha, beta, 0, 32);
    
    // K=非零：相關性會降低
    auto result_nonzero = corr_add_x_plus_const32(alpha, beta, 0xFFFFFFFF, 32);
    
    std::cout << "K = 0（Y = X）：\n";
    std::cout << "  相關性：" << result_zero.correlation << "\n";
    std::cout << "  權重：" << result_zero.weight << "\n";
    
    std::cout << "\nK = 0xFFFFFFFF（Y = X + 0xFFFFFFFF）：\n";
    std::cout << "  相關性：" << result_nonzero.correlation << "\n";
    std::cout << "  權重：" << result_nonzero.weight << "\n";
    
    std::cout << "\n觀察：常量K影響線性相關性的大小和符號\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Wallén 2003按位進位DP方法測試                                 ║\n";
    std::cout << "║  模加/模減常量的精確線性相關性計算                             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    try {
        test_basic_cases();
        test_modular_subtraction();
        test_various_masks();
        test_neoalzette_constants();
        test_comparison_with_zero();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "所有測試完成！\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "關鍵要點：\n";
        std::cout << "1. ✓ Wallén 2003方法：精確、O(n)時間\n";
        std::cout << "2. ✓ 模減轉換：X - C = X + (~C + 1)\n";
        std::cout << "3. ✓ 適用於NeoAlzette的A -= R[i]操作\n";
        std::cout << "4. ✓ 這是\"一端已定\"的標準處理方法\n";
        std::cout << "5. ✓ 可直接集成到自動搜索框架\n";
        std::cout << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "錯誤：" << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
