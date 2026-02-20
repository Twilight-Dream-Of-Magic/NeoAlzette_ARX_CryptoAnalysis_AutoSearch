#pragma once
#if !defined( TWILIGHTDREAM_LINEAR_HULL_BATCH_EXPORT_HPP )
#define TWILIGHTDREAM_LINEAR_HULL_BATCH_EXPORT_HPP

#include "auto_search_frame/linear_hull_callback_aggregator.hpp"
#include "auto_search_frame/detail/hull_batch_checkpoint_shared.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "common/runtime_component.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <mutex>
#include <utility>
#include <vector>

namespace TwilightDream::hull_callback_aggregator
{
	struct LinearBatchHullJobSummary
	{
		std::size_t											   job_index = 0;
		LinearBatchJob										   job {};
		bool												   collected = false;
		bool												   best_search_executed = false;
		bool												   best_weight_certified = false;
		int													   best_weight = TwilightDream::auto_search_linear::INFINITE_WEIGHT;
		int													   collect_weight_cap = 0;
		bool												   exact_within_collect_weight_cap = false;
		TwilightDream::auto_search_linear::StrictCertificationFailureReason strict_runtime_rejection_reason =
			TwilightDream::auto_search_linear::StrictCertificationFailureReason::None;
		TwilightDream::auto_search_linear::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_linear::StrictCertificationFailureReason::None;
		std::uint64_t best_search_nodes_visited = 0;
		std::uint64_t hull_nodes_visited = 0;
		std::uint64_t collected_trails = 0;
		std::size_t	  endpoint_hull_count = 0;
		long double	  global_abs_correlation_mass = 0.0L;
		long double	  strongest_trail_abs_correlation = 0.0L;
		bool		  hit_any_limit = false;
	};

	struct LinearBatchHullRunSummary
	{
		std::vector<LinearBatchHullJobSummary> jobs {};
		std::uint64_t						  total_best_search_nodes = 0;
		std::uint64_t						  total_hull_nodes = 0;
		std::size_t							  exact_jobs = 0;
		std::size_t							  partial_jobs = 0;
		std::size_t							  rejected_jobs = 0;
		std::size_t							  jobs_hit_hard_limits = 0;
	};

	static inline LinearBatchHullJobSummary summarize_linear_batch_hull_job(
		std::size_t												 job_index,
		const LinearBatchJob&									 job,
		const TwilightDream::auto_search_linear::LinearStrictHullRuntimeResult& runtime_result,
		const LinearCallbackHullAggregator&						 callback_aggregator )
	{
		LinearBatchHullJobSummary summary {};
		summary.job_index = job_index;
		summary.job = job;
		summary.collected = runtime_result.collected;
		summary.best_search_executed = runtime_result.best_search_executed;
		summary.best_weight_certified = runtime_result.best_search_result.best_weight_certified;
		summary.best_weight = runtime_result.best_search_result.best_weight;
		summary.collect_weight_cap = runtime_result.collect_weight_cap;
		summary.strict_runtime_rejection_reason = runtime_result.strict_runtime_rejection_reason;
		summary.exact_within_collect_weight_cap = runtime_result.aggregation_result.exact_within_collect_weight_cap;
		summary.exactness_rejection_reason = runtime_result.aggregation_result.exactness_rejection_reason;
		summary.best_search_nodes_visited = runtime_result.best_search_result.nodes_visited;
		summary.hull_nodes_visited = runtime_result.aggregation_result.nodes_visited;
		summary.collected_trails = callback_aggregator.collected_trail_count;
		summary.endpoint_hull_count = callback_aggregator.endpoint_hulls.size();
		summary.global_abs_correlation_mass = callback_aggregator.aggregate_abs_correlation_mass;
		summary.strongest_trail_abs_correlation = std::fabsl( callback_aggregator.strongest_trail_signed_correlation );
		summary.hit_any_limit =
			TwilightDream::auto_search_linear::strict_certification_failure_reason_is_hard_limit( runtime_result.strict_runtime_rejection_reason ) ||
			linear_aggregation_hit_any_limit( runtime_result.aggregation_result );
		return summary;
	}

