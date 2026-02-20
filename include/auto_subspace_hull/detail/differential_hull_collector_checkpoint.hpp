#pragma once
#if !defined( TWILIGHTDREAM_DIFFERENTIAL_HULL_COLLECTOR_CHECKPOINT_HPP )
#define TWILIGHTDREAM_DIFFERENTIAL_HULL_COLLECTOR_CHECKPOINT_HPP

#include "auto_subspace_hull/differential_hull_batch_export.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"

#include <iomanip>
#include <sstream>

namespace TwilightDream::auto_search_differential
{
	struct DifferentialHullCollectorCheckpointState
	{
		DifferentialBestSearchConfiguration base_configuration {};
		DifferentialHullCollectorExecutionState collector_state {};
		TwilightDream::hull_callback_aggregator::DifferentialCallbackHullAggregator callback_aggregator {};
		std::uint32_t start_difference_a = 0;
		std::uint32_t start_difference_b = 0;
		bool used_best_weight_reference = false;
		bool best_search_executed = false;
		MatsuiSearchRunDifferentialResult best_search_result {};
		std::string runtime_log_path {};
	};

	inline std::string default_differential_hull_collector_checkpoint_path(
		int round_count,
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b ) noexcept
	{
		std::ostringstream oss;
		oss << "collector_checkpoint_R" << round_count
			<< "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << start_difference_a
			<< "_DiffB" << std::setw( 8 ) << std::setfill( '0' ) << start_difference_b << std::dec;
		return TwilightDream::runtime_component::make_unique_timestamped_artifact_path( oss.str(), ".ckpt" );
	}

	inline std::string default_differential_hull_collector_runtime_log_path(
		int round_count,
		std::uint32_t start_difference_a,
		std::uint32_t start_difference_b ) noexcept
	{
		std::ostringstream oss;
		oss << "collector_runtime_R" << round_count
			<< "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << start_difference_a
			<< "_DiffB" << std::setw( 8 ) << std::setfill( '0' ) << start_difference_b << std::dec;
		return TwilightDream::runtime_component::make_unique_timestamped_artifact_path( oss.str(), ".runtime.log" );
	}

	namespace differential_hull_collector_checkpoint_detail
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;

		static inline void write_best_search_result(
			TwilightDream::auto_search_checkpoint::BinaryWriter& w,
			const MatsuiSearchRunDifferentialResult& result )
		{
			write_bool( w, result.found );
			w.write_u64( result.best_weight );
			w.write_u64( result.nodes_visited );
			write_bool( w, result.hit_maximum_search_nodes );
			write_bool( w, result.hit_time_limit );
			write_counted_container(
				w,
				result.best_trail,
				[&]( const DifferentialTrailStepRecord& step ) {
					write_trail_step( w, step );
				} );
			write_bool( w, result.used_non_strict_branch_cap );
			write_bool( w, result.used_target_best_weight_shortcut );
			write_bool( w, result.exhaustive_completed );
			write_bool( w, result.best_weight_certified );
			write_enum_u8( w, result.strict_rejection_reason );
		}

		static inline bool read_best_search_result(
			TwilightDream::auto_search_checkpoint::BinaryReader& r,
			MatsuiSearchRunDifferentialResult& result )
		{
			return
				read_bool( r, result.found ) &&
				r.read_u64( result.best_weight ) &&
				r.read_u64( result.nodes_visited ) &&
				read_bool( r, result.hit_maximum_search_nodes ) &&
				read_bool( r, result.hit_time_limit ) &&
				read_counted_container(
					r,
					result.best_trail,
					[&]( DifferentialTrailStepRecord& step ) {
						return read_trail_step( r, step );
					} ) &&
				read_bool( r, result.used_non_strict_branch_cap ) &&
				read_bool( r, result.used_target_best_weight_shortcut ) &&
				read_bool( r, result.exhaustive_completed ) &&
				read_bool( r, result.best_weight_certified ) &&
				read_enum_u8( r, result.strict_rejection_reason );
		}

		static inline void write_hull_shell_summary(
			TwilightDream::auto_search_checkpoint::BinaryWriter& w,
			const DifferentialHullShellSummary& summary )
		{
			w.write_u64( summary.trail_count );
			write_long_double( w, summary.aggregate_probability );
			write_long_double( w, summary.strongest_trail_probability );
			w.write_u32( summary.strongest_output_branch_a_difference );
			w.write_u32( summary.strongest_output_branch_b_difference );
			write_counted_container(
				w,
				summary.strongest_trail,
				[&]( const DifferentialTrailStepRecord& step ) {
					write_trail_step( w, step );
				} );
		}

		static inline bool read_hull_shell_summary(
			TwilightDream::auto_search_checkpoint::BinaryReader& r,
			DifferentialHullShellSummary& summary )
		{
			return
				r.read_u64( summary.trail_count ) &&
				read_long_double( r, summary.aggregate_probability ) &&
				read_long_double( r, summary.strongest_trail_probability ) &&
				r.read_u32( summary.strongest_output_branch_a_difference ) &&
				r.read_u32( summary.strongest_output_branch_b_difference ) &&
				read_counted_container(
					r,
					summary.strongest_trail,
					[&]( DifferentialTrailStepRecord& step ) {
						return read_trail_step( r, step );
					} );
		}

