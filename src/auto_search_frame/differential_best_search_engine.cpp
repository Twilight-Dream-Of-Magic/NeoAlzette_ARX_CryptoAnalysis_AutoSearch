#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

namespace TwilightDream::auto_search_differential
{
	namespace
	{
		enum class DifferentialResidualStageCursor : std::uint8_t
		{
			FirstAdd = 0,
			FirstConst = 1,
			InjB = 2,
			SecondAdd = 3,
			SecondConst = 4,
			InjA = 5,
			RoundEnd = 6
		};

		std::uint8_t normalize_differential_residual_stage_cursor( DifferentialSearchStage stage ) noexcept
		{
			switch ( stage )
			{
			case DifferentialSearchStage::Enter:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::FirstAdd );
			case DifferentialSearchStage::FirstAdd:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::FirstAdd );
			case DifferentialSearchStage::FirstConst:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::FirstConst );
			case DifferentialSearchStage::InjB:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::InjB );
			case DifferentialSearchStage::SecondAdd:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::SecondAdd );
			case DifferentialSearchStage::SecondConst:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::SecondConst );
			case DifferentialSearchStage::InjA:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::InjA );
			default:
				return static_cast<std::uint8_t>( DifferentialResidualStageCursor::RoundEnd );
			}
		}

		inline std::uint32_t differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
			std::uint32_t branch_b_difference_before_prewhitening ) noexcept
		{
			// Explicit split step:
			//   B_pre = B_raw xor RC[4]
			// XOR-difference is invariant under xor-by-constant, so the propagated
			// difference on the pre-whitened wire is identical and costs weight 0.
			return branch_b_difference_before_prewhitening;
		}

		inline std::uint32_t differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
			std::uint32_t branch_a_difference_before_prewhitening ) noexcept
		{
			// Explicit split step:
			//   A_pre = A_raw xor RC[9]
			// XOR-difference is invariant under xor-by-constant.
			return branch_a_difference_before_prewhitening;
		}

		inline std::uint32_t differential_apply_cross_xor_rot_r0_on_branch_a(
			std::uint32_t branch_a_difference_before_xor,
			std::uint32_t branch_b_difference_current ) noexcept
		{
			// Explicit zero-cost deterministic step:
			//   A ^= rotl(B, CROSS_XOR_ROT_R0)
			return branch_a_difference_before_xor ^
				NeoAlzetteCore::rotl<std::uint32_t>(
					branch_b_difference_current,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		}

		inline std::uint32_t differential_apply_cross_xor_rot_r1_on_branch_b(
			std::uint32_t branch_b_difference_before_xor,
			std::uint32_t branch_a_difference_current ) noexcept
		{
			// Explicit zero-cost deterministic step:
			//   B ^= rotl(A, CROSS_XOR_ROT_R1)
			return branch_b_difference_before_xor ^
				NeoAlzetteCore::rotl<std::uint32_t>(
					branch_a_difference_current,
					NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		}

		inline void differential_apply_first_subround_cross_xor_bridge(
			std::uint32_t output_branch_a_difference_after_first_constant_subtraction,
			std::uint32_t output_branch_b_difference_after_first_addition,
			std::uint32_t& branch_a_difference_after_first_xor_with_rotated_branch_b,
			std::uint32_t& branch_b_difference_after_first_xor_with_rotated_branch_a ) noexcept
		{
			branch_a_difference_after_first_xor_with_rotated_branch_b =
				differential_apply_cross_xor_rot_r0_on_branch_a(
					output_branch_a_difference_after_first_constant_subtraction,
					output_branch_b_difference_after_first_addition );
			branch_b_difference_after_first_xor_with_rotated_branch_a =
				differential_apply_cross_xor_rot_r1_on_branch_b(
					output_branch_b_difference_after_first_addition,
					branch_a_difference_after_first_xor_with_rotated_branch_b );
		}

		inline void differential_apply_second_subround_cross_xor_bridge(
			std::uint32_t output_branch_a_difference_after_second_addition,
			std::uint32_t output_branch_b_difference_after_second_constant_subtraction,
			std::uint32_t& branch_b_difference_after_second_xor_with_rotated_branch_a,
			std::uint32_t& branch_a_difference_after_second_xor_with_rotated_branch_b ) noexcept
		{
			branch_b_difference_after_second_xor_with_rotated_branch_a =
				differential_apply_cross_xor_rot_r0_on_branch_a(
					output_branch_b_difference_after_second_constant_subtraction,
					output_branch_a_difference_after_second_addition );
			branch_a_difference_after_second_xor_with_rotated_branch_b =
				differential_apply_cross_xor_rot_r1_on_branch_b(
					output_branch_a_difference_after_second_addition,
					branch_b_difference_after_second_xor_with_rotated_branch_a );
		}

		struct DifferentialEngineResidualFrontierHelper final
		{
			DifferentialEngineResidualFrontierHelper(
				DifferentialBestSearchContext& context_in,
				DifferentialSearchCursor& cursor_in )
				: context( context_in ), cursor( cursor_in )
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
					TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Differential,
					TwilightDream::residual_frontier_shared::ResidualObjectiveKind::BestWeight,
					context.configuration.round_count,
					normalize_differential_residual_stage_cursor( DifferentialSearchStage::Enter ),
					context.start_difference_a,
					context.start_difference_b,
					SearchWeight( 0 ),
					sequence,
					context.run_visited_node_count );
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, context.configuration.round_count );
				record.key.suffix_profile_id =
					compute_differential_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = root_residual_source_tag();
				return record;
			}

			TwilightDream::residual_frontier_shared::ResidualProblemRecord make_boundary_record(
				std::int32_t rounds_remaining,
				DifferentialSearchStage stage_cursor,
				std::uint32_t pair_a,
				std::uint32_t pair_b,
				SearchWeight best_prefix_weight ) const noexcept
			{
				const auto& counters = context.residual_counters;
				const std::uint64_t sequence =
					counters.interrupted_output_as_next_input_pair_count +
					counters.completed_output_as_next_input_pair_count +
					1u;
				auto record = TwilightDream::residual_frontier_shared::make_residual_problem_record(
					TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Differential,
					TwilightDream::residual_frontier_shared::ResidualObjectiveKind::BestWeight,
					rounds_remaining,
					normalize_differential_residual_stage_cursor( stage_cursor ),
					pair_a,
					pair_b,
					best_prefix_weight,
					sequence,
					context.run_visited_node_count );
				record.key.absolute_round_index =
					compute_residual_absolute_round_index( context.configuration.round_count, rounds_remaining );
				record.key.suffix_profile_id =
					compute_differential_residual_suffix_profile_id( context.configuration );
				record.key.source_tag = child_residual_source_tag();
				return record;
			}

			void emit_source_pair_event(
				TwilightDream::residual_frontier_shared::ResidualPairEventKind kind,
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				bool persistent )
			{
				using namespace TwilightDream::residual_frontier_shared;
				if ( kind == ResidualPairEventKind::InterruptedSourceInputPair )
					++context.residual_counters.interrupted_source_input_pair_count;
				else if ( kind == ResidualPairEventKind::CompletedSourceInputPair )
					++context.residual_counters.completed_source_input_pair_count;

				{
					std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
					TwilightDream::runtime_component::print_progress_prefix( std::cout );
					std::cout << "[Residual][Differential] event=" << residual_pair_event_kind_to_string( kind )
							  << " rounds_remaining=" << record.key.rounds_remaining
							  << " stage_cursor=" << unsigned( record.key.stage_cursor )
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

				if ( !persistent || !context.runtime_event_log )
					return;
				context.runtime_event_log->write_event(
					residual_pair_event_kind_to_string( kind ),
					[&]( std::ostream& out ) {
						out << "domain=differential\n";
						out << "rounds_remaining=" << record.key.rounds_remaining << "\n";
						out << "stage_cursor=" << unsigned( record.key.stage_cursor ) << "\n";
						write_residual_problem_key_debug_fields_multiline( out, record.key );
						out << "pair_a=" << hex8( record.key.pair_a ) << "\n";
						out << "pair_b=" << hex8( record.key.pair_b ) << "\n";
						out << "interrupted_source_input_pair_count=" << context.residual_counters.interrupted_source_input_pair_count << "\n";
						out << "completed_source_input_pair_count=" << context.residual_counters.completed_source_input_pair_count << "\n";
					} );
			}

			void emit_interrupted_output_pair_progress(
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				std::uint64_t recent_added_count )
			{
				using namespace TwilightDream::residual_frontier_shared;
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Residual][Differential] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::InterruptedOutputAsNextInputPair )
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
					std::cout << "[Residual][Differential] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::CompletedOutputAsNextInputPair )
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
							out << "domain=differential\n";
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

			bool try_enqueue_pending_frontier_entry( const DifferentialResidualFrontierEntry& entry )
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
				const DifferentialSearchFrame* frame_snapshot = nullptr,
				const std::vector<DifferentialTrailStepRecord>* trail_snapshot = nullptr,
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
					DifferentialResidualFrontierEntry entry {};
					entry.record = record;
					entry.frame_snapshot = std::make_shared<DifferentialSearchFrame>( *frame_snapshot );
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
				bool solved )
			{
				for ( auto& existing : context.global_residual_result_table )
				{
					if ( existing.key == record.key )
					{
						existing.best_weight = context.best_total_weight;
						existing.solved = solved;
						return;
					}
				}

				TwilightDream::residual_frontier_shared::ResidualResultRecord result {};
				result.key = record.key;
				result.best_weight = context.best_total_weight;
				result.solved = solved;
				context.global_residual_result_table.push_back( result );
			}

			void complete_active_problem()
			{
				if ( !context.active_problem_valid )
					return;

				const auto record = context.active_problem_record;
				context.completed_residual_set.emplace( record.key );
				upsert_residual_result( record, true );
				if ( context.active_problem_is_root )
				{
					context.completed_source_input_pairs.push_back( record );
					emit_source_pair_event(
						TwilightDream::residual_frontier_shared::ResidualPairEventKind::CompletedSourceInputPair,
						record,
						true );
				}
				else
				{
					context.completed_output_as_next_input_pairs.push_back( record );
					emit_completed_output_pair_event( record );
				}
				clear_active_problem();
			}

			void interrupt_root_if_needed( bool hit_node_limit, bool hit_time_limit )
			{
				if ( !context.active_problem_valid || !context.active_problem_is_root )
					return;
				if ( !hit_node_limit && !hit_time_limit )
					return;

				emit_source_pair_event(
					TwilightDream::residual_frontier_shared::ResidualPairEventKind::InterruptedSourceInputPair,
					context.active_problem_record,
					true );
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
					DifferentialResidualFrontierEntry entry = context.pending_frontier_entries[ index ];
					if ( !entry.frame_snapshot )
					{
						erase_pending_frontier_entry_at( index );
						continue;
					}
					if ( !retighten_pending_frontier_entry_for_current_incumbent( entry ) )
					{
						erase_pending_frontier_entry_at( index );
						erase_pending_frontier_record_by_key( entry.record.key );
						continue;
					}

					erase_pending_frontier_entry_at( index );
					erase_pending_frontier_record_by_key( entry.record.key );

					context.current_differential_trail = std::move( entry.current_trail_snapshot );
					cursor.stack.clear();
					cursor.stack.push_back( *entry.frame_snapshot );
					set_active_problem( entry.record, false );
					reset_active_residual_bnb_state();
					return true;
				}
				return false;
			}

		private:
			DifferentialBestSearchContext& context;
			DifferentialSearchCursor& cursor;

			SearchWeight current_after_round_lower_bound( int round_boundary_depth ) const
			{
				if ( !context.configuration.enable_remaining_round_lower_bound )
					return 0;
				const int rounds_left_after = context.configuration.round_count - ( round_boundary_depth + 1 );
				if ( rounds_left_after < 0 )
					return 0;
				const auto& table = context.configuration.remaining_round_min_weight;
				const std::size_t idx = std::size_t( rounds_left_after );
				return ( idx < table.size() ) ? table[ idx ] : 0;
			}

			SearchWeight current_budget_after_prefix( SearchWeight prefix_weight, SearchWeight remaining_round_lb_after_this_round ) const
			{
				if ( context.best_total_weight >= INFINITE_WEIGHT )
					return INFINITE_WEIGHT;
				const SearchWeight occupied = saturating_add_search_weight( prefix_weight, remaining_round_lb_after_this_round );
				if ( occupied >= context.best_total_weight )
					return 0;
				return remaining_search_weight_budget( context.best_total_weight, occupied );
			}

			bool retighten_pending_frontier_entry_for_current_incumbent( DifferentialResidualFrontierEntry& entry )
			{
				if ( !entry.frame_snapshot )
					return false;

				auto& frame = *entry.frame_snapshot;
				auto& state = frame.state;
				state.remaining_round_weight_lower_bound_after_this_round =
					current_after_round_lower_bound( state.round_boundary_depth );

				switch ( frame.stage )
				{
				case DifferentialSearchStage::Enter:
					return state.accumulated_weight_so_far < context.best_total_weight;
				case DifferentialSearchStage::FirstConst:
				{
					if ( state.accumulated_weight_after_first_addition >= context.best_total_weight )
						return false;
					SearchWeight cap = current_budget_after_prefix(
						state.accumulated_weight_after_first_addition,
						state.remaining_round_weight_lower_bound_after_this_round );
					cap = std::min<SearchWeight>( cap, SearchWeight( 32 ) );
					cap = std::min<SearchWeight>( cap, context.configuration.constant_subtraction_weight_cap );
					frame.enum_first_const.reset(
						state.branch_a_input_difference,
						NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
						cap );
					return true;
				}
				case DifferentialSearchStage::InjB:
				{
					if ( state.accumulated_weight_before_second_addition >= context.best_total_weight )
						return false;
					// Explicit defense step: B ^= RC[4].
					const std::uint32_t branch_b_difference_after_explicit_prewhitening_before_injection =
						differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
							state.branch_b_difference_after_first_xor_with_rotated_branch_a );
					const InjectionAffineTransition transition =
						compute_injection_transition_from_branch_b(
							branch_b_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_b = transition.rank_weight;
					state.accumulated_weight_before_second_addition =
						state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;
					frame.enum_inj_b.reset( transition, context.configuration.maximum_transition_output_differences );
					return state.accumulated_weight_before_second_addition < context.best_total_weight;
				}
				case DifferentialSearchStage::SecondAdd:
				{
					if ( state.accumulated_weight_before_second_addition >= context.best_total_weight )
						return false;
					state.second_addition_term_difference =
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 31 ) ^
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 17 );
					const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
						find_optimal_gamma_with_weight(
							state.branch_a_difference_after_injection_from_branch_b,
							state.second_addition_term_difference,
							32 );
					if ( optimal_weight_second_addition >= INFINITE_WEIGHT )
						return false;
					state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
					state.optimal_weight_second_addition = optimal_weight_second_addition;
					SearchWeight cap = current_budget_after_prefix(
						state.accumulated_weight_before_second_addition,
						state.remaining_round_weight_lower_bound_after_this_round );
					cap = std::min<SearchWeight>( cap, SearchWeight( 31 ) );
					cap = std::min<SearchWeight>( cap, context.configuration.addition_weight_cap );
					state.weight_cap_second_addition = cap;
					if ( state.optimal_weight_second_addition > state.weight_cap_second_addition )
						return false;
					frame.enum_second_add.reset(
						state.branch_a_difference_after_injection_from_branch_b,
						state.second_addition_term_difference,
						state.optimal_output_branch_a_difference_after_second_addition,
						state.weight_cap_second_addition );
					return true;
				}
				case DifferentialSearchStage::SecondConst:
				{
					if ( state.accumulated_weight_after_second_addition >= context.best_total_weight )
						return false;
					SearchWeight cap = current_budget_after_prefix(
						state.accumulated_weight_after_second_addition,
						state.remaining_round_weight_lower_bound_after_this_round );
					cap = std::min<SearchWeight>( cap, SearchWeight( 32 ) );
					cap = std::min<SearchWeight>( cap, context.configuration.constant_subtraction_weight_cap );
					frame.enum_second_const.reset(
						state.branch_b_difference_after_first_bridge,
						NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
						cap );
					return true;
				}
				case DifferentialSearchStage::InjA:
				{
					if ( state.accumulated_weight_after_second_constant_subtraction >= context.best_total_weight )
						return false;
					differential_apply_second_subround_cross_xor_bridge(
						state.output_branch_a_difference_after_second_addition,
						state.output_branch_b_difference_after_second_constant_subtraction,
						state.branch_b_difference_after_second_xor_with_rotated_branch_a,
						state.branch_a_difference_after_second_xor_with_rotated_branch_b );
					// Explicit defense step: A ^= RC[9].
					const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
						differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
							state.branch_a_difference_after_second_xor_with_rotated_branch_b );
					const InjectionAffineTransition transition =
						compute_injection_transition_from_branch_a(
							branch_a_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_a = transition.rank_weight;
					state.accumulated_weight_at_round_end =
						state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;
					if ( saturating_add_search_weight(
							state.accumulated_weight_at_round_end,
							state.remaining_round_weight_lower_bound_after_this_round ) >= context.best_total_weight )
						return false;
					frame.enum_inj_a.reset( transition, context.configuration.maximum_transition_output_differences );
					return true;
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
					( context.configuration.round_count > 0 ) ? std::size_t( context.configuration.round_count ) : 0u,
					context.configuration.enable_state_memoization,
					"memoization.activate_residual" );
			}

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
	}

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
	class DifferentialBNB final
	{
	public:
		DifferentialBNB(
			DifferentialBestSearchContext& context_in,
			DifferentialSearchCursor& cursor_in,
			DifferentialEngineResidualFrontierHelper& helper_in )
			: search_context( context_in ), cursor( cursor_in ), helper( helper_in )
		{
		}

		void start_from_initial_frame(std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference)
		{
			cursor.stack.clear();
			search_context.current_differential_trail.clear();
			search_context.local_state_dominance.clear();
			search_context.pending_frontier.clear();
			search_context.pending_frontier_entries.clear();
			search_context.pending_frontier_index_by_key.clear();
			search_context.pending_frontier_entry_index_by_key.clear();
			DifferentialSearchFrame frame{};
			frame.stage = DifferentialSearchStage::Enter;
			frame.trail_size_at_entry = search_context.current_differential_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.branch_a_input_difference = branch_a_input_difference;
			frame.state.branch_b_input_difference = branch_b_input_difference;
			cursor.stack.push_back(frame);
		}

		void search_from_start(std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference)
		{
			start_from_initial_frame(branch_a_input_difference, branch_b_input_difference);
			run();
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		DifferentialBestSearchContext& search_context;
		DifferentialSearchCursor& cursor;
		DifferentialEngineResidualFrontierHelper& helper;

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_differential_boundary_record(
			std::int32_t rounds_remaining,
			DifferentialSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return helper.make_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_differential_boundary_record(
			const DifferentialBestSearchContext&,
			std::int32_t rounds_remaining,
			DifferentialSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_differential_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				best_prefix_weight );
		}

		bool try_register_differential_child_residual_candidate(
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const DifferentialSearchFrame* frame_snapshot = nullptr,
			const std::vector<DifferentialTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			return helper.try_register_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

		bool try_register_differential_child_residual_candidate(
			DifferentialBestSearchContext&,
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const DifferentialSearchFrame* frame_snapshot = nullptr,
			const std::vector<DifferentialTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			return try_register_differential_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

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
			if (cursor.stack.empty())
				return;
			const std::size_t target = cursor.stack.back().trail_size_at_entry;
			if (search_context.current_differential_trail.size() > target)
				search_context.current_differential_trail.resize(target);
			cursor.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if (!search_context.binary_checkpoint)
				return;
			if (best_search_shared_core::should_poll_binary_checkpoint(
				search_context.binary_checkpoint->pending_best_change(),
				search_context.binary_checkpoint->pending_runtime_request() ||
				TwilightDream::runtime_component::runtime_watchdog_checkpoint_request_pending(search_context.runtime_state),
				search_context.run_visited_node_count,
				search_context.progress_node_mask))
				search_context.binary_checkpoint->poll(search_context, cursor);
		}

		bool prepare_second_add_residual_snapshot(
			const DifferentialSearchFrame& frame,
			const DifferentialRoundSearchState& state,
			DifferentialSearchFrame& snapshot ) const
		{
			snapshot = frame;
			snapshot.stage = DifferentialSearchStage::SecondAdd;
			snapshot.state.second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 17 );

			const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
				find_optimal_gamma_with_weight(
					state.branch_a_difference_after_injection_from_branch_b,
					snapshot.state.second_addition_term_difference,
					32 );
			if ( optimal_weight_second_addition >= INFINITE_WEIGHT )
				return false;

			snapshot.state.optimal_output_branch_a_difference_after_second_addition =
				optimal_output_branch_a_difference_after_second_addition;
			snapshot.state.optimal_weight_second_addition = optimal_weight_second_addition;

			SearchWeight weight_cap_second_addition =
				search_context.best_total_weight -
				state.accumulated_weight_before_second_addition -
				state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, SearchWeight( 31 ) );
			weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, search_context.configuration.addition_weight_cap );
			snapshot.state.weight_cap_second_addition = weight_cap_second_addition;
			snapshot.enum_second_add.reset(
				state.branch_a_difference_after_injection_from_branch_b,
				snapshot.state.second_addition_term_difference,
				snapshot.state.optimal_output_branch_a_difference_after_second_addition,
				snapshot.state.weight_cap_second_addition );
			return true;
		}

		void run()
		{
			while (!cursor.stack.empty())
			{
				DifferentialSearchFrame& frame = current_frame();
				DifferentialRoundSearchState& state = frame.state;

				switch (frame.stage)
				{
				case DifferentialSearchStage::Enter:
				{
					if (should_stop_search(state.round_boundary_depth, state.accumulated_weight_so_far))
					{
						if (differential_runtime_budget_hit(search_context) ||
							(search_context.configuration.target_best_weight < INFINITE_WEIGHT &&
								search_context.best_total_weight <= search_context.configuration.target_best_weight))
							return;
						const int rounds_remaining = search_context.configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									rounds_remaining,
									DifferentialSearchStage::Enter,
									state.branch_a_input_difference,
									state.branch_b_input_difference,
									state.accumulated_weight_so_far),
								&frame,
								&search_context.current_differential_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}
					if (handle_round_end_if_needed(state.round_boundary_depth, state.accumulated_weight_so_far))
					{
						pop_frame();
						break;
					}
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::Enter,
						state.branch_a_input_difference,
						state.branch_b_input_difference,
						state.accumulated_weight_so_far))
					{
						pop_frame();
						break;
					}
					if (should_prune_state_memoization(state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far))
					{
						const int rounds_remaining = search_context.configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									rounds_remaining,
									DifferentialSearchStage::Enter,
									state.branch_a_input_difference,
									state.branch_b_input_difference,
									state.accumulated_weight_so_far),
								&frame,
								&search_context.current_differential_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}
					if (!prepare_round_state(state, state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far))
					{
						const int rounds_remaining = search_context.configuration.round_count - state.round_boundary_depth;
						if (rounds_remaining > 0)
						{
							(void)try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									rounds_remaining,
									DifferentialSearchStage::Enter,
									state.branch_a_input_difference,
									state.branch_b_input_difference,
									state.accumulated_weight_so_far),
								&frame,
								&search_context.current_differential_trail,
								state.accumulated_weight_so_far);
						}
						pop_frame();
						break;
					}

					frame.enum_first_add.reset(state.branch_b_input_difference, state.first_addition_term_difference, state.optimal_output_branch_b_difference_after_first_addition, state.weight_cap_first_addition);
					frame.stage = DifferentialSearchStage::FirstAdd;
					break;
				}
				case DifferentialSearchStage::FirstAdd:
				{
					std::uint32_t output_branch_b_difference_after_first_addition = 0;
					SearchWeight weight_first_addition = 0;
					if (!frame.enum_first_add.next(search_context, output_branch_b_difference_after_first_addition, weight_first_addition))
					{
						if (frame.enum_first_add.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_after_first_addition))
					{
						const std::uint32_t round_constant_for_first_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[1];
						const SearchWeight weight_cap_first_constant_subtraction =
							std::min<SearchWeight>(
								std::min<SearchWeight>(search_context.configuration.constant_subtraction_weight_cap, SearchWeight(32)),
								search_context.best_total_weight - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round);
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::FirstConst;
						snapshot.enum_first_const.reset(state.branch_a_input_difference, round_constant_for_first_subtraction, weight_cap_first_constant_subtraction);
						(void)try_register_differential_child_residual_candidate(
							search_context,
							make_differential_boundary_record(
								search_context,
								search_context.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::FirstConst,
								state.branch_a_input_difference,
								state.output_branch_b_difference_after_first_addition,
								state.accumulated_weight_after_first_addition),
							&snapshot,
							&search_context.current_differential_trail,
							state.accumulated_weight_after_first_addition);
						break;
					}

					const std::uint32_t round_constant_for_first_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[1];
					const SearchWeight weight_cap_first_constant_subtraction =
						std::min<SearchWeight>(std::min<SearchWeight>(search_context.configuration.constant_subtraction_weight_cap, SearchWeight(32)),
							search_context.best_total_weight - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round);
					frame.enum_first_const.reset(state.branch_a_input_difference, round_constant_for_first_subtraction, weight_cap_first_constant_subtraction);
					frame.stage = DifferentialSearchStage::FirstConst;
					break;
				}
				case DifferentialSearchStage::FirstConst:
				{
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::FirstConst,
						state.branch_a_input_difference,
						state.output_branch_b_difference_after_first_addition,
						state.accumulated_weight_after_first_addition))
					{
						frame.stage = DifferentialSearchStage::FirstAdd;
						break;
					}

					std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
					SearchWeight weight_first_constant_subtraction = 0;
					if (!frame.enum_first_const.next(search_context, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction))
					{
						if (frame.enum_first_const.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_after_first_constant_subtraction))
						break;

					differential_apply_first_subround_cross_xor_bridge(
						output_branch_a_difference_after_first_constant_subtraction,
						state.output_branch_b_difference_after_first_addition,
						state.branch_a_difference_after_first_xor_with_rotated_branch_b,
						state.branch_b_difference_after_first_xor_with_rotated_branch_a );
					state.branch_b_difference_after_first_bridge = state.branch_b_difference_after_first_xor_with_rotated_branch_a;
					const std::uint32_t branch_b_difference_after_explicit_prewhitening_before_injection =
						differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
							state.branch_b_difference_after_first_xor_with_rotated_branch_a );

					// Injection bridge B->A: after the explicit zero-cost CROSS_XOR_ROT bridge
					// and explicit zero-cost pre-whitening step B_pre = B_raw xor RC[4],
					// derive the exact affine output-difference space of the quadratic injector.
					const InjectionAffineTransition injection_transition_from_branch_b =
						compute_injection_transition_from_branch_b(
							branch_b_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
					state.accumulated_weight_before_second_addition = state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_before_second_addition))
					{
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::InjB;
						snapshot.enum_inj_b.reset(injection_transition_from_branch_b, search_context.configuration.maximum_transition_output_differences);
						(void)try_register_differential_child_residual_candidate(
							search_context,
							make_differential_boundary_record(
								search_context,
								search_context.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::InjB,
								state.branch_a_difference_after_first_xor_with_rotated_branch_b,
								state.branch_b_difference_after_first_bridge,
								state.accumulated_weight_before_second_addition),
							&snapshot,
							&search_context.current_differential_trail,
							state.accumulated_weight_before_second_addition);
						break;
					}

					frame.enum_inj_b.reset(injection_transition_from_branch_b, search_context.configuration.maximum_transition_output_differences);
					frame.stage = DifferentialSearchStage::InjB;
					break;
				}
				case DifferentialSearchStage::InjB:
				{
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::InjB,
						state.branch_a_difference_after_first_xor_with_rotated_branch_b,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_before_second_addition))
					{
						frame.stage = DifferentialSearchStage::FirstConst;
						break;
					}

					std::uint32_t injection_from_branch_b_xor_difference = 0;
					if (!frame.enum_inj_b.next(search_context, injection_from_branch_b_xor_difference))
					{
						if (frame.enum_inj_b.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_before_second_addition))
					{
						DifferentialSearchFrame snapshot {};
						if ( prepare_second_add_residual_snapshot( frame, state, snapshot ) )
						{
							( void )try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									search_context.configuration.round_count - state.round_boundary_depth,
									DifferentialSearchStage::SecondAdd,
									state.branch_a_difference_after_injection_from_branch_b,
									state.branch_b_difference_after_first_bridge,
									state.accumulated_weight_before_second_addition ),
								&snapshot,
								&search_context.current_differential_trail,
								state.accumulated_weight_before_second_addition );
						}
						break;
					}

					// Second ARX bridge of the round:
					//   A_after_injB + (rotl(B,31) xor rotl(B,17))
					// with the exact LM2001 differential operator used for the add step.
					state.second_addition_term_difference =
						NeoAlzetteCore::rotl<std::uint32_t>(state.branch_b_difference_after_first_bridge, 31) ^
						NeoAlzetteCore::rotl<std::uint32_t>(state.branch_b_difference_after_first_bridge, 17);

					const auto [optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition] =
						find_optimal_gamma_with_weight(state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, 32);
					state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
					state.optimal_weight_second_addition = optimal_weight_second_addition;
					if (state.optimal_weight_second_addition >= INFINITE_WEIGHT)
						break;
					if (saturating_add_search_weight(
						saturating_add_search_weight(state.accumulated_weight_before_second_addition, state.optimal_weight_second_addition),
						state.remaining_round_weight_lower_bound_after_this_round) >= search_context.best_total_weight)
					{
						DifferentialSearchFrame snapshot {};
						if ( prepare_second_add_residual_snapshot( frame, state, snapshot ) )
						{
							( void )try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									search_context.configuration.round_count - state.round_boundary_depth,
									DifferentialSearchStage::SecondAdd,
									state.branch_a_difference_after_injection_from_branch_b,
									state.branch_b_difference_after_first_bridge,
									state.accumulated_weight_before_second_addition ),
								&snapshot,
								&search_context.current_differential_trail,
								state.accumulated_weight_before_second_addition );
						}
						break;
					}

					SearchWeight weight_cap_second_addition = search_context.best_total_weight - state.accumulated_weight_before_second_addition - state.remaining_round_weight_lower_bound_after_this_round;
					weight_cap_second_addition = std::min<SearchWeight>(weight_cap_second_addition, SearchWeight(31));
					weight_cap_second_addition = std::min<SearchWeight>(weight_cap_second_addition, search_context.configuration.addition_weight_cap);
					state.weight_cap_second_addition = weight_cap_second_addition;
					if (state.optimal_weight_second_addition > weight_cap_second_addition)
					{
						DifferentialSearchFrame snapshot {};
						if ( prepare_second_add_residual_snapshot( frame, state, snapshot ) )
						{
							( void )try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									search_context.configuration.round_count - state.round_boundary_depth,
									DifferentialSearchStage::SecondAdd,
									state.branch_a_difference_after_injection_from_branch_b,
									state.branch_b_difference_after_first_bridge,
									state.accumulated_weight_before_second_addition ),
								&snapshot,
								&search_context.current_differential_trail,
								state.accumulated_weight_before_second_addition );
						}
						break;
					}

					frame.enum_second_add.reset(state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, state.optimal_output_branch_a_difference_after_second_addition, state.weight_cap_second_addition);
					frame.stage = DifferentialSearchStage::SecondAdd;
					break;
				}
				case DifferentialSearchStage::SecondAdd:
				{
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::SecondAdd,
						state.branch_a_difference_after_injection_from_branch_b,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_before_second_addition))
					{
						frame.stage = DifferentialSearchStage::InjB;
						break;
					}

					std::uint32_t output_branch_a_difference_after_second_addition = 0;
					SearchWeight weight_second_addition = 0;
					if (!frame.enum_second_add.next(search_context, output_branch_a_difference_after_second_addition, weight_second_addition))
					{
						if (frame.enum_second_add.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_after_second_addition))
					{
						const std::uint32_t round_constant_for_second_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[6];
						const SearchWeight weight_cap_second_constant_subtraction =
							std::min<SearchWeight>(
								std::min<SearchWeight>(search_context.configuration.constant_subtraction_weight_cap, SearchWeight(32)),
								search_context.best_total_weight - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round);
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::SecondConst;
						snapshot.enum_second_const.reset(state.branch_b_difference_after_first_bridge, round_constant_for_second_subtraction, weight_cap_second_constant_subtraction);
						(void)try_register_differential_child_residual_candidate(
							search_context,
							make_differential_boundary_record(
								search_context,
								search_context.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::SecondConst,
								state.output_branch_a_difference_after_second_addition,
								state.branch_b_difference_after_first_bridge,
								state.accumulated_weight_after_second_addition),
							&snapshot,
							&search_context.current_differential_trail,
							state.accumulated_weight_after_second_addition);
						break;
					}

					const std::uint32_t round_constant_for_second_subtraction = NeoAlzetteCore::ROUND_CONSTANTS[6];
					const SearchWeight weight_cap_second_constant_subtraction =
						std::min<SearchWeight>(std::min<SearchWeight>(search_context.configuration.constant_subtraction_weight_cap, SearchWeight(32)),
							search_context.best_total_weight - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round);
					frame.enum_second_const.reset(state.branch_b_difference_after_first_bridge, round_constant_for_second_subtraction, weight_cap_second_constant_subtraction);
					frame.stage = DifferentialSearchStage::SecondConst;
					break;
				}
				case DifferentialSearchStage::SecondConst:
				{
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::SecondConst,
						state.output_branch_a_difference_after_second_addition,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_after_second_addition))
					{
						frame.stage = DifferentialSearchStage::SecondAdd;
						break;
					}

					std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
					SearchWeight weight_second_constant_subtraction = 0;
					if (!frame.enum_second_const.next(search_context, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction))
					{
						if (frame.enum_second_const.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_after_second_constant_subtraction))
					{
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::InjA;
						std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a_snapshot = 0;
						std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b_snapshot = 0;
						differential_apply_second_subround_cross_xor_bridge(
							state.output_branch_a_difference_after_second_addition,
							state.output_branch_b_difference_after_second_constant_subtraction,
							branch_b_difference_after_second_xor_with_rotated_branch_a_snapshot,
							branch_a_difference_after_second_xor_with_rotated_branch_b_snapshot );
						const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection_snapshot =
							differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
								branch_a_difference_after_second_xor_with_rotated_branch_b_snapshot );
						const InjectionAffineTransition injection_transition_from_branch_a_snapshot =
							compute_injection_transition_from_branch_a(
								branch_a_difference_after_explicit_prewhitening_before_injection_snapshot );
						snapshot.enum_inj_a.reset(injection_transition_from_branch_a_snapshot, search_context.configuration.maximum_transition_output_differences);
						(void)try_register_differential_child_residual_candidate(
							search_context,
							make_differential_boundary_record(
								search_context,
								search_context.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::InjA,
								state.output_branch_a_difference_after_second_addition,
								state.output_branch_b_difference_after_second_constant_subtraction,
								state.accumulated_weight_after_second_constant_subtraction),
							&snapshot,
							&search_context.current_differential_trail,
							state.accumulated_weight_after_second_constant_subtraction);
						break;
					}

					differential_apply_second_subround_cross_xor_bridge(
						state.output_branch_a_difference_after_second_addition,
						output_branch_b_difference_after_second_constant_subtraction,
						state.branch_b_difference_after_second_xor_with_rotated_branch_a,
						state.branch_a_difference_after_second_xor_with_rotated_branch_b );
					const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
						differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
							state.branch_a_difference_after_second_xor_with_rotated_branch_b );

					// Injection bridge A->B: after the explicit zero-cost CROSS_XOR_ROT bridge
					// and explicit zero-cost pre-whitening step A_pre = A_raw xor RC[9],
					// derive the exact affine output-difference space of the second injector.
					const InjectionAffineTransition injection_transition_from_branch_a =
						compute_injection_transition_from_branch_a(
							branch_a_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
					state.accumulated_weight_at_round_end = state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_at_round_end))
					{
						const int rounds_remaining_after = search_context.configuration.round_count - (state.round_boundary_depth + 1);
						if (rounds_remaining_after > 0)
						{
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
							auto trail_snapshot = search_context.current_differential_trail;
							trail_snapshot.push_back(step);
							DifferentialSearchFrame child{};
							child.stage = DifferentialSearchStage::Enter;
							child.trail_size_at_entry = trail_snapshot.size() - 1u;
							child.state.round_boundary_depth = state.round_boundary_depth + 1;
							child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
							child.state.branch_a_input_difference = state.output_branch_a_difference;
							child.state.branch_b_input_difference = state.output_branch_b_difference;
							(void)try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									rounds_remaining_after,
									DifferentialSearchStage::Enter,
									state.output_branch_a_difference,
									state.output_branch_b_difference,
									state.accumulated_weight_at_round_end),
								&child,
								&trail_snapshot,
								state.accumulated_weight_at_round_end);
						}
						break;
					}

					frame.enum_inj_a.reset(injection_transition_from_branch_a, search_context.configuration.maximum_transition_output_differences);
					frame.stage = DifferentialSearchStage::InjA;
					break;
				}
				case DifferentialSearchStage::InjA:
				{
					if (should_prune_local_state_dominance(
						DifferentialSearchStage::InjA,
						state.output_branch_a_difference_after_second_addition,
						state.output_branch_b_difference_after_second_constant_subtraction,
						state.accumulated_weight_after_second_constant_subtraction))
					{
						frame.stage = DifferentialSearchStage::SecondConst;
						break;
					}

					std::uint32_t injection_from_branch_a_xor_difference = 0;
					if (!frame.enum_inj_a.next(search_context, injection_from_branch_a_xor_difference))
					{
						if (frame.enum_inj_a.stop_due_to_limits)
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

					if (should_prune_with_remaining_round_lower_bound(state, state.accumulated_weight_at_round_end))
					{
						const int rounds_remaining_after = search_context.configuration.round_count - (state.round_boundary_depth + 1);
						if (rounds_remaining_after > 0)
						{
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
							auto trail_snapshot = search_context.current_differential_trail;
							trail_snapshot.push_back(step);
							DifferentialSearchFrame child{};
							child.stage = DifferentialSearchStage::Enter;
							child.trail_size_at_entry = trail_snapshot.size() - 1u;
							child.state.round_boundary_depth = state.round_boundary_depth + 1;
							child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
							child.state.branch_a_input_difference = state.output_branch_a_difference;
							child.state.branch_b_input_difference = state.output_branch_b_difference;
							(void)try_register_differential_child_residual_candidate(
								search_context,
								make_differential_boundary_record(
									search_context,
									rounds_remaining_after,
									DifferentialSearchStage::Enter,
									state.output_branch_a_difference,
									state.output_branch_b_difference,
									state.accumulated_weight_at_round_end),
								&child,
								&trail_snapshot,
								state.accumulated_weight_at_round_end);
						}
						break;
					}

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

					search_context.current_differential_trail.push_back(step);

					DifferentialSearchFrame child{};
					child.stage = DifferentialSearchStage::Enter;
					// Store the parent trail size (exclude the step we just pushed) so pop_frame() removes it.
					child.trail_size_at_entry = search_context.current_differential_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
					child.state.branch_a_input_difference = state.output_branch_a_difference;
					child.state.branch_b_input_difference = state.output_branch_b_difference;
					cursor.stack.push_back(child);
					break;
				}
				}

				maybe_poll_checkpoint();
			}
		}

		// Stop conditions and global pruning (budget/time/best bound).
		bool should_stop_search(int round_boundary_depth, SearchWeight accumulated_weight_so_far)
		{
			// Early stop: reached target probability (weight) already.
			if (search_context.configuration.target_best_weight < INFINITE_WEIGHT && search_context.best_total_weight <= search_context.configuration.target_best_weight)
				return true;

			if (search_context.stop_due_to_time_limit)
				return true;

			// Count visited nodes for progress reporting even when maximum_search_nodes is unlimited (0).
			if (differential_note_runtime_node_visit(search_context))
				return true;

			if (differential_runtime_node_limit_hit(search_context))
				return true;

			maybe_print_single_run_progress(search_context, round_boundary_depth);
			maybe_poll_checkpoint();
			if (!search_context.best_differential_trail.empty() &&
				accumulated_weight_so_far >= search_context.best_total_weight)
				return true;

			if (should_prune_remaining_round_lower_bound(round_boundary_depth, accumulated_weight_so_far))
				return true;

			return false;
		}

		bool should_prune_remaining_round_lower_bound(int round_boundary_depth, SearchWeight accumulated_weight_so_far) const
		{
			if (search_context.best_total_weight < INFINITE_WEIGHT && search_context.configuration.enable_remaining_round_lower_bound)
			{
				const int rounds_left = search_context.configuration.round_count - round_boundary_depth;
				if (rounds_left >= 0)
				{
					const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
					const std::size_t table_index = std::size_t(rounds_left);
					if (table_index < remaining_round_min_weight_table.size())
					{
						const SearchWeight weight_lower_bound = remaining_round_min_weight_table[table_index];
						if (accumulated_weight_so_far + weight_lower_bound >= search_context.best_total_weight)
							return true;
					}
				}
			}
			return false;
		}

		bool handle_round_end_if_needed(int round_boundary_depth, SearchWeight accumulated_weight_so_far)
		{
			if (round_boundary_depth != search_context.configuration.round_count)
				return false;

			if (accumulated_weight_so_far < search_context.best_total_weight || search_context.best_differential_trail.empty())
			{
				const SearchWeight old = search_context.best_total_weight;
				search_context.best_total_weight = accumulated_weight_so_far;
				search_context.best_differential_trail = search_context.current_differential_trail;
				if (search_context.checkpoint && accumulated_weight_so_far != old)
					search_context.checkpoint->maybe_write(search_context, "improved");
				if (search_context.binary_checkpoint && accumulated_weight_so_far != old)
					search_context.binary_checkpoint->mark_best_changed();
			}
			return true;
		}

		bool should_prune_state_memoization(int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, SearchWeight accumulated_weight_so_far)
		{
			if (!search_context.configuration.enable_state_memoization)
				return false;

			const std::size_t hint = differential_runtime_memo_reserve_hint(search_context);

			const std::uint64_t key = pack_two_word32_differences(branch_a_input_difference, branch_b_input_difference);
			return search_context.memoization.should_prune_and_update(std::size_t(round_boundary_depth), key, accumulated_weight_so_far, true, true, hint, 192ull, "memoization.reserve", "memoization.try_emplace");
		}

		bool should_prune_local_state_dominance(
			DifferentialSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight prefix_weight )
		{
			return search_context.local_state_dominance.should_prune_or_update(
				static_cast<std::uint8_t>( stage_cursor ),
				pair_a,
				pair_b,
				prefix_weight );
		}

		SearchWeight compute_remaining_round_weight_lower_bound_after_this_round(int round_boundary_depth) const
		{
			if (!search_context.configuration.enable_remaining_round_lower_bound)
				return 0;
			const int rounds_left_after = search_context.configuration.round_count - (round_boundary_depth + 1);
			if (rounds_left_after < 0)
				return 0;
			const auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			const std::size_t idx = std::size_t(rounds_left_after);
			if (idx >= remaining_round_min_weight_table.size())
				return 0;
			return remaining_round_min_weight_table[idx];
		}

		bool should_prune_with_remaining_round_lower_bound(const DifferentialRoundSearchState& state, SearchWeight accumulated_weight) const
		{
			return accumulated_weight + state.remaining_round_weight_lower_bound_after_this_round >= search_context.best_total_weight;
		}

		bool prepare_round_state(DifferentialRoundSearchState& state, int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, SearchWeight accumulated_weight_so_far)
		{
			state.round_boundary_depth = round_boundary_depth;
			state.accumulated_weight_so_far = accumulated_weight_so_far;
			state.branch_a_input_difference = branch_a_input_difference;
			state.branch_b_input_difference = branch_b_input_difference;
			state.remaining_round_weight_lower_bound_after_this_round = compute_remaining_round_weight_lower_bound_after_this_round(round_boundary_depth);

			state.base_step = DifferentialTrailStepRecord{};
			state.base_step.round_index = round_boundary_depth + 1;
			state.base_step.input_branch_a_difference = branch_a_input_difference;
			state.base_step.input_branch_b_difference = branch_b_input_difference;

			// First ARX bridge of the round:
			//   B + (rotl(A,31) xor rotl(A,17))
			// The exact LM2001 operator first gives the best possible output-difference weight,
			// then `ModularAdditionEnumerator` enumerates the full shell under that cap.
			state.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>(branch_a_input_difference, 31) ^ NeoAlzetteCore::rotl<std::uint32_t>(branch_a_input_difference, 17);
			state.base_step.first_addition_term_difference = state.first_addition_term_difference;

			const auto [optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition] =
				find_optimal_gamma_with_weight(branch_b_input_difference, state.first_addition_term_difference, 32);
			state.optimal_output_branch_b_difference_after_first_addition = optimal_output_branch_b_difference_after_first_addition;
			state.optimal_weight_first_addition = optimal_weight_first_addition;
			if (state.optimal_weight_first_addition >= INFINITE_WEIGHT)
				return false;

			SearchWeight weight_cap_first_addition = search_context.best_total_weight - accumulated_weight_so_far - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_first_addition = std::min<SearchWeight>(weight_cap_first_addition, SearchWeight(31));
			weight_cap_first_addition = std::min<SearchWeight>(weight_cap_first_addition, search_context.configuration.addition_weight_cap);
			state.weight_cap_first_addition = weight_cap_first_addition;
			if (state.optimal_weight_first_addition > weight_cap_first_addition)
				return false;

			return true;
		}
	};

	class DifferentialBNBScheduler final
	{
	public:
		DifferentialBNBScheduler( DifferentialBestSearchContext& context_in, DifferentialSearchCursor& cursor_in )
			: context( context_in ), cursor( cursor_in ), helper( context_in, cursor_in )
		{
		}

		void run_from_cursor()
		{
			helper.rebuild_pending_frontier_indexes();
			if ( !context.active_problem_valid && !cursor.stack.empty() )
				helper.set_active_problem( helper.make_root_source_record(), true );
			while ( true )
			{
				DifferentialBNB bnb( context, cursor, helper );
				bnb.search_from_cursor();
				if ( !cursor.stack.empty() )
					return;
				helper.complete_active_problem();
				if ( !helper.restore_next_pending_frontier_entry() )
					return;
			}
		}

		void interrupt_root_if_needed( bool hit_node_limit, bool hit_time_limit )
		{
			helper.interrupt_root_if_needed( hit_node_limit, hit_time_limit );
		}

	private:
		DifferentialBestSearchContext& context;
		DifferentialSearchCursor& cursor;
		DifferentialEngineResidualFrontierHelper helper;
	};

	void continue_differential_best_search_from_cursor(DifferentialBestSearchContext& search_context, DifferentialSearchCursor& cursor)
	{
		DifferentialBNBScheduler( search_context, cursor ).run_from_cursor();
	}

	MatsuiSearchRunDifferentialResult run_differential_best_search(
		int round_count,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		const DifferentialBestSearchConfiguration& input_search_configuration,
		const DifferentialBestSearchRuntimeControls& runtime_controls,
		bool print_output,
		bool progress_print_differences,
		SearchWeight seeded_upper_bound_weight,
		const std::vector<DifferentialTrailStepRecord>* seeded_upper_bound_trail,
		BestWeightHistory* checkpoint,
		BinaryCheckpointManager* binary_checkpoint,
		RuntimeEventLog* runtime_event_log,
		const SearchInvocationMetadata* invocation_metadata)
	{
		MatsuiSearchRunDifferentialResult result{};
		DifferentialBestSearchContext search_context{};
		search_context.configuration = input_search_configuration;
		search_context.runtime_controls = runtime_controls;
		search_context.configuration.round_count = round_count;
		search_context.configuration.addition_weight_cap = std::min<SearchWeight>(search_context.configuration.addition_weight_cap, SearchWeight(31));
		search_context.configuration.constant_subtraction_weight_cap = std::min<SearchWeight>(search_context.configuration.constant_subtraction_weight_cap, SearchWeight(32));
		configure_weight_sliced_pddt_cache_for_run(
			search_context.configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes());
		search_context.start_difference_a = initial_branch_a_difference;
		search_context.start_difference_b = initial_branch_b_difference;
		search_context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata{};
		search_context.checkpoint = checkpoint;
		search_context.runtime_event_log = runtime_event_log;
		search_context.binary_checkpoint = binary_checkpoint;
		begin_differential_runtime_invocation(search_context);
		best_search_shared_core::prepare_binary_checkpoint(
			search_context.binary_checkpoint,
			search_context.runtime_controls.checkpoint_every_seconds,
			false);
		best_search_shared_core::SearchControlSession<DifferentialBestSearchContext> control_session(search_context);
		control_session.begin();
		differential_runtime_log_basic_event(search_context, "best_search_start");
		search_context.memoization.initialize((round_count > 0) ? std::size_t(round_count) : 0u, search_context.configuration.enable_state_memoization, "memoization.init");

		// Normalize Matsui-style remaining-round lower bound table (weight domain).
		// Missing entries are treated as 0 (safe but weaker).
		if (search_context.configuration.enable_remaining_round_lower_bound)
		{
			auto& remaining_round_min_weight_table = search_context.configuration.remaining_round_min_weight;
			if (remaining_round_min_weight_table.empty())
			{
				remaining_round_min_weight_table.assign(std::size_t(std::max(0, round_count)) + 1u, 0);
			}
			else
			{
				// Ensure remaining_round_min_weight_table[0] exists and is 0.
				if (remaining_round_min_weight_table.size() < 1u)
					remaining_round_min_weight_table.resize(1u, 0);
				remaining_round_min_weight_table[0] = 0;
				// Pad to round_count+1 with 0 (safe lower bound).
				const std::size_t need = std::size_t(std::max(0, round_count)) + 1u;
				if (remaining_round_min_weight_table.size() < need)
					remaining_round_min_weight_table.resize(need, 0);
				for (SearchWeight& round_min_weight : remaining_round_min_weight_table)
				{
					if (round_min_weight >= INFINITE_WEIGHT)
						round_min_weight = 0;
				}
			}
		}
		// initial upper bound (greedy)
		search_context.best_total_weight = compute_greedy_upper_bound_weight(search_context.configuration, initial_branch_a_difference, initial_branch_b_difference);
		if (search_context.best_total_weight >= INFINITE_WEIGHT)
			search_context.best_total_weight = INFINITE_WEIGHT;

		// Seed best_trail with an explicit greedy construction to avoid false [FAIL] when DFS hits max_nodes early.
		{
			SearchWeight initial_weight = INFINITE_WEIGHT;
			auto gtrail = construct_greedy_initial_differential_trail(search_context.configuration, initial_branch_a_difference, initial_branch_b_difference, initial_weight);
			if (!gtrail.empty() && initial_weight < INFINITE_WEIGHT)
			{
				search_context.best_total_weight = initial_weight;
				search_context.best_differential_trail = std::move(gtrail);
			}
		}

		// Optional: seed a tighter upper bound from a previous run (e.g., auto breadth -> deep).
		if (seeded_upper_bound_weight < INFINITE_WEIGHT && seeded_upper_bound_weight < search_context.best_total_weight)
		{
			search_context.best_total_weight = seeded_upper_bound_weight;
			if (seeded_upper_bound_trail && !seeded_upper_bound_trail->empty())
			{
				search_context.best_differential_trail = *seeded_upper_bound_trail;
			}
		}

		// Persistence (auto mode): record the initial best (greedy/seeded) once.
		if (search_context.checkpoint)
		{
			search_context.checkpoint->maybe_write(search_context, "init");
		}

		if (print_output)
		{
			std::cout << "[BestSearch] mode=matsui(injection-affine)\n";
			std::cout << "  rounds=" << round_count << "  addition_weight_cap=" << search_context.configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_context.configuration.constant_subtraction_weight_cap << "  maximum_transition_output_differences=" << search_context.configuration.maximum_transition_output_differences << "  runtime_maximum_search_nodes=" << search_context.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "  memo=" << (search_context.configuration.enable_state_memoization ? "on" : "off") << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name(TwilightDream::runtime_component::runtime_time_limit_scope())
				<< "  startup_memory_gate_policy=" << (search_context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject") << "\n";
			std::cout << "  weight_sliced_pddt=" << (search_context.configuration.enable_weight_sliced_pddt ? "on" : "off")
				<< "  weight_sliced_pddt_max_weight=" << search_context.configuration.weight_sliced_pddt_max_weight << "\n";
			std::cout << "  greedy_upper_bound_weight=" << (search_context.best_total_weight >= INFINITE_WEIGHT ? -1 : search_context.best_total_weight) << "\n";
			if (seeded_upper_bound_weight < INFINITE_WEIGHT)
			{
				std::cout << "  seeded_upper_bound_weight=" << seeded_upper_bound_weight << "\n";
			}
			std::cout << "\n";
		}

		// Enable single-run progress printing if requested.
		if (best_search_shared_core::initialize_progress_tracking(search_context, search_context.runtime_controls.progress_every_seconds))
		{
			search_context.progress_print_differences = progress_print_differences;
			if (print_output)
			{
				std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
				TwilightDream::runtime_component::print_progress_prefix(std::cout);
				std::cout << "[Progress] enabled: every " << search_context.progress_every_seconds << " seconds (time-check granularity ~" << (search_context.progress_node_mask + 1) << " nodes)\n\n";
			}
		}

		DifferentialSearchCursor cursor{};
		cursor.stack.clear();
		search_context.current_differential_trail.clear();
		DifferentialSearchFrame root_frame {};
		root_frame.stage = DifferentialSearchStage::Enter;
		root_frame.trail_size_at_entry = search_context.current_differential_trail.size();
		root_frame.state.round_boundary_depth = 0;
		root_frame.state.accumulated_weight_so_far = 0;
		root_frame.state.branch_a_input_difference = initial_branch_a_difference;
		root_frame.state.branch_b_input_difference = initial_branch_b_difference;
		cursor.stack.push_back( root_frame );
		continue_differential_best_search_from_cursor(search_context, cursor);
		control_session.finalize(
			search_context.binary_checkpoint,
			cursor.stack.empty(),
			differential_runtime_budget_hit(search_context),
			[&](const char* reason)
			{
				return search_context.binary_checkpoint->write_now(search_context, cursor, reason);
			},
			[&](const char* reason)
			{
				differential_runtime_log_basic_event(search_context, "checkpoint_preserved", reason);
			});
		if (runtime_maximum_search_nodes_hit(search_context.runtime_controls, search_context.runtime_state))
			differential_runtime_log_basic_event(search_context, "best_search_stop", "hit_maximum_search_nodes");
		else if (runtime_time_limit_hit(search_context.runtime_controls, search_context.runtime_state))
			differential_runtime_log_basic_event(search_context, "best_search_stop", "hit_time_limit");
		else
			differential_runtime_log_basic_event(search_context, "best_search_stop", "completed");

		const bool hit_node_limit = runtime_maximum_search_nodes_hit(search_context.runtime_controls, search_context.runtime_state);
		const bool hit_time_limit = runtime_time_limit_hit(search_context.runtime_controls, search_context.runtime_state);
		DifferentialBNBScheduler( search_context, cursor ).interrupt_root_if_needed( hit_node_limit, hit_time_limit );
		if ( ( hit_node_limit || hit_time_limit ) && search_context.binary_checkpoint )
			( void )search_context.binary_checkpoint->write_now( search_context, cursor, "runtime_limit_snapshot" );

		result.nodes_visited = search_context.visited_node_count;
		result.hit_maximum_search_nodes = hit_node_limit;
		result.hit_time_limit = hit_time_limit;
		result.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap(search_context.configuration);
		result.used_target_best_weight_shortcut =
			search_context.configuration.target_best_weight < INFINITE_WEIGHT &&
			search_context.best_total_weight <= search_context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;

		if (search_context.best_differential_trail.empty())
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_differential_best_search_strict_rejection_reason(
					result,
					search_context.configuration);
			result.best_weight_certified = false;
			if (print_output)
			{
				if (result.hit_maximum_search_nodes || result.hit_time_limit)
					std::cout << "[PAUSE] No trail found yet before the runtime budget expired; checkpoint/resume can continue.\n";
				else
					std::cout << "[FAIL] No trail found within limits.\n";
			}
			return result;
		}

		result.found = true;
		result.best_weight = search_context.best_total_weight;
		result.best_trail = std::move(search_context.best_differential_trail);
		result.strict_rejection_reason =
			classify_differential_best_search_strict_rejection_reason(
				result,
				search_context.configuration);
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if (print_output)
		{
			std::cout << "[OK] best_weight=" << result.best_weight << "  (DP ~= 2^-" << result.best_weight << ")\n";
			std::cout << "  approx_DP=" << std::setprecision(10) << weight_to_probability(result.best_weight) << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited << (result.hit_maximum_search_nodes ? "  [HIT maximum_search_nodes]" : "");
			if (result.hit_time_limit)
			{
				std::cout << "  [HIT maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "]";
			}
			if (search_context.configuration.target_best_weight < INFINITE_WEIGHT && result.best_weight <= search_context.configuration.target_best_weight)
			{
				std::cout << "  [HIT target_best_weight=" << search_context.configuration.target_best_weight << "]";
			}
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string(best_weight_certification_status(result)) << "\n";
			std::cout << "  exact_best_weight_certified=" << (result.best_weight_certified ? 1 : 0) << "\n\n";

			for (const auto& s : result.best_trail)
			{
				std::cout << "R" << s.round_index << "  round_weight=" << s.round_weight << "  weight_first_addition=" << s.weight_first_addition << "  weight_first_constant_subtraction=" << s.weight_first_constant_subtraction << "  weight_injection_from_branch_b=" << s.weight_injection_from_branch_b << "  weight_second_addition=" << s.weight_second_addition << "  weight_second_constant_subtraction=" << s.weight_second_constant_subtraction << "  weight_injection_from_branch_a=" << s.weight_injection_from_branch_a << "\n";
				print_word32_hex("  input_branch_a_difference=", s.input_branch_a_difference);
				std::cout << "  ";
				print_word32_hex("input_branch_b_difference=", s.input_branch_b_difference);
				std::cout << "\n";

				print_word32_hex("  output_branch_b_difference_after_first_addition=", s.output_branch_b_difference_after_first_addition);
				std::cout << "  ";
				print_word32_hex("first_addition_term_difference=", s.first_addition_term_difference);
				std::cout << "\n";

				print_word32_hex("  output_branch_a_difference_after_first_constant_subtraction=", s.output_branch_a_difference_after_first_constant_subtraction);
				std::cout << "  ";
				print_word32_hex("branch_a_difference_after_first_xor_with_rotated_branch_b=", s.branch_a_difference_after_first_xor_with_rotated_branch_b);
				std::cout << "\n";

				print_word32_hex("  injection_from_branch_b_xor_difference=", s.injection_from_branch_b_xor_difference);
				std::cout << "  ";
				print_word32_hex("branch_a_difference_after_injection_from_branch_b=", s.branch_a_difference_after_injection_from_branch_b);
				std::cout << "\n";

				print_word32_hex("  branch_b_difference_after_first_bridge=", s.branch_b_difference_after_first_bridge);
				std::cout << "  ";
				print_word32_hex("second_addition_term_difference=", s.second_addition_term_difference);
				std::cout << "\n";

				print_word32_hex("  output_branch_b_difference_after_second_constant_subtraction=", s.output_branch_b_difference_after_second_constant_subtraction);
				std::cout << "  ";
				print_word32_hex("branch_b_difference_after_second_xor_with_rotated_branch_a=", s.branch_b_difference_after_second_xor_with_rotated_branch_a);
				std::cout << "\n";

				print_word32_hex("  injection_from_branch_a_xor_difference=", s.injection_from_branch_a_xor_difference);
				std::cout << "  ";
				print_word32_hex("output_branch_b_difference=", s.output_branch_b_difference);
				std::cout << "\n";

				print_word32_hex("  output_branch_a_difference=", s.output_branch_a_difference);
				std::cout << "  ";
				print_word32_hex("output_branch_b_difference=", s.output_branch_b_difference);
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
		const TwilightDream::best_search_shared_core::ResumeProgressReportingOptions* progress_reporting_opt)
	{
		MatsuiSearchRunDifferentialResult result{};
		if (checkpoint_path.empty())
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}

		DifferentialBestSearchConfiguration resolved_expected_configuration = expected_configuration;
		configure_weight_sliced_pddt_cache_for_run(
			resolved_expected_configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes());

		DifferentialBestSearchContext search_context{};
		DifferentialCheckpointLoadResult load{};
		if (!read_differential_checkpoint(checkpoint_path, load, search_context.memoization))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		if (load.start_difference_a != expected_start_difference_a || load.start_difference_b != expected_start_difference_b)
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}
		if (!differential_configs_compatible_for_resume(resolved_expected_configuration, load.configuration))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}

		DifferentialBestSearchConfiguration exec_configuration =
			execution_configuration_override ? *execution_configuration_override : load.configuration;
		configure_weight_sliced_pddt_cache_for_run(
			exec_configuration,
			TwilightDream::runtime_component::rebuildable_pool().budget_bytes());

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
		const DifferentialResumeFingerprint loaded_fingerprint = compute_differential_resume_fingerprint(load);

		search_context.configuration = std::move(exec_configuration);
		TwilightDream::best_search_shared_core::apply_resume_runtime_plan(search_context, resume_runtime_plan);
		search_context.start_difference_a = load.start_difference_a;
		search_context.start_difference_b = load.start_difference_b;
		search_context.history_log_output_path = load.history_log_path;
		search_context.runtime_log_output_path = load.runtime_log_path;
		search_context.best_total_weight = load.best_total_weight;
		search_context.best_differential_trail = std::move(load.best_trail);
		search_context.current_differential_trail = std::move(load.current_trail);
		search_context.pending_frontier = std::move(load.pending_frontier);
		search_context.pending_frontier_entries = std::move(load.pending_frontier_entries);
		search_context.completed_source_input_pairs = std::move(load.completed_source_input_pairs);
		search_context.completed_output_as_next_input_pairs = std::move(load.completed_output_as_next_input_pairs);
		search_context.completed_residual_set = std::move(load.completed_residual_set);
		search_context.best_prefix_by_residual_key = std::move(load.best_prefix_by_residual_key);
		search_context.global_residual_result_table = std::move(load.global_residual_result_table);
		search_context.residual_counters = load.residual_counters;
		search_context.active_problem_valid = load.active_problem_valid;
		search_context.active_problem_is_root = load.active_problem_is_root;
		search_context.active_problem_record = load.active_problem_record;
		search_context.checkpoint = checkpoint;
		search_context.runtime_event_log = runtime_event_log;
		search_context.binary_checkpoint = binary_checkpoint;
		search_context.invocation_metadata = invocation_metadata ? *invocation_metadata : SearchInvocationMetadata{};
		DifferentialSearchCursor cursor = std::move(load.cursor);
		// The binary checkpoint already restored trail/cursor/enumerator positions.
		// This step only reconstructs accelerator state that is declared rebuildable,
		// so resume continues from the stored DFS node rather than restarting the round.
		if (!materialize_differential_resume_rebuildable_state(search_context, cursor))
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
			return result;
		}
		const DifferentialResumeFingerprint materialized_fingerprint = compute_differential_resume_fingerprint(search_context, cursor);
		if (materialized_fingerprint.hash != loaded_fingerprint.hash)
		{
			result.strict_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
			return result;
		}

		if (best_search_shared_core::initialize_progress_tracking(
			search_context,
			best_search_shared_core::effective_resume_progress_interval_seconds(search_context, progress_reporting_opt)))
		{
			search_context.progress_print_differences = progress_print_differences;
			if (print_output)
			{
				std::scoped_lock lk(TwilightDream::runtime_component::cout_mutex());
				TwilightDream::runtime_component::print_progress_prefix(std::cout);
				std::cout << "[Progress] enabled: every " << search_context.progress_every_seconds << " seconds (time-check granularity ~" << (search_context.progress_node_mask + 1) << " nodes)\n\n";
			}
		}

		best_search_shared_core::run_resume_control_session(
			search_context,
			cursor,
			[&](DifferentialBestSearchContext& ctx) {
				best_search_shared_core::prepare_binary_checkpoint(
					ctx.binary_checkpoint,
					ctx.runtime_controls.checkpoint_every_seconds,
					true,
					checkpoint_path);
			},
			[](DifferentialBestSearchContext& ctx) {
				begin_differential_runtime_invocation(ctx);
			},
			[](DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor) {
				differential_runtime_log_resume_event(ctx, resume_cursor, "resume_start");
			},
			[](DifferentialBestSearchContext& ctx, DifferentialSearchCursor&) {
				if (ctx.checkpoint && ctx.best_total_weight < INFINITE_WEIGHT && !ctx.best_differential_trail.empty())
					ctx.checkpoint->maybe_write(ctx, "resume_init");
			},
			[](DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor) {
				if (ctx.checkpoint)
					write_differential_resume_snapshot(*ctx.checkpoint, ctx, resume_cursor, "resume_init");
			},
			[](DifferentialBestSearchContext& ctx, DifferentialSearchCursor& resume_cursor) {
				continue_differential_best_search_from_cursor(ctx, resume_cursor);
			},
			[](DifferentialBestSearchContext& ctx) {
				return differential_runtime_budget_hit(ctx);
			},
			[](DifferentialBestSearchContext& ctx, const char* reason) {
				differential_runtime_log_basic_event(ctx, "checkpoint_preserved", reason);
			},
			[](DifferentialBestSearchContext& ctx) {
				if (runtime_maximum_search_nodes_hit(ctx.runtime_controls, ctx.runtime_state))
					differential_runtime_log_basic_event(ctx, "resume_stop", "hit_maximum_search_nodes");
				else if (runtime_time_limit_hit(ctx.runtime_controls, ctx.runtime_state))
					differential_runtime_log_basic_event(ctx, "resume_stop", "hit_time_limit");
				else
					differential_runtime_log_basic_event(ctx, "resume_stop", "completed");
			});

		const bool resume_hit_node_limit =
			runtime_maximum_search_nodes_hit( search_context.runtime_controls, search_context.runtime_state );
		const bool resume_hit_time_limit =
			runtime_time_limit_hit( search_context.runtime_controls, search_context.runtime_state );
		DifferentialBNBScheduler( search_context, cursor ).interrupt_root_if_needed(
			resume_hit_node_limit,
			resume_hit_time_limit );
		if ( ( resume_hit_node_limit || resume_hit_time_limit ) && search_context.binary_checkpoint )
			( void )search_context.binary_checkpoint->write_now( search_context, cursor, "runtime_limit_snapshot" );

		result.nodes_visited = search_context.visited_node_count;
		result.hit_maximum_search_nodes = resume_hit_node_limit;
		result.hit_time_limit = resume_hit_time_limit;
		result.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap(search_context.configuration);
		result.used_target_best_weight_shortcut =
			search_context.configuration.target_best_weight < INFINITE_WEIGHT &&
			search_context.best_total_weight <= search_context.configuration.target_best_weight;
		result.exhaustive_completed =
			!result.hit_maximum_search_nodes &&
			!result.hit_time_limit &&
			!result.used_target_best_weight_shortcut;

		if (search_context.best_differential_trail.empty())
		{
			result.found = false;
			result.best_weight = INFINITE_WEIGHT;
			result.strict_rejection_reason =
				classify_differential_best_search_strict_rejection_reason(
					result,
					search_context.configuration);
			return result;
		}

		result.found = true;
		result.best_weight = search_context.best_total_weight;
		result.best_trail = std::move(search_context.best_differential_trail);
		result.strict_rejection_reason =
			classify_differential_best_search_strict_rejection_reason(
				result,
				search_context.configuration);
		result.best_weight_certified =
			result.strict_rejection_reason == StrictCertificationFailureReason::None &&
			result.exhaustive_completed &&
			result.found &&
			result.best_weight < INFINITE_WEIGHT;

		if (print_output)
		{
			std::cout << "[BestSearch][Resume] checkpoint_path=" << checkpoint_path << "\n";
			std::cout << "  runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name(TwilightDream::runtime_component::runtime_time_limit_scope())
				<< "  startup_memory_gate_policy=" << (search_context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject") << "\n";
			std::cout << "[OK] best_weight=" << result.best_weight << "\n";
			std::cout << "  nodes_visited=" << result.nodes_visited;
			if (result.hit_maximum_search_nodes)
				std::cout << "  [HIT maximum_search_nodes]";
			if (result.hit_time_limit)
				std::cout << "  [HIT maximum_search_seconds=" << search_context.runtime_controls.maximum_search_seconds << "]";
			if (result.used_target_best_weight_shortcut)
				std::cout << "  [HIT target_best_weight=" << search_context.configuration.target_best_weight << "]";
			std::cout << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string(best_weight_certification_status(result)) << "\n";
			std::cout << "  exact_best_weight_certified=" << (result.best_weight_certified ? 1 : 0) << "\n";
		}

		return result;
	}

}  // namespace TwilightDream::auto_search_differential
