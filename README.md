NeoAlzette ARX 分析与自动搜索

### 项目简介
本仓库实现了对 NeoAlzette 式 ARX 结构的差分/线性性质评估与自动搜索：
- 差分侧采用 LM-2001/Lipmaa–Moriai 模型与一轮精确下界，结合 pDDT/Highway 表的后缀下界进行 Matsui 阈值搜索与启发式搜索。
- 线性侧采用 Wallén-2003 的加法线性近似分类与快速相关计算，同样结合后缀下界进行搜索。
- 提供 Highway（后缀下界）表的构建与查询，以及若干演示程序与 MILP 骨架导出。

核心思想来源于以下经典工作（仓库 `papers/` 附录）：
- Biryukov–Velichkov（ARX pDDT 与阈值搜索，Highways & Country Roads）
- Lipmaa–Moriai（加法差分性质与 Θ(log n) 级算法）
- Wallén（加法线性近似与 Θ(log n) 级相关计算）


### 构建
依赖：
- CMake ≥ 3.16
- C++20 编译器（如 gcc 12+/clang 15+）

构建示例：
```bash
mkdir -p build && cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

可选：编译全部演示/工具可执行文件（默认关闭）
```bash
cmake -D CMAKE_BUILD_TYPE=Release -D NA_BUILD_DEMOS=ON ..
cmake --build . -j
```

生成的二进制位于 `build/` 下，常见目标：
- 默认始终构建：`diff_search`、`lin_search`、`analyze_medcp`、`analyze_melcc`
- 当 `NA_BUILD_DEMOS=ON` 时，额外构建：`pddt_demo`、`threshold_demo`、`search_beam_diff`、`milp_diff`、`threshold_lin`、`gen_round_lb_table`、`highway_table_build`、`highway_table_build_lin`


### 使用与参数（与代码内 CLI 帮助保持一致）

- analyze_medcp（差分阈值搜索，包含导出与可重复起点设置）：
```
MEDCP Analyzer - Differential Trail Search for NeoAlzette (build only)
Usage:
  %s R Wcap [highway.bin] [--start-hex dA dB] [--export out.csv] [--k1 K] [--k2 K]

Arguments:
  R                 Number of rounds
  Wcap              Global weight cap for threshold search
  highway.bin       Optional differential Highway suffix-LB file

Options:
  --start-hex dA dB   Initial differences in hex (e.g., 0x1 0x0)
  --export out.csv     Append a one-line summary (algo, R, Wcap, start, K1, K2, best_w)
  --k1 K               Top-K candidates for var–var in one-round LB (default 4)
  --k2 K               Top-K candidates for var–const in one-round LB (default 4)

Notes:
  - Only builds; do not run heavy searches in this environment.
  - Highway provides O(1) suffix lower bounds with linear space.
```

- analyze_melcc（线性阈值/启发式搜索，支持线性 Highway 与导出）：
```
MELCC Analyzer - Linear Trail Search for NeoAlzette (build only)
Usage:
  %s R Wcap [--start-hex mA mB] [--export out.csv] [--lin-highway H.bin]

Arguments:
  R                    Number of rounds
  Wcap                 Global weight cap for threshold/beam search

Options:
  --start-hex mA mB      Initial masks in hex (e.g., 0x1 0x0)
  --export out.csv        Append a one-line summary (algo, R, Wcap, start, best_w)
  --lin-highway H.bin     Optional linear Highway suffix-LB file

Notes:
  - Exact (L^{-1})^T is used for mask transport; Wallén model for adds.
  - Only builds; do not run heavy searches in this environment.
