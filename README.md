# NeoAlzette 密码分析工具集

[English](README_EN.md) | 中文

## 项目概述

本项目实现了针对 ARX 密码（Addition, Rotation, XOR）的**高性能密码分析工具集**，特别专注于 NeoAlzette 64位 ARX-box 的差分和线性密码分析。项目基于 CRYPTO 2022 等顶级会议的最新研究成果，提供了完整的自动化搜索和分析框架。

> ⚠️ **重要提醒**：这是一个**计算密集型**的研究工具，单次搜索可能需要数小时到数天的CPU时间和数GB内存。请在充分理解算法复杂度后谨慎使用。

### 核心算法

- **MEDCP (Maximum Expected Differential Characteristic Probability)**：模加模减的 最大期望差分特征概率算法
- **MELCC (Maximum Expected Linear Characteristic Correlation)**：模加模减的 最大期望差分特征相关算法

### 算法意义

这两个算法首次解决了 Niu et al. 在 CRYPTO 2022 提出的开放问题：**如何自动搜索 ARX 密码的差分线性轨道**。通过将任意矩阵乘法链转换为混合整数二次约束规划（MIQCP），实现了：

- 计算复杂度降低约 **8 倍**
- 首次支持**任意输出掩码**的自动搜索  
- 找到了 SPECK、Alzette 等 ARX 密码的**最佳差分线性区分器**

## 算法背景

### ARX 密码结构

ARX 密码基于三种基本运算：
- **Addition (⊞)**：模 2^n 加法
- **Rotation (<<<, >>>)**：循环位移  
- **XOR (⊕)**：异或运算

这种结构在软件实现中具有优异的性能，同时提供了足够的密码学强度。

### 理论基础

本项目基于以下核心理论成果：

1. **Lipmaa-Moriai (2001)** - 模加差分性质的高效计算算法
2. **Wallén (2003)** - 模加线性逼近的相关性分析
3. **混合整数二次约束规划 (MIQCP)** - 用于自动搜索差分线性轨道
4. **Highway 表技术** - O(1) 后缀下界查询，线性空间复杂度

### NeoAlzette 算法

NeoAlzette 是基于 Alzette 64位 ARX-box 的改进版本，具有以下特点：

- **64位状态**：(A, B) ∈ F₃₂² 
- **双子轮结构**：每轮包含两个子轮操作
- **非线性函数**：F(A) = B = B ⊞ (A<<< 31) ⊕ (A <<< 17) ⊕ RC[0], F(B) = A = A ⊞ (B<<< 31) ⊕ (B <<< 17) ⊕ RC[5]
- **线性扩散层**：L₁, L₂ 提供分支数保证
- **轮常数注入**：16个预定义轮常数

## 完整项目结构

