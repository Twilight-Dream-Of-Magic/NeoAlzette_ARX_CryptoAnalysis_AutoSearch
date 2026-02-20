# NeoAlzette ARX CryptoAnalysis AutoSearch

> Heads-up: several programs in this repo are **compute-intensive**. Start with small rounds / small sample sizes.

This repository contains:

- **NeoAlzette core implementation**: `TwilightDream::NeoAlzetteCore` (1-round `forward/backward`, linear layers `mask0/mask1`, and the cross-branch injection helpers).
- **Paper operators for ARX analysis** (header-only): XOR-differential and linear-correlation operators for modular addition / addition-by-constant.
- **Root research / experiment programs** (`test_*.cpp`): differential trail tracing, best-trail search, and hull wrappers.
- **Independent PNB subproject**: all PNB / neutral-bit tools now live under `pnb_distinguisher/`, and the root project intentionally keeps no duplicate PNB source or binary entry points.
  The new prototype only keeps the word "strict" when full pre-cut conditioning and affine-coset validation both hold. By default it is strict-only; heuristic continuation requires `--allow-heuristic-fallback`.

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
- **Independent PNB workspace**
  - `pnb_distinguisher/`

---

## Build (Windows / Linux)

### Recommended path: CMake (cross-platform)

The repository already contains Linux runtime branches (`__linux__`) and a
cross-platform `CMakeLists.txt`. CMake links `psapi` only on Windows and uses
`Threads::Threads` on both systems.

On Windows, `-G Ninja` is the safest recommendation for the repo's current
`CLANG64` / MinGW-clang flow and avoids Visual Studio generator pitfalls in
paths like this workspace.

Windows:

```powershell
cmake -G Ninja -S . -B build\windows-release -DNEOALZETTE_BUILD_PROGRAMS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build\windows-release --config Release --parallel
```

Linux:

```bash
cmake -S . -B build/linux-release -DNEOALZETTE_BUILD_PROGRAMS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-release --parallel
```

### Quick scripts

- Windows: run `build_and_test.bat`
- Linux: run `chmod +x ./build_and_test.sh && ./build_and_test.sh`
- `build_and_test.bat` keeps the historical direct-`clang++` flow and writes the built `.exe` files into the repo root.
- `build_and_test.sh` uses CMake and writes Linux binaries into `build/linux-release/`.
- PNB tools are no longer built from the root script; use `pnb_distinguisher/` for those executables.
- The public best-search headers stay stable as facades:
  - `include/auto_search_frame/test_neoalzette_linear_best_search.hpp`
  - `include/auto_search_frame/test_neoalzette_differential_best_search.hpp`
- Best-search implementation now lives under:
  - `include/auto_search_frame/detail/`
  - `src/auto_search_frame/`
- The best-search executables and hull wrappers now build with a shared source list that includes:
  - `src/auto_search_frame/best_search_shared_core.cpp`
  - `src/auto_search_frame/linear_best_search_engine.cpp`
  - `src/auto_search_frame/linear_best_search_collector.cpp`
  - `src/auto_search_frame/differential_best_search_engine.cpp`
  - `src/auto_search_frame/differential_best_search_collector.cpp`

### Windows manual build examples (optional)

All commands below build a single `.exe` (C++20) from source:

```bat
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_arx_trace.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_arx_trace.exe
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_differential_best_search.cpp test_arx_operator_self_test.cpp common\runtime_component.cpp src\auto_search_frame\best_search_shared_core.cpp src\auto_search_frame\differential_best_search_engine.cpp src\auto_search_frame\differential_best_search_collector.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_differential_best_search.exe
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_linear_best_search.cpp test_arx_operator_self_test.cpp common\runtime_component.cpp src\auto_search_frame\best_search_shared_core.cpp src\auto_search_frame\linear_best_search_engine.cpp src\auto_search_frame\linear_best_search_collector.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_linear_best_search.exe
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_differential_hull_wrapper.cpp common\runtime_component.cpp src\auto_search_frame\best_search_shared_core.cpp src\auto_search_frame\differential_best_search_engine.cpp src\auto_search_frame\differential_best_search_collector.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_differential_hull_wrapper.exe
"%CLANG64%\clang++.exe" -std=c++20 -O3 -static -Wall -Wextra -lpsapi -I.\include -I. test_neoalzette_linear_hull_wrapper.cpp common\runtime_component.cpp src\auto_search_frame\best_search_shared_core.cpp src\auto_search_frame\linear_best_search_engine.cpp src\auto_search_frame\linear_best_search_collector.cpp src\neoalzette\neoalzette_core.cpp -o test_neoalzette_linear_hull_wrapper.exe
```

---

## Run

Windows examples below use `.exe`. On Linux, use the same target basename
without `.exe`. If you built with `build_and_test.sh` or the Linux CMake
example above, the executables live under `build/linux-release/`.

If you want a practical runbook instead of reading raw `--help` output, use
`program command.txt`. It is maintained as the scenario-oriented command guide
for:

- build / CLI sanity checks,
- exact-best strict runs,
- threshold-certified runs,
- explicit-cap exact hull collection,
- auto mode,
- and batch wrapper workflows on both Windows and Linux.

### `test_neoalzette_arx_trace.exe` (step-by-step differential trace)

- No CLI arguments.
- It traces two starting differences: \((\Delta A,\Delta B)=(1,0)\) and \((0,1)\).
- It uses:
  - LM2001 optimal-gamma operator for modular addition
  - exact integer-weight wrapper for subtraction-by-constant (`diff_subconst_exact_weight_ceil_int` and the other exact-named interfaces)
  - An **affine-derivative** injection model (rank-based weight)

```bat
test_neoalzette_arx_trace.exe
```

### `test_neoalzette_differential_best_search.exe` (single-run best trail search / auto pipeline)

Current scope of this executable:

- `strategy` / `detail`: single-run best-search from one explicit start difference, or from one RNG-generated start pair when you provide `--seed`.
- `auto`: breadth scan -> deep search for one explicit start pair.
- Multi-job batch breadth->deep no longer runs here; use `test_neoalzette_differential_hull_wrapper.exe`.

CLI (3 frontends / subcommands):

- **Strategy mode (recommended, fewer knobs)**:
  - `test_neoalzette_differential_best_search.exe strategy <time|balanced|space> --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [strategy_flags]`
- **Detail mode (full knobs, long option names)**:
  - `test_neoalzette_differential_best_search.exe detail --round-count <R> [--delta-a <DA> --delta-b <DB> | --seed <S>] [detail_flags]`
- **Auto mode (two-stage: breadth scan -> deep search, requires explicit start diffs)**:
  - `test_neoalzette_differential_best_search.exe auto --round-count <R> --delta-a <DA> --delta-b <DB> [auto_flags]`

Notes:

- If you omit `--delta-a/--delta-b` in `strategy` or `detail`, **you must provide `--seed`**.
- `--mode strategy|detail|auto` is equivalent to choosing the subcommand explicitly.
- In `strategy` mode, the program prints a **`[Strategy] resolved settings`** block at startup.
- Strategy preset `time` disables heuristics and can become much slower than `balanced` / `space`.
- Auto mode **requires explicit** `--delta-a` and `--delta-b`, and **does not support batch mode**.

Useful single-run / detail knobs:

- `--total-work N` (alias `--total`): auto-scales the single-run node budget and related cache sizing.
- `--addition-weight-cap N` (alias `--add`) and `--constant-subtraction-weight-cap N` (alias `--subtract`)
- `--maximum-search-nodes N` (alias `--maxnodes`) and `--maximum-search-seconds T` (alias `--maxsec`)
- `--target-best-weight W` (alias `--target-weight`)
  If hit under otherwise strict settings, output now reports `best_weight_certification=threshold_target_certified`.
  That is distinct from `exact_best_certified`.
