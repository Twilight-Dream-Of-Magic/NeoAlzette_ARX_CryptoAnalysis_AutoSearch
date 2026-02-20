#pragma once

#include "auto_search_frame/detail/differential_best_search_primitives.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

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
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace TwilightDream::auto_search_differential
{
	//
	//   (3)  A_out = A5 ⊕ RC10
	//        B_out = B5 ⊕ RC11
	//
	// Record fields below store Δ-values at each boundary and the per-step weight.
	// ============================================================================

	struct DifferentialTrailStepRecord
	{
		int round_index = 0;

		// Round input XOR-differences:
		//   ΔA0 = A0 ⊕ A0' ,  ΔB0 = B0 ⊕ B0'
		std::uint32_t input_branch_a_difference = 0;  // ΔA0
		std::uint32_t input_branch_b_difference = 0;  // ΔB0

		// ------------------------------------------------------------------------
		// (1) B-addition:  B1 = B0 ⊞ T0
		//     where T0 = ROTL_31(A0) ⊕ ROTL_17(A0) ⊕ RC0
		//
		//   β0 = ΔT0,  γ0 = ΔB1,  w_add0 = -log2 Pr[Δ(B0 ⊞ T0)=γ0 | ΔB0, β0]
		std::uint32_t first_addition_term_difference = 0;					// β0 = ΔT0
		std::uint32_t output_branch_b_difference_after_first_addition = 0;	// γ0 = ΔB1
		int			  weight_first_addition = 0;							// w_add0

		// (1) A-const subtraction:  A1 = A0 ⊟ RC1
		//   γA0 = ΔA1,  w_subA = diff_subconst_exact_weight_ceil_int(ΔA0 -> γA0; RC1)
		std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;	// ΔA1
		int			  weight_first_constant_subtraction = 0;							// w_subA

		// (1) XOR/ROT mixing:
		//   A2 = A1 ⊕ ROTL_{R0}(B1)
		//   B2 = B1 ⊕ ROTL_{R1}(A2)
		// XOR-diff propagation is deterministic:
		//   ΔA2 = ΔA1 ⊕ ROTL_{R0}(ΔB1)
		//   ΔB2 = ΔB1 ⊕ ROTL_{R1}(ΔA2)
		std::uint32_t branch_a_difference_after_first_xor_with_rotated_branch_b = 0;  // ΔA2
		std::uint32_t branch_b_difference_after_first_xor_with_rotated_branch_a = 0;  // ΔB2

		// (1) Injection B -> A (structure-preserved):
		//   (C0, D0) = cd_injection_from_B(B2; rc0, rc1)
		//   f_B(B2) = ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ ROTL_8((C0 >> 1) ⊕ (D0 << 1)) ⊕ RC4
		//   A3 = A2 ⊕ f_B(B2)
		//
		// We store:
		//   ΔI_B = Δf_B(B2)   (the injected XOR mask difference)
		// and attach the exact affine-derivative rank weight via InjectionAffineTransition (rank_weight).
		std::uint32_t injection_from_branch_b_xor_difference = 0;			  // ΔI_B
		int			  weight_injection_from_branch_b = 0;					  // w_injB (rank_weight)
		std::uint32_t branch_a_difference_after_injection_from_branch_b = 0;  // ΔA3 = ΔA2 ⊕ ΔI_B

		// ΔB on the B-branch after the first subround XOR/ROT bridge (equals ΔB2); feeds T1 in the second subround.
		std::uint32_t branch_b_difference_after_first_bridge = 0;

		// ------------------------------------------------------------------------
		// (2) A-addition:  A4 = A3 ⊞ T1
		//     where T1 = ROTL_31(B3) ⊕ ROTL_17(B3) ⊕ RC5   and B3 == B2 in the current V6 core
		//
		//   β1 = ΔT1,  γ1 = ΔA4,  w_add1 computed by add BV-weight operator
		std::uint32_t second_addition_term_difference = 0;					 // β1 = ΔT1
		std::uint32_t output_branch_a_difference_after_second_addition = 0;	 // γ1 = ΔA4
		int			  weight_second_addition = 0;							 // w_add1

		// (2) B-const subtraction:  B4 = B3 ⊟ RC6
		//   γB1 = ΔB4, w_subB = diff_subconst_exact_weight_ceil_int(ΔB3 -> γB1; RC6)
		std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;	 // ΔB4
		int			  weight_second_constant_subtraction = 0;							 // w_subB

		// (2) XOR/ROT mixing:
		//   B5 = B4 ⊕ ROTL_{R0}(A4)
		//   A5 = A4 ⊕ ROTL_{R1}(B5)
		std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;  // ΔB5
		std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;  // ΔA5

		// (2) Injection A -> B (structure-preserved):
		//   (C1, D1) = cd_injection_from_A(A5; rc0, rc1)
		//   f_A(A5) = ROTR_24(C1) ⊕ ROTR_16(D1) ⊕ ROTR_8((C1 << 3) ⊕ (D1 >> 3)) ⊕ RC9
		//   B6 = B5 ⊕ f_A(A5)
		// Store injected mask difference ΔI_A and the exact affine-derivative rank weight.
		std::uint32_t injection_from_branch_a_xor_difference = 0;  // ΔI_A
		int			  weight_injection_from_branch_a = 0;		   // w_injA (rank_weight)

		// End-of-round boundary in the current V6 core:
		//   ΔA_out = ΔA5,  ΔB_out = ΔB6
		// The final XOR with RC[10]/RC[11] cancels in XOR-difference propagation.
		std::uint32_t output_branch_a_difference = 0;  // ΔA_out (round boundary)
		std::uint32_t output_branch_b_difference = 0;  // ΔB_out (round boundary)

		// Round aggregate weight:
		//   w_round = w_add0 + w_subA + w_injB + w_add1 + w_subB + w_injA
		int round_weight = 0;
	};

	struct DifferentialBestSearchConfiguration
	{
		int			round_count = 2;
		int			addition_weight_cap = 31;						// extra cap (0..31). default=31 (no extra cap beyond the B&B bound)
		int			constant_subtraction_weight_cap = 32;			// cap for subconst weight (0..32). 32 means no extra cap.
		// Performance knob: branch limiter for enumerating injected transition output differences (0=exact/all).
		// NOTE: This does not change the underlying transition model; it only limits branching during search.
		std::size_t	  maximum_transition_output_differences = 0;
		// Runtime shadow copies. The true runtime budget lives in `runtime_controls`.
		std::uint64_t maximum_search_nodes = 0;
		int			  target_best_weight = -1;				// if >=0: stop early once best_total_weight <= target_best_weight
		std::uint64_t maximum_search_seconds = 0;
		bool		  enable_state_memoization = true;		// prune revisits: (depth, difference_a, difference_b) -> best weight so far
		bool		  enable_verbose_output = false;

		// Matsui-style remaining-round lower bound (weight domain):
		// Paper notation uses probabilities B_{n-r} for the remaining (n-r) rounds. In weight form:
		//   W_k = -log2(B_k)
		// and pruning is:
		//   accumulated_weight_so_far + W_{rounds_left} >= best_total_weight  => prune.
		//
		// IMPORTANT correctness rule:
		// `remaining_round_min_weight[k]` must be a LOWER bound on the minimal possible weight of ANY k-round continuation
		// (i.e., it must never overestimate). If unsure, leave this disabled or provide zeros.
		bool			  enable_remaining_round_lower_bound = false;
		std::vector<int>  remaining_round_min_weight {};	// index: rounds_left (0..round_count). Must satisfy remaining_round_min_weight[0]==0.
		bool			  strict_remaining_round_lower_bound = true;
		// Weight-Sliced Partial DDT (w-pDDT) cache: exact weight shells for modular addition.
		bool			  enable_weight_sliced_pddt = true;		 // accelerator only; miss => exact fallback
		int				  weight_sliced_pddt_max_weight = -1;	 // -1 = auto from budget; otherwise 0..31
	};

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
		UsedTargetBestWeightShortcut = 10
	};

	static inline const char* strict_certification_failure_reason_to_string( StrictCertificationFailureReason reason ) noexcept
	{
		switch ( reason )
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
		default:
			return "unknown";
		}
	}

	static inline bool strict_certification_failure_reason_is_hard_limit( StrictCertificationFailureReason reason ) noexcept
	{
		switch ( reason )
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

	static inline const char* best_weight_certification_status_to_string( BestWeightCertificationStatus status ) noexcept
	{
		switch ( status )
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

	struct MatsuiSearchRunDifferentialResult
	{
		bool									 found = false;
		int										 best_weight = INFINITE_WEIGHT;
		std::uint64_t							 nodes_visited = 0;
		bool									 hit_maximum_search_nodes = false;
		bool									 hit_time_limit = false;
		std::vector<DifferentialTrailStepRecord> best_trail;
		bool									 used_non_strict_branch_cap = false;
		bool									 used_target_best_weight_shortcut = false;
		bool									 exhaustive_completed = false;
		bool									 best_weight_certified = false;
		StrictCertificationFailureReason		 strict_rejection_reason = StrictCertificationFailureReason::None;
	};

	static inline BestWeightCertificationStatus best_weight_certification_status(
		bool exact_best_weight_certified,
		StrictCertificationFailureReason strict_rejection_reason ) noexcept
	{
		if ( exact_best_weight_certified )
			return BestWeightCertificationStatus::ExactBestCertified;
		if ( strict_rejection_reason == StrictCertificationFailureReason::UsedTargetBestWeightShortcut )
			return BestWeightCertificationStatus::ThresholdTargetCertified;
		return BestWeightCertificationStatus::None;
	}

	static inline BestWeightCertificationStatus best_weight_certification_status( const MatsuiSearchRunDifferentialResult& result ) noexcept
	{
		return best_weight_certification_status( result.best_weight_certified, result.strict_rejection_reason );
	}

	struct DifferentialHullCollectedTrailView
	{
		int total_weight = 0;
		std::uint32_t output_branch_a_difference = 0;
		std::uint32_t output_branch_b_difference = 0;
		long double exact_probability = 0.0L;
		const std::vector<DifferentialTrailStepRecord>* trail = nullptr;
	};

	using DifferentialHullTrailCallback = std::function<bool( const DifferentialHullCollectedTrailView& )>;

	struct DifferentialHullShellSummary
	{
		std::uint64_t trail_count = 0;
		long double aggregate_probability = 0.0L;
		long double strongest_trail_probability = 0.0L;
		std::uint32_t strongest_output_branch_a_difference = 0;
		std::uint32_t strongest_output_branch_b_difference = 0;
		std::vector<DifferentialTrailStepRecord> strongest_trail {};
	};

	struct DifferentialHullAggregationResult
	{
		bool found_any = false;
		std::uint64_t nodes_visited = 0;
		std::uint64_t collected_trail_count = 0;
		bool hit_maximum_search_nodes = false;
		bool hit_time_limit = false;
		bool hit_collection_limit = false;
		bool hit_callback_stop = false;
		bool used_non_strict_branch_cap = false;
		bool exact_within_collect_weight_cap = false;
		bool best_weight_certified = false;
		StrictCertificationFailureReason exactness_rejection_reason = StrictCertificationFailureReason::None;
		int collect_weight_cap = 0;
		long double aggregate_probability = 0.0L;
		long double strongest_trail_probability = 0.0L;
		std::map<int, DifferentialHullShellSummary> shell_summaries {};
		std::vector<DifferentialTrailStepRecord> strongest_trail {};
	};

	enum class HullCollectCapMode : std::uint8_t
	{
		BestWeightPlusWindow = 0,
		ExplicitCap = 1
	};

	struct BestWeightHistory;
	struct BinaryCheckpointManager;

	struct DifferentialHullCollectionOptions
	{
		int collect_weight_cap = 0;
		std::uint64_t maximum_collected_trails = 0;
		DifferentialBestSearchRuntimeControls runtime_controls {};
		DifferentialHullTrailCallback on_trail {};
	};

	struct DifferentialStrictHullRuntimeOptions
	{
		HullCollectCapMode collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
		int explicit_collect_weight_cap = 0;
		int collect_weight_window = 0;
		std::uint64_t maximum_collected_trails = 0;
		DifferentialBestSearchRuntimeControls runtime_controls {};
		DifferentialHullTrailCallback on_trail {};
		bool best_search_seed_present = false;
		int best_search_seeded_upper_bound_weight = INFINITE_WEIGHT;
		std::vector<DifferentialTrailStepRecord> best_search_seeded_upper_bound_trail {};
		std::string best_search_resume_checkpoint_path {};
		BestWeightHistory* best_search_history_log = nullptr;
		struct BinaryCheckpointManager* best_search_binary_checkpoint = nullptr;
		RuntimeEventLog* best_search_runtime_log = nullptr;
	};

	struct DifferentialStrictHullRuntimeResult
	{
		bool best_search_executed = false;
		bool used_best_weight_reference = false;
		bool collected = false;
		int collect_weight_cap = 0;
		MatsuiSearchRunDifferentialResult best_search_result {};
		DifferentialHullAggregationResult aggregation_result {};
		StrictCertificationFailureReason strict_runtime_rejection_reason = StrictCertificationFailureReason::None;
	};

	struct DifferentialBestSearchContext
	{
		DifferentialBestSearchConfiguration configuration;
		DifferentialBestSearchRuntimeControls runtime_controls {};
		RuntimeInvocationState runtime_state {};
		SearchInvocationMetadata invocation_metadata {};
		std::uint32_t			start_difference_a = 0;
		std::uint32_t			start_difference_b = 0;

		std::uint64_t							 visited_node_count = 0;
		std::uint64_t							 run_visited_node_count = 0;
		int										 best_total_weight = INFINITE_WEIGHT;
		std::vector<DifferentialTrailStepRecord> best_differential_trail;
		std::vector<DifferentialTrailStepRecord> current_differential_trail;

		// Best-effort memoization: prune revisits at round boundaries.
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int> memoization;

		// Run start time (used for progress/checkpoints even when maximum_search_seconds==0).
		std::chrono::steady_clock::time_point run_start_time {};
		std::uint64_t						  accumulated_elapsed_usec = 0;

		// Single-run progress reporting (optional; batch has its own progress monitor).
		// Printed only when enabled by CLI: --progress-every-seconds S (S>0).
		std::uint64_t						  progress_every_seconds = 0;				// 0 = disable
		std::uint64_t						  progress_node_mask = ( 1ull << 18 ) - 1;	// check clock every ~262k nodes to reduce overhead
		std::chrono::steady_clock::time_point progress_start_time {};
		std::chrono::steady_clock::time_point progress_last_print_time {};
		std::uint64_t						  progress_last_print_nodes = 0;
		bool								  progress_print_differences = false;  // if enabled, print (dA,dB) snapshots on progress lines (useful for long/unbounded runs)

		// Time limit (single-run). Implemented as a global stop flag to avoid deep unwinding costs.
		std::chrono::steady_clock::time_point time_limit_start_time {};
		bool								  stop_due_to_time_limit = false;

		// Optional: checkpoint writer (e.g., auto mode) to persist best-weight changes.
		BestWeightHistory* checkpoint = nullptr;

		// Optional: binary checkpoint manager for resumable DFS.
		BinaryCheckpointManager* binary_checkpoint = nullptr;
		RuntimeEventLog* runtime_event_log = nullptr;
		std::string		 history_log_output_path {};
		std::string		 runtime_log_output_path {};
	};

	static inline bool differential_configuration_has_strict_branch_cap( const DifferentialBestSearchConfiguration& configuration ) noexcept
	{
		return configuration.maximum_transition_output_differences != 0;
	}

	static inline bool differential_configuration_uses_non_strict_remaining_round_bound( const DifferentialBestSearchConfiguration& configuration ) noexcept
	{
		return configuration.enable_remaining_round_lower_bound && !configuration.strict_remaining_round_lower_bound;
	}

	static inline bool differential_configs_equal( const DifferentialBestSearchConfiguration& a, const DifferentialBestSearchConfiguration& b )
	{
		return a.round_count == b.round_count &&
			a.addition_weight_cap == b.addition_weight_cap &&
			a.constant_subtraction_weight_cap == b.constant_subtraction_weight_cap &&
			a.maximum_transition_output_differences == b.maximum_transition_output_differences &&
			a.target_best_weight == b.target_best_weight &&
			a.enable_state_memoization == b.enable_state_memoization &&
			a.enable_verbose_output == b.enable_verbose_output &&
			a.enable_remaining_round_lower_bound == b.enable_remaining_round_lower_bound &&
			a.remaining_round_min_weight == b.remaining_round_min_weight &&
			a.strict_remaining_round_lower_bound == b.strict_remaining_round_lower_bound &&
			a.enable_weight_sliced_pddt == b.enable_weight_sliced_pddt &&
			a.weight_sliced_pddt_max_weight == b.weight_sliced_pddt_max_weight;
	}

	static inline void normalize_differential_config_for_compare( DifferentialBestSearchConfiguration& configuration )
	{
		configuration.addition_weight_cap = std::clamp( configuration.addition_weight_cap, 0, 31 );
		configuration.constant_subtraction_weight_cap = std::clamp( configuration.constant_subtraction_weight_cap, 0, 32 );
		if ( configuration.enable_remaining_round_lower_bound )
		{
			auto& remaining_round_min_weight_table = configuration.remaining_round_min_weight;
			if ( remaining_round_min_weight_table.empty() )
			{
				remaining_round_min_weight_table.assign( std::size_t( std::max( 0, configuration.round_count ) ) + 1u, 0 );
			}
			else
			{
				remaining_round_min_weight_table[ 0 ] = 0;
				const std::size_t need = std::size_t( std::max( 0, configuration.round_count ) ) + 1u;
				if ( remaining_round_min_weight_table.size() < need )
					remaining_round_min_weight_table.resize( need, 0 );
				for ( int& round_min_weight : remaining_round_min_weight_table )
				{
					if ( round_min_weight < 0 )
						round_min_weight = 0;
				}
			}
		}
	}

	static inline bool differential_configs_compatible_for_resume( DifferentialBestSearchConfiguration a, DifferentialBestSearchConfiguration b )
	{
		normalize_differential_config_for_compare( a );
		normalize_differential_config_for_compare( b );
		return differential_configs_equal( a, b );
	}

	static inline std::size_t differential_runtime_memo_reserve_hint( const DifferentialBestSearchContext& context ) noexcept
	{
		const std::uint64_t effective_limit = runtime_effective_maximum_search_nodes( context.runtime_controls );
		const std::size_t hint =
			( effective_limit == 0 ) ?
				std::size_t( 0 ) :
				static_cast<std::size_t>( std::min<std::uint64_t>( effective_limit, std::uint64_t( std::numeric_limits<std::size_t>::max() ) ) );
		return compute_initial_memo_reserve_hint( hint );
	}

	static inline void sync_differential_runtime_state( DifferentialBestSearchContext& context ) noexcept
	{
		context.visited_node_count = context.runtime_state.total_nodes_visited;
		context.run_visited_node_count = context.runtime_state.run_nodes_visited;
		context.run_start_time = context.runtime_state.run_start_time;
		context.progress_node_mask = context.runtime_state.progress_node_mask;
		context.stop_due_to_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
		context.configuration.maximum_search_nodes = runtime_effective_maximum_search_nodes( context.runtime_controls );
		context.configuration.maximum_search_seconds = context.runtime_controls.maximum_search_seconds;
	}

	static inline void begin_differential_runtime_invocation( DifferentialBestSearchContext& context ) noexcept
	{
		context.runtime_state.total_nodes_visited = context.visited_node_count;
		begin_runtime_invocation( context.runtime_controls, context.runtime_state );
		context.progress_every_seconds = context.runtime_controls.progress_every_seconds;
		context.progress_start_time = context.runtime_state.run_start_time;
		context.progress_last_print_time = {};
		context.progress_last_print_nodes = 0;
		sync_differential_runtime_state( context );
	}

	static inline bool differential_note_runtime_node_visit( DifferentialBestSearchContext& context ) noexcept
	{
		const bool stop = runtime_note_node_visit( context.runtime_controls, context.runtime_state );
		sync_differential_runtime_state( context );
		return stop;
	}

	static inline bool differential_runtime_node_limit_hit( DifferentialBestSearchContext& context ) noexcept
	{
		const bool hit = runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
		sync_differential_runtime_state( context );
		return hit;
	}

	static inline bool differential_runtime_node_limit_hit( const DifferentialBestSearchContext& context ) noexcept
	{
		return runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
	}

	static inline bool differential_runtime_budget_hit( DifferentialBestSearchContext& context ) noexcept
	{
		return runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state ) ||
			runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
	}

	static inline void differential_runtime_log_basic_event(
		const DifferentialBestSearchContext& context,
		const char* event_name,
		const char* reason = "running" )
	{
		if ( !context.runtime_event_log )
			return;
		context.runtime_event_log->write_event(
			event_name,
			[&]( std::ostream& out ) {
				out << "round_count=" << context.configuration.round_count << "\n";
				out << "start_difference_branch_a=" << hex8( context.start_difference_a ) << "\n";
				out << "start_difference_branch_b=" << hex8( context.start_difference_b ) << "\n";
				out << "best_weight=" << ( ( context.best_total_weight >= INFINITE_WEIGHT ) ? -1 : context.best_total_weight ) << "\n";
				out << "run_nodes_visited=" << context.run_visited_node_count << "\n";
				out << "total_nodes_visited=" << context.visited_node_count << "\n";
				out << "elapsed_seconds=" << std::fixed << std::setprecision( 3 ) << TwilightDream::best_search_shared_core::accumulated_elapsed_seconds( context ) << "\n";
				out << "runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "\n";
				out << "runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "\n";
				out << "runtime_progress_every_seconds=" << context.runtime_controls.progress_every_seconds << "\n";
				out << "runtime_checkpoint_every_seconds=" << context.runtime_controls.checkpoint_every_seconds << "\n";
				out << "runtime_progress_node_mask=" << context.runtime_state.progress_node_mask << "\n";
				out << "runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() ) << "\n";
				out << "runtime_budget_mode=" << runtime_budget_mode_name( context.runtime_controls ) << "\n";
				out << "maxnodes_ignored_due_to_time_limit=" << ( runtime_nodes_ignored_due_to_time_limit( context.runtime_controls ) ? 1 : 0 ) << "\n";
				out << "stop_reason=" << ( reason ? reason : "running" ) << "\n";
				out << "physical_available_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.physical_available_bytes ) << "\n";
				out << "estimated_must_live_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.estimated_must_live_bytes ) << "\n";
				out << "estimated_optional_rebuildable_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.estimated_optional_rebuildable_bytes ) << "\n";
				out << "memory_gate=" << memory_gate_status_name( context.invocation_metadata.memory_gate_status ) << "\n";
				out << "startup_memory_gate_policy=" << ( context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
				out << "reason=" << ( reason ? reason : "running" ) << "\n";
			} );
	}

	static inline StrictCertificationFailureReason classify_differential_best_search_strict_rejection_reason(
		const MatsuiSearchRunDifferentialResult& result,
		const DifferentialBestSearchConfiguration& configuration ) noexcept
	{
		if ( result.strict_rejection_reason != StrictCertificationFailureReason::None )
			return result.strict_rejection_reason;
		if ( differential_configuration_has_strict_branch_cap( configuration ) )
			return StrictCertificationFailureReason::UsedBranchCap;
		if ( differential_configuration_uses_non_strict_remaining_round_bound( configuration ) )
			return StrictCertificationFailureReason::UsedNonStrictRemainingBound;
		if ( result.hit_maximum_search_nodes )
			return StrictCertificationFailureReason::HitMaximumSearchNodes;
		if ( result.hit_time_limit )
			return StrictCertificationFailureReason::HitTimeLimit;
		if ( result.used_target_best_weight_shortcut )
			return StrictCertificationFailureReason::UsedTargetBestWeightShortcut;
		return StrictCertificationFailureReason::None;
	}

	static inline StrictCertificationFailureReason classify_differential_collection_exactness_reason(
		const DifferentialHullAggregationResult& aggregation_result ) noexcept
	{
		if ( aggregation_result.exactness_rejection_reason != StrictCertificationFailureReason::None )
			return aggregation_result.exactness_rejection_reason;
		if ( aggregation_result.hit_maximum_search_nodes )
			return StrictCertificationFailureReason::HitMaximumSearchNodes;
		if ( aggregation_result.hit_time_limit )
			return StrictCertificationFailureReason::HitTimeLimit;
		if ( aggregation_result.hit_collection_limit )
			return StrictCertificationFailureReason::HitCollectionLimit;
		if ( aggregation_result.hit_callback_stop )
			return StrictCertificationFailureReason::HitCallbackStop;
		if ( aggregation_result.used_non_strict_branch_cap )
			return StrictCertificationFailureReason::UsedBranchCap;
		return StrictCertificationFailureReason::None;
	}

	static inline long double exact_differential_step_probability( const DifferentialTrailStepRecord& step ) noexcept
	{
		return std::pow( 2.0L, -static_cast<long double>( step.round_weight ) );
	}

	static inline void maybe_print_single_run_progress( DifferentialBestSearchContext& context, int round_boundary_depth_hint )
	{
		if ( context.progress_every_seconds == 0 )
			return;
		if ( ( context.visited_node_count & context.progress_node_mask ) != 0 )
			return;

		const auto now = std::chrono::steady_clock::now();
		memory_governor_poll_if_needed( now );
		if ( context.progress_last_print_time.time_since_epoch().count() != 0 )
		{
			const double since_last = std::chrono::duration<double>( now - context.progress_last_print_time ).count();
			if ( since_last < double( context.progress_every_seconds ) )
				return;
		}

		const double		elapsed = std::chrono::duration<double>( now - context.progress_start_time ).count();
		const double		window = ( context.progress_last_print_time.time_since_epoch().count() == 0 ) ? elapsed : std::chrono::duration<double>( now - context.progress_last_print_time ).count();
		const std::uint64_t delta_nodes = ( context.visited_node_count >= context.progress_last_print_nodes ) ? ( context.visited_node_count - context.progress_last_print_nodes ) : 0;
		const double		rate = ( window > 1e-9 ) ? ( double( delta_nodes ) / window ) : 0.0;

		std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );

		// Save/restore formatting (avoid perturbing later prints).
		const auto old_flags = std::cout.flags();
		const auto old_prec = std::cout.precision();
		const auto old_fill = std::cout.fill();

		TwilightDream::runtime_component::print_progress_prefix( std::cout );
		std::cout << "[Progress] visited_node_count=" << context.visited_node_count
				  << "  visited_nodes_per_second=" << std::fixed << std::setprecision( 2 ) << rate
				  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed
				  << "  best_total_weight=" << ( ( context.best_total_weight >= INFINITE_WEIGHT ) ? -1 : context.best_total_weight );
		if ( round_boundary_depth_hint >= 0 )
		{
			std::cout << "  current_depth=" << round_boundary_depth_hint << "  total_rounds=" << context.configuration.round_count;
		}
		if ( context.progress_print_differences )
		{
			std::cout << "  ";
			print_word32_hex( "start_difference_branch_a=", context.start_difference_a );
			std::cout << " ";
			print_word32_hex( "start_difference_branch_b=", context.start_difference_b );
			if ( !context.best_differential_trail.empty() )
			{
				const auto& r1 = context.best_differential_trail.front();
				const auto& rn = context.best_differential_trail.back();
				std::cout << " ";
				print_word32_hex( "best_trail_round1_output_difference_branch_a=", r1.output_branch_a_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_round1_output_difference_branch_b=", r1.output_branch_b_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_output_difference_branch_a=", rn.output_branch_a_difference );
				std::cout << " ";
				print_word32_hex( "best_trail_output_difference_branch_b=", rn.output_branch_b_difference );
			}
		}
		std::cout << "\n";

		std::cout.flags( old_flags );
		std::cout.precision( old_prec );
		std::cout.fill( old_fill );

		context.progress_last_print_time = now;
		context.progress_last_print_nodes = context.visited_node_count;
	}

	// Per-round shared state for nested enumeration (moved out for checkpointing).
	struct DifferentialRoundSearchState
	{
		int round_boundary_depth = 0;
		int accumulated_weight_so_far = 0;
		int remaining_round_weight_lower_bound_after_this_round = 0;

		std::uint32_t branch_a_input_difference = 0;
		std::uint32_t branch_b_input_difference = 0;

		DifferentialTrailStepRecord base_step {};

		std::uint32_t first_addition_term_difference = 0;
		std::uint32_t optimal_output_branch_b_difference_after_first_addition = 0;
		int			  optimal_weight_first_addition = 0;
		int			  weight_cap_first_addition = 0;

		std::uint32_t output_branch_b_difference_after_first_addition = 0;
		int			  weight_first_addition = 0;
		int			  accumulated_weight_after_first_addition = 0;

		std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
		int			  weight_first_constant_subtraction = 0;
		int			  accumulated_weight_after_first_constant_subtraction = 0;

		std::uint32_t branch_a_difference_after_first_xor_with_rotated_branch_b = 0;
		std::uint32_t branch_b_difference_after_first_xor_with_rotated_branch_a = 0;
		// Same value as branch_b_difference_after_first_xor_with_rotated_branch_a (checkpoint field order preserved).
		std::uint32_t branch_b_difference_after_first_bridge = 0;

		int			  weight_injection_from_branch_b = 0;
		int			  accumulated_weight_before_second_addition = 0;
		std::uint32_t injection_from_branch_b_xor_difference = 0;
		std::uint32_t branch_a_difference_after_injection_from_branch_b = 0;

		std::uint32_t second_addition_term_difference = 0;
		std::uint32_t optimal_output_branch_a_difference_after_second_addition = 0;
		int			  optimal_weight_second_addition = 0;
		int			  weight_cap_second_addition = 0;

		std::uint32_t output_branch_a_difference_after_second_addition = 0;
		int			  weight_second_addition = 0;
		int			  accumulated_weight_after_second_addition = 0;

		std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
		int			  weight_second_constant_subtraction = 0;
		int			  accumulated_weight_after_second_constant_subtraction = 0;

		std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;
		std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;

		int			  weight_injection_from_branch_a = 0;
		int			  accumulated_weight_at_round_end = 0;
		std::uint32_t injection_from_branch_a_xor_difference = 0;

		std::uint32_t output_branch_a_difference = 0;
		std::uint32_t output_branch_b_difference = 0;
	};

	// ============================================================================
	// Resumable enumerators (preserve traversal order)
	// ============================================================================

	struct ModularAdditionEnumerator
	{
		struct Frame
		{
			int			  bit_position = 0;
			std::uint32_t prefix = 0;
			std::uint32_t prefer = 0;
			std::uint8_t  state = 0;  // 0=enter, 1=after prefer-branch, 2=done
		};

		static constexpr int MAX_STACK = 33;

		bool initialized = false;
		bool stop_due_to_limits = false;
		bool dfs_active = false;
		bool using_cached_shell = false;
		std::uint32_t alpha = 0;
		std::uint32_t beta = 0;
		std::uint32_t output_hint = 0;
		int			  weight_cap = 0;
		int			  target_weight = 0;
		int			  word_bits = 32;
		std::size_t	  shell_index = 0;
		std::vector<std::uint32_t> shell_cache {};
		int			  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( std::uint32_t a, std::uint32_t b, std::uint32_t hint, int cap, int bit_position = 0, std::uint32_t prefix = 0, int bits = 32 );

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_gamma, int& out_weight );
	};

	struct SubConstEnumerator
	{
		struct Frame
		{
			int							 bit_position = 0;
			std::uint32_t				 prefix = 0;
			std::array<std::uint64_t, 4> prefix_counts {};
			std::uint32_t				 preferred_bit = 0;
			std::uint8_t				 state = 0;  // 0=enter, 1=after preferred-branch, 2=done
		};

		static constexpr int MAX_STACK = 33;
		static constexpr int SLACK = 8;

		bool initialized = false;
		bool stop_due_to_limits = false;
		std::uint32_t input_difference = 0;
		std::uint32_t subtractive_constant = 0;
		std::uint32_t additive_constant = 0;
		std::uint32_t output_hint = 0;
		int						  cap_bitvector = 0;
		int						  cap_dynamic_planning = 0;
		int						  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( std::uint32_t dx, std::uint32_t sub_const, int bvweight_cap );

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_difference, int& out_weight );
	};

	struct AffineSubspaceEnumerator
	{
		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_difference = 0;
			std::uint8_t  state = 0;  // 0=enter, 1=after branch0
		};

		static constexpr int MAX_STACK = 33;

		bool initialized = false;
		bool stop_due_to_limits = false;
		InjectionAffineTransition transition {};
		std::size_t maximum_output_difference_count = 0;
		std::size_t produced_output_difference_count = 0;
		int		  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( const InjectionAffineTransition& t, std::size_t max_outputs );

		bool next( DifferentialBestSearchContext& context, std::uint32_t& out_difference );
	};

	enum class DifferentialSearchStage : std::uint8_t
	{
		Enter = 0,
		FirstAdd = 1,
		FirstConst = 2,
		InjB = 3,
		SecondAdd = 4,
		SecondConst = 5,
		InjA = 6
	};

	struct DifferentialSearchFrame
	{
		DifferentialSearchStage stage = DifferentialSearchStage::Enter;
		std::size_t			   trail_size_at_entry = 0;
		DifferentialRoundSearchState state {};
		ModularAdditionEnumerator	  enum_first_add {};
		SubConstEnumerator			  enum_first_const {};
		AffineSubspaceEnumerator	  enum_inj_b {};
		ModularAdditionEnumerator	  enum_second_add {};
		SubConstEnumerator			  enum_second_const {};
		AffineSubspaceEnumerator	  enum_inj_a {};
	};

	struct DifferentialSearchCursor
	{
		std::vector<DifferentialSearchFrame> stack;
	};

	MatsuiSearchRunDifferentialResult run_differential_best_search(
		int round_count,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialBestSearchRuntimeControls& runtime_controls = {},
		bool print_output = false,
		bool progress_print_differences = false,
		int seeded_upper_bound_weight = INFINITE_WEIGHT,
		const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail = nullptr,
		BestWeightHistory* checkpoint = nullptr,
		BinaryCheckpointManager* binary_checkpoint = nullptr,
		RuntimeEventLog* runtime_event_log = nullptr,
		const SearchInvocationMetadata* invocation_metadata = nullptr );

	inline MatsuiSearchRunDifferentialResult run_matsui_best_search_with_injection_internal(
		int round_count,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialBestSearchRuntimeControls& runtime_controls = {},
		bool print_output = false,
		bool progress_print_differences = false,
		int seeded_upper_bound_weight = INFINITE_WEIGHT,
		const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail = nullptr,
		BestWeightHistory* checkpoint = nullptr,
		BinaryCheckpointManager* binary_checkpoint = nullptr,
		RuntimeEventLog* runtime_event_log = nullptr,
		const SearchInvocationMetadata* invocation_metadata = nullptr )
	{
		return run_differential_best_search(
			round_count,
			initial_branch_a_difference,
			initial_branch_b_difference,
			input_search_configuration,
			runtime_controls,
			print_output,
			progress_print_differences,
			seeded_upper_bound_weight,
			seeded_upper_bound_trail,
			checkpoint,
			binary_checkpoint,
			runtime_event_log,
			invocation_metadata );
	}

	MatsuiSearchRunDifferentialResult run_differential_best_search_resume(
		const std::string& checkpoint_path,
		std::uint32_t expected_start_difference_a,
		std::uint32_t expected_start_difference_b,
		const DifferentialBestSearchConfiguration& expected_configuration,
		const DifferentialBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_differences,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata,
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask* runtime_override_mask_opt = nullptr,
		const DifferentialBestSearchConfiguration* execution_configuration_override = nullptr,
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt = nullptr );

	void continue_differential_best_search_from_cursor( DifferentialBestSearchContext& context, DifferentialSearchCursor& cursor );

	DifferentialHullAggregationResult collect_differential_hull_exact(
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialHullCollectionOptions& options = {} );

	DifferentialStrictHullRuntimeResult run_differential_strict_hull_runtime(
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialStrictHullRuntimeOptions& options = {},
		bool print_output = false,
		bool progress_print_differences = false );

}  // namespace TwilightDream::auto_search_differential
