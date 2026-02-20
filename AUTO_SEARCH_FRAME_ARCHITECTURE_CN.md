# NeoAlzette 自动搜索框架架构说明

本文档描述 residual-frontier v3 对齐之后，仓库里 `auto_search_frame` / `auto_subspace_hull` 的当前真实实现。

它取代旧的“固定 root 单次 DFS / collector 只能一次性跑完”的说明。当前实现已经是：

- best-search 与 strict-hull collector 都使用 residual-frontier，
- best-search 与 collector 都支持二进制 checkpoint / resume，
- 但 NeoAlzette 轮函数 wiring 与既有 Q2+Q1 数学流程保持不变。

## 1. 当前模块拆分

### 1.1 模块归属

| 区域 | 主要目录 | 责任 |
| --- | --- | --- |
| NeoAlzette 密码核心 | `include/neoalzette/`, `src/neoalzette/` | 轮函数真值实现 |
| 搜索框架核心 | `include/auto_search_frame/detail/`, `src/auto_search_frame/` | best-search 数学、可恢复 BnB 引擎、residual-frontier checkpoint/resume |
| strict hull 运行时与 wrapper | `include/auto_subspace_hull/`, `src/auto_subspace_hull/` | 可恢复 collector、batch/subspace orchestration、wrapper 级 checkpoint |
| 通用运行时支撑 | `common/` | binary I/O、runtime log、watchdog、内存/运行预算控制 |

### 1.2 关键源码位置

| 角色 | 差分 | 线性 |
| --- | --- | --- |
| Engine | `src/auto_search_frame/differential_best_search_engine.cpp` | `src/auto_search_frame/linear_best_search_engine.cpp` |
| Engine checkpoint | `src/auto_search_frame/differential_best_search_checkpoint.cpp` | `src/auto_search_frame/linear_best_search_checkpoint.cpp` |
| Collector | `src/auto_subspace_hull/differential_best_search_collector.cpp` | `src/auto_subspace_hull/linear_best_search_collector.cpp` |
| Collector checkpoint | `include/auto_subspace_hull/detail/differential_hull_collector_checkpoint.hpp` | `include/auto_subspace_hull/detail/linear_hull_collector_checkpoint.hpp` |
| Residual 共用类型 | `include/auto_search_frame/detail/residual_frontier_shared.hpp` | 同左 |

当前 `auto_search_frame_bnb_detail` 的源码树按三层语义分工：

- `src/auto_search_frame_bnb_detail/polarity/<domain>/*.cpp`
  只放 BnB 配置 / polarity profile。
- `src/auto_search_frame_bnb_detail/<domain>/*.cpp`
  只放 domain-root 的硬核分析加速器实现。
- `src/auto_search_frame_bnb_detail/polarity/<domain>/varconst/*.cpp`
  与
  `src/auto_search_frame_bnb_detail/polarity/<domain>/varvar/*.cpp`
  只放 search-frame polarity 语义到 operator / theorem / Q1 judge 的桥接层。

当前线性侧对应为：

- domain-root accelerator：
  `src/auto_search_frame_bnb_detail/linear/varvar_z_shell_weight_sliced_clat_q2.cpp`
- polarity profiles：
  `src/auto_search_frame_bnb_detail/polarity/linear/linear_bnb_profile_fixed_*.cpp`
- polarity bridges：
  `src/auto_search_frame_bnb_detail/polarity/linear/varconst/*.cpp`
  `src/auto_search_frame_bnb_detail/polarity/linear/varvar/*.cpp`

当前差分侧对应为：

- domain-root accelerator：
  `src/auto_search_frame_bnb_detail/differential/varvar_weight_sliced_pddt_q2.cpp`
- polarity root 目录保留给 profile/config 源；
  当前未额外新增差分 profile `.cpp`
- polarity bridges：
  `src/auto_search_frame_bnb_detail/polarity/differential/varconst/*.cpp`
  `src/auto_search_frame_bnb_detail/polarity/differential/varvar/*.cpp`

### 1.3 CMake 层面的真实情况

