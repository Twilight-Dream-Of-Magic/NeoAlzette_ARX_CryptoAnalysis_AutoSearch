#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "neoalzette/neoalzette_differential.hpp"
#include "arx_search_framework/threshold_search_framework.hpp"

namespace neoalz {

/**
 * @file neoalzette_medcp_analyzer.hpp
 * @brief NeoAlzette專用的MEDCP（最大期望差分特征概率）分析器
 * 
 * 本分析器：
 * 1. 使用NeoAlzetteDifferentialModel的精確單輪模型
 * 2. 應用論文的Highway/Country Roads搜索策略
 * 3. 計算多輪NeoAlzette的MEDCP
 * 
 * 區別於通用MEDCPAnalyzer：
 * - 專門處理NeoAlzette的複雜操作（模加變量XOR、模減常量、線性層、交叉分支）
 * - 使用bit-vector論文的模加常量模型
 * - 考慮NeoAlzette的SPN結構（不是Feistel）
 */
class NeoAlzetteMEDCPAnalyzer {
public:
    // ========================================================================
    // 配置結構
    // ========================================================================
    
    struct Config {
        int num_rounds = 4;          ///< 分析輪數
        int weight_cap = 30;         ///< 權重上限
        std::uint32_t initial_dA = 0x00000001;  ///< 初始差分A
        std::uint32_t initial_dB = 0;            ///< 初始差分B
        bool use_highway = true;     ///< 是否使用Highway表
        std::string highway_file;    ///< Highway表文件路徑
        bool verbose = false;        ///< 是否輸出詳細信息
    };
    
    /**
     * @brief 差分軌道元素
     */
    struct TrailElement {
        int round;
        std::uint32_t delta_A, delta_B;
        int weight;
        double probability;
    };
    
    /**
     * @brief 完整差分軌道
     */
    struct DifferentialTrail {
        std::vector<TrailElement> elements;
        int total_weight;
        double total_probability;
        
        void add(int round, std::uint32_t dA, std::uint32_t dB, int weight) {
            elements.push_back({round, dA, dB, weight, std::pow(2.0, -weight)});
        }
    };
    
    /**
     * @brief MEDCP分析結果
     */
    struct Result {
        double MEDCP;                ///< 最大期望差分特征概率
        int best_weight;             ///< 最佳權重
        DifferentialTrail best_trail; ///< 最優軌道
        std::uint64_t nodes_visited; ///< 訪問節點數
        std::uint64_t time_ms;       ///< 耗時（毫秒）
        bool search_complete;        ///< 搜索是否完成
        
        // 統計信息
        size_t num_highways;         ///< Highway表大小
        size_t num_country_roads;    ///< Country roads數量
    };
    
    // ========================================================================
    // 主要分析接口
    // ========================================================================
    
    /**
     * @brief 計算NeoAlzette的MEDCP
     * 
     * 完整流程：
     * 1. 可選：構建或加載Highway表（pDDT）
     * 2. 使用Branch-and-bound搜索多輪差分軌道
     * 3. 應用NeoAlzette專門的單輪差分模型
     * 4. 返回MEDCP和最優軌道
     * 
     * @param config 配置參數
     * @return 分析結果
     */
    static Result compute_MEDCP(const Config& config);
    
    /**
     * @brief 驗證差分軌道
     * 
     * 用於驗證搜索結果的正確性
     * 
     * @param trail 差分軌道
     * @return 軌道是否可行
     */
    static bool verify_trail(const DifferentialTrail& trail);
    
    /**
     * @brief 輸出軌道到文件
     * 
     * @param trail 差分軌道
     * @param filename 文件名
     */
    static void export_trail(const DifferentialTrail& trail, const std::string& filename);
    
private:
    // ========================================================================
    // 內部搜索實現
    // ========================================================================
    
    /**
     * @brief 搜索狀態
     */
    struct SearchState {
        int round;
        std::uint32_t delta_A, delta_B;
        int accumulated_weight;
        DifferentialTrail partial_trail;
    };
    
    /**
     * @brief 遞歸搜索實現
     */
    static void search_recursive(
        const Config& config,
        const SearchState& current,
        Result& result
    );
    
    /**
     * @brief 單輪差分枚舉（使用NeoAlzetteDifferentialModel）
     */
    static std::vector<SearchState> enumerate_next_round(
        const SearchState& current,
        const Config& config
    );
};

} // namespace neoalz
