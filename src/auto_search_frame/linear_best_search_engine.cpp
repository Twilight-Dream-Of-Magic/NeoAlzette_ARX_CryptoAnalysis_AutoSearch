#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"
#include "auto_search_frame/detail/remaining_round_lower_bound_bootstrap.hpp"

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
			// In absolute-correlation mask transport, xor-by-constant preserves the mask
			// wire and costs weight 0. The sign-phase is intentionally not modeled here.
			return branch_b_output_mask_after_injection;
		}

		inline std::uint32_t linear_mask_after_explicit_prewhitening_before_injection_from_branch_b(
			std::uint32_t branch_a_output_mask_after_injection ) noexcept
		{
			// Explicit split step:
			//   B_pre = B_raw xor RC[4]
			// Absolute-correlation mask propagation through xor-by-constant is identity.
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

		struct LinearEngineResidualFrontierHelper final
		{
			LinearEngineResidualFrontierHelper(LinearBestSearchContext& context_in, LinearSearchCursor& cursor_in)
				: context(context_in), cursor(cursor_in)
			{
			}

			TwilightDream::residual_frontier_shared::ResidualProblemRecord make_root_source_record() const noexcept
			{
				const auto& counters = context.residual_counters;
				const std::uint64_t sequence =
					counters.interrupted_source_input_pair_count +
					counters.completed_source_input_pair_count +
					1u;
				auto record = TwilightDream::residual_frontier_shared::make_residual_problem_record(
					TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Linear,
					TwilightDream::residual_frontier_shared::ResidualObjectiveKind::BestWeight,
					context.configuration.round_count,
					normalize_linear_residual_stage_cursor( LinearSearchStage::Enter ),
					context.start_output_branch_a_mask,
					context.start_output_branch_b_mask,
					SearchWeight(0),
					sequence,
					context.run_visited_node_count);
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, context.configuration.round_count );
				record.key.suffix_profile_id =
					compute_linear_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = root_residual_source_tag();
				return record;
			}

			TwilightDream::residual_frontier_shared::ResidualProblemRecord make_boundary_record(
				std::int32_t rounds_remaining,
				LinearSearchStage stage_cursor,
				std::uint32_t pair_a,
				std::uint32_t pair_b,
				std::uint32_t pair_c,
				SearchWeight best_prefix_weight) const noexcept
			{
				const auto& counters = context.residual_counters;
				const std::uint64_t sequence =
					counters.interrupted_output_as_next_input_pair_count +
					counters.completed_output_as_next_input_pair_count +
					1u;
				auto record = TwilightDream::residual_frontier_shared::make_residual_problem_record(
					TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Linear,
					TwilightDream::residual_frontier_shared::ResidualObjectiveKind::BestWeight,
					rounds_remaining,
					normalize_linear_residual_stage_cursor( stage_cursor ),
					pair_a,
					pair_b,
					best_prefix_weight,
					sequence,
					context.run_visited_node_count);
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, rounds_remaining );
				record.key.suffix_profile_id =
					compute_linear_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = child_residual_source_tag();
				record.key.pair_c = pair_c;
				return record;
			}

			void emit_source_pair_event(
				TwilightDream::residual_frontier_shared::ResidualPairEventKind kind,
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				bool persistent)
			{
				using namespace TwilightDream::residual_frontier_shared;
				if (kind == ResidualPairEventKind::InterruptedSourceInputPair)
					++context.residual_counters.interrupted_source_input_pair_count;
				else if (kind == ResidualPairEventKind::CompletedSourceInputPair)
					++context.residual_counters.completed_source_input_pair_count;

				{
					std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
					TwilightDream::runtime_component::print_progress_prefix(std::cout);
					std::cout << "[Residual][Linear] event=" << residual_pair_event_kind_to_string(kind)
						<< " rounds_remaining=" << record.key.rounds_remaining
						<< " stage_cursor=" << unsigned(record.key.stage_cursor)
						<< " interrupted_source_input_pair_count=" << context.residual_counters.interrupted_source_input_pair_count
						<< " completed_source_input_pair_count=" << context.residual_counters.completed_source_input_pair_count
						<< " ";
					write_residual_problem_key_debug_fields_inline(std::cout, record.key);
					std::cout << " ";
					print_word32_hex("pair_a=", record.key.pair_a);
					std::cout << " ";
					print_word32_hex("pair_b=", record.key.pair_b);
					std::cout << "\n";
				}

				if (!persistent || !context.runtime_event_log)
					return;
				context.runtime_event_log->write_event(
					residual_pair_event_kind_to_string(kind),
					[&](std::ostream& out) {
						out << "domain=linear\n";
						out << "rounds_remaining=" << record.key.rounds_remaining << "\n";
						out << "stage_cursor=" << unsigned(record.key.stage_cursor) << "\n";
						write_residual_problem_key_debug_fields_multiline(out, record.key);
						out << "pair_a=" << hex8(record.key.pair_a) << "\n";
						out << "pair_b=" << hex8(record.key.pair_b) << "\n";
						out << "interrupted_source_input_pair_count=" << context.residual_counters.interrupted_source_input_pair_count << "\n";
						out << "completed_source_input_pair_count=" << context.residual_counters.completed_source_input_pair_count << "\n";
					});
			}

			void emit_interrupted_output_pair_progress(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				std::uint64_t recent_added_count)
			{
				using namespace TwilightDream::residual_frontier_shared;
				std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
				TwilightDream::runtime_component::print_progress_prefix(std::cout);
				std::cout << "[Residual][Linear] event=" << residual_pair_event_kind_to_string(ResidualPairEventKind::InterruptedOutputAsNextInputPair)
					<< " rounds_remaining=" << record.key.rounds_remaining
					<< " stage_cursor=" << unsigned(record.key.stage_cursor)
					<< " interrupted_output_as_next_input_pair_count=" << context.residual_counters.interrupted_output_as_next_input_pair_count
					<< " recent_added=" << recent_added_count
					<< " ";
				write_residual_problem_key_debug_fields_inline(std::cout, record.key);
				std::cout << " ";
				print_word32_hex("pair_a=", record.key.pair_a);
				std::cout << " ";
				print_word32_hex("pair_b=", record.key.pair_b);
				std::cout << "\n";
			}

			void emit_completed_output_pair_event(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record)
			{
				using namespace TwilightDream::residual_frontier_shared;
				++context.residual_counters.completed_output_as_next_input_pair_count;
				{
					std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
					TwilightDream::runtime_component::print_progress_prefix(std::cout);
					std::cout << "[Residual][Linear] event=" << residual_pair_event_kind_to_string(ResidualPairEventKind::CompletedOutputAsNextInputPair)
						<< " rounds_remaining=" << record.key.rounds_remaining
						<< " stage_cursor=" << unsigned(record.key.stage_cursor)
						<< " completed_output_as_next_input_pair_count=" << context.residual_counters.completed_output_as_next_input_pair_count
						<< " ";
					write_residual_problem_key_debug_fields_inline(std::cout, record.key);
					std::cout << " ";
					print_word32_hex("pair_a=", record.key.pair_a);
					std::cout << " ";
					print_word32_hex("pair_b=", record.key.pair_b);
					std::cout << "\n";
				}
				if (context.runtime_event_log)
				{
					context.runtime_event_log->write_event(
						residual_pair_event_kind_to_string(ResidualPairEventKind::CompletedOutputAsNextInputPair),
						[&](std::ostream& out) {
							out << "domain=linear\n";
							out << "rounds_remaining=" << record.key.rounds_remaining << "\n";
							out << "stage_cursor=" << unsigned(record.key.stage_cursor) << "\n";
							write_residual_problem_key_debug_fields_multiline(out, record.key);
							out << "pair_a=" << hex8(record.key.pair_a) << "\n";
							out << "pair_b=" << hex8(record.key.pair_b) << "\n";
							out << "completed_output_as_next_input_pair_count=" << context.residual_counters.completed_output_as_next_input_pair_count << "\n";
						});
				}
			}

			bool try_enqueue_pending_frontier_record(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record)
			{
				if (auto it = context.pending_frontier_index_by_key.find(record.key); it != context.pending_frontier_index_by_key.end())
				{
					auto& existing = context.pending_frontier[it->second];
					if (existing.best_prefix_weight <= record.best_prefix_weight)
						return false;
					existing = record;
					return true;
				}
				context.pending_frontier_index_by_key.emplace(record.key, context.pending_frontier.size());
				context.pending_frontier.push_back(record);
				return true;
			}

			bool try_enqueue_pending_frontier_entry(const LinearResidualFrontierEntry& entry)
			{
				if (auto it = context.pending_frontier_entry_index_by_key.find(entry.record.key); it != context.pending_frontier_entry_index_by_key.end())
				{
					auto& existing = context.pending_frontier_entries[it->second];
					if (existing.record.best_prefix_weight <= entry.record.best_prefix_weight)
						return false;
					existing = entry;
					return true;
				}
				context.pending_frontier_entry_index_by_key.emplace(entry.record.key, context.pending_frontier_entries.size());
				context.pending_frontier_entries.push_back(entry);
				return true;
			}

			bool try_register_child_residual_candidate(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				const LinearSearchFrame* frame_snapshot = nullptr,
				const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
				SearchWeight prefix_weight_offset = 0)
			{
				using namespace TwilightDream::residual_frontier_shared;
				if (context.completed_residual_set.find(record.key) != context.completed_residual_set.end())
				{
					++context.residual_counters.repeated_or_dominated_residual_skip_count;
					return false;
				}
				if (auto it = context.best_prefix_by_residual_key.find(record.key); it != context.best_prefix_by_residual_key.end())
				{
					if (it->second <= record.best_prefix_weight)
					{
						++context.residual_counters.repeated_or_dominated_residual_skip_count;
						return false;
					}
					it->second = record.best_prefix_weight;
				}
				else
				{
					context.best_prefix_by_residual_key.emplace(record.key, record.best_prefix_weight);
				}

				std::uint64_t recent_added_count = 0;
				bool replaced_existing = false;
				for (auto& existing : context.transient_output_as_next_input_pair_candidates)
				{
					if (existing.key == record.key)
					{
						recent_added_count = 0;
						replaced_existing = true;
						existing = record;
						break;
					}
				}
				if (!replaced_existing)
				{
					context.transient_output_as_next_input_pair_candidates.push_back(record);
					recent_added_count = 1;
				}
				if (frame_snapshot != nullptr && trail_snapshot != nullptr)
				{
					LinearResidualFrontierEntry entry{};
					entry.record = record;
					entry.frame_snapshot = std::make_shared<LinearSearchFrame>(*frame_snapshot);
					entry.current_trail_snapshot = *trail_snapshot;
					entry.prefix_weight_offset = prefix_weight_offset;

					bool replaced_snapshot = false;
					for (auto& existing_entry : context.transient_output_as_next_input_pair_entries)
					{
						if (existing_entry.record.key == record.key)
						{
							existing_entry = std::move(entry);
							replaced_snapshot = true;
							break;
						}
					}
					if (!replaced_snapshot)
						context.transient_output_as_next_input_pair_entries.push_back(std::move(entry));
				}
				++context.residual_counters.interrupted_output_as_next_input_pair_count;
				emit_interrupted_output_pair_progress(record, recent_added_count);
				commit_transient_output_pairs();
				return true;
			}

			void commit_transient_output_pairs()
			{
				for (const auto& record : context.transient_output_as_next_input_pair_candidates)
					(void)try_enqueue_pending_frontier_record(record);
				context.transient_output_as_next_input_pair_candidates.clear();
				for (const auto& entry : context.transient_output_as_next_input_pair_entries)
					(void)try_enqueue_pending_frontier_entry(entry);
				context.transient_output_as_next_input_pair_entries.clear();
			}

			void set_active_problem(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				bool is_root)
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
				bool solved)
			{
				for (auto& existing : context.global_residual_result_table)
				{
					if (existing.key == record.key)
					{
						existing.best_weight = context.best_weight;
						existing.solved = solved;
						return;
					}
				}

				TwilightDream::residual_frontier_shared::ResidualResultRecord result{};
				result.key = record.key;
				result.best_weight = context.best_weight;
				result.solved = solved;
				context.global_residual_result_table.push_back(result);
			}

			void complete_active_problem()
			{
				if (!context.active_problem_valid)
					return;

				const auto record = context.active_problem_record;
				context.completed_residual_set.emplace(record.key);
				upsert_residual_result(record, true);
				if (context.active_problem_is_root)
				{
					context.completed_source_input_pairs.push_back(record);
					emit_source_pair_event(
						TwilightDream::residual_frontier_shared::ResidualPairEventKind::CompletedSourceInputPair,
						record,
						true);
				}
				else
				{
					context.completed_output_as_next_input_pairs.push_back(record);
					emit_completed_output_pair_event(record);
				}
				clear_active_problem();
			}

			void interrupt_root_if_needed(bool hit_node_limit, bool hit_time_limit)
			{
				if (!context.active_problem_valid || !context.active_problem_is_root)
					return;
				if (!hit_node_limit && !hit_time_limit)
					return;

				emit_source_pair_event(
					TwilightDream::residual_frontier_shared::ResidualPairEventKind::InterruptedSourceInputPair,
					context.active_problem_record,
					true);
			}

			void rebuild_pending_frontier_indexes()
			{
				context.pending_frontier_index_by_key.clear();
				for (std::size_t i = 0; i < context.pending_frontier.size(); ++i)
					context.pending_frontier_index_by_key[context.pending_frontier[i].key] = i;
				context.pending_frontier_entry_index_by_key.clear();
				for (std::size_t i = 0; i < context.pending_frontier_entries.size(); ++i)
					context.pending_frontier_entry_index_by_key[context.pending_frontier_entries[i].record.key] = i;
			}

			bool restore_next_pending_frontier_entry()
			{
				if (!cursor.stack.empty())
					return false;

				for (std::size_t index = 0; index < context.pending_frontier_entries.size(); )
				{
					LinearResidualFrontierEntry entry = context.pending_frontier_entries[index];
					if (!entry.frame_snapshot)
					{
						erase_pending_frontier_entry_at(index);
						continue;
					}
					if (!retighten_pending_frontier_entry_for_current_incumbent(entry))
					{
						erase_pending_frontier_entry_at(index);
						erase_pending_frontier_record_by_key(entry.record.key);
						continue;
					}

					erase_pending_frontier_entry_at(index);
					erase_pending_frontier_record_by_key(entry.record.key);

					context.current_linear_trail = std::move(entry.current_trail_snapshot);
					cursor.stack.clear();
					cursor.stack.push_back(*entry.frame_snapshot);
					set_active_problem(entry.record, false);
					reset_active_residual_bnb_state();
					return true;
				}
				return false;
			}

		private:
			LinearBestSearchContext& context;
			LinearSearchCursor& cursor;

			SearchWeight current_after_round_lower_bound(int round_boundary_depth) const
			{
				if (!context.configuration.enable_remaining_round_lower_bound)
					return 0;
				const int rounds_left_after = context.configuration.round_count - (round_boundary_depth + 1);
				if (rounds_left_after < 0)
					return 0;
				const auto& table = context.configuration.remaining_round_min_weight;
				const std::size_t idx = std::size_t(rounds_left_after);
				return (idx < table.size()) ? table[idx] : 0;
			}

			bool retighten_common_round_budget(LinearRoundSearchState& state)
			{
				if (context.best_weight >= INFINITE_WEIGHT)
					return true;

				state.round_index = context.configuration.round_count - state.round_boundary_depth;
				state.remaining_round_weight_lower_bound_after_this_round =
					current_after_round_lower_bound(state.round_boundary_depth);
				const SearchWeight base_cap =
					remaining_search_weight_budget(context.best_weight, state.accumulated_weight_so_far);
				state.round_weight_cap =
					remaining_search_weight_budget(base_cap, state.remaining_round_weight_lower_bound_after_this_round);
				if (state.round_weight_cap == 0)
					return false;

				linear_prepare_round_entry_bridge(
					state,
					state.round_output_branch_a_mask,
					state.round_output_branch_b_mask );
				if (state.weight_injection_from_branch_a >= state.round_weight_cap)
					return false;

				state.remaining_after_inj_a = state.round_weight_cap - state.weight_injection_from_branch_a;
				if (state.remaining_after_inj_a == 0)
					return false;

				state.second_subconst_weight_cap =
					std::min(context.configuration.constant_subtraction_weight_cap, state.remaining_after_inj_a - 1);
				state.second_add_weight_cap =
					std::min(context.configuration.addition_weight_cap, state.remaining_after_inj_a - 1);
				return true;
			}

			bool retighten_pending_frontier_entry_for_current_incumbent(LinearResidualFrontierEntry& entry)
			{
				if (!entry.frame_snapshot)
					return false;

				auto& frame = *entry.frame_snapshot;
				auto& state = frame.state;
				switch (frame.stage)
				{
				case LinearSearchStage::Enter:
					return state.accumulated_weight_so_far < context.best_weight;
				case LinearSearchStage::InjA:
				{
					if (!retighten_common_round_budget(state))
						return false;
					state.injection_from_branch_a_enumerator.reset(
						state.injection_from_branch_a_transition,
						context.configuration.maximum_injection_input_masks);
					return true;
				}
				case LinearSearchStage::SecondAdd:
				{
					if (!retighten_common_round_budget(state))
						return false;
					return prepare_varvar_step_q2(
						state,
						LinearVarVarStageSlot::SecondAdd,
						context.configuration,
						[]() { return false; });
				}
				case LinearSearchStage::InjB:
				{
					if (!retighten_common_round_budget(state))
						return false;
					if (state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition >= context.best_weight)
						return false;
					state.branch_b_mask_contribution_from_second_addition_term =
						linear_cross_xor_mask_from_addition_term( state.second_addition_term_mask_from_branch_b );
					linear_prepare_inj_b_bridge_after_second_add( state );
					if (state.base_weight_after_inj_b >= state.round_weight_cap)
						return false;
					state.injection_from_branch_b_enumerator.reset(
						state.injection_from_branch_b_transition,
						context.configuration.maximum_injection_input_masks);
					state.second_constant_subtraction_candidate_index = 0;
					return true;
				}
				case LinearSearchStage::SecondConst:
				{
					if (!retighten_common_round_budget(state))
						return false;
					state.branch_b_mask_contribution_from_second_addition_term =
						linear_cross_xor_mask_from_addition_term( state.second_addition_term_mask_from_branch_b );
					linear_prepare_inj_b_bridge_after_second_add( state );
					if (state.base_weight_after_inj_b >= state.round_weight_cap)
						return false;
					const auto status =
						enter_varconst_step_q2(
							state,
							LinearVarConstStageSlot::SecondConst,
							context.configuration,
							LinearFixedBetaHotPathCallSite::EnginePrepareSecondSubconst,
							[]() { return false; });
					return status == LinearFixedBetaOuterHotPathEnterStatus::Prepared;
				}
				case LinearSearchStage::FirstSubconst:
				{
					if (!retighten_common_round_budget(state))
						return false;
					if (state.accumulated_weight_so_far + state.base_weight_after_second_subconst >= context.best_weight)
						return false;
					if ( !linear_prepare_first_subround_caps( state, context.configuration ) )
						return false;
					const auto status =
						enter_varconst_step_q2(
							state,
							LinearVarConstStageSlot::FirstSubconst,
							context.configuration,
							LinearFixedBetaHotPathCallSite::EnginePrepareFirstSubconst,
							[]() { return false; });
					return status == LinearFixedBetaOuterHotPathEnterStatus::Prepared;
				}
				case LinearSearchStage::FirstAdd:
				{
					if (!retighten_common_round_budget(state))
						return false;
					const SearchWeight base_weight_after_first_subconst =
						state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current;
					if (state.accumulated_weight_so_far + base_weight_after_first_subconst >= context.best_weight)
						return false;
					const SearchWeight remaining_for_first_add =
						remaining_search_weight_budget(state.round_weight_cap, base_weight_after_first_subconst);
					if (remaining_for_first_add == 0)
						return false;
					state.first_add_weight_cap =
						std::min(context.configuration.addition_weight_cap, remaining_for_first_add - 1);
					return prepare_varvar_step_q2(
						state,
						LinearVarVarStageSlot::FirstAdd,
						context.configuration,
						[]() { return false; });
				}
				default:
					return true;
				}
			}

			void reset_active_residual_bnb_state()
			{
				context.local_state_dominance.clear();
				context.local_state_dominance.set_capacity(
					TwilightDream::residual_frontier_shared::LocalResidualStateDominanceTable::kDefaultCapacity );
				context.memoization.initialize(
					std::size_t(context.configuration.round_count) + 1u,
					context.configuration.enable_state_memoization,
					"linear_memo.activate_residual");
			}

			void erase_pending_frontier_entry_at(std::size_t index)
			{
				if (index >= context.pending_frontier_entries.size())
					return;
				context.pending_frontier_entry_index_by_key.erase(context.pending_frontier_entries[index].record.key);
				const std::size_t last = context.pending_frontier_entries.size() - 1u;
				if (index != last)
				{
					context.pending_frontier_entries[index] = std::move(context.pending_frontier_entries[last]);
					context.pending_frontier_entry_index_by_key[context.pending_frontier_entries[index].record.key] = index;
				}
				context.pending_frontier_entries.pop_back();
			}

			void erase_pending_frontier_record_by_key(const TwilightDream::residual_frontier_shared::ResidualProblemKey& key)
			{
				const auto it = context.pending_frontier_index_by_key.find(key);
				if (it == context.pending_frontier_index_by_key.end())
					return;
				const std::size_t index = it->second;
				context.pending_frontier_index_by_key.erase(it);
				const std::size_t last = context.pending_frontier.size() - 1u;
				if (index != last)
				{
					context.pending_frontier[index] = std::move(context.pending_frontier[last]);
					context.pending_frontier_index_by_key[context.pending_frontier[index].key] = index;
				}
				context.pending_frontier.pop_back();
			}
		};
	}

	// ARX Automatic Search Frame - Linear Analysis Paper:
	// [eprint-iacr-org-2019-1319] Automatic Search for the Linear (hull) Characteristics of ARX Ciphers - Applied to SPECK, SPARX, Chaskey and CHAM-64
	// Is applied to NeoAlzette ARX-Box Algorithm every step of the round
	//
	// This resumable DFS cursor is the canonical BnB carrier for one reverse linear round.
	// Pure linear transport is handled by transpose propagation with weight 0 before the
	// nonlinear operator pipeline is entered.  Inside the round, the stage order is fixed:
	//   InjA -> SecondAdd -> InjB -> SecondConst -> FirstSubconst -> FirstAdd.
	//
	// Q2 / Q1 split for every nonlinear step:
	// - injection: exact affine correlated-input space + exact local rank/2 weight;
	// - var-var add: Q2 chooses the active polarity object (fixed-u z-shell stream or
	//   fixed-(v,w) exact column-optimal u*), Q1 is the exact local Schulte-Geers / CCZ weight;
	// - var-const subtract: Q2 supplies fixed-beta / fixed-alpha floor and/or ordered
	//   witness stream, Q1 is the exact var-const local weight.
	//
	// BnB rules enforced here:
	// - every nonlinear step may prune immediately from its exact local weight or exact floor;
	// - ordered strict streams may stop early only when the operator contract proves the
	//   remaining tail cannot improve the current round cap;
	// - split-8, weight-sliced cLAT and hot-path tables are accelerators only, never
	//   substitutes for the exact operator semantics.
	//
	// Mathematical wiring inside this resumable DFS cursor:
	// - `compute_injection_transition_from_branch_a/b()` derives the exact affine
	//   input-mask space of the two quadratic injection layers.
	// - `enumerate_affine_subspace_input_masks()` walks that injection space.
	// - fixed-output subconst hot wires always compute a fixed-beta column floor first;
	//   collapsed mode consumes only that single local `alpha*`, while strict mode keeps
	//   the exact ordered fixed-beta alpha enumeration witness.
	// - `AddVarVarSplit8Enumerator32` and the weight-sliced cLAT streaming cursor provide
	//   the Schulte-Geers / Huang-Wang var-var addition candidates under exact z-shell weights
	//   for the row-side **fixed output mask u → enumerate (v,w)** polarity.
	// - When `varvar_modular_add_bnb_mode == FixedInputVW_ColumnOptimalOutputU`, the same `(v,w)`
	//   candidates are rescored through the exact Gap-A column operator
	//   `(v,w) -> find_optimal_output_u_ccz -> u* -> linear_correlation_add_ccz_weight/value`,
	//   and the exact column-optimal `u*` is written onto the trail sum wire.
	//
	// The checkpoint stores this cursor together with the in-flight enumerator/candidate
	// state, so resume continues from the same search node instead of rebuilding the round.
	class LinearBNB final
	{
	public:
		LinearBNB(
			LinearBestSearchContext& context_in,
			LinearSearchCursor& cursor_in,
			LinearEngineResidualFrontierHelper& helper_in)
			: context(context_in),
			search_configuration(context_in.configuration),
			cursor(cursor_in),
			helper(helper_in)
		{
		}

		void start_from_initial_frame(std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask)
		{
			cursor.stack.clear();
			cursor.stack.reserve(std::size_t(std::max(1, search_configuration.round_count)) + 1u);
			context.current_linear_trail.clear();
			context.local_state_dominance.clear();
			context.pending_frontier.clear();
			context.pending_frontier_entries.clear();
			context.pending_frontier_index_by_key.clear();
			context.pending_frontier_entry_index_by_key.clear();
			LinearSearchFrame frame{};
			frame.stage = LinearSearchStage::Enter;
			frame.trail_size_at_entry = context.current_linear_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.round_output_branch_a_mask = output_branch_a_mask;
			frame.state.round_output_branch_b_mask = output_branch_b_mask;
			cursor.stack.push_back(frame);
		}

		void search_from_start(std::uint32_t output_branch_a_mask, std::uint32_t output_branch_b_mask)
		{
			start_from_initial_frame(output_branch_a_mask, output_branch_b_mask);
			run();
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		LinearBestSearchContext& context;
		const LinearBestSearchConfiguration& search_configuration;
		LinearSearchCursor& cursor;
		LinearEngineResidualFrontierHelper& helper;

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			std::uint32_t pair_c,
			SearchWeight best_prefix_weight) const noexcept
		{
			return helper.make_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				pair_c,
				best_prefix_weight);
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight) const noexcept
		{
			return make_linear_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				0u,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			const LinearBestSearchContext&,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			std::uint32_t pair_c,
			SearchWeight best_prefix_weight) const noexcept
		{
			return make_linear_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				pair_c,
				best_prefix_weight);
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			const LinearBestSearchContext& ctx,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight) const noexcept
		{
			return make_linear_boundary_record(
				ctx,
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				0u,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			const LinearRoundSearchState& state,
			SearchWeight best_prefix_weight ) const noexcept
		{
			const LinearResidualSemanticKeyParts key = linear_semantic_key_for_stage( stage_cursor, state );
			return make_linear_boundary_record(
				rounds_remaining,
				stage_cursor,
				key.pair_a,
				key.pair_b,
				key.pair_c,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_linear_boundary_record(
			const LinearBestSearchContext&,
			std::int32_t rounds_remaining,
			LinearSearchStage stage_cursor,
			const LinearRoundSearchState& state,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_linear_boundary_record(
				rounds_remaining,
				stage_cursor,
				state,
				best_prefix_weight );
		}

		bool try_register_linear_child_residual_candidate(
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const LinearSearchFrame* frame_snapshot = nullptr,
			const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0)
		{
			return helper.try_register_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset);
		}

		bool try_register_linear_child_residual_candidate(
			LinearBestSearchContext&,
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const LinearSearchFrame* frame_snapshot = nullptr,
			const std::vector<LinearTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0)
		{
			return try_register_linear_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset);
		}

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
			if (cursor.stack.empty())
				return;
			const std::size_t target = cursor.stack.back().trail_size_at_entry;
			if (context.current_linear_trail.size() > target)
				context.current_linear_trail.resize(target);
			cursor.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if (!context.binary_checkpoint)
				return;
			if (best_search_shared_core::should_poll_binary_checkpoint(
				context.binary_checkpoint->pending_best_change(),
				context.binary_checkpoint->pending_runtime_request() ||
				TwilightDream::runtime_component::runtime_watchdog_checkpoint_request_pending(context.runtime_state),
				context.run_visited_node_count,
				context.progress_node_mask))
				context.binary_checkpoint->poll(context, cursor);
		}

		void run()
		{
			while (!cursor.stack.empty())
			{
				LinearSearchFrame& frame = current_frame();
				LinearRoundSearchState& state = frame.state;

				switch (frame.stage)
				{
				case LinearSearchStage::Enter:
				{
					if (should_stop_search(state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far))
					{
						if (context.stop_due_to_time_limit || linear_runtime_node_limit_hit(context) || context.stop_due_to_target)
							return;
						const int rounds_remaining = search_configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_linear_child_residual_candidate(
								context,
								make_linear_boundary_record(
									context,
									rounds_remaining,
									LinearSearchStage::Enter,
									state.round_output_branch_a_mask,
									state.round_output_branch_b_mask,
									state.accumulated_weight_so_far),
								&frame,
								&context.current_linear_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}
					if (handle_round_end_if_needed(state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far))
					{
						pop_frame();
						break;
					}
					if (should_prune_local_state_dominance(
						LinearSearchStage::Enter,
						state,
						state.accumulated_weight_so_far))
					{
						pop_frame();
						break;
					}
					if (should_prune_state_memoization(state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far))
					{
						const int rounds_remaining = search_configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_linear_child_residual_candidate(
								context,
								make_linear_boundary_record(
									context,
									rounds_remaining,
									LinearSearchStage::Enter,
									state.round_output_branch_a_mask,
									state.round_output_branch_b_mask,
									state.accumulated_weight_so_far),
								&frame,
								&context.current_linear_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}

					if (!prepare_round_state(state.round_boundary_depth, state.round_output_branch_a_mask, state.round_output_branch_b_mask, state.accumulated_weight_so_far))
					{
						const int rounds_remaining = search_configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_linear_child_residual_candidate(
								context,
								make_linear_boundary_record(
									context,
									rounds_remaining,
									LinearSearchStage::Enter,
									state.round_output_branch_a_mask,
									state.round_output_branch_b_mask,
									state.accumulated_weight_so_far),
								&frame,
								&context.current_linear_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}

					begin_streaming_round();
					break;
				}
				case LinearSearchStage::InjA:
				{
					if (!advance_stage_inj_a())
						return;
					break;
				}
				case LinearSearchStage::SecondAdd:
				{
					if (!advance_stage_second_add())
						return;
					break;
				}
				case LinearSearchStage::InjB:
				{
					if (!advance_stage_inj_b())
						return;
					break;
				}
				case LinearSearchStage::SecondConst:
				{
					if (!advance_stage_second_const())
						return;
					break;
				}
				case LinearSearchStage::FirstSubconst:
				{
					if (!advance_stage_first_subconst())
						return;
					break;
				}
				case LinearSearchStage::FirstAdd:
				{
					if (!advance_stage_first_add())
						return;
					break;
				}
				case LinearSearchStage::Enumerate:
				{
					if (context.stop_due_to_time_limit || linear_runtime_node_limit_hit(context) || context.stop_due_to_target)
						return;

					reset_round_predecessor_buffer();
					begin_streaming_round();
					break;
				}
				case LinearSearchStage::Recurse:
				{
					if (context.stop_due_to_time_limit || linear_runtime_node_limit_hit(context) || context.stop_due_to_target)
						return;
					if (frame.predecessor_index >= state.round_predecessors.size())
					{
						pop_frame();
						break;
					}

					const auto& step = state.round_predecessors[frame.predecessor_index++];
					if (state.accumulated_weight_so_far + step.round_weight >= context.best_weight)
					{
						const int rounds_remaining_after = search_configuration.round_count - (state.round_boundary_depth + 1);
						if (rounds_remaining_after > 0)
						{
							auto trail_snapshot = context.current_linear_trail;
							trail_snapshot.push_back(step);
							LinearSearchFrame child{};
							child.stage = LinearSearchStage::Enter;
							child.trail_size_at_entry = trail_snapshot.size() - 1u;
							child.state.round_boundary_depth = state.round_boundary_depth + 1;
							child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + step.round_weight;
							child.state.round_output_branch_a_mask = step.input_branch_a_mask;
							child.state.round_output_branch_b_mask = step.input_branch_b_mask;
							(void)try_register_linear_child_residual_candidate(
								context,
								make_linear_boundary_record(
									context,
									rounds_remaining_after,
									LinearSearchStage::Enter,
									step.input_branch_a_mask,
									step.input_branch_b_mask,
									state.accumulated_weight_so_far + step.round_weight),
								&child,
								&trail_snapshot,
								state.accumulated_weight_so_far + step.round_weight);
						}
						break;
					}

					context.current_linear_trail.push_back(step);
					LinearSearchFrame child{};
					child.stage = LinearSearchStage::Enter;
					// Store the parent trail size (exclude the step we just pushed) so pop_frame() removes it.
					child.trail_size_at_entry = context.current_linear_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + step.round_weight;
					child.state.round_output_branch_a_mask = step.input_branch_a_mask;
					child.state.round_output_branch_b_mask = step.input_branch_b_mask;
					cursor.stack.push_back(child);
					break;
				}
				}

				maybe_poll_checkpoint();
			}
		}

		bool sync_and_check_runtime_stop()
		{
			sync_linear_runtime_legacy_fields_from_state(context);
			return context.stop_due_to_target || linear_runtime_budget_hit(context);
		}

		bool poll_shared_runtime()
		{
			const bool stop = runtime_poll(context.runtime_controls, context.runtime_state);
			sync_linear_runtime_legacy_fields_from_state(context);
			return stop || context.stop_due_to_target;
		}

		void maybe_print_streaming_progress()
		{
			const auto& state = current_round_state();
			maybe_print_single_run_progress(
				context,
				state.round_boundary_depth,
				state.round_output_branch_a_mask,
				state.round_output_branch_b_mask);
			maybe_poll_checkpoint();
		}

		template <typename T>
		void clear_rebuildable_vector(std::vector<T>& values, std::size_t keep_capacity = 4096u)
		{
			values.clear();
			if (values.capacity() > keep_capacity && memory_governor_in_pressure())
			{
				std::vector<T> empty;
				values.swap(empty);
			}
		}

		bool use_split8_streaming_add_cursor() const
		{
			const auto contract = linear_varvar_row_q2_contract(search_configuration);
			return search_configuration.search_mode == SearchMode::Strict &&
				contract.kind == LinearVarVarRowQ2ContractKind::Split8ExactPath;
		}

		bool use_weight_sliced_clat_streaming_add_cursor() const
		{
			const auto contract = linear_varvar_row_q2_contract(search_configuration);
			return search_configuration.search_mode == SearchMode::Strict &&
				contract.kind == LinearVarVarRowQ2ContractKind::WeightSlicedClatExactShellIndex;
		}

		std::optional<VarConstSubColumnOptimalOnOutputWire> compute_second_subconst_q2_floor(LinearRoundSearchState& state) const
		{
			return compute_varconst_step_q2_floor(
				state,
				LinearVarConstStageSlot::SecondConst,
				search_configuration);
		}

		SearchWeight ensure_second_subconst_q2_floor(LinearRoundSearchState& state) const
		{
			return ensure_varconst_step_q2_floor(
				state,
				LinearVarConstStageSlot::SecondConst,
				search_configuration);
		}

		std::optional<VarConstSubColumnOptimalOnOutputWire> compute_first_subconst_q2_floor(LinearRoundSearchState& state) const
		{
			return compute_varconst_step_q2_floor(
				state,
				LinearVarConstStageSlot::FirstSubconst,
				search_configuration);
		}

		SearchWeight ensure_first_subconst_q2_floor(LinearRoundSearchState& state) const
		{
			return ensure_varconst_step_q2_floor(
				state,
				LinearVarConstStageSlot::FirstSubconst,
				search_configuration);
		}

		LinearFixedBetaOuterHotPathEnterStatus enter_second_subconst_q2_gate(LinearRoundSearchState& state)
		{
			return enter_varconst_step_q2(
				state,
				LinearVarConstStageSlot::SecondConst,
				search_configuration,
				LinearFixedBetaHotPathCallSite::EnginePrepareSecondSubconst,
				[this]() { return sync_and_check_runtime_stop(); });
		}

		LinearFixedBetaOuterHotPathEnterStatus enter_first_subconst_q2_gate(LinearRoundSearchState& state)
		{
			return enter_varconst_step_q2(
				state,
				LinearVarConstStageSlot::FirstSubconst,
				search_configuration,
				LinearFixedBetaHotPathCallSite::EnginePrepareFirstSubconst,
				[this]() { return sync_and_check_runtime_stop(); });
		}

		void clear_first_stage_buffers(LinearRoundSearchState& state)
		{
			clear_rebuildable_vector(state.first_constant_subtraction_candidates_for_branch_a);
			state.first_constant_subtraction_stream_cursor = {};
			state.first_constant_subtraction_fixed_alpha_stream_cursor = {};
			clear_rebuildable_vector(state.first_addition_candidates_for_branch_b);
			clear_rebuildable_vector(state.first_constant_subtraction_fixed_alpha_candidates_for_branch_a);
			state.first_addition_stream_cursor = {};
			state.first_addition_weight_sliced_clat_stream_cursor = {};
			state.first_constant_subtraction_candidate_index = 0;
			state.first_addition_candidate_index = 0;
			state.first_subconst_weight_floor = INFINITE_WEIGHT;
			state.first_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
			state.first_add_trail_sum_wire_u = 0;
		}

		void clear_second_stage_buffers(LinearRoundSearchState& state)
		{
			state.second_constant_subtraction_stream_cursor = {};
			state.second_constant_subtraction_fixed_alpha_stream_cursor = {};
			clear_rebuildable_vector(state.second_constant_subtraction_candidates_for_branch_b);
			clear_rebuildable_vector(state.second_constant_subtraction_fixed_alpha_candidates_for_branch_b);
			clear_rebuildable_vector(state.second_addition_candidates_storage);
			state.second_addition_stream_cursor = {};
			state.second_addition_weight_sliced_clat_stream_cursor = {};
			state.second_addition_candidates_for_branch_a = nullptr;
			state.second_addition_candidate_index = 0;
			state.second_constant_subtraction_candidate_index = 0;
			state.second_subconst_weight_floor = INFINITE_WEIGHT;
			state.second_subconst_fixed_alpha_weight_floor = INFINITE_WEIGHT;
			state.second_add_trail_sum_wire_u = 0;
			state.injection_from_branch_b_enumerator = {};
			clear_first_stage_buffers(state);
		}

		// Enter the canonical reverse-round pipeline:
		//   explicit zero-cost cross-xor / pre-whitening bridges
		//   + nonlinear stages InjA -> SecondAdd -> InjB -> SecondConst -> FirstSubconst -> FirstAdd.
		// The deterministic CROSS_XOR_ROT bridges are now kept as explicit helper-level
		// analysis steps instead of being hidden inside the injection model.
		void begin_streaming_round()
		{
			auto& frame = current_frame();
			auto& state = frame.state;
			clear_second_stage_buffers(state);
			state.injection_from_branch_a_enumerator.reset(
				state.injection_from_branch_a_transition,
				search_configuration.maximum_injection_input_masks);
			frame.stage = LinearSearchStage::InjA;
		}

		bool advance_stage_inj_a()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::InjA,
				state,
				state.accumulated_weight_so_far))
			{
				pop_frame();
				return true;
			}
			while (true)
			{
				std::uint32_t chosen_mask = 0;
				if (!state.injection_from_branch_a_enumerator.next(context, state.injection_from_branch_a_transition, chosen_mask))
				{
					if (sync_and_check_runtime_stop())
						return false;
					clear_second_stage_buffers(state);
					if (using_round_predecessor_mode())
					{
						trim_round_predecessors(true);
						frame.predecessor_index = 0;
						frame.stage = LinearSearchStage::Recurse;
					}
					else
					{
						pop_frame();
					}
					return true;
				}

				if (sync_and_check_runtime_stop())
					return false;

				maybe_print_streaming_progress();
				state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_mask;
				linear_project_second_subround_outputs_from_inj_a_choice( state );

				// After fixing the injection mask choice, the second subround reconnects to the
				// exact ARX operators: var-const subtraction on B and var-var addition on A.
				// The fixed-beta Gap-B root operator now supplies an exact local floor even when
				// strict search still needs the full subconst enumeration backend.
				switch (enter_second_subconst_q2_gate(state))
				{
				case LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate:
					continue;
				case LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop:
					return false;
				case LinearFixedBetaOuterHotPathEnterStatus::Prepared:
				default:
					break;
				}

				if (!prepare_varvar_step_q2(
					state,
					LinearVarVarStageSlot::SecondAdd,
					search_configuration,
					[this]() { return sync_and_check_runtime_stop(); }))
				{
					return false;
				}
				state.injection_from_branch_b_enumerator = {};
				state.second_constant_subtraction_candidate_index = 0;
				clear_first_stage_buffers(state);
				frame.stage = LinearSearchStage::SecondAdd;
				return true;
			}
		}

		bool advance_stage_second_add()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::SecondAdd,
				state,
				state.accumulated_weight_so_far + state.weight_injection_from_branch_a))
			{
				frame.stage = LinearSearchStage::InjA;
				return true;
			}
			LinearVarVarQ2Candidate q2_candidate{};
			while (true)
			{
				if (linear_runtime_node_limit_hit(context))
					return false;
				if (linear_note_runtime_node_visit(context))
					return false;
				maybe_print_streaming_progress();

				if (!next_varvar_step_q2_candidate(
					state,
					LinearVarVarStageSlot::SecondAdd,
					search_configuration,
					q2_candidate))
				{
					if (sync_and_check_runtime_stop())
						return false;
					break;
				}
				const auto q1_weight =
					evaluate_varvar_q1_exact_weight(q2_candidate, 32);
				if (!q1_weight.has_value())
					continue;
				const SearchWeight weight_second_addition = q1_weight.value();
				linear_load_second_add_candidate_state(
					state,
					q2_candidate,
					weight_second_addition );
				if (state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap)
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
						search_configuration.maximum_injection_input_masks);
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::InjB,
							snapshot.state,
							state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition);
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
						break;
					continue;
				}

				// Reverse-round middle bridge: once the second modular-addition candidate is
				// fixed, the B->A injection again becomes an exact affine input-mask space.
				linear_prepare_inj_b_bridge_after_second_add( state );
				if (state.base_weight_after_inj_b >= state.round_weight_cap)
				{
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::SecondConst;
					snapshot.state.second_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::SecondConst,
							state.input_branch_a_mask_before_second_addition,
							state.branch_b_mask_contribution_from_second_addition_term,
							state.accumulated_weight_so_far + state.base_weight_after_inj_b),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b);
					continue;
				}
				if (state.base_weight_after_inj_b + ensure_second_subconst_q2_floor(state) >= state.round_weight_cap)
				{
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::SecondConst;
					snapshot.state.second_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::SecondConst,
							state.input_branch_a_mask_before_second_addition,
							state.branch_b_mask_contribution_from_second_addition_term,
							state.accumulated_weight_so_far + state.base_weight_after_inj_b),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b);
					continue;
				}

				state.injection_from_branch_b_enumerator.reset(
					state.injection_from_branch_b_transition,
					search_configuration.maximum_injection_input_masks);
				state.second_constant_subtraction_candidate_index = 0;
				frame.stage = LinearSearchStage::InjB;
				return true;
			}

			clear_second_stage_buffers(state);
			frame.stage = LinearSearchStage::InjA;
			return true;
		}

		bool advance_stage_inj_b()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::InjB,
				state,
				state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition))
			{
				frame.stage = LinearSearchStage::SecondAdd;
				return true;
			}
			std::uint32_t chosen_mask = 0;
			if (!state.injection_from_branch_b_enumerator.next(context, state.injection_from_branch_b_transition, chosen_mask))
			{
				if (sync_and_check_runtime_stop())
					return false;
				clear_first_stage_buffers(state);
				frame.stage = LinearSearchStage::SecondAdd;
				return true;
			}

			if (sync_and_check_runtime_stop())
				return false;

			maybe_print_streaming_progress();
			state.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_mask;
			state.second_constant_subtraction_candidate_index = 0;
			clear_first_stage_buffers(state);
			frame.stage = LinearSearchStage::SecondConst;
			return true;
		}

		bool advance_stage_second_const()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::SecondConst,
				state,
				state.accumulated_weight_so_far + state.base_weight_after_inj_b))
			{
				frame.stage = LinearSearchStage::InjB;
				return true;
			}
			LinearVarConstQ2Candidate q2_candidate{};
			constexpr auto call_site = LinearFixedBetaHotPathCallSite::EnginePrepareSecondSubconst;
			while (true)
			{
				if (linear_runtime_node_limit_hit(context))
					return false;
				if (linear_note_runtime_node_visit(context))
					return false;
				maybe_print_streaming_progress();

				if (!next_varconst_step_q2_candidate(
					state,
					LinearVarConstStageSlot::SecondConst,
					search_configuration,
					call_site,
					q2_candidate))
				{
					if (sync_and_check_runtime_stop())
						return false;
					break;
				}
				const auto q1_weight =
					evaluate_varconst_q1_exact_weight(
						q2_candidate,
						NeoAlzetteCore::ROUND_CONSTANTS[6],
						state.second_subconst_weight_cap,
						&state.second_subconst_fixed_alpha_weight_floor,
						32);
				if (!q1_weight.has_value())
					continue;
				linear_load_second_subconst_candidate_state(
					state,
					q2_candidate,
					q1_weight.value() );
				if (state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap)
				{
					linear_project_first_subround_outputs_from_inj_b_choice( state );
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstSubconst;
					snapshot.state.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
					snapshot.state.base_weight_after_second_subconst = state.base_weight_after_second_subconst;
					snapshot.state.first_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstSubconst,
							state,
							state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction);
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
					{
						state.second_constant_subtraction_candidate_index =
							state.second_constant_subtraction_candidates_for_branch_b.size();
						break;
					}
					continue;
				}

				if (state.base_weight_after_second_subconst >= state.round_weight_cap)
					continue;
				if ( !linear_prepare_first_subround_caps( state, search_configuration ) )
					continue;
				linear_project_first_subround_outputs_from_inj_b_choice( state );

				if (state.base_weight_after_second_subconst + ensure_first_subconst_q2_floor(state) >= state.round_weight_cap)
				{
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstSubconst;
					snapshot.state.first_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstSubconst,
							state,
							state.accumulated_weight_so_far + state.base_weight_after_second_subconst),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_second_subconst);
					continue;
				}

				switch (enter_first_subconst_q2_gate(state))
				{
				case LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate:
					continue;
				case LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop:
					return false;
				case LinearFixedBetaOuterHotPathEnterStatus::Prepared:
				default:
					break;
				}

				if (!prepare_varvar_step_q2(
					state,
					LinearVarVarStageSlot::FirstAdd,
					search_configuration,
					[this]() { return sync_and_check_runtime_stop(); }))
				{
					return false;
				}
				state.first_constant_subtraction_candidate_index = 0;
				frame.stage = LinearSearchStage::FirstSubconst;
				return true;
			}

			clear_first_stage_buffers(state);
			frame.stage = LinearSearchStage::InjB;
			return true;
		}

		bool advance_stage_first_subconst()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::FirstSubconst,
				state,
				state.accumulated_weight_so_far + state.base_weight_after_second_subconst))
			{
				frame.stage = LinearSearchStage::SecondConst;
				return true;
			}
			LinearVarConstQ2Candidate q2_candidate{};
			constexpr auto call_site = LinearFixedBetaHotPathCallSite::EnginePrepareFirstSubconst;
			while (true)
			{
				if (linear_runtime_node_limit_hit(context))
					return false;
				if (linear_note_runtime_node_visit(context))
					return false;
				maybe_print_streaming_progress();

				if (!next_varconst_step_q2_candidate(
					state,
					LinearVarConstStageSlot::FirstSubconst,
					search_configuration,
					call_site,
					q2_candidate))
				{
					if (sync_and_check_runtime_stop())
						return false;
					break;
				}
				const auto q1_weight =
					evaluate_varconst_q1_exact_weight(
						q2_candidate,
						NeoAlzetteCore::ROUND_CONSTANTS[1],
						state.first_subconst_weight_cap,
						&state.first_subconst_fixed_alpha_weight_floor,
						32);
				if (!q1_weight.has_value())
					continue;
				const SearchWeight base_weight_after_first_subconst =
					state.base_weight_after_second_subconst + q1_weight.value();
				if (base_weight_after_first_subconst >= state.round_weight_cap)
				{
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstAdd;
					snapshot.state.input_branch_a_mask_before_first_constant_subtraction_current = q2_candidate.input_mask_alpha;
					snapshot.state.weight_first_constant_subtraction_current = q1_weight.value();
					snapshot.state.first_addition_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstAdd,
							q2_candidate.input_mask_alpha,
							state.output_branch_b_mask_after_first_addition,
							state.accumulated_weight_so_far + base_weight_after_first_subconst),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + base_weight_after_first_subconst);
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
					{
						state.first_constant_subtraction_candidate_index =
							state.first_constant_subtraction_candidates_for_branch_a.size();
						break;
					}
					continue;
				}

				state.input_branch_a_mask_before_first_constant_subtraction_current =
					q2_candidate.input_mask_alpha;
				state.weight_first_constant_subtraction_current = q1_weight.value();
				state.first_addition_candidate_index = 0;
				frame.stage = LinearSearchStage::FirstAdd;
				return true;
			}

			frame.stage = LinearSearchStage::SecondConst;
			return true;
		}

		bool advance_stage_first_add()
		{
			if (poll_shared_runtime())
				return false;

			auto& frame = current_frame();
			auto& state = frame.state;
			if (should_prune_local_state_dominance(
				LinearSearchStage::FirstAdd,
				state,
				state.accumulated_weight_so_far + state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current))
			{
				frame.stage = LinearSearchStage::FirstSubconst;
				return true;
			}
			const SearchWeight base_weight_after_first_subconst =
				state.base_weight_after_second_subconst + state.weight_first_constant_subtraction_current;
			LinearVarVarQ2Candidate q2_candidate{};
			while (true)
			{
				if (linear_runtime_node_limit_hit(context))
					return false;
				if (linear_note_runtime_node_visit(context))
					return false;
				maybe_print_streaming_progress();

				if (!next_varvar_step_q2_candidate(
					state,
					LinearVarVarStageSlot::FirstAdd,
					search_configuration,
					q2_candidate))
				{
					if (sync_and_check_runtime_stop())
						return false;
					break;
				}
				const auto q1_weight =
					evaluate_varvar_q1_exact_weight(q2_candidate, 32);
				if (!q1_weight.has_value())
					continue;
				const SearchWeight weight_first_addition = q1_weight.value();
				state.first_add_trail_sum_wire_u = q2_candidate.sum_wire_u;

				const SearchWeight total_w = base_weight_after_first_subconst + weight_first_addition;
				if (total_w >= state.round_weight_cap)
				{
					const int rounds_remaining_after = search_configuration.round_count - (state.round_boundary_depth + 1);
					if (rounds_remaining_after > 0)
					{
						const LinearTrailStepRecord step =
							linear_build_round_step(
								state,
								state.input_branch_a_mask_before_first_constant_subtraction_current,
								state.weight_first_constant_subtraction_current,
								q2_candidate,
								weight_first_addition );
						auto trail_snapshot = context.current_linear_trail;
						trail_snapshot.push_back(step);
						LinearSearchFrame child{};
						child.stage = LinearSearchStage::Enter;
						child.trail_size_at_entry = trail_snapshot.size() - 1u;
						child.state.round_boundary_depth = state.round_boundary_depth + 1;
						child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + total_w;
						child.state.round_output_branch_a_mask = step.input_branch_a_mask;
						child.state.round_output_branch_b_mask = step.input_branch_b_mask;
						(void)try_register_linear_child_residual_candidate(
							context,
							make_linear_boundary_record(
								context,
								rounds_remaining_after,
								LinearSearchStage::Enter,
								step.input_branch_a_mask,
								step.input_branch_b_mask,
								state.accumulated_weight_so_far + total_w),
							&child,
							&trail_snapshot,
							state.accumulated_weight_so_far + total_w);
					}
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
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

				if (state.accumulated_weight_so_far + step.round_weight >= context.best_weight)
					continue;

				if (using_round_predecessor_mode())
				{
					state.round_predecessors.push_back(step);
					trim_round_predecessors(false);
					continue;
				}

				context.current_linear_trail.push_back(step);
				LinearSearchFrame child{};
				child.stage = LinearSearchStage::Enter;
				child.trail_size_at_entry = context.current_linear_trail.size() - 1u;
				child.state.round_boundary_depth = state.round_boundary_depth + 1;
				child.state.accumulated_weight_so_far =
					state.accumulated_weight_so_far + step.round_weight;
				child.state.round_output_branch_a_mask = step.input_branch_a_mask;
				child.state.round_output_branch_b_mask = step.input_branch_b_mask;
				cursor.stack.push_back(child);
				return true;
			}

			frame.stage = LinearSearchStage::FirstSubconst;
			return true;
		}

		// Global stop conditions, node/time budget, and trivial weight pruning.
		bool should_stop_search(int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight)
		{
			if (context.stop_due_to_time_limit || context.stop_due_to_target)
				return true;

			if (linear_runtime_node_limit_hit(context))
				return true;

			if (linear_note_runtime_node_visit(context))
				return true;

			maybe_print_single_run_progress(context, depth, current_round_output_branch_a_mask, current_round_output_branch_b_mask);
			maybe_poll_checkpoint();

			if (accumulated_weight >= context.best_weight)
				return true;

			if (should_prune_remaining_round_lower_bound(depth, accumulated_weight))
				return true;

			return false;
		}

		bool should_prune_remaining_round_lower_bound(int depth, SearchWeight accumulated_weight) const
		{
			if (context.best_weight >= INFINITE_WEIGHT)
				return false;
			if (!search_configuration.enable_remaining_round_lower_bound)
				return false;

			const int rounds_left = search_configuration.round_count - depth;
			if (rounds_left < 0)
				return false;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t table_index = std::size_t(rounds_left);
			if (table_index >= remaining_round_min_weight_table.size())
				return false;
			const SearchWeight weight_lower_bound = remaining_round_min_weight_table[table_index];
			return accumulated_weight + weight_lower_bound >= context.best_weight;
		}

		bool handle_round_end_if_needed(int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight)
		{
			if (depth != search_configuration.round_count)
				return false;

			context.best_weight = accumulated_weight;
			context.best_linear_trail = context.current_linear_trail;
			context.best_input_branch_a_mask = current_round_output_branch_a_mask;
			context.best_input_branch_b_mask = current_round_output_branch_b_mask;
			if (context.checkpoint)
				context.checkpoint->maybe_write(context, "improved");
			if (context.binary_checkpoint)
				context.binary_checkpoint->mark_best_changed();
			if (search_configuration.target_best_weight < INFINITE_WEIGHT && context.best_weight <= search_configuration.target_best_weight)
				context.stop_due_to_target = true;
			return true;
		}

		bool should_prune_state_memoization(int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight)
		{
			if (!search_configuration.enable_state_memoization)
				return false;

			const std::size_t hint = linear_runtime_memo_reserve_hint(context);

			const std::uint64_t key = (std::uint64_t(current_round_output_branch_a_mask) << 32) | std::uint64_t(current_round_output_branch_b_mask);
			return context.memoization.should_prune_and_update(std::size_t(depth), key, accumulated_weight, true, true, hint, 192ull, "linear_memo.reserve", "linear_memo.try_emplace");
		}

		bool should_prune_local_state_dominance(
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

		bool should_prune_local_state_dominance(
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

		bool should_prune_local_state_dominance(
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

		// Prepare the round-local BnB budget before the first nonlinear step fires.
		//
		// The resulting `round_weight_cap` is the exact residual allowance for this round after
		// accounting for the already accumulated trail weight and the lower bound of all later
		// rounds.  Every local operator below must consume from this same cap, so the pruning
		// chain stays mathematically aligned:
		//   injection exact weight
		//   + var-var local weight
		//   + var-const column floor / witness weight
		//   + downstream remaining-round bound.
		bool prepare_round_state(int depth, std::uint32_t current_round_output_branch_a_mask, std::uint32_t current_round_output_branch_b_mask, SearchWeight accumulated_weight)
		{
			auto& state = current_round_state();
			state.round_boundary_depth = depth;
			state.accumulated_weight_so_far = accumulated_weight;
			state.round_index = search_configuration.round_count - depth;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round(depth);
			const SearchWeight base_cap = (context.best_weight >= INFINITE_WEIGHT) ? INFINITE_WEIGHT : remaining_search_weight_budget(context.best_weight, accumulated_weight);
			state.round_weight_cap = (base_cap >= INFINITE_WEIGHT) ? INFINITE_WEIGHT : remaining_search_weight_budget(base_cap, state.remaining_round_weight_lower_bound_after_this_round);
			if (state.round_weight_cap == 0)
				return false;

			// Reverse-round entry bridge: from the current round-output masks, compute the
			// exact affine input-mask space induced by the A->B injection layer.
			linear_prepare_round_entry_bridge(
				state,
				current_round_output_branch_a_mask,
				current_round_output_branch_b_mask );
			if (state.weight_injection_from_branch_a >= state.round_weight_cap)
				return false;

			state.remaining_after_inj_a = state.round_weight_cap - state.weight_injection_from_branch_a;
			if (state.remaining_after_inj_a == 0)
				return false;

			state.second_subconst_weight_cap = std::min(search_configuration.constant_subtraction_weight_cap, state.remaining_after_inj_a - 1);
			state.second_add_weight_cap = std::min(search_configuration.addition_weight_cap, state.remaining_after_inj_a - 1);
			state.second_addition_candidates_for_branch_a = nullptr;
			reset_round_predecessor_buffer();

			return true;
		}

		SearchWeight compute_remaining_round_weight_lower_bound_after_this_round(int depth) const
		{
			if (!search_configuration.enable_remaining_round_lower_bound)
				return 0;
			const int rounds_left_after = search_configuration.round_count - (depth + 1);
			if (rounds_left_after < 0)
				return 0;
			const auto& remaining_round_min_weight_table = search_configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t(rounds_left_after);
			if (idx >= remaining_round_min_weight_table.size())
				return 0;
			return remaining_round_min_weight_table[idx];
		}

		void reset_round_predecessor_buffer()
		{
			auto& state = current_round_state();
			state.round_predecessors.clear();
			state.round_predecessors.reserve(std::min<std::size_t>(search_configuration.maximum_round_predecessors ? search_configuration.maximum_round_predecessors : 32, 512));
		}

		void trim_round_predecessors(bool force)
		{
			if (search_configuration.maximum_round_predecessors == 0)
				return;

			auto& state = current_round_state();
			const std::size_t cap = search_configuration.maximum_round_predecessors;
			if (cap == 0)
				return;
			const std::size_t threshold = std::min<std::size_t>(cap * 8u, 16'384u);
			if (!force && state.round_predecessors.size() <= threshold)
				return;

			std::sort(state.round_predecessors.begin(), state.round_predecessors.end(), [](const LinearTrailStepRecord& a, const LinearTrailStepRecord& b) {
				if (a.round_weight != b.round_weight)
					return a.round_weight < b.round_weight;
				if (a.input_branch_a_mask != b.input_branch_a_mask)
					return a.input_branch_a_mask < b.input_branch_a_mask;
				return a.input_branch_b_mask < b.input_branch_b_mask;
				});
			state.round_predecessors.erase(std::unique(state.round_predecessors.begin(), state.round_predecessors.end(), [](const LinearTrailStepRecord& a, const LinearTrailStepRecord& b) {
				return a.input_branch_a_mask == b.input_branch_a_mask && a.input_branch_b_mask == b.input_branch_b_mask;
				}), state.round_predecessors.end());
			if (state.round_predecessors.size() > cap)
				state.round_predecessors.resize(cap);
		}

		void enumerate_round_predecessors()
		{
			enumerate_injection_from_branch_a_masks();
			trim_round_predecessors(true);
		}

		void enumerate_injection_from_branch_a_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_a_transition,
				search_configuration.maximum_injection_input_masks,
				[this](std::uint32_t m) { handle_injection_from_branch_a_mask(m); });
		}

		void handle_injection_from_branch_a_mask(std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_a)
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_a = chosen_correlated_input_mask_for_injection_from_branch_a;
			linear_project_second_subround_outputs_from_inj_a_choice( state );

			const auto second_subconst_column_floor = compute_second_subconst_q2_floor(state);
			if (!second_subconst_column_floor.has_value())
				return;
			static_assert(
				linear_fixed_beta_hot_path_call_site_requires_strict_candidates(
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorSecondSubconstMaterialization));
			state.second_constant_subtraction_candidates_for_branch_b.clear();
			for (const auto& q2_candidate :
				materialize_varconst_q2_candidates_for_call_site(
					search_configuration,
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorSecondSubconstMaterialization,
					second_subconst_column_floor,
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[6],
					state.second_subconst_weight_cap))
			{
				state.second_constant_subtraction_candidates_for_branch_b.push_back(
					SubConstCandidate{
						q2_candidate.input_mask_alpha,
						q2_candidate.exact_weight_hint });
			}
			materialize_varvar_row_q2_candidates_for_output_mask_u(
				state.second_addition_candidates_storage,
				state.output_branch_a_mask_after_second_addition,
				state.second_add_weight_cap,
				search_configuration);
			state.second_addition_candidates_for_branch_a = &state.second_addition_candidates_storage;

			enumerate_second_addition_candidates();
		}

		void enumerate_second_addition_candidates()
		{
			auto& state = current_round_state();
			if (!state.second_addition_candidates_for_branch_a)
				return;

			for (const auto& second_addition_candidate_for_branch_a : *state.second_addition_candidates_for_branch_a)
			{
				if (linear_runtime_node_limit_hit(context))
					break;
				if (linear_note_runtime_node_visit(context))
					break;

				LinearVarVarQ2Candidate q2_candidate{};
				if (!resolve_varvar_q2_candidate(
					search_configuration,
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap,
					second_addition_candidate_for_branch_a,
					q2_candidate))
				{
					continue;
				}
				const auto q1_weight =
					evaluate_varvar_q1_exact_weight(q2_candidate, 32);
				if (!q1_weight.has_value())
					continue;
				const SearchWeight weight_second_addition = q1_weight.value();
				linear_load_second_add_candidate_state(
					state,
					q2_candidate,
					weight_second_addition );
				if (state.weight_injection_from_branch_a + state.weight_second_addition >= state.round_weight_cap)
				{
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
						break;
					continue;
				}
				handle_second_addition_candidate();
			}
		}

		void handle_second_addition_candidate()
		{
			auto& frame = current_frame();
			auto& state = current_round_state();
			linear_prepare_inj_b_bridge_after_second_add( state );
			if (state.base_weight_after_inj_b >= state.round_weight_cap)
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::SecondConst;
				snapshot.state.second_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_child_residual_candidate(
					context,
					make_linear_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::SecondConst,
						state.input_branch_a_mask_before_second_addition,
						state.branch_b_mask_contribution_from_second_addition_term,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_inj_b);
				return;
			}
			if (state.base_weight_after_inj_b + ensure_second_subconst_q2_floor(state) >= state.round_weight_cap)
			{
				LinearSearchFrame snapshot = frame;
				snapshot.stage = LinearSearchStage::SecondConst;
				snapshot.state.second_constant_subtraction_candidate_index = 0;
				(void)try_register_linear_child_residual_candidate(
					context,
					make_linear_boundary_record(
						context,
						search_configuration.round_count - state.round_boundary_depth,
						LinearSearchStage::SecondConst,
						state.input_branch_a_mask_before_second_addition,
						state.branch_b_mask_contribution_from_second_addition_term,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b),
					&snapshot,
					&context.current_linear_trail,
					state.accumulated_weight_so_far + state.base_weight_after_inj_b);
				return;
			}

			enumerate_injection_from_branch_b_masks();
		}

		void enumerate_injection_from_branch_b_masks()
		{
			auto& state = current_round_state();
			enumerate_affine_subspace_input_masks(
				context,
				state.injection_from_branch_b_transition,
				search_configuration.maximum_injection_input_masks,
				[this](std::uint32_t m) { handle_injection_from_branch_b_mask(m); });
		}

		void handle_injection_from_branch_b_mask(std::uint32_t chosen_correlated_input_mask_for_injection_from_branch_b)
		{
			auto& state = current_round_state();
			state.chosen_correlated_input_mask_for_injection_from_branch_b = chosen_correlated_input_mask_for_injection_from_branch_b;
			enumerate_second_constant_subtraction_candidates();
		}

		void enumerate_second_constant_subtraction_candidates()
		{
			auto& state = current_round_state();
			if (should_prune_local_state_dominance(
				LinearSearchStage::InjB,
				state,
				state.accumulated_weight_so_far + state.weight_injection_from_branch_a + state.weight_second_addition))
			{
				return;
			}
			for (const auto& second_constant_subtraction_candidate_for_branch_b : state.second_constant_subtraction_candidates_for_branch_b)
			{
				if (linear_runtime_node_limit_hit(context))
					break;
				if (linear_note_runtime_node_visit(context))
					break;

				LinearVarConstQ2Candidate q2_candidate{};
				if (!resolve_varconst_q2_candidate(
					search_configuration,
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorSecondSubconstMaterialization,
					state.output_branch_b_mask_after_second_constant_subtraction,
					second_constant_subtraction_candidate_for_branch_b,
					q2_candidate))
				{
					continue;
				}
				const auto q1_weight =
					evaluate_varconst_q1_exact_weight(
						q2_candidate,
						NeoAlzetteCore::ROUND_CONSTANTS[6],
						state.second_subconst_weight_cap,
						&state.second_subconst_fixed_alpha_weight_floor,
						32);
				if (!q1_weight.has_value())
					continue;
				linear_load_second_subconst_candidate_state(
					state,
					q2_candidate,
					q1_weight.value() );
				if (state.base_weight_after_inj_b + state.weight_second_constant_subtraction >= state.round_weight_cap)
				{
					auto& frame = current_frame();
					linear_project_first_subround_outputs_from_inj_b_choice( state );
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstSubconst;
					snapshot.state.weight_second_constant_subtraction = state.weight_second_constant_subtraction;
					snapshot.state.base_weight_after_second_subconst = state.base_weight_after_second_subconst;
					snapshot.state.first_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstSubconst,
							state,
							state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_inj_b + state.weight_second_constant_subtraction);
					if (q2_candidate.ordered_stream.safe_break_when_cap_exceeded)
						break;
					continue;
				}

				if (state.base_weight_after_second_subconst >= state.round_weight_cap)
					continue;
				if ( !linear_prepare_first_subround_caps( state, search_configuration ) )
					continue;
				linear_project_first_subround_outputs_from_inj_b_choice( state );

				const auto first_subconst_column_floor = compute_first_subconst_q2_floor(state);
				if (!first_subconst_column_floor.has_value())
					continue;
				if (state.base_weight_after_second_subconst + state.first_subconst_weight_floor >= state.round_weight_cap)
				{
					auto& frame = current_frame();
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstSubconst;
					snapshot.state.first_constant_subtraction_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstSubconst,
							state,
							state.accumulated_weight_so_far + state.base_weight_after_second_subconst),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + state.base_weight_after_second_subconst);
					continue;
				}

				enumerate_first_subround_candidates();
			}
		}

		void enumerate_first_subround_candidates()
		{
			auto& frame = current_frame();
			auto& state = current_round_state();
			if (should_prune_local_state_dominance(
				LinearSearchStage::SecondConst,
				state,
				state.accumulated_weight_so_far + state.base_weight_after_inj_b))
			{
				frame.stage = LinearSearchStage::SecondAdd;
				return;
			}
			// Final reverse subround:
			// - exact sub-const candidates for A <- A ⊟ RC1
			// - exact var-var addition candidates for B <- B ⊞ T0
			// Together these determine the predecessor masks at the round input boundary.
			std::vector<SubConstCandidate> first_constant_subtraction_candidates_for_branch_a{};
			static_assert(
				linear_fixed_beta_hot_path_call_site_requires_strict_candidates(
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorFirstSubconstMaterialization));
			first_constant_subtraction_candidates_for_branch_a.clear();
			for (const auto& q2_candidate :
				materialize_varconst_q2_candidates_for_call_site(
					search_configuration,
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorFirstSubconstMaterialization,
					compute_first_subconst_q2_floor(state),
					state.output_branch_a_mask_after_first_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[1],
					state.first_subconst_weight_cap))
			{
				first_constant_subtraction_candidates_for_branch_a.push_back(
					SubConstCandidate{
						q2_candidate.input_mask_alpha,
						q2_candidate.exact_weight_hint });
			}
			std::vector<AddCandidate> first_addition_candidates_for_branch_b{};
			materialize_varvar_row_q2_candidates_for_output_mask_u(
				first_addition_candidates_for_branch_b,
				state.output_branch_b_mask_after_first_addition,
				state.first_add_weight_cap,
				search_configuration);

			for (const auto& first_constant_subtraction_candidate_for_branch_a : first_constant_subtraction_candidates_for_branch_a)
			{
				if (linear_runtime_node_limit_hit(context))
					break;
				if (linear_note_runtime_node_visit(context))
					break;

				LinearVarConstQ2Candidate first_subconst_q2{};
				if (!resolve_varconst_q2_candidate(
					search_configuration,
					LinearFixedBetaHotPathCallSite::EngineRoundPredecessorFirstSubconstMaterialization,
					state.output_branch_a_mask_after_first_constant_subtraction,
					first_constant_subtraction_candidate_for_branch_a,
					first_subconst_q2))
				{
					continue;
				}
				const auto first_subconst_q1_weight =
					evaluate_varconst_q1_exact_weight(
						first_subconst_q2,
						NeoAlzetteCore::ROUND_CONSTANTS[1],
						state.first_subconst_weight_cap,
						&state.first_subconst_fixed_alpha_weight_floor,
						32);
				if (!first_subconst_q1_weight.has_value())
					continue;
				const std::uint32_t input_branch_a_mask_before_first_constant_subtraction = first_subconst_q2.input_mask_alpha;
				const SearchWeight base_weight_after_first_subconst = state.base_weight_after_second_subconst + first_subconst_q1_weight.value();
				if (should_prune_local_state_dominance(
					LinearSearchStage::FirstSubconst,
					state,
					state.accumulated_weight_so_far + state.base_weight_after_second_subconst))
				{
					return;
				}
				if (base_weight_after_first_subconst >= state.round_weight_cap)
				{
					LinearSearchFrame snapshot = frame;
					snapshot.stage = LinearSearchStage::FirstAdd;
					snapshot.state.input_branch_a_mask_before_first_constant_subtraction_current = input_branch_a_mask_before_first_constant_subtraction;
					snapshot.state.weight_first_constant_subtraction_current = first_subconst_q1_weight.value();
					snapshot.state.first_addition_candidate_index = 0;
					(void)try_register_linear_child_residual_candidate(
						context,
						make_linear_boundary_record(
							context,
							search_configuration.round_count - state.round_boundary_depth,
							LinearSearchStage::FirstAdd,
							input_branch_a_mask_before_first_constant_subtraction,
							state.output_branch_b_mask_after_first_addition,
							state.accumulated_weight_so_far + base_weight_after_first_subconst),
						&snapshot,
						&context.current_linear_trail,
						state.accumulated_weight_so_far + base_weight_after_first_subconst);
					if (first_subconst_q2.ordered_stream.safe_break_when_cap_exceeded)
						break;
					continue;
				}

				for (const auto& first_addition_candidate_for_branch_b : first_addition_candidates_for_branch_b)
				{
					if (linear_runtime_node_limit_hit(context))
						break;
					if (linear_note_runtime_node_visit(context))
						break;

					LinearVarVarQ2Candidate first_add_q2{};
					if (!resolve_varvar_q2_candidate(
						search_configuration,
						state.output_branch_b_mask_after_first_addition,
						state.first_add_weight_cap,
						first_addition_candidate_for_branch_b,
						first_add_q2))
					{
						continue;
					}
					const auto first_add_q1_weight =
						evaluate_varvar_q1_exact_weight(first_add_q2, 32);
					if (!first_add_q1_weight.has_value())
						continue;
					SearchWeight weight_first_addition = first_add_q1_weight.value();
					state.first_add_trail_sum_wire_u = first_add_q2.sum_wire_u;

					const LinearTrailStepRecord step =
						linear_build_round_step(
							state,
							input_branch_a_mask_before_first_constant_subtraction,
							first_subconst_q1_weight.value(),
							first_add_q2,
							weight_first_addition );
					const SearchWeight total_w = step.round_weight;
					if (should_prune_local_state_dominance(
						LinearSearchStage::FirstAdd,
						input_branch_a_mask_before_first_constant_subtraction,
						state.output_branch_b_mask_after_first_addition,
						0u,
						state.accumulated_weight_so_far + base_weight_after_first_subconst))
					{
						return;
					}
					if (total_w >= state.round_weight_cap)
					{
						const int rounds_remaining_after = search_configuration.round_count - (state.round_boundary_depth + 1);
						if (rounds_remaining_after > 0)
						{
							auto trail_snapshot = context.current_linear_trail;
							trail_snapshot.push_back(step);
							LinearSearchFrame child{};
							child.stage = LinearSearchStage::Enter;
							child.trail_size_at_entry = trail_snapshot.size() - 1u;
							child.state.round_boundary_depth = state.round_boundary_depth + 1;
							child.state.accumulated_weight_so_far = state.accumulated_weight_so_far + total_w;
							child.state.round_output_branch_a_mask = step.input_branch_a_mask;
							child.state.round_output_branch_b_mask = step.input_branch_b_mask;
							(void)try_register_linear_child_residual_candidate(
								context,
								make_linear_boundary_record(
									context,
									rounds_remaining_after,
									LinearSearchStage::Enter,
									step.input_branch_a_mask,
									step.input_branch_b_mask,
									state.accumulated_weight_so_far + total_w),
								&child,
								&trail_snapshot,
								state.accumulated_weight_so_far + total_w);
						}
						if (first_add_q2.ordered_stream.safe_break_when_cap_exceeded)
							break;
						continue;
					}

					state.round_predecessors.push_back(step);
					trim_round_predecessors(false);
				}
			}
		}
	};

	class LinearBNBScheduler final
	{
	public:
		LinearBNBScheduler(LinearBestSearchContext& context_in, LinearSearchCursor& cursor_in)
			: context(context_in), cursor(cursor_in), helper(context_in, cursor_in)
		{
		}

		void run_from_cursor()
		{
			if (!linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_best_engine(
				context.configuration.varvar_modular_add_bnb_mode))
				return;
			helper.rebuild_pending_frontier_indexes();
			if (!context.active_problem_valid && !cursor.stack.empty())
				helper.set_active_problem(helper.make_root_source_record(), true);
			while (true)
			{
				LinearBNB bnb(context, cursor, helper);
				bnb.search_from_cursor();
				if (!cursor.stack.empty())
					return;
				helper.complete_active_problem();
				if (!helper.restore_next_pending_frontier_entry())
					return;
			}
		}

		void interrupt_root_if_needed(bool hit_node_limit, bool hit_time_limit)
		{
			helper.interrupt_root_if_needed(hit_node_limit, hit_time_limit);
		}

	private:
		LinearBestSearchContext& context;
		LinearSearchCursor& cursor;
		LinearEngineResidualFrontierHelper helper;
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
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt)
	{
		MatsuiSearchRunLinearResult result{};
		if (checkpoint_path.empty())
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}

		LinearBestSearchContext  context{};
		LinearCheckpointLoadResult load{};
		if (!read_linear_checkpoint(checkpoint_path, load, context.memoization))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		if (load.start_mask_a != expected_output_branch_a_mask || load.start_mask_b != expected_output_branch_b_mask)
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if (!linear_configs_compatible_for_resume(expected_configuration, load.configuration))
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
				load.runtime_checkpoint_every_seconds);
		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask default_runtime_override_mask{
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
				load.accumulated_elapsed_usec);
		const LinearResumeFingerprint loaded_fingerprint = compute_linear_resume_fingerprint(load);

		context.configuration = exec_configuration;
		if (!linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_best_engine(
			context.configuration.varvar_modular_add_bnb_mode))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode;
			return result;
		}
		TwilightDream::best_search_shared_core::apply_resume_runtime_plan(context, resume_runtime_plan);
		context.start_output_branch_a_mask = load.start_mask_a;
		context.start_output_branch_b_mask = load.start_mask_b;
		context.best_weight = load.best_weight;
		context.best_input_branch_a_mask = load.best_input_mask_a;
		context.best_input_branch_b_mask = load.best_input_mask_b;
		context.best_linear_trail = std::move(load.best_trail);
		context.current_linear_trail = std::move(load.current_trail);
		context.pending_frontier = std::move(load.pending_frontier);
		context.pending_frontier_entries = std::move(load.pending_frontier_entries);
		context.completed_source_input_pairs = std::move(load.completed_source_input_pairs);
		context.completed_output_as_next_input_pairs = std::move(load.completed_output_as_next_input_pairs);
		context.completed_residual_set = std::move(load.completed_residual_set);
		context.best_prefix_by_residual_key = std::move(load.best_prefix_by_residual_key);
		context.global_residual_result_table = std::move(load.global_residual_result_table);
		context.residual_counters = load.residual_counters;
		context.active_problem_valid = load.active_problem_valid;
		context.active_problem_is_root = load.active_problem_is_root;
		context.active_problem_record = load.active_problem_record;
		context.history_log_output_path = load.history_log_path;
		context.runtime_log_output_path = load.runtime_log_path;
		context.checkpoint = checkpoint;
		context.runtime_event_log = runtime_event_log;
		context.binary_checkpoint = binary_checkpoint;
		context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata{};

		if (context.configuration.target_best_weight < INFINITE_WEIGHT &&
			context.best_weight < INFINITE_WEIGHT &&
			context.best_weight <= context.configuration.target_best_weight)
		{
			context.stop_due_to_target = true;
		}

		// The loaded cursor already contains the saved cLAT/split-lookup-recombine state and
		// exact sub-const state for the in-flight round, so DFS can resume at the same node.
		LinearSearchCursor cursor = std::move(load.cursor);
		const LinearResumeFingerprint materialized_fingerprint = compute_linear_resume_fingerprint(context, cursor);
		if (materialized_fingerprint.hash != loaded_fingerprint.hash)
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if (best_search_shared_core::initialize_progress_tracking(
			context,
			best_search_shared_core::effective_resume_progress_interval_seconds(context, progress_reporting_opt)))
		{
			context.progress_print_masks = progress_print_masks;
			if (print_output)
			{
				std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
				TwilightDream::runtime_component::print_progress_prefix(std::cout);
				std::cout << "[Progress] enabled: every " << context.progress_every_seconds << " seconds (time-check granularity ~" << (context.progress_node_mask + 1) << " nodes)\n\n";
			}
		}

		best_search_shared_core::run_resume_control_session(
			context,
			cursor,
			[&](LinearBestSearchContext& ctx) {
				best_search_shared_core::prepare_binary_checkpoint(
					ctx.binary_checkpoint,
					ctx.runtime_controls.checkpoint_every_seconds,
					true,
					checkpoint_path);
			},
			[](LinearBestSearchContext& ctx) {
				begin_linear_runtime_invocation(ctx);
			},
			[](LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor) {
				linear_runtime_log_resume_event(ctx, resume_cursor, "resume_start");
			},
			[](LinearBestSearchContext& ctx, LinearSearchCursor&) {
				if (ctx.checkpoint)
					ctx.checkpoint->maybe_write(ctx, "resume_init");
			},
			[](LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor) {
				if (ctx.checkpoint)
					write_linear_resume_snapshot(*ctx.checkpoint, ctx, resume_cursor, "resume_init");
			},
			[](LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor) {
				ScopedRuntimeTimeLimitProbe time_probe(ctx.runtime_controls, ctx.runtime_state);
				linear_best_search_continue_from_cursor(ctx, resume_cursor);
			},
			[](LinearBestSearchContext& ctx) {
				return linear_runtime_budget_hit(ctx);
			},
			[](LinearBestSearchContext& ctx, const char* reason) {
				linear_runtime_log_basic_event(ctx, "checkpoint_preserved", reason);
			},
			[](LinearBestSearchContext& ctx) {
				if (runtime_maximum_search_nodes_hit(ctx.runtime_controls, ctx.runtime_state))
					linear_runtime_log_basic_event(ctx, "resume_stop", "hit_maximum_search_nodes");
				else if (runtime_time_limit_hit(ctx.runtime_controls, ctx.runtime_state))
					linear_runtime_log_basic_event(ctx, "resume_stop", "hit_time_limit");
				else if (ctx.stop_due_to_target)
					linear_runtime_log_basic_event(ctx, "resume_stop", "hit_target_best_weight");
				else
					linear_runtime_log_basic_event(ctx, "resume_stop", "completed");
			});

		const bool resume_hit_node_limit = runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state);
		const bool resume_hit_time_limit = runtime_time_limit_hit(context.runtime_controls, context.runtime_state);
		LinearBNBScheduler(context, cursor).interrupt_root_if_needed(resume_hit_node_limit, resume_hit_time_limit);
		if ((resume_hit_node_limit || resume_hit_time_limit) && context.binary_checkpoint)
			(void)context.binary_checkpoint->write_now(context, cursor, "runtime_limit_snapshot");

		result.nodes_visited = context.visited_node_count;
		result.hit_maximum_search_nodes = resume_hit_node_limit;
		result.hit_time_limit = resume_hit_time_limit;
		result.hit_target_best_weight = context.stop_due_to_target;
		result.used_non_strict_branch_cap = linear_configuration_has_strict_branch_cap(context.configuration);
		result.used_target_best_weight_shortcut =
			context.configuration.target_best_weight < INFINITE_WEIGHT &&
			context.best_weight <= context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;
		result.best_input_branch_a_mask = context.best_input_branch_a_mask;
		result.best_input_branch_b_mask = context.best_input_branch_b_mask;

		const bool used_non_strict_remaining_round_bound =
			linear_configuration_uses_non_strict_remaining_round_bound(context.configuration);
		if (context.best_linear_trail.empty())
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_linear_best_search_strict_rejection_reason(
					result,
					context.configuration,
					used_non_strict_remaining_round_bound);
			if (print_output)
			{
				print_linear_weight_sliced_clat_banner(context.configuration);
				std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
				if (result.hit_maximum_search_nodes || result.hit_time_limit)
					std::cout << "[PAUSE] no trail found yet before the runtime budget expired; checkpoint/resume can continue.\n";
				else
					std::cout << "[FAIL] no trail found within limits.\n";
				std::cout << "  nodes_visited=" << result.nodes_visited;
				if (result.hit_maximum_search_nodes)
					std::cout << "  [HIT maximum_search_nodes]";
				if (result.hit_time_limit)
					std::cout << "  [HIT maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "]";
				if (result.hit_target_best_weight)
					std::cout << "  [HIT target_best_weight=" << context.configuration.target_best_weight << "]";
				std::cout << "\n";
				std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string(best_weight_certification_status(result)) << "\n";
				std::cout << "  exact_best_weight_certified=" << (result.best_weight_certified ? 1 : 0) << "\n";
			}
			return result;
		}

		result.found = true;
		result.best_weight = context.best_weight;
		result.best_linear_trail = std::move(context.best_linear_trail);
		result.strict_rejection_reason =
			classify_linear_best_search_strict_rejection_reason(
				result,
				context.configuration,
				used_non_strict_remaining_round_bound);
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if (print_output)
		{
			print_linear_weight_sliced_clat_banner(context.configuration);
			std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name(TwilightDream::runtime_component::runtime_time_limit_scope())
				<< "  startup_memory_gate_policy=" << (context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject") << "\n";
			std::cout << "[OK] best_weight=" << result.best_weight << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited;
			if (result.hit_maximum_search_nodes)
				std::cout << "  [HIT maximum_search_nodes]";
			if (result.hit_time_limit)
				std::cout << "  [HIT maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "]";
			if (result.hit_target_best_weight)
				std::cout << "  [HIT target_best_weight=" << context.configuration.target_best_weight << "]";
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string(best_weight_certification_status(result)) << "\n";
			std::cout << "  exact_best_weight_certified=" << (result.best_weight_certified ? 1 : 0) << "\n";
		}

		return result;
	}

	void linear_best_search_continue_from_cursor(LinearBestSearchContext& context, LinearSearchCursor& cursor)
	{
		LinearBNBScheduler(context, cursor).run_from_cursor();
	}

	MatsuiSearchRunLinearResult run_linear_best_search(
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask,
		const LinearBestSearchConfiguration& search_configuration,
		const LinearBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_masks,
		SearchWeight seeded_upper_bound_weight,
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata)
	{
		MatsuiSearchRunLinearResult result{};
		if (search_configuration.round_count <= 0)
			return result;
		if (!linear_varvar_modular_add_bnb_mode_integrated_in_neoalzette_linear_best_engine(
			search_configuration.varvar_modular_add_bnb_mode))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::UnsupportedVarVarModularAddBnBMode;
			return result;
		}

		LinearBestSearchContext context{};
		context.configuration = search_configuration;
		context.runtime_controls = runtime_controls;
		context.start_output_branch_a_mask = output_branch_a_mask;
		context.start_output_branch_b_mask = output_branch_b_mask;
		context.checkpoint = checkpoint;
		context.runtime_event_log = runtime_event_log;
		context.binary_checkpoint = binary_checkpoint;
		context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata{};
		best_search_shared_core::prepare_binary_checkpoint(
			context.binary_checkpoint,
			context.runtime_controls.checkpoint_every_seconds,
			false);

		SearchWeight& best = context.best_weight;
		std::uint32_t& best_input_branch_a_mask = context.best_input_branch_a_mask;
		std::uint32_t& best_input_branch_b_mask = context.best_input_branch_b_mask;
		std::vector<LinearTrailStepRecord>& best_linear_trail = context.best_linear_trail;
		std::vector<LinearTrailStepRecord>& current = context.current_linear_trail;

		current.reserve(std::size_t(search_configuration.round_count));

		// Runtime init (single-run).
		begin_linear_runtime_invocation(context);
		best_search_shared_core::SearchControlSession<LinearBestSearchContext> control_session(context);
		control_session.begin();
		linear_runtime_log_basic_event(context, "best_search_start");
		context.memoization.initialize(std::size_t(search_configuration.round_count) + 1u, search_configuration.enable_state_memoization, "linear_memo.init");

		bool remaining_round_lower_bound_disabled_due_to_strict = false;
		bool remaining_round_lower_bound_autogenerated = false;
		bool remaining_round_lower_bound_used_non_strict = false;

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		const LinearRemainingRoundLowerBoundBootstrapStatus remaining_round_lower_bound_status =
			prepare_linear_remaining_round_lower_bound_table(
				context.configuration,
				search_configuration.round_count,
				output_branch_a_mask,
				output_branch_b_mask);
		remaining_round_lower_bound_disabled_due_to_strict = remaining_round_lower_bound_status.disabled_due_to_strict;
		remaining_round_lower_bound_autogenerated = remaining_round_lower_bound_status.autogenerated;
		remaining_round_lower_bound_used_non_strict = remaining_round_lower_bound_status.used_non_strict_generation;

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if (seeded_upper_bound_weight < INFINITE_WEIGHT && seeded_upper_bound_weight < best)
		{
			best = seeded_upper_bound_weight;
			if (seeded_upper_bound_trail && !seeded_upper_bound_trail->empty())
			{
				best_linear_trail = *seeded_upper_bound_trail;
				best_input_branch_a_mask = best_linear_trail.back().input_branch_a_mask;
				best_input_branch_b_mask = best_linear_trail.back().input_branch_b_mask;
			}
		}

		// Optional: persist an initial snapshot once, even if no finite best is known yet.
		if (checkpoint)
		{
			checkpoint->maybe_write(context, "init");
		}

		if (print_output)
		{
			print_linear_weight_sliced_clat_banner(search_configuration);
			std::cout << "[BestSearch] mode=matsui(injection-affine)(reverse)\n";
			std::cout << "  rounds=" << search_configuration.round_count << "  search_mode=" << search_mode_to_string(search_configuration.search_mode)
				<< "  runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds
				<< "  memo=" << (search_configuration.enable_state_memoization ? "on" : "off") << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name(TwilightDream::runtime_component::runtime_time_limit_scope())
				<< "  startup_memory_gate_policy=" << (context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject") << "\n";
			std::cout << "  max_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  max_round_predecessors=" << search_configuration.maximum_round_predecessors << "\n";
			std::cout << "  varvar_modular_add_bnb_mode=" << linear_varvar_modular_add_bnb_mode_to_string(search_configuration.varvar_modular_add_bnb_mode) << "\n";
			if (remaining_round_lower_bound_disabled_due_to_strict)
			{
				std::cout << "  remaining_round_lower_bound=off  reason=strict_mode_non_exhaustive_generation\n";
			}
			else if (context.configuration.enable_remaining_round_lower_bound)
			{
				std::cout << "  remaining_round_lower_bound=on";
				if (remaining_round_lower_bound_autogenerated)
				{
					std::cout << "  source=auto_generated";
					if (context.configuration.remaining_round_lower_bound_generation_nodes != 0)
						std::cout << "  gen_nodes=" << context.configuration.remaining_round_lower_bound_generation_nodes;
					if (context.configuration.remaining_round_lower_bound_generation_seconds != 0)
						std::cout << "  gen_seconds=" << context.configuration.remaining_round_lower_bound_generation_seconds;
				}
				std::cout << "\n";
			}
			if (best < INFINITE_WEIGHT)
			{
				std::cout << "  seeded_upper_bound_weight=" << best << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if (best_search_shared_core::initialize_progress_tracking(context, context.runtime_controls.progress_every_seconds))
		{
			context.progress_print_masks = progress_print_masks;
			if (print_output)
			{
				std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
				TwilightDream::runtime_component::print_progress_prefix(std::cout);
				std::cout << "[Progress] enabled: every " << context.progress_every_seconds << " seconds (time-check granularity ~" << (context.progress_node_mask + 1) << " nodes)\n\n";
			}
		}

		LinearSearchCursor cursor{};
		cursor.stack.clear();
		cursor.stack.reserve(std::size_t(std::max(1, search_configuration.round_count)) + 1u);
		context.current_linear_trail.clear();
		LinearSearchFrame root_frame{};
		root_frame.stage = LinearSearchStage::Enter;
		root_frame.trail_size_at_entry = context.current_linear_trail.size();
		root_frame.state.round_boundary_depth = 0;
		root_frame.state.accumulated_weight_so_far = 0;
		root_frame.state.round_output_branch_a_mask = output_branch_a_mask;
		root_frame.state.round_output_branch_b_mask = output_branch_b_mask;
		cursor.stack.push_back(root_frame);
		ScopedRuntimeTimeLimitProbe time_probe(context.runtime_controls, context.runtime_state);
		linear_best_search_continue_from_cursor(context, cursor);
		control_session.finalize(
			context.binary_checkpoint,
			cursor.stack.empty(),
			linear_runtime_budget_hit(context),
			[&](const char* reason)
			{
				return context.binary_checkpoint->write_now(context, cursor, reason);
			},
			[&](const char* reason)
			{
				linear_runtime_log_basic_event(context, "checkpoint_preserved", reason);
			});
		if (runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state))
			linear_runtime_log_basic_event(context, "best_search_stop", "hit_maximum_search_nodes");
		else if (runtime_time_limit_hit(context.runtime_controls, context.runtime_state))
			linear_runtime_log_basic_event(context, "best_search_stop", "hit_time_limit");
		else if (context.stop_due_to_target)
			linear_runtime_log_basic_event(context, "best_search_stop", "hit_target_best_weight");
		else
			linear_runtime_log_basic_event(context, "best_search_stop", "completed");

		const bool hit_node_limit = runtime_maximum_search_nodes_hit(context.runtime_controls, context.runtime_state);
		const bool hit_time_limit = runtime_time_limit_hit(context.runtime_controls, context.runtime_state);
		LinearBNBScheduler(context, cursor).interrupt_root_if_needed(hit_node_limit, hit_time_limit);
		if ((hit_node_limit || hit_time_limit) && context.binary_checkpoint)
			(void)context.binary_checkpoint->write_now(context, cursor, "runtime_limit_snapshot");

		result.nodes_visited = context.visited_node_count;
		result.hit_maximum_search_nodes = hit_node_limit;
		result.hit_time_limit = hit_time_limit;
		result.hit_target_best_weight = context.stop_due_to_target;
		result.used_non_strict_branch_cap = linear_configuration_has_strict_branch_cap(search_configuration);
		result.used_target_best_weight_shortcut = result.hit_target_best_weight;
		result.exhaustive_completed = !result.hit_maximum_search_nodes && !result.hit_time_limit && !result.used_target_best_weight_shortcut;
		if (best < INFINITE_WEIGHT && !best_linear_trail.empty())
		{
			result.found = true;
			result.best_weight = best;
			result.best_linear_trail = std::move(best_linear_trail);
			result.best_input_branch_a_mask = best_input_branch_a_mask;
			result.best_input_branch_b_mask = best_input_branch_b_mask;
		}
		result.strict_rejection_reason =
			classify_linear_best_search_strict_rejection_reason(
				result,
				search_configuration,
				remaining_round_lower_bound_used_non_strict);
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;
		return result;
	}


}  // namespace TwilightDream::auto_search_linear
