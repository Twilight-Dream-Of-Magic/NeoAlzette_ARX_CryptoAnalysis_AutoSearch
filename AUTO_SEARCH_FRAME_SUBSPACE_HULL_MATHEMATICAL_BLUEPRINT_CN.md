# NeoAlzette 自动搜索框架：SubSpace Hull 的极度严格数学蓝图（Unicode 版）

## 0. 总原则

本文不是重新定义差分 Forward BNB 与线性 Backward BNB。  
它建立在下列两个主算法蓝图之上：

- `AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md` 中的差分 Algorithm 1A
- `AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md` 中的线性 Algorithm 2A

因此本文只回答下面四件事：

1. `sub-space hull` 到底是什么数学对象。
2. `best-weight` 型与 `mass/statistics` 型 `sub-space hull` 如何严格区分。
3. `strict collector`、`endpoint hull`、`coverage`、`evidence`、`breadth→deep→collector` 之间各自是什么数学角色。
4. 当前工程实现与这些对象到底逐条对齐了什么、还没对齐什么。

核心原则不变：

`工程实现不定义正确性；工程实现只能承载已经独立成立的数学定义。`

---

## 0.1 阅读导图

建议按下面顺序阅读：

1. 若你是实现者：
   先看 `1`、`2`、`3`、`4`、`6`、`7`
2. 若你是审稿人：
   先看 `0.2`、`2`、`3`、`4`、`5`、`7`
3. 若你要核对当前代码：
   先看 `7.1` 与 `7.2`

## 0.2 审稿人速查：本文最终冻结的结论

本文最终冻结以下结论：

1. `sub-space hull` 至少有两类不同对象：
   - `best-weight` 型
   - `mass/statistics` 型
2. `strict collector` 之所以叫 collector，是因为它收集的是**cap 以内全部轨迹族**，不是只找最优单轨迹。
3. `endpoint` 不等于最佳权重；它是“最佳轨迹落到的终端边界状态”，也是 hull 聚合的自然终端对象。
4. 单个 fixed-source strict BNB / strict collector 的数学本体不要求先做宽度 BFS。
5. wrapper 层的 `breadth proposal / selection -> deep best-search -> strict collector` 是**source-space orchestration**，不是 fixed-source strict collector 的数学定义。
6. differential full-space theorem 与 linear full-space theorem 不同：
   - 差分侧是 total probability theorem
   - 线性侧是 total signed-correlation theorem + endpoint L2 theorem
7. 当前工程里，`source_hull` / `endpoint_hull` / `combined_source_hull` / `coverage` / `evidence` 对象大体已经存在；
   但 subspace strictness 最终仍继承自下层两个 BNB 对象边界是否完全冻结。

---

## 1. 统一记号

### 1.1 参数空间与固定参数最优

设完整参数空间为 `Θ`。  
其中每个参数 `θ ∈ Θ` 可以是：

- 差分侧的输入差分对
- 线性侧的输出掩码对
- 或更一般的 root boundary 条件

对每个固定参数 `θ`，定义可行轨迹集合：

`𝒯(θ)`

以及严格最优权重：

`W^*(θ) = min_{τ ∈ 𝒯(θ)} W(τ)`

这里：

- `τ` 是轨迹
- `W(τ)` 是轨迹总权重

### 1.2 全空间全局最优

定义：

`W_global = min_{θ ∈ Θ} W^*(θ)`

它表示：

> 在完整参数空间上做“外层参数枚举 + 内层 strict BNB”后，真正得到的全局最优最小权重。

### 1.3 参数子空间

对某个参数子空间 `Θ_s ⊆ Θ`，称其为一个 `sub-space`。

它可以来自：

- prescribed job file
- batch selection 后保留下来的 selected-source union
- prescribed subspace file 的 shard

### 1.4 两种 sub-space hull

#### 定义 1.4.1  Best-weight 型 sub-space hull

定义：

`H_min(Θ_s) := min_{θ ∈ Θ_s} W^*(θ)`

它回答的问题是：

> 在参数子空间 `Θ_s` 内，最强那一条轨迹的 best weight 是多少？

