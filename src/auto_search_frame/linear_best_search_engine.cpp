#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

namespace TwilightDream::auto_search_linear
{
	void linear_best_search_continue_from_cursor( LinearBestSearchContext& context, LinearSearchCursor& cursor );

	static inline std::vector<int> auto_generate_remaining_round_lower_bound_table( const LinearBestSearchConfiguration& base_configuration, std::uint32_t start_output_branch_a_mask, std::uint32_t start_output_branch_b_mask )
	{
		const int round_count = std::max( 0, base_configuration.round_count );
		std::vector<int> table( std::size_t( round_count ) + 1u, 0 );
		if ( round_count <= 0 )
			return table;

		for ( int k = 1; k <= round_count; ++k )
		{
			LinearBestSearchConfiguration config = base_configuration;
			config.search_mode = SearchMode::Strict;
			config.round_count = k;
			config.enable_remaining_round_lower_bound = false;
			config.remaining_round_min_weight.clear();
			config.auto_generate_remaining_round_lower_bound = false;
			config.target_best_weight = -1;
			// Make the lower-bound search as complete as possible by removing heuristic caps.
			config.maximum_round_predecessors = 0;
			config.maximum_injection_input_masks = 0;
			LinearBestSearchRuntimeControls runtime_controls {};
			runtime_controls.maximum_search_nodes = base_configuration.remaining_round_lower_bound_generation_nodes;
			runtime_controls.maximum_search_seconds = base_configuration.remaining_round_lower_bound_generation_seconds;

			LinearBestSearchContext tmp_context {};
			tmp_context.configuration = config;
			tmp_context.runtime_controls = runtime_controls;
			tmp_context.start_output_branch_a_mask = start_output_branch_a_mask;
			tmp_context.start_output_branch_b_mask = start_output_branch_b_mask;
			begin_linear_runtime_invocation( tmp_context );
			tmp_context.memoization.initialize( std::size_t( config.round_count ) + 1u, config.enable_state_memoization, "linear_memo.lb.init" );

			LinearSearchCursor cursor {};
			LinearSearchFrame  frame {};
			frame.stage = LinearSearchStage::Enter;
			frame.trail_size_at_entry = tmp_context.current_linear_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.round_output_branch_a_mask = start_output_branch_a_mask;
			frame.state.round_output_branch_b_mask = start_output_branch_b_mask;
			cursor.stack.push_back( frame );
			ScopedRuntimeTimeLimitProbe time_probe( tmp_context.runtime_controls, tmp_context.runtime_state );
			linear_best_search_continue_from_cursor( tmp_context, cursor );

			if ( tmp_context.best_weight < INFINITE_WEIGHT )
				table[ std::size_t( k ) ] = tmp_context.best_weight;
		}

		return table;
	}

	// ARX Automatic Search Frame - Linear Analysis Paper:
	// [eprint-iacr-org-2019-1319] Automatic Search for the Linear (hull) Characteristics of ARX Ciphers - Applied to SPECK, SPARX, Chaskey and CHAM-64
	// Is applied to NeoAlzette ARX-Box Algorithm every step of the round
	//
	// Mathematical wiring inside this resumable DFS cursor:
	// - `compute_injection_transition_from_branch_a/b()` derives the exact affine
	//   input-mask space of the two quadratic injection layers.
	// - `enumerate_affine_subspace_input_masks()` walks that injection space.
	// - `generate_subconst_candidates_for_fixed_beta()` gives exact var-const candidates.
	// - `AddVarVarSplit8Enumerator32` and the weight-sliced cLAT streaming cursor provide
	//   the Schulte-Geers / Huang-Wang var-var addition candidates under exact z-shell weights.
	//
	// The checkpoint stores this cursor together with the in-flight enumerator/candidate
	// state, so resume continues from the same search node instead of rebuilding the round.
	class LinearBestTrailSearcherCursor final
	{
	public:
		LinearBestTrailSearcherCursor( LinearBestSearchContext& context_in, LinearSearchCursor& cursor_in )
			: context( context_in ), search_configuration( context_in.configuration ), cursor( cursor_in )
		{
		}

		void start_from_initial_frame( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask )
		{
			cursor.stack.clear();
			cursor.stack.reserve( std::size_t( std::max( 1, search_configuration.round_count ) ) + 1u );
			context.current_linear_trail.clear();
			LinearSearchFrame frame {};
			frame.stage = LinearSearchStage::Enter;
			frame.trail_size_at_entry = context.current_linear_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.round_output_branch_a_mask = output_branch_a_mask;
			frame.state.round_output_branch_b_mask = output_branch_b_mask;
			cursor.stack.push_back( frame );
		}

		void search_from_start( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask )
		{
			start_from_initial_frame( output_branch_a_mask, output_branch_b_mask );
			run();
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		LinearBestSearchContext&			   context;
		const LinearBestSearchConfiguration& search_configuration;
		LinearSearchCursor&				   cursor;

		LinearSearchFrame& current_frame()
		{
			return cursor.stack.back();
		}

		LinearRoundSearchState& current_round_state()
		{
			return current_frame().state;
		}

		bool using_round_predecessor_mode() const noexcept
		{
			return search_configuration.maximum_round_predecessors != 0;
		}

		void pop_frame()
		{
			if ( cursor.stack.empty() )
				return;
			const std::size_t target = cursor.stack.back().trail_size_at_entry;
			if ( context.current_linear_trail.size() > target )
				context.current_linear_trail.resize( target );
			cursor.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if ( !context.binary_checkpoint )
				return;
			if ( best_search_shared_core::should_poll_binary_checkpoint(
					context.binary_checkpoint->pending_best_change(),
					context.binary_checkpoint->pending_runtime_request() ||
						TwilightDream::runtime_component::runtime_watchdog_checkpoint_request_pending( context.runtime_state ),
					context.run_visited_node_count,
					context.progress_node_mask ) )
				context.binary_checkpoint->poll( context, cursor );
		}

		void run()
		{
			while ( !cursor.stack.empty() )
			{
				LinearSearchFrame&	   frame = current_frame();
				LinearRoundSearchState& state = frame.state;

				switch ( frame.stage )
				{
				case LinearSearchStage::Enter:
				{
					if ( should_stop_search( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
					{
						if ( context.stop_due_to_time_limit || linear_runtime_node_limit_hit( context ) || context.stop_due_to_target )
							return;
						pop_frame();
						break;
					}
					if ( handle_round_end_if_needed( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( should_prune_state_memoization( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}

					if ( !prepare_round_state( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}

					begin_streaming_round();
					break;
				}
				case LinearSearchStage::InjA:
				{
					if ( !advance_stage_inj_a() )
						return;
					break;
				}
				case LinearSearchStage::SecondAdd:
				{
					if ( !advance_stage_second_add() )
						return;
					break;
				}
				case LinearSearchStage::InjB:
				{
					if ( !advance_stage_inj_b() )
						return;
					break;
				}
				case LinearSearchStage::SecondConst:
				{
					if ( !advance_stage_second_const() )
						return;
					break;
				}
				case LinearSearchStage::FirstSubconst:
				{
					if ( !advance_stage_first_subconst() )
						return;
					break;
				}
				case LinearSearchStage::FirstAdd:
				{
					if ( !advance_stage_first_add() )
						return;
					break;
				}
				case LinearSearchStage::Enumerate:
				{
					if ( context.stop_due_to_time_limit || linear_runtime_node_limit_hit( context ) || context.stop_due_to_target )
						return;

					reset_round_predecessor_buffer();
					begin_streaming_round();
					break;
				}
				case LinearSearchStage::Recurse:
				{
					if ( context.stop_due_to_time_limit || linear_runtime_node_limit_hit( context ) || context.stop_due_to_target )
						return;
					if ( frame.predecessor_index >= state.round_predecessors.size() )
					{
						pop_frame();
						break;
					}

					const auto& step = state.round_predecessors[ frame.predecessor_index++ ];
					if ( state.accumulated_weight_so_far + step.round_weight >= context.best_weight )
						break;

					context.current_linear_trail.push_back( step );
					LinearSearchFrame child {};
					child.stage = LinearSearchStage::Enter;
					// Store the parent trail size (exclude the step we just pushed) so pop_frame() removes it.
					child.trail_size_at_entry = context.current_linear_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + step.round_weight;
					child.state.round_output_branch_a_mask = step.input_branch_a_mask;
					child.state.round_output_branch_b_mask = step.input_branch_b_mask;
					cursor.stack.push_back( child );
					break;
				}
				}

				maybe_poll_checkpoint();
			}
		}

		bool sync_and_check_runtime_stop()
		{
			sync_linear_runtime_legacy_fields_from_state( context );
			return context.stop_due_to_target || linear_runtime_budget_hit( context );
		}

		bool poll_shared_runtime()
		{
			const bool stop = runtime_poll( context.runtime_controls, context.runtime_state );
			sync_linear_runtime_legacy_fields_from_state( context );
			return stop || context.stop_due_to_target;
		}

		void maybe_print_streaming_progress()
		{
			const auto& state = current_round_state();
			maybe_print_single_run_progress(
				context,
				state.round_boundary_depth,
				state.round_output_branch_a_mask,
				state.round_output_branch_b_mask );
			maybe_poll_checkpoint();
		}

		template <typename T>
		void clear_rebuildable_vector( std::vector<T>& values, std::size_t keep_capacity = 4096u )
		{
			values.clear();
			if ( values.capacity() > keep_capacity && memory_governor_in_pressure() )
			{
				std::vector<T> empty;
				values.swap( empty );
			}
		}

		bool use_split8_streaming_add_cursor() const
		{
			return search_configuration.search_mode == SearchMode::Strict &&
				   !search_configuration.enable_weight_sliced_clat;
		}

		bool use_weight_sliced_clat_streaming_add_cursor() const
		{
			return search_configuration.search_mode == SearchMode::Strict &&
				   search_configuration.enable_weight_sliced_clat &&
				   search_configuration.weight_sliced_clat_max_candidates == 0;
		}

		bool use_streaming_subconst_cursor() const
		{
			return search_configuration.search_mode == SearchMode::Strict;
		}

		void clear_first_stage_buffers( LinearRoundSearchState& state )
		{
			clear_rebuildable_vector( state.first_constant_subtraction_candidates_for_branch_a );
			state.first_constant_subtraction_stream_cursor = {};
			clear_rebuildable_vector( state.first_addition_candidates_for_branch_b );
			state.first_addition_stream_cursor = {};
			state.first_addition_weight_sliced_clat_stream_cursor = {};
			state.first_constant_subtraction_candidate_index = 0;
			state.first_addition_candidate_index = 0;
		}

		void clear_second_stage_buffers( LinearRoundSearchState& state )
		{
			state.second_constant_subtraction_stream_cursor = {};
			clear_rebuildable_vector( state.second_constant_subtraction_candidates_for_branch_b );
			clear_rebuildable_vector( state.second_addition_candidates_storage );
			state.second_addition_stream_cursor = {};
			state.second_addition_weight_sliced_clat_stream_cursor = {};
			state.second_addition_candidates_for_branch_a = nullptr;
			state.second_addition_candidate_index = 0;
			state.second_constant_subtraction_candidate_index = 0;
			state.injection_from_branch_b_enumerator = {};
			clear_first_stage_buffers( state );
		}

		void begin_streaming_round()
		{
			auto& frame = current_frame();
			auto& state = frame.state;
			clear_second_stage_buffers( state );
			state.injection_from_branch_a_enumerator.reset(
				state.injection_from_branch_a_transition,
				search_configuration.maximum_injection_input_masks );
			frame.stage = LinearSearchStage::InjA;
		}

		bool advance_stage_inj_a()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			std::uint32_t chosen_mask = 0;
			if ( !state.injection_from_branch_a_enumerator.next( context, state.injection_from_branch_a_transition, chosen_mask ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				clear_second_stage_buffers( state );
				if ( using_round_predecessor_mode() )
				{
					trim_round_predecessors( true );
					frame.predecessor_index = 0;
					frame.stage = LinearSearchStage::Recurse;
				}
				else
				{
					pop_frame();
				}
				return true;
			}

			if ( sync_and_check_runtime_stop() )
				return false;

			maybe_print_streaming_progress();
			state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_mask;

			const std::uint32_t branch_a_round_output_mask_before_inj_from_a_with_choice =
				state.branch_a_round_output_mask_before_inj_from_a ^ chosen_mask;
			const std::uint32_t branch_b_mask_before_injection_from_branch_a_for_this_choice =
				state.branch_b_mask_before_injection_from_branch_a;

			state.output_branch_a_mask_after_second_addition =
				branch_a_round_output_mask_before_inj_from_a_with_choice;
			const std::uint32_t branch_b_mask_after_second_xor_with_rotated_branch_a =
				branch_b_mask_before_injection_from_branch_a_for_this_choice ^
				NeoAlzetteCore::rotr(
					branch_a_round_output_mask_before_inj_from_a_with_choice,
					NeoAlzetteCore::CROSS_XOR_ROT_R1 );
			state.output_branch_b_mask_after_second_constant_subtraction =
				branch_b_mask_after_second_xor_with_rotated_branch_a;
			state.output_branch_a_mask_after_second_addition ^=
				NeoAlzetteCore::rotr(
					branch_b_mask_after_second_xor_with_rotated_branch_a,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );

			// After fixing the injection mask choice, the second subround reconnects to the
			// exact ARX operators: var-const subtraction on B and var-var addition on A.
			// These candidate sources may be materialized or streamed, but their cursor/state
			// is exactly what the resumable checkpoint persists.
			if ( use_streaming_subconst_cursor() )
			{
				reset_subconst_streaming_cursor(
					state.second_constant_subtraction_stream_cursor,
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					state.second_subconst_weight_cap );
				if ( sync_and_check_runtime_stop() )
					return false;
				clear_rebuildable_vector( state.second_constant_subtraction_candidates_for_branch_b );
			}
			else
			{
				state.second_constant_subtraction_candidates_for_branch_b =
					generate_subconst_candidates_for_fixed_beta(
						state.output_branch_b_mask_after_second_constant_subtraction,
						NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
						state.second_subconst_weight_cap );
				if ( sync_and_check_runtime_stop() )
					return false;
			}

			if ( use_split8_streaming_add_cursor() )
			{
				AddVarVarSplit8Enumerator32::reset_streaming_cursor(
					state.second_addition_stream_cursor,
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap );
				if ( sync_and_check_runtime_stop() )
					return false;
				clear_rebuildable_vector( state.second_addition_candidates_storage );
			}
			else if ( use_weight_sliced_clat_streaming_add_cursor() )
			{
				reset_weight_sliced_clat_streaming_cursor(
					state.second_addition_weight_sliced_clat_stream_cursor,
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap );
				if ( sync_and_check_runtime_stop() )
					return false;
				clear_rebuildable_vector( state.second_addition_candidates_storage );
			}
			else
			{
				const auto& second_addition_candidates =
					AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
						state.output_branch_a_mask_after_second_addition,
						state.second_add_weight_cap,
						search_configuration.search_mode,
						search_configuration.enable_weight_sliced_clat,
						search_configuration.weight_sliced_clat_max_candidates );
				if ( sync_and_check_runtime_stop() )
					return false;

				state.second_addition_candidates_storage.assign(
					second_addition_candidates.begin(),
					second_addition_candidates.end() );
			}
			state.second_addition_candidate_index = 0;
			state.injection_from_branch_b_enumerator = {};
			state.second_constant_subtraction_candidate_index = 0;
			clear_first_stage_buffers( state );
			frame.stage = LinearSearchStage::SecondAdd;
			return true;
		}

		bool advance_stage_second_add()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			AddCandidate candidate {};
			while ( true )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					return false;
				if ( linear_note_runtime_node_visit( context ) )
					return false;
				maybe_print_streaming_progress();

				if ( use_split8_streaming_add_cursor() )
				{
					if ( !AddVarVarSplit8Enumerator32::next_streaming_candidate( state.second_addition_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else if ( use_weight_sliced_clat_streaming_add_cursor() )
				{
					if ( !next_weight_sliced_clat_streaming_candidate( state.second_addition_weight_sliced_clat_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else
				{
					if ( state.second_addition_candidate_index >= state.second_addition_candidates_storage.size() )
						break;
					candidate = state.second_addition_candidates_storage[ state.second_addition_candidate_index++ ];
				}
				if ( candidate.linear_weight > state.second_add_weight_cap )
				{
					if ( !use_split8_streaming_add_cursor() )
						state.second_addition_candidate_index = state.second_addition_candidates_storage.size();
					break;
				}

				state.weight_second_addition = candidate.linear_weight;
				if ( state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap )
				{
					if ( !use_split8_streaming_add_cursor() )
						state.second_addition_candidate_index = state.second_addition_candidates_storage.size();
					break;
				}

				state.input_branch_a_mask_before_second_addition = candidate.input_mask_x;
				state.second_addition_term_mask_from_branch_b = candidate.input_mask_y;
				state.branch_b_mask_contribution_from_second_addition_term =
					NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 31 ) ^
					NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 17 );

				// Reverse-round middle bridge: once the second modular-addition candidate is
				// fixed, the B->A injection again becomes an exact affine input-mask space.
				state.injection_from_branch_b_transition =
					compute_injection_transition_from_branch_b(
						state.input_branch_a_mask_before_second_addition );
				state.weight_injection_from_branch_b = state.injection_from_branch_b_transition.weight;
				state.base_weight_after_inj_b =
					state.weight_injection_from_branch_a +
					state.weight_second_addition +
					state.weight_injection_from_branch_b;
				if ( state.base_weight_after_inj_b >= state.round_weight_cap )
					continue;

				state.injection_from_branch_b_enumerator.reset(
					state.injection_from_branch_b_transition,
					search_configuration.maximum_injection_input_masks );
				state.second_constant_subtraction_candidate_index = 0;
				frame.stage = LinearSearchStage::InjB;
				return true;
			}

			clear_second_stage_buffers( state );
			frame.stage = LinearSearchStage::InjA;
			return true;
		}

		bool advance_stage_inj_b()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			std::uint32_t chosen_mask = 0;
			if ( !state.injection_from_branch_b_enumerator.next( context, state.injection_from_branch_b_transition, chosen_mask ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				clear_first_stage_buffers( state );
				frame.stage = LinearSearchStage::SecondAdd;
				return true;
			}

			if ( sync_and_check_runtime_stop() )
				return false;

			maybe_print_streaming_progress();
			state.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_mask;
			state.second_constant_subtraction_candidate_index = 0;
			clear_first_stage_buffers( state );
			frame.stage = LinearSearchStage::SecondConst;
			return true;
		}

		bool advance_stage_second_const()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			SubConstCandidate candidate {};
			while ( true )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					return false;
				if ( linear_note_runtime_node_visit( context ) )
					return false;
				maybe_print_streaming_progress();

				if ( use_streaming_subconst_cursor() )
				{
					if ( !next_subconst_streaming_candidate( state.second_constant_subtraction_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else
				{
					if ( state.second_constant_subtraction_candidate_index >= state.second_constant_subtraction_candidates_for_branch_b.size() )
						break;
					candidate = state.second_constant_subtraction_candidates_for_branch_b[ state.second_constant_subtraction_candidate_index++ ];
				}
				state.weight_second_constant_subtraction = candidate.linear_weight;
				if ( state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap )
				{
					if ( use_streaming_subconst_cursor() )
						continue;
					state.second_constant_subtraction_candidate_index =
						state.second_constant_subtraction_candidates_for_branch_b.size();
					break;
				}

				state.input_branch_b_mask_before_second_constant_subtraction = candidate.input_mask_on_x;
				state.branch_b_mask_after_second_add_term_removed =
					state.input_branch_b_mask_before_second_constant_subtraction ^
					state.branch_b_mask_contribution_from_second_addition_term;
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base =
					state.branch_b_mask_after_second_add_term_removed;

				state.base_weight_after_second_subconst =
					state.base_weight_after_inj_b + state.weight_second_constant_subtraction;
				if ( state.base_weight_after_second_subconst >= state.round_weight_cap )
					continue;

				const int remaining_after_second_subconst =
					state.round_weight_cap - state.base_weight_after_second_subconst;
				if ( remaining_after_second_subconst <= 0 )
					continue;
				state.first_subconst_weight_cap =
					std::min(
						search_configuration.constant_subtraction_weight_cap,
						remaining_after_second_subconst - 1 );
				state.first_add_weight_cap =
					std::min(
						search_configuration.addition_weight_cap,
						remaining_after_second_subconst - 1 );
				if ( state.first_add_weight_cap < 0 )
					continue;

				const std::uint32_t output_branch_a_mask_after_injection_from_branch_b =
					state.input_branch_a_mask_before_second_addition;
				const std::uint32_t branch_a_mask_after_first_xor_with_rotated_branch_b =
					output_branch_a_mask_after_injection_from_branch_b;
				const std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a =
					state.branch_b_mask_after_first_xor_with_rotated_branch_a_base ^
					state.chosen_correlated_input_mask_for_injection_from_branch_b;

				state.output_branch_a_mask_after_first_constant_subtraction =
					branch_a_mask_after_first_xor_with_rotated_branch_b ^
					NeoAlzetteCore::rotr(
						branch_b_mask_after_first_xor_with_rotated_branch_a,
						NeoAlzetteCore::CROSS_XOR_ROT_R1 );
				state.output_branch_b_mask_after_first_addition =
					branch_b_mask_after_first_xor_with_rotated_branch_a ^
					NeoAlzetteCore::rotr(
						state.output_branch_a_mask_after_first_constant_subtraction,
						NeoAlzetteCore::CROSS_XOR_ROT_R0 );

				if ( use_streaming_subconst_cursor() )
				{
					reset_subconst_streaming_cursor(
						state.first_constant_subtraction_stream_cursor,
						state.output_branch_a_mask_after_first_constant_subtraction,
						NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
						state.first_subconst_weight_cap );
					if ( sync_and_check_runtime_stop() )
						return false;
					clear_rebuildable_vector( state.first_constant_subtraction_candidates_for_branch_a );
				}
				else
				{
					state.first_constant_subtraction_candidates_for_branch_a =
						generate_subconst_candidates_for_fixed_beta(
							state.output_branch_a_mask_after_first_constant_subtraction,
							NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
							state.first_subconst_weight_cap );
					if ( sync_and_check_runtime_stop() )
						return false;
				}

				if ( use_split8_streaming_add_cursor() )
				{
					AddVarVarSplit8Enumerator32::reset_streaming_cursor(
						state.first_addition_stream_cursor,
						state.output_branch_b_mask_after_first_addition,
						state.first_add_weight_cap );
					if ( sync_and_check_runtime_stop() )
						return false;
					clear_rebuildable_vector( state.first_addition_candidates_for_branch_b );
				}
				else if ( use_weight_sliced_clat_streaming_add_cursor() )
				{
					reset_weight_sliced_clat_streaming_cursor(
						state.first_addition_weight_sliced_clat_stream_cursor,
						state.output_branch_b_mask_after_first_addition,
						state.first_add_weight_cap );
					if ( sync_and_check_runtime_stop() )
						return false;
					clear_rebuildable_vector( state.first_addition_candidates_for_branch_b );
				}
				else
				{
					const auto& first_addition_candidates =
						AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
							state.output_branch_b_mask_after_first_addition,
							state.first_add_weight_cap,
							search_configuration.search_mode,
							search_configuration.enable_weight_sliced_clat,
							search_configuration.weight_sliced_clat_max_candidates );
					if ( sync_and_check_runtime_stop() )
						return false;
					state.first_addition_candidates_for_branch_b.assign(
						first_addition_candidates.begin(),
						first_addition_candidates.end() );
				}
				state.first_constant_subtraction_candidate_index = 0;
				state.first_addition_candidate_index = 0;
				frame.stage = LinearSearchStage::FirstSubconst;
				return true;
			}

			clear_first_stage_buffers( state );
			frame.stage = LinearSearchStage::InjB;
			return true;
		}

		bool advance_stage_first_subconst()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			SubConstCandidate candidate {};
			while ( true )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					return false;
				if ( linear_note_runtime_node_visit( context ) )
					return false;
				maybe_print_streaming_progress();

				if ( use_streaming_subconst_cursor() )
				{
					if ( !next_subconst_streaming_candidate( state.first_constant_subtraction_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else
				{
					if ( state.first_constant_subtraction_candidate_index >= state.first_constant_subtraction_candidates_for_branch_a.size() )
						break;
					candidate = state.first_constant_subtraction_candidates_for_branch_a[ state.first_constant_subtraction_candidate_index++ ];
				}
				const int base_weight_after_first_subconst =
					state.base_weight_after_second_subconst + candidate.linear_weight;
				if ( base_weight_after_first_subconst >= state.round_weight_cap )
				{
					if ( use_streaming_subconst_cursor() )
						continue;
					state.first_constant_subtraction_candidate_index =
						state.first_constant_subtraction_candidates_for_branch_a.size();
					break;
				}

				state.input_branch_a_mask_before_first_constant_subtraction_current =
					candidate.input_mask_on_x;
				state.weight_first_constant_subtraction_current = candidate.linear_weight;
				state.first_addition_candidate_index = 0;
				frame.stage = LinearSearchStage::FirstAdd;
				return true;
			}

			frame.stage = LinearSearchStage::SecondConst;
			return true;
		}

		bool advance_stage_first_add()
		{
			if ( poll_shared_runtime() )
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			const int base_weight_after_first_subconst =
				state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current;
			AddCandidate candidate {};
			while ( true )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					return false;
				if ( linear_note_runtime_node_visit( context ) )
					return false;
				maybe_print_streaming_progress();

				if ( use_split8_streaming_add_cursor() )
				{
					if ( !AddVarVarSplit8Enumerator32::next_streaming_candidate( state.first_addition_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else if ( use_weight_sliced_clat_streaming_add_cursor() )
				{
					if ( !next_weight_sliced_clat_streaming_candidate( state.first_addition_weight_sliced_clat_stream_cursor, candidate ) )
					{
						if ( sync_and_check_runtime_stop() )
							return false;
						break;
					}
				}
				else
				{
					if ( state.first_addition_candidate_index >= state.first_addition_candidates_for_branch_b.size() )
						break;
					candidate = state.first_addition_candidates_for_branch_b[ state.first_addition_candidate_index++ ];
				}
				if ( candidate.linear_weight > state.first_add_weight_cap )
				{
					if ( !use_split8_streaming_add_cursor() )
						state.first_addition_candidate_index = state.first_addition_candidates_for_branch_b.size();
					break;
				}

				const int total_w = base_weight_after_first_subconst + candidate.linear_weight;
				if ( total_w >= state.round_weight_cap )
				{
					if ( !use_split8_streaming_add_cursor() )
						state.first_addition_candidate_index = state.first_addition_candidates_for_branch_b.size();
					break;
				}

				const std::uint32_t branch_a_mask_contribution_from_first_addition_term =
					NeoAlzetteCore::rotr( candidate.input_mask_y, 31 ) ^
					NeoAlzetteCore::rotr( candidate.input_mask_y, 17 );
				const std::uint32_t input_branch_a_mask_at_round_input =
					state.input_branch_a_mask_before_first_constant_subtraction_current ^
					branch_a_mask_contribution_from_first_addition_term;

				LinearTrailStepRecord step {};
				step.round_index = state.round_index;
				step.output_branch_a_mask = state.round_output_branch_a_mask;
				step.output_branch_b_mask = state.round_output_branch_b_mask;
				step.input_branch_a_mask = input_branch_a_mask_at_round_input;
				step.input_branch_b_mask = candidate.input_mask_x;

				step.output_branch_b_mask_after_second_constant_subtraction =
					state.output_branch_b_mask_after_second_constant_subtraction;
				step.input_branch_b_mask_before_second_constant_subtraction =
					state.input_branch_b_mask_before_second_constant_subtraction;
				step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;

				step.output_branch_a_mask_after_second_addition = state.output_branch_a_mask_after_second_addition;
				step.input_branch_a_mask_before_second_addition = state.input_branch_a_mask_before_second_addition;
				step.second_addition_term_mask_from_branch_b = state.second_addition_term_mask_from_branch_b;
				step.weight_second_addition = state.weight_second_addition;

				step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
				step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
				step.chosen_correlated_input_mask_for_injection_from_branch_a =
					state.chosen_correlated_input_mask_for_injection_from_branch_a;
				step.chosen_correlated_input_mask_for_injection_from_branch_b =
					state.chosen_correlated_input_mask_for_injection_from_branch_b;

				step.output_branch_a_mask_after_first_constant_subtraction =
					state.output_branch_a_mask_after_first_constant_subtraction;
				step.input_branch_a_mask_before_first_constant_subtraction =
					state.input_branch_a_mask_before_first_constant_subtraction_current;
				step.weight_first_constant_subtraction =
					state.weight_first_constant_subtraction_current;

				step.output_branch_b_mask_after_first_addition =
					state.output_branch_b_mask_after_first_addition;
				step.input_branch_b_mask_before_first_addition = candidate.input_mask_x;
				step.first_addition_term_mask_from_branch_a = candidate.input_mask_y;
				step.weight_first_addition = candidate.linear_weight;
				step.round_weight = total_w;

				if ( state.accumulated_weight_so_far + step.round_weight >= context.best_weight )
					continue;

				if ( using_round_predecessor_mode() )
				{
					state.round_predecessors.push_back( step );
					trim_round_predecessors( false );
					continue;
				}

				context.current_linear_trail.push_back( step );
				LinearSearchFrame child {};
				child.stage = LinearSearchStage::Enter;
				child.trail_size_at_entry = context.current_linear_trail.size() - 1u;
				child.state.round_boundary_depth = state.round_boundary_depth + 1;
				child.state.accumulated_weight_so_far =
					state.accumulated_weight_so_far + step.round_weight;
				child.state.round_output_branch_a_mask = step.input_branch_a_mask;
				child.state.round_output_branch_b_mask = step.input_branch_b_mask;
				cursor.stack.push_back( child );
				return true;
			}

			frame.stage = LinearSearchStage::FirstSubconst;
			return true;
		}

		// Global stop conditions, node/time budget, and trivial weight pruning.
		bool should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( context.stop_due_to_time_limit || context.stop_due_to_target )
				return true;

			if ( linear_runtime_node_limit_hit( context ) )
				return true;

			if ( linear_note_runtime_node_visit( context ) )
				return true;

			maybe_print_single_run_progress( context, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask );
			maybe_poll_checkpoint();

			if ( accumulated_weight >= context.best_weight )
				return true;

			if ( should_prune_remaining_round_lower_bound( depth, accumulated_weight ) )
				return true;

			return false;
		}

		bool should_prune_remaining_round_lower_bound( int depth, int accumulated_weight ) const
		{
			if ( context.best_weight >= INFINITE_WEIGHT )
				return false;
			if ( !search_configuration.enable_remaining_round_lower_bound )
				return false;

			const int rounds_left = search_configuration.round_count - depth;
			if ( rounds_left < 0 )
				return false;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t table_index = std::size_t( rounds_left );
			if ( table_index >= remaining_round_min_weight_table.size() )
				return false;
			const int weight_lower_bound = remaining_round_min_weight_table[ table_index ];
			return accumulated_weight + weight_lower_bound >= context.best_weight;
		}

		bool handle_round_end_if_needed( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( depth != search_configuration.round_count )
				return false;

			context.best_weight = accumulated_weight;
			context.best_linear_trail = context.current_linear_trail;
			context.best_input_branch_a_mask = current_round_output_branch_a_mask;
			context.best_input_branch_b_mask = current_round_output_branch_b_mask;
			if ( context.checkpoint )
				context.checkpoint->maybe_write( context, "improved" );
			if ( context.binary_checkpoint )
				context.binary_checkpoint->mark_best_changed();
			if ( search_configuration.target_best_weight >= 0 && context.best_weight <= search_configuration.target_best_weight )
				context.stop_due_to_target = true;
			return true;
		}

		bool should_prune_state_memoization( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			if ( !search_configuration.enable_state_memoization )
				return false;

			const std::size_t hint = linear_runtime_memo_reserve_hint( context );

			const std::uint64_t key = ( std::uint64_t( current_round_output_branch_a_mask ) << 32 ) | std::uint64_t( current_round_output_branch_b_mask );
			return context.memoization.should_prune_and_update( std::size_t( depth ), key, accumulated_weight, true, true, hint, 192ull, "linear_memo.reserve", "linear_memo.try_emplace" );
		}

		bool prepare_round_state( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, int accumulated_weight )
		{
			auto& state = current_round_state();
			state.round_boundary_depth = depth;
			state.accumulated_weight_so_far = accumulated_weight;
			state.round_index = search_configuration.round_count - depth;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( depth );
			const int base_cap = ( context.best_weight >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : ( context.best_weight - accumulated_weight );
			state.round_weight_cap = ( base_cap >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : ( base_cap - state.remaining_round_weight_lower_bound_after_this_round );
			if ( state.round_weight_cap <= 0 )
				return false;

			state.round_output_branch_a_mask = current_round_output_branch_a_mask;
			state.round_output_branch_b_mask = current_round_output_branch_b_mask;

			state.branch_a_round_output_mask_before_inj_from_a = current_round_output_branch_a_mask;
			state.branch_b_mask_before_injection_from_branch_a = current_round_output_branch_b_mask;

			// Reverse-round entry bridge: from the current round-output masks, compute the
			// exact affine input-mask space induced by the A->B injection layer.
			state.injection_from_branch_a_transition = compute_injection_transition_from_branch_a( current_round_output_branch_b_mask );
			state.weight_injection_from_branch_a = state.injection_from_branch_a_transition.weight;
			if ( state.weight_injection_from_branch_a >= state.round_weight_cap )
				return false;

			state.remaining_after_inj_a = state.round_weight_cap - state.weight_injection_from_branch_a;
			if ( state.remaining_after_inj_a <= 0 )
				return false;

			state.second_subconst_weight_cap = std::min( search_configuration.constant_subtraction_weight_cap, state.remaining_after_inj_a - 1 );
			state.second_add_weight_cap = std::min( search_configuration.addition_weight_cap, state.remaining_after_inj_a - 1 );
			if ( state.second_add_weight_cap < 0 )
				return false;

			state.second_addition_candidates_for_branch_a = nullptr;
			reset_round_predecessor_buffer();

			return true;
		}

		int compute_remaining_round_weight_lower_bound_after_this_round( int depth ) const
		{
			if ( !search_configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left_after = search_configuration.round_count - ( depth + 1 );
			if ( rounds_left_after < 0 )
				return 0;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t( rounds_left_after );
			if ( idx >= remaining_round_min_weight_table.size() )
				return 0;
			return std::max( 0, remaining_round_min_weight_table[ idx ] );
		}

		void reset_round_predecessor_buffer()
		{
			auto& state = current_round_state();
			state.round_predecessors.clear();
			state.round_predecessors.reserve( std::min<std::size_t>( search_configuration.maximum_round_predecessors ? search_configuration.maximum_round_predecessors : 32, 512 ) );
		}

		void trim_round_predecessors( bool force )
		{
			if ( search_configuration.maximum_round_predecessors == 0 )
				return;

			auto& state = current_round_state();
			const std::size_t cap = search_configuration.maximum_round_predecessors;
			if ( cap == 0 )
				return;
			const std::size_t threshold = std::min<std::size_t>( cap * 8u, 16'384u );
			if ( !force && state.round_predecessors.size() <= threshold )
				return;

			std::sort( state.round_predecessors.begin(), state.round_predecessors.end(), []( const LinearTrailStepRecord& a, const LinearTrailStepRecord& b ) {
				if ( a.round_weight != b.round_weight )
					return a.round_weight < b.round_weight;
				if ( a.input_branch_a_mask != b.input_branch_a_mask )
					return a.input_branch_a_mask < b.input_branch_a_mask;
				return a.input_branch_b_mask < b.input_branch_b_mask;
			} );
			state.round_predecessors.erase( std::unique( state.round_predecessors.begin(), state.round_predecessors.end(), []( const LinearTrailStepRecord& a, const LinearTrailStepRecord& b ) {
				return a.input_branch_a_mask == b.input_branch_a_mask && a.input_branch_b_mask == b.input_branch_b_mask;
			} ), state.round_predecessors.end() );
			if ( state.round_predecessors.size() > cap )
				state.round_predecessors.resize( cap );
		}

		void enumerate_round_predecessors()
		{
			enumerate_injection_from_branch_a_masks();
			trim_round_predecessors( true );
		}

		void enumerate_injection_from_branch_a_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_a_transition,
				search_configuration.maximum_injection_input_masks,
				[ this ]( std::uint32_t m ) { handle_injection_from_branch_a_mask( m ); } );
		}

		void handle_injection_from_branch_a_mask( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a )
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_correlated_input_mask_for_injection_from_branch_a;

			const std::uint32_t branch_a_round_output_mask_before_inj_from_a_with_choice =
				state.branch_a_round_output_mask_before_inj_from_a ^ chosen_correlated_input_mask_for_injection_from_branch_a;
			const std::uint32_t branch_b_mask_before_injection_from_branch_a_for_this_choice = state.branch_b_mask_before_injection_from_branch_a;

			state.output_branch_a_mask_after_second_addition = branch_a_round_output_mask_before_inj_from_a_with_choice;
			const std::uint32_t branch_b_mask_after_second_xor_with_rotated_branch_a =
				branch_b_mask_before_injection_from_branch_a_for_this_choice ^ NeoAlzetteCore::rotr( branch_a_round_output_mask_before_inj_from_a_with_choice, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
			state.output_branch_b_mask_after_second_constant_subtraction = branch_b_mask_after_second_xor_with_rotated_branch_a;
			state.output_branch_a_mask_after_second_addition ^=
				NeoAlzetteCore::rotr( branch_b_mask_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R0 );

			state.second_constant_subtraction_candidates_for_branch_b =
				generate_subconst_candidates_for_fixed_beta(
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					state.second_subconst_weight_cap );
			state.second_addition_candidates_for_branch_a =
				&AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap,
					search_configuration.search_mode,
					search_configuration.enable_weight_sliced_clat,
					search_configuration.weight_sliced_clat_max_candidates );

			enumerate_second_addition_candidates();
		}

		void enumerate_second_addition_candidates()
		{
			auto& state = current_round_state();
			if ( !state.second_addition_candidates_for_branch_a )
				return;

			for ( const auto& second_addition_candidate_for_branch_a : *state.second_addition_candidates_for_branch_a )
			{
				if ( second_addition_candidate_for_branch_a.linear_weight > state.second_add_weight_cap )
					break;

				if ( linear_runtime_node_limit_hit( context ) )
					break;
				if ( linear_note_runtime_node_visit( context ) )
					break;

				state.weight_second_addition = second_addition_candidate_for_branch_a.linear_weight;
				if ( state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap )
					break;

				state.input_branch_a_mask_before_second_addition = second_addition_candidate_for_branch_a.input_mask_x;
				state.second_addition_term_mask_from_branch_b = second_addition_candidate_for_branch_a.input_mask_y;

				handle_second_addition_candidate();
			}
		}

		void handle_second_addition_candidate()
		{
			auto& state = current_round_state();
			state.branch_b_mask_contribution_from_second_addition_term =
				NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 31 ) ^ NeoAlzetteCore::rotr( state.second_addition_term_mask_from_branch_b, 17 );

			state.injection_from_branch_b_transition = compute_injection_transition_from_branch_b( state.input_branch_a_mask_before_second_addition );
			state.weight_injection_from_branch_b = state.injection_from_branch_b_transition.weight;
			state.base_weight_after_inj_b = state.weight_injection_from_branch_a + state.weight_second_addition + state.weight_injection_from_branch_b;
			if ( state.base_weight_after_inj_b >= state.round_weight_cap )
				return;

			enumerate_injection_from_branch_b_masks();
		}

		void enumerate_injection_from_branch_b_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_b_transition,
				search_configuration.maximum_injection_input_masks,
				[ this ]( std::uint32_t m ) { handle_injection_from_branch_b_mask( m ); } );
		}

		void handle_injection_from_branch_b_mask( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b )
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_correlated_input_mask_for_injection_from_branch_b;
			enumerate_second_constant_subtraction_candidates();
		}

		void enumerate_second_constant_subtraction_candidates()
		{
			auto& state = current_round_state();
			for ( const auto& second_constant_subtraction_candidate_for_branch_b : state.second_constant_subtraction_candidates_for_branch_b )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					break;
				if ( linear_note_runtime_node_visit( context ) )
					break;

				state.weight_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.linear_weight;
				if ( state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap )
					break;

				state.input_branch_b_mask_before_second_constant_subtraction = second_constant_subtraction_candidate_for_branch_b.input_mask_on_x;
				state.branch_b_mask_after_second_add_term_removed =
					state.input_branch_b_mask_before_second_constant_subtraction ^ state.branch_b_mask_contribution_from_second_addition_term;
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base = state.branch_b_mask_after_second_add_term_removed;

				state.base_weight_after_second_subconst = state.base_weight_after_inj_b + state.weight_second_constant_subtraction;
				if ( state.base_weight_after_second_subconst >= state.round_weight_cap )
					continue;

				const int remaining_after_second_subconst = state.round_weight_cap - state.base_weight_after_second_subconst;
				if ( remaining_after_second_subconst <= 0 )
					continue;
				state.first_subconst_weight_cap = std::min( search_configuration.constant_subtraction_weight_cap, remaining_after_second_subconst - 1 );
				state.first_add_weight_cap = std::min( search_configuration.addition_weight_cap, remaining_after_second_subconst - 1 );
				if ( state.first_add_weight_cap < 0 )
					continue;

				const std::uint32_t output_branch_a_mask_after_injection_from_branch_b = state.input_branch_a_mask_before_second_addition;
				const std::uint32_t branch_a_mask_after_first_xor_with_rotated_branch_b = output_branch_a_mask_after_injection_from_branch_b;
				const std::uint32_t branch_b_mask_after_first_xor_with_rotated_branch_a =
					state.branch_b_mask_after_first_xor_with_rotated_branch_a_base ^ state.chosen_correlated_input_mask_for_injection_from_branch_b;

				state.output_branch_a_mask_after_first_constant_subtraction =
					branch_a_mask_after_first_xor_with_rotated_branch_b ^ NeoAlzetteCore::rotr( branch_b_mask_after_first_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
				state.output_branch_b_mask_after_first_addition =
					branch_b_mask_after_first_xor_with_rotated_branch_a ^ NeoAlzetteCore::rotr( state.output_branch_a_mask_after_first_constant_subtraction, NeoAlzetteCore::CROSS_XOR_ROT_R0 );

				enumerate_first_subround_candidates();
			}
		}

		void enumerate_first_subround_candidates()
		{
			auto& state = current_round_state();
			// Final reverse subround:
			// - exact sub-const candidates for A <- A ⊟ RC1
			// - exact var-var addition candidates for B <- B ⊞ T0
			// Together these determine the predecessor masks at the round input boundary.
			const auto first_constant_subtraction_candidates_for_branch_a =
				generate_subconst_candidates_for_fixed_beta(
					state.output_branch_a_mask_after_first_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					state.first_subconst_weight_cap );
			const auto& first_addition_candidates_for_branch_b =
				AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
					state.output_branch_b_mask_after_first_addition,
					state.first_add_weight_cap,
					search_configuration.search_mode,
					search_configuration.enable_weight_sliced_clat,
					search_configuration.weight_sliced_clat_max_candidates );

			for ( const auto& first_constant_subtraction_candidate_for_branch_a : first_constant_subtraction_candidates_for_branch_a )
			{
				if ( linear_runtime_node_limit_hit( context ) )
					break;
				if ( linear_note_runtime_node_visit( context ) )
					break;

				const std::uint32_t input_branch_a_mask_before_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.input_mask_on_x;
				const int			base_weight_after_first_subconst = state.base_weight_after_second_subconst + first_constant_subtraction_candidate_for_branch_a.linear_weight;
				if ( base_weight_after_first_subconst >= state.round_weight_cap )
					break;

				for ( const auto& first_addition_candidate_for_branch_b : first_addition_candidates_for_branch_b )
				{
					if ( first_addition_candidate_for_branch_b.linear_weight > state.first_add_weight_cap )
						break;

					if ( linear_runtime_node_limit_hit( context ) )
						break;
					if ( linear_note_runtime_node_visit( context ) )
						break;

					const std::uint32_t input_branch_b_mask_before_first_addition = first_addition_candidate_for_branch_b.input_mask_x;
					const std::uint32_t first_addition_term_mask_from_branch_a = first_addition_candidate_for_branch_b.input_mask_y;

					const std::uint32_t branch_a_mask_contribution_from_first_addition_term =
						NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 31 ) ^ NeoAlzetteCore::rotr( first_addition_term_mask_from_branch_a, 17 );
					const std::uint32_t input_branch_a_mask_at_round_input =
						input_branch_a_mask_before_first_constant_subtraction ^ branch_a_mask_contribution_from_first_addition_term;

					const int total_w = base_weight_after_first_subconst + first_addition_candidate_for_branch_b.linear_weight;
					if ( total_w >= state.round_weight_cap )
						break;

					LinearTrailStepRecord step {};
					step.round_index = state.round_index;
					step.output_branch_a_mask = state.round_output_branch_a_mask;
					step.output_branch_b_mask = state.round_output_branch_b_mask;
					step.input_branch_a_mask = input_branch_a_mask_at_round_input;
					step.input_branch_b_mask = input_branch_b_mask_before_first_addition;

					step.output_branch_b_mask_after_second_constant_subtraction = state.output_branch_b_mask_after_second_constant_subtraction;
					step.input_branch_b_mask_before_second_constant_subtraction = state.input_branch_b_mask_before_second_constant_subtraction;
					step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;

					step.output_branch_a_mask_after_second_addition = state.output_branch_a_mask_after_second_addition;
					step.input_branch_a_mask_before_second_addition = state.input_branch_a_mask_before_second_addition;
					step.second_addition_term_mask_from_branch_b = state.second_addition_term_mask_from_branch_b;
					step.weight_second_addition = state.weight_second_addition;

					step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
					step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
					step.chosen_correlated_input_mask_for_injection_from_branch_a = state.chosen_correlated_input_mask_for_injection_from_branch_a;
					step.chosen_correlated_input_mask_for_injection_from_branch_b = state.chosen_correlated_input_mask_for_injection_from_branch_b;

					step.output_branch_a_mask_after_first_constant_subtraction = state.output_branch_a_mask_after_first_constant_subtraction;
					step.input_branch_a_mask_before_first_constant_subtraction = input_branch_a_mask_before_first_constant_subtraction;
					step.weight_first_constant_subtraction = first_constant_subtraction_candidate_for_branch_a.linear_weight;

					step.output_branch_b_mask_after_first_addition = state.output_branch_b_mask_after_first_addition;
					step.input_branch_b_mask_before_first_addition = input_branch_b_mask_before_first_addition;
					step.first_addition_term_mask_from_branch_a = first_addition_term_mask_from_branch_a;
					step.weight_first_addition = first_addition_candidate_for_branch_b.linear_weight;

					step.round_weight = total_w;

					state.round_predecessors.push_back( step );
					trim_round_predecessors( false );
				}
			}
		}
	};

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
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask* runtime_override_mask_opt,
		const LinearBestSearchConfiguration* execution_configuration_override,
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt )
	{
		MatsuiSearchRunLinearResult result {};
		if ( checkpoint_path.empty() )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}

		LinearBestSearchContext  context {};
		LinearCheckpointLoadResult load {};
		if ( !read_linear_checkpoint( checkpoint_path, load, context.memoization ) )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		if ( load.start_mask_a != expected_output_branch_a_mask || load.start_mask_b != expected_output_branch_b_mask )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if ( !linear_configs_compatible_for_resume( expected_configuration, load.configuration ) )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}

		const LinearBestSearchConfiguration& exec_configuration =
			execution_configuration_override ? *execution_configuration_override : load.configuration;

		const TwilightDream::best_search_shared_core::StoredRuntimeMetadata stored_runtime_metadata =
			TwilightDream::best_search_shared_core::stored_runtime_metadata_for_resume_control_merge(
				load.runtime_maximum_search_nodes,
				load.runtime_maximum_search_seconds,
				load.runtime_progress_every_seconds,
				load.runtime_checkpoint_every_seconds );
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask default_runtime_override_mask {
			runtime_controls.maximum_search_nodes != 0,
			runtime_controls.maximum_search_seconds != 0,
			runtime_controls.progress_every_seconds != 0,
			runtime_controls.checkpoint_every_seconds != 0
		};
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask& runtime_override_mask =
			runtime_override_mask_opt ? *runtime_override_mask_opt : default_runtime_override_mask;
		const auto resume_runtime_plan =
			TwilightDream::best_search_shared_core::build_resume_runtime_plan(
				runtime_controls,
				stored_runtime_metadata,
				runtime_override_mask,
				load.total_nodes_visited,
				load.accumulated_elapsed_usec );
		const LinearResumeFingerprint loaded_fingerprint = compute_linear_resume_fingerprint( load );

		context.configuration = exec_configuration;
		TwilightDream::best_search_shared_core::apply_resume_runtime_plan( context, resume_runtime_plan );
		context.start_output_branch_a_mask = load.start_mask_a;
		context.start_output_branch_b_mask = load.start_mask_b;
		context.best_weight = load.best_weight;
		context.best_input_branch_a_mask = load.best_input_mask_a;
		context.best_input_branch_b_mask = load.best_input_mask_b;
		context.best_linear_trail = std::move( load.best_trail );
		context.current_linear_trail = std::move( load.current_trail );
		context.history_log_output_path = load.history_log_path;
		context.runtime_log_output_path = load.runtime_log_path;
		context.checkpoint = checkpoint;
		context.runtime_event_log = runtime_event_log;
		context.binary_checkpoint = binary_checkpoint;
		context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata {};

		if ( context.configuration.target_best_weight >= 0 &&
			 context.best_weight < INFINITE_WEIGHT &&
			 context.best_weight <= context.configuration.target_best_weight )
		{
			context.stop_due_to_target = true;
		}

		// The loaded cursor already contains the saved cLAT/split-lookup-recombine state and
		// exact sub-const state for the in-flight round, so DFS can resume at the same node.
		LinearSearchCursor cursor = std::move( load.cursor );
		const LinearResumeFingerprint materialized_fingerprint = compute_linear_resume_fingerprint( context, cursor );
		if ( materialized_fingerprint.hash != loaded_fingerprint.hash )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if ( best_search_shared_core::initialize_progress_tracking(
				 context,
				 best_search_shared_core::effective_resume_progress_interval_seconds( context, progress_reporting_opt ) ) )
		{
			context.progress_print_masks = progress_print_masks;
			if ( print_output )
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << context.progress_every_seconds << " seconds (time-check granularity ~" << ( context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		best_search_shared_core::run_resume_control_session(
			context,
			cursor,
			[ & ]( LinearBestSearchContext& ctx ) {
				best_search_shared_core::prepare_binary_checkpoint(
					ctx.binary_checkpoint,
					ctx.runtime_controls.checkpoint_every_seconds,
					true,
					checkpoint_path );
			},
			[]( LinearBestSearchContext& ctx ) {
				begin_linear_runtime_invocation( ctx );
			},
			[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
				linear_runtime_log_resume_event( ctx, resume_cursor, "resume_start" );
			},
			[]( LinearBestSearchContext& ctx, LinearSearchCursor& ) {
				if ( ctx.checkpoint )
					ctx.checkpoint->maybe_write( ctx, "resume_init" );
			},
			[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
				if ( ctx.checkpoint )
					write_linear_resume_snapshot( *ctx.checkpoint, ctx, resume_cursor, "resume_init" );
			},
			[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
				LinearBestTrailSearcherCursor searcher( ctx, resume_cursor );
				ScopedRuntimeTimeLimitProbe time_probe( ctx.runtime_controls, ctx.runtime_state );
				searcher.search_from_cursor();
			},
			[]( LinearBestSearchContext& ctx ) {
				return linear_runtime_budget_hit( ctx );
			},
			[]( LinearBestSearchContext& ctx, const char* reason ) {
				linear_runtime_log_basic_event( ctx, "checkpoint_preserved", reason );
			},
			[]( LinearBestSearchContext& ctx ) {
				if ( runtime_maximum_search_nodes_hit( ctx.runtime_controls, ctx.runtime_state ) )
					linear_runtime_log_basic_event( ctx, "resume_stop", "hit_maximum_search_nodes" );
				else if ( runtime_time_limit_hit( ctx.runtime_controls, ctx.runtime_state ) )
					linear_runtime_log_basic_event( ctx, "resume_stop", "hit_time_limit" );
				else if ( ctx.stop_due_to_target )
					linear_runtime_log_basic_event( ctx, "resume_stop", "hit_target_best_weight" );
				else
					linear_runtime_log_basic_event( ctx, "resume_stop", "completed" );
			} );

		result.nodes_visited = context.visited_node_count;
		result.hit_maximum_search_nodes = runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
		result.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
		result.hit_target_best_weight = context.stop_due_to_target;
		result.used_non_strict_branch_cap = linear_configuration_has_strict_branch_cap( context.configuration );
		result.used_target_best_weight_shortcut =
			context.configuration.target_best_weight >= 0 &&
			context.best_weight <= context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;
		result.best_input_branch_a_mask = context.best_input_branch_a_mask;
		result.best_input_branch_b_mask = context.best_input_branch_b_mask;

		const bool used_non_strict_remaining_round_bound =
			linear_configuration_uses_non_strict_remaining_round_bound( context.configuration );
		if ( context.best_linear_trail.empty() )
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_linear_best_search_strict_rejection_reason(
					result,
					context.configuration,
					used_non_strict_remaining_round_bound );
			if ( print_output )
			{
				print_linear_weight_sliced_clat_banner( context.configuration );
				std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
				if ( result.hit_maximum_search_nodes || result.hit_time_limit )
					std::cout << "[PAUSE] no trail found yet before the runtime budget expired; checkpoint/resume can continue.\n";
				else
					std::cout << "[FAIL] no trail found within limits.\n";
				std::cout << "  nodes_visited=" << result.nodes_visited;
				if ( result.hit_maximum_search_nodes )
					std::cout << "  [HIT maximum_search_nodes]";
				if ( result.hit_time_limit )
					std::cout << "  [HIT maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "]";
				if ( result.hit_target_best_weight )
					std::cout << "  [HIT target_best_weight=" << context.configuration.target_best_weight << "]";
				std::cout << "\n";
				std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( result ) ) << "\n";
				std::cout << "  exact_best_weight_certified=" << ( result.best_weight_certified ? 1 : 0 ) << "\n";
			}
			return result;
		}

		result.found = true;
		result.best_weight = context.best_weight;
		result.best_linear_trail = std::move( context.best_linear_trail );
		result.strict_rejection_reason =
			classify_linear_best_search_strict_rejection_reason(
				result,
				context.configuration,
				used_non_strict_remaining_round_bound );
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if ( print_output )
		{
			print_linear_weight_sliced_clat_banner( context.configuration );
			std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() )
					  << "  startup_memory_gate_policy=" << ( context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
			std::cout << "[OK] best_weight=" << result.best_weight << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited;
			if ( result.hit_maximum_search_nodes )
				std::cout << "  [HIT maximum_search_nodes]";
			if ( result.hit_time_limit )
				std::cout << "  [HIT maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "]";
			if ( result.hit_target_best_weight )
				std::cout << "  [HIT target_best_weight=" << context.configuration.target_best_weight << "]";
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( result ) ) << "\n";
			std::cout << "  exact_best_weight_certified=" << ( result.best_weight_certified ? 1 : 0 ) << "\n";
		}

		return result;
	}

	void linear_best_search_continue_from_cursor( LinearBestSearchContext& context, LinearSearchCursor& cursor )
	{
		LinearBestTrailSearcherCursor searcher( context, cursor );
		searcher.search_from_cursor();
	}

	MatsuiSearchRunLinearResult run_linear_best_search(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& search_configuration,
		const LinearBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_masks,
		int seeded_upper_bound_weight,
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata )
	{
		MatsuiSearchRunLinearResult result {};
		if ( search_configuration.round_count <= 0 )
			return result;

		LinearBestSearchContext context {};
		context.configuration = search_configuration;
		context.runtime_controls = runtime_controls;
		context.start_output_branch_a_mask = output_branch_a_mask;
		context.start_output_branch_b_mask = output_branch_b_mask;
		context.checkpoint = checkpoint;
		context.runtime_event_log = runtime_event_log;
		context.binary_checkpoint = binary_checkpoint;
		context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata {};
		best_search_shared_core::prepare_binary_checkpoint(
			context.binary_checkpoint,
			context.runtime_controls.checkpoint_every_seconds,
			false );

		int&								best = context.best_weight;
		std::uint32_t&						best_input_branch_a_mask = context.best_input_branch_a_mask;
		std::uint32_t&						best_input_branch_b_mask = context.best_input_branch_b_mask;
		std::vector<LinearTrailStepRecord>& best_linear_trail = context.best_linear_trail;
		std::vector<LinearTrailStepRecord>& current = context.current_linear_trail;

		current.reserve( std::size_t( search_configuration.round_count ) );

		// Runtime init (single-run).
		begin_linear_runtime_invocation( context );
		best_search_shared_core::SearchControlSession<LinearBestSearchContext> control_session( context );
		control_session.begin();
		linear_runtime_log_basic_event( context, "best_search_start" );
		context.memoization.initialize( std::size_t( search_configuration.round_count ) + 1u, search_configuration.enable_state_memoization, "linear_memo.init" );

		bool remaining_round_lower_bound_disabled_due_to_strict = false;
		bool remaining_round_lower_bound_autogenerated = false;
		bool remaining_round_lower_bound_used_non_strict = false;

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		if ( context.configuration.enable_remaining_round_lower_bound )
		{
			const bool generation_limited =
				context.configuration.auto_generate_remaining_round_lower_bound &&
				( context.configuration.remaining_round_lower_bound_generation_nodes != 0 ||
				  context.configuration.remaining_round_lower_bound_generation_seconds != 0 );
			if ( generation_limited && context.configuration.strict_remaining_round_lower_bound )
			{
				context.configuration.enable_remaining_round_lower_bound = false;
				context.configuration.remaining_round_min_weight.clear();
				remaining_round_lower_bound_disabled_due_to_strict = true;
			}
			else if ( generation_limited && context.configuration.auto_generate_remaining_round_lower_bound && context.configuration.remaining_round_min_weight.empty() )
			{
				remaining_round_lower_bound_used_non_strict = true;
			}

			auto& remaining_round_min_weight_table = context.configuration.remaining_round_min_weight;
			if ( remaining_round_min_weight_table.empty() && context.configuration.auto_generate_remaining_round_lower_bound )
			{
				remaining_round_min_weight_table =
					auto_generate_remaining_round_lower_bound_table(
						context.configuration,
						output_branch_a_mask,
						output_branch_b_mask );
				remaining_round_lower_bound_autogenerated = true;
			}

			if ( remaining_round_min_weight_table.empty() )
			{
				remaining_round_min_weight_table.assign( std::size_t( std::max( 0, search_configuration.round_count ) ) + 1u, 0 );
			}
			else
			{
				// Ensure remaining_round_min_weight_table[0] exists and is 0.
				if ( remaining_round_min_weight_table.size() < 1u )
					remaining_round_min_weight_table.resize( 1u, 0 );
				remaining_round_min_weight_table[ 0 ] = 0;
				// Pad to round_count+1 with 0 (safe lower bound).
				const std::size_t need = std::size_t( std::max( 0, search_configuration.round_count ) ) + 1u;
				if ( remaining_round_min_weight_table.size() < need )
					remaining_round_min_weight_table.resize( need, 0 );
				for ( int& round_min_weight : remaining_round_min_weight_table )
				{
					if ( round_min_weight < 0 )
						round_min_weight = 0;
				}
			}
		}

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < best )
		{
			best = seeded_upper_bound_weight;
			if ( seeded_upper_bound_trail && !seeded_upper_bound_trail->empty() )
			{
				best_linear_trail = *seeded_upper_bound_trail;
				best_input_branch_a_mask = best_linear_trail.back().input_branch_a_mask;
				best_input_branch_b_mask = best_linear_trail.back().input_branch_b_mask;
			}
		}

		// Optional: persist an initial snapshot once, even if no finite best is known yet.
		if ( checkpoint )
		{
			checkpoint->maybe_write( context, "init" );
		}

		if ( print_output )
		{
			print_linear_weight_sliced_clat_banner( search_configuration );
			std::cout << "[BestSearch] mode=matsui(injection-affine)(reverse)\n";
			std::cout << "  rounds=" << search_configuration.round_count << "  search_mode=" << search_mode_to_string( search_configuration.search_mode )
					  << "  runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds
					  << "  memo=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() )
					  << "  startup_memory_gate_policy=" << ( context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
			std::cout << "  max_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  max_round_predecessors=" << search_configuration.maximum_round_predecessors << "\n";
			if ( remaining_round_lower_bound_disabled_due_to_strict )
			{
				std::cout << "  remaining_round_lower_bound=off  reason=strict_mode_non_exhaustive_generation\n";
			}
			else if ( context.configuration.enable_remaining_round_lower_bound )
			{
				std::cout << "  remaining_round_lower_bound=on";
				if ( remaining_round_lower_bound_autogenerated )
				{
					std::cout << "  source=auto_generated";
					if ( context.configuration.remaining_round_lower_bound_generation_nodes != 0 )
						std::cout << "  gen_nodes=" << context.configuration.remaining_round_lower_bound_generation_nodes;
					if ( context.configuration.remaining_round_lower_bound_generation_seconds != 0 )
						std::cout << "  gen_seconds=" << context.configuration.remaining_round_lower_bound_generation_seconds;
				}
				std::cout << "\n";
			}
			if ( best < INFINITE_WEIGHT )
			{
				std::cout << "  seeded_upper_bound_weight=" << best << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if ( best_search_shared_core::initialize_progress_tracking( context, context.runtime_controls.progress_every_seconds ) )
		{
			context.progress_print_masks = progress_print_masks;
			if ( print_output )
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << context.progress_every_seconds << " seconds (time-check granularity ~" << ( context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		LinearSearchCursor cursor {};
		LinearBestTrailSearcherCursor searcher( context, cursor );
		searcher.start_from_initial_frame( output_branch_a_mask, output_branch_b_mask );
		ScopedRuntimeTimeLimitProbe time_probe( context.runtime_controls, context.runtime_state );
		searcher.search_from_cursor();
		control_session.finalize(
			context.binary_checkpoint,
			cursor.stack.empty(),
			linear_runtime_budget_hit( context ),
			[ & ]( const char* reason )
			{
				return context.binary_checkpoint->write_now( context, cursor, reason );
			},
			[ & ]( const char* reason )
			{
				linear_runtime_log_basic_event( context, "checkpoint_preserved", reason );
			} );
		if ( runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state ) )
			linear_runtime_log_basic_event( context, "best_search_stop", "hit_maximum_search_nodes" );
		else if ( runtime_time_limit_hit( context.runtime_controls, context.runtime_state ) )
			linear_runtime_log_basic_event( context, "best_search_stop", "hit_time_limit" );
		else if ( context.stop_due_to_target )
			linear_runtime_log_basic_event( context, "best_search_stop", "hit_target_best_weight" );
		else
			linear_runtime_log_basic_event( context, "best_search_stop", "completed" );

		result.nodes_visited = context.visited_node_count;
		result.hit_maximum_search_nodes = runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
		result.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
		result.hit_target_best_weight = context.stop_due_to_target;
		result.used_non_strict_branch_cap = linear_configuration_has_strict_branch_cap( search_configuration );
		result.used_target_best_weight_shortcut = result.hit_target_best_weight;
		result.exhaustive_completed = !result.hit_maximum_search_nodes && !result.hit_time_limit && !result.used_target_best_weight_shortcut;
		if ( best < INFINITE_WEIGHT && !best_linear_trail.empty() )
		{
			result.found = true;
			result.best_weight = best;
			result.best_linear_trail = std::move( best_linear_trail );
			result.best_input_branch_a_mask = best_input_branch_a_mask;
			result.best_input_branch_b_mask = best_input_branch_b_mask;
		}
		result.strict_rejection_reason =
			classify_linear_best_search_strict_rejection_reason(
				result,
				search_configuration,
				remaining_round_lower_bound_used_non_strict );
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;
		return result;
	}


}  // namespace TwilightDream::auto_search_linear
