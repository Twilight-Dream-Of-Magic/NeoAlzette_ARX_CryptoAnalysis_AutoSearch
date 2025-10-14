# ARX 密码分析核心论文导读（实作导向版）📚

> 这不是“神书”，更像一份能跑起来的读书笔记：每个结论要么能指回论文，要么能用脚本复现。  
> 从「看不懂」到「能跑通」的路线图（不保证你一夜变专家，保证你少走冤枉路）

---

## 0. 开场：现实点，我们要的到底是什么？

**目标**很朴素：

1. 把 ARX 的两条主线——**差分**与**线性**——讲到能跑；
2. 把论文里的“可证正确的”东西搬下来；
3. 把工程实现对齐到现有仓库；
4. 把中间那些“学到想哭”的坎，一个个填平。

> 这篇文章不是“堆材料”，而是“给指路 + 给路标 + 给交通工具”。路我走过一遍也踩过坑；你照着走，会省不少时间。

## 0.1 论文全览与核心价值链（先用这张表定位）📋

| 序号     | 论文                                                                                                    |        年份 | 核心突破              | 解决的根本问题                      | 在我们项目中的体现                                                                                            |
| ------ | ----------------------------------------------------------------------------------------------------- | --------: | ----------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| 🥇     | **Efficient Algorithms for Computing Differential Properties of Addition Modulo 2^n**（Lipmaa–Moriai）  |      2001 | ψ & 进位结构          | 模加差分从指数枚举降到**位级可算/可剪枝**      | `include/arx_analysis_operators/differential_xdp_add.hpp`；`include/auto_search_frame/test_neoalzette_differential_best_search.hpp` |
| 🥈     | **Linear Approximations of Addition Modulo 2^n**（Wallén）                                              |      2003 | MnT 支撑 `z*`       | 模加线性的**可行性与相关度**可计算          | `include/arx_analysis_operators/linear_correlation_add_logn.hpp`；`include/auto_search_frame/test_neoalzette_linear_best_search.hpp` |
| 🥉     | **A Bit-Vector Differential Model for Modular Addition by a Constant**（会议+扩展）                         | 2020–2021 | 位级精确建模            | `x←x+c` 的差分/线性**不能偷懒**，需专门模型 | `include/arx_analysis_operators/differential_addconst.hpp`；`include/arx_analysis_operators/linear_correlation_addconst.hpp` |
| 4️⃣    | **Automatic Search for Differential Trails in ARX Ciphers**                                           |      2017 | BnB 搜索框架          | 多轮差分最优**不漏**且高效剪枝            | `include/auto_search_frame/test_neoalzette_differential_best_search.hpp`；`test_neoalzette_differential_best_search.cpp` |
| 5️⃣    | **Automatic Search for the Best Trails in ARX – Application to Block Cipher Speck**                   | 2015–2016 | 搜索策略细化            | ARX 实例上的**最优轨道**实证           | 作为**搜索策略与验证案例**参照                                                                                    |
| 6️⃣    | **Automatic Search of Linear Trails in ARX (SPECK & Chaskey)**                                        | 2016–2017 | 线性搜索套路            | 线性掩码空间的**系统枚举与剪枝**           | `include/auto_search_frame/test_neoalzette_linear_best_search.hpp`；`test_neoalzette_linear_best_search.cpp` |
| 7️⃣    | **Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers**（SPECK/SPARX/Chaskey/CHAM64） | 2017–2019 | 线性 **hull** 视角    | 相关度聚合/上界评估更稳健                | 线性主线的 **hull 口径**与实验对照                                                                               |
| 8️⃣    | **MILP-Based Automatic Search Algorithms for Differential and Linear Trails for Speck**               |      2016 | ARX-MILP 建模       | 约束化建模，便于**后缀求精**             | 作为可选思路（求解器后缀精化）                                                                                      |
| 9️⃣    | **Alzette: A 64-Bit ARX-box (feat. CRAX & TRAX)**                                                     |      2020 | 三步流水线             | 12 步、AES-级性质、工程范式            | `include/neoalzette/neoalzette_core.hpp`；`src/neoalzette/neoalzette_core.cpp`；`include/neoalzette/neoalzette_injection_constexpr.hpp` |
| 🔟     | **Sparkle 规范**（Schwaemm/Esch）                                                                         |      2020 | 工程化部署             | Alzette 在真实协议/原语中的落地         | 常量/接口参考与**应用语境**                                                                                     |
| 1️⃣1️⃣ | **（合并）线性/差分系列的补充材料**                                                                                  | 2016–2019 | cLAT/Highway 思想来源 | 把**后缀/支撑**变成 O(1) 查表         | （本仓库未单独实现 cLAT/Highway；改为在 best_search 内做候选/状态缓存）`include/auto_search_frame/test_neoalzette_linear_best_search.hpp`；`include/auto_search_frame/test_neoalzette_differential_best_search.hpp`；`common/runtime_component.hpp`；`common/runtime_component.cpp` |