当前可执行 target 链接的 collector 已经来自 `src/auto_subspace_hull/*_best_search_collector.cpp`，不再是旧文档里写的 `src/auto_search_frame/*_best_search_collector.cpp`。

所以“collector 仍在 `auto_search_frame` 且只能 one-shot”这类说法已经过时。

## 2. Residual-Frontier 搜索模型

### 2.1 统一残问题

当前 best-search 与 collector 都不是“只围绕一个固定 root pair 的单次 DFS”，而是围绕 residual problem 运转。

每个 residual problem 具有：

- `domain = Differential | Linear`
- `objective = BestWeight | HullCollect`
- `rounds_remaining`
- `stage_cursor`
- `pair_a`, `pair_b`
- `suffix_profile_id`

实现里还会额外保存：

- `absolute_round_index`
- `source_tag`

这两个字段仍然会被序列化，也会继续用于 debug / provenance 输出，但它们不再属于 dedup / dominance / completed 的语义键。

### 2.2 实际生效的语义键

当前生效的 residual semantic key 为：

`{domain, objective, rounds_remaining, stage_cursor, pair_a, pair_b, suffix_profile_id}`

这个键用于：

- `pending_frontier` 去重，
- `completed_residual_set`，
- `best_prefix_by_residual_key`，
- repeated/dominated skip 计数。

### 2.3 合法 stage 边界

child residual 只允许在现有轮函数步骤边界生成，不引入 operator 内部临时状态。

差分 stage cursor：

- `FirstAdd`
- `FirstConst`
- `InjB`
- `SecondAdd`
- `SecondConst`
- `InjA`
- `RoundEnd`

线性 stage cursor：

- `InjA`
- `SecondAdd`
- `InjB`
- `SecondConst`
- `FirstSubconst`
- `FirstAdd`
- `RoundEnd`

当前实现还有两类**显式但不单独升格为新 stage_cursor**的 deterministic helper-level 子步骤：

- 显式 `CROSS_XOR_ROT` 桥接步
- 显式注入前预白化 `xor RC[4] / xor RC[9]` 防御步

其中：

- 差分侧把这两类步当成 XOR 差分下的零权重、确定性传输；
- 线性侧把它们当成 absolute-correlation mask transport 下的零权重、确定性传输；
- 它们现在已经在 engine / collector 代码里显式 helper 化，
- 但仍然附着在现有 `InjA / InjB / FirstConst / SecondConst` 相邻阶段内部，
- 不会新增新的 residual key 字段，也不会新增新的 checkpoint stage id。

### 2.4 单轮与多轮语义

- 若 `rounds_remaining == 1`：
  - 仍允许在本轮内部 stage 边界持续长出 child residual，
  - 但不允许 `RoundEnd -> next round`
- 若 `rounds_remaining > 1`：
  - 允许本轮内部 child residual，
  - 也允许 `RoundEnd -> next round` residual

### 2.5 全局反循环/去重状态

全局 frontier 状态分为：

- `pending_frontier`
- `pending_frontier_entries`
- `completed_source_input_pairs`
- `completed_output_as_next_input_pairs`
- `completed_residual_set`
- `best_prefix_by_residual_key`
- `global_residual_result_table`
- `residual_counters`

每个 active residual 还会维护一个局部、可重建的 dominance 表，键为：

`{stage_cursor, pair_a, pair_b}`

它只是优化件，不进入 checkpoint。

## 3. Best-Search 与 Collector 的运行语义

### 3.1 Best-search

best-search 不应再被理解为“固定 root 的可恢复 DFS”。

当前它是：

- 一个 residual-frontier BnB engine，
- 同时维护 active residual cursor 与 pending frontier，
- 任意被剪掉但处于合法 stage 边界的状态，都可以提升为 child residual。

### 3.2 Collector

strict collector 也已经 residual-frontier 化。

它不再是 one-shot only，而是会保存：

- active collection cursor，
- active residual record，
- pending frontier records / resumable entries，
- completed residual/result 状态，
- callback aggregation 状态，
- 自己的 binary checkpoint。