#### 定义 1.4.2  Mass / statistics 型 sub-space hull

它不看“最小权重”，而看一个子空间整体有多厚、多重、多危险。

典型例子：

- 差分：
  `H_mass^d(Θ_s) := Σ_{θ ∈ Θ_s} Σ_{τ ∈ 𝒯(θ)} 2^{-W(τ)}`
- 线性：
  `H_mass^l(Θ_s) := Σ_{θ ∈ Θ_s} ( Σ_{τ ∈ 𝒯(θ)} corr(τ) )²`

也可以是更工程化的统计量：

- `Σ_{θ ∈ Θ_s} 2^{-W^*(θ)}`
- top-`k` shell sum
- endpoint L2 mass
- source-union signed / absolute / squared mass

它回答的问题是：

> 这个参数子空间整体有多重？

### 1.5 Source 对象

定义一个 root source 对象 `s`：

- 差分侧：
  `s_d = (r, ΔA_in, ΔB_in)`
- 线性侧：
  `s_l = (r, U_A,out, U_B,out)`

其中 `r` 为轮数。

source 空间记为：

- 差分：`𝒮_d(r)`
- 线性：`𝒮_l(r)`

### 1.6 Collect cap

记 `W_cap` 为 strict hull collection 的 total-weight 上界。

它有两种来源：

1. `ExplicitCap(W_cap)`
2. `BestWeightPlusWindow(W* + ΔW)`

只有当 `W_cap` 真正被固定下来时，才允许谈：

- `exact within collect_weight_cap`
- `full-space exact within collect_weight_cap`

### 1.7 Source namespace

令 `N_src` 表示 outer source namespace，例如：

- `WrapperFixedSource`
- `BatchJob`
- `SubspaceJob`

它只负责 provenance / 去歧义，不改变 hull 本体。

---

## 2. 定义层

### 定义 2.1  子空间三段量

对 `Θ_s ⊆ Θ` 与任意 `θ₀ ∈ Θ_s`，定义：

1. 全空间全局最优
   `W_global`
2. 固定参数最优
   `W^*(θ₀)`
3. 子空间最优
   `W_sub(Θ_s) := H_min(Θ_s)`

### 定义 2.2  Differential fixed-source hull

对固定 source `s_d` 与固定 cap `W_cap`，定义：

`H_src^d(s_d ; W_cap)`

它包含所有从 `s_d` 出发、总权重 `≤ W_cap` 的差分轨迹。

### 定义 2.3  Differential endpoint hull

设 endpoint 为

`e_d = (ΔA_out, ΔB_out)`

定义：

`H_end^d(s_d -> e_d ; W_cap)`

并有分区关系：

`H_src^d(s_d ; W_cap) = ⨆_{e_d} H_end^d(s_d -> e_d ; W_cap)`

### 定义 2.4  Differential shell object

对固定 endpoint `e_d` 与 shell weight `w`，定义：

`H_end,w^d(s_d -> e_d ; W_cap)`

对应数值对象：

- `P_src^col(s_d ; W_cap)`
- `P_end^exact(s_d -> e_d ; W_cap)`
- `P_end,w^exact(s_d -> e_d ; W_cap)`

### 定义 2.5  Linear fixed-source hull

对固定 source `s_l` 与固定 cap `W_cap`，定义：

`H_src^l(s_l ; W_cap)`

它包含所有从固定输出 mask source `s_l` 回溯得到、总权重 `≤ W_cap` 的线性轨迹。

### 定义 2.6  Linear endpoint hull

设 endpoint 为

`e_l = (U_A,in, U_B,in)`

定义：

`H_end^l(s_l -> e_l ; W_cap)`

并有：

`H_src^l(s_l ; W_cap) = ⨆_{e_l} H_end^l(s_l -> e_l ; W_cap)`

### 定义 2.7  Linear shell object

对固定 endpoint `e_l` 与 shell weight `w`，定义：

`H_end,w^l(s_l -> e_l ; W_cap)`

对应严格数值对象：

1. signed hull sum
   `C_end^sgn(s_l -> e_l ; W_cap)`
