# NeoAlzette 自动搜索框架：两个 BNB 主算法的严格数学施工蓝图（Unicode 版）

## 0. 总原则与当前交付范围

本文件只做一件事：先把两个主算法的数学对象、搜索语义、剪枝正确性、残问题自然生长机制写成稳定蓝图，再允许 C++ 去承载它们。

在 BNB 主算法语义这一层，本文件应视为新的基准说明；旧的 `AUTO_SEARCH_FRAME_ARCHITECTURE_*.md` 只保留历史参考价值，不再作为主算法数学语义的最终依据。

核心原则只有一句：

`工程实现不定义正确性；工程实现只能承载已经独立成立的数学定义。`

因此本文不以 checkpoint 设计为出发点，而是先冻结数学对象；checkpoint 只能序列化已经稳定的“问题状态对象”。  
在本文的增强版里，我们不仅说明“为什么不能让 checkpoint 抢跑”，也会把**当数学对象已经冻结后，checkpoint 必须序列化哪些内容**说清楚。

本文的目标是先稳定以下五件事：

1. 两个主算法的严格数学蓝图。
2. 残问题自然生长机制的严格语义。
3. Q2 选参器到 Q1 评估器的接口闭环。
4. 注入层 rank 分析在主算法中的位置。
5. 这些机制为什么在全局搜索中严格成立。

### 0.1 阅读导图

建议按下面顺序阅读：

1. 若你是实现者：
   先看 `0.3`、`1.5`、`2.3`、`3.6.4`、`4.6.4`、`6`、`7`
2. 若你是审稿人：
   先看 `0.3`、`2`、`3.8`、`4.8`、`5`、`8`
3. 若你想核对论文到工程的对应：
   差分看 `3.5` 与 `3.7.11`
   线性看 `4.5` 与 `4.7.10`

### 0.2 本文中“严格”的判定标准

本文里“严格”不是泛泛地说“理论上合理”，而是要求同时满足下面四条：

1. **对象严格**：每个被复用、去重、支配、checkpoint 的对象，都有独立于实现的数学身份。
2. **局部算子严格**：真正进入累计权重的局部量，必须来自 exact Q1 或 exact 注入 rank，而不是 heuristic Q2。
3. **剪枝严格**：每次剪枝都必须能写成
   `w_pref + exact_local_term + admissible_suffix_lower_bound ≥ incumbent`
   或其等价变形。
4. **复用严格**：两个搜索请求只有在“后缀问题定义完全相同”时才允许共享结果或共享 dominance 判定。

只要缺失任意一条，就不能称为本文意义下的“严格”。

### 0.3 审稿人速查：本文最终冻结的结论

为了让读者和审稿人先抓住最核心的判断，本文最终冻结以下结论：

1. 残问题必须拆成：
   - 数学语义身份
   - provenance / 来源身份
   - 调度载体
   - 可选执行快照
2. **数学语义身份**与**执行快照**绝不能混同。
3. `Q2` 只负责候选、shell、列极值、严格有序 witness 流、严格下界；
   **只有 `Q1` 或注入 rank 才能写入累计权重。**
4. 差分 Forward 与线性 Backward 的**合法 residual anchor 集合**必须逐 stage 明确列出。
5. 规范化边界状态 `σ̂` 必须逐 stage 明确给出，而不是留给实现者自行脑补。
6. 若某 stage 的 `σ̂` 不是两字状态，则工程里的 `(pair_a, pair_b)` 仅在给出**单独的 injective canonicalization 证明**后才可充当语义键载体。
7. `r_abs` 与 `SrcTag` 属于 provenance / metadata；它们默认不进入语义支配关系，除非后缀问题定义本身显式依赖它们。
8. 在本文增强版冻结这些对象后，checkpoint 才有资格被视为“严格恢复语义”的承载层。

---

## 1. 统一记号

### 1.1 基本代数记号

- 字长固定为 `n = 32`，但定义写成一般 `n` 更稳妥。
- `⊞` 表示模 `2ⁿ` 加法。
- `⊟` 表示模 `2ⁿ` 减法。
- `⊕` 表示按位 XOR。
- `ROTL_r`, `ROTR_r` 表示循环移位。

### 1.2 两类局部权重

- 差分侧局部权重：
  `w_diff = -log₂ P`
- 线性侧局部权重：
  `w_lin = ⌈-log₂ |corr|⌉`

在本文里，“严格”始终指：

- 差分局部权重由精确 DP 或精确可达集合给出。
- 线性局部权重由精确相关绝对值或精确 rank 给出。
- Q2 只能提供候选、列极值、严格有序候选流、或严格下界；
  它永远不能代替 Q1 的正确性来源。

### 1.3 方向

- `Dir = DiffForward`：差分分析，正向。
- `Dir = LinBackward`：线性分析，反向。

### 1.4 目标标签

令 `Ω` 表示当前任务目标标签。它至少允许以下类型：

- `Best`
- `Threshold(τ)`
- `TopK(k)`
- `Distribution(W_max)`
- `HullCap(W_max)`

默认最严格策略是：`Ω` 必须进入残问题身份，不允许把不同目标标签的结果直接混并。

为便于工程键与数学键对齐，另定义 `ObjKind = coarse_type(Ω)`，它只保留粗粒度目标族，例如：

- `Best`
- `HullCollect`
- `Distribution`
- `TopK`
- `Threshold`

`ObjKind` 不能替代 `Ω`；它只是工程键中常见的粗投影。

### 1.5 锚点、语义身份、provenance 身份

令 `κ` 表示轮内锚点阶段，`r_abs` 表示绝对轮号，`r_rem` 表示剩余轮数。

为了避免把“同一个后缀问题的数学身份”和“它从哪里来、怎么恢复”混为一谈，本文把一个 residual object 分成四层：

1. **语义身份层**
   `R_sem = (Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`
2. **provenance 层**
   `R_meta = (r_abs, SrcTag)`
3. **调度层**
   `R_sched = (R_sem, R_meta, w_pref)`
4. **执行快照层**
   `Snap(R_sched)`

其中：

- `Dir`：分析方向。
- `ObjKind`：粗目标型别，例如 `Best`、`HullCollect`、`Distribution`。
- `κ`：锚点步骤索引 / 轮内位置。
- `r_rem`：从该锚点继续求解还剩多少轮。
- `σ̂`：规范化边界状态。
- `Ω`：目标标签。
- `SfxDef`：后缀问题定义。
- `r_abs`：锚点所处绝对轮号。
- `SrcTag`：来源标签。
- `w_pref`：到当前锚点为止已经累计的前缀权重。

必须强调：

- `R_sem` 才是**支配、去重、求解结果复用**的严格来源。
- `R_meta` 只负责 provenance / 审计 / 防止错误外层合并。
- `R_sched` 才是前沿队列里真正被调度的对象。
- `Snap(R_sched)` 只是恢复加速器；它可以帮助从中断点继续跑，但不能决定残问题的数学身份。

`σ̂` 的意义是：

- 差分侧：当前锚点继续向前搜索所需的最小边界差分状态。
- 线性侧：当前锚点继续向后搜索所需的最小边界线性掩码状态。

注意：`σ̂` 不是“当前 frame 里所有临时变量”的转存；它只能保存继续定义后缀问题所必需的最小规范化状态。其余执行细节最多只是“恢复加速器状态”的工程缓存，不是数学身份。

这里还要明确一条工程边界：

- `SrcTag` 可以承载 wrapper / batch / subspace 这类 outer provenance 命名空间。
- 这些 provenance 标签只用于防止错误合并。
- 它们不改变 `subspace hull`、`endpoint hull`、`shell mass`、`signed mass`、`theorem identity` 这些对象的数学定义本身。

再进一步，把 `R_sem` 压缩成工程键时，必须经过一个**规范编码**：

`K_sem(R) = Canon(Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`

若工程实现想把它压成有限字段，例如

`(domain, objective, stage_cursor, rounds_remaining, pair_a, pair_b, suffix_profile_id)`

则必须证明这套编码对所有合法 residual anchor 是**单射**。  
若没有单射证明，该压缩键就不能被当成严格数学键。

### 1.6 后缀问题定义 `SfxDef`

`SfxDef` 至少必须固定：

- 方向 `Dir`
- 算法型别与轮函数版本
- 锚点阶段 `κ`
- 剩余轮数 `r_rem`
- 该后缀从此状态继续时所使用的严格局部算子族

只要 `SfxDef` 不同，就不是同一个残问题。

---

## 2. 残问题自然生长机制的严格语义

### 2.1 残问题不是废料，而是后缀子问题

BNB 中一个被截断的中间节点，不能被数学上视为“被证明无意义的垃圾”。

它真正代表的是：

`在当前锚点 κ、当前规范化边界状态 σ̂、当前目标 Ω 下，一个尚未继续展开的严格后缀子问题。`

所以被剪掉的不是“信息”，而只是“本轮没有继续算完这个后缀子问题的机会”。  
因此它必须被提升为可复用对象 `R`。

### 2.2 残问题的四层载体

为了防止“语义身份”和“恢复快照”再次被混同，本文在此重复并落实 `1.5` 的最终分层：

1. 数学语义层：`R_sem = (Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`
2. provenance 层：`R_meta = (r_abs, SrcTag)`
3. 调度层：`R_sched = (R_sem, R_meta, w_pref)`
4. 执行快照层：`Snap(R_sched)`

其中：

- `R_sem` 才是严格性来源。
- `R_meta` 负责来源与审计。
- `R_sched` 负责进入 frontier / dominance / solved 判定。
- `Snap(R_sched)` 只是加速恢复用的执行载体。

若 `Snap(R_sched)` 丢失，只要 `R_sched` 还在，理论上就必须允许从 `R_sem + R_meta + w_pref` 重新构造同一个后缀问题。  
这也是为什么 checkpoint 必须排在数学对象稳定之后。

### 2.3 残问题的主键与当前工程对象的关系

当前工程里已有接近这个语义的对象：

- `ResidualProblemKey`
- `ResidualProblemRecord`
- `pending_frontier`
- `pending_frontier_entries`
- `completed_residual_set`
- `best_prefix_by_residual_key`
- `global_residual_result_table`

本文在此把“最终严格版本”冻结如下。

### 2.3.1 数学语义键

真正参与去重 / 支配 / solved 复用的键应是

`K_sem(R) = Canon(Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`

等价地，也可以写成

`K_sem(R) = (Dir, ObjKind, κ, r_rem, Canon(σ̂), SfxProfile)`

其中：