---

---

## 1. 重要澄清（Alzette vs NeoAlzette）

### 1.1 重要澄清（先看对照文档）

**请先阅读 [ALZETTE_VS_NEOALZETTE.md](ALZETTE_VS_NEOALZETTE.md)**

本文档中关于Alzette的描述可能混淆了：
- **Alzette**（2020论文的原始精妙三步设计）
- **NeoAlzette**（我们项目的复杂扩展实现）

* **Alzette**（论文原型）：64‑bit ARX‑box，一次迭代 12 步（4×「加→异或→加常量」），旋转序列固定：
  r = (31, 17, 0, 24)，s = (24, 17, 31, 16)。
* **NeoAlzette**（我们仓库）：实验/扩展实现 + 自动分析平台。接口与论文对齐，但功能更工程化。
* **用法**：论文做“标准尺子”、NeoAlzette做“实验跑道”。不要混写数值结论。

---

## 2. 先把“大地图”打开（只保留正确的主干）

**差分线（Differential）**

* **LM‑2001**：发现 ψ 函数，模加差分不再靠 2^{2n} 枚举；核心是把“是否成立”化为**进位条件**的位级判定。
* **Add‑Const（位向量模型）**：`x ← x + c` 必须单列建模，常量位参与进位传播。
* **搜索（Matsui BnB）**：乐观下界 + 阈值剪枝，保证不漏最优；速度主要看下界质量与“重复子问题”能不能被缓存吃掉。

**线性线（Linear）**

* **Wallén‑2003**：MnT 支撑 `z*` 告诉你“哪些位可能被进位影响”；可行性 = 掩码受不受支撑。
* **Add‑Const（线性模型）**：常量改变支撑形态，不能偷懒当作 var‑var。所以我们不得不用Wallén通用矩阵。
* **cLAT/Highway（概念）**：给线性空间修“高速路”，后缀查询 O(1)。本仓库没单独实现它们，而是用候选缓存/记忆化做类似的加速。

> 以上都不是口号，是**能落地**的。我们下面每一条都有“对应源码 + SOP”。

---

## 3. 差分主线：我到底是怎么想通的？

### 3.1 把“不可算”变成“可算”（LM‑2001）

* 原问题：
  [
  DP_+(\alpha,\beta\to\gamma)=\Pr[\text{carry}(x,y)\oplus\text{carry}(x\oplus\alpha,y\oplus\beta)=\alpha\oplus\beta\oplus\gamma]
  ]
* 关键量：`ψ = (α ⊕ β) & (α ⊕ γ)`；**可行**时 `HW(ψ)`给出主导权重（对应 −log₂ 概率）。
* **前缀可行性**：高位一旦冲突，整棵子树剪掉 → 这就是算法上“跑得动”的根本原因。

> **我的止痛片**：别死磕进位的所有状态，把“会不会违反”当作布尔断言逐位判断，效率自然出来了。

### 3.2 Add‑Const 必须单列（位向量）

* `x ← x + c` 不是“随口一改就行”的：常量位 + 进位 = 传播形态改变。
* 结论：**把 var‑const 当 var‑var 会错**，你会漏到关键路径或误估概率。

### 3.3 对应源码（差分）

