Adversarial Initialization（攻击者视角的起始差分选择）

在差分分析的标准威胁模型中，攻击者通常被允许自由选择输入差分。因此本文在起始差分选择上采用对抗性初始化（adversarial initialization）：将输入差分视为攻击面的自由变量。本文作者采用“低成本、强对抗”的启发式策略，手工构造并优先测试低汉明重量（low Hamming weight）的候选差分，尤其偏向低位组合差分，以检验 NeoAlzette 轮函数在“潜在最脆弱输入附近”是否存在一轮即可显著降低差分权重（提升差分概率）的最优轨迹。

本文中一个 64-bit 输入块表示为 $(A,B)$ 两个 32-bit 分量，对应输入差分对 $(\Delta A,\Delta B)$。对任意差分轨迹的概率 $p$，定义差分权重

w := -\log_2(p),

Stage 0：快速扫描（低预算预筛选）

我们对候选起始差分集合 $S$ 中的若干 $(\Delta A,\Delta B)$ 进行低预算试跑：每个候选均在相同搜索管线、相同随机种子与相同预算设置下执行一次 1 轮最佳差分轨迹搜索，并以工具输出 best_weight 作为该起始差分在“给定模型与预算”下的最优已知攻击质量度量（权重越小代表已找到概率越高的轨迹）。

在基线扫描中，多数候选的 1 轮最优已知权重落在 $[W_{\min}, W_{\max}]$（占位符：由扫描结果填写）；其中我们观察到 best_weight = 54 的可复现结果（见日志与 checkpoint），作为后续加压深挖的参考锚点。

通过上述快速尝试，我们最终定位到起始差分

(\Delta A,\Delta B) = (0x00000000,\ 0x00000102).


---

Experimental Setup & Search Pipeline（白盒可复现）

本文公开产生结果所依赖的完整搜索管线与参数。我们使用自研工具 test_neoalzette_differential_best_search.exe 的 auto 前端，对 NeoAlzette 的 1 轮（--round-count 1）最佳差分轨迹进行自动化搜索。输入差分固定为 $\Delta A=0x00000000$、$\Delta B=0x00000102$。

搜索流程分两阶段：

Breadth（广度候选阶段）
在固定随机种子 0x12345678 下生成并评估候选入口，并选取 top 16 组成候选集合。该阶段的目标是以高吞吐率为深度搜索提供更紧的初始上界（upper bound）与更优入口。结果通过候选以及最佳排序，我们筛选到广度切换到深度阶段的差分是(0x00000000, 0x000020002)

Deep（深度搜索阶段：DFS + Branch-and-Bound）
从候选集合中选取当前最优入口后，采用深度优先搜索（DFS）结合分支定界（Branch-and-Bound, BnB）在搜索树上进行分支扩展与剪枝。需要强调：起始差分固定并不意味着轨迹唯一；DFS/BnB 实际搜索的是从同一 $(\Delta A,\Delta B)$ 出发的不同可行轨迹空间。
搜索过程中一旦发现更小的轨迹权重，即刷新全局上界 best_weight，并使用该上界对后续分支进行剪枝，从而压缩搜索规模。


日志字段与工作量度量

nodes_visited：已扩展的搜索节点数（工作量度量）。在本文实现中，“节点”对应搜索树中的一个部分状态（partial trail / partial assignment / partial path），代表一次被展开并用于生成子分支的状态；它不是“概率”，而是计算工作量与搜索空间覆盖的近似指标。

nodes_per_sec：吞吐率（节点扩展速度）。

elapsed_sec：运行时间（秒）。

best_weight：在当前预算下已找到的最小轨迹权重（经验上界）。


本文将 best_weight 视为“给定模型与预算约束下的最优已知上界”；是否达到数学意义的全局最优仍取决于更强预算或形式化下界证明。

运行稳定性设置

为保证可复现性与稳定吞吐，运行中采取：

固定进度打印周期为 60 秒（--progress-sec 60）。

启用 --memory-ballast 并保留 --memory-headroom-mib 2048，以降低系统内存波动对吞吐的影响。

时间预算上限为 1 天（--auto-max-time 1d）。

深搜节点数不设上限（--auto-deep-maxnodes 0）。

广度阶段 bitflip 扩展上限为 8（--auto-breadth-max-bitflips 8），以空间换时间扩大覆盖。



---

Results（当前最优已知上界：1 轮）

在上述参数约束下，工具在 1 轮搜索中得到当前最优已知轨迹权重上界为：

best_weight = 40（截至本次运行日志截取时）

elapsed_sec ≈ 37501（约 10.4 小时）


并给出对应日志与 checkpoint 以支持复跑与对照，包括 nodes_visited、elapsed_sec、nodes_per_sec 以及最优轨迹的轮级输入/输出差分记录（例如 best_r1_dA / best_r1_dB 与 best_out_dA / best_out_dB 等字段，按工具实际输出为准）。

> 注：上述 best_weight=40 为“已找到的最小权重”，并不自动意味着不存在权重更小（概率更高）的轨迹；它仅是该预算下的最优已知上界。


---

Budget-Extension Protocol（预算扩展复现实验协议）

在保持实现版本（commit/tag）、模型配置与搜索参数不变的前提下，增加计算预算（例如运行时长、可用内存、并行度）并记录 best_weight 的收敛过程（随 nodes_visited 或 elapsed_sec 的变化）。若在显著更高预算下 best_weight 仍无法降低，则可将本文结果视为在该模型下具有更强经验支撑的上界；若 best_weight 被进一步降低，则据此更新最优已知上界并报告新的对应轨迹与日志。


---

Response to “Cherry-picking” Concerns（最坏情况对抗性测试）

读者可能质疑：本文选择的起始差分是否存在刻意挑选（cherry-picking）。对此澄清如下：

在差分威胁模型中攻击者可自由选择输入差分，因此优先测试低汉明重量与低位组合差分属于最坏情况（worst-case）测试，其目的在于尽可能暴露潜在弱点，而非挑选“好看样本”。

本文差分筛选与后续深挖均在固定预算、固定随机种子与公开参数下执行，并公开日志与 checkpoint；读者可复跑获得一致结果，也可扩展预算继续向下搜索并对照验证本文报告的最优已知上界。



---

Adversarial Initialization（攻击者视角的起始差分选择）程序参数

