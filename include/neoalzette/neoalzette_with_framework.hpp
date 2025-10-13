/**
 * @file neoalzette_with_framework.hpp
 * @brief NeoAlzette與ARX框架的完整集成
 * 
 * 本文件展示如何將NeoAlzette應用到完整的ARX自動化搜索框架：
 * 
 * 【底層ARX算子】→【NeoAlzette模型】→【搜索框架】→【MEDCP/MELCC】
 * 
 * 集成層次：
 * 1. 底層ARX算子（arx_analysis_operators/）
 *    - differential_xdp_add.hpp: LM-2001, O(1)
 *    - differential_addconst.hpp: BvWeight, O(log²n)
 *    - linear_cor_add.hpp: Wallén M_n^T, O(n)
 *    - linear_cor_addconst.hpp: Wallén DP, O(n)
 * 
 * 2. NeoAlzette差分/線性模型（neoalzette/）
 *    - neoalzette_differential.hpp: 使用底層差分算子
 *    - neoalzette_linear.hpp: 使用底層線性算子
 * 
 * 3. 搜索框架（arx_search_framework/）
 *    - pddt/pddt_algorithm1.hpp: pDDT構建
 *    - matsui/matsui_algorithm2.hpp: Matsui閾值搜索
 *    - clat/: cLAT完整框架（Algorithm 1/2/3 + SLR）
 * 
 * 4. 分析器（neoalzette/）
 *    - neoalzette_medcp.hpp: 使用pDDT + Matsui
 *    - neoalzette_melcc.hpp: 使用cLAT + SLR搜索
 */

#pragma once

#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_differential_step.hpp"  // diff_one_round_xdp_32
#include "neoalzette/neoalzette_linear_step.hpp"
// 下列兩個頭僅在示例中提及，實際構建未必需要
// #include "neoalzette/neoalzette_medcp.hpp"
// #include "neoalzette/neoalzette_melcc.hpp"

// 底層ARX算子
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"  // ✅ 使用精確的對數版本
#include "arx_analysis_operators/linear_cor_addconst.hpp"

// 搜索框架
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
#include "arx_search_framework/clat/algorithm1_const.hpp"
#include "arx_search_framework/clat/clat_builder.hpp"
#include "arx_search_framework/clat/clat_search.hpp"
#include "arx_search_framework/medcp_analyzer.hpp"
#include "arx_search_framework/melcc_analyzer.hpp"

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


/**
 * @brief NeoAlzette完整分析管線
 * 
 * 展示如何將所有組件連接到一起
 */
class NeoAlzetteFullPipeline {
public:
    /**
     * @brief 差分分析完整流程：底層算子 → 模型 → 搜索 → MEDCP
     * 
     * 流程：
     * 1. 底層差分算子計算單步差分權重
     * 2. NeoAlzetteDifferentialModel組合成單輪模型
     * 3. pDDT構建預計算表
     * 4. Matsui Algorithm 2搜索最優軌道
     * 5. 計算MEDCP
     */
    struct DifferentialPipeline {
        // 以你的一輪黑盒分析器為核心（不拆不改）
        // 這裡只做“門檻式擴展 + 權重剪枝”，不重建一輪模型。

        struct Config {
            int rounds = 4;
            int weight_cap = 30;          // 門檻：累積權重達上限即剪枝
            std::uint32_t start_dA = 0x00000001u;
            std::uint32_t start_dB = 0x00000000u;
            bool verbose = false;
        };

        struct Node {
            int round;
            std::uint32_t dA;
            std::uint32_t dB;
            int accumulated_weight;
        };

        // 簡單門檻搜索（Matsui Algorithm 2 風格的單分支版）：
        // - 擴展步：直接調用 diff_one_round_xdp_32 取得下一輪 (dA,dB,weight)
        // - 剪枝：acc_weight + step_weight < weight_cap
        // - 終止：達到 rounds 記錄最優；返回 MEDCP = 2^{-best_weight}
        static double run_differential_analysis(int num_rounds) {
            Config cfg;
            cfg.rounds = num_rounds;
            return run_differential_analysis(cfg);
        }