2. absolute shell / trail mass
   `C_end^abs(s_l -> e_l ; W_cap)`

### 定义 2.8  Endpoint L2 mass

定义：

`L2_end(s_l -> e_l ; W_cap) = ( C_end^sgn(s_l -> e_l ; W_cap) )²`

以及：

`L2_src^col(s_l ; W_cap) = Σ_{e_l} L2_end(s_l -> e_l ; W_cap)`

### 定义 2.9  Combined source hull

对 source 子集 `S = {s₁, …, s_m}`，定义：

- 差分：
  `H_S^d(W_cap) = ⨆_{s ∈ S} H_src^d(s ; W_cap)`
- 线性：
  `H_S^l(W_cap) = ⨆_{s ∈ S} H_src^l(s ; W_cap)`

它只是一个 restricted source-union object。

### 定义 2.10  Differential full-source theorem object

对完整 source 空间 `𝒮_d`，定义：

`Θ_{𝒮_d}^d = |𝒮_d|`

以及 residual：

`R_{𝒮_d \setminus S}^d = Θ_{𝒮_d}^d - P_S^col(W_cap)`

### 定义 2.11  Linear full-source theorem object

对完整 source 空间 `𝒮_l`，定义：

1. total signed-correlation theorem
   `Θ_{𝒮_l}^{sgn}`
2. total endpoint-L2 theorem
   `Θ_{𝒮_l}^{L2} = |𝒮_l|`

以及 residual：

- `R_S^sgn = Θ_S^sgn - C_S^sgn,col`
- `R_S^{L2} = Θ_S^{L2} - L2_S^col`

### 定义 2.12  Prescribed subspace object

定义：

`SubspaceSpec = (N_src, S_prescribed, partition_count, partition_index)`

其中：

- `S_prescribed ⊆ 𝒮`
- `partition_count` 与 `partition_index` 只在 `S_prescribed` 上做确定性切片

当前 active shard 记为：

`S_active ⊆ S_prescribed`

### 定义 2.13  Exclusion evidence record

定义：

`E_excl = (source_count, min_best_weight, label)`

其严格含义是：

> 对某一批 source jobs，已经证明其中每个 source 的 best weight 都严格大于某个阈值。

### 定义 2.14  Generated evidence summary

对 `S_active`，定义：

`E_gen(S_active)`

它至少包含：

- `available`
- `certification ∈ {None, LowerBoundOnly, ExactBestCertified}`
- `min_best_weight`
- `exact_best_job_count`
- `lower_bound_only_job_count`
- `unresolved_job_count`
- `prepass_job_count`
- `prepass_total_nodes`

### 定义 2.15  Coverage summary

定义：

`Cov = (|S_prescribed|, |S_active|, evidence_record_count, evidence_source_count, full_space_source_count, full_space_exact_within_collect_weight_cap, … )`

它是关于 source-space 覆盖关系的元对象，不是 hull 本体。

---

## 3. 命题层

### 命题 3.1  Best-search 与 strict collector 不是同一对象

对固定 source `s`：

- best-search 输出 `W^*(s)`
- strict collector 输出 `H_src(s ; W_cap)`

因此 best-search 是 min-value / argmin engine，strict collector 是 bounded full-collection engine。

### 命题 3.2  strict collector 为什么叫 collector

strict collector 之所以叫 collector，是因为它做的是：

1. 收集轨迹
2. 按 endpoint / shell 分组
3. 聚合质量
4. 构造 hull 对象

而不是只做：

- 发现一条更优轨迹
- 更新 incumbent

### 命题 3.3  单 source strict BNB 本体不要求 BFS 预处理

单个 fixed-source strict best-search / strict collector 的数学本体是：

- 给定一个 fixed source
- 直接运行 strict residual-frontier BnB / collector

因此它本身不是

`BFS 预处理 + DFS 深搜`

的数学定义。

### 命题 3.4  wrapper 的 breadth→deep→collector 是工程编排

在 batch / subspace wrapper 场景里，经常出现：

1. breadth proposal / selection
2. deep best-search
3. strict collector

这里的 breadth 阶段只承担：