```
├── include/                          # 核心算法头文件库
│   ├── neoalzette.hpp               # NeoAlzette ARX-box 核心实现（轮函数、常数）
│   ├── lm_fast.hpp                  # Lipmaa-Moriai 差分快速枚举算法
│   ├── lm_wallen.hpp                # LM-Wallén 混合模型（差分+线性）
│   ├── wallen_fast.hpp              # Wallén 线性相关性快速计算
│   ├── wallen_optimized.hpp         # Wallén 优化版：预计算自动机
│   ├── highway_table.hpp            # 差分 Highway 后缀下界表（O(1) 查询）
│   ├── highway_table_lin.hpp        # 线性 Highway 后缀下界表
│   ├── lb_round_full.hpp            # 完整轮函数差分下界估计
│   ├── lb_round_lin.hpp             # 完整轮函数线性下界估计
│   ├── suffix_lb.hpp                # 多轮后缀差分下界计算
│   ├── suffix_lb_lin.hpp            # 多轮后缀线性下界计算
│   ├── threshold_search.hpp         # Matsui 阈值搜索框架
│   ├── threshold_search_optimized.hpp # 并行化阈值搜索
│   ├── matsui_complete.hpp          # 完整的Matsui Algorithm 2实现
│   ├── canonicalize.hpp             # 状态标准化（旋转等价类）
│   ├── state_optimized.hpp          # 优化的状态表示和缓存管理
│   ├── diff_add_const.hpp           # 模加常数差分性质（MEDCP核心）
│   ├── mask_backtranspose.hpp       # 线性掩码反向传播
│   ├── neoalz_lin.hpp               # NeoAlzette 线性层精确实现
│   ├── pddt.hpp                     # 部分差分分布表 (Algorithm 1)
│   ├── pddt_optimized.hpp           # 优化的pDDT构建算法
│   └── trail_export.hpp             # 轨道导出和CSV格式化
├── src/                             # 主要分析工具
│   ├── analyze_medcp.cpp            # 🔥 MEDCP 轨道搜索器（主要工具）
│   ├── analyze_medcp_optimized.cpp  # ⚡ 优化版MEDCP分析器（推荐）
│   ├── analyze_melcc.cpp            # 🔥 MELCC 轨道搜索器（主要工具）
│   ├── analyze_melcc_optimized.cpp  # ⚡ 优化版MELCC分析器（推荐）
│   ├── complete_matsui_demo.cpp     # 完整Matsui Algorithm 1&2演示
│   ├── bnb.cpp                      # 分支限界搜索实现
│   ├── neoalzette.cpp               # NeoAlzette 算法实现
│   ├── pddt.cpp                     # 部分差分分布表实现
│   ├── gen_round_lb_table.cpp       # 轮下界表生成器
│   ├── highway_table_build.cpp      # 差分 Highway 表构建器
│   ├── highway_table_build_lin.cpp  # 线性 Highway 表构建器
│   ├── threshold_lin.cpp            # 线性阈值搜索
│   ├── search_beam_diff.cpp         # 束搜索差分分析
│   └── milp_diff.cpp                # MILP 差分模型（实验性）
├── papers/                          # 核心理论论文（PDF）
├── papers_txt/                      # 论文文本提取版本（便于分析）
├── PAPERS_COMPLETE_ANALYSIS_CN.md   # 🔥 11篇论文完全理解指南（25,000+字）
├── ALZETTE_VS_NEOALZETTE.md         # Alzette vs NeoAlzette 设计对比
├── ALGORITHM_IMPLEMENTATION_STATUS.md # 论文算法实现状态分析
├── CMakeLists.txt                   # 构建配置文件
├── LICENSE                          # GPL v3.0 开源许可证
└── .gitignore                       # Git 忽略配置
```

### 核心文件说明

#### 🔥 主要分析工具
- **`analyze_medcp.cpp`** - **MEDCP差分分析器**：搜索最优差分轨道，支持Highway加速、导出CSV、权重直方图等
- **`analyze_melcc.cpp`** - **MELCC线性分析器**：搜索最优线性轨道，支持精确反向传播、beam搜索等

#### 🧮 核心算法实现
- **`lm_fast.hpp`** - **Lipmaa-Moriai 2001算法**：O(log n)时间计算模加差分概率，支持前缀剪枝
- **`wallen_fast.hpp`** - **Wallén 2003算法**：O(log n)时间计算模加线性相关性
- **`highway_table*.hpp`** - **Highway表技术**：预计算后缀下界，O(1)查询时间，线性空间

#### 🔧 辅助工具  
- **`highway_table_build*.cpp`** - Highway表预计算工具（可选，用于大规模搜索加速）
- **`gen_round_lb_table.cpp`** - 轮下界表生成器（优化单轮剪枝）
- **`trail_export.hpp`** - 统一的CSV导出格式（便于后续分析）

#### 📚 核心文档
- **`PAPERS_COMPLETE_ANALYSIS_CN.md`** - **🔥 11篇论文完全理解指南**
  - 25,000+字的深度技术分析
  - 从数学公式到代码实现的完整链条
  - Alzette三步流水线设计的工程艺术解析
  - Lipmaa-Moriai、Wallén等核心算法的层层剖析
  - 常见困惑的澄清和学习路径指导
  - **每当遇到算法理解困难时的必读文档**

## 构建说明

### 系统要求

- **编译器**：支持 C++20 的现代编译器（GCC 10+ / Clang 12+ / MSVC 2019+）
- **内存**：推荐 8GB+ RAM（用于大型搜索任务）
- **CPU**：支持 popcount 指令的 x86_64 处理器

### 构建步骤

```bash
# 克隆仓库
git clone <repository-url>
cd neoalzette_search

# 创建构建目录
mkdir build && cd build

# 配置构建
cmake ..

# 编译
make -j$(nproc)

# 可选：构建演示程序
cmake -DNA_BUILD_DEMOS=ON ..
make -j$(nproc)
```

### 构建产物

