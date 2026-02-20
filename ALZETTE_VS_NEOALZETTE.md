# Alzette vs NeoAlzette — what actually changed (and what the current evidence does / does not say)

> This doc exists for one reason: **stop the “read 8k LOC and guess what I meant” game**.
> It’s a design delta + modeling delta + evidence delta.  
> No hype, no fake % numbers, no “paper-grade bounds” claim unless we actually have them.

**Last updated: 2026-01-31**

---

## Preface (kept from v1)

Original v1 title: **Alzette vs NeoAlzette — design intuition, modeling notes, and reproducible evidence (WIP)**

> **What this is:** a living design memo + a record of what our scripts currently show.  
> **What this is NOT:** a formal security proof, nor a paper-quality differential/linear bound.

---

## 0) Two different “evidence tracks” (don’t mix them up)

### Track A — C++ best-trail search (the “real engine”)
- Repo contains **best-trail search code** for:
  - **XOR differential** best characteristics (branch-and-bound style search with pruning / memo / checkpoints).
  - **linear** best approximations / hull-related search (also best-search style).
- This is the part that generates numbers like your checkpoint:
  - `nodes_visited = 555,415,788,751` and best weight reaching around **2^-50**.
- **This is not something Python can “just run”** on a normal desktop. Python is not the bottleneck—**the search space is.**

### Track B — Python plotting / Monte-Carlo harness (the “trend probe”)
- Python exists for a different job: **Quick chart generation + Strict ARX model + Rapid trend comparison**.
- Important: it is **NOT “pure Monte-Carlo instead of analysis.”**
  - MC is the **outer sampling loop** (choose random inputs / deltas / trails under a fixed harness).
  - Inside that loop, you still apply your **ARX step operators / weight rules** (the “strict local model” part).
- So: Python plots are **small evidence (trend signal)**, not “paper-grade best-trail bounds.”

If someone says “You're using Monte Carlo methods exclusively.”，that’s simply a category error.

---

**Important nuance:** Track B is *not* “pure Monte Carlo handwaving.” The *sampling* is Monte Carlo, but the *local operators* it samples are the same ARX-step models (add/sub/xor/rot + injection abstraction) we use elsewhere. It’s still **weak evidence** by definition, because it’s sampling.

---

## Status and scope

- **Design goal:** keep the *Alzette-style pipeline feel* (confusion → diffusion → reset) while experimenting with *stronger coupling + faster diffusion*.
- **Claims policy:** any “+x bits” in this document is a **model-dependent score delta** produced by our current harness, **not** a proven bound.
- **Current evidence:** Monte Carlo *difference-domain* scoring runs (reproducible by script + seed).
- **Next step:** once experiments stabilize, replace “score evidence” with a reproducible report and (ideally) SAT/MILP-based trail search.

**Update (2026-01-31):**
- I do have both **differential best-trail search** and **linear best-trail search** implementations in this repo (see the best-search headers). They’re compute-limited on my current hardware: practically, I can push until around the “$2^{-50}$-ish” region and then it becomes a wall.
- The **Python charts** are not trying to replace those best-search engines. They exist because when someone demands “show me something reproducible *now*,” a Monte-Carlo trend probe is the fastest way to show whether a claimed direction is even plausible—*without pretending it’s a bound*.


---

## One-screen delta summary (Alzette → NeoAlzette)

### What stays the same (the family resemblance)
- 64-bit ARX-box style: state as two 32-bit words `(A, B)`.
- Round structure: still built from **add/sub**, **xor**, **rotations**, and **round constants**.
- Goal: keep a compact primitive that can be used as a mixing core inside higher-level designs.

### What NeoAlzette changes (the real differences)
Below is the “delta list” you can cite without forcing people to read code.

#### (D1) Constant subtraction is introduced in each subround
In `NeoAlzetteCore::forward`:
- `A -= RC[1];` in subround 1
- `B -= RC[6];` in subround 2

These are **add-by-constant (sub-by-constant)** operations that create their own carry constraints under XOR differences.

#### (D2) Cross-xor with rotations uses a different rotation pair
Still cross-mixing, but with a deliberate rotation choice:
- `CROSS_XOR_ROT_R0 = 23`, `CROSS_XOR_ROT_R1 = 16`
- With a safety assert that `(R0 + R1)` is odd to avoid large rotation fixed-point subspaces.

#### (D3) A cross-branch injection gadget is inserted (nonlinear bitwise layer)
After the core ARX steps in each subround, NeoAlzette injects a cross-branch “PRF-like” gadget:

- From **B → A**: `cd_injection_from_B(B, ...)`, then mix into `A`
- From **A → B**: `cd_injection_from_A(A, ...)`, then mix into `B`

Each injection internally uses:
- a **dynamic diffusion mask** built from **12 rotations** and XORs:
  - `generate_dynamic_diffusion_mask0/1(X) = XOR of rotl/rotr(X, (2, 3, 6, 9, 10, 13, 16, 17, 20, 24, 27, 31))`
