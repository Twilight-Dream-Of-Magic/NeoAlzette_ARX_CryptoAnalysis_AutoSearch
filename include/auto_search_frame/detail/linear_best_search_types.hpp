#pragma once

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
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

namespace TwilightDream::auto_search_linear
{
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

	struct LinearTrailStepRecord
	{
		int round_index = 0;

		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;

		std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;
		std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;
		int			  weight_second_constant_subtraction = 0;

		std::uint32_t output_branch_a_mask_after_second_addition = 0;
		std::uint32_t input_branch_a_mask_before_second_addition = 0;
		std::uint32_t second_addition_term_mask_from_branch_b = 0;
		int			  weight_second_addition = 0;

		int weight_injection_from_branch_a = 0;
		int weight_injection_from_branch_b = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;
		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;

		std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;
		std::uint32_t input_branch_a_mask_before_first_constant_subtraction = 0;
		int			  weight_first_constant_subtraction = 0;

		std::uint32_t output_branch_b_mask_after_first_addition = 0;
		std::uint32_t input_branch_b_mask_before_first_addition = 0;
		std::uint32_t first_addition_term_mask_from_branch_a = 0;
		int			  weight_first_addition = 0;

		int round_weight = 0;
	};

	static inline long double exact_linear_step_correlation( const LinearTrailStepRecord& step ) noexcept
	{
		return std::pow( 2.0L, -static_cast<long double>( step.round_weight ) );
	}

	struct LinearBestSearchConfiguration
	{
		SearchMode   search_mode = SearchMode::Strict;
		int			  round_count = 1;
		int			  addition_weight_cap = 31;
		int			  constant_subtraction_weight_cap = 32;
		bool		  enable_weight_sliced_clat = false;
		std::size_t	  weight_sliced_clat_max_candidates = 0;
		std::size_t	  maximum_injection_input_masks = 0;
		std::size_t	  maximum_round_predecessors = 0;
		std::uint64_t maximum_search_nodes = 0;
		int			  target_best_weight = -1;
		std::uint64_t maximum_search_seconds = 0;
		bool		  enable_state_memoization = true;
		bool		  enable_remaining_round_lower_bound = false;
		std::vector<int> remaining_round_min_weight {};
		bool		  auto_generate_remaining_round_lower_bound = false;
		std::uint64_t remaining_round_lower_bound_generation_nodes = 0;
		std::uint64_t remaining_round_lower_bound_generation_seconds = 0;
		bool		  strict_remaining_round_lower_bound = true;
		bool		  enable_verbose_output = false;
	};

	struct LinearHullCollectedTrailView
	{
		int			  total_weight = 0;
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;
		long double	  exact_signed_correlation = 0.0L;
		const std::vector<LinearTrailStepRecord>* trail = nullptr;
	};

	using LinearHullTrailCallback = std::function<bool( const LinearHullCollectedTrailView& )>;

	struct LinearHullShellSummary
	{
		std::uint64_t trail_count = 0;
		long double	  aggregate_signed_correlation = 0.0L;
		long double	  aggregate_abs_correlation_mass = 0.0L;
		long double	  strongest_trail_signed_correlation = 0.0L;
		std::uint32_t strongest_input_branch_a_mask = 0;
		std::uint32_t strongest_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> strongest_trail {};
	};

	struct LinearHullAggregationResult
	{
		bool		  found_any = false;
		std::uint64_t nodes_visited = 0;
		std::uint64_t collected_trail_count = 0;
		bool		  hit_maximum_search_nodes = false;
		bool		  hit_time_limit = false;
		bool		  hit_collection_limit = false;
		bool		  hit_callback_stop = false;
		bool		  used_non_strict_branch_cap = false;
		bool		  exact_within_collect_weight_cap = false;
		bool		  best_weight_certified = false;
		StrictCertificationFailureReason exactness_rejection_reason = StrictCertificationFailureReason::None;
		int			  collect_weight_cap = 0;
		long double	  aggregate_signed_correlation = 0.0L;
		long double	  aggregate_abs_correlation_mass = 0.0L;
		long double	  strongest_trail_signed_correlation = 0.0L;
		std::uint32_t strongest_input_branch_a_mask = 0;
		std::uint32_t strongest_input_branch_b_mask = 0;
		std::map<int, LinearHullShellSummary> shell_summaries {};
		std::vector<LinearTrailStepRecord> strongest_trail {};
	};

