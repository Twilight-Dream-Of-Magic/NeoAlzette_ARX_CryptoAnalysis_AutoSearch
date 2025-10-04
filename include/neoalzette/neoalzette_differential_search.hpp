#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include "neoalzette/neoalzette_core.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
#include "utility_tools.hpp"  // SimplePDDT

namespace neoalz {

/**
 * @file neoalzette_differential_search.hpp
 * @brief NeoAlzette差分搜索 - 把算法操作直接嵌入搜索框架
 * 
 * ============================================================================
 * 核心设计理念：为什么要"混在一起"？
 * ============================================================================
 * 
 * ❌ 错误方式：先枚举后搜索
 * ----------------------------
 * Step 1: 枚举单轮所有可能的差分输出（需要缓存10000+种结果）
 * Step 2: 把结果传给搜索框架，再遍历一遍
 * 
 * 问题：
 * - 内存爆炸：需要缓存大量中间结果
 * - 计算浪费：遍历两次（枚举一次+搜索一次）
 * - 剪枝无效：已经枚举完了，无法提前停止
 * - 复杂度爆炸：单轮10000种可能，4轮就是10000^4
 * 
 * ✅ 正确方式：边搜索边执行（本文件的实现）
 * ---------------------------------------------
 * 在搜索递归过程中，直接执行NeoAlzette的每个操作步骤：
 * 
 * search_recursive(当前差分状态, 轮数) {
 *     // 直接执行NeoAlzette的每一步！
 *     
 *     For 候选dB_after:
 *         调用 ARX算子 xdp_add_lm2001() 计算权重
 *         if 权重超过阈值:
 *             continue  ← 立即剪枝！不缓存！
 *         
 *         For 候选dA_after:
 *             调用 ARX算子 diff_addconst_bvweight() 计算权重
 *             if 累积权重超过阈值:
 *                 continue  ← 立即剪枝！
 *             
 *             执行线性操作（确定性传播）
 *             
 *             立即递归搜索下一轮（不缓存结果）
 * }
 * 
 * 优点：
 * - 不缓存：只保存当前搜索路径
 * - 只遍历一次：边搜索边计算
 * - 剪枝有效：及时停止不可能的分支
 * - 复杂度可控：O(实际搜索树大小)，而不是O(N^轮数)
 * 
 * ============================================================================
 * 搜索流程详解
 * ============================================================================
 * 
 * 初始状态：(dA=1, dB=0)，权重=0
 * 
 * 第0轮：
 *   执行 Subround 0:
 *     Step 1: B += (rotl(A,31)^rotl(A,17)^RC[0])
 *       → 调用 xdp_add_lm2001(dB, beta, 候选dB_after)
 *       → 只尝试高概率的候选（不是暴力枚举2^32！）
 *       → 对每个可行的dB_after，记录权重w1
 *     
 *     Step 2: A -= RC[1]
 *       → 调用 diff_addconst_bvweight(dA, RC[1], 候选dA_after)
 *       → 只尝试几个候选
 *       → 对每个可行的dA_after，记录权重w2
 *     
 *     Step 3-7: 线性操作（XOR、旋转、线性层、跨分支注入）
 *       → 这些是确定性的，直接计算
 *   
 *   执行 Subround 1: （类似Subround 0）
 *   
 *   得到新的差分状态 (dA', dB')，累积权重 = w1+w2+...
 *   
 *   if 累积权重 < 阈值:
 *       递归搜索第1轮（从新状态开始）
 *   else:
 *       剪枝，停止这个分支
 * 
 * 第1轮、第2轮、... ：重复上述过程
 * 
 * 到达最终轮：记录最优权重和轨道
 * 
 * ============================================================================
 * 与通用pDDT+Matsui框架的关系
 * ============================================================================
 * 
 * 本实现是专门为NeoAlzette定制的搜索框架。
 * 
 * 通用框架（pDDT+Matsui）：
 * - 适用于任意ARX密码
 * - 需要适配层来接入特定算法
 * - 更复杂但更通用
 * 
 * 本实现（NeoAlzette专用）：
 * - 直接针对NeoAlzette的结构
 * - 把NeoAlzette的每一步直接嵌入搜索
 * - 更简单直接，更容易理解和调试
 * - 后续可以用通用框架替换（作为优化）
 */
class NeoAlzetteDifferentialSearch {
public:
    /**
     * @brief 差分状态
     * 
     * 表示某一时刻的差分：(ΔA, ΔB) 和累积权重
     */
    struct DiffState {
        std::uint32_t dA, dB;  ///< 差分值
        int weight;             ///< 累积权重 = -log2(概率)
        
        DiffState(std::uint32_t da, std::uint32_t db, int w = 0)
            : dA(da), dB(db), weight(w) {}
    };
    
