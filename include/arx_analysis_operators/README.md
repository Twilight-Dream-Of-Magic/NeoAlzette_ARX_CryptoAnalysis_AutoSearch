# ARX 分析算子 · 低层优化实现（中英双语 README）

ARX Analysis Operators · Low-Level Optimized Implementations (ZH/EN)

* 更新时间 Updated: **2025-10-13**
* 适配位宽 Supported word sizes: **n = 8/16/32/64**（具体见各函数签名）

---

## 目标 / Goal

* **ZH**：本目录提供 ARX 密码分析中“模加 ⊞/⊟、旋转、异或”相关的**差分**与**线性**算子的**最快可读实现**，覆盖“变量-变量”“变量-常量”两类场景，并给出精确或对数级（SWAR）复杂度。
* **EN**: This folder ships **readable yet fast** operators for ARX analysis (Addition/Rotation/XOR), covering **differential** and **linear** cases for **var-var** and **var-const**, with exact or bit-parallel (SWAR) complexities.

---

## 📁 头文件一览 / Header Inventory

* `bitvector_ops.hpp`
* `differential_xdp_add.hpp`
* `differential_addconst.hpp`
* `differential_optimal_gamma.hpp`
* `linear_cor_add_logn.hpp`
* `linear_cor_addconst.hpp`
* `linear_cor_add.hpp.DEPRECATED`  ← **已弃用 / deprecated**

---

## 🧪 差分算子 / Differential Operators

### 1) `differential_xdp_add.hpp` — XOR 差分模加（变量-变量）

**XDP of modular addition (var-var)**

* **参考 / Ref**: Lipmaa & Moriai, *Efficient Algorithms for Computing Differential Properties of Addition* (FSE 2001)
* **算法 / Algo**: LM-2001（ψ 约束 + 前缀运算）
* **复杂度 / Complexity**:

  * 位模型 bit-model：Θ(log n)
  * 固定机字宽 fixed word (e.g. 32/64-bit, SWAR/POPCNT)：≈ **O(1)**
* **API（核心）/ Core API**:

  * `int xdp_add_lm2001(uint32_t α, uint32_t β, uint32_t γ)` → 返回权重 *w = −log₂DP*；`-1` 表示不可能
  * `double xdp_add_probability(uint32_t α, uint32_t β, uint32_t γ)` → 返回 *DP*
  * `int xdp_add_lm2001_n(uint64_t α, uint64_t β, uint64_t γ, int n)` → 指定位宽
* **说明 / Notes**: 实现中将论文的 *eq/ψ* 条件写成无分支的位并行形式，便于内联与常量折叠。

---

### 2) `differential_addconst.hpp` — 常量加法差分（变量-常量）

**Differential for x ⊞ K (var-const)**

* **参考 / Ref**: Azimi et al., *A Bit-Vector Differential Model for the Modular Addition by a Constant* (DCC 2022 / Asiacrypt’20 扩展)
* **算法 / Algos**:

  * **Algorithm 1 (BvWeight)**：**O(log² n)** 位向量近似，输出 4-bit 小数精度的权重（更快，默认）
  * （文内注释含）Machado/逐位精确迭代 **O(n)**（可切换为基准验证）
* **API / Core API**:

  * `int diff_addconst_bvweight(uint32_t Δx, uint32_t K, uint32_t Δz)` → 近似 *w ≈ −log₂DP*（含 1/16 精度）
  * `double diff_addconst_probability(uint32_t Δx, uint32_t K, uint32_t Δz)` → 近似 *DP*
  * `int diff_subconst_bvweight(...)` → 对应 *x ⊟ K*
* **说明 / Notes**: BvWeight 利用 `Carry/Rev/RevCarry/HW/LZ/ParallelLog/ParallelTrunc` 等 SWAR 原语在 **O(log² n)** 内给出二进制对数概率，适合自动化搜索与约束编码。

---

### 3) `differential_optimal_gamma.hpp` — 最优输出差分 γ 搜索

**Find optimal γ for (α,β) in addition**