	static inline LinearBatchHullRunSummary summarize_linear_batch_hull_run( std::vector<LinearBatchHullJobSummary> jobs )
	{
		LinearBatchHullRunSummary batch_summary {};
		batch_summary.jobs = std::move( jobs );
		for ( const auto& summary : batch_summary.jobs )
		{
			batch_summary.total_best_search_nodes += summary.best_search_nodes_visited;
			batch_summary.total_hull_nodes += summary.hull_nodes_visited;
			if ( summary.hit_any_limit )
				++batch_summary.jobs_hit_hard_limits;
			if ( !summary.collected )
				++batch_summary.rejected_jobs;
			else if ( summary.exact_within_collect_weight_cap )
				++batch_summary.exact_jobs;
			else
				++batch_summary.partial_jobs;
		}
		return batch_summary;
	}

	static inline TwilightDream::auto_search_linear::LinearHullTrailCallback compose_linear_hull_trail_callbacks(
		TwilightDream::auto_search_linear::LinearHullTrailCallback primary,
		TwilightDream::auto_search_linear::LinearHullTrailCallback secondary )
	{
		if ( !primary )
			return secondary;
		if ( !secondary )
			return primary;
		return
			[ primary = std::move( primary ), secondary = std::move( secondary ) ]( const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view ) mutable {
				return primary( trail_view ) && secondary( trail_view );
			};
	}

	struct LinearBatchHullBestSearchSeed
	{
		bool present = false;
		int seeded_upper_bound_weight = TwilightDream::auto_search_linear::INFINITE_WEIGHT;
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> seeded_upper_bound_trail {};
	};

	struct LinearBatchHullPipelineOptions
	{
		int															 worker_thread_count = 1;
		TwilightDream::auto_search_linear::LinearBestSearchConfiguration base_search_configuration {};
		TwilightDream::auto_search_linear::LinearStrictHullRuntimeOptions runtime_options_template {};
		std::vector<LinearBatchHullBestSearchSeed>						 best_search_seeds {};
		bool															 enable_combined_source_aggregator = false;
		StoredTrailPolicy												 stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		std::size_t														 maximum_stored_trails = 0;
	};

	struct LinearCombinedSourceHullSummary
	{
		bool						 enabled = false;
		std::size_t					 source_count = 0;
		bool						 all_jobs_collected = false;
		bool						 all_jobs_exact_within_collect_weight_cap = false;
		bool						 all_jobs_hard_limit_free = false;
		LinearCallbackHullAggregator callback_aggregator {};
	};

	struct LinearBatchHullPipelineResult
	{
		LinearBatchHullRunSummary	   batch_summary {};
		LinearCombinedSourceHullSummary combined_source_hull {};
	};

	struct LinearBatchHullRuntimeOptionsSnapshot
	{
		TwilightDream::auto_search_linear::HullCollectCapMode collect_cap_mode =
			TwilightDream::auto_search_linear::HullCollectCapMode::BestWeightPlusWindow;
		int			  explicit_collect_weight_cap = 0;
		int			  collect_weight_window = 0;
		std::uint64_t maximum_collected_trails = 0;
		TwilightDream::auto_search_linear::LinearBestSearchRuntimeControls runtime_controls {};
	};

	struct LinearBatchHullPipelineCheckpointState
	{
		TwilightDream::auto_search_linear::LinearBestSearchConfiguration base_search_configuration {};
		LinearBatchHullRuntimeOptionsSnapshot							 runtime_options_snapshot {};
		std::vector<LinearBatchJob>										 jobs {};
		std::vector<LinearBatchHullBestSearchSeed>						 best_search_seeds {};
		bool															 enable_combined_source_aggregator = false;
		StoredTrailPolicy												 stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		std::size_t														 maximum_stored_trails = 0;
		std::vector<std::uint8_t>										 completed_job_flags {};
		std::vector<LinearBatchHullJobSummary>							 summaries {};
		LinearCallbackHullAggregator									 combined_callback_aggregator {};
	};

	static inline LinearBatchHullRuntimeOptionsSnapshot make_linear_batch_hull_runtime_options_snapshot(
		const TwilightDream::auto_search_linear::LinearStrictHullRuntimeOptions& options )
	{
		LinearBatchHullRuntimeOptionsSnapshot snapshot {};
		snapshot.collect_cap_mode = options.collect_cap_mode;
		snapshot.explicit_collect_weight_cap = options.explicit_collect_weight_cap;
		snapshot.collect_weight_window = options.collect_weight_window;
		snapshot.maximum_collected_trails = options.maximum_collected_trails;
		snapshot.runtime_controls = options.runtime_controls;
		return snapshot;
	}