    /**
     * @brief 搜索配置
     */
    struct SearchConfig {
        int num_rounds = 4;              ///< 搜索轮数
        int weight_cap = 30;             ///< 权重上限（MEDCP = 2^-weight_cap）
        std::uint32_t initial_dA = 1;    ///< 初始差分A（通常设为1）
        std::uint32_t initial_dB = 0;    ///< 初始差分B（通常设为0）
        bool use_pddt = false;           ///< 是否使用pDDT表加速（需要预先构建表）
        int pddt_threshold = 10;         ///< pDDT表的权重阈值
        
        // pDDT表指针（如果use_pddt=true，必须提供）
        const void* pddt_table = nullptr;  ///< SimplePDDT*类型
    };
    
    /**
     * @brief 搜索结果
     */
    struct SearchResult {
        int best_weight;                   ///< 最优权重
        std::vector<DiffState> best_trail; ///< 最优差分轨道
        std::uint64_t nodes_visited;       ///< 访问的搜索节点数
        bool found;                        ///< 是否找到可行轨道
    };
    
    /**
     * @brief 执行完整的差分搜索
     * 
     * @param config 搜索配置
     * @return 搜索结果（包含最优权重、轨道、统计信息）
     */
    static SearchResult search(const SearchConfig& config);
    
private:
    /**
     * @brief 递归搜索实现 - 这里是核心！
     * 
     * 在这个函数里直接执行NeoAlzette的每一步操作！
     * 不是先枚举再搜索，而是边搜索边执行！
     * 
     * @param config 搜索配置
     * @param current 当前差分状态
     * @param round 当前轮数（0-based）
     * @param trail 当前搜索路径（用于记录最优轨道）
     * @param result 搜索结果（用于更新最优解）
     */
    static void search_recursive(
        const SearchConfig& config,
        const DiffState& current,
        int round,
        std::vector<DiffState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 执行NeoAlzette Subround 0的差分传播
     * 
     * ========================================================================
     * 为什么用回调（Yield）而不是返回vector？
     * ========================================================================
     * 
     * ❌ 返回vector:
     *    std::vector<Result> results = execute_subround0(...);
     *    for (auto& r : results) { ... }
     *    → 需要缓存所有结果，内存浪费
     * 
     * ✅ 用回调：
     *    execute_subround0(..., [&](dA, dB, weight) {
     *        // 立即处理，不缓存
     *    });
     *    → 不缓存，立即处理
     * 
     * ========================================================================
     * Subround 0的操作步骤
     * ========================================================================
     * 
     * 前向加密（值域）：
     *   B += (rotl(A, 31) ^ rotl(A, 17) ^ RC[0]);  // ← 唯一的非线性操作
     *   A -= RC[1];                                // ← 常量模减
     *   A ^= rotl(B, 24);                          // ← 线性扩散
     *   B ^= rotl(A, 16);                          // ← 线性扩散
     *   A = l1_forward(A);                         // ← 线性层
     *   B = l2_forward(B);                         // ← 线性层
     *   [C0, D0] = cd_from_B(B, RC[2], RC[3]);    // ← 跨分支注入
     *   A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ RC[4]);// ← XOR常量
     * 
     * 差分传播（差分域）：
     *   Step 1: ΔB += (rotl(ΔA,31) ^ rotl(ΔA,17))
     *     → β = rotl(ΔA,31) ^ rotl(ΔA,17)  (常量RC[0]消失)
     *     → 调用 xdp_add_lm2001(ΔB, β, ΔB_after) 计算权重
     *     → 只尝试高概率的ΔB_after（不是暴力枚举！）
     * 
     *   Step 2: ΔA -= RC[1]
     *     → 调用 diff_addconst_bvweight(ΔA, RC[1], ΔA_after)
     *     → 只尝试几个候选的ΔA_after
     * 
     *   Step 3-7: 线性操作
     *     → 差分通过线性操作确定性传播
     *     → ΔA' = ΔA ^ rotl(ΔB, 24)
     *     → ΔB' = ΔB ^ rotl(ΔA', 16)
     *     → ΔA' = l1_forward(ΔA')  (线性层)
     *     → ΔB' = l2_forward(ΔB')  (线性层)
     *     → [ΔC0, ΔD0] = cd_from_B_delta(ΔB')
     *     → ΔA' ^= (rotl(ΔC0, 24) ^ rotl(ΔD0, 16))
     * 
     * @param input 输入差分状态
     * @param weight_budget 剩余权重预算
     * @param yield 回调函数：(dA_out, dB_out, weight_consumed) → void
     */
    template<typename Yield>
    static void execute_subround0(
        const DiffState& input,
        int weight_budget,
        Yield&& yield
    );
    
    /**
     * @brief 执行NeoAlzette Subround 1的差分传播
     * 
     * Subround 1与Subround 0类似，但A和B的角色互换：
     * - Subround 0: B先模加，A模减
     * - Subround 1: A先模加，B模减
     * 
     * 这种对称设计确保了A和B的平衡处理。
     * 
     * @param input 输入差分状态（来自Subround 0的输出）
     * @param weight_budget 剩余权重预算
     * @param yield 回调函数：(dA_out, dB_out, weight_consumed) → void
     */
    template<typename Yield>
    static void execute_subround1(
        const DiffState& input,
        int weight_budget,
        Yield&& yield
    );
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround0(
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // ========================================================================
    // Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
    // ========================================================================
    // 
    // 前向加密（值域）：
    //   B_new = B_old + (rotl(A, 31) ^ rotl(A, 17) ^ RC[0])
    // 
    // 差分传播（差分域）：
    //   ΔB_new = ΔB_old + (rotl(ΔA, 31) ^ rotl(ΔA, 17))
    //   注意：常量RC[0]在差分域消失！（因为差分是两次执行的XOR）
    // 
    // 定义：β = rotl(ΔA, 31) ^ rotl(ΔA, 17)
    // 问题：给定(ΔB_old, β)，枚举所有可能的ΔB_new及其权重
    // 
    std::uint32_t beta = NeoAlzetteCore::rotl(dA, 31) ^ NeoAlzetteCore::rotl(dA, 17);
    
    // 候选策略：不是暴力枚举2^32种可能！
    // 而是只尝试高概率的候选：
    // 1. dB（权重0，最可能）：差分不传播
    // 2. dB ^ beta（直接传播）：beta直接加到dB上
    // 3. 低权重的单比特/多比特变化
    // 
    // TODO: 后续优化：使用pDDT表预计算高概率候选
    std::vector<std::uint32_t> dB_candidates = {
        dB,           // 候选1：权重0
        dB ^ beta,    // 候选2：直接传播
    };
    
    // 候选3: 枚举单比特翻转
    for (int bit = 0; bit < 32; ++bit) {
        dB_candidates.push_back(dB ^ (1u << bit));
        dB_candidates.push_back((dB ^ beta) ^ (1u << bit));
    }
    
    // 对每个候选，调用ARX算子计算权重
    for (std::uint32_t dB_after : dB_candidates) {
        // ====================================================================
        // 调用ARX分析算子：xdp_add_lm2001
        // ====================================================================
        // 
        // 这是Lipmaa-Moriai 2001论文的Algorithm 2：
        // - 输入：(α, β, γ) = (dB, beta, dB_after)
        // - 输出：权重w，使得 Pr[α + β → γ] = 2^{-w}
        // - 复杂度：O(1)
        // - 包含"good"检查：不可能的差分返回-1
        // 
        int w1 = arx_operators::xdp_add_lm2001(dB, beta, dB_after);
        
        // 剪枝1：不可行的差分（w1 < 0）
        // 剪枝2：权重超过预算（w1 >= weight_budget）
        if (w1 < 0 || w1 >= weight_budget) continue;
        
        // ====================================================================
        // Step 2: A -= RC[1]
        // ====================================================================
        // 
        // 前向加密：A_new = A_old - RC[1]
        // 差分传播：ΔA_new = ΔA_old - 0 = ΔA_old（理论上）
        // 
        // 但实际上，模减常量会影响carry传播，从而影响差分：
        // - 如果ΔA导致的carry与RC[1]相互作用，差分可能改变
        // - 需要调用diff_addconst_bvweight精确计算
        // 
        const std::uint32_t RC1 = NeoAlzetteCore::ROUND_CONSTANTS[1];
        
        // 候选策略：
        // 1. dA（权重0，最常见）：常量不影响差分
        // 2. dA ^ 1, dA ^ 3：低权重的小变化
        std::vector<std::uint32_t> dA_candidates = {
            dA,      // 候选1：无变化
            dA ^ 1,  // 候选2：第0位翻转
            dA ^ 3,  // 候选3：第0-1位翻转
        };
        
        for (std::uint32_t dA_after : dA_candidates) {
            // ================================================================
            // 调用ARX分析算子：diff_addconst_bvweight
            // ================================================================
            // 
            // 这是Bit-Vector 2022论文的Algorithm 1（BvWeight）：
            // - 输入：(ΔX, K, ΔY) = (dA, RC1, dA_after)
            // - 输出：权重w，使得 Pr[X - K: ΔX → ΔY] ≈ 2^{-w}
            // - 复杂度：O(log²n)
            // - 注意：这是近似算法，返回上界
            // 
            int w2 = arx_operators::diff_addconst_bvweight(dA, RC1, dA_after);
            
            // 剪枝：累积权重超过预算
            if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
            
            // ================================================================
            // Step 3-7: 线性操作（确定性传播）
            // ================================================================
            // 
            // 所有线性操作（XOR、旋转、线性层）的差分传播是确定性的：
            // - ΔL(X) = L(ΔX)（线性函数的性质）
            // - 权重为0（没有额外的概率损失）
            // 
            
            // Step 3: A ^= rotl(B, 24)
            // 差分：ΔA' = ΔA ^ rotl(ΔB, 24)
            std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_after, 24);
            
            // Step 4: B ^= rotl(A, 16)
            // 差分：ΔB' = ΔB ^ rotl(ΔA', 16)
            std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_temp, 16);
            
            // Step 5: A = l1_forward(A)
            // 差分：ΔA' = l1_forward(ΔA)（线性层的线性性质）
            dA_temp = NeoAlzetteCore::l1_forward(dA_temp);
            
            // Step 6: B = l2_forward(B)
            // 差分：ΔB' = l2_forward(ΔB)
            dB_temp = NeoAlzetteCore::l2_forward(dB_temp);
            
            // Step 7: cd_from_B → A ^= ...
            // cd_from_B是线性的，差分版本：cd_from_B_delta
            // 常量RC[2], RC[3], RC[4]在差分域消失
            auto [dC0, dD0] = NeoAlzetteCore::cd_from_B_delta(dB_temp);
            dA_temp ^= (NeoAlzetteCore::rotl(dC0, 24) ^ NeoAlzetteCore::rotl(dD0, 16));
            
            // ================================================================
            // 输出：通过回调返回结果（不缓存！）
            // ================================================================
            // 
            // 为什么用回调而不是vector？
            // - 不缓存：立即处理，节省内存
            // - 及时剪枝：在回调中可以立即检查并递归
            // 
            int weight_consumed = w1 + w2;  // 本Subround消耗的权重
            yield(dA_temp, dB_temp, weight_consumed);
            // 注意：这里不return，继续尝试其他候选
        }
    }
}

