# ARX 分析算子（以“模加”为核心）/ ARX Analysis Operators (Addition-Centric)

* 更新时间 Updated: **2026-03-21**
* 主要命名空间 / Namespace:
  - `TwilightDream::arx_operators`（对外算子 / public operators）
  - `TwilightDream::bitvector`（位向量/SWAR primitives；当前实现在 `differential_addconst.hpp` 内）

---

## 目标 / Goal

- **ZH**：本目录提供 ARX 密码分析里“模加 ⊞/⊟”相关的**差分**与**线性相关度**算子实现，强调**可审计**（注释对齐论文符号）与**可直接用于自动化搜索**（返回 weight / feasibility），并保留若干位向量 primitives 便于将来做 SMT/bit-vector 编码。
- **EN**: This folder provides **auditable** and **search-friendly** operators for ARX analysis, focused on modular addition/subtraction under XOR differentials and linear correlations, with clear API contracts and feasibility/weight conventions.

---

## 📁 头文件一览 / Header Inventory (current)

- `differential_xdp_add.hpp`：LM-2001 XOR 差分 var-var add；`psi` / `psi_with_mask` + exact `*_n` weight / feasibility
- `differential_optimal_gamma.hpp`：LM-2001 Algorithm 4；`find_optimal_gamma*` + `carry_aop` / `aop` / `aopr`
- `differential_addconst.hpp`：var-const add/sub 差分；精确 count / DP / weight、exact ceil-int wrappers、Qκ 近似、bitvector primitives
- `linear_correlation_add_logn.hpp`：Wallén CPM 参考实现 + bitsliced logn + 32-bit 包装权重接口
- `linear_correlation_addconst.hpp`：O(n) 精确线性相关（2×2 carry-state transfer matrices；var-const + var-var）
- `linear_correlation_addconst_flat.hpp`：var-const 精确 / windowed 线性相关（run 扁平化、run cache、binary-lift、cascade）
- `math_util.hpp`：小工具（目前提供 `neg_mod_2n<T>(k,n)`）
- `modular_addition_ccz.hpp`：Schulte-Geers 显式差分 / 线性公式 + row/column maxima helpers

> 注：README 只描述 **目录中实际存在且可 include 的文件**；旧文件名（如 `bitvector_ops.hpp` / `linear_cor_addconst.hpp`）已不再使用。

---

## 🔗 NeoAlzette 搜索流程中的实际接入 / Actual Integration in NeoAlzette Search

> 这一节回答一个和“数学有没有被写歪”直接相关的问题：
> **目录里有哪些算子**，和 **NeoAlzette 主搜索当前实际用了哪些算子**，不是一回事。
> 当前仓库的主搜索路径是：
> - **模加 / 常量减**：尽量复用本目录算子
> - **注入层**：不强行塞进通用 add/sub 算子，而是保持与 `NeoAlzetteCore` / `neoalzette_injection_constexpr.hpp` 对齐的精确模型

