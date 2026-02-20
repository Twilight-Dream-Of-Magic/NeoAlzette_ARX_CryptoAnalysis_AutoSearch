#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_varvar_modular_add_bnb_mode.hpp"

namespace TwilightDream::auto_search_linear
{
	enum class LinearVarVarStageSlot : std::uint8_t
	{
		SecondAdd = 0,
		FirstAdd = 1,
	};

	struct LinearOrderedQ2StreamContract
	{
		bool exact_weight_monotone_non_decreasing = false;
		bool safe_break_when_cap_exceeded = false;
		bool accelerator_is_index_only = false;
	};

	struct LinearVarVarQ2Candidate
	{
		AddCandidate q1_input {};
		std::uint32_t sum_wire_u = 0;
		SearchWeight exact_weight_hint = INFINITE_WEIGHT;
		LinearOrderedQ2StreamContract ordered_stream {};
	};

	struct LinearVarVarQ2Q1Evaluation
	{
		LinearVarVarAddCandidateResolutionKind kind =
			LinearVarVarAddCandidateResolutionKind::reject_candidate;
		LinearResolvedVarVarAddCandidate candidate {};
		LinearOrderedQ2StreamContract ordered_stream {};

		[[nodiscard]] bool accepted() const noexcept
		{
			return kind == LinearVarVarAddCandidateResolutionKind::accept_candidate;
		}

		[[nodiscard]] bool stop_enumerating() const noexcept
		{
			return kind == LinearVarVarAddCandidateResolutionKind::stop_enumerating;
		}
	};

	struct LinearVarConstQ2Q1Evaluation
	{
		bool accepted = false;
		SearchWeight exact_local_weight = INFINITE_WEIGHT;
		LinearOrderedQ2StreamContract ordered_stream {};
	};

	struct LinearVarConstQ2Candidate
	{
		std::uint32_t input_mask_alpha = 0;
		std::uint32_t output_mask_beta = 0;
		SearchWeight exact_weight_hint = INFINITE_WEIGHT;
		LinearOrderedQ2StreamContract ordered_stream {};
	};

	[[nodiscard]] std::optional<SearchWeight> weight_sub_const_ceil_int(
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant,
		std::uint32_t output_mask_beta ) noexcept;

	[[nodiscard]] std::optional<SearchWeight> evaluate_varconst_sub_q1_exact_weight(
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant,
		std::uint32_t output_mask_beta ) noexcept;

	[[nodiscard]] AddCandidate add_candidate_phi2_row_max(
		std::uint32_t output_mask_u,
		int n = 32 ) noexcept;

	[[nodiscard]] std::optional<VarVarAddColumnOptimalOnSumWire> try_varvar_add_column_optimal_u_within_cap_u32(
		std::uint32_t input_mask_v,
		std::uint32_t input_mask_w,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] LinearOrderedQ2StreamContract linear_varvar_q2_ordered_stream_contract(
		const LinearBestSearchConfiguration& configuration ) noexcept;

	[[nodiscard]] LinearOrderedQ2StreamContract linear_varconst_fixed_beta_q2_ordered_stream_contract(
		const LinearFixedBetaOuterHotPathGate& hot_path_gate ) noexcept;

	[[nodiscard]] LinearVarVarQ2Q1Evaluation evaluate_varvar_add_q2_q1_candidate(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t fixed_output_mask_u,
		const AddCandidate& candidate,
		SearchWeight weight_cap,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] LinearVarConstQ2Q1Evaluation evaluate_varconst_sub_q2_q1_candidate(
		const LinearBestSearchConfiguration& configuration,
		const SubConstCandidate& candidate,
		std::uint32_t output_mask_beta,
		std::uint32_t subtrahend_constant,
		SearchWeight weight_cap,
		const LinearFixedBetaOuterHotPathGate& hot_path_gate,
		SearchWeight* fixed_alpha_weight_floor = nullptr ) noexcept;

	[[nodiscard]] LinearVarConstQ2Q1Evaluation evaluate_fixed_alpha_subconst_q2_q1_candidate(
		const FixedAlphaSubConstCandidate& candidate,
		std::uint32_t input_mask_alpha,
		std::uint32_t subtrahend_constant ) noexcept;

	bool prepare_varvar_step_q2(
		LinearRoundSearchState& state,
		LinearVarVarStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		const std::function<bool()>& sync_and_check_runtime_stop );

	bool next_varvar_step_q2_candidate(
		LinearRoundSearchState& state,
		LinearVarVarStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearVarVarQ2Candidate& out_candidate );

	bool resolve_varvar_q2_candidate(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t fixed_output_mask_u,
		SearchWeight weight_cap,
		const AddCandidate& raw_candidate,
		LinearVarVarQ2Candidate& out_candidate ) noexcept;

	[[nodiscard]] std::optional<SearchWeight> evaluate_varvar_q1_exact_weight(
		const LinearVarVarQ2Candidate& candidate,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] std::optional<VarConstSubColumnOptimalOnOutputWire> compute_varconst_step_q2_floor(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration ) noexcept;

	[[nodiscard]] SearchWeight ensure_varconst_step_q2_floor(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration ) noexcept;

	[[nodiscard]] LinearFixedBetaOuterHotPathEnterStatus enter_varconst_step_q2(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		const std::function<bool()>& sync_and_check_runtime_stop );

	bool next_varconst_step_q2_candidate(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		LinearVarConstQ2Candidate& out_candidate );

	bool resolve_varconst_q2_candidate(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		std::uint32_t output_mask_beta,
		const SubConstCandidate& raw_candidate,
		LinearVarConstQ2Candidate& out_candidate ) noexcept;

	[[nodiscard]] std::optional<SearchWeight> evaluate_varconst_q1_exact_weight(
		const LinearVarConstQ2Candidate& candidate,
		std::uint32_t subtrahend_constant,
		SearchWeight weight_cap = INFINITE_WEIGHT,
		SearchWeight* fixed_alpha_weight_floor = nullptr,
		int n_bits = 32 ) noexcept;

	[[nodiscard]] std::vector<LinearVarConstQ2Candidate> materialize_varconst_q2_candidates_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap );
}  // namespace TwilightDream::auto_search_linear