### 3.3 Q2+Q1 不变性

这次 residual-frontier 对齐不改数学步骤契约。

以下内容保持不变：

- 差分 Q2/Q1 流程，
- 线性 Q2+Q1 流程，
- 候选枚举顺序语义，
- strict/fast 的既有定义，
- NeoAlzette 轮函数步骤顺序与 wiring。

特别是：

- differential `w-pDDT` 仍然只是 exact shell accelerator，
- linear `z-shell` / `Weight-Sliced cLAT` 仍通过现有 Q2+Q1 接口接入，
- strict runtime 仍依赖 exact local scoring 与现有严格证书判据。

### 3.4 当前实现里已经显式化的零代价桥接步

当前代码已经不再把下面这些 deterministic 步骤偷偷吸收到注入 rank helper 的输入语义里，而是显式写成 helper-level 分析步骤：

- 差分：
  - 第一子轮 `FirstConst -> CROSS_XOR_ROT -> pre-whitening(B ^= RC[4]) -> InjB`
  - 第二子轮 `SecondConst -> CROSS_XOR_ROT -> pre-whitening(A ^= RC[9]) -> InjA`
- 线性：
  - `InjA` 之后显式逆向展开第二子轮的 `CROSS_XOR_ROT`
  - `SecondAdd` 之后显式执行 `B ^= RC[4]` 对应的零代价 mask/pre-whitening 传输
  - `SecondConst` 之后显式逆向展开第一子轮的 `CROSS_XOR_ROT`
  - round-entry 处显式执行 `A ^= RC[9]` 对应的零代价 mask/pre-whitening 传输

这意味着：

- 当前工程在“局部步骤显式拆分”这一点上，已经比旧版架构文档更细；
- 但这些 helper-level 子步骤仍服务于现有 stage machine，不代表 residual anchor 集合已经改变。

## 4. Checkpoint / Resume 契约

### 4.1 版本与 SearchKind

`include/auto_search_frame/search_checkpoint.hpp` 当前定义：

- `kVersion = 1`

与单次 residual-frontier 运行直接相关的 kind 为：

- `LinearResidualFrontierBest`
- `DifferentialResidualFrontierBest`
- `LinearResidualFrontierCollector`
- `DifferentialResidualFrontierCollector`

旧的 `kVersion = 0` payload 当前 reader 不再接受。

### 4.2 Engine checkpoint 保存什么

best-search checkpoint 会保存继续当前 BnB 节点所需的精确状态：

- 配置与 runtime controls，
- best trail / current trail，
- active cursor，
- active residual record，
- memoization，
- pending frontier records / entries，
- completed residual/result 状态，
- residual counters。

### 4.3 Collector checkpoint 保存什么

collector checkpoint 会保存：

- active collection cursor，
- current trail，
- active residual record，
- pending frontier records / entries，
- completed residual/result 状态，
- residual counters，
- collect cap / 运行预算，
- aggregation result，
- callback aggregator 状态，
- 若 collector 依赖先前 best-search，则还会保存对应的 best-search reference result。

### 4.4 Rebuildable-only 状态

checkpoint 对“当前正在跑的 BnB 节点”保存的是精确游标状态，但可选加速器仍保持 rebuildable-only。

这包括：

- 差分 modular-add shell cache，
- 线性 helper cache / helper materialization，
- local residual dominance table。

也就是说：

- checkpoint 保存继续当前节点所需的 exact cursor / stream position，
- resume 后再重建这些可选优化件。

### 4.5 Resume 顺序

resume 时采用以下顺序：

1. 读取 binary payload
2. 恢复 active cursor 与 active residual record
3. 重建 rebuildable-only accelerator state
4. 若 active residual 存在，则优先继续它
5. 否则恢复下一个未被支配的 pending frontier entry

### 4.6 命中运行预算时的快照

当 node/time budget 命中且配置了 checkpoint 输出路径时，运行时现在会强制写出最新 residual-frontier 快照。

这个快照要覆盖：

