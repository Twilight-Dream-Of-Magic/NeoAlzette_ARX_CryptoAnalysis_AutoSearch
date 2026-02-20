#pragma once

#if !defined(NEOALZETTE_LINEAR_BEST_SEARCH_TYPES_HPP)
#define NEOALZETTE_LINEAR_BEST_SEARCH_TYPES_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_strict_witness.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_varvar_modular_add_bnb_mode.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/subtract_bnb_mode.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"
#include "auto_search_frame/detail/residual_frontier_shared.hpp"
#include "injection_analysis/linear_rank.hpp"

namespace TwilightDream::auto_search_linear
{
	struct LinearSearchFrame;

	// -----------------------------------------------------------------------------
	// Forward round structure (encryption direction) — see linear_best_search_math.hpp
	// -----------------------------------------------------------------------------
	//
	// Notes about injection modeling:
	// - For a fixed output mask u on the injected XOR term, the set of *correlated* input masks is an
	//   affine subspace  v ∈ l(u) ⊕ im(S(u))  (see injection section above). We enumerate masks in that
	//   affine subspace (capped by search_configuration.maximum_injection_input_masks) and charge weight = rank(S(u))/2.

	enum class StrictCertificationFailureReason : std::uint8_t
	{
		None = 0,
		UsedNonStrictSearchMode = 1,
		UsedBranchCap = 2,
		UsedNonStrictRemainingBound = 3,
		CheckpointLoadFailed = 4,
		ResumeCheckpointMismatch = 5,
		HitMaximumSearchNodes = 6,
		HitTimeLimit = 7,
		HitCollectionLimit = 8,
		HitCallbackStop = 9,
		UsedTargetBestWeightShortcut = 10,
		UnsupportedVarVarModularAddBnBMode = 11,
		UsedNonStrictSubconstMode = 12,
		UnsupportedVarConstSubBnbMode = 13,
		UnsupportedVarConstOuterQ2BindingForStage = 14
	};

	static inline const char* strict_certification_failure_reason_to_string(StrictCertificationFailureReason reason) noexcept
	{
		switch (reason)
		{
		case StrictCertificationFailureReason::None:
			return "none";
		case StrictCertificationFailureReason::UsedNonStrictSearchMode:
			return "used_non_strict_search_mode";
		case StrictCertificationFailureReason::UsedBranchCap:
			return "used_branch_cap";
		case StrictCertificationFailureReason::UsedNonStrictRemainingBound:
			return "used_non_strict_remaining_round_bound";
		case StrictCertificationFailureReason::CheckpointLoadFailed:
			return "checkpoint_load_failed";
		case StrictCertificationFailureReason::ResumeCheckpointMismatch:
			return "resume_checkpoint_mismatch";
		case StrictCertificationFailureReason::HitMaximumSearchNodes:
			return "hit_maximum_search_nodes";
		case StrictCertificationFailureReason::HitTimeLimit:
			return "hit_time_limit";
		case StrictCertificationFailureReason::HitCollectionLimit:
			return "hit_collection_limit";
		case StrictCertificationFailureReason::HitCallbackStop:
			return "hit_callback_stop";
		case StrictCertificationFailureReason::UsedTargetBestWeightShortcut:
			return "used_target_best_weight_shortcut";
		case StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode:
			return "unsupported_varvar_modular_add_bnb_mode";
		case StrictCertificationFailureReason::UsedNonStrictSubconstMode:
			return "used_non_strict_subconst_mode";
		case StrictCertificationFailureReason::UnsupportedVarConstSubBnbMode:
			return "unsupported_varconst_sub_bnb_mode";
		case StrictCertificationFailureReason::UnsupportedVarConstOuterQ2BindingForStage:
			return "unsupported_varconst_outer_q2_binding_for_stage";
		default:
			return "unknown";
		}
	}

	static inline bool strict_certification_failure_reason_is_hard_limit(StrictCertificationFailureReason reason) noexcept
	{
		switch (reason)
		{
		case StrictCertificationFailureReason::HitMaximumSearchNodes:
		case StrictCertificationFailureReason::HitTimeLimit:
		case StrictCertificationFailureReason::HitCollectionLimit:
		case StrictCertificationFailureReason::HitCallbackStop:
			return true;
		default:
			return false;
		}
	}

	enum class BestWeightCertificationStatus : std::uint8_t
	{
		None = 0,
		ThresholdTargetCertified = 1,
		ExactBestCertified = 2
	};

	static inline const char* best_weight_certification_status_to_string(BestWeightCertificationStatus status) noexcept
	{
		switch (status)
		{
		case BestWeightCertificationStatus::None:
			return "none";
		case BestWeightCertificationStatus::ThresholdTargetCertified:
			return "threshold_target_certified";
		case BestWeightCertificationStatus::ExactBestCertified:
			return "exact_best_certified";
		default:
			return "unknown";
		}
	}

	struct LinearTrailStepRecord
	{
		int round_index = 0;

		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;

		std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;
		std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;
		SearchWeight	  weight_second_constant_subtraction = 0;

		std::uint32_t output_branch_a_mask_after_second_addition = 0;
		std::uint32_t input_branch_a_mask_before_second_addition = 0;
		std::uint32_t second_addition_term_mask_from_branch_b = 0;
		SearchWeight	  weight_second_addition = 0;

		SearchWeight weight_injection_from_branch_a = 0;
		SearchWeight weight_injection_from_branch_b = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;
		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;
		// Reverse-round A-branch mask immediately after undoing the second-subround
		// A->B injection choice, before unwinding the surrounding cross-xor bridge.
		std::uint32_t branch_a_mask_after_undoing_injection_from_branch_a = 0;
		// Reverse-round B-branch mask after removing the second var-var add term,
		// but before undoing the first-subround B->A injection choice.
		std::uint32_t branch_b_mask_before_undoing_injection_from_branch_b = 0;
		// Reverse-round B-branch mask immediately after undoing the first-subround
		// B->A injection choice, before unwinding the surrounding cross-xor bridge.
		std::uint32_t branch_b_mask_after_undoing_injection_from_branch_b = 0;

		std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;
		std::uint32_t input_branch_a_mask_before_first_constant_subtraction = 0;
		SearchWeight	  weight_first_constant_subtraction = 0;

		std::uint32_t output_branch_b_mask_after_first_addition = 0;
		std::uint32_t input_branch_b_mask_before_first_addition = 0;
		std::uint32_t first_addition_term_mask_from_branch_a = 0;
		SearchWeight	  weight_first_addition = 0;

		SearchWeight round_weight = 0;
	};

	static inline long double exact_linear_step_correlation(const LinearTrailStepRecord& step) noexcept
	{
		return std::pow(2.0L, -static_cast<long double>(step.round_weight));
	}

	struct LinearBestSearchConfiguration
	{
		SearchMode   search_mode = SearchMode::Strict;
		int			  round_count = 1;
		SearchWeight  addition_weight_cap = 31;
		SearchWeight  constant_subtraction_weight_cap = 32;
		// Weight-sliced cLAT (split-8 SLR backend): **accelerator** for the exact Schulte–Geers z-shell
		// enumerator on the var-var row-side Q2 object (fixed sum-wire output mask u -> enumerate (v,w)).
		// Same mathematical object as plain split-8 streaming; default **on** for strict runs.
		// A cache/table miss must **never** be interpreted as “no correlation at this |z|”; it only means
		// the fast path did not cover that block (see `linear_best_search_math.hpp` z-shell section).
		bool		  enable_weight_sliced_clat = true;
		// 0  : strict-safe row-side Q2 shell index; shell-by-shell exact, miss != impossible.
		// >0 : materialized helper list cap; still only an index/cache hint, but the list is truncated,
		//      so strict shell-by-shell exactness is no longer claimed.
		std::size_t	  weight_sliced_clat_max_candidates = 0;
		// Var-var modular add: fixed u → enumerate (v,w) **vs** fixed (v,w) → exact / strict
		// column-optimal u* (see
		// `linear_varvar_modular_add_bnb_mode.hpp`). Binary checkpoint kVersion=0 config blob is unchanged;
		// this field is carried by in-memory / text metadata and `linear_configs_equal` / resume checks.
		LinearVarVarModularAddBnBMode varvar_modular_add_bnb_mode =
			LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW;
		// Var-const subtraction: fixed beta → enumerate/optimize alpha, or fixed alpha
		// → enumerate/optimize beta. This field selects the canonical Q2 polarity family.
		// The fixed-`β` accepted continuation is pinned to
		// `kterm4t0olent1hookprofneps`; the fixed-`α` accepted line is pinned to
		// `v116 tailtheorempath24_uclass4_root256_final512`.
		// This field does not by itself force an outer-hot-path collapsed branch.
		// That wiring is guarded by the polarity-specific gate booleans below.
		// This field is not serialized into the frozen binary config blob yet.
		LinearVarConstSubBnbMode varconst_sub_bnb_mode =
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha;
		// Research-only gate for the fixed-`β` outer fixed-output hot path.
		// Default stays strict/off; when enabled together with column-optimal fixed-`β`
		// mode, only eligible outer hot call sites may enter the collapsed branch.
		// This field is not serialized into the frozen binary config blob yet.
		bool		  enable_fixed_beta_outer_hot_path_gate = false;
		// Research-only gate for the fixed-`α` outer fixed-input hot path.
		// Default stays strict/off; current reverse-stage call sites only consume this
		// polarity where alpha is already fixed (or both sides are fixed).
		// This field is not serialized into the frozen binary config blob yet.
		bool		  enable_fixed_alpha_outer_hot_path_gate = false;
		std::size_t	  maximum_injection_input_masks = 0;
		std::size_t	  maximum_round_predecessors = 0;
		std::uint64_t maximum_search_nodes = 0;
		SearchWeight	  target_best_weight = INFINITE_WEIGHT;
		std::uint64_t maximum_search_seconds = 0;
		bool		  enable_state_memoization = true;
		bool		  enable_remaining_round_lower_bound = false;
		std::vector<SearchWeight> remaining_round_min_weight{};
		bool		  auto_generate_remaining_round_lower_bound = false;
		std::uint64_t remaining_round_lower_bound_generation_nodes = 0;
		std::uint64_t remaining_round_lower_bound_generation_seconds = 0;
		bool		  strict_remaining_round_lower_bound = true;
		bool		  enable_verbose_output = false;
	};

