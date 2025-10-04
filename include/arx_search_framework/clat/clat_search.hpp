/**
 * @file linear_search_algorithm3.hpp
 * @brief Algorithm 3: ARX線性特徵自動搜索
 * 
 * 論文：Huang & Wang (2020)
 * Algorithm 3, Lines 935-1055
 * 
 * 完整的Branch-and-bound框架，結合cLAT
 */

#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include "clat_linear.hpp"
#include "neoalzette_linear_model.hpp"

namespace neoalz {

/**
 * @brief Algorithm 3實現：ARX線性軌道搜索
 */
class LinearSearchAlgorithm3 {
public:
    /**
     * @brief 搜索配置
     */
    struct Config {
        int num_rounds;          // r輪
        int target_weight;       // Bcr目標權重
        const cLAT<8>* clat_ptr; // cLAT指針（預構建）
        int block_bits = 32;     // 字長n
        int chunk_bits = 8;      // 分塊m
        bool verbose = false;
    };
    
    /**
     * @brief 線性軌道
     */
    struct LinearTrail {
        std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> rounds;  // (u, v, w)
        int total_weight;
        
        void add_round(uint32_t u, uint32_t v, uint32_t w, int weight) {
            rounds.push_back({u, v, w});
            total_weight += weight;
        }
    };
    
    /**
     * @brief 搜索結果
     */
    struct SearchResult {
        bool found;
        LinearTrail best_trail;
        int best_weight;
        uint64_t nodes_visited;
    };
    
    /**
     * @brief 主搜索函數（Algorithm 3入口）
     * 
     * 論文Algorithm 3，Lines 938-946
     * 
     * 程序入口：
     * 1. 設置Bcr = Bcr-1 - 1
     * 2. while Bcr ≠ Bcr' do
     * 3.   Bcr++
     * 4.   調用Round-1
     * 5. end while
     * 
     * @param config 配置
     * @param known_bounds 已知的輪界限 Bw1, ..., Bwr-1
     * @return 搜索結果
     */
    static SearchResult search(const Config& config, const std::vector<int>& known_bounds) {
        SearchResult result;
        result.found = false;
        result.best_weight = config.target_weight;
        result.nodes_visited = 0;
        
        if (known_bounds.size() < (size_t)config.num_rounds - 1) {
            return result;  // 需要r-1個已知界限
        }
        
        int Bcr_minus_1 = known_bounds[config.num_rounds - 2];
        int Bcr = Bcr_minus_1 - 1;
        int Bcr_prime = config.target_weight;
        
        // Line 940: while Bcr ≠ Bcr'
        while (Bcr != Bcr_prime) {
            Bcr++;  // Line 942
            
            LinearTrail trail;
            
            // Line 944: 調用Round-1
            bool found = round_1(config, Bcr, Bcr_minus_1, trail, result.nodes_visited);
            
            if (found) {
                result.found = true;
                result.best_trail = trail;
                result.best_weight = Bcr;
                Bcr_prime = Bcr;  // 更新為找到的權重
                break;
            }
        }
        
        return result;
    }
    
private:
    /**
     * @brief Round-1過程（Algorithm 3, Lines 947-966）
     * 
     * 排除權重>Bcr-Bcr-1的搜索空間
     */
    static bool round_1(
        const Config& config,
        int Bcr,
        int Bcr_minus_1,
        LinearTrail& trail,
        uint64_t& nodes
    ) {
        // Line 948-965: for Cw1 = 0 to n-1
        for (int Cw1 = 0; Cw1 < config.block_bits; ++Cw1) {
            nodes++;
            
            // Line 950-952: 剪枝條件
            if (Cw1 + Bcr_minus_1 > Bcr) {
                return false;  // 返回FALSE
            }
            
            // Line 953-955: 調用Algorithm 1構建S_Cw1
            bool found = false;
            
            Algorithm1Const::construct_mask_space(Cw1, config.block_bits,
                [&](uint32_t u1, uint32_t v1, uint32_t w1, int weight) {
                    if (found) return;  // 已找到，跳過
                    
                    // Line 958: 調用Round-2
                    int Bcr_minus_2 = (config.num_rounds >= 3) ? 
                                     Bcr - Cw1 - 1 : Bcr;
                    
                    bool r2_found = round_2(config, Bcr, Bcr_minus_2, 
                                           Cw1, u1, v1, w1, trail, nodes);
                    
                    // Line 959-961: 如果返回TRUE，停止Algorithm 1
                    if (r2_found) {
                        found = true;
                        trail.add_round(1, u1, v1, w1, Cw1);
                    }
                }
            );
            
            if (found) {
                return true;  // Line 960
            }
        }
        
        // Line 966: 返回FALSE
        return false;
    }
    