- `Canon(σ̂)` 是 stage-aware 的规范边界编码；
- `SfxProfile = Canon(Ω, SfxDef)`；
- 只要 `K_sem` 相同，就代表“继续向后求解时面对的是同一个严格后缀问题”。

### 2.3.2 provenance 键

`r_abs` 与 `SrcTag` 不默认进入 `K_sem`。  
它们属于 provenance / metadata：

- `r_abs` 解释“这个残问题是在哪个绝对轮号上被观察到的”
- `SrcTag` 解释“这个残问题来自哪个 outer provenance 命名空间”

只有当后缀问题定义本身显式依赖某个 provenance 字段时，该字段才允许提升进入 `SfxDef`，从而间接进入 `K_sem`。

### 2.3.3 与当前工程键的严格对应关系

当前工程里可读作：

- `domain` ↔ `Dir`
- `objective` ↔ `ObjKind`
- `rounds_remaining` ↔ `r_rem`
- `stage_cursor` ↔ `κ`
- `pair_a, pair_b` ↔ `Canon(σ̂)` 的工程载体
- `suffix_profile_id` ↔ `SfxProfile`
- `absolute_round_index` ↔ `r_abs` 的 provenance 载体
- `source_tag` ↔ `SrcTag` 的 provenance 载体

于是当前工程里**可被严格接受**的解释应是：

`ResidualProblemKey = (domain, objective, rounds_remaining, stage_cursor, pair_a, pair_b, suffix_profile_id)`

是语义键；

`absolute_round_index` 与 `source_tag`

是附带元数据，而不是支配/去重本体。

### 2.3.4 单射性要求

这里有一个审稿人必须抓住的硬约束：

- 若某个合法 stage 的 `σ̂` 需要三字或更多状态，
- 而工程键只保留 `(pair_a, pair_b)` 两字，
- 那么只有在给出**单独的 injective canonicalization 证明**时，这种压缩才是严格的。

否则：

- 该 stage 不能被当前两字段工程键严格承载；
- 或者工程实现必须扩展键载体。

这条约束不是实现建议，而是严格性红线。

### 2.4 残问题结果对象

对同一个残问题 `R`，允许有不同种类的结果：

- `Ans_best(R)`
- `Ans_threshold(R, τ)`
- `Ans_topk(R, k)`
- `Ans_dist(R, W_max)`

默认安全规则：

- 只有目标标签完全相同，结果才允许直接复用。
- 若想跨目标复用，必须先给出单独的语义包含证明。

例如：

- 已知 `Ans_best(R)`，可以直接给出后缀最优值。
- 已知 `Ans_dist(R, W_max)`，可导出所有 `≤ W_max` 的 threshold / shell 信息。
- 已知 `Ans_topk(R, k)`，并不能直接推出完整 distribution。

### 2.5 前缀移位定律

残问题复用之所以数学上成立，根本原因是目标函数对前缀和后缀是可加或可移位的。

若某次搜索在锚点 `R` 前已经累积前缀权重 `w_pref`，则：

- 最优值：
  `Best_total = w_pref + Best_suffix(R)`
- 阈值：
  `Threshold_total(τ)` 等价于 `Threshold_suffix(τ - w_pref)`
- top-k：
  对后缀结果的每个元素统一做 `+ w_pref` 的权重平移，再把前缀 trail 接上
- distribution：
  后缀 shell 分布整体向右平移 `w_pref`

因此，残问题的求解结果本质上是“可被前缀平移”的后缀库存。

### 2.6 有限量记忆体的正确角色

需要加入一个有限量记忆体来压制重复残问题循环，但这个记忆体不是正确性来源。

记忆体只做两件事：

1. 去重
2. 支配剪枝

定义一个有限表 `M_recent`，记录最近看到的

`(K_sem(R), best_prefix_weight_seen)`

若再次看到同一个语义键 `K_sem(R)`，且新的前缀权重 `w_pref_new ≥ w_pref_best_seen`，则可安全跳过该重复展开。

理由很简单：

- 后缀问题定义相同。
- 总权重是 `w_pref + suffix_weight`。
- 同一个后缀，较重前缀不可能产生更优总结果。

若表项因为容量限制而被逐出，唯一后果只是以后可能重复搜索；严格性不受影响。

---

## 3. Algorithm 1：差分分析严格数学的工程版算法（Forward）

## 3.1 单轮前向分解

设本轮输入差分为

`(ΔA₀, ΔB₀)`

当前轮的严格数学分解为：

1. 第一模加：
   `T₀ = ROTL₃₁(ΔA₀) ⊕ ROTL₁₇(ΔA₀)`

   `ΔB₁` 是
   `ΔB₀ ⊞ T₀`
   的输出差分。

2. 第一常量模减：
   `ΔA₁` 是
   `A₀ ⊟ RC₁`
   在 XOR 差分下的输出差分。

3. 第一组确定性 XOR/ROT 传输：

   `ΔA₂ = ΔA₁ ⊕ ROTL_R0(ΔB₁)`

   `ΔB₂ = ΔB₁ ⊕ ROTL_R1(ΔA₂)`

4. 显式预白化防御步：

   `ΔB_pre = ΔB₂`

   因为工程真实步骤是
   `B_pre = B_raw ⊕ RC₄`，
   所以在 XOR 差分下这是一个**零代价、确定性**步骤。

5. `B → A` 注入：

   设注入映射为 `F_B`，其输入是已经过预白化的 `B_pre`。
   则注入差分 `ΔI_B` 来自 `F_B` 的精确可达仿射子空间。

   `ΔA₃ = ΔA₂ ⊕ ΔI_B`

6. 第二模加：
   `T₁ = ROTL₃₁(ΔB₂) ⊕ ROTL₁₇(ΔB₂)`

   `ΔA₄` 是
   `ΔA₃ ⊞ T₁`
   的输出差分。

7. 第二常量模减：

   `ΔB₄` 是
   `B₂ ⊟ RC₆`
   的输出差分。

8. 第二组确定性 XOR/ROT 传输：

   `ΔB₅ = ΔB₄ ⊕ ROTL_R0(ΔA₄)`

   `ΔA₅ = ΔA₄ ⊕ ROTL_R1(ΔB₅)`

9. 显式预白化防御步：

   `ΔA_pre = ΔA₅`

   因为工程真实步骤是
   `A_pre = A_raw ⊕ RC₉`，
   所以在 XOR 差分下这同样是一个**零代价、确定性**步骤。

10. `A → B` 注入：

   设注入映射为 `F_A`，其输入是已经过预白化的 `A_pre`。
   则注入差分 `ΔI_A` 来自 `F_A` 的精确可达仿射子空间。

   `ΔB₆ = ΔB₅ ⊕ ΔI_A`

11. 本轮输出边界：

   `ΔA_out = ΔA₅`

   `ΔB_out = ΔB₆`

本轮总权重为

`w_round = w_add0 + w_subA + w_injB + w_add1 + w_subB + w_injA`

---

## 3.2 差分侧两类 Q1 精确评估器

### 3.2.1 variable-variable Q1

对模加 `γ = α ⊞ β` 的 XOR 差分，定义

`Q1ᵈ,vv(α, β, γ) = w_diff_vv(α, β → γ)`

它必须由精确 differential probability 模型给出。  
在当前工程中，它对应 LM2001 型精确权重评估：

- `xdp_add_lm2001`
- `xdp_add_lm2001_n`

因此：

- Q2 只能告诉我们“该先试哪个 `γ`”或“当前 shell 是哪一层”。
- 真正决定局部正确性的，仍然是 `Q1ᵈ,vv`。

### 3.2.2 variable-constant Q1

对模加常量或模减常量：

- `β = x ⊞ C` 的差分权重
- `β = x ⊟ C` 的差分权重

统一记作

`Q1ᵈ,vc(α, C, β, op) = w_diff_vc(α --op,C→ β)`

其中 `op ∈ {⊞const, ⊟const}`。

它必须由精确 var-const DP 给出。  
在当前工程中，这对应 `constant_weight_evaluation.hpp` 里的精确 count / exact weight 路线，而不是近似权重替身。

---

## 3.3 差分侧四类 Q2 参数选择优化器

差分主算法需要把“固定哪一侧，优化哪一侧”的四个局部对象全部明确定义。

### 3.3.1 variable-variable，固定输入差分

定义

`Q2ᵈ,vv,in(α, β) = argmin_γ Q1ᵈ,vv(α, β, γ)`

它返回局部最优输出差分 `γ*`，以及与之对应的最优局部权重。  
在当前工程中，这对应 `find_optimal_gamma` / `find_optimal_gamma_with_weight`。

这是差分 Forward 主算法最自然、也最直接消费的 var-var Q2。

### 3.3.2 variable-constant，固定输入差分

定义

`Q2ᵈ,vc,in(α; C, op) = argmin_β Q1ᵈ,vc(α, C, β, op)`

它返回固定输入差分下的最优输出差分 `β*`。  
在当前工程中，这对应 `constant_optimal_input_alpha.hpp`。

### 3.3.3 variable-variable，固定输出差分

定义

`Q2ᵈ,vv,out(γ; 𝒞_loc) = argmin_(α,β)∈𝒞_loc Q1ᵈ,vv(α, β, γ)`

这里 `𝒞_loc` 表示当前锚点上下文允许的局部输入候选域。  
这个对象在当前差分 Forward 主搜索里不是最常用极性，但在以下场景中是必须的：

1. 把某个 add 节点当作新的 suffix 根来继续生长时。
2. 某个外层目标先固定了下游边界输出差分，再让上游输入对去适配时。
3. 做中间状态空间自然生长，而不是只做原始输入口 `foreach` 时。

因此，哪怕当前主引擎主要消费固定输入极性，数学蓝图里也必须把固定输出极性立为第一类对象。

### 3.3.4 variable-constant，固定输出差分

定义

`Q2ᵈ,vc,out(β; C, op) = argmin_α Q1ᵈ,vc(α, C, β, op)`

它返回固定输出差分下的最优输入差分 `α*`。  
在当前工程中，这对应 `constant_optimal_output_beta.hpp`。

---

## 3.4 注入层的二阶差分导数 rank 分析

令注入层真实映射分别为

- `F_B : {0,1}ⁿ → {0,1}ⁿ`
- `F_A : {0,1}ⁿ → {0,1}ⁿ`

它们都是向量二次 Boolean 映射。

对固定输入差分 `Δ`，定义一阶差分导数

`D_ΔF(x) = F(x) ⊕ F(x ⊕ Δ)`