```
E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x0000 --delta-b 0x0102 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x00000102

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=39.70  headroom_gibibytes=1.00  derived_budget_gibibytes=38.70
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=48693705  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=-1

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=31751168.82  elapsed_sec=0.01  best_weight=61
[Progress] nodes=38797312  nodes_per_sec=37826808.06  elapsed_sec=1.03  best_weight=54
[Progress] nodes=44826624  nodes_per_sec=5813376.46  elapsed_sec=2.06  best_weight=54
[Progress] nodes=50331648  nodes_per_sec=5430930.90  elapsed_sec=3.08  best_weight=54
[Progress] nodes=57409536  nodes_per_sec=7046239.81  elapsed_sec=4.08  best_weight=54
[Progress] nodes=64487424  nodes_per_sec=6928969.89  elapsed_sec=5.10  best_weight=54
[Progress] nodes=68943872  nodes_per_sec=4315341.93  elapsed_sec=6.14  best_weight=54
[Progress] nodes=76021760  nodes_per_sec=6896554.82  elapsed_sec=7.16  best_weight=54
[Progress] nodes=83099648  nodes_per_sec=6951199.61  elapsed_sec=8.18  best_weight=54
[Progress] nodes=90963968  nodes_per_sec=7821931.39  elapsed_sec=9.19  best_weight=54
[Progress] nodes=99090432  nodes_per_sec=8111553.34  elapsed_sec=10.19  best_weight=54
[Progress] nodes=107741184  nodes_per_sec=8532176.08  elapsed_sec=11.20  best_weight=54
[Progress] nodes=116391936  nodes_per_sec=8540236.22  elapsed_sec=12.22  best_weight=54
[Progress] nodes=120324096  nodes_per_sec=3145698.05  elapsed_sec=13.47  best_weight=54
[Progress] nodes=129761280  nodes_per_sec=9308293.92  elapsed_sec=14.48  best_weight=54
[Progress] nodes=137101312  nodes_per_sec=7131790.84  elapsed_sec=15.51  best_weight=54
[Progress] nodes=143917056  nodes_per_sec=6566621.48  elapsed_sec=16.55  best_weight=54
[Progress] nodes=151519232  nodes_per_sec=7575834.82  elapsed_sec=17.55  best_weight=54
[Progress] nodes=159121408  nodes_per_sec=7330793.68  elapsed_sec=18.59  best_weight=54
[Progress] nodes=166461440  nodes_per_sec=7273352.09  elapsed_sec=19.60  best_weight=54
[Progress] nodes=174063616  nodes_per_sec=7574467.08  elapsed_sec=20.60  best_weight=54
[Progress] nodes=182714368  nodes_per_sec=8581875.58  elapsed_sec=21.61  best_weight=54
[Progress] nodes=191889408  nodes_per_sec=9017268.46  elapsed_sec=22.62  best_weight=54
[OK] best_weight=54  (DP ~= 2^-54)
  approx_DP=5.551115123e-17
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=54  weight_first_addition=2  weight_first_constant_subtraction=0  weight_injection_from_branch_b=8  weight_second_addition=19  weight_second_constant_subtraction=9  weight_injection_from_branch_a=16
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x00000102
  output_branch_b_difference_after_first_addition=0x00000102  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02000001
  injection_from_branch_b_xor_difference=0x80458f4a  branch_a_difference_after_injection_from_branch_b=0x82458f4b
  branch_b_difference_after_linear_layer_one_backward=0x1c6cdcac  second_addition_term_difference=0xb76e568f
  output_branch_b_difference_after_second_constant_subtraction=0x3c25dcac  branch_b_difference_after_second_xor_with_rotated_branch_a=0x6636396b
  injection_from_branch_a_xor_difference=0x392e5752  output_branch_b_difference=0x5f186e39
  output_branch_a_difference=0x0af4164d  output_branch_b_difference=0x5f186e39
```

---

正式搜索

使用此命令 执行完Breadth 在Deep阶段找到甜点特征差分。(0x00000000, 0x00020002)
```
.\test_neoalzette_differential_best_search.exe auto --round-count 1 --delta-a 0x00000000 --delta-b 0x00000102 --auto-breadth-jobs 1024 --auto-breadth-top_candidates 16 --auto-breadth-threads 0 --auto-breadth-seed 0x12345678 --auto-breadth-max-bitflips 8 --auto-breadth-maxnodes 16777216 --auto-deep-maxnodes 0 --auto-max-time 1d --auto-target-best-weight 8 --progress-sec 60 --memory-ballast --memory-headroom-mib 2048
```
此轮搜索可以验证，我们最终只找到 Best weigh=40,  probability 2^{-40}

