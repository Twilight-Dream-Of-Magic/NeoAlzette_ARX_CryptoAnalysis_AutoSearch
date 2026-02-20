# A Bit-Vector Linear Correlation Model for Modular Addition by a Constant (BvCorr-FLAT)

**Author:** Twilight-Dream  
**Status:** engineering master draft aligned with the current paper and `linear_correlation/constant_weight_evaluation_flat.hpp`  
**Scope:** variable-plus-constant Walsh correlation, exact run-flattened evaluation, Binary-Lift, certified windowed restriction

---

## Abstract

We study the Walsh correlation of modular addition by a fixed constant,
$y = x \boxplus c \mod 2^n$,
and subtraction via two's complement.
The starting point is the classical carry-state formulation for modular addition, but the target is the fixed-constant branch rather than the full two-variable problem.

The core observation is structural.
Inside a maximal run of equal bits of the constant, the single-bit carry kernels indexed by $t_i = \\alpha_i \\oplus \\beta_i$ commute.
This allows every $\\beta=0$ interval to be collapsed into a closed-form update that depends only on counts of $t$ bits.
The only genuinely non-commuting events occur at positions where $\\beta_i = 1$; those positions are handled explicitly by a $\\beta$-sparse partition.
The result is an exact run-flattened evaluator.

On top of this exact core, we introduce a Binary-Lift that extracts the length-1 carry layer into a purely bitwise parity term and leaves a residual carry contribution for a windowed restricted evaluator.
The restricted evaluator is not a different model; it is the same exact run-flattened kernel applied only on the working set
$S = \\mathrm{supp}(t) \\cup \\mathrm{LeftBand}(\\beta,L)$.
This yields a deterministic absolute error bound
$|C(\\alpha,\\beta) - \\widehat C_L(\\alpha,\\beta)| \\le 2\\,\\mathrm{wt}(\\beta)\\,2^{-L}$,
and therefore a conservative linear-weight bound.

In the current C++20 implementation, one-shot calls internally reuse a thread-local cached run table keyed by $(c,n)$.
The exact path walks cached runs directly, while the windowed path first dispatches active runs and then selects among exact-span, count-only, sparse-event, and segmented per-run kernels.
This is the engineering realization of the same algebraic model.

---

## 1. Positioning

This document is the engineering master draft for BvCorr-FLAT.
It is deliberately not written as a second LaTeX paper clone.
Instead, it plays three roles at once:

- it states the mathematical model in a paper-compatible form,
- it explains how that model is realized in the current header-only implementation,
- it records the complexity split and implementation architecture in the same language used by the code.

The intended reading order is:

- paper for the short formal version,
- this Markdown for the engineering mother document,
- `linear_correlation/constant_weight_evaluation_flat.hpp` for the executable specification.

---

## 2. Problem statement

We work over $\{0,1\}^n$ with little-endian indexing.
For masks $\alpha,\beta \in \{0,1\}^n$ and
$y = x \boxplus c \bmod 2^n$,
define the Walsh correlation

$$
C(\alpha,\beta)
=
\mathbb E_{x \in \{0,1\}^n}
(-1)^{\alpha \cdot x \oplus \beta \cdot y}.
$$

Let

$$
t = \alpha \oplus \beta.
$$

If $s_0 = 0$ and

$$
s_{i+1} = \mathrm{maj}(x_i, c_i, s_i),
\qquad
y_i = x_i \oplus c_i \oplus s_i,
$$

then

$$
C(\alpha,\beta)
=
(-1)^{\beta \cdot c}
\mathbb E_x
(-1)^{t \cdot x \oplus \sum_i \beta_i s_i}.
$$

The fixed-constant branch is special because the carry recursion splits into maximal runs of equal bits of $c$.
Inside a 0-run one has an AND-chain,
inside a 1-run one has an OR-chain.
That run structure is the main lever used by BvCorr-FLAT.

---

## 3. Structural objects

### 3.1 Runs of the constant

Decompose the fixed constant $c$ into maximal intervals of equal bits:

$$
\mathcal R(c) = \{[a_k,b_k]\}_{k=1}^r,
\qquad r = \#\mathrm{runs}(c).
$$

Inside a 0-run,

$$
s_{i+1} = x_i \wedge s_i,
$$

and inside a 1-run,

$$
s_{i+1} = x_i \vee s_i.
$$

### 3.2 Carry-state row vector

The exact evaluator uses a two-state row vector $(V_0,V_1)$ indexed by the current carry state $s \in \{0,1\}$.
At each bit, the transition is one of four single-bit kernels:

- $K_0, K_1$ inside a 0-run,
- $K_0', K_1'$ inside a 1-run.

The important algebraic fact is run-local commutativity:

$$
[K_0,K_1] = 0,
\qquad
[K_0',K_1'] = 0.
$$

So as long as $\beta_i = 0$, the order of updates inside one run can be rearranged and collapsed into closed-form powers.

### 3.3 Beta-sparse partition