- plus one boolean nonlinearity:
  - `~(B & mask0(B))` or `~(A | mask1(A))`
- plus two linear transforms `l1_forward/l2_forward` and a 16-bit rotate-mix inside the gadget.

This is the “quadratic gadget” that reviewers are reacting to: degree ≤ 2 in GF(2), but **not cheap** (lots of rotates).

#### (D4) Extra linear mixing layers (L1/L2) are used as part of the injection path
NeoAlzette applies `l1_forward/l2_forward` (and their inverses) to shape diffusion before/after injection.

So the injection is not “just a single AND”—it is a **small sub-structure**.

---


### High-level changes (narrative view; kept from v1)


We currently view NeoAlzette as:

- **Nonlinear budget (per box):** keep only *two* variable–variable modular adds per full box (similar intent: limit “nonlinear cost”).  
- **Carry “reset” / de-correlation:** insert *variable–constant* subtraction right after each var–var add (carry is perturbed without adding another var–var add).  
- **Stronger linear diffusion:** add cross-branch XOR/rotations and linear layers $L_1/L_2$ so low-weight differences spread faster.  
- **Cross-branch injection:** an extra coupling term derived from the other branch using AND/OR with dynamic masks (this part is “controversial” and must be modeled correctly; see below).

This should be read as: “the structure is more complex,” **not** “security is strictly higher.”

---

## 2) “Is this a normal S-box?” — terminology fix (and why it matters)

People hear “S-box” and default to “8-bit table with DDT/LAT.” That mental model breaks here.

- The injection gadget is a **32-bit boolean mapping with dynamic masks**.
- It is not a fixed lookup table, so “enumerate DDT/LAT” is not the right workflow.
- In this repo, the safe phrasing is:
  - **ARX-box / mixing primitive / injection gadget**, not “classic S-box”.

This is mostly a communication landmine: the analysis tooling is different.

---


### Background note kept from v1 (same topic, different angle)


In block-cipher jargon, an “S-box” is simply a fixed nonlinear substitution block.  
An ARX-box (like Alzette) is a deterministic mapping $(x,y) \mapsto (x',y')$ on 64-bit state, so it **is** a (large) S-box/permutation in that broad sense.

Calling it an S-box does **not** mean we assume “8-bit table lookup” behavior.  
It only means we analyze it as a *standalone nonlinear building block* for trail search / scoring.

---

## The intuition note I refuse to throw away (English rendering)

Why is the Alzette ARX-box strong?  
My core intuition is: **its 3-step pipeline works because variables and constants complement each other**.

For each pipeline stage (3 steps), each step both improves security and blocks certain structural attacks that you can already “see by inspection”:

1) `x ← x + (y >>> r0) mod 2^n`  
   - `y` is the *nonlinearity source* for `x` (carry interactions).  
   - the rotation helps prevent *carry-chain add/sub patterns* from stacking up too fast (not “forbidding” stacking — the original design allows accumulation — but avoiding overly trivial correlation).

2) `y ← y ⊕ (x >>> r1)`  
   - `x` already contains carry-based nonlinearity; now it is injected into `y`.  
   - XOR + rotation provides **linear diffusion**, and also helps block “rotation-equivalent” patterns that can make add/sub behave too cleanly.

3) `x ← x ⊕ rc`  
   - constant injection refreshes `x` for the next 3-step pipeline.  
   - it also desynchronizes structure and slows down overly clean carry-chain reuse across stages.

And yes: modular add/sub is the nonlinear “confusion” part (Shannon), while XOR+rotations are linear layers used for diffusion.  
If you miss that split, you miss why the pipeline order matters.

In our NeoAlzette design (relative to Alzette), we tried to keep the pipeline spirit but change the “reset / coupling / diffusion” balance. A legacy internal note claimed an increase of about **+1.3~1.5 bits per round** in our *internal differential difficulty score* (not a proof; see the “Legacy experiment” appendix).

### Original CN note kept verbatim (history)

<details>
<summary>Click to expand</summary>