#### 🔥 主要分析工具
- **`analyze_medcp`** - 标准MEDCP轨道搜索
- **`analyze_medcp_optimized`** - **⚡ 优化版MEDCP分析器**（推荐使用）
- **`analyze_melcc`** - 标准MELCC轨道搜索  
- **`analyze_melcc_optimized`** - **⚡ 优化版MELCC分析器**（推荐使用）

#### 🔧 辅助工具
- **`complete_matsui_demo`** - 完整Matsui Algorithm 1&2演示
- `highway_table_build*` - Highway表构建工具

#### ⚡ 优化版本特性
**新增的优化版本包含以下改进**：
- **Wallén算法重写**：预计算自动机替代运行时递归，性能提升2-5倍
- **并行化搜索**：多线程工作窃取，充分利用多核CPU
- **缓存友好设计**：64位打包状态表示，减少内存访问开销  
- **快速标准化**：优化的bit操作算法，加速状态等价性检查
- **改进剪枝策略**：更好的下界估计和重复状态检测

## 使用方法

### ⚠️ 使用前必读

这些工具是**计算密集型**的研究级算法实现：

- **时间复杂度**：指数级最坏情况，实际性能取决于剪枝效率
- **空间复杂度**：O(2^{权重上限}) 内存使用  
- **建议环境**：高性能服务器或集群，而非个人笔记本电脑
- **参数调试**：从小参数开始试验，逐步增加复杂度

### MEDCP 轨道搜索

MEDCP 分析器使用 **Matsui 阈值搜索 + Lipmaa-Moriai 局部枚举**，支持完整的轨道路径记录。

#### 基本语法
```bash
./analyze_medcp R Wcap [highway.bin] [选项]
```

#### 参数说明
- **`R`** - 搜索轮数（建议范围：4-12轮）
- **`Wcap`** - 全局权重上限（越小越快，建议20-50）
- **`highway.bin`** - 可选的预计算Highway表文件

#### 选项详解
- **`--start-hex dA dB`** - 起始差分状态（32位十六进制）
  - `dA`: A寄存器差分
  - `dB`: B寄存器差分  
  - 示例：`--start-hex 0x80000000 0x1`
- **`--k1 K`** - 变量-变量加法的Top-K候选数（默认4，范围1-16）
- **`--k2 K`** - 变量-常数加法的Top-K候选数（默认4，范围1-16）
- **`--export path.csv`** - 导出搜索摘要到CSV文件
- **`--export-trace path.csv`** - 导出完整最优轨道路径
- **`--export-hist path.csv`** - 导出权重分布直方图
- **`--export-topN N path.csv`** - 导出前N个最优结果

#### 标准版本使用示例
```bash
# 入门示例：6轮搜索，权重上限25（约1分钟）
./analyze_medcp 6 25

# 标准搜索：8轮，权重35，自定义起始差分
./analyze_medcp 8 35 --start-hex 0x1 0x0
```

#### ⚡ 优化版本使用示例（推荐）
```bash
# 基础优化搜索：自动检测线程数
./analyze_medcp_optimized 6 25

# 高性能搜索：指定线程数，使用Highway表
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 8 --k1 8 --k2 8

# 快速搜索：使用快速标准化（适合大规模搜索）
./analyze_medcp_optimized 10 40 --fast-canonical --threads 16

# 完整优化分析：导出详细结果
./analyze_medcp_optimized 8 30 \
  --export-trace trail_opt.csv \
  --export-hist histogram_opt.csv \
  --export-topN 10 top10_opt.csv \
  --threads 8

# 性能对比测试
echo "Standard version:" && time ./analyze_medcp 6 25
echo "Optimized version:" && time ./analyze_medcp_optimized 6 25 --threads 4
```

#### 性能提升说明
- **2-5倍速度提升**：优化的Wallén算法和并行化
- **更低内存使用**：打包状态表示和内存池管理
- **更好的可扩展性**：多线程支持充分利用现代多核CPU

### MELCC 轨道搜索

MELCC 分析器使用 **优先队列搜索 + Wallén 线性枚举**，支持精确的反向掩码传播。

#### 基本语法
```bash
./analyze_melcc R Wcap [选项]
```

#### 参数说明
- **`R`** - 搜索轮数（建议范围：4-10轮）
- **`Wcap`** - 权重上限（线性分析通常比差分更严格）

#### 选项详解
- **`--start-hex mA mB`** - 起始线性掩码（32位十六进制）
  - `mA`: A寄存器掩码
  - `mB`: B寄存器掩码