- `--search-mode strict|fast`
- `--enable-pddt`, `--disable-pddt`, `--pddt-max-weight W`
- `--disable-state-memoization` (alias `--nomemo`) and `--enable-verbose-output` (alias `--verbose`)
- `--memory-headroom-mib M`, `--memory-ballast`, `--allow-high-memory-usage`, `--rebuildable-reserve-mib M`
- `--resume PATH`, `--runtime-log PATH`, `--checkpoint-out PATH`, `--checkpoint-every-seconds S`

Auto-stage knobs:

- Breadth stage:
  - `--auto-breadth-jobs N` (alias `--auto-breadth-max-runs`)
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T`
  - `--auto-breadth-seed S`
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-max-bitflips F`
  - `--auto-breadth-heuristic-branch-cap N` (alias `--auto-breadth-hcap`, ignored because breadth is strict)
  - `--auto-print-breadth-candidates`
- Deep stage:
  - `--auto-deep-maxnodes N`
  - `--auto-max-time T`
  - `--auto-target-best-weight W`
    If hit under otherwise strict local settings, output reports `best_weight_certification=threshold_target_certified`.
    It does not mean `exact_best_certified`.
Examples:

```bat
test_neoalzette_differential_best_search.exe strategy balanced --round-count 4 --delta-a 0x0 --delta-b 0x1
test_neoalzette_differential_best_search.exe detail --round-count 4 --seed 0x1234 --maximum-search-nodes 5000000
test_neoalzette_differential_best_search.exe auto --round-count 4 --delta-a 0x0 --delta-b 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_linear_best_search.exe` (single-run best linear trail / mask search)

This program searches best **linear correlations** (reverse-mask model). Inputs are two **output masks**:

- `--output-branch-a-mask MASK_A` (aliases: `--out-mask-a`, `--mask-a`)
- `--output-branch-b-mask MASK_B` (aliases: `--out-mask-b`, `--mask-b`)

Current scope of this executable:

- `strategy` / `detail`: single-run best-search from one explicit mask pair, or from one RNG-generated pair when you provide `--seed`.
- `auto`: breadth scan -> deep search for one explicit mask pair.
- Multi-job batch breadth->deep no longer runs here; use `test_neoalzette_linear_hull_wrapper.exe`.

CLI (3 frontends / subcommands; same style as the differential program):

- `test_neoalzette_linear_best_search.exe strategy <time|balanced|space> --round-count <R> [--output-branch-a-mask <MA> --output-branch-b-mask <MB> | --seed <S>]`
- `test_neoalzette_linear_best_search.exe detail --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`
- `test_neoalzette_linear_best_search.exe auto --round-count <R> --output-branch-a-mask <MA> --output-branch-b-mask <MB> [options]`

Notes:

- If you omit the two masks in `strategy` or `detail`, **you must provide `--seed`**.
- `--mode strategy|detail|auto` is supported here too.
- Auto breadth stays **strict**, so `--auto-breadth-hcap` is accepted only for CLI compatibility and then ignored.

Useful single-run / detail knobs:

- `--total-work N` (alias `--total`)
- `--maximum-search-nodes N` (alias `--maxnodes`) and `--maximum-search-seconds T` (alias `--maxsec`)
- `--target-best-weight W` (alias `--target-weight`)
  If hit under otherwise strict settings, output now reports `best_weight_certification=threshold_target_certified`.
  That is distinct from `exact_best_certified`.
- `--search-mode strict|fast`
- `--addition-weight-cap N`, `--constant-subtraction-weight-cap N`
- `--maximum-injection-input-masks N`
- `--maximum-round-predecessors N`
- `--enable-z-shell`, `--disable-z-shell`, `--z-shell-max-candidates N`
- `--disable-state-memoization`, `--enable-verbose-output`
- `--memory-headroom-mib M`, `--memory-ballast`, `--allow-high-memory-usage`, `--rebuildable-reserve-mib M`
- `--resume PATH`, `--runtime-log PATH`, `--checkpoint-out PATH`, `--checkpoint-every-seconds S`