由于 `F` 是二次映射，`D_ΔF` 对固定 `Δ` 一定是仿射映射，因此可写成

`D_ΔF(x) = M_Δ x ⊕ c_Δ`

这里真正进入 BNB 的不是“某个经验概率表”，而是以下严格对象：

- 可达输出差分集合：
  `c_Δ ⊕ im(M_Δ)`
- 每个可达输出差分的精确概率：
  `2^(-rank(M_Δ))`

于是注入层差分局部权重严格等于

`w_inj(Δ) = rank(M_Δ)`

因此注入层在差分主算法里不需要再拆成“Q2 候选器 + Q1 评估器”两层；  
它本身已经直接给出：

1. 精确可达仿射子空间
2. 精确局部权重

这正是 `InjectionAffineTransition = (offset, basis, rank_weight)` 的数学含义。

---

## 3.5 `Weight-Sliced pDDT` 的论文来源、数学对象与严格语义

### 3.5.1 原论文里的 `pDDT` 是什么

在 ARX threshold search 路线里，`pDDT` 的含义是：

`pDDT_{p_thres}(α,β) = { γ | DP(α,β→γ) ≥ p_thres }`

也就是：

- 固定输入差分 `(α,β)`
- 只保留概率不低于阈值 `p_thres` 的输出差分 `γ`

这个对象的本质是“概率阈值截断表”。  
它的优势是快，缺点也同样明显：若把表本身当成真值，就会丢掉阈值以下但仍可能属于真正最优 trail 的候选，因此它只能支持 heuristic threshold search，而不能直接支持严格最优性。

### 3.5.2 2016 Best-Trail 论文给出的严格路线是什么

`Automatic Search for the Best Trails in ARX - Application to Block Cipher Speck` 的关键转折是：

- 不再依赖 `pDDT`
- 改用 Matsui 式 round recursion + bit recursion
- 直接对局部输出差分 `γ` 的 bit-prefix 做精确前缀概率检查

论文明确说，这个版本：

- `does not use any heuristics`
- `finds optimal results`

因此，对我们来说，真正严格的差分主语义来自：

1. exact `xdp⁺`
2. prefix monotonicity
3. Matsui branch-and-bound pruning

而不是来自 `pDDT`。

### 3.5.3 我们的 `Weight-Sliced pDDT` 不再是 threshold table

因此仓库里的 `Weight-Sliced pDDT` 必须定义成：

`Shell_tᵈ(α,β) = { γ | Q1ᵈ,vv(α,β,γ) = t }`

其中

`Q1ᵈ,vv(α,β,γ) = xdp_add_lm2001_n(α,β,γ,n)`

于是对固定 `(α,β)` 的全部 exact 可行输出差分集合为

`Ωᵈ(α,β) = ⨆_{t=0}^{n-1} Shell_tᵈ(α,β)`

也就是说：

- 原始 `pDDT`：按概率阈值切
- `Weight-Sliced pDDT`：按 exact weight shell 切

这两者不是一回事。

### 3.5.4 `Weight-Sliced pDDT` 在严格搜索里到底干什么

它只能做四件事：

1. 对固定 `(α,β)` 提供低权 shell 的索引或缓存。
2. 保证候选按 `t = 0,1,2,...` 的 exact weight 递增顺序吐出。
3. 在 shell hit 时减少重复 exact 生成。
4. 在 shell miss 时回退到 exact shell generator。

它绝对不能做第五件事：

5. 不能把 `miss` 解释成“不存在候选”。

严格语义必须是：

- `hit`：命中某一完整 shell 的缓存
- `miss`：没有缓存到该 shell
- `empty`：只有 exact shell generator 证明该 shell 为空时才成立

### 3.5.5 `output_hint` 的角色

当前工程里 `WeightSlicedPddtKey` 还带有 `output_hint`。  
严格数学上要这样理解：

- `Shell_tᵈ(α,β)` 决定候选集合本身
- `output_hint` 只决定集合内部的遍历顺序

所以 `output_hint` 是顺序偏置，不是集合定义本体。

### 3.5.6 strict-safe 合同

因此差分 `Weight-Sliced pDDT` 的严格合同是：

1. 壳层对象是 `Shell_tᵈ(α,β)`，不是 threshold subset。
2. 每个壳层必须完整。
3. hit 只加速，不定义真值。
4. miss 只能触发 exact fallback。
5. 缓存是 rebuildable accelerator，不是 must-live correctness state。

---

## 3.6 差分 Forward BNB 的全局接口闭环

### 3.6.1 全局 incumbent 与剩余轮下界

令当前已知全局最优上界为 `W*`，当前前缀累计权重为 `w_pref`。  
令 `LB_rem(r)` 表示“从当前边界起还剩 `r` 轮时的严格可采纳下界”。

则全局剪枝条件是

`w_pref + LB_rem(r) ≥ W*  ⇒  可安全剪枝`

这里的“可安全”要求 `LB_rem(r)` 绝不能高估真实最优后缀权重。

### 3.6.2 单轮剩余预算

在第 `r` 个边界进入本轮时，定义本轮可用预算

`Cap_round = W* - w_pref - LB_rem(after_this_round)`

任一局部算子在本轮中继续展开之前，都必须保证：

`exact_weight_committed_so_far + exact_lower_bound_of_rest < Cap_round`

否则该局部节点可直接剪枝或提升为残问题。

### 3.6.3 Q2 → Q1 闭环

对每个非线性局部节点，都必须满足以下闭环：

1. Q2 给出候选、列极值、shell、严格有序候选流、或严格局部下界。
2. Q1 对具体候选做精确局部权重判定。
3. 只有 Q1 或注入 rank 才能真正写入累计权重。
4. 所有剪枝都基于
   `w_pref + exact_local_weight + admissible_future_lower_bound`
   或
   `w_pref + exact_local_floor + admissible_future_lower_bound`

### 3.6.4 差分 Forward 的规范化边界状态

差分侧在任一剪枝边界，必须能把当前局部状态外化为新问题对象。  
记 stage 为 `κ`，则规范化边界状态写成

`σ̂_diff(κ) = N_diff,κ(local_state)`

差分 Forward 的**合法 residual anchor 集合**在本文中冻结为：

`κ ∈ { Enter, FirstConst, InjB, SecondAdd, SecondConst, InjA }`

并且各自的规范化状态明确规定为：

- `N_diff,Enter(local_state)       = (ΔA₀, ΔB₀)`
- `N_diff,FirstConst(local_state)  = (ΔA₀, ΔB₁)`
- `N_diff,InjB(local_state)        = (ΔA₂, ΔB₂)`
- `N_diff,SecondAdd(local_state)   = (ΔA₃, ΔB₂)`
- `N_diff,SecondConst(local_state) = (ΔA₄, ΔB₂)`
- `N_diff,InjA(local_state)        = (ΔA₅, ΔB₅)`

这里的严格要求是：

1. `N_diff,κ` 必须只保留“从该 stage 继续向前求解所必需的最小边界差分状态”。
2. 任意 Q2 枚举器内部游标、shell index、hint 顺序、缓存命中信息，都不属于 `σ̂_diff(κ)`。
3. 这些执行信息若需要恢复，只能进入 `Snap(R_sched)`，不能反过来影响 `R_sem`。

因此差分 Forward 的数学身份层在所有合法 residual anchor 上都是**严格二字边界状态**；  
这也解释了为什么差分侧更容易被工程里 `(pair_a, pair_b)` 型语义键承载。

---

## 3.7 Algorithm 1 的 Unicode 严格数学伪代码

### 3.7.1 主过程

```text
Algorithm 1A  DiffForward-Best-BNB

Input:
  R₀ = (Dir=DiffForward, κ=Enter, r_abs=0, r_rem=R, σ̂=(ΔA_in,ΔB_in), Ω=Best)

Global State:
  W*                // 当前已知最优总权重，初值可由 greedy upper bound 给出
  Trail*            // 当前最优 trail
  LB_rem[0..R]      // admissible remaining-round lower bound
  Frontier          // 待继续生长的残问题前沿
  Solved            // 已求解残问题结果表
  BestPrefixSeen    // 同 residual key 下最优前缀表
  CursorStack       // 显式栈 DFS

Procedure MAIN():
  ActivateResidual(R₀, Snap = null)
  while true:
    while CursorStack ≠ ∅:
      let F = Top(CursorStack)
      switch F.κ:
        case Enter:       Diff-Enter(F)
        case FirstAdd:    Diff-FirstAdd(F)
        case FirstConst:  Diff-FirstConst(F)
        case InjB:        Diff-InjB(F)
        case SecondAdd:   Diff-SecondAdd(F)
        case SecondConst: Diff-SecondConst(F)
        case InjA:        Diff-InjA(F)
    MarkActiveResidualCompleted()
    if not RestoreNextResidualFromFrontier():
      break
  return (W*, Trail*)
```

### 3.7.2 残问题注册与恢复

```text
Procedure RegisterDiffResidual(κ, r_abs, r_rem, local_state, w_pref, Snap?):
  σ̂ ← N_diff,κ(local_state)
  R  ← (DiffForward, κ, r_abs, r_rem, σ̂, Ω_current, SfxDef_current, SrcTag_current)
  if R ∈ Solved:
    return
  if BestPrefixSeen[R] ≤ w_pref:
    return
  BestPrefixSeen[R] ← w_pref
  PushOrImprove(Frontier, (R, w_pref, Snap))

Procedure ActivateResidual(R, Snap):
  if Snap exists:
    CursorStack ← ResumeFromSnapshot(Snap)
  else:
    CursorStack ← [ MakeDiffFrame(κ=Enter, r_abs=R.r_abs, r_rem=R.r_rem, σ̂=R.σ̂, w_pref=0) ]
```

### 3.7.3 进入一轮