* `include/arx_analysis_operators/differential_xdp_add.hpp`（LM‑2001 var‑var）
* `include/arx_analysis_operators/differential_addconst.hpp`（Add‑Const 位向量）
* `include/auto_search_frame/test_neoalzette_differential_best_search.hpp`（Matsui/BnB 框架 + 注入/常量差分枚举）
* `test_neoalzette_differential_best_search.cpp`（可执行入口/CLI/多线程运行）
* `include/neoalzette/neoalzette_core.hpp` + `src/neoalzette/neoalzette_core.cpp`（NeoAlzette 轮函数实现）
* `common/runtime_component.hpp` + `common/runtime_component.cpp`（线程/内存治理/进度输出等运行时组件）

### 3.4 差分 SOP（最小可跑）

```bash
# 编译
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  test_neoalzette_differential_best_search.cpp \
  src/neoalzette/neoalzette_core.cpp \
  common/runtime_component.cpp -o build/neoalzette_diff

# 运行（默认 4 轮；也可传入轮数）
./build/neoalzette_diff      
./build/neoalzette_diff 5
```

**看什么**：打印的 `MEDCP (rounds=X): 2^{-Weight}` 是否随轮数上升（相关度衰减），最优权重“稳不稳”；调大 cap 只会更慢，不会“凭空更优”。

---

## 4. 线性主线：为什么相关度能算？

### 4.1 MnT 支撑（Wallén‑2003）

* `z*` 是“进位的可能支撑图”。
* 可行性：令 `a = μ ⊕ ω, b = ν ⊕ ω`，若 `a ⪯ z*` 且 `b ⪯ z*` 才有非零相关。
* 算法感：把复杂矩阵换成**逐步递推/查表**，每步 O(1) 更新，跑起来不虚。

### 4.2 Add‑Const 的线性模型

* 常量影响支撑，不是“顺手复用 var‑var”。用专门模型，别偷懒。

### 4.3 对应源码（线性）

* `include/arx_analysis_operators/linear_correlation_add_logn.hpp`（Wallén/MnT）
* `include/arx_analysis_operators/linear_correlation_addconst.hpp`（Add‑Const 线性）
* `include/auto_search_frame/test_neoalzette_linear_best_search.hpp`（Matsui/BnB 框架 + 注入仿射子空间枚举/候选缓存）
* `test_neoalzette_linear_best_search.cpp`（可执行入口/CLI/多线程运行）
* `include/neoalzette/neoalzette_core.hpp` + `src/neoalzette/neoalzette_core.cpp`（NeoAlzette 轮函数实现）
* `common/runtime_component.hpp` + `common/runtime_component.cpp`（线程/内存治理/进度输出等运行时组件）

### 4.4 线性 SOP（最小可跑）

```bash
# 编译
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  test_neoalzette_linear_best_search.cpp \
  src/neoalzette/neoalzette_core.cpp \
  common/runtime_component.cpp -o build/neoalzette_lin

# 运行
./build/neoalzette_lin      
./build/neoalzette_lin 6
```

**看什么**：打印的 `MELCC (rounds=X): 2^{-Weight}` 是否随轮数上升（相关度衰减）；调整候选上限/记忆化等开关时，最优值应稳定（只影响速度与搜索节点数）。

---

## 5. Alzette：必须保留的“逐行解析”（艾瑞卡版）

> 一次迭代共 12 步，分 4 组；`≫` 为循环右移，`c` 为常量。

```text
Algorithm 1 Alzette_c (one iteration)
Input/Output: (x, y) ∈ (F₂³²)²

# Group 1
1)  x ← x + (y ≫ 31)          # r0 = 31 → 非线性（进位链）
2)  y ← y ⊕ (x ≫ 24)          # s0 = 24 → 线性扩散/破对称
3)  x ← x ⊕ c                 # 常量注入 → 破结构/稳节奏

# Group 2
4)  x ← x + (y ≫ 17)          # r1 = 17
5)  y ← y ⊕ (x ≫ 17)          # s1 = 17
6)  x ← x ⊕ c

# Group 3
7)  x ← x + (y ≫ 0)           # r2 = 0 → 无旋直加，强化邻位耦合
8)  y ← y ⊕ (x ≫ 31)          # s2 = 31
9)  x ← x ⊕ c

# Group 4
10) x ← x + (y ≫ 24)          # r3 = 24
11) y ← y ⊕ (x ≫ 16)          # s3 = 16
12) x ← x ⊕ c

return (x, y)
```