```
我的原意 给我用英文按照我的原意，说清楚。:
Alzette ARX‑box为什么强? 秘密在于变量和常量互相互补它的流水线设计!
Alzette ARX‑box 的每一层的三步流水线设计极其精妙。每一步既提高了安全性，又在防御一些攻击。
比如已知能够看出的就有：
x ← x + (y >>> r0) mod n 对x应用y提供非线性+混淆来源，同时使用比特旋转防止进位模加模减链快速叠加（并不是阻止叠加，因为原始设计是允许的。）如果这里两个变量单独的用模加模减就会导致相关性以及模加进位模减借位链极其快速叠加
y ← y ⊕ (x >>> r1) 对y应用x，x已经带好非线性来源，用比特异或运算和比特旋转进行线性扩散(xor and bit-rotation)，同时防止等价模加模减的旋转攻击 
x ← x ⊕ rc 对x应用轮常量rc 使得x被更新，能在下一层三步流水线使用，并且减缓进位模加模减链快速叠加

自己他喵的看。然后我再强调一遍，模加模减运算是非线性函数是它带有混淆的作用，这符合香农提到的
线性函数叠加的线性层，比如说我们这里用的是一会运算和比特旋转，对吧？然后形成的类似于可能有矩阵高于空间这种的。这是干嘛的？这是扩散啊，你没搞懂吗？
所以对于我们的NeoAlzette 对比之下做了一下改进 我们的差分的困难性至少每轮增加1.3~1.5比特左右的模加模减的差分困难度
Alzette ARX-box 是 \textsc{{Sparkle}} 提交中定义的 64 位 ARX-box，用于轻量级密码算法。其核心思想是交替使用“\textsc{{Add of rotation}}”\bigl($x\leftarrow x+(y\ll r)$\bigr) 与“\textsc{{Xor of rotation}}”\bigl($x\leftarrow x\oplus (y\ll s)$\bigr)。原设计者指出，这些操作在 ARM 处理器上可在单个时钟周期完成，并通过在各轮选择不同旋转常数来提高扩散与抵抗差分/线性攻击的能力\cite{{SparkleSpecR2}}。最终采用四轮结构，理由是通过长轨策略可以从两次四轮的连接推导差分和线性上界，而更多轮数会降低效率\cite{{SparkleSpecR2}}。

NeoAlzette 的目标是在不增加非线性预算的前提下，提升差分统计强度并保持实现友好。为此在 Alzette 的基础上做出三处改动：
\begin{{enumerate}}
  \item \textbf{{进位断链：}}在每次变量--变量模加后立即执行一次变量--常量减法（如 \verb|A -= RC[i]|）。在 Lipmaa–Moriai 模型下，这等价于单输入加法，其差分概率仅依赖输入掩膜与常量位型\cite{{LM2001ADPAdd,Lipmaa2004ADPXor}}。这种“断链”阻断了前一加法产生的进位对下一步的影响。
  \item \textbf{{强化线性扩散：}}每个子轮后加入三角搅拌（\verb|A ^= rotl(B,24); B ^= ROTL(A,16)|）、SM4 风格的线性映射 $L_1$、$L_2$，并使用 \textsf{{CD}}\_A/\textsf{{CD}}\_B 模块做进一步的可逆扩散。原规范建议不同轮选用不同旋转常数以提高安全性\cite{{SparkleSpecR2}}；Neo-Alzette 采用的旋转集合\,$\{{31,17,24,16\}}$ 与常量序列配合，使低权重差分更快扩散。
  \item \textbf{{非线性预算不变：}}每轮仍只包含两次变量--变量模加，其余步骤均为 $\mathbb{{F}}_2$ 上的线性映射。
\end{{enumerate}}
这些修改旨在在 Lipmaa–Moriai 2001这篇论文的精确模型下每轮获得微小但稳定的增益，当多轮连接时，这种微增会线性累积，从而显著提高统计强度。

旧实验部分：

E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>neo_runner.py --src "NeoAlzette S-box (Constant Add) Differential Analysis _Experiment.py" --n 32 --R 4 --total 20000000 --chunk 50000 --seed 20251001 --out out_r4_n32 --resume
[i] Resuming...
[i] n=32, R=4, total=20000000, chunk=50000 -> batches=400
[batch 1/400] size=50000 median: Neo=130.0 Alz=124.0 Δ=6.0 p10Δ=5.0
  processed=50000 throughput≈8439.3 pairs/s ETA≈2364.0s
[batch 2/400] size=50000 median: Neo=130.0 Alz=124.0 Δ=6.0 p10Δ=5.0
  processed=100000 throughput≈8576.8 pairs/s ETA≈2320.2s
[batch 3/400] size=50000 median: Neo=130.0 Alz=124.0 Δ=6.0 p10Δ=5.0
[batch 400/400] size=50000 median: Neo=130.0 Alz=124.0 Δ=6.0 p10Δ=5.0
  processed=20000000 throughput≈6039.1 pairs/s ETA≈0.0s

==== SUMMARY ====
 R    count  median_new2sr  median_alzette  delta  p10_new2sr  p10_alzette  delta_p10  p90_new2sr  p90_alzette  mean_new2sr  mean_alzette  std_new2sr  std_alzette
 4 20000000          130.0           124.0    6.0       118.0        113.0        5.0       142.0        135.0   129.742672    124.001761    9.545113      8.67441

E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>
```

</details>

---

## How we model weights (what the repo is actually computing)

### Differential (XOR differences)
- Linear steps (rot/xor and linear layers): **weight 0**, just propagate differences.
- Modular add/sub steps: weight comes from carry constraints.
  - variable+variable addition: search/DP over carry patterns to pick the most likely output difference.
  - add/sub by constant: use a **bit-vector differential model** / exact counting DP for best output and integer weight.
- Injection gadget:
  - treat the boolean core as (at most) **quadratic** over GF(2).
  - for quadratic `f`, derivative `D_Δ f(x) = f(x) ⊕ f(x⊕Δ)` is **affine in x**:
    - `D_Δ f(x) = M_Δ x ⊕ c_Δ`
  - output difference set is an affine subspace; probability is `2^(-rank(M_Δ))` under uniform-x assumption.
  - so injection weight uses **rank(M_Δ)**.

