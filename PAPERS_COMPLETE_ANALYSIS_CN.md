# ARX密码分析核心论文完全理解指南（最完善版）

> **艾瑞卡的专属学习圣经** 📚✨  
> 从"啊???!!"到"ARX专家"的完整进化路径

---

## 0. 开场：现实点，我们要的到底是什么？

**目标**很朴素：

1. 把 ARX 的两条主线——**差分**与**线性**——讲到能跑；
2. 把论文里的“可证正确的”东西搬下来；
3. 把工程实现对齐到现有仓库；
4. 把中间那些“学到想哭”的坎，一个个填平。

> 这篇文章不是“堆材料”，而是“给指路 + 给路标 + 给交通工具”。路，是我走过的；你照走即可。

## 📋 **论文全览与核心价值链**

| 序号     | 论文                                                                                                    |        年份 | 核心突破              | 解决的根本问题                      | 在我们项目中的体现                                                                                            |
| ------ | ----------------------------------------------------------------------------------------------------- | --------: | ----------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| 🥇     | **Efficient Algorithms for Computing Differential Properties of Addition Modulo 2^n**（Lipmaa–Moriai）  |      2001 | ψ & 进位结构          | 模加差分从指数枚举降到**位级可算/可剪枝**      | `include/arx_analysis_operators/differential_xdp_add.hpp`；`pddt_*` 预表                                |
| 🥈     | **Linear Approximations of Addition Modulo 2^n**（Wallén）                                              |      2003 | MnT 支撑 `z*`       | 模加线性的**可行性与相关度**可计算          | `include/arx_analysis_operators/linear_correlation_add_logn.hpp`                                     |
| 🥉     | **A Bit-Vector Differential Model for Modular Addition by a Constant**（会议+扩展）                         | 2020–2021 | 位级精确建模            | `x←x+c` 的差分/线性**不能偷懒**，需专门模型 | `include/arx_analysis_operators/differential_addconst.hpp`；`linear_correlation_addconst.hpp`         |
| 4️⃣    | **Automatic Search for Differential Trails in ARX Ciphers**                                           |      2017 | BnB 搜索框架          | 多轮差分最优**不漏**且高效剪枝            | `include/arx_search_framework/matsui/matsui_algorithm2.hpp`；`src/.../matsui_algorithm2_complete.cpp` |
| 5️⃣    | **Automatic Search for the Best Trails in ARX – Application to Block Cipher Speck**                   | 2015–2016 | 搜索策略细化            | ARX 实例上的**最优轨道**实证           | 作为**搜索策略与验证案例**参照                                                                                    |
| 6️⃣    | **Automatic Search of Linear Trails in ARX (SPECK & Chaskey)**                                        | 2016–2017 | 线性搜索套路            | 线性掩码空间的**系统枚举与剪枝**           | `include/arx_search_framework/clat/*`（cLAT/SLR 框架）                                                   |
| 7️⃣    | **Automatic Search for the Linear (Hull) Characteristics of ARX Ciphers**（SPECK/SPARX/Chaskey/CHAM64） | 2017–2019 | 线性 **hull** 视角    | 相关度聚合/上界评估更稳健                | 线性主线的 **hull 口径**与实验对照                                                                               |
| 8️⃣    | **MILP-Based Automatic Search Algorithms for Differential and Linear Trails for Speck**               |      2016 | ARX-MILP 建模       | 约束化建模，便于**后缀求精**             | 作为可选思路（求解器后缀精化）                                                                                      |
| 9️⃣    | **Alzette: A 64-Bit ARX-box (feat. CRAX & TRAX)**                                                     |      2020 | 三步流水线             | 12 步、AES-级性质、工程范式            | `include/neoalzette/neoalzette_core.hpp` 等（🔖 **NeoAlzette 的参考来源**）                                  |
| 🔟     | **Sparkle 规范**（Schwaemm/Esch）                                                                         |      2020 | 工程化部署             | Alzette 在真实协议/原语中的落地         | 常量/接口参考与**应用语境**                                                                                     |
| 1️⃣1️⃣ | **（合并）线性/差分系列的补充材料**                                                                                  | 2016–2019 | cLAT/Highway 思想来源 | 把**后缀/支撑**变成 O(1) 查表         | `include/arx_search_framework/clat/*`（统一归并，不单列“Highway论文”）                                           |

---

---

## 1. 重要澄清（Alzette vs NeoAlzette）