Algorithm 1 Alzette_c (one iteration)
Input/Output: (x, y) ∈ (F₂³²)²


# Group 1
1)  x ← x + (y ≫ 31)          # r0 = 31 → 非线性来源（进位链）
2)  y ← y ⊕ (x ≫ 24)          # s0 = 24 → 线性扩散/破对称
3)  x ← x ⊕ c                 # 注常量 → 打破结构与周期


# Group 2
4)  x ← x + (y ≫ 17)          # r1 = 17
5)  y ← y ⊕ (x ≫ 17)          # s1 = 17
6)  x ← x ⊕ c


# Group 3
7)  x ← x + (y ≫ 0)           # r2 = 0  → 无旋转直加，强化相邻位干扰
8)  y ← y ⊕ (x ≫ 31)          # s2 = 31
9)  x ← x ⊕ c


# Group 4
10) x ← x + (y ≫ 24)          # r3 = 24
11) y ← y ⊕ (x ≫ 16)          # s3 = 16
12) x ← x ⊕ c


return (x, y)

**三步流水线：每步只负责一件事（讲人话版）**

- **加（模加）**：主要非线性来自 carry 传播；`x += (y ≫ rᵢ)` 让 carry 的触发位置不容易“每轮都对齐”，从而更难被简单模型吃掉。

- **异或 + 旋转**：线性扩散层。它把上一句产生的非线性“铺开”，并通过不同旋转量打破旋转等价/镜像对称这类结构性捷径。

- **常量注入**：`x ^= c` 的目的很朴素：破坏潜在周期与结构性对齐，避免“刚好抵消/刚好同相”的坏事在多轮里越滚越大。

**旋转量怎么理解（保守口径）**

- 论文给的是固定序列 \(r=(31, 17, 0, 24)\)、\(s=(24, 17, 31, 16)\)。这里不编“玄学比例”的故事，只认论文与可复现实验。
- 更稳妥的工程说法是：旋转量覆盖不同距离（近/中/远），尽量减少结构性对齐与短周期；最后以“能否复现论文口径 + 实测趋势是否一致”为准。

---

## 6. 结果口径（只说论文允许的）

* 一次 Alzette 的差分/线性性质与 **AES S‑box** 可比；两次串联与 **AES super‑S‑box** 同等级。
* 多轮时，最优差分概率与最大绝对相关度**快速衰减**；这是分析与实验共同验证的**趋势**。
* 其他攻击（代数、积分、不可能差分、零相关）均有相应分析框架作为佐证。

> 注意：这里**不写未经论文明确给出的强数字**。我们只保留正确、稳健、可复核的表述。

---

## 7. 把论文变成代码：仓库一对一映射

* 差分主线：`differential_xdp_add.hpp` → `differential_addconst.hpp` → `include/auto_search_frame/test_neoalzette_differential_best_search.hpp` → `test_neoalzette_differential_best_search.cpp`
* 线性主线：`linear_correlation_add_logn.hpp` → `linear_correlation_addconst.hpp` → `include/auto_search_frame/test_neoalzette_linear_best_search.hpp` → `test_neoalzette_linear_best_search.cpp`
* 公共：`include/neoalzette/neoalzette_core.hpp` + `src/neoalzette/neoalzette_core.cpp`（核心轮函数）；`common/runtime_component.*`（运行时/并发/资源治理）

**流程图（双线并行）**

```
差分：LM ψ/前缀可行 →（按位递归枚举 + weight cap）→ BnB(下界+阈值) → 最优路径/统计
线性：MnT 支撑/相关度计算 →（注入仿射子空间枚举 + 候选缓存）→ BnB/后缀求精 → MELCC/最优轨道
```

---

## 8. 读者常问（我当时也被坑过）

**Q1：为什么 var‑const 不能偷懒？**
A：常量位会塑造不同的进位/支撑形态；偷懒 = 漏路径/错评估。模型要分开。

**Q2：BnB 为什么“不会漏”？**
A：剪枝用的是**可证**的乐观下界；比你现在最好的还差，就没必要看了。