于是我们把这个作为下一轮的甜点特征差分，并高度怀疑其结构，并修改了参数，使得比特翻转可达更有可能的候选
```
D:\Downloads>.\test_neoalzette_differential_best_search.exe auto --round-count 1 --delta-a 0x00000000 --delta-b 0x00020002 --auto-breadth-jobs 1024 --auto-breadth-top_candidates 16 --auto-breadth-threads 16 --auto-breadth-seed 0x12345678 --auto-breadth-max-bitflips 16 --auto-breadth-maxnodes 33554432 --auto-deep-maxnodes 0 --auto-max-time 1d --auto-target-best-weight 8 --progress-sec 60 --memory-ballast --memory-headroom-mib 2048
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: auto
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
[Auto] round_count=1
[Auto] start_delta_a=0x00000000
[Auto] start_delta_b=0x00020002

[Auto] memory_ballast=on  headroom_gibibytes=2.00
[Auto][Breadth] jobs=1024  threads=16  seed=0x12345678
  per_candidate: maximum_search_nodes=33554432  maximum_constant_subtraction_candidates=128  heuristic_branch_cap=512  maximum_bit_flips=16  state_memoization=on
  system_memory: available_physical_gibibytes=7.95
  cache_entries_per_thread=187247 (auto)
  transition_shared_cache_total_entries=4493928 shards=2048 (auto) (auto)

[Auto][Breadth] progress 28/1024 (2.73%)  jobs_per_sec=0.47  elapsed_sec=60.09  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x00001000 d
[Auto][Breadth] progress 59/1024 (5.76%)  jobs_per_sec=0.52  elapsed_sec=120.16  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x00800000
[Auto][Breadth] progress 92/1024 (8.98%)  jobs_per_sec=0.55  elapsed_sec=180.27  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x80000000
[Auto][Breadth] progress 126/1024 (12.30%)  jobs_per_sec=0.57  elapsed_sec=240.38  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 163/1024 (15.92%)  jobs_per_sec=0.62  elapsed_sec=300.40  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 196/1024 (19.14%)  jobs_per_sec=0.55  elapsed_sec=360.51  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 227/1024 (22.17%)  jobs_per_sec=0.52  elapsed_sec=420.57  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x8000000
[Auto][Breadth] progress 261/1024 (25.49%)  jobs_per_sec=0.57  elapsed_sec=480.67  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x2040000
[Auto][Breadth] progress 295/1024 (28.81%)  jobs_per_sec=0.57  elapsed_sec=540.74  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 332/1024 (32.42%)  jobs_per_sec=0.62  elapsed_sec=600.83  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x00c0000
[Auto][Breadth] progress 364/1024 (35.55%)  jobs_per_sec=0.53  elapsed_sec=660.88  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000002
[Auto][Breadth] progress 399/1024 (38.96%)  jobs_per_sec=0.58  elapsed_sec=720.90  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000040
[Auto][Breadth] progress 436/1024 (42.58%)  jobs_per_sec=0.62  elapsed_sec=780.93  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 468/1024 (45.70%)  jobs_per_sec=0.53  elapsed_sec=841.12  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000000
[Auto][Breadth] progress 499/1024 (48.73%)  jobs_per_sec=0.52  elapsed_sec=901.24  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x2000000
[Auto][Breadth] progress 534/1024 (52.15%)  jobs_per_sec=0.58  elapsed_sec=961.30  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x0000020
[Auto][Breadth] progress 570/1024 (55.66%)  jobs_per_sec=0.60  elapsed_sec=1021.31  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000200
[Auto][Breadth] progress 603/1024 (58.89%)  jobs_per_sec=0.55  elapsed_sec=1081.34  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000000
[Auto][Breadth] progress 639/1024 (62.40%)  jobs_per_sec=0.60  elapsed_sec=1141.44  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x008008
[Auto][Breadth] progress 672/1024 (65.62%)  jobs_per_sec=0.55  elapsed_sec=1201.52  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000000
[Auto][Breadth] progress 705/1024 (68.85%)  jobs_per_sec=0.55  elapsed_sec=1261.53  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000000
[Auto][Breadth] progress 737/1024 (71.97%)  jobs_per_sec=0.53  elapsed_sec=1321.57  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x801004
[Auto][Breadth] progress 770/1024 (75.20%)  jobs_per_sec=0.55  elapsed_sec=1381.60  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000140
[Auto][Breadth] progress 804/1024 (78.52%)  jobs_per_sec=0.57  elapsed_sec=1441.66  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000002
[Auto][Breadth] progress 840/1024 (82.03%)  jobs_per_sec=0.60  elapsed_sec=1501.73  best_entry_w=50  best_w=50  best_start=delta_a=0x00000000 delta_b=0x00020002  active=16 {delta_a=0x000000
[Auto][Breadth] progress 876/1024 (85.55%)  jobs_per_sec=0.60  elapsed_sec=1561.82  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202  active=16 {delta_a=0x00000000
[Auto][Breadth] progress 911/1024 (88.96%)  jobs_per_sec=0.58  elapsed_sec=1621.89  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202  active=16 {delta_a=0x00008100
[Auto][Breadth] progress 946/1024 (92.38%)  jobs_per_sec=0.58  elapsed_sec=1681.89  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202  active=16 {delta_a=0x00480000
[Auto][Breadth] progress 984/1024 (96.09%)  jobs_per_sec=0.63  elapsed_sec=1742.08  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202  active=16 {delta_a=0x00000000
[Auto][Breadth] progress 1022/1024 (99.80%)  jobs_per_sec=0.63  elapsed_sec=1802.13  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202  active=2 {delta_a=0x00000800
[Auto][Breadth] progress 1024/1024 (100.00%)  jobs_per_sec=0.57  elapsed_sec=1805.63  best_entry_w=8  best_w=8  best_start=delta_a=0x00000000 delta_b=0x02020202

[Auto][Breadth] done. total_nodes_visited=34326184959
[Auto][Breadth] TOP-16:
  #1  entry_round1_weight=8  best_weight=8  start=delta_a=0x00000000 delta_b=0x02020202  entry=delta_a_round1=0x00000000 delta_b_round1=0x02020202  nodes=0
  #2  entry_round1_weight=54  best_weight=54  start=delta_a=0x00000000 delta_b=0x00020000  entry=delta_a_round1=0xb33cd31d delta_b_round1=0x8f6e1e56  nodes=33554433 [HIT maximum_search_node
  #3  entry_round1_weight=57  best_weight=57  start=delta_a=0x00000000 delta_b=0x00020022  entry=delta_a_round1=0xa22772c7 delta_b_round1=0x61b0c48e  nodes=33554433 [HIT maximum_search_node
  #4  entry_round1_weight=60  best_weight=60  start=delta_a=0x00000000 delta_b=0x00020006  entry=delta_a_round1=0x342c0c18 delta_b_round1=0x24dddbe1  nodes=33554433 [HIT maximum_search_node
  #5  entry_round1_weight=60  best_weight=60  start=delta_a=0x00000001 delta_b=0x00020003  entry=delta_a_round1=0xb348287e delta_b_round1=0x289587c7  nodes=33554433 [HIT maximum_search_node
  #6  entry_round1_weight=61  best_weight=61  start=delta_a=0x00000000 delta_b=0x0002000a  entry=delta_a_round1=0x5b999f48 delta_b_round1=0xc4027a0f  nodes=33554433 [HIT maximum_search_node
  #7  entry_round1_weight=62  best_weight=62  start=delta_a=0x00000002 delta_b=0x00020000  entry=delta_a_round1=0x802a2a80 delta_b_round1=0x7a397e36  nodes=33554433 [HIT maximum_search_node
  #8  entry_round1_weight=63  best_weight=63  start=delta_a=0x00000010 delta_b=0x00020002  entry=delta_a_round1=0xa2bc0fdb delta_b_round1=0xeda22ced  nodes=33554433 [HIT maximum_search_node
  #9  entry_round1_weight=65  best_weight=65  start=delta_a=0x00000000 delta_b=0x00020003  entry=delta_a_round1=0xa8b01ea2 delta_b_round1=0x8169e12a  nodes=33554433 [HIT maximum_search_node
  #10  entry_round1_weight=67  best_weight=67  start=delta_a=0x00000004 delta_b=0x00020006  entry=delta_a_round1=0x157ba200 delta_b_round1=0x84c78cf7  nodes=33554433 [HIT maximum_search_nod
  #11  entry_round1_weight=69  best_weight=69  start=delta_a=0x00000000 delta_b=0x00020012  entry=delta_a_round1=0x66d36fce delta_b_round1=0x71c2c046  nodes=33554433 [HIT maximum_search_nod
  #12  entry_round1_weight=69  best_weight=69  start=delta_a=0x00000020 delta_b=0x00020002  entry=delta_a_round1=0x13e6d91a delta_b_round1=0xed2be012  nodes=33554433 [HIT maximum_search_nod
  #13  entry_round1_weight=72  best_weight=72  start=delta_a=0x00000008 delta_b=0x00020002  entry=delta_a_round1=0xf3216ad4 delta_b_round1=0x659ae914  nodes=33554433 [HIT maximum_search_nod
  #14  entry_round1_weight=73  best_weight=73  start=delta_a=0x00000002 delta_b=0x00020002  entry=delta_a_round1=0x60ccdf85 delta_b_round1=0x019810d2  nodes=33554433 [HIT maximum_search_nod
  #15  entry_round1_weight=76  best_weight=76  start=delta_a=0x00000010 delta_b=0x00020012  entry=delta_a_round1=0x14aa1036 delta_b_round1=0xb6792ae0  nodes=33554433 [HIT maximum_search_nod
  #16  entry_round1_weight=77  best_weight=77  start=delta_a=0x00000008 delta_b=0x0002000a  entry=delta_a_round1=0x88a0dfe4 delta_b_round1=0xcc7b355a  nodes=33554433 [HIT maximum_search_nod

[Auto][Deep] inputs (from breadth top candidates): count=16
  #1 delta_a=0x00000000 delta_b=0x02020202  breadth_entry_w=8  breadth_best_w=8
  #2 delta_a=0x00000000 delta_b=0x00020000  breadth_entry_w=54  breadth_best_w=54
  #3 delta_a=0x00000000 delta_b=0x00020022  breadth_entry_w=57  breadth_best_w=57
  #4 delta_a=0x00000000 delta_b=0x00020006  breadth_entry_w=60  breadth_best_w=60
  #5 delta_a=0x00000001 delta_b=0x00020003  breadth_entry_w=60  breadth_best_w=60
  #6 delta_a=0x00000000 delta_b=0x0002000a  breadth_entry_w=61  breadth_best_w=61
  #7 delta_a=0x00000002 delta_b=0x00020000  breadth_entry_w=62  breadth_best_w=62
  #8 delta_a=0x00000010 delta_b=0x00020002  breadth_entry_w=63  breadth_best_w=63
  #9 delta_a=0x00000000 delta_b=0x00020003  breadth_entry_w=65  breadth_best_w=65
  #10 delta_a=0x00000004 delta_b=0x00020006  breadth_entry_w=67  breadth_best_w=67
  #11 delta_a=0x00000000 delta_b=0x00020012  breadth_entry_w=69  breadth_best_w=69
  #12 delta_a=0x00000020 delta_b=0x00020002  breadth_entry_w=69  breadth_best_w=69
  #13 delta_a=0x00000008 delta_b=0x00020002  breadth_entry_w=72  breadth_best_w=72
  #14 delta_a=0x00000002 delta_b=0x00020002  breadth_entry_w=73  breadth_best_w=73
  #15 delta_a=0x00000010 delta_b=0x00020012  breadth_entry_w=76  breadth_best_w=76
  #16 delta_a=0x00000008 delta_b=0x0002000a  breadth_entry_w=77  breadth_best_w=77
  target_best_weight=8  (prob >= 2^-8)
  deep_maximum_search_nodes=0 (unlimited)
  deep_maximum_search_seconds=86400

[Auto][Deep] #1/16  maximum_search_nodes=0(unlimited)  progress_every_seconds=60
  start=delta_a=0x00000000 delta_b=0x02020202  (seeded_upper_bound_weight=8)

[Auto][Deep] checkpoint_file=auto_checkpoint_R1_dA00000000_dB02020202.log (append on best-weight changes)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=0  maximum_s
  greedy_upper_bound_weight=8
  seeded_upper_bound_weight=8

[Progress] enabled: every 60 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=8  (DP ~= 2^-8)
  approx_DP=0.00390625
  nodes_visited=0  [HIT target_best_weight=8]

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_in
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

[Auto][Deep] #2/16  maximum_search_nodes=0(unlimited)  progress_every_seconds=60
  start=delta_a=0x00000000 delta_b=0x00020000  (seeded_upper_bound_weight=54)

[Auto][Deep] checkpoint_file=auto_checkpoint_R1_dA00000000_dB00020000.log (append on best-weight changes)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=0  maximum_s
  greedy_upper_bound_weight=54
  seeded_upper_bound_weight=54

[Progress] enabled: every 60 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=2095227.74  elapsed_sec=0.13  best_weight=54  start_dA=0x00000000 start_dB=0x00020000 best_r1_dA=0xb33cd31d best_r1_dB=0x8f6e1e56 best_out_dA=0xb33cd3
libc++abi: terminating due to uncaught exception of type std::bad_alloc: std::bad_alloc

D:\Downloads>dir *.log
 驱动器 D 中的卷是 Application Library Multimedia
 卷的序列号是 0FE5-5920

 D:\Downloads 的目录

2026-01-12  18:15               276 auto_checkpoint_R1_dA00000000_dB00020000.log
2026-01-12  03:10             1,725 auto_checkpoint_R1_dA00000000_dB00020002.log
2026-01-12  18:15               274 auto_checkpoint_R1_dA00000000_dB02020202.log
               3 个文件          2,275 字节
               0 个目录 138,070,265,856 可用字节
```