## ❗ **重要澄清说明**

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
* **pDDT/搜索（Matsui BnB）**：乐观下界 + 阈值剪枝，保证不漏最优；速度看下界质量。

**线性线（Linear）**

* **Wallén‑2003**：MnT 支撑 `z*` 告诉你“哪些位可能被进位影响”；可行性 = 掩码受不受支撑。
* **Add‑Const（线性模型）**：常量改变支撑形态，不能偷懒当作 var‑var。所以我们不得不用Wallén通用矩阵。
* **cLAT/Highway**：给线性空间修“高速路”，后缀查询 O(1)。

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
* `src/arx_search_framework/pddt_algorithm1_complete.cpp`（差分表/先验）
* `src/arx_search_framework/matsui_algorithm2_complete.cpp`（Matsui BnB）
* `src/neoalzette/neoalzette_differential_step.cpp` + `include/neoalzette/neoalzette_differential_step.hpp`

### 3.4 差分 SOP（最小可跑）

```bash
# 编译
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  src/arx_search_framework/pddt_algorithm1_complete.cpp \
  src/arx_search_framework/matsui_algorithm2_complete.cpp \
  src/neoalzette/neoalzette_core.cpp \
  src/neoalzette/neoalzette_differential_step.cpp \
  src/neoalzette_differential_main_search.cpp -o build/neoalzette_diff

# 运行（默认 4 轮；也可传入轮数）
./build/neoalzette_diff      
./build/neoalzette_diff 5
```

**看什么**：打印的 `MEDCP (rounds=X): 2^{-Weight}` 是否随轮数上升（相关度衰减），最优权重“稳不稳”；调大 cap 只会更慢，不会“神奇更优”。

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
* `include/arx_search_framework/clat/algorithm1_const.hpp` / `clat_builder.hpp` / `clat_search.hpp`
* `src/neoalzette/neoalzette_linear_step.cpp` + `include/neoalzette/neoalzette_linear_step.hpp`

### 4.4 线性 SOP（最小可跑）

```bash
# 编译
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  src/neoalzette/neoalzette_core.cpp \
  src/neoalzette/neoalzette_linear_step.cpp \
  src/neoalzette_linear_main_search.cpp -o build/neoalzette_lin

# 运行
./build/neoalzette_lin      
./build/neoalzette_lin 6
```

**看什么**：打印的 `MELCC (rounds=X): 2^{-Weight}` 是否随轮数上升（相关度衰减），开/关 cLAT/Highway 结果一致（速度不同）。

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

🧠 **三步流水线的「互补哲学」与功能拆解** 🌀

- ➕ **加（模加）＝混淆**：作为唯一非线性来源，`x += (y ≫ rᵢ)` 通过进位传播制造跨位依赖，带来深度混淆。🔗 非线性（混淆）的核心就在这里！

- 🔄 **异或+旋转＝扩散**：通过 `y ^= (x ≫ sᵢ)` 把已有非线性「铺开」，实现线性扩散，避免旋转等价与镜像对称。🌊 快速把影响扩散开来，保持均衡。

- 💥 **常量＝破结构/稳节奏**：`x ^= c` 注入常量，拆掉潜在周期与线性/差分的相干叠加，给下一组输入以“新鲜度”。🆕 同时调控线性 hull 的符号叠加，破坏不良结构。

- 🎯 **旋转量选择的理据（升级版）**：
    - **理论理想**：在理想模型中，我们追求**黄金比例**（≈0.618）的扩散效果，因此倾向于选择接近 **1/4, 1/2, 3/4** 或更精细的 **1/8, 7/8** 等分数位置的旋转量，以实现远近交替、均衡的比特搅动。📐
    - **工程约束**：但在 `n` 比特的字长中，可选的旋转量只能是 **0 到 n-1** 的**整数**，无法实现完美的分数。
    - **核心原则**：因此，设计的精髓转变为：选择一组在数值上**互质（相对质数，co-prime）** 的旋转量。这能确保在连续应用中，比特被搅动到字内的各个位置，模拟了理想分数比例的扩散效果，并**最大程度地破坏了简单的线性对称性和短周期模式**。⚙️
    - **实例解读**：
        - 🌀 **31/24**：远距离、强扰动（接近字长或3/4），且与其它量互质。
        - ⚖️ **17**：中距离均衡，一个良好的质数选择。
        - ⚔️ **0**：直面进位链——提供最直接的非线性，但作为协调互质组合的一部分，用以打破模式单调性。
        - 🔄 **16**：半幅（1/2），与奇数旋转量形成互补。
    - **最终艺术**：通过组合使用这组**互质**的、覆盖远、中、近的旋转量，系统获得了丰富而非单调的计算路径，既能制造强大的混淆与扩散，又能有效抵御基于固定模式的密码分析。

