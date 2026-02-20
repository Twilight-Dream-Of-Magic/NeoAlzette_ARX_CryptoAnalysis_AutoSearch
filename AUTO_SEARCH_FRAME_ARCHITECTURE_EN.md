# NeoAlzette Auto Search Frame Architecture

This document describes the current `auto_search_frame` / `auto_subspace_hull` architecture as it exists after the residual-frontier v3 alignment.

It replaces the earlier single-root / one-shot-collector description. The current implementation is:

- residual-frontier driven for both best-search and strict-hull collection,
- resumable for both best-search and collector runtimes,
- still based on the same NeoAlzette round-function wiring and the same Q2+Q1 mathematical flow.

## 1. Current Module Split

### 1.1 Ownership

| Area | Main location | Responsibility |
| --- | --- | --- |
| NeoAlzette cipher core | `include/neoalzette/`, `src/neoalzette/` | Ground-truth round-function implementation |
| Search framework core | `include/auto_search_frame/detail/`, `src/auto_search_frame/` | best-search math, resumable BnB engines, residual-frontier checkpoint/resume |
| Strict hull runtime and wrappers | `include/auto_subspace_hull/`, `src/auto_subspace_hull/` | resumable strict collector, batch/subspace orchestration, wrapper-level checkpoints |
| Common runtime support | `common/` | binary I/O, runtime logs, watchdog helpers, memory/runtime controls |

### 1.2 Important source files

| Role | Differential | Linear |
| --- | --- | --- |
| Engine | `src/auto_search_frame/differential_best_search_engine.cpp` | `src/auto_search_frame/linear_best_search_engine.cpp` |
| Engine checkpoint | `src/auto_search_frame/differential_best_search_checkpoint.cpp` | `src/auto_search_frame/linear_best_search_checkpoint.cpp` |
| Collector | `src/auto_subspace_hull/differential_best_search_collector.cpp` | `src/auto_subspace_hull/linear_best_search_collector.cpp` |
| Collector checkpoint | `include/auto_subspace_hull/detail/differential_hull_collector_checkpoint.hpp` | `include/auto_subspace_hull/detail/linear_hull_collector_checkpoint.hpp` |
| Shared residual types | `include/auto_search_frame/detail/residual_frontier_shared.hpp` | same |

The current `auto_search_frame_bnb_detail` tree is split into three layers:

- `src/auto_search_frame_bnb_detail/polarity/<domain>/*.cpp`
  contains only BnB configuration / polarity-profile sources.
- `src/auto_search_frame_bnb_detail/<domain>/*.cpp`
  contains only domain-root accelerator implementations.
- `src/auto_search_frame_bnb_detail/polarity/<domain>/varconst/*.cpp`
  and
  `src/auto_search_frame_bnb_detail/polarity/<domain>/varvar/*.cpp`
  contain only the search-frame bridge layer that connects polarity semantics to
  operator/theorem/Q1 judge code.

Current linear layout:

- domain-root accelerator:
  `src/auto_search_frame_bnb_detail/linear/varvar_z_shell_weight_sliced_clat_q2.cpp`
- polarity profiles:
  `src/auto_search_frame_bnb_detail/polarity/linear/linear_bnb_profile_fixed_*.cpp`
- polarity bridges:
  `src/auto_search_frame_bnb_detail/polarity/linear/varconst/*.cpp`
  `src/auto_search_frame_bnb_detail/polarity/linear/varvar/*.cpp`

Current differential layout:

- domain-root accelerator:
  `src/auto_search_frame_bnb_detail/differential/varvar_weight_sliced_pddt_q2.cpp`
- polarity root is reserved for profile/config sources;
  no extra differential profile `.cpp` is added in this refactor
- polarity bridges:
  `src/auto_search_frame_bnb_detail/polarity/differential/varconst/*.cpp`
  `src/auto_search_frame_bnb_detail/polarity/differential/varvar/*.cpp`

### 1.3 CMake reality

The executable targets compile the collector from `src/auto_subspace_hull/*_best_search_collector.cpp`, not from `src/auto_search_frame/`.

That means the old statement “collector lives in `src/auto_search_frame` and is one-shot only” is no longer true.

## 2. Residual-Frontier Search Model

### 2.1 Unified residual problem

Both best-search and collector runtimes now work on residual problems instead of a single fixed root DFS only.

Each residual problem carries:

- `domain = Differential | Linear`
- `objective = BestWeight | HullCollect`
- `rounds_remaining`
- `stage_cursor`
- `pair_a`, `pair_b`
- `suffix_profile_id`

The implementation also stores:

- `absolute_round_index`
- `source_tag`

These two are serialized and preserved for provenance/debug output, but they are no longer part of the semantic dedup / dominance / completed identity.

### 2.2 Effective semantic key

The effective semantic key is:

`{domain, objective, rounds_remaining, stage_cursor, pair_a, pair_b, suffix_profile_id}`

This is the key used by:

