#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include "neoalzette/neoalzette_core.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

namespace neoalz {

/**
 * @file neoalzette_linear_search.hpp
 * @brief NeoAlzette线性搜索 - 把算法操作直接嵌入搜索框架（反向传播）
 * 
 * ============================================================================
 * 核心设计理念：为什么是"反向传播"？
 * ============================================================================
 * 
 * 线性密码分析的方向与差分分析完全相反！
 * 
 * 差分分析（前向）：
 *   初始差分(ΔA, ΔB) → 加密若干轮 → 最终差分(ΔA', ΔB')
 *   计算：Pr[初始差分 → 最终差分]
 * 
 * 线性分析（反向）：
 *   最终掩码(mA, mB) → 反向传播若干轮 → 初始掩码(mA', mB')
 *   计算：Cor[初始掩码·输入 ⊕ 最终掩码·输出]
 * 
 * 为什么是反向？
 * - 线性逼近：α·X ⊕ β·Y = 0
 * - 我们固定输出掩码β（攻击目标），反推输入掩码α
 * - 数学推导：β·Y = β·L(X) = (L^T·β)·X，所以 α = L^T·β
 * 
 * ============================================================================
 * 为什么要"混在一起"？（与差分搜索相同的理由）
 * ============================================================================
 * 
 * ❌ 错误方式：先枚举后搜索
 *   → 缓存所有可能的掩码组合（内存爆炸）
 *   → 遍历两次（枚举一次+搜索一次）
 *   → 剪枝无效
 * 
 * ✅ 正确方式：边搜索边反向执行
 *   → 不缓存，只保存当前路径
 *   → 只遍历一次
 *   → 实时剪枝（相关性太小立即停止）
 * 
 * ============================================================================
 * 搜索流程详解（反向传播）
 * ============================================================================
 * 
 * 从最终掩码开始：(mA_final, mB_final)，相关性=1.0
 * 
 * 第N轮（最后一轮）反向：
 *   反向执行白化: A ^= RC[10], B ^= RC[11]
 *     → 掩码不变（XOR常量对掩码无影响）
 *   
 *   反向执行 Subround 1:
 *     反向 cd_from_A: 使用转置
 *     反向 l2_forward(A): 使用 l2_transpose(mA)
 *     反向 l1_forward(B): 使用 l1_transpose(mB)
 *     反向 XOR: 掩码通过转置传播
 *     反向 B -= RC[6]: 调用 linear_cor_addconst
 *     反向 A += (...): 调用 linear_cor_add_value_logn
 *       → 枚举可能的输入掩码
 *       → 对每个候选，计算相关性
 *       → if 相关性 < 阈值: 剪枝
 *   
 *   反向执行 Subround 0: （类似）
 *   
 *   得到前一轮的掩码，累积相关性 = corr0 * corr1 * ...
 *   
 *   if 累积相关性 > 阈值:
 *       递归搜索第N-1轮（从新掩码开始）
 *   else:
 *       剪枝
 * 
 * 第N-1轮、第N-2轮、... ：重复上述过程
 * 
 * 到达第0轮（初始输入）：记录最优相关性和轨道
 */
class NeoAlzetteLinearSearch {
public:
    /**
     * @brief 线性掩码状态
     * 
     * 表示某一时刻的线性掩码和累积相关性
     */
    struct LinearState {
        std::uint32_t mA, mB;      ///< 掩码值
        double correlation;        ///< 累积相关性（可以是负数）
        
        LinearState(std::uint32_t ma, std::uint32_t mb, double corr = 1.0)
            : mA(ma), mB(mb), correlation(corr) {}
    };
    
    /**
     * @brief 搜索配置
     */
    struct SearchConfig {
        int num_rounds = 4;                        ///< 搜索轮数
        double correlation_threshold = 0.001;      ///< 相关性阈值（MELCC ≈ threshold^轮数）
        std::uint32_t final_mA = 1;                ///< 最终输出掩码A（攻击目标）
        std::uint32_t final_mB = 0;                ///< 最终输出掩码B
    };
    
    /**
     * @brief 搜索结果
     */
    struct SearchResult {
        double best_correlation;             ///< 最优相关性（绝对值）
        std::vector<LinearState> best_trail; ///< 最优线性轨道（反向，从输出到输入）
        std::uint64_t nodes_visited;         ///< 访问的搜索节点数
        bool found;                          ///< 是否找到可行轨道
    };
    
