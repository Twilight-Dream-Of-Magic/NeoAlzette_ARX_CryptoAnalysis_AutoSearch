# NeoAlzette ARX 自动化分析与搜索框架（工程说明，简体）

> 警告：本项目包含计算密集型流程。请不要在不了解计算成本的情况下运行多轮搜索；在个人电脑上仅建议做单轮或少量轮次的快速测试。

[English README](README_EN.md)

---

## 项目概述

本项目实现了一个三层 ARX 密码分析框架，专门用于分析 NeoAlzette 密码——一个受 Alzette 启发的 64 位 ARX-box。框架提供：

- 优化的 ARX 算子：差分和线性分析的 Θ(log n) 算法
- 自动化搜索框架：带有 highways 优化的 Branch-and-Bound 搜索
- NeoAlzette 集成：完整的差分和线性特征搜索

本框架能做什么：

- 搜索最优差分特征（MEDCP - Maximum Expected Differential Characteristic Probability）
- 搜索最优线性特征（MELCC - Maximum Expected Linear Characteristic Correlation）
- 执行多轮自动化密码分析

本框架不能做什么：

- 不提供密码分析结果（无预计算的路径）
- 不保证找到最优特征（启发式搜索）
- 不破解密码（这是一个研究工具）

---

## 工程实现与思路（不讲论文推导，只谈怎么做）

本项目从工程上将问题拆成三层，统一命名空间为 `TwilightDream`：

1) 底层 ARX 分析算子（`include/arx_analysis_operators/`）
- 差分（XOR of addition）：`differential_xdp_add.hpp`（LM-2001 算法 2），`differential_optimal_gamma.hpp`（算法 4）
- 线性（correlation of addition）：
  - `linear_correlation_add_logn.hpp`：Wallén 风格的对数时间权重计算（Θ(log n)）
  - `linear_correlation_addconst.hpp`：2×2 进位转移矩阵链（精确 O(n)），并提供 32/64 位便捷封装 `linear_x_modulo_{plus,minus}_const{32,64}`
- 目标：给上层“一轮模型”一个可信、可复用的精确评分接口。

2) NeoAlzette 一轮 BlackBox 评分器（`include/neoalzette/` + `src/neoalzette/`）
- 差分：`neoalzette_differential_step.hpp`
- 线性：`neoalzette_linear_step.hpp`（声明）、`src/neoalzette/neoalzette_linear_step.cpp`（定义）
- 思路：
  - 将旋转/XOR/模加(减)/常量注入/线性层 L1/L2 形式化；
  - 以掩码/差分为数据流做转置/回推，遇到“模加”时调用底层算子累计权重；
  - 同步记录相位与轮常数使用集合，返回一轮入口掩码/差分与总权重（差分 −log₂DP，线性 −log₂|corr|）。

3) 搜索框架组件（`include/arx_search_framework/` + `src/arx_search_framework/`）
- 差分：
  - pDDT 构建（Algorithm 1）：`pddt/pddt_algorithm1.hpp`，`src/arx_search_framework/pddt_algorithm1_complete.cpp`
  - Matsui 阈值搜索（Algorithm 2）：`matsui/matsui_algorithm2.hpp`，`src/arx_search_framework/matsui_algorithm2_complete.cpp`
- 线性：
  - cLAT 构建与查找（Algorithm 2）：`clat/clat_builder.hpp`（可选 `clat_search.hpp`）
- 组装思路：将 BlackBox 作为“最小评分单元”。差分方向以 pDDT/Highways 为骨干，必要时用 Country-roads 连接并通过 Bn 门槛剪枝；线性方向以 cLAT+SLR 产生候选，再用 BlackBox 精确评分。

> 已移除的旧模块：`utility_tools.*`、`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`、`highway_table_build*.cpp`。

---

## 构建与安装