---

## 6. 结果口径（只说论文允许的）

* 一次 Alzette 的差分/线性性质与 **AES S‑box** 可比；两次串联与 **AES super‑S‑box** 同等级。
* 多轮时，最优差分概率与最大绝对相关度**快速衰减**；这是分析与实验共同验证的**趋势**。
* 其他攻击（代数、积分、不可能差分、零相关）均有相应分析框架作为佐证。

> 注意：这里**不写未经论文明确给出的强数字**。我们只保留正确、稳健、可复核的表述。

---

## 7. 把论文变成代码：仓库一对一映射

* 差分主线：`differential_xdp_add.hpp` → `differential_addconst.hpp` → `pddt_*` → `matsui_algorithm2_*` → `neoalzette_differential_step.*`
* 线性主线：`linear_correlation_add_logn.hpp` → `linear_correlation_addconst.hpp` → `clat/*` → `neoalzette_linear_step.*`
* 公共：`neoalzette_core.*`（旋转/常量参数化）、`neoalzette_with_framework.hpp`（统一入口）。

**流程图（双线并行）**

```
差分：LM ψ/前缀 → (可选)pDDT → BnB(下界+阈值) → 最优路径/统计
线性：MnT 支撑 → cLAT/Highway → BnB/后缀求精 → MELCC/最优轨道
```

---

## 8. 读者常问（我当时也被坑过）

**Q1：为什么 var‑const 不能偷懒？**
A：常量位会塑造不同的进位/支撑形态；偷懒 = 漏路径/错评估。模型要分开。

**Q2：BnB 为什么“不会漏”？**
A：剪枝用的是**可证**的乐观下界；比你现在最好的还差，就没必要看了。

**Q3：cLAT/Highway 值得吗？**
A：当你要反复查“后缀/支撑/下界”时，它等价于给你修了一条高速路：一次构建，多次 O(1) 查询。

**Q4：实验怎么自证不忽悠？**
A：关掉/打开预表与剪枝，**最优结果一致**；做几组 Monte‑Carlo 抽样看趋势一致。

---

## 9. 一键复现：我给你的剧本

**差分线**：先 4 轮，cap 保守；稳定了再加轮数/放 cap。
**线性线**：先 4→6 轮，确认 MELCC 单调衰减；再把后缀交给 cLAT/Highway 提速。

> 复现实验都写在前面的 SOP 里，不赘述。

---

## 10. 还差什么？（下一步我能立刻做的）

* 给两个 `*_main_search.cpp` 加命令行参数（`--cap/--start-a/--start-b/--threads/...`）。
* 给仓库补一个 `CMakeLists.txt`（生成 `neoalzette_diff` / `neoalzette_lin`）。
* 附带一个 `tools/verify_mc.py`（Monte‑Carlo 抽样复核）。

> 你一句“继续”，我就把这三件事补齐，并把教学文档同步更新。

---

## 11. 终章：从“啊??!!”到“啊，原来这样”

* 别把“复杂”当成“神秘”——LM 把差分变成位条件，Wallén 把线性变成支撑检查；
* 别把“工程”当成“杂活”——BnB/cLAT/Highway 就是工程把数学落地；
* 别把“数值”当成“护身符”——只说论文给的，别自己编；
* 走过的路我已经铺好，你只需要：**按图施工**。

我们继续，下一步让它**更好用、更好看、更好跑**。🚀

---

## 12. 我怎么和 AI 一起读完十几篇论文？（墨镜奖章版）😎🏅

> 主题：把“看不动 → 看得懂 → 用得上”做成流程，而不是靠意志力硬扛。

**总原则（S3）**：**S**elect（选对材料）→ **S**implify（降维解释）→ **S**titch（拼到工程里）。

* 🥇 **选（Select）**：

  * 只选**原始论文/官方规格/一手实现**做基准（例如：LM‑2001、Wallén‑2003、Alzette 2020、Sparkle 规范）。
  * 给 AI 的提问要带**边界**：年份、对象、要验证的命题、希望的输出格式。
  * 产出：带引用的“口径对齐版摘要 + 待验证清单”。