- **`--lin-highway H.bin`** - 线性Highway表文件
- **`--export path.csv`** - 导出分析摘要
- **`--export-trace path.csv`** - 导出最优轨道
- **`--export-hist path.csv`** - 导出权重分布
- **`--export-topN N path.csv`** - 导出Top-N结果

#### 标准版本使用示例
```bash
# 入门示例：6轮线性搜索
./analyze_melcc 6 20

# 高权重掩码搜索
./analyze_melcc 8 25 --start-hex 0x80000001 0x0
```

#### ⚡ 优化版本使用示例（推荐）
```bash
# 基础优化线性搜索
./analyze_melcc_optimized 6 20

# 高性能线性分析：多线程+Highway表
./analyze_melcc_optimized 8 25 --lin-highway highway_lin.bin --threads 6

# 快速线性搜索：使用快速标准化
./analyze_melcc_optimized 10 30 --fast-canonical --threads 8

# 完整优化线性分析流水线
./analyze_melcc_optimized 8 25 \
  --start-hex 0x1 0x0 \
  --export-trace linear_trail_opt.csv \
  --export-topN 5 best_linear_opt.csv \
  --threads 6

# 性能对比：标准 vs 优化版本
time ./analyze_melcc 6 20
time ./analyze_melcc_optimized 6 20 --threads 4
```

#### 线性分析特有优化
- **预计算Wallén自动机**：避免运行时递归，显著提升枚举速度
- **优化的反向传播**：精确的(L^{-1})^T计算，减少不必要的状态生成
- **改进的优先队列**：使用打包状态减少内存占用和提升缓存命中率

### Highway 表构建

```bash
# 构建差分 Highway 表（可选，用于加速搜索）
./highway_table_build output_diff.bin [max_rounds]

# 构建线性 Highway 表  
./highway_table_build_lin output_lin.bin [max_rounds]
```

### 论文算法演示

```bash
# 演示完整的Matsui Algorithm 1&2实现
./complete_matsui_demo --full

# 快速验证算法正确性
./complete_matsui_demo --quick
```

## 📋 **完整CLI使用指南**

### 🔥 **analyze_medcp / analyze_medcp_optimized - MEDCP差分轨道搜索**

#### **完整语法**
```bash
./analyze_medcp[_optimized] R Wcap [highway.bin] [选项]
```

#### **必需参数**
- **`R`** - 搜索轮数 (整数，建议4-12)
  - 个人电脑建议：4-6轮
  - 集群环境：6-12轮
- **`Wcap`** - 权重上限 (整数，建议15-50)
  - 越小搜索越快，但可能找不到解
  - 个人电脑建议：15-25
  - 集群环境：25-50

#### **可选参数**
- **`highway.bin`** - Highway表文件路径
  - 预计算的后缀下界表，大幅提升搜索速度
  - 可选，但强烈推荐用于重复搜索

#### **所有支持选项**

| 选项 | 参数 | 说明 | 示例 |
|------|------|------|------|
| `--start-hex` | `dA dB` | 起始差分状态(32位十六进制) | `--start-hex 0x1 0x0` |
| `--export` | `file.csv` | 导出搜索摘要 | `--export results.csv` |
| `--export-trace` | `file.csv` | 导出完整轨道路径 | `--export-trace trail.csv` |
| `--export-hist` | `file.csv` | 导出权重分布直方图 | `--export-hist histogram.csv` |
| `--export-topN` | `N file.csv` | 导出前N个最优结果 | `--export-topN 10 top10.csv` |
| `--k1` | `K` | var-var加法Top-K候选数(1-16) | `--k1 8` |
| `--k2` | `K` | var-const加法Top-K候选数(1-16) | `--k2 8` |
| `--threads` | `N` | 线程数(仅优化版) | `--threads 8` |
| `--fast-canonical` | 无 | 快速标准化(仅优化版) | `--fast-canonical` |

#### **使用示例从入门到专家**

**🟢 入门级 (个人电脑)**：
```bash
# 最小验证测试
./analyze_medcp_optimized 4 15

# 基础差分搜索  
./analyze_medcp_optimized 4 20 --start-hex 0x1 0x0

# 导出结果用于分析
./analyze_medcp_optimized 4 25 --export basic_result.csv
```