- source-space orchestration
- job filtering / proposal
- 资源分配与并行调度

因此它是工程编排层，而不是 fixed-source strict collector 的数学本体。

### 命题 3.5  Endpoint 不是最佳权重

对固定 source `s`：

- `W^*(s)` 是最优权重
- `τ^*(s)` 是最优轨迹
- `e^*(s) = Endpoint(τ^*(s))` 是该轨迹落到的终端边界状态

三者不是同一对象。

### 命题 3.6  Endpoint hull 是自然终端分区对象

`endpoint hull` 把“所有落到同一个终端边界状态的轨迹”归到同一个严格对象里：

- 差分：
  `H_end^d(s -> e ; W_cap)`
- 线性：
  `H_end^l(s -> e ; W_cap)`

因此 endpoint 是 hull 聚合最自然的终端接口层。

### 命题 3.7  Coverage / evidence 只证明未覆盖部分

- `coverage` 描述已覆盖 source 与宣告 full-source space 之间的关系
- `evidence` 证明某批 source 在阈值以内可以排除

因此二者都是 strictness proof object，不是 hull 本体。

---

## 4. 定理层

### 定理 4.1  Best-weight 型 sub-space hull 的夹逼不等式

对任意 `Θ_s ⊆ Θ` 与任意 `θ₀ ∈ Θ_s`，有：

`W_global ≤ H_min(Θ_s) ≤ W^*(θ₀)`

**证明**  
因为：

- `Θ_s ⊆ Θ`
- `θ₀ ∈ Θ_s`

故：

- 全空间上的最小值不大于子空间上的最小值
- 子空间上的最小值不大于该子空间里任意一点的值

证毕。

### 定理 4.2  Differential full-source theorem

对差分侧，每个 fixed source 的 total differential mass theorem 为 `1`。  
因此：

`Θ_{𝒮_d}^d = |𝒮_d|`

对任意 collected subset `S ⊆ 𝒮_d`：

`R_{𝒮_d \setminus S}^d = Θ_{𝒮_d}^d - P_S^col(W_cap)`

是完整 source 空间在 cap 内尚未被 collected mass 覆盖的 residual quantity。

### 定理 4.3  Linear full-source theorem

对线性侧，必须同时保留两类 theorem：

1. signed theorem：
   `Θ_{𝒮_l}^{sgn}`
2. endpoint-L2 theorem：
   `Θ_{𝒮_l}^{L2} = |𝒮_l|`

并定义：

- `R_S^sgn = Θ_S^sgn - C_S^sgn,col`
- `R_S^{L2} = Θ_S^{L2} - L2_S^col`

线性 full-space exactness 的严格判断不能只看 signed residual，还必须看 endpoint-L2 residual。

### 定理 4.4  Explicit-cap 下的 full-space exactness判据

若给定 explicit cap `W_cap`，并且：

1. active strict subset 已覆盖 source 数 `active_covered`
2. exclusion evidence 已排除 source 数 `evidence_excluded`
3. `active_covered + evidence_excluded = |𝒮|`
4. 每条 evidence 记录都证明 `min_best_weight > W_cap`

则可推出：

`full_space_exact_within_collect_weight_cap = true`

也就是说，当前 collected subspace hull 已经在完整 source 空间上严格 exact 到 `W_cap`。

### 定理 4.5  Subspace hull strictness 继承自下层两个 BNB 主对象

若：

- differential fixed-source strict hull 由 Algorithm 1A 的 strict collector 严格产生
- linear fixed-source strict hull 由 Algorithm 2A 的 strict collector 严格产生

则：

- `source hull`
- `endpoint hull`
- `combined source hull`
- `coverage/evidence` 的严格拼接

都可以向上成立。

反之，若下层两个主 BNB 对象的语义键或 strict collector 边界未完全冻结，则 subspace hull 的 strictness 也不能独立成立。

---

## 5. 推论层

### 推论 5.1  为什么 endpoint 比单条 best trail 更重要

best-search 最优权重只回答：

> 最强那一条有多强？

而 endpoint hull 回答：

> 所有通向同一个终点的那一整个族有多强？

