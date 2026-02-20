# NeoAlzette ARX CryptoAnalysis AutoSearch

> Heads-up: several programs in this repo are **compute-intensive**. Start with small rounds / small sample sizes.

This repository contains:

- **NeoAlzette core implementation**: `TwilightDream::NeoAlzetteCore` (1-round `forward/backward`, linear layers `l1/l2`, and the cross-branch injection helpers).
- **Paper operators for ARX analysis** (header-only): XOR-differential and linear-correlation operators for modular addition / addition-by-constant.
- **Research / experiment programs** (`test_*.cpp`): differential trail tracing, best-trail search (with an injection DP model), and PNB/neutral-bit style experiments.

---

## What’s in the repo (current codebase)

- **Core cipher / ARX-box**
  - `include/neoalzette/neoalzette_core.hpp`
  - `src/neoalzette/neoalzette_core.cpp`
- **ARX analysis operators (header-only)**
  - `include/arx_analysis_operators/`  
    See `include/arx_analysis_operators/README.md` for operator notes and references.
- **Programs (entry points)**
  - `test_neoalzette_arx_trace.cpp`
  - `test_neoalzette_differential_best_search.cpp`
  - `test_neoalzette_linear_best_search.cpp`
  - `test_neoalzette_arx_probabilistic_neutral_bits.cpp`
  - `test_neoalzette_arx_probabilistic_neutral_bits_average.cpp`

---

## Build (Windows / MinGW-w64)

### Quick build script (recommended)

- Run `build_and_test.bat` (it compiles everything and runs `test_neoalzette_arx_probabilistic_neutral_bits.exe`).
- It expects an environment variable **`%MINGW64%`** pointing to your toolchain `bin` directory (e.g. `...\mingw64\bin`) and uses `"%MINGW64%\clang++"`.

### Manual build examples

All commands below build a single `.exe` (C++20) from source:

```bat
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_trace.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_trace.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_differential_best_search.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_differential_best_search.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_linear_best_search.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_linear_best_search.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_probabilistic_neutral_bits.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_probabilistic_neutral_bits.exe
"%MINGW64%\clang++" -std=c++20 -O3 -I.\include test_neoalzette_arx_probabilistic_neutral_bits_average.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_probabilistic_neutral_bits_average.exe
```

---

## Run

### `test_neoalzette_arx_trace.exe` (step-by-step differential trace)

- No CLI arguments.
- It traces two starting differences: \((\Delta A,\Delta B)=(1,0)\) and \((0,1)\).
- It uses:
  - LM2001 optimal-gamma operator for modular addition
  - BvWeight operator for subtraction-by-constant
  - An **affine-derivative** injection model (rank-based weight)

```bat
test_neoalzette_arx_trace.exe
```

### `test_neoalzette_differential_best_search.exe` (best trail search with injection model)

CLI (4 frontends / subcommands):

- **Strategy mode (recommended, fewer knobs)**:
  - `test_neoalzette_differential_best_search.exe strategy <time|balanced|space> --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [strategy_flags]`
- **Detail mode (full knobs, long option names)**:
  - `test_neoalzette_differential_best_search.exe detail --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [detail_flags]`
- **Auto mode (two-stage: breadth scan -> deep search, requires explicit start diffs)**:
  - `test_neoalzette_differential_best_search.exe auto --round-count <R> --delta-a <DA> --delta-b <DB> [auto_flags]`
- **Legacy mode (backward-compatible)**:
  - `test_neoalzette_differential_best_search.exe legacy <round_count> <deltaA_hex> <deltaB_hex> [flags]`
  - `test_neoalzette_differential_best_search.exe legacy <round_count> --seed <seed> [flags]`

Notes:

- If you omit `--delta-a/--delta-b`, **you must provide `--seed`** (no silent default input difference / seed).
- `--maximum-constant-subtraction-candidates 0` and `--maximum-affine-mixing-output-differences 0` mean **exact/unlimited enumeration** (can be very slow).
- In **strategy mode**, the program will print a **`[Strategy] resolved settings`** block at startup (all auto-derived knobs).
- **Batch mode requires `--seed` only when generating random jobs** (e.g. `--batch-job-count N`). If you use `--batch-file`, seed is not required.
- When shared cache is enabled and you don't explicitly set `--shared-cache-shards` (alias `--cache-shards`), the program may **auto-increase shards** based on thread count and shared-cache size.
- Entry point aliases:
  - You can also select the frontend via `--mode strategy|detail|auto|legacy` (equivalent to using the subcommand).
  - If you run the executable without a subcommand, it defaults to the legacy/detail-compatible parser (positional args still work).

Strategy presets (strategy mode):

- `time`: **time-first / memory-heavy**, disables heuristics (forces `--maximum-affine-mixing-output-differences 0`), and auto-sizes caches from available RAM (keeps headroom by default, typically ~2–4GiB; see `--memory-headroom-mib` default rule).
- `balanced`: balanced default; heuristics enabled and **`maxmix` is auto-chosen** from threads/rounds.
- `space`: space-first / smaller memory; memoization off; heuristics enabled and **`maxmix` is auto-chosen** (smaller).
- Strategy-only endpoint knob:
  - `--total-work N` (alias `--total`): sets an overall “endpoint/amount”. Bigger `N` auto-scales budgets.
    - In both single/batch: it increases per-job `maximum_search_nodes` (gently, roughly by order of magnitude).
    - In batch mode: it also decides `batch-job-count` **when you enable batch via `--batch`**.
  - `--batch` (strategy-only): enables batch mode while keeping the CLI “lazy”; use together with `--total-work N` and `--seed`.