The non-commuting event is not the run boundary itself.
It is the position where $\beta_i = 1$.
At such a position, the sign flip

$$
D = \mathrm{diag}(1,-1)
$$

must be applied in place before the one-step kernel.
Therefore, each run is split into:

- $\beta=0$ intervals, processed by count-only closed forms,
- $\beta=1$ singletons, processed explicitly.

This is the exact flattened evaluator.

---

## 4. Exact evaluator

### 4.1 Exact algorithmic statement

For a fixed constant $c$, after run decomposition the exact evaluator does the following.

- Walk runs in order.
- Inside each run, split by the positions where $\beta_i = 1$.
- On each $\beta=0$ interval, count the number of $t=0$ and $t=1$ bits, then apply the corresponding closed-form powers.
- On each $\beta=1$ singleton, apply $D$ and then the corresponding one-bit kernel.
- Multiply by the global sign $(-1)^{\beta \cdot c}$.

The result is exact.
In code it is represented as a dyadic rational `DyadicCorrelation`.

### 4.2 Complexity model

BvCorr-FLAT is intentionally described with an offline/online split.

- **Offline on the fixed constant.** Run decomposition of $c$ into a cached run table.
- **Online for a query $(\alpha,\beta)$.** Exact evaluation driven by runs and $\beta$-synchronization points.

With the run table cached, the natural online complexity is