> Key point: this is **not pretending injection is linear**.  
> It is using the derivative structure that quadratic functions guarantee.

### Linear (linear approximations / hull-ish)
Repo also has a linear best-search path; it is just compute-limited on local hardware.

---


### More detailed modeling notes (kept from v1; includes the injection-derivative argument)


Our scripts operate in the *difference domain* and assign a “weight” (in bits) to each nonlinear step, then add them up.

### Modular add/sub: what probability model?

- For **variable–variable add/sub** (mod $2^w$), we use a simplified best-output-difference search to estimate the best attainable differential probability under the chosen model.
- For **variable–constant subtraction** (e.g., `A -= RC[i]`), we treat it as a *single-input* modular operation whose differential probability depends on the input mask and the constant’s bit pattern (this aligns with the classic ADP-style modeling tradition).

Important: this is a *model*. It is not a published bound unless we fully specify and validate every assumption.

### Injection-layer modeling: why the old “linearity” check was misleading

If injection contains AND/OR with dynamic masks, the function $f(x)$ is typically **quadratic over $\mathbb{F}_2$**, hence not linear.

For quadratic Boolean functions, the derivative
$$
D_\Delta f(x)=f(x)\oplus f(x\oplus \Delta)
$$
is **affine in $x$** (linear part + constant offset). This is exactly what the updated script reports:

```
Branch B delta-map is NOT linear (expected for dynamic AND/OR).
Branch A delta-map is NOT linear (expected for dynamic AND/OR).
f is NOT linear (expected).
Derivative behaves affine (expected).
```

**Practical consequence:** do **not** model injection as a linear “delta-map” that depends only on $\Delta$.  
Instead, for each input difference $\Delta$, extract the affine transition:

- `offset = D_Δ f(0)`
- `linear basis = { D_Δ f(e_i) ⊕ offset }` for all basis bits $e_i`

Then you can compute rank / coset size correctly and use that as a score/constraint in search code.

(Reference implementation lives in `neoalzette_injection_affine_model.py` in this repo.)

---

## Reproducible experiment: N=20000,40000,80000 per round, seed=0

Environment: Python harness, difference-domain scoring, $w=32$, seed fixed.

Console summary (excerpt):

- `N per round = 20000,40000,80000`
- `R = 1..8`
- last-round mean(Neo-Alz) = `508.336` bits
- distribution at R=1: mean `62.585`, min `-23`, max `121`

### Results table (mean weights)

| boxes (rounds) | mean(Alzette) | mean(NeoAlzette) | mean(Neo−Alz) |
|---:|---:|---:|---:|
| 1 | 4.237 | 66.822 | 62.585 |
| 2 | 7.370 | 133.599 | 126.229 |
| 3 | 10.595 | 200.488 | 189.893 |
| 4 | 13.848 | 267.346 | 253.497 |
| 5 | 17.091 | 334.181 | 317.090 |
| 6 | 20.292 | 400.963 | 380.672 |
| 7 | 23.421 | 467.968 | 444.547 |
| 8 | 26.527 | 534.864 | 508.336 |

```
E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>neo_alzette_compare_plotfix.py
Branch B delta-map is NOT linear (expected for dynamic AND/OR).
Branch A delta-map is NOT linear (expected for dynamic AND/OR).
f is NOT linear (expected).
Derivative behaves affine (expected).
f is NOT linear (expected).
Derivative behaves affine (expected).
[rounds=1] mean_alz=4.237, mean_neo=66.822, mean_diff=62.585
[rounds=2] mean_alz=7.370, mean_neo=133.599, mean_diff=126.229
[rounds=3] mean_alz=10.595, mean_neo=200.488, mean_diff=189.893
[rounds=4] mean_alz=13.848, mean_neo=267.346, mean_diff=253.497
[rounds=5] mean_alz=17.091, mean_neo=334.181, mean_diff=317.090
[rounds=6] mean_alz=20.292, mean_neo=400.963, mean_diff=380.672
[rounds=7] mean_alz=23.421, mean_neo=467.968, mean_diff=444.547
[rounds=8] mean_alz=26.527, mean_neo=534.864, mean_diff=508.336
Round experiment saved to results_by_round.csv
[trend] Loaded 8 round-points from results_by_round.csv
[trend] N per round = 20000
[trend] seed = 0
[trend] last-round mean(Neo-Alz) = 508.336 bits
[trend] Plot saved to weight_diff_trend_2lines.png
Experiment completed for 20000 samples.
Average weight difference (NeoAlzette - Alzette): 62.585 bits
Min difference: -23.000 bits, Max difference: 121.000 bits
Plot saved to weight_difference.png