Detail flags (long names; short-name aliases still accepted):

- `--addition-weight-cap N` (alias `--add`, 0..31)
- `--constant-subtraction-weight-cap N` (alias `--subtract`, 0..32)
- `--maximum-constant-subtraction-candidates N` (alias `--maxconst`, 0=exact/all)
- `--maximum-affine-mixing-output-differences N` (alias `--maxmix`, 0=exact/all)
- `--maximum-search-nodes N` (alias `--maxnodes`, `0`=unlimited)
- `--disable-state-memoization` (alias `--nomemo`)
- `--enable-verbose-output` (alias `--verbose`)
- Batch:
  - `--batch-job-count N` (alias `--batch`)
  - `--batch-file PATH` (jobs from file; each line: `dA dB` or `R dA dB`; supports `0x..` hex; `#` starts a comment)
  - `--thread-count T` (alias `--threads`, `0`=auto)
  - `--seed S`
  - `--progress-every-jobs N` (alias `--progress`, `0`=disable)
  - `--progress-every-seconds S` (alias `--progress-sec`, `0`=disable)
  - `--memory-headroom-mib M` (keep ~M MiB free RAM headroom; default: `max(2GiB, min(4GiB, avail/10))`)
  - `--memory-ballast` (adaptive ballast: allocate/release RAM to keep free RAM near headroom)
  - `--cache-max-entries-per-thread N` (alias `--cache`, `0`=disable)
  - `--shared-cache-total-entries N` (alias `--cache-shared`, `0`=disable)
  - `--shared-cache-shards S` (alias `--cache-shards`)
- `--legacy` (legacy Route-B regression mode)

Auto flags (auto mode, high-signal knobs):

- Breadth stage (many candidates, small budget):
  - `--auto-breadth-jobs N` (alias: `--auto-breadth-max-runs`)
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T` (0=auto)
  - `--auto-breadth-seed S` (default: derived from the provided start diffs)
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-maxconst N`
  - `--auto-breadth-heuristic-branch-cap N` (alias: `--auto-breadth-hcap`)
  - `--auto-breadth-max-bitflips F`
  - `--auto-print-breadth-candidates`
- Deep stage (top-K candidates, big budget):
  - `--auto-deep-maxnodes N` (0=unlimited)
  - `--auto-max-time T` (only used when deep maxnodes is unlimited; supports `3600`, `60m`, `24h`, `30d`, `4w`)
  - `--auto-target-best-weight W`

Auto-mode constraints:

- Auto mode **does not support batch mode**.
- Auto mode **requires explicit** `--delta-a` and `--delta-b` (no `--seed` fallback).
- Auto mode **does not support** `--legacy`.

Examples:

```bat
test_neoalzette_differential_best_search.exe strategy balanced --round-count 3 --delta-a 0x0 --delta-b 0x1
test_neoalzette_differential_best_search.exe strategy balanced --round-count 4 --batch-job-count 2000 --thread-count 16 --seed 0x1234
test_neoalzette_differential_best_search.exe detail --round-count 3 --delta-a 0x0 --delta-b 0x1 --maximum-search-nodes 5000000
test_neoalzette_differential_best_search.exe auto --round-count 4 --delta-a 0x0 --delta-b 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
test_neoalzette_differential_best_search.exe legacy 3 0x0 0x1 --maxnodes 5000000
```

### `test_neoalzette_linear_best_search.exe` (best linear trail / mask search)

This program searches best **linear correlations** (reverse-mask model). Inputs are two **output masks**:

- `--output-branch-a-mask MASK_A` (aliases: `--out-mask-a`, `--mask-a`)
- `--output-branch-b-mask MASK_B` (aliases: `--out-mask-b`, `--mask-b`)

CLI (3 frontends / subcommands; same style as the differential program):

- `test_neoalzette_linear_best_search.exe strategy <time|balanced|space> --round-count <R> [--output-branch-a-mask <MA> --output-branch-b-mask <MB> | --seed <S>]`
- `test_neoalzette_linear_best_search.exe detail --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`
- `test_neoalzette_linear_best_search.exe auto --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`

Examples:

```bat
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --batch-job-count 2000 --thread-count 16 --seed 0x1234
test_neoalzette_linear_best_search.exe auto --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_arx_probabilistic_neutral_bits.exe` (PNB-style + heatmap, outputs CSV)

Quick CLI:

- `test_neoalzette_arx_probabilistic_neutral_bits.exe <rounds> <trials_per_inputbit>`
- Example:

```bat
test_neoalzette_arx_probabilistic_neutral_bits.exe 1 200000
```

It will dump `neoalzette_heatmap_r<rounds>_*.csv` in the current directory.

### `test_neoalzette_arx_probabilistic_neutral_bits_average.exe` (average PNB-style per-bit neutrality)

This program is **configured in source** (no CLI): edit `test_neoalzette_arx_probabilistic_neutral_bits_average.cpp` to change:

- input difference \((\Delta A,\Delta B)\)
- signature mode (FULL64 / A32 / byte / masked)
- rounds list and sample count

---

## References / operators used

- **XOR-differential of modular addition**: LM2001 (`xdp_add_lm2001`, `find_optimal_gamma_with_weight`)
- **Addition/subtraction by constant**: bit-vector weight operators (`diff_addconst_bvweight`, `diff_subconst_bvweight`)
- **Linear correlation of addition** (also included): see `include/arx_analysis_operators/linear_correlation_add_*.hpp`

---

## License

MIT License (see `LICENSE`). Please cite original papers as appropriate.