	struct MatsuiSearchRunLinearResult
	{
		bool							   found = false;
		int								   best_weight = INFINITE_WEIGHT;
		std::uint64_t					   nodes_visited = 0;
		bool							   hit_maximum_search_nodes = false;
		bool							   hit_time_limit = false;
		bool							   hit_target_best_weight = false;
		std::uint32_t					   best_input_branch_a_mask = 0;
		std::uint32_t					   best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail {};
		bool							   used_non_strict_branch_cap = false;
		bool							   used_target_best_weight_shortcut = false;
		bool							   exhaustive_completed = false;
		bool							   best_weight_certified = false;
		StrictCertificationFailureReason   strict_rejection_reason = StrictCertificationFailureReason::None;
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

	static inline BestWeightCertificationStatus best_weight_certification_status( const MatsuiSearchRunLinearResult& result ) noexcept
	{
		return best_weight_certification_status( result.best_weight_certified, result.strict_rejection_reason );
	}

	enum class HullCollectCapMode : std::uint8_t
	{
		BestWeightPlusWindow = 0,
		ExplicitCap = 1
	};

	struct BestWeightHistory;
	struct BinaryCheckpointManager;

	struct LinearHullCollectionOptions
	{
		int			  collect_weight_cap = 0;
		std::uint64_t maximum_collected_trails = 0;
		LinearBestSearchRuntimeControls runtime_controls {};
		LinearHullTrailCallback on_trail {};
	};

	struct LinearStrictHullRuntimeOptions
	{
		HullCollectCapMode collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
		int			  explicit_collect_weight_cap = 0;
		int			  collect_weight_window = 0;
		std::uint64_t maximum_collected_trails = 0;
		LinearBestSearchRuntimeControls runtime_controls {};
		LinearHullTrailCallback on_trail {};
		bool		  best_search_seed_present = false;
		int			  best_search_seeded_upper_bound_weight = INFINITE_WEIGHT;
		std::vector<LinearTrailStepRecord> best_search_seeded_upper_bound_trail {};
		std::string	  best_search_resume_checkpoint_path {};
		BestWeightHistory* best_search_history_log = nullptr;
		BinaryCheckpointManager* best_search_binary_checkpoint = nullptr;
		RuntimeEventLog* best_search_runtime_log = nullptr;
	};

	struct LinearStrictHullRuntimeResult
	{
		bool		  best_search_executed = false;
		bool		  used_best_weight_reference = false;
		bool		  collected = false;
		int			  collect_weight_cap = 0;
		MatsuiSearchRunLinearResult best_search_result {};
		LinearHullAggregationResult aggregation_result {};
		StrictCertificationFailureReason strict_runtime_rejection_reason = StrictCertificationFailureReason::None;
	};

	struct LinearBestSearchContext
	{
		LinearBestSearchConfiguration configuration;
		LinearBestSearchRuntimeControls runtime_controls {};
		RuntimeInvocationState runtime_state {};
		SearchInvocationMetadata invocation_metadata {};

		std::uint32_t start_output_branch_a_mask = 0;
		std::uint32_t start_output_branch_b_mask = 0;

		std::uint64_t visited_node_count = 0;
		std::uint64_t run_visited_node_count = 0;
		int			  best_weight = INFINITE_WEIGHT;
		std::uint32_t best_input_branch_a_mask = 0;
		std::uint32_t best_input_branch_b_mask = 0;
		std::vector<LinearTrailStepRecord> best_linear_trail;
		std::vector<LinearTrailStepRecord> current_linear_trail;

		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int> memoization;