E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>neo_alzette_compare_plotfix.py
[rounds=1] mean_alz=4.237, mean_neo=66.820, mean_diff=62.583
[rounds=2] mean_alz=7.445, mean_neo=133.599, mean_diff=126.154
[rounds=3] mean_alz=10.730, mean_neo=200.483, mean_diff=189.753
[rounds=4] mean_alz=13.999, mean_neo=267.268, mean_diff=253.268
[rounds=5] mean_alz=17.222, mean_neo=334.113, mean_diff=316.891
[rounds=6] mean_alz=20.445, mean_neo=400.999, mean_diff=380.554
[rounds=7] mean_alz=23.613, mean_neo=467.957, mean_diff=444.344
[rounds=8] mean_alz=26.878, mean_neo=534.892, mean_diff=508.014
Round experiment saved to results_by_round.csv
[trend] Loaded 8 round-points from results_by_round.csv
[trend] N per round = 40000
[trend] seed = 0
[trend] last-round mean(Neo-Alz) = 508.014 bits
[trend] Plot saved to weight_diff_trend_2lines.png
Experiment completed for 40000 samples.
Average weight difference (NeoAlzette - Alzette): 62.583 bits
Min difference: -29.000 bits, Max difference: 123.000 bits
Plot saved to weight_difference.png

E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>neo_alzette_compare_plotfix.py
[rounds=1] mean_alz=4.318, mean_neo=66.863, mean_diff=62.545
[rounds=2] mean_alz=7.551, mean_neo=133.691, mean_diff=126.140
[rounds=3] mean_alz=10.793, mean_neo=200.579, mean_diff=189.786
[rounds=4] mean_alz=14.057, mean_neo=267.386, mean_diff=253.329
[rounds=5] mean_alz=17.255, mean_neo=334.218, mean_diff=316.963
[rounds=6] mean_alz=20.432, mean_neo=401.133, mean_diff=380.701
[rounds=7] mean_alz=23.597, mean_neo=468.035, mean_diff=444.438
[rounds=8] mean_alz=26.849, mean_neo=534.922, mean_diff=508.073
Round experiment saved to results_by_round.csv
[trend] Loaded 8 round-points from results_by_round.csv
[trend] N per round = 80000
[trend] seed = 0
[trend] last-round mean(Neo-Alz) = 508.073 bits
[trend] Plot saved to weight_diff_trend_2lines.png
Experiment completed for 80000 samples.
Average weight difference (NeoAlzette - Alzette): 62.545 bits
Min difference: -29.000 bits, Max difference: 123.000 bits
Plot saved to weight_difference.png

E:\[Twilight-Dream_Sparkle-Magical_Desktop-Data]\Crypto Markdown\NeoAlzette ARX S-Box>
```

### Interpretation (be careful!)

- These are **mean scores** under our harness.  
- The NeoAlzette score is currently much larger because the harness includes injection/coupling costs as “weight,” so this is not a like-for-like performance/security statement.
- The only safe claim is: **under this specific model + implementation, NeoAlzette produces larger weights on average than Alzette** for the sampled differences.

---

## Compute reality check (why “2^-50 then it dies” is not an excuse, it’s physics)

A reviewer who hasn’t run these searches tends to underestimate the scale.

Your checkpoint example (1 round) already shows the point:

- `best_weight` improved from 53 → 51 → 50
- `nodes_visited` grew to `555,415,788,751`
- elapsed time ~ 19,234 sec

That’s the kind of search cost that makes “just do 2^-100” sound like a joke.

So the repo’s current stance should be explicit:
- C++ best-search exists and is the intended rigorous route,
- but **on a single workstation** the reachable bound is limited (currently ~2^-50 scale).

---

**Concrete proof that “this is not Python-scale” (checkpoint snippet):**

```text
=== checkpoint ===
timestamp_local=2026-01-14 01:11:56
reason=init
rounds=1
start_delta_a=0x00000000
start_delta_b=0x00004049
best_weight=53
nodes_visited=0
elapsed_sec=0.000
trail_steps=1
R1 round_w=53 in_delta_a=0x00000000 in_delta_b=0x00004049 out_delta_a=0x6eddbe3d out_delta_b=0xd40b887b

=== checkpoint ===
timestamp_local=2026-01-14 01:12:10
reason=improved
rounds=1
start_delta_a=0x00000000
start_delta_b=0x00004049
best_weight=51
nodes_visited=215613545
elapsed_sec=14.204
trail_steps=1
R1 round_w=51 in_delta_a=0x00000000 in_delta_b=0x00004049 out_delta_a=0x00000000 out_delta_b=0xccbeaa7f

=== checkpoint ===
timestamp_local=2026-01-14 06:32:30
reason=improved
rounds=1
start_delta_a=0x00000000
start_delta_b=0x00004049
best_weight=50
nodes_visited=555415788751
elapsed_sec=19234.706
trail_steps=1
R1 round_w=50 in_delta_a=0x00000000 in_delta_b=0x00004049 out_delta_a=0x00000000 out_delta_b=0xcd3caa7f