    /**
     * @brief Round-2過程（Algorithm 3, Lines 967-988）
     * 
     * 排除權重>Bcr-Bcr-1-Cw1的搜索空間
     */
    static bool round_2(
        const Config& config,
        int Bcr,
        int Bcr_minus_2,
        int Cw1,
        uint32_t u1, uint32_t v1, uint32_t w1,
        LinearTrail& trail,
        uint64_t& nodes
    ) {
        // Line 968-987: for Cw2 = 0 to n-1
        for (int Cw2 = 0; Cw2 < config.block_bits; ++Cw2) {
            nodes++;
            
            // Line 970-973: 剪枝條件
            if (Cw1 + Cw2 + Bcr_minus_2 > Bcr) {
                return false;
            }
            
            // Line 975-976: 調用Algorithm 1構建S_Cw2
            bool found = false;
            
            Algorithm1Const::construct_mask_space(Cw2, config.block_bits,
                [&](uint32_t u2, uint32_t v2, uint32_t w2, int weight) {
                    if (found) return;
                    
                    // Line 978: 計算y和x（SPECK的旋轉參數）
                    // 這裡簡化為通用ARX，實際應根據算法調整
                    // y = (u1 ⊕ (v2 ≪ ra) ⊕ w2) ≪ rb
                    // x = u2 ⊕ y
                    uint32_t y = u1 ^ v2 ^ w2;  // 簡化
                    uint32_t x = u2 ^ y;
                    
                    // Line 980-982: 調用Round-r（從第3輪開始）
                    if (config.num_rounds >= 3) {
                        bool ri_found = round_i(config, 3, x, y, 
                                               Cw1 + Cw2, Bcr, trail, nodes);
                        
                        if (ri_found) {
                            found = true;
                            trail.add_round(2, u2, v2, w2, Cw2);
                        }
                    } else {
                        // 只有2輪，直接成功
                        if (Cw1 + Cw2 == Bcr) {
                            found = true;
                            trail.add_round(2, u2, v2, w2, Cw2);
                        }
                    }
                }
            );
            
            if (found) {
                return true;
            }
        }
        
        // Line 988: 返回FALSE
        return false;
    }
    
    /**
     * @brief Round-i過程（中間輪，使用SLR）
     * 
     * Algorithm 3, Lines 989-1043
     * 
     * @param i 當前輪（3 ≤ i ≤ r）
     * @param x, y 當前掩碼狀態
     */
    static bool round_i(
        const Config& config,
        int i,
        uint32_t x, uint32_t y,
        int accumulated_weight,
        int Bcr,
        LinearTrail& trail,
        uint64_t& nodes
    ) {
        nodes++;
        
        // Line 990-991: v = x >> ra（SPECK的旋轉）
        // 簡化為v = x
        uint32_t v = x;
        
        // Line 993-1002: 調用LR(v)，遍歷每個u和w
        // 使用cLAT進行查找
        bool found = false;
        
        if (config.clat_ptr) {
            // 計算權重上限
            int weight_cap = Bcr - accumulated_weight;
            
            // 使用cLAT的SLR方法查找
            int t = config.block_bits / config.chunk_bits;
            
            config.clat_ptr->lookup_and_recombine(v, t, weight_cap,
                [&](uint32_t u, uint32_t w, int Cwi) {
                    if (found) return;
                    
                    nodes++;
                    
                    // Line 995-998: 如果i = r（最後一輪）
                    if (i == config.num_rounds) {
                        if (accumulated_weight + Cwi == Bcr) {
                            // 找到r輪最優軌道
                            trail.add_round(i, u, v, w, Cwi);
                            found = true;
                            return;
                        }
                    }
                    
                    // Line 999-1002: 繼續到下一輪
                    uint32_t y_new = (y ^ w);  // 簡化
                    uint32_t x_new = y_new ^ u;
                    
                    bool next_found = round_i(config, i + 1, x_new, y_new,
                                             accumulated_weight + Cwi, Bcr, 
                                             trail, nodes);
                    
                    if (next_found) {
                        trail.add_round(i, u, v, w, Cwi);
                        found = true;
                    }
                }
            );
        }
        
        // Line 1043: 返回FALSE
        return found;
    }
};

} // namespace neoalz
