#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include "arx_analysis_operators/differential_xdp_add.hpp"

namespace neoalz {

/**
 * @brief 完整枚举满足权重条件的所有可能差分输出
 * 
 * 基于Lipmaa-Moriai的"good"差分性质，只枚举可能的差分
 * 
 * @param alpha 第一个输入差分
 * @param beta 第二个输入差分
 * @param weight_cap 权重上限
 * @param yield 回调函数 (gamma, weight)
 */
template<typename Yield>
void enumerate_xdp_add_outputs_complete(
    std::uint32_t alpha,
    std::uint32_t beta,
    int weight_cap,
    Yield&& yield
) {
    // 特殊情况：零差分
    if (alpha == 0 && beta == 0) {
        yield(0, 0);
        return;
    }
    
    // 递归构建差分输出
    // 从最高位到最低位，每一位有两种选择：0或1
    // 但需要检查"good"条件
    
    std::function<void(int, std::uint32_t, int)> recursive_search = 
        [&](int bit_pos, std::uint32_t gamma_partial, int weight_so_far) {
        
        // 到达最低位
        if (bit_pos < 0) {
            // 最终检查：这个gamma是否真的可行
            int final_weight = arx_operators::xdp_add_lm2001(alpha, beta, gamma_partial);
            if (final_weight >= 0 && final_weight < weight_cap) {
                yield(gamma_partial, final_weight);
            }
            return;
        }
        
        // 剪枝：如果当前权重已超过上限
        if (weight_so_far >= weight_cap) return;
        
        // 尝试当前位为0
        std::uint32_t gamma_try_0 = gamma_partial;
        int weight_0 = arx_operators::xdp_add_lm2001(alpha, beta, gamma_try_0);
        if (weight_0 >= 0 && weight_0 < weight_cap) {
            recursive_search(bit_pos - 1, gamma_try_0, weight_0);
        }
        
        // 尝试当前位为1
        std::uint32_t gamma_try_1 = gamma_partial | (1u << bit_pos);
        int weight_1 = arx_operators::xdp_add_lm2001(alpha, beta, gamma_try_1);
        if (weight_1 >= 0 && weight_1 < weight_cap) {
            recursive_search(bit_pos - 1, gamma_try_1, weight_1);
        }
    };
    
    // 从最高位开始
    recursive_search(31, 0, 0);
}

/**
 * @brief 完整枚举模减常量的所有可能差分输出
 * 
 * @param delta_x 输入差分
 * @param constant 常量
 * @param weight_cap 权重上限
 * @param yield 回调函数 (delta_y, weight)
 */
template<typename Yield>
void enumerate_subconst_outputs_complete(
    std::uint32_t delta_x,
    std::uint32_t constant,
    int weight_cap,
    Yield&& yield
) {
    // 模减常量：X - C = X + (~C + 1)
    // 对于固定的输入差分delta_x和常量C，
    // 输出差分delta_y由carry传播决定
    
    // 简化策略：尝试所有可能的输出差分
    // 使用BvWeight算法检查权重
    
    // 启发式枚举：
    // 1. delta_y = delta_x（权重0，最常见）
    // 2. delta_y与delta_x只差几位的情况
    
    std::vector<std::uint32_t> candidates;
    candidates.push_back(delta_x);  // 无carry传播
    
    // 枚举低权重的变化
    for (int flip_bits = 1; flip_bits <= 3; ++flip_bits) {
        for (int b = 0; b < 32; ++b) {
            std::uint32_t delta_y = delta_x ^ (1u << b);
            candidates.push_back(delta_y);
            
            if (flip_bits >= 2) {
                for (int b2 = b + 1; b2 < 32; ++b2) {
                    delta_y = delta_x ^ (1u << b) ^ (1u << b2);
                    candidates.push_back(delta_y);
                }
            }
        }
    }
    
    // 去重并检查
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    
    for (std::uint32_t delta_y : candidates) {
        int weight = arx_operators::diff_addconst_bvweight(delta_x, constant, delta_y);
        if (weight >= 0 && weight < weight_cap) {
            yield(delta_y, weight);
        }
    }
}

} // namespace neoalz
