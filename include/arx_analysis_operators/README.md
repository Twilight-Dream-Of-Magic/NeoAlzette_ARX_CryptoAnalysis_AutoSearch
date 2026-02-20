# ARX Analysis Operators

Last reviewed against the current tree: **2026-04-16**

This directory contains the ARX operator implementations used by the NeoAlzette search code. The focus is modular addition and addition/subtraction by a constant under:

- XOR differentials
- linear correlations
- Q1 evaluators for fixed masks/differences
- Q2 helpers that optimize the opposite side when one side is fixed

The primary public namespace is `TwilightDream::arx_operators`. The auxiliary namespace `TwilightDream::bitvector` is used by the differential add-constant evaluator for reusable bit-vector primitives.

## Scope

The current operator tree is organized by analysis domain:

- `differential_probability/`
- `linear_correlation/`

Most evaluators are header-only. The main exception is the exact var-const linear Q2 solver:

- `linear_correlation/constant_optimal_alpha.hpp`
- `linear_correlation/constant_optimal_beta.hpp`

Those two headers declare the public API, while their compiled cores live in:

- `src/arx_analysis_operators/linear_correlation/constant_fixed_beta_core.cpp`
- `src/arx_analysis_operators/linear_correlation/constant_fixed_alpha_core.cpp`

## Current File Layout

### Shared support

| Path | Role |
| --- | --- |
| `DefineSearchWeight.hpp` | Defines `SearchWeight` and the common sentinel values used by the search framework. |
| `math_util.hpp` | Small arithmetic helpers such as modular negation `neg_mod_2n<T>(...)`. |
| `SignedInteger128Bit.hpp` | Fixed-width signed 128-bit helper used by exact numerator paths. |
| `UnsignedInteger128Bit.hpp` | Fixed-width unsigned 128-bit helper used by exact count paths. |

### Differential operators

| Path | Status | Main role |
| --- | --- | --- |
| `differential_probability/weight_evaluation.hpp` | public | Exact LM-2001 XOR-differential weight/probability/feasibility for var-var modular addition. |
| `differential_probability/optimal_gamma.hpp` | public | LM-2001 Algorithm 4: construct the output difference `gamma*` that maximizes `DP+`. |
| `differential_probability/constant_weight_evaluation.hpp` | public | Exact and approximate XOR-differential evaluation for add/sub by constant, including exact count/DP/weight, log2-pi exact weight, and approximate `BvWeight^k`. |
| `differential_probability/constant_optimal_q2_common.hpp` | internal support | Shared frontier/count utilities for constant Q2 differential generators. |
| `differential_probability/constant_optimal_input_alpha.hpp` | public | Fixed-input-delta Q2 solver: given `delta_x`, find the best `delta_y`. |
| `differential_probability/constant_optimal_output_beta.hpp` | public | Fixed-output-delta Q2 solver: given `delta_y`, find the best `delta_x`. |

### Linear operators

| Path | Status | Main role |
| --- | --- | --- |
| `linear_correlation/weight_evaluation.hpp` | public | Var-var linear correlation: Wallen CPM routes plus exact matrix-chain add/sub evaluators. |
| `linear_correlation/weight_evaluation_ccz.hpp` | public | Explicit Schulte-Geers / CCZ formulas for differential and linear addition, including row/column maximizers. |
| `linear_correlation/constant_weight_evaluation.hpp` | public | Exact var-const linear correlation for add/sub by constant; this is the current Q1 hot path for subtraction-by-constant. |
| `linear_correlation/constant_weight_evaluation_flat.hpp` | public specialist path | Run-flattened exact/windowed var-const evaluator, plus binary-lift and cascade experiment helpers. |
| `linear_correlation/constant_optimal_alpha.hpp` | public declarations | Fixed-beta exact var-const Q2 API and shared Q2 types. |
| `linear_correlation/constant_optimal_beta.hpp` | public declarations | Fixed-alpha exact var-const Q2 API. |
| `linear_correlation/fixed_alpha_research_debug.hpp` | research/debug | Diagnostics for fixed-alpha Q2 work. |
| `linear_correlation/fixed_beta_research_debug.hpp` | research/debug | Diagnostics for fixed-beta Q2 work. |