template<typename Yield>
void NeoAlzetteDifferentialSearch::execute_subround1(
    const DiffState& input,
    int weight_budget,
    Yield&& yield
) {
    const std::uint32_t dA = input.dA;
    const std::uint32_t dB = input.dB;
    
    // ========================================================================
    // Subround 1: A和B角色互换
    // ========================================================================
    // 
    // Subround 0: B先模加，A模减
    // Subround 1: A先模加，B模减  ← 角色互换
    // 
    // 这种对称设计：
    // - 确保A和B都经历非线性操作
    // - 防止某个分支被过度优化攻击
    // - 增强差分传播的复杂性
    // 
    
    // Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    std::uint32_t beta = NeoAlzetteCore::rotl(dB, 31) ^ NeoAlzetteCore::rotl(dB, 17);
    
    std::vector<std::uint32_t> dA_candidates = {
        dA, dA ^ beta,
    };
    for (int bit = 0; bit < 32; ++bit) {
        dA_candidates.push_back(dA ^ (1u << bit));
    }
    
    for (std::uint32_t dA_after : dA_candidates) {
        int w1 = arx_operators::xdp_add_lm2001(dA, beta, dA_after);
        if (w1 < 0 || w1 >= weight_budget) continue;
        
        // Step 2: B -= RC[6]
        const std::uint32_t RC6 = NeoAlzetteCore::ROUND_CONSTANTS[6];
        std::vector<std::uint32_t> dB_candidates = {dB, dB ^ 1};
        
        for (std::uint32_t dB_after : dB_candidates) {
            int w2 = arx_operators::diff_addconst_bvweight(dB, RC6, dB_after);
            if (w2 < 0 || (w1 + w2) >= weight_budget) continue;
            
            // Step 3-7: 线性操作（确定性）
            std::uint32_t dB_temp = dB_after ^ NeoAlzetteCore::rotl(dA_after, 24);
            std::uint32_t dA_temp = dA_after ^ NeoAlzetteCore::rotl(dB_temp, 16);
            dB_temp = NeoAlzetteCore::l1_forward(dB_temp);
            dA_temp = NeoAlzetteCore::l2_forward(dA_temp);
            
            auto [dC1, dD1] = NeoAlzetteCore::cd_from_A_delta(dA_temp);
            dB_temp ^= (NeoAlzetteCore::rotl(dC1, 24) ^ NeoAlzetteCore::rotl(dD1, 16));
            
            // 白化: A ^= RC[10], B ^= RC[11]
            // 差分域：常量消失，差分不变
            
            int weight_consumed = w1 + w2;
            yield(dA_temp, dB_temp, weight_consumed);
        }
    }
}

} // namespace neoalz