```text
Procedure Diff-Enter(F):
  let (ΔA₀, ΔB₀) = F.σ̂
  let r_rem      = F.r_rem
  let w_pref     = F.w_pref

  if w_pref + LB_rem[r_rem] ≥ W*:
    RegisterDiffResidual(Enter, F.r_abs, r_rem, (ΔA₀,ΔB₀), w_pref, Snapshot(F))
    Pop(CursorStack); return

  if r_rem = 0:
    if w_pref < W*:
      W*    ← w_pref
      Trail*← CurrentTrail()
    Pop(CursorStack); return

  let LB_next   = LB_rem[r_rem-1]
  let Cap_round = W* - w_pref - LB_next
  if Cap_round ≤ 0:
    RegisterDiffResidual(Enter, F.r_abs, r_rem, (ΔA₀,ΔB₀), w_pref, Snapshot(F))
    Pop(CursorStack); return

  T₀ ← ROTL₃₁(ΔA₀) ⊕ ROTL₁₇(ΔA₀)
  (ΔB₁^hint, w_add0^min) ← Q2ᵈ,vv,in(ΔB₀, T₀)
  if w_add0^min > min(Cap_round, Cap_add):
    RegisterDiffResidual(Enter, F.r_abs, r_rem, (ΔA₀,ΔB₀), w_pref, Snapshot(F))
    Pop(CursorStack); return

  F.enum_first_add ← DiffVVIn-ShellEnum(ΔB₀, T₀, cap=min(Cap_round,Cap_add), hint=ΔB₁^hint)
  F.κ ← FirstAdd
```

### 3.7.4 第一模加

```text
Procedure Diff-FirstAdd(F):
  if not Next(F.enum_first_add, out ΔB₁, out w_add0):
    Pop(CursorStack); return

  w₁ ← F.w_pref + w_add0
  if w₁ + LB_rem[F.r_rem-1] ≥ W*:
    RegisterDiffResidual(FirstConst, F.r_abs, F.r_rem, (ΔA₀,ΔB₁), w₁, SnapshotAfterFirstAdd(F,ΔB₁,w_add0))
    return

  Cap_subA ← min(Cap_const, W* - w₁ - LB_rem[F.r_rem-1])
  if Cap_subA < 0:
    RegisterDiffResidual(FirstConst, F.r_abs, F.r_rem, (ΔA₀,ΔB₁), w₁, SnapshotAfterFirstAdd(F,ΔB₁,w_add0))
    return

  F.state.ΔB₁    ← ΔB₁
  F.state.w_add0 ← w_add0
  F.enum_first_const ← DiffVCIn-Enum(ΔA₀, RC₁, op=⊟, cap=Cap_subA)
  F.κ ← FirstConst
```

### 3.7.5 第一常量模减与第一桥接

```text
Procedure Diff-FirstConst(F):
  if not Next(F.enum_first_const, out ΔA₁, out w_subA):
    F.κ ← FirstAdd; return

  w₂ ← F.w_pref + F.state.w_add0 + w_subA

  ΔA₂ ← ΔA₁ ⊕ ROTL_R0(F.state.ΔB₁)
  ΔB₂ ← F.state.ΔB₁ ⊕ ROTL_R1(ΔA₂)
  ΔB_pre ← ΔB₂                       // explicit zero-cost defense step: B_pre = B_raw ⊕ RC₄

  Trans_B = DiffRank_B(ΔB_pre)
  w_injB  = rank(Trans_B)

  if w₂ + w_injB + LB_rem[F.r_rem-1] ≥ W*:
    RegisterDiffResidual(InjB, F.r_abs, F.r_rem, (ΔA₂,ΔB₂), w₂, SnapshotBeforeInjB(F,ΔA₂,ΔB₂,w₂))
    return

  F.state.ΔA₂ ← ΔA₂
  F.state.ΔB₂ ← ΔB₂
  F.state.w_subA ← w_subA
  F.state.Trans_B ← Trans_B
  F.κ ← InjB
```

### 3.7.6 `B → A` 注入与第二模加

```text
Procedure Diff-InjB(F):
  if not NextAffine(F.state.Trans_B, out ΔI_B):
    F.κ ← FirstConst; return

  w₃  ← F.w_pref + F.state.w_add0 + F.state.w_subA + rank(F.state.Trans_B)
  ΔA₃ ← F.state.ΔA₂ ⊕ ΔI_B
  T₁  ← ROTL₃₁(F.state.ΔB₂) ⊕ ROTL₁₇(F.state.ΔB₂)

  (ΔA₄^hint, w_add1^min) ← Q2ᵈ,vv,in(ΔA₃, T₁)
  if w₃ + w_add1^min + LB_rem[F.r_rem-1] ≥ W*:
    RegisterDiffResidual(SecondAdd, F.r_abs, F.r_rem, (ΔA₃,F.state.ΔB₂), w₃, SnapshotBeforeSecondAdd(F,ΔA₃,w₃))
    return

  Cap_add1 ← min(Cap_add, W* - w₃ - LB_rem[F.r_rem-1])
  F.state.ΔA₃    ← ΔA₃
  F.state.ΔI_B   ← ΔI_B
  F.state.w_injB ← rank(F.state.Trans_B)
  F.enum_second_add ← DiffVVIn-ShellEnum(ΔA₃, T₁, cap=Cap_add1, hint=ΔA₄^hint)
  F.κ ← SecondAdd
```

### 3.7.7 第二模加与第二常量模减

```text
Procedure Diff-SecondAdd(F):
  if not Next(F.enum_second_add, out ΔA₄, out w_add1):
    F.κ ← InjB; return

  w₄ ← F.w_pref + F.state.w_add0 + F.state.w_subA + F.state.w_injB + w_add1
  if w₄ + LB_rem[F.r_rem-1] ≥ W*:
    RegisterDiffResidual(SecondConst, F.r_abs, F.r_rem, (ΔA₄,F.state.ΔB₂), w₄, SnapshotBeforeSecondConst(F,ΔA₄,w₄))
    return

  Cap_subB ← min(Cap_const, W* - w₄ - LB_rem[F.r_rem-1])
  F.state.ΔA₄    ← ΔA₄
  F.state.w_add1 ← w_add1
  F.enum_second_const ← DiffVCIn-Enum(F.state.ΔB₂, RC₆, op=⊟, cap=Cap_subB)
  F.κ ← SecondConst
```

### 3.7.8 第二常量模减、第二桥接与 `A → B` 注入

```text
Procedure Diff-SecondConst(F):
  if not Next(F.enum_second_const, out ΔB₄, out w_subB):
    F.κ ← SecondAdd; return

  ΔB₅ ← ΔB₄ ⊕ ROTL_R0(F.state.ΔA₄)
  ΔA₅ ← F.state.ΔA₄ ⊕ ROTL_R1(ΔB₅)
  ΔA_pre ← ΔA₅                       // explicit zero-cost defense step: A_pre = A_raw ⊕ RC₉

  Trans_A = DiffRank_A(ΔA_pre)
  w_injA  = rank(Trans_A)
  w₅      ← F.w_pref + F.state.w_add0 + F.state.w_subA + F.state.w_injB + F.state.w_add1 + w_subB

  if w₅ + w_injA + LB_rem[F.r_rem-1] ≥ W*:
    RegisterDiffResidual(InjA, F.r_abs, F.r_rem, (ΔA₅,ΔB₅), w₅, SnapshotBeforeInjA(F,ΔA₅,ΔB₅,w₅))
    return

  F.state.ΔA₅    ← ΔA₅
  F.state.ΔB₅    ← ΔB₅
  F.state.w_subB ← w_subB
  F.state.Trans_A← Trans_A
  F.κ ← InjA
```

### 3.7.9 轮结束与递归到下一轮

```text
Procedure Diff-InjA(F):
  if not NextAffine(F.state.Trans_A, out ΔI_A):
    F.κ ← SecondConst; return

  ΔA_out ← F.state.ΔA₅
  ΔB_out ← F.state.ΔB₅ ⊕ ΔI_A

  w_round ← F.state.w_add0 + F.state.w_subA + F.state.w_injB
          + F.state.w_add1 + F.state.w_subB + rank(F.state.Trans_A)
  w_new   ← F.w_pref + w_round

  AppendCurrentRoundStep(
    input=(ΔA₀,ΔB₀),
    first_add=(T₀,ΔB₁,F.state.w_add0),
    first_sub=(ΔA₁,F.state.w_subA),
    injB=(ΔI_B,F.state.w_injB),
    second_add=(T₁,ΔA₄,F.state.w_add1),
    second_sub=(ΔB₄,F.state.w_subB),
    injA=(ΔI_A,rank(F.state.Trans_A)),
    output=(ΔA_out,ΔB_out))

  Push(CursorStack,
    MakeDiffFrame(
      κ    = Enter,
      r_abs= F.r_abs + 1,
      r_rem= F.r_rem - 1,
      σ̂    = (ΔA_out,ΔB_out),
      w_pref= w_new))
```

### 3.7.10 差分局部 exact shell 生成器

```text
Procedure DiffVVIn-ShellEnum(α, β, cap, hint):
  let t_min = Q2ᵈ,vv,in(α,β).weight
  for t = t_min to cap:
    if Cache_wpDDT has complete Shell_tᵈ(α,β):
      for γ in OrderedByHint(Cache_wpDDT[α,β,t], hint):
        yield (γ, t)
    else:
      Shell_tᵈ(α,β) ← ExactBitRecursionShell(α,β,t,hint)
      Cache_wpDDT.maybe_put(α,β,t,Shell_tᵈ(α,β))
      for γ in OrderedByHint(Shell_tᵈ(α,β), hint):
        yield (γ, t)

Procedure DiffVCIn-Enum(α, C, op, cap):
  // 当前工程默认主方向使用 fixed-input polarity
  // 但数学上也允许 fixed-output polarity 的 exact generator
  for each β such that Q1ᵈ,vc(α,C,β,op) ≤ cap, ordered by exact weight:
    yield (β, Q1ᵈ,vc(α,C,β,op))
```

### 3.7.11 这套伪代码与 2016 论文的对应关系

对应关系必须看清：

1. 2016 论文的 round recursion + bit recursion 是 strict 核心。
2. 本文伪代码把它重写成显式 stage machine，与当前工程阶段一一对应：
   `Enter → FirstAdd → FirstConst → InjB → SecondAdd → SecondConst → InjA`
3. `Weight-Sliced pDDT` 不是替代 2016 论文 strict 核心，而是把其中的 exact bit recursion 结果 materialize 成 shell cache。

---

## 3.8 为什么 Algorithm 1 严格成立

差分 Forward 主算法的严格性来源分成五层：

1. `Q1ᵈ,vv` 与 `Q1ᵈ,vc` 是精确局部权重定义。
2. 注入层 rank 模型给出精确可达集合和精确概率权重。
3. `Weight-Sliced pDDT` 只缓存完整 exact shell，不改变候选全集。
4. 剩余轮下界 `LB_rem` 是可采纳下界。
5. memo / 最近支配表只在“同一残问题键、较差前缀”时跳过。

因此：

- 不会漏掉真正更优的全局 trail。
- 不会把不可能局部转移当成可能。
- 不会把 cache miss 解释成“不存在”。
- 把剪枝节点保存为残问题库存，也不会改变当前单目标 BNB 的正确性。

