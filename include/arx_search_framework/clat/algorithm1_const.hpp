/**
 * @file algorithm1_const.hpp
 * @brief Algorithm 1: Const(S_Cw) - 構建指定權重的掩碼空間
 * 
 * 論文：Huang & Wang (2020)
 * Algorithm 1, Lines 552-619
 * 
 * 關鍵數學定義：
 * - ξ_i = u_i || v_i || w_i ∈ F_2^3（八進制字序列）
 * - U0 = {1, 2, 4, 7} （當x_i = 1時）
 * - U1 = {0, 3, 5, 6} （當x_i = 0時）
 * - U2 = {0, 7}
 * - z_i = ⊕_{j=i+1}^{n-1} x_j，其中x = u ⊕ v ⊕ w
 */

#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <algorithm>

namespace neoalz {

/**
 * @brief Algorithm 1實現類
 */
class Algorithm1Const {
public:
    /**
     * @brief 構建指定權重的掩碼空間
     * 
     * 論文Algorithm 1，構建所有權重為Cw的(u,v,w)三元組
     * 
     * @param Cw 目標權重（0 ≤ Cw ≤ n-1）
     * @param n 字長（默認32）
     * @param yield 回調函數 (u, v, w, actual_weight)
     * @return 生成的三元組數量
     */
    template<typename Yield>
    static uint64_t construct_mask_space(
        int Cw,
        int n,
        Yield&& yield
    ) {
        if (Cw < 0 || Cw >= n) return 0;
        
        // 生成漢明權重分布模式 Λ = {λ_Cw, ..., λ_1}
        // 這是組合問題：從n-1個位置選Cw個位置為1
        std::vector<std::vector<int>> lambda_patterns;
        generate_combinations(n-1, Cw, lambda_patterns);
        
        uint64_t total_count = 0;
        
        // 對每個漢明權重分布模式
        for (const auto& lambda : lambda_patterns) {
            uint64_t count = 0;
            
            // 調用LSB函數開始遞歸構建
            func_lsb(Cw, lambda, n, [&](uint32_t u, uint32_t v, uint32_t w) {
                yield(u, v, w, Cw);
                count++;
            });
            
            total_count += count;
        }
        
        return total_count;
    }
    
private:
    // 八進制字的集合定義
    static constexpr std::array<int, 4> U0 = {1, 2, 4, 7};  // x_i = 1
    static constexpr std::array<int, 4> U1 = {0, 3, 5, 6};  // x_i = 0
    static constexpr std::array<int, 2> U2 = {0, 7};
    static constexpr std::array<int, 8> F32 = {0, 1, 2, 3, 4, 5, 6, 7};
    
    /**
     * @brief 生成組合 C(n, k)
     * 
     * 計算從n-1個位置選擇Cw個位置的所有組合
     * 對應論文中的"combinations algorithm in [9]"
     */
    static void generate_combinations(
        int n_minus_1,
        int Cw,
        std::vector<std::vector<int>>& result
    ) {
        if (Cw == 0) {
            result.push_back({});
            return;
        }
        
        std::vector<int> combination;
        combination.reserve(Cw);
        
        // 使用遞歸生成所有組合
        std::function<void(int, int)> backtrack = [&](int start, int remaining) {
            if (remaining == 0) {
                result.push_back(combination);
                return;
            }
            
            for (int i = start; i <= n_minus_1 - remaining; ++i) {
                combination.push_back(i);
                backtrack(i + 1, remaining - 1);
                combination.pop_back();
            }
        };
        
        backtrack(0, Cw);
    }
    
    /**
     * @brief Func LSB: 構建最低有效位
     * 
     * 論文Lines 559-569
     */
    template<typename Yield>
    static void func_lsb(
        int Cw,
        const std::vector<int>& lambda,
        int n,
        Yield&& yield
    ) {
        // Lambda按降序排列 {λ_Cw, ..., λ_1}
        std::vector<int> sorted_lambda = lambda;
        std::sort(sorted_lambda.rbegin(), sorted_lambda.rend());
        
        std::vector<uint8_t> xi_seq(n, 0);  // 八進制字序列
        
        int i = 0;  // LSB位置
        
        // Line 560-563: 如果Cw = 0
        if (Cw == 0) {
            // 輸出(1,1,1)或(0,0,0)
            yield(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);  // (1,1,1) 全1
            yield(0x00000000, 0x00000000, 0x00000000);  // (0,0,0) 全0
            return;
        }
        
        // Line 564-569
        int c, Fw;
        if (sorted_lambda[0] != 0) {  // λ_1 ≠ 0
            // Line 565-566: ξ_i ∈ U2
            for (int xi : U2) {
                xi_seq[i] = xi;
                c = 1;
                Fw = 0;
                func_middle(i + 1, c, Fw, Cw, sorted_lambda, xi_seq, n, yield);
            }
        } else {  // λ_1 = 0
            // Line 568-569: ξ_i ∈ F_2^3
            for (int xi : F32) {
                xi_seq[i] = xi;
                c = 2;
                Fw = 1;
                func_middle(i + 1, c, Fw, Cw, sorted_lambda, xi_seq, n, yield);
            }
        }
    }
    
