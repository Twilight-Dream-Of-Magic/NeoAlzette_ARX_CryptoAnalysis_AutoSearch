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

## 🏗️ 现代模块化架构

### 🎯 **5大核心类模块**

```
include/
├── 🔴 medcp_analyzer.hpp           # MEDCP差分分析器类
│   ├── MEDCPAnalyzer               # 主分析器类  
│   ├── ├── DifferentialState       # 差分状态封装
│   ├── ├── HighwayTable           # 差分Highway表管理
│   ├── ├── BoundsComputer         # 差分下界计算器
│   ├── └── enumerate_lm_gammas     # Lipmaa-Moriai枚举算法
│   │   
├── 🔵 melcc_analyzer.hpp           # MELCC线性分析器类
│   ├── MELCCAnalyzer              # 主分析器类
│   ├── ├── LinearState            # 线性状态封装  
│   ├── ├── WallenAutomaton        # Wallén自动机优化
│   ├── ├── LinearHighwayTable     # 线性Highway表管理
│   ├── ├── LinearBoundsComputer   # 线性下界计算器
│   ├── └── enumerate_wallen_omegas # Wallén枚举算法
│   │   
├── ⚪ neoalzette_core.hpp          # NeoAlzette核心ARX-box类
│   ├── NeoAlzetteCore             # 静态核心算法类
│   ├── ├── rotl, rotr             # 基础旋转运算(模板)
│   ├── ├── l1_forward, l2_forward # 线性扩散层
│   ├── ├── cd_from_A, cd_from_B   # 交叉分支注入
│   ├── └── forward, backward      # 主ARX-box变换
│   │   
├── ⚡ threshold_search_framework.hpp # 阈值搜索框架类
│   ├── ThresholdSearchFramework   # 主搜索框架类
│   ├── ├── matsui_threshold_search # Matsui Algorithm 2
│   ├── ├── WorkQueue              # 并行工作队列
│   ├── ├── PDDT                   # 部分差分分布表
│   ├── ├── MatsuiComplete         # 完整Matsui实现
│   ├── └── PerformanceMonitor     # 性能监控器
│   │   
└── 🛠️ utility_tools.hpp            # 工具类集合
    ├── UtilityTools               # 主工具类
    ├── ├── Canonicalizer          # 状态标准化工具
    ├── ├── TrailExporter          # 轨道导出工具
    ├── ├── BasicBounds            # 基础下界计算
    ├── ├── SimplePDDT             # 简单PDDT实现
    ├── ├── ConfigValidator        # 配置验证器
    ├── ├── PerformanceUtils       # 性能工具
    ├── └── StringUtils            # 字符串工具
```

### 🎨 **模块化设计优势**

**1. 面向对象的清晰设计**
- 每个类都有单一明确的职责
- 封装了相关的数据和操作
- 提供了简洁的公共接口

**2. 工程最佳实践**
- **分离定义和实现**：.hpp声明，.cpp实现
- **模板函数优化**：性能关键的模板保持内联
- **静态库编译**：所有功能打包到libneoalzette.a

**3. 维护和扩展友好**
- **高内聚**：每个模块内部功能紧密相关
- **低耦合**：模块间依赖关系清晰最小
- **易于测试**：每个类都可以独立测试

**4. 学习路径清晰**
- 想学差分分析？从 `MEDCPAnalyzer` 开始
- 想学线性分析？从 `MELCCAnalyzer` 开始
- 想理解ARX结构？从 `NeoAlzetteCore` 开始
- 想学习搜索算法？从 `ThresholdSearchFramework` 开始

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

# 编译核心工具
make -j$(nproc)