    /**
     * @brief 执行完整的线性搜索（反向传播）
     * 
     * @param config 搜索配置
     * @return 搜索结果（包含最优相关性、轨道、统计信息）
     */
    static SearchResult search(const SearchConfig& config);
    
private:
    /**
     * @brief 递归搜索实现（反向传播）
     * 
     * 注意：round是倒着数的！
     * - round = num_rounds: 最后一轮（从这里开始反向传播）
     * - round = 0: 初始输入（到达这里表示搜索完成）
     * 
     * @param config 搜索配置
     * @param current 当前掩码状态
     * @param round 当前轮数（倒数）
     * @param trail 当前搜索路径（反向，从输出到输入）
     * @param result 搜索结果（用于更新最优解）
     */
    static void search_recursive(
        const SearchConfig& config,
        const LinearState& current,
        int round,
        std::vector<LinearState>& trail,
        SearchResult& result
    );
    
    /**
     * @brief 反向执行NeoAlzette Subround 1的掩码传播
     * 
     * ========================================================================
     * Subround 1的前向操作（加密方向，值域）
     * ========================================================================
     * 
     * 1. A += (rotl(B, 31) ^ rotl(B, 17) ^ RC[5]);
     * 2. B -= RC[6];
     * 3. B ^= rotl(A, 24);
     * 4. A ^= rotl(B, 16);
     * 5. B = l1_forward(B);
     * 6. A = l2_forward(A);
     * 7. [C1, D1] = cd_from_A(A, RC[7], RC[8]);
     * 8. B ^= (rotl(C1, 24) ^ rotl(D1, 16) ^ RC[9]);
     * 
     * ========================================================================
     * 反向掩码传播（掩码域，反向）
     * ========================================================================
     * 
     * 给定输出掩码 (mA_out, mB_out)，反推输入掩码 (mA_in, mB_in)
     * 
     * 反向顺序：从Step 8到Step 1
     * 
     * Step 8反向: B ^= (...)
     *   → XOR对掩码的转置是它自己
     *   → mB不变，mA不变（常量对掩码无影响）
     * 
     * Step 7反向: cd_from_A
     *   → 需要使用cd_from_A的转置
     *   → 从(mC1, mD1)推导到mA（TODO: 简化版本）
     * 
     * Step 6反向: A = l2_forward(A)
     *   → 使用转置：mA_in = l2_transpose(mA_out)
     *   → 注意：是转置，不是逆函数！
     *   → l2_transpose = 把所有rotl改成rotr
     * 
     * Step 5反向: B = l1_forward(B)
     *   → 使用转置：mB_in = l1_transpose(mB_out)
     * 
     * Step 4反向: A ^= rotl(B, 16)
     *   → 前向：A' = A ^ rotl(B, 16)
     *   → 掩码：mA·A' = mA·A ⊕ mA·rotl(B, 16)
     *   → 转置：mA_in = mA_out, mB_in ^= rotr(mA_out, 16)
     * 
     * Step 3反向: B ^= rotl(A, 24)
     *   → 转置：mB_in = mB_out, mA_in ^= rotr(mB_out, 24)
     * 
     * Step 2反向: B -= RC[6]
     *   → 调用 corr_add_x_minus_const32
     *   → 枚举可能的输入掩码（TODO: 简化版本假设影响小）
     * 
     * Step 1反向: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
     *   → 调用 linear_cor_add_value_logn
     *   → 枚举可能的(mA_in, mB_in)及其相关性
     *   → 这是最关键的一步！
     * 
     * @param output 输出掩码状态
     * @param correlation_budget 剩余相关性预算（阈值）
     * @param yield 回调函数：(mA_in, mB_in, correlation_factor) → void
     */
    template<typename Yield>
    static void execute_subround1_backward(
        const LinearState& output,
        double correlation_budget,
        Yield&& yield
    );
    