---

## 4. Algorithm 2：线性分析严格数学的工程版算法（Backward）

## 4.1 单轮反向分解

线性分析按 Backward 方向进行。  
本轮从当前轮输出边界掩码

`(U_A_out, U_B_out)`

开始反向回溯，轮内阶段顺序固定为：

`Enter → InjA → SecondAdd → InjB → SecondConst → FirstSubconst → FirstAdd → Recurse`

其核心含义是：

1. 先处理 `A → B` 注入在输出边界上的相关输入掩码空间。
2. 再处理第二个 variable-variable 模加。
3. 再处理 `B → A` 注入。
4. 再处理两个 variable-constant 模减。
5. 最后处理第一个 variable-variable 模加。
6. 形成该轮的前驱边界掩码，递归到更早一轮。

这里所有确定性 XOR/ROT 传输都只做转置传播，权重为 `0`。  
真正进入 BNB 权重链的只有：

- 两个注入层
- 两个 variable-variable 模加
- 两个 variable-constant 模减

在当前 V6 core 的严格拆分里，还必须把两类以前容易被“顺手并进注入步骤”的对象明确写成独立零代价分析步骤：

1. 显式 `CROSS_XOR_ROT` 逆向桥接步
   - `U_B,5 = U_B,out ⊕ ROTR_R1(U_A,5)`
   - `u₂    = U_A,5 ⊕ ROTR_R0(U_B,5)`
   - `U_B,2 = α₂ ⊕ Λ_B ⊕ μ_B`
   - `U_A,1sub = α₂add ⊕ ROTR_R1(U_B,2)`
   - `u₁ = U_B,2 ⊕ ROTR_R0(U_A,1sub)`
2. 显式注入前预白化防御步
   - 第一注入核输入满足 `B_pre = B_raw ⊕ RC₄`
   - 第二注入核输入满足 `A_pre = A_raw ⊕ RC₉`
   - 在 absolute-correlation 掩码传输里，这两个步都是**零代价、确定性恒等传输**

因此，线性 Backward 的严格轮内对象不是“注入 + 周围桥接的一坨合成式”，而是：

`注入枚举 / CROSS_XOR_ROT 零代价桥 / pre-whitening 零代价桥 / exact Q1 局部节点`

这四类对象串接而成的 stage-aware 算子链。

---

## 4.2 线性侧两类 Q1 精确评估器

### 4.2.1 variable-variable Q1

对 `u` 为和线 mask、`v,w` 为两个输入 mask 的模加线性分析，定义

`Q1ˡ,vv(u, v, w) = w_lin_vv(u, v, w)`

它必须等于 Schulte-Geers / CCZ 精确对象给出的局部权重。  
在当前工程里，可写成

- `w = |z|`
- 其中 `z = Mᵀ(u ⊕ v ⊕ w)`

并由精确 CCZ 权重接口核验。

### 4.2.2 variable-constant Q1

对 `y = x ⊟ C` 或 `y = x ⊞ C` 的线性相关，定义

`Q1ˡ,vc(α, C, β, op) = ⌈-log₂ |corr(α --op,C→ β)|⌉`

它必须由精确 carry transfer / exact correlation 路线给出。  
因此，Q2 在这里同样只能提供候选、列极值、严格有序 witness 流、或严格局部 floor。

---

## 4.3 线性侧四类 Q2 参数选择优化器

### 4.3.1 variable-variable，固定输出掩码

定义

`Q2ˡ,vv,out(u) = {(v,w)}`

它返回固定和线 mask `u` 时的精确 `(v,w)` 候选壳，按 `|z|` shell 组织。  
在当前工程里，这对应：

- split-8 exact path
- weight-sliced cLAT exact shell index

但要强调：

- `weight-sliced cLAT` 只是 exact z-shell 的索引层
- 它不是新的数学对象
- miss 绝不表示“不相关”

这是线性 Backward 主算法最自然、也最直接消费的 var-var Q2。

### 4.3.2 variable-variable，固定输入掩码

定义

`Q2ˡ,vv,in(v, w) = argmin_u Q1ˡ,vv(u, v, w)`

它返回固定 `(v,w)` 时的列最优和线掩码 `u*`。  
在当前工程里，这对应 CCZ / Theorem 7 一侧的 column-optimal `u*`。

这正是线性侧对应于差分 `find_optimal_gamma` 的对偶对象。

### 4.3.3 variable-constant，固定输出掩码

定义

`Q2ˡ,vc,out(β; C, op) = argmin_α Q1ˡ,vc(α, C, β, op)`

它返回固定输出掩码 `β` 下的最优输入掩码 `α*`，或给出严格有序 witness 流。  
在当前工程里，这对应 fixed-`β` theorem / strict witness / hot-path 三层对象。

这是线性 Backward 主算法当前最自然消费的 var-const Q2。

### 4.3.4 variable-constant，固定输入掩码

定义

`Q2ˡ,vc,in(α; C, op) = argmin_β Q1ˡ,vc(α, C, β, op)`

它返回固定输入掩码 `α` 时的最优输出掩码 `β*`，或给出严格有序 witness 流。  
在当前工程里，这对应 fixed-`α` theorem / strict witness。

虽然当前 Backward 主引擎更常从 fixed-`β` 极性出发，但自然生长残问题和不同 profile 组合要求 fixed-`α` 极性也必须成为第一类数学对象。

---

## 4.4 注入层的二阶线性导数 rank 分析

对注入映射 `F` 和固定输出 mask `u`，定义标量二次函数

`g_u(x) = ⟨u, F(x)⟩`

由于 `F` 是向量二次映射，`g_u` 是二次 Boolean 函数。  
其 Walsh 绝对值由二次型对应的双线性矩阵 `S(u)` 控制。

严格结论是：

- 相关输入掩码集合形成一个仿射子空间
  `ℓ(u) ⊕ im(S(u))`
- 每个相关输入掩码都具有相同绝对相关值
  `2^(-rank(S(u))/2)`

因此线性注入层的严格局部权重为

`w_inj_lin(u) = ⌈rank(S(u)) / 2⌉`

所以线性注入层与差分注入层一样，也是不需要额外再拆成独立 Q2/Q1 的“自带精确候选空间 + 精确局部权重”的局部对象。

但必须同时冻结一个当前-core 级别的对象边界：

- `F_B` 与 `F_A` 的数学对象只表示**注入核本身**
- 注入前的 `xor RC₄` 与 `xor RC₉` 必须视为独立的、零代价 deterministic bridge
- 因此 `LinRank_B` / `LinRank_A` 接收的应是“已经过显式预白化桥”的输入变量，而不是把常量再偷偷吸回 rank 定义内部

---

## 4.5 `z-shell based Weight-Sliced cLAT` 的论文来源、数学对象与严格语义

### 4.5.1 2019 论文先给了什么数学对象

`Automatic Search for the Linear (hull) Characteristics of ARX Ciphers - Applied to SPECK, SPARX, Chaskey and CHAM-64`
先给出的不是 cache，而是精确权重分层：

`S = ⋃_{Cw=0}^{n-1} S_Cw`

其中：

- `S` 是模加线性 mask 三元组总空间
- `S_Cw` 是 correlation weight 恰为 `Cw` 的输入输出 mask 子空间

论文随后给出：

- `Algorithm 1 Const(SCw)`：构造 fixed weight 子空间
- `Algorithm 2`：构造 block-level `cLAT`
- `Algorithm 3`：前两轮用 `Const(SCw)`，中间轮用 `Spliting-Lookup-Recombination`

所以真正的第一数学对象不是 table，而是 `S_Cw`。

### 4.5.2 为什么它本质上是 `z`-shell

论文和 Schulte-Geers 路线都指向同一件事：

`z = Mᵀ(u ⊕ v ⊕ w)`

一旦相关非零，则精确线性权重就是

`w_lin(u,v,w) = |z|`

因此：

`S_Cw = { (u,v,w) | corr(u,v,w) ≠ 0 ∧ |Mᵀ(u⊕v⊕w)| = Cw }`

也就是说，`S_Cw` 其实就是 exact `z`-shell。

### 4.5.3 原论文 `cLAT` 是什么

论文的 `cLAT` 不是 full LAT，也不是简单 threshold truncation。

它是：

- 把 `n = m·t` 按 block 切开
- 固定一个输入子块 `v_k`
- 再带上 connection status `b ∈ {0,1}`
- 索引所有可行的 `(u_k,w_k)` 与对应局部 correlation weight

并额外给出：

- `cLATmin[v_k][b]`

用来做 recombination 阶段的严格最小局部权重下界。

### 4.5.4 我们的 `z-shell based Weight-Sliced cLAT` 是什么

在当前仓库里，主引擎默认的 var-var 模加 Q2 极性是：

- fixed output mask `u`
- 枚举输入 `(v,w)`

这与论文 Algorithm 3 中间轮“固定一个输入 mask `v` 去找 `(u,w)`”的极性不同。  
但两者共享同一个 exact weight 对象：

`z = Mᵀ(u ⊕ v ⊕ w)`

因此我们必须把工程里的 `Weight-Sliced cLAT` 解释成：

`Shell_tˡ(u) = { (v,w) | corr(u,v,w) ≠ 0 ∧ |Mᵀ(u⊕v⊕w)| = t }`

而不是解释成“某个经验 top-k 候选表”。

所以名字里的 `cLAT` 是论文 lineage，  
名字里的 `weight-sliced` 则强调它按 `|z|` exact shell 组织。

### 4.5.5 strict-safe 合同

线性 `Weight-Sliced cLAT` 的严格合同必须是：

1. 数学对象是 exact `z`-shell。
2. row-side fixed-`u` 与 paper 中 fixed-`v` 只是索引极性不同，不改变 exact weight 本体。
3. split-8 / cLAT / block connection status 只是 shell 索引器。
4. hit 只代表“索引命中”。
5. miss 只能触发 exact shell rebuild 或 exact split generator。
6. truncated helper list 不能在 strict mode 下充当真值来源。

### 4.5.6 `split-lookup-recombine` 在严格模式里的真实角色

论文里的 `Spliting-Lookup-Recombination` 必须被理解为：

- `Split`：把大对象拆成 block chain
- `Lookup`：在 block-local exact index 中枚举局部候选与局部 weight
- `Recombine`：借由 connection status 与 `cLATmin` 做 exact-or-admissible recombination

只要：