```

That `nodes_visited` number (555,415,788,751) is exactly why the repo has a C++ best-search engine with pruning/memoization and why Python is used only as a plotting / sampling layer.


## Performance note (why 200k samples can “not run”)

The current harness cost is roughly $\mathcal{O}(N\cdot R)$ evaluations, each involving multiple bit-ops + branching logic in Python.  
Going from 20k → 200k is a strict 10× wall-clock increase. If the inner loop allocates objects / uses dicts / does Python-level branching, it can become “unusable” quickly.

The practical ceiling for pure Python often lands around **20k–40k** per round unless you:
- vectorize with NumPy,
- move hot loops to Cython/Numba,
- or cache/precompute per-constant DP tables for var–const steps.

---

## Efficiency critique: yes, it’s heavy (and that’s currently intentional)

NeoAlzette is not presented as “a minimal SPARKLE-like core.”  
Right now it behaves more like a **research platform** for “ARX + small nonlinear gadgets.”

So it’s fair for someone to say:
- you pay a lot (rotates, gadget cost),
- and you haven’t yet shown a paper-grade security advantage.

Both can be true.

The correct response is not denial; it’s: **“I agree on the cost, and this repo is about measuring the tradeoff.”**

---

---

## About AI, papers, and “You're not serious about your studies.”

The clean way to answer “Do you only know how to ask AI?” is boring but effective:

- **AI is used as an assistant**, not as a replacement for model definitions.
- The repo’s operators and search scaffolding are derived from standard ARX-analysis workflows (B&B / DP / MILP / SAT traditions), then engineered into code.
- I keep a local reading set covering:
  - ARX best-trail search (Speck-style branch-and-bound),
  - ARX linear trail / hull papers,
  - modular addition differential models (including by-constant),
  - SPARKLE / Alzette references,
  - and ChaCha/Salsa ARX analysis references.

If needed, you can literally point to your `papers/` folder snapshot (it exists, and it’s not empty).

---

**My local paper folder (so “you don’t read papers” is just factually wrong):**

```text
E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch\papers>dir
 驱动器 E 中的卷是 Application Library Multimedia
 卷的序列号是 E182-D523

 E:\[About Programming]\[CodeProjects]\C++\NeoAlzette_ARX_CryptoAnalysis_AutoSearch\papers 的目录

2026/01/15  20:48    <DIR>          .
2026/01/31  19:24    <DIR>          ..
2025/10/15  02:48         1,310,838 A Bit-Vector Differential Model for the Modular Addition by a Constant and its Applications to Differential and Impossible-Differential Cryptanalysis.pdf
2025/10/15  02:48           589,784 A Bit-Vector Differential Model for the Modular Addition by a Constant(978-3-030-64837-4_13).pdf
2025/10/15  02:48           980,375 A MIQCP-Based Automatic Search Algorithm for Differential-Linear Trails of ARX Ciphers(Long Paper).pdf
2025/10/15  02:48           802,356 Alzette A 64-Bit ARX-box (feat CRAX and TRAX) - Hal-Inria.pdf
2025/10/15  02:48           383,423 Automatic Search for Differential Trails in ARX Ciphers.pdf
2025/10/15  02:48           296,979 Automatic Search for the Best Trails in ARX - Application to Block Cipher Speck.pdf
2026/01/15  20:48            66,151 Automatic Search for the Best Trails in ARX - Application to Block Cipher Speck.txt
2025/10/15  02:48           372,682 Automatic search of linear trails in ARX with applications to SPECK and Chaskey(978-3-319-39555-5_26).pdf
2026/01/09  20:45           112,546 chacha-20080128.pdf
2026/01/09  20:45           511,288 cryptrec-ex-2601-2016.pdf
2025/10/15  02:48           284,018 Efficient Algorithms for Computing Differential Properties of Addition (springer 3-540-45473-x_28).pdf
2026/01/15  19:56            77,195 eprint-2019-1319-linear-hull-arx.txt
2025/10/15  02:48           213,955 Linear Approximations of Addition Modulo 2^n (springer 978-3-540-39887-5_20).pdf
2025/10/15  02:48           405,012 MILP-Based Automatic Search Algorithms for Differential and Linear Trails for Speck.pdf
2026/01/13  22:25           224,779 On CCZ-equivalence of addition mod $2^n$.pdf
2026/01/14  13:13            27,323 On CCZ-equivalence of addition mod 2^n.txt
2026/01/09  20:45           177,084 salsafamily-20071225.pdf
2025/10/15  02:48         1,063,298 sparkle-spec-final.pdf
2026/01/10  20:42            51,917 [eprint-iacr-org-2001-052] Differential Probability of Modular Addition with a Constant Operand.pdf
2026/01/09  20:45           241,629 [eprint-iacr-org-2013-328] Towards Finding Optimal Differential Characteristics for ARX Application to Salsa20.pdf
2026/01/09  20:45           337,142 [eprint-iacr-org-2014-613] A Security Analysis of the Composition of ChaCha20 and Poly1305.pdf
2026/01/09  20:45           243,060 [eprint-iacr-org-2017-472] New Features of Latin Dances Analysis of Salsa, ChaCha, and Rumba.pdf
2025/10/15  02:48           833,354 [eprint-iacr-org-2019-1319] Automatic Search for the Linear (hull) Characteristics of ARX Ciphers - Applied to SPECK, SPARX, Chaskey and CHAM-64.pdf
2026/01/13  22:22           391,254 [eprint-iacr-org-2021-224] Improved Linear Approximations to ARX Ciphers and Attacks Against ChaCha.pdf
              24 个文件      9,997,442 字节
               2 个目录 881,274,798,080 可用字节
