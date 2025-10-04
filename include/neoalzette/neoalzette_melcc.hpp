#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include "neoalzette/neoalzette_linear.hpp"

namespace neoalz {

/**
 * @file neoalzette_melcc_analyzer.hpp
 * @brief NeoAlzette專用的MELCC（最大期望線性特征相關性）分析器
 * 
 * 本分析器：
 * 1. 使用NeoAlzetteLinearModel的精確線性模型
 * 2. 應用Wallén論文的線性枚舉方法
 * 3. 使用矩陣乘法鏈精確計算MELCC（基於MIQCP論文）
 * 
 * 區別於通用線性分析：
 * - 專門處理NeoAlzette的線性層和交叉分支
 * - 使用2×2相關性矩陣表示
 * - 精確的矩陣乘法鏈計算
 */
class NeoAlzetteMELCCAnalyzer {
public:
    // ========================================================================
    // 配置結構
    // ========================================================================
    
    struct Config {
        int num_rounds = 4;          ///< 分析輪數
        double correlation_threshold = 0.001;  ///< 相關性閾值
        std::uint32_t initial_mask_A = 0x00000001;  ///< 初始掩碼A
        std::uint32_t initial_mask_B = 0;           ///< 初始掩碼B
        bool use_matrix_chain = true;  ///< 是否使用矩陣乘法鏈
        bool verbose = false;         ///< 是否輸出詳細信息
    };
    
    /**
     * @brief 線性軌道元素
     */
    struct TrailElement {
        int round;
        std::uint32_t mask_A, mask_B;
        double correlation;
        
        TrailElement() = default;
        TrailElement(int r, std::uint32_t mA, std::uint32_t mB, double corr)
            : round(r), mask_A(mA), mask_B(mB), correlation(corr) {}
    };
    
    /**
     * @brief 完整線性軌道
     */
    struct LinearTrail {
        std::vector<TrailElement> elements;
        double total_correlation;
        
        void add(int round, std::uint32_t mA, std::uint32_t mB, double corr) {
            elements.push_back({round, mA, mB, corr});
        }
        
        void compute_total() {
            total_correlation = 1.0;
            for (const auto& elem : elements) {
                total_correlation *= elem.correlation;
            }
        }
    };
    
    /**
     * @brief MELCC分析結果
     */
    struct Result {
        double MELCC;                ///< 最大期望線性特征相關性
        LinearTrail best_trail;      ///< 最優軌道
        std::uint64_t nodes_visited; ///< 訪問節點數
        std::uint64_t time_ms;       ///< 耗時（毫秒）
        bool search_complete;        ///< 搜索是否完成
        
        // 矩陣鏈方法的結果
        std::vector<NeoAlzetteLinearModel::CorrelationMatrix<2>> matrices;  ///< 每輪的相關性矩陣
    };
    
    // ========================================================================
    // 主要分析接口
    // ========================================================================
    
    /**
     * @brief 計算NeoAlzette的MELCC
     * 
     * 兩種方法：
     * 1. 矩陣乘法鏈（use_matrix_chain=true）：精確方法
     * 2. 搜索方法（use_matrix_chain=false）：啟發式搜索
     * 
     * @param config 配置參數
     * @return 分析結果
     */
    static Result compute_MELCC(const Config& config);
    
    /**
     * @brief 使用矩陣乘法鏈計算MELCC（精確方法）
     * 
     * 基於MIQCP論文：M_total = M_1 ⊗ M_2 ⊗ ... ⊗ M_R
     * 
     * @param num_rounds 輪數
     * @return MELCC值
     */
    static double compute_MELCC_matrix_chain(int num_rounds);
    
    /**
     * @brief 使用搜索方法計算MELCC（啟發式）
     * 
     * 基於Wallén枚舉+Branch-and-bound
     * 
     * @param config 配置參數
     * @return 分析結果
     */
    static Result compute_MELCC_search(const Config& config);
    
    /**
     * @brief 驗證線性軌道
     * 
     * @param trail 線性軌道
     * @return 軌道是否可行
     */
    static bool verify_trail(const LinearTrail& trail);
    
    /**
     * @brief 輸出軌道到文件
     * 
     * @param trail 線性軌道
     * @param filename 文件名
     */
    static void export_trail(const LinearTrail& trail, const std::string& filename);
    
private:
    // ========================================================================
    // 內部搜索實現
    // ========================================================================
    
    struct SearchState {
        int round;
        std::uint32_t mask_A, mask_B;
        double accumulated_correlation;
        LinearTrail partial_trail;
    };
    
    static std::vector<SearchState> enumerate_next_round_linear(
        const SearchState& current,
        const Config& config
    );
};

} // namespace neoalz