	static inline TwilightDream::auto_search_linear::LinearStrictHullRuntimeOptions make_linear_batch_hull_runtime_options(
		const LinearBatchHullPipelineCheckpointState& state,
		TwilightDream::auto_search_linear::LinearHullTrailCallback extra_on_trail = {} )
	{
		TwilightDream::auto_search_linear::LinearStrictHullRuntimeOptions runtime_options {};
		runtime_options.collect_cap_mode = state.runtime_options_snapshot.collect_cap_mode;
		runtime_options.explicit_collect_weight_cap = state.runtime_options_snapshot.explicit_collect_weight_cap;
		runtime_options.collect_weight_window = state.runtime_options_snapshot.collect_weight_window;
		runtime_options.maximum_collected_trails = state.runtime_options_snapshot.maximum_collected_trails;
		runtime_options.runtime_controls = state.runtime_options_snapshot.runtime_controls;
		runtime_options.on_trail = std::move( extra_on_trail );
		runtime_options.runtime_controls.checkpoint_every_seconds = 0;
		runtime_options.best_search_resume_checkpoint_path.clear();
		runtime_options.best_search_history_log = nullptr;
		runtime_options.best_search_binary_checkpoint = nullptr;
		runtime_options.best_search_runtime_log = nullptr;
		return runtime_options;
	}

	static inline LinearBatchHullPipelineCheckpointState make_initial_linear_batch_hull_pipeline_checkpoint_state(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchHullPipelineOptions& options )
	{
		LinearBatchHullPipelineCheckpointState state {};
		state.base_search_configuration = options.base_search_configuration;
		state.runtime_options_snapshot = make_linear_batch_hull_runtime_options_snapshot( options.runtime_options_template );
		state.jobs = jobs;
		state.best_search_seeds = options.best_search_seeds;
		state.enable_combined_source_aggregator = options.enable_combined_source_aggregator;
		state.stored_trail_policy = options.stored_trail_policy;
		state.maximum_stored_trails = options.maximum_stored_trails;
		state.completed_job_flags.assign( jobs.size(), 0u );
		state.summaries.resize( jobs.size() );
		state.combined_callback_aggregator.set_stored_trail_policy( options.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( options.maximum_stored_trails );
		return state;
	}

	static inline void write_linear_batch_job( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearBatchJob& job )
	{
		w.write_i32( job.rounds );
		w.write_u32( job.output_branch_a_mask );
		w.write_u32( job.output_branch_b_mask );
	}

	static inline bool read_linear_batch_job( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearBatchJob& job )
	{
		return
			r.read_i32( job.rounds ) &&
			r.read_u32( job.output_branch_a_mask ) &&
			r.read_u32( job.output_branch_b_mask );
	}

	static inline void write_linear_batch_hull_job_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearBatchHullJobSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_size( w, summary.job_index );
		write_linear_batch_job( w, summary.job );
		write_bool( w, summary.collected );
		write_bool( w, summary.best_search_executed );
		write_bool( w, summary.best_weight_certified );
		w.write_i32( summary.best_weight );
		w.write_i32( summary.collect_weight_cap );
		write_bool( w, summary.exact_within_collect_weight_cap );
		write_enum_u8( w, summary.strict_runtime_rejection_reason );
		write_enum_u8( w, summary.exactness_rejection_reason );
		w.write_u64( summary.best_search_nodes_visited );
		w.write_u64( summary.hull_nodes_visited );
		w.write_u64( summary.collected_trails );
		write_size( w, summary.endpoint_hull_count );
		write_long_double( w, summary.global_abs_correlation_mass );
		write_long_double( w, summary.strongest_trail_abs_correlation );
		write_bool( w, summary.hit_any_limit );
	}