* 🥈 **简（Simplify）**：

  * 让 AI 先用**小学数学**口吻讲一次，再用**研究生**口吻讲一次；两版对齐，暴露歧义点。
  * 把公式变成**位级布尔断言**或**极小代码片段**（例如 ψ、MnT 支撑 z* 的小函数）。
  * 产出：可单元测试的**微模型**（几行就能跑）。

* 🥉 **拼（Stitch）**：

  * 把微模型拼进**真实模块**：差分线→`differential_*`，线性线→`linear_*`，搜索→`matsui/clat`。
  * 以 AI 做“胶水”，我做“审计”：接口、边界条件、异常路径全部列清。
  * 产出：**SOP + 验收项**，可复现。

**为什么要用 AI？不是我不会写代码**：

* 🤖 AI 像**随身研究助理**：超快梳理脉络、生成草图、提醒边角；
* 🧑‍💻 我是**首席工程师**：断言真伪、下定义域、写关键路径；
* 🔁 两者迭代：我提问更准，AI 反馈更稳。**“不蠢”= 保持怀疑 + 要证据 + 先做极小实现。**

---

## 13. Prompt Playbook（提问模板，拿去即用）🗣️🎯

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

## 14. 论文 ↔ 代码 ↔ 实验：闭环怎么跑？♻️

1. **论文口径对齐**：AI 生成“校准版摘要 + 术语对照表”。
2. **微模型实现**：把关键算子（ψ、z*、Add‑Const）写成 10–30 行函数，配 8/16 位用例。
3. **拼装到主干**：差分→`pddt`/`matsui`，线性→`clat`；只改**适配层**，不碰核心逻辑。
4. **SOP 跑通**：小轮数、小 cap，确保最优结果稳定；开/关预表结果一致。
5. **记录与复核**：CSV 输出 + Monte‑Carlo 抽样；一旦偏离，回到步骤 2。

> 准则：**先对，再快**。对齐口径是刹车，Highway/cLAT 才是油门。🏎️

---

## 15. “串珠法”：十几篇论文各管哪一颗珠子？📿

* 😎 **核心算子**：

  * 🥇 LM‑2001 → 差分 ψ / 前缀可行（`differential_xdp_add.hpp`）。
  * 🥈 Wallén‑2003 → 线性 MnT 支撑 / 可行性 / 相关（`linear_correlation_add_logn.hpp`）。
  * 🥉 Add‑Const 位向量/线性 → 常量加的精确模型（`differential_addconst.hpp` / `linear_correlation_addconst.hpp`）。
* 🛣️ **搜索与加速**：

  * Matsui BnB → 最优不漏的搜索框架（`matsui_algorithm2_*`）。
  * cLAT/SLR/Highway → 后缀/支撑高速查询（`clat/*`）。
  * MILP/MIQCP → 小尾巴的精细求解（可选）。
* 🧩 **设计与规范**：

  * Alzette 2020 → 三步流水线的工程范式；
  * Sparkle 规范 → 实际部署与常量取值的工程口径。

> 用法：**每颗珠子只干一件事**，串起来就是“论文 → 代码 → 实验”的项链。

---

## 16. 心智模型与陷阱地图 🧠🗺️

* ❌ 把 var‑const 当 var‑var：**错估**传播；✅ 走专用模型。
* ❌ 只盯单路径：线性会**低估**风险；✅ 做 hull 聚合与抽样复核。
* ❌ 追“不靠谱的强数字”：✅ 只说论文给的，其他用**趋势 + 验收**表达。
* ❌ 先求性能：✅ 先跑 8/16 位极小用例，保证**对齐**再扩到 32/64 位。

---

## 17. 无痛学习“止痛片”清单 💊🍫

* 🍬 **25 分钟番茄**：一个粒度只攻一个算子或一个断言。
* 🧪 **每日一实验**：哪怕只跑 4 轮/低 cap，也要有曲线/CSV。
* 🧱 **不懂就降维**：把 32 位问题缩到 8 位，先看边界与反例。
* 📈 **每周一复盘**：把本周学到的写成 10 条“对读者友好的断言”。

---

## 18. 协作分工：我 × AI × 代码 × 算力 🤝

* 我：定目标/画边界/拍板口径/写关键路径；
* AI：找材料/对齐术语/生成草图/写适配层/列测试；
* 代码：把算子模块化；
* 算力：HPC 或多核跑搜索。
  **规则**：任何结论进文档前，要么有**论文出处**，要么有**SOP 复现**。

---

## 19. Cheat Sheet（命令速查 + 参数建议）⌨️