* **参考 / Ref**: LM-2001 Algorithm 4（find_optimal_gamma）
* **复杂度 / Complexity**: **Θ(log n)**
* **API**: `uint32_t find_optimal_gamma(uint32_t α, uint32_t β, int n=32)`
* **用途 / Use**: 给定 (α,β)，快速返回使 DP 最大的 γ，便于分支定界或 MILP/SAT 提示。

---

## 📈 线性算子 / Linear-Correlation Operators

### 4) `linear_cor_add_logn.hpp` — 模加线性相关度 · **Θ(log n)**（变量-变量）

**Linear correlation of x ⊞ y (var-var) in Θ(log n)**

* **参考 / Ref**: Wallén, *Linear Approximations of Addition Modulo 2ⁿ* (FSE 2003)
* **关键 / Key**: 利用 **Common-Prefix Mask (cpm)** 与 Wallén Thm.2 / Cor.1，将相关度权重化为 **HW(cpm)**，并以并行前缀（SWAR）在 **Θ(log n)** 时间求得。
* **API / Core API**:

  * `int    linear_cor_add_wallen_logn(uint32_t u, uint32_t v, uint32_t w)` → *Lw = −log₂|corr|*（整数权重）
  * `double linear_cor_add_value_logn(uint32_t u, uint32_t v, uint32_t w)` → `|corr|`
* **说明 / Notes**: 与传统逐位 DP/相关转移 O(n) 相比，本实现为 **对数时间**（位并行）版本，适合大规模穷举或在线评估。

---

### 5) `linear_cor_addconst.hpp` — 模加线性相关度（变量-常量 & 变量-变量，**精确 O(n)** 基准）

**Exact linear correlation for x ⊞ K (var-const) and x ⊞ y (var-var), O(n) baseline**

* **参考 / Ref**: Wallén 2003（按位**2×2 载波状态转移矩阵**重构）
* **平均因子 / Averaging factor**（关键约定，避免歧义）:

  * **var-const**: 仅 *x_i* 随机 → **1/2 = 0.5**
  * **var-var**: *x_i,y_i* 均随机 → **1/4 = 0.25**
* **API / Core API**（返回结构含 *corr* 与 *weight*）:

  * `LinearCorrelation corr_add_x_plus_const32(uint32_t α, uint32_t β, uint32_t K, int n=32)`
  * `LinearCorrelation corr_add_x_minus_const32(...)`；`...64` 同理
  * `LinearCorrelation corr_add_varvar32(uint32_t α, uint32_t β, uint32_t γ, int n=32)`；`...64` 同理
  * 结构体：`struct LinearCorrelation { double correlation; double weight; /* weight = −log₂|corr| */ }`
* **用途 / Use**: 作为**精确基线**与单测“金标准”；同时给出与 `linear_cor_add_logn.hpp` 的**一致性校验**。

---

## 🧩 位向量工具 / Bit-Vector Utilities

### 6) `bitvector_ops.hpp` — SWAR 原语集合

**Carry/Rev/RevCarry/HW/LZ/ParallelLog/ParallelTrunc/compute_cpm_logn/eq…**

* **作用 / Purpose**: 为 BvWeight 与 Wallén 对数算法提供 **O(log n)** 级别的位并行基元；所有函数均为内联、无分支或低分支，便于编译器优化。
* **典型函数 / Typical functions**:

  * `uint32_t HW(x)`，`uint32_t Rev(x)`，`uint32_t Carry(x,y)`，`uint32_t RevCarry(x,y)`
  * `uint32_t ParallelLog(x, sep)`，`uint32_t ParallelTrunc(x, sep)`
  * `uint32_t compute_cpm_logn(u, eq_mask)`，`uint32_t eq(a,b)`

---

## 🧯 弃用说明 / Deprecation

* `linear_cor_add.hpp.DEPRECATED`：早期 **O(n)** 线性相关实现，已由 **`linear_cor_add_logn.hpp`**（对数时间）与 **`linear_cor_addconst.hpp`**（精确基线）共同取代。
  **请迁移 / Please migrate**。

---

## 🎯 使用示例 / Usage Examples

