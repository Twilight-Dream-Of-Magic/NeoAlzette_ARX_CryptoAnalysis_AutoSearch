# ARX 分析算子（以“模加”为核心）/ ARX Analysis Operators (Addition-Centric)

* 更新时间 Updated: **2026-01-16**
* 主要命名空间 / Namespace:
  - `TwilightDream::arx_operators`（对外算子 / public operators）
  - `TwilightDream::bitvector`（位向量/SWAR primitives；当前实现在 `differential_addconst.hpp` 内）

---

## 目标 / Goal

- **ZH**：本目录提供 ARX 密码分析里“模加 ⊞/⊟”相关的**差分**与**线性相关度**算子实现，强调**可审计**（注释对齐论文符号）与**可直接用于自动化搜索**（返回 weight / feasibility），并保留若干位向量 primitives 便于将来做 SMT/bit-vector 编码。
- **EN**: This folder provides **auditable** and **search-friendly** operators for ARX analysis, focused on modular addition/subtraction under XOR differentials and linear correlations, with clear API contracts and feasibility/weight conventions.

---

## 📁 头文件一览 / Header Inventory (current)

- `differential_xdp_add.hpp`：XOR 差分下的 var-var 加法 DP⁺ / weight（LM-2001）
- `differential_optimal_gamma.hpp`：给定 (α,β) 构造最优 γ（LM-2001 Algorithm 4）
- `differential_addconst.hpp`：var-const 加/减的差分（精确 count/DP/weight + BvWeight^κ 近似）
- `linear_correlation_add_logn.hpp`：Wallén 风格的“对数算法”实现（当前实现以 32-bit 为主）
- `linear_correlation_addconst.hpp`：O(n) 精确线性相关（2×2 carry-state transfer matrices；var-const + var-var）
- `math_util.hpp`：小工具（目前提供 `neg_mod_2n<T>(k,n)`）
- `modular_addition_ccz.hpp`：Addition mod \(2^n\) 的 CCZ 等价与显式差分/线性公式算子（Schulte-Geers）

> 注：README 只描述 **目录中实际存在且可 include 的文件**；旧文件名（如 `bitvector_ops.hpp` / `linear_cor_addconst.hpp`）已不再使用。

---

## 🧪 差分算子 / Differential Operators

### 1) `differential_xdp_add.hpp` — XOR 差分模加（变量-变量，LM-2001）

- **定义 / Definition**：\(z=x ⊞ y\)，\(z'=(x⊕α) ⊞ (y⊕β)\)，\(γ=z⊕z'\)，计算 \(DP^+(α,β↦γ)\) 与 \(w=-\log_2 DP^+\)。
- **API（核心）/ Core API**（均在 `TwilightDream::arx_operators`）：
  - `int xdp_add_lm2001(uint32_t alpha, uint32_t beta, uint32_t gamma)`：返回整数 weight `w`；不可能返回 `-1`
  - `int xdp_add_lm2001_n(uint32_t alpha, uint32_t beta, uint32_t gamma, int n)`：支持 `1..32` 位（输入会 mask 到低 n 位）
  - `double xdp_add_probability(uint32_t alpha, uint32_t beta, uint32_t gamma)`：返回 `DP^+`（不可能返回 `0.0`）
  - `bool is_xdp_add_possible(uint32_t alpha, uint32_t beta, uint32_t gamma)`

### 2) `differential_optimal_gamma.hpp` — 最优输出差分 γ（LM-2001 Algorithm 4）

- **用途 / Use**：给定 (α,β) 直接构造使 \(DP^+\) 最大的 γ（避免枚举 γ）。
- **API**（`TwilightDream::arx_operators`）：
  - `uint32_t find_optimal_gamma(uint32_t alpha, uint32_t beta, int n=32)`
  - `std::pair<uint32_t,int> find_optimal_gamma_with_weight(uint32_t alpha, uint32_t beta, int n=32)`
    - `weight` 通过 `xdp_add_lm2001(_n)` 计算；不可能时为 `-1`

### 3) `differential_addconst.hpp` — 常量加/减的 XOR 差分（变量-常量）

- **问题 / Problem**：\(y=x ⊞ a\)（或 \(y=x ⊟ a\)），输入差分 \(Δx\)，输出差分 \(Δy\)。
- **三类输出 / Three kinds of outputs**（同一文件内）：
  - **精确 count/DP/weight（O(n)）**：基于 carry-pair 4-state 逐位 DP，返回解数 `count` 与精确 `DP` / `weight`
  - **闭式 weight（double）**：按 Azimi Lemma 3/4/5 的 \(\sum \log_2(\pi_i)\) 形式计算（不可行返回 `+∞`）
  - **BvWeight^κ（Qκ fixed-point，近似）**：返回 `uint32_t`，低 `κ` bits 为小数；不可行返回 `0xFFFFFFFF`