# 可选：构建演示程序
cmake -DNA_BUILD_DEMOS=ON ..
make -j$(nproc)
```

### 构建产物

#### 🔥 **核心分析工具**（编译后大小）
- **`analyze_medcp`** (239KB) - 标准MEDCP差分轨道搜索
- **`analyze_medcp_optimized`** (244KB) - **⚡ 优化版MEDCP分析器**（推荐）
- **`analyze_melcc`** (280KB) - 标准MELCC线性轨道搜索  
- **`analyze_melcc_optimized`** (300KB) - **⚡ 优化版MELCC分析器**（推荐）

#### 🔧 **辅助工具**
- **`complete_matsui_demo`** (384KB) - 完整Matsui Algorithm 1&2演示
- **`highway_table_build`** - 差分Highway表构建工具
- **`highway_table_build_lin`** - 线性Highway表构建工具  
- **`pddt_demo`** - 简单PDDT演示

#### ⚡ **核心库**
- **`libneoalzette.a`** (1.3MB) - 包含所有算法实现的静态库

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

#### 🏢 **计算机集群必要场景**

| 研究类型 | 轮数 | 权重上限 | 内存需求 | 计算节点 | 申请资源 |
|----------|------|----------|----------|----------|----------|
| **论文实验** | 6-8 | 25-35 | 4-8GB | 1-2节点 | 中等计算资源 |
| **深度分析** | 8-10 | 35-45 | 8-16GB | 2-4节点 | 高性能计算资源 |
| **突破研究** | 10-12+ | 45+ | 16-64GB | 4-16节点 | 超算中心资源 |

## 📋 **完整CLI使用指南**

### 🔥 **analyze_medcp_optimized - MEDCP差分轨道搜索（推荐）**

#### **完整语法**
```bash
./analyze_medcp_optimized R Wcap [highway.bin] [选项]
```

#### **参数说明**
- **`R`** - 搜索轮数 (4-12轮，个人电脑建议4-6轮)
- **`Wcap`** - 权重上限 (15-50，个人电脑建议15-25)
- **`highway.bin`** - 可选Highway表文件

#### **完整选项列表**

| 选项 | 参数 | 功能说明 | 使用示例 |
|------|------|----------|----------|
| `--start-hex` | `dA dB` | 起始差分状态(32位十六进制) | `--start-hex 0x1 0x0` |
| `--export` | `file.csv` | 导出搜索摘要 | `--export results.csv` |
| `--export-trace` | `file.csv` | 导出完整轨道路径 | `--export-trace trail.csv` |
| `--export-hist` | `file.csv` | 导出权重分布直方图 | `--export-hist histogram.csv` |
| `--export-topN` | `N file.csv` | 导出前N个最优结果 | `--export-topN 10 top10.csv` |
| `--k1` | `K` | var-var加法Top-K候选数(1-16) | `--k1 8` |
| `--k2` | `K` | var-const加法Top-K候选数(1-16) | `--k2 8` |
| `--threads` | `N` | 线程数(自动检测最优值) | `--threads 8` |
| `--fast-canonical` | 无 | 快速标准化(牺牲精度换速度) | `--fast-canonical` |

#### **使用示例**

**🟢 个人电脑入门级**：
```bash
# 最小验证（30秒内完成）
./analyze_medcp_optimized 4 15

# 基础分析（2-5分钟）
./analyze_medcp_optimized 4 20 --start-hex 0x1 0x0 --export basic.csv

# 参数理解（测试不同权重）
for w in {15..25}; do
  echo "Testing weight $w:"
  time ./analyze_medcp_optimized 4 $w
done
```

**🟡 个人电脑挑战级**：
```bash
# 多线程搜索（5-15分钟，需关闭其他程序）
./analyze_medcp_optimized 5 25 --threads 4

# 完整结果导出
./analyze_medcp_optimized 5 22 \
  --export summary.csv \
  --export-trace trail.csv \
  --export-hist histogram.csv
```

**🔴 集群专业级**：
```bash
# 高性能研究分析（需要申请集群资源）
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 16 --k1 8 --k2 8

# 大规模参数空间搜索
for start in 0x1 0x8000 0x80000000; do
  sbatch --mem=16G --time=24:00:00 \
    run_medcp.sh 8 35 $start 0x0
done
```

### 🔥 **analyze_melcc_optimized - MELCC线性轨道搜索（推荐）**

#### **完整语法**
```bash
./analyze_melcc_optimized R Wcap [选项]
```

#### **参数说明**
- **`R`** - 搜索轮数 (4-10轮，线性比差分更复杂)
- **`Wcap`** - 权重上限 (10-40，线性通常更严格)

#### **完整选项列表**

| 选项 | 参数 | 功能说明 | 使用示例 |
|------|------|----------|----------|
| `--start-hex` | `mA mB` | 起始线性掩码(32位十六进制) | `--start-hex 0x80000000 0x1` |
| `--export` | `file.csv` | 导出分析摘要 | `--export linear_results.csv` |
| `--export-trace` | `file.csv` | 导出最优线性轨道 | `--export-trace linear_trail.csv` |
| `--export-hist` | `file.csv` | 导出权重分布 | `--export-hist linear_hist.csv` |
| `--export-topN` | `N file.csv` | 导出前N个最优结果 | `--export-topN 5 top5.csv` |
| `--lin-highway` | `H.bin` | 线性Highway表文件 | `--lin-highway highway_lin.bin` |
| `--threads` | `N` | 线程数(自动检测最优值) | `--threads 6` |
| `--fast-canonical` | 无 | 快速标准化(牺牲精度换速度) | `--fast-canonical` |

#### **使用示例**

**🟢 个人电脑入门级**：
```bash
# 最小线性分析
./analyze_melcc_optimized 4 15

