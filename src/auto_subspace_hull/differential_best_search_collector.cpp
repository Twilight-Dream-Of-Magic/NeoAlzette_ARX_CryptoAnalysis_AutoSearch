#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/remaining_round_lower_bound_bootstrap.hpp"

#include <new>

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
			// XOR-difference is invariant under xor-by-constant.
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
			return branch_a_difference_before_xor ^
				NeoAlzetteCore::rotl<std::uint32_t>(
					branch_b_difference_current,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		}

		inline std::uint32_t differential_apply_cross_xor_rot_r1_on_branch_b(
			std::uint32_t branch_b_difference_before_xor,
			std::uint32_t branch_a_difference_current ) noexcept
		{
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

		struct DifferentialCollectorResidualFrontierHelper final
		{
			DifferentialCollectorResidualFrontierHelper(
				DifferentialBestSearchContext& context_in,
				DifferentialSearchCursor& cursor_in )
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
						TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Differential,
						TwilightDream::residual_frontier_shared::ResidualObjectiveKind::HullCollect,
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
					std::cout << "[Residual][DifferentialCollector] event=" << residual_pair_event_kind_to_string( kind )
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
						out << "domain=differential\n";
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
				SearchWeight collect_weight_cap,
				const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
				bool solved,
				bool exact_within_collect_weight_cap )
			{
				for ( auto& existing : context.global_residual_result_table )
				{
					if ( existing.key == record.key )
					{
						existing.best_weight = context.best_total_weight;
						existing.collect_weight_cap = collect_weight_cap;
						existing.solved = solved;
						existing.exact_within_collect_weight_cap = exact_within_collect_weight_cap;
						return;
					}
				}

				TwilightDream::residual_frontier_shared::ResidualResultRecord result {};
				result.key = record.key;
				result.best_weight = context.best_total_weight;
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
				upsert_residual_result( collect_weight_cap, record, true, exact_within_collect_weight_cap );
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
				auto record =
					TwilightDream::residual_frontier_shared::make_residual_problem_record(
						TwilightDream::residual_frontier_shared::ResidualAnalysisDomain::Differential,
						TwilightDream::residual_frontier_shared::ResidualObjectiveKind::HullCollect,
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
				std::cout << "[Residual][DifferentialCollector] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::InterruptedOutputAsNextInputPair )
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
					std::cout << "[Residual][DifferentialCollector] event=" << residual_pair_event_kind_to_string( ResidualPairEventKind::CompletedOutputAsNextInputPair )
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

					erase_pending_frontier_entry_at( index );
					erase_pending_frontier_record_by_key( entry.record.key );

					context.current_differential_trail = std::move( entry.current_trail_snapshot );
					cursor.stack.clear();
					cursor.stack.push_back( *entry.frame_snapshot );
					set_active_problem( entry.record, false );
					return true;
				}
				return false;
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

		private:
			DifferentialBestSearchContext& context;
			DifferentialSearchCursor& cursor;

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
	}  // namespace

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
			  collect_weight_cap_( options.collect_weight_cap ),
			  maximum_collected_trails_( options.maximum_collected_trails )
		{
			context_.configuration = base_configuration;
			context_.runtime_controls = options.runtime_controls;
			context_.start_difference_a = start_difference_a;
			context_.start_difference_b = start_difference_b;
			prepare_differential_remaining_round_lower_bound_table(
				context_.configuration,
				context_.configuration.round_count );
			context_.configuration.enable_state_memoization = false;
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
		SearchWeight collect_weight_cap_ = 0;
		std::uint64_t					 maximum_collected_trails_ = 0;

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

		bool should_stop_search( int depth, SearchWeight accumulated_weight )
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

		void collect_current_trail( SearchWeight total_weight, std::uint32_t output_branch_a_difference, std::uint32_t output_branch_b_difference, long double exact_probability )
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
			SearchWeight accumulated_weight_so_far,
			long double accumulated_exact_probability )
		{
			if ( should_stop_search( round_boundary_depth, accumulated_weight_so_far ) )
				return;

			if ( round_boundary_depth == context_.configuration.round_count )
			{
				collect_current_trail( accumulated_weight_so_far, branch_a_input_difference, branch_b_input_difference, accumulated_exact_probability );
				return;
			}

			const SearchWeight remaining_round_lb_after_this_round = remaining_round_lower_bound_after_this_round( round_boundary_depth );
			if ( accumulated_weight_so_far + remaining_round_lb_after_this_round > collect_weight_cap_ )
				return;
			const SearchWeight round_budget = collect_weight_cap_ - accumulated_weight_so_far - remaining_round_lb_after_this_round;

			DifferentialTrailStepRecord step {};
			step.round_index = round_boundary_depth + 1;
			step.input_branch_a_difference = branch_a_input_difference;
			step.input_branch_b_difference = branch_b_input_difference;

			step.first_addition_term_difference = NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^ NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, step.first_addition_term_difference, 32 );
			if ( optimal_weight_first_addition >= INFINITE_WEIGHT )
				return;

			SearchWeight weight_cap_first_addition = std::min<SearchWeight>( round_budget, SearchWeight( 31 ) );
			weight_cap_first_addition = std::min<SearchWeight>( weight_cap_first_addition, context_.configuration.addition_weight_cap );
			if ( optimal_weight_first_addition > weight_cap_first_addition )
				return;

			ModularAdditionEnumerator enum_first_add {};
			enum_first_add.reset( branch_b_input_difference, step.first_addition_term_difference, optimal_output_branch_b_difference_after_first_addition, weight_cap_first_addition );

			std::uint32_t output_branch_b_difference_after_first_addition = 0;
			SearchWeight weight_first_addition = 0;
			while ( enum_first_add.next( context_, output_branch_b_difference_after_first_addition, weight_first_addition ) )
			{
				step.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
				step.weight_first_addition = weight_first_addition;

				const int accumulated_after_first_addition = accumulated_weight_so_far + weight_first_addition;
				if ( accumulated_after_first_addition + remaining_round_lb_after_this_round > collect_weight_cap_ )
					continue;

				SearchWeight weight_cap_first_constant_subtraction = collect_weight_cap_ - accumulated_after_first_addition - remaining_round_lb_after_this_round;
				weight_cap_first_constant_subtraction = std::min<SearchWeight>( weight_cap_first_constant_subtraction, std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ) );

				SubConstEnumerator enum_first_const {};
				enum_first_const.reset( branch_a_input_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], weight_cap_first_constant_subtraction );

				std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
				SearchWeight weight_first_constant_subtraction = 0;
				while ( enum_first_const.next( context_, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction ) )
				{
					step.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
					step.weight_first_constant_subtraction = weight_first_constant_subtraction;

					const int accumulated_after_first_constant_subtraction = accumulated_after_first_addition + weight_first_constant_subtraction;
					if ( accumulated_after_first_constant_subtraction + remaining_round_lb_after_this_round > collect_weight_cap_ )
						continue;

					differential_apply_first_subround_cross_xor_bridge(
						output_branch_a_difference_after_first_constant_subtraction,
						output_branch_b_difference_after_first_addition,
						step.branch_a_difference_after_first_xor_with_rotated_branch_b,
						step.branch_b_difference_after_first_xor_with_rotated_branch_a );

					const std::uint32_t branch_b_difference_after_explicit_prewhitening_before_injection =
						differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
							step.branch_b_difference_after_first_xor_with_rotated_branch_a );
					const InjectionAffineTransition injection_transition_from_branch_b =
						compute_injection_transition_from_branch_b(
							branch_b_difference_after_explicit_prewhitening_before_injection );
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
						if ( optimal_weight_second_addition >= INFINITE_WEIGHT )
							continue;

						SearchWeight weight_cap_second_addition = collect_weight_cap_ - accumulated_before_second_addition - remaining_round_lb_after_this_round;
						weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, SearchWeight( 31 ) );
						weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, context_.configuration.addition_weight_cap );
						if ( optimal_weight_second_addition > weight_cap_second_addition )
							continue;

						ModularAdditionEnumerator enum_second_add {};
						enum_second_add.reset( step.branch_a_difference_after_injection_from_branch_b, step.second_addition_term_difference, optimal_output_branch_a_difference_after_second_addition, weight_cap_second_addition );

						std::uint32_t output_branch_a_difference_after_second_addition = 0;
						SearchWeight weight_second_addition = 0;
						while ( enum_second_add.next( context_, output_branch_a_difference_after_second_addition, weight_second_addition ) )
						{
							step.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
							step.weight_second_addition = weight_second_addition;

							const int accumulated_after_second_addition = accumulated_before_second_addition + weight_second_addition;
							if ( accumulated_after_second_addition + remaining_round_lb_after_this_round > collect_weight_cap_ )
								continue;

							SearchWeight weight_cap_second_constant_subtraction = collect_weight_cap_ - accumulated_after_second_addition - remaining_round_lb_after_this_round;
							weight_cap_second_constant_subtraction = std::min<SearchWeight>( weight_cap_second_constant_subtraction, std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ) );

							SubConstEnumerator enum_second_const {};
							enum_second_const.reset( step.branch_b_difference_after_first_bridge, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], weight_cap_second_constant_subtraction );

							std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
							SearchWeight weight_second_constant_subtraction = 0;
							while ( enum_second_const.next( context_, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction ) )
							{
								step.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
								step.weight_second_constant_subtraction = weight_second_constant_subtraction;

								const int accumulated_after_second_constant_subtraction = accumulated_after_second_addition + weight_second_constant_subtraction;
								if ( accumulated_after_second_constant_subtraction + remaining_round_lb_after_this_round > collect_weight_cap_ )
									continue;

								differential_apply_second_subround_cross_xor_bridge(
									output_branch_a_difference_after_second_addition,
									output_branch_b_difference_after_second_constant_subtraction,
									step.branch_b_difference_after_second_xor_with_rotated_branch_a,
									step.branch_a_difference_after_second_xor_with_rotated_branch_b );

								const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
									differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
										step.branch_a_difference_after_second_xor_with_rotated_branch_b );
								const InjectionAffineTransition injection_transition_from_branch_a =
									compute_injection_transition_from_branch_a(
										branch_a_difference_after_explicit_prewhitening_before_injection );
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

	class ResumableDifferentialHullCollectorCursor final
	{
	public:
		ResumableDifferentialHullCollectorCursor(
			DifferentialHullCollectorExecutionState& state_in,
			DifferentialCollectorResidualFrontierHelper& helper_in,
			DifferentialHullTrailCallback on_trail_in,
			std::function<void()> checkpoint_hook_in = {} )
			: state_( state_in ),
			  context_( state_in.context ),
			  aggregation_( state_in.aggregation_result ),
			  cursor_( state_in.cursor ),
			  helper_( helper_in ),
			  on_trail_( std::move( on_trail_in ) ),
			  checkpoint_hook_( std::move( checkpoint_hook_in ) )
		{
		}

		void start_from_initial_frame( std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference )
		{
			cursor_.stack.clear();
			context_.current_differential_trail.clear();
			DifferentialSearchFrame frame {};
			frame.stage = DifferentialSearchStage::Enter;
			frame.trail_size_at_entry = context_.current_differential_trail.size();
			frame.state.round_boundary_depth = 0;
			frame.state.accumulated_weight_so_far = 0;
			frame.state.branch_a_input_difference = branch_a_input_difference;
			frame.state.branch_b_input_difference = branch_b_input_difference;
			cursor_.stack.push_back( frame );
		}

		void search_from_cursor()
		{
			run();
		}

	private:
		DifferentialHullCollectorExecutionState& state_;
		DifferentialBestSearchContext&		   context_;
		DifferentialHullAggregationResult&	   aggregation_;
		DifferentialSearchCursor&			   cursor_;
		DifferentialCollectorResidualFrontierHelper& helper_;
		DifferentialHullTrailCallback		   on_trail_ {};
		std::function<void()>				   checkpoint_hook_ {};
		std::chrono::steady_clock::time_point last_checkpoint_time_ {};

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_differential_hull_boundary_record(
			std::int32_t rounds_remaining,
			DifferentialSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return helper_.make_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				best_prefix_weight );
		}

		TwilightDream::residual_frontier_shared::ResidualProblemRecord make_differential_hull_boundary_record(
			const DifferentialBestSearchContext&,
			std::int32_t rounds_remaining,
			DifferentialSearchStage stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight best_prefix_weight ) const noexcept
		{
			return make_differential_hull_boundary_record(
				rounds_remaining,
				stage_cursor,
				pair_a,
				pair_b,
				best_prefix_weight );
		}

		bool try_register_differential_hull_child_residual_candidate(
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const DifferentialSearchFrame* frame_snapshot = nullptr,
			const std::vector<DifferentialTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			if ( state_.residual_boundary_mode == DifferentialCollectorResidualBoundaryMode::PruneAtResidualBoundary )
				return false;
			return helper_.try_register_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

		bool try_register_differential_hull_child_residual_candidate(
			DifferentialBestSearchContext&,
			const TwilightDream::residual_frontier_shared::ResidualProblemRecord& record,
			const DifferentialSearchFrame* frame_snapshot = nullptr,
			const std::vector<DifferentialTrailStepRecord>* trail_snapshot = nullptr,
			SearchWeight prefix_weight_offset = 0 )
		{
			return try_register_differential_hull_child_residual_candidate(
				record,
				frame_snapshot,
				trail_snapshot,
				prefix_weight_offset );
		}

		DifferentialSearchFrame& current_frame()
		{
			return cursor_.stack.back();
		}

		void pop_frame()
		{
			if ( cursor_.stack.empty() )
				return;
			const std::size_t target = cursor_.stack.back().trail_size_at_entry;
			if ( context_.current_differential_trail.size() > target )
				context_.current_differential_trail.resize( target );
			cursor_.stack.pop_back();
		}

		void maybe_poll_checkpoint()
		{
			if ( !checkpoint_hook_ || context_.runtime_controls.checkpoint_every_seconds == 0 )
				return;
			if ( ( context_.visited_node_count & context_.progress_node_mask ) != 0 )
				return;
			const auto now = std::chrono::steady_clock::now();
			if ( last_checkpoint_time_.time_since_epoch().count() != 0 )
			{
				const double since_last = std::chrono::duration<double>( now - last_checkpoint_time_ ).count();
				if ( since_last < double( context_.runtime_controls.checkpoint_every_seconds ) )
					return;
			}
			checkpoint_hook_();
			last_checkpoint_time_ = now;
		}

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

		void collect_current_trail( SearchWeight total_weight, std::uint32_t output_branch_a_difference, std::uint32_t output_branch_b_difference )
		{
			const long double exact_probability = std::pow( 2.0L, -static_cast<long double>( total_weight ) );
			aggregation_.found_any = true;
			++aggregation_.collected_trail_count;
			aggregation_.aggregate_probability += exact_probability;
			auto& shell = aggregation_.shell_summaries[ total_weight ];
			++shell.trail_count;
			shell.aggregate_probability += exact_probability;
			if ( exact_probability > aggregation_.strongest_trail_probability || aggregation_.strongest_trail.empty() )
			{
				aggregation_.strongest_trail_probability = exact_probability;
				aggregation_.strongest_trail = context_.current_differential_trail;
			}
			if ( on_trail_ )
			{
				const DifferentialHullCollectedTrailView trail_view {
					total_weight,
					output_branch_a_difference,
					output_branch_b_difference,
					exact_probability,
					&context_.current_differential_trail
				};
				if ( !on_trail_( trail_view ) )
					aggregation_.hit_callback_stop = true;
			}
			if ( state_.maximum_collected_trails != 0 && aggregation_.collected_trail_count >= state_.maximum_collected_trails )
				aggregation_.hit_collection_limit = true;
		}

		void sync_limit_flags_from_enumerator( bool stop_due_to_limits )
		{
			if ( !stop_due_to_limits )
				return;
			if ( differential_runtime_node_limit_hit( context_ ) )
				aggregation_.hit_maximum_search_nodes = true;
			if ( context_.stop_due_to_time_limit )
				aggregation_.hit_time_limit = true;
		}

		bool should_stop_search( int depth, SearchWeight accumulated_weight )
		{
			if ( aggregation_.hit_callback_stop )
				return true;
			if ( state_.maximum_collected_trails != 0 && aggregation_.collected_trail_count >= state_.maximum_collected_trails )
			{
				aggregation_.hit_collection_limit = true;
				return true;
			}
			if ( context_.stop_due_to_time_limit )
				return true;
			if ( differential_note_runtime_node_visit( context_ ) )
			{
				aggregation_.hit_time_limit = context_.stop_due_to_time_limit;
				aggregation_.hit_maximum_search_nodes = differential_runtime_node_limit_hit( context_ );
				return true;
			}
			if ( differential_runtime_node_limit_hit( context_ ) )
			{
				aggregation_.hit_maximum_search_nodes = true;
				return true;
			}

			maybe_print_single_run_progress( context_, depth );
			maybe_poll_checkpoint();

			if ( accumulated_weight > state_.collect_weight_cap )
				return true;
			if ( accumulated_weight + remaining_round_lower_bound( depth ) > state_.collect_weight_cap )
				return true;
			return false;
		}

		bool should_prune_after_this_round( const DifferentialRoundSearchState& state, SearchWeight accumulated_weight ) const
		{
			return accumulated_weight + state.remaining_round_weight_lower_bound_after_this_round > state_.collect_weight_cap;
		}

		bool should_prune_local_state_dominance(
			DifferentialSearchStage stage_cursor,
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

		bool prepare_round_state( DifferentialRoundSearchState& state, int round_boundary_depth, std::uint32_t branch_a_input_difference, std::uint32_t branch_b_input_difference, SearchWeight accumulated_weight_so_far )
		{
			state.round_boundary_depth = round_boundary_depth;
			state.accumulated_weight_so_far = accumulated_weight_so_far;
			state.branch_a_input_difference = branch_a_input_difference;
			state.branch_b_input_difference = branch_b_input_difference;
			state.remaining_round_weight_lower_bound_after_this_round = remaining_round_lower_bound_after_this_round( round_boundary_depth );

			state.base_step = DifferentialTrailStepRecord {};
			state.base_step.round_index = round_boundary_depth + 1;
			state.base_step.input_branch_a_difference = branch_a_input_difference;
			state.base_step.input_branch_b_difference = branch_b_input_difference;

			state.first_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( branch_a_input_difference, 17 );
			state.base_step.first_addition_term_difference = state.first_addition_term_difference;

			const auto [ optimal_output_branch_b_difference_after_first_addition, optimal_weight_first_addition ] =
				find_optimal_gamma_with_weight( branch_b_input_difference, state.first_addition_term_difference, 32 );
			state.optimal_output_branch_b_difference_after_first_addition = optimal_output_branch_b_difference_after_first_addition;
			state.optimal_weight_first_addition = optimal_weight_first_addition;
			if ( state.optimal_weight_first_addition >= INFINITE_WEIGHT )
				return false;

			SearchWeight weight_cap_first_addition = state_.collect_weight_cap - accumulated_weight_so_far - state.remaining_round_weight_lower_bound_after_this_round;
			weight_cap_first_addition = std::min<SearchWeight>( weight_cap_first_addition, SearchWeight( 31 ) );
			weight_cap_first_addition = std::min<SearchWeight>( weight_cap_first_addition, context_.configuration.addition_weight_cap );
			state.weight_cap_first_addition = weight_cap_first_addition;
			return state.optimal_weight_first_addition <= state.weight_cap_first_addition;
		}

		void run()
		{
			while ( !cursor_.stack.empty() )
			{
				DifferentialSearchFrame& frame = current_frame();
				DifferentialRoundSearchState& state = frame.state;

				switch ( frame.stage )
				{
				case DifferentialSearchStage::Enter:
				{
					if ( should_stop_search( state.round_boundary_depth, state.accumulated_weight_so_far ) )
					{
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || context_.stop_due_to_time_limit )
							return;
						const int rounds_remaining = context_.configuration.round_count - state.round_boundary_depth;
						if ( rounds_remaining > 0 )
						{
							(void)try_register_differential_hull_child_residual_candidate(
								context_,
								make_differential_hull_boundary_record(
									context_,
									rounds_remaining,
									DifferentialSearchStage::Enter,
									state.branch_a_input_difference,
									state.branch_b_input_difference,
									state.accumulated_weight_so_far ),
								&frame,
								&context_.current_differential_trail,
								state.accumulated_weight_so_far );
						}
						pop_frame();
						break;
					}
					if ( state.round_boundary_depth == context_.configuration.round_count )
					{
						collect_current_trail(
							state.accumulated_weight_so_far,
							state.branch_a_input_difference,
							state.branch_b_input_difference );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_callback_stop )
							return;
						pop_frame();
						break;
					}
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::Enter,
						state.branch_a_input_difference,
						state.branch_b_input_difference,
						state.accumulated_weight_so_far ) )
					{
						pop_frame();
						break;
					}
					if ( !prepare_round_state( state, state.round_boundary_depth, state.branch_a_input_difference, state.branch_b_input_difference, state.accumulated_weight_so_far ) )
					{
						const int rounds_remaining = context_.configuration.round_count - state.round_boundary_depth;
						if ( rounds_remaining > 0 )
						{
							(void)try_register_differential_hull_child_residual_candidate(
								context_,
								make_differential_hull_boundary_record(
									context_,
									rounds_remaining,
									DifferentialSearchStage::Enter,
									state.branch_a_input_difference,
									state.branch_b_input_difference,
									state.accumulated_weight_so_far ),
								&frame,
								&context_.current_differential_trail,
								state.accumulated_weight_so_far );
						}
						pop_frame();
						break;
					}
					frame.enum_first_add.reset(
						state.branch_b_input_difference,
						state.first_addition_term_difference,
						state.optimal_output_branch_b_difference_after_first_addition,
						state.weight_cap_first_addition );
					frame.stage = DifferentialSearchStage::FirstAdd;
					break;
				}
				case DifferentialSearchStage::FirstAdd:
				{
					std::uint32_t output_branch_b_difference_after_first_addition = 0;
					SearchWeight weight_first_addition = 0;
					if ( !frame.enum_first_add.next( context_, output_branch_b_difference_after_first_addition, weight_first_addition ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_first_add.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						pop_frame();
						break;
					}

					state.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
					state.weight_first_addition = weight_first_addition;
					state.accumulated_weight_after_first_addition = state.accumulated_weight_so_far + weight_first_addition;
					if ( should_prune_after_this_round( state, state.accumulated_weight_after_first_addition ) )
					{
						const SearchWeight weight_cap_first_constant_subtraction =
							std::min<SearchWeight>(
								std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ),
								state_.collect_weight_cap - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round );
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::FirstConst;
						snapshot.enum_first_const.reset( state.branch_a_input_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], weight_cap_first_constant_subtraction );
						(void)try_register_differential_hull_child_residual_candidate(
							context_,
							make_differential_hull_boundary_record(
								context_,
								context_.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::FirstConst,
								state.branch_a_input_difference,
								state.output_branch_b_difference_after_first_addition,
								state.accumulated_weight_after_first_addition ),
							&snapshot,
							&context_.current_differential_trail,
							state.accumulated_weight_after_first_addition );
						break;
					}

					const SearchWeight weight_cap_first_constant_subtraction =
						std::min<SearchWeight>(
							std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ),
							state_.collect_weight_cap - state.accumulated_weight_after_first_addition - state.remaining_round_weight_lower_bound_after_this_round );
					frame.enum_first_const.reset( state.branch_a_input_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], weight_cap_first_constant_subtraction );
					frame.stage = DifferentialSearchStage::FirstConst;
					break;
				}
				case DifferentialSearchStage::FirstConst:
				{
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::FirstConst,
						state.branch_a_input_difference,
						state.output_branch_b_difference_after_first_addition,
						state.accumulated_weight_after_first_addition ) )
					{
						frame.stage = DifferentialSearchStage::FirstAdd;
						break;
					}

					std::uint32_t output_branch_a_difference_after_first_constant_subtraction = 0;
					SearchWeight weight_first_constant_subtraction = 0;
					if ( !frame.enum_first_const.next( context_, output_branch_a_difference_after_first_constant_subtraction, weight_first_constant_subtraction ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_first_const.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						frame.stage = DifferentialSearchStage::FirstAdd;
						break;
					}

					state.output_branch_a_difference_after_first_constant_subtraction = output_branch_a_difference_after_first_constant_subtraction;
					state.weight_first_constant_subtraction = weight_first_constant_subtraction;
					state.accumulated_weight_after_first_constant_subtraction = state.accumulated_weight_after_first_addition + weight_first_constant_subtraction;
					if ( should_prune_after_this_round( state, state.accumulated_weight_after_first_constant_subtraction ) )
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
					const InjectionAffineTransition injection_transition_from_branch_b =
						compute_injection_transition_from_branch_b(
							branch_b_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
					state.accumulated_weight_before_second_addition =
						state.accumulated_weight_after_first_constant_subtraction + state.weight_injection_from_branch_b;
					if ( should_prune_after_this_round( state, state.accumulated_weight_before_second_addition ) )
					{
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::InjB;
						snapshot.enum_inj_b.reset( injection_transition_from_branch_b, context_.configuration.maximum_transition_output_differences );
						(void)try_register_differential_hull_child_residual_candidate(
							context_,
							make_differential_hull_boundary_record(
								context_,
								context_.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::InjB,
								state.branch_a_difference_after_first_xor_with_rotated_branch_b,
								state.branch_b_difference_after_first_bridge,
								state.accumulated_weight_before_second_addition ),
							&snapshot,
							&context_.current_differential_trail,
							state.accumulated_weight_before_second_addition );
						break;
					}

					frame.enum_inj_b.reset( injection_transition_from_branch_b, context_.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjB;
					break;
				}
				case DifferentialSearchStage::InjB:
				{
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::InjB,
						state.branch_a_difference_after_first_xor_with_rotated_branch_b,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_before_second_addition ) )
					{
						frame.stage = DifferentialSearchStage::FirstConst;
						break;
					}

					std::uint32_t injection_from_branch_b_xor_difference = 0;
					if ( !frame.enum_inj_b.next( context_, injection_from_branch_b_xor_difference ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_inj_b.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						frame.stage = DifferentialSearchStage::FirstConst;
						break;
					}

					state.injection_from_branch_b_xor_difference = injection_from_branch_b_xor_difference;
					state.branch_a_difference_after_injection_from_branch_b =
						state.branch_a_difference_after_first_xor_with_rotated_branch_b ^ injection_from_branch_b_xor_difference;

					state.second_addition_term_difference =
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 31 ) ^
						NeoAlzetteCore::rotl<std::uint32_t>( state.branch_b_difference_after_first_bridge, 17 );
					const auto [ optimal_output_branch_a_difference_after_second_addition, optimal_weight_second_addition ] =
						find_optimal_gamma_with_weight( state.branch_a_difference_after_injection_from_branch_b, state.second_addition_term_difference, 32 );
					state.optimal_output_branch_a_difference_after_second_addition = optimal_output_branch_a_difference_after_second_addition;
					state.optimal_weight_second_addition = optimal_weight_second_addition;
					if ( state.optimal_weight_second_addition >= INFINITE_WEIGHT )
						break;

					SearchWeight weight_cap_second_addition = state_.collect_weight_cap - state.accumulated_weight_before_second_addition - state.remaining_round_weight_lower_bound_after_this_round;
					weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, SearchWeight( 31 ) );
					weight_cap_second_addition = std::min<SearchWeight>( weight_cap_second_addition, context_.configuration.addition_weight_cap );
					state.weight_cap_second_addition = weight_cap_second_addition;
					if ( state.optimal_weight_second_addition > weight_cap_second_addition )
						break;

					frame.enum_second_add.reset(
						state.branch_a_difference_after_injection_from_branch_b,
						state.second_addition_term_difference,
						state.optimal_output_branch_a_difference_after_second_addition,
						state.weight_cap_second_addition );
					frame.stage = DifferentialSearchStage::SecondAdd;
					break;
				}
				case DifferentialSearchStage::SecondAdd:
				{
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::SecondAdd,
						state.branch_a_difference_after_injection_from_branch_b,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_before_second_addition ) )
					{
						frame.stage = DifferentialSearchStage::InjB;
						break;
					}

					std::uint32_t output_branch_a_difference_after_second_addition = 0;
					SearchWeight weight_second_addition = 0;
					if ( !frame.enum_second_add.next( context_, output_branch_a_difference_after_second_addition, weight_second_addition ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_second_add.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						frame.stage = DifferentialSearchStage::InjB;
						break;
					}

					state.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
					state.weight_second_addition = weight_second_addition;
					state.accumulated_weight_after_second_addition = state.accumulated_weight_before_second_addition + weight_second_addition;
					if ( should_prune_after_this_round( state, state.accumulated_weight_after_second_addition ) )
					{
						const SearchWeight weight_cap_second_constant_subtraction =
							std::min<SearchWeight>(
								std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ),
								state_.collect_weight_cap - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round );
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::SecondConst;
						snapshot.enum_second_const.reset( state.branch_b_difference_after_first_bridge, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], weight_cap_second_constant_subtraction );
						(void)try_register_differential_hull_child_residual_candidate(
							context_,
							make_differential_hull_boundary_record(
								context_,
								context_.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::SecondConst,
								state.output_branch_a_difference_after_second_addition,
								state.branch_b_difference_after_first_bridge,
								state.accumulated_weight_after_second_addition ),
							&snapshot,
							&context_.current_differential_trail,
							state.accumulated_weight_after_second_addition );
						break;
					}

					const SearchWeight weight_cap_second_constant_subtraction =
						std::min<SearchWeight>(
							std::min<SearchWeight>( context_.configuration.constant_subtraction_weight_cap, SearchWeight( 32 ) ),
							state_.collect_weight_cap - state.accumulated_weight_after_second_addition - state.remaining_round_weight_lower_bound_after_this_round );
					frame.enum_second_const.reset( state.branch_b_difference_after_first_bridge, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], weight_cap_second_constant_subtraction );
					frame.stage = DifferentialSearchStage::SecondConst;
					break;
				}
				case DifferentialSearchStage::SecondConst:
				{
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::SecondConst,
						state.output_branch_a_difference_after_second_addition,
						state.branch_b_difference_after_first_bridge,
						state.accumulated_weight_after_second_addition ) )
					{
						frame.stage = DifferentialSearchStage::SecondAdd;
						break;
					}

					std::uint32_t output_branch_b_difference_after_second_constant_subtraction = 0;
					SearchWeight weight_second_constant_subtraction = 0;
					if ( !frame.enum_second_const.next( context_, output_branch_b_difference_after_second_constant_subtraction, weight_second_constant_subtraction ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_second_const.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						frame.stage = DifferentialSearchStage::SecondAdd;
						break;
					}

					state.output_branch_b_difference_after_second_constant_subtraction = output_branch_b_difference_after_second_constant_subtraction;
					state.weight_second_constant_subtraction = weight_second_constant_subtraction;
					state.accumulated_weight_after_second_constant_subtraction = state.accumulated_weight_after_second_addition + weight_second_constant_subtraction;
					if ( should_prune_after_this_round( state, state.accumulated_weight_after_second_constant_subtraction ) )
					{
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::InjA;
						std::uint32_t branch_b_difference_after_second_xor_with_rotated_branch_a = 0;
						std::uint32_t branch_a_difference_after_second_xor_with_rotated_branch_b = 0;
						differential_apply_second_subround_cross_xor_bridge(
							state.output_branch_a_difference_after_second_addition,
							output_branch_b_difference_after_second_constant_subtraction,
							branch_b_difference_after_second_xor_with_rotated_branch_a,
							branch_a_difference_after_second_xor_with_rotated_branch_b );
						const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
							differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
								branch_a_difference_after_second_xor_with_rotated_branch_b );
						const InjectionAffineTransition injection_transition_from_branch_a =
							compute_injection_transition_from_branch_a(
								branch_a_difference_after_explicit_prewhitening_before_injection );
						snapshot.enum_inj_a.reset( injection_transition_from_branch_a, context_.configuration.maximum_transition_output_differences );
						(void)try_register_differential_hull_child_residual_candidate(
							context_,
							make_differential_hull_boundary_record(
								context_,
								context_.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::InjA,
								state.output_branch_a_difference_after_second_addition,
								output_branch_b_difference_after_second_constant_subtraction,
								state.accumulated_weight_after_second_constant_subtraction ),
							&snapshot,
							&context_.current_differential_trail,
							state.accumulated_weight_after_second_constant_subtraction );
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
					const InjectionAffineTransition injection_transition_from_branch_a =
						compute_injection_transition_from_branch_a(
							branch_a_difference_after_explicit_prewhitening_before_injection );
					state.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
					state.accumulated_weight_at_round_end = state.accumulated_weight_after_second_constant_subtraction + state.weight_injection_from_branch_a;
					if ( should_prune_after_this_round( state, state.accumulated_weight_at_round_end ) )
					{
						DifferentialSearchFrame snapshot = frame;
						snapshot.stage = DifferentialSearchStage::InjA;
						snapshot.enum_inj_a.reset( injection_transition_from_branch_a, context_.configuration.maximum_transition_output_differences );
						(void)try_register_differential_hull_child_residual_candidate(
							context_,
							make_differential_hull_boundary_record(
								context_,
								context_.configuration.round_count - state.round_boundary_depth,
								DifferentialSearchStage::InjA,
								state.output_branch_a_difference_after_second_addition,
								state.output_branch_b_difference_after_second_constant_subtraction,
								state.accumulated_weight_at_round_end ),
							&snapshot,
							&context_.current_differential_trail,
							state.accumulated_weight_at_round_end );
						break;
					}

					frame.enum_inj_a.reset( injection_transition_from_branch_a, context_.configuration.maximum_transition_output_differences );
					frame.stage = DifferentialSearchStage::InjA;
					break;
				}
				case DifferentialSearchStage::InjA:
				{
					if ( should_prune_local_state_dominance(
						DifferentialSearchStage::InjA,
						state.output_branch_a_difference_after_second_addition,
						state.output_branch_b_difference_after_second_constant_subtraction,
						state.accumulated_weight_after_second_constant_subtraction ) )
					{
						frame.stage = DifferentialSearchStage::SecondConst;
						break;
					}

					std::uint32_t injection_from_branch_a_xor_difference = 0;
					if ( !frame.enum_inj_a.next( context_, injection_from_branch_a_xor_difference ) )
					{
						sync_limit_flags_from_enumerator( frame.enum_inj_a.stop_due_to_limits );
						if ( aggregation_.hit_collection_limit || aggregation_.hit_maximum_search_nodes || aggregation_.hit_callback_stop || aggregation_.hit_time_limit )
							return;
						frame.stage = DifferentialSearchStage::SecondConst;
						break;
					}

					state.injection_from_branch_a_xor_difference = injection_from_branch_a_xor_difference;
					state.output_branch_b_difference = state.branch_b_difference_after_second_xor_with_rotated_branch_a ^ injection_from_branch_a_xor_difference;
					state.output_branch_a_difference = state.branch_a_difference_after_second_xor_with_rotated_branch_b;

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
						state.weight_first_addition +
						state.weight_first_constant_subtraction +
						state.weight_injection_from_branch_b +
						state.weight_second_addition +
						state.weight_second_constant_subtraction +
						state.weight_injection_from_branch_a;

					context_.current_differential_trail.push_back( step );
					DifferentialSearchFrame child {};
					child.stage = DifferentialSearchStage::Enter;
					child.trail_size_at_entry = context_.current_differential_trail.size() - 1u;
					child.state.round_boundary_depth = state.round_boundary_depth + 1;
					child.state.accumulated_weight_so_far = state.accumulated_weight_at_round_end;
					child.state.branch_a_input_difference = state.output_branch_a_difference;
					child.state.branch_b_input_difference = state.output_branch_b_difference;
					cursor_.stack.push_back( child );
					break;
				}
				}
				maybe_poll_checkpoint();
			}
		}
	};

	class DifferentialHullBNBScheduler final
	{
	public:
		DifferentialHullBNBScheduler( DifferentialHullCollectorExecutionState& state_in )
			: state( state_in ), helper( state_in.context, state_in.cursor )
		{
		}

		void run( DifferentialHullTrailCallback on_trail, std::function<void()> checkpoint_hook )
		{
			const std::function<void()> final_checkpoint_hook = checkpoint_hook;
			helper.rebuild_pending_frontier_indexes();
			for ( auto& frame : state.cursor.stack )
			{
				frame.enum_first_add.stop_due_to_limits = false;
				frame.enum_first_const.stop_due_to_limits = false;
				frame.enum_inj_b.stop_due_to_limits = false;
				frame.enum_second_add.stop_due_to_limits = false;
				frame.enum_second_const.stop_due_to_limits = false;
				frame.enum_inj_a.stop_due_to_limits = false;
			}
			begin_differential_runtime_invocation( state.context );
			state.context.progress_start_time = state.context.run_start_time;
			best_search_shared_core::SearchControlSession<DifferentialBestSearchContext> control_session( state.context );
			control_session.begin();
			{
				ScopedRuntimeTimeLimitProbe time_probe( state.context.runtime_controls, state.context.runtime_state );
				ResumableDifferentialHullCollectorCursor searcher( state, helper, std::move( on_trail ), std::move( checkpoint_hook ) );
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
			state.aggregation_result.used_non_strict_branch_cap = differential_configuration_has_strict_branch_cap( state.context.configuration );
			state.aggregation_result.exact_within_collect_weight_cap =
				!state.aggregation_result.hit_maximum_search_nodes &&
				!state.aggregation_result.hit_time_limit &&
				!state.aggregation_result.hit_collection_limit &&
				!state.aggregation_result.hit_callback_stop &&
				!state.aggregation_result.used_non_strict_branch_cap;
			state.aggregation_result.exactness_rejection_reason = classify_differential_collection_exactness_reason( state.aggregation_result );
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
		DifferentialHullCollectorExecutionState& state;
		DifferentialCollectorResidualFrontierHelper helper;
	};

	DifferentialHullAggregationResult collect_differential_hull_exact(
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b,
		const DifferentialBestSearchConfiguration& base_configuration,
		const DifferentialHullCollectionOptions& options )
	{
		DifferentialHullCollectorExecutionState state {};
		initialize_differential_hull_collection_state( start_difference_a, start_difference_b, base_configuration, options, state );
		continue_differential_hull_collection_from_state( state, options.on_trail );
		return state.aggregation_result;
	}

	void initialize_differential_hull_collection_state(
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b,
		const DifferentialBestSearchConfiguration& base_configuration,
		const DifferentialHullCollectionOptions& options,
		DifferentialHullCollectorExecutionState& state )
	{
		state.~DifferentialHullCollectorExecutionState();
		::new ( static_cast<void*>( &state ) ) DifferentialHullCollectorExecutionState {};
		state.collect_weight_cap = options.collect_weight_cap;
		state.maximum_collected_trails = options.maximum_collected_trails;
		state.residual_boundary_mode = options.residual_boundary_mode;
		state.context.configuration = base_configuration;
		state.context.runtime_controls = options.runtime_controls;
		state.context.start_difference_a = start_difference_a;
		state.context.start_difference_b = start_difference_b;
		prepare_differential_remaining_round_lower_bound_table(
			state.context.configuration,
			state.context.configuration.round_count );
		state.context.configuration.enable_state_memoization = false;
		state.context.best_total_weight = ( state.collect_weight_cap >= INFINITE_WEIGHT - 1 ) ? INFINITE_WEIGHT : ( state.collect_weight_cap + 1 );
		state.context.progress_every_seconds = state.context.runtime_controls.progress_every_seconds;
		state.context.current_differential_trail.reserve( std::size_t( std::max( 1, state.context.configuration.round_count ) ) );
		state.aggregation_result.collect_weight_cap = state.collect_weight_cap;

		state.cursor.stack.clear();
		state.context.current_differential_trail.clear();
		state.context.pending_frontier.clear();
		state.context.pending_frontier_entries.clear();
		state.context.pending_frontier_index_by_key.clear();
		state.context.pending_frontier_entry_index_by_key.clear();
		DifferentialSearchFrame root_frame {};
		root_frame.stage = DifferentialSearchStage::Enter;
		root_frame.trail_size_at_entry = state.context.current_differential_trail.size();
		root_frame.state.round_boundary_depth = 0;
		root_frame.state.accumulated_weight_so_far = 0;
		root_frame.state.branch_a_input_difference = start_difference_a;
		root_frame.state.branch_b_input_difference = start_difference_b;
		state.cursor.stack.push_back( root_frame );
	}

	void continue_differential_hull_collection_from_state(
		DifferentialHullCollectorExecutionState& state,
		DifferentialHullTrailCallback on_trail,
		std::function<void()> checkpoint_hook )
	{
		DifferentialHullBNBScheduler( state ).run( std::move( on_trail ), std::move( checkpoint_hook ) );
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
			runtime_result.collect_weight_cap = std::min<SearchWeight>( options.explicit_collect_weight_cap, INFINITE_WEIGHT - 1 );
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
		const SearchWeight seeded_upper_bound_weight =
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
				runtime_result.best_search_result.best_weight + options.collect_weight_window );
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