	static inline bool read_linear_batch_hull_job_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearBatchHullJobSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_size( r, summary.job_index ) &&
			read_linear_batch_job( r, summary.job ) &&
			read_bool( r, summary.collected ) &&
			read_bool( r, summary.best_search_executed ) &&
			read_bool( r, summary.best_weight_certified ) &&
			r.read_i32( summary.best_weight ) &&
			r.read_i32( summary.collect_weight_cap ) &&
			read_bool( r, summary.exact_within_collect_weight_cap ) &&
			read_enum_u8( r, summary.strict_runtime_rejection_reason ) &&
			read_enum_u8( r, summary.exactness_rejection_reason ) &&
			r.read_u64( summary.best_search_nodes_visited ) &&
			r.read_u64( summary.hull_nodes_visited ) &&
			r.read_u64( summary.collected_trails ) &&
			read_size( r, summary.endpoint_hull_count ) &&
			read_long_double( r, summary.global_abs_correlation_mass ) &&
			read_long_double( r, summary.strongest_trail_abs_correlation ) &&
			read_bool( r, summary.hit_any_limit );
	}

	static inline void write_linear_batch_hull_best_search_seed(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearBatchHullBestSearchSeed& seed )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_bool( w, seed.present );
		w.write_i32( seed.seeded_upper_bound_weight );
		write_counted_container(
			w,
			seed.seeded_upper_bound_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_batch_hull_best_search_seed(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearBatchHullBestSearchSeed& seed )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_bool( r, seed.present ) &&
			r.read_i32( seed.seeded_upper_bound_weight ) &&
			read_counted_container(
				r,
				seed.seeded_upper_bound_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_hull_endpoint_key(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearHullEndpointKey& key )
	{
		w.write_u32( key.input_branch_a_mask );
		w.write_u32( key.input_branch_b_mask );
	}

	static inline bool read_linear_hull_endpoint_key(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearHullEndpointKey& key )
	{
		return
			r.read_u32( key.input_branch_a_mask ) &&
			r.read_u32( key.input_branch_b_mask );
	}

	static inline void write_linear_hull_source_key(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearHullSourceKey& key )
	{
		TwilightDream::hull_batch_checkpoint_shared::write_size( w, key.source_index );
	}

	static inline bool read_linear_hull_source_key(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearHullSourceKey& key )
	{
		return TwilightDream::hull_batch_checkpoint_shared::read_size( r, key.source_index );
	}

	static inline void write_linear_callback_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearCallbackShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_signed_correlation );
		write_long_double( w, summary.aggregate_abs_correlation_mass );
		write_long_double( w, summary.strongest_trail_signed_correlation );
		w.write_u32( summary.strongest_input_branch_a_mask );
		w.write_u32( summary.strongest_input_branch_b_mask );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_callback_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearCallbackShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_signed_correlation ) &&
			read_long_double( r, summary.aggregate_abs_correlation_mass ) &&
			read_long_double( r, summary.strongest_trail_signed_correlation ) &&
			r.read_u32( summary.strongest_input_branch_a_mask ) &&
			r.read_u32( summary.strongest_input_branch_b_mask ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_endpoint_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearEndpointShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_signed_correlation );
		write_long_double( w, summary.aggregate_abs_correlation_mass );
		write_long_double( w, summary.strongest_trail_signed_correlation );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_endpoint_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearEndpointShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_signed_correlation ) &&
			read_long_double( r, summary.aggregate_abs_correlation_mass ) &&
			read_long_double( r, summary.strongest_trail_signed_correlation ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_endpoint_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearEndpointHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_signed_correlation );
		write_long_double( w, summary.aggregate_abs_correlation_mass );
		write_long_double( w, summary.strongest_trail_signed_correlation );
		write_counted_map(
			w,
			summary.shell_summaries,
			[&]( int shell_weight ) { w.write_i32( shell_weight ); },
			[&]( const LinearEndpointShellSummary& shell_summary ) {
				write_linear_endpoint_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_endpoint_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearEndpointHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_signed_correlation ) &&
			read_long_double( r, summary.aggregate_abs_correlation_mass ) &&
			read_long_double( r, summary.strongest_trail_signed_correlation ) &&
			read_counted_map<std::map<int, LinearEndpointShellSummary>, int, LinearEndpointShellSummary>(
				r,
				summary.shell_summaries,
				[&]( int& shell_weight ) { return r.read_i32( shell_weight ); },
				[&]( LinearEndpointShellSummary& shell_summary ) {
					return read_linear_endpoint_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_source_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearSourceHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_size( w, summary.source_index );
		w.write_i32( summary.source_rounds );
		w.write_u64( summary.trail_count );
		w.write_u32( summary.source_output_branch_a_mask );
		w.write_u32( summary.source_output_branch_b_mask );
		write_long_double( w, summary.aggregate_signed_correlation );
		write_long_double( w, summary.aggregate_abs_correlation_mass );
		write_long_double( w, summary.strongest_trail_signed_correlation );
		w.write_u32( summary.strongest_input_branch_a_mask );
		w.write_u32( summary.strongest_input_branch_b_mask );
		write_bool( w, summary.runtime_collected );
		write_bool( w, summary.best_search_executed );
		write_bool( w, summary.used_best_weight_reference );
		write_bool( w, summary.best_weight_certified );
		write_bool( w, summary.exact_within_collect_weight_cap );
		w.write_i32( summary.collect_weight_cap );
		w.write_u64( summary.best_search_nodes_visited );
		w.write_u64( summary.hull_nodes_visited );
		write_enum_u8( w, summary.strict_runtime_rejection_reason );
		write_enum_u8( w, summary.exactness_rejection_reason );
		write_counted_map(
			w,
			summary.endpoint_hulls,
			[&]( const LinearHullEndpointKey& key ) { write_linear_hull_endpoint_key( w, key ); },
			[&]( const LinearEndpointHullSummary& endpoint_summary ) {
				write_linear_endpoint_hull_summary( w, endpoint_summary );
			} );
		write_counted_map(
			w,
			summary.shell_summaries,
			[&]( int shell_weight ) { w.write_i32( shell_weight ); },
			[&]( const LinearCallbackShellSummary& shell_summary ) {
				write_linear_callback_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_source_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearSourceHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_size( r, summary.source_index ) &&
			r.read_i32( summary.source_rounds ) &&
			r.read_u64( summary.trail_count ) &&
			r.read_u32( summary.source_output_branch_a_mask ) &&
			r.read_u32( summary.source_output_branch_b_mask ) &&
			read_long_double( r, summary.aggregate_signed_correlation ) &&
			read_long_double( r, summary.aggregate_abs_correlation_mass ) &&
			read_long_double( r, summary.strongest_trail_signed_correlation ) &&
			r.read_u32( summary.strongest_input_branch_a_mask ) &&
			r.read_u32( summary.strongest_input_branch_b_mask ) &&
			read_bool( r, summary.runtime_collected ) &&
			read_bool( r, summary.best_search_executed ) &&
			read_bool( r, summary.used_best_weight_reference ) &&
			read_bool( r, summary.best_weight_certified ) &&
			read_bool( r, summary.exact_within_collect_weight_cap ) &&
			r.read_i32( summary.collect_weight_cap ) &&
			r.read_u64( summary.best_search_nodes_visited ) &&
			r.read_u64( summary.hull_nodes_visited ) &&
			read_enum_u8( r, summary.strict_runtime_rejection_reason ) &&
			read_enum_u8( r, summary.exactness_rejection_reason ) &&
			read_counted_map<std::map<LinearHullEndpointKey, LinearEndpointHullSummary>, LinearHullEndpointKey, LinearEndpointHullSummary>(
				r,
				summary.endpoint_hulls,
				[&]( LinearHullEndpointKey& key ) { return read_linear_hull_endpoint_key( r, key ); },
				[&]( LinearEndpointHullSummary& endpoint_summary ) {
					return read_linear_endpoint_hull_summary( r, endpoint_summary );
				} ) &&
			read_counted_map<std::map<int, LinearCallbackShellSummary>, int, LinearCallbackShellSummary>(
				r,
				summary.shell_summaries,
				[&]( int& shell_weight ) { return r.read_i32( shell_weight ); },
				[&]( LinearCallbackShellSummary& shell_summary ) {
					return read_linear_callback_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_collected_trail_record(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearCollectedTrailRecord& record )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( record.arrival_index );
		w.write_i32( record.total_weight );
		w.write_u32( record.input_branch_a_mask );
		w.write_u32( record.input_branch_b_mask );
		write_bool( w, record.source_present );
		write_size( w, record.source_index );
		w.write_i32( record.source_rounds );
		w.write_u32( record.source_output_branch_a_mask );
		w.write_u32( record.source_output_branch_b_mask );
		write_long_double( w, record.exact_signed_correlation );
		write_counted_container(
			w,
			record.trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_collected_trail_record(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearCollectedTrailRecord& record )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( record.arrival_index ) &&
			r.read_i32( record.total_weight ) &&
			r.read_u32( record.input_branch_a_mask ) &&
			r.read_u32( record.input_branch_b_mask ) &&
			read_bool( r, record.source_present ) &&
			read_size( r, record.source_index ) &&
			r.read_i32( record.source_rounds ) &&
			r.read_u32( record.source_output_branch_a_mask ) &&
			r.read_u32( record.source_output_branch_b_mask ) &&
			read_long_double( r, record.exact_signed_correlation ) &&
			read_counted_container(
				r,
				record.trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline void write_linear_callback_hull_aggregator(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearCallbackHullAggregator& aggregator )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_bool( w, aggregator.found_any );
		write_bool( w, aggregator.stop_requested );
		w.write_u64( aggregator.collected_trail_count );
		w.write_u64( aggregator.dropped_stored_trail_count );
		write_size( w, aggregator.maximum_stored_trails );
		write_enum_u8( w, aggregator.stored_trail_policy );
		write_long_double( w, aggregator.aggregate_signed_correlation );
		write_long_double( w, aggregator.aggregate_abs_correlation_mass );
		write_long_double( w, aggregator.strongest_trail_signed_correlation );
		w.write_u32( aggregator.strongest_input_branch_a_mask );
		w.write_u32( aggregator.strongest_input_branch_b_mask );
		write_counted_map(
			w,
			aggregator.endpoint_hulls,
			[&]( const LinearHullEndpointKey& key ) { write_linear_hull_endpoint_key( w, key ); },
			[&]( const LinearEndpointHullSummary& endpoint_summary ) {
				write_linear_endpoint_hull_summary( w, endpoint_summary );
			} );
		write_counted_map(
			w,
			aggregator.source_hulls,
			[&]( const LinearHullSourceKey& key ) { write_linear_hull_source_key( w, key ); },
			[&]( const LinearSourceHullSummary& source_summary ) {
				write_linear_source_hull_summary( w, source_summary );
			} );
		write_counted_map(
			w,
			aggregator.shell_summaries,
			[&]( int shell_weight ) { w.write_i32( shell_weight ); },
			[&]( const LinearCallbackShellSummary& shell_summary ) {
				write_linear_callback_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			aggregator.collected_trails,
			[&]( const LinearCollectedTrailRecord& record ) {
				write_linear_collected_trail_record( w, record );
			} );
		write_counted_container(
			w,
			aggregator.strongest_trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_callback_hull_aggregator(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearCallbackHullAggregator& aggregator )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_bool( r, aggregator.found_any ) &&
			read_bool( r, aggregator.stop_requested ) &&
			r.read_u64( aggregator.collected_trail_count ) &&
			r.read_u64( aggregator.dropped_stored_trail_count ) &&
			read_size( r, aggregator.maximum_stored_trails ) &&
			read_enum_u8( r, aggregator.stored_trail_policy ) &&
			read_long_double( r, aggregator.aggregate_signed_correlation ) &&
			read_long_double( r, aggregator.aggregate_abs_correlation_mass ) &&
			read_long_double( r, aggregator.strongest_trail_signed_correlation ) &&
			r.read_u32( aggregator.strongest_input_branch_a_mask ) &&
			r.read_u32( aggregator.strongest_input_branch_b_mask ) &&
			read_counted_map<std::map<LinearHullEndpointKey, LinearEndpointHullSummary>, LinearHullEndpointKey, LinearEndpointHullSummary>(
				r,
				aggregator.endpoint_hulls,
				[&]( LinearHullEndpointKey& key ) { return read_linear_hull_endpoint_key( r, key ); },
				[&]( LinearEndpointHullSummary& endpoint_summary ) {
					return read_linear_endpoint_hull_summary( r, endpoint_summary );
				} ) &&
			read_counted_map<std::map<LinearHullSourceKey, LinearSourceHullSummary>, LinearHullSourceKey, LinearSourceHullSummary>(
				r,
				aggregator.source_hulls,
				[&]( LinearHullSourceKey& key ) { return read_linear_hull_source_key( r, key ); },
				[&]( LinearSourceHullSummary& source_summary ) {
					return read_linear_source_hull_summary( r, source_summary );
				} ) &&
			read_counted_map<std::map<int, LinearCallbackShellSummary>, int, LinearCallbackShellSummary>(
				r,
				aggregator.shell_summaries,
				[&]( int& shell_weight ) { return r.read_i32( shell_weight ); },
				[&]( LinearCallbackShellSummary& shell_summary ) {
					return read_linear_callback_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				aggregator.collected_trails,
				[&]( LinearCollectedTrailRecord& record ) {
					return read_linear_collected_trail_record( r, record );
				} ) &&
			read_counted_container(
				r,
				aggregator.strongest_trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline bool write_linear_batch_hull_pipeline_checkpoint(
		const std::string& path,
		const LinearBatchHullPipelineCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[&]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::LinearHullBatch ) )
					return false;
				TwilightDream::auto_search_linear::write_config( w, state.base_search_configuration );
				write_enum_u8( w, state.runtime_options_snapshot.collect_cap_mode );
				w.write_i32( state.runtime_options_snapshot.explicit_collect_weight_cap );
				w.write_i32( state.runtime_options_snapshot.collect_weight_window );
				w.write_u64( state.runtime_options_snapshot.maximum_collected_trails );
				write_runtime_controls( w, state.runtime_options_snapshot.runtime_controls );
				write_bool( w, state.enable_combined_source_aggregator );
				write_enum_u8( w, state.stored_trail_policy );
				write_size( w, state.maximum_stored_trails );
				write_counted_container( w, state.jobs, [&]( const LinearBatchJob& job ) { write_linear_batch_job( w, job ); } );
				write_counted_container(
					w,
					state.best_search_seeds,
					[&]( const LinearBatchHullBestSearchSeed& seed ) { write_linear_batch_hull_best_search_seed( w, seed ); } );
				write_u8_vector( w, state.completed_job_flags, [&]( std::uint8_t flag ) { w.write_u8( flag ); } );
				write_counted_container(
					w,
					state.summaries,
					[&]( const LinearBatchHullJobSummary& summary ) { write_linear_batch_hull_job_summary( w, summary ); } );
				write_linear_callback_hull_aggregator( w, state.combined_callback_aggregator );
				return w.ok();
			} );
	}

	static inline bool read_linear_batch_hull_pipeline_checkpoint(
		const std::string& path,
		LinearBatchHullPipelineCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::LinearHullBatch )
			return false;
		if ( !TwilightDream::auto_search_linear::read_config( r, state.base_search_configuration ) )
			return false;
		if ( !read_enum_u8( r, state.runtime_options_snapshot.collect_cap_mode ) )
			return false;
		if ( !r.read_i32( state.runtime_options_snapshot.explicit_collect_weight_cap ) )
			return false;
		if ( !r.read_i32( state.runtime_options_snapshot.collect_weight_window ) )
			return false;
		if ( !r.read_u64( state.runtime_options_snapshot.maximum_collected_trails ) )
			return false;
		if ( !read_runtime_controls( r, state.runtime_options_snapshot.runtime_controls ) )
			return false;
		if ( !read_bool( r, state.enable_combined_source_aggregator ) )
			return false;
		if ( !read_enum_u8( r, state.stored_trail_policy ) )
			return false;
		if ( !read_size( r, state.maximum_stored_trails ) )
			return false;
		if ( !read_counted_container( r, state.jobs, [&]( LinearBatchJob& job ) { return read_linear_batch_job( r, job ); } ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.best_search_seeds,
				 [&]( LinearBatchHullBestSearchSeed& seed ) { return read_linear_batch_hull_best_search_seed( r, seed ); } ) )
			return false;
		if ( !read_u8_vector( r, state.completed_job_flags, [&]( std::uint8_t& flag ) { return r.read_u8( flag ); } ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.summaries,
				 [&]( LinearBatchHullJobSummary& summary ) { return read_linear_batch_hull_job_summary( r, summary ); } ) )
			return false;
		if ( !read_linear_callback_hull_aggregator( r, state.combined_callback_aggregator ) )
			return false;
		state.combined_callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		if ( state.summaries.size() < state.jobs.size() )
			state.summaries.resize( state.jobs.size() );
		return true;
	}

	template <typename OnJobSummaryFn>
	static inline LinearBatchHullPipelineResult run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
		LinearBatchHullPipelineCheckpointState& state,
		int worker_thread_count,
		TwilightDream::auto_search_linear::LinearHullTrailCallback extra_on_trail,
		OnJobSummaryFn&& on_job_summary,
		const std::string& checkpoint_path = {},
		const std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )>& on_checkpoint_written = {} )
	{
		using TwilightDream::runtime_component::run_worker_threads;

		LinearBatchHullPipelineResult result {};
		const int worker_threads_clamped = std::max( 1, worker_thread_count );
		std::atomic<std::size_t> next_index { 0 };
		std::mutex on_job_summary_mutex;
		std::mutex state_mutex;

		state.combined_callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		if ( state.summaries.size() < state.jobs.size() )
			state.summaries.resize( state.jobs.size() );

		run_worker_threads(
			worker_threads_clamped,
			[&]( int ) {
				for ( ;; )
				{
					const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
					if ( job_index >= state.jobs.size() )
						break;
					if ( state.completed_job_flags[ job_index ] != 0u )
						continue;

					const LinearBatchJob& job = state.jobs[ job_index ];
					TwilightDream::auto_search_linear::LinearBestSearchConfiguration job_configuration = state.base_search_configuration;
					job_configuration.round_count = job.rounds;

					LinearCallbackHullAggregator callback_aggregator {};
					callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
					callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );

					TwilightDream::auto_search_linear::LinearStrictHullRuntimeOptions runtime_options =
						make_linear_batch_hull_runtime_options( state, extra_on_trail );
					if ( job_index < state.best_search_seeds.size() )
					{
						const LinearBatchHullBestSearchSeed& seed = state.best_search_seeds[ job_index ];
						runtime_options.best_search_seed_present = seed.present;
						runtime_options.best_search_seeded_upper_bound_weight = seed.seeded_upper_bound_weight;
						runtime_options.best_search_seeded_upper_bound_trail = seed.seeded_upper_bound_trail;
					}

					const LinearHullSourceContext source_context {
						true,
						job_index,
						job.rounds,
						job.output_branch_a_mask,
						job.output_branch_b_mask };

					TwilightDream::auto_search_linear::LinearHullTrailCallback combined_source_callback {};
					if ( state.enable_combined_source_aggregator )
					{
						combined_source_callback =
							[&state, &state_mutex, source_context]( const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view ) {
								std::scoped_lock lk( state_mutex );
								return state.combined_callback_aggregator.on_trail_from_source( source_context, trail_view );
							};
					}

					runtime_options.on_trail =
						compose_linear_hull_trail_callbacks(
							combined_source_callback,
							compose_linear_hull_trail_callbacks( callback_aggregator.make_callback(), std::move( runtime_options.on_trail ) ) );

					const TwilightDream::auto_search_linear::LinearStrictHullRuntimeResult runtime_result =
						TwilightDream::auto_search_linear::run_linear_strict_hull_runtime(
							job.output_branch_a_mask,
							job.output_branch_b_mask,
							job_configuration,
							runtime_options,
							false,
							false );

					const LinearBatchHullJobSummary summary =
						summarize_linear_batch_hull_job(
							job_index,
							job,
							runtime_result,
							callback_aggregator );

					{
						std::scoped_lock lk( state_mutex );
						if ( state.enable_combined_source_aggregator )
							state.combined_callback_aggregator.annotate_source_runtime_result( source_context, runtime_result );
						state.summaries[ job_index ] = summary;
						state.completed_job_flags[ job_index ] = 1u;
						if ( !checkpoint_path.empty() )
						{
							if ( write_linear_batch_hull_pipeline_checkpoint( checkpoint_path, state ) && on_checkpoint_written )
								on_checkpoint_written( state, "strict_hull_job_completed" );
						}
					}

					{
						std::scoped_lock lk( on_job_summary_mutex );
						on_job_summary( summary );
					}
				}
			} );

		result.batch_summary = summarize_linear_batch_hull_run( state.summaries );
		if ( state.enable_combined_source_aggregator )
		{
			result.combined_source_hull.enabled = true;
			result.combined_source_hull.source_count = state.combined_callback_aggregator.source_hulls.size();
			result.combined_source_hull.all_jobs_collected = ( result.batch_summary.rejected_jobs == 0 );
			result.combined_source_hull.all_jobs_exact_within_collect_weight_cap =
				( result.batch_summary.rejected_jobs == 0 && result.batch_summary.partial_jobs == 0 );
			result.combined_source_hull.all_jobs_hard_limit_free = ( result.batch_summary.jobs_hit_hard_limits == 0 );
			result.combined_source_hull.callback_aggregator = state.combined_callback_aggregator;
		}
		return result;
	}

	template <typename OnJobSummaryFn>
	static inline LinearBatchHullPipelineResult run_linear_batch_strict_hull_pipeline(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchHullPipelineOptions& options,
		OnJobSummaryFn&& on_job_summary )
	{
		LinearBatchHullPipelineCheckpointState state =
			make_initial_linear_batch_hull_pipeline_checkpoint_state( jobs, options );
		return run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
			state,
			options.worker_thread_count,
			options.runtime_options_template.on_trail,
			std::forward<OnJobSummaryFn>( on_job_summary ) );
	}

	static inline LinearBatchHullPipelineResult run_linear_batch_strict_hull_pipeline(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchHullPipelineOptions& options )
	{
		return run_linear_batch_strict_hull_pipeline(
			jobs,
			options,
			[]( const LinearBatchHullJobSummary& ) {
			} );
	}
}

#endif