```

- search_beam_diff（差分 Beam 搜索演示）：
```
Usage: %s R Wcap BEAM [highway.bin]
```

- threshold_lin（线性阈值搜索演示）：
```
Usage: %s R Wcap [highway_lin.bin]
```

- highway_table_build（差分 Highway 后缀下界表构建）：
```
Usage: %s R samples_per_bucket out.bin nbits(=32)
```

- highway_table_build_lin（线性 Highway 后缀下界表构建）：
```
Usage: %s R samples_per_bucket out.bin nbits(=32) [seed]
```

- milp_diff（差分 MILP 骨架导出）：
```
Usage: %s R n out.lp
```

- 其他演示：
  - `pddt_demo`、`threshold_demo`、`gen_round_lb_table` 为简要演示/工具。
  - `diff_search`、`lin_search` 为极简演示程序（无参数或仅少量默认）。


### 算法背景与实现要点
- 差分（MEDCP）：
  - 一轮内四次加法（两次变元-变元、两次变元-常数）使用 LM-2001 的精确/快速可行性枚举与最小权重计算（对 var-const 另有“加常数”模型）。
  - 线性层与跨分支注入使用精确线性传播；状态进行旋转规范化去重。
  - 阈值搜索（Matsui）以“当前累积权重 + 一轮下界 + 后缀下界”裁剪；后缀下界可选 pDDT/Highway（O(1) 查询）。

- 线性（MELCC）：
  - 使用 Wallén-2003 分类与 Θ(log n) 级的相关计算/枚举，对四次加法（含 var-const 情形）建立线性代价。
  - L 的转置-逆精确回传（backtranspose 精确实现）。
  - 同样结合一轮线性下界与线性 Highway 后缀下界进行搜索/剪枝。

- Highway 表：
  - 将剩余轮数、(|dA|,|dB|,奇偶) 或线性掩码的简要特征映射到保守后缀下界；
  - 通过随机采样与一轮精确下界折算，按“剩余轮数”分块存储为 `uint16_t`；查询 O(1)。


### 资源消耗与并行/集群建议
- 单机可完成：小轮数（例如 R≤8~10）与较松全局权重上限 `Wcap` 的搜索、以及 Highway 表的小规模采样构建。
- 计算热点：
  - LM/Wallen 可行解枚举（指数分支但强剪枝）
  - PriorityQueue/Beam/Threshold 的大量状态扩展与下界查询
  - Highway 表构建的采样外循环（`R × buckets × samples_per_bucket`）
- 并行化建议：
  - Highway 构建：以 `rem`（剩余轮数）或 bucket 分片并行，线性扩展良好。
  - 搜索：可对不同起点差分/掩码、不同随机种子或 Beam 槽位分区并行；注意去重/结果合并。
- 内存：
  - 搜索阶段以若干哈希表/优先队列为主，随剪枝与 `Wcap` 增减。一般数百 MB 量级可运行中小规模；
  - Highway 表为 `O(R × BLOCK)` 的 `uint16_t` 存储（BLOCK 随桶设计而定），通常为数十 MB 以内（取决于 n 与 R）。
- 是否需要集群：
  - 不是必须。若需对更大 R、更严格 `Wcap`、或进行广泛参数/起点扫描，建议使用多节点并行（作业队列或 MPI/多进程），以提升覆盖率与加速表构建。


### 实验建议
- 先构建差分/线性 Highway 表（中小 `samples_per_bucket` 验证流程，再按需增大）。
- 使用 `analyze_medcp` / `analyze_melcc` 做小 R 烟囱测试，校验导出 CSV、trace/hist/topN 等工作流；
- 逐步提高 R 与严格 `Wcap`，必要时启用 Highway 表以获得更紧的后缀下界与更快的裁剪。


### 目录
- `include/` 算法与模型头文件（LM/Wallen、下界、Highway、规范化、导出等）
- `src/` 可执行程序入口与实现
- `papers/` 参考论文 PDF


### 许可
本项目遵循仓库根目录的 `LICENSE` 文件。


### 引用（节选）
- Alex Biryukov, Vesselin Velichkov. Automatic Search for Differential Trails in ARX Ciphers.
- Helger Lipmaa, Shiho Moriai. Efficient Algorithms for Computing Differential Properties of Addition.
- Johan Wallén. Linear Approximations of Addition Modulo 2^n.