> 注1（关于 `nodes_visited=0`）：在 `[Auto][Deep] #1/16` 中程序打印 `nodes_visited=0  [HIT target_best_weight=8]`，表示 **greedy 初始化上界已经等于 `--auto-target-best-weight 8`**，因此深搜阶段被提前终止；这不构成“weight < 8 不存在”的证明。若要继续验证，需要把 `--auto-target-best-weight` 设得更小（或关闭该提前停止条件）。
>
> 注2（关于 `std::bad_alloc`）：`#2/16` 的崩溃通常来自 **breadth 阶段超大规模节点 + 多线程缓存（thread-local + shared cache）** 导致内存压力过大。若在可用内存较小环境复现实验，建议：
> - 降低 `--auto-breadth-threads`、减少 `--auto-breadth-maxnodes` / `--auto-breadth-jobs`
> - 或显式减小缓存：`--cache-max-entries-per-thread N`（alias `--cache`）
> - 或关闭共享缓存：`--shared-cache-total-entries 0`（alias `--cache-shared`）
> - 并适当增大 `--memory-headroom-mib`（避免 OS/分配器抖动与硬性 OOM）

结果我们的怀疑是正确的，我们找到了Best weigh=8,  probability 2^{-8}。
于是我们对相似结构进行排查。
```
E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.07  headroom_gibibytes=1.00  derived_budget_gibibytes=37.07
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46646784  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=8

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=8  (DP ~= 2^-8)
  approx_DP=0.00390625
  nodes_visited=6949

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000002 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000002
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.07  headroom_gibibytes=1.00  derived_budget_gibibytes=37.07
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46650974  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=-1

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=31755784.37  elapsed_sec=0.01  best_weight=86  depth=1/1
[Progress] nodes=165150720  nodes_per_sec=164880892.55  elapsed_sec=1.01  best_weight=86
[OK] best_weight=86  (DP ~= 2^-86)
  approx_DP=1.292469707e-26
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=86  weight_first_addition=6  weight_first_constant_subtraction=1  weight_injection_from_branch_b=21  weight_second_addition=20  weight_second_constant_subtraction=11  weight_injection_from_branch_a=27
  input_branch_a_difference=0x00000002  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020203  first_addition_term_difference=0x00040001
  output_branch_a_difference_after_first_constant_subtraction=0x00000006  branch_a_difference_after_first_xor_with_rotated_branch_b=0x03020204
  injection_from_branch_b_xor_difference=0x409fc305  branch_a_difference_after_injection_from_branch_b=0x439dc101
  branch_b_difference_after_linear_layer_one_backward=0xe26bc3cb  second_addition_term_difference=0x76a22532
  output_branch_b_difference_after_second_constant_subtraction=0x2218c1c7  branch_b_difference_after_second_xor_with_rotated_branch_a=0x33cadc23
  injection_from_branch_a_xor_difference=0x969acdfe  output_branch_b_difference=0xa55011dd
  output_branch_a_difference=0x1bde4f7a  output_branch_b_difference=0xa55011dd

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000202 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000202
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.08  headroom_gibibytes=1.00  derived_budget_gibibytes=37.08
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46655692  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=-1

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=31832521.77  elapsed_sec=0.01  best_weight=92
[Progress] nodes=87031808  nodes_per_sec=84811120.79  elapsed_sec=1.03  best_weight=80
[Progress] nodes=92012544  nodes_per_sec=4965902.35  elapsed_sec=2.03  best_weight=80
[Progress] nodes=96206848  nodes_per_sec=4051244.43  elapsed_sec=3.07  best_weight=80
[Progress] nodes=102498304  nodes_per_sec=6291318.85  elapsed_sec=4.07  best_weight=80  depth=1/1
[Progress] nodes=109051904  nodes_per_sec=6517715.41  elapsed_sec=5.08  best_weight=77
[Progress] nodes=112721920  nodes_per_sec=3599583.67  elapsed_sec=6.09  best_weight=77
[Progress] nodes=119537664  nodes_per_sec=6600726.01  elapsed_sec=7.13  best_weight=77
[Progress] nodes=126615552  nodes_per_sec=6890641.70  elapsed_sec=8.15  best_weight=77
[Progress] nodes=133693440  nodes_per_sec=7053232.72  elapsed_sec=9.16  best_weight=77
[Progress] nodes=140509184  nodes_per_sec=6706901.72  elapsed_sec=10.17  best_weight=77
[Progress] nodes=147062784  nodes_per_sec=6490686.43  elapsed_sec=11.18  best_weight=77
[Progress] nodes=152567808  nodes_per_sec=3524315.85  elapsed_sec=12.75  best_weight=77
[Progress] nodes=160169984  nodes_per_sec=7489632.29  elapsed_sec=13.76  best_weight=77
[Progress] nodes=168034304  nodes_per_sec=7646695.80  elapsed_sec=14.79  best_weight=77
[Progress] nodes=175898624  nodes_per_sec=7624168.60  elapsed_sec=15.82  best_weight=77
[Progress] nodes=183500800  nodes_per_sec=7447069.19  elapsed_sec=16.84  best_weight=77
[Progress] nodes=190840832  nodes_per_sec=7298676.24  elapsed_sec=17.85  best_weight=77
[Progress] nodes=198180864  nodes_per_sec=7137067.36  elapsed_sec=18.88  best_weight=77
[OK] best_weight=77  (DP ~= 2^-77)
  approx_DP=6.6174449e-24
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=77  weight_first_addition=8  weight_first_constant_subtraction=2  weight_injection_from_branch_b=23  weight_second_addition=21  weight_second_constant_subtraction=11  weight_injection_from_branch_a=12
  input_branch_a_difference=0x00000202  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020303  first_addition_term_difference=0x04040101
  output_branch_a_difference_after_first_constant_subtraction=0x00000206  branch_a_difference_after_first_xor_with_rotated_branch_b=0x03020005
  injection_from_branch_b_xor_difference=0xca58c645  branch_a_difference_after_injection_from_branch_b=0xc95ac640
  branch_b_difference_after_linear_layer_one_backward=0xdcad1d6d  second_addition_term_difference=0xd48d37ec
  output_branch_b_difference_after_second_constant_subtraction=0xfce51f75  branch_b_difference_after_second_xor_with_rotated_branch_a=0x9860d5ab
  injection_from_branch_a_xor_difference=0x49f612f1  output_branch_b_difference=0xd196c75a
  output_branch_a_difference=0xcf256c4b  output_branch_b_difference=0xd196c75a

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x03030303 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x03030303

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.05  headroom_gibibytes=1.00  derived_budget_gibibytes=37.05
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46623907  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=16

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=16861171.14  elapsed_sec=0.02  best_weight=16
[OK] best_weight=12  (DP ~= 2^-12)
  approx_DP=0.000244140625
  nodes_visited=455297

R1  round_weight=12  weight_first_addition=8  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x03030303
  output_branch_b_difference_after_first_addition=0x01010101  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x01010101
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x01010101
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x01010101
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x01010101
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x01010101

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x04040404 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x04040404

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.97  headroom_gibibytes=1.00  derived_budget_gibibytes=36.97
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46515158  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=8

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=8  (DP ~= 2^-8)
  approx_DP=0.00390625
  nodes_visited=6874

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x04040404
  output_branch_b_difference_after_first_addition=0x04040404  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x04040404
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x04040404
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x04040404
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x04040404
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x04040404

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x08080808 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x08080808

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.00  headroom_gibibytes=1.00  derived_budget_gibibytes=37.00
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46559280  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=8

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=8  (DP ~= 2^-8)
  approx_DP=0.00390625
  nodes_visited=6860

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x08080808
  output_branch_b_difference_after_first_addition=0x08080808  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x08080808
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x08080808
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x08080808
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x08080808
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x08080808

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x10101010 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x10101010

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.96  headroom_gibibytes=1.00  derived_budget_gibibytes=36.96
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46512096  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=8

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=8  (DP ~= 2^-8)
  approx_DP=0.00390625
  nodes_visited=6842

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x10101010
  output_branch_b_difference_after_first_addition=0x10101010  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x10101010
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x10101010
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x10101010
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x10101010
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x10101010

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x80808080 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x80808080

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.94  headroom_gibibytes=1.00  derived_budget_gibibytes=36.94
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46478112  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=6

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=6  (DP ~= 2^-6)
  approx_DP=0.015625
  nodes_visited=2119

R1  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>
```
确实存在路径！！！ 并且我们也找到了更优的差分Best weigh=6,  probability 2^{-6}