| 头文件 / Header | NeoAlzette 主搜索是否直接使用 | 当前实际角色 / Current role | 主要接入点 / Main integration sites |
| --- | --- | --- | --- |
| `differential_xdp_add.hpp` | **是**（差分） | LM-2001 XOR-diff var-var add 可行性 / prefix weight / 严格枚举 | `include/auto_search_frame/detail/differential_best_search_math.hpp`, `src/auto_search_frame/differential_best_search_math.cpp` |
| `differential_optimal_gamma.hpp` | **是**（差分） | 用 `find_optimal_gamma_with_weight()` 做 greedy 初始化、最优 hint、分支裁剪辅助 | `src/auto_search_frame/differential_best_search_math.cpp`, `src/auto_search_frame/differential_best_search_engine.cpp`, `src/auto_search_frame/differential_best_search_collector.cpp` |
| `differential_addconst.hpp` | **是**（差分） | var-const subtraction 的实际权重接口是 `diff_subconst_exact_weight_ceil_int()`；搜索框架已统一改用 exact 命名 | `include/auto_search_frame/detail/differential_best_search_math.hpp`, `src/auto_search_frame/differential_best_search_math.cpp` |
| `modular_addition_ccz.hpp` | **是**（线性） | var-var 模加的**精确线性权重**后端：`linear_correlation_add_ccz_weight()` | `include/auto_search_frame/detail/linear_best_search_math.hpp` |
| `linear_correlation_addconst.hpp` | **是**（线性） | var-const subtraction 的**精确线性相关**后端：`linear_x_modulo_minus_const32()` | `include/auto_search_frame/detail/linear_best_search_math.hpp` |
| `linear_correlation_add_logn.hpp` | **默认否**（但保留兜底） | 当前默认主线性搜索不走它做生产打分；它主要用于自测 / 对照 / CPM 与 Wallen 路线回归，并在 `linear_best_search_math.hpp` 邻近保留为通用 fallback / regression include | `test_arx_operator_self_test.cpp`, `include/auto_search_frame/detail/linear_best_search_math.hpp` |
| `linear_correlation_addconst_flat.hpp` | **否**（主搜索当前不直接调用） | 当前主要用于自测 / 精确对照 / windowed / binary-lift 实验，不是 NeoAlzette 主线性搜索的生产路径 | `test_arx_operator_self_test.cpp` |
| `math_util.hpp` | **间接** | 主要作为 add-const / sub-const 相关实现的辅助依赖 | 被其他头文件间接包含 |

### 线性搜索当前的真实结构 / What the linear search really does

- **var-var 模加候选枚举**：当前 NeoAlzette 线性主搜索并不是直接调用 `linear_correlation_add_logn.hpp` 来“枚举候选”。
  - 实际枚举器在 `include/auto_search_frame/detail/linear_best_search_math.hpp`：
    - `generate_add_candidates_for_fixed_u()`
    - `AddVarVarSplit8Enumerator32`
  - 它们基于 Schulte-Geers 显式约束 / z-shell / split-lookup-recombine 路线做**候选生成**。
  - 这不只是“借 cLAT 这个名字”：数学上它对应 Huang/Wang 2019 的 **specific correlation weight space**，也就是按 `z = M^T(u xor v xor w)` 的 `wt(z)` 分层；工程上再用 split/lookup/recombine 实现，所以更准确的口径是 **`z-shell based Weight-Sliced cLAT`**。
- **var-var 模加权重**：候选生成之后，实际权重后端是 `modular_addition_ccz.hpp` 的
  `linear_correlation_add_ccz_weight()`，不是 Wallen-logn 的公开接口。
- **Wallen-logn 保留方式**：
  - `linear_correlation_add_logn.hpp` 目前不是默认热路径的生产后端；
  - 但它在 `linear_best_search_math.hpp` 里仍保留 include，作为通用 fallback / regression / 对照路线，而不是“已经死掉的残留头文件”。
- **var-const subtraction**：主线性搜索对常量减使用
  `linear_correlation_addconst.hpp` 的 `linear_x_modulo_minus_const32()`。
- **注入层**：不被粗暴地降成“通用 add/sub 小算子”。
  - 当前保持与 `NeoAlzetteCore` + `neoalzette_injection_constexpr.hpp` 对齐的**精确二次模型**：
    `g_u(x)=<u,f(x)>`，相关输入 mask 集合为 `v ∈ l(u) ⊕ im(S(u))`。

### 差分搜索当前的真实结构 / What the differential search really does

- **var-var 模加**：
  - 严格枚举与 prefix 剪枝使用 `differential_xdp_add.hpp` 的 `xdp_add_lm2001_n()`。
  - greedy 初始化 / 最优 hint 使用 `differential_optimal_gamma.hpp` 的 `find_optimal_gamma_with_weight()`。
  - `w-pDDT` 承袭的是 Biryukov-Velichkov 的 pDDT / threshold-search 路线，但当前工程把原来 `DP >= p_thres` 的 threshold pDDT，升级成了按精确整数权重分层的 **Weight-Sliced pDDT**。