**🟡 标准级 (个人电脑/小型集群)**：
```bash
# 多线程搜索
./analyze_medcp_optimized 6 25 --threads 4

# 使用Highway表加速  
./analyze_medcp_optimized 6 30 highway_diff.bin --threads 4

# 完整结果导出
./analyze_medcp_optimized 6 25 \
  --export summary.csv \
  --export-trace trail.csv \
  --export-hist histogram.csv
```

**🔴 专业级 (必需集群)**：
```bash
# 高性能搜索
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 16 --k1 8 --k2 8

# 大规模参数扫描
for start in 0x1 0x8000 0x80000000; do
  ./analyze_medcp_optimized 8 35 highway_diff.bin \
    --start-hex $start 0x0 \
    --threads 16 \
    --export results_${start}.csv
done

# 完整研究分析
./analyze_medcp_optimized 10 40 highway_diff.bin \
  --fast-canonical \
  --threads 32 \
  --export-trace trail_10r.csv \
  --export-hist hist_10r.csv \
  --export-topN 20 top20_10r.csv
```

### 🔥 **analyze_melcc / analyze_melcc_optimized - MELCC线性轨道搜索**

#### **完整语法**
```bash
./analyze_melcc[_optimized] R Wcap [选项]
```

#### **必需参数**
- **`R`** - 搜索轮数 (整数，建议4-10)
  - 线性分析比差分更加计算密集
  - 个人电脑建议：4-5轮
  - 集群环境：6-10轮
- **`Wcap`** - 权重上限 (整数，建议10-40)
  - 线性分析的权重通常比差分更严格

#### **所有支持选项**

| 选项 | 参数 | 说明 | 示例 |
|------|------|------|------|
| `--start-hex` | `mA mB` | 起始线性掩码(32位十六进制) | `--start-hex 0x80000000 0x1` |
| `--export` | `file.csv` | 导出分析摘要 | `--export linear_results.csv` |
| `--export-trace` | `file.csv` | 导出最优线性轨道 | `--export-trace linear_trail.csv` |
| `--export-hist` | `file.csv` | 导出权重分布 | `--export-hist linear_hist.csv` |
| `--export-topN` | `N file.csv` | 导出前N个最优结果 | `--export-topN 5 top5_linear.csv` |
| `--lin-highway` | `H.bin` | 线性Highway表文件 | `--lin-highway highway_lin.bin` |
| `--threads` | `N` | 线程数(仅优化版) | `--threads 6` |
| `--fast-canonical` | 无 | 快速标准化(仅优化版) | `--fast-canonical` |

#### **使用示例从入门到专家**

**🟢 入门级 (个人电脑)**：
```bash
# 最小线性分析
./analyze_melcc_optimized 4 15

# 指定起始掩码
./analyze_melcc_optimized 4 20 --start-hex 0x1 0x0

# 导出线性分析结果
./analyze_melcc_optimized 4 20 --export linear_basic.csv
```

**🟡 标准级 (个人电脑/小型集群)**：
```bash
# 多线程线性搜索
./analyze_melcc_optimized 5 20 --threads 4

# 使用线性Highway表
./analyze_melcc_optimized 6 25 --lin-highway highway_lin.bin

# 完整线性分析流水线
./analyze_melcc_optimized 5 22 \
  --start-hex 0x80000001 0x0 \
  --export-trace linear_trail.csv \
  --export-topN 5 best_linear.csv \
  --threads 4
```

**🔴 专业级 (必需集群)**：
```bash
# 高精度线性分析
./analyze_melcc_optimized 8 30 highway_lin.bin --threads 16

# 大规模线性掩码搜索
for mask in 0x1 0x80000000 0xFFFF0000; do
  ./analyze_melcc_optimized 8 28 highway_lin.bin \
    --start-hex $mask 0x0 \
    --threads 12 \
    --export linear_results_${mask}.csv
done
```

### 🔧 **highway_table_build / highway_table_build_lin - Highway表构建**

#### **完整语法**
```bash
./highway_table_build output_file.bin [max_rounds]
./highway_table_build_lin output_file.bin [max_rounds]
```

#### **参数说明**
- **`output_file.bin`** - 输出Highway表文件路径
- **`max_rounds`** - 最大轮数 (可选，默认10)

#### **使用建议**
```bash
# 构建差分Highway表 (一次构建，多次使用)
./highway_table_build highway_diff.bin 12
# 构建时间：~1-3小时，文件大小：~2-4GB

# 构建线性Highway表
./highway_table_build_lin highway_lin.bin 10  
# 构建时间：~30分钟-2小时，文件大小：~500MB-2GB

# 使用Highway表进行加速搜索
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 8
./analyze_melcc_optimized 8 30 --lin-highway highway_lin.bin --threads 6
```

