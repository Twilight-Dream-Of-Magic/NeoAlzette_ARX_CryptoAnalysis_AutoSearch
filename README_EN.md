## NeoAlzette ARX Automated Analysis & Search (Engineering Overview)

> Heads-up: This repository contains compute-intensive workflows. Do not run multi-round searches on personal laptops without understanding the cost; start small.

This document explains how the project is engineered: how ARX operators, the per-round NeoAlzette BlackBox scorers, and the pDDT / Matsui / cLAT components are wired together. We do not re-derive papers here; we describe implementation choices, interfaces, and how to use the system.

- Namespace: `TwilightDream`
- Binaries:
  - `neoalz_diff_search` (differential demo – do not run for long)
  - `neoalz_lin_search` (linear demo – do not run for long)
- Removed legacy modules: `utility_tools.*`, `medcp_analyzer.*`, `melcc_analyzer.*`, `threshold_search_framework.*`, `highway_table_build*.cpp`

---

### 1) Architecture (from an engineering perspective)

Three decoupled layers:

1) ARX analysis operators (`include/arx_analysis_operators/`)
- Encapsulate exact computations for modular addition under differential/linear models and convenience wrappers:
  - Differential: `differential_xdp_add.hpp`, `differential_optimal_gamma.hpp`
  - Linear: `linear_correlation_add_logn.hpp` (log-time), `linear_correlation_addconst.hpp` (2×2 transfer matrix chain, with 32/64-bit wrappers)
- Goal: reliable, auditable APIs consumed by the per-round model.

2) NeoAlzette per-round BlackBox scorers (`include/neoalzette/` + `src/neoalzette/`)
- Differential: `neoalzette_differential_step.hpp`
- Linear: `neoalzette_linear_step.hpp` (decl), `src/neoalzette/neoalzette_linear_step.cpp` (def)
- Idea:
  - Formalize all round primitives (rotations, XOR, add/sub constants, L1/L2)
  - Push/pop masks/differences through the round; call ARX operators only at additions to accumulate weight
  - Track phase and round-constants usage; return entry masks with total weight

3) Search frameworks (`include/arx_search_framework/` + `src/arx_search_framework/`)
- Differential:
  - pDDT construction (Alg.1): `pddt/pddt_algorithm1.hpp`, `src/arx_search_framework/pddt_algorithm1_complete.cpp`
  - Matsui threshold search (Alg.2): `matsui/matsui_algorithm2.hpp`, `src/arx_search_framework/matsui_algorithm2_complete.cpp`
- Linear:
  - cLAT build & lookup (Alg.2): `clat/clat_builder.hpp` (optionally `clat_search.hpp`)
- Engineering idea: treat BlackBox as the atomic scorer. Use Highways (pDDT) as backbone, fall back to Country-roads where needed. For linear, use cLAT+SLR to propose candidates and BlackBox to score them.

---

### 2) Build

- Requirements: C++20, CMake 3.14+
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"
```
- Artifacts: `neoalz_diff_search`, `neoalz_lin_search`
- Do not run long jobs on low-end machines.

---

### 3) CLI (standardized)

1) Differential `neoalz_diff_search`
- Options:
  - `--rounds, -r <int>` (default 4)
  - `--weight-cap, -w <int>` (default 30)
  - `--start-a <hex>`, `--start-b <hex>` (0x-prefixed supported)
  - `--precompute` / `--no-precompute` (pDDT)
  - `--pddt-seed-stride <int>` (default 8)
- Example:
```bash
./neoalz_diff_search -r 6 -w 32 --start-a 0x1 --start-b 0x0 --no-precompute --pddt-seed-stride 8
```

2) Linear `neoalz_lin_search`
- Options:
  - `--rounds, -r <int>` (default 4)
  - `--weight-cap, -w <int>` (default 30)
  - `--start-mask-a <hex>`, `--start-mask-b <hex>` (0x supported)
  - `--precompute` / `--no-precompute` (cLAT)
- Example:
```bash
./neoalz_lin_search -r 6 -w 32 --start-mask-a 0x1 --start-mask-b 0x0 --precompute
```

---

### 4) Library APIs (use in your C++ code)

- pDDT:
```cpp
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
using namespace TwilightDream;

PDDTAlgorithm1Complete::PDDTConfig cfg; cfg.bit_width = 32; cfg.set_weight_threshold(7);
auto entries = PDDTAlgorithm1Complete::compute_pddt(cfg);
```
- Matsui:
```cpp
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
using namespace TwilightDream;

MatsuiAlgorithm2Complete::SearchConfig sc; sc.num_rounds = 4; sc.initial_estimate = 1e-12;
auto result = MatsuiAlgorithm2Complete::execute_threshold_search(sc);
```
- cLAT (8-bit chunks):
```cpp
#include "arx_search_framework/clat/clat_builder.hpp"
using namespace TwilightDream;

cLAT<8> clat; clat.build();
clat.lookup_and_recombine(0x12345678u, /*t=*/4, /*weight_cap=*/30,
  [](uint32_t u, uint32_t w, int weight) {/* use (u,w,weight) */});
```
- NeoAlzette + pipeline shell:
```cpp
#include "neoalzette/neoalzette_with_framework.hpp"
using TwilightDream::NeoAlzetteFullPipeline;

NeoAlzetteFullPipeline::DifferentialPipeline::Config cfg; cfg.rounds = 4;
auto medcp = NeoAlzetteFullPipeline::run_differential_analysis(cfg);
```

---

### 5) Engineering highlights (must-know)

- Compute-heavy: multi-round searches explode; start with small rounds.
- Single-threaded: no parallelism yet; don’t expect full-core utilization.
- Exact per-round scoring: only additions contribute weight; rotations/XOR/linear layers just move/combine masks.
- Unified namespace: `TwilightDream` across operators, BlackBox, and frameworks.
- Legacy modules removed: `utility_tools.*`, `medcp_analyzer.*`, `melcc_analyzer.*`, `threshold_search_framework.*`, `highway_table_build*.cpp` are gone.

---

### 6) Repo layout (current)

```
.
├── CMakeLists.txt
├── include/
│   ├── arx_analysis_operators/
│   │   ├── differential_xdp_add.hpp
│   │   ├── differential_optimal_gamma.hpp
│   │   ├── linear_correlation_add_logn.hpp
│   │   └── linear_correlation_addconst.hpp
│   ├── arx_search_framework/
│   │   ├── pddt/pddt_algorithm1.hpp
│   │   ├── matsui/matsui_algorithm2.hpp
│   │   ├── clat/algorithm1_const.hpp
│   │   ├── clat/clat_builder.hpp
│   │   └── README.md
│   └── neoalzette/
│       ├── neoalzette_core.hpp
│       ├── neoalzette_differential_step.hpp
│       ├── neoalzette_linear_step.hpp
│       └── neoalzette_with_framework.hpp
├── src/
│   ├── neoalzette_differential_main_search.cpp
│   ├── neoalzette_linear_main_search.cpp
│   ├── neoalzette/
│   │   └── neoalzette_linear_step.cpp
│   └── arx_search_framework/
│       ├── pddt_algorithm1_complete.cpp
│       └── matsui_algorithm2_complete.cpp
└── papers_txt/
```

---

### License

MIT License (see `LICENSE`). Algorithms come from published papers; please cite the originals and this project in academic work.
