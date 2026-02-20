#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"

namespace TwilightDream::auto_search_differential
{
	// One-shot hull collector.
	//
	// This is not a second best-search engine and it does not compete with the resumable
	// checkpoint path in `differential_best_search_engine.cpp`. Its job is to enumerate and
	// aggregate all trails up to a caller-provided weight cap, reusing the same ARX math
	// bridges (LM2001 modular addition, exact sub-const, exact injection transition model)
	// without maintaining a resumable DFS cursor.
	class DifferentialTrailCollector final
	{
	public:
		DifferentialTrailCollector(
			std::uint32_t start_difference_a,
			std::uint32_t start_difference_b,
			const DifferentialBestSearchConfiguration& base_configuration,
			const DifferentialHullCollectionOptions& options )
			: options_( options ),
			  collect_weight_cap_( std::max( 0, options.collect_weight_cap ) ),
			  maximum_collected_trails_( options.maximum_collected_trails )
		{
			context_.configuration = base_configuration;
			context_.runtime_controls = options.runtime_controls;
			context_.configuration.enable_state_memoization = false;
			context_.start_difference_a = start_difference_a;
			context_.start_difference_b = start_difference_b;
			context_.best_total_weight = ( collect_weight_cap_ >= INFINITE_WEIGHT - 1 ) ? INFINITE_WEIGHT : ( collect_weight_cap_ + 1 );
			context_.progress_every_seconds = context_.runtime_controls.progress_every_seconds;
			begin_differential_runtime_invocation( context_ );
			context_.progress_start_time = context_.run_start_time;
			context_.current_differential_trail.reserve( std::size_t( std::max( 1, context_.configuration.round_count ) ) );
			result_.collect_weight_cap = collect_weight_cap_;
		}

		DifferentialHullAggregationResult run()
		{
			ScopedRuntimeTimeLimitProbe time_probe( context_.runtime_controls, context_.runtime_state );
			search( 0, context_.start_difference_a, context_.start_difference_b, 0, 1.0L );
			result_.nodes_visited = context_.visited_node_count;
			result_.hit_time_limit = runtime_time_limit_hit( context_.runtime_controls, context_.runtime_state );
			result_.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap( context_.configuration );
			result_.exact_within_collect_weight_cap =
				!result_.hit_maximum_search_nodes &&
				!result_.hit_time_limit &&
				!result_.hit_collection_limit &&
				!result_.hit_callback_stop &&
				!result_.used_non_strict_branch_cap;
			result_.exactness_rejection_reason = classify_differential_collection_exactness_reason( result_ );
			return result_;
		}

	private:
		DifferentialBestSearchContext	 context_ {};
		DifferentialHullAggregationResult result_ {};
		DifferentialHullCollectionOptions options_ {};
		int								 collect_weight_cap_ = 0;
		std::uint64_t					 maximum_collected_trails_ = 0;

		int remaining_round_lower_bound( int depth ) const
		{
			if ( !context_.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left = context_.configuration.round_count - depth;
			if ( rounds_left < 0 )
				return 0;
			const std::size_t index = std::size_t( rounds_left );
			if ( index >= context_.configuration.remaining_round_min_weight.size() )
				return 0;
			return std::max( 0, context_.configuration.remaining_round_min_weight[ index ] );
		}

		int remaining_round_lower_bound_after_this_round( int depth ) const
		{
			if ( !context_.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left = context_.configuration.round_count - ( depth + 1 );
			if ( rounds_left < 0 )
				return 0;
			const std::size_t index = std::size_t( rounds_left );
			if ( index >= context_.configuration.remaining_round_min_weight.size() )
				return 0;
			return std::max( 0, context_.configuration.remaining_round_min_weight[ index ] );
		}

		bool should_stop_search( int depth, int accumulated_weight )
		{
			if ( result_.hit_callback_stop )
				return true;
			if ( maximum_collected_trails_ != 0 && result_.collected_trail_count >= maximum_collected_trails_ )
			{
				result_.hit_collection_limit = true;
				return true;
			}
			if ( context_.stop_due_to_time_limit )
				return true;

			if ( differential_note_runtime_node_visit( context_ ) )
			{
				result_.hit_time_limit = context_.stop_due_to_time_limit;
				result_.hit_maximum_search_nodes = differential_runtime_node_limit_hit( context_ );
				if ( result_.hit_maximum_search_nodes )
					return true;
				if ( result_.hit_time_limit )
					return true;
			}

			if ( differential_runtime_node_limit_hit( context_ ) )
			{
				result_.hit_maximum_search_nodes = true;
				return true;
			}

			maybe_print_single_run_progress( context_, depth );

			if ( accumulated_weight > collect_weight_cap_ )
				return true;
			if ( accumulated_weight + remaining_round_lower_bound( depth ) > collect_weight_cap_ )
				return true;
			return false;
		}

		void collect_current_trail( int total_weight, std::uint32_t output_branch_a_difference, std::uint32_t output_branch_b_difference, long double exact_probability )
		{
			result_.found_any = true;
			++result_.collected_trail_count;
			result_.aggregate_probability += exact_probability;
			auto& shell = result_.shell_summaries[ total_weight ];
			++shell.trail_count;
			shell.aggregate_probability += exact_probability;
			if ( exact_probability > result_.strongest_trail_probability || result_.strongest_trail.empty() )
			{
				result_.strongest_trail_probability = exact_probability;
				result_.strongest_trail = context_.current_differential_trail;
			}
			if ( options_.on_trail )
			{
				const DifferentialHullCollectedTrailView trail_view {
					total_weight,
					output_branch_a_difference,
					output_branch_b_difference,
					exact_probability,
					&context_.current_differential_trail
				};
				if ( !options_.on_trail( trail_view ) )
					result_.hit_callback_stop = true;
			}
			if ( maximum_collected_trails_ != 0 && result_.collected_trail_count >= maximum_collected_trails_ )
				result_.hit_collection_limit = true;
		}

		void sync_limit_flags_from_enumerator( bool stop_due_to_limits )
		{
			if ( !stop_due_to_limits )
				return;
			if ( differential_runtime_node_limit_hit( context_ ) )
				result_.hit_maximum_search_nodes = true;
			if ( context_.stop_due_to_time_limit )
				result_.hit_time_limit = true;
		}

		void search(
			int round_boundary_depth,
			std::uint32_t branch_a_input_difference,
			std::uint32_t branch_b_input_difference,
			int accumulated_weight_so_far,
			long double accumulated_exact_probability )
		{
			if ( should_stop_search( round_boundary_depth, accumulated_weight_so_far ) )
				return;

			if ( round_boundary_depth == context_.configuration.round_count )
			{
				collect_current_trail( accumulated_weight_so_far, branch_a_input_difference, branch_b_input_difference, accumulated_exact_probability );
				return;
			}

			const int remaining_round_lb_after_this_round = remaining_round_lower_bound_after_this_round( round_boundary_depth );
			const int round_budget = collect_weight_cap_ - accumulated_weight_so_far - remaining_round_lb_after_this_round;
			if ( round_budget < 0 )
				return;

			DifferentialTrailStepRecord step {};
			step.round_index = round_boundary_depth + 1;
			step.input_branch_a_difference = branch_a_input_difference;
			step.input_branch_b_difference = branch_b_input_difference;

			step.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, step.first_addition_term_difference, 32 );
			if ( optimal_weight_first_addition < 0 )
				return;

			int weight_cap_first_addition = std::min( round_budget, 31 );
			weight_cap_first_addition = std::min( weight_cap_first_addition, std::clamp( context_.configuration.addition_weight_cap, 0, 31 ) );
			if ( weight_cap_first_addition < 0 || optimal_weight_first_addition > weight_cap_first_addition )
				return;

			ModularAdditionEnumerator enum_first_add {};
			enum_first_add.reset( branch_b_input_difference, step.first_addition_term_difference, optimal_output_branch_b_difference_after_first_addition, weight_cap_first_addition );

			std::uint32_t output_branch_b_difference_after_first_addition = 0;
			int			  weight_first_addition = 0;
			while ( enum_first_add.next( context_, output_branch_b_difference_after_first_addition, weight_first_addition ) )
			{
				step.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
				step.weight_first_addition = weight_first_addition;

				const int accumulated_after_first_addition = accumulated_weight_so_far + weight_first_addition;
				if ( accumulated_after_first_addition + remaining_round_lb_after_this_round > collect_weight_cap_ )
					continue;

				int weight_cap_first_constant_subtraction = collect_weight_cap_ - accumulated_after_first_addition - remaining_round_lb_after_this_round;
				weight_cap_first_constant_subtraction = std::min( weight_cap_first_constant_subtraction, std::clamp( context_.configuration.constant_subtraction_weight_cap, 0, 32 ) );
				if ( weight_cap_first_constant_subtraction < 0 )
					continue;

				SubConstEnumerator enum_first_const {};
				enum_first_const.reset( branch_a_input_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], weight_cap_first_constant_subtraction );

				std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
				int			  weight_first_constant_subtraction = 0;
				while ( enum_first_const.next( context_, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction ) )
				{
					step.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
					step.weight_first_constant_subtraction = weight_first_constant_subtraction;

					const int accumulated_after_first_constant_subtraction = accumulated_after_first_addition + weight_first_constant_subtraction;
					if ( accumulated_after_first_constant_subtraction + remaining_round_lb_after_this_round > collect_weight_cap_ )
						continue;

					step.branch_a_difference_after_first_xor_with_rotated_branch_b =
						output_branch_a_difference_after_first_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					step.branch_b_difference_after_first_xor_with_rotated_branch_a =
						output_branch_b_difference_after_first_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( step.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

					const InjectionAffineTransition injection_transition_from_branch_b = compute_injection_transition_from_branch_b( step.branch_b_difference_after_first_xor_with_rotated_branch_a );
					step.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
					const int accumulated_before_second_addition = accumulated_after_first_constant_subtraction + step.weight_injection_from_branch_b;
					if ( accumulated_before_second_addition + remaining_round_lb_after_this_round > collect_weight_cap_ )
						continue;

					AffineSubspaceEnumerator enum_inj_b {};
					enum_inj_b.reset( injection_transition_from_branch_b, context_.configuration.maximum_transition_output_differences );

					std::uint32_t injection_from_branch_b_xor_difference = 0;
					while ( enum_inj_b.next( context_, injection_from_branch_b_xor_difference ) )
					{
						step.injection_from_branch_b_xor_difference = injection_from_branch_b_xor_difference;
						step.branch_a_difference_after_injection_from_branch_b = step.branch_a_difference_after_first_xor_with_rotated_branch_b ^ injection_from_branch_b_xor_difference;
						step.branch_b_difference_after_first_bridge = step.branch_b_difference_after_first_xor_with_rotated_branch_a;

						step.second_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( step.branch_b_difference_after_first_bridge, 31 ) ^
														 NeoAlzetteCore::rotl<std::uint32_t>( step.branch_b_difference_after_first_bridge, 17 );
						const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
							find_optimal_gamma_with_weight( step.branch_a_difference_after_injection_from_branch_b, step.second_addition_term_difference, 32 );
						if ( optimal_weight_second_addition < 0 )
							continue;

						int weight_cap_second_addition = collect_weight_cap_ - accumulated_before_second_addition - remaining_round_lb_after_this_round;
						weight_cap_second_addition = std::min( weight_cap_second_addition, 31 );
						weight_cap_second_addition = std::min( weight_cap_second_addition, std::clamp( context_.configuration.addition_weight_cap, 0, 31 ) );
						if ( weight_cap_second_addition < 0 || optimal_weight_second_addition > weight_cap_second_addition )
							continue;

						ModularAdditionEnumerator enum_second_add {};
						enum_second_add.reset( step.branch_a_difference_after_injection_from_branch_b, step.second_addition_term_difference, optimal_output_branch_a_difference_after_second_addition, weight_cap_second_addition );

						std::uint32_t output_branch_a_difference_after_second_addition = 0;
						int			  weight_second_addition = 0;
						while ( enum_second_add.next( context_, output_branch_a_difference_after_second_addition, weight_second_addition ) )
						{
							step.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
							step.weight_second_addition = weight_second_addition;

							const int accumulated_after_second_addition = accumulated_before_second_addition + weight_second_addition;
							if ( accumulated_after_second_addition + remaining_round_lb_after_this_round > collect_weight_cap_ )
								continue;

							int weight_cap_second_constant_subtraction = collect_weight_cap_ - accumulated_after_second_addition - remaining_round_lb_after_this_round;
							weight_cap_second_constant_subtraction = std::min( weight_cap_second_constant_subtraction, std::clamp( context_.configuration.constant_subtraction_weight_cap, 0, 32 ) );
							if ( weight_cap_second_constant_subtraction < 0 )
								continue;

							SubConstEnumerator enum_second_const {};
							enum_second_const.reset( step.branch_b_difference_after_first_bridge, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], weight_cap_second_constant_subtraction );

							std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
							int			  weight_second_constant_subtraction = 0;
							while ( enum_second_const.next( context_, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction ) )
							{
								step.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
								step.weight_second_constant_subtraction = weight_second_constant_subtraction;

								const int accumulated_after_second_constant_subtraction = accumulated_after_second_addition + weight_second_constant_subtraction;
								if ( accumulated_after_second_constant_subtraction + remaining_round_lb_after_this_round > collect_weight_cap_ )
									continue;

								step.branch_b_difference_after_second_xor_with_rotated_branch_a =
									output_branch_b_difference_after_second_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
								step.branch_a_difference_after_second_xor_with_rotated_branch_b =
									output_branch_a_difference_after_second_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( step.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

								const InjectionAffineTransition injection_transition_from_branch_a = compute_injection_transition_from_branch_a( step.branch_a_difference_after_second_xor_with_rotated_branch_b );
								step.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
								const int accumulated_at_round_end = accumulated_after_second_constant_subtraction + step.weight_injection_from_branch_a;
								if ( accumulated_at_round_end + remaining_round_lb_after_this_round > collect_weight_cap_ )
									continue;

								AffineSubspaceEnumerator enum_inj_a {};
								enum_inj_a.reset( injection_transition_from_branch_a, context_.configuration.maximum_transition_output_differences );

								std::uint32_t injection_from_branch_a_xor_difference = 0;
								while ( enum_inj_a.next( context_, injection_from_branch_a_xor_difference ) )
								{
									step.injection_from_branch_a_xor_difference = injection_from_branch_a_xor_difference;
									step.output_branch_a_difference = step.branch_a_difference_after_second_xor_with_rotated_branch_b;
									step.output_branch_b_difference = step.branch_b_difference_after_second_xor_with_rotated_branch_a ^ injection_from_branch_a_xor_difference;
									step.round_weight =
										step.weight_first_addition +
										step.weight_first_constant_subtraction +
										step.weight_injection_from_branch_b +
										step.weight_second_addition +
										step.weight_second_constant_subtraction +
										step.weight_injection_from_branch_a;

									context_.current_differential_trail.push_back( step );
									search(
										round_boundary_depth + 1,
										step.output_branch_a_difference,
										step.output_branch_b_difference,
										accumulated_at_round_end,
										accumulated_exact_probability * exact_differential_step_probability( step ) );
									context_.current_differential_trail.pop_back();

									if ( result_.hit_collection_limit || result_.hit_maximum_search_nodes || result_.hit_callback_stop || context_.stop_due_to_time_limit )
										return;
								}
								sync_limit_flags_from_enumerator( enum_inj_a.stop_due_to_limits );
								if ( result_.hit_maximum_search_nodes || result_.hit_time_limit || result_.hit_callback_stop )
									return;
							}
							sync_limit_flags_from_enumerator( enum_second_const.stop_due_to_limits );
							if ( result_.hit_maximum_search_nodes || result_.hit_time_limit || result_.hit_callback_stop )
								return;
						}
						sync_limit_flags_from_enumerator( enum_second_add.stop_due_to_limits );
						if ( result_.hit_maximum_search_nodes || result_.hit_time_limit || result_.hit_callback_stop )
							return;
					}
					sync_limit_flags_from_enumerator( enum_inj_b.stop_due_to_limits );
					if ( result_.hit_maximum_search_nodes || result_.hit_time_limit || result_.hit_callback_stop )
						return;
				}
				sync_limit_flags_from_enumerator( enum_first_const.stop_due_to_limits );
				if ( result_.hit_maximum_search_nodes || result_.hit_time_limit || result_.hit_callback_stop )
					return;
			}
			sync_limit_flags_from_enumerator( enum_first_add.stop_due_to_limits );
		}
	};

	DifferentialHullAggregationResult collect_differential_hull_exact(
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b,
		const DifferentialBestSearchConfiguration& base_configuration,
		const DifferentialHullCollectionOptions& options )
	{
		DifferentialTrailCollector collector( start_difference_a, start_difference_b, base_configuration, options );
		return collector.run();
	}

	DifferentialStrictHullRuntimeResult run_differential_strict_hull_runtime(
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b,
		const DifferentialBestSearchConfiguration& base_configuration,
		const DifferentialStrictHullRuntimeOptions& options,
		bool print_output,
		bool progress_print_differences )
	{
		DifferentialStrictHullRuntimeResult runtime_result {};

		if ( differential_configuration_has_strict_branch_cap( base_configuration ) )
		{
			runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::UsedBranchCap;
			return runtime_result;
		}
		if ( differential_configuration_uses_non_strict_remaining_round_bound( base_configuration ) )
		{
			runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::UsedNonStrictRemainingBound;
			return runtime_result;
		}

		DifferentialHullCollectionOptions collection_options {};
		collection_options.maximum_collected_trails = options.maximum_collected_trails;
		collection_options.runtime_controls = options.runtime_controls;
		collection_options.on_trail = options.on_trail;

		if ( options.collect_cap_mode == HullCollectCapMode::ExplicitCap )
		{
			runtime_result.collect_weight_cap = std::clamp( options.explicit_collect_weight_cap, 0, INFINITE_WEIGHT - 1 );
			collection_options.collect_weight_cap = runtime_result.collect_weight_cap;
			runtime_result.aggregation_result =
				collect_differential_hull_exact(
					start_difference_a,
					start_difference_b,
					base_configuration,
					collection_options );
			runtime_result.aggregation_result.best_weight_certified = false;
			runtime_result.collected = true;
			return runtime_result;
		}

		// Strict hull runtime is a composition:
		// 1) use the resumable best-search engine to certify the best weight,
		// 2) run the one-shot collector to aggregate all trails inside the chosen window.
		runtime_result.used_best_weight_reference = true;
		runtime_result.best_search_executed = true;
		const int seeded_upper_bound_weight =
			options.best_search_seed_present ? options.best_search_seeded_upper_bound_weight : INFINITE_WEIGHT;
		const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail =
			( options.best_search_seed_present && !options.best_search_seeded_upper_bound_trail.empty() ) ?
				&options.best_search_seeded_upper_bound_trail :
				nullptr;
		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			runtime_result.best_search_result =
				run_differential_best_search_resume(
					options.best_search_resume_checkpoint_path,
					start_difference_a,
					start_difference_b,
					base_configuration,
					options.runtime_controls,
					print_output,
					progress_print_differences,
					options.best_search_history_log,
					options.best_search_binary_checkpoint,
					options.best_search_runtime_log,
					nullptr );
		}
		else
		{
			runtime_result.best_search_result =
				run_differential_best_search(
					base_configuration.round_count,
					start_difference_a,
					start_difference_b,
					base_configuration,
					options.runtime_controls,
					print_output,
					progress_print_differences,
					seeded_upper_bound_weight,
					seeded_upper_bound_trail,
					options.best_search_history_log,
					options.best_search_binary_checkpoint,
					options.best_search_runtime_log );
		}
		if ( !runtime_result.best_search_result.best_weight_certified )
		{
			runtime_result.strict_runtime_rejection_reason = runtime_result.best_search_result.strict_rejection_reason;
			return runtime_result;
		}

		runtime_result.collect_weight_cap =
			std::min(
				INFINITE_WEIGHT - 1,
				runtime_result.best_search_result.best_weight + std::max( 0, options.collect_weight_window ) );
		collection_options.collect_weight_cap = runtime_result.collect_weight_cap;
		runtime_result.aggregation_result =
			collect_differential_hull_exact(
				start_difference_a,
				start_difference_b,
				base_configuration,
				collection_options );
		runtime_result.aggregation_result.best_weight_certified = runtime_result.best_search_result.best_weight_certified;
		runtime_result.collected = true;
		return runtime_result;
	}

}  // namespace TwilightDream::auto_search_differential