**Q3：cLAT/Highway 值得吗？**
A：如果你的瓶颈是“同类子问题反复算”，那缓存/记忆化就值；本仓库目前把这类加速做在 best_search 里（候选缓存、注入缓存、状态记忆化），而不是独立的 cLAT/Highway 模块。

**Q4：实验怎么自证不忽悠？**
A：关掉/打开预表与剪枝，**最优结果一致**；做几组 Monte‑Carlo 抽样看趋势一致。

---

## 9. 一键复现：我给你的剧本

**差分线**：先 4 轮，cap 保守；稳定了再加轮数/放 cap。
**线性线**：先 4→6 轮，确认 MELCC 单调衰减；再调缓存/候选上限/线程数提速（最优值应不变）。

> 复现实验都写在前面的 SOP 里，不赘述。

---

## 10. 还差什么？（下一步我能立刻做的）

* 给两个 `test_neoalzette_*_best_search.cpp` 加命令行参数（`--cap/--start-a/--start-b/--threads/...`）。
* 给仓库补一个 `CMakeLists.txt`（生成 `neoalzette_diff` / `neoalzette_lin`）。
* 附带一个 `tools/verify_mc.py`（Monte‑Carlo 抽样复核）。

> 你一句“继续”，我就把这三件事补齐，并把教学文档同步更新。

---

## 11. 终章：从“啊??!!”到“啊，原来这样”

* 别把“复杂”当成“神秘”——LM 把差分变成位条件，Wallén 把线性变成支撑检查；
* 别把“工程”当成“杂活”——BnB + 缓存/并发/资源治理，就是工程把数学落地；
* 别把“数值”当成“护身符”——只说论文给的，别自己编；
* 走过的路我已经铺好，你只需要：**按图施工**。

我们继续，下一步让它**更好用、更好跑**：不是靠口号，靠参数、脚本与对照表。

---

## 12. 我怎么和 AI 一起读完十几篇论文？（工作流版）

> 主题：把“看不动 → 看得懂 → 用得上”做成流程，而不是靠意志力硬扛。

**总原则（S3）**：**S**elect（选对材料）→ **S**implify（降维解释）→ **S**titch（拼到工程里）。

* **选（Select）**：

  * 只选**原始论文/官方规格/一手实现**做基准（例如：LM‑2001、Wallén‑2003、Alzette 2020、Sparkle 规范）。
  * 给 AI 的提问要带**边界**：年份、对象、要验证的命题、希望的输出格式。
  * 产出：带引用的“口径对齐版摘要 + 待验证清单”。

* **简（Simplify）**：

  * 让 AI 先用**小学数学**口吻讲一次，再用**研究生**口吻讲一次；两版对齐，暴露歧义点。
  * 把公式变成**位级布尔断言**或**极小代码片段**（例如 ψ、MnT 支撑 z* 的小函数）。
  * 产出：可单元测试的**微模型**（几行就能跑）。

* **拼（Stitch）**：

  * 把微模型拼进**真实模块**：差分线→`differential_*`，线性线→`linear_*`，搜索→best_search（本仓库在 `include/auto_search_frame/test_neoalzette_*_best_search.hpp`）。
  * 以 AI 做“胶水”，我做“审计”：接口、边界条件、异常路径全部列清。
  * 产出：**SOP + 验收项**，可复现。

**为什么要用 AI**：

* AI 像**随身研究助理**：快，擅长梳理脉络、生成草图、提醒边角（但它会自信地胡说，所以要管）。
* 我这边更像**审计/验收**：断言真伪、划定义域、补边界用例、把“看起来对”变成“可复现地对”。
* 两者迭代：提问越具体，反馈越稳。**“不被忽悠”= 保持怀疑 + 要证据 + 先做极小实现。**

---

## 13. Prompt Playbook（提问模板，拿去即用）

**P‑5 模板**：

1. **背景**：对象/年份/版本（例：Wallén‑2003 模加线性）；
2. **目标**：我想得到什么（例：z* 可行性判据 + 8 位示例）；
3. **输入**：精确符号/变量定义；
4. **约束**：只要论文口径、要位级伪代码、不要口号；
5. **输出**：列表/表格/代码片段 + 验收用例。

**示例**：