		std::chrono::steady_clock::time_point run_start_time {};
		std::uint64_t						  accumulated_elapsed_usec = 0;

		std::uint64_t						  progress_every_seconds = 0;
		std::uint64_t						  progress_node_mask = ( 1ull << 18 ) - 1;
		std::chrono::steady_clock::time_point progress_start_time {};
		std::chrono::steady_clock::time_point progress_last_print_time {};
		std::uint64_t						  progress_last_print_nodes = 0;
		bool								  progress_print_masks = false;

		std::chrono::steady_clock::time_point time_limit_start_time {};
		bool								  stop_due_to_time_limit = false;
		bool								  stop_due_to_target = false;

		BestWeightHistory* checkpoint = nullptr;
		BinaryCheckpointManager* binary_checkpoint = nullptr;
		RuntimeEventLog* runtime_event_log = nullptr;
		std::string		 history_log_output_path {};
		std::string		 runtime_log_output_path {};
	};

	static inline bool linear_configuration_has_strict_branch_cap( const LinearBestSearchConfiguration& configuration ) noexcept
	{
		return configuration.maximum_injection_input_masks != 0 ||
			configuration.maximum_round_predecessors != 0 ||
			( configuration.enable_weight_sliced_clat && configuration.weight_sliced_clat_max_candidates != 0 );
	}

	static inline bool linear_configuration_uses_non_strict_remaining_round_bound( const LinearBestSearchConfiguration& configuration ) noexcept
	{
		return configuration.enable_remaining_round_lower_bound && !configuration.strict_remaining_round_lower_bound;
	}

	static inline bool linear_configuration_uses_non_strict_search_mode( const LinearBestSearchConfiguration& configuration ) noexcept
	{
		return configuration.search_mode != SearchMode::Strict;
	}

	static inline bool linear_collector_configuration_has_non_strict_branch_cap( const LinearBestSearchConfiguration& configuration ) noexcept
	{
		return linear_configuration_has_strict_branch_cap( configuration );
	}

	static inline bool linear_configs_equal( const LinearBestSearchConfiguration& a, const LinearBestSearchConfiguration& b )
	{
		return a.search_mode == b.search_mode &&
			a.round_count == b.round_count &&
			a.addition_weight_cap == b.addition_weight_cap &&
			a.constant_subtraction_weight_cap == b.constant_subtraction_weight_cap &&
			a.enable_weight_sliced_clat == b.enable_weight_sliced_clat &&
			a.weight_sliced_clat_max_candidates == b.weight_sliced_clat_max_candidates &&
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

	static inline void normalize_linear_config_for_compare( LinearBestSearchConfiguration& configuration )
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
				if ( remaining_round_min_weight_table.empty() )
					remaining_round_min_weight_table.resize( 1u, 0 );
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

	static inline bool linear_configs_compatible_for_resume( LinearBestSearchConfiguration a, LinearBestSearchConfiguration b )
	{
		normalize_linear_config_for_compare( a );
		normalize_linear_config_for_compare( b );
		return linear_configs_equal( a, b );
	}

	static inline std::size_t linear_runtime_memo_reserve_hint( const LinearBestSearchContext& context ) noexcept
	{
		const std::uint64_t effective_limit = runtime_effective_maximum_search_nodes( context.runtime_controls );
		const std::size_t	hint =
			( effective_limit == 0 ) ?
				std::size_t( 0 ) :
				static_cast<std::size_t>( std::min<std::uint64_t>( effective_limit, std::uint64_t( std::numeric_limits<std::size_t>::max() ) ) );
		return compute_initial_memo_reserve_hint( hint );
	}

	static inline void sync_linear_runtime_legacy_fields_from_state( LinearBestSearchContext& context ) noexcept
	{
		context.visited_node_count = context.runtime_state.total_nodes_visited;
		context.run_visited_node_count = context.runtime_state.run_nodes_visited;
		context.run_start_time = context.runtime_state.run_start_time;
		context.progress_node_mask = context.runtime_state.progress_node_mask;
		context.stop_due_to_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
		context.configuration.maximum_search_nodes = runtime_effective_maximum_search_nodes( context.runtime_controls );
		context.configuration.maximum_search_seconds = context.runtime_controls.maximum_search_seconds;
	}

	static inline void begin_linear_runtime_invocation( LinearBestSearchContext& context ) noexcept
	{
		context.runtime_state.total_nodes_visited = context.visited_node_count;
		begin_runtime_invocation( context.runtime_controls, context.runtime_state );
		context.progress_every_seconds = context.runtime_controls.progress_every_seconds;
		context.progress_start_time = context.runtime_state.run_start_time;
		context.progress_last_print_time = {};
		context.progress_last_print_nodes = 0;
		sync_linear_runtime_legacy_fields_from_state( context );
	}

	static inline bool linear_note_runtime_node_visit( LinearBestSearchContext& context ) noexcept
	{
		const bool stop = runtime_note_node_visit( context.runtime_controls, context.runtime_state );
		sync_linear_runtime_legacy_fields_from_state( context );
		return stop;
	}

	static inline bool linear_runtime_node_limit_hit( LinearBestSearchContext& context ) noexcept
	{
		const bool hit = runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
		sync_linear_runtime_legacy_fields_from_state( context );
		return hit;
	}

	static inline bool linear_runtime_node_limit_hit( const LinearBestSearchContext& context ) noexcept
	{
		return runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
	}

	static inline bool linear_runtime_budget_hit( LinearBestSearchContext& context ) noexcept
	{
		return runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state ) ||
			runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
	}