Auto-stage knobs:

- Breadth stage:
  - `--auto-breadth-jobs N` (alias `--auto-breadth-max-runs`)
  - `--auto-breadth-top_candidates K`
  - `--auto-breadth-threads T`
  - `--auto-breadth-seed S`
  - `--auto-breadth-maxnodes N`
  - `--auto-breadth-max-bitflips F`
  - `--auto-print-breadth-candidates`
- Deep stage:
  - `--auto-deep-maxnodes N`
  - `--auto-max-time T`
  - `--auto-target-best-weight W`
    If hit under otherwise strict local settings, output reports `best_weight_certification=threshold_target_certified`.
    It does not mean `exact_best_certified`.
Examples:

```bat
test_neoalzette_linear_best_search.exe strategy balanced --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1
test_neoalzette_linear_best_search.exe detail --round-count 4 --seed 0x1234 --maximum-search-nodes 5000000
test_neoalzette_linear_best_search.exe auto --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0
```

### `test_neoalzette_*_hull_wrapper.exe` (strict hull wrappers / batch endpoint aggregation)

The two hull wrappers are the paper-facing batch endpoints:

- `test_neoalzette_differential_hull_wrapper.exe`
- `test_neoalzette_linear_hull_wrapper.exe`

Both wrappers expose:

- single-source strict hull collection,
- batch source selection (`breadth -> deep -> strict hull`),
- combined `source_hulls` / `endpoint_hulls` aggregation,
- stage/job-boundary batch checkpoint / resume,
- and batch runtime event logs for audit.

Best-weight certification wording:

- `best_weight_certified=1` means the best-search stage certified the exact best weight.
- `best_weight_certification=threshold_target_certified` means a target threshold was hit under otherwise strict local settings.
- The latter is a threshold statement, not an exact-best statement.

Wrapper-side accelerator knobs:

- Differential wrapper: `--enable-pddt`, `--disable-pddt`, `--pddt-max-weight W`
- Linear wrapper: `--enable-z-shell`, `--disable-z-shell`, `--z-shell-max-candidates N`
- The linear wrapper always runs in strict mode, so nonzero `--z-shell-max-candidates` is forced back to `0`.

Quick usage examples:

```bat
test_neoalzette_differential_hull_wrapper.exe --round-count 4 --delta-a 0x0 --delta-b 0x1 --enable-pddt --pddt-max-weight 12 --collect-weight-window 0
test_neoalzette_differential_hull_wrapper.exe --round-count 4 --batch-job-count 512 --seed 0x1234 --thread-count 16 --enable-pddt --pddt-max-weight 12 --auto-breadth-maxnodes 1048576 --auto-deep-maxnodes 0 --auto-max-time-seconds 3600 --collect-weight-window 0
test_neoalzette_linear_hull_wrapper.exe --round-count 4 --batch-file linear_jobs.txt --thread-count 16 --auto-breadth-maxnodes 1048576 --auto-deep-maxnodes 0 --auto-max-time-seconds 3600 --collect-weight-window 0
```

Batch file formats:

- differential wrapper: each non-empty line is `delta_a delta_b` or `round_count delta_a delta_b`
- linear wrapper: each non-empty line is `mask_a mask_b` or `round_count mask_a mask_b`
- numbers accept hex (`0x...`) or decimal; commas are allowed; lines starting with `#` are comments

Batch resume knobs:

- `--batch-checkpoint-out PATH`
  - writes a batch checkpoint after the relevant stage/job boundary.
