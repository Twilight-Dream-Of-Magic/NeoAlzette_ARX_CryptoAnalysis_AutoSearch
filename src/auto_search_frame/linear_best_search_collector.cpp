#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"

namespace TwilightDream::auto_search_linear
{
	// One-shot hull collector.
	//
	// This is not a second best-search engine and it does not compete with the resumable
	// checkpoint path in `linear_best_search_engine.cpp`. Its job is to enumerate and
	// aggregate all trails up to a caller-provided weight cap, reusing the same ARX math
	// bridges (exact injection-mask spaces, exact sub-const, exact z-shell add candidates)
	// without maintaining a resumable DFS cursor.
	class LinearTrailCollector final
	{
	public:
		LinearTrailCollector(
			std::uint32_t output_branch_a_mask,
			std::uint32_t output_branch_b_mask,
			const LinearBestSearchConfiguration& base_configuration,
			const LinearHullCollectionOptions& options )
			: options_( options ),
			  collect_weight_cap_( std::max( 0, options.collect_weight_cap ) ),
			  maximum_collected_trails_( options.maximum_collected_trails )
		{
			context_.configuration = base_configuration;
			context_.runtime_controls = options.runtime_controls;
			context_.configuration.enable_state_memoization = false;
			context_.start_output_branch_a_mask = output_branch_a_mask;
			context_.start_output_branch_b_mask = output_branch_b_mask;
			context_.best_weight = ( collect_weight_cap_ >= INFINITE_WEIGHT - 1 ) ? INFINITE_WEIGHT : ( collect_weight_cap_ + 1 );
			context_.progress_every_seconds = context_.runtime_controls.progress_every_seconds;
			begin_linear_runtime_invocation( context_ );
			context_.progress_start_time = context_.run_start_time;
			context_.current_linear_trail.reserve( std::size_t( std::max( 1, context_.configuration.round_count ) ) );
			result_.collect_weight_cap = collect_weight_cap_;
		}

		LinearHullAggregationResult run()
		{
			ScopedRuntimeTimeLimitProbe time_probe( context_.runtime_controls, context_.runtime_state );
			search( 0, context_.start_output_branch_a_mask, context_.start_output_branch_b_mask, 0, 1.0L );
			result_.nodes_visited = context_.visited_node_count;
			result_.hit_time_limit = runtime_time_limit_hit( context_.runtime_controls, context_.runtime_state );
			result_.used_non_strict_branch_cap = linear_collector_configuration_has_non_strict_branch_cap( context_.configuration );
			const bool used_non_strict_search_mode = linear_configuration_uses_non_strict_search_mode( context_.configuration );
			result_.exact_within_collect_weight_cap =
				!used_non_strict_search_mode &&
				!result_.hit_maximum_search_nodes &&
				!result_.hit_time_limit &&
				!result_.hit_collection_limit &&
				!result_.hit_callback_stop &&
				!result_.used_non_strict_branch_cap;
			result_.exactness_rejection_reason =
				classify_linear_collection_exactness_reason( result_, used_non_strict_search_mode );
			return result_;
		}

	private:
		LinearBestSearchContext	 context_ {};
		LinearHullAggregationResult result_ {};
		LinearHullCollectionOptions options_ {};
		int						 collect_weight_cap_ = 0;
		std::uint64_t			 maximum_collected_trails_ = 0;

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

		bool should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
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

			if ( linear_note_runtime_node_visit( context_ ) )
			{
				result_.hit_time_limit = runtime_time_limit_hit( context_.runtime_controls, context_.runtime_state );
				result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context_ );
				return true;
			}

			if ( linear_runtime_node_limit_hit( context_ ) )
			{
				result_.hit_maximum_search_nodes = true;
				return true;
			}

			maybe_print_single_run_progress( context_, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask );

			if ( accumulated_weight > collect_weight_cap_ )
				return true;
			if ( accumulated_weight + remaining_round_lower_bound( depth ) > collect_weight_cap_ )
				return true;
			return false;
		}

		bool inner_limits_hit( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask )
		{
			if ( result_.hit_callback_stop )
				return true;
			if ( maximum_collected_trails_ != 0 && result_.collected_trail_count >= maximum_collected_trails_ )
			{
				result_.hit_collection_limit = true;
				return true;
			}

			if ( runtime_time_limit_hit( context_.runtime_controls, context_.runtime_state ) )
			{
				result_.hit_time_limit = true;
				return true;
			}

			if ( linear_runtime_node_limit_hit( context_ ) )
			{
				result_.hit_maximum_search_nodes = true;
				return true;
			}

			maybe_print_single_run_progress( context_, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask );
			return context_.stop_due_to_time_limit;
		}

		void collect_current_trail( int total_weight, std::uint32_t input_branch_a_mask, std::uint32_t input_branch_b_mask, long double exact_signed_correlation )
		{
			result_.found_any = true;
			++result_.collected_trail_count;
			result_.aggregate_signed_correlation += exact_signed_correlation;
			result_.aggregate_abs_correlation_mass += std::fabsl( exact_signed_correlation );
			auto& shell = result_.shell_summaries[ total_weight ];
			++shell.trail_count;
			shell.aggregate_signed_correlation += exact_signed_correlation;
			shell.aggregate_abs_correlation_mass += std::fabsl( exact_signed_correlation );
			if ( std::fabsl( exact_signed_correlation ) > std::fabsl( result_.strongest_trail_signed_correlation ) || result_.strongest_trail.empty() )
			{
				result_.strongest_trail_signed_correlation = exact_signed_correlation;
				result_.strongest_input_branch_a_mask = input_branch_a_mask;
				result_.strongest_input_branch_b_mask = input_branch_b_mask;
				result_.strongest_trail = context_.current_linear_trail;
			}
			if ( options_.on_trail )
			{
				const LinearHullCollectedTrailView trail_view {
					total_weight,
					input_branch_a_mask,
					input_branch_b_mask,
					exact_signed_correlation,
					&context_.current_linear_trail
				};
				if ( !options_.on_trail( trail_view ) )
					result_.hit_callback_stop = true;
			}
			if ( maximum_collected_trails_ != 0 && result_.collected_trail_count >= maximum_collected_trails_ )
				result_.hit_collection_limit = true;
		}

		void search(
			int depth,
			std::uint32_t current_round_output_branch_a_mask,
			std::uint32_t current_round_output_branch_b_mask,
			int accumulated_weight,
			long double accumulated_signed_correlation )
		{
			if ( should_stop_search( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_weight ) )
				return;

			if ( depth == context_.configuration.round_count )
			{
				collect_current_trail( accumulated_weight, current_round_output_branch_a_mask, current_round_output_branch_b_mask, accumulated_signed_correlation );
				return;
			}

			const int round_index = context_.configuration.round_count - depth;
			const int remaining_round_lb_after_this_round = remaining_round_lower_bound_after_this_round( depth );
			const int round_weight_cap = collect_weight_cap_ - accumulated_weight - remaining_round_lb_after_this_round;
			if ( round_weight_cap < 0 )
				return;

			const std::uint32_t branch_a_round_output_mask_before_inj_from_a = current_round_output_branch_a_mask;
			const std::uint32_t branch_b_mask_before_injection_from_branch_a = current_round_output_branch_b_mask;
			const InjectionCorrelationTransition injection_from_branch_a_transition = compute_injection_transition_from_branch_a( current_round_output_branch_b_mask );
			const int weight_injection_from_branch_a = injection_from_branch_a_transition.weight;
			if ( weight_injection_from_branch_a > round_weight_cap )
				return;

			const int remaining_after_inj_a = round_weight_cap - weight_injection_from_branch_a;
			const int second_subconst_weight_cap = std::min( context_.configuration.constant_subtraction_weight_cap, remaining_after_inj_a );
			const int second_add_weight_cap = std::min( context_.configuration.addition_weight_cap, remaining_after_inj_a );

			enumerate_affine_subspace_input_masks(
				context_,
				injection_from_branch_a_transition,
				context_.configuration.maximum_injection_input_masks,
				[&]( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a ) {
					if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
						return;

					const std::uint32_t branch_a_round_output_mask_before_inj_from_a_with_choice =
						branch_a_round_output_mask_before_inj_from_a ^ chosen_correlated_input_mask_for_injection_from_branch_a;
					const std::uint32_t output_branch_a_mask_after_second_addition =
						branch_a_round_output_mask_before_inj_from_a_with_choice ^
						NeoAlzetteCore::rotr(
							branch_b_mask_before_injection_from_branch_a ^ NeoAlzetteCore::rotr( branch_a_round_output_mask_before_inj_from_a_with_choice, NeoAlzetteCore::CROSS_XOR_ROT_R1 ),
							NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					const std::uint32_t output_branch_b_mask_after_second_constant_subtraction =
						branch_b_mask_before_injection_from_branch_a ^ NeoAlzetteCore::rotr( branch_a_round_output_mask_before_inj_from_a_with_choice, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

					const auto second_constant_subtraction_candidates_for_branch_b =
						generate_subconst_candidates_for_fixed_beta(
							output_branch_b_mask_after_second_constant_subtraction,
							NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
							second_subconst_weight_cap );
					const auto& second_addition_candidates_for_branch_a =
						AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
							output_branch_a_mask_after_second_addition,
							second_add_weight_cap,
							context_.configuration.search_mode,
							context_.configuration.enable_weight_sliced_clat,
							context_.configuration.weight_sliced_clat_max_candidates );

					for ( const auto& second_addition_candidate_for_branch_a : second_addition_candidates_for_branch_a )
					{
						if ( second_addition_candidate_for_branch_a.linear_weight > second_add_weight_cap )
							break;
						if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
							return;

						const int weight_second_addition = second_addition_candidate_for_branch_a.linear_weight;
						if ( weight_injection_from_branch_a + weight_second_addition > round_weight_cap )
							break;

						const std::uint32_t input_branch_a_mask_before_second_addition = second_addition_candidate_for_branch_a.input_mask_x;
						const std::uint32_t second_addition_term_mask_from_branch_b = second_addition_candidate_for_branch_a.input_mask_y;
						const std::uint32_t branch_b_mask_contribution_from_second_addition_term =
							NeoAlzetteCore::rotr( second_addition_term_mask_from_branch_b, 31 ) ^
							NeoAlzetteCore::rotr( second_addition_term_mask_from_branch_b, 17 );

						const InjectionCorrelationTransition injection_from_branch_b_transition = compute_injection_transition_from_branch_b( input_branch_a_mask_before_second_addition );
						const int weight_injection_from_branch_b = injection_from_branch_b_transition.weight;
						const int base_weight_after_inj_b = weight_injection_from_branch_a + weight_second_addition + weight_injection_from_branch_b;
						if ( base_weight_after_inj_b > round_weight_cap )
							continue;

						enumerate_affine_subspace_input_masks(
							context_,
							injection_from_branch_b_transition,
							context_.configuration.maximum_injection_input_masks,
							[&]( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b ) {
								if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
									return;

								for ( const auto& second_constant_subtraction_candidate_for_branch_b : second_constant_subtraction_candidates_for_branch_b )
								{
									if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
										return;

									const int weight_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.linear_weight;
									if ( base_weight_after_inj_b + weight_second_constant_subtraction > round_weight_cap )
										break;

									const std::uint32_t input_branch_b_mask_before_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.input_mask_on_x;
									const std::uint32_t branch_b_mask_after_second_add_term_removed =
										input_branch_b_mask_before_second_constant_subtraction ^ branch_b_mask_contribution_from_second_addition_term;
									const std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a =
										branch_b_mask_after_second_add_term_removed ^ chosen_correlated_input_mask_for_injection_from_branch_b;
									const std::uint32_t output_branch_a_mask_after_first_constant_subtraction =
										input_branch_a_mask_before_second_addition ^
										NeoAlzetteCore::rotr( branch_b_mask_after_first_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
									const std::uint32_t output_branch_b_mask_after_first_addition =
										branch_b_mask_after_first_xor_with_rotated_branch_a ^
										NeoAlzetteCore::rotr( output_branch_a_mask_after_first_constant_subtraction, NeoAlzetteCore::CROSS_XOR_ROT_R0 );

									const int base_weight_after_second_subconst = base_weight_after_inj_b + weight_second_constant_subtraction;
									const int remaining_after_second_subconst = round_weight_cap - base_weight_after_second_subconst;
									if ( remaining_after_second_subconst < 0 )
										continue;

									const int first_subconst_weight_cap = std::min( context_.configuration.constant_subtraction_weight_cap, remaining_after_second_subconst );
									const int first_add_weight_cap = std::min( context_.configuration.addition_weight_cap, remaining_after_second_subconst );

									const auto first_constant_subtraction_candidates_for_branch_a =
										generate_subconst_candidates_for_fixed_beta(
											output_branch_a_mask_after_first_constant_subtraction,
											NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
											first_subconst_weight_cap );
									const auto& first_addition_candidates_for_branch_b =
										AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
											output_branch_b_mask_after_first_addition,
											first_add_weight_cap,
											context_.configuration.search_mode,
											context_.configuration.enable_weight_sliced_clat,
											context_.configuration.weight_sliced_clat_max_candidates );

									for ( const auto& first_constant_subtraction_candidate_for_branch_a : first_constant_subtraction_candidates_for_branch_a )
									{
										if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
											return;

										const int weight_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.linear_weight;
										const int base_weight_after_first_subconst = base_weight_after_second_subconst + weight_first_constant_subtraction;
										if ( base_weight_after_first_subconst > round_weight_cap )
											break;

										const std::uint32_t input_branch_a_mask_before_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.input_mask_on_x;
										for ( const auto& first_addition_candidate_for_branch_b : first_addition_candidates_for_branch_b )
										{
											if ( first_addition_candidate_for_branch_b.linear_weight > first_add_weight_cap )
												break;
											if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
												return;

											const int weight_first_addition = first_addition_candidate_for_branch_b.linear_weight;
											const int total_round_weight = base_weight_after_first_subconst + weight_first_addition;
											if ( total_round_weight > round_weight_cap )
												break;

											const std::uint32_t input_branch_b_mask_before_first_addition = first_addition_candidate_for_branch_b.input_mask_x;
											const std::uint32_t first_addition_term_mask_from_branch_a = first_addition_candidate_for_branch_b.input_mask_y;
											const std::uint32_t input_branch_a_mask =
												input_branch_a_mask_before_first_constant_subtraction ^
												NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 31 ) ^
												NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 17 );

											LinearTrailStepRecord step {};
											step.round_index = round_index;
											step.output_branch_a_mask = current_round_output_branch_a_mask;
											step.output_branch_b_mask = current_round_output_branch_b_mask;
											step.input_branch_a_mask = input_branch_a_mask;
											step.input_branch_b_mask = input_branch_b_mask_before_first_addition;

											step.output_branch_b_mask_after_second_constant_subtraction = output_branch_b_mask_after_second_constant_subtraction;
											step.input_branch_b_mask_before_second_constant_subtraction = input_branch_b_mask_before_second_constant_subtraction;
											step.weight_second_constant_subtraction = weight_second_constant_subtraction;

											step.output_branch_a_mask_after_second_addition = output_branch_a_mask_after_second_addition;
											step.input_branch_a_mask_before_second_addition = input_branch_a_mask_before_second_addition;
											step.second_addition_term_mask_from_branch_b = second_addition_term_mask_from_branch_b;
											step.weight_second_addition = weight_second_addition;

											step.weight_injection_from_branch_a = weight_injection_from_branch_a;
											step.weight_injection_from_branch_b = weight_injection_from_branch_b;
											step.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_correlated_input_mask_for_injection_from_branch_a;
											step.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_correlated_input_mask_for_injection_from_branch_b;

											step.output_branch_a_mask_after_first_constant_subtraction = output_branch_a_mask_after_first_constant_subtraction;
											step.input_branch_a_mask_before_first_constant_subtraction = input_branch_a_mask_before_first_constant_subtraction;
											step.weight_first_constant_subtraction = weight_first_constant_subtraction;

											step.output_branch_b_mask_after_first_addition = output_branch_b_mask_after_first_addition;
											step.input_branch_b_mask_before_first_addition = input_branch_b_mask_before_first_addition;
											step.first_addition_term_mask_from_branch_a = first_addition_term_mask_from_branch_a;
											step.weight_first_addition = weight_first_addition;
											step.round_weight = total_round_weight;

											context_.current_linear_trail.push_back( step );
											search(
												depth + 1,
												step.input_branch_a_mask,
												step.input_branch_b_mask,
												accumulated_weight + total_round_weight,
												accumulated_signed_correlation * exact_linear_step_correlation( step ) );
											context_.current_linear_trail.pop_back();

											if ( result_.hit_collection_limit || result_.hit_maximum_search_nodes || result_.hit_callback_stop || context_.stop_due_to_time_limit )
												return;
										}
									}
								}
							} );
					}
				} );
		}
	};

	LinearHullAggregationResult collect_linear_hull_exact(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& base_configuration,
		const LinearHullCollectionOptions& options = {} )
	{
		LinearTrailCollector collector( output_branch_a_mask, output_branch_b_mask, base_configuration, options );
		return collector.run();
	}


	LinearStrictHullRuntimeResult run_linear_strict_hull_runtime(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& base_configuration,
		const LinearStrictHullRuntimeOptions& options,
		bool print_output,
		bool progress_print_masks )
	{
		LinearStrictHullRuntimeResult runtime_result {};

		if ( linear_configuration_uses_non_strict_search_mode( base_configuration ) )
		{
			runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::UsedNonStrictSearchMode;
			return runtime_result;
		}
		if ( linear_configuration_has_strict_branch_cap( base_configuration ) )
		{
			runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::UsedBranchCap;
			return runtime_result;
		}
		if ( linear_configuration_uses_non_strict_remaining_round_bound( base_configuration ) )
		{
			runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::UsedNonStrictRemainingBound;
			return runtime_result;
		}

		LinearHullCollectionOptions collection_options {};
		collection_options.maximum_collected_trails = options.maximum_collected_trails;
		collection_options.runtime_controls = options.runtime_controls;
		collection_options.on_trail = options.on_trail;

		if ( options.collect_cap_mode == HullCollectCapMode::ExplicitCap )
		{
			runtime_result.collect_weight_cap = std::clamp( options.explicit_collect_weight_cap, 0, INFINITE_WEIGHT - 1 );
			collection_options.collect_weight_cap = runtime_result.collect_weight_cap;
			runtime_result.aggregation_result =
				collect_linear_hull_exact(
					output_branch_a_mask,
					output_branch_b_mask,
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
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail =
			( options.best_search_seed_present && !options.best_search_seeded_upper_bound_trail.empty() ) ?
				&options.best_search_seeded_upper_bound_trail :
				nullptr;
		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			runtime_result.best_search_result =
				run_linear_best_search_resume(
					options.best_search_resume_checkpoint_path,
					output_branch_a_mask,
					output_branch_b_mask,
					base_configuration,
					options.runtime_controls,
					print_output,
					progress_print_masks,
					options.best_search_history_log,
					options.best_search_binary_checkpoint,
					options.best_search_runtime_log,
					nullptr );
		}
		else
		{
			runtime_result.best_search_result =
				run_linear_best_search(
					output_branch_a_mask,
					output_branch_b_mask,
					base_configuration,
					options.runtime_controls,
					print_output,
					progress_print_masks,
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
			collect_linear_hull_exact(
				output_branch_a_mask,
				output_branch_b_mask,
				base_configuration,
				collection_options );
		runtime_result.aggregation_result.best_weight_certified = runtime_result.best_search_result.best_weight_certified;
		runtime_result.collected = true;
		return runtime_result;
	}

	// ============================================================================

}  // namespace TwilightDream::auto_search_linear