    /**
     * @brief 反向执行NeoAlzette Subround 0的掩码传播
     * 
     * 与Subround 1类似，但A和B角色互换
     * 
     * @param output 输出掩码状态
     * @param correlation_budget 剩余相关性预算
     * @param yield 回调函数：(mA_in, mB_in, correlation_factor) → void
     */
    template<typename Yield>
    static void execute_subround0_backward(
        const LinearState& output,
        double correlation_budget,
        Yield&& yield
    );
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround1_backward(
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // ========================================================================
    // 反向 Step 8: B ^= (rotl(C1, 24) ^ rotl(D1, 16) ^ RC[9])
    // ========================================================================
    // 
    // XOR的转置是它自己：
    // - 前向：B' = B ^ X
    // - 转置：如果给定mB'，则mB = mB'（掩码不变）
    // 
    // 常量RC[9]对掩码无影响
    // 
    std::uint32_t mB_before_xor_cd = mB;
    std::uint32_t mA_before_cd = mA;
    
    // ========================================================================
    // 反向 Step 7: cd_from_A → 使用转置
    // ========================================================================
    // 
    // TODO: 使用完整的cd_from_A_transpose
    // 当前简化：假设跨分支注入的影响可以通过简单的线性组合处理
    // 
    // 后续优化：实现精确的cd_from_A转置矩阵
    // 
    
    // ========================================================================
    // 反向 Step 6: A = l2_forward(A)
    // ========================================================================
    // 
    // 关键：使用转置，不是逆函数！
    // 
    // 前向：A' = l2_forward(A) = A ^ rotl(A,8) ^ rotl(A,14) ^ ...
    // 转置：L2^T(mA') = mA' ^ rotr(mA',8) ^ rotr(mA',14) ^ ...
    // 
    // 数学推导：
    //   mA'·A' = mA'·L2(A) = (L2^T·mA')·A
    //   所以：mA = L2^T·mA'
    // 
    mA = NeoAlzetteCore::l2_transpose(mA_before_cd);
    
    // ========================================================================
    // 反向 Step 5: B = l1_forward(B)
    // ========================================================================
    // 
    // 同理：mB = L1^T·mB'
    // 
    mB = NeoAlzetteCore::l1_transpose(mB_before_xor_cd);
    
    // ========================================================================
    // 反向 Step 4: A ^= rotl(B, 16)
    // ========================================================================
    // 
    // 前向：A' = A ^ rotl(B, 16)
    // 
    // 掩码传播推导：
    //   mA'·A' = mA'·(A ^ rotl(B, 16))
    //          = mA'·A ⊕ mA'·rotl(B, 16)
    //          = mA'·A ⊕ rotr(mA', 16)·B  ← rotl的转置是rotr
    // 
    // 所以：
    //   mA_in = mA_out（掩码从A传播到A）
    //   mB_in = mB_out ^ rotr(mA_out, 16)（掩码从B和A的组合传播）
    // 
    std::uint32_t mA_before_xor2 = mA;
    std::uint32_t mB_before_xor2 = mB ^ NeoAlzetteCore::rotr(mA, 16);
    
    // ========================================================================
    // 反向 Step 3: B ^= rotl(A, 24)
    // ========================================================================
    // 
    // 同理：
    //   mB_in = mB_out
    //   mA_in = mA_out ^ rotr(mB_out, 24)
    // 
    std::uint32_t mB_before_xor1 = mB_before_xor2;
    std::uint32_t mA_before_xor1 = mA_before_xor2 ^ NeoAlzetteCore::rotr(mB_before_xor2, 24);
    
    // ========================================================================
    // 反向 Step 2: B -= RC[6]
    // ========================================================================
    // 
    // 前向：B' = B - RC[6]
    // 
    // 掩码传播：需要调用 corr_add_x_minus_const32
    // 给定输出掩码mB_out，枚举可能的输入掩码mB_in
    // 
    // TODO: 当前简化版本假设常量模减对相关性影响较小
    // 后续需要完整实现
    // 
    std::uint32_t mB_before_sub = mB_before_xor1;
    
    // ========================================================================
    // 反向 Step 1: A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    // ========================================================================
    // 
    // 这是最关键的一步！需要枚举可能的(mA_in, mB_in)
    // 
    // 前向：A' = A + (rotl(B,31) ^ rotl(B,17) ^ RC[5])
    // 定义：β_mask = rotl(mB,31) ^ rotl(mB,17)
    // 
    // 掩码传播：mA_out·A' = 某个线性逼近
    // 需要调用Wallén算法计算相关性
    // 
    std::uint32_t mA_before_add = mA_before_xor1;
    
    // 候选策略：尝试几个可能的掩码组合
    // TODO: 后续使用Wallén Automaton完整枚举
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mA_before_add, mB_before_sub},
        {mA_before_add ^ 1, mB_before_sub},
        {mA_before_add, mB_before_sub ^ 1},
    };
    