## Legacy Name Mapping

Older notes in this workspace still mention flat header names that are no longer the current include paths. Use the following mapping:

| Legacy name | Current path |
| --- | --- |
| `differential_xdp_add.hpp` | `differential_probability/weight_evaluation.hpp` |
| `differential_optimal_gamma.hpp` | `differential_probability/optimal_gamma.hpp` |
| `differential_addconst.hpp` | `differential_probability/constant_weight_evaluation.hpp` |
| `linear_correlation_add_logn.hpp` | `linear_correlation/weight_evaluation.hpp` |
| `linear_correlation_addconst.hpp` | `linear_correlation/constant_weight_evaluation.hpp` |
| `modular_addition_ccz.hpp` | `linear_correlation/weight_evaluation_ccz.hpp` |

The current README is intentionally written against the headers that actually exist in `include/arx_analysis_operators/`.

## How The Search Code Uses These Operators Today

This section describes the current wiring in the search framework, not just mathematical capability.

### Differential search

- `include/auto_search_frame/detail/differential_best_search_math.hpp` includes:
  - `differential_probability/weight_evaluation.hpp`
  - `differential_probability/optimal_gamma.hpp`
  - `differential_probability/constant_weight_evaluation.hpp`
- `src/auto_search_frame/differential_best_search_math.cpp` uses:
  - `find_optimal_gamma_with_weight(...)` for greedy initialization and per-addition local optimization
  - `diff_subconst_exact_weight_ceil_int(...)` for exact subtraction-by-constant scoring
- `src/auto_search_frame_bnb_detail/differential/varvar_weight_sliced_pddt_q2.cpp` implements the rebuildable weight-sliced pDDT accelerator:
  - exact shell generation is still defined by `xdp_add_lm2001_n(...)`
  - cached shell semantics are `S_t(alpha,beta) = { gamma | w(alpha,beta -> gamma) = t }`
- `differential_probability/constant_optimal_input_alpha.hpp` and `differential_probability/constant_optimal_output_beta.hpp` are operator-level exact Q2 generators. In the current tree they are primarily used by self-tests and research/audit programs, not by the listed main differential BnB hot path.

### Linear search

- `include/auto_search_frame/detail/linear_best_search_math.hpp` includes:
  - `linear_correlation/constant_weight_evaluation.hpp`
  - `linear_correlation/weight_evaluation.hpp`
  - `linear_correlation/weight_evaluation_ccz.hpp`
- `src/auto_search_frame_bnb_detail/polarity/linear/varconst/varconst_q1.cpp` uses `correlation_sub_const_weight_ceil_int_logdepth(...)` as the exact Q1 evaluator for subtraction by a constant.
- `src/auto_search_frame_bnb_detail/polarity/linear/varconst/fixed_alpha_q2.cpp` uses `find_optimal_beta_varconst_mod_sub(...)` for fixed-alpha Q2.
- `src/auto_search_frame_bnb_detail/polarity/linear/varconst/fixed_beta_q2.cpp` uses `find_optimal_alpha_varconst_mod_sub(...)` for fixed-beta Q2.
- `src/auto_search_frame_bnb_detail/polarity/linear/varvar/varvar_q1.cpp` uses `linear_correlation_add_ccz_weight(...)` as the exact var-var Q1 backend.
- `src/auto_search_frame_bnb_detail/polarity/linear/varvar/fixed_u_q2.cpp` prepares row-side `(v,w)` candidates for fixed `u`. Depending on configuration it uses:
  - split-8 exact streaming
  - weight-sliced cLAT shell streaming
  - materialized candidate lists
- `src/auto_search_frame_bnb_detail/polarity/linear/varvar/fixed_vw_q2.cpp` uses `linear_correlation_add_phi2_column_max(...)` to obtain the exact column-optimal `u*` for fixed `(v,w)`.
- `src/auto_search_frame_bnb_detail/linear/varvar_z_shell_weight_sliced_clat_q2.cpp` is the split-8 / z-shell enumerator used by the row-side exact shell path. Candidate shells are finalized with `linear_correlation_add_ccz_weight(...)`.
- `linear_correlation/weight_evaluation.hpp` is still kept nearby as a fallback/regression backend, but the default var-var linear hot path is the CCZ backend in `linear_correlation/weight_evaluation_ccz.hpp`.
- `linear_correlation/constant_weight_evaluation_flat.hpp` is not the default BnB hot path. It is the specialist exact/windowed evaluator for audits, experiments, and self-tests.