		static inline void write_aggregation_result(
			TwilightDream::auto_search_checkpoint::BinaryWriter& w,
			const DifferentialHullAggregationResult& result )
		{
			write_bool( w, result.found_any );
			w.write_u64( result.nodes_visited );
			w.write_u64( result.collected_trail_count );
			write_bool( w, result.hit_maximum_search_nodes );
			write_bool( w, result.hit_time_limit );
			write_bool( w, result.hit_collection_limit );
			write_bool( w, result.hit_callback_stop );
			write_bool( w, result.used_non_strict_branch_cap );
			write_bool( w, result.exact_within_collect_weight_cap );
			write_bool( w, result.best_weight_certified );
			write_enum_u8( w, result.exactness_rejection_reason );
			w.write_u64( result.collect_weight_cap );
			write_long_double( w, result.aggregate_probability );
			write_long_double( w, result.strongest_trail_probability );
			write_counted_map(
				w,
				result.shell_summaries,
				[&]( TwilightDream::auto_search_differential::SearchWeight shell_weight ) { w.write_u64( shell_weight ); },
				[&]( const DifferentialHullShellSummary& summary ) { write_hull_shell_summary( w, summary ); } );
			write_counted_container(
				w,
				result.strongest_trail,
				[&]( const DifferentialTrailStepRecord& step ) {
					write_trail_step( w, step );
				} );
		}