	static inline std::uint32_t compute_residual_absolute_round_index(
		int round_count,
		int rounds_remaining ) noexcept
	{
		if ( round_count <= 0 || rounds_remaining <= 0 || rounds_remaining > round_count )
			return 0u;
		return static_cast<std::uint32_t>( round_count - rounds_remaining + 1 );
	}

	static inline std::uint8_t root_residual_source_tag() noexcept
	{
		return static_cast<std::uint8_t>( TwilightDream::residual_frontier_shared::ResidualSourceTag::SourceInputPair );
	}

	static inline std::uint8_t child_residual_source_tag() noexcept
	{
		return static_cast<std::uint8_t>( TwilightDream::residual_frontier_shared::ResidualSourceTag::OutputAsNextInputPair );
	}

	static inline std::uint64_t compute_linear_residual_suffix_profile_id(
		const LinearBestSearchConfiguration& configuration ) noexcept;

	struct MatsuiSearchRunLinearResult
	{
		bool							   found = false;
		SearchWeight						   best_weight = INFINITE_WEIGHT;
		std::uint64_t					   nodes_visited = 0;
		bool							   hit_maximum_search_nodes = false;
		bool							   hit_time_limit = false;
		bool							   hit_target_best_weight = false;
		std::uint32_t					   best_input_branch_a_mask = 0;
		std::uint32_t					   best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail{};
		bool							   used_non_strict_branch_cap = false;
		bool							   used_target_best_weight_shortcut = false;
		bool							   exhaustive_completed = false;
		bool							   best_weight_certified = false;
		StrictCertificationFailureReason   strict_rejection_reason = StrictCertificationFailureReason::None;
	};

	static inline BestWeightCertificationStatus best_weight_certification_status(
		bool exact_best_weight_certified,
		StrictCertificationFailureReason strict_rejection_reason) noexcept
	{
		if (exact_best_weight_certified)
			return BestWeightCertificationStatus::ExactBestCertified;
		if (strict_rejection_reason == StrictCertificationFailureReason::UsedTargetBestWeightShortcut)
			return BestWeightCertificationStatus::ThresholdTargetCertified;
		return BestWeightCertificationStatus::None;
	}

	static inline BestWeightCertificationStatus best_weight_certification_status(const MatsuiSearchRunLinearResult& result) noexcept
	{
		return best_weight_certification_status(result.best_weight_certified, result.strict_rejection_reason);
	}

	#include "auto_subspace_hull/detail/linear_hull_runtime_types.hpp"

	struct LinearResidualFrontierEntry
	{
		TwilightDream::residual_frontier_shared::ResidualProblemRecord record{};
		std::shared_ptr<LinearSearchFrame> frame_snapshot{};
		std::vector<LinearTrailStepRecord> current_trail_snapshot{};
		SearchWeight prefix_weight_offset = 0;
	};

	struct LinearBestSearchContext
	{
		LinearBestSearchConfiguration configuration;
		LinearBestSearchRuntimeControls runtime_controls{};
		RuntimeInvocationState runtime_state{};
		SearchInvocationMetadata invocation_metadata{};

		std::uint32_t start_output_branch_a_mask = 0;
		std::uint32_t start_output_branch_b_mask = 0;

		std::uint64_t visited_node_count = 0;
		std::uint64_t run_visited_node_count = 0;
		SearchWeight  best_weight = INFINITE_WEIGHT;
		std::uint32_t best_input_branch_a_mask = 0;
		std::uint32_t best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail;
		std::vector<LinearTrailStepRecord> current_linear_trail;

		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, SearchWeight> memoization;

		std::chrono::steady_clock::time_point run_start_time{};
		std::uint64_t						  accumulated_elapsed_usec = 0;

		std::uint64_t						  progress_every_seconds = 0;
		std::uint64_t						  progress_node_mask = (1ull << 18) - 1;
		std::chrono::steady_clock::time_point progress_start_time{};
		std::chrono::steady_clock::time_point progress_last_print_time{};
		std::uint64_t						  progress_last_print_nodes = 0;
		bool								  progress_print_masks = false;

		std::chrono::steady_clock::time_point time_limit_start_time{};
		bool								  stop_due_to_time_limit = false;
		bool								  stop_due_to_target = false;

		BestWeightHistory* checkpoint = nullptr;
		BinaryCheckpointManager* binary_checkpoint = nullptr;
		RuntimeEventLog* runtime_event_log = nullptr;
		std::string		 history_log_output_path{};
		std::string		 runtime_log_output_path{};
		bool active_problem_valid = false;
		bool active_problem_is_root = false;
		TwilightDream::residual_frontier_shared::ResidualProblemRecord active_problem_record{};

		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> pending_frontier{};
		std::vector<LinearResidualFrontierEntry> pending_frontier_entries{};
		std::unordered_map<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			std::size_t,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> pending_frontier_index_by_key{};
		std::unordered_map<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			std::size_t,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> pending_frontier_entry_index_by_key{};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> completed_source_input_pairs{};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> completed_output_as_next_input_pairs{};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> transient_output_as_next_input_pair_candidates{};
		std::vector<LinearResidualFrontierEntry> transient_output_as_next_input_pair_entries{};
		std::unordered_set<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> completed_residual_set{};
		std::unordered_map<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			SearchWeight,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> best_prefix_by_residual_key{};
		std::vector<TwilightDream::residual_frontier_shared::ResidualResultRecord> global_residual_result_table{};
		TwilightDream::residual_frontier_shared::ResidualCounters residual_counters{};
		TwilightDream::residual_frontier_shared::LocalResidualStateDominanceTable local_state_dominance{};
	};

	static inline bool linear_configuration_has_strict_branch_cap(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.maximum_injection_input_masks != 0 ||
			configuration.maximum_round_predecessors != 0 ||
			(configuration.enable_weight_sliced_clat && configuration.weight_sliced_clat_max_candidates != 0);
	}

	static inline bool linear_configuration_uses_non_strict_remaining_round_bound(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.enable_remaining_round_lower_bound && !configuration.strict_remaining_round_lower_bound;
	}

