/**
 * @file bitvector_ops.hpp
 * @brief 位向量操作輔助函數（基於Bit-Vector論文）
 * 
 * 論文：A Bit-Vector Differential Model for the Modular Addition by a Constant (2022)
 * 複雜度：所有函數都是O(log n)或更快
 */

#pragma once

#include <cstdint>
#include <bit>

namespace neoalz {
namespace bitvector {

// ============================================================================
// 基礎位向量操作（O(1) 或 O(log n)）
// ============================================================================

/**
 * @brief HW(x) - Hamming Weight（漢明重量）
 * 
 * 計算x中1的個數
 * 複雜度：O(1)（硬件指令）
 * 
 * 論文：第209-213行
 */
inline uint32_t HW(uint32_t x) noexcept {
    return __builtin_popcount(x);
}

/**
 * @brief Rev(x) - Bit Reversal（位反轉）
 * 
 * 反轉x的位順序：Rev(x) = (x[0], x[1], ..., x[n-1])
 * 複雜度：O(log n)
 * 
 * 論文：第204-206行
 * 參考：Hacker's Delight, Fig. 7-1
 */
inline uint32_t Rev(uint32_t x) noexcept {
    // 交換相鄰位
    x = ((x & 0x55555555) << 1) | ((x >> 1) & 0x55555555);
    // 交換相鄰2位組
    x = ((x & 0x33333333) << 2) | ((x >> 2) & 0x33333333);
    // 交換相鄰4位組
    x = ((x & 0x0F0F0F0F) << 4) | ((x >> 4) & 0x0F0F0F0F);
    // 交換字節
    x = ((x & 0x00FF00FF) << 8) | ((x >> 8) & 0x00FF00FF);
    // 交換半字
    x = (x << 16) | (x >> 16);
    return x;
}

/**
 * @brief Carry(x, y) - 進位鏈
 * 
 * 計算x + y的進位鏈
 * 公式：Carry(x, y) = x ⊕ y ⊕ (x ⊞ y)
 * 複雜度：O(1)
 * 
 * 論文：第198-200行
 */
inline uint32_t Carry(uint32_t x, uint32_t y) noexcept {
    return x ^ y ^ ((x + y) & 0xFFFFFFFF);
}

/**
 * @brief RevCarry(x, y) - 反向進位
 * 
 * 從右到左的進位傳播
 * 公式：RevCarry(x, y) = Rev(Carry(Rev(x), Rev(y)))
 * 複雜度：O(log n)
 * 
 * 論文：第207-208行
 */
inline uint32_t RevCarry(uint32_t x, uint32_t y) noexcept {
    return Rev(Carry(Rev(x), Rev(y)));
}

/**
 * @brief LZ(x) - Leading Zeros（前導零標記）
 * 
 * 標記x的前導零位
 * 定義：LZ(x)[i] = 1 ⟺ x[n-1, i] = 0
 * 即：從最高位到第i位都是0
 * 複雜度：O(1)（使用硬件指令）
 * 
 * 論文：第214-218行
 */
inline uint32_t LZ(uint32_t x) noexcept {
    if (x == 0) return 0xFFFFFFFF;
    
    // 使用CLZ（Count Leading Zeros）
    int clz = __builtin_clz(x);  // 計算前導零個數
    
    // 構造標記向量：從最高位到clz位都設為1
    return (clz == 32) ? 0xFFFFFFFF : (0xFFFFFFFF << (32 - clz));
}

// ============================================================================
// 高級位向量操作（Algorithm 1核心）
// ============================================================================

/**
 * @brief ParallelLog(x, y) - 並行對數
 * 
 * 對於y分隔的子向量，並行計算x的對數（整數部分）
 * 公式：ParallelLog(x, y) = HW(RevCarry(x ∧ y, y))
 * 複雜度：O(log n)
 * 
 * 論文：第1479行，Proposition 1(a)
 * 
 * @param x 數據向量
 * @param y 分隔向量（每個子向量為 (1,1,...,1,0)）
 * @return 所有子向量的 log₂ 之和
 */
inline uint32_t ParallelLog(uint32_t x, uint32_t y) noexcept {
    return HW(RevCarry(x & y, y));
}

/**
 * @brief ParallelTrunc(x, y) - 並行截斷
 * 
 * 對於y分隔的子向量，並行提取x的小數部分（最高4位）
 * 公式：ParallelTrunc(x, y) = (HW(z₀) << 0) ⊕ (HW(z₁) << 2) ⊕ (HW(z₂) << 1) ⊕ HW(z₃)
 * 其中 z_λ = x ∧ (y << λ) ∧ ¬(y << (λ+1))
 * 複雜度：O(log n)
 * 
 * 論文：第1480-1492行，Proposition 1(b)
 * 
 * @param x 數據向量
 * @param y 分隔向量
 * @return 所有子向量的截斷小數部分之和
 */
inline uint32_t ParallelTrunc(uint32_t x, uint32_t y) noexcept {
    // z_λ = x ∧ (y << λ) ∧ ¬(y << (λ+1))
    uint32_t z0 = x & y & ~(y << 1);
    uint32_t z1 = x & (y << 1) & ~(y << 2);
    uint32_t z2 = x & (y << 2) & ~(y << 3);
    uint32_t z3 = x & (y << 3);
    
    // ParallelTrunc = (HW(z₀) << 0) ⊕ (HW(z₁) << 2) ⊕ (HW(z₂) << 1) ⊕ HW(z₃)
    uint32_t result = HW(z0);
    result ^= (HW(z1) << 2);
    result ^= (HW(z2) << 1);
    result ^= HW(z3);
    
    return result;
}

} // namespace bitvector
} // namespace neoalz