```

Yes, I still ask AI questions sometimes — because time/energy is finite and humans are not build systems. But the *modeling choices* and *implementation* are mine, and the references above are literally what the repo is built around.


---

## Legacy numbers / rough % claims policy

If any doc still contains things like:
- “+1.3–1.5 bit per round”
- “30–50% / 50–70% improvement”

…then they must be treated as **legacy early-prototype observations** unless they are:
- backed by a reproducible script,
- with a fixed harness definition,
- and clear assumptions (what weight means, which trail model, which pruning, etc.).

In this doc, we do **not** present % as proof.

---


## Appendix: legacy results (kept for history; NOT rigorous)

This section exists only to preserve the historical claim trail. It is not used as evidence.

### Legacy claim snapshot (for context only)

- An earlier internal note (now treated as obsolete) said: “~+1.3 to +1.5 bits per round” under a previous scoring setup.
- The raw console log for that older run is preserved inside the “Original CN note kept verbatim” section above.

Do **not** cite this legacy number as a bound. It was a descriptive statistic from a prototype harness.

---

## Appendix: code anchors (for readers who *do* want to verify quickly)

- dynamic masks:
  - `generate_dynamic_diffusion_mask0/1()` in `neoalzette_core.cpp`
- injection gadget:
  - `cd_injection_from_B()`, `cd_injection_from_A()` in `neoalzette_core.cpp`
- main transform:
  - `NeoAlzetteCore::forward()` and `backward()`

---

_End of doc._

- best-trail search (differential): `test_neoalzette_differential_best_search.hpp` (current snapshot ~2159 lines)
- best-trail search (linear): `test_neoalzette_linear_best_search.hpp` (current snapshot ~2095 lines)


---

## TODO

- Replace “mean score” with confidence intervals + sensitivity to seed and sample size.
- Provide per-component breakdown: add/sub vs injection vs linear layers.
- Implement SAT/MILP trail search (reduced-round first) and publish best trails with reproducible scripts.
- Provide cycle/area cost estimates if we want to discuss “lightweight” seriously.

# Source Code 

## What NeoAlzette changes (high level)

We currently view NeoAlzette as:

- **Nonlinear budget (per box):** keep only *two* variable–variable modular adds per full box (similar intent: limit “nonlinear cost”).  
- **Carry “reset” / de-correlation:** insert *variable–constant* subtraction right after each var–var add (carry is perturbed without adding another var–var add).  
- **Stronger linear diffusion:** add cross-branch XOR/rotations and linear layers $L_1/L_2$ so low-weight differences spread faster.  
- **Cross-branch injection:** an extra coupling term derived from the other branch using AND/OR with dynamic masks (this part is “controversial” and must be modeled correctly; see below).

This should be read as: “the structure is more complex,” **not** “security is strictly higher.”

## Modeling notes: what our harness is actually doing

```


	// generate dynamic diffusion mask for NeoAlzette
	// 3 + 7 + 7 + 7 .... mod 32 generate 3,10,17,24,31,6,13,20,27,2,9,16

	std::uint32_t generate_dynamic_diffusion_mask0( std::uint32_t X ) noexcept
	{
		return NeoAlzetteCore::rotl( X, 2 ) ^ NeoAlzetteCore::rotl( X, 3 ) ^ NeoAlzetteCore::rotl( X, 6 ) ^ NeoAlzetteCore::rotl( X, 9 ) 
			^ NeoAlzetteCore::rotl( X, 10 ) ^ NeoAlzetteCore::rotl( X, 13 ) ^ NeoAlzetteCore::rotl( X, 16 ) ^ NeoAlzetteCore::rotl( X, 17 ) 
			^ NeoAlzetteCore::rotl( X, 20 ) ^ NeoAlzetteCore::rotl( X, 24 ) ^ NeoAlzetteCore::rotl( X, 27 ) ^ NeoAlzetteCore::rotl( X, 31 );
	}

	std::uint32_t generate_dynamic_diffusion_mask1( std::uint32_t X ) noexcept
	{
		return NeoAlzetteCore::rotr( X, 2 ) ^ NeoAlzetteCore::rotr( X, 3 ) ^ NeoAlzetteCore::rotr( X, 6 ) ^ NeoAlzetteCore::rotr( X, 9 ) 
			^ NeoAlzetteCore::rotr( X, 10 ) ^ NeoAlzetteCore::rotr( X, 13 ) ^ NeoAlzetteCore::rotr( X, 16 ) ^ NeoAlzetteCore::rotr( X, 17 ) 
			^ NeoAlzetteCore::rotr( X, 20 ) ^ NeoAlzetteCore::rotr( X, 24 ) ^ NeoAlzetteCore::rotr( X, 27 ) ^ NeoAlzetteCore::rotr( X, 31 );
	}

	// ============================================================================
	// Cross-branch injection (value domain with constants)
	// ============================================================================

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_B( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		std::uint32_t s_box_in_B = ( B ^ RC[ 2 ] ) ^ ( ~( B & generate_dynamic_diffusion_mask0( B ) ) );

		std::uint32_t c = NeoAlzetteCore::l2_forward( B );
		std::uint32_t d = NeoAlzetteCore::l1_forward( B ) ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_B;
		d ^= NeoAlzetteCore::rotr( t, 16 ) ^ rc1;
		return { c, d };
	}

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_A( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		std::uint32_t s_box_in_A = ( A ^ RC[ 7 ] ) ^ ( ~( A | generate_dynamic_diffusion_mask1( A ) ) );

		std::uint32_t c = NeoAlzetteCore::l1_forward( A );
		std::uint32_t d = NeoAlzetteCore::l2_forward( A ) ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_A;
		d ^= NeoAlzetteCore::rotl( t, 16 ) ^ rc1;
		return { c, d };
	}

	// ============================================================================
	// Main ARX-box transformations
	// ============================================================================

         // Cross XOR/ROT using (23, 16), ensuring (23+16)=39≡7(mod 32) is odd to prevent large rotations from freezing the subspace
         // Cross-branch injection: cd_injection_from_B / cd_injection_from_A
         // Linear layers participate in injection and outer layers through forward/backward combinations via l1/l2
         // After all, what I want is to enhance security at minimal cost in both the linear and nonlinear layers, and this must be achieved without altering the design performance overhead of the Alzette model—neither increasing nor decreasing it.

	void NeoAlzetteCore::forward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// First subround
		B += ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );
		A -= RC[ 1 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
			B = NeoAlzetteCore::l1_backward( B );
		}

		// Second subround
		A += ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );
		B -= RC[ 6 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
			A = NeoAlzetteCore::l2_backward( A );
		}

		// Final constant addition
		A ^= RC[ 10 ];
		B ^= RC[ 11 ];
		a = A;
		b = B;
	}

	void NeoAlzetteCore::backward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// Reverse final constant addition
		B ^= RC[ 11 ];
		A ^= RC[ 10 ];

		// Reverse second subround
		{
			A = NeoAlzetteCore::l2_forward( A );
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
		}
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B += RC[ 6 ];
		A -= ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );

		// Reverse first subround
		{
			B = NeoAlzetteCore::l1_forward( B );
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
		}
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A += RC[ 1 ];
		B -= ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );

		a = A;
		b = B;
	}