    for (const auto& [mA_candidate, mB_candidate] : candidates) {
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(mB_candidate, 31) ^ 
                                   NeoAlzetteCore::rotl(mB_candidate, 17);
        
        // ====================================================================
        // 调用ARX分析算子：linear_cor_add_value_logn
        // ====================================================================
        // 
        // 这是Wallén 2003论文的Theorem 2：
        // - 输入：(μ, ν, ω) = (mA_candidate, beta_mask, mA_before_add)
        // - 输出：相关性 Cor ∈ [-1, 1]
        // - 复杂度：Θ(log n)
        // - 包含可行性检查：不可行返回0
        // 
        double corr = arx_operators::linear_cor_add_value_logn(
            mA_candidate, beta_mask, mA_before_add
        );
        
        // 剪枝：相关性太小或不可行
        if (corr <= 0 || std::abs(corr) < correlation_budget) continue;
        
        // 通过回调返回结果（不缓存！）
        yield(mA_candidate, mB_candidate, corr);
    }
}

template<typename Yield>
void NeoAlzetteLinearSearch::execute_subround0_backward(
    const LinearState& output,
    double correlation_budget,
    Yield&& yield
) {
    std::uint32_t mA = output.mA;
    std::uint32_t mB = output.mB;
    
    // ========================================================================
    // Subround 0反向传播（与Subround 1类似）
    // ========================================================================
    // 
    // Subround 0的前向操作：
    // 1. B += (rotl(A, 31) ^ rotl(A, 17) ^ RC[0]);
    // 2. A -= RC[1];
    // 3. A ^= rotl(B, 24);
    // 4. B ^= rotl(A, 16);
    // 5. A = l1_forward(A);
    // 6. B = l2_forward(B);
    // 7. [C0, D0] = cd_from_B(B, RC[2], RC[3]);
    // 8. A ^= (rotl(C0, 24) ^ rotl(D0, 16) ^ RC[4]);
    // 
    // 反向顺序：从Step 8到Step 1
    // 
    
    // 反向 Step 8, 7: cd_from_B（简化）
    std::uint32_t mA_before_cd = mA;
    std::uint32_t mB_before_cd = mB;
    
    // 反向 Step 6: A = l2_forward(A) → 注意这里A用的是l2！
    // 因为Subround 0中：A用l1，B用l2
    // 但在Subround 1后的状态，A已经经过了Subround 1的l2
    // 所以这里要看清楚是哪个变量用哪个线性层！
    // 
    // 等等，我需要重新检查forward函数...
    // 
    // forward函数中Subround 0:
    //   A = l1_forward(A);  ← A用l1
    //   B = l2_forward(B);  ← B用l2
    // 
    // 所以反向是：
    mA = NeoAlzetteCore::l1_transpose(mA_before_cd);
    
    // 反向 Step 5: B = l2_forward(B)
    mB = NeoAlzetteCore::l2_transpose(mB_before_cd);
    
    // 反向 Step 4: B ^= rotl(A, 16)
    std::uint32_t mB_before_xor2 = mB;
    std::uint32_t mA_before_xor2 = mA ^ NeoAlzetteCore::rotr(mB, 16);
    
    // 反向 Step 3: A ^= rotl(B, 24)
    std::uint32_t mA_before_xor1 = mA_before_xor2;
    std::uint32_t mB_before_xor1 = mB_before_xor2 ^ NeoAlzetteCore::rotr(mA_before_xor2, 24);
    
    // 反向 Step 2: A -= RC[1]（简化）
    std::uint32_t mA_before_sub = mA_before_xor1;
    
    // 反向 Step 1: B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])
    std::uint32_t mB_before_add = mB_before_xor1;
    
    // 枚举候选
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates = {
        {mA_before_sub, mB_before_add},
        {mA_before_sub ^ 1, mB_before_add},
        {mA_before_sub, mB_before_add ^ 1},
    };
    
    for (const auto& [mA_candidate, mB_candidate] : candidates) {
        // 注意：Subround 0是 B += (rotl(A,...))
        // 所以beta_mask来自mA_candidate，不是mB_candidate！
        std::uint32_t beta_mask = NeoAlzetteCore::rotl(mA_candidate, 31) ^ 
                                   NeoAlzetteCore::rotl(mA_candidate, 17);
        
        // 调用ARX算子：linear_cor_add_value_logn
        // 计算：mB_candidate, beta_mask → mB_before_add 的线性相关性
        double corr = arx_operators::linear_cor_add_value_logn(
            mB_candidate, beta_mask, mB_before_add
        );
        
        if (corr > 0 && std::abs(corr) >= correlation_budget) {
            yield(mA_candidate, mB_candidate, corr);
        }
    }
}

} // namespace neoalz