### 🎯 **complete_matsui_demo - 论文算法演示**

#### **完整语法**
```bash
./complete_matsui_demo [--quick|--full]
```

#### **选项说明**
- **`--quick`** - 快速验证模式，验证Algorithm 1&2基本功能
- **`--full`** - 完整演示模式，展示highways/country roads策略

#### **教育价值**
```bash
# 理解论文算法实现
./complete_matsui_demo --quick

# 深入理解搜索策略  
./complete_matsui_demo --full
```

### ⚠️ **关键使用注意事项**

#### **参数选择指南**

**权重上限 (Wcap) 选择策略**：
```bash
# 过小：可能找不到任何轨道
./analyze_medcp_optimized 6 10    # 可能无结果

# 适中：通常能找到有意义的轨道  
./analyze_medcp_optimized 6 25    # 推荐起始值

# 过大：搜索时间指数增长
./analyze_medcp_optimized 6 50    # ⚠️ 可能需要数小时
```

**起始状态选择技巧**：
```bash
# ✅ 好的起始状态：稀疏差分
--start-hex 0x1 0x0           # 单bit差分
--start-hex 0x80000000 0x1    # 首末位差分
--start-hex 0x8000 0x8        # 对称稀疏差分

# ❌ 避免的起始状态：密集差分  
--start-hex 0xFFFFFFFF 0xAAAAAAAA  # 太多活跃位
--start-hex 0x0 0x0           # 零差分无意义
```

**K值调优策略**：
```bash
# 保守设置：快速但可能遗漏最优解
--k1 2 --k2 2

# 标准设置：平衡性能和完整性
--k1 4 --k2 4    # 默认值

# 激进设置：更完整但显著更慢
--k1 8 --k2 8    # ⚠️ 仅用于集群环境
```

#### **常见问题排查**

**如果搜索无结果**：
```bash
# 1. 降低权重上限
./analyze_medcp_optimized 4 15 --start-hex 0x1 0x0

# 2. 尝试不同起始状态
./analyze_medcp_optimized 4 20 --start-hex 0x8000 0x0

# 3. 检查参数有效性
./analyze_medcp_optimized 4 25    # 使用默认起始状态
```

**如果搜索过慢**：
```bash
# 1. 降低复杂度参数
./analyze_medcp_optimized 4 20 --fast-canonical

# 2. 减少线程数避免资源竞争
./analyze_medcp_optimized 4 25 --threads 1

# 3. 使用Highway表加速
./highway_table_build highway.bin 8
./analyze_medcp_optimized 6 25 highway.bin
```

**如果内存不足**：
```bash
# 使用快速标准化模式
./analyze_medcp_optimized 4 25 --fast-canonical

# 降低权重上限
./analyze_medcp_optimized 4 20

# 关闭高级导出功能
./analyze_medcp_optimized 4 25 --export simple.csv  # 仅基本导出
```

#### **输出文件格式说明**

**基本摘要 (--export)**：
```csv
algo,R,Wcap,start_dA,start_dB,K1,K2,best_w,time_ms,threads
MEDCP_OPTIMIZED,6,25,0x1,0x0,4,4,18,15432,4
```

**轨道路径 (--export-trace)**：
```csv  
algo,MEDCP,field,round,dA,dB,acc_weight
MEDCP,trace,0,0x1,0x0,0
MEDCP,trace,1,0x8000,0x8,5
MEDCP,trace,2,0x4000,0x4000,12
```

**权重直方图 (--export-hist)**：
```csv
algo,MEDCP,field,weight,count  
MEDCP,hist,15,1
MEDCP,hist,18,3
MEDCP,hist,20,1
```

#### **性能调优建议**

**单次搜索优化**：
```bash
# 使用所有优化选项
./analyze_medcp_optimized 6 25 highway_diff.bin \
  --fast-canonical \
  --threads $(nproc) \
  --k1 6 --k2 6
```

**批量搜索优化**：
```bash
# 预建Highway表，批量复用
./highway_table_build highway.bin 10

# 并行批处理
for w in {20..30..2}; do
  ./analyze_medcp_optimized 6 $w highway.bin --export batch_w${w}.csv &
done
wait  # 等待所有任务完成
```