因此当研究对象从单轨迹提升为 hull / subspace hull 时，endpoint 比单条 best trail 的 best weight 更重要。

### 推论 5.2  Collector 的“平均平方 / 聚合公式”不等于子空间最优值

若 collector 输出的是：

- probability mass
- signed correlation mass
- endpoint L2 mass
- shell/top-`k` 统计

则这些对象属于 `mass-objective`，而不是

`H_min(Θ_s)`

因此不能用某个“平均平方公式”直接替代子空间最优权重本身。

### 推论 5.3  CLI / wrapper 上层不需要大规模改数学接口

既然：

- fixed-source strict对象
- breadth/deep/collector 的工程编排
- coverage/evidence 的元对象

已经在数学上分层清楚，

则 CLI 上层通常不需要大规模改动数学接口；  
它主要需要做的是把“当前运行在哪个层次上”表达清楚，而不是重新定义 hull 对象本身。

---

## 6. Unicode 严格数学伪代码

### 6.1 Algorithm B0：Best-weight 型 sub-space hull

```text
Algorithm B0  Subspace-BestWeight-Hull

Input:
  parameter subspace Θ_s

Output:
  H_min(Θ_s) = min_{θ ∈ Θ_s} W^*(θ)

Procedure MAIN():
  W_sub ← +∞
  for each parameter θ in Θ_s, possibly in parallel:
    Wθ ← StrictBestSearch(θ)
    if Wθ < W_sub:
      W_sub ← Wθ
  return W_sub
```

### 6.2 Algorithm S1：Differential fixed-source strict hull

```text
Algorithm S1A  Differential-FixedSource-StrictHull

Input:
  s = (r, ΔA_in, ΔB_in)
  W_cap

Output:
  H_src^d(s ; W_cap)

Procedure MAIN():
  run strict residual-frontier collector on source s under total cap W_cap
  for each collected trail τ:
    let e = Endpoint_d(τ) = (ΔA_out, ΔB_out)
    let w = Weight(τ)
    insert τ into shell object H_end,w^d(s -> e ; W_cap)
    accumulate exact probability into:
      P_end,w^exact(s -> e ; W_cap)
      P_end^exact(s -> e ; W_cap)
      P_src^col(s ; W_cap)
  return H_src^d(s ; W_cap)
```

### 6.3 Algorithm S2：Linear fixed-source strict hull

```text
Algorithm S2A  Linear-FixedSource-StrictHull

Input:
  s = (r, U_A,out, U_B,out)
  W_cap

Output:
  H_src^l(s ; W_cap)

Procedure MAIN():
  run strict residual-frontier collector on source s under total cap W_cap
  for each collected trail τ:
    let e = Endpoint_l(τ) = (U_A,in, U_B,in)
    let w = Weight(τ)
    let c = ExactSignedCorrelation(τ)
    insert τ into shell object H_end,w^l(s -> e ; W_cap)
    accumulate into:
      C_end^sgn(s -> e ; W_cap) += c
      C_end^abs(s -> e ; W_cap) += |c|
      C_src^sgn,col(s ; W_cap)  += c
      C_src^abs,col(s ; W_cap)  += |c|
  compute endpoint L2 objects:
    L2_end = (C_end^sgn)^2
    L2_src^col = Σ_e L2_end
  return H_src^l(s ; W_cap)
```

### 6.4 Algorithm S3：Prescribed-subset subspace hull

```text
Algorithm S3A  PrescribedSubset-StrictSubspaceHull

Input:
  SubspaceSpec = (N_src, S_prescribed, partition_count, partition_index)
  W_cap

Output:
  Combined source hull on S_active
  Coverage summary Cov

Procedure MAIN():
  S_active ← DeterministicShard(S_prescribed, partition_count, partition_index)
  H ← ∅
  for each source s in S_active:
    H_s ← FixedSourceStrictHull(s, W_cap)
    H   ← MergeSourceHull(H, H_s)
  Cov.active_partition_source_count ← |S_active|
  Cov.prescribed_source_count       ← |S_prescribed|
  return (H, Cov)
```

### 6.5 Algorithm S4：Evidence-only subspace prepass