	static inline void linear_runtime_log_basic_event(
		const LinearBestSearchContext& context,
		const char* event_name,
		const char* reason = "running" )
	{
		if ( !context.runtime_event_log )
			return;
		context.runtime_event_log->write_event(
			event_name,
			[&]( std::ostream& out ) {
				out << "round_count=" << context.configuration.round_count << "\n";
				out << "start_output_mask_branch_a=" << hex8( context.start_output_branch_a_mask ) << "\n";
				out << "start_output_mask_branch_b=" << hex8( context.start_output_branch_b_mask ) << "\n";
				out << "best_weight=" << ( ( context.best_weight >= INFINITE_WEIGHT ) ? -1 : context.best_weight ) << "\n";
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

	template <class OnInputMaskFn>
	static inline void enumerate_affine_subspace_input_masks( LinearBestSearchContext& context, const InjectionCorrelationTransition& transition, std::size_t maximum_input_mask_count, OnInputMaskFn&& on_input_mask )
	{
		std::size_t produced_count = 0;
		if ( transition.rank <= 0 )
		{
			if ( runtime_time_limit_reached() )
				return;
			if ( linear_runtime_node_limit_hit( context ) )
				return;
			if ( linear_note_runtime_node_visit( context ) )
				return;
			on_input_mask( transition.offset_mask );
			return;
		}

		struct Frame
		{
			int			  basis_index = 0;
			std::uint32_t current_mask = 0;
			std::uint8_t  branch_state = 0;
		};

		std::array<Frame, 64> stack {};
		int					  stack_step = 0;
		stack[ stack_step++ ] = Frame { 0, transition.offset_mask, 0 };

		while ( stack_step > 0 )
		{
			if ( runtime_time_limit_reached() )
				return;
			if ( maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count )
				return;
			if ( linear_runtime_node_limit_hit( context ) )
				return;

			Frame& frame = stack[ stack_step - 1 ];
			if ( frame.branch_state == 0 )
			{
				if ( frame.basis_index >= transition.rank )
				{
					if ( linear_runtime_node_limit_hit( context ) )
						return;
					if ( linear_note_runtime_node_visit( context ) )
						return;
					on_input_mask( frame.current_mask );
					++produced_count;
					--stack_step;
					continue;
				}

				frame.branch_state = 1;
				stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_mask, 0 };
				continue;
			}

			const std::uint32_t next_mask = frame.current_mask ^ transition.basis_vectors[ std::size_t( frame.basis_index ) ];
			const int			next_index = frame.basis_index + 1;
			--stack_step;
			stack[ stack_step++ ] = Frame { next_index, next_mask, 0 };
		}
	}

	static inline StrictCertificationFailureReason classify_linear_best_search_strict_rejection_reason(
		const MatsuiSearchRunLinearResult& result,
		const LinearBestSearchConfiguration& configuration,
		bool used_non_strict_remaining_round_bound_override ) noexcept
	{
		if ( result.strict_rejection_reason != StrictCertificationFailureReason::None )
			return result.strict_rejection_reason;
		if ( linear_configuration_uses_non_strict_search_mode( configuration ) )
			return StrictCertificationFailureReason::UsedNonStrictSearchMode;
		if ( linear_configuration_has_strict_branch_cap( configuration ) )
			return StrictCertificationFailureReason::UsedBranchCap;
		if ( used_non_strict_remaining_round_bound_override )
			return StrictCertificationFailureReason::UsedNonStrictRemainingBound;
		if ( result.hit_maximum_search_nodes )
			return StrictCertificationFailureReason::HitMaximumSearchNodes;
		if ( result.hit_time_limit )
			return StrictCertificationFailureReason::HitTimeLimit;
		if ( result.hit_target_best_weight || result.used_target_best_weight_shortcut )
			return StrictCertificationFailureReason::UsedTargetBestWeightShortcut;
		return StrictCertificationFailureReason::None;
	}

	static inline StrictCertificationFailureReason classify_linear_collection_exactness_reason(
		const LinearHullAggregationResult& aggregation_result,
		bool used_non_strict_search_mode = false ) noexcept
	{
		if ( aggregation_result.exactness_rejection_reason != StrictCertificationFailureReason::None )
			return aggregation_result.exactness_rejection_reason;
		if ( used_non_strict_search_mode )
			return StrictCertificationFailureReason::UsedNonStrictSearchMode;
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

	static inline void print_linear_weight_sliced_clat_banner( const LinearBestSearchConfiguration& c )
	{
		std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
		TwilightDream::runtime_component::print_progress_prefix( std::cout );
		std::cout << "[Linear] weight_sliced_clat=" << ( c.enable_weight_sliced_clat ? "on" : "off" )
				  << "  weight_sliced_clat_max_candidates=" << c.weight_sliced_clat_max_candidates
				  << "  allocation=on_demand_exact\n";
	}

	static inline void maybe_print_single_run_progress( LinearBestSearchContext& ctx, int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask )
	{
		if ( ctx.progress_every_seconds == 0 )
			return;
		if ( ( ctx.visited_node_count & ctx.progress_node_mask ) != 0 )
			return;

		const auto now = std::chrono::steady_clock::now();
		memory_governor_poll_if_needed( now );
		if ( ctx.progress_last_print_time.time_since_epoch().count() != 0 )
		{
			const double since_last = std::chrono::duration<double>( now - ctx.progress_last_print_time ).count();
			if ( since_last < double( ctx.progress_every_seconds ) )
				return;
		}

		const double		elapsed = std::chrono::duration<double>( now - ctx.progress_start_time ).count();
		const double		window = ( ctx.progress_last_print_time.time_since_epoch().count() == 0 ) ? elapsed : std::chrono::duration<double>( now - ctx.progress_last_print_time ).count();
		const std::uint64_t delta_nodes = ( ctx.visited_node_count >= ctx.progress_last_print_nodes ) ? ( ctx.visited_node_count - ctx.progress_last_print_nodes ) : 0;
		const double		rate = ( window > 1e-9 ) ? ( double( delta_nodes ) / window ) : 0.0;

		std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );

		const auto old_flags = std::cout.flags();
		const auto old_prec = std::cout.precision();
		const auto old_fill = std::cout.fill();

		TwilightDream::runtime_component::print_progress_prefix( std::cout );
		std::cout << "[Progress] visited_node_count=" << ctx.visited_node_count
				  << "  visited_nodes_per_second=" << std::fixed << std::setprecision( 2 ) << rate
				  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed
				  << "  best_weight=" << ( ( ctx.best_weight >= INFINITE_WEIGHT ) ? -1 : ctx.best_weight );
		std::cout << "  current_depth=" << depth << "  total_rounds=" << ctx.configuration.round_count;
		if ( ctx.progress_print_masks )
		{
			std::cout << "  ";
			print_word32_hex( "start_output_mask_branch_a=", ctx.start_output_branch_a_mask );
			std::cout << " ";
			print_word32_hex( "start_output_mask_branch_b=", ctx.start_output_branch_b_mask );
			std::cout << " ";
			print_word32_hex( "current_output_mask_branch_a=", current_round_output_branch_a_mask );
			std::cout << " ";
			print_word32_hex( "current_output_mask_branch_b=", current_round_output_branch_b_mask );
		}
		std::cout << "\n";

		std::cout.flags( old_flags );
		std::cout.precision( old_prec );
		std::cout.fill( old_fill );

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
		int			  rank = 0;
		int			  stack_step = 0;
		std::array<Frame, MAX_STACK> stack {};

		void reset( const InjectionCorrelationTransition& transition, std::size_t max_masks )
		{
			initialized = true;
			stop_due_to_limits = false;
			maximum_input_mask_count = max_masks;
			produced_count = 0;
			rank = transition.rank;
			stack_step = 0;
			stack[ stack_step++ ] = Frame { 0, transition.offset_mask, 0 };
		}

		bool next( LinearBestSearchContext& context, const InjectionCorrelationTransition& transition, std::uint32_t& out_mask )
		{
			if ( !initialized || stop_due_to_limits )
				return false;

			while ( stack_step > 0 )
			{
				if ( maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count )
					return false;

				Frame& frame = stack[ stack_step - 1 ];

				if ( frame.branch_state == 0 )
				{
					if ( linear_note_runtime_node_visit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress( context, -1, 0u, 0u );

					if ( frame.basis_index >= transition.rank )
					{
						out_mask = frame.current_mask;
						++produced_count;
						--stack_step;
						return true;
					}

					frame.branch_state = 1;
					stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_mask, 0 };
					continue;
				}

				if ( linear_runtime_node_limit_hit( context ) )
				{
					stop_due_to_limits = true;
					return false;
				}
				if ( maximum_input_mask_count != 0 && produced_count >= maximum_input_mask_count )
					return false;

				frame = Frame { frame.basis_index + 1, frame.current_mask ^ transition.basis_vectors[ std::size_t( frame.basis_index ) ], 0 };
			}

			return false;
		}
	};

	struct LinearRoundSearchState
	{
		int round_boundary_depth = 0;
		int accumulated_weight_so_far = 0;
		int round_index = 0;
		int round_weight_cap = 0;
		int remaining_round_weight_lower_bound_after_this_round = 0;

		std::uint32_t round_output_branch_a_mask = 0;
		std::uint32_t round_output_branch_b_mask = 0;

		// Round-output mask on A before enumerating correlated input masks for injection from A (backward walk).
		std::uint32_t branch_a_round_output_mask_before_inj_from_a = 0;
		std::uint32_t branch_b_mask_before_injection_from_branch_a = 0;

		InjectionCorrelationTransition injection_from_branch_a_transition {};
		LinearAffineMaskEnumerator injection_from_branch_a_enumerator {};
		int							   weight_injection_from_branch_a = 0;
		int							   remaining_after_inj_a = 0;
		int							   second_subconst_weight_cap = 0;
		int							   second_add_weight_cap = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a = 0;

		std::uint32_t output_branch_a_mask_after_second_addition = 0;
		std::uint32_t output_branch_b_mask_after_second_constant_subtraction = 0;

		SubConstStreamingCursor second_constant_subtraction_stream_cursor {};
		WeightSlicedClatStreamingCursor second_addition_weight_sliced_clat_stream_cursor {};
		std::vector<SubConstCandidate> second_constant_subtraction_candidates_for_branch_b;
		std::vector<AddCandidate> second_addition_candidates_storage;
		AddVarVarSplit8Enumerator32::StreamingCursor second_addition_stream_cursor {};
		const std::vector<AddCandidate>* second_addition_candidates_for_branch_a = nullptr;

		std::size_t second_addition_candidate_index = 0;

		std::uint32_t input_branch_a_mask_before_second_addition = 0;
		std::uint32_t second_addition_term_mask_from_branch_b = 0;
		int			  weight_second_addition = 0;
		std::uint32_t branch_b_mask_contribution_from_second_addition_term = 0;

		InjectionCorrelationTransition injection_from_branch_b_transition {};
		LinearAffineMaskEnumerator injection_from_branch_b_enumerator {};
		int							   weight_injection_from_branch_b = 0;
		int							   base_weight_after_inj_b = 0;

		std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b = 0;

		std::uint32_t input_branch_b_mask_before_second_constant_subtraction = 0;
		int			  weight_second_constant_subtraction = 0;
		// B-branch mask after removing the second var-var addition term contribution (backward unwind).
		std::uint32_t branch_b_mask_after_second_add_term_removed = 0;
		std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a_base = 0;

		std::size_t second_constant_subtraction_candidate_index = 0;

		int base_weight_after_second_subconst = 0;
		int first_subconst_weight_cap = 0;
		int first_add_weight_cap = 0;

		std::uint32_t output_branch_a_mask_after_first_constant_subtraction = 0;
		std::uint32_t output_branch_b_mask_after_first_addition = 0;

		SubConstStreamingCursor first_constant_subtraction_stream_cursor {};
		WeightSlicedClatStreamingCursor first_addition_weight_sliced_clat_stream_cursor {};
		std::vector<SubConstCandidate> first_constant_subtraction_candidates_for_branch_a;
		std::vector<AddCandidate> first_addition_candidates_for_branch_b;
		AddVarVarSplit8Enumerator32::StreamingCursor first_addition_stream_cursor {};

		std::size_t first_constant_subtraction_candidate_index = 0;
		std::size_t first_addition_candidate_index = 0;

		std::uint32_t input_branch_a_mask_before_first_constant_subtraction_current = 0;
		int			  weight_first_constant_subtraction_current = 0;

		std::vector<LinearTrailStepRecord> round_predecessors;
	};

	enum class LinearSearchStage : std::uint8_t
	{
		Enter = 0,
		Enumerate = 1,
		InjA = 2,
		SecondAdd = 3,
		InjB = 4,
		SecondConst = 5,
		FirstSubconst = 6,
		FirstAdd = 7,
		Recurse = 8
	};

	struct LinearSearchFrame
	{
		LinearSearchStage stage = LinearSearchStage::Enter;
		std::size_t		  trail_size_at_entry = 0;
		LinearRoundSearchState state {};
		std::size_t		  predecessor_index = 0;
	};

	struct LinearSearchCursor
	{
		std::vector<LinearSearchFrame> stack;
	};

	void linear_best_search_continue_from_cursor( LinearBestSearchContext& context, LinearSearchCursor& cursor );

	MatsuiSearchRunLinearResult run_linear_best_search(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& search_configuration,
		const LinearBestSearchRuntimeControls& runtime_controls = {},
		bool print_output = false,
		bool progress_print_masks = false,
		int seeded_upper_bound_weight = INFINITE_WEIGHT,
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail = nullptr,
		BestWeightHistory* checkpoint = nullptr,
		BinaryCheckpointManager* binary_checkpoint = nullptr,
		RuntimeEventLog* runtime_event_log = nullptr,
		const SearchInvocationMetadata* invocation_metadata = nullptr );

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
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt = nullptr );

	LinearStrictHullRuntimeResult run_linear_strict_hull_runtime(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& base_configuration,
		const LinearStrictHullRuntimeOptions& options = {},
		bool print_output = false,
		bool progress_print_masks = false );

}  // namespace TwilightDream::auto_search_linear