**集群环境优化**：
```bash  
# 大内存节点的最大化利用
./analyze_medcp_optimized 10 45 highway_diff.bin \
  --threads 64 \
  --k1 16 --k2 16 \
  --export-trace research_trail.csv
```

### 并行化和集群部署

该工具集设计为**CPU密集型**应用，支持：

- **多核并行**：使用 `make -j$(nproc)` 充分利用多核CPU
- **内存高效**：使用记忆化和增量计算减少内存占用
- **集群友好**：各工具独立运行，易于在集群环境部署

#### 集群使用建议

```bash
# 将大任务分解为多个子任务并行执行
for r in {6..12}; do
    for w in {20..40..5}; do
        sbatch run_analysis.sh $r $w
    done  
done
```

## 🖥️ 个人电脑 vs 🏢 计算机集群：使用场景详解

### ⚠️ **重要资源需求说明**

**个人电脑的现实限制**：
- 💻 **内存容量**：通常8-16GB，对于研究级分析严重不足
- ⏱️ **计算时间**：单核性能有限，多轮搜索可能需要数天
- 🌡️ **散热限制**：长时间高负载可能导致降频或过热
- 📊 **参数限制**：只能运行很低参数的"验证性"测试

**计算机集群的必要性**：
- 🎯 **真正的研究分析**（8轮以上）需要申请集群资源
- 📈 **突破性发现**（10轮以上）必须使用高性能计算环境
- 💾 **内存要求**：32GB+ RAM才能处理复杂搜索任务
- 🔄 **并行扩展**：数百核心同时工作才能在合理时间内完成

### 详细使用场景对比

#### 👨‍💻 **个人电脑适用场景**

| 参数配置 | 轮数 | 权重上限 | 内存需求 | 时间范围 | 用途 |
|----------|------|----------|----------|----------|------|
| **验证级** | 4 | 15-20 | 50-200MB | 10秒-2分钟 | 算法验证、理解工具 |
| **学习级** | 4-5 | 20-25 | 200-500MB | 2-10分钟 | 学习密码分析、参数理解 |
| **测试级** | 6 | 25-30 | 500MB-1GB | 10-60分钟 | 工具测试、方法验证 |

**个人电脑的实际限制**：
```bash
# ✅ 可行：验证工具是否正常工作
./analyze_medcp_optimized 4 15    # ~30秒

# 🟡 勉强：需要关闭其他程序
./analyze_medcp_optimized 4 25    # ~5-15分钟，内存压力

# ❌ 不建议：可能导致系统卡死或内存不足
./analyze_medcp_optimized 6 30    # 可能需要数小时，内存可能不够
```

#### 🏢 **计算机集群必要场景**

| 研究类型 | 轮数 | 权重上限 | 内存需求 | 计算节点 | 申请资源 |
|----------|------|----------|----------|----------|----------|
| **论文实验** | 6-8 | 25-35 | 4-8GB | 1-2节点 | 中等计算资源 |
| **深度分析** | 8-10 | 35-45 | 8-16GB | 2-4节点 | 高性能计算资源 |
| **突破研究** | 10-12+ | 45+ | 16-64GB | 4-16节点 | 超算中心资源 |

**集群资源申请指南**：
```bash
# 中等研究任务
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1  
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=24:00:00

# 重型研究任务  
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=32
#SBATCH --mem=128G
#SBATCH --time=168:00:00  # 1周
```

### 📊 **现实的性能期望管理**

#### **个人电脑能做什么**：
```
✅ 算法验证：确认工具正常工作
✅ 参数理解：学习不同参数的影响  
✅ 方法验证：验证分析方法的正确性
✅ 小规模实验：获得基础的分析数据
✅ 工具熟悉：掌握使用方法和接口

❌ 不能做：真正的密码学研究级分析
❌ 不能做：发现新的密码学结果
❌ 不能做：与文献结果的对比验证
❌ 不能做：完整的安全性评估
```

#### **集群计算能做什么**：
```
🎯 真正的研究：
- 发现新的最优差分/线性轨道
- 验证或挑战现有的安全性声明  
- 开发新的攻击方法
- 在顶级会议发表突破性成果

📈 大规模分析：
- 完整的参数空间搜索
- 统计显著的实验验证
- 与现有文献的对比分析
- 新ARX密码的安全性评估
```

### 💰 **资源成本估算**

#### **个人电脑成本**：
- 💻 **硬件成本**：0（使用现有设备）
- ⏱️ **时间成本**：低（短时间验证）
- 🎯 **研究价值**：有限（仅限学习和验证）