```text
Algorithm S4A  PrescribedSubset-EvidenceOnly

Input:
  SubspaceSpec = (N_src, S_prescribed, partition_count, partition_index)
  threshold object induced by W_cap

Output:
  E_gen(S_active)

Procedure MAIN():
  S_active ← DeterministicShard(S_prescribed, partition_count, partition_index)
  init E_gen.available = true
  init E_gen.min_best_weight = +∞
  for each source s in S_active:
    ans ← StrictBestOrLowerBound(s)
    update E_gen.min_best_weight
    if ans is exact best:
      E_gen.exact_best_job_count += 1
    else if ans is certified lower bound:
      E_gen.lower_bound_only_job_count += 1
    else:
      E_gen.unresolved_job_count += 1
  if E_gen.unresolved_job_count > 0:
    E_gen.available = false
  return E_gen
```

### 6.6 Algorithm S5：Coverage merge and full-space exactness

```text
Algorithm S5A  SubspaceCoverageMerge

Input:
  coverage reports {Cov_i}
  evidence files   {E_j}
  optional full-space source count |𝒮|
  explicit cap W_cap

Output:
  merged coverage summary Cov*

Procedure MAIN():
  merge active covered source counts from all unique Cov_i
  merge excluded source counts from all E_j with min_best_weight > W_cap
  if full-space source count is provided and
     active_covered + evidence_excluded = |𝒮| and
     every evidence record proves min_best_weight > W_cap:
       set Cov*.full_space_exact_within_collect_weight_cap = true
  else:
       set Cov*.full_space_exact_within_collect_weight_cap = false
  return Cov*
```

### 6.7 Algorithm O1：Wrapper orchestration（非 hull 本体）

```text
Algorithm O1  Breadth-Deep-Collector-Orchestration

Input:
  prescribed source set S
  runtime budgets

Output:
  engineering-selected source subset S_selected
  fixed-source best-search results
  strict collected hull objects

Procedure MAIN():
  S_selected ← BreadthProposalOrSelection(S)
  for each s in S_selected:
    W^*(s) ← DeepBestSearch(s)
  choose collect cap policy:
    either ExplicitCap(W_cap)
    or BestWeightPlusWindow(W^*(s)+ΔW)
  for each s in S_selected:
    H_src(s ; W_cap) ← StrictCollector(s, W_cap)
  merge all H_src into combined source hull
  return merged result
```

注意：`Algorithm O1` 是工程编排，不是 fixed-source strict hull 的数学定义。

---

## 7. 与当前代码逐条对齐 / 未对齐表