```cpp
#include "arx_analysis_operators/bitvector_ops.hpp"
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/linear_cor_add_logn.hpp"
#include "arx_analysis_operators/linear_cor_addconst.hpp"

using namespace neoalz::arx_operators;

// 1) 差分 · 变量-变量（LM-2001）
int w_xdp = xdp_add_lm2001(0x01, 0x01, 0x02);     // −log2 DP；-1 表示不可能
double dp   = xdp_add_probability(0x01, 0x01, 0x02);

// 2) 差分 · 变量-常量（BvWeight 近似）
int w_addc = diff_addconst_bvweight(0x01, 0x05, 0x04);
double dp_c = diff_addconst_probability(0x01, 0x05, 0x04);

// 3) 求最优 γ（LM-2001 Algorithm.4）
uint32_t gamma_star = find_optimal_gamma(0x01, 0x01, 32);

// 4) 线性 · 变量-变量（Wallén · Θ(log n)）
int    lw_vv  = linear_cor_add_wallen_logn(0x01, 0x01, 0x01); // −log2|corr|
double cor_vv = linear_cor_add_value_logn(0x01, 0x01, 0x01);  // |corr|

// 5) 线性 · 基准精确（矩阵转移 O(n)）
auto lc_vc = corr_add_x_plus_const32(0x01, 0x01, 0x05); // var-const
// lc_vc.correlation ∈ [-1,1], lc_vc.weight = −log2(|corr|)

auto lc_vv = corr_add_varvar32(0x01, 0x01, 0x01);       // var-var（精确基线）
```

---

## 📊 复杂度与精确度对照 / Complexity & Accuracy

| 算子 / Operator          | 场景 / Case | 算法 / Method        | 复杂度 / Complexity   | 精确度 / Accuracy  |
| ---------------------- | --------- | ------------------ | ------------------ | --------------- |
| XDP⁺ ⊞（LM-2001）        | var-var   | ψ + 前缀（SWAR）       | Θ(log n)（机字宽≈O(1)） | **精确**          |
| XDP⁺ ⊞ K（BvWeight）     | var-const | Bit-Vector（DCC’22） | **O(log² n)**      | **近似**（4bit 小数） |
| 线性 corr ⊞（Wallén-logn） | var-var   | cpm + HW           | **Θ(log n)**       | **精确**（权重/数值）   |
| 线性 corr ⊞ K（矩阵）        | var-const | 2×2 载波矩阵           | **O(n)**           | **精确**          |
| 线性 corr ⊞（矩阵）          | var-var   | 2×2 载波矩阵           | **O(n)**           | **精确**          |

> 说明：在 **固定 32/64-bit** 机型上，`Θ(log n)` 多以少量 SWAR 层级实现，实践中与 **O(1)** 无异。

---

## ✅ 一致性与约定 / Conventions & Consistency

* **差分权重 / Differential weight**: `w = −log₂(DP)`；`-1` 表示不可能（impossible）。
* **线性权重 / Linear weight**: `Lw = −log₂(|corr|)`；返回结构同时提供 `correlation`。
* **平均因子 / Averaging factors**：`linear_cor_addconst.hpp` 中明确区分

  * var-const：**0.5**，var-var：**0.25**（与 Wallén 矩阵定义一致）。
* **位宽 / Word size**：所有 API 提供 32/64 版本或 `n` 参数；超定长请使用 `*_n` 接口或移植模板化版本。

---

## 🧷 构建与依赖 / Build & Deps

* **C++20**（或更高），支持 `__builtin_popcount(ll)` / `std::popcount` 与常见内联函数。
* 无第三方依赖；可在编译期内联优化；禁用 AVX/SIMD 以保持跨平台**可读性**与**确定性**。

---

## 🔁 变更摘要 / Changelog (2025-10-13)

* 新增：`linear_cor_add_logn.hpp`（**Θ(log n)** 线性相关算法，取代旧 `linear_cor_add.hpp`）。
* 新增：`linear_cor_addconst.hpp` 精确基准（**O(n)**），统一 **0.25/0.5** 因子约定。
* 差分：`differential_addconst.hpp` 默认启用 **BvWeight O(log² n)**，并保留注释指向精确逐位法。
* 工具：`bitvector_ops.hpp` 完整 SWAR 原语，支撑 cpm/Carry/RevCarry 等对数级操作。