## Differential Operator Notes

### `differential_probability/weight_evaluation.hpp`

Purpose:

- Exact XOR-differential weight for `z = x boxplus y` under LM-2001.

Key API:

- `psi(...)`
- `psi_with_mask(...)`
- `xdp_add_lm2001(alpha, beta, gamma)`
- `xdp_add_lm2001_n(alpha, beta, gamma, n)`
- `xdp_add_probability(alpha, beta, gamma)`
- `is_xdp_add_possible(alpha, beta, gamma)`

Notes:

- The public exact weight backend is integer-valued `SearchWeight`.
- The `*_n` form supports widths `1..32`.
- Infeasible transitions return `INFINITE_WEIGHT` or `0.0`, depending on the API.

### `differential_probability/optimal_gamma.hpp`

Purpose:

- Construct `gamma* = argmax_gamma DP+(alpha, beta -> gamma)` without enumerating all outputs.

Key API:

- `carry_aop(...)`
- `aop(...)`
- `aopr(...)`
- `bitreverse_n(...)`
- `find_optimal_gamma<WordT>(...)`
- `find_optimal_gamma32(...)`
- `find_optimal_gamma64(...)`
- `find_optimal_gamma128(...)`
- `find_optimal_gamma_with_weight(alpha, beta, n)`

Notes:

- Generic bit helpers are implemented for `uint32_t`, `uint64_t`, and `UnsignedInteger128Bit`.
- The convenience `find_optimal_gamma_with_weight(...)` wrapper is currently provided for the 32-bit XDP backend family.

### `differential_probability/constant_weight_evaluation.hpp`

Purpose:

- Evaluate XOR differentials for `y = x boxplus a` and `y = x boxminus a`.

Main result families:

- exact count / probability / weight
- exact closed-form weight via the log2-pi decomposition
- approximate `BvWeight^k` fixed-point evaluators

Representative API:

- `is_diff_addconst_possible_n(...)`
- `diff_addconst_exact_count_n(...)`
- `diff_addconst_exact_probability_n(...)`
- `diff_addconst_exact_weight_n(...)`
- `diff_addconst_exact_weight_ceil_int_n(...)`
- `diff_addconst_weight_log2pi_n(...)`
- `diff_addconst_bvweight_fixed_point_n(...)`
- `diff_addconst_bvweight_q4_n(...)`
- `diff_subconst_exact_count_n(...)`
- `diff_subconst_exact_probability_n(...)`
- `diff_subconst_exact_weight_n(...)`
- `diff_subconst_exact_weight_ceil_int(...)`
- `diff_addconst_probability(...)`
- `diff_subconst_probability(...)`

Notes:

- Public overloads exist for both 32-bit and 64-bit carriers.
- The exact count type is widened automatically:
  - `uint64_t` count for 32-bit inputs
  - `UnsignedInteger128Bit` count for 64-bit inputs
- `TwilightDream::bitvector` in this file exposes reusable primitives such as:
  - `HammingWeight`
  - `BitReverse`
  - `Carry`
  - `LeadingZeros`
  - `ParallelLog`
  - `ParallelTrunc`

### `differential_probability/constant_optimal_input_alpha.hpp`

Purpose:

- Fixed-input-delta Q2 generator for add/sub by constant.

Public result type:

- `DiffConstOptimalOutputDeltaResult`

Representative API:

- `find_optimal_output_delta_addconst_exact_reference(...)`
- `find_optimal_output_delta_subconst_exact_reference(...)`
- `find_optimal_output_delta_addconst_phase8_banded_support_prototype(...)`
- `find_optimal_output_delta_subconst_phase8_banded_support_prototype(...)`
- `find_optimal_output_delta_addconst(...)`
- `find_optimal_output_delta_subconst(...)`

Notes:

- The exact reference path is based on suffix response-vector Pareto frontiers and exact-count verification.
- The prototype path adds a banded-support cache for the fixed-input direction.