- **var-const subtraction**：
  - 主差分路径实际吃的是 `differential_addconst.hpp` 的 `diff_subconst_exact_weight_ceil_int()`。
  - 旧的 `diff_*_bvweight()` 兼容别名已经移除，避免把 exact ceil-int weight 和 Azimi 系列的近似 BvWeight 混在一起。
  - 搜索框架围绕它实现了严格枚举器，而不是把 sub-const 简化成恒等传播。
- **注入层**：
  - 当前保持与 `NeoAlzetteCore` + `neoalzette_injection_constexpr.hpp` 对齐的**精确仿射导数模型**：
    `D_Δ f(x) = M_Δ x ⊕ c_Δ`。
  - 搜索里使用 `InjectionAffineTransition{ offset, basis_vectors, rank_weight }`，
    reachable output-difference 集合是 `offset ⊕ span(basis_vectors)`，每个 reachable 差分的概率是 `2^{-rank(M_Δ)}`。

### 一句总结 / One-line summary

- **主搜索并不是“把所有东西都塞给一个通用 ARX 算子库”**。
- 当前正确的说法是：
  - **模加 / 常量减**：由本目录算子给出局部精确权重 / 可行性
  - **NeoAlzette 注入层**：由实现对齐的 exact model 单独处理

---

## 🧪 差分算子 / Differential Operators

### 1) `differential_xdp_add.hpp` — XOR 差分模加（变量-变量，LM-2001）

- **定义 / Definition**：\(z=x ⊞ y\)，\(z'=(x⊕α) ⊞ (y⊕β)\)，\(γ=z⊕z'\)，计算 \(DP^+(α,β↦γ)\) 与 \(w=-\log_2 DP^+\)。
- **API（核心）/ Core API**（均在 `TwilightDream::arx_operators`）：
  - `template <BitWord T> constexpr T psi(T alpha, T beta, T gamma)`：LM-2001 `eq/ψ` 约束的工程化实现
  - `template <BitWord T> constexpr T psi_with_mask(T alpha, T beta, T gamma, T mask)`：`n<32` 时显式 mask 的版本
  - `int xdp_add_lm2001(uint32_t alpha, uint32_t beta, uint32_t gamma)`：返回整数 weight `w`；不可能返回 `-1`
  - `int xdp_add_lm2001_n(uint32_t alpha, uint32_t beta, uint32_t gamma, int n)`：支持 `1..32` 位（输入会 mask 到低 n 位）
  - `double xdp_add_probability(uint32_t alpha, uint32_t beta, uint32_t gamma)`：返回 `DP^+`（不可能返回 `0.0`）
  - `bool is_xdp_add_possible(uint32_t alpha, uint32_t beta, uint32_t gamma)`

### 2) `differential_optimal_gamma.hpp` — 最优输出差分 γ（LM-2001 Algorithm 4）

- **用途 / Use**：给定 (α,β) 直接构造使 \(DP^+\) 最大的 γ（避免枚举 γ）。
- **API**（`TwilightDream::arx_operators`）：
  - `uint32_t carry_aop(uint32_t alpha, uint32_t beta, uint32_t gamma)`：Algorithm 4 里会用到的 carry-support helper
  - `uint32_t aop(uint32_t x)` / `uint32_t aopr(uint32_t x, int n=32)`：all-one parity 及其 MSB 方向版本
  - `uint32_t bitreverse_n(uint32_t x, int n=32)` / `uint32_t bitreverse32(uint32_t x)`：Algorithm 4 的 bit-reverse helper
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
    - 32-bit wrappers：`diff_addconst_exact_count / diff_addconst_exact_probability / diff_addconst_exact_weight / diff_addconst_exact_weight_ceil_int`
  - **闭式（log2π）**：`double diff_addconst_weight_log2pi_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`
    - 32-bit wrapper：`diff_addconst_weight_log2pi`
  - **近似（BvWeight^κ）**：
    - `uint32_t diff_addconst_bvweight_fixed_point_n(uint32_t dx, uint32_t a, uint32_t dy, int n, int fraction_bit_count)`
    - `uint32_t diff_addconst_bvweight_q4_n(uint32_t dx, uint32_t a, uint32_t dy, int n)`（κ=4）
    - 32-bit wrappers：`diff_addconst_bvweight_fixed_point / diff_addconst_bvweight_q4`
  - **精确整型接口（与当前搜索框架对接）**：
    - `int diff_addconst_exact_weight_ceil_int(uint32_t dx, uint32_t a, uint32_t dy)`：32-bit add-const 的 `ceil(exact_weight)`
    - `int diff_subconst_exact_weight_ceil_int(uint32_t dx, uint32_t a, uint32_t dy)`：32-bit sub-const 的 `ceil(exact_weight)`
    - `int diff_addconst_bvweight_q4_int_ceil(...)`：仅用于“近似 Q4 → int”的对照/实验（仍是近似）
  - **近似概率（由 Q4 weight 换算）**：
    - `double diff_addconst_probability(uint32_t dx, uint32_t a, uint32_t dy)`
    - `double diff_subconst_probability(uint32_t dx, uint32_t a, uint32_t dy)`
  - **减法**：`diff_subconst_*` 系列通过 `neg_mod_2n` 转换到 add-const

