# NeoAlzette 密码分析工具集

[English](README_EN.md) | 中文

## 项目概述

本项目实现了针对 ARX 密码（Addition, Rotation, XOR）的高效密码分析工具集，特别专注于 NeoAlzette 64位 ARX-box 的差分和线性密码分析。项目基于最新的密码分析理论和算法，提供了完整的自动化搜索和分析框架。

### 核心算法

- **MEDCP (Modular Addition by Constant - Differential Property)**：模加常数的差分性质分析
- **MELCC (Modular Addition Linear Cryptanalysis with Correlation)**：基于相关性的模加线性密码分析

## 算法背景

### ARX 密码结构

ARX 密码基于三种基本运算：
- **Addition (⊞)**：模 2^n 加法
- **Rotation (≪, ≫)**：循环位移  
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
- **非线性函数**：F(x) = (x ≪ 31) ⊕ (x ≪ 17)
- **线性扩散层**：L₁, L₂ 提供分支数保证
- **轮常数注入**：16个预定义轮常数

## 项目结构

```
├── include/                 # 头文件
│   ├── neoalzette.hpp      # NeoAlzette 算法实现
│   ├── lm_fast.hpp         # Lipmaa-Moriai 快速枚举
│   ├── wallen_fast.hpp     # Wallén 线性分析
│   ├── highway_table.hpp   # Highway 后缀下界表
│   └── ...
├── src/                    # 源文件
│   ├── analyze_medcp.cpp   # MEDCP 差分分析器
│   ├── analyze_melcc.cpp   # MELCC 线性分析器
│   ├── main_diff.cpp       # 差分密码分析主程序
│   ├── main_lin.cpp        # 线性密码分析主程序
│   └── ...
├── papers/                 # 相关论文
└── papers_txt/            # 论文文本版本
```

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

- `analyze_medcp` - MEDCP 差分轨道搜索
- `analyze_melcc` - MELCC 线性轨道搜索  
- `diff_search` - 通用差分搜索工具
- `lin_search` - 通用线性搜索工具
- `highway_table_build*` - Highway 表构建工具

## 使用方法

### MEDCP 差分分析

```bash
# 基本用法：搜索 R 轮差分轨道，权重上限 Wcap
./analyze_medcp R Wcap [highway.bin] [选项]

# 示例：搜索8轮差分，权重上限30
./analyze_medcp 8 30

# 使用自定义起始差分（十六进制）
./analyze_medcp 8 30 --start-hex 0x1 0x0

# 使用 Highway 表加速（预计算后缀下界）
./analyze_medcp 8 30 highway_diff.bin

# 导出搜索结果到 CSV
./analyze_medcp 8 30 --export results.csv

# 调整搜索参数
./analyze_medcp 8 30 --k1 8 --k2 8  # Top-K候选数
```

### MELCC 线性分析

```bash
# 基本用法：搜索 R 轮线性轨道，权重上限 Wcap  
./analyze_melcc R Wcap [选项]

# 示例：搜索8轮线性近似，权重上限25
./analyze_melcc 8 25

# 使用自定义起始掩码
./analyze_melcc 8 25 --start-hex 0x80000000 0x1

# 使用线性 Highway 表
./analyze_melcc 8 25 --lin-highway highway_lin.bin

# 导出分析结果
./analyze_melcc 8 25 --export linear_results.csv
```

### Highway 表构建

```bash
# 构建差分 Highway 表（可选，用于加速搜索）
./highway_table_build output_diff.bin [max_rounds]

# 构建线性 Highway 表  
./highway_table_build_lin output_lin.bin [max_rounds]
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

## 性能特征

### 计算复杂度

- **差分枚举**：平均 << 2^n（由于前缀不可行性剪枝）
- **线性相关计算**：O(log n) 时间复杂度
- **Highway 查询**：O(1) 时间，O(n) 空间
- **阈值搜索**：指数级最坏情况，实际性能依赖剪枝效率

### 资源消耗

| 分析类型 | 内存需求 | CPU 时间 | 适用轮数 |
|----------|----------|----------|----------|
| MEDCP 6-8轮 | ~500MB | 分钟级 | 轻量级 |
| MEDCP 9-12轮 | ~2GB | 小时级 | 中等 |
| MELCC 6-8轮 | ~1GB | 分钟级 | 轻量级 |
| MELCC 9-12轮 | ~4GB | 小时级 | 重型 |

### 优化建议

1. **预计算 Highway 表**减少重复计算
2. **调整权重上限**平衡搜索深度与时间
3. **使用集群**并行处理不同参数组合
4. **监控内存使用**避免大型搜索任务内存溢出

## 理论结果

根据论文实验结果，本工具在以下方面取得突破：

### 差分分析结果

- **NeoAlzette 8轮**：最优差分概率 2^{-32}
- **SPECK32 11轮**：相关性 -2^{-17.09}  
- **SPECK64 12轮**：相关性 -2^{-20.46}

### 线性分析结果

- **算法改进**：相比之前方法，计算复杂度降低约 8 倍
- **自动化搜索**：首次实现任意输出掩码的自动 DL 轨道搜索
- **MIQCP 转换**：将矩阵乘法链转换为可求解的优化问题

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