### `differential_probability/constant_optimal_output_beta.hpp`

Purpose:

- Fixed-output-delta Q2 generator for add/sub by constant.

Public result type:

- `DiffConstOptimalInputDeltaResult`

Representative API:

- `find_optimal_input_delta_addconst_exact_reference(...)`
- `find_optimal_input_delta_subconst_exact_reference(...)`
- `find_optimal_input_delta_addconst(...)`
- `find_optimal_input_delta_subconst(...)`

Notes:

- This direction currently exposes the exact-reference solver family.

### `differential_probability/constant_optimal_q2_common.hpp`

Purpose:

- Shared support code for the constant Q2 differential generators.

Important shared utilities:

- width masking and normalization helpers
- exact-count conversion helpers
- Pareto-frontier pruning
- lightweight statistics updates

## Linear Operator Notes

### `linear_correlation/weight_evaluation.hpp`

Purpose:

- Var-var linear correlation only.

It contains two families of evaluators:

- Wallen CPM routes for fixed `(u,v,w)`
- exact carry-transfer-matrix add/sub evaluators for var-var modular addition/subtraction

Representative API:

- `wallen_weight(mu, nu, omega, n)`
- `compute_cpm_recursive(x, y, n)`
- `compute_cpm_logn_bitsliced(x, y, n)`
- `compute_cpm_logn(x, y, n)`
- `internal_addition_wallen_logn(u, v, w)`
- `linear_correlation_add_value_logn(u, v, w)`
- `linear_correlation_add_logn32(u, v, w)`
- `correlation_add_varvar(...)`
- `correlation_sub_varvar(...)`
- `linear_add_varvar32(...)`
- `linear_add_varvar64(...)`
- `linear_sub_varvar32(...)`
- `linear_sub_varvar64(...)`

Notes:

- `compute_cpm_*` supports widths up to 64 bits.
- The Wallen route is valuable both as a direct evaluator and as a regression/reference backend.

### `linear_correlation/weight_evaluation_ccz.hpp`

Purpose:

- Explicit Schulte-Geers / CCZ formulas for modular addition.

It provides both:

- explicit differential formulas
- explicit Walsh / linear-correlation formulas

Representative API:

- `mask_n(...)`
- `L_of(...)`
- `R_of(...)`
- `M_of(...)`
- `MnT_of(...)`
- `differential_probability_add_ccz_value(...)`
- `differential_probability_add_ccz_weight(...)`
- `differential_equation_add_ccz_solvable(...)`
- `linear_correlation_add_ccz_value(...)`
- `linear_correlation_add_ccz_weight(...)`
- `row_best_correlation_value(...)`
- `column_best_correlation_value(...)`
- `linear_correlation_add_phi2_row_max(...)`
- `linear_correlation_add_phi2_column_max(...)`
- `find_optimal_output_u_ccz(v, w, n)`

Notes:

- This is the current exact var-var linear hot path in the search code.
- The row/column maximizers are the linear-side analog of "fix one side, optimize the other side" operator design.

### `linear_correlation/constant_weight_evaluation.hpp`

Purpose:

- Exact linear correlation for:
  - `z = x boxplus a`
  - `z = x boxminus a`

Public result type:

- `LinearCorrelation { correlation, weight, is_feasible() }`

Representative API:

- `correlation_add_const(...)`
- `correlation_sub_const(...)`
- `correlation_add_const_exact_numerator_logdepth(...)`
- `correlation_add_const_weight_ceil_int_logdepth(...)`
- `correlation_sub_const_exact_numerator_logdepth(...)`
- `correlation_sub_const_weight_ceil_int_logdepth(...)`
- `linear_x_modulo_plus_const32(...)`
- `linear_x_modulo_minus_const32(...)`
- `linear_x_modulo_plus_const64(...)`
- `linear_x_modulo_minus_const64(...)`
- `linear_x_modulo_plus_const32_logdepth(...)`
- `linear_x_modulo_minus_const32_logdepth(...)`
- `linear_x_modulo_plus_const64_logdepth(...)`
- `linear_x_modulo_minus_const64_logdepth(...)`

Notes:

- This file is the current exact Q1 backend for subtraction by a constant in the linear search.
- The optimized path is event/run based, even though public names still retain the historical `*_logdepth` spelling.

### `linear_correlation/constant_weight_evaluation_flat.hpp`

Purpose:

- Run-flattened exact and windowed linear correlation for `y = x boxplus c`, `n <= 64`.

Public result/report types:

- `DyadicCorrelation`
- `WindowedCorrelationReport`
- `BinaryLiftMasks`
- `BinaryLiftedWindowedReport`
- `CascadeRound`
- `CascadeReport`

Representative API:

- `linear_correlation_add_const_exact_flat_dyadic(...)`
- `linear_correlation_add_const_exact_flat(...)`
- `linear_correlation_add_const_exact_flat_ld(...)`
- `linear_correlation_add_const_exact_flat_weight_ceil_int(...)`
- `linear_correlation_add_const_flat_bin_report(...)`
- `linear_correlation_add_const_flat_bin(...)`
- `linear_correlation_add_const_flat_bin_ld(...)`
- `binary_lift_addconst_masks(...)`
- `corr_add_const_binary_lifted_report(...)`
- `corr_add_const_cascade(...)`

Notes:

- Exact results are dyadic rationals with denominator `2^n`.
- With the internal run table cached, the natural online exact complexity is close to `O(#runs(c) + wt(beta))`.
- A supporting note for this model lives in:
  - `A Bit-Vector Linear Correlation Model for Modular Addition by a Constant (BvCorr-FLAT).md`

### `linear_correlation/constant_optimal_alpha.hpp` and `linear_correlation/constant_optimal_beta.hpp`

Purpose:

- Exact var-const linear Q2 operators.

Shared/public Q2 types:

- `VarConstQ2Direction`
- `VarConstQ2Operation`
- `VarConstQ2MainlineMethod`
- `VarConstQ2MainlineRequest`
- `VarConstQ2MainlineResult`
- `VarConstOptimalInputMaskResult`
- `VarConstOptimalOutputMaskResult`
- `VarConstQ2MainlineStats`

Representative API:

- `solve_varconst_q2_mainline(...)`
- `solve_fixed_alpha_q2_canonical(...)`
- `solve_fixed_alpha_q2_exact_transition_reference(...)`
- `solve_fixed_alpha_q2_raw_reference(...)`
- `find_optimal_alpha_varconst_mod_sub(...)`
- `find_optimal_alpha_varconst_mod_add(...)`
- `find_optimal_beta_varconst_mod_sub(...)`
- `find_optimal_beta_varconst_mod_add(...)`

Notes:

- These Q2 APIs are the operator layer behind the fixed-alpha and fixed-beta linear BnB stages.
- `constant_optimal_alpha.hpp` holds the shared type system and the fixed-beta-facing mainline entry point.
- `constant_optimal_beta.hpp` exposes the fixed-alpha-facing canonical and reference entry points.

## Conventions

- Bit numbering is consistent across the operator tree:
  - bit 0 = LSB
  - carries propagate from lower bits to higher bits
- Differential weight:
  - `w = -log2(DP)`
  - exact LM-2001 var-var weights are integer-valued
  - constant-model exact integer APIs return ceil-style `SearchWeight`
- Linear weight:
  - `Lw = -log2(|corr|)`
  - zero correlation maps to infinite weight
- Sentinel behavior:
  - integer-valued impossible cases use `INFINITE_WEIGHT`
  - floating-point impossible cases use `0.0` or `+infinity`, depending on the API contract

## Build Notes

- The operator code expects a C++20 toolchain with `<bit>` support.
- There are no third-party dependencies inside this directory.
- Exact 128-bit-style arithmetic is provided by local fixed-width helpers instead of compiler-specific native `__int128` assumptions.

## One-Line Summary

The current ARX operator implementation is no longer a single flat set of headers. It is a domain-structured operator library with:

- LM-2001 exact differential backends for var-var addition
- exact and approximate differential backends for add/sub by constant
- CCZ- and transfer-matrix-based linear backends
- exact Q2 optimizers for var-const differential and linear problems
- explicit row-shell and column-optimal accelerators that the NeoAlzette BnB search reuses directly