> “请用 Wallén‑2003 的口径给出 `z*` 定义和 `a⪯z* & b⪯z*` 的可行性检查，附 8‑bit 例子与 3 个边界用例；再给一段 15 行内的 C++ 伪实现，变量命名与 `linear_correlation_add_logn.hpp` 对齐。”

> 记住：**每问必验**。让它给**反例**和**越界输入**，我们拿来做单测。

---

## 14. 论文 ↔ 代码 ↔ 实验：闭环怎么跑？

1. **论文口径对齐**：AI 生成“校准版摘要 + 术语对照表”。
2. **微模型实现**：把关键算子（ψ、z*、Add‑Const）写成 10–30 行函数，配 8/16 位用例。
3. **拼装到主干**：差分→best_search（BnB），线性→best_search（BnB）；只改**适配层**，不碰核心逻辑。
4. **SOP 跑通**：小轮数、小 cap，确保最优结果稳定；开/关预表结果一致。
5. **记录与复核**：CSV 输出 + Monte‑Carlo 抽样；一旦偏离，回到步骤 2。

> 准则：**先对，再快**。对齐口径是刹车，缓存/并发/剪枝才是油门。

---

## 15. “串珠法”：十几篇论文各管哪一颗珠子？

* **核心算子**：

  * LM‑2001 → 差分 ψ / 前缀可行（`differential_xdp_add.hpp`）。
  * Wallén‑2003 → 线性 MnT 支撑 / 可行性 / 相关（`linear_correlation_add_logn.hpp`）。
  * Add‑Const 位向量/线性 → 常量加的精确模型（`differential_addconst.hpp` / `linear_correlation_addconst.hpp`）。
* **搜索与加速**：

  * Matsui/BnB → 本仓库的实现入口：`include/auto_search_frame/test_neoalzette_differential_best_search.hpp`、`include/auto_search_frame/test_neoalzette_linear_best_search.hpp`。
  * 缓存/记忆化（减少重复子问题）→ 同上两份 best_search 文件 + `common/runtime_component.*`。
  * MILP/MIQCP → 小尾巴的精细求解（可选）。
* **设计与规范**：

  * Alzette 2020 → 三步流水线的工程范式；
  * Sparkle 规范 → 实际部署与常量取值的工程口径。

> 用法：**每颗珠子只干一件事**，串起来就是“论文 → 代码 → 实验”的项链。

---

## 16. 心智模型与陷阱地图

* **误区**：把 var‑const 当 var‑var → **错估**传播；**对策**：走专用模型。
* **误区**：只盯单路径 → 线性会**低估**风险；**对策**：做 hull 聚合与抽样复核。
* **误区**：追“不靠谱的强数字”；**对策**：只说论文给的，其他用**趋势 + 验收**表达。
* **误区**：先求性能；**对策**：先跑 8/16 位极小用例，保证**对齐**再扩到 32/64 位。

---

## 17. 无痛学习“止痛片”清单

* **25 分钟番茄**：一个粒度只攻一个算子或一个断言。
* **每日一实验**：哪怕只跑 4 轮/低 cap，也要有曲线/CSV。
* **不懂就降维**：把 32 位问题缩到 8 位，先看边界与反例。
* **每周一复盘**：把本周学到的写成 10 条“对读者友好的断言”。

---

## 18. 协作分工：我 × AI × 代码 × 算力

* 我：定目标/画边界/拍板口径/写关键路径；
* AI：找材料/对齐术语/生成草图/写适配层/列测试；
* 代码：把算子模块化；
* 算力：HPC 或多核跑搜索。
  **规则**：任何结论进文档前，要么有**论文出处**，要么有**SOP 复现**。

---

## 19. Cheat Sheet（命令速查 + 参数建议）

```bash
# 差分
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  test_neoalzette_differential_best_search.cpp \
  src/neoalzette/neoalzette_core.cpp \
  common/runtime_component.cpp -o build/neoalzette_diff
./build/neoalzette_diff 4   # 起步：4 轮 / 保守 cap

# 线性
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  test_neoalzette_linear_best_search.cpp \
  src/neoalzette/neoalzette_core.cpp \
  common/runtime_component.cpp -o build/neoalzette_lin
./build/neoalzette_lin 6   # 观察 MELCC 随轮数衰减
```