- **位向量 primitives（在同一头文件内）/ Bit-vector primitives**（`TwilightDream::bitvector`）：
  - `HammingWeight`, `BitReverse`, `BitReverse_n`, `Carry`, `Carry_n`, `RevCarry_n`, `BitReverseCarry`
  - `LeadingZeros`, `LeadingZeros_n`, `ParallelLog`, `ParallelLog_n`, `ParallelTrunc`, `ParallelTrunc_n`

> 重要：当前工程实现为了“可审计/可单测”，在 BvWeight^κ 的计算上采用逐链展开（整体仍是 **O(n)** 量级）。论文中的纯 bit-vector（含 ParallelLog/ParallelTrunc）写法在本文件中作为 primitives 保留，便于未来回切到 SMT-friendly 的表达式形式。

---

## 📈 线性算子 / Linear-Correlation Operators

### 4) `linear_correlation_add_logn.hpp` — Wallén 风格对数算法（变量-变量）

- **核心 / Key idea**：这个文件实际上分成两层。
  - **CPM reference 层**：`compute_cpm_recursive(x,y,n)` 直接按 Wallén Definition 6 递归实现 `cpm`，可作为 truth / regression baseline
  - **CPM logn 层**：`compute_cpm_logn_bitsliced(x,y,n)` / `compute_cpm_logn(...)` 提供 bit-sliced Θ(log n) 路线；当 `n` 不是 2 的幂时会自动回退到 reference 版本
- **实现细节 / Implementation note**：
  - `compute_cpm_logn_bitsliced` 在实现里做了一次 `n=8` 的穷举对齐，用来消除 Theorem 2 伪代码到 `bit0=LSB` 工程表示时的歧义；这不是“又写了一套新数学”，而是为了保证 bit-sliced 版本和 Definition 6 递归真值表严格一致
- **API**（`TwilightDream::arx_operators`）：
  - `uint64_t compute_cpm_recursive(uint64_t x, uint64_t y, int n=32)`
  - `uint64_t compute_cpm_logn_bitsliced(uint64_t x, uint64_t y, int n=32)`
  - `uint64_t compute_cpm_logn(uint64_t x, uint64_t y, int n=32)`
  - `std::optional<int> wallen_weight(uint32_t mu, uint32_t nu, uint32_t omega, int n)`
  - `int    internal_addition_wallen_logn(uint32_t u, uint32_t v, uint32_t w)`：返回 `Lw = -log2(|corr|)`（不可行返回 `-1`）
  - `double linear_correlation_add_value_logn(uint32_t u, uint32_t v, uint32_t w)`：返回 `|corr|`（不可行返回 `0.0`；当前实现返回**绝对值**）
  - `uint32_t MnT_of(uint32_t v)`：Wallén 路线里的 32-bit carry-support helper

### 5) `linear_correlation_addconst.hpp` — 精确线性相关（var-const + var-var，O(n) 基线）

- **方法 / Method**：每 bit 构造 2×2 carry-state transfer matrix，逐位左乘累积得到最终相关度 `corr`；再转为线性 weight：
  - `Lw = -log2(|corr|)`，当 `corr==0` 时为 `+∞`