- 每个 block lookup 是完整的
- `cLATmin` 是严格下界
- recombination 不做 heuristic truncation

那么这套东西在 strict mode 下依然是严格的。

---

## 4.6 线性 Backward BNB 的全局接口闭环

### 4.6.1 全局剪枝

令当前 incumbent 为 `W*`，当前前缀累计权重为 `w_pref`，剩余轮可采纳下界为 `LB_rem(r)`。

则全局剪枝条件仍然是

`w_pref + LB_rem(r) ≥ W*  ⇒  可安全剪枝`

### 4.6.2 单轮预算

在当前反向边界进入本轮时，定义

`Cap_round = W* - w_pref - LB_rem(after_this_round)`

本轮所有局部算子的 exact local weight 或 exact local floor，都必须消耗自同一个 `Cap_round`。  
这保证：

- `InjA`
- `SecondAdd`
- `InjB`
- `SecondConst`
- `FirstSubconst`
- `FirstAdd`

都在同一条严格预算链里。

### 4.6.3 Q2 → Q1 闭环

线性侧每个非线性局部节点都必须满足：

1. Q2 只负责产生精确候选壳、列最优、严格 witness 流、或严格 floor。
2. Q1 对具体 `(u,v,w)` 或 `(α,β)` 计算 exact local weight。
3. 若一个 ordered stream 声称“后面不可能更轻”，则必须由严格单调性契约支持。

### 4.6.4 线性 Backward 的规范化边界状态

线性侧在任一剪枝边界，也必须能把当前边界掩码外化成新问题对象。  
记 stage 为 `κ`，则

`σ̂_lin(κ) = N_lin,κ(local_state)`

线性 Backward 的**合法 residual anchor 集合**在本文中冻结为：

`κ ∈ { Enter, SecondAdd, InjB, SecondConst, FirstSubconst, FirstAdd }`

注意：`Recurse` 不是独立语义 anchor。  
`Recurse` 只负责产生更早一轮的 child `Enter` 问题。

各 stage 的规范化状态明确规定为：

- `N_lin,Enter(local_state)        = (U_A,out, U_B,out)`
- `N_lin,SecondAdd(local_state)    = (u₂, β₂)`
- `N_lin,InjB(local_state)         = (u₂^resolved, α₂add, τ₂)`
- `N_lin,SecondConst(local_state)  = (α₂add, Λ_B)`
- `N_lin,FirstSubconst(local_state)= (α₂, Λ_B, μ_B)`
- `N_lin,FirstAdd(local_state)     = (α₁, u₁)`

这里要特别强调三条严格约束：

1. 线性侧的 `σ̂_lin(κ)` **不是统一的“两字状态”**；它是 stage-dependent 的规范对象。
2. 对于 `InjB` 与 `FirstSubconst` 这两个 anchor，`σ̂_lin(κ)` 本身是三字状态。
3. 因此若某个工程实现仍想用 `(pair_a, pair_b)` 作为线性 residual 语义键载体，就必须额外给出一个**单射的规范压缩映射**，证明不会把两个不同的 `σ̂_lin(κ)` 压成同一个键。

这条要求非常重要，因为线性侧最容易在“工程压缩键”和“真正数学键”之间偷偷失去严格性。

换句话说：

- `σ̂_lin(κ)` 是数学对象；
- `(pair_a, pair_b)` 最多只是工程编码；
- 只有在 injective proof 已给出的前提下，工程编码才等价于数学对象本身。

---

## 4.7 Algorithm 2 的 Unicode 严格数学伪代码

### 4.7.1 主过程

```text
Algorithm 2A  LinBackward-Best-BNB

Input:
  R₀ = (Dir=LinBackward, κ=Enter, r_abs=R, r_rem=R, σ̂=(U_A,out,U_B,out), Ω=Best)

Global State:
  W*                // 当前已知最优总线性权重
  Trail*
  LB_rem[0..R]
  Frontier
  Solved
  BestPrefixSeen
  CursorStack

Procedure MAIN():
  ActivateResidual(R₀, Snap = null)
  while true:
    while CursorStack ≠ ∅:
      let F = Top(CursorStack)
      switch F.κ:
        case Enter:        Lin-Enter(F)
        case InjA:         Lin-InjA(F)
        case SecondAdd:    Lin-SecondAdd(F)
        case InjB:         Lin-InjB(F)
        case SecondConst:  Lin-SecondConst(F)
        case FirstSubconst:Lin-FirstSubconst(F)
        case FirstAdd:     Lin-FirstAdd(F)
        case Recurse:      Lin-Recurse(F)
    MarkActiveResidualCompleted()
    if not RestoreNextResidualFromFrontier():
      break
  return (W*, Trail*)
```

### 4.7.2 残问题注册

```text
Procedure RegisterLinResidual(κ, r_abs, r_rem, local_state, w_pref, Snap?):
  σ̂ ← N_lin,κ(local_state)
  R  ← (LinBackward, κ, r_abs, r_rem, σ̂, Ω_current, SfxDef_current, SrcTag_current)
  if R ∈ Solved:
    return
  if BestPrefixSeen[R] ≤ w_pref:
    return
  BestPrefixSeen[R] ← w_pref
  PushOrImprove(Frontier, (R, w_pref, Snap))
```

### 4.7.3 进入一轮

```text
Procedure Lin-Enter(F):
  let (U_A,out, U_B,out) = F.σ̂
  let r_rem              = F.r_rem
  let w_pref             = F.w_pref

  if w_pref + LB_rem[r_rem] ≥ W*:
    RegisterLinResidual(Enter, F.r_abs, r_rem, (U_A,out,U_B,out), w_pref, Snapshot(F))
    Pop(CursorStack); return

  if r_rem = 0:
    if w_pref < W*:
      W*    ← w_pref
      Trail*← CurrentTrail()
    Pop(CursorStack); return

  let LB_next   = LB_rem[r_rem-1]
  let Cap_round = W* - w_pref - LB_next
  if Cap_round ≤ 0:
    RegisterLinResidual(Enter, F.r_abs, r_rem, (U_A,out,U_B,out), w_pref, Snapshot(F))
    Pop(CursorStack); return

  U_B,out^pre = U_B,out                     // explicit zero-cost defense step: A_pre = A_raw ⊕ RC₉
  Trans_A = LinRank_A(U_B,out^pre)         // correlated input masks for A→B injection
  w_injA  = ceil(rank(Trans_A)/2)
  if w_injA ≥ Cap_round:
    RegisterLinResidual(Enter, F.r_abs, r_rem, (U_A,out,U_B,out), w_pref, Snapshot(F))
    Pop(CursorStack); return

  F.state.Trans_A  ← Trans_A
  F.state.w_injA   ← w_injA
  F.state.Cap_round← Cap_round
  F.κ ← InjA
```

### 4.7.4 `A → B` 注入与第二模加输出掩码

```text
Procedure Lin-InjA(F):
  if not NextAffineMask(F.state.Trans_A, out μ_A):
    Pop(CursorStack); return

  // Undo the final A→B injection.
  U_A,5 = U_A,out ⊕ μ_A

  // Explicit zero-cost reverse CROSS_XOR_ROT bridge.
  U_B,5 = U_B,out ⊕ ROTR_R1(U_A,5)
  u₂    = U_A,5   ⊕ ROTR_R0(U_B,5)          // fixed output mask on the second add sum wire
  β₂    = U_B,5                               // fixed output mask for the second var-const node

  Cap_after_injA = F.state.Cap_round - F.state.w_injA
  if Cap_after_injA ≤ 0:
    RegisterLinResidual(SecondAdd, F.r_abs, F.r_rem, (u₂,β₂), F.w_pref + F.state.w_injA, SnapshotAfterInjA(F,u₂,β₂))
    return

  F.state.μ_A ← μ_A
  F.state.u₂  ← u₂
  F.state.β₂  ← β₂
  F.κ ← SecondAdd
```

### 4.7.5 第二模加：`fixed-u` 行壳层或 `fixed-(v,w)` 列极值

```text
Procedure Lin-SecondAdd(F):
  // 默认 strict 主极性：fixed output mask u₂, enumerate exact z-shell Shell_tˡ(u₂)
  // 可选极性：fixed (v,w), compute exact column-optimal u₂*

  if not NextVarVarQ2Candidate(
         polarity = configured_varvar_mode,
         fixed_u  = F.state.u₂,
         cap      = Cap_add₂,
         out      = (α₂add, τ₂, u₂^resolved, w_add₂)):
    F.κ ← InjA; return

  w_pref₂ = F.w_pref + F.state.w_injA + w_add₂
  if w_pref₂ + LB_rem[F.r_rem-1] ≥ W*:
    RegisterLinResidual(InjB, F.r_abs, F.r_rem, (u₂^resolved, α₂add, τ₂), w_pref₂, SnapshotBeforeInjB(F,u₂^resolved,α₂add,τ₂))
    return

  Λ_B = ROTR₃₁(τ₂) ⊕ ROTR₁₇(τ₂)             // transpose contribution of the second add term
  α₂add_pre = α₂add                           // explicit zero-cost defense step: B_pre = B_raw ⊕ RC₄
  Trans_B = LinRank_B(α₂add_pre)             // exact correlated-input affine space for B→A injection
  w_injB  = ceil(rank(Trans_B)/2)

  if w_pref₂ + w_injB ≥ F.state.Cap_round:
    RegisterLinResidual(SecondConst, F.r_abs, F.r_rem, (α₂add,Λ_B), w_pref₂ + w_injB, SnapshotBeforeSecondConst(F,α₂add,Λ_B))
    return

  F.state.α₂add   ← α₂add                    // input mask before second add on branch A
  F.state.τ₂      ← τ₂                       // add-term input mask from branch B
  F.state.u₂_res  ← u₂^resolved
  F.state.w_add₂  ← w_add₂
  F.state.Λ_B     ← Λ_B
  F.state.Trans_B ← Trans_B
  F.state.w_injB  ← w_injB
  F.κ ← InjB
```

### 4.7.6 `B → A` 注入与第二常量模减