**参数建议**：

* 先关高速路，确认结果；再开高速路，看加速但结果不变；
* `cap` 从低到高，记录一次**不会回头**的阈值；
* 任何异常，先缩位宽 + 打开断言日志。

---

## 20. 质量与伦理

* **可追溯**：每条结论都能指回论文或 CSV。
* **可复现**：SOP + 固定随机种子 + 版本化参数。
* **可撤回**：一旦发现口径冲突，标注“已撤回/更正”，不给读者留下悬置结论。

---

## 21. 结业自测（打完卡才算学会）

* [ ] 能在 8 位实现 ψ 和 z*，并构造 3 个反例；
* [ ] 能跑 4→6 轮差分与线性 SOP，并解释“为什么最优不变”；
* [ ] 能把 var‑const 与 var‑var 的差异讲给队友听，用 10 行代码演示；
* [ ] 能在 `include/auto_search_frame/test_neoalzette_*_best_search.hpp` 里换一种下界/剪枝策略，并评估影响；
* [ ] 能把一次 Alzette 的“三步互补”讲清楚且不编数字。

---

## 22.（可选）进度里程碑（不发勋章，发可复现成果）

* **口径检查**：发现并更正一处“不合论文口径”的表述（附引用或复现实验）。
* **SOP 复现**：写出一个最小可复现脚本，并确认他人可复用。
* **降维验证**：把 32 位问题缩到 8 位，写出反例/边界用例，并说明为何一致/不一致。
* **加速验证**：调整缓存/候选上限/线程数，确认“结果不变、速度更快”（用同一组参数对照）。

> 下一步如果要继续补工程：命令行参数 + CMake 样板 + Monte‑Carlo 复核脚本，三件一起做，效果最好（也最不容易自我感动）。

---

## 23. pDDT 和 cLAT 到底是啥？Matsui BnB 为啥看起来像“多级递归黑魔法”？

> 先别怕名词：我们一句话把轮廓拉清，再给一套“人话版” BnB。

### 23.1 pDDT 是什么？（p = precomputed/partial）

* **它存什么**：把某个**局部算子**（常见是模加 ⊞ 或“一个半轮”）的「**输入差分 → 可能的输出差分 + 权重（-log₂ 概率）**」**预先表**出来。
* **它为什么有用**：搜索时就不用临时推导一遍“这步可能走向哪几个γ、各自权重多少”，直接**O(1) 查表**；还能作为**前缀/后缀上界**。
* **我们项目里（现状）**：本仓库没有独立的 pDDT 生成模块；相近的“提速手段”主要是按位递归枚举 + weight cap + 缓存/记忆化（见 `include/auto_search_frame/test_neoalzette_differential_best_search.hpp`）。
* **你需要记住的**：pDDT = “**差分路标**”。先修好路标，后面导航就不迷路。

### 23.2 cLAT 是什么？（carry‑aware/constrained LAT）

* **它存什么**：对**线性逼近**的“**受进位约束**的 LAT（Linear Approximation Table）”，即在 **MnT 支撑**下，`(μ, ν)` 给定时，哪些 `ω`/下一步掩码组合是**可行**、**对应的相关度贡献**如何。
* **它为什么有用**：线性搜索常需要反复判断“掩码能不能走、走过去的相关度上界多少”。cLAT 把这步变成**O(1) 查表**，相当于给线性空间修了**高速路**。
* **我们项目里（现状）**：本仓库没有独立的 cLAT 工具链；线性侧的候选生成/缓存与 BnB 主要在 `include/auto_search_frame/test_neoalzette_linear_best_search.hpp` 中实现。
* **你需要记住的**：cLAT = “**线性高速路**”。先修路，再飞跑。

### 23.3 Matsui BnB：用人话、用非递归版说一遍

* **问题**：多轮搜索，有指数级分支；我们要**不漏最优**，还要**尽量少看烂路**。
* **核心想法**：

  1. 维护一个**当前最好** `best`；
  2. 展开某个节点前，先估个**“乐观下界”** `lower_bound`（通常来自可证明的下界表、快速估计或保守模型；实现上常配合缓存/记忆化）；
  3. 如果 `lower_bound` 是**可证明**的乐观下界，那么当 `cost_so_far + lower_bound ≥ best` 时，这条路不可能优于当前最好，可以直接**剪枝**；
  4. 否则就扩展孩子节点、继续评估。