| 数学对象 / 断言 | 当前工程承载 | 状态 | 备注 |
| --- | --- | --- | --- |
| `W^*(s)` fixed-source best weight | `test_neoalzette_*_best_search.cpp` + `src/auto_search_frame/*_best_search_engine.cpp` | 已对齐 | best-search 对象明确存在 |
| Differential `H_src^d(s ; W_cap)` | `src/auto_subspace_hull/differential_best_search_collector.cpp` + differential callback aggregator | 已对齐 | fixed-source strict differential hull 已有独立 collector 承载 |
| Linear `H_src^l(s ; W_cap)` | `src/auto_subspace_hull/linear_best_search_collector.cpp` + linear callback aggregator | 已对齐 | fixed-source strict linear hull 已有独立 collector 承载 |
| Differential endpoint hull | `DifferentialEndpointHullSummary` | 已对齐 | endpoint 作为严格分区对象存在 |
| Linear endpoint hull | `LinearEndpointHullSummary` | 已对齐 | signed / abs / shell 三层结构存在 |
| Linear endpoint L2 mass | `compute_linear_endpoint_hull_l2_mass` / `compute_linear_source_hull_endpoint_l2_mass` | 已对齐 | theorem object 已进入代码 |
| Combined source hull | `DifferentialCombinedSourceHullSummary` / `LinearCombinedSourceHullSummary` | 已对齐 | source-union theorem / residual 对象存在 |
| Coverage summary | `SubspaceCoverageSummary` | 已对齐 | 作为元对象存在 |
| Exclusion evidence | `SubspaceExclusionEvidenceRecord` | 已对齐 | 作为排除证书存在 |
| Generated evidence summary | `GeneratedSubspaceEvidenceSummary` | 已对齐 | 作为 lower-bound / exact-best 证据对象存在 |
| Best-weight 型 `H_min(Θ_s)` | 间接存在于 per-source `W^*(s)` 集合与 wrapper selection/deep 结果中 | 部分对齐 | 有数据来源，但不是独立命名的一等对象 |
| Mass/statistics 型 `H_mass(Θ_s)` | callback aggregator + combined source hull summaries | 已对齐 | 工程上主要承载的是这一类对象 |
| `strict collector ≠ best-search` | best-search engine 与 collector 分文件分对象 | 已对齐 | 但文档解释以前不够，现在已补齐 |
| 单 source strict hull 不要求 BFS | fixed-source runtime 直接可跑 strict collector | 已对齐 | breadth 主要出现在 wrapper orchestration |
| wrapper breadth→deep→collector 是工程编排 | `test_neoalzette_*_hull_wrapper.cpp` | 已对齐 | orchestration 已存在且与 fixed-source collector 分离 |
| Differential full-space theorem | `compute_differential_callback_hull_source_union_total_probability_theorem` + `DifferentialBatchFullSourceSpaceSummary` | 已对齐 | 差分侧 theorem 对象存在 |
| Linear full-space theorem | `compute_linear_source_total_signed_correlation_theorem` + `LinearBatchFullSourceSpaceSummary` | 已对齐 | signed theorem 与 endpoint-L2 theorem 都存在 |
| subspace checkpoint 粒度 = job boundary | `*_subspace_hull_pipeline_checkpoint` + wrapper logic | 已对齐 | 当前明确是 completed-job granularity |
| in-flight strict-hull job 的 same-node BnB resume | fixed-source collector checkpoint 有，subspace pipeline 默认不承诺 | 部分对齐 | 单 source collector 可以，subspace pipeline 仍以 job boundary 为主 |
| subspace strictness 对下层 BNB 对象的继承 | 实际依赖下层 best-search/collector residual-frontier 语义键 | 部分对齐 | 依赖关系明确，但下层线性语义键压缩仍需 injective proof 才能完全冻结 |
| 线性主 BNB 某些三字 `σ̂_lin(κ)` 向上 strict 传递 | 当前 residual 语义键仍以 `(pair_a,pair_b,…)` 为主 | 未完全对齐 | 这是当前最明确的严格性缺口 |

---

## 8. 最终结论

本文把 `sub-space hull` 的极度严格数学语义固定为：

1. 若研究对象是“全局最优夹逼”，就使用
   `best-weight 型 sub-space hull`
   即
   `H_min(Θ_s) = min_{θ∈Θ_s} W^*(θ)`
2. 若研究对象是“总体质量 / 厚度 / cancellation / squared mass”，就使用
   `mass/statistics 型 sub-space hull`
3. `strict collector` 是 bounded full-collection engine，不是另一个 best-search。
4. `endpoint` 不是 best weight 本身，而是 hull 分解与聚合最自然的终端边界对象。
5. wrapper 层的 breadth→deep→collector 是工程编排，不是 fixed-source strict hull 的数学定义。
6. 当前代码已经把大多数 `subspace hull` 对象承载出来了，但 subspace strictness 最终仍继承自下层两个 BNB 主对象是否完全冻结。

### 8.1 给读者与审稿人的最终一句话

判断一个 `sub-space hull` 实现是否真的严格，不要先看它是不是输出了很多报表，而要先问：

1. 它的 `source hull` / `endpoint hull` 是否是独立 truth-bearing 对象？
2. 它有没有把 `best-weight` 型对象与 `mass` 型对象混为一谈？
3. 它的 `coverage/evidence` 是否只在证明未覆盖部分，而没有伪装成 hull 本体？
4. 它的 strictness 证明链是否明确回溯到了下层两个主 BNB 对象？

只有四个问题都答得上来，这个 `sub-space hull` 才配称为严格数学蓝图的工程承载。