```text
Procedure Lin-InjB(F):
  if not NextAffineMask(F.state.Trans_B, out μ_B):
    F.κ ← SecondAdd; return

  F.state.μ_B ← μ_B
  F.κ ← SecondConst

Procedure Lin-SecondConst(F):
  if not NextVarConstQ2Candidate(
         polarity = configured_varconst_mode,
         fixed_output_beta = F.state.β₂,
         cap = Cap_sub₂,
         out = (α₂, β₂^resolved, w_sub₂)):
    F.κ ← InjB; return

  w₃ = F.w_pref + F.state.w_injA + F.state.w_add₂ + F.state.w_injB + w_sub₂
  if w₃ ≥ F.state.Cap_round:
    RegisterLinResidual(FirstSubconst, F.r_abs, F.r_rem, (α₂,F.state.Λ_B,F.state.μ_B), w₃, SnapshotBeforeFirstSubconst(F,α₂,w₃))
    return

  // Explicit zero-cost reverse CROSS_XOR_ROT bridge of the first subround.
  U_B,2 = α₂ ⊕ F.state.Λ_B ⊕ F.state.μ_B
  U_A,1sub = F.state.α₂add ⊕ ROTR_R1(U_B,2)
  u₁       = U_B,2 ⊕ ROTR_R0(U_A,1sub)      // fixed output mask on the first add sum wire

  F.state.α₂      ← α₂
  F.state.β₂_res  ← β₂^resolved
  F.state.w_sub₂  ← w_sub₂
  F.state.U_B,2   ← U_B,2
  F.state.U_A,1sub← U_A,1sub
  F.state.u₁      ← u₁
  F.κ ← FirstSubconst
```

### 4.7.7 第一常量模减与第一模加

```text
Procedure Lin-FirstSubconst(F):
  if not NextVarConstQ2Candidate(
         polarity = configured_varconst_mode,
         fixed_output_beta = F.state.U_A,1sub,
         cap = Cap_sub₁,
         out = (α₁, β₁^resolved, w_sub₁)):
    F.κ ← SecondConst; return

  w₄ = F.w_pref + F.state.w_injA + F.state.w_add₂ + F.state.w_injB + F.state.w_sub₂ + w_sub₁
  if w₄ ≥ F.state.Cap_round:
    RegisterLinResidual(FirstAdd, F.r_abs, F.r_rem, (α₁,F.state.u₁), w₄, SnapshotBeforeFirstAdd(F,α₁,w₄))
    return

  F.state.α₁     ← α₁
  F.state.β₁_res ← β₁^resolved
  F.state.w_sub₁ ← w_sub₁
  F.κ ← FirstAdd

Procedure Lin-FirstAdd(F):
  if not NextVarVarQ2Candidate(
         polarity = configured_varvar_mode,
         fixed_u  = F.state.u₁,
         cap      = Cap_add₁,
         out      = (β₀, τ₀, u₁^resolved, w_add₁)):
    F.κ ← FirstSubconst; return

  Λ_A = ROTR₃₁(τ₀) ⊕ ROTR₁₇(τ₀)
  U_A,in = F.state.α₁ ⊕ Λ_A
  U_B,in = β₀

  w_round = F.state.w_injA + F.state.w_add₂ + F.state.w_injB + F.state.w_sub₂ + F.state.w_sub₁ + w_add₁
  w_new   = F.w_pref + w_round

  BuildAndBufferPredecessorStep(
    output_masks = F.σ̂,
    input_masks  = (U_A,in,U_B,in),
    injA         = (F.state.μ_A, F.state.w_injA),
    second_add   = (F.state.u₂_res, F.state.α₂add, F.state.τ₂, F.state.w_add₂),
    injB         = (F.state.μ_B, F.state.w_injB),
    second_const = (F.state.α₂, F.state.β₂_res, F.state.w_sub₂),
    first_const  = (F.state.α₁, F.state.β₁_res, F.state.w_sub₁),
    first_add    = (u₁^resolved, β₀, τ₀, w_add₁),
    round_weight = w_round)

  F.κ ← Recurse
```

### 4.7.8 递归到前一轮

```text
Procedure Lin-Recurse(F):
  let Step = NextBufferedPredecessor(F)
  if Step = none:
    Pop(CursorStack); return

  if F.w_pref + Step.round_weight + LB_rem[F.r_rem-1] ≥ W*:
    RegisterLinResidual(
      Enter, F.r_abs-1, F.r_rem-1,
      (Step.input_branch_a_mask, Step.input_branch_b_mask),
      F.w_pref + Step.round_weight,
      SnapshotChildEnter(F, Step))
    return

  Push(CursorStack,
    MakeLinFrame(
      κ    = Enter,
      r_abs= F.r_abs - 1,
      r_rem= F.r_rem - 1,
      σ̂    = (Step.input_branch_a_mask, Step.input_branch_b_mask),
      w_pref= F.w_pref + Step.round_weight))
```

### 4.7.9 线性 var-var / var-const Q2 生成器

```text
Procedure NextVarVarQ2Candidate(polarity, fixed_u, cap, out):
  if polarity = fixed_u_row_shell:
    for t = t_min to cap:
      if WeightSlicedClat has exact Shell_tˡ(fixed_u):
        yield next (v,w) in that shell
      else:
        Shell_tˡ(fixed_u) ← ExactSplit8OrExactShellBuilder(fixed_u, t)
        yield next (v,w) in that shell
      set local weight by exact Q1ˡ,vv
  else if polarity = fixed_(v,w)_column_optimal_u:
    take candidate source (v,w)
    compute u* = argmin_u Q1ˡ,vv(u,v,w)
    yield (v,w,u*,Q1ˡ,vv(u*,v,w))

Procedure NextVarConstQ2Candidate(polarity, fixed_output_beta, cap, out):
  if polarity = fixed_beta:
    use theorem floor / strict witness stream / exact Q1 verification
  else if polarity = fixed_alpha:
    use dual theorem floor / strict witness stream / exact Q1 verification
```

### 4.7.10 这套伪代码与 2019 论文的对应关系

对应关系如下：

1. 论文的 `Const(SCw)` 给了 exact weight shell 对象。
2. 论文的 `Algorithm 2 cLAT` 给了 block-level exact index 与 `cLATmin` floor。
3. 论文的 `Algorithm 3` 给了 first rounds 的 shell expansion 与 middle rounds 的 `Spliting-Lookup-Recombination`。
4. 本文把这些对象重写为当前仓库 backward search 需要的 stage machine：
   `Enter → InjA → SecondAdd → InjB → SecondConst → FirstSubconst → FirstAdd → Recurse`
5. 因而 `Weight-Sliced cLAT` 不是新的数学定义，而是对 exact `z`-shell 的工程索引。

---

## 4.8 为什么 Algorithm 2 严格成立

线性 Backward 主算法的严格性来源分成五层：

1. `Q1ˡ,vv` 与 `Q1ˡ,vc` 是精确局部权重定义。
2. 注入层 `rank(S(u))` 给出精确相关掩码空间和精确局部权重。
3. `Weight-Sliced cLAT` / split-8 只索引 exact `z`-shell，不改变候选全集。
4. `LB_rem` 是可采纳下界。
5. memo / 最近支配表只对同键、较差前缀成立。

因此：

- 不会漏掉真正更优的线性 trail。
- 不会把 truncated helper 或 cache miss 误判为“不存在更好候选”。
- ordered strict stream 的早停，只有在单调性契约成立时才允许。
- 把中间边界注册成残问题，不会改变单目标 BNB 本身的剪枝正确性。

---

## 5. 自然生长残问题前沿为什么在全局搜索中严格成立

## 5.1 单目标 BNB 的正确性不被改变

新增机制做的事情不是“修改原剪枝判据”，而是：

`在原来会停下来的地方，把当前后缀子问题显式保存下来。`

所以对固定目标 `Best` 而言：

- 原来的剪枝逻辑仍然保持原样。
- 原来的正确性来源仍然是 exact local weight + admissible lower bound。
- 新增残问题机制只是把一次性的剪枝边界外化为可复用库存。

因此，它不会破坏原本单目标 BNB 的正确性。

## 5.2 全局搜索从“原始输入 foreach”提升为“中间状态自然生长”

对单轮或多轮 ARX 算子串接系统，任意中间边界状态都能定义一个严格后缀问题。  
于是全局搜索空间可以从

`只从原始输入口出发`

提升为

`在层化中间状态空间里对残问题前沿做自然生长`

这不是启发式改写，而是对同一搜索树的显式重分解。

每个残问题 `R` 都对应原搜索树中的一个严格子树。  
因此整个全局空间就是这些子树的并集，经由规范化主键去重后得到一个多源搜索森林。

## 5.3 同一残问题的复用为何严格

若两个展开请求对应同一残问题键 `R`，则它们共享：

- 相同方向
- 相同锚点
- 相同规范化状态
- 相同目标标签
- 相同后缀问题定义

于是它们的可行后缀集合完全相同。  
不同的只可能是前缀累计权重。

所以：

- suffix 最优值可以直接复用
- suffix top-k 可以直接复用后做前缀拼接
- suffix distribution 可以直接复用后做权重平移

这就是全局复用严格成立的根本原因。

## 5.4 为什么必须保留 stage / anchor 身份

同样的二元边界值 `(x,y)`，如果位于不同 stage、不同轮号、不同后缀定义中，往后能走的物理算子并不相同。

因此以下信息绝不能省略：

- `Dir`
- `κ`
- `r_abs`
- `r_rem`
- `σ̂`
- `Ω`
- `SfxDef`

否则就会发生“数值看着一样，但其实是不同后缀问题”的错误合并。

## 5.5 为什么 `SrcTag` 也是必要的

一旦系统进入多目标、多 profile、多 canonicalization、多统计语义复用阶段，哪怕前面几项都相同，也仍可能出现“来自不同外层语义”的对象。

因此严格数学蓝图里必须允许加入 `SrcTag`。  
只有在已经证明两个来源标签可安全折叠时，`SrcTag` 才可以取空或取公共规范值。

当前工程已经把这层 outer provenance 明确化为独立命名空间，例如：

- `WrapperFixedSource`
- `BatchJob`
- `SubspaceJob`

这些值进入 source provenance、batch/subspace checkpoint 与审计链路，但它们仍然只是 `SrcTag` 的工程化实例，而不是新的 hull 数学对象。

---

## 6. 对主算法状态对象的稳定要求

主算法内部可以分析任意多轮，但一旦到了剪枝边界，必须支持把当前边界外化成新问题对象。

因此稳定后的主状态对象必须按层表达，而不是混成一个“巨大的运行时 frame dump”。

本文在这里把最终分层冻结为：

1. **数学语义对象**
   `R_sem = (Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`
2. **provenance 对象**
   `R_meta = (r_abs, SrcTag)`
3. **调度对象**
   `R_sched = (R_sem, R_meta, w_pref)`
4. **执行恢复对象**
   `Snap(R_sched)`

其中：