	static inline bool linear_configuration_uses_non_strict_search_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.search_mode != SearchMode::Strict;
	}

	static inline bool linear_configuration_uses_fixed_beta_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha ||
			configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha;
	}

	static inline bool linear_configuration_uses_fixed_alpha_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta ||
			configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
	}

	static inline bool linear_configuration_uses_fixed_beta_column_optimal_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha;
	}

	static inline bool linear_configuration_uses_fixed_alpha_column_optimal_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.varconst_sub_bnb_mode ==
			LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
	}

	static inline bool linear_configuration_uses_column_optimal_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return
			linear_configuration_uses_fixed_beta_column_optimal_subconst_mode(configuration) ||
			linear_configuration_uses_fixed_alpha_column_optimal_subconst_mode(configuration);
	}

	static inline bool linear_configuration_uses_fixed_beta_outer_hot_path_gate(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.enable_fixed_beta_outer_hot_path_gate;
	}

	static inline bool linear_configuration_uses_fixed_alpha_outer_hot_path_gate(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return configuration.enable_fixed_alpha_outer_hot_path_gate;
	}

	static inline bool linear_configuration_requests_fixed_beta_outer_hot_path_collapsed_branch(
		const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_uses_fixed_beta_outer_hot_path_gate(configuration) &&
			linear_configuration_uses_fixed_beta_column_optimal_subconst_mode(configuration);
	}

	static inline bool linear_configuration_requests_fixed_alpha_outer_hot_path_collapsed_branch(
		const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_uses_fixed_alpha_outer_hot_path_gate(configuration) &&
			linear_configuration_uses_fixed_alpha_column_optimal_subconst_mode(configuration);
	}

	enum class LinearFixedBetaHotPathSubconstMode
	{
		ColumnOptimalCollapsed,
		StrictStreaming,
		StrictMaterialized
	};

	// Call-site gate for the fixed-beta outer hot path.
	// This belongs to the search-frame replacement contract layer, not to the
	// fixed-beta theorem engine itself.
	enum class LinearFixedBetaHotPathCallSite
	{
		EnginePrepareSecondSubconst,
		EnginePrepareFirstSubconst,
		CollectorPrepareSecondSubconst,
		CollectorPrepareFirstSubconst,
		EngineRoundPredecessorSecondSubconstMaterialization,
		EngineRoundPredecessorFirstSubconstMaterialization,
		CollectorRecursiveSecondSubconstMaterialization,
		CollectorRecursiveFirstSubconstMaterialization
	};

	struct LinearFixedBetaHotPathCallSiteContract
	{
		bool allows_collapsed_mode = false;
		bool consumes_candidate_presence = false;
		bool consumes_input_mask_alpha = false;
		bool consumes_linear_weight = false;
		bool consumes_weight_floor = false;
		bool requires_multiple_candidates = false;
		bool requires_shell_order = false;
		bool requires_enumeration_side_effects = false;
	};

	enum class LinearFixedBetaOuterHotPathEnterStatus : std::uint8_t
	{
		NoFeasibleCandidate = 0,
		Prepared = 1,
		RuntimeStop = 2
	};

	struct LinearFixedBetaOuterHotPathGate
	{
		LinearFixedBetaHotPathCallSite call_site = LinearFixedBetaHotPathCallSite::EnginePrepareSecondSubconst;
		bool collapsed_path_requested = false;
		bool collapsed_path_allowed = false;
		bool collapsed_path_active = false;
		LinearFixedBetaHotPathSubconstMode effective_subconst_mode =
			LinearFixedBetaHotPathSubconstMode::StrictStreaming;
	};

	static inline constexpr LinearFixedBetaHotPathCallSiteContract linear_fixed_beta_hot_path_call_site_contract(
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		switch (call_site)
		{
		case LinearFixedBetaHotPathCallSite::EnginePrepareSecondSubconst:
		case LinearFixedBetaHotPathCallSite::EnginePrepareFirstSubconst:
		case LinearFixedBetaHotPathCallSite::CollectorPrepareSecondSubconst:
		case LinearFixedBetaHotPathCallSite::CollectorPrepareFirstSubconst:
			return LinearFixedBetaHotPathCallSiteContract{
				.allows_collapsed_mode = true,
				.consumes_candidate_presence = true,
				.consumes_input_mask_alpha = true,
				.consumes_linear_weight = true,
				.consumes_weight_floor = true,
				.requires_multiple_candidates = false,
				.requires_shell_order = false,
				.requires_enumeration_side_effects = false };
		case LinearFixedBetaHotPathCallSite::EngineRoundPredecessorSecondSubconstMaterialization:
		case LinearFixedBetaHotPathCallSite::EngineRoundPredecessorFirstSubconstMaterialization:
		case LinearFixedBetaHotPathCallSite::CollectorRecursiveSecondSubconstMaterialization:
		case LinearFixedBetaHotPathCallSite::CollectorRecursiveFirstSubconstMaterialization:
			return LinearFixedBetaHotPathCallSiteContract{
				.allows_collapsed_mode = false,
				.consumes_candidate_presence = true,
				.consumes_input_mask_alpha = true,
				.consumes_linear_weight = true,
				.consumes_weight_floor = true,
				.requires_multiple_candidates = true,
				.requires_shell_order = true,
				.requires_enumeration_side_effects = false };
		default:
			return {};
		}
	}

	static inline constexpr bool linear_fixed_beta_hot_path_call_site_allows_collapsed_mode(
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		return linear_fixed_beta_hot_path_call_site_contract(call_site).allows_collapsed_mode;
	}

	static inline constexpr bool linear_fixed_beta_hot_path_call_site_consumes_only_local_best_semantics(
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		const auto contract = linear_fixed_beta_hot_path_call_site_contract(call_site);
		return contract.allows_collapsed_mode &&
			contract.consumes_candidate_presence &&
			contract.consumes_input_mask_alpha &&
			contract.consumes_linear_weight &&
			contract.consumes_weight_floor &&
			!contract.requires_multiple_candidates &&
			!contract.requires_shell_order &&
			!contract.requires_enumeration_side_effects;
	}

	static inline constexpr bool linear_fixed_beta_hot_path_call_site_requires_strict_candidates(
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		const auto contract = linear_fixed_beta_hot_path_call_site_contract(call_site);
		return !contract.allows_collapsed_mode &&
			(contract.requires_multiple_candidates ||
				contract.requires_shell_order ||
				contract.requires_enumeration_side_effects);
	}

	static inline LinearFixedBetaHotPathSubconstMode linear_configuration_fixed_beta_hot_path_subconst_mode(
		const LinearBestSearchConfiguration& configuration) noexcept
	{
		if (linear_configuration_requests_fixed_beta_outer_hot_path_collapsed_branch(configuration))
			return LinearFixedBetaHotPathSubconstMode::ColumnOptimalCollapsed;
		if (configuration.search_mode == SearchMode::Strict)
			return LinearFixedBetaHotPathSubconstMode::StrictStreaming;
		return LinearFixedBetaHotPathSubconstMode::StrictMaterialized;
	}

	static inline LinearFixedBetaHotPathSubconstMode linear_configuration_fixed_beta_hot_path_subconst_mode_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		const auto base_mode = linear_configuration_fixed_beta_hot_path_subconst_mode(configuration);
		if (base_mode != LinearFixedBetaHotPathSubconstMode::ColumnOptimalCollapsed)
			return base_mode;
		if (linear_fixed_beta_hot_path_call_site_allows_collapsed_mode(call_site))
			return base_mode;
		return LinearFixedBetaHotPathSubconstMode::StrictMaterialized;
	}

	static inline bool linear_configuration_fixed_beta_hot_path_uses_streaming_cursor_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		return linear_configuration_fixed_beta_hot_path_subconst_mode_for_call_site(configuration, call_site) ==
			LinearFixedBetaHotPathSubconstMode::StrictStreaming;
	}

	static inline bool linear_configuration_fixed_beta_hot_path_uses_materialized_candidates_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		return !linear_configuration_fixed_beta_hot_path_uses_streaming_cursor_for_call_site(
			configuration,
			call_site);
	}

	static inline bool linear_configuration_uses_strict_subconst_enumeration(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_fixed_beta_hot_path_subconst_mode(configuration) !=
			LinearFixedBetaHotPathSubconstMode::ColumnOptimalCollapsed;
	}

	static inline LinearFixedBetaOuterHotPathGate linear_configuration_fixed_beta_outer_hot_path_gate_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site) noexcept
	{
		const auto effective_mode =
			linear_configuration_fixed_beta_hot_path_subconst_mode_for_call_site(
				configuration,
				call_site);
		const bool collapsed_requested =
			linear_configuration_requests_fixed_beta_outer_hot_path_collapsed_branch(configuration);
		const bool collapsed_allowed =
			linear_fixed_beta_hot_path_call_site_allows_collapsed_mode(call_site);
		return LinearFixedBetaOuterHotPathGate{
			.call_site = call_site,
			.collapsed_path_requested = collapsed_requested,
			.collapsed_path_allowed = collapsed_allowed,
			.collapsed_path_active = collapsed_requested && collapsed_allowed,
			.effective_subconst_mode = effective_mode };
	}

	static inline bool linear_fixed_beta_outer_hot_path_gate_uses_streaming_cursor(
		const LinearFixedBetaOuterHotPathGate& gate) noexcept
	{
		return gate.effective_subconst_mode == LinearFixedBetaHotPathSubconstMode::StrictStreaming;
	}

	static inline bool linear_fixed_beta_outer_hot_path_gate_uses_materialized_candidates(
		const LinearFixedBetaOuterHotPathGate& gate) noexcept
	{
		return !linear_fixed_beta_outer_hot_path_gate_uses_streaming_cursor(gate);
	}

	static inline bool linear_configuration_uses_streaming_subconst_cursor(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_fixed_beta_hot_path_subconst_mode(configuration) ==
			LinearFixedBetaHotPathSubconstMode::StrictStreaming;
	}

	static inline bool linear_configuration_uses_non_strict_subconst_mode(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_uses_column_optimal_subconst_mode(configuration);
	}

	static inline bool linear_configuration_requests_unsupported_varconst_outer_q2_binding(
		const LinearBestSearchConfiguration& configuration) noexcept
	{
		// In the current reverse-round topology, both outer subconst stage objects fix beta first.
		// Fixed-alpha remains a concrete-pair/runtime resolve mode only; it is not yet a true outer-Q2 shell source.
		return linear_configuration_uses_fixed_alpha_subconst_mode(configuration);
	}

	static inline bool linear_collector_configuration_has_non_strict_branch_cap(const LinearBestSearchConfiguration& configuration) noexcept
	{
		return linear_configuration_has_strict_branch_cap(configuration);
	}

	static inline bool linear_configs_equal(const LinearBestSearchConfiguration& a, const LinearBestSearchConfiguration& b)
	{
		return a.search_mode == b.search_mode &&
			a.round_count == b.round_count &&
			a.addition_weight_cap == b.addition_weight_cap &&
			a.constant_subtraction_weight_cap == b.constant_subtraction_weight_cap &&
			a.enable_weight_sliced_clat == b.enable_weight_sliced_clat &&
			a.weight_sliced_clat_max_candidates == b.weight_sliced_clat_max_candidates &&
			a.varvar_modular_add_bnb_mode == b.varvar_modular_add_bnb_mode &&
			a.varconst_sub_bnb_mode == b.varconst_sub_bnb_mode &&
			a.enable_fixed_beta_outer_hot_path_gate == b.enable_fixed_beta_outer_hot_path_gate &&
			a.enable_fixed_alpha_outer_hot_path_gate == b.enable_fixed_alpha_outer_hot_path_gate &&
			a.maximum_injection_input_masks == b.maximum_injection_input_masks &&
			a.maximum_round_predecessors == b.maximum_round_predecessors &&
			a.target_best_weight == b.target_best_weight &&
			a.enable_state_memoization == b.enable_state_memoization &&
			a.enable_verbose_output == b.enable_verbose_output &&
			a.enable_remaining_round_lower_bound == b.enable_remaining_round_lower_bound &&
			a.remaining_round_min_weight == b.remaining_round_min_weight &&
			a.strict_remaining_round_lower_bound == b.strict_remaining_round_lower_bound &&
			a.auto_generate_remaining_round_lower_bound == b.auto_generate_remaining_round_lower_bound &&
			a.remaining_round_lower_bound_generation_nodes == b.remaining_round_lower_bound_generation_nodes &&
			a.remaining_round_lower_bound_generation_seconds == b.remaining_round_lower_bound_generation_seconds;
	}

	static inline void normalize_linear_config_for_compare(LinearBestSearchConfiguration& configuration)
	{
		configuration.addition_weight_cap = std::min<SearchWeight>(configuration.addition_weight_cap, 31);
		configuration.constant_subtraction_weight_cap = std::min<SearchWeight>(configuration.constant_subtraction_weight_cap, 32);
		if (configuration.enable_remaining_round_lower_bound)
		{
			auto& remaining_round_min_weight_table = configuration.remaining_round_min_weight;
			if (remaining_round_min_weight_table.empty())
			{
				remaining_round_min_weight_table.assign(std::size_t(std::max(0, configuration.round_count)) + 1u, 0);
			}
			else
			{
				if (remaining_round_min_weight_table.empty())
					remaining_round_min_weight_table.resize(1u, 0);
				remaining_round_min_weight_table[0] = 0;
				const std::size_t need = std::size_t(std::max(0, configuration.round_count)) + 1u;
				if (remaining_round_min_weight_table.size() < need)
					remaining_round_min_weight_table.resize(need, 0);
				for (SearchWeight& round_min_weight : remaining_round_min_weight_table)
				{
					(void)round_min_weight;
				}
			}
		}
	}

	static inline bool linear_configs_compatible_for_resume(LinearBestSearchConfiguration a, LinearBestSearchConfiguration b)
	{
		normalize_linear_config_for_compare(a);
		normalize_linear_config_for_compare(b);
		return linear_configs_equal(a, b);
	}

	static inline std::size_t linear_runtime_memo_reserve_hint(const LinearBestSearchContext& context) noexcept
	{
		const std::uint64_t effective_limit = runtime_effective_maximum_search_nodes(context.runtime_controls);
		const std::size_t	hint =
			(effective_limit == 0) ?
			std::size_t(0) :
			static_cast<std::size_t>(std::min<std::uint64_t>(effective_limit, std::uint64_t(std::numeric_limits<std::size_t>::max())));
		return compute_initial_memo_reserve_hint(hint);
	}

	static inline void sync_linear_runtime_legacy_fields_from_state(LinearBestSearchContext& context) noexcept
	{
		context.visited_node_count = context.runtime_state.total_nodes_visited;
		context.run_visited_node_count = context.runtime_state.run_nodes_visited;
		context.run_start_time = context.runtime_state.run_start_time;
		context.progress_node_mask = context.runtime_state.progress_node_mask;
		context.stop_due_to_time_limit = runtime_time_limit_hit(context.runtime_controls, context.runtime_state);
		context.configuration.maximum_search_nodes = runtime_effective_maximum_search_nodes(context.runtime_controls);
		context.configuration.maximum_search_seconds = context.runtime_controls.maximum_search_seconds;
	}

	static inline void begin_linear_runtime_invocation(LinearBestSearchContext& context) noexcept
	{
		context.runtime_state.total_nodes_visited = context.visited_node_count;
		begin_runtime_invocation(context.runtime_controls, context.runtime_state);
		context.progress_every_seconds = context.runtime_controls.progress_every_seconds;
		context.progress_start_time = context.runtime_state.run_start_time;
		context.progress_last_print_time = {};
		context.progress_last_print_nodes = 0;
		sync_linear_runtime_legacy_fields_from_state(context);
	}

	static inline bool linear_note_runtime_node_visit(LinearBestSearchContext& context) noexcept
	{
		const bool stop = runtime_note_node_visit(context.runtime_controls, context.runtime_state);
		sync_linear_runtime_legacy_fields_from_state(context);
		return stop;
	}

	static inline bool linear_runtime_node_limit_hit(LinearBestSearchContext& context) noexcept
	{
		const bool hit = runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state);
		sync_linear_runtime_legacy_fields_from_state(context);
		return hit;
	}

	static inline bool linear_runtime_node_limit_hit(const LinearBestSearchContext& context) noexcept
	{
		return runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state);
	}

	static inline bool linear_runtime_budget_hit(LinearBestSearchContext& context) noexcept
	{
		return runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state) ||
			runtime_time_limit_hit(context.runtime_controls, context.runtime_state);
	}

	static inline void linear_runtime_log_basic_event(
		const LinearBestSearchContext& context,
		const char* event_name,
		const char* reason = "running")
	{
		if (!context.runtime_event_log)
			return;
		context.runtime_event_log->write_event(
			event_name,
			[&](std::ostream& out) {
				out << "round_count=" << context.configuration.round_count << "\n";
				out << "start_output_mask_branch_a=" << hex8(context.start_output_branch_a_mask) << "\n";
				out << "start_output_mask_branch_b=" << hex8(context.start_output_branch_b_mask) << "\n";
				out << "best_weight=" << display_search_weight(context.best_weight) << "\n";
				out << "run_nodes_visited=" << context.run_visited_node_count << "\n";
				out << "total_nodes_visited=" << context.visited_node_count << "\n";
				out << "elapsed_seconds=" << std::fixed << std::setprecision(3) << TwilightDream::best_search_shared_core::accumulated_elapsed_seconds(context) << "\n";
				out << "runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "\n";
				out << "runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "\n";
				out << "runtime_progress_every_seconds=" << context.runtime_controls.progress_every_seconds << "\n";
				out << "runtime_checkpoint_every_seconds=" << context.runtime_controls.checkpoint_every_seconds << "\n";
				out << "runtime_progress_node_mask=" << context.runtime_state.progress_node_mask << "\n";
				out << "runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name(TwilightDream::runtime_component::runtime_time_limit_scope()) << "\n";
				out << "runtime_budget_mode=" << runtime_budget_mode_name(context.runtime_controls) << "\n";
				out << "maxnodes_ignored_due_to_time_limit=" << (runtime_nodes_ignored_due_to_time_limit(context.runtime_controls) ? 1 : 0) << "\n";
				out << "stop_reason=" << (reason ? reason : "running") << "\n";
				out << "physical_available_gib=" << std::fixed << std::setprecision(3) << bytes_to_gibibytes(context.invocation_metadata.physical_available_bytes) << "\n";
				out << "estimated_must_live_gib=" << std::fixed << std::setprecision(3) << bytes_to_gibibytes(context.invocation_metadata.estimated_must_live_bytes) << "\n";
				out << "estimated_optional_rebuildable_gib=" << std::fixed << std::setprecision(3) << bytes_to_gibibytes(context.invocation_metadata.estimated_optional_rebuildable_bytes) << "\n";
				out << "memory_gate=" << memory_gate_status_name(context.invocation_metadata.memory_gate_status) << "\n";
				out << "startup_memory_gate_policy=" << (context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject") << "\n";
				out << "reason=" << (reason ? reason : "running") << "\n";
			});
	}

	template <class OnInputMaskFn>
	static inline void enumerate_affine_subspace_input_masks(LinearBestSearchContext& context, const InjectionCorrelationTransition& transition, std::size_t maximum_input_mask_count, OnInputMaskFn&& on_input_mask)
	{
		std::size_t produced_count = 0;
		if (transition.rank <= 0)
		{
			if (runtime_time_limit_reached())
				return;
			if (linear_runtime_node_limit_hit(context))
				return;
			if (linear_note_runtime_node_visit(context))
				return;
			on_input_mask(transition.offset_mask);
			return;
		}

		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_mask = 0;
			std::uint8_t  branch_state = 0;
		};

		std::array<Frame, 64> stack{};
		int					  stack_step = 0;
		stack[stack_step++] = Frame{ 0, transition.offset_mask, 0 };

		while (stack_step > 0)
		{
			if (runtime_time_limit_reached())
				return;
			if (maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count)
				return;
			if (linear_runtime_node_limit_hit(context))
				return;

			Frame& frame = stack[stack_step - 1];
			if (frame.branch_state == 0)
			{
				if (static_cast<std::uint64_t>(frame.basis_index) >= transition.rank)
				{
					if (linear_runtime_node_limit_hit(context))
						return;
					if (linear_note_runtime_node_visit(context))
						return;
					on_input_mask(frame.current_mask);
					++produced_count;
					--stack_step;
					continue;
				}

				frame.branch_state = 1;
				stack[stack_step++] = Frame{ frame.basis_index + 1, frame.current_mask, 0 };
				continue;
			}

			const std::uint32_t next_mask = frame.current_mask ^ transition.basis_vectors[std::size_t(frame.basis_index)];
			const int			next_index = frame.basis_index + 1;
			--stack_step;
			stack[stack_step++] = Frame{ next_index, next_mask, 0 };
		}
	}

	static inline StrictCertificationFailureReason classify_linear_best_search_strict_rejection_reason(
		const MatsuiSearchRunLinearResult& result,
		const LinearBestSearchConfiguration& configuration,
		bool used_non_strict_remaining_round_bound_override) noexcept
	{
		if (result.strict_rejection_reason != StrictCertificationFailureReason::None)
			return result.strict_rejection_reason;
		if (!linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_best_engine(
			configuration.varvar_modular_add_bnb_mode))
			return StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode;
		if (!linear_varconst_sub_bnb_mode_integrated_in_neoalzette_linear_best_engine(
			configuration.varconst_sub_bnb_mode))
			return StrictCertificationFailureReason::UnsupportedVarConstSubBnbMode;
		if (linear_configuration_requests_unsupported_varconst_outer_q2_binding(configuration))
			return StrictCertificationFailureReason::UnsupportedVarConstOuterQ2BindingForStage;
		if (linear_configuration_uses_non_strict_subconst_mode(configuration))
			return StrictCertificationFailureReason::UsedNonStrictSubconstMode;
		if (linear_configuration_uses_non_strict_search_mode(configuration))
			return StrictCertificationFailureReason::UsedNonStrictSearchMode;
		if (linear_configuration_has_strict_branch_cap(configuration))
			return StrictCertificationFailureReason::UsedBranchCap;
		if (used_non_strict_remaining_round_bound_override)
			return StrictCertificationFailureReason::UsedNonStrictRemainingBound;
		if (result.hit_maximum_search_nodes)
			return StrictCertificationFailureReason::HitMaximumSearchNodes;
		if (result.hit_time_limit)
			return StrictCertificationFailureReason::HitTimeLimit;
		if (result.hit_target_best_weight || result.used_target_best_weight_shortcut)
			return StrictCertificationFailureReason::UsedTargetBestWeightShortcut;
		return StrictCertificationFailureReason::None;
	}

	static inline StrictCertificationFailureReason classify_linear_collection_exactness_reason(
		const LinearHullAggregationResult& aggregation_result,
		bool used_non_strict_search_mode = false,
		bool used_non_strict_subconst_mode = false,
		bool unsupported_varconst_outer_q2_binding = false) noexcept
	{
		if (aggregation_result.exactness_rejection_reason != StrictCertificationFailureReason::None)
			return aggregation_result.exactness_rejection_reason;
		if (unsupported_varconst_outer_q2_binding)
			return StrictCertificationFailureReason::UnsupportedVarConstOuterQ2BindingForStage;
		if (used_non_strict_subconst_mode)
			return StrictCertificationFailureReason::UsedNonStrictSubconstMode;
		if (used_non_strict_search_mode)
			return StrictCertificationFailureReason::UsedNonStrictSearchMode;
		if (aggregation_result.hit_maximum_search_nodes)
			return StrictCertificationFailureReason::HitMaximumSearchNodes;
		if (aggregation_result.hit_time_limit)
			return StrictCertificationFailureReason::HitTimeLimit;
		if (aggregation_result.hit_collection_limit)
			return StrictCertificationFailureReason::HitCollectionLimit;
		if (aggregation_result.hit_callback_stop)
			return StrictCertificationFailureReason::HitCallbackStop;
		if (aggregation_result.used_non_strict_branch_cap)
			return StrictCertificationFailureReason::UsedBranchCap;
		return StrictCertificationFailureReason::None;
	}

	enum class LinearVarVarRowQ2ContractKind : std::uint8_t
	{
		Split8ExactPath = 0,
		WeightSlicedClatExactShellIndex = 1,
		WeightSlicedClatTruncatedHelper = 2
	};

	struct LinearVarVarRowQ2Contract
	{
		LinearVarVarRowQ2ContractKind kind = LinearVarVarRowQ2ContractKind::Split8ExactPath;
		bool accelerator_enabled = false;
		bool shell_by_shell_exact = true;
		bool miss_means_cache_only_not_impossible = false;
		bool materialized_list_is_truncated = false;
		bool used_as_candidate_source_for_column_mode = false;
	};

	static inline const char* linear_varvar_row_q2_contract_kind_to_string(LinearVarVarRowQ2ContractKind kind) noexcept
	{
		switch (kind)
		{
		case LinearVarVarRowQ2ContractKind::Split8ExactPath:
			return "split8_exact_path";
		case LinearVarVarRowQ2ContractKind::WeightSlicedClatExactShellIndex:
			return "weight_sliced_clat_exact_shell_index";
		case LinearVarVarRowQ2ContractKind::WeightSlicedClatTruncatedHelper:
			return "weight_sliced_clat_truncated_helper";
		default:
			return "unknown";
		}
	}

	static inline LinearVarVarRowQ2Contract linear_varvar_row_q2_contract(const LinearBestSearchConfiguration& configuration) noexcept
	{
		const bool candidate_source_for_column_mode =
			configuration.varvar_modular_add_bnb_mode ==
			LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU;

		if (!configuration.enable_weight_sliced_clat)
		{
			return LinearVarVarRowQ2Contract{
				.kind = LinearVarVarRowQ2ContractKind::Split8ExactPath,
				.accelerator_enabled = false,
				.shell_by_shell_exact = true,
				.miss_means_cache_only_not_impossible = false,
				.materialized_list_is_truncated = false,
				.used_as_candidate_source_for_column_mode = candidate_source_for_column_mode };
		}

		if (configuration.weight_sliced_clat_max_candidates == 0)
		{
			return LinearVarVarRowQ2Contract{
				.kind = LinearVarVarRowQ2ContractKind::WeightSlicedClatExactShellIndex,
				.accelerator_enabled = true,
				.shell_by_shell_exact = true,
				.miss_means_cache_only_not_impossible = true,
				.materialized_list_is_truncated = false,
				.used_as_candidate_source_for_column_mode = candidate_source_for_column_mode };
		}

		return LinearVarVarRowQ2Contract{
			.kind = LinearVarVarRowQ2ContractKind::WeightSlicedClatTruncatedHelper,
			.accelerator_enabled = true,
			.shell_by_shell_exact = false,
			.miss_means_cache_only_not_impossible = true,
			.materialized_list_is_truncated = true,
			.used_as_candidate_source_for_column_mode = candidate_source_for_column_mode };
	}

	enum class LinearVarConstOuterQ2Polarity : std::uint8_t
	{
		None = 0,
		FixedBeta = 1
	};

	enum class LinearVarConstOuterFloorKind : std::uint8_t
	{
		None = 0,
		FixedBetaColumnFloor = 1
	};

	enum class LinearVarConstOuterStreamKind : std::uint8_t
	{
		None = 0,
		FixedBetaStrictWitness = 1
	};

	enum class LinearVarConstConcretePairResolveKind : std::uint8_t
	{
		None = 0,
		FixedBetaExactQ1 = 1,
		FixedAlphaRuntimeExact = 2
	};

	enum class LinearVarConstStageSlot : std::uint8_t
	{
		None = 0,
		SecondConst = 1,
		FirstSubconst = 2
	};

	struct LinearVarConstStageContract
	{
		LinearVarConstStageSlot stage = LinearVarConstStageSlot::None;
		bool is_varconst_stage = false;
		LinearVarConstOuterQ2Polarity outer_q2_polarity = LinearVarConstOuterQ2Polarity::None;
		LinearVarConstOuterFloorKind outer_floor_kind = LinearVarConstOuterFloorKind::None;
		LinearVarConstOuterStreamKind outer_stream_kind = LinearVarConstOuterStreamKind::None;
		LinearVarConstConcretePairResolveKind concrete_pair_resolve_kind =
			LinearVarConstConcretePairResolveKind::None;
		bool ordered_stream_safe_break = false;
		bool current_stage_binding_supported = true;
		const char* summary = "";
	};

	static inline const char* linear_varconst_outer_q2_polarity_to_string(LinearVarConstOuterQ2Polarity polarity) noexcept
	{
		switch (polarity)
		{
		case LinearVarConstOuterQ2Polarity::FixedBeta:
			return "fixed_beta";
		case LinearVarConstOuterQ2Polarity::None:
		default:
			return "none";
		}
	}

	static inline const char* linear_varconst_concrete_pair_resolve_kind_to_string(
		LinearVarConstConcretePairResolveKind kind) noexcept
	{
		switch (kind)
		{
		case LinearVarConstConcretePairResolveKind::FixedBetaExactQ1:
			return "fixed_beta_exact_q1";
		case LinearVarConstConcretePairResolveKind::FixedAlphaRuntimeExact:
			return "fixed_alpha_runtime_exact";
		case LinearVarConstConcretePairResolveKind::None:
		default:
			return "none";
		}
	}

	static inline LinearVarConstStageContract linear_varconst_stage_contract(
		LinearVarConstStageSlot stage,
		const LinearBestSearchConfiguration& configuration) noexcept
	{
		switch (stage)
		{
		case LinearVarConstStageSlot::SecondConst:
		case LinearVarConstStageSlot::FirstSubconst:
			return LinearVarConstStageContract{
				.stage = stage,
				.is_varconst_stage = true,
				.outer_q2_polarity = LinearVarConstOuterQ2Polarity::FixedBeta,
				.outer_floor_kind = LinearVarConstOuterFloorKind::FixedBetaColumnFloor,
				.outer_stream_kind = LinearVarConstOuterStreamKind::FixedBetaStrictWitness,
				.concrete_pair_resolve_kind =
					linear_configuration_uses_fixed_alpha_subconst_mode(configuration) ?
						LinearVarConstConcretePairResolveKind::FixedAlphaRuntimeExact :
						LinearVarConstConcretePairResolveKind::FixedBetaExactQ1,
				.ordered_stream_safe_break = true,
				.current_stage_binding_supported =
					!linear_configuration_requests_unsupported_varconst_outer_q2_binding(configuration),
				.summary =
					linear_configuration_uses_fixed_alpha_subconst_mode(configuration) ?
						"outer fixed-beta Q2 with fixed-alpha runtime exact resolve" :
						"outer fixed-beta Q2 with fixed-beta exact Q1 resolve" };
		default:
			return LinearVarConstStageContract{
				.stage = stage,
				.is_varconst_stage = false,
				.outer_q2_polarity = LinearVarConstOuterQ2Polarity::None,
				.outer_floor_kind = LinearVarConstOuterFloorKind::None,
				.outer_stream_kind = LinearVarConstOuterStreamKind::None,
				.concrete_pair_resolve_kind = LinearVarConstConcretePairResolveKind::None,
				.ordered_stream_safe_break = false,
				.current_stage_binding_supported = true,
				.summary = "not a var-const stage" };
		}
	}

	static inline void print_linear_varconst_stage_contract_banner(const LinearBestSearchConfiguration& configuration)
	{
		const auto second = linear_varconst_stage_contract(LinearVarConstStageSlot::SecondConst, configuration);
		const auto first = linear_varconst_stage_contract(LinearVarConstStageSlot::FirstSubconst, configuration);
		std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] second_const_outer_q2="
			<< linear_varconst_outer_q2_polarity_to_string(second.outer_q2_polarity)
			<< "  second_const_pair_resolve="
			<< linear_varconst_concrete_pair_resolve_kind_to_string(second.concrete_pair_resolve_kind)
			<< "  second_const_binding_supported=" << (second.current_stage_binding_supported ? "yes" : "no")
			<< "\n";
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] first_subconst_outer_q2="
			<< linear_varconst_outer_q2_polarity_to_string(first.outer_q2_polarity)
			<< "  first_subconst_pair_resolve="
			<< linear_varconst_concrete_pair_resolve_kind_to_string(first.concrete_pair_resolve_kind)
			<< "  first_subconst_binding_supported=" << (first.current_stage_binding_supported ? "yes" : "no")
			<< "\n";
	}

	static inline void print_linear_weight_sliced_clat_banner(const LinearBestSearchConfiguration& c)
	{
		const LinearVarVarRowQ2Contract row_q2_contract = linear_varvar_row_q2_contract(c);
		const LinearVarConstStageContract second_subconst_contract =
			linear_varconst_stage_contract(LinearVarConstStageSlot::SecondConst, c);
		const LinearVarConstStageContract first_subconst_contract =
			linear_varconst_stage_contract(LinearVarConstStageSlot::FirstSubconst, c);
		std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] weight_sliced_clat=" << (c.enable_weight_sliced_clat ? "on" : "off")
			<< "  weight_sliced_clat_max_candidates=" << c.weight_sliced_clat_max_candidates
			<< "  varvar_modular_add_bnb=" << linear_varvar_modular_add_bnb_mode_to_string(c.varvar_modular_add_bnb_mode)
			<< "  varconst_sub_bnb=" << linear_varconst_sub_bnb_mode_to_string(c.varconst_sub_bnb_mode)
			<< "  fixed_beta_outer_hot_path_gate=" << (c.enable_fixed_beta_outer_hot_path_gate ? "on" : "off")
			<< "  fixed_alpha_outer_hot_path_gate=" << (c.enable_fixed_alpha_outer_hot_path_gate ? "on" : "off")
			<< "  allocation=on_demand_exact\n";
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] varvar_row_q2_contract="
			<< linear_varvar_row_q2_contract_kind_to_string(row_q2_contract.kind)
			<< "  shell_by_shell_exact=" << (row_q2_contract.shell_by_shell_exact ? "yes" : "no")
			<< "  miss_semantics="
			<< (row_q2_contract.miss_means_cache_only_not_impossible ? "cache_only_not_impossible" : "not_applicable")
			<< "  truncated_list=" << (row_q2_contract.materialized_list_is_truncated ? "yes" : "no")
			<< "  q2_role="
			<< (row_q2_contract.used_as_candidate_source_for_column_mode ? "candidate_source_for_column_mode" : "active_row_side_q2")
			<< "\n";
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] second_const_outer_q2="
			<< linear_varconst_outer_q2_polarity_to_string(second_subconst_contract.outer_q2_polarity)
			<< "  second_const_pair_resolve="
			<< linear_varconst_concrete_pair_resolve_kind_to_string(second_subconst_contract.concrete_pair_resolve_kind)
			<< "  second_const_binding_supported=" << (second_subconst_contract.current_stage_binding_supported ? "yes" : "no")
			<< "\n";
		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Linear] first_subconst_outer_q2="
			<< linear_varconst_outer_q2_polarity_to_string(first_subconst_contract.outer_q2_polarity)
			<< "  first_subconst_pair_resolve="
			<< linear_varconst_concrete_pair_resolve_kind_to_string(first_subconst_contract.concrete_pair_resolve_kind)
			<< "  first_subconst_binding_supported=" << (first_subconst_contract.current_stage_binding_supported ? "yes" : "no")
			<< "\n";
	}

	static inline std::uint64_t compute_linear_residual_suffix_profile_id(
		const LinearBestSearchConfiguration& configuration ) noexcept
	{
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
		const auto row_q2_contract = linear_varvar_row_q2_contract( configuration );
		const auto second_subconst_contract =
			linear_varconst_stage_contract( LinearVarConstStageSlot::SecondConst, configuration );
		const auto first_subconst_contract =
			linear_varconst_stage_contract( LinearVarConstStageSlot::FirstSubconst, configuration );
		fp.mix_enum( configuration.search_mode );
		fp.mix_u64( configuration.addition_weight_cap );
		fp.mix_u64( configuration.constant_subtraction_weight_cap );
		fp.mix_bool( configuration.enable_weight_sliced_clat );
		fp.mix_u64( static_cast<std::uint64_t>( configuration.weight_sliced_clat_max_candidates ) );
		fp.mix_enum( configuration.varvar_modular_add_bnb_mode );
		fp.mix_enum( configuration.varconst_sub_bnb_mode );
		fp.mix_bool( configuration.enable_fixed_beta_outer_hot_path_gate );
		fp.mix_bool( configuration.enable_fixed_alpha_outer_hot_path_gate );
		fp.mix_u64( static_cast<std::uint64_t>( configuration.maximum_injection_input_masks ) );
		fp.mix_u64( static_cast<std::uint64_t>( configuration.maximum_round_predecessors ) );
		fp.mix_bool( configuration.enable_remaining_round_lower_bound );
		fp.mix_bool( configuration.strict_remaining_round_lower_bound );
		fp.mix_enum( row_q2_contract.kind );
		fp.mix_bool( row_q2_contract.accelerator_enabled );
		fp.mix_bool( row_q2_contract.shell_by_shell_exact );
		fp.mix_bool( row_q2_contract.miss_means_cache_only_not_impossible );
		fp.mix_bool( row_q2_contract.materialized_list_is_truncated );
		fp.mix_bool( row_q2_contract.used_as_candidate_source_for_column_mode );
		fp.mix_enum( second_subconst_contract.outer_q2_polarity );
		fp.mix_enum( second_subconst_contract.outer_floor_kind );
		fp.mix_enum( second_subconst_contract.outer_stream_kind );
		fp.mix_enum( second_subconst_contract.concrete_pair_resolve_kind );
		fp.mix_bool( second_subconst_contract.ordered_stream_safe_break );
		fp.mix_bool( second_subconst_contract.current_stage_binding_supported );
		fp.mix_enum( first_subconst_contract.outer_q2_polarity );
		fp.mix_enum( first_subconst_contract.outer_floor_kind );
		fp.mix_enum( first_subconst_contract.outer_stream_kind );
		fp.mix_enum( first_subconst_contract.concrete_pair_resolve_kind );
		fp.mix_bool( first_subconst_contract.ordered_stream_safe_break );
		fp.mix_bool( first_subconst_contract.current_stage_binding_supported );
		return fp.finish();
	}

	static inline void maybe_print_single_run_progress(LinearBestSearchContext& ctx, int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask)
	{
		if (ctx.progress_every_seconds == 0)
			return;
		if ((ctx.visited_node_count & ctx.progress_node_mask) != 0)
			return;

		const auto now = std::chrono::steady_clock::now();
		memory_governor_poll_if_needed(now);
		if (ctx.progress_last_print_time.time_since_epoch().count() != 0)
		{
			const double since_last = std::chrono::duration<double>(now - ctx.progress_last_print_time).count();
			if (since_last < double(ctx.progress_every_seconds))
				return;
		}

		const double		elapsed = std::chrono::duration<double>(now - ctx.progress_start_time).count();
		const double		window = (ctx.progress_last_print_time.time_since_epoch().count() == 0) ? elapsed : std::chrono::duration<double>(now - ctx.progress_last_print_time).count();
		const std::uint64_t delta_nodes = (ctx.visited_node_count >= ctx.progress_last_print_nodes) ? (ctx.visited_node_count - ctx.progress_last_print_nodes) : 0;
		const double		rate = (window > 1e-9) ? (double(delta_nodes) / window) : 0.0;

		std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());

		const auto old_flags = std::cout.flags();
		const auto old_prec = std::cout.precision();
		const auto old_fill = std::cout.fill();

		TwilightDream::runtime_component::print_progress_prefix(std::cout);
		std::cout << "[Progress] visited_node_count=" << ctx.visited_node_count
			<< "  visited_nodes_per_second=" << std::fixed << std::setprecision(2) << rate
			<< "  elapsed_seconds=" << std::fixed << std::setprecision(2) << elapsed
			<< "  best_weight=" << display_search_weight(ctx.best_weight);
		std::cout << "  current_depth=" << depth << "  total_rounds=" << ctx.configuration.round_count;
		if (ctx.progress_print_masks)
		{
			std::cout << "  ";
			print_word32_hex("start_output_mask_branch_a=", ctx.start_output_branch_a_mask);
			std::cout << " ";
			print_word32_hex("start_output_mask_branch_b=", ctx.start_output_branch_b_mask);
			std::cout << " ";
			print_word32_hex("current_output_mask_branch_a=", current_round_output_branch_a_mask);
			std::cout << " ";
			print_word32_hex("current_output_mask_branch_b=", current_round_output_branch_b_mask);
		}
		std::cout << "\n";

		std::cout.flags(old_flags);
		std::cout.precision(old_prec);
		std::cout.fill(old_fill);

		ctx.progress_last_print_time = now;
		ctx.progress_last_print_nodes = ctx.visited_node_count;
	}

	struct LinearAffineMaskEnumerator
	{
		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_mask = 0;
			std::uint8_t  branch_state = 0;
		};

		static constexpr int MAX_STACK = 64;

		bool		  initialized = false;
		bool		  stop_due_to_limits = false;
		std::size_t	  maximum_input_mask_count = 0;
		std::size_t	  produced_count = 0;
		std::uint64_t rank = 0;
		int			  stack_step = 0;
		std::array<Frame, MAX_STACK> stack{};

		void reset(const InjectionCorrelationTransition& transition, std::size_t max_masks)
		{
			initialized = true;
			stop_due_to_limits = false;
			maximum_input_mask_count = max_masks;
			produced_count = 0;
			rank = transition.rank;
			stack_step = 0;
			stack[stack_step++] = Frame{ 0, transition.offset_mask, 0 };
		}

		bool next(LinearBestSearchContext& context, const InjectionCorrelationTransition& transition, std::uint32_t& out_mask)
		{
			if (!initialized || stop_due_to_limits)
				return false;

			while (stack_step > 0)
			{
				if (maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count)
					return false;

				Frame& frame = stack[stack_step - 1];

				if (frame.branch_state == 0)
				{
					if (linear_note_runtime_node_visit(context))
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress(context, -1, 0u, 0u);

					if (static_cast<std::uint64_t>(frame.basis_index) >= transition.rank)
					{
						out_mask = frame.current_mask;
						++produced_count;
						--stack_step;
						return true;
					}

					frame.branch_state = 1;
					stack[stack_step++] = Frame{ frame.basis_index + 1, frame.current_mask, 0 };
					continue;
				}

				if (linear_runtime_node_limit_hit(context))
				{
					stop_due_to_limits = true;
					return false;
				}
				if (maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count)
					return false;

				frame = Frame{ frame.basis_index + 1, frame.current_mask ^ transition.basis_vectors[std::size_t(frame.basis_index)], 0 };
			}

			return false;
		}
	};

	// -----------------------------------------------------------------------------
	// Round-local linear BnB contract (reverse mask search).
	//
	// Pure linear transport is handled outside this state by transpose propagation and
	// contributes weight 0.  Only nonlinear ARX steps enter the round pipeline below.
	//
	// Canonical NeoAlzette reverse-round operator order:
	//   1. InjA       : exact injection operator on the current branch-B output mask
	//   2. SecondAdd  : var-var modular-add operator on branch A
	//   3. InjB       : exact injection operator induced after fixing the second add
	//   4. SecondConst: var-const subtraction operator on branch B
	//   5. FirstSubconst
	//                 : var-const subtraction operator on branch A
	//   6. FirstAdd   : var-var modular-add operator on branch B
	//
	// Local operator semantics:
	// - Injection does not split into Q2/Q1 files; it already gives an exact affine
	//   correlated-input space plus exact local weight rank/2.
	// - var-var modular add is consumed as:
	//     Q2 = fixed-u z-shell stream OR fixed-(v,w) exact column-optimal u*
	//     Q1 = exact Schulte-Geers / CCZ local weight on a concrete (u,v,w)
	// - var-const subtraction is consumed as:
	//     Q2 = fixed-beta / fixed-alpha local floor and/or ordered witness stream
	//     Q1 = exact var-const local weight on a concrete (alpha, C, beta)
	//
	// Engineering rules that the engine / collector must preserve:
	// - every nonlinear step may prune immediately from its exact local weight or exact
	//   lower bound;
	// - an ordered strict stream may stop early only when its contract proves all
	//   remaining candidates are not lighter;
	// - accelerators (split-8, weight-sliced cLAT, hot-path caches) are indexing layers,
	//   never truth oracles.
	// ----------------------------------------------------------------------------

	struct LinearRoundSearchState
	{
		int round_boundary_depth = 0;
		SearchWeight accumulated_weight_so_far = 0;
		int round_index = 0;
		SearchWeight round_weight_cap = 0;
		SearchWeight remaining_round_weight_lower_bound_after_this_round = 0;

		std::uint32_t round_output_branch_a_mask = 0;
		std::uint32_t round_output_branch_b_mask = 0;

		// Round-output mask on A before enumerating correlated input masks for injection from A (backward walk).
		std::uint32_t branch_a_round_output_mask_before_inj_from_a = 0;
		std::uint32_t branch_b_mask_before_injection_from_branch_a = 0;

		InjectionCorrelationTransition injection_from_branch_a_transition{};
		LinearAffineMaskEnumerator injection_from_branch_a_enumerator{};
		SearchWeight					   weight_injection_from_branch_a = 0;
		SearchWeight					   remaining_after_inj_a = 0;
		SearchWeight					   second_subconst_weight_cap = 0;
		// Optimization-only cache: exact minimum local weight on the fixed-beta second subconst column.
		// -1 = unknown/not computed yet, INFINITE_WEIGHT = no candidate within the current cap.
		// INFINITE_WEIGHT = unknown/not computed yet; MAX_FINITE_SEARCH_WEIGHT = no candidate within current cap.
		SearchWeight					   second_subconst_weight_floor = INFINITE_WEIGHT;
		// Dual-fixed exact check / future fixed-alpha hot-path cache for the same second subconst slot.
		SearchWeight					   second_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
		SearchWeight					   second_add_weight_cap = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;

		std::uint32_t output_branch_a_mask_after_second_addition = 0;
		std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;

		SubConstStreamingCursor second_constant_subtraction_stream_cursor{};
		FixedAlphaSubColumnStrictWitnessStreamingCursor second_constant_subtraction_fixed_alpha_stream_cursor{};
		WeightSlicedClatStreamingCursor second_addition_weight_sliced_clat_stream_cursor{};
		std::vector<SubConstCandidate> second_constant_subtraction_candidates_for_branch_b;
		std::vector<FixedAlphaSubConstCandidate> second_constant_subtraction_fixed_alpha_candidates_for_branch_b;
		std::vector<AddCandidate> second_addition_candidates_storage;
		AddVarVarSplit8Enumerator32::StreamingCursor second_addition_stream_cursor{};
		const std::vector<AddCandidate>* second_addition_candidates_for_branch_a = nullptr;

		std::size_t second_addition_candidate_index = 0;

		std::uint32_t input_branch_a_mask_before_second_addition = 0;
		std::uint32_t second_addition_term_mask_from_branch_b = 0;
		SearchWeight	  weight_second_addition = 0;
		// FixedInputVW_ColumnOptimalOutputU: exact / strict column-optimal u* on sum wire after second add
		// (Theorem 7); fixed-u 模式为 0 表示未用。
		std::uint32_t second_add_trail_sum_wire_u = 0;
		std::uint32_t branch_b_mask_contribution_from_second_addition_term = 0;

		InjectionCorrelationTransition injection_from_branch_b_transition{};
		LinearAffineMaskEnumerator injection_from_branch_b_enumerator{};
		SearchWeight					   weight_injection_from_branch_b = 0;
		SearchWeight					   base_weight_after_inj_b = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;

		std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;
		SearchWeight	  weight_second_constant_subtraction = 0;
		// B-branch mask after removing the second var-var addition term contribution (backward unwind).
		std::uint32_t branch_b_mask_after_second_add_term_removed = 0;
		std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a_base = 0;

		std::size_t second_constant_subtraction_candidate_index = 0;

		SearchWeight base_weight_after_second_subconst = 0;
		SearchWeight first_subconst_weight_cap = 0;
		// Optimization-only cache for the first fixed-beta subconst column on the current branch choice.
		SearchWeight first_subconst_weight_floor = INFINITE_WEIGHT;
		// Dual-fixed exact check / future fixed-alpha hot-path cache for the first subconst slot.
		SearchWeight first_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
		SearchWeight first_add_weight_cap = 0;

		std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;
		std::uint32_t output_branch_b_mask_after_first_addition = 0;

		SubConstStreamingCursor first_constant_subtraction_stream_cursor{};
		FixedAlphaSubColumnStrictWitnessStreamingCursor first_constant_subtraction_fixed_alpha_stream_cursor{};
		WeightSlicedClatStreamingCursor first_addition_weight_sliced_clat_stream_cursor{};
		std::vector<SubConstCandidate> first_constant_subtraction_candidates_for_branch_a;
		std::vector<FixedAlphaSubConstCandidate> first_constant_subtraction_fixed_alpha_candidates_for_branch_a;
		std::vector<AddCandidate> first_addition_candidates_for_branch_b;
		AddVarVarSplit8Enumerator32::StreamingCursor first_addition_stream_cursor{};

		std::size_t first_constant_subtraction_candidate_index = 0;
		std::size_t first_addition_candidate_index = 0;

		std::uint32_t input_branch_a_mask_before_first_constant_subtraction_current = 0;
		SearchWeight	  weight_first_constant_subtraction_current = 0;
		// FixedInputVW_ColumnOptimalOutputU: exact / strict column-optimal u* on sum wire after first add
		// (branch B)；固定 u 模式为 0。
		std::uint32_t first_add_trail_sum_wire_u = 0;

		std::vector<LinearTrailStepRecord> round_predecessors;
	};

	// Round-expansion stage machine for the canonical reverse BnB layout above.
	// The stage order is part of the mathematical wiring, not just UI / control flow:
	// each stage commits exactly one local nonlinear operator (or its branch handoff).
	enum class LinearSearchStage : std::uint8_t
	{
		Enter = 0,       // Global pruning + derive round-local caps / bounds before touching nonlinear operators.
		Enumerate = 1,   // Round-predecessor replay handoff for buffered expansion mode.
		InjA = 2,        // Injection-A exact affine mask space + exact rank/2 weight.
		SecondAdd = 3,   // Second var-var add: Q2 candidate/root operator, then exact local weight.
		InjB = 4,        // Injection-B exact affine mask space + exact rank/2 weight.
		SecondConst = 5, // Second var-const subtract: Q2 floor/witness stream, then exact local weight.
		FirstSubconst = 6, // First var-const subtract: Q2 floor/witness stream, then exact local weight.
		FirstAdd = 7,      // First var-var add: Q2 candidate/root operator, then exact local weight.
		Recurse = 8        // Emit/consume completed predecessor steps for the next round boundary.
	};

	enum class LinearSearchStageOperatorKind : std::uint8_t
	{
		Control = 0,
		Injection = 1,
		VarVarModularAdd = 2,
		VarConstSubtraction = 3
	};

	struct LinearSearchStageContract
	{
		LinearSearchStage stage = LinearSearchStage::Enter;
		LinearSearchStageOperatorKind operator_kind = LinearSearchStageOperatorKind::Control;
		bool is_nonlinear_operator_stage = false;
		bool uses_split_q2_q1 = false;
		bool exact_local_weight_available = false;
		bool ordered_strict_stream_may_early_stop = false;
		const char* summary = "";
	};

	// Centralized semantic table for the linear reverse-round BnB pipeline.
	// Future ARX algorithms may change the concrete operators, but if they reuse this
	// framework they should still preserve the stage-level meaning recorded here:
	// control stages only manage search state, while nonlinear operator stages must expose
	// exact local weights / floors according to the listed contract.
	static constexpr std::array<LinearSearchStageContract, 9> kLinearSearchStageContracts = {
		LinearSearchStageContract {
			LinearSearchStage::Enter,
			LinearSearchStageOperatorKind::Control,
			false,
			false,
			false,
			false,
			"derive round-local caps and global pruning state before nonlinear operators"
		},
		LinearSearchStageContract {
			LinearSearchStage::Enumerate,
			LinearSearchStageOperatorKind::Control,
			false,
			false,
			false,
			false,
			"replay buffered predecessor list in round-predecessor mode"
		},
		LinearSearchStageContract {
			LinearSearchStage::InjA,
			LinearSearchStageOperatorKind::Injection,
			true,
			false,
			true,
			false,
			"exact injection operator: affine input-mask space plus exact rank/2 weight"
		},
		LinearSearchStageContract {
			LinearSearchStage::SecondAdd,
			LinearSearchStageOperatorKind::VarVarModularAdd,
			true,
			true,
			true,
			true,
			"second var-var modular add: Q2 candidate/root polarity then exact local add weight"
		},
		LinearSearchStageContract {
			LinearSearchStage::InjB,
			LinearSearchStageOperatorKind::Injection,
			true,
			false,
			true,
			false,
			"exact injection operator induced after fixing the second add"
		},
		LinearSearchStageContract {
			LinearSearchStage::SecondConst,
			LinearSearchStageOperatorKind::VarConstSubtraction,
			true,
			true,
			true,
			true,
			"second var-const subtract: Q2 floor/witness stream then exact local var-const weight"
		},
		LinearSearchStageContract {
			LinearSearchStage::FirstSubconst,
			LinearSearchStageOperatorKind::VarConstSubtraction,
			true,
			true,
			true,
			true,
			"first var-const subtract: Q2 floor/witness stream then exact local var-const weight"
		},
		LinearSearchStageContract {
			LinearSearchStage::FirstAdd,
			LinearSearchStageOperatorKind::VarVarModularAdd,
			true,
			true,
			true,
			true,
			"first var-var modular add: Q2 candidate/root polarity then exact local add weight"
		},
		LinearSearchStageContract {
			LinearSearchStage::Recurse,
			LinearSearchStageOperatorKind::Control,
			false,
			false,
			false,
			false,
			"commit a finished predecessor step and descend to the next round boundary"
		}
	};

	static inline constexpr const LinearSearchStageContract& linear_search_stage_contract(LinearSearchStage stage) noexcept
	{
		return kLinearSearchStageContracts[std::size_t(stage)];
	}

	struct LinearSearchFrame
	{
		LinearSearchStage stage = LinearSearchStage::Enter;
		std::size_t		  trail_size_at_entry = 0;
		LinearRoundSearchState state{};
		std::size_t		  predecessor_index = 0;
	};

	struct LinearSearchCursor
	{
		std::vector<LinearSearchFrame> stack;
	};

	// Matsui engine cursor entry point. The active var-var modular-add polarity is recorded in
	// `LinearBestSearchConfiguration::varvar_modular_add_bnb_mode` and is handled strictly by
	// `linear_best_search_engine.cpp`.
	void linear_best_search_continue_from_cursor(LinearBestSearchContext& context, LinearSearchCursor& cursor);

	MatsuiSearchRunLinearResult run_linear_best_search(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& search_configuration,
		const LinearBestSearchRuntimeControls& runtime_controls = {},
		bool print_output = false,
		bool progress_print_masks = false,
		SearchWeight seeded_upper_bound_weight = INFINITE_WEIGHT,
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail = nullptr,
		BestWeightHistory* checkpoint = nullptr,
		BinaryCheckpointManager* binary_checkpoint = nullptr,
		RuntimeEventLog* runtime_event_log = nullptr,
		const SearchInvocationMetadata* invocation_metadata = nullptr);

	MatsuiSearchRunLinearResult run_linear_best_search_resume(
		const std::string& checkpoint_path,
		std::uint32_t expected_output_branch_a_mask,
		std::uint32_t expected_output_branch_b_mask,
		const LinearBestSearchConfiguration& expected_configuration,
		const LinearBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_masks,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata,
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask* runtime_override_mask_opt = nullptr,
		const LinearBestSearchConfiguration* execution_configuration_override = nullptr,
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt = nullptr);

	#include "auto_subspace_hull/detail/linear_hull_runtime_api.hpp"

}  // namespace TwilightDream::auto_search_linear

#endif