        static double run_differential_analysis(const Config& cfg) {
            auto cmp = [](const Node& a, const Node& b) {
                return a.accumulated_weight > b.accumulated_weight; // 小權重優先
            };
            std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

            pq.push(Node{0, cfg.start_dA, cfg.start_dB, 0});

            int best_weight = std::numeric_limits<int>::max();

            while (!pq.empty()) {
                Node cur = pq.top();
                pq.pop();

                if (cur.accumulated_weight >= best_weight) continue;
                if (cur.accumulated_weight >= cfg.weight_cap) continue;

                if (cur.round >= cfg.rounds) {
                    if (cur.accumulated_weight < best_weight) {
                        best_weight = cur.accumulated_weight;
                    }
                    continue;
                }

                // 一輪黑盒步進（你的函數）
                auto step = NeoAlzetteDifferentialStep::diff_one_round_xdp_32(cur.dA, cur.dB);
                if (step.weight < 0) continue; // 不可行

                int next_w = cur.accumulated_weight + step.weight;
                if (next_w >= cfg.weight_cap) continue; // 門檻剪枝

                pq.push(Node{cur.round + 1, step.dA_out, step.dB_out, next_w});
            }

            if (best_weight == std::numeric_limits<int>::max()) return 0.0; // 找不到路徑
            return std::ldexp(1.0, -best_weight); // MEDCP = 2^{-best_weight}
        }
    };
    
    /**
     * @brief 線性分析完整流程：底層算子 → 模型 → cLAT → MELCC
     * 
     * 流程：
     * 1. 底層線性算子計算單步相關度
     * 2. NeoAlzetteLinearModel組合成單輪模型
     * 3. cLAT構建（Algorithm 2）
     * 4. SLR搜索（Algorithm 3）
     * 5. 計算MELCC
     */
    struct LinearPipeline {
        // Step 1: 底層ARX算子（已在neoalzette_linear.hpp中調用）
        // 使用: linear_cor_add.hpp, linear_cor_addconst.hpp
        
        // Step 2: NeoAlzette線性模型
        NeoAlzetteLinearModel linear_model;
        
        // Step 3: cLAT構建（TODO：需要實現NeoAlzette專用cLAT）
        // 使用: clat_builder.hpp (Algorithm 2)
        cLAT<8> clat;
        
        // Step 4: SLR搜索（TODO：需要整合到NeoAlzette）
        // 使用: clat_search.hpp (Algorithm 3)
        
        // Step 5: MELCC分析
        NeoAlzetteMELCCAnalyzer melcc_analyzer;
        
        /**
         * @brief 執行完整線性分析
         */
        double run_linear_analysis(int num_rounds) {
            // TODO: 實現完整流程
            // 1. 構建cLAT
            // 2. 運行SLR搜索
            // 3. 計算MELCC
            return 0.0;
        }
    };
    
    /**
     * @brief 完整分析：差分 + 線性
     */
    static void run_full_analysis(int num_rounds) {
        DifferentialPipeline diff_pipeline;
        LinearPipeline lin_pipeline;
        
        // 差分分析
        double medcp = diff_pipeline.run_differential_analysis(num_rounds);
        
        // 線性分析
        double melcc = lin_pipeline.run_linear_analysis(num_rounds);
        
        // 輸出結果
        printf("=== NeoAlzette %d輪分析結果 ===\n", num_rounds);
        printf("MEDCP: 2^%.2f\n", -std::log2(medcp));
        printf("MELCC: 2^%.2f\n", -std::log2(melcc));
    }
};

/**
 * @brief 使用示例：如何調用底層ARX算子
 */
class ARXOperatorUsageExample {
public:
    /**
     * @brief 示例1：直接使用底層差分算子
     */
    static void example_differential_operators() {
        using namespace arx_operators;
        
        // 變量-變量：LM-2001, O(1)
        uint32_t alpha = 0x1, beta = 0x1, gamma = 0x2;
        int weight = xdp_add_lm2001(alpha, beta, gamma);
        printf("xdp⁺(%08X, %08X → %08X) = 2^-%d\n", alpha, beta, gamma, weight);
        
        // 變量-常量：BvWeight, O(log²n)
        uint32_t delta_x = 0x1, K = 0x12345678, delta_y = 0x12345679;
        int weight_const = diff_addconst_bvweight(delta_x, K, delta_y);
        printf("xdp⁺const(%08X + %08X → %08X) = 2^-%d\n", delta_x, K, delta_y, weight_const);
    }
    
    /**
     * @brief 示例2：通過NeoAlzette模型使用
     */
    static void example_through_neoalzette_model() {
        // NeoAlzette內部會調用底層ARX算子
        NeoAlzetteDifferentialModel model;
        
        // 這會內部調用 arx_operators::xdp_add_lm2001
        int weight = model.compute_diff_weight_add(0x1, 0x1, 0x2);
        printf("通過NeoAlzette模型: weight = %d\n", weight);
    }
};

} // namespace neoalz