- `pending_frontier` dedup,
- `completed_residual_set`,
- `best_prefix_by_residual_key`,
- repeated/dominated skip accounting.

### 2.3 Stage boundaries

Residual children are created only at existing round-function stage boundaries.

Differential stage cursor:

- `FirstAdd`
- `FirstConst`
- `InjB`
- `SecondAdd`
- `SecondConst`
- `InjA`
- `RoundEnd`

Linear stage cursor:

- `InjA`
- `SecondAdd`
- `InjB`
- `SecondConst`
- `FirstSubconst`
- `FirstAdd`
- `RoundEnd`

The current implementation also contains two kinds of **explicit deterministic helper-level substeps that are not promoted into new `stage_cursor` values**:

- explicit `CROSS_XOR_ROT` bridge steps,
- explicit pre-injection pre-whitening defense steps `xor RC[4] / xor RC[9]`.

More precisely:

- in the differential runtime, both are treated as zero-weight deterministic XOR-difference transport,
- in the linear runtime, both are treated as zero-weight deterministic absolute-correlation mask transport,
- they are now explicit in the engine/collector code,
- but they still live inside the existing neighboring `InjA / InjB / FirstConst / SecondConst` stage handlers,
- so they do not add new residual-key fields or new checkpoint stage ids.

No operator-internal temporary state is promoted into the residual key.

### 2.4 Single-round and multi-round semantics

- If `rounds_remaining == 1`, the search may still emit child residuals inside the current round, but may not cross `RoundEnd -> next round`.
- If `rounds_remaining > 1`, the search may emit both:
  - in-round child residuals at stage boundaries,
  - and `RoundEnd -> next round` residuals.

### 2.5 Global anti-looping

The global frontier state is split into:

- `pending_frontier`
- `pending_frontier_entries`
- `completed_source_input_pairs`
- `completed_output_as_next_input_pairs`
- `completed_residual_set`
- `best_prefix_by_residual_key`
- `global_residual_result_table`
- `residual_counters`

Each active residual also keeps a local rebuildable dominance table keyed by:

`{stage_cursor, pair_a, pair_b}`

That table is an optimization only and is not serialized.

## 3. Best-Search and Collector Runtime Semantics

### 3.1 Best-search

Best-search is no longer described as “a single fixed-root resumable DFS”.

It is now:

- a resumable residual-frontier BnB engine,
- with one active residual cursor plus a pending frontier,
- where interrupted/pruned stage-boundary states can become child residuals.

### 3.2 Collector

The strict collector is also residual-frontier driven.

It is not one-shot-only anymore. The collector now keeps:

- the active collection cursor,
- the active residual record,
- pending frontier records and resumable frontier entries,
- completed residual/result state,
- the callback aggregation state,
- and its own binary checkpoint format.

### 3.3 Q2+Q1 invariants

The residual-frontier rewrite does not change the mathematical step contracts.

The following remain intentionally unchanged:

- differential Q2/Q1 flow,
- linear Q2+Q1 flow,
- candidate ordering semantics,
- strict vs fast behavior of the existing linear helper modes,
- NeoAlzette round-function step order and wiring.

In particular:

- differential `w-pDDT` remains an accelerator over exact shells,
- linear `z-shell` / `Weight-Sliced cLAT` remains wired through the current Q2+Q1 interfaces,
- strict runtimes still rely on exact local scoring and strict certification rules.

### 3.4 Explicit Zero-Cost Bridges In The Current Code

The code no longer hides the following deterministic steps inside the implicit input semantics of the injection-rank helpers. They are now explicit helper-level analysis steps:

- Differential:
  - first subround `FirstConst -> CROSS_XOR_ROT -> pre-whitening(B ^= RC[4]) -> InjB`
  - second subround `SecondConst -> CROSS_XOR_ROT -> pre-whitening(A ^= RC[9]) -> InjA`
- Linear:
  - explicit reverse expansion of the second-subround `CROSS_XOR_ROT` after `InjA`
  - explicit zero-cost mask/pre-whitening transport for `B ^= RC[4]` after `SecondAdd`
  - explicit reverse expansion of the first-subround `CROSS_XOR_ROT` after `SecondConst`
  - explicit zero-cost mask/pre-whitening transport for `A ^= RC[9]` at round entry

This means:

- the current implementation is now more explicit than the older architecture text at the local-step level,
- but these helper-level substeps still serve the same stage machine and do not change the residual-anchor set.

## 4. Checkpoint / Resume Contract

### 4.1 Version and kinds

`include/auto_search_frame/search_checkpoint.hpp` currently defines:

- `kVersion = 1`

Relevant single-run residual-frontier kinds are:

- `LinearResidualFrontierBest`
- `DifferentialResidualFrontierBest`
- `LinearResidualFrontierCollector`
- `DifferentialResidualFrontierCollector`

Older `kVersion = 0` payloads are not accepted by the current reader.

### 4.2 What engine checkpoints store

Best-search checkpoints store the exact resumable BnB state needed to continue from the interrupted node:

- configuration and runtime controls,
- best trail / current trail,
- active cursor,
- active residual record,
- memoization,
- pending frontier records and entries,
- completed residual/result state,
- residual counters.

### 4.3 What collector checkpoints store

Collector checkpoints store:

- the active collection cursor,
- current trail,
- active residual record,
- pending frontier records and entries,
- completed residual/result state,
- residual counters,
- collection options/caps,
- aggregation result,
- callback aggregator state,
- best-search reference result when the collector depends on a prior best-search phase.

### 4.4 Rebuildable-only state

Checkpointed state is exact for the in-flight BnB node, but optional accelerators remain rebuildable-only.

This includes:

- differential modular-add shell caches,
- linear helper caches / helper materializations,
- local residual dominance tables.

The checkpoint stores the exact cursor/stream position needed to continue BnB, then rebuilds optional accelerator state after load.

### 4.5 Resume order

On resume the runtime follows this order:

1. load the binary payload,
2. restore the active cursor and active residual record,
3. rebuild rebuildable-only accelerator state,
4. continue the active residual first if present,
5. otherwise restore the next non-dominated pending frontier entry.

### 4.6 Runtime-budget interruption

When node/time budget is hit and a checkpoint path is configured, the runtime now forces a latest checkpoint snapshot for the interrupted residual-frontier state.

That snapshot is intended to capture:

- current trail,
- active cursor,
- active residual record,
- pending frontier state,
- completed residual state,
- counters and result tables.

## 5. Pair Events and Runtime Logs

The code distinguishes four pair-event families:

- `interrupted_source_input_pair`
- `completed_source_input_pair`
- `interrupted_output_as_next_input_pair`
- `completed_output_as_next_input_pair`

Semantics:

- `interrupted_source_input_pair`
  - stdout: yes
  - runtime log: yes
  - checkpointed through normal state snapshot: yes
- `completed_source_input_pair`
  - stdout: yes
  - runtime log: yes
  - checkpointed through normal state snapshot: yes
- `interrupted_output_as_next_input_pair`
  - stdout: yes, aggregated progress style
  - runtime log: no dedicated event
  - standalone checkpoint event: no
  - resulting frontier state still appears in the regular checkpoint payload
- `completed_output_as_next_input_pair`
  - stdout: yes
  - runtime log: yes
  - checkpointed through normal state snapshot: yes

The event output now keeps `absolute_round_index` and `source_tag` as provenance/debug metadata.

## 6. Differential and Linear Accelerator Notes

### 6.1 Differential

- `w-pDDT` is still an exact-shell accelerator, not a different search definition.
- Cache misses must fall back to exact generation.
- Checkpoints do not persist `shell_cache`; they persist only the cursor state required to continue enumeration.

### 6.2 Linear

- `Weight-Sliced cLAT` is still the current `z-shell`-driven engineering path.
- Strict mode must remain exact at the core implementation layer.
- Fast helper truncation remains a helper-only behavior and must not silently redefine strict search space semantics.

## 7. Batch / Subspace / Wrapper Relation

Wrapper-level batch and subspace pipelines still take only root jobs from external files.

The job file format stays root-only:

- root source pairs come from batch/subspace inputs,
- internal residual child pairs stay inside the runtime frontier/checkpoint/journal state,
- internal residuals are not promoted into new external job rows.

The wrapper-level checkpoint skeleton remains reusable:

- completed job flags,
- selected-source summaries,
- combined callback aggregation,
- batch/subspace runtime logs,
- batch/subspace checkpoint merge flows.

But the deep stage executed inside each single-job runtime is now residual-frontier based.

## 8. Current File-Level Mental Model

If you are reading the code today, the shortest correct summary is:

- `auto_search_frame`
  - owns best-search math, engine, single-run residual-frontier state, and single-run best-search checkpoint/resume
- `auto_subspace_hull`
  - owns strict collector runtime, collector checkpoints, batch/subspace orchestration, and wrapper-level aggregation
- both best-search and collector
  - are resumable,
  - are residual-frontier driven,
  - preserve current Q2/Q1 math,
  - use `kVersion = 1` binary checkpoint payloads,
  - and now expose pre-whitening plus `CROSS_XOR_ROT` as explicit zero-cost bridge steps inside the runtimes

One more sentence is needed for the current relation to the stricter mathematical blueprint:

- the current code has caught up on explicit local operator decomposition,
- but the linear residual semantic key is still the compressed engineering key `{stage_cursor, pair_a, pair_b, ...}`,
- and the architecture layer still does not provide an injective canonicalization proof for the stage-dependent blueprint object `σ̂_lin(κ)`.

## 9. Outdated Statements That No Longer Apply

The following old statements should no longer be used when reviewing this repository:

- “collector is one-shot only”
- “collector lives in `src/auto_search_frame/*_best_search_collector.cpp`”
- “single-run checkpoint version is `kVersion = 0`”
- “single-run best-search is only a fixed-root DFS with no internal residual frontier”

Those statements describe a previous generation of the codebase, not the current implementation.
