/**
 * @file clat_linear.hpp
 * @brief cLAT（組合線性近似表）實現
 * 
 * 論文：Huang & Wang (2020), "Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers"
 * Algorithm 2: 構建m位cLAT
 * 
 * 關鍵特性：
 * - 固定一個輸入掩碼v，查找所有(u,w)及其權重
 * - 分塊存儲：8位塊，存儲 ~1.2GB
 * - Splitting-Lookup-Recombination (SLR)方法
 */

#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <unordered_map>

namespace neoalz {

/**
 * @brief cLAT（組合線性近似表）
 * 
 * 數據結構：
 * - cLATw[v][b][idx] = w（輸入掩碼）
 * - cLATu[v][b][idx] = u（輸出掩碼）
 * - cLATN[v][b][Cw] = 對應權重Cw的三元組數量
 * - cLATmin[v][b] = 最小權重
 * - cLATb[u][v][w][b] = 連接狀態
 */
template<int M_BITS = 8>
class cLAT {
public:
    static constexpr int m = M_BITS;  // 分塊大小
    static constexpr int mask_size = (1 << m);  // 2^m
    
    /**
     * @brief 三元組條目
     */
    struct Entry {
        uint32_t u;  // 輸出掩碼
        uint32_t w;  // 輸入掩碼2
        int weight;  // 權重
        int conn_status;  // 連接狀態b
    };
    
    /**
     * @brief Algorithm 2: 構建m位cLAT
     * 
     * 論文Lines 713-774
     * 
     * 完整實現：
     * - Line 714-717: 初始化cLATmin, MT, cLATN
     * - Line 719-773: 遍歷所有(v,b,w,u)組合
     * - Line 723: 計算A, B, C
     * - Line 725-729: 計算Cb（C的位）
     * - Line 731-751: 計算權重Cw和MT
     * - Line 753: Property 6檢查
     * - Line 755-770: 存儲有效條目
     * 
     * @return 構建是否成功
     */
    bool build() {
        // Line 714-717: 初始化
        for (int v = 0; v < mask_size; ++v) {
            for (int b = 0; b < 2; ++b) {
                cLATmin_[v][b] = m;  // 初始為最大
                
                // 初始化cLATN為0
                for (int k = 0; k <= m; ++k) {
                    count_map_[v][b][k] = 0;
                }
            }
        }
        
        // Line 719-773: 遍歷所有(v, b, w, u)組合
        for (int v = 0; v < mask_size; ++v) {
            for (int b = 0; b < 2; ++b) {
                for (int w = 0; w < mask_size; ++w) {
                    for (int u = 0; u < mask_size; ++u) {
                        // Line 723: A = u⊕v, B = u⊕w, C = u⊕v⊕w
                        uint32_t A = u ^ v;
                        uint32_t B = u ^ w;
                        uint32_t C = u ^ v ^ w;
                        int Cw = 0;
                        
                        // Line 725-729: 計算Cb[j] = (C >> (m-1-j)) & 1
                        std::array<int, M_BITS> Cb;
                        for (int j = 0; j < m; ++j) {
                            Cb[j] = (C >> (m - 1 - j)) & 1;
                        }
                        
                        // Line 731-739: 初始化連接狀態
                        std::array<int, M_BITS> MT;
                        uint32_t Z;
                        
                        if (b == 1) {
                            // Line 732-733: b=1時，進位來自上一塊
                            Cw++;
                            MT[0] = 1;
                            Z = 1 << (m - 1);
                        } else {
                            // Line 735-737: b=0時，無進位
                            MT[0] = 0;
                            Z = 0;
                        }
                        
                        // Line 741-751: 計算權重Cw和MT
                        for (int i = 1; i < m; ++i) {
                            // Line 743: MT[i] = (Cb[i-1] + MT[i-1]) & 1
                            MT[i] = (Cb[i-1] + MT[i-1]) & 1;
                            
                            // Line 745-747: 如果MT[i] = 1，增加權重
                            if (MT[i] == 1) {
                                Cw++;
                                Z |= (1 << (m - 1 - i));
                            }
                        }
                        
                        // Line 753: Property 6檢查
                        // F1 = A ∧ (¬(A ∧ Z))，檢查u⊕v ⊥ z
                        // F2 = B ∧ (¬(B ∧ Z))，檢查u⊕w ⊥ z
                        uint32_t F1 = A & (~(A & Z));
                        uint32_t F2 = B & (~(B & Z));
                        
                        // Line 755-770: 只有F1=0 且 F2=0時才存儲
                        if (F1 == 0 && F2 == 0) {
                            Entry entry;
                            entry.u = u;
                            entry.w = w;
                            entry.weight = Cw;
                            
                            // Line 763: 計算連接狀態
                            // cLATb[u][v][w][b] = (MT[m-1] + Cb[m-1]) & 1
                            entry.conn_status = (MT[m-1] + Cb[m-1]) & 1;
                            
                            // Line 757-761: 存儲到cLAT
                            table_[v][b].push_back(entry);
                            count_map_[v][b][Cw]++;
                            
                            // Line 765-767: 更新最小權重
                            if (cLATmin_[v][b] > Cw) {
                                cLATmin_[v][b] = Cw;
                            }
                        }
                    }
                }
            }
        }
        
        // 按權重排序（優化查找）
        for (int v = 0; v < mask_size; ++v) {
            for (int b = 0; b < 2; ++b) {
                std::sort(table_[v][b].begin(), table_[v][b].end(),
                         [](const Entry& a, const Entry& b) {
                             return a.weight < b.weight;
                         });
            }
        }
        
        return true;
    }
    
