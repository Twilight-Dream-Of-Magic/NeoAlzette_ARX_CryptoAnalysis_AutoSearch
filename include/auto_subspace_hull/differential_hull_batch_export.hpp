#pragma once
#if !defined( TWILIGHTDREAM_DIFFERENTIAL_HULL_BATCH_EXPORT_HPP )
#define TWILIGHTDREAM_DIFFERENTIAL_HULL_BATCH_EXPORT_HPP

#include "auto_subspace_hull/differential_hull_callback_aggregator.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_subspace_hull/detail/hull_batch_checkpoint_shared.hpp"
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
	struct DifferentialBatchHullJobSummary
	{
		std::size_t job_index = 0;
		DifferentialBatchJob job {};
		bool collected = false;
		bool best_search_executed = false;
		bool best_weight_certified = false;
		TwilightDream::auto_search_differential::SearchWeight best_weight = TwilightDream::auto_search_differential::INFINITE_WEIGHT;
		TwilightDream::auto_search_differential::SearchWeight collect_weight_cap = 0;
		bool exact_within_collect_weight_cap = false;
		TwilightDream::auto_search_differential::StrictCertificationFailureReason strict_runtime_rejection_reason =
			TwilightDream::auto_search_differential::StrictCertificationFailureReason::None;
		TwilightDream::auto_search_differential::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_differential::StrictCertificationFailureReason::None;
		std::uint64_t best_search_nodes_visited = 0;
		std::uint64_t hull_nodes_visited = 0;
		std::uint64_t collected_trails = 0;
		std::size_t	  endpoint_hull_count = 0;
		long double	  global_probability_mass = 0.0L;
		long double	  strongest_trail_probability = 0.0L;
		bool		  hit_any_limit = false;
	};

	struct DifferentialBatchHullRunSummary
	{
		std::vector<DifferentialBatchHullJobSummary> jobs {};
		std::uint64_t								total_best_search_nodes = 0;
		std::uint64_t								total_hull_nodes = 0;
		std::size_t									exact_jobs = 0;
		std::size_t									partial_jobs = 0;
		std::size_t									rejected_jobs = 0;
		std::size_t									jobs_hit_hard_limits = 0;
	};

	static inline DifferentialBatchHullJobSummary summarize_differential_batch_hull_job(
		std::size_t															 job_index,
		const DifferentialBatchJob&											 job,
		const TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeResult& runtime_result,
		const DifferentialCallbackHullAggregator&							 callback_aggregator )
	{
		DifferentialBatchHullJobSummary summary {};
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
		summary.global_probability_mass = callback_aggregator.aggregate_probability;
		summary.strongest_trail_probability = callback_aggregator.strongest_trail_probability;
		summary.hit_any_limit =
			TwilightDream::auto_search_differential::strict_certification_failure_reason_is_hard_limit( runtime_result.strict_runtime_rejection_reason ) ||
			differential_aggregation_hit_any_limit( runtime_result.aggregation_result );
		return summary;
	}

	static inline DifferentialBatchHullRunSummary summarize_differential_batch_hull_run( std::vector<DifferentialBatchHullJobSummary> jobs )
	{
		DifferentialBatchHullRunSummary batch_summary {};
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

	static inline TwilightDream::auto_search_differential::DifferentialHullTrailCallback compose_differential_hull_trail_callbacks(
		TwilightDream::auto_search_differential::DifferentialHullTrailCallback primary,
		TwilightDream::auto_search_differential::DifferentialHullTrailCallback secondary )
	{
		if ( !primary )
			return secondary;
		if ( !secondary )
			return primary;
		return
			[ primary = std::move( primary ), secondary = std::move( secondary ) ]( const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view ) mutable {
				return primary( trail_view ) && secondary( trail_view );
			};
	}

	struct DifferentialBatchHullBestSearchSeed
	{
		bool present = false;
		TwilightDream::auto_search_differential::SearchWeight seeded_upper_bound_weight = TwilightDream::auto_search_differential::INFINITE_WEIGHT;
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> seeded_upper_bound_trail {};
	};

	struct DifferentialBatchHullPipelineOptions
	{
		int																			 worker_thread_count = 1;
		TwilightDream::auto_search_differential::DifferentialBestSearchConfiguration	 base_search_configuration {};
		TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeOptions runtime_options_template {};
		std::vector<DifferentialBatchHullBestSearchSeed>							 best_search_seeds {};
		bool																		 enable_combined_source_aggregator = false;
		OuterSourceNamespace														 source_namespace = OuterSourceNamespace::BatchJob;
		StoredTrailPolicy															 stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		std::size_t																	 maximum_stored_trails = 0;
	};

	struct DifferentialCombinedSourceHullSummary
	{
		bool							 enabled = false;
		std::size_t						 source_count = 0;
		bool							 all_jobs_collected = false;
		bool							 all_jobs_exact_within_collect_weight_cap = false;
		bool							 all_jobs_hard_limit_free = false;
		long double						 aggregate_probability_mass = 0.0L;
		long double						 source_union_total_probability_theorem = 0.0L;
		long double						 source_union_residual_probability_exact = 0.0L;
		long double						 source_union_residual_probability_upper_bound = 0.0L;
		bool							 source_union_probability_residual_certified_zero = false;
		DifferentialCallbackHullAggregator callback_aggregator {};
	};

	struct DifferentialBatchHullPipelineResult
	{
		DifferentialBatchHullRunSummary	   batch_summary {};
		DifferentialCombinedSourceHullSummary combined_source_hull {};
	};

	struct DifferentialBatchFullSourceSpaceSummary
	{
		std::size_t full_source_count = 0;
		long double total_probability_theorem = 0.0L;
		long double residual_probability_exact = 0.0L;
		long double residual_probability_upper_bound = 0.0L;
		bool residual_probability_certified_zero = false;
	};

	static inline DifferentialBatchFullSourceSpaceSummary compute_differential_batch_full_source_space_summary(
		const std::vector<DifferentialBatchJob>& full_source_jobs,
		const DifferentialCombinedSourceHullSummary& combined_source_hull )
	{
		DifferentialBatchFullSourceSpaceSummary summary {};
		summary.full_source_count = full_source_jobs.size();
		summary.total_probability_theorem = static_cast<long double>( summary.full_source_count );
		summary.residual_probability_exact =
			summary.total_probability_theorem - combined_source_hull.aggregate_probability_mass;
		summary.residual_probability_upper_bound =
			( summary.residual_probability_exact < 0.0L ) ? 0.0L : summary.residual_probability_exact;
		summary.residual_probability_certified_zero =
			std::fabsl( summary.residual_probability_upper_bound ) <= 1e-18L;
		return summary;
	}

	struct DifferentialBatchHullRuntimeOptionsSnapshot
	{
		TwilightDream::auto_search_differential::HullCollectCapMode collect_cap_mode =
			TwilightDream::auto_search_differential::HullCollectCapMode::BestWeightPlusWindow;
		TwilightDream::auto_search_differential::SearchWeight explicit_collect_weight_cap = 0;
		TwilightDream::auto_search_differential::SearchWeight collect_weight_window = 0;
		std::uint64_t maximum_collected_trails = 0;
		TwilightDream::auto_search_differential::DifferentialBestSearchRuntimeControls runtime_controls {};
		TwilightDream::auto_search_differential::DifferentialCollectorResidualBoundaryMode residual_boundary_mode =
			TwilightDream::auto_search_differential::DifferentialCollectorResidualBoundaryMode::ContinueResidualSearch;
	};

	struct DifferentialBatchHullPipelineCheckpointState
	{
		TwilightDream::auto_search_differential::DifferentialBestSearchConfiguration base_search_configuration {};
		DifferentialBatchHullRuntimeOptionsSnapshot									runtime_options_snapshot {};
		std::vector<DifferentialBatchJob>											jobs {};
		std::vector<DifferentialBatchHullBestSearchSeed>							best_search_seeds {};
		bool																		enable_combined_source_aggregator = false;
		OuterSourceNamespace														source_namespace = OuterSourceNamespace::BatchJob;
		StoredTrailPolicy															stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		std::size_t																	maximum_stored_trails = 0;
		std::vector<std::uint8_t>													completed_job_flags {};
		std::vector<DifferentialBatchHullJobSummary>								summaries {};
		DifferentialCallbackHullAggregator											combined_callback_aggregator {};
	};

	static inline DifferentialBatchHullRuntimeOptionsSnapshot make_differential_batch_hull_runtime_options_snapshot(
		const TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeOptions& options )
	{
		DifferentialBatchHullRuntimeOptionsSnapshot snapshot {};
		snapshot.collect_cap_mode = options.collect_cap_mode;
		snapshot.explicit_collect_weight_cap = options.explicit_collect_weight_cap;
		snapshot.collect_weight_window = options.collect_weight_window;
		snapshot.maximum_collected_trails = options.maximum_collected_trails;
		snapshot.runtime_controls = options.runtime_controls;
		snapshot.residual_boundary_mode = options.residual_boundary_mode;
		return snapshot;
	}

	static inline TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeOptions make_differential_batch_hull_runtime_options(
		const DifferentialBatchHullPipelineCheckpointState& state,
		TwilightDream::auto_search_differential::DifferentialHullTrailCallback extra_on_trail = {} )
	{
		TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeOptions runtime_options {};
		runtime_options.collect_cap_mode = state.runtime_options_snapshot.collect_cap_mode;
		runtime_options.explicit_collect_weight_cap = state.runtime_options_snapshot.explicit_collect_weight_cap;
		runtime_options.collect_weight_window = state.runtime_options_snapshot.collect_weight_window;
		runtime_options.maximum_collected_trails = state.runtime_options_snapshot.maximum_collected_trails;
		runtime_options.runtime_controls = state.runtime_options_snapshot.runtime_controls;
		runtime_options.residual_boundary_mode = state.runtime_options_snapshot.residual_boundary_mode;
		runtime_options.on_trail = std::move( extra_on_trail );
		runtime_options.runtime_controls.checkpoint_every_seconds = 0;
		runtime_options.best_search_resume_checkpoint_path.clear();
		runtime_options.best_search_history_log = nullptr;
		runtime_options.best_search_binary_checkpoint = nullptr;
		runtime_options.best_search_runtime_log = nullptr;
		return runtime_options;
	}

	static inline DifferentialBatchHullPipelineCheckpointState make_initial_differential_batch_hull_pipeline_checkpoint_state(
		const std::vector<DifferentialBatchJob>& jobs,
		const DifferentialBatchHullPipelineOptions& options )
	{
		DifferentialBatchHullPipelineCheckpointState state {};
		state.base_search_configuration = options.base_search_configuration;
		state.runtime_options_snapshot = make_differential_batch_hull_runtime_options_snapshot( options.runtime_options_template );
		state.jobs = jobs;
		state.best_search_seeds = options.best_search_seeds;
		state.enable_combined_source_aggregator = options.enable_combined_source_aggregator;
		state.source_namespace = options.source_namespace;
		state.stored_trail_policy = options.stored_trail_policy;
		state.maximum_stored_trails = options.maximum_stored_trails;
		state.completed_job_flags.assign( jobs.size(), 0u );
		state.summaries.resize( jobs.size() );
		state.combined_callback_aggregator.set_stored_trail_policy( options.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( options.maximum_stored_trails );
		return state;
	}

	static inline void write_differential_batch_job( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialBatchJob& job )
	{
		w.write_i32( job.rounds );
		w.write_u32( job.initial_branch_a_difference );
		w.write_u32( job.initial_branch_b_difference );
	}

	static inline bool read_differential_batch_job( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialBatchJob& job )
	{
		return
			r.read_i32( job.rounds ) &&
			r.read_u32( job.initial_branch_a_difference ) &&
			r.read_u32( job.initial_branch_b_difference );
	}

	static inline void write_differential_batch_hull_job_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialBatchHullJobSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_size( w, summary.job_index );
		write_differential_batch_job( w, summary.job );
		write_bool( w, summary.collected );
		write_bool( w, summary.best_search_executed );
		write_bool( w, summary.best_weight_certified );
		w.write_u64( summary.best_weight );
		w.write_u64( summary.collect_weight_cap );
		write_bool( w, summary.exact_within_collect_weight_cap );
		write_enum_u8( w, summary.strict_runtime_rejection_reason );
		write_enum_u8( w, summary.exactness_rejection_reason );
		w.write_u64( summary.best_search_nodes_visited );
		w.write_u64( summary.hull_nodes_visited );
		w.write_u64( summary.collected_trails );
		write_size( w, summary.endpoint_hull_count );
		write_long_double( w, summary.global_probability_mass );
		write_long_double( w, summary.strongest_trail_probability );
		write_bool( w, summary.hit_any_limit );
	}

	static inline bool read_differential_batch_hull_job_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialBatchHullJobSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_size( r, summary.job_index ) &&
			read_differential_batch_job( r, summary.job ) &&
			read_bool( r, summary.collected ) &&
			read_bool( r, summary.best_search_executed ) &&
			read_bool( r, summary.best_weight_certified ) &&
			r.read_u64( summary.best_weight ) &&
			r.read_u64( summary.collect_weight_cap ) &&
			read_bool( r, summary.exact_within_collect_weight_cap ) &&
			read_enum_u8( r, summary.strict_runtime_rejection_reason ) &&
			read_enum_u8( r, summary.exactness_rejection_reason ) &&
			r.read_u64( summary.best_search_nodes_visited ) &&
			r.read_u64( summary.hull_nodes_visited ) &&
			r.read_u64( summary.collected_trails ) &&
			read_size( r, summary.endpoint_hull_count ) &&
			read_long_double( r, summary.global_probability_mass ) &&
			read_long_double( r, summary.strongest_trail_probability ) &&
			read_bool( r, summary.hit_any_limit );
	}

	static inline void write_differential_batch_hull_best_search_seed(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialBatchHullBestSearchSeed& seed )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_bool( w, seed.present );
		w.write_u64( seed.seeded_upper_bound_weight );
		write_counted_container(
			w,
			seed.seeded_upper_bound_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_batch_hull_best_search_seed(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialBatchHullBestSearchSeed& seed )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_bool( r, seed.present ) &&
			r.read_u64( seed.seeded_upper_bound_weight ) &&
			read_counted_container(
				r,
				seed.seeded_upper_bound_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_hull_endpoint_key(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialHullEndpointKey& key )
	{
		w.write_u32( key.output_branch_a_difference );
		w.write_u32( key.output_branch_b_difference );
	}

	static inline bool read_differential_hull_endpoint_key(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialHullEndpointKey& key )
	{
		return
			r.read_u32( key.output_branch_a_difference ) &&
			r.read_u32( key.output_branch_b_difference );
	}

	static inline void write_differential_hull_source_key(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialHullSourceKey& key )
	{
		TwilightDream::hull_batch_checkpoint_shared::write_size( w, key.source_index );
		TwilightDream::hull_batch_checkpoint_shared::write_enum_u8( w, key.source_namespace );
	}

	static inline bool read_differential_hull_source_key(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialHullSourceKey& key )
	{
		return
			TwilightDream::hull_batch_checkpoint_shared::read_size( r, key.source_index ) &&
			TwilightDream::hull_batch_checkpoint_shared::read_enum_u8( r, key.source_namespace );
	}

	static inline void write_differential_callback_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialCallbackShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_probability );
		write_long_double( w, summary.strongest_trail_probability );
		w.write_u32( summary.strongest_output_branch_a_difference );
		w.write_u32( summary.strongest_output_branch_b_difference );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_callback_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialCallbackShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_probability ) &&
			read_long_double( r, summary.strongest_trail_probability ) &&
			r.read_u32( summary.strongest_output_branch_a_difference ) &&
			r.read_u32( summary.strongest_output_branch_b_difference ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_endpoint_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialEndpointShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_probability );
		write_long_double( w, summary.strongest_trail_probability );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_endpoint_shell_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialEndpointShellSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_probability ) &&
			read_long_double( r, summary.strongest_trail_probability ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_endpoint_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialEndpointHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( summary.trail_count );
		write_long_double( w, summary.aggregate_probability );
		write_long_double( w, summary.strongest_trail_probability );
		write_counted_map(
			w,
			summary.shell_summaries,
			[&]( TwilightDream::auto_search_differential::SearchWeight shell_weight ) { w.write_u64( shell_weight ); },
			[&]( const DifferentialEndpointShellSummary& shell_summary ) {
				write_differential_endpoint_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_endpoint_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialEndpointHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( summary.trail_count ) &&
			read_long_double( r, summary.aggregate_probability ) &&
			read_long_double( r, summary.strongest_trail_probability ) &&
			read_counted_map<std::map<TwilightDream::auto_search_differential::SearchWeight, DifferentialEndpointShellSummary>, TwilightDream::auto_search_differential::SearchWeight, DifferentialEndpointShellSummary>(
				r,
				summary.shell_summaries,
				[&]( TwilightDream::auto_search_differential::SearchWeight& shell_weight ) { return r.read_u64( shell_weight ); },
				[&]( DifferentialEndpointShellSummary& shell_summary ) {
					return read_differential_endpoint_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_source_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialSourceHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_size( w, summary.source_index );
		write_enum_u8( w, summary.source_namespace );
		w.write_i32( summary.source_rounds );
		w.write_u64( summary.trail_count );
		w.write_u32( summary.source_input_branch_a_difference );
		w.write_u32( summary.source_input_branch_b_difference );
		write_long_double( w, summary.aggregate_probability );
		write_long_double( w, summary.strongest_trail_probability );
		w.write_u32( summary.strongest_output_branch_a_difference );
		w.write_u32( summary.strongest_output_branch_b_difference );
		write_bool( w, summary.runtime_collected );
		write_bool( w, summary.best_search_executed );
		write_bool( w, summary.used_best_weight_reference );
		write_bool( w, summary.best_weight_certified );
		write_bool( w, summary.exact_within_collect_weight_cap );
		w.write_u64( summary.collect_weight_cap );
		w.write_u64( summary.best_search_nodes_visited );
		w.write_u64( summary.hull_nodes_visited );
		write_enum_u8( w, summary.strict_runtime_rejection_reason );
		write_enum_u8( w, summary.exactness_rejection_reason );
		write_counted_map(
			w,
			summary.endpoint_hulls,
			[&]( const DifferentialHullEndpointKey& key ) { write_differential_hull_endpoint_key( w, key ); },
			[&]( const DifferentialEndpointHullSummary& endpoint_summary ) {
				write_differential_endpoint_hull_summary( w, endpoint_summary );
			} );
		write_counted_map(
			w,
			summary.shell_summaries,
			[&]( TwilightDream::auto_search_differential::SearchWeight shell_weight ) { w.write_u64( shell_weight ); },
			[&]( const DifferentialCallbackShellSummary& shell_summary ) {
				write_differential_callback_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			summary.strongest_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_source_hull_summary(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialSourceHullSummary& summary )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_size( r, summary.source_index ) &&
			read_enum_u8( r, summary.source_namespace ) &&
			r.read_i32( summary.source_rounds ) &&
			r.read_u64( summary.trail_count ) &&
			r.read_u32( summary.source_input_branch_a_difference ) &&
			r.read_u32( summary.source_input_branch_b_difference ) &&
			read_long_double( r, summary.aggregate_probability ) &&
			read_long_double( r, summary.strongest_trail_probability ) &&
			r.read_u32( summary.strongest_output_branch_a_difference ) &&
			r.read_u32( summary.strongest_output_branch_b_difference ) &&
			read_bool( r, summary.runtime_collected ) &&
			read_bool( r, summary.best_search_executed ) &&
			read_bool( r, summary.used_best_weight_reference ) &&
			read_bool( r, summary.best_weight_certified ) &&
			read_bool( r, summary.exact_within_collect_weight_cap ) &&
			r.read_u64( summary.collect_weight_cap ) &&
			r.read_u64( summary.best_search_nodes_visited ) &&
			r.read_u64( summary.hull_nodes_visited ) &&
			read_enum_u8( r, summary.strict_runtime_rejection_reason ) &&
			read_enum_u8( r, summary.exactness_rejection_reason ) &&
			read_counted_map<std::map<DifferentialHullEndpointKey, DifferentialEndpointHullSummary>, DifferentialHullEndpointKey, DifferentialEndpointHullSummary>(
				r,
				summary.endpoint_hulls,
				[&]( DifferentialHullEndpointKey& key ) { return read_differential_hull_endpoint_key( r, key ); },
				[&]( DifferentialEndpointHullSummary& endpoint_summary ) {
					return read_differential_endpoint_hull_summary( r, endpoint_summary );
				} ) &&
			read_counted_map<std::map<TwilightDream::auto_search_differential::SearchWeight, DifferentialCallbackShellSummary>, TwilightDream::auto_search_differential::SearchWeight, DifferentialCallbackShellSummary>(
				r,
				summary.shell_summaries,
				[&]( TwilightDream::auto_search_differential::SearchWeight& shell_weight ) { return r.read_u64( shell_weight ); },
				[&]( DifferentialCallbackShellSummary& shell_summary ) {
					return read_differential_callback_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				summary.strongest_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_collected_trail_record(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialCollectedTrailRecord& record )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		w.write_u64( record.arrival_index );
		w.write_u64( record.total_weight );
		w.write_u32( record.output_branch_a_difference );
		w.write_u32( record.output_branch_b_difference );
		write_bool( w, record.source_present );
		write_size( w, record.source_index );
		write_enum_u8( w, record.source_namespace );
		w.write_i32( record.source_rounds );
		w.write_u32( record.source_input_branch_a_difference );
		w.write_u32( record.source_input_branch_b_difference );
		write_long_double( w, record.exact_probability );
		write_counted_container(
			w,
			record.trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_collected_trail_record(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialCollectedTrailRecord& record )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			r.read_u64( record.arrival_index ) &&
			r.read_u64( record.total_weight ) &&
			r.read_u32( record.output_branch_a_difference ) &&
			r.read_u32( record.output_branch_b_difference ) &&
			read_bool( r, record.source_present ) &&
			read_size( r, record.source_index ) &&
			read_enum_u8( r, record.source_namespace ) &&
			r.read_i32( record.source_rounds ) &&
			r.read_u32( record.source_input_branch_a_difference ) &&
			r.read_u32( record.source_input_branch_b_difference ) &&
			read_long_double( r, record.exact_probability ) &&
			read_counted_container(
				r,
				record.trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline void write_differential_callback_hull_aggregator(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialCallbackHullAggregator& aggregator )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_bool( w, aggregator.found_any );
		write_bool( w, aggregator.stop_requested );
		w.write_u64( aggregator.collected_trail_count );
		w.write_u64( aggregator.dropped_stored_trail_count );
		write_size( w, aggregator.maximum_stored_trails );
		write_enum_u8( w, aggregator.stored_trail_policy );
		write_long_double( w, aggregator.aggregate_probability );
		write_long_double( w, aggregator.strongest_trail_probability );
		w.write_u32( aggregator.strongest_output_branch_a_difference );
		w.write_u32( aggregator.strongest_output_branch_b_difference );
		write_counted_map(
			w,
			aggregator.endpoint_hulls,
			[&]( const DifferentialHullEndpointKey& key ) { write_differential_hull_endpoint_key( w, key ); },
			[&]( const DifferentialEndpointHullSummary& endpoint_summary ) {
				write_differential_endpoint_hull_summary( w, endpoint_summary );
			} );
		write_counted_map(
			w,
			aggregator.source_hulls,
			[&]( const DifferentialHullSourceKey& key ) { write_differential_hull_source_key( w, key ); },
			[&]( const DifferentialSourceHullSummary& source_summary ) {
				write_differential_source_hull_summary( w, source_summary );
			} );
		write_counted_map(
			w,
			aggregator.shell_summaries,
			[&]( TwilightDream::auto_search_differential::SearchWeight shell_weight ) { w.write_u64( shell_weight ); },
			[&]( const DifferentialCallbackShellSummary& shell_summary ) {
				write_differential_callback_shell_summary( w, shell_summary );
			} );
		write_counted_container(
			w,
			aggregator.collected_trails,
			[&]( const DifferentialCollectedTrailRecord& record ) {
				write_differential_collected_trail_record( w, record );
			} );
		write_counted_container(
			w,
			aggregator.strongest_trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_callback_hull_aggregator(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialCallbackHullAggregator& aggregator )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_bool( r, aggregator.found_any ) &&
			read_bool( r, aggregator.stop_requested ) &&
			r.read_u64( aggregator.collected_trail_count ) &&
			r.read_u64( aggregator.dropped_stored_trail_count ) &&
			read_size( r, aggregator.maximum_stored_trails ) &&
			read_enum_u8( r, aggregator.stored_trail_policy ) &&
			read_long_double( r, aggregator.aggregate_probability ) &&
			read_long_double( r, aggregator.strongest_trail_probability ) &&
			r.read_u32( aggregator.strongest_output_branch_a_difference ) &&
			r.read_u32( aggregator.strongest_output_branch_b_difference ) &&
			read_counted_map<std::map<DifferentialHullEndpointKey, DifferentialEndpointHullSummary>, DifferentialHullEndpointKey, DifferentialEndpointHullSummary>(
				r,
				aggregator.endpoint_hulls,
				[&]( DifferentialHullEndpointKey& key ) { return read_differential_hull_endpoint_key( r, key ); },
				[&]( DifferentialEndpointHullSummary& endpoint_summary ) {
					return read_differential_endpoint_hull_summary( r, endpoint_summary );
				} ) &&
			read_counted_map<std::map<DifferentialHullSourceKey, DifferentialSourceHullSummary>, DifferentialHullSourceKey, DifferentialSourceHullSummary>(
				r,
				aggregator.source_hulls,
				[&]( DifferentialHullSourceKey& key ) { return read_differential_hull_source_key( r, key ); },
				[&]( DifferentialSourceHullSummary& source_summary ) {
					return read_differential_source_hull_summary( r, source_summary );
				} ) &&
			read_counted_map<std::map<TwilightDream::auto_search_differential::SearchWeight, DifferentialCallbackShellSummary>, TwilightDream::auto_search_differential::SearchWeight, DifferentialCallbackShellSummary>(
				r,
				aggregator.shell_summaries,
				[&]( TwilightDream::auto_search_differential::SearchWeight& shell_weight ) { return r.read_u64( shell_weight ); },
				[&]( DifferentialCallbackShellSummary& shell_summary ) {
					return read_differential_callback_shell_summary( r, shell_summary );
				} ) &&
			read_counted_container(
				r,
				aggregator.collected_trails,
				[&]( DifferentialCollectedTrailRecord& record ) {
					return read_differential_collected_trail_record( r, record );
				} ) &&
			read_counted_container(
				r,
				aggregator.strongest_trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline bool write_differential_hull_pipeline_checkpoint_with_kind(
		const std::string& path,
		const DifferentialBatchHullPipelineCheckpointState& state,
		TwilightDream::auto_search_checkpoint::SearchKind kind )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[&]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, kind ) )
					return false;
				TwilightDream::auto_search_differential::write_config( w, state.base_search_configuration );
				write_enum_u8( w, state.runtime_options_snapshot.collect_cap_mode );
				w.write_u64( state.runtime_options_snapshot.explicit_collect_weight_cap );
				w.write_u64( state.runtime_options_snapshot.collect_weight_window );
				w.write_u64( state.runtime_options_snapshot.maximum_collected_trails );
				write_runtime_controls( w, state.runtime_options_snapshot.runtime_controls );
				write_enum_u8( w, state.runtime_options_snapshot.residual_boundary_mode );
				write_bool( w, state.enable_combined_source_aggregator );
				write_enum_u8( w, state.source_namespace );
				write_enum_u8( w, state.stored_trail_policy );
				write_size( w, state.maximum_stored_trails );
				write_counted_container( w, state.jobs, [&]( const DifferentialBatchJob& job ) { write_differential_batch_job( w, job ); } );
				write_counted_container(
					w,
					state.best_search_seeds,
					[&]( const DifferentialBatchHullBestSearchSeed& seed ) { write_differential_batch_hull_best_search_seed( w, seed ); } );
				write_u8_vector( w, state.completed_job_flags, [&]( std::uint8_t flag ) { w.write_u8( flag ); } );
				write_counted_container(
					w,
					state.summaries,
					[&]( const DifferentialBatchHullJobSummary& summary ) { write_differential_batch_hull_job_summary( w, summary ); } );
				write_differential_callback_hull_aggregator( w, state.combined_callback_aggregator );
				return w.ok();
			} );
	}

	static inline bool write_differential_batch_hull_pipeline_checkpoint(
		const std::string& path,
		const DifferentialBatchHullPipelineCheckpointState& state )
	{
		return write_differential_hull_pipeline_checkpoint_with_kind(
			path,
			state,
			TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullBatch );
	}

	static inline bool write_differential_subspace_hull_pipeline_checkpoint(
		const std::string& path,
		const DifferentialBatchHullPipelineCheckpointState& state )
	{
		return write_differential_hull_pipeline_checkpoint_with_kind(
			path,
			state,
			TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullSubspace );
	}

	static inline bool read_differential_hull_pipeline_checkpoint_with_kind(
		const std::string& path,
		DifferentialBatchHullPipelineCheckpointState& state,
		TwilightDream::auto_search_checkpoint::SearchKind expected_kind )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != expected_kind )
			return false;
		if ( !TwilightDream::auto_search_differential::read_config( r, state.base_search_configuration ) )
			return false;
		if ( !read_enum_u8( r, state.runtime_options_snapshot.collect_cap_mode ) )
			return false;
		if ( !r.read_u64( state.runtime_options_snapshot.explicit_collect_weight_cap ) )
			return false;
		if ( !r.read_u64( state.runtime_options_snapshot.collect_weight_window ) )
			return false;
		if ( !r.read_u64( state.runtime_options_snapshot.maximum_collected_trails ) )
			return false;
		if ( !read_runtime_controls( r, state.runtime_options_snapshot.runtime_controls ) )
			return false;
		if ( !read_enum_u8( r, state.runtime_options_snapshot.residual_boundary_mode ) )
			return false;
		if ( !read_bool( r, state.enable_combined_source_aggregator ) )
			return false;
		if ( !read_enum_u8( r, state.source_namespace ) )
			return false;
		if ( !read_enum_u8( r, state.stored_trail_policy ) )
			return false;
		if ( !read_size( r, state.maximum_stored_trails ) )
			return false;
		if ( !read_counted_container( r, state.jobs, [&]( DifferentialBatchJob& job ) { return read_differential_batch_job( r, job ); } ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.best_search_seeds,
				 [&]( DifferentialBatchHullBestSearchSeed& seed ) { return read_differential_batch_hull_best_search_seed( r, seed ); } ) )
			return false;
		if ( !read_u8_vector( r, state.completed_job_flags, [&]( std::uint8_t& flag ) { return r.read_u8( flag ); } ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.summaries,
				 [&]( DifferentialBatchHullJobSummary& summary ) { return read_differential_batch_hull_job_summary( r, summary ); } ) )
			return false;
		if ( !read_differential_callback_hull_aggregator( r, state.combined_callback_aggregator ) )
			return false;
		state.combined_callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		if ( state.summaries.size() < state.jobs.size() )
			state.summaries.resize( state.jobs.size() );
		return true;
	}

	static inline bool read_differential_batch_hull_pipeline_checkpoint(
		const std::string& path,
		DifferentialBatchHullPipelineCheckpointState& state )
	{
		return read_differential_hull_pipeline_checkpoint_with_kind(
			path,
			state,
			TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullBatch );
	}

	static inline bool read_differential_subspace_hull_pipeline_checkpoint(
		const std::string& path,
		DifferentialBatchHullPipelineCheckpointState& state )
	{
		return read_differential_hull_pipeline_checkpoint_with_kind(
			path,
			state,
			TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullSubspace );
	}

	template <typename OnJobSummaryFn>
	static inline DifferentialBatchHullPipelineResult run_differential_batch_strict_hull_pipeline_from_checkpoint_state(
		DifferentialBatchHullPipelineCheckpointState& state,
		int worker_thread_count,
		TwilightDream::auto_search_differential::DifferentialHullTrailCallback extra_on_trail,
		OnJobSummaryFn&& on_job_summary,
		const std::string& checkpoint_path = {},
		const std::function<void( const DifferentialBatchHullPipelineCheckpointState&, const char* )>& on_checkpoint_written = {},
		const std::function<bool( const std::string&, const DifferentialBatchHullPipelineCheckpointState& )>& checkpoint_writer = {} )
	{
		using TwilightDream::runtime_component::submit_named_async_job;

		DifferentialBatchHullPipelineResult result {};
		const int worker_threads_clamped = std::max( 1, worker_thread_count );
		std::mutex on_job_summary_mutex;
		std::mutex state_mutex;

		state.combined_callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
		state.combined_callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		if ( state.summaries.size() < state.jobs.size() )
			state.summaries.resize( state.jobs.size() );

		struct DifferentialBatchHullAsyncOutcome
		{
			std::size_t job_index = 0;
			DifferentialBatchHullJobSummary summary {};
			TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeResult runtime_result {};
			DifferentialHullSourceContext source_context {};
		};

		struct ActiveDifferentialHullJob
		{
			std::size_t job_index = 0;
			TwilightDream::runtime_component::RuntimeAsyncJobHandle<DifferentialBatchHullAsyncOutcome> handle {};
		};

		std::vector<std::size_t> pending_job_indices {};
		pending_job_indices.reserve( state.jobs.size() );
		for ( std::size_t job_index = 0; job_index < state.jobs.size(); ++job_index )
		{
			if ( state.completed_job_flags[ job_index ] == 0u )
				pending_job_indices.push_back( job_index );
		}

		auto submit_job =
			[&]( std::size_t job_index ) {
				return submit_named_async_job(
					"differential-hull-export-job#" + std::to_string( job_index + 1 ),
					[&, job_index]( TwilightDream::runtime_component::RuntimeTaskContext& ) -> DifferentialBatchHullAsyncOutcome {
						const DifferentialBatchJob& job = state.jobs[ job_index ];
						TwilightDream::auto_search_differential::DifferentialBestSearchConfiguration job_configuration = state.base_search_configuration;
						job_configuration.round_count = job.rounds;

						DifferentialCallbackHullAggregator callback_aggregator {};
						callback_aggregator.set_stored_trail_policy( state.stored_trail_policy );
						callback_aggregator.set_maximum_stored_trails( state.maximum_stored_trails );

						TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeOptions runtime_options =
							make_differential_batch_hull_runtime_options( state, extra_on_trail );
						if ( job_index < state.best_search_seeds.size() )
						{
							const DifferentialBatchHullBestSearchSeed& seed = state.best_search_seeds[ job_index ];
							runtime_options.best_search_seed_present = seed.present;
							runtime_options.best_search_seeded_upper_bound_weight = seed.seeded_upper_bound_weight;
							runtime_options.best_search_seeded_upper_bound_trail = seed.seeded_upper_bound_trail;
						}

						DifferentialBatchHullAsyncOutcome outcome {};
						outcome.job_index = job_index;
						outcome.source_context =
							DifferentialHullSourceContext {
								true,
								job_index,
								state.source_namespace,
								job.rounds,
								job.initial_branch_a_difference,
								job.initial_branch_b_difference };

						TwilightDream::auto_search_differential::DifferentialHullTrailCallback combined_source_callback {};
						if ( state.enable_combined_source_aggregator )
						{
							combined_source_callback =
								[&state, &state_mutex, source_context = outcome.source_context]( const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view ) {
									std::scoped_lock lk( state_mutex );
									return state.combined_callback_aggregator.on_trail_from_source( source_context, trail_view );
								};
						}

						runtime_options.on_trail =
							compose_differential_hull_trail_callbacks(
								combined_source_callback,
								compose_differential_hull_trail_callbacks( callback_aggregator.make_callback(), std::move( runtime_options.on_trail ) ) );

						outcome.runtime_result =
							TwilightDream::auto_search_differential::run_differential_strict_hull_runtime(
								job.initial_branch_a_difference,
								job.initial_branch_b_difference,
								job_configuration,
								runtime_options,
								false,
								false );

						outcome.summary =
							summarize_differential_batch_hull_job(
								job_index,
								job,
								outcome.runtime_result,
								callback_aggregator );
						return outcome;
					} );
			};

		std::vector<ActiveDifferentialHullJob> active_jobs {};
		active_jobs.reserve( std::size_t( worker_threads_clamped ) );
		std::size_t next_submit = 0;
		while ( next_submit < pending_job_indices.size() || !active_jobs.empty() )
		{
			while ( active_jobs.size() < std::size_t( worker_threads_clamped ) && next_submit < pending_job_indices.size() )
			{
				const std::size_t job_index = pending_job_indices[ next_submit++ ];
				active_jobs.push_back( ActiveDifferentialHullJob { job_index, submit_job( job_index ) } );
			}

			bool progressed = false;
			for ( std::size_t i = 0; i < active_jobs.size(); )
			{
				if ( !active_jobs[ i ].handle.done() )
				{
					++i;
					continue;
				}

				DifferentialBatchHullAsyncOutcome outcome = active_jobs[ i ].handle.result();
				{
					std::scoped_lock lk( state_mutex );
					if ( state.enable_combined_source_aggregator )
						state.combined_callback_aggregator.annotate_source_runtime_result( outcome.source_context, outcome.runtime_result );
					state.summaries[ outcome.job_index ] = outcome.summary;
					state.completed_job_flags[ outcome.job_index ] = 1u;
					if ( !checkpoint_path.empty() )
					{
						const bool checkpoint_ok =
							checkpoint_writer ?
								checkpoint_writer( checkpoint_path, state ) :
								write_differential_batch_hull_pipeline_checkpoint( checkpoint_path, state );
						if ( checkpoint_ok && on_checkpoint_written )
							on_checkpoint_written( state, "strict_hull_job_completed" );
					}
				}

				{
					std::scoped_lock lk( on_job_summary_mutex );
					on_job_summary( outcome.summary );
				}

				active_jobs.erase( active_jobs.begin() + static_cast<std::ptrdiff_t>( i ) );
				progressed = true;
			}

			if ( !progressed && !active_jobs.empty() )
				std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}

		result.batch_summary = summarize_differential_batch_hull_run( state.summaries );
		if ( state.enable_combined_source_aggregator )
		{
			result.combined_source_hull.enabled = true;
			result.combined_source_hull.source_count = state.combined_callback_aggregator.source_hulls.size();
			result.combined_source_hull.all_jobs_collected = ( result.batch_summary.rejected_jobs == 0 );
			result.combined_source_hull.all_jobs_exact_within_collect_weight_cap =
				( result.batch_summary.rejected_jobs == 0 && result.batch_summary.partial_jobs == 0 );
			result.combined_source_hull.all_jobs_hard_limit_free = ( result.batch_summary.jobs_hit_hard_limits == 0 );
			result.combined_source_hull.aggregate_probability_mass = state.combined_callback_aggregator.aggregate_probability;
			result.combined_source_hull.source_union_total_probability_theorem =
				compute_differential_callback_hull_source_union_total_probability_theorem( state.combined_callback_aggregator );
			result.combined_source_hull.source_union_residual_probability_exact =
				result.combined_source_hull.source_union_total_probability_theorem -
				result.combined_source_hull.aggregate_probability_mass;
			result.combined_source_hull.source_union_residual_probability_upper_bound =
				( result.combined_source_hull.source_union_residual_probability_exact < 0.0L ) ?
					0.0L :
					result.combined_source_hull.source_union_residual_probability_exact;
			result.combined_source_hull.source_union_probability_residual_certified_zero =
				std::fabsl( result.combined_source_hull.source_union_residual_probability_upper_bound ) <= 1e-18L;
			result.combined_source_hull.callback_aggregator = state.combined_callback_aggregator;
		}
		return result;
	}

	template <typename OnJobSummaryFn>
	static inline DifferentialBatchHullPipelineResult run_differential_batch_strict_hull_pipeline(
		const std::vector<DifferentialBatchJob>& jobs,
		const DifferentialBatchHullPipelineOptions& options,
		OnJobSummaryFn&& on_job_summary )
	{
		DifferentialBatchHullPipelineCheckpointState state =
			make_initial_differential_batch_hull_pipeline_checkpoint_state( jobs, options );
		return run_differential_batch_strict_hull_pipeline_from_checkpoint_state(
			state,
			options.worker_thread_count,
			options.runtime_options_template.on_trail,
			std::forward<OnJobSummaryFn>( on_job_summary ) );
	}

	static inline DifferentialBatchHullPipelineResult run_differential_batch_strict_hull_pipeline(
		const std::vector<DifferentialBatchJob>& jobs,
		const DifferentialBatchHullPipelineOptions& options )
	{
		return run_differential_batch_strict_hull_pipeline(
			jobs,
			options,
			[]( const DifferentialBatchHullJobSummary& ) {
			} );
	}
}

#endif