- 依赖：C++20 编译器（Clang/GCC/MSVC）、CMake ≥ 3.14
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"
```
- 产物：`neoalz_diff_search`（差分演示）、`neoalz_lin_search`（线性演示）

> 提醒：演示程序可能触发密集计算，请先用小轮数试跑，避免长时间运行。

---

## 命令行（已规范化）

差分 `neoalz_diff_search`：
- 选项：
  - `--rounds, -r <int>`（默认 4）
  - `--weight-cap, -w <int>`（默认 30）
  - `--start-a <hex>`、`--start-b <hex>`（支持 0x 前缀）
  - `--precompute` / `--no-precompute`（pDDT）
  - `--pddt-seed-stride <int>`（默认 8）
- 示例：
```bash
./neoalz_diff_search -r 6 -w 32 --start-a 0x1 --start-b 0x0 --no-precompute --pddt-seed-stride 8
```

线性 `neoalz_lin_search`：
- 选项：
  - `--rounds, -r <int>`（默认 4）
  - `--weight-cap, -w <int>`（默认 30）
  - `--start-mask-a <hex>`、`--start-mask-b <hex>`（支持 0x 前缀）
  - `--precompute` / `--no-precompute`（cLAT）
- 示例：
```bash
./neoalz_lin_search -r 6 -w 32 --start-mask-a 0x1 --start-mask-b 0x0 --precompute
```

---

## 库级 API 示例

- pDDT：
```cpp
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
using namespace TwilightDream;

PDDTAlgorithm1Complete::PDDTConfig cfg; cfg.bit_width = 32; cfg.set_weight_threshold(7);
auto entries = PDDTAlgorithm1Complete::compute_pddt(cfg);
```
- Matsui：
```cpp
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
using namespace TwilightDream;

MatsuiAlgorithm2Complete::SearchConfig sc; sc.num_rounds = 4; sc.initial_estimate = 1e-12;
auto result = MatsuiAlgorithm2Complete::execute_threshold_search(sc);
```
- cLAT（8 位分块）：
```cpp
#include "arx_search_framework/clat/clat_builder.hpp"
using namespace TwilightDream;

cLAT<8> clat; clat.build();
clat.lookup_and_recombine(0x12345678u, /*t=*/4, /*weight_cap=*/30,
  [](uint32_t u, uint32_t w, int weight) {/* 使用 (u,w,weight) */});
```
- NeoAlzette 管线外壳：
```cpp
#include "neoalzette/neoalzette_with_framework.hpp"
using TwilightDream::NeoAlzetteFullPipeline;

NeoAlzetteFullPipeline::DifferentialPipeline::Config cfg; cfg.rounds = 4;
auto medcp = NeoAlzetteFullPipeline::run_differential_analysis(cfg);
```

---

## 工程注意事项（必须了解）

- 计算密集：多轮搜索呈指数增长；请从小轮数开始并逐步扩大。
- 单线程：当前未做并行化；不要期待自动吃满多核心。
- 一轮精确评分：只有“模加/模减”产生权重；旋转/XOR/线性层只做掩码变换与组合。
- 统一命名空间：`TwilightDream`；底层算子、BlackBox、搜索框架全部一致。
- 旧模块已移除：`utility_tools.*`、`medcp_analyzer.*`、`melcc_analyzer.*`、`threshold_search_framework.*`、`highway_table_build*.cpp` 不再提供/引用。

---

## 当前目录结构（已与代码同步）

```
.
├── CMakeLists.txt
├── include/
│   ├── arx_analysis_operators/
│   │   ├── differential_xdp_add.hpp
│   │   ├── differential_optimal_gamma.hpp
│   │   ├── linear_correlation_add_logn.hpp
│   │   └── linear_correlation_addconst.hpp
│   ├── arx_search_framework/
│   │   ├── pddt/pddt_algorithm1.hpp
│   │   ├── matsui/matsui_algorithm2.hpp
│   │   ├── clat/algorithm1_const.hpp
│   │   ├── clat/clat_builder.hpp
│   │   └── README.md
│   └── neoalzette/
│       ├── neoalzette_core.hpp
│       ├── neoalzette_differential_step.hpp
│       ├── neoalzette_linear_step.hpp
│       └── neoalzette_with_framework.hpp
├── src/
│   ├── neoalzette_differential_main_search.cpp
│   ├── neoalzette_linear_main_search.cpp
│   ├── neoalzette/
│   │   └── neoalzette_linear_step.cpp
│   └── arx_search_framework/
│       ├── pddt_algorithm1_complete.cpp
│       └── matsui_algorithm2_complete.cpp
└── papers_txt/
```

---

## 许可证

MIT License（见 `LICENSE`）。算法出自公开论文；若用于学术，请同时引用原论文与本项目。