我们又找一轮相似的做对照。
```That
E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x90808080 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x90808080

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.22  headroom_gibibytes=1.00  derived_budget_gibibytes=37.22
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46832068  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=67

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=31577146.85  elapsed_sec=0.01  best_weight=67  depth=1/1
[Progress] nodes=137363456  nodes_per_sec=136577618.78  elapsed_sec=1.01  best_weight=63
[Progress] nodes=155713536  nodes_per_sec=18178431.97  elapsed_sec=2.02  best_weight=59
[Progress] nodes=166985728  nodes_per_sec=11265799.79  elapsed_sec=3.02  best_weight=59
[Progress] nodes=177995776  nodes_per_sec=10865038.85  elapsed_sec=4.04  best_weight=59
[Progress] nodes=189530112  nodes_per_sec=11332337.09  elapsed_sec=5.05  best_weight=59
[OK] best_weight=59  (DP ~= 2^-59)
  approx_DP=1.734723476e-18
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=59  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=17  weight_second_addition=18  weight_second_constant_subtraction=5  weight_injection_from_branch_a=15
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x90808080
  output_branch_b_difference_after_first_addition=0x90808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80908080
  injection_from_branch_b_xor_difference=0x54100450  branch_a_difference_after_injection_from_branch_b=0xd48084d0
  branch_b_difference_after_linear_layer_one_backward=0x40414445  second_addition_term_difference=0x28aa22a0
  output_branch_b_difference_after_second_constant_subtraction=0x4041444d  branch_b_difference_after_second_xor_with_rotated_branch_a=0x90ccaeea
  injection_from_branch_a_xor_difference=0x4208a923  output_branch_b_difference=0xd2c407c9
  output_branch_a_difference=0xee511e81  output_branch_b_difference=0xd2c407c9

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x80808080 --maximum-test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x0F0F0F0F --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x0f0f0f0f

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.03  headroom_gibibytes=1.00  derived_budget_gibibytes=37.03
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46592040  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=32

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=27492815.94  elapsed_sec=0.01  best_weight=32
[Progress] nodes=44040192  nodes_per_sec=43495099.33  elapsed_sec=1.02  best_weight=32
[Progress] nodes=97779712  nodes_per_sec=53705116.50  elapsed_sec=2.02  best_weight=28
[Progress] nodes=154140672  nodes_per_sec=56241733.15  elapsed_sec=3.02  best_weight=24
[OK] best_weight=24  (DP ~= 2^-24)
  approx_DP=5.960464478e-08
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=24  weight_first_addition=16  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=8  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x0f0f0f0f
  output_branch_b_difference_after_first_addition=0x03030303  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x03030303
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x03030303
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x03030303
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x03030303
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x03030303

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0xF0F0F0F0 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0xf0f0f0f0

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.99  headroom_gibibytes=1.00  derived_budget_gibibytes=36.99
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46538750  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=30

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=25302986.43  elapsed_sec=0.01  best_weight=30
[OK] best_weight=19  (DP ~= 2^-19)
  approx_DP=1.907348633e-06
  nodes_visited=47865913

R1  round_weight=19  weight_first_addition=15  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0xf0f0f0f0
  output_branch_b_difference_after_first_addition=0x10101010  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x10101010
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x10101010
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x10101010
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x10101010
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x10101010

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x40302010 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x40302010

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.11  headroom_gibibytes=1.00  derived_budget_gibibytes=37.11
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46693406  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=-1

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=32638262.90  elapsed_sec=0.01  best_weight=74
[Progress] nodes=12058624  nodes_per_sec=11571616.87  elapsed_sec=1.03  best_weight=64
[Progress] nodes=22020096  nodes_per_sec=9842647.62  elapsed_sec=2.04  best_weight=64
[Progress] nodes=31195136  nodes_per_sec=9016466.50  elapsed_sec=3.06  best_weight=64
[Progress] nodes=41943040  nodes_per_sec=10684753.90  elapsed_sec=4.06  best_weight=64
[Progress] nodes=52690944  nodes_per_sec=10703943.97  elapsed_sec=5.07  best_weight=64
[Progress] nodes=63438848  nodes_per_sec=7806726.73  elapsed_sec=6.44  best_weight=64
[Progress] nodes=74973184  nodes_per_sec=11261894.99  elapsed_sec=7.47  best_weight=64
[Progress] nodes=86769664  nodes_per_sec=11526713.41  elapsed_sec=8.49  best_weight=64
[Progress] nodes=99876864  nodes_per_sec=12967380.52  elapsed_sec=9.50  best_weight=64
[Progress] nodes=112459776  nodes_per_sec=12372473.55  elapsed_sec=10.52  best_weight=64
[Progress] nodes=125566976  nodes_per_sec=12985986.21  elapsed_sec=11.53  best_weight=64
[Progress] nodes=137625600  nodes_per_sec=11986553.65  elapsed_sec=12.53  best_weight=64
[Progress] nodes=146538496  nodes_per_sec=5212503.67  elapsed_sec=14.24  best_weight=64
[Progress] nodes=160169984  nodes_per_sec=13434676.70  elapsed_sec=15.26  best_weight=64
[Progress] nodes=173277184  nodes_per_sec=12890887.05  elapsed_sec=16.28  best_weight=64
[Progress] nodes=186908672  nodes_per_sec=13475631.54  elapsed_sec=17.29  best_weight=64
[OK] best_weight=64  (DP ~= 2^-64)
  approx_DP=5.421010862e-20
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=64  weight_first_addition=5  weight_first_constant_subtraction=0  weight_injection_from_branch_b=18  weight_second_addition=20  weight_second_constant_subtraction=11  weight_injection_from_branch_a=10
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x40302010
  output_branch_b_difference_after_first_addition=0x40302010  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x10403020
  injection_from_branch_b_xor_difference=0xf848e939  branch_a_difference_after_injection_from_branch_b=0xe808d919
  branch_b_difference_after_linear_layer_one_backward=0x070b171b  second_addition_term_difference=0xadb3859b
  output_branch_b_difference_after_second_constant_subtraction=0x0f191117  branch_b_difference_after_second_xor_with_rotated_branch_a=0xbfa499c8
  injection_from_branch_a_xor_difference=0xace4ed24  output_branch_b_difference=0x134074ec
  output_branch_a_difference=0x52d202c2  output_branch_b_difference=0x134074ec

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x10203040 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x10203040

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.25  headroom_gibibytes=1.00  derived_budget_gibibytes=37.25
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46876910  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=-1

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=30959573.89  elapsed_sec=0.01  best_weight=81
[Progress] nodes=121634816  nodes_per_sec=119891153.04  elapsed_sec=1.02  best_weight=70
[Progress] nodes=132382720  nodes_per_sec=10605238.10  elapsed_sec=2.03  best_weight=70
[Progress] nodes=143392768  nodes_per_sec=10949980.79  elapsed_sec=3.04  best_weight=70
[Progress] nodes=152829952  nodes_per_sec=9280396.34  elapsed_sec=4.06  best_weight=69
[Progress] nodes=165150720  nodes_per_sec=12177387.78  elapsed_sec=5.07  best_weight=69
[Progress] nodes=177995776  nodes_per_sec=12765780.50  elapsed_sec=6.07  best_weight=69
[Progress] nodes=188219392  nodes_per_sec=8861883.86  elapsed_sec=7.23  best_weight=69
[OK] best_weight=69  (DP ~= 2^-69)
  approx_DP=1.694065895e-21
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=69  weight_first_addition=5  weight_first_constant_subtraction=0  weight_injection_from_branch_b=22  weight_second_addition=20  weight_second_constant_subtraction=10  weight_injection_from_branch_a=12
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x10203040
  output_branch_b_difference_after_first_addition=0x10203040  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x40102030
  injection_from_branch_b_xor_difference=0xf879a948  branch_a_difference_after_injection_from_branch_b=0xb8698978
  branch_b_difference_after_linear_layer_one_backward=0x031f130f  second_addition_term_difference=0xa7918fb9
  output_branch_b_difference_after_second_constant_subtraction=0x01291113  branch_b_difference_after_second_xor_with_rotated_branch_a=0x30313a0a
  injection_from_branch_a_xor_difference=0xcbc82f8a  output_branch_b_difference=0xfbf91580
  output_branch_a_difference=0x52e2746c  output_branch_b_difference=0xfbf91580

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0xA0A0A0A0 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0xa0a0a0a0

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.02  headroom_gibibytes=1.00  derived_budget_gibibytes=37.02
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46586337  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=14

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=18810292.62  elapsed_sec=0.01  best_weight=14
[OK] best_weight=14  (DP ~= 2^-14)
  approx_DP=6.103515625e-05
  nodes_visited=281977

R1  round_weight=14  weight_first_addition=7  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=7  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0xa0a0a0a0
  output_branch_b_difference_after_first_addition=0xa0a0a0a0  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0xa0a0a0a0
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0xa0a0a0a0
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0xa0a0a0a0
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0xa0a0a0a0
  output_branch_a_difference=0x00000000  output_branch_b_difference=0xa0a0a0a0

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 1 --delta-a 0x00000000 --delta-b 0x0A0A0A0A --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=1
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x0a0a0a0a

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.94  headroom_gibibytes=1.00  derived_budget_gibibytes=36.94
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46483257  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=1  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=16

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=18644930.94  elapsed_sec=0.01  best_weight=16
[OK] best_weight=16  (DP ~= 2^-16)
  approx_DP=1.525878906e-05
  nodes_visited=1077502

R1  round_weight=16  weight_first_addition=8  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=8  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x0a0a0a0a
  output_branch_b_difference_after_first_addition=0x0a0a0a0a  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x0a0a0a0a
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x0a0a0a0a
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x0a0a0a0a
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x0a0a0a0a
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x0a0a0a0a

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>
```

