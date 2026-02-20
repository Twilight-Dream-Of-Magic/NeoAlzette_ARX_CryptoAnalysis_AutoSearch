## NeoAlzette ARX CryptoAnalysis AutoSearch

This root README is the **navigation page** for the repository.

Implementation details, CLI details, checkpoint / resume semantics, and
accelerator-specific notes are intentionally kept in the language-specific
READMEs and the architecture document.

### Read First

- **English detailed README**: [README_EN.md](README_EN.md)
- **中文（繁體）詳細 README**: [README_CN.md](README_CN.md)
- **Architecture / module decomposition**: [AUTO_SEARCH_FRAME_ARCHITECTURE_EN.md](AUTO_SEARCH_FRAME_ARCHITECTURE_EN.md)
- **Strict mathematical BNB blueprint (中文)**: [AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md](AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md)
- **Batch / single-run checkpoint QA**: [QA_checkpoint_resume.ps1](QA_checkpoint_resume.ps1)
- **Command cookbook**: [program command.txt](program%20command.txt)

### Scope

This repository targets the **NeoAlzette** ARX-box used by the current codebase,
not an older generic Alzette sketch.

The root project contains:

- `include/neoalzette/` + `src/neoalzette/`
  - NeoAlzette core implementation
- `include/arx_analysis_operators/`
  - exact local ARX analysis operators
- `include/auto_search_frame/detail/` + `src/auto_search_frame/`
  - residual-frontier best-search core, math, and single-run checkpoint / resume
- `include/auto_subspace_hull/` + `src/auto_subspace_hull/`
  - resumable strict-hull collector, wrapper orchestration, and batch/subspace checkpoint layer
- `test_neoalzette_arx_trace.cpp`
  - trace / instrumentation entry
- `test_neoalzette_differential_best_search.cpp`
  - differential single-run / auto best-search entry
- `test_neoalzette_linear_best_search.cpp`
  - linear single-run / auto best-search entry
- `test_neoalzette_differential_hull_wrapper.cpp`
  - differential strict-hull / batch endpoint wrapper
- `test_neoalzette_linear_hull_wrapper.cpp`
  - linear strict-hull / batch endpoint wrapper
- PNB / neutral-bit tooling
  - currently not included in this repository; under active development in a separate repository

### Where To Look

- If you want to **build and run programs**, use [README_EN.md](README_EN.md) or
  [README_CN.md](README_CN.md).
- If you want to understand **how the search / hull pipeline is organized**, use
  [AUTO_SEARCH_FRAME_ARCHITECTURE_EN.md](AUTO_SEARCH_FRAME_ARCHITECTURE_EN.md).
- If you want the **strict mathematical BNB semantics / residual-problem model**, use
  [AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md](AUTO_SEARCH_FRAME_BNB_MATHEMATICAL_BLUEPRINT_CN.md).
- If you want to inspect **resume / checkpoint regression coverage**, use
  [QA_checkpoint_resume.ps1](QA_checkpoint_resume.ps1).
- If you want **copy-paste command lines**, use [program command.txt](program%20command.txt).
- PNB / neutral-bit tooling is currently under active development in a separate repository and is not part of this repository at the moment.

### Current Repo Policy

- The root README stays short and acts as an index.
- Detailed CLI semantics belong in `README_EN.md` / `README_CN.md`.
- Detailed implementation / resume / audit contracts belong in the architecture
  document.
- Detailed strict mathematical search semantics belong in the BNB mathematical
  blueprint.

### Important Reference Lineage: 📚 **Original Alzette Paper**

The block below is kept as a **paper-reference excerpt / design-lineage note** for the original Alzette ARX-box.
It matches the structure given in Algorithm 1 of the Alzette paper.

- this section explains the **original Alzette paper instance and its design rationale**
- the **current codebase still targets NeoAlzette**
- therefore this section should be read as **background / lineage**, not as the literal specification of the current `NeoAlzetteCore`


This repo contains NeoAlzette core code + ARX analysis operators +
root research programs (best-trail search / tracing). PNB / neutral-bit tooling
is currently under active development in a separate repository and does not
appear in this repository at the moment.


#### **Exact Alzette ARX S-box Algorithm Design from the Paper**:
```
Input/Output: (x, y) ∈ F₃₂² × F₃₂²
Input/ rc

x ← x + (y >>> 31)    // Modular addition: Applies y to x for nonlinear
confusion source. Carry chain from modular addition, borrow chain from
modular subtraction (but not a direct chain!!! Thus does not stack
quickly due to bit rotation diffusion)
y ← y ⊕ (x >>> 24)    // XOR: Applies x to y, where x already carries
the nonlinear source. Uses bitwise XOR and bit rotation for linear
diffusion (XOR and bit-rotation), while preventing rotation attacks
equivalent to modulo addition/subtraction
x ← x ⊕ rc         // XOR constant: Apply round constant rc to x to
update it (reset the modular addition/subtraction chain state), enabling
use in the next three-step pipeline stage while slowing down rapid
accumulation of carry-based chains, and anti-rotational attack

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
