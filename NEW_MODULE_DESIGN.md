# 新模块化设计方案

## 设计理念
将22个小头文件整合成5个大的类封装模块，每个模块都有清晰的职责和面向对象的设计。

## 新模块结构

### 1. MEDCPAnalyzer - 差分分析器类
**文件**: `include/medcp_analyzer.hpp` + `src/medcp_analyzer.cpp`
**整合内容**:
- lm_fast.hpp (Lipmaa-Moriai算法)
- lb_round_full.hpp (完整轮差分下界)
- highway_table.hpp (差分Highway表)
- suffix_lb.hpp (差分后缀下界)
- diff_add_const.hpp (模加常数差分)

### 2. MELCCAnalyzer - 线性分析器类
**文件**: `include/melcc_analyzer.hpp` + `src/melcc_analyzer.cpp`
**整合内容**:
- wallen_fast.hpp, wallen_optimized.hpp (Wallén算法)
- lb_round_lin.hpp (完整轮线性下界)
- highway_table_lin.hpp (线性Highway表)
- suffix_lb_lin.hpp (线性后缀下界)
- mask_backtranspose.hpp (线性掩码传播)

### 3. NeoAlzetteCore - NeoAlzette核心类
**文件**: `include/neoalzette_core.hpp` + `src/neoalzette_core.cpp`
**整合内容**:
- neoalzette.hpp (NeoAlzette核心)
- neoalz_lin.hpp (NeoAlzette线性层)

### 4. ThresholdSearchFramework - 阈值搜索框架类
**文件**: `include/threshold_search_framework.hpp` + `src/threshold_search_framework.cpp`
**整合内容**:
- threshold_search.hpp, threshold_search_optimized.hpp
- matsui_complete.hpp (完整Matsui算法)
- state_optimized.hpp (优化状态表示)

### 5. UtilityTools - 工具类集合
**文件**: `include/utility_tools.hpp` + `src/utility_tools.cpp`
**整合内容**:
- pddt.hpp, pddt_optimized.hpp
- canonicalize.hpp (状态标准化)
- trail_export.hpp (轨道导出)
- lb_round.hpp (基础轮下界)

## 设计原则
- 每个类都有清晰的单一职责
- 模板函数保留在.hpp文件中
- 非模板函数实现移到.cpp文件中
- 使用RAII和现代C++设计模式
- 提供简洁的公共接口