- current trail，
- active cursor，
- active residual record，
- pending frontier，
- completed residual 状态，
- counters 与结果表。

## 5. Pair Event 与 Runtime Log 语义

当前代码区分四类 pair event：

- `interrupted_source_input_pair`
- `completed_source_input_pair`
- `interrupted_output_as_next_input_pair`
- `completed_output_as_next_input_pair`

其落点语义为：

- `interrupted_source_input_pair`
  - stdout：有
  - runtime log：有
  - checkpoint：通过常规状态快照体现
- `completed_source_input_pair`
  - stdout：有
  - runtime log：有
  - checkpoint：通过常规状态快照体现
- `interrupted_output_as_next_input_pair`
  - stdout：有，且采用聚合进度输出
  - runtime log：没有独立事件
  - 单独 checkpoint 事件：没有
  - 但它造成的 frontier 变化仍会进入常规 checkpoint payload
- `completed_output_as_next_input_pair`
  - stdout：有
  - runtime log：有
  - checkpoint：通过常规状态快照体现

当前事件输出里也会保留 `absolute_round_index` 与 `source_tag` 作为 provenance/debug 元数据。

## 6. 差分与线性加速器说明

### 6.1 Differential

- `w-pDDT` 仍然只是 exact shell accelerator，不是另一套搜索定义。
- cache miss 必须回退到 exact shell 生成。
- checkpoint 不持久化 `shell_cache`，只持久化继续枚举所需的 cursor 状态。

### 6.2 Linear

- `Weight-Sliced cLAT` 仍然是当前 `z-shell` 驱动的工程实现。
- strict mode 必须在核心实现层保持 exact，不允许依赖 CLI 清 cap 来“假装严格”。
- fast helper truncation 仍只是 helper 行为，不得悄悄改写 strict 搜索空间语义。

## 7. Batch / Subspace / Wrapper 与内部 Residual 的关系

wrapper 级 batch/subspace 流程依然只从外部 job 文件读入 root jobs。

外部 job 格式保持 root-only：

- root source pairs 来自 batch/subspace 输入，
- internal residual child pairs 只留在 frontier / checkpoint / journal 内部，
- internal residual 不会上升成新的外部 job 行。

wrapper 级 checkpoint 骨架仍继续复用：

- completed job flags，
- selected-source summaries，
- combined callback aggregation，
- batch/subspace runtime logs，
- batch/subspace checkpoint merge 流程。

但每个 single-job deep runtime 内部，实际跑的已经是 residual-frontier deep stage。

## 8. 当前可执行层面的正确心智模型

今天阅读这个仓库时，最简洁且正确的总结是：

- `auto_search_frame`
  - 负责 best-search 数学、engine、single-run residual-frontier 状态、single-run best-search checkpoint/resume
- `auto_subspace_hull`
  - 负责 strict collector runtime、collector checkpoint、batch/subspace orchestration、wrapper 级 aggregation
- best-search 与 collector
  - 都可恢复，
  - 都是 residual-frontier 驱动，
  - 都保持当前 Q2/Q1 数学流程不变，
  - 都使用 `kVersion = 1` binary checkpoint payload，
  - 并且当前 engine / collector 已把 pre-whitening 与 `CROSS_XOR_ROT` 写成显式零代价桥接步骤

还要补一句当前与“最严数学蓝图”的关系：

- 当前工程在局部 operator 分解上已经追平并部分超过旧文档粒度；
- 但线性 residual semantic key 仍是压缩后的 `{stage_cursor, pair_a, pair_b, ...}` 工程键，
- 尚未在架构层给出对蓝图里 stage-dependent `σ̂_lin(κ)` 的 injective canonicalization 证明。

## 9. 已经过时、不要再引用的旧说法

下面这些说法不再适用于当前代码：

- “collector 只能 one-shot”
- “collector 还在 `src/auto_search_frame/*_best_search_collector.cpp`”
- “single-run checkpoint version 还是 `kVersion = 0`”
- “single-run best-search 只是固定 root DFS，没有内部 residual frontier”

这些都属于旧实现阶段，不再描述当前仓库。