- **平均因子 / Averaging factor（非常关键）**：
  - **var-const**：只平均 `x_i` 两种情况 ⇒ `1/2`
  - **var-var**：平均 `(x_i,y_i)` 四种情况 ⇒ `1/4`
- **低层 exact evaluators / Low-level exact evaluators**（`TwilightDream::arx_operators`）：
  - `double correlation_add_const(...)` / `double correlation_sub_const(...)`
  - `double correlation_add_varvar(...)` / `double corr_sub_varvar(...)`
  - `Matrix2D`：单 bit 的 2×2 carry-state transfer matrix
- **对外封装 API / Public wrappers**（`TwilightDream::arx_operators`）：
  - `LinearCorrelation linear_x_modulo_plus_const32(uint32_t alpha, uint32_t K, uint32_t beta, int nbits=32)`
  - `LinearCorrelation linear_x_modulo_minus_const32(uint32_t alpha, uint32_t C, uint32_t beta, int nbits=32)`
  - `LinearCorrelation linear_x_modulo_plus_const64(uint64_t alpha, uint64_t K, uint64_t beta, int nbits=64)`
  - `LinearCorrelation linear_x_modulo_minus_const64(uint64_t alpha, uint64_t C, uint64_t beta, int nbits=64)`
  - `LinearCorrelation linear_add_varvar32(uint32_t alpha, uint32_t beta, uint32_t gamma, int nbits=32)`
  - `LinearCorrelation linear_add_varvar64(uint64_t alpha, uint64_t beta, uint64_t gamma, int nbits=64)`
  - `struct LinearCorrelation { double correlation; double weight; bool is_feasible() const; }`

### 5b) `linear_correlation_addconst_flat.hpp` — 精确线性相关（var-const，run 扁平化 + β 稀疏，n≤64）

- **定位 / Positioning**：只覆盖 \(y=x \boxplus c\)（变量-常量），但把实现拆成
  - **exact flat path**：run-flatten + thread-local run table cache
  - **windowed path**：working set \(S=\mathrm{supp}(\alpha \oplus \beta) \cup \mathrm{LeftBand}(\beta,L)\) 上的 certified estimator
- **exact path 特点 / Exact-path notes**：
  - 结果类型核心是 `DyadicCorrelation`，分母固定为 `2^n`
  - 预建 run table 后，online 路径的自然复杂度接近 `O(#runs(c)+wt(beta))`
- **API（核心）/ Core API**（`TwilightDream::arx_operators`）：
  - `DyadicCorrelation linear_correlation_add_const_exact_flat_dyadic(uint64_t alpha, uint64_t constant, uint64_t beta, int n)`
  - `double          linear_correlation_add_const_exact_flat(uint64_t alpha, uint64_t constant, uint64_t beta, int n)`
  - `long double     linear_correlation_add_const_exact_flat_ld(uint64_t alpha, uint64_t constant, uint64_t beta, int n)`
  - `int             linear_correlation_add_const_exact_flat_weight_ceil_int(uint64_t alpha, uint64_t constant, uint64_t beta, int n)`
  - 论文参数顺序别名（α,β,c,n）：`corr_add_const_exact_flat_* (alpha, beta, constant, n)`
- **API（窗口/抬升/串接）/ Window + Lift + Cascade**（`TwilightDream::arx_operators`）：
  - `WindowedCorrelationReport linear_correlation_add_const_flat_bin_report(uint64_t alpha, uint64_t constant, uint64_t beta, int n, int L)`
    - 返回：`corr_hat`（dyadic, denom_log2==n）、`delta_bound`、`weight_conservative`、`working_set_mask`
    - 误差界：\(\delta = 2\,\mathrm{wt}(\beta)\,2^{-L}\)
  - 便捷封装：`linear_correlation_add_const_flat_bin_dyadic / _flat_bin / _flat_bin_ld`
  - Binary-Lift：
    - `BinaryLiftMasks binary_lift_addconst_masks(uint64_t alpha, uint64_t beta, uint64_t constant, int n)`
    - `BinaryLiftedWindowedReport corr_add_const_binary_lifted_report(uint64_t alpha, uint64_t beta, uint64_t constant, int n, int L)`（强制 `L>=2`）
  - Cascade：
    - `struct CascadeRound { alpha,beta,constant,n,L,lift }`
    - `CascadeReport corr_add_const_cascade(std::span<const CascadeRound> rounds)`