* **不用深奥递归，栈/堆都行**：

```cpp
struct Node { int round; int cost; State s; };
int best = INF;                 // 当前已知的最优权重
std::vector<Node> stack = {root};
while (!stack.empty()) {
  Node cur = stack.back(); stack.pop_back();
  int lb = optimistic_lower_bound(cur.s, R - cur.round); // 下界估计（常配合缓存/记忆化）
  if (cur.cost + lb >= best) continue;                    // 剪！
  if (cur.round == R) { best = std::min(best, cur.cost); continue; }
  for (auto child : expand_with_tables(cur)) {            // O(1) 查表扩展
    stack.push_back(child);
  }
}
```

* **重点**：BnB 的“魔法”不在递归，而在**下界的质量** + **表驱动的扩展速度**。看懂这句，整篇论文都顺了。

---

## 24. 怎么把 ARX 分析算子装进我的框架，做成一个“黑盒”好使又不炸？

> 有人问：为啥接起来“不太痛”？答案其实很土：先**把边界划清楚**，再用“适配器（Adapter）+ 门面（Facade）”把复杂度包起来。

### 24.1 三层结构（Ports & Adapters）

1. **Operators（算子层）**：纯函数，不依赖全局（LM‑ψ、Add‑Const 位向量、MnT 支撑/相关度等）。
2. **Engine（引擎层）**：搜索器（Matsui BnB）+ 候选生成/缓存/记忆化，只和算子层说话。
3. **Facade（门面层）**：对外暴露**一个**统一入口：`analyze_differential(...)` / `analyze_linear(...)`。

### 24.2 最小接口（差分是正向的，线性可正/反向）

```cpp
// 差分（正向）：给起点差分与轮数，返回最优权重与路径
struct DiffQuery { uint32_t dA, dB; int rounds, cap; bool enable_cache=true; };
struct DiffHit   { int best_weight; /* + path/hull 可选 */ };
DiffHit analyze_differential(const DiffQuery& q);

// 线性（常用“前向 + 可行性”或“后向倒推”二合一门面）
struct LinQuery { uint32_t mA, mB; int rounds, cap; bool enable_cache=true; };
struct LinHit   { int best_corr_w; /* -log2 |corr| 的权重表示 */ };
LinHit analyze_linear(const LinQuery& q);
```

* **差分为什么好接**：它就是顺着轮函数**前向**走；把“该有的点”（算子与前缀可行）弄对了，再用下界/缓存做剪枝就行。
* **线性为什么容易晕**：有时需要**后向**思考（给末端掩码倒推可行前缀）；我们用 MnT 支撑判据 + 候选生成/缓存，把它封装成“你只管给起点，我来倒推/前推混合”。

### 24.3 适配层怎么写（别碰核心库）

* **做一层薄薄的 mapping**：

  * `NeoAlzette::step_differential()` 内部只调用 `differential_xdp_add.hpp` / `differential_addconst.hpp`，并给出“下一步候选差分 + 权重”；
  * `NeoAlzette::step_linear()` 内部只调用 `linear_correlation_add_logn.hpp` / `linear_correlation_addconst.hpp`（必要时再叠加候选缓存/记忆化）；
* **BnB 引擎**只拿这两个 API，不直接碰底层细节 → **黑盒一体化**。

### 24.4 验收脚本（你就看这三件事）

1. **一致性**：开/关缓存/记忆化/候选上限，**最优不变**；
2. **对称性**：对称起点/等价类标准化后，结果等价；
3. **可复现**：同一 seed/参数，结果恒定；CSV 记录完整。

> 这块其实也没啥玄学：线性我也被“反向推导”拷打过。后来发现把算子做成**纯函数**、把复杂度交给**表 + BnB**、把接口做成**一个门面**，事情就开始听话了。

---


到这里，这份导读就算能用了：可查、可跑、可复现。后面要扩内容，也建议先把“口径与验收”补齐，再谈加速与规模。