# 指定掩码搜索
./analyze_melcc_optimized 4 18 --start-hex 0x1 0x0 --export linear_basic.csv
```

**🟡 个人电脑标准级**：
```bash
# 多线程线性搜索
./analyze_melcc_optimized 5 20 --threads 4

# 使用Highway表加速
./analyze_melcc_optimized 5 22 --lin-highway highway_lin.bin
```

**🔴 集群研究级**：
```bash
# 高精度线性分析
./analyze_melcc_optimized 8 30 highway_lin.bin --threads 16

# 大规模线性掩码搜索  
for mask in 0x1 0x80000000 0xFFFF0000; do
  sbatch --mem=32G --time=48:00:00 \
    run_melcc.sh 8 28 $mask 0x0
done
```

### 🔧 **辅助工具使用**

#### **Highway表构建工具**
```bash
# 构建差分Highway表（一次性，多次复用）
./highway_table_build highway_diff.bin 10
# 时间：1-3小时，大小：500MB-2GB

# 构建线性Highway表
./highway_table_build_lin highway_lin.bin 8  
# 时间：30分钟-2小时，大小：200MB-1GB

# 使用Highway表进行加速搜索
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 8
```

#### **论文算法演示工具**
```bash
# 快速验证Algorithm 1&2实现正确性（推荐用于个人电脑）
./complete_matsui_demo --quick

# 完整演示highways/country roads策略（需要更多资源）
./complete_matsui_demo --full
```

## ⚠️ **关键使用注意事项**

### **个人电脑使用限制**

**✅ 推荐在个人电脑上尝试**：
```bash
# 验证工具正常（必定快速完成）
./analyze_medcp_optimized 4 15

# 学习参数影响（内置资源估算）
./analyze_medcp_optimized 4 20 --start-hex 0x1 0x0

# 理解输出格式
./analyze_medcp_optimized 4 18 --export test.csv --export-trace trail.csv
```

**❌ 不要在个人电脑上尝试**：
```bash
# 这些会导致内存不足或长时间卡死：
./analyze_medcp_optimized 8 35     # ❌ 8轮需要集群
./analyze_medcp_optimized 6 45     # ❌ 高权重需要集群  
./analyze_melcc_optimized 7 30     # ❌ 7轮线性需要集群
```

### **智能资源估算**

**新工具内置智能资源估算功能**：
```bash
# 运行任何分析前，工具会自动显示：
# ✓ 内存需求估算
# ✓ 时间需求估算  
# ✓ 推荐线程数
# ✓ 个人电脑适用性判断

# 示例输出：
# Resource Estimate:
# - Memory: ~1,234 MB
# - Time: ~5m 30s
# - Personal Computer Suitable: ✓ YES
```

### **集群资源申请指南**

#### **SLURM作业脚本示例**

**中等研究任务**：
```bash
#!/bin/bash
#SBATCH --job-name=neoalzette_analysis
#SBATCH --nodes=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=24:00:00

# 执行搜索
./analyze_medcp_optimized 8 35 highway_diff.bin \
  --threads 16 \
  --export results_${SLURM_JOB_ID}.csv
```

**重型研究任务**：
```bash
#!/bin/bash
#SBATCH --job-name=breakthrough_search
#SBATCH --nodes=4
#SBATCH --cpus-per-task=32  
#SBATCH --mem=128G
#SBATCH --time=168:00:00

# 分布式大规模搜索
srun ./analyze_medcp_optimized 10 45 highway_diff.bin \
  --threads 32 \
  --export-trace breakthrough_trail.csv