$$
O\bigl(\#\mathrm{runs}(c) + \mathrm{wt}(\beta)\bigr).
$$

This is not an unconditional worst-case sublinear claim in $n$.
It is a structure-sensitive claim in the fixed-constant model.
If $c$ alternates at every bit, then $\#\mathrm{runs}(c) = \Theta(n)$ and the exact evaluator reverts to linear worst-case behavior.
That is fine.
The point is that it scales with structural events, not blindly with bit width.

---

## 5. Windowed restricted evaluator

### 5.1 Working set

For a window length $L \ge 0$, define

$$
\mathrm{LeftBand}(\beta,L)
=
\bigcup_{i:\beta_i=1} ([i-L,i] \cap [0,n-1]).
$$

Then define the working set

$$
S = \mathrm{supp}(t) \cup \mathrm{LeftBand}(\beta,L).
$$

The windowed estimator $\widehat C_L$ is obtained by applying the same exact run-flattened kernel only on $S$ and ignoring the complement.
So the approximate side is a restricted exact side, not a different theory.

### 5.2 Certified error bound

The deterministic absolute error bound is

$$
|C(\alpha,\beta) - \widehat C_L(\alpha,\beta)|
\le 2\,\mathrm{wt}(\beta)\,2^{-L}.
$$

This immediately gives the conservative weight

$$
\widehat w_L
=
-\log_2 \Bigl( \max\{ |\widehat C_L| - \delta,\; 2^{-n} \} \Bigr),
\qquad
\delta = 2\,\mathrm{wt}(\beta)\,2^{-L}.
$$

### 5.3 Complexity statement

The correct way to read the windowed complexity is two-layered.

- **Fine active-set form**

$$
O\bigl(B(L,n) + \#\mathrm{runs}(c|_S) + \mathrm{wt}(\beta|_S)\bigr),
$$

where $B(L,n)$ is the cost of building `LeftBand`.

- **Coarse engineering form**

$$
O\bigl(\log n + \#\mathrm{runs}(c) + \mathrm{wt}(\beta)\bigr).
$$

In the current implementation, `LeftBand` is built by an exact doubling-style routine, so

$$
B(L,n) = O\bigl(\log \min\{L+1,n\}\bigr)
$$

in the word-RAM view.
This is the right place for the logarithm.
It does **not** belong to the exact online cost.

---

## 6. Binary-Lift

Binary-Lift extracts the length-1 carry layer into a parity term and leaves a residual pair $(t_{\mathrm{res}}, \beta_{\mathrm{res}})$ for the windowed residual evaluator.
It is a semantic transformation, not a standalone estimator.

The current code exposes this through:

- `binary_lift_addconst_masks(...)`
- `corr_add_const_binary_lifted_report(...)`

The returned display-layer masks are `(u,v,w)`, while the residual certified estimator lives inside the nested `WindowedCorrelationReport`.

The residual complexity and residual bound are computed with $\beta_{\mathrm{res}}$, not with the original $\beta$.
That is the whole point of the lift.

---

## 7. Implementation architecture (`linear_correlation/constant_weight_evaluation_flat.hpp`)

This section is the direct bridge from the mathematics to the current code.

### 7.1 Internal structural cache

The public API is one-shot and does **not** expose a manual precompute object.
That is a deliberate API choice.
Internally, however, the implementation does use an offline structural cache.

Current mechanism:

- `detail::FlatRunTable`
- `detail::build_flat_run_table(...)`
- `detail::get_flat_run_table_cached(...)`

The cache is thread-local and keyed by `(constant, n)`.
So repeated one-shot calls with the same constant naturally reuse the run decomposition.

### 7.2 Exact path

Current exact path:

- `linear_correlation_add_const_exact_flat_dyadic(...)`
- internal span/count helpers
- cached run-table walk

This is the executable realization of the exact flattened evaluator described above.

### 7.3 Windowed path

Current windowed path:

- `linear_correlation_add_const_flat_bin_report(...)`
- internal working-set evaluator on `S`
- active-run dispatch
- per-run choice among:
  - exact-span,
  - count-only,
  - sparse-event,
  - segmented fallback.

This is an event-driven implementation in two layers:

- first by active runs,
- then by intra-run event density.

That is why the engineering complexity is better described in active-set language than as a blind scan over all bits.

### 7.4 `LeftBand` builder

The current `left_band_u64(...)` is an exact doubling-style routine.
It constructs

$$
\bigvee_{d=0}^{L} (\beta >> d)
$$

without a naive $O(L)$ shift loop.
The implementation grows the covered distance from 1 to $L+1$ by repeatedly OR-ing a shifted copy of the already-covered band.
This is the code-level realization of the $B(L,n)$ term.

### 7.5 Reports and diagnostics

The current report structures are:

- `WindowedCorrelationReport`
- `BinaryLiftedWindowedReport`
- `CascadeReport`

Important conventions:

- `delta_bound` stores the certified **absolute** bound $2\,\mathrm{wt}(\beta)\,2^{-L}$.
- `working_set_mask` stores $S = \mathrm{supp}(\alpha \oplus \beta) \cup \mathrm{LeftBand}(\beta,L)$.
- `weight_conservative` is derived from $|\widehat C_L| - \delta$.

---

## 8. Public API summary

### 8.1 Exact

- `linear_correlation_add_const_exact_flat_dyadic(alpha, constant, beta, n)`
- `linear_correlation_add_const_exact_flat_ld(alpha, constant, beta, n)`
- `linear_correlation_add_const_exact_flat(alpha, constant, beta, n)`
- paper-order aliases `corr_add_const_exact_flat_* (alpha, beta, constant, n)`

### 8.2 Windowed

- `linear_correlation_add_const_flat_bin_report(alpha, constant, beta, n, L)`
- `linear_correlation_add_const_flat_bin_dyadic(...)`
- `linear_correlation_add_const_flat_bin_ld(...)`
- `linear_correlation_add_const_flat_bin(...)`
- paper-order alias `corr_add_const_flat_bin_report(alpha, beta, constant, n, L)`

### 8.3 Binary-Lift

- `binary_lift_addconst_masks(alpha, beta, constant, n)`
- `corr_add_const_binary_lifted_report(alpha, beta, constant, n, L)`

### 8.4 Cascades

- `corr_add_const_cascade(std::span<const CascadeRound>)`

---

## 9. Recommended usage rules

- Use exact-flat when you want exact correlation or when the working set covers most of the word.
- Use the windowed report when you want a certified approximate value and a conservative weight.
- Use Binary-Lift when $\beta$ is dense enough that reducing the residual mask weight is valuable.
- In cascades, treat the per-round errors additively under the product-estimator model.

Practical heuristics:

- If $L \ge n$, switch to exact.
- If the working-set coverage ratio is high, switch to exact.
- If $|\widehat C_L| \le 2\delta$, increase $L$ or fall back to exact.
- For Binary-Lift, enforce $L \ge 2$.

---

## 10. Regression and verification checklist

Recommended checks for future edits:

- exact-flat against exhaustive truth for small $n$,
- windowed full-mask degeneration against exact-flat,
- sparse-event path against segmented fallback,
- Binary-Lift residual path against non-lifted baselines,
- cascade error accumulation against per-round `delta_bound`,
- `n = 64` guards for masks and shifts,
- repeated same-constant queries to ensure the structural cache is still exercised.

---

## 11. What this document now claims, and what it does not claim

### Claimed

- exact run-flattened evaluation for the fixed-constant branch,
- explicit offline/online split for the constant-structured model,
- certified windowed restriction with deterministic absolute error,
- Binary-Lift as a semantic extraction of the length-1 carry layer,
- implementation architecture consistent with the mathematics.

### Not claimed

- unconditional worst-case sublinear exact complexity in $n$,
- a universal lower bound theorem for all imaginable machine models,
- Binary-Lift as a standalone estimator without the residual kernel,
- full two-variable modular-addition theory.

---

## 12. References

- Johan Wallén, *Linear Approximations of Addition Modulo $2^n$*, FSE 2003.
- Seyyed Arash Azimi et al., *A Bit-Vector Differential Model for the Modular Addition by a Constant*, ePrint 2020/1025.
- Hans Georg Schulte-Geers, *On CCZ-equivalence of addition mod $2^n$*.

---

## 13. Maintenance note

This Markdown is now aligned with:

- the short LaTeX paper in `bvcorr_flat_formal.tex`,
- the executable specification in `linear_correlation/constant_weight_evaluation_flat.hpp`.

If future edits change the internal implementation architecture, this document should be updated **before** the paper text is expanded again.
That keeps the engineering mother document ahead of the derived paper, not behind it.