- `R_sem` 是必须长期稳定的数学对象。
- `R_meta` 是审计与外层命名空间对象。
- `R_sched` 是 frontier / dominance / solved table 实际消费的调度载体。
- `Snap(R_sched)` 可以后续演化，但只能作为恢复加速器，而不能反过来决定 `R_sem` 的数学含义。

为了让实现者没有自由裁量空间，本文再把四层对象的职责写死：

### 6.0.1 什么必须进入 `R_sem`

下面这些字段只要不同，就必须视为不同后缀问题：

- 方向 `Dir`
- 粗目标型别 `ObjKind`
- stage / anchor `κ`
- 剩余轮数 `r_rem`
- 规范化边界状态 `σ̂`
- 目标标签 `Ω`
- 后缀定义 `SfxDef`

### 6.0.2 什么只能进入 `R_meta`

下面这些字段默认只能视为 provenance：

- 绝对轮号 `r_abs`
- outer provenance / 来源标签 `SrcTag`

若将来某个 provenance 字段会改变“继续往后求解时面对的局部算子族”，那么它不再是 metadata，而必须被提升并吸收进 `SfxDef`。

### 6.0.3 什么只能进入 `Snap`

下面这些字段无论多么有利于恢复性能，都不能进入语义身份：

- shell cache
- helper cache
- ordered stream 当前游标
- DFS stack 内部已展开到哪一层
- LRU dominance 表
- 任何仅为恢复速度服务的临时候选缓冲

这些都只能进入 `Snap(R_sched)`。

---

## 6.1 当前工程中的承载位置

下面这张对照表只说明“当前代码把这些数学对象放在哪”，不表示“代码已经定义了数学正确性”。

### 6.1.1 当前对齐结论

截至当前仓库版本，可以把“蓝图与工程”的关系明确拆成两句：

1. 在**局部步骤显式拆分**这一点上，工程已经追上并部分超出旧文档粒度：
   - 注入前预白化 `xor RC₄ / xor RC₉`
   - `CROSS_XOR_ROT` 确定性桥接
   现在都已经在 BNB engine / collector 中显式 helper 化，不再被含混地并进注入对象输入语义。
2. 在**线性 residual 语义键严格性**这一点上，工程仍低于本文蓝图：
   - 本文要求 stage-dependent 的 `σ̂_lin(κ)`
   - 当前工程 residual semantic key 仍主要使用压缩后的 `{stage_cursor, pair_a, pair_b, suffix_profile_id, ...}`
   - 若没有额外 injective canonicalization 证明，就还不能宣称完全达到本文的最严版本。

### 差分侧

- variable-variable fixed-input Q2：
  `include/arx_analysis_operators/differential_probability/optimal_gamma.hpp`
  `src/auto_search_frame_bnb_detail/differential/varvar_weight_sliced_pddt_q2.cpp`
- variable-constant fixed-input Q2：
  `include/arx_analysis_operators/differential_probability/constant_optimal_input_alpha.hpp`
- variable-constant fixed-output Q2：
  `include/arx_analysis_operators/differential_probability/constant_optimal_output_beta.hpp`
- variable-variable Q1：
  `include/arx_analysis_operators/differential_probability/weight_evaluation.hpp`
- variable-constant Q1：
  `include/arx_analysis_operators/differential_probability/constant_weight_evaluation.hpp`
- 注入层 affine-derivative rank：
  `include/injection_analysis/differential_rank.hpp`
- Forward 主引擎：
  `src/auto_search_frame/differential_best_search_engine.cpp`

### 线性侧

- variable-variable fixed-output Q2：
  `src/auto_search_frame_bnb_detail/linear/varvar_z_shell_weight_sliced_clat_q2.cpp`
- variable-variable fixed-input Q2：
  `src/auto_search_frame_bnb_detail/polarity/linear/varvar/fixed_vw_q2.cpp`
- variable-constant fixed-output Q2：
  `src/auto_search_frame_bnb_detail/polarity/linear/varconst/fixed_beta_q2.cpp`
  与
  `include/auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp`
- variable-constant fixed-input Q2：
  `src/auto_search_frame_bnb_detail/polarity/linear/varconst/fixed_alpha_q2.cpp`
  与
  `include/auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_theorem.hpp`
- variable-variable Q1：
  `src/auto_search_frame_bnb_detail/polarity/linear/varvar/varvar_q1.cpp`
  与
  `include/arx_analysis_operators/linear_correlation/weight_evaluation_ccz.hpp`
- variable-constant Q1：
  `src/auto_search_frame_bnb_detail/polarity/linear/varconst/varconst_q1.cpp`
  与
  `include/arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp`
- 注入层 bilinear-rank：
  `include/injection_analysis/linear_rank.hpp`
- Backward 主引擎：
  `src/auto_search_frame/linear_best_search_engine.cpp`

### 残问题前沿的当前工程承载

- 共享残问题键与结果对象：
  `include/auto_search_frame/detail/residual_frontier_shared.hpp`
- 差分上下文中的残问题前沿容器：
  `include/auto_search_frame/detail/differential_best_search_types.hpp`
- 线性上下文中的残问题前沿容器：
  `include/auto_search_frame/detail/linear_best_search_types.hpp`
- 两个主引擎里的注册 / 去重 / 快照恢复逻辑：
  `src/auto_search_frame/differential_best_search_engine.cpp`
  与
  `src/auto_search_frame/linear_best_search_engine.cpp`

---

## 7. Checkpoint 的冻结条件与当前结论

checkpoint 的作用是“把搜索状态保存下来，以便长时间服务器运行后恢复”。  
但恢复的前提是：先知道到底在保存什么对象。

本文增强版的核心价值之一，就是把这件事从“原则性提醒”推进到“可冻结结论”。

### 7.1 先后顺序为什么不能颠倒

在数学对象未冻结之前，checkpoint 至少有三件事不能提前固化：

1. 残问题语义键到底包含哪些字段。
2. `σ̂` 的 stage 语义到底是什么。
3. `R_sem / R_meta / R_sched / Snap` 的边界到底画在哪里。

所以严格顺序必须是：

1. 先冻结主算法数学对象。
2. 再冻结 checkpoint 语义合同。
3. 最后才讨论 binary version 与兼容策略。

### 7.2 在本文增强版下，checkpoint 现在已经可以冻结什么

在本文给出的最终分层下，一个严格的 residual-frontier checkpoint 至少必须保存：

1. 当前 active residual 的 `R_sched`
2. 当前 active residual 的 `Snap(R_sched)`（若存在）
3. `Frontier` 中尚未求解的 `R_sched` 集合
4. `Solved` / `BestPrefixSeen` / 已完成 residual 结果表
5. 当前 incumbent、最佳 trail、以及与目标 `Ω` 对应的必要全局结果对象

也就是说，checkpoint 语义上保存的是：

`{ active R_sched, active Snap, pending frontier, solved tables, incumbent state }`

而不是“随便把当前运行时对象 dump 下来”。

### 7.3 哪些内容明确不应进入 checkpoint 语义层

下面这些内容即使被序列化，也只能被视为**可选恢复加速器**，不能改变 checkpoint 的语义身份：

- shell cache
- z-shell / pDDT helper materialization
- ordered stream helper truncation缓存
- 最近支配表
- 任何仅影响速度、不影响恢复后搜索树语义的临时缓存

换句话说：

- checkpoint 的严格语义核心是 `R_sched + frontier + solved state`
- 可选 cache 最多只是“恢复快一点”

### 7.4 审稿人应当检查什么

看到任何 checkpoint 设计时，审稿人都应当立即问四个问题：

1. 这个 payload 里是否能唯一恢复 `R_sched`？
2. 这个 payload 里是否把 `Snap` 错当成了数学身份？
3. 去掉所有 rebuildable cache 后，恢复是否仍然定义同一个后缀问题？
4. payload 里的 key 是否与本文冻结的 `K_sem` 一致？

只要有一个问题答不上来，该 checkpoint 设计就不应被称为“严格恢复语义”。

---

## 8. 本文的最终结论

本文把两个主算法的严格数学蓝图固定为：

1. 差分 Forward：  
   `Q2ᵈ,vv,in / Q2ᵈ,vv,out / Q2ᵈ,vc,in / Q2ᵈ,vc,out`
   与
   `Q1ᵈ,vv / Q1ᵈ,vc`
   组成精确局部算子闭环；注入层由 affine-derivative rank 直接给出精确可达集合与精确权重；BNB 用 exact local weight 与 admissible suffix lower bound 做全局剪枝。

2. 线性 Backward：  
   `Q2ˡ,vv,out / Q2ˡ,vv,in / Q2ˡ,vc,out / Q2ˡ,vc,in`
   与
   `Q1ˡ,vv / Q1ˡ,vc`
   组成精确局部算子闭环；注入层由 bilinear-rank `S(u)` 直接给出精确相关掩码空间与精确局部权重；BNB 用 exact local weight 与 admissible suffix lower bound 做全局剪枝。

3. 残问题自然生长机制不是 memo 小修小补，而是：
   `把被剪掉的中间节点提升为带锚点身份的严格 suffix 子问题库存。`

4. 残问题必须分成：
   - `R_sem`
   - `R_meta`
   - `R_sched`
   - `Snap(R_sched)`

5. 语义键 `K_sem` 的本体来自
   `Canon(Dir, ObjKind, κ, r_rem, σ̂, Ω, SfxDef)`；
   provenance 默认不进入支配/去重本体。

6. 差分 Forward 的合法 residual anchor 全部是严格二字状态；  
   线性 Backward 则存在三字规范状态，因此任何两字段工程压缩都必须附带 injective proof。

7. 这个机制不会推翻单目标 BNB 的剪枝正确性；它只是把一次性剪枝信息变成跨实例、跨阈值、跨统计目标可复用的搜索库存。

8. checkpoint 不能抢跑；但在本文增强版把数学对象冻结之后，checkpoint 的严格语义合同已经可以被明确设计与审查。

### 8.1 给读者与审稿人的最终一句话

若要判断一个实现是否真的“符合本文蓝图”，不要先看它跑得快不快，而先看它是否同时满足下面四句：

1. `Q2` 从不直接定义正确性。
2. residual 的语义身份与恢复快照被严格分层。
3. 每个合法 anchor 的 `σ̂` 已被逐 stage 写死。
4. 所有去重 / 支配 / checkpoint 设计都以 `K_sem` 为准，而不是以随手可取的工程字段为准。

只有四句都成立，这个实现才配称为“严格数学蓝图的工程承载”。