		static inline bool read_aggregation_result(
			TwilightDream::auto_search_checkpoint::BinaryReader& r,
			DifferentialHullAggregationResult& result )
		{
			return
				read_bool( r, result.found_any ) &&
				r.read_u64( result.nodes_visited ) &&
				r.read_u64( result.collected_trail_count ) &&
				read_bool( r, result.hit_maximum_search_nodes ) &&
				read_bool( r, result.hit_time_limit ) &&
				read_bool( r, result.hit_collection_limit ) &&
				read_bool( r, result.hit_callback_stop ) &&
				read_bool( r, result.used_non_strict_branch_cap ) &&
				read_bool( r, result.exact_within_collect_weight_cap ) &&
				read_bool( r, result.best_weight_certified ) &&
				read_enum_u8( r, result.exactness_rejection_reason ) &&
				r.read_u64( result.collect_weight_cap ) &&
				read_long_double( r, result.aggregate_probability ) &&
				read_long_double( r, result.strongest_trail_probability ) &&
				read_counted_map<std::map<TwilightDream::auto_search_differential::SearchWeight, DifferentialHullShellSummary>, TwilightDream::auto_search_differential::SearchWeight, DifferentialHullShellSummary>(
					r,
					result.shell_summaries,
					[&]( TwilightDream::auto_search_differential::SearchWeight& shell_weight ) { return r.read_u64( shell_weight ); },
					[&]( DifferentialHullShellSummary& summary ) { return read_hull_shell_summary( r, summary ); } ) &&
				read_counted_container(
					r,
					result.strongest_trail,
					[&]( DifferentialTrailStepRecord& step ) {
						return read_trail_step( r, step );
					} );
		}
	}  // namespace differential_hull_collector_checkpoint_detail

	inline bool write_differential_hull_collector_checkpoint(
		const std::string& path,
		const DifferentialHullCollectorCheckpointState& state )
	{
		using namespace differential_hull_collector_checkpoint_detail;
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[&]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::DifferentialResidualFrontierCollector ) )
					return false;
				write_config( w, state.base_configuration );
				w.write_u32( state.start_difference_a );
				w.write_u32( state.start_difference_b );
				write_runtime_controls( w, state.collector_state.context.runtime_controls );
				w.write_u64( state.collector_state.context.visited_node_count );
				w.write_u64( state.collector_state.context.accumulated_elapsed_usec );
				w.write_u64( state.collector_state.collect_weight_cap );
				w.write_u64( state.collector_state.maximum_collected_trails );
				write_enum_u8( w, state.collector_state.residual_boundary_mode );
				write_bool( w, state.used_best_weight_reference );
				write_bool( w, state.best_search_executed );
				write_best_search_result( w, state.best_search_result );
				w.write_string( state.runtime_log_path );
				write_trail_vector( w, state.collector_state.context.current_differential_trail );
				write_cursor( w, state.collector_state.cursor );
				write_bool( w, state.collector_state.context.active_problem_valid );
				write_bool( w, state.collector_state.context.active_problem_is_root );
				TwilightDream::residual_frontier_shared::write_residual_problem_record(
					w,
					state.collector_state.context.active_problem_record );
				TwilightDream::residual_frontier_shared::write_record_vector(
					w,
					state.collector_state.context.pending_frontier,
					TwilightDream::residual_frontier_shared::write_residual_problem_record );
				TwilightDream::residual_frontier_shared::write_record_vector(
					w,
					state.collector_state.context.pending_frontier_entries,
					write_residual_frontier_entry );
				TwilightDream::residual_frontier_shared::write_record_vector(
					w,
					state.collector_state.context.completed_source_input_pairs,
					TwilightDream::residual_frontier_shared::write_residual_problem_record );
				TwilightDream::residual_frontier_shared::write_record_vector(
					w,
					state.collector_state.context.completed_output_as_next_input_pairs,
					TwilightDream::residual_frontier_shared::write_residual_problem_record );
				TwilightDream::residual_frontier_shared::write_completed_residual_set(
					w,
					state.collector_state.context.completed_residual_set );
				TwilightDream::residual_frontier_shared::write_best_prefix_table(
					w,
					state.collector_state.context.best_prefix_by_residual_key );
				TwilightDream::residual_frontier_shared::write_record_vector(
					w,
					state.collector_state.context.global_residual_result_table,
					TwilightDream::residual_frontier_shared::write_residual_result_record );
				TwilightDream::residual_frontier_shared::write_residual_counters(
					w,
					state.collector_state.context.residual_counters );
				write_aggregation_result( w, state.collector_state.aggregation_result );
				TwilightDream::hull_callback_aggregator::write_differential_callback_hull_aggregator( w, state.callback_aggregator );
				return w.ok();
			} );
	}

	inline bool read_differential_hull_collector_checkpoint(
		const std::string& path,
		DifferentialHullCollectorCheckpointState& state )
	{
		using namespace differential_hull_collector_checkpoint_detail;
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::DifferentialResidualFrontierCollector )
			return false;
		if ( !read_config( r, state.base_configuration ) )
			return false;
		if ( !r.read_u32( state.start_difference_a ) )
			return false;
		if ( !r.read_u32( state.start_difference_b ) )
			return false;
		if ( !read_runtime_controls( r, state.collector_state.context.runtime_controls ) )
			return false;
		if ( !r.read_u64( state.collector_state.context.visited_node_count ) )
			return false;
		if ( !r.read_u64( state.collector_state.context.accumulated_elapsed_usec ) )
			return false;
		if ( !r.read_u64( state.collector_state.collect_weight_cap ) )
			return false;
		if ( !r.read_u64( state.collector_state.maximum_collected_trails ) )
			return false;
		if ( !read_enum_u8( r, state.collector_state.residual_boundary_mode ) )
			return false;
		if ( !read_bool( r, state.used_best_weight_reference ) )
			return false;
		if ( !read_bool( r, state.best_search_executed ) )
			return false;
		if ( !read_best_search_result( r, state.best_search_result ) )
			return false;
		if ( !r.read_string( state.runtime_log_path ) )
			return false;
		if ( !read_trail_vector( r, state.collector_state.context.current_differential_trail ) )
			return false;
		if ( !read_cursor( r, state.collector_state.cursor ) )
			return false;
		if ( !read_bool( r, state.collector_state.context.active_problem_valid ) )
			return false;
		if ( !read_bool( r, state.collector_state.context.active_problem_is_root ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_residual_problem_record(
				 r,
				 state.collector_state.context.active_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 state.collector_state.context.pending_frontier,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 state.collector_state.context.pending_frontier_entries,
				 read_residual_frontier_entry ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 state.collector_state.context.completed_source_input_pairs,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 state.collector_state.context.completed_output_as_next_input_pairs,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_completed_residual_set(
				 r,
				 state.collector_state.context.completed_residual_set ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_best_prefix_table(
				 r,
				 state.collector_state.context.best_prefix_by_residual_key ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 state.collector_state.context.global_residual_result_table,
				 TwilightDream::residual_frontier_shared::read_residual_result_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_residual_counters(
				 r,
				 state.collector_state.context.residual_counters ) )
			return false;
		if ( !read_aggregation_result( r, state.collector_state.aggregation_result ) )
			return false;
		if ( !TwilightDream::hull_callback_aggregator::read_differential_callback_hull_aggregator( r, state.callback_aggregator ) )
			return false;

		state.collector_state.context.configuration = state.base_configuration;
		state.collector_state.context.configuration.enable_state_memoization = false;
		state.collector_state.context.start_difference_a = state.start_difference_a;
		state.collector_state.context.start_difference_b = state.start_difference_b;
		state.collector_state.context.best_total_weight = successor_search_weight( state.collector_state.collect_weight_cap );
		state.collector_state.context.progress_every_seconds = state.collector_state.context.runtime_controls.progress_every_seconds;
		state.collector_state.context.run_visited_node_count = 0;
		state.collector_state.context.current_differential_trail.reserve( std::size_t( std::max( 1, state.base_configuration.round_count ) ) );
		state.collector_state.aggregation_result.collect_weight_cap = state.collector_state.collect_weight_cap;
		state.callback_aggregator.set_stored_trail_policy( state.callback_aggregator.stored_trail_policy );
		state.callback_aggregator.set_maximum_stored_trails( state.callback_aggregator.maximum_stored_trails );
		return true;
	}

}  // namespace TwilightDream::auto_search_differential

#endif