#### **集群资源成本**：
- 💰 **申请难度**：需要正式研究项目支持
- 📝 **资源申请**：详细的计算需求说明
- ⏱️ **等待时间**：可能需要排队等待资源
- 🎯 **研究价值**：高（真正的突破性发现）

### 🎓 **不同研究阶段的建议**

#### **学习阶段（个人电脑足够）**：
```bash
# 理解工具基本功能
./analyze_medcp_optimized 4 15 --export test1.csv
./analyze_melcc_optimized 4 15 --export test2.csv

# 理解参数影响
for w in {15..25}; do 
  time ./analyze_medcp_optimized 4 $w
done

# 掌握导出和分析功能
./analyze_medcp_optimized 4 20 \
  --export-trace trail.csv \
  --export-hist hist.csv
```

#### **研究阶段（必需申请集群）**：
```bash
# 申请集群资源后的实际研究
sbatch run_analysis.sbatch 8 35    # 发现新轨道
sbatch run_analysis.sbatch 10 40   # 挑战安全边界  
sbatch run_comparison.sbatch       # 与文献对比
```

### 🚨 **关键提醒**

**不要在个人电脑上尝试**：
- ❌ 8轮以上的搜索（可能导致内存不足）
- ❌ 权重上限>30的任务（可能跑数天）
- ❌ 没有Highway表的大型搜索（效率极低）  
- ❌ 多个工具同时运行（资源竞争）

**集群申请的必要条件**：
- 📚 **明确的研究目标**：说明要解决什么密码学问题
- 📊 **详细的资源需求**：基于小规模测试的合理估算
- ⏱️ **现实的时间预期**：大型搜索可能需要数周
- 💾 **存储计划**：结果数据的存储和管理方案

## 开发和扩展

### 添加新密码算法

1. 在 `include/` 中定义算法结构
2. 实现轮函数和状态转换
3. 适配差分/线性局部模型
4. 添加相应的分析程序

### 自定义搜索策略

```cpp
// 实现自定义的下界函数
auto custom_lower_bound = [](const State& s, int round) -> int {
    // 自定义下界计算逻辑
    return compute_bound(s, round);
};

// 使用自定义策略进行搜索
auto result = matsui_threshold_search(rounds, start, cap, 
                                     next_states, custom_lower_bound);
```

## 许可证

本项目采用 GNU General Public License v3.0 许可证。详情请参阅 [LICENSE](LICENSE) 文件。

## 📚 学习资源导引

### **文档使用建议**
- **想快速上手？** → 阅读本README的"使用方法"章节
- **遇到算法困惑？** → 查阅 `PAPERS_COMPLETE_ANALYSIS_CN.md`
- **需要深入理解？** → 从MnT操作符开始，逐层理解算法封装
- **优化遇到瓶颈？** → 参考论文分析文档中的"性能优化"章节

### **学习路径推荐** 
```
第1步：熟悉工具 → 运行4轮小测试，理解基本概念
第2步：理解算法 → 阅读论文分析，掌握数学原理
第3步：深入应用 → 尝试8轮分析，探索参数优化  
第4步：进阶研究 → 扩展到其他ARX密码，发表成果
```

## 引用

如果您在研究中使用本工具，请引用相关论文：

```bibtex
@inproceedings{miqcp2022,
  title={A MIQCP-Based Automatic Search Algorithm for Differential-Linear Trails of ARX Ciphers},
  author={Guangqiu Lv and Chenhui Jin and Ting Cui},
  booktitle={Cryptology ePrint Archive},
  year={2022}
}

@inproceedings{alzette2020,
  title={Alzette: A 64-Bit ARX-box},  
  author={Christof Beierle and Alex Biryukov and Luan Cardoso dos Santos and others},
  booktitle={Annual International Cryptology Conference},
  pages={419--448},
  year={2020}
}
```

## 贡献

欢迎提交 Issue 和 Pull Request！请确保：

1. 代码符合 C++20 标准
2. 添加适当的测试和文档
3. 遵循现有的代码风格
4. 详细描述变更内容

## 联系方式

如有技术问题或合作意向，请通过以下方式联系：

- 创建 GitHub Issue
- 发送邮件至项目维护者
- 参与相关学术会议和讨论

---

**注意**：本工具仅供学术研究和教育用途，请勿用于恶意攻击或非法活动。