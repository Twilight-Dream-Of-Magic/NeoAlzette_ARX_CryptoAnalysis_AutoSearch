#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/remaining_round_lower_bound_bootstrap.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"

#include <new>

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		enum class LinearResidualStageCursor : std::uint8_t
		{
			InjA = 0,
			SecondAdd = 1,
			InjB = 2,
			SecondConst = 3,
			FirstSubconst = 4,
			FirstAdd = 5,
			RoundEnd = 6
		};

		std::uint8_t normalize_linear_residual_stage_cursor( LinearSearchStage stage ) noexcept
		{
			switch ( stage )
			{
			case LinearSearchStage::Enter:
			case LinearSearchStage::Enumerate:
			case LinearSearchStage::Recurse:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::InjA );
			case LinearSearchStage::InjA:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::InjA );
			case LinearSearchStage::SecondAdd:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::SecondAdd );
			case LinearSearchStage::InjB:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::InjB );
			case LinearSearchStage::SecondConst:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::SecondConst );
			case LinearSearchStage::FirstSubconst:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::FirstSubconst );
			case LinearSearchStage::FirstAdd:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::FirstAdd );
			default:
				return static_cast<std::uint8_t>( LinearResidualStageCursor::RoundEnd );
			}
		}

		inline std::uint32_t linear_cross_xor_mask_from_addition_term( std::uint32_t term_mask ) noexcept
		{
			return NeoAlzetteCore::rotr( term_mask, 31 ) ^ NeoAlzetteCore::rotr( term_mask, 17 );
		}

		inline std::uint32_t linear_backward_mask_after_cross_xor_rot_r1_on_branch_b(
			std::uint32_t branch_b_mask_before_xor,
			std::uint32_t branch_a_mask_after_xor ) noexcept
		{
			// Explicit zero-cost reverse step for:
			//   B ^= rotl(A, CROSS_XOR_ROT_R1)
			return branch_b_mask_before_xor ^
				NeoAlzetteCore::rotr(
					branch_a_mask_after_xor,
					NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		}

		inline std::uint32_t linear_backward_mask_after_cross_xor_rot_r0_on_branch_a(
			std::uint32_t branch_a_mask_before_xor,
			std::uint32_t branch_b_mask_after_xor ) noexcept
		{
			// Explicit zero-cost reverse step for:
			//   A ^= rotl(B, CROSS_XOR_ROT_R0)
			return branch_a_mask_before_xor ^
				NeoAlzetteCore::rotr(
					branch_b_mask_after_xor,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		}

		inline std::uint32_t linear_mask_after_explicit_prewhitening_before_injection_from_branch_a(
			std::uint32_t branch_b_output_mask_after_injection ) noexcept
		{
			// Explicit split step:
			//   A_pre = A_raw xor RC[9]
			// Absolute-correlation mask transport through xor-by-constant is identity.
			return branch_b_output_mask_after_injection;
		}

		inline std::uint32_t linear_mask_after_explicit_prewhitening_before_injection_from_branch_b(
			std::uint32_t branch_a_output_mask_after_injection ) noexcept
		{
			// Explicit split step:
			//   B_pre = B_raw xor RC[4]
			// Absolute-correlation mask transport through xor-by-constant is identity.
			return branch_a_output_mask_after_injection;
		}

		inline void linear_prepare_round_entry_bridge(
			LinearRoundSearchState& state,
			std::uint32_t current_round_output_branch_a_mask,
			std::uint32_t current_round_output_branch_b_mask )
		{
			state.round_output_branch_a_mask = current_round_output_branch_a_mask;
			state.round_output_branch_b_mask = current_round_output_branch_b_mask;
			state.branch_a_round_output_mask_before_inj_from_a = current_round_output_branch_a_mask;
			state.branch_b_mask_before_injection_from_branch_a = current_round_output_branch_b_mask;
			const std::uint32_t branch_a_mask_after_explicit_prewhitening_before_injection =
				linear_mask_after_explicit_prewhitening_before_injection_from_branch_a(
					current_round_output_branch_b_mask );
			state.injection_from_branch_a_transition =
				compute_injection_transition_from_branch_a(
					branch_a_mask_after_explicit_prewhitening_before_injection );
			state.weight_injection_from_branch_a = state.injection_from_branch_a_transition.weight;
		}

		inline void linear_project_second_subround_outputs_from_inj_a_choice( LinearRoundSearchState& state )
		{
			const std::uint32_t branch_a_mask_after_undoing_injection_from_branch_a =
				state.branch_a_round_output_mask_before_inj_from_a ^
				state.chosen_correlated_input_mask_for_injection_from_branch_a;
			const std::uint32_t branch_b_mask_after_second_xor_with_rotated_branch_a =
				linear_backward_mask_after_cross_xor_rot_r1_on_branch_b(
					state.branch_b_mask_before_injection_from_branch_a,
					branch_a_mask_after_undoing_injection_from_branch_a );
			state.output_branch_b_mask_after_second_constant_subtraction =
				branch_b_mask_after_second_xor_with_rotated_branch_a;
			state.output_branch_a_mask_after_second_addition =
				linear_backward_mask_after_cross_xor_rot_r0_on_branch_a(
					branch_a_mask_after_undoing_injection_from_branch_a,
					branch_b_mask_after_second_xor_with_rotated_branch_a );
		}

		inline void linear_load_second_add_candidate_state(
			LinearRoundSearchState& state,
			const LinearVarVarQ2Candidate& q2_candidate,
			SearchWeight weight_second_addition )
		{
			state.weight_second_addition = weight_second_addition;
			state.second_add_trail_sum_wire_u = q2_candidate.sum_wire_u;
			state.input_branch_a_mask_before_second_addition = q2_candidate.q1_input.input_mask_x;
			state.second_addition_term_mask_from_branch_b = q2_candidate.q1_input.input_mask_y;
			state.branch_b_mask_contribution_from_second_addition_term =
				linear_cross_xor_mask_from_addition_term( state.second_addition_term_mask_from_branch_b );
		}

		inline void linear_prepare_inj_b_bridge_after_second_add( LinearRoundSearchState& state )
		{
			const std::uint32_t branch_b_mask_after_explicit_prewhitening_before_injection =
				linear_mask_after_explicit_prewhitening_before_injection_from_branch_b(
					state.input_branch_a_mask_before_second_addition );
			state.injection_from_branch_b_transition =
				compute_injection_transition_from_branch_b(
					branch_b_mask_after_explicit_prewhitening_before_injection );
			state.weight_injection_from_branch_b = state.injection_from_branch_b_transition.weight;
			state.base_weight_after_inj_b =
				state.weight_injection_from_branch_a +
				state.weight_second_addition +
				state.weight_injection_from_branch_b;
		}

		inline void linear_load_second_subconst_candidate_state(
			LinearRoundSearchState& state,
			const LinearVarConstQ2Candidate& q2_candidate,
			SearchWeight weight_second_constant_subtraction )
		{
			state.weight_second_constant_subtraction = weight_second_constant_subtraction;
			state.input_branch_b_mask_before_second_constant_subtraction = q2_candidate.input_mask_alpha;
			state.branch_b_mask_after_second_add_term_removed =
				state.input_branch_b_mask_before_second_constant_subtraction ^
				state.branch_b_mask_contribution_from_second_addition_term;
			state.branch_b_mask_after_first_xor_with_rotated_branch_a_base =
				state.branch_b_mask_after_second_add_term_removed;
			state.base_weight_after_second_subconst =
				state.base_weight_after_inj_b + state.weight_second_constant_subtraction;
		}

		inline void linear_project_first_subround_outputs_from_inj_b_choice( LinearRoundSearchState& state )
		{
			const std::uint32_t branch_b_mask_after_undoing_injection_from_branch_b =
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base ^
				state.chosen_correlated_input_mask_for_injection_from_branch_b;
			state.output_branch_a_mask_after_first_constant_subtraction =
				linear_backward_mask_after_cross_xor_rot_r1_on_branch_b(
					state.input_branch_a_mask_before_second_addition,
					branch_b_mask_after_undoing_injection_from_branch_b );
			state.output_branch_b_mask_after_first_addition =
				linear_backward_mask_after_cross_xor_rot_r0_on_branch_a(
					branch_b_mask_after_undoing_injection_from_branch_b,
					state.output_branch_a_mask_after_first_constant_subtraction );
		}

		inline bool linear_prepare_first_subround_caps(
			LinearRoundSearchState& state,
			const LinearBestSearchConfiguration& search_configuration ) noexcept
		{
			const SearchWeight remaining_after_second_subconst =
				state.round_weight_cap - state.base_weight_after_second_subconst;
			if ( remaining_after_second_subconst == 0 )
				return false;
			state.first_subconst_weight_cap =
				std::min(
					search_configuration.constant_subtraction_weight_cap,
					remaining_after_second_subconst - 1 );
			state.first_add_weight_cap =
				std::min(
					search_configuration.addition_weight_cap,
					remaining_after_second_subconst - 1 );
			return true;
		}

		inline std::uint32_t linear_compute_round_input_branch_a_mask(
			std::uint32_t input_branch_a_mask_before_first_constant_subtraction,
			std::uint32_t first_addition_term_mask_from_branch_a ) noexcept
		{
			return input_branch_a_mask_before_first_constant_subtraction ^
				linear_cross_xor_mask_from_addition_term( first_addition_term_mask_from_branch_a );
		}

		inline void linear_fill_round_step_common_fields(
			LinearTrailStepRecord& step,
			const LinearRoundSearchState& state ) noexcept
		{
			step.round_index = state.round_index;
			step.output_branch_a_mask = state.round_output_branch_a_mask;
			step.output_branch_b_mask = state.round_output_branch_b_mask;
			step.output_branch_b_mask_after_second_constant_subtraction =
				state.output_branch_b_mask_after_second_constant_subtraction;
			step.input_branch_b_mask_before_second_constant_subtraction =
				state.input_branch_b_mask_before_second_constant_subtraction;
			step.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
			step.output_branch_a_mask_after_second_addition = state.second_add_trail_sum_wire_u;
			step.input_branch_a_mask_before_second_addition = state.input_branch_a_mask_before_second_addition;
			step.second_addition_term_mask_from_branch_b = state.second_addition_term_mask_from_branch_b;
			step.weight_second_addition = state.weight_second_addition;
			step.weight_injection_from_branch_a = state.weight_injection_from_branch_a;
			step.weight_injection_from_branch_b = state.weight_injection_from_branch_b;
			step.chosen_correlated_input_mask_for_injection_from_branch_a =
				state.chosen_correlated_input_mask_for_injection_from_branch_a;
			step.chosen_correlated_input_mask_for_injection_from_branch_b =
				state.chosen_correlated_input_mask_for_injection_from_branch_b;
			step.branch_a_mask_after_undoing_injection_from_branch_a =
				state.output_branch_a_mask_after_second_addition ^
				NeoAlzetteCore::rotr(
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			step.branch_b_mask_before_undoing_injection_from_branch_b =
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base;
			step.branch_b_mask_after_undoing_injection_from_branch_b =
				state.branch_b_mask_after_first_xor_with_rotated_branch_a_base ^
				state.chosen_correlated_input_mask_for_injection_from_branch_b;
			step.output_branch_a_mask_after_first_constant_subtraction =
				state.output_branch_a_mask_after_first_constant_subtraction;
		}

		inline LinearTrailStepRecord linear_build_round_step(
			const LinearRoundSearchState& state,
			std::uint32_t input_branch_a_mask_before_first_constant_subtraction,
			SearchWeight weight_first_constant_subtraction,
			const LinearVarVarQ2Candidate& first_add_q2,
			SearchWeight weight_first_addition )
		{
			LinearTrailStepRecord step {};
			linear_fill_round_step_common_fields( step, state );
			step.input_branch_a_mask_before_first_constant_subtraction =
				input_branch_a_mask_before_first_constant_subtraction;
			step.weight_first_constant_subtraction = weight_first_constant_subtraction;
			step.output_branch_b_mask_after_first_addition = state.first_add_trail_sum_wire_u;
			step.input_branch_b_mask_before_first_addition = first_add_q2.q1_input.input_mask_x;
			step.first_addition_term_mask_from_branch_a = first_add_q2.q1_input.input_mask_y;
			step.weight_first_addition = weight_first_addition;
			step.input_branch_a_mask =
				linear_compute_round_input_branch_a_mask(
					input_branch_a_mask_before_first_constant_subtraction,
					first_add_q2.q1_input.input_mask_y );
			step.input_branch_b_mask = first_add_q2.q1_input.input_mask_x;
			step.round_weight =
				state.base_weight_after_second_subconst +
				weight_first_constant_subtraction +
				weight_first_addition;
			return step;
		}

		struct LinearResidualSemanticKeyParts
		{
			std::uint32_t pair_a = 0;
			std::uint32_t pair_b = 0;
			std::uint32_t pair_c = 0;
		};

		inline LinearResidualSemanticKeyParts linear_semantic_key_for_stage(
			LinearSearchStage stage,
			const LinearRoundSearchState& state ) noexcept
		{
			switch ( stage )
			{
			case LinearSearchStage::Enter:
			case LinearSearchStage::InjA:
				return { state.round_output_branch_a_mask, state.round_output_branch_b_mask, 0u };
			case LinearSearchStage::SecondAdd:
				return {
					state.output_branch_a_mask_after_second_addition,
					state.output_branch_b_mask_after_second_constant_subtraction,
					0u };
			case LinearSearchStage::InjB:
				return {
					state.second_add_trail_sum_wire_u,
					state.input_branch_a_mask_before_second_addition,
					state.second_addition_term_mask_from_branch_b };
			case LinearSearchStage::SecondConst:
				return {
					state.input_branch_a_mask_before_second_addition,
					state.branch_b_mask_contribution_from_second_addition_term,
					0u };
			case LinearSearchStage::FirstSubconst:
				return {
					state.input_branch_b_mask_before_second_constant_subtraction,
					state.branch_b_mask_contribution_from_second_addition_term,
					state.chosen_correlated_input_mask_for_injection_from_branch_b };
			case LinearSearchStage::FirstAdd:
				return {
					state.input_branch_a_mask_before_first_constant_subtraction_current,
					state.output_branch_b_mask_after_first_addition,
					0u };
			default:
				return {};
			}
		}

		struct LinearCollectorResidualFrontierHelper final
		{
			LinearCollectorResidualFrontierHelper( LinearBestSearchContext& context_in, LinearSearchCursor& cursor_in )
				: context( context_in ), cursor( cursor_in )
			{
			}

			TwilightDream::residual_frontier_shared::ResidualProblemRecord make_root_source_record(
				SearchWeight ) const noexcept
			{
				const auto& counters = context.residual_counters;
				const std::uint64_t sequence =
					counters.interrupted_source_input_pair_count +
					counters.completed_source_input_pair_count +
					1u;
				auto record =
					TwilightDream::residual_frontier_shared::make_residual_problem_record(
						TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Linear,
						TwilightDream::residual_frontier_shared::ResidualObjectiveKind::HullCollect,
						context.configuration.round_count,
						normalize_linear_residual_stage_cursor( LinearSearchStage::Enter ),
						context.start_output_branch_a_mask,
						context.start_output_branch_b_mask,
						SearchWeight( 0 ),
						sequence,
						context.run_visited_node_count );
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, context.configuration.round_count );
				record.key.suffix_profile_id =
					compute_linear_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = root_residual_source_tag();
				return record;
			}

			void emit_source_pair_event(
				TwilightDream::residual_frontier_shared::ResidualPairEventKind kind,
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				SearchWeight collect_weight_cap )
			{
				using namespace TwilightDream::residual_frontier_shared;
				if ( kind == ResidualPairEventKind::InterruptedSourceInputPair )
					++context.residual_counters.interrupted_source_input_pair_count;
				else if ( kind == ResidualPairEventKind::CompletedSourceInputPair )
					++context.residual_counters.completed_source_input_pair_count;

				{
					std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
					TwilightDream::runtime_component::print_progress_prefix( std::cout );
					std::cout << "[Residual][LinearCollector] event=" << residual_pair_event_kind_to_string( kind )
							  << " rounds_remaining=" << record.key.rounds_remaining
							  << " collect_weight_cap=" << collect_weight_cap
							  << " interrupted_source_input_pair_count=" << context.residual_counters.interrupted_source_input_pair_count
							  << " completed_source_input_pair_count=" << context.residual_counters.completed_source_input_pair_count
							  << " ";
					write_residual_problem_key_debug_fields_inline( std::cout, record.key );
					std::cout << " ";
					print_word32_hex( "pair_a=", record.key.pair_a );
					std::cout << " ";
					print_word32_hex( "pair_b=", record.key.pair_b );
					std::cout << "\n";
				}

				if ( !context.runtime_event_log )
					return;
				context.runtime_event_log->write_event(
					residual_pair_event_kind_to_string( kind ),
					[&]( std::ostream& out ) {
						out << "domain=linear\n";
						out << "objective=hull_collect\n";
						out << "rounds_remaining=" << record.key.rounds_remaining << "\n";
						write_residual_problem_key_debug_fields_multiline( out, record.key );
						out << "pair_a=" << hex8( record.key.pair_a ) << "\n";
						out << "pair_b=" << hex8( record.key.pair_b ) << "\n";
						out << "collect_weight_cap=" << collect_weight_cap << "\n";
						out << "interrupted_source_input_pair_count=" << context.residual_counters.interrupted_source_input_pair_count << "\n";
						out << "completed_source_input_pair_count=" << context.residual_counters.completed_source_input_pair_count << "\n";
					} );
			}

			void set_active_problem(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				bool is_root )
			{
				context.local_state_dominance.clear();
				context.active_problem_valid = true;
				context.active_problem_is_root = is_root;
				context.active_problem_record = record;
			}

			void clear_active_problem()
			{
				context.active_problem_valid = false;
				context.active_problem_is_root = false;
				context.active_problem_record = {};
			}

			void upsert_residual_result(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				SearchWeight collect_weight_cap,
				bool solved,
				bool exact_within_collect_weight_cap )
			{
				for ( auto& existing : context.global_residual_result_table )
				{
					if ( existing.key == record.key )
					{
						existing.best_weight = context.best_weight;
						existing.collect_weight_cap = collect_weight_cap;
						existing.solved = solved;
						existing.exact_within_collect_weight_cap = exact_within_collect_weight_cap;
						return;
					}
				}

				TwilightDream::residual_frontier_shared::ResidualResultRecord result {};
				result.key = record.key;
				result.best_weight = context.best_weight;
				result.collect_weight_cap = collect_weight_cap;
				result.solved = solved;
				result.exact_within_collect_weight_cap = exact_within_collect_weight_cap;
				context.global_residual_result_table.push_back( result );
			}

			void complete_active_problem(
				SearchWeight collect_weight_cap,
				bool exact_within_collect_weight_cap )
			{
				if ( !context.active_problem_valid )
					return;

				const auto record = context.active_problem_record;
				context.completed_residual_set.emplace( record.key );
				upsert_residual_result( record, collect_weight_cap, true, exact_within_collect_weight_cap );
				if ( context.active_problem_is_root )
				{
					context.completed_source_input_pairs.push_back( record );
					emit_source_pair_event(
						TwilightDream::residual_frontier_shared::ResidualPairEventKind::CompletedSourceInputPair,
						record,
						collect_weight_cap );
				}
				else
				{
					context.completed_output_as_next_input_pairs.push_back( record );
					emit_completed_output_pair_event( record );
				}
				clear_active_problem();
			}

			void interrupt_root_if_needed( bool hit_node_limit, bool hit_time_limit, SearchWeight collect_weight_cap )
			{
				if ( !context.active_problem_valid || !context.active_problem_is_root )
					return;
				if ( !hit_node_limit && !hit_time_limit )
					return;

				emit_source_pair_event(
					TwilightDream::residual_frontier_shared::ResidualPairEventKind::InterruptedSourceInputPair,
					context.active_problem_record,
					collect_weight_cap );
			}

			TwilightDream::residual_frontier_shared::ResidualProblemRecord make_boundary_record(
				std::int32_t rounds_remaining,
				LinearSearchStage stage_cursor,
				std::uint32_t pair_a,
				std::uint32_t pair_b,
				std::uint32_t pair_c,
				SearchWeight best_prefix_weight ) const noexcept
			{
				const auto& counters = context.residual_counters;
				const std::uint64_t sequence =
					counters.interrupted_output_as_next_input_pair_count +
					counters.completed_output_as_next_input_pair_count +
					1u;
				auto record =
					TwilightDream::residual_frontier_shared::make_residual_problem_record(
						TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Linear,
						TwilightDream::residual_frontier_shared::ResidualObjectiveKind::HullCollect,
						rounds_remaining,
						normalize_linear_residual_stage_cursor( stage_cursor ),
						pair_a,
						pair_b,
						best_prefix_weight,
						sequence,
					context.run_visited_node_count );
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, rounds_remaining );
				record.key.suffix_profile_id =
					compute_linear_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = child_residual_source_tag();
				record.key.pair_c = pair_c;
				record.best_prefix_weight = best_prefix_weight;
				return record;
			}

			void emit_interrupted_output_pair_progress(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				std::uint64_t recent_added_count )
			{
				using namespace TwilightDream::residual_frontier_shared;
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Residual][LinearCollector] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::InterruptedOutputAsNextInputPair )
						  << " rounds_remaining=" << record.key.rounds_remaining
						  << " stage_cursor=" << unsigned( record.key.stage_cursor )
						  << " interrupted_output_as_next_input_pair_count=" << context.residual_counters.interrupted_output_as_next_input_pair_count
						  << " recent_added=" << recent_added_count
						  << " ";
				write_residual_problem_key_debug_fields_inline( std::cout, record.key );
				std::cout << " ";
				print_word32_hex( "pair_a=", record.key.pair_a );
				std::cout << " ";
				print_word32_hex( "pair_b=", record.key.pair_b );
				std::cout << "\n";
			}

			void emit_completed_output_pair_event(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record )
			{
				using namespace TwilightDream::residual_frontier_shared;
				++context.residual_counters.completed_output_as_next_input_pair_count;
				{
					std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
					TwilightDream::runtime_component::print_progress_prefix( std::cout );
					std::cout << "[Residual][LinearCollector] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::CompletedOutputAsNextInputPair )
							  << " rounds_remaining=" << record.key.rounds_remaining
							  << " stage_cursor=" << unsigned( record.key.stage_cursor )
							  << " completed_output_as_next_input_pair_count=" << context.residual_counters.completed_output_as_next_input_pair_count
							  << " ";
					write_residual_problem_key_debug_fields_inline( std::cout, record.key );
					std::cout << " ";
					print_word32_hex( "pair_a=", record.key.pair_a );
					std::cout << " ";
					print_word32_hex( "pair_b=", record.key.pair_b );
					std::cout << "\n";
				}
				if ( context.runtime_event_log )
				{
					context.runtime_event_log->write_event(
						residual_pair_event_kind_to_string( ResidualPairEventKind::CompletedOutputAsNextInputPair ),
						[&]( std::ostream& out ) {
							out << "domain=linear\n";
							out << "objective=hull_collect\n";
							out << "rounds_remaining=" << record.key.rounds_remaining << "\n";
							out << "stage_cursor=" << unsigned( record.key.stage_cursor ) << "\n";
							write_residual_problem_key_debug_fields_multiline( out, record.key );
							out << "pair_a=" << hex8( record.key.pair_a ) << "\n";
							out << "pair_b=" << hex8( record.key.pair_b ) << "\n";
							out << "completed_output_as_next_input_pair_count=" << context.residual_counters.completed_output_as_next_input_pair_count << "\n";
						} );
				}
			}

			bool try_enqueue_pending_frontier_record(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record )
			{
				if ( auto it = context.pending_frontier_index_by_key.find( record.key ); it != context.pending_frontier_index_by_key.end() )
				{
					auto& existing = context.pending_frontier[ it->second ];
					if ( existing.best_prefix_weight <= record.best_prefix_weight )
						return false;
					existing = record;
					return true;
				}
				context.pending_frontier_index_by_key.emplace( record.key, context.pending_frontier.size() );
				context.pending_frontier.push_back( record );
				return true;
			}

			bool try_enqueue_pending_frontier_entry( const LinearResidualFrontierEntry& entry )
			{
				if ( auto it = context.pending_frontier_entry_index_by_key.find( entry.record.key ); it != context.pending_frontier_entry_index_by_key.end() )
				{
					auto& existing = context.pending_frontier_entries[ it->second ];
					if ( existing.record.best_prefix_weight <= entry.record.best_prefix_weight )
						return false;
					existing = entry;
					return true;
				}
				context.pending_frontier_entry_index_by_key.emplace( entry.record.key, context.pending_frontier_entries.size() );
				context.pending_frontier_entries.push_back( entry );
				return true;
			}

			bool try_register_child_residual_candidate(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				const LinearSearchFrame* frame_snapshot = nullptr,
				const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
				SearchWeight prefix_weight_offset = 0 )
			{
				using namespace TwilightDream::residual_frontier_shared;
				if ( context.completed_residual_set.find( record.key ) != context.completed_residual_set.end() )
				{
					++context.residual_counters.repeated_or_dominated_residual_skip_count;
					return false;
				}
				if ( auto it = context.best_prefix_by_residual_key.find( record.key ); it != context.best_prefix_by_residual_key.end() )
				{
					if ( it->second <= record.best_prefix_weight )
					{
						++context.residual_counters.repeated_or_dominated_residual_skip_count;
						return false;
					}
					it->second = record.best_prefix_weight;
				}
				else
				{
					context.best_prefix_by_residual_key.emplace( record.key, record.best_prefix_weight );
				}

				std::uint64_t recent_added_count = 0;
				bool replaced_existing = false;
				for ( auto& existing : context.transient_output_as_next_input_pair_candidates )
				{
					if ( existing.key == record.key )
					{
						recent_added_count = 0;
						replaced_existing = true;
						existing = record;
						break;
					}
				}
				if ( !replaced_existing )
				{
					context.transient_output_as_next_input_pair_candidates.push_back( record );
					recent_added_count = 1;
				}
				if ( frame_snapshot != nullptr && trail_snapshot != nullptr )
				{
					LinearResidualFrontierEntry entry {};
					entry.record = record;
					entry.frame_snapshot = std::make_shared<LinearSearchFrame>( *frame_snapshot );
					entry.current_trail_snapshot = *trail_snapshot;
					entry.prefix_weight_offset = prefix_weight_offset;

					bool replaced_snapshot = false;
					for ( auto& existing_entry : context.transient_output_as_next_input_pair_entries )
					{
						if ( existing_entry.record.key == record.key )
						{
							existing_entry = std::move( entry );
							replaced_snapshot = true;
							break;
						}
					}
					if ( !replaced_snapshot )
						context.transient_output_as_next_input_pair_entries.push_back( std::move( entry ) );
				}
				++context.residual_counters.interrupted_output_as_next_input_pair_count;
				emit_interrupted_output_pair_progress( record, recent_added_count );
				commit_transient_output_pairs();
				return true;
			}

			void commit_transient_output_pairs()
			{
				for ( const auto& record : context.transient_output_as_next_input_pair_candidates )
					( void )try_enqueue_pending_frontier_record( record );
				context.transient_output_as_next_input_pair_candidates.clear();
				for ( const auto& entry : context.transient_output_as_next_input_pair_entries )
					( void )try_enqueue_pending_frontier_entry( entry );
				context.transient_output_as_next_input_pair_entries.clear();
			}

			void rebuild_pending_frontier_indexes()
			{
				context.pending_frontier_index_by_key.clear();
				for ( std::size_t i = 0; i < context.pending_frontier.size(); ++i )
					context.pending_frontier_index_by_key[ context.pending_frontier[ i ].key ] = i;
				context.pending_frontier_entry_index_by_key.clear();
				for ( std::size_t i = 0; i < context.pending_frontier_entries.size(); ++i )
					context.pending_frontier_entry_index_by_key[ context.pending_frontier_entries[ i ].record.key ] = i;
			}

			bool restore_next_pending_frontier_entry()
			{
				if ( !cursor.stack.empty() )
					return false;

				for ( std::size_t index = 0; index < context.pending_frontier_entries.size(); )
				{
					LinearResidualFrontierEntry entry = context.pending_frontier_entries[ index ];
					if ( !entry.frame_snapshot )
					{
						erase_pending_frontier_entry_at( index );
						continue;
					}

					erase_pending_frontier_entry_at( index );
					erase_pending_frontier_record_by_key( entry.record.key );

					context.current_linear_trail = std::move( entry.current_trail_snapshot );
					cursor.stack.clear();
					cursor.stack.push_back( *entry.frame_snapshot );
					set_active_problem( entry.record, false );
					return true;
				}
				return false;
			}

		private:
			LinearBestSearchContext& context;
			LinearSearchCursor& cursor;

			void erase_pending_frontier_entry_at( std::size_t index )
			{
				if ( index >= context.pending_frontier_entries.size() )
					return;
				context.pending_frontier_entry_index_by_key.erase( context.pending_frontier_entries[ index ].record.key );
				const std::size_t last = context.pending_frontier_entries.size() - 1u;
				if ( index != last )
				{
					context.pending_frontier_entries[ index ] = std::move( context.pending_frontier_entries[ last ] );
					context.pending_frontier_entry_index_by_key[ context.pending_frontier_entries[ index ].record.key ] = index;
				}
				context.pending_frontier_entries.pop_back();
			}

			void erase_pending_frontier_record_by_key( const TwilightDream::residual_frontier_shared::ResidualProblemKey& key )
			{
				const auto it = context.pending_frontier_index_by_key.find( key );
				if ( it == context.pending_frontier_index_by_key.end() )
					return;
				const std::size_t index = it->second;
				context.pending_frontier_index_by_key.erase( it );
				const std::size_t last = context.pending_frontier.size() - 1u;
				if ( index != last )
				{
					context.pending_frontier[ index ] = std::move( context.pending_frontier[ last ] );
					context.pending_frontier_index_by_key[ context.pending_frontier[ index ].key ] = index;
				}
				context.pending_frontier.pop_back();
			}
		};
	} // namespace
	// One-shot hull collector.
	//
	// This is not a second best-search engine and it does not compete with the resumable
	// checkpoint path in `linear_best_search_engine.cpp`. Its job is to enumerate and
	// aggregate all trails up to a caller-provided weight cap, reusing the same ARX math
	// bridges (exact injection-mask spaces, exact sub-const, exact z-shell add candidates)
	// without maintaining a resumable DFS cursor.
	//
	// Gap B fixed-output hot path uses the same three-way semantics as the resumable
	// cursor path:
	// - column floor is always available as a local feasibility / lower-bound gate,
	// - collapsed mode materializes only the single fixed-beta `alpha*` witness,
	// - strict mode keeps full fixed-beta alpha enumeration.
	// One-shot recursive collector following the same round-level nonlinear layout as the
	// resumable cursor and best-search engine:
	//   InjA -> SecondAdd -> InjB -> SecondConst -> FirstSubconst -> FirstAdd.
	// This class is intentionally simpler in control flow, but it must keep the same local
	// operator semantics and pruning chain so its exactness claims match the resumable path.
	class LinearTrailCollector final
	{
public:
		LinearTrailCollector(
			std::uint32_t output_branch_a_mask,
			std::uint32_t output_branch_b_mask,
			const LinearBestSearchConfiguration& base_configuration,
			const LinearHullCollectionOptions& options )
			: options_( options ),
			  collect_weight_cap_( options.collect_weight_cap ),
			  maximum_collected_trails_( options.maximum_collected_trails )
		{
			context_.configuration = base_configuration;
			context_.runtime_controls = options.runtime_controls;
			context_.start_output_branch_a_mask = output_branch_a_mask;
			context_.start_output_branch_b_mask = output_branch_b_mask;
			prepare_linear_remaining_round_lower_bound_table(
				context_.configuration,
				context_.configuration.round_count,
				output_branch_a_mask,
				output_branch_b_mask );
			context_.configuration.enable_state_memoization = false;
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
			const bool used_non_strict_subconst_mode = linear_configuration_uses_non_strict_subconst_mode( context_.configuration );
			const bool unsupported_varconst_outer_binding =
				linear_configuration_requests_unsupported_varconst_outer_q2_binding( context_.configuration );
			result_.exact_within_collect_weight_cap =
				!used_non_strict_search_mode &&
				!used_non_strict_subconst_mode &&
				!unsupported_varconst_outer_binding &&
				!result_.hit_maximum_search_nodes &&
				!result_.hit_time_limit &&
				!result_.hit_collection_limit &&
				!result_.hit_callback_stop &&
				!result_.used_non_strict_branch_cap;
			result_.exactness_rejection_reason =
				classify_linear_collection_exactness_reason(
					result_,
					used_non_strict_search_mode,
					used_non_strict_subconst_mode,
					unsupported_varconst_outer_binding );
			return result_;
		}

	private:
		LinearBestSearchContext	 context_ {};
		LinearHullAggregationResult result_ {};
		LinearHullCollectionOptions options_ {};
		SearchWeight collect_weight_cap_ = 0;
		std::uint64_t			 maximum_collected_trails_ = 0;

		SearchWeight remaining_round_lower_bound( int depth ) const
		{
			if ( !context_.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left = context_.configuration.round_count - depth;
			if ( rounds_left < 0 )
				return 0;
			const std::size_t index = std::size_t( rounds_left );
			if ( index >= context_.configuration.remaining_round_min_weight.size() )
				return 0;
			return context_.configuration.remaining_round_min_weight[ index ];
		}

		SearchWeight remaining_round_lower_bound_after_this_round( int depth ) const
		{
			if ( !context_.configuration.enable_remaining_round_lower_bound )
				return 0;
			const int rounds_left = context_.configuration.round_count - ( depth + 1 );
			if ( rounds_left < 0 )
				return 0;
			const std::size_t index = std::size_t( rounds_left );
			if ( index >= context_.configuration.remaining_round_min_weight.size() )
				return 0;
			return context_.configuration.remaining_round_min_weight[ index ];
		}

		bool should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight )
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

		bool should_prune_local_state_dominance(
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight prefix_weight )
		{
			return context_.local_state_dominance.should_prune_or_update(
				static_cast<std::uint8_t>( stage_cursor ),
				pair_a,
				pair_b,
				prefix_weight );
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

		void collect_current_trail( SearchWeight total_weight, std::uint32_t input_branch_a_mask, std::uint32_t input_branch_b_mask, long double exact_signed_correlation )
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
			SearchWeight accumulated_weight,
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
			const SearchWeight remaining_round_lb_after_this_round = remaining_round_lower_bound_after_this_round( depth );
			if ( accumulated_weight + remaining_round_lb_after_this_round > collect_weight_cap_ )
				return;
			const SearchWeight round_weight_cap = collect_weight_cap_ - accumulated_weight - remaining_round_lb_after_this_round;

			const std::uint32_t branch_a_round_output_mask_before_inj_from_a = current_round_output_branch_a_mask;
			const std::uint32_t branch_b_mask_before_injection_from_branch_a = current_round_output_branch_b_mask;
			const std::uint32_t branch_a_mask_after_explicit_prewhitening_before_injection =
				linear_mask_after_explicit_prewhitening_before_injection_from_branch_a(
					current_round_output_branch_b_mask );
			const InjectionCorrelationTransition injection_from_branch_a_transition =
				compute_injection_transition_from_branch_a(
					branch_a_mask_after_explicit_prewhitening_before_injection );
			const SearchWeight weight_injection_from_branch_a = injection_from_branch_a_transition.weight;
			if ( weight_injection_from_branch_a > round_weight_cap )
				return;

			const SearchWeight remaining_after_inj_a = round_weight_cap - weight_injection_from_branch_a;
			const SearchWeight second_subconst_weight_cap = std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, remaining_after_inj_a );
			const SearchWeight second_add_weight_cap = std::min<SearchWeight>( context_.configuration.addition_weight_cap, remaining_after_inj_a );

			enumerate_affine_subspace_input_masks(
				context_,
				injection_from_branch_a_transition,
				context_.configuration.maximum_injection_input_masks,
				[&]( std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a ) {
					if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
						return;

					const std::uint32_t branch_a_mask_after_undoing_injection_from_branch_a =
						branch_a_round_output_mask_before_inj_from_a ^ chosen_correlated_input_mask_for_injection_from_branch_a;
					const std::uint32_t output_branch_b_mask_after_second_constant_subtraction =
						linear_backward_mask_after_cross_xor_rot_r1_on_branch_b(
							branch_b_mask_before_injection_from_branch_a,
							branch_a_mask_after_undoing_injection_from_branch_a );
					const std::uint32_t output_branch_a_mask_after_second_addition =
						linear_backward_mask_after_cross_xor_rot_r0_on_branch_a(
							branch_a_mask_after_undoing_injection_from_branch_a,
							output_branch_b_mask_after_second_constant_subtraction );

					const auto second_subconst_column_floor =
						try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
							try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
								make_fixed_beta_sub_column_root_theorem_request(
									output_branch_b_mask_after_second_constant_subtraction,
									NeoAlzetteCore::ROUND_CONSTANTS[ 6 ] ),
								second_subconst_weight_cap ) );
					if ( !second_subconst_column_floor.has_value() )
						return;
					static_assert(
						linear_fixed_beta_hot_path_call_site_requires_strict_candidates(
							LinearFixedBetaHotPathCallSite::CollectorRecursiveSecondSubconstMaterialization ) );
					const auto second_constant_subtraction_candidates_for_branch_b =
						[](
							const LinearBestSearchConfiguration& configuration,
							const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
							std::uint32_t output_mask_beta,
							std::uint32_t constant,
							SearchWeight weight_cap ) {
							std::vector<SubConstCandidate> out;
							for ( const auto& q2_candidate :
								  materialize_varconst_q2_candidates_for_call_site(
									configuration,
									LinearFixedBetaHotPathCallSite::CollectorRecursiveSecondSubconstMaterialization,
									column_floor,
									output_mask_beta,
									constant,
									weight_cap ) )
							{
								out.push_back( SubConstCandidate {
									q2_candidate.input_mask_alpha,
									q2_candidate.exact_weight_hint } );
							}
							return out;
						}(
							context_.configuration,
							second_subconst_column_floor,
							output_branch_b_mask_after_second_constant_subtraction,
							NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
							second_subconst_weight_cap );
					std::vector<AddCandidate> second_addition_candidates_for_branch_a {};
					materialize_varvar_row_q2_candidates_for_output_mask_u(
						second_addition_candidates_for_branch_a,
						output_branch_a_mask_after_second_addition,
						second_add_weight_cap,
						context_.configuration );

					for ( const auto& second_addition_candidate_for_branch_a : second_addition_candidates_for_branch_a )
					{
						LinearVarVarQ2Candidate second_add_q2 {};
						if ( !resolve_varvar_q2_candidate(
								context_.configuration,
								output_branch_a_mask_after_second_addition,
								second_add_weight_cap,
								second_addition_candidate_for_branch_a,
								second_add_q2 ) )
						{
							continue;
						}
						const auto second_add_q1_weight =
							evaluate_varvar_q1_exact_weight( second_add_q2, 32 );
						if ( !second_add_q1_weight.has_value() )
							continue;
						const SearchWeight weight_second_addition = second_add_q1_weight.value();
						const std::uint32_t u_second_for_step = second_add_q2.sum_wire_u;
						if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
							return;

						if ( weight_injection_from_branch_a + weight_second_addition > round_weight_cap )
						{
							if ( second_add_q2.ordered_stream.safe_break_when_cap_exceeded )
								break;
							continue;
						}

						const std::uint32_t input_branch_a_mask_before_second_addition = second_add_q2.q1_input.input_mask_x;
						const std::uint32_t second_addition_term_mask_from_branch_b = second_add_q2.q1_input.input_mask_y;
						const std::uint32_t branch_b_mask_contribution_from_second_addition_term =
							NeoAlzetteCore::rotr( second_addition_term_mask_from_branch_b, 31 ) ^
							NeoAlzetteCore::rotr( second_addition_term_mask_from_branch_b, 17 );

						const std::uint32_t branch_b_mask_after_explicit_prewhitening_before_injection =
							linear_mask_after_explicit_prewhitening_before_injection_from_branch_b(
								input_branch_a_mask_before_second_addition );
						const InjectionCorrelationTransition injection_from_branch_b_transition =
							compute_injection_transition_from_branch_b(
								branch_b_mask_after_explicit_prewhitening_before_injection );
						const SearchWeight weight_injection_from_branch_b = injection_from_branch_b_transition.weight;
						const SearchWeight base_weight_after_inj_b = weight_injection_from_branch_a + weight_second_addition + weight_injection_from_branch_b;
						if ( base_weight_after_inj_b > round_weight_cap )
							continue;
						if ( base_weight_after_inj_b + second_subconst_column_floor->linear_weight > round_weight_cap )
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

									LinearVarConstQ2Candidate second_subconst_q2 {};
									if ( !resolve_varconst_q2_candidate(
											context_.configuration,
											LinearFixedBetaHotPathCallSite::CollectorRecursiveSecondSubconstMaterialization,
											output_branch_b_mask_after_second_constant_subtraction,
											second_constant_subtraction_candidate_for_branch_b,
											second_subconst_q2 ) )
									{
										continue;
									}
									SearchWeight second_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
									const auto second_subconst_q1_weight =
										evaluate_varconst_q1_exact_weight(
											second_subconst_q2,
											NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
											second_subconst_weight_cap,
											&second_subconst_fixed_alpha_weight_floor,
											32 );
									if ( !second_subconst_q1_weight.has_value() )
										continue;
									const SearchWeight weight_second_constant_subtraction = second_subconst_q1_weight.value();
									if ( base_weight_after_inj_b + weight_second_constant_subtraction > round_weight_cap )
									{
										if ( second_subconst_q2.ordered_stream.safe_break_when_cap_exceeded )
											break;
										continue;
									}

									const std::uint32_t input_branch_b_mask_before_second_constant_subtraction = second_subconst_q2.input_mask_alpha;
									const std::uint32_t branch_b_mask_after_second_add_term_removed =
										input_branch_b_mask_before_second_constant_subtraction ^ branch_b_mask_contribution_from_second_addition_term;
									const std::uint32_t branch_b_mask_after_undoing_injection_from_branch_b =
										branch_b_mask_after_second_add_term_removed ^ chosen_correlated_input_mask_for_injection_from_branch_b;
									const std::uint32_t output_branch_a_mask_after_first_constant_subtraction =
										linear_backward_mask_after_cross_xor_rot_r1_on_branch_b(
											input_branch_a_mask_before_second_addition,
											branch_b_mask_after_undoing_injection_from_branch_b );
									const std::uint32_t output_branch_b_mask_after_first_addition =
										linear_backward_mask_after_cross_xor_rot_r0_on_branch_a(
											branch_b_mask_after_undoing_injection_from_branch_b,
											output_branch_a_mask_after_first_constant_subtraction );

									const SearchWeight base_weight_after_second_subconst = base_weight_after_inj_b + weight_second_constant_subtraction;
									const SearchWeight remaining_after_second_subconst = round_weight_cap - base_weight_after_second_subconst;
									const SearchWeight first_subconst_weight_cap = std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, remaining_after_second_subconst );
									const SearchWeight first_add_weight_cap = std::min<SearchWeight>( context_.configuration.addition_weight_cap, remaining_after_second_subconst );

									const auto first_subconst_column_floor =
										try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
											try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
												make_fixed_beta_sub_column_root_theorem_request(
													output_branch_a_mask_after_first_constant_subtraction,
													NeoAlzetteCore::ROUND_CONSTANTS[ 1 ] ),
												first_subconst_weight_cap ) );
									if ( !first_subconst_column_floor.has_value() )
										continue;
									if ( base_weight_after_second_subconst + first_subconst_column_floor->linear_weight > round_weight_cap )
										continue;
									static_assert(
										linear_fixed_beta_hot_path_call_site_requires_strict_candidates(
											LinearFixedBetaHotPathCallSite::CollectorRecursiveFirstSubconstMaterialization ) );
									const auto first_constant_subtraction_candidates_for_branch_a =
										[](
											const LinearBestSearchConfiguration& configuration,
											const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
											std::uint32_t output_mask_beta,
											std::uint32_t constant,
											SearchWeight weight_cap ) {
											std::vector<SubConstCandidate> out;
											for ( const auto& q2_candidate :
												  materialize_varconst_q2_candidates_for_call_site(
													configuration,
													LinearFixedBetaHotPathCallSite::CollectorRecursiveFirstSubconstMaterialization,
													column_floor,
													output_mask_beta,
													constant,
													weight_cap ) )
											{
												out.push_back( SubConstCandidate {
													q2_candidate.input_mask_alpha,
													q2_candidate.exact_weight_hint } );
											}
											return out;
										}(
											context_.configuration,
											first_subconst_column_floor,
											output_branch_a_mask_after_first_constant_subtraction,
											NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
											first_subconst_weight_cap );
									std::vector<AddCandidate> first_addition_candidates_for_branch_b {};
									materialize_varvar_row_q2_candidates_for_output_mask_u(
										first_addition_candidates_for_branch_b,
										output_branch_b_mask_after_first_addition,
										first_add_weight_cap,
										context_.configuration );

									for ( const auto& first_constant_subtraction_candidate_for_branch_a : first_constant_subtraction_candidates_for_branch_a )
									{
										if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
											return;

										LinearVarConstQ2Candidate first_subconst_q2 {};
										if ( !resolve_varconst_q2_candidate(
												context_.configuration,
												LinearFixedBetaHotPathCallSite::CollectorRecursiveFirstSubconstMaterialization,
												output_branch_a_mask_after_first_constant_subtraction,
												first_constant_subtraction_candidate_for_branch_a,
												first_subconst_q2 ) )
										{
											continue;
										}
										SearchWeight first_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
										const auto first_subconst_q1_weight =
											evaluate_varconst_q1_exact_weight(
												first_subconst_q2,
												NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
												first_subconst_weight_cap,
												&first_subconst_fixed_alpha_weight_floor,
												32 );
										if ( !first_subconst_q1_weight.has_value() )
											continue;
										const SearchWeight weight_first_constant_subtraction = first_subconst_q1_weight.value();
										const SearchWeight base_weight_after_first_subconst = base_weight_after_second_subconst + weight_first_constant_subtraction;
										if ( base_weight_after_first_subconst > round_weight_cap )
										{
											if ( first_subconst_q2.ordered_stream.safe_break_when_cap_exceeded )
												break;
											continue;
										}

										const std::uint32_t input_branch_a_mask_before_first_constant_subtraction = first_subconst_q2.input_mask_alpha;
										for ( const auto& first_addition_candidate_for_branch_b : first_addition_candidates_for_branch_b )
										{
											LinearVarVarQ2Candidate first_add_q2 {};
											if ( !resolve_varvar_q2_candidate(
													context_.configuration,
													output_branch_b_mask_after_first_addition,
													first_add_weight_cap,
													first_addition_candidate_for_branch_b,
													first_add_q2 ) )
											{
												continue;
											}
											const auto first_add_q1_weight =
												evaluate_varvar_q1_exact_weight( first_add_q2, 32 );
											if ( !first_add_q1_weight.has_value() )
												continue;
											SearchWeight weight_first_addition = first_add_q1_weight.value();
											std::uint32_t u_first_for_step = first_add_q2.sum_wire_u;
											if ( inner_limits_hit( depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask ) )
												return;

											const SearchWeight total_round_weight = base_weight_after_first_subconst + weight_first_addition;
											if ( total_round_weight > round_weight_cap )
											{
												if ( first_add_q2.ordered_stream.safe_break_when_cap_exceeded )
													break;
												continue;
											}

											const std::uint32_t input_branch_b_mask_before_first_addition = first_add_q2.q1_input.input_mask_x;
											const std::uint32_t first_addition_term_mask_from_branch_a = first_add_q2.q1_input.input_mask_y;
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

											step.output_branch_a_mask_after_second_addition = u_second_for_step;
											step.input_branch_a_mask_before_second_addition = input_branch_a_mask_before_second_addition;
											step.second_addition_term_mask_from_branch_b = second_addition_term_mask_from_branch_b;
											step.weight_second_addition = weight_second_addition;

											step.weight_injection_from_branch_a = weight_injection_from_branch_a;
											step.weight_injection_from_branch_b = weight_injection_from_branch_b;
											step.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_correlated_input_mask_for_injection_from_branch_a;
											step.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_correlated_input_mask_for_injection_from_branch_b;
											step.branch_a_mask_after_undoing_injection_from_branch_a = branch_a_mask_after_undoing_injection_from_branch_a;
											step.branch_b_mask_before_undoing_injection_from_branch_b = branch_b_mask_after_second_add_term_removed;
											step.branch_b_mask_after_undoing_injection_from_branch_b = branch_b_mask_after_undoing_injection_from_branch_b;

											step.output_branch_a_mask_after_first_constant_subtraction = output_branch_a_mask_after_first_constant_subtraction;
											step.input_branch_a_mask_before_first_constant_subtraction = input_branch_a_mask_before_first_constant_subtraction;
											step.weight_first_constant_subtraction = weight_first_constant_subtraction;

											step.output_branch_b_mask_after_first_addition = u_first_for_step;
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

	// Resumable hull collector cursor.
	//
	// This class must stay stage-isomorphic to `LinearBestTrailSearcherCursor`: it uses the
	// same reverse-round nonlinear operator layout and the same local analysis-operator
	// contracts, but aggregates every trail inside a caller-supplied cap instead of updating
	// a single best bound.
	//
	// Canonical per-round order:
	//   InjA -> SecondAdd -> InjB -> SecondConst -> FirstSubconst -> FirstAdd.
	//
	// Operator responsibilities are unchanged from the best-search engine:
	// - injection: exact affine input-mask space + exact local rank/2 weight;
	// - var-var add: Q2 chooses the fixed-u or fixed-(v,w) polarity object, Q1 provides the
	//   exact local Schulte-Geers / CCZ weight;
	// - var-const subtract: Q2 provides fixed-beta / fixed-alpha floors or ordered witnesses,
	//   Q1 provides the exact var-const local weight.
	//
	// As in the engine, accelerators are indexing layers only.  A cache miss or truncated
	// materialized list is an engineering event, not a mathematical proof of emptiness.
	class ResumableLinearHullCollectorCursor final
	{
public:
		ResumableLinearHullCollectorCursor(
			LinearHullCollectorExecutionState& state_in,
			LinearCollectorResidualFrontierHelper& helper_in,
			LinearHullTrailCallback on_trail_in,
			std::function<void()> checkpoint_hook_in = {} );

		void start_from_initial_frame( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask );
		void search_from_cursor();

	private:
		LinearHullCollectorExecutionState&   state_;
		LinearBestSearchContext&			   context;
		const LinearBestSearchConfiguration& search_configuration;
		LinearSearchCursor&				   cursor;
		LinearCollectorResidualFrontierHelper& helper;
		LinearHullAggregationResult&		   result_;
		LinearHullTrailCallback			   on_trail_ {};
		std::function<void()>			   checkpoint_hook_ {};
		std::chrono::steady_clock::time_point last_checkpoint_time_ {};

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			std::uint32_t pair_c,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return helper.make_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				pair_c,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			const LinearBestSearchContext&,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			std::uint32_t pair_c,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_linear_hull_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				pair_c,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_linear_hull_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				0u,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			const LinearBestSearchContext& ctx,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_linear_hull_boundary_record(
				ctx,
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				0u,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			const LinearRoundSearchState& state,
			SearchWeight best_prefix_weight ) const noexcept
		{
			const LinearResidualSemanticKeyParts key = linear_semantic_key_for_stage( stage_cursor, state );
			return make_linear_hull_boundary_record(
				rounds_remaining,
				stage_cursor,
				key.pair_a,
				key.pair_b,
				key.pair_c,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_hull_boundary_record(
			const LinearBestSearchContext&,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			const LinearRoundSearchState& state,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_linear_hull_boundary_record(
				rounds_remaining,
				stage_cursor,
				state,
				best_prefix_weight );
		}

		bool try_register_linear_hull_child_residual_candidate(
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const LinearSearchFrame* frame_snapshot = nullptr,
			const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			if ( state_.residual_boundary_mode == LinearCollectorResidualBoundaryMode::PruneAtResidualBoundary )
				return false;
			return helper.try_register_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

		bool try_register_linear_hull_child_residual_candidate(
			LinearBestSearchContext&,
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const LinearSearchFrame* frame_snapshot = nullptr,
			const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			return try_register_linear_hull_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

		LinearSearchFrame& current_frame();
		LinearRoundSearchState& current_round_state();
		void pop_frame();
		bool sync_and_check_runtime_stop();
		bool poll_shared_runtime();
		void maybe_poll_checkpoint();
		void maybe_print_streaming_progress();

		template <typename T>
		void clear_rebuildable_vector( std::vector<T>& values, std::size_t keep_capacity = 4096u );

		bool use_split8_streaming_add_cursor() const;
		bool use_weight_sliced_clat_streaming_add_cursor() const;
		std::optional<VarConstSubColumnOptimalOnOutputWire> compute_second_subconst_q2_floor( LinearRoundSearchState& state ) const;
		SearchWeight ensure_second_subconst_q2_floor( LinearRoundSearchState& state ) const;
		std::optional<VarConstSubColumnOptimalOnOutputWire> compute_first_subconst_q2_floor( LinearRoundSearchState& state ) const;
		SearchWeight ensure_first_subconst_q2_floor( LinearRoundSearchState& state ) const;
		LinearFixedBetaOuterHotPathEnterStatus enter_second_subconst_q2_gate( LinearRoundSearchState& state );
		LinearFixedBetaOuterHotPathEnterStatus enter_first_subconst_q2_gate( LinearRoundSearchState& state );
		void clear_first_stage_buffers( LinearRoundSearchState& state );
		void clear_second_stage_buffers( LinearRoundSearchState& state );
		void begin_streaming_round();
		void collect_current_trail( SearchWeight total_weight, std::uint32_t input_branch_a_mask, std::uint32_t input_branch_b_mask );

		bool advance_stage_inj_a();
		bool advance_stage_second_add();
		bool advance_stage_inj_b();
		bool advance_stage_second_const();
		bool advance_stage_first_subconst();
		bool advance_stage_first_add();
		bool should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight );
		bool should_prune_local_state_dominance( LinearSearchStage stage_cursor, std::uint32_t pair_a, std::uint32_t pair_b, std::uint32_t pair_c, SearchWeight prefix_weight );
		bool should_prune_local_state_dominance( LinearSearchStage stage_cursor, std::uint32_t pair_a, std::uint32_t pair_b, SearchWeight prefix_weight );
		bool should_prune_local_state_dominance( LinearSearchStage stage_cursor, const LinearRoundSearchState& state, SearchWeight prefix_weight );
		bool should_prune_remaining_round_lower_bound( int depth, SearchWeight accumulated_weight ) const;
		bool handle_round_end_if_needed( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight );
		bool prepare_round_state( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight );
		SearchWeight compute_remaining_round_weight_lower_bound_after_this_round( int depth ) const;
		void run();
	};

	class LinearHullBNBScheduler final
	{
	public:
		LinearHullBNBScheduler( LinearHullCollectorExecutionState& state_in )
			: state( state_in ), helper( state_in.context, state_in.cursor )
		{
		}

		void run( LinearHullTrailCallback on_trail, std::function<void()> checkpoint_hook )
		{
			const std::function<void()> final_checkpoint_hook = checkpoint_hook;
			if ( !linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_search(
					 state.context.configuration.varvar_modular_add_bnb_mode ) )
			{
				state.aggregation_result.exactness_rejection_reason =
					StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode;
				state.aggregation_result.best_weight_certified = false;
				return;
			}

			helper.rebuild_pending_frontier_indexes();
			for ( auto& frame : state.cursor.stack )
			{
				frame.state.injection_from_branch_a_enumerator.stop_due_to_limits = false;
				frame.state.second_constant_subtraction_stream_cursor.stop_due_to_limits = false;
				frame.state.second_addition_weight_sliced_clat_stream_cursor.stop_due_to_limits = false;
				frame.state.second_addition_stream_cursor.stop_due_to_limits = false;
				frame.state.injection_from_branch_b_enumerator.stop_due_to_limits = false;
				frame.state.first_constant_subtraction_stream_cursor.stop_due_to_limits = false;
				frame.state.first_addition_weight_sliced_clat_stream_cursor.stop_due_to_limits = false;
				frame.state.first_addition_stream_cursor.stop_due_to_limits = false;
			}

			begin_linear_runtime_invocation( state.context );
			state.context.progress_start_time = state.context.run_start_time;
			best_search_shared_core::SearchControlSession<LinearBestSearchContext> control_session( state.context );
			control_session.begin();
			{
				ScopedRuntimeTimeLimitProbe time_probe( state.context.runtime_controls, state.context.runtime_state );
				ResumableLinearHullCollectorCursor searcher( state, helper, std::move( on_trail ), std::move( checkpoint_hook ) );
				if ( !state.context.active_problem_valid && !state.cursor.stack.empty() )
					helper.set_active_problem( helper.make_root_source_record( state.collect_weight_cap ), true );
				while ( true )
				{
					searcher.search_from_cursor();
					if ( !state.cursor.stack.empty() )
						break;
					helper.complete_active_problem(
						state.collect_weight_cap,
						state.aggregation_result.exact_within_collect_weight_cap );
					if ( !helper.restore_next_pending_frontier_entry() )
						break;
				}
			}
			control_session.stop();

			state.aggregation_result.nodes_visited = state.context.visited_node_count;
			state.aggregation_result.hit_time_limit = runtime_time_limit_hit( state.context.runtime_controls, state.context.runtime_state );
			state.aggregation_result.used_non_strict_branch_cap = linear_collector_configuration_has_non_strict_branch_cap( state.context.configuration );
			const bool used_non_strict_search_mode = linear_configuration_uses_non_strict_search_mode( state.context.configuration );
			const bool used_non_strict_subconst_mode = linear_configuration_uses_non_strict_subconst_mode( state.context.configuration );
			const bool unsupported_varconst_outer_binding =
				linear_configuration_requests_unsupported_varconst_outer_q2_binding( state.context.configuration );
			state.aggregation_result.exact_within_collect_weight_cap =
				!used_non_strict_search_mode &&
				!used_non_strict_subconst_mode &&
				!unsupported_varconst_outer_binding &&
				!state.aggregation_result.hit_maximum_search_nodes &&
				!state.aggregation_result.hit_time_limit &&
				!state.aggregation_result.hit_collection_limit &&
				!state.aggregation_result.hit_callback_stop &&
				!state.aggregation_result.used_non_strict_branch_cap;
			state.aggregation_result.exactness_rejection_reason =
				classify_linear_collection_exactness_reason(
					state.aggregation_result,
					used_non_strict_search_mode,
					used_non_strict_subconst_mode,
					unsupported_varconst_outer_binding );

			helper.interrupt_root_if_needed(
				state.aggregation_result.hit_maximum_search_nodes,
				state.aggregation_result.hit_time_limit,
				state.collect_weight_cap );
			if ( final_checkpoint_hook &&
				 ( state.aggregation_result.hit_maximum_search_nodes || state.aggregation_result.hit_time_limit ) )
			{
				final_checkpoint_hook();
			}
		}

	private:
		LinearHullCollectorExecutionState& state;
		LinearCollectorResidualFrontierHelper helper;
	};

	ResumableLinearHullCollectorCursor::ResumableLinearHullCollectorCursor(
		LinearHullCollectorExecutionState& state_in,
		LinearCollectorResidualFrontierHelper& helper_in,
		LinearHullTrailCallback on_trail_in,
		std::function<void()> checkpoint_hook_in )
		: state_( state_in ),
		  context( state_in.context ),
		  search_configuration( state_in.context.configuration ),
		  cursor( state_in.cursor ),
		  helper( helper_in ),
		  result_( state_in.aggregation_result ),
		  on_trail_( std::move( on_trail_in ) ),
		  checkpoint_hook_( std::move( checkpoint_hook_in ) )
	{
	}

	void ResumableLinearHullCollectorCursor::start_from_initial_frame( std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask )
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

	void ResumableLinearHullCollectorCursor::search_from_cursor()
	{
		run();
	}

	LinearSearchFrame& ResumableLinearHullCollectorCursor::current_frame()
	{
		return cursor.stack.back();
	}

	LinearRoundSearchState& ResumableLinearHullCollectorCursor::current_round_state()
	{
		return current_frame().state;
	}

	void ResumableLinearHullCollectorCursor::pop_frame()
	{
		if ( cursor.stack.empty() )
			return;
		const std::size_t target = cursor.stack.back().trail_size_at_entry;
		if ( context.current_linear_trail.size() > target )
			context.current_linear_trail.resize( target );
		cursor.stack.pop_back();
	}

	bool ResumableLinearHullCollectorCursor::sync_and_check_runtime_stop()
	{
		sync_linear_runtime_legacy_fields_from_state( context );
		return linear_runtime_budget_hit( context );
	}

	bool ResumableLinearHullCollectorCursor::poll_shared_runtime()
	{
		const bool stop = runtime_poll( context.runtime_controls, context.runtime_state );
		sync_linear_runtime_legacy_fields_from_state( context );
		return stop;
	}

	void ResumableLinearHullCollectorCursor::maybe_poll_checkpoint()
	{
		if ( !checkpoint_hook_ || context.runtime_controls.checkpoint_every_seconds == 0 )
			return;
		if ( ( context.visited_node_count & context.progress_node_mask ) != 0 )
			return;
		const auto now = std::chrono::steady_clock::now();
		if ( last_checkpoint_time_.time_since_epoch().count() != 0 )
		{
			const double since_last = std::chrono::duration<double>( now - last_checkpoint_time_ ).count();
			if ( since_last < double( context.runtime_controls.checkpoint_every_seconds ) )
				return;
		}
		checkpoint_hook_();
		last_checkpoint_time_ = now;
	}

	void ResumableLinearHullCollectorCursor::maybe_print_streaming_progress()
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
	void ResumableLinearHullCollectorCursor::clear_rebuildable_vector( std::vector<T>& values, std::size_t keep_capacity )
	{
		values.clear();
		if ( values.capacity() > keep_capacity && memory_governor_in_pressure() )
		{
			std::vector<T> empty;
			values.swap( empty );
		}
	}

	bool ResumableLinearHullCollectorCursor::use_split8_streaming_add_cursor() const
	{
		const auto contract = linear_varvar_row_q2_contract( search_configuration );
		return search_configuration.search_mode == SearchMode::Strict &&
			   contract.kind == LinearVarVarRowQ2ContractKind::Split8ExactPath;
	}

	bool ResumableLinearHullCollectorCursor::use_weight_sliced_clat_streaming_add_cursor() const
	{
		const auto contract = linear_varvar_row_q2_contract( search_configuration );
		return search_configuration.search_mode == SearchMode::Strict &&
			   contract.kind == LinearVarVarRowQ2ContractKind::WeightSlicedClatExactShellIndex;
	}

	std::optional<VarConstSubColumnOptimalOnOutputWire> ResumableLinearHullCollectorCursor::compute_second_subconst_q2_floor( LinearRoundSearchState& state ) const
	{
		return compute_varconst_step_q2_floor(
			state,
			LinearVarConstStageSlot::SecondConst,
			search_configuration );
	}

	SearchWeight ResumableLinearHullCollectorCursor::ensure_second_subconst_q2_floor( LinearRoundSearchState& state ) const
	{
		return ensure_varconst_step_q2_floor(
			state,
			LinearVarConstStageSlot::SecondConst,
			search_configuration );
	}

	std::optional<VarConstSubColumnOptimalOnOutputWire> ResumableLinearHullCollectorCursor::compute_first_subconst_q2_floor( LinearRoundSearchState& state ) const
	{
		return compute_varconst_step_q2_floor(
			state,
			LinearVarConstStageSlot::FirstSubconst,
			search_configuration );
	}

	SearchWeight ResumableLinearHullCollectorCursor::ensure_first_subconst_q2_floor( LinearRoundSearchState& state ) const
	{
		return ensure_varconst_step_q2_floor(
			state,
			LinearVarConstStageSlot::FirstSubconst,
			search_configuration );
	}

	LinearFixedBetaOuterHotPathEnterStatus ResumableLinearHullCollectorCursor::enter_second_subconst_q2_gate( LinearRoundSearchState& state )
	{
		return enter_varconst_step_q2(
			state,
			LinearVarConstStageSlot::SecondConst,
			search_configuration,
			LinearFixedBetaHotPathCallSite::CollectorPrepareSecondSubconst,
			[this]() { return sync_and_check_runtime_stop(); } );
	}

	LinearFixedBetaOuterHotPathEnterStatus ResumableLinearHullCollectorCursor::enter_first_subconst_q2_gate( LinearRoundSearchState& state )
	{
		return enter_varconst_step_q2(
			state,
			LinearVarConstStageSlot::FirstSubconst,
			search_configuration,
			LinearFixedBetaHotPathCallSite::CollectorPrepareFirstSubconst,
			[this]() { return sync_and_check_runtime_stop(); } );
	}

	void ResumableLinearHullCollectorCursor::clear_first_stage_buffers( LinearRoundSearchState& state )
	{
		clear_rebuildable_vector( state.first_constant_subtraction_candidates_for_branch_a );
		state.first_constant_subtraction_stream_cursor = {};
		clear_rebuildable_vector( state.first_addition_candidates_for_branch_b );
		state.first_addition_stream_cursor = {};
		state.first_addition_weight_sliced_clat_stream_cursor = {};
		state.first_constant_subtraction_candidate_index = 0;
		state.first_addition_candidate_index = 0;
		state.first_add_trail_sum_wire_u = 0;
		state.first_subconst_weight_floor = INFINITE_WEIGHT;
	}

	void ResumableLinearHullCollectorCursor::clear_second_stage_buffers( LinearRoundSearchState& state )
	{
		state.second_constant_subtraction_stream_cursor = {};
		clear_rebuildable_vector( state.second_constant_subtraction_candidates_for_branch_b );
		clear_rebuildable_vector( state.second_addition_candidates_storage );
		state.second_addition_stream_cursor = {};
		state.second_addition_weight_sliced_clat_stream_cursor = {};
		state.second_addition_candidates_for_branch_a = nullptr;
		state.second_addition_candidate_index = 0;
		state.second_constant_subtraction_candidate_index = 0;
		state.second_add_trail_sum_wire_u = 0;
		state.second_subconst_weight_floor = INFINITE_WEIGHT;
		state.injection_from_branch_b_enumerator = {};
		clear_first_stage_buffers( state );
	}

	// Enter the same canonical nonlinear stage pipeline used by best-search so hull
	// collection and best-search stay mathematically comparable.
	void ResumableLinearHullCollectorCursor::begin_streaming_round()
	{
		auto& frame = current_frame();
		auto& state = frame.state;
		clear_second_stage_buffers( state );
		state.injection_from_branch_a_enumerator.reset(
			state.injection_from_branch_a_transition,
			search_configuration.maximum_injection_input_masks );
		frame.stage = LinearSearchStage::InjA;
	}

	void ResumableLinearHullCollectorCursor::collect_current_trail( SearchWeight total_weight, std::uint32_t input_branch_a_mask, std::uint32_t input_branch_b_mask )
	{
		const long double exact_signed_correlation = std::pow( 2.0L, -static_cast<long double>( total_weight ) );
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
			result_.strongest_trail = context.current_linear_trail;
		}
		if ( on_trail_ )
		{
			const LinearHullCollectedTrailView trail_view {
				total_weight,
				input_branch_a_mask,
				input_branch_b_mask,
				exact_signed_correlation,
				&context.current_linear_trail
			};
			if ( !on_trail_( trail_view ) )
				result_.hit_callback_stop = true;
		}
		if ( state_.maximum_collected_trails != 0 && result_.collected_trail_count >= state_.maximum_collected_trails )
			result_.hit_collection_limit = true;
	}

bool ResumableLinearHullCollectorCursor::advance_stage_inj_a()
{
	if ( poll_shared_runtime() )
		return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::InjA,
		state,
		state.accumulated_weight_so_far ) )
	{
		pop_frame();
		return true;
	}
	while ( true )
	{
		std::uint32_t chosen_mask = 0;
		if ( !state.injection_from_branch_a_enumerator.next( context, state.injection_from_branch_a_transition, chosen_mask ) )
		{
			if ( sync_and_check_runtime_stop() )
				return false;
			clear_second_stage_buffers( state );
			pop_frame();
			return true;
		}

		if ( sync_and_check_runtime_stop() )
			return false;

		maybe_print_streaming_progress();
		state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_mask;
		linear_project_second_subround_outputs_from_inj_a_choice( state );

		switch ( enter_second_subconst_q2_gate( state ) )
		{
		case LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate:
			continue;
		case LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop:
			return false;
		case LinearFixedBetaOuterHotPathEnterStatus::Prepared:
		default:
			break;
		}

		if ( !prepare_varvar_step_q2(
				state,
				LinearVarVarStageSlot::SecondAdd,
				search_configuration,
				[this]() { return sync_and_check_runtime_stop(); } ) )
		{
			return false;
		}
		state.injection_from_branch_b_enumerator = {};
		state.second_constant_subtraction_candidate_index = 0;
		clear_first_stage_buffers( state );
		frame.stage = LinearSearchStage::SecondAdd;
		return true;
	}
}

	bool ResumableLinearHullCollectorCursor::advance_stage_second_add()
	{
		if ( poll_shared_runtime() )
			return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::SecondAdd,
		state,
		state.accumulated_weight_so_far + state.weight_injection_from_branch_a ) )
	{
		frame.stage = LinearSearchStage::InjA;
		return true;
	}
	LinearVarVarQ2Candidate q2_candidate {};
		while ( true )
		{
			if ( linear_runtime_node_limit_hit( context ) )
			{
				result_.hit_maximum_search_nodes = true;
				return false;
			}
			if ( linear_note_runtime_node_visit( context ) )
			{
				result_.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
				result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context );
				return false;
			}
			maybe_print_streaming_progress();

			if ( !next_varvar_step_q2_candidate(
					state,
					LinearVarVarStageSlot::SecondAdd,
					search_configuration,
					q2_candidate ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				break;
			}
			const auto q1_weight =
				evaluate_varvar_q1_exact_weight( q2_candidate, 32 );
			if ( !q1_weight.has_value() )
				continue;
			SearchWeight weight_second_addition = q1_weight.value();
			linear_load_second_add_candidate_state(
				state,
				q2_candidate,
				weight_second_addition );
			if ( state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap )
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::InjB;
				linear_load_second_add_candidate_state(
					snapshot.state,
					q2_candidate,
					weight_second_addition );
				linear_prepare_inj_b_bridge_after_second_add( snapshot.state );
				snapshot.state.second_constant_subtraction_candidate_index = 0;
				snapshot.state.injection_from_branch_b_enumerator.reset(
					snapshot.state.injection_from_branch_b_transition,
					search_configuration.maximum_injection_input_masks );
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::InjB,
						snapshot.state,
						state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition );
				if ( q2_candidate.ordered_stream.safe_break_when_cap_exceeded )
					break;
				continue;
			}

			linear_prepare_inj_b_bridge_after_second_add( state );
			if ( state.base_weight_after_inj_b >= state.round_weight_cap )
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::SecondConst;
				snapshot.state.second_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::SecondConst,
						state.input_branch_a_mask_before_second_addition,
						state.branch_b_mask_contribution_from_second_addition_term,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_inj_b );
				continue;
			}
			if ( state.base_weight_after_inj_b + ensure_second_subconst_q2_floor( state ) >= state.round_weight_cap )
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::SecondConst;
				snapshot.state.second_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::SecondConst,
						state.input_branch_a_mask_before_second_addition,
						state.branch_b_mask_contribution_from_second_addition_term,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_inj_b );
				continue;
			}

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

	bool ResumableLinearHullCollectorCursor::advance_stage_inj_b()
	{
		if ( poll_shared_runtime() )
			return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::InjB,
		state,
		state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition ) )
	{
		frame.stage = LinearSearchStage::SecondAdd;
		return true;
	}
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

	bool ResumableLinearHullCollectorCursor::advance_stage_second_const()
	{
		if ( poll_shared_runtime() )
			return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::SecondConst,
		state,
		state.accumulated_weight_so_far + state.base_weight_after_inj_b ) )
	{
		frame.stage = LinearSearchStage::InjB;
		return true;
	}
	LinearVarConstQ2Candidate q2_candidate {};
		constexpr auto call_site = LinearFixedBetaHotPathCallSite::CollectorPrepareSecondSubconst;
		while ( true )
		{
			if ( linear_runtime_node_limit_hit( context ) )
			{
				result_.hit_maximum_search_nodes = true;
				return false;
			}
			if ( linear_note_runtime_node_visit( context ) )
			{
				result_.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
				result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context );
				return false;
			}
			maybe_print_streaming_progress();

			if ( !next_varconst_step_q2_candidate(
					state,
					LinearVarConstStageSlot::SecondConst,
					search_configuration,
					call_site,
					q2_candidate ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				break;
			}
			const auto q1_weight =
				evaluate_varconst_q1_exact_weight(
					q2_candidate,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					state.second_subconst_weight_cap,
					&state.second_subconst_fixed_alpha_weight_floor,
					32 );
			if ( !q1_weight.has_value() )
				continue;
			linear_load_second_subconst_candidate_state(
				state,
				q2_candidate,
				q1_weight.value() );
			if ( state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap )
			{
				linear_project_first_subround_outputs_from_inj_b_choice( state );
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::FirstSubconst;
				snapshot.state.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
				snapshot.state.base_weight_after_second_subconst = state.base_weight_after_second_subconst;
				snapshot.state.first_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::FirstSubconst,
						state,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction );
				if ( q2_candidate.ordered_stream.safe_break_when_cap_exceeded )
				{
					state.second_constant_subtraction_candidate_index = state.second_constant_subtraction_candidates_for_branch_b.size();
					break;
				}
				continue;
			}

			if ( state.base_weight_after_second_subconst >= state.round_weight_cap )
				continue;
			if ( !linear_prepare_first_subround_caps( state, search_configuration ) )
				continue;
			linear_project_first_subround_outputs_from_inj_b_choice( state );

			if ( state.base_weight_after_second_subconst + ensure_first_subconst_q2_floor( state ) >= state.round_weight_cap )
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::FirstSubconst;
				snapshot.state.first_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::FirstSubconst,
						state,
						state.accumulated_weight_so_far + state.base_weight_after_second_subconst ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_second_subconst );
				continue;
			}

			switch ( enter_first_subconst_q2_gate( state ) )
			{
			case LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate:
				continue;
			case LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop:
				return false;
			case LinearFixedBetaOuterHotPathEnterStatus::Prepared:
			default:
				break;
			}

			if ( !prepare_varvar_step_q2(
					state,
					LinearVarVarStageSlot::FirstAdd,
					search_configuration,
					[this]() { return sync_and_check_runtime_stop(); } ) )
			{
				return false;
			}
			state.first_constant_subtraction_candidate_index = 0;
			frame.stage = LinearSearchStage::FirstSubconst;
			return true;
		}

		clear_first_stage_buffers( state );
		frame.stage = LinearSearchStage::InjB;
		return true;
	}

	bool ResumableLinearHullCollectorCursor::advance_stage_first_subconst()
	{
		if ( poll_shared_runtime() )
			return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::FirstSubconst,
		state,
		state.accumulated_weight_so_far + state.base_weight_after_second_subconst ) )
	{
		frame.stage = LinearSearchStage::SecondConst;
		return true;
	}
	LinearVarConstQ2Candidate q2_candidate {};
		constexpr auto call_site = LinearFixedBetaHotPathCallSite::CollectorPrepareFirstSubconst;
		while ( true )
		{
			if ( linear_runtime_node_limit_hit( context ) )
			{
				result_.hit_maximum_search_nodes = true;
				return false;
			}
			if ( linear_note_runtime_node_visit( context ) )
			{
				result_.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
				result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context );
				return false;
			}
			maybe_print_streaming_progress();

			if ( !next_varconst_step_q2_candidate(
					state,
					LinearVarConstStageSlot::FirstSubconst,
					search_configuration,
					call_site,
					q2_candidate ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				break;
			}
			const auto q1_weight =
				evaluate_varconst_q1_exact_weight(
					q2_candidate,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					state.first_subconst_weight_cap,
					&state.first_subconst_fixed_alpha_weight_floor,
					32 );
			if ( !q1_weight.has_value() )
				continue;
			const SearchWeight base_weight_after_first_subconst = state.base_weight_after_second_subconst + q1_weight.value();
			if ( base_weight_after_first_subconst >= state.round_weight_cap )
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::FirstAdd;
				snapshot.state.input_branch_a_mask_before_first_constant_subtraction_current = q2_candidate.input_mask_alpha;
				snapshot.state.weight_first_constant_subtraction_current = q1_weight.value();
				snapshot.state.first_addition_candidate_index = 0;
				(void)try_register_linear_hull_child_residual_candidate(
					context,
					make_linear_hull_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::FirstAdd,
						q2_candidate.input_mask_alpha,
						state.output_branch_b_mask_after_first_addition,
						state.accumulated_weight_so_far + base_weight_after_first_subconst ),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + base_weight_after_first_subconst );
				if ( q2_candidate.ordered_stream.safe_break_when_cap_exceeded )
				{
					state.first_constant_subtraction_candidate_index = state.first_constant_subtraction_candidates_for_branch_a.size();
					break;
				}
				continue;
			}
			state.input_branch_a_mask_before_first_constant_subtraction_current = q2_candidate.input_mask_alpha;
			state.weight_first_constant_subtraction_current = q1_weight.value();
			state.first_addition_candidate_index = 0;
			frame.stage = LinearSearchStage::FirstAdd;
			return true;
		}

		frame.stage = LinearSearchStage::SecondConst;
		return true;
	}

	bool ResumableLinearHullCollectorCursor::advance_stage_first_add()
	{
		if ( poll_shared_runtime() )
			return false;

	auto& frame = current_frame();
	auto& state = frame.state;
	if ( should_prune_local_state_dominance(
		LinearSearchStage::FirstAdd,
		state,
		state.accumulated_weight_so_far + state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current ) )
	{
		frame.stage = LinearSearchStage::FirstSubconst;
		return true;
	}
	const SearchWeight base_weight_after_first_subconst = state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current;
		LinearVarVarQ2Candidate q2_candidate {};
		while ( true )
		{
			if ( linear_runtime_node_limit_hit( context ) )
			{
				result_.hit_maximum_search_nodes = true;
				return false;
			}
			if ( linear_note_runtime_node_visit( context ) )
			{
				result_.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
				result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context );
				return false;
			}
			maybe_print_streaming_progress();

			if ( !next_varvar_step_q2_candidate(
					state,
					LinearVarVarStageSlot::FirstAdd,
					search_configuration,
					q2_candidate ) )
			{
				if ( sync_and_check_runtime_stop() )
					return false;
				break;
			}
			const auto q1_weight =
				evaluate_varvar_q1_exact_weight( q2_candidate, 32 );
			if ( !q1_weight.has_value() )
				continue;
			SearchWeight weight_first_addition = q1_weight.value();
			state.first_add_trail_sum_wire_u = q2_candidate.sum_wire_u;

			const SearchWeight total_w = base_weight_after_first_subconst + weight_first_addition;
			if ( total_w >= state.round_weight_cap )
			{
				const int rounds_remaining_after = search_configuration.round_count - ( state.round_boundary_depth + 1 );
				if ( rounds_remaining_after > 0 )
				{
					auto trail_snapshot = context.current_linear_trail;
					const LinearTrailStepRecord step =
						linear_build_round_step(
							state,
							state.input_branch_a_mask_before_first_constant_subtraction_current,
							state.weight_first_constant_subtraction_current,
							q2_candidate,
							weight_first_addition );
					LinearSearchFrame child {};
					child.stage = LinearSearchStage::Enter;
					child.trail_size_at_entry = trail_snapshot.size();
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + total_w;
					child.state.round_output_branch_a_mask = step.input_branch_a_mask;
					child.state.round_output_branch_b_mask = step.input_branch_b_mask;
					(void)try_register_linear_hull_child_residual_candidate(
						context,
						make_linear_hull_boundary_record(
							context,
							rounds_remaining_after,
							LinearSearchStage::Enter,
							step.input_branch_a_mask,
							step.input_branch_b_mask,
							state.accumulated_weight_so_far + total_w ),
						&child,
						&trail_snapshot,
						state.accumulated_weight_so_far + total_w );
				}
				if ( q2_candidate.ordered_stream.safe_break_when_cap_exceeded )
					break;
				continue;
			}

			LinearTrailStepRecord step =
				linear_build_round_step(
					state,
					state.input_branch_a_mask_before_first_constant_subtraction_current,
					state.weight_first_constant_subtraction_current,
					q2_candidate,
					weight_first_addition );

			context.current_linear_trail.push_back( step );
			LinearSearchFrame child {};
			child.stage = LinearSearchStage::Enter;
			child.trail_size_at_entry = context.current_linear_trail.size() - 1u;
			child.state.round_boundary_depth = state.round_boundary_depth + 1;
			child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + step.round_weight;
			child.state.round_output_branch_a_mask = step.input_branch_a_mask;
			child.state.round_output_branch_b_mask = step.input_branch_b_mask;
			cursor.stack.push_back( child );
			return true;
		}

		frame.stage = LinearSearchStage::FirstSubconst;
		return true;
	}

	bool ResumableLinearHullCollectorCursor::should_stop_search( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight )
	{
		if ( result_.hit_callback_stop )
			return true;
		if ( state_.maximum_collected_trails != 0 && result_.collected_trail_count >= state_.maximum_collected_trails )
		{
			result_.hit_collection_limit = true;
			return true;
		}
		if ( context.stop_due_to_time_limit )
			return true;
		if ( linear_runtime_node_limit_hit( context ) )
		{
			result_.hit_maximum_search_nodes = true;
			return true;
		}
		if ( linear_note_runtime_node_visit( context ) )
		{
			result_.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
			result_.hit_maximum_search_nodes = linear_runtime_node_limit_hit( context );
			return true;
		}
		maybe_print_single_run_progress( context, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask );
		maybe_poll_checkpoint();
		if ( accumulated_weight >= context.best_weight )
			return true;
		if ( should_prune_remaining_round_lower_bound( depth, accumulated_weight ) )
			return true;
		return false;
	}

	bool ResumableLinearHullCollectorCursor::should_prune_local_state_dominance(
		LinearSearchStage stage_cursor,
		std::uint32_t pair_a,
		std::uint32_t pair_b,
		std::uint32_t pair_c,
		SearchWeight prefix_weight )
	{
		return context.local_state_dominance.should_prune_or_update(
			static_cast<std::uint8_t>( stage_cursor ),
			pair_a,
			pair_b,
			pair_c,
			prefix_weight );
	}

	bool ResumableLinearHullCollectorCursor::should_prune_local_state_dominance(
		LinearSearchStage stage_cursor,
		std::uint32_t pair_a,
		std::uint32_t pair_b,
		SearchWeight prefix_weight )
	{
		return should_prune_local_state_dominance(
			stage_cursor,
			pair_a,
			pair_b,
			0u,
			prefix_weight );
	}

	bool ResumableLinearHullCollectorCursor::should_prune_local_state_dominance(
		LinearSearchStage stage_cursor,
		const LinearRoundSearchState& state,
		SearchWeight prefix_weight )
	{
		const LinearResidualSemanticKeyParts key = linear_semantic_key_for_stage( stage_cursor, state );
		return should_prune_local_state_dominance(
			stage_cursor,
			key.pair_a,
			key.pair_b,
			key.pair_c,
			prefix_weight );
	}

	bool ResumableLinearHullCollectorCursor::should_prune_remaining_round_lower_bound( int depth, SearchWeight accumulated_weight ) const
	{
		if ( context.best_weight >= INFINITE_WEIGHT )
			return false;
		if ( !search_configuration.enable_remaining_round_lower_bound )
			return false;
		const int rounds_left = search_configuration.round_count - depth;
		if ( rounds_left < 0 )
			return false;
		const auto& table = search_configuration.remaining_round_min_weight;
		const std::size_t index = std::size_t( rounds_left );
		if ( index >= table.size() )
			return false;
		return accumulated_weight + table[ index ] >= context.best_weight;
	}

	bool ResumableLinearHullCollectorCursor::handle_round_end_if_needed( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight )
	{
		if ( depth != search_configuration.round_count )
			return false;
		collect_current_trail( accumulated_weight, current_round_output_branch_a_mask, current_round_output_branch_b_mask );
		return true;
	}

	// Prepare the round-local weight budget for hull collection.
	//
	// Even though the collector aggregates all trails up to a fixed cap rather than improving
	// a best bound, the pruning chain must stay identical to the best-search engine:
	// exact injection weight, exact/additional var-var cost, exact var-const floor/witness
	// weight, then downstream remaining-round lower bound.
	bool ResumableLinearHullCollectorCursor::prepare_round_state( int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight )
	{
		auto& state = current_round_state();
		state.round_boundary_depth = depth;
		state.accumulated_weight_so_far = accumulated_weight;
		state.round_index = search_configuration.round_count - depth;
		state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round( depth );
		const SearchWeight base_cap = ( context.best_weight >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : remaining_search_weight_budget( context.best_weight, accumulated_weight );
		state.round_weight_cap = ( base_cap >= INFINITE_WEIGHT ) ? INFINITE_WEIGHT : remaining_search_weight_budget( base_cap, state.remaining_round_weight_lower_bound_after_this_round );
		if ( state.round_weight_cap == 0 )
			return false;

		linear_prepare_round_entry_bridge(
			state,
			current_round_output_branch_a_mask,
			current_round_output_branch_b_mask );
		if ( state.weight_injection_from_branch_a >= state.round_weight_cap )
			return false;

		state.remaining_after_inj_a = state.round_weight_cap - state.weight_injection_from_branch_a;
		if ( state.remaining_after_inj_a == 0 )
			return false;

		state.second_subconst_weight_cap = std::min( search_configuration.constant_subtraction_weight_cap, state.remaining_after_inj_a - 1 );
		state.second_add_weight_cap = std::min( search_configuration.addition_weight_cap, state.remaining_after_inj_a - 1 );
		state.second_addition_candidates_for_branch_a = nullptr;
		return true;
	}

	SearchWeight ResumableLinearHullCollectorCursor::compute_remaining_round_weight_lower_bound_after_this_round( int depth ) const
	{
		if ( !search_configuration.enable_remaining_round_lower_bound )
			return 0;
		const int rounds_left_after = search_configuration.round_count - ( depth + 1 );
		if ( rounds_left_after < 0 )
			return 0;
		const auto& table = search_configuration.remaining_round_min_weight;
		const std::size_t idx = std::size_t( rounds_left_after );
		if ( idx >= table.size() )
			return 0;
		return table[ idx ];
	}

	void ResumableLinearHullCollectorCursor::run()
	{
		while ( !cursor.stack.empty() )
		{
			LinearSearchFrame& frame = current_frame();
			LinearRoundSearchState& state = frame.state;

			switch ( frame.stage )
			{
			case LinearSearchStage::Enter:
			{
				if ( should_stop_search( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
				{
					if ( result_.hit_collection_limit || result_.hit_maximum_search_nodes || result_.hit_callback_stop || context.stop_due_to_time_limit )
						return;
					const int rounds_remaining = search_configuration.round_count - state.round_boundary_depth;
					if ( rounds_remaining > 0 )
					{
						(void)try_register_linear_hull_child_residual_candidate(
							context,
							make_linear_hull_boundary_record(
								context,
								rounds_remaining,
								LinearSearchStage::Enter,
								state.round_output_branch_a_mask,
								state.round_output_branch_b_mask,
								state.accumulated_weight_so_far ),
							&frame,
							&context.current_linear_trail,
							state.accumulated_weight_so_far );
					}
					pop_frame();
					break;
				}
				if ( handle_round_end_if_needed( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
				{
					if ( result_.hit_collection_limit || result_.hit_callback_stop )
						return;
					pop_frame();
					break;
				}
			if ( should_prune_local_state_dominance(
				LinearSearchStage::Enter,
				state,
				state.accumulated_weight_so_far ) )
				{
					pop_frame();
					break;
				}
				if ( !prepare_round_state( state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far ) )
				{
					const int rounds_remaining = search_configuration.round_count - state.round_boundary_depth;
					if ( rounds_remaining > 0 )
					{
						(void)try_register_linear_hull_child_residual_candidate(
							context,
							make_linear_hull_boundary_record(
								context,
								rounds_remaining,
								LinearSearchStage::Enter,
								state.round_output_branch_a_mask,
								state.round_output_branch_b_mask,
								state.accumulated_weight_so_far ),
							&frame,
							&context.current_linear_trail,
							state.accumulated_weight_so_far );
					}
					pop_frame();
					break;
				}
				begin_streaming_round();
				break;
			}
			case LinearSearchStage::InjA:
				if ( !advance_stage_inj_a() ) return;
				break;
			case LinearSearchStage::SecondAdd:
				if ( !advance_stage_second_add() ) return;
				break;
			case LinearSearchStage::InjB:
				if ( !advance_stage_inj_b() ) return;
				break;
			case LinearSearchStage::SecondConst:
				if ( !advance_stage_second_const() ) return;
				break;
			case LinearSearchStage::FirstSubconst:
				if ( !advance_stage_first_subconst() ) return;
				break;
			case LinearSearchStage::FirstAdd:
				if ( !advance_stage_first_add() ) return;
				break;
			default:
				pop_frame();
				break;
			}
			maybe_poll_checkpoint();
		}
	}

	LinearHullAggregationResult collect_linear_hull_exact(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& base_configuration,
		const LinearHullCollectionOptions& options = {} )
	{
		LinearHullCollectorExecutionState state {};
		initialize_linear_hull_collection_state( output_branch_a_mask, output_branch_b_mask, base_configuration, options, state );
		continue_linear_hull_collection_from_state( state, options.on_trail );
		return state.aggregation_result;
	}

	void initialize_linear_hull_collection_state(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& base_configuration,
		const LinearHullCollectionOptions& options,
		LinearHullCollectorExecutionState& state )
	{
		state.~LinearHullCollectorExecutionState();
		::new ( static_cast<void*>( &state ) ) LinearHullCollectorExecutionState {};
		state.collect_weight_cap = options.collect_weight_cap;
		state.maximum_collected_trails = options.maximum_collected_trails;
		state.residual_boundary_mode = options.residual_boundary_mode;
		state.context.configuration = base_configuration;
		state.context.runtime_controls = options.runtime_controls;
		state.context.start_output_branch_a_mask = output_branch_a_mask;
		state.context.start_output_branch_b_mask = output_branch_b_mask;
		prepare_linear_remaining_round_lower_bound_table(
			state.context.configuration,
			state.context.configuration.round_count,
			output_branch_a_mask,
			output_branch_b_mask );
		state.context.configuration.enable_state_memoization = false;
		state.context.best_weight = ( state.collect_weight_cap >= INFINITE_WEIGHT - 1 ) ? INFINITE_WEIGHT : ( state.collect_weight_cap + 1 );
		state.context.progress_every_seconds = state.context.runtime_controls.progress_every_seconds;
		state.context.current_linear_trail.reserve( std::size_t( std::max( 1, state.context.configuration.round_count ) ) );
		state.aggregation_result.collect_weight_cap = state.collect_weight_cap;

		state.cursor.stack.clear();
		state.cursor.stack.reserve( std::size_t( std::max( 1, state.context.configuration.round_count ) ) + 1u );
		state.context.current_linear_trail.clear();
		state.context.pending_frontier.clear();
		state.context.pending_frontier_entries.clear();
		state.context.pending_frontier_index_by_key.clear();
		state.context.pending_frontier_entry_index_by_key.clear();
		LinearSearchFrame root_frame {};
		root_frame.stage = LinearSearchStage::Enter;
		root_frame.trail_size_at_entry = state.context.current_linear_trail.size();
		root_frame.state.round_boundary_depth = 0;
		root_frame.state.accumulated_weight_so_far = 0;
		root_frame.state.round_output_branch_a_mask = output_branch_a_mask;
		root_frame.state.round_output_branch_b_mask = output_branch_b_mask;
		state.cursor.stack.push_back( root_frame );
	}

	void continue_linear_hull_collection_from_state(
		LinearHullCollectorExecutionState& state,
		LinearHullTrailCallback on_trail,
		std::function<void()> checkpoint_hook )
	{
		LinearHullBNBScheduler( state ).run( std::move( on_trail ), std::move( checkpoint_hook ) );
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
		if ( !linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_search(
				 base_configuration.varvar_modular_add_bnb_mode ) )
		{
			runtime_result.strict_runtime_rejection_reason =
				StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode;
			return runtime_result;
		}

		LinearHullCollectionOptions collection_options {};
		collection_options.maximum_collected_trails = options.maximum_collected_trails;
		collection_options.runtime_controls = options.runtime_controls;
		collection_options.on_trail = options.on_trail;

		if ( options.collect_cap_mode == HullCollectCapMode::ExplicitCap )
		{
			runtime_result.collect_weight_cap = std::min<SearchWeight>( options.explicit_collect_weight_cap, INFINITE_WEIGHT - 1 );
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
		const SearchWeight seeded_upper_bound_weight =
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
				runtime_result.best_search_result.best_weight + options.collect_weight_window );
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
