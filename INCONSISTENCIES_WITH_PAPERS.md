# 与论文不一致的地方 - 完整列表

## 🔍 发现的不一致

### 1️⃣ pDDT Algorithm 1 - 额外的优化代码

**论文伪代码** (Lines 349-365):
```
1: procedure compute_pddt(n, pthres, k, pk, αk, βk, γk) do
2:   if n = k then
3:     Add (α, β, γ) ← (αk, βk, γk) to D
4:     return
5:   for x, y, z ∈ {0, 1} do
6:     αk+1 ← x|αk, βk+1 ← y|βk, γk+1 ← z|γk
7:     pk+1 = DP(αk+1, βk+1 → γk+1)
8:     if pk+1 ≥ pthres then
9:       compute_pddt(n, pthres, k+1, pk+1, αk+1, βk+1, γk+1)
```

**我们的实现** (`pddt_algorithm1_complete.cpp` Lines 99-105):
```cpp
// ⚠️ 论文中没有这个！
if (config.enable_pruning) {
    if (check_prefix_impossible(alpha_k1, beta_k1, gamma_k1, k + 1)) {
        stats.nodes_pruned++;
        continue;
    }
}
```

**问题**: 
- ❌ 论文中**没有**提到`check_prefix_impossible`
- ❌ 这是我们添加的"工程优化"
- ⚠️ 虽然不影响正确性，但**不是论文原始算法**

---

### 2️⃣ pDDT - 额外的约束优化函数

**我们的实现** (`pddt_algorithm1_complete.cpp` Lines 136-207):
```cpp
// ⚠️ 这个函数不在论文主算法中！
std::vector<PDDTAlgorithm1Complete::PDDTTriple> 
PDDTAlgorithm1Complete::compute_pddt_with_constraints(
    const PDDTConfig& config,
    int rotation_constraint
) {
    // From paper Appendix D.4: "Improving the efficiency of Algorithm 1"
    // ...
}
```

**问题**:
- ⚠️ 这是Appendix D.4的优化，不是主算法
- ⚠️ 对于TEA等特定结构的优化
- ⚠️ 不是论文主体的Algorithm 1

---

### 3️⃣ Matsui Algorithm 2 - 复杂的工程实现

**论文伪代码**是简洁的递归：
```
procedure threshold_search(n, r, H, B̂, Bn, T̂) do
    if ((r = 1) ∨ (r = 2)) ∧ (r ≠ n) then
        for all (α, β, p) in H do
            ...
```

**我们的实现**是分拆成多个函数：
```cpp
void threshold_search_recursive(...)  // 主递归
void process_early_rounds(...)        // 处理rounds 1-2
void process_intermediate_rounds(...) // 处理中间rounds
void process_final_round(...)         // 处理最后round
```

**问题**:
- ⚠️ 论文是一个函数，我们拆成了4个函数
- ⚠️ 虽然逻辑对应，但**结构不一样**
- ⚠️ 增加了大量工程细节（索引计算、回溯等）

---

### 4️⃣ cLAT算法 - 无法完全验证

**论文Algorithm 2** (Lines 713-774):
- 63行复杂的伪代码
- 大量位运算和状态转移

**我们的实现** (`clat_builder.hpp`):
- 200+行C++代码
- 嵌套循环和复杂逻辑

**问题**:
- ⚠️ **太复杂，我无法短时间内逐行验证**
- ⚠️ 可能有细节差异
- ⚠️ 需要单元测试才能确认

---

## 🎯 需要修复什么？

### 选项1: 严格按照论文（删除所有额外代码）

**删除/修改**:
1. ❌ 删除`check_prefix_impossible`优化
2. ❌ 删除`compute_pddt_with_constraints`
3. ❌ 将Matsui合并成单个函数（像论文一样）
4. ⚠️ 简化cLAT实现

**后果**:
- ✅ 100%对准论文伪代码
- ❌ 性能下降（失去优化）
- ❌ 代码可读性下降（单个巨型函数）

### 选项2: 保留优化，但添加"纯净版"

**添加**:
1. ✅ `pddt_algorithm1_strict.cpp` - 严格按论文，无优化
2. ✅ `matsui_algorithm2_strict.cpp` - 单函数，像论文
3. ✅ 保留现有的优化版本

**后果**:
- ✅ 可以验证正确性（对照纯净版）
- ✅ 保留性能优化
- ⚠️ 维护两套代码

### 选项3: 添加详细注释标记差异

**修改**:
1. 在每个"额外代码"处添加明显注释
2. 说明为什么添加
3. 说明如何关闭（config选项）

**后果**:
- ✅ 保留优化
- ✅ 清楚标记差异
- ❌ 仍然不是"纯论文实现"

---

## ❓ 你想要哪种修复？

**请告诉我**:
1. **严格论文版**（删除所有优化）？
2. **添加纯净版**（保留优化+添加严格版）？
3. **只添加注释**（标记差异）？
4. **其他方案**？

**我等你的决定，立即执行！** 🔧