```

## 📊 **实际性能期望**

### **个人电脑能完成的任务**：
- ✅ **算法验证**：确认工具正常工作
- ✅ **参数学习**：理解不同设置的影响
- ✅ **方法理解**：掌握密码分析基本概念
- ✅ **小规模实验**：获得基础分析数据
- ✅ **配置验证**：内置工具验证参数合理性

### **个人电脑无法完成的任务**：
- ❌ **研究级分析**：发现新的密码学结果
- ❌ **文献对比**：与已发表结果的验证
- ❌ **完整评估**：新算法的安全性评估
- ❌ **突破性发现**：在顶级会议发表的研究

### **集群计算的价值**：
- 🎯 **真正的密码学研究**：发现新轨道、挑战安全性声明
- 📈 **大规模验证**：统计显著的实验结果
- 🔬 **方法创新**：开发新的分析技术和攻击方法

## 🚀 **新架构特性亮点**

### **1. 智能化用户体验**
- **自动资源估算**：运行前预测内存和时间需求
- **配置验证**：自动检查参数合理性，给出警告和建议
- **进度监控**：实时显示搜索进度和性能统计
- **错误诊断**：提供详细的错误信息和解决建议

### **2. 高性能优化**
- **并行搜索框架**：工作窃取队列，最大化CPU利用率
- **缓存友好设计**：优化数据布局，减少内存访问延迟
- **算法改进**：基于最新论文的优化实现
- **模板优化**：编译时优化，运行时零开销

### **3. 研究级功能**
- **完整Matsui Algorithm 2**：包含highways/country roads策略
- **Wallén优化自动机**：预计算状态转换，O(1)查询
- **Highway表技术**：O(1)后缀下界查询，显著剪枝
- **多格式导出**：CSV、轨道、直方图、TopN结果

## 实验验证建议

```bash
# 第1步：验证新架构工作正常
./complete_matsui_demo --quick
# 预期：3-5秒完成，验证所有类正常工作

# 第2步：理解资源估算功能
./analyze_medcp_optimized 4 15
# 预期：显示详细的资源估算和配置验证

# 第3步：测试导出功能
./analyze_medcp_optimized 4 18 \
  --start-hex 0x1 0x0 \
  --export summary.csv \
  --export-trace trail.csv
# 预期：生成正确格式的CSV文件

# 第4步：性能基准测试
time ./analyze_medcp_optimized 4 20 --threads 1
time ./analyze_medcp_optimized 4 20 --threads 4  
# 预期：观察多线程性能提升
```

## 📚 学习资源导引

### **推荐学习路径**
```
第1步：工具验证 → ./complete_matsui_demo --quick
第2步：架构理解 → 学习5大核心类的设计
第3步：算法学习 → 从MEDCPAnalyzer开始理解差分分析
第4步：实践应用 → 测试不同参数配置
第5步：高级研究 → 申请集群资源进行深度分析
```

### **重要文档索引**
- **快速上手**: 本README的CLI使用指南
- **算法理解**: `PAPERS_COMPLETE_ANALYSIS_CN.md` (25,000+字深度分析)
- **设计对比**: `ALZETTE_VS_NEOALZETTE.md` (原始vs扩展设计)
- **模块化设计**: `NEW_MODULE_DESIGN.md` (新架构设计说明)

## 许可证和引用

本项目采用 **GNU General Public License v3.0** 许可证。

```bibtex
@inproceedings{miqcp2022,
  title={A MIQCP-Based Automatic Search Algorithm for Differential-Linear Trails of ARX Ciphers},
  author={Guangqiu Lv and Chenhui Jin and Ting Cui},
  year={2022}
}

@inproceedings{alzette2020,
  title={Alzette: A 64-Bit ARX-box},  
  author={Christof Beierle and Alex Biryukov and others},
  booktitle={Annual International Cryptology Conference},
  pages={419--448},
  year={2020}
}
```

## 🎯 **总结**

NeoAlzette密码分析工具集现在提供了：
- ✅ **现代模块化架构**：5个核心类，清晰的职责分离
- ✅ **面向对象设计**：C++20标准，工程最佳实践
- ✅ **智能用户体验**：自动资源估算，配置验证，进度监控
- ✅ **高性能优化**：并行框架，缓存优化，算法改进
- ✅ **完整功能覆盖**：从基础验证到突破性研究的全套工具

**新架构特别适合**：
- 🎓 **密码学研究者**：开展ARX密码安全性分析
- 👨‍🎓 **研究生和博士生**：学习现代密码分析技术和软件工程
- 🏢 **产业界研究员**：评估ARX设计的安全性  
- 📚 **教育工作者**：教授高级密码学概念和算法实现
- 👨‍💻 **软件工程师**：学习高性能C++和算法优化技术

---

**注意**：本工具仅供学术研究和教育用途，请勿用于恶意攻击或非法活动。