- **API（常用）/ Common API**（`TwilightDream::arx_operators`）：
  - **可行性**：`bool is_diff_addconst_possible_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`
  - **精确**：
    - `uint64_t diff_addconst_exact_count_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`
    - `double   diff_addconst_exact_probability_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`
    - `double   diff_addconst_exact_weight_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`（不可能返回 `+∞`）
    - `int      diff_addconst_exact_weight_ceil_int_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`（不可能返回 `-1`）
    - 32-bit wrappers：`diff_addconst_exact_count / diff_addconst_exact_probability / diff_addconst_exact_weight`
  - **闭式（log2π）**：`double diff_addconst_weight_log2pi_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`
    - 32-bit wrapper：`diff_addconst_weight_log2pi`
  - **近似（BvWeight^κ）**：
    - `uint32_t diff_addconst_bvweight_fixed_point_n(uint32_t dx, uint32_t a, uint32_t dy, int n, int fraction_bit_count)`
    - `uint32_t diff_addconst_bvweight_q4_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`（κ=4）
    - 32-bit wrappers：`diff_addconst_bvweight_fixed_point / diff_addconst_bvweight_q4`
  - **与现有“只吃 int 权重”的搜索框架对接**：
    - `int diff_addconst_bvweight(uint32_t dx, uint32_t a, uint32_t dy)`：**返回精确 weight 的上取整**（即 `diff_addconst_exact_weight_ceil_int_n(...,32)`）
    - `int diff_addconst_bvweight_q4_int_ceil(...)`：仅用于“近似 Q4 → int”的对照/实验（仍是近似）
  - **近似概率（由 Q4 weight 换算）**：
    - `double diff_addconst_probability(uint32_t dx, uint32_t a, uint32_t dy)`
    - `double diff_subconst_probability(uint32_t dx, uint32_t a, uint32_t dy)`
  - **减法**：`diff_subconst_*` 系列通过 `neg_mod_2n` 转换到 add-const

- **位向量 primitives（在同一头文件内）/ Bit-vector primitives**（`TwilightDream::bitvector`）：
  - `HammingWeight / Rev / Carry / RevCarry / LeadingZeros / ParallelLog / ParallelTrunc`
  - 同名的 `*_n` 版本（支持 `n!=32` 的 domain）

> 重要：当前工程实现为了“可审计/可单测”，在 BvWeight^κ 的计算上采用逐链展开（整体仍是 **O(n)** 量级）。论文中的纯 bit-vector（含 ParallelLog/ParallelTrunc）写法在本文件中作为 primitives 保留，便于未来回切到 SMT-friendly 的表达式形式。

---

## 📈 线性算子 / Linear-Correlation Operators

### 4) `linear_correlation_add_logn.hpp` — Wallén 风格对数算法（变量-变量）

- **核心 / Key idea**：将相关度可行性与权重归约到 carry-support 向量 / cpm（Common Prefix Mask）之上；不可行返回 `-1`。
- **API（当前实现为 32-bit）/ API (current implementation is 32-bit focused)**（`TwilightDream::arx_operators`）：
  - `int    internal_addition_wallen_logn(uint32_t u, uint32_t v, uint32_t w)`：返回 `Lw = -log2(|corr|)`（不可行返回 `-1`）
  - `double linear_correlation_add_value_logn(uint32_t u, uint32_t v, uint32_t w)`：返回 `|corr|`（不可行返回 `0.0`；当前实现返回**绝对值**）
  - 另外暴露 `compute_cpm_*` / `eq(x,y)` 等辅助函数（用于对照论文与回归测试）

### 5) `linear_correlation_addconst.hpp` — 精确线性相关（var-const + var-var，O(n) 基线）

- **方法 / Method**：每 bit 构造 2×2 carry-state transfer matrix，逐位左乘累积得到最终相关度 `corr`；再转为线性 weight：
  - `Lw = -log2(|corr|)`，当 `corr==0` 时为 `+∞`
- **平均因子 / Averaging factor（非常关键）**：
  - **var-const**：只平均 `x_i` 两种情况 ⇒ `1/2`
  - **var-var**：平均 `(x_i,y_i)` 四种情况 ⇒ `1/4`