发现是一类有效甜点差分带，但是条件极其苛刻。

这是真的要大修啊。
```
E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 2 --delta-a 0x00000000 --delta-b 0x80808080 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=2
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x80808080

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.88  headroom_gibibytes=1.00  derived_budget_gibibytes=36.88
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46409294  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=2  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=12

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=12  (DP ~= 2^-12)
  approx_DP=0.000244140625
  nodes_visited=27350

R1  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R2  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 3 --delta-a 0x00000000 --delta-b 0x80808080 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=3
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x80808080

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.96  headroom_gibibytes=1.00  derived_budget_gibibytes=36.96
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46506868  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=3  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=18

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=18  (DP ~= 2^-18)
  approx_DP=3.814697266e-06
  nodes_visited=113219

R1  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R2  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R3  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 4 --delta-a 0x00000000 --delta-b 0x80808080 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=4
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x80808080

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.96  headroom_gibibytes=1.00  derived_budget_gibibytes=36.96
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46506225  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=4  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=24

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=13424487.77  elapsed_sec=0.02  best_weight=24
[OK] best_weight=24  (DP ~= 2^-24)
  approx_DP=5.960464478e-08
  nodes_visited=3721940

R1  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R2  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R3  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080
R4  round_weight=6  weight_first_addition=3  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=3  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x80808080
  output_branch_b_difference_after_first_addition=0x80808080  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x80808080
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x80808080
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x80808080
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x80808080
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x80808080

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 2 --delta-a 0x00000000 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=2
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=38.00  headroom_gibibytes=1.00  derived_budget_gibibytes=37.00
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46556774  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=2  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=16

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=16  (DP ~= 2^-16)
  approx_DP=1.525878906e-05
  nodes_visited=183115

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R2  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 2 --delta-a 0x00000000 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=2
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.82  headroom_gibibytes=1.00  derived_budget_gibibytes=36.82
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46330675  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=2  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=16

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[OK] best_weight=16  (DP ~= 2^-16)
  approx_DP=1.525878906e-05
  nodes_visited=183115

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R2  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 3 --delta-a 0x00000000 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=3
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.81  headroom_gibibytes=1.00  derived_budget_gibibytes=36.81
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46320542  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=3  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=24

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=15485456.39  elapsed_sec=0.02  best_weight=24
[OK] best_weight=24  (DP ~= 2^-24)
  approx_DP=5.960464478e-08
  nodes_visited=5304652

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R2  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R3  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>test_neoalzette_differential_best_search.exe strategy time --round-count 4 --delta-a 0x00000000 --delta-b 0x02020202 --maximum-search-nodes 200000000
============================================================
  Best Trail Search (Injection differential probability via affine derivative)
  - cd_injection_from_* is propagated in the difference path (NOT removed)
  - Injection affine-differential model (XOR with NOT-AND/NOT-OR): InjectionAffineTransition{basis_vectors, offset, rank_weight}
  - Uses LM2001 xdp_add operator for modular-addition weights
  - Command-line interface frontend: strategy (preset=time, heuristics=off)
  - Mode: Matsui/Best-search (round-level branch-and-bound + bit recursion + injection branching)
============================================================
round_count=4
initial_branch_a_difference=0x00000000
initial_branch_b_difference=0x02020202

[Strategy] resolved settings:
  preset=time  heuristics=off
  resolved_worker_threads=1
  system_memory: total_physical_gibibytes=63.75  available_physical_gibibytes=37.82  headroom_gibibytes=1.00  derived_budget_gibibytes=36.82
  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  heuristic_branch_cap=0  maximum_search_node=200000000  enable_state_memoization=on
  cache_per_thread=46328496  cache_shared_total=0  cache_shared_shards=256 (off)

[BestSearch] mode=matsui(injection-affine)
  rounds=4  addition_weight_cap=31  constant_subtraction_weight_cap=32  maximum_constant_subtraction_candidates=0  maximum_transition_output_differences=0  maximum_search_nodes=200000000  maximum_search_seconds=0  memo=on
  greedy_upper_bound_weight=32

[Progress] enabled: every 1 seconds (time-check granularity ~262144 nodes)

[Progress] nodes=262144  nodes_per_sec=13591816.21  elapsed_sec=0.02  best_weight=32
[Progress] nodes=17563648  nodes_per_sec=15066975.16  elapsed_sec=1.17  best_weight=32  depth=1/4
[Progress] nodes=29360128  nodes_per_sec=9361489.99  elapsed_sec=2.43  best_weight=32
[Progress] nodes=47710208  nodes_per_sec=18308271.23  elapsed_sec=3.43  best_weight=32
[Progress] nodes=56623104  nodes_per_sec=5552917.69  elapsed_sec=5.04  best_weight=32
[Progress] nodes=76546048  nodes_per_sec=19809861.39  elapsed_sec=6.04  best_weight=32  depth=1/4
[Progress] nodes=94371840  nodes_per_sec=17762231.63  elapsed_sec=7.04  best_weight=32
[Progress] nodes=165150720  nodes_per_sec=70771852.36  elapsed_sec=8.04  best_weight=32
[OK] best_weight=32  (DP ~= 2^-32)
  approx_DP=2.328306437e-10
  nodes_visited=200000001  [HIT maximum_search_nodes]

R1  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R2  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R3  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202
R4  round_weight=8  weight_first_addition=4  weight_first_constant_subtraction=0  weight_injection_from_branch_b=0  weight_second_addition=4  weight_second_constant_subtraction=0  weight_injection_from_branch_a=0
  input_branch_a_difference=0x00000000  input_branch_b_difference=0x02020202
  output_branch_b_difference_after_first_addition=0x02020202  first_addition_term_difference=0x00000000
  output_branch_a_difference_after_first_constant_subtraction=0x00000000  branch_a_difference_after_first_xor_with_rotated_branch_b=0x02020202
  injection_from_branch_b_xor_difference=0x00000000  branch_a_difference_after_injection_from_branch_b=0x02020202
  branch_b_difference_after_linear_layer_one_backward=0x00000000  second_addition_term_difference=0x00000000
  output_branch_b_difference_after_second_constant_subtraction=0x00000000  branch_b_difference_after_second_xor_with_rotated_branch_a=0x02020202
  injection_from_branch_a_xor_difference=0x00000000  output_branch_b_difference=0x02020202
  output_branch_a_difference=0x00000000  output_branch_b_difference=0x02020202

E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch>
```