    /**
     * @brief LR(v) - Splitting-Lookup-Recombination
     * 
     * 論文Lines 1004-1055，Algorithm 3的LR過程
     * 
     * 給定固定的輸入掩碼v（n位），查找所有(u,w)
     * 
     * @param v_full 完整的n位掩碼
     * @param t 分塊數量（n = m×t）
     * @param weight_cap 權重上限
     * @param yield 回調函數
     */
    template<typename Yield>
    void lookup_and_recombine(
        uint32_t v_full,
        int t,
        int weight_cap,
        Yield&& yield
    ) const {
        // 分割v為t個m位塊
        std::vector<uint32_t> v_blocks(t);
        for (int k = 0; k < t; ++k) {
            v_blocks[k] = (v_full >> (k * m)) & ((1 << m) - 1);
        }
        
        // Line 1045: 計算每塊的最小權重
        std::vector<int> c_min(t);
        std::vector<int> b(t, 0);
        for (int k = 0; k < t; ++k) {
            c_min[k] = std::min(cLATmin_[v_blocks[k]][0], 
                               cLATmin_[v_blocks[k]][1]);
        }
        
        // 遞歸查找（從MSB到LSB）
        std::vector<Entry> selected(t);
        lookup_recursive(v_blocks, t-1, 0, weight_cap, selected, yield);
    }
    
    /**
     * @brief 獲取最小權重
     */
    int get_min_weight(uint32_t v, int b) const {
        if (v >= mask_size || b > 1) return m;
        return cLATmin_[v][b];
    }
    
    /**
     * @brief 獲取指定(v,b,weight)的所有條目
     */
    const std::vector<Entry>& get_entries(uint32_t v, int b) const {
        static const std::vector<Entry> empty;
        if (v >= mask_size || b > 1) return empty;
        return table_[v][b];
    }
    
    /**
     * @brief 獲取表的統計信息
     */
    struct Stats {
        uint64_t total_entries = 0;
        uint64_t memory_bytes = 0;
        double avg_entries_per_block = 0.0;
    };
    
    Stats get_stats() const {
        Stats stats;
        for (int v = 0; v < mask_size; ++v) {
            for (int b = 0; b < 2; ++b) {
                stats.total_entries += table_[v][b].size();
            }
        }
        stats.memory_bytes = stats.total_entries * sizeof(Entry);
        stats.avg_entries_per_block = (double)stats.total_entries / (mask_size * 2);
        return stats;
    }
    
private:
    // cLAT數據結構（論文中的定義）
    // cLATw[v][b][idx] = w, cLATu[v][b][idx] = u
    std::array<std::array<std::vector<Entry>, 2>, (1 << M_BITS)> table_;
    
    // cLATmin[v][b] = 最小權重
    std::array<std::array<int, 2>, (1 << M_BITS)> cLATmin_;
    
    // cLATN[v][b][Cw] = 權重為Cw的條目數量
    std::array<std::array<std::array<int, M_BITS+1>, 2>, (1 << M_BITS)> count_map_;
    
    /**
     * @brief 遞歸查找（實現Algorithm 3的LR過程）
     */
    template<typename Yield>
    void lookup_recursive(
        const std::vector<uint32_t>& v_blocks,
        int k,
        int acc_weight,
        int weight_cap,
        std::vector<Entry>& selected,
        Yield&& yield
    ) const {
        if (k < 0) {
            // 重組u和w
            uint32_t u = 0, w = 0;
            int total_weight = 0;
            for (size_t i = 0; i < selected.size(); ++i) {
                u |= (uint32_t)selected[i].u << (i * m);
                w |= (uint32_t)selected[i].w << (i * m);
                total_weight += selected[i].weight;
            }
            yield(u, w, total_weight);
            return;
        }
        
        // 當前塊
        uint32_t v_k = v_blocks[k];
        int b_k = (k == (int)v_blocks.size() - 1) ? 0 : selected[k+1].conn_status;
        
        // 遍歷該塊的所有條目
        const auto& entries = table_[v_k][b_k];
        for (const auto& entry : entries) {
            int new_weight = acc_weight + entry.weight;
            if (new_weight <= weight_cap) {
                selected[k] = entry;
                lookup_recursive(v_blocks, k-1, new_weight, weight_cap, selected, yield);
            }
        }
    }
};

} // namespace neoalz
