#pragma once

#include <optional>
#include <cstdint>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"

namespace TwilightDream::auto_search_linear
{
	// =============================================================================
	// Var-var modular addition (Schulte–Geers φ₂): two BnB / branching polarities
	// =============================================================================
	//
	// One physical add node s = x ⊞ y carries output mask u on s and input masks v on x, w on y.
	// The **order** in which the search fixes (u) vs (v,w) defines two different branch-and-bound shapes.
	//
	// 1) FixedOutputMaskU_EnumerateInputVW  —  **fixed u → (v,w)**
	//    - Fix u on the sum wire, enumerate (v,w) in the exact Schulte–Geers z-shell ∪_t L_t(u).
	//    - Acceleration: Weight-Sliced cLAT / split-8 SLR as an **index** on that shell (same multiset as
	//      bit-wise z recursion); final weights use `linear_correlation_add_ccz_weight(u,v,w)`.
	//    - This is the **default** NeoAlzette reverse-round integration in this repository.
	//
	// 2) FixedInputVW_ColumnOptimalOutputU —  **fixed (v,w) → u** (column / Theorem 7 side)
	//    - This local line is exact / strict for the fixed-(v,w) object:
	//         (v,w) -> `find_optimal_output_u_ccz` -> u* -> exact CCZ weight / value.
	//    - The root operator and the final CCZ rescoring follow the same Schulte-Geers φ₂ object as row max;
	//      for the fixed-slot `n <= 64` implementation, the column-max root path has the same Θ(log n)-depth
	//      contract stated in `modular_addition_ccz.hpp`.
	//    - See `linear_correlation_add_phi2_column_max` / `try_varvar_add_column_optimal_u_within_cap_u32`
	//      in `linear_best_search_math.hpp`. This is the direct linear analogue of differential
	//      `find_optimal_gamma` + XDP weight.
	//    - **Collector (`linear_best_search_collector.cpp`)**: emitted `(v,w)` pairs are rescored by this
	//      exact column oracle, and the resulting exact `u*` / column weight are written into
	//      `LinearTrailStepRecord`.
	//    - The difference from the default fixed-`u` branch is branch polarity / trail object, not local
	//      exactness: default NeoAlzette reverse-round search fixes `u` first, whereas this mode fixes `(v,w)`
	//      first and then takes the strict column maximum on `u`.
	//
	// Future extension (not implemented): full **fixed (v,w) → enumerate u** dual z-shell over `u` within a
	// weight cap, for callers that explicitly need the whole opposite shell rather than the single exact
	// column optimum `u*`.
	//
	// Search-frame integration rule:
	// - caller first obtains a raw `(v,w,weight_on_fixed_u)` candidate from the row-side shell generator;
	// - then `resolve_varvar_add_candidate_for_mode(...)` maps that raw candidate into the active local Q2
	//   object chosen by this mode:
	//     * row-side mode   => keep fixed `u`, keep the candidate's exact weight;
	//     * column-side mode => recompute exact / strict `u*` for that fixed `(v,w)`.
	// This keeps both var-var Q2 polarities on one interface instead of exposing the row-side shell and
	// the column-side root through unrelated call surfaces.
	//
	enum class LinearVarVarModularAddBnBMode : std::uint8_t
	{
		FixedOutputMaskU_EnumerateInputVW = 0,
		FixedInputVW_ColumnOptimalOutputU = 1,
	};

	// Unified search-frame view of the local var-var Q2 object:
	// regardless of whether the branch fixed `u` first (row-side) or fixed `(v,w)` first
	// (column-side), the caller receives one resolved exact local add candidate carrying:
	// - the exact local linear weight
	// - the sum-wire mask `u` that must be written onto the trail record
	struct LinearResolvedVarVarAddCandidate
	{
		SearchWeight linear_weight = INFINITE_WEIGHT;
		std::uint32_t sum_wire_u = 0;
	};

	enum class LinearVarVarAddCandidateResolutionKind : std::uint8_t
	{
		reject_candidate = 0,
		accept_candidate = 1,
		stop_enumerating = 2,
	};

	struct LinearVarVarAddCandidateResolution
	{
		LinearVarVarAddCandidateResolutionKind kind =
			LinearVarVarAddCandidateResolutionKind::reject_candidate;
		LinearResolvedVarVarAddCandidate candidate {};

		[[nodiscard]] bool accepted() const noexcept
		{
			return kind == LinearVarVarAddCandidateResolutionKind::accept_candidate;
		}

		[[nodiscard]] bool stop_enumerating() const noexcept
		{
			return kind == LinearVarVarAddCandidateResolutionKind::stop_enumerating;
		}
	};

	static inline const char* linear_varvar_modular_add_bnb_mode_to_string( LinearVarVarModularAddBnBMode m ) noexcept
	{
		switch ( m )
		{
		case LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW:
			return "fixed_u_enumerate_vw";
		case LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU:
			return "fixed_vw_column_optimal_u";
		default:
			return "unknown_varvar_modular_add_bnb_mode";
		}
	}

	/// @brief Hull **collector** 与 resumable Matsui `linear_best_search_engine` 都已接线的模式。
	static inline bool linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_search(
		LinearVarVarModularAddBnBMode m ) noexcept
	{
		return m == LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW ||
			m == LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU;
	}

	/// @brief Best-search engine 已接线的模式。
	static inline bool linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_best_engine(
		LinearVarVarModularAddBnBMode m ) noexcept
	{
		return m == LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW ||
			m == LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU;
	}

	// Unified exact local Q2 resolver for one physical add node:
	// - row-side: fixed `u` + candidate `(v,w)` => exact local weight on that fixed `u`
	// - column-side: fixed `(v,w)` => exact / strict column-optimal `u*`
	[[nodiscard]] LinearVarVarAddCandidateResolution resolve_varvar_add_candidate_for_mode(
		LinearVarVarModularAddBnBMode mode,
		std::uint32_t fixed_output_mask_u,
		const AddCandidate& candidate,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept;
}