---

在NeoAlzette 的差分分析中，会出现像 `(0, 0x02020202)` 或 `(0, 0x80808080)` 这类每轮权重极低的轨迹，并不是因为注入函数（例如 `cd_injection_from_*`）本身被直接攻破，而是因为攻击者可以构造输入差分，使得差分传播到注入点时，输入的那一支差分恰好为 0。这样，注入项在差分层面就被完全绕过去了，此时整轮只剩下两次加法操作贡献差分概率权重。

NeoAlzette 的单轮（`NeoAlzetteCore::forward`）结构分为两个子轮。第一子轮是：先在 B 上加常数，然后进行 A 和 B 之间的交叉 XOR 与循环移位，接着执行从 B 到 A 的注入，最后对 B 做线性层的逆操作。第二子轮是：先在 A 上加常数，然后进行另一轮交叉 XOR 与循环移位，接着执行从 A 到 B 的注入，最后对 A 做线性层的逆操作。其中，两段 **XOR/ROT** 操作的形式非常关键，分别是：
`A ^= rotl(B,24);  B ^= rotl(A,16);`
以及
`B ^= rotl(A,24);  A ^= rotl(B,16);`

这里的一个关键观察是：旋转常数 24 和 16 相加等于 8（模 32），即 `24 + 16 ≡ 8 (mod 32)`。这个关系会导致一种特殊的“字节周期差分”在到达注入点时被抵消掉。假设进入第一段 XOR/ROT 之前的差分为 `(ΔA, ΔB)`，并且我们特意选择 **ΔA=0**。那么，第一步 `A ^= rotl(B,24)` 之后，A 的差分 ΔA1 就变成了 `rotl(ΔB,24)`。接着，第二步 `B ^= rotl(A,16)` 之后，B 的差分 ΔB1 就等于 `ΔB ⊕ rotl(ΔA1,16)`，也就是 `ΔB ⊕ rotl(rotl(ΔB,24),16)`，化简后得到 `ΔB ⊕ rotl(ΔB, 8)`。