- `--batch-resume PATH`
  - resumes from a batch checkpoint.
  - the wrapper auto-detects whether `PATH` is:
    - a **source-selection checkpoint** (`*_HullBatchSelection`), or
    - a **selected-source strict-hull checkpoint** (`*_HullBatch`).
- `--batch-runtime-log PATH`
  - writes the wrapper-level runtime event stream for the batch pipeline.

Current resume semantics are deliberately conservative:

- **Selection checkpoint**
  - stores the breadth/source-selection state:
    - jobs,
    - completed breadth jobs,
    - accumulated breadth nodes,
    - current `top_candidates`,
    - stage (`selection_breadth` / `selection_deep_ready`).
- **Strict-hull checkpoint**
  - stores the selected-source strict-hull state:
    - selected jobs,
    - per-job summaries,
    - completed strict-hull jobs,
    - combined callback aggregator (`source_hulls`, `endpoint_hulls`, optional stored trails).
- **Resume granularity**
  - batch resume is at **stage/job boundary**.
  - it does **not** claim same-node DFS resume for an in-flight batch job.
  - same-node DFS resume still belongs to the embedded single-run best-search engine.

Batch runtime-event contract:

- `batch_start`
  - batch invocation metadata and chosen checkpoint/runtime-log paths.
- `batch_resume_start`
  - emitted when loading a batch checkpoint.
  - includes:
    - `checkpoint_kind`,
    - `stage`,
    - `batch_resume_fingerprint_hash`,
    - `batch_resume_fingerprint_completed_jobs`,
    - `batch_resume_fingerprint_payload_count`,
    - `batch_resume_fingerprint_payload_digest`,
    - and, for strict-hull checkpoints, `source_hull_count` / `endpoint_hull_count` / `collected_trail_count`.
- `batch_checkpoint_write`
  - emitted after a successful batch checkpoint write.
  - includes:
    - `checkpoint_kind`,
    - `checkpoint_reason`,
    - `checkpoint_path`,
    - the batch resume fingerprint fields.
- `batch_stop`
  - emitted when the wrapper-level batch run finishes.
  - includes final job counts, selected-source counts, and the final batch fingerprint.

Important interpretation rule:

- the **truth-bearing object** is still the in-memory / checkpointed combined callback aggregator,
  not the console summary and not CSV exports.
- the runtime log is the audit trail for how that object was resumed and extended.

Regression coverage:

- `build_and_test.bat` runs the wrapper self-tests.
- `QA_checkpoint_resume.ps1` now validates:
  - single-run best-search checkpoint / resume,
  - auto-pipeline checkpoint / resume,
  - wrapper batch selection checkpoint / resume,
  - wrapper strict-hull checkpoint / resume,
  - and wrapper batch runtime-event / fingerprint continuity.

### `pnb_distinguisher/` (independent PNB workspace)

All PNB tools were split out of the root project and are now maintained under
`pnb_distinguisher/`.

That subproject contains:

- the legacy heatmap/event PNB tool,
- the legacy strict per-bit PNB tool,
- and the new `test_neoalzette_pnb_distinguisher` prototype for the 1-round
  conditional quotient-aware distinguisher study.

Build and run those executables from `pnb_distinguisher/`, not from the root
project. The root directory intentionally no longer retains duplicate PNB
`test_*.cpp` sources or built `.exe` artifacts.

---

## References / operators used

- **XOR-differential of modular addition**: LM2001 (`xdp_add_lm2001`, `find_optimal_gamma_with_weight`)
- **Addition/subtraction by constant**: exact integer-weight wrappers (`diff_addconst_exact_weight_ceil_int`, `diff_subconst_exact_weight_ceil_int`; the old `diff_*_bvweight` aliases were removed to avoid ambiguity with approximate BvWeight APIs)
- **Linear correlation of addition** (also included): see `include/arx_analysis_operators/linear_correlation_add_*.hpp`

---

## License

MIT License (see `LICENSE`). Please cite original papers as appropriate.
