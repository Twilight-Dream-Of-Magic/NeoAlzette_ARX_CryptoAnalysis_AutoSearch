#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

namespace TwilightDream::auto_search_differential
{
	// ARX Automatic Search Frame - Differential Analysis Paper:
	// Automatic Search for the Best Trails in ARX - Application to Block Cipher Speck
	// Is applied to NeoAlzette ARX-Box Algorithm every step of the round
	//
	// Mathematical wiring inside this resumable DFS cursor:
	// - `find_optimal_gamma_with_weight()` and `ModularAdditionEnumerator` are the exact
	//   LM2001 differential bridge for each var-var addition, optionally accelerated by
	//   weight-sliced pDDT shells.
	// - `SubConstEnumerator` is the exact var-const subtraction bridge.
	// - `compute_injection_transition_from_branch_a/b()` and `AffineSubspaceEnumerator`
	//   are the exact affine-difference model of the two quadratic injection layers.
	//
	// The checkpoint stores this cursor's stage/stack/enumerator state so resume continues
	// from the same in-flight search node; only rebuildable accelerator state is materialized
	// separately after loading.
	class DifferentialBestTrailSearcherCursor final
	{
	public:
		DifferentialBestTrailSearcherCursor( DifferentialBestSearchContext& context_in, DifferentialSearchCursor& cursor_in )
			: search_context( context_in ), cursor( cursor_in )
		{
		}

		void start_from_initial_frame( std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference )
		{
			cursor.stack.clear();
			search_context.current_differential_trail.clear();
			DifferentialSearchFrame frame {};
			frame.stage = DifferentialSearchStage::Enter;
			frame.trail_size_at_entry = search_context.current_differential_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.branch_a_input_difference = branch_a_input_difference;
			frame.state.branch_b_input_difference = branch_b_input_difference;
			cursor.stack.push_back( frame );
		}

		void search_from_start( std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference )
		{
			start_from_initial_frame( branch_a_input_difference, branch_b_input_difference );
			run();
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		DifferentialBestSearchContext& search_context;
		DifferentialSearchCursor&	   cursor;

		DifferentialSearchFrame& current_frame()
		{
			return cursor.stack.back();
		}

		DifferentialRoundSearchState& current_round_state()
		{
			return current_frame().state;
		}

		void pop_frame()
		{
			if ( cursor.stack.empty() )
				return;
			const std::size_t target = cursor.stack.back().trail_size_at_entry;
			if ( search_context.current_differential_trail.size() > target )
				search_context.current_differential_trail.resize( target );
			cursor.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if ( !search_context.binary_checkpoint )
				return;
			if ( best_search_shared_core::should_poll_binary_checkpoint(
					search_context.binary_checkpoint->pending_best_change(),
					search_context.binary_checkpoint->pending_runtime_request() ||
						TwilightDream::runtime_component::runtime_watchdog_checkpoint_request_pending( search_context.runtime_state ),
					search_context.run_visited_node_count,
					search_context.progress_node_mask ) )
				search_context.binary_checkpoint->poll( search_context, cursor );
		}

		void run()
		{
			while ( !cursor.stack.empty() )
			{
				DifferentialSearchFrame&	  frame = current_frame();
				DifferentialRoundSearchState& state = frame.state;

				switch ( frame.stage )
				{
				case DifferentialSearchStage::Enter:
				{
					if ( should_stop_search( state.round_boundary_depth, state.accumulated_weight_so_far ) )
					{
						if ( differential_runtime_budget_hit( search_context ) ||
							 ( search_context.configuration.target_best_weight >= 0 &&
							   search_context.best_total_weight <= search_context.configuration.target_best_weight ) )
							return;
						pop_frame();
						break;
					}
					if ( handle_round_end_if_needed( state.round_boundary_depth, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( should_prune_state_memoization( state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( !prepare_round_state( state, state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}

					frame.enum_first_add.reset( state.branch_b_input_difference, state.first_addition_term_difference, state.optimal_output_branch_b_difference_after_first_addition, state.weight_cap_first_addition );
					frame.stage = DifferentialSearchStage::FirstAdd;
					break;
				}
				case DifferentialSearchStage::FirstAdd:
				{
					std::uint32_t output_branch_b_difference_after_first_addition = 0;
					int			  weight_first_addition = 0;
					if ( !frame.enum_first_add.next( search_context, output_branch_b_difference_after_first_addition, weight_first_addition ) )
					{
						if ( frame.enum_first_add.stop_due_to_limits )
						{
							frame.enum_first_add.stop_due_to_limits = false;
							return;
						}
						else
							pop_frame();
						break;
					}

					state.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
					state.weight_first_addition = weight_first_addition;
					state.accumulated_weight_after_first_addition = state.accumulated_weight_so_far + weight_first_addition;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_first_addition ) )
						break;

					const std::uint32_t round_constant_for_first_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 1 ];
					const int			weight_cap_first_constant_subtraction =
						std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
							search_context.best_total_weight - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round );
					if ( weight_cap_first_constant_subtraction < 0 )
						break;

					frame.enum_first_const.reset( state.branch_a_input_difference, round_constant_for_first_subtraction, weight_cap_first_constant_subtraction );
					frame.stage = DifferentialSearchStage::FirstConst;
					break;
				}
				case DifferentialSearchStage::FirstConst:
				{
					std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
					int			  weight_first_constant_subtraction = 0;
					if ( !frame.enum_first_const.next( search_context, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction ) )
					{
						if ( frame.enum_first_const.stop_due_to_limits )
						{
							frame.enum_first_const.stop_due_to_limits = false;
							return;
						}
						else
							frame.stage = DifferentialSearchStage::FirstAdd;
						break;
					}

					state.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
					state.weight_first_constant_subtraction = weight_first_constant_subtraction;
					state.accumulated_weight_after_first_constant_subtraction = state.accumulated_weight_after_first_addition + weight_first_constant_subtraction;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_first_constant_subtraction ) )
						break;

					state.branch_a_difference_after_first_xor_with_rotated_branch_b =
						output_branch_a_difference_after_first_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					state.branch_b_difference_after_first_xor_with_rotated_branch_a =
						state.output_branch_b_difference_after_first_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
					state.branch_b_difference_after_first_bridge = state.branch_b_difference_after_first_xor_with_rotated_branch_a;

					// Injection bridge B->A: derive the exact affine output-difference space
					// contributed by the quadratic injection before enumerating that space.
					const InjectionAffineTransition injection_transition_from_branch_b = compute_injection_transition_from_branch_b( state.branch_b_difference_after_first_xor_with_rotated_branch_a );
					state.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
					state.accumulated_weight_before_second_addition = state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_before_second_addition ) )
						break;

					frame.enum_inj_b.reset( injection_transition_from_branch_b, search_context.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjB;
					break;
				}
				case DifferentialSearchStage::InjB:
				{
					std::uint32_t injection_from_branch_b_xor_difference = 0;
					if ( !frame.enum_inj_b.next( search_context, injection_from_branch_b_xor_difference ) )
					{
						if ( frame.enum_inj_b.stop_due_to_limits )
						{
							frame.enum_inj_b.stop_due_to_limits = false;
							return;
						}
						else
							frame.stage = DifferentialSearchStage::FirstConst;
						break;
					}

					state.injection_from_branch_b_xor_difference = injection_from_branch_b_xor_difference;
					state.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b ^ injection_from_branch_b_xor_difference;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_before_second_addition ) )
						break;

					// Second ARX bridge of the round:
					//   A_after_injB + (rotl(B,31) xor rotl(B,17))
					// with the exact LM2001 differential operator used for the add step.
					state.second_addition_term_difference =
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 31 ) ^
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 17 );