因此，如果选择的差分满足 `ΔB = rotl(ΔB,8)`，那么 ΔB1 的结果就是 0。满足这个条件的差分，正好对应 32 比特字中的“字节周期”差分，即 `ΔB = x · 0x01010101`（其中 x 是 0 到 255 之间的值，意味着四个字节完全相同）。一旦 ΔB1 变为 0，随之而来的影响是：在 **B→A 的注入点**，输入差分是 0，所以注入函数的输出差分也必然是 0（概率为 1），权重贡献为 0；之后的 `B = l1_backward(B)` 线性逆操作对 0 差分也保持不变，输出仍是 0。第二段 XOR/ROT 和第二子轮的注入（A→B）也存在完全对称的情况：当第二子轮注入点之前的 A 侧差分能变为 0 时，A→B 的注入也同样被绕过。

这就解释了为什么在日志中看到的这类差分轨迹，其权重会呈现“每轮固定权重 × 轮数”的线性增长模式。对于像 `(ΔA,ΔB)=(0, x·0x01010101)` 这样的差分，只要两次加法操作都选择“输出差分仍然落在同一个字节周期子空间内”（特别是等于输入差分），那么整轮剩下的操作就会把差分带回到相同的形式（甚至完全变回原差分），从而得到一个**可迭代的差分轨迹**。具体例子就是：
- 对于 `(0, 0x02020202)`，每轮权重是 8（两次加法各贡献 4），并且输出差分仍然是 `(0,0x02020202)`。
- 对于 `(0, 0x80808080)`，每轮权重是 6（两次加法各贡献 3），并且输出差分仍然是 `(0,0x80808080)`。
因此，2轮、3轮、4轮的总权重就分别是 12/18/24 或 16/24/32，呈现线性累积。

需要强调的是，这里的“绕过注入”在差分层面是**严格成立**的，因为输入差分为 0 必然导致任何函数的输出差分为 0，这与注入函数具体的 affine-derivative 概率模型无关。真正贡献权重的，只是那两次加法差分的概率（在分析工具中使用的是 LM2001 的 `xdp_add` 模型来计算）。

那么，这是否意味着算法必须进行大幅修改呢？至少可以明确一点：**存在一类结构性的输入差分，能够使得两个注入点（即设计中非线性最强的部分）在差分意义上完全不参与**，从而导致出现“每轮仅付出两次加法权重”的高概率差分轨迹，并且该轨迹可以迭代扩展到多轮。这属于一个明显的设计警示。是否“必须大修”取决于目标的安全级别和总轮数，以及是否能够接受存在这种可迭代的高概率差分所带来的区分器或潜在攻击面。

一个直接的工程修复思路是：**打破 `ΔB ⊕ rotl(ΔB,8)` 这类能够被大子空间置零的结构**。例如，可以修改 XOR/ROT 操作中的旋转常数，使得两次旋转的位移量之和模 32 的结果与 32 互质，这样除了全零差分外，就不再存在大规模的不动点或周期子空间。