```

### 📚 **Complete Design of the Alzette ARX-box Algorithm Paper**

#### **Exact Algorithm Design from the Paper**:
```
Input/Output: (x, y) ∈ F₃₂² × F₃₂²
Input/ rc

x ← x + (y >>> 31)    // Modular addition: Applies y to x for nonlinear confusion source. Carry chain from modular addition, borrow chain from modular subtraction (but not a direct chain!!! Thus does not stack quickly due to bit rotation diffusion)
y ← y ⊕ (x >>> 24)    // XOR: Applies x to y, where x already carries the nonlinear source. Uses bitwise XOR and bit rotation for linear diffusion (XOR and bit-rotation), while preventing rotation attacks equivalent to modulo addition/subtraction
x ← x ⊕ rc         // XOR constant: Apply round constant rc to x to update it (reset the modular addition/subtraction chain state), enabling use in the next three-step pipeline stage while slowing down rapid accumulation of carry-based chains

x ← x + (y >>> 17)
y ← y ⊕ (x >>> 17)
x ← x ⊕ rc          // Same reset

x ← x + (y >>> 0) // Modular addition: Maximizes carry-in/borrow chains
y ← y ⊕ (x >>> 31)
x ← x ⊕ rc         // Same Reset

x ← x + (y >>> 24)    // Final confusion
y ← y ⊕ (x >>> 16)    // Final diffusion
x ← x ⊕ rc          // Final reset

return (x, y)
```

#### **Key Security Analysis Points in the Paper**:

**1. Differential Analysis Resistance**:
```
Paper Proof: Through meticulous differential propagation analysis
- Single-round Alzette: Differential probability lower bound 2^{-6}
- Double-round Alzette: Achieves AES super-S-box security level
- Multi-round: Exponential security growth
```

**2. Linear Analysis Resistance**:
```
Paper Proof: Using Wallén model analysis
- Rapid decay of correlation under linear approximations
- Branch count guarantees provide diffusion lower bounds
- Rotation quantity selection optimizes linear resistance
```

**3. Resistance to Algebraic Attacks**:
```
Paper Analysis: Degree Growth Analysis
- Modular addition operations per round increase algebraic degree
- Constant injection prevents degree saturation
- Approaches maximum degree after multiple rounds
```
