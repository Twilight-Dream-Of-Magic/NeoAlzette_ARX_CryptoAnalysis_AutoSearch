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

### 高级选项

#### 导出和分析选项

```bash
# 导出完整轨道路径
./analyze_medcp 8 30 --export-trace trail.csv

# 导出权重分布直方图
./analyze_medcp 8 30 --export-hist histogram.csv  

# 导出前 N 个最优结果
./analyze_medcp 8 30 --export-topN 10 top10.csv
```

#### 搜索参数调优

- `--k1 K`：变量-变量加法的 Top-K 候选数（默认4）
- `--k2 K`：变量-常数加法的 Top-K 候选数（默认4）
- 增大 K 值可能找到更好的轨道，但会显著增加搜索时间

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

## 性能特征与计算复杂度

### 算法复杂度分析

#### 核心算法复杂度
- **Lipmaa-Moriai 枚举**：平均 << 2^n（前缀不可行性剪枝效率高）
- **Wallén 线性计算**：O(log n) 时间，增量popcount优化
- **Highway 表查询**：O(1) 时间，O(4^轮数) 空间预计算
- **阈值搜索**：O(状态空间 × 分支因子^轮数），实际取决于剪枝效率

#### 实际性能估算
```
搜索空间大小 ≈ (有效差分数) × (轮数) × (分支因子)
其中：
- 有效差分数 ≈ 2^{32} × 剪枝率（通常 < 1%）
- 分支因子 ≈ 平均每状态的后继数（50-500）
- 剪枝效率 ≈ 下界剪枝命中率（90%+）
```

### 详细资源消耗表

| 配置 | 轮数 | 权重上限 | 内存使用 | CPU时间 | 磁盘I/O | 适用场景 |
|------|------|----------|----------|---------|---------|----------|
| **入门级** | 4-6 | 15-25 | 100MB-500MB | 30秒-5分钟 | 最小 | 算法验证、教学演示 |
| **标准级** | 6-8 | 25-35 | 500MB-2GB | 5分钟-2小时 | 中等 | 论文实验、方法对比 |  
| **专业级** | 8-10 | 35-45 | 2GB-8GB | 2小时-1天 | 高 | 深度分析、最优轨道 |
| **研究级** | 10-12+ | 45+ | 8GB-32GB | 1天-1周 | 极高 | 突破性研究、集群计算 |

### 内存使用模式

```
内存组件分析
记忆化哈希表:     O(搜索状态数) ≈ 10MB - 1GB  
优先队列:        O(活跃节点数) ≈ 1MB - 100MB
Highway 表:      O(4^最大轮数) ≈ 16MB - 4GB
轨道路径存储:     O(最大轮数 × Top-N) ≈ 1MB - 10MB
临时计算缓存:     O(单轮展开数) ≈ 10MB - 100MB
```

### 个人电脑使用建议

#### **4轮搜索 - 完全可行**
- ⏱️ **时间**：10秒到10分钟（取决于权重上限）
- 💾 **内存**：50MB到1GB（通常<500MB）
- 💻 **CPU**：中等使用率，不会让电脑卡死
- 📁 **存储**：几乎无磁盘I/O需求

#### 建议测试流程
```bash
# 第1步：最小测试（必定成功）
time ./analyze_medcp_optimized 4 15

# 第2步：标准测试  
time ./analyze_medcp_optimized 4 20 --threads 2

# 第3步：挑战测试（如果硬件允许）
time ./analyze_medcp_optimized 4 25 --threads 4
```

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