					const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
						find_optimal_gamma_with_weight( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, 32 );
					state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
					state.optimal_weight_second_addition = optimal_weight_second_addition;
					if ( state.optimal_weight_second_addition < 0 )
						break;
					if ( state.accumulated_weight_before_second_addition + state.optimal_weight_second_addition + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight )
						break;

					int weight_cap_second_addition = search_context.best_total_weight - state.accumulated_weight_before_second_addition - state.remaining_round_weight_lower_bound_after_this_round;
					weight_cap_second_addition = std::min( weight_cap_second_addition, 31 );
					weight_cap_second_addition = std::min( weight_cap_second_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
					state.weight_cap_second_addition = weight_cap_second_addition;
					if ( weight_cap_second_addition < 0 || state.optimal_weight_second_addition > weight_cap_second_addition )
						break;

					frame.enum_second_add.reset( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, state.optimal_output_branch_a_difference_after_second_addition, state.weight_cap_second_addition );
					frame.stage = DifferentialSearchStage::SecondAdd;
					break;
				}
				case DifferentialSearchStage::SecondAdd:
				{
					std::uint32_t output_branch_a_difference_after_second_addition = 0;
					int			  weight_second_addition = 0;
					if ( !frame.enum_second_add.next( search_context, output_branch_a_difference_after_second_addition, weight_second_addition ) )
					{
						if ( frame.enum_second_add.stop_due_to_limits )
						{
							frame.enum_second_add.stop_due_to_limits = false;
							return;
						}
						else
							frame.stage = DifferentialSearchStage::InjB;
						break;
					}

					state.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
					state.weight_second_addition = weight_second_addition;
					state.accumulated_weight_after_second_addition = state.accumulated_weight_before_second_addition + weight_second_addition;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_second_addition ) )
						break;

					const std::uint32_t round_constant_for_second_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[ 6 ];
					const int			weight_cap_second_constant_subtraction =
						std::min( std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 ),
							search_context.best_total_weight - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round );
					if ( weight_cap_second_constant_subtraction < 0 )
						break;

					frame.enum_second_const.reset( state.branch_b_difference_after_first_bridge, round_constant_for_second_subtraction, weight_cap_second_constant_subtraction );
					frame.stage = DifferentialSearchStage::SecondConst;
					break;
				}
				case DifferentialSearchStage::SecondConst:
				{
					std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
					int			  weight_second_constant_subtraction = 0;
					if ( !frame.enum_second_const.next( search_context, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction ) )
					{
						if ( frame.enum_second_const.stop_due_to_limits )
						{
							frame.enum_second_const.stop_due_to_limits = false;
							return;
						}
						else
							frame.stage = DifferentialSearchStage::SecondAdd;
						break;
					}

					state.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
					state.weight_second_constant_subtraction = weight_second_constant_subtraction;
					state.accumulated_weight_after_second_constant_subtraction = state.accumulated_weight_after_second_addition + weight_second_constant_subtraction;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_after_second_constant_subtraction ) )
						break;

					state.branch_b_difference_after_second_xor_with_rotated_branch_a =
						output_branch_b_difference_after_second_constant_subtraction ^ NeoAlzetteCore::rotl<std::uint32_t>( state.output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
					state.branch_a_difference_after_second_xor_with_rotated_branch_b =
						state.output_branch_a_difference_after_second_addition ^ NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

					// Injection bridge A->B: same exact affine-difference model as above,
					// now applied to the second quadratic injection layer.
					const InjectionAffineTransition injection_transition_from_branch_a = compute_injection_transition_from_branch_a( state.branch_a_difference_after_second_xor_with_rotated_branch_b );
					state.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
					state.accumulated_weight_at_round_end = state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_at_round_end ) )
						break;

					frame.enum_inj_a.reset( injection_transition_from_branch_a, search_context.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjA;
					break;
				}
				case DifferentialSearchStage::InjA:
				{
					std::uint32_t injection_from_branch_a_xor_difference = 0;
					if ( !frame.enum_inj_a.next( search_context, injection_from_branch_a_xor_difference ) )
					{
						if ( frame.enum_inj_a.stop_due_to_limits )
						{
							frame.enum_inj_a.stop_due_to_limits = false;
							return;
						}
						else
							frame.stage = DifferentialSearchStage::SecondConst;
						break;
					}

					state.injection_from_branch_a_xor_difference = injection_from_branch_a_xor_difference;
					state.output_branch_b_difference = state.branch_b_difference_after_second_xor_with_rotated_branch_a ^ injection_from_branch_a_xor_difference;
					state.output_branch_a_difference = state.branch_a_difference_after_second_xor_with_rotated_branch_b;

					if ( should_prune_with_remaining_round_lower_bound( state, state.accumulated_weight_at_round_end ) )
						break;

					DifferentialTrailStepRecord step = state.base_step;
					step.output_branch_b_difference_after_first_addition = state.output_branch_b_difference_after_first_addition;
					step.weight_first_addition = state.weight_first_addition;
					step.output_branch_a_difference_after_first_constant_subtraction = state.output_branch_a_difference_after_first_constant_subtraction;
					step.weight_first_constant_subtraction = state.weight_first_constant_subtraction;
					step.branch_a_difference_after_first_xor_with_rotated_branch_b = state.branch_a_difference_after_first_xor_with_rotated_branch_b;
					step.branch_b_difference_after_first_xor_with_rotated_branch_a = state.branch_b_difference_after_first_xor_with_rotated_branch_a;
					step.injection_from_branch_b_xor_difference = state.injection_from_branch_b_xor_difference;
					step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
					step.branch_a_difference_after_injection_from_branch_b = state.branch_a_difference_after_injection_from_branch_b;
					step.branch_b_difference_after_first_bridge = state.branch_b_difference_after_first_bridge;
					step.second_addition_term_difference = state.second_addition_term_difference;
					step.output_branch_a_difference_after_second_addition = state.output_branch_a_difference_after_second_addition;
					step.weight_second_addition = state.weight_second_addition;
					step.output_branch_b_difference_after_second_constant_subtraction = state.output_branch_b_difference_after_second_constant_subtraction;
					step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
					step.branch_b_difference_after_second_xor_with_rotated_branch_a = state.branch_b_difference_after_second_xor_with_rotated_branch_a;
					step.branch_a_difference_after_second_xor_with_rotated_branch_b = state.branch_a_difference_after_second_xor_with_rotated_branch_b;
					step.injection_from_branch_a_xor_difference = state.injection_from_branch_a_xor_difference;
					step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
					step.output_branch_a_difference = state.output_branch_a_difference;
					step.output_branch_b_difference = state.output_branch_b_difference;
					step.round_weight =
						state.weight_first_addition + state.weight_first_constant_subtraction + state.weight_injection_from_branch_b +
						state.weight_second_addition + state.weight_second_constant_subtraction + state.weight_injection_from_branch_a;

					search_context.current_differential_trail.push_back( step );

					DifferentialSearchFrame child {};
					child.stage = DifferentialSearchStage::Enter;
					// Store the parent trail size (exclude the step we just pushed) so pop_frame() removes it.
					child.trail_size_at_entry = search_context.current_differential_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
					child.state.branch_a_input_difference = state.output_branch_a_difference;
					child.state.branch_b_input_difference = state.output_branch_b_difference;
					cursor.stack.push_back( child );
					break;
				}
				}

				maybe_poll_checkpoint();
			}
		}

		// Stop conditions and global pruning (budget/time/best bound).
		bool should_stop_search( int round_boundary_depth, int accumulated_weight_so_far )
		{
			// Early stop: reached target probability (weight) already.
			if ( search_context.configuration.target_best_weight >= 0 && search_context.best_total_weight <= search_context.configuration.target_best_weight )
				return true;

			if ( search_context.stop_due_to_time_limit )
				return true;

			// Count visited nodes for progress reporting even when maximum_search_nodes is unlimited (0).
			if ( differential_note_runtime_node_visit( search_context ) )
				return true;

			if ( differential_runtime_node_limit_hit( search_context ) )
				return true;

			maybe_print_single_run_progress( search_context, round_boundary_depth );
			maybe_poll_checkpoint();
			if ( accumulated_weight_so_far > search_context.best_total_weight )
				return true;

			if ( should_prune_remaining_round_lower_bound( round_boundary_depth, accumulated_weight_so_far ) )
				return true;

			return false;
		}

		bool should_prune_remaining_round_lower_bound( int round_boundary_depth, int accumulated_weight_so_far ) const
		{
			if ( search_context.best_total_weight < INFINITE_WEIGHT && search_context.configuration.enable_remaining_round_lower_bound )
			{
				const int rounds_left = search_context.configuration.round_count - round_boundary_depth;
				if ( rounds_left >= 0 )
				{
					const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
					const std::size_t table_index = std::size_t( rounds_left );
					if ( table_index < remaining_round_min_weight_table.size() )
					{
						const int weight_lower_bound = remaining_round_min_weight_table[ table_index ];
						if ( accumulated_weight_so_far + weight_lower_bound >= search_context.best_total_weight )
							return true;
					}
				}
			}
			return false;
		}

		bool handle_round_end_if_needed( int round_boundary_depth, int accumulated_weight_so_far )
		{
			if ( round_boundary_depth != search_context.configuration.round_count )
				return false;

			if ( accumulated_weight_so_far < search_context.best_total_weight || search_context.best_differential_trail.empty() )
			{
				const int old = search_context.best_total_weight;
				search_context.best_total_weight = accumulated_weight_so_far;
				search_context.best_differential_trail = search_context.current_differential_trail;
				if ( search_context.checkpoint && accumulated_weight_so_far != old )
					search_context.checkpoint->maybe_write( search_context, "improved" );
				if ( search_context.binary_checkpoint && accumulated_weight_so_far != old )
					search_context.binary_checkpoint->mark_best_changed();
			}
			return true;
		}

		bool should_prune_state_memoization( int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			if ( !search_context.configuration.enable_state_memoization )
				return false;

			const std::size_t hint = differential_runtime_memo_reserve_hint( search_context );

			const std::uint64_t key = pack_two_word32_differences( branch_a_input_difference, branch_b_input_difference );
			return search_context.memoization.should_prune_and_update( std::size_t( round_boundary_depth ), key, accumulated_weight_so_far, true, true, hint, 192ull, "memoization.reserve", "memoization.try_emplace" );
		}

		int compute_remaining_round_weight_lower_bound_after_this_round( int round_boundary_depth ) const
		{
			if ( !search_context.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left_after = search_context.configuration.round_count - ( round_boundary_depth + 1 );
			if ( rounds_left_after < 0 )
				return 0;
			const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t( rounds_left_after );
			if ( idx >= remaining_round_min_weight_table.size() )
				return 0;
			return std::max( 0, remaining_round_min_weight_table[ idx ] );
		}

		bool should_prune_with_remaining_round_lower_bound( const DifferentialRoundSearchState& state, int accumulated_weight ) const
		{
			return accumulated_weight + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight;
		}

		bool prepare_round_state( DifferentialRoundSearchState& state, int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, int accumulated_weight_so_far )
		{
			state.round_boundary_depth = round_boundary_depth;
			state.accumulated_weight_so_far = accumulated_weight_so_far;
			state.branch_a_input_difference = branch_a_input_difference;
			state.branch_b_input_difference = branch_b_input_difference;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( round_boundary_depth );

			state.base_step = DifferentialTrailStepRecord {};
			state.base_step.round_index = round_boundary_depth + 1;
			state.base_step.input_branch_a_difference = branch_a_input_difference;
			state.base_step.input_branch_b_difference = branch_b_input_difference;

			// First ARX bridge of the round:
			//   B + (rotl(A,31) xor rotl(A,17))
			// The exact LM2001 operator first gives the best possible output-difference weight,
			// then `ModularAdditionEnumerator` enumerates the full shell under that cap.
			state.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			state.base_step.first_addition_term_difference = state.first_addition_term_difference;

			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, state.first_addition_term_difference, 32 );
			state.optimal_output_branch_b_difference_after_first_addition = optimal_output_branch_b_difference_after_first_addition;
			state.optimal_weight_first_addition = optimal_weight_first_addition;
			if ( state.optimal_weight_first_addition < 0 )
				return false;

			int weight_cap_first_addition = search_context.best_total_weight - accumulated_weight_so_far - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_first_addition = std::min( weight_cap_first_addition, 31 );
			weight_cap_first_addition = std::min( weight_cap_first_addition, std::clamp( search_context.configuration.addition_weight_cap, 0, 31 ) );
			state.weight_cap_first_addition = weight_cap_first_addition;
			if ( weight_cap_first_addition < 0 || state.optimal_weight_first_addition > weight_cap_first_addition )
				return false;

			return true;
		}
	};

	void continue_differential_best_search_from_cursor( DifferentialBestSearchContext& search_context, DifferentialSearchCursor& cursor )
	{
		DifferentialBestTrailSearcherCursor searcher( search_context, cursor );
		searcher.search_from_cursor();
	}

	MatsuiSearchRunDifferentialResult run_differential_best_search(
		int round_count,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_differences,
		int seeded_upper_bound_weight,
		const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata )
	{
		MatsuiSearchRunDifferentialResult result {};
		DifferentialBestSearchContext search_context {};
		search_context.configuration = input_search_configuration;
		search_context.runtime_controls = runtime_controls;
		search_context.configuration.round_count = round_count;
		search_context.configuration.addition_weight_cap = std::clamp( search_context.configuration.addition_weight_cap, 0, 31 );
		search_context.configuration.constant_subtraction_weight_cap = std::clamp( search_context.configuration.constant_subtraction_weight_cap, 0, 32 );
		configure_weight_sliced_pddt_cache_for_run(
			search_context.configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes() );
		search_context.start_difference_a = initial_branch_a_difference;
		search_context.start_difference_b = initial_branch_b_difference;
		search_context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata {};
		search_context.checkpoint = checkpoint;
		search_context.runtime_event_log = runtime_event_log;
		search_context.binary_checkpoint = binary_checkpoint;
		begin_differential_runtime_invocation( search_context );
		best_search_shared_core::prepare_binary_checkpoint(
			search_context.binary_checkpoint,
			search_context.runtime_controls.checkpoint_every_seconds,
			false );
		best_search_shared_core::SearchControlSession<DifferentialBestSearchContext> control_session( search_context );
		control_session.begin();
		differential_runtime_log_basic_event( search_context, "best_search_start" );
		search_context.memoization.initialize( ( round_count > 0 ) ? std::size_t( round_count ) : 0u, search_context.configuration.enable_state_memoization, "memoization.init" );

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		if ( search_context.configuration.enable_remaining_round_lower_bound )
		{
			auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			if ( remaining_round_min_weight_table.empty() )
			{
				remaining_round_min_weight_table.assign( std::size_t( std::max( 0, round_count ) ) + 1u, 0 );
			}
			else
			{
				// Ensure remaining_round_min_weight_table[0] exists and is 0.
				if ( remaining_round_min_weight_table.size() < 1u )
					remaining_round_min_weight_table.resize( 1u, 0 );
				remaining_round_min_weight_table[ 0 ] = 0;
				// Pad to round_count+1 with 0 (safe lower bound).
				const std::size_t need = std::size_t( std::max( 0, round_count ) ) + 1u;
				if ( remaining_round_min_weight_table.size() < need )
					remaining_round_min_weight_table.resize( need, 0 );
				for ( int& round_min_weight : remaining_round_min_weight_table )
				{
					if ( round_min_weight < 0 )
						round_min_weight = 0;
				}
			}
		}
		// initial upper bound (greedy)
		search_context.best_total_weight = compute_greedy_upper_bound_weight( search_context.configuration, initial_branch_a_difference, initial_branch_b_difference );
		if ( search_context.best_total_weight >= INFINITE_WEIGHT )
			search_context.best_total_weight = INFINITE_WEIGHT;

		// Seed best_trail with an explicit greedy construction to avoid false [FAIL] when DFS hits max_nodes early.
		{
			int	 initial_weight = INFINITE_WEIGHT;
			auto gtrail = construct_greedy_initial_differential_trail( search_context.configuration, initial_branch_a_difference, initial_branch_b_difference, initial_weight );
			if ( !gtrail.empty() && initial_weight < INFINITE_WEIGHT )
			{
				search_context.best_total_weight = initial_weight;
				search_context.best_differential_trail = std::move( gtrail );
			}
		}

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < search_context.best_total_weight )
		{
			search_context.best_total_weight = seeded_upper_bound_weight;
			if ( seeded_upper_bound_trail && !seeded_upper_bound_trail->empty() )
			{
				search_context.best_differential_trail = *seeded_upper_bound_trail;
			}
		}

		// Persistence (auto mode): record the initial best (greedy/seeded) once.
		if ( search_context.checkpoint )
		{
			search_context.checkpoint->maybe_write( search_context, "init" );
		}

		if ( print_output )
		{
			std::cout << "[BestSearch] mode=matsui(injection-affine)\n";
			std::cout << "  rounds=" << round_count << "  addition_weight_cap=" << search_context.configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_context.configuration.constant_subtraction_weight_cap << "  maximum_transition_output_differences=" << search_context.configuration.maximum_transition_output_differences << "  runtime_maximum_search_nodes=" << search_context.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "  memo=" << ( search_context.configuration.enable_state_memoization ? "on" : "off" ) << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() )
					  << "  startup_memory_gate_policy=" << ( search_context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
			std::cout << "  weight_sliced_pddt=" << ( search_context.configuration.enable_weight_sliced_pddt ? "on" : "off" )
					  << "  weight_sliced_pddt_max_weight=" << search_context.configuration.weight_sliced_pddt_max_weight << "\n";
			std::cout << "  greedy_upper_bound_weight=" << ( search_context.best_total_weight >= INFINITE_WEIGHT ? -1 : search_context.best_total_weight ) << "\n";
			if ( seeded_upper_bound_weight >= 0 && seeded_upper_bound_weight < INFINITE_WEIGHT )
			{
				std::cout << "  seeded_upper_bound_weight=" << seeded_upper_bound_weight << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if ( best_search_shared_core::initialize_progress_tracking( search_context, search_context.runtime_controls.progress_every_seconds ) )
		{
			search_context.progress_print_differences = progress_print_differences;
			if ( print_output )
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << search_context.progress_every_seconds << " seconds (time-check granularity ~" << ( search_context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		DifferentialSearchCursor cursor {};
		DifferentialBestTrailSearcherCursor searcher( search_context, cursor );
		searcher.start_from_initial_frame( initial_branch_a_difference, initial_branch_b_difference );
		searcher.search_from_cursor();
		control_session.finalize(
			search_context.binary_checkpoint,
			cursor.stack.empty(),
			differential_runtime_budget_hit( search_context ),
			[ & ]( const char* reason )
			{
				return search_context.binary_checkpoint->write_now( search_context, cursor, reason );
			},
			[ & ]( const char* reason )
			{
				differential_runtime_log_basic_event( search_context, "checkpoint_preserved", reason );
			} );
		if ( runtime_maximum_search_nodes_hit( search_context.runtime_controls, search_context.runtime_state ) )
			differential_runtime_log_basic_event( search_context, "best_search_stop", "hit_maximum_search_nodes" );
		else if ( runtime_time_limit_hit( search_context.runtime_controls, search_context.runtime_state ) )
			differential_runtime_log_basic_event( search_context, "best_search_stop", "hit_time_limit" );
		else
			differential_runtime_log_basic_event( search_context, "best_search_stop", "completed" );

		result.nodes_visited = search_context.visited_node_count;
		result.hit_maximum_search_nodes = runtime_maximum_search_nodes_hit( search_context.runtime_controls, search_context.runtime_state );
		result.hit_time_limit = runtime_time_limit_hit( search_context.runtime_controls, search_context.runtime_state );
		result.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap( search_context.configuration );
		result.used_target_best_weight_shortcut =
			search_context.configuration.target_best_weight >= 0 &&
			search_context.best_total_weight <= search_context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;

		if ( search_context.best_differential_trail.empty() )
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_differential_best_search_strict_rejection_reason(
					result,
					search_context.configuration );
			result.best_weight_certified = false;
			if ( print_output )
			{
				if ( result.hit_maximum_search_nodes || result.hit_time_limit )
					std::cout << "[PAUSE] No trail found yet before the runtime budget expired; checkpoint/resume can continue.\n";
				else
					std::cout << "[FAIL] No trail found within limits.\n";
			}
			return result;
		}

		result.found = true;
		result.best_weight = search_context.best_total_weight;
		result.best_trail = std::move( search_context.best_differential_trail );
		result.strict_rejection_reason =
			classify_differential_best_search_strict_rejection_reason(
				result,
				search_context.configuration );
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if ( print_output )
		{
			std::cout << "[OK] best_weight=" << result.best_weight << "  (DP ~= 2^-" << result.best_weight << ")\n";
			std::cout << "  approx_DP=" << std::setprecision( 10 ) << weight_to_probability( result.best_weight ) << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited << ( result.hit_maximum_search_nodes ? "  [HIT maximum_search_nodes]" : "" );
			if ( result.hit_time_limit )
			{
				std::cout << "  [HIT maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "]";
			}
			if ( search_context.configuration.target_best_weight >= 0 && result.best_weight <= search_context.configuration.target_best_weight )
			{
				std::cout << "  [HIT target_best_weight=" << search_context.configuration.target_best_weight << "]";
			}
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( result ) ) << "\n";
			std::cout << "  exact_best_weight_certified=" << ( result.best_weight_certified ? 1 : 0 ) << "\n\n";

			for ( const auto& s : result.best_trail )
			{
				std::cout << "R" << s.round_index << "  round_weight=" << s.round_weight << "  weight_first_addition=" << s.weight_first_addition << "  weight_first_constant_subtraction=" << s.weight_first_constant_subtraction << "  weight_injection_from_branch_b=" << s.weight_injection_from_branch_b << "  weight_second_addition=" << s.weight_second_addition << "  weight_second_constant_subtraction=" << s.weight_second_constant_subtraction << "  weight_injection_from_branch_a=" << s.weight_injection_from_branch_a << "\n";
				print_word32_hex( "  input_branch_a_difference=", s.input_branch_a_difference );
				std::cout << "  ";
				print_word32_hex( "input_branch_b_difference=", s.input_branch_b_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_b_difference_after_first_addition=", s.output_branch_b_difference_after_first_addition );
				std::cout << "  ";
				print_word32_hex( "first_addition_term_difference=", s.first_addition_term_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_a_difference_after_first_constant_subtraction=", s.output_branch_a_difference_after_first_constant_subtraction );
				std::cout << "  ";
				print_word32_hex( "branch_a_difference_after_first_xor_with_rotated_branch_b=", s.branch_a_difference_after_first_xor_with_rotated_branch_b );
				std::cout << "\n";

				print_word32_hex( "  injection_from_branch_b_xor_difference=", s.injection_from_branch_b_xor_difference );
				std::cout << "  ";
				print_word32_hex( "branch_a_difference_after_injection_from_branch_b=", s.branch_a_difference_after_injection_from_branch_b );
				std::cout << "\n";

				print_word32_hex( "  branch_b_difference_after_first_bridge=", s.branch_b_difference_after_first_bridge );
				std::cout << "  ";
				print_word32_hex( "second_addition_term_difference=", s.second_addition_term_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_b_difference_after_second_constant_subtraction=", s.output_branch_b_difference_after_second_constant_subtraction );
				std::cout << "  ";
				print_word32_hex( "branch_b_difference_after_second_xor_with_rotated_branch_a=", s.branch_b_difference_after_second_xor_with_rotated_branch_a );
				std::cout << "\n";

				print_word32_hex( "  injection_from_branch_a_xor_difference=", s.injection_from_branch_a_xor_difference );
				std::cout << "  ";
				print_word32_hex( "output_branch_b_difference=", s.output_branch_b_difference );
				std::cout << "\n";

				print_word32_hex( "  output_branch_a_difference=", s.output_branch_a_difference );
				std::cout << "  ";
				print_word32_hex( "output_branch_b_difference=", s.output_branch_b_difference );
				std::cout << "\n";
			}
		}
		return result;
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
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask* runtime_override_mask_opt,
		const DifferentialBestSearchConfiguration* execution_configuration_override,
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt )
	{
		MatsuiSearchRunDifferentialResult result {};
		if ( checkpoint_path.empty() )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}

		DifferentialBestSearchConfiguration resolved_expected_configuration = expected_configuration;
		configure_weight_sliced_pddt_cache_for_run(
			resolved_expected_configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes() );

		DifferentialBestSearchContext search_context {};
		DifferentialCheckpointLoadResult load {};
		if ( !read_differential_checkpoint( checkpoint_path, load, search_context.memoization ) )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		if ( load.start_difference_a != expected_start_difference_a || load.start_difference_b != expected_start_difference_b )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if ( !differential_configs_compatible_for_resume( resolved_expected_configuration, load.configuration ) )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}

		DifferentialBestSearchConfiguration exec_configuration =
			execution_configuration_override ? *execution_configuration_override : load.configuration;
		configure_weight_sliced_pddt_cache_for_run(
			exec_configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes() );

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
		const DifferentialResumeFingerprint loaded_fingerprint = compute_differential_resume_fingerprint( load );

		search_context.configuration = std::move( exec_configuration );
		TwilightDream::best_search_shared_core::apply_resume_runtime_plan( search_context, resume_runtime_plan );
		search_context.start_difference_a = load.start_difference_a;
		search_context.start_difference_b = load.start_difference_b;
		search_context.history_log_output_path = load.history_log_path;
		search_context.runtime_log_output_path = load.runtime_log_path;
		search_context.best_total_weight = load.best_total_weight;
		search_context.best_differential_trail = std::move( load.best_trail );
		search_context.current_differential_trail = std::move( load.current_trail );
		search_context.checkpoint = checkpoint;
		search_context.runtime_event_log = runtime_event_log;
		search_context.binary_checkpoint = binary_checkpoint;
		search_context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata {};
		DifferentialSearchCursor cursor = std::move( load.cursor );
		// The binary checkpoint already restored trail/cursor/enumerator positions.
		// This step only reconstructs accelerator state that is declared rebuildable,
		// so resume continues from the stored DFS node rather than restarting the round.
		if ( !materialize_differential_resume_rebuildable_state( search_context, cursor ) )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		const DifferentialResumeFingerprint materialized_fingerprint = compute_differential_resume_fingerprint( search_context, cursor );
		if ( materialized_fingerprint.hash != loaded_fingerprint.hash )
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}

		if ( best_search_shared_core::initialize_progress_tracking(
				 search_context,
				 best_search_shared_core::effective_resume_progress_interval_seconds( search_context, progress_reporting_opt ) ) )
		{
			search_context.progress_print_differences = progress_print_differences;
			if ( print_output )
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Progress] enabled: every " << search_context.progress_every_seconds << " seconds (time-check granularity ~" << ( search_context.progress_node_mask + 1 ) << " nodes)\n\n";
			}
		}

		best_search_shared_core::run_resume_control_session(
			search_context,
			cursor,
			[ & ]( DifferentialBestSearchContext& ctx ) {
				best_search_shared_core::prepare_binary_checkpoint(
					ctx.binary_checkpoint,
					ctx.runtime_controls.checkpoint_every_seconds,
					true,
					checkpoint_path );
			},
			[]( DifferentialBestSearchContext& ctx ) {
				begin_differential_runtime_invocation( ctx );
			},
			[]( DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor ) {
				differential_runtime_log_resume_event( ctx, resume_cursor, "resume_start" );
			},
			[]( DifferentialBestSearchContext& ctx, DifferentialSearchCursor& ) {
				if ( ctx.checkpoint && ctx.best_total_weight < INFINITE_WEIGHT && !ctx.best_differential_trail.empty() )
					ctx.checkpoint->maybe_write( ctx, "resume_init" );
			},
			[]( DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor ) {
				if ( ctx.checkpoint )
					write_differential_resume_snapshot( *ctx.checkpoint, ctx, resume_cursor, "resume_init" );
			},
			[]( DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor ) {
				continue_differential_best_search_from_cursor( ctx, resume_cursor );
			},
			[]( DifferentialBestSearchContext& ctx ) {
				return differential_runtime_budget_hit( ctx );
			},
			[]( DifferentialBestSearchContext& ctx, const char* reason ) {
				differential_runtime_log_basic_event( ctx, "checkpoint_preserved", reason );
			},
			[]( DifferentialBestSearchContext& ctx ) {
				if ( runtime_maximum_search_nodes_hit( ctx.runtime_controls, ctx.runtime_state ) )
					differential_runtime_log_basic_event( ctx, "resume_stop", "hit_maximum_search_nodes" );
				else if ( runtime_time_limit_hit( ctx.runtime_controls, ctx.runtime_state ) )
					differential_runtime_log_basic_event( ctx, "resume_stop", "hit_time_limit" );
				else
					differential_runtime_log_basic_event( ctx, "resume_stop", "completed" );
			} );

		result.nodes_visited = search_context.visited_node_count;
		result.hit_maximum_search_nodes = runtime_maximum_search_nodes_hit( search_context.runtime_controls, search_context.runtime_state );
		result.hit_time_limit = runtime_time_limit_hit( search_context.runtime_controls, search_context.runtime_state );
		result.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap( search_context.configuration );
		result.used_target_best_weight_shortcut =
			search_context.configuration.target_best_weight >= 0 &&
			search_context.best_total_weight <= search_context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;

		if ( search_context.best_differential_trail.empty() )
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_differential_best_search_strict_rejection_reason(
					result,
					search_context.configuration );
			return result;
		}

		result.found = true;
		result.best_weight = search_context.best_total_weight;
		result.best_trail = std::move( search_context.best_differential_trail );
		result.strict_rejection_reason =
			classify_differential_best_search_strict_rejection_reason(
				result,
				search_context.configuration );
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if ( print_output )
		{
			std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() )
					  << "  startup_memory_gate_policy=" << ( search_context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
			std::cout << "[OK] best_weight=" << result.best_weight << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited;
			if ( result.hit_maximum_search_nodes )
				std::cout << "  [HIT maximum_search_nodes]";
			if ( result.hit_time_limit )
				std::cout << "  [HIT maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "]";
			if ( result.used_target_best_weight_shortcut )
				std::cout << "  [HIT target_best_weight=" << search_context.configuration.target_best_weight << "]";
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( result ) ) << "\n";
			std::cout << "  exact_best_weight_certified=" << ( result.best_weight_certified ? 1 : 0 ) << "\n";
		}

		return result;
	}

}  // namespace TwilightDream::auto_search_differential