```bash
# 差分
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  src/arx_search_framework/pddt_algorithm1_complete.cpp \
  src/arx_search_framework/matsui_algorithm2_complete.cpp \
  src/neoalzette/neoalzette_core.cpp \
  src/neoalzette/neoalzette_differential_step.cpp \
  src/neoalzette_differential_main_search.cpp -o build/neoalzette_diff
./build/neoalzette_diff 4   # 起步：4 轮 / 保守 cap

# 线性
mkdir -p build && g++ -O3 -std=c++20 -Iinclude \
  src/neoalzette/neoalzette_core.cpp \
  src/neoalzette/neoalzette_linear_step.cpp \
  src/neoalzette_linear_main_search.cpp -o build/neoalzette_lin
./build/neoalzette_lin 6   # 观察 MELCC 随轮数衰减
```

**参数建议**：

* 先关高速路，确认结果；再开高速路，看加速但结果不变；
* `cap` 从低到高，记录一次**不会回头**的阈值；
* 任何异常，先缩位宽 + 打开断言日志。

---

## 20. 质量与伦理 🧭

* 📑 **可追溯**：每条结论都能指回论文或 CSV。
* 🔁 **可复现**：SOP + 固定随机种子 + 版本化参数。
* 🧯 **可撤回**：一旦发现口径冲突，标注“已撤回/更正”，不给读者留下悬置结论。

---

## 21. 结业自测（打完卡才算学会）🧪🏁

* [ ] 能在 8 位实现 ψ 和 z*，并构造 3 个反例；
* [ ] 能跑 4→6 轮差分与线性 SOP，并解释“为什么最优不变”；
* [ ] 能把 var‑const 与 var‑var 的差异讲给队友听，用 10 行代码演示；
* [ ] 能在 `matsui_algorithm2_*` 里换一种下界，并评估影响；
* [ ] 能把一次 Alzette 的“三步互补”讲清楚且不编数字。

---

## 22. 彩蛋：墨镜奖章系统 😎🏅

* 🥇 **口径骑士**：抓到一次“不合论文口径”的说法并改正；
* 🥈 **SOP 工匠**：写出一个最小可复现脚本，被他人复用 3 次；
* 🥉 **降维大师**：把 32 位问题成功缩到 8 位并解释差异；
* 🎖️ **高速路修建者**：把 cLAT/Highway 接入并验证“结果不变、速度更快”。

> 领完勋章，咱继续开图。下一步：我来把命令行参数与 CMake 样板加上，再把 Monte‑Carlo 复核脚本接进来，一起升级到“**一键跑 + 一键验**”。🚀

---

## 23. pDDT 和 cLAT 到底是啥？Matsui BnB 为啥看起来像“多级递归黑魔法”？😎🧠🛣️

> 先别怕名词，戴上墨镜，我们一句话把轮廓拉清，再给你一套“人话版” BnB。

### 23.1 pDDT 是什么？（p = precomputed/partial）📦

* **它存什么**：把某个**局部算子**（常见是模加 ⊞ 或“一个半轮”）的「**输入差分 → 可能的输出差分 + 权重（-log₂ 概率）**」**预先表**出来。
* **它为什么有用**：搜索时就不用临时推导一遍“这步可能走向哪几个γ、各自权重多少”，直接**O(1) 查表**；还能作为**前缀/后缀上界**。
* **我们项目里**：`src/arx_search_framework/pddt_algorithm1_complete.cpp` 构建的就是这种**按算子分块**的预计算差分分布表，供 BnB 快速扩展与剪枝。
* **你需要记住的**：pDDT = “**差分路标**”。先修好路标，后面导航就不迷路。🗺️

### 23.2 cLAT 是什么？（carry‑aware/constrained LAT）🎯

* **它存什么**：对**线性逼近**的“**受进位约束**的 LAT（Linear Approximation Table）”，即在 **MnT 支撑**下，`(μ, ν)` 给定时，哪些 `ω`/下一步掩码组合是**可行**、**对应的相关度贡献**如何。
* **它为什么有用**：线性搜索常需要反复判断“掩码能不能走、走过去的相关度上界多少”。cLAT 把这步变成**O(1) 查表**，相当于给线性空间修了**高速路**。🛣️
* **我们项目里**：`include/arx_search_framework/clat/*` 这票文件就是构建/查询 cLAT 的工具链。
* **你需要记住的**：cLAT = “**线性高速路**”。先修路，再飞跑。