- **对外封装 API / Public wrappers**（`TwilightDream::arx_operators`）：
  - `LinearCorrelation linear_x_modulo_plus_const32(uint32_t alpha, uint32_t K, uint32_t beta, int nbits=32)`
  - `LinearCorrelation linear_x_modulo_minus_const32(uint32_t alpha, uint32_t C, uint32_t beta, int nbits=32)`
  - `LinearCorrelation linear_x_modulo_plus_const64(uint64_t alpha, uint64_t K, uint64_t beta, int nbits=64)`
  - `LinearCorrelation linear_x_modulo_minus_const64(uint64_t alpha, uint64_t C, uint64_t beta, int nbits=64)`
  - `LinearCorrelation linear_add_varvar32(uint32_t alpha, uint32_t beta, uint32_t gamma, int nbits=32)`
  - `LinearCorrelation linear_add_varvar64(uint64_t alpha, uint64_t beta, uint64_t gamma, int nbits=64)`
  - `struct LinearCorrelation { double correlation; double weight; bool is_feasible() const; }`

### 6) `modular_addition_ccz.hpp` — CCZ 等价与显式公式（差分 + 线性）

- **定位 / Positioning**：给出 addition mod \(2^n\) 的**显式差分概率**（Theorem 3）与**显式 Walsh/相关系数**（Theorem 4）形式；适合作为“公式基准/交叉验证”，也可直接用于搜索中的可行性与权重计算。
- **差分 API / Differential API**（`TwilightDream::arx_operators`）：
  - `double differential_probability_add_ccz_value(uint64_t alpha, uint64_t beta, uint64_t gamma, int n)`：返回 \(2^{-k}\) 或 `0.0`
  - `std::optional<int> differential_probability_add_ccz_weight(uint64_t alpha, uint64_t beta, uint64_t gamma, int n)`：返回 \(k\) 或 `nullopt`
  - `bool differential_equation_add_ccz_solvable(uint64_t a, uint64_t b, uint64_t d, int n)`
- **线性 API / Linear API**（`TwilightDream::arx_operators`）：
  - `std::optional<double> linear_correlation_add_ccz_value(uint64_t u, uint64_t v, uint64_t w, int n)`：返回 \(±2^{-k}\) 或 `nullopt`
  - `std::optional<int> linear_correlation_add_ccz_weight(uint64_t u, uint64_t v, uint64_t w, int n)`：返回 \(k\) 或 `nullopt`
  - `double row_best_correlation_value(uint64_t u, int n)` / `std::optional<double> column_best_correlation_value(uint64_t v, uint64_t w, int n)`：行/列最大相关的便捷封装

---

## 📊 复杂度与精确度对照 / Complexity & Accuracy (as implemented)

| 算子 / Operator | 场景 / Case | 方法 / Method | 复杂度 / Complexity | 精确度 / Accuracy |
| --- | --- | --- | --- | --- |
| XDP⁺ of ⊞ | var-var | LM-2001（ψ/eq + popcount） | 固定 32-bit ≈ O(1)；`*_n` 为常数级位运算 | **精确** |
| XOR diff of ⊞a | var-const | carry-pair DP count | **O(n)** | **精确** |
| XOR diff of ⊞a | var-const | Lemma 3/4/5（log2π） | **O(n)** | **精确**（浮点误差除外） |
| XOR diff of ⊞a | var-const | BvWeight^κ（Qκ fixed-point） | **O(n)**（当前逐链实现） | **近似** |
| linear corr of ⊞ | var-var | Wallén-logn（实现偏 32-bit） | 固定 32-bit 近似常数成本 | **精确**（权重/数值） |
| linear corr of ⊞ / ⊞a | var-var / var-const | 2×2 transfer matrices | **O(n)** | **精确** |

---

## ✅ 一致性与约定 / Conventions

- **差分 weight / Differential weight**：`w = -log2(DP)`；不可能通常用 `-1`（整数接口）或 `+∞`（double 接口）
- **线性 weight / Linear weight**：`Lw = -log2(|corr|)`；`corr==0` ⇒ `+∞`
- **位宽 / Word size**：
  - `differential_xdp_add.hpp` 的 `*_n` 支持 `1..32`
  - `linear_correlation_addconst.hpp` 的封装支持 `1..64`（通过 `uint64_t` + `nbits`）

---

## 🧷 构建与依赖 / Build & Notes

- 代码中存在 `__builtin_popcount` / `__builtin_clz` 等内建（GCC/Clang/clang-cl 直接可用）；若使用 MSVC `cl.exe`，建议：
  - 直接改用 clang-cl；或
  - 为这些内建提供兼容层（本目录内 `modular_addition_ccz.hpp` 已对 MSVC 做了 popcount 分支）。
- 无第三方依赖；以 header-only 为主；`math_util.hpp` 提供通用的模 \(2^n\) 取负 `neg_mod_2n`。