- **数值与跨平台 / Numerics & portability**：
  - 默认：GCC/Clang 使用 `__int128` 做分子（最快）；否则自动回退到内置 `FixedInt256`
  - 可强制便携后端：编译时定义 `TWILIGHTDREAM_ARX_FORCE_INT256`

### 6) `modular_addition_ccz.hpp` — CCZ 等价与显式公式（差分 + 线性）

- **定位 / Positioning**：给出 addition mod \(2^n\) 的**显式差分概率**（Theorem 3）与**显式 Walsh/相关系数**（Theorem 4）形式；适合作为“公式基准/交叉验证”，也可直接用于搜索中的可行性与权重计算。
- **结构 helper / Structural helpers**（`TwilightDream::arx_operators`）：
  - `L_of / R_of / M_of / MnT_of`
  - `aL_of / row_best_b_of / row_best_d_of`
  - `m_of / column_best_a_of / column_best_u_of`
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
| optimal γ for XDP⁺ | var-var | LM-2001 Algorithm 4（`aop` / `aopr`） | 固定 32-bit ≈ O(1) | **精确 argmax** |
| XOR diff of ⊞a | var-const | carry-pair DP count | **O(n)** | **精确** |
| XOR diff of ⊞a | var-const | Lemma 3/4/5（log2π） | **O(n)** | **精确**（浮点误差除外） |
| XOR diff of ⊞a | var-const | BvWeight^κ（Qκ fixed-point） | **O(n)**（当前逐链实现） | **近似** |
| linear corr of ⊞ | var-var | Wallén CPM（recursive reference / bitsliced logn / 32-bit wrapper） | recursive **O(n)**；pow2-domain bitsliced **O(log n)** | **精确** |
| linear corr of ⊞ / ⊞a | var-var / var-const | 2×2 transfer matrices | **O(n)** | **精确** |
| linear corr of ⊞a | var-const | run-flatten exact path | 预建 run table 后自然 online 复杂度约 **O(#runs(c)+wt(β))** | **精确** |
| linear corr of ⊞a | var-const | windowed evaluator / certified estimator \(\widehat{C}_L\) | **O(log min(L+1,n)+#runs(c\|_S)+wt(β\|_S))** | **认证估计**（满足 \(|C-\widehat{C}_L| \le \delta\)） |

---

## ✅ 一致性与约定 / Conventions

- **差分 weight / Differential weight**：`w = -log2(DP)`；不可能通常用 `-1`（整数接口）或 `+∞`（double 接口）
- **线性 weight / Linear weight**：`Lw = -log2(|corr|)`；`corr==0` ⇒ `+∞`
- **位宽 / Word size**：
  - `differential_xdp_add.hpp` / `differential_optimal_gamma.hpp` 的 `*_n` 支持 `1..32`
  - `differential_addconst.hpp` 的公开差分接口以 `uint32_t` 为载体，`n` 域为 `1..32`
  - `linear_correlation_add_logn.hpp` 的 `compute_cpm_*` 支持 `1..64`；`internal_addition_wallen_logn` / `linear_correlation_add_value_logn` 是 32-bit wrapper
  - `linear_correlation_addconst.hpp` / `linear_correlation_addconst_flat.hpp` / `modular_addition_ccz.hpp` 支持 `1..64`

---

## 🧷 构建与依赖 / Build & Notes

- 代码当前优先使用 C++20 `<bit>` 提供的 `std::popcount` / `std::countl_zero` / `std::bit_width`。
- 构建前提是支持 C++20 bit operations 的标准库与编译器；本仓库默认工具链已满足这一点。
- 无第三方依赖；以 header-only 为主；`math_util.hpp` 提供通用的模 \(2^n\) 取负 `neg_mod_2n`。