    /**
     * @brief Func Middle: 構建中間位
     * 
     * 論文Lines 571-598
     */
    template<typename Yield>
    static void func_middle(
        int i,
        int c,
        int Fw,
        int Cw,
        const std::vector<int>& lambda,
        std::vector<uint8_t>& xi_seq,
        int n,
        Yield&& yield
    ) {
        // Line 571-573: 如果c = Cw，轉到MSB函數
        if (c == Cw) {
            func_msb(i, c, Fw, Cw, lambda, xi_seq, n, yield);
            return;
        }
        
        // Line 574-576: 如果λ_c ≠ i
        if (c >= (int)lambda.size() || lambda[c-1] != i) {
            // Line 577-586
            if (Fw == 0) {
                // ξ_i = 0
                xi_seq[i] = 0;
                int Fw_new = 0;
                func_middle(i + 1, c, Fw_new, Cw, lambda, xi_seq, n, yield);
            } else {
                // ξ_i = 7
                xi_seq[i] = 7;
                int Fw_new = 0;
                func_middle(i + 1, c, Fw_new, Cw, lambda, xi_seq, n, yield);
            }
        } else {
            // Line 587-598: λ_c = i
            if (Fw == 0) {
                // Line 589-592: ξ_i ∈ U0
                for (int xi : U0) {
                    xi_seq[i] = xi;
                    int Fw_new = 1;
                    func_middle(i + 1, c + 1, Fw_new, Cw, lambda, xi_seq, n, yield);
                }
            } else {
                // Line 593-598: ξ_i ∈ U1
                for (int xi : U1) {
                    xi_seq[i] = xi;
                    int Fw_new = 1;
                    func_middle(i + 1, c + 1, Fw_new, Cw, lambda, xi_seq, n, yield);
                }
            }
        }
    }
    
    /**
     * @brief Func MSB: 構建高位
     * 
     * 論文Lines 599-619
     */
    template<typename Yield>
    static void func_msb(
        int i,
        int c,
        int Fw,
        int Cw,
        const std::vector<int>& lambda,
        std::vector<uint8_t>& xi_seq,
        int n,
        Yield&& yield
    ) {
        // Line 600-610: 如果λ_c ≠ i
        if (i >= n || (c < (int)lambda.size() && lambda[c-1] != i)) {
            if (Fw == 0) {
                // ξ_i = 0
                xi_seq[i] = 0;
                int Fw_new = 0;
                if (i + 1 < n) {
                    func_msb(i + 1, c, Fw_new, Cw, lambda, xi_seq, n, yield);
                } else {
                    output_tuple(xi_seq, n, yield);
                }
            } else {
                // ξ_i = 7
                xi_seq[i] = 7;
                int Fw_new = 0;
                if (i + 1 < n) {
                    func_msb(i + 1, c, Fw_new, Cw, lambda, xi_seq, n, yield);
                } else {
                    output_tuple(xi_seq, n, yield);
                }
            }
        } else {
            // Line 611-619: λ_c = i
            if (Fw == 0) {
                // Line 613-616: ξ_i ∈ U0, ξ_{i+1} = 7
                for (int xi : U0) {
                    xi_seq[i] = xi;
                    if (i + 1 < n) {
                        xi_seq[i + 1] = 7;
                    }
                    output_tuple(xi_seq, n, yield);
                }
            } else {
                // Line 617-619: ξ_i ∈ U1, ξ_{i+1} = 7
                for (int xi : U1) {
                    xi_seq[i] = xi;
                    if (i + 1 < n) {
                        xi_seq[i + 1] = 7;
                    }
                    output_tuple(xi_seq, n, yield);
                }
            }
        }
    }
    
    /**
     * @brief 從八進制字序列構建(u,v,w)三元組
     * 
     * ξ_i = u_i || v_i || w_i
     * 其中 ξ_i ∈ {0,1,2,3,4,5,6,7}:
     * 0 = 000, 1 = 001, 2 = 010, 3 = 011,
     * 4 = 100, 5 = 101, 6 = 110, 7 = 111
     */
    template<typename Yield>
    static void output_tuple(
        const std::vector<uint8_t>& xi_seq,
        int n,
        Yield&& yield
    ) {
        uint32_t u = 0, v = 0, w = 0;
        
        for (int i = 0; i < n && i < 32; ++i) {
            uint8_t xi = xi_seq[i];
            
            // 解碼: ξ_i = u_i || v_i || w_i
            int u_i = (xi >> 2) & 1;  // 最高位
            int v_i = (xi >> 1) & 1;  // 中間位
            int w_i = (xi >> 0) & 1;  // 最低位
            
            u |= (uint32_t)u_i << i;
            v |= (uint32_t)v_i << i;
            w |= (uint32_t)w_i << i;
        }
        
        yield(u, v, w);
    }
};

} // namespace neoalz