### 23.3 Matsui BnB：用人话、用非递归版说一遍 🌳

* **问题**：多轮搜索，有指数级分支；我们要**不漏最优**，还要**尽量少看烂路**。
* **核心想法**：

  1. 维护一个**当前最好** `best`；
  2. 展开某个节点前，先估个**“乐观下界”** `lower_bound`（比如靠 pDDT/cLAT/Highway）；
  3. 如果 `cost_so_far + lower_bound ≥ best`，这条路**注定不可能超过**当前最好，直接**剪枝**；
  4. 否则就扩展孩子节点、继续评估。
* **不用深奥递归，栈/堆都行**：

```cpp
struct Node { int round; int cost; State s; };
int best = INF;                 // 当前已知的最优权重
std::vector<Node> stack = {root};
while (!stack.empty()) {
  Node cur = stack.back(); stack.pop_back();
  int lb = optimistic_lower_bound(cur.s, R - cur.round); // pDDT/cLAT/Highway
  if (cur.cost + lb >= best) continue;                    // 剪！
  if (cur.round == R) { best = std::min(best, cur.cost); continue; }
  for (auto child : expand_with_tables(cur)) {            // O(1) 查表扩展
    stack.push_back(child);
  }
}
```

* **重点**：BnB 的“魔法”不在递归，而在**下界的质量** + **表驱动的扩展速度**。看懂这句，整篇论文都顺了。🧩

---

## 24. 怎么把 ARX 分析算子装进我的框架，做成一个“黑盒”好使又不炸？🧰🧪

> 有人问：为啥你接得这么顺？答案其实很土：**把边界划清楚**，然后用“适配器（Adapter）+ 门面（Facade）”模式把复杂度包起来。

### 24.1 三层结构（Ports & Adapters）🏗️

1. **Operators（算子层）**：纯函数，不依赖全局（LM‑ψ、Add‑Const 位向量、MnT/cLAT）。
2. **Engine（引擎层）**：搜索器（Matsui BnB）+ 预表（pDDT/cLAT/Highway），只和算子层说话。
3. **Facade（门面层）**：对外暴露**一个**统一入口：`analyze_differential(...)` / `analyze_linear(...)`。

### 24.2 最小接口（差分是正向的，线性可正/反向）🧩

```cpp
// 差分（正向）：给起点差分与轮数，返回最优权重与路径
struct DiffQuery { uint32_t dA, dB; int rounds, cap; bool use_pddt=true; };
struct DiffHit   { int best_weight; /* + path/hull 可选 */ };
DiffHit analyze_differential(const DiffQuery& q);

// 线性（常用“前向 + 可行性”或“后向倒推”二合一门面）
struct LinQuery { uint32_t mA, mB; int rounds, cap; bool use_clat=true; };
struct LinHit   { int best_corr_w; /* -log2 |corr| 的权重表示 */ };
LinHit analyze_linear(const LinQuery& q);
```

* **差分为什么好接**：它就是顺着轮函数**前向**走，pDDT 自然拼上去；把“该有的点”（算子与前缀可行）弄对了就行。✅
* **线性为什么容易晕**：有时需要**后向**思考（给末端掩码倒推可行前缀），但我们有 cLAT/支撑判据，照样可以在门面里封装成“你只管给起点，我来倒推/前推混合”。

### 24.3 适配层怎么写（别碰核心库）🧷

* **做一层薄薄的 mapping**：

  * `NeoAlzette::step_differential()` 内部只调用 `differential_xdp_add.hpp` / `differential_addconst.hpp`，并给出“下一步候选差分 + 权重”；
  * `NeoAlzette::step_linear()` 内部只调用 `linear_correlation_add_logn.hpp` / `linear_correlation_addconst.hpp` 或 cLAT 查询；
* **BnB 引擎**只拿这两个 API，不直接碰底层细节 → **黑盒一体化**。

### 24.4 验收脚本（你就看这三件事）✅

1. **一致性**：开/关 pDDT/cLAT/Highway，**最优不变**；
2. **对称性**：对称起点/等价类标准化后，结果等价；
3. **可复现**：同一 seed/参数，结果恒定；CSV 记录完整。

> 我不牛逼，真的。线性我也被“反向推导”拷打过；但把算子做成**纯函数**、把复杂度交给**表 + BnB**、把接口做成**一个门面**，事情就开始听话了。😌

---


**🎉 这就是你的ARX密码分析完全参考手册！永远的学习伙伴！** 🚀✨
