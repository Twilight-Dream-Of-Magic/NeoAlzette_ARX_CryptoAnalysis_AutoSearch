#pragma once
#if !defined( TWILIGHTDREAM_DIFFERENTIAL_HULL_CALLBACK_AGGREGATOR_HPP )
#define TWILIGHTDREAM_DIFFERENTIAL_HULL_CALLBACK_AGGREGATOR_HPP

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include "auto_search_frame/hull_growth_common.hpp"
#include "auto_search_frame/test_neoalzette_differential_best_search.hpp"

namespace TwilightDream::hull_callback_aggregator
{
	struct DifferentialCallbackShellSummary
	{
		std::uint64_t trail_count = 0;
		long double	  aggregate_probability = 0.0L;
		long double	  strongest_trail_probability = 0.0L;
		std::uint32_t strongest_output_branch_a_difference = 0;
		std::uint32_t strongest_output_branch_b_difference = 0;
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> strongest_trail {};
	};

	struct DifferentialCollectedTrailRecord
	{
		std::uint64_t arrival_index = 0;
		int			  total_weight = 0;
		std::uint32_t output_branch_a_difference = 0;
		std::uint32_t output_branch_b_difference = 0;
		bool		  source_present = false;
		std::size_t	  source_index = 0;
		int			  source_rounds = 0;
		std::uint32_t source_input_branch_a_difference = 0;
		std::uint32_t source_input_branch_b_difference = 0;
		long double	  exact_probability = 0.0L;
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> trail {};
	};

	struct DifferentialHullEndpointKey
	{
		std::uint32_t output_branch_a_difference = 0;
		std::uint32_t output_branch_b_difference = 0;

		friend bool operator<( const DifferentialHullEndpointKey& lhs, const DifferentialHullEndpointKey& rhs ) noexcept
		{
			if ( lhs.output_branch_a_difference != rhs.output_branch_a_difference )
				return lhs.output_branch_a_difference < rhs.output_branch_a_difference;
			return lhs.output_branch_b_difference < rhs.output_branch_b_difference;
		}
	};

	struct DifferentialEndpointShellSummary
	{
		std::uint64_t trail_count = 0;
		long double	  aggregate_probability = 0.0L;
		long double	  strongest_trail_probability = 0.0L;
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> strongest_trail {};
	};

	struct DifferentialEndpointHullSummary
	{
		std::uint64_t										 trail_count = 0;
		long double											 aggregate_probability = 0.0L;
		long double											 strongest_trail_probability = 0.0L;
		std::map<int, DifferentialEndpointShellSummary>		 shell_summaries {};
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> strongest_trail {};
	};

	struct DifferentialHullSourceContext
	{
		bool		  present = false;
		std::size_t	  source_index = 0;
		int			  source_rounds = 0;
		std::uint32_t source_input_branch_a_difference = 0;
		std::uint32_t source_input_branch_b_difference = 0;
	};

	struct DifferentialHullSourceKey
	{
		std::size_t source_index = 0;

		friend bool operator<( const DifferentialHullSourceKey& lhs, const DifferentialHullSourceKey& rhs ) noexcept
		{
			return lhs.source_index < rhs.source_index;
		}
	};

	struct DifferentialSourceHullSummary
	{
		std::size_t												 source_index = 0;
		int														 source_rounds = 0;
		std::uint64_t											 trail_count = 0;
		std::uint32_t											 source_input_branch_a_difference = 0;
		std::uint32_t											 source_input_branch_b_difference = 0;
		long double												 aggregate_probability = 0.0L;
		long double												 strongest_trail_probability = 0.0L;
		std::uint32_t											 strongest_output_branch_a_difference = 0;
		std::uint32_t											 strongest_output_branch_b_difference = 0;
		bool													 runtime_collected = false;
		bool													 best_search_executed = false;
		bool													 used_best_weight_reference = false;
		bool													 best_weight_certified = false;
		bool													 exact_within_collect_weight_cap = false;
		int														 collect_weight_cap = 0;
		std::uint64_t											 best_search_nodes_visited = 0;
		std::uint64_t											 hull_nodes_visited = 0;
		TwilightDream::auto_search_differential::StrictCertificationFailureReason strict_runtime_rejection_reason =
			TwilightDream::auto_search_differential::StrictCertificationFailureReason::None;
		TwilightDream::auto_search_differential::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_differential::StrictCertificationFailureReason::None;
		std::map<DifferentialHullEndpointKey, DifferentialEndpointHullSummary> endpoint_hulls {};
		std::map<int, DifferentialCallbackShellSummary>		 shell_summaries {};
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> strongest_trail {};
	};

	struct DifferentialCallbackHullAggregator
	{
		bool													found_any = false;
		bool													stop_requested = false;
		std::uint64_t										collected_trail_count = 0;
		std::uint64_t										dropped_stored_trail_count = 0;
		std::size_t											maximum_stored_trails = 0;
		StoredTrailPolicy									stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		long double											aggregate_probability = 0.0L;
		long double											strongest_trail_probability = 0.0L;
		std::uint32_t										strongest_output_branch_a_difference = 0;
		std::uint32_t										strongest_output_branch_b_difference = 0;
		std::map<DifferentialHullEndpointKey, DifferentialEndpointHullSummary> endpoint_hulls {};
		std::map<DifferentialHullSourceKey, DifferentialSourceHullSummary> source_hulls {};
		std::map<int, DifferentialCallbackShellSummary>		shell_summaries {};
		std::vector<DifferentialCollectedTrailRecord>		collected_trails {};
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> strongest_trail {};

		void request_stop() noexcept
		{
			stop_requested = true;
		}

		const DifferentialCallbackShellSummary* find_shell_summary( int shell_weight ) const noexcept
		{
			const auto it = shell_summaries.find( shell_weight );
			return ( it == shell_summaries.end() ) ? nullptr : &it->second;
		}

		const DifferentialEndpointHullSummary* find_endpoint_hull( std::uint32_t output_branch_a_difference, std::uint32_t output_branch_b_difference ) const noexcept
		{
			const auto it = endpoint_hulls.find( DifferentialHullEndpointKey { output_branch_a_difference, output_branch_b_difference } );
			return ( it == endpoint_hulls.end() ) ? nullptr : &it->second;
		}

		const DifferentialSourceHullSummary* find_source_hull( std::size_t source_index ) const noexcept
		{
			const auto it = source_hulls.find( DifferentialHullSourceKey { source_index } );
			return ( it == source_hulls.end() ) ? nullptr : &it->second;
		}

		static bool is_selected_shell( int shell_weight, int shell_start, std::uint64_t shell_count ) noexcept
		{
			return shell_is_selected( shell_weight, shell_start, shell_count );
		}

		void set_stored_trail_policy( StoredTrailPolicy policy )
		{
			stored_trail_policy = policy;
			normalize_stored_trails_for_policy();
		}

		void set_maximum_stored_trails( std::size_t max_trails )
		{
			maximum_stored_trails = max_trails;
			normalize_stored_trails_for_policy();
		}

		TwilightDream::auto_search_differential::DifferentialHullTrailCallback make_callback()
		{
			return [this]( const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view ) {
				return this->on_trail( trail_view );
			};
		}

		TwilightDream::auto_search_differential::DifferentialHullTrailCallback make_callback_for_source( DifferentialHullSourceContext source_context )
		{
			return
				[this, source_context = std::move( source_context )]( const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view ) {
					return this->on_trail_from_source( source_context, trail_view );
				};
		}

		template <typename Fn>
		void for_each_selected_shell( int shell_start, std::uint64_t shell_count, Fn&& fn ) const
		{
			for ( const auto& [ shell_weight, shell_summary ] : shell_summaries )
			{
				if ( !shell_is_selected( shell_weight, shell_start, shell_count ) )
					continue;
				fn( shell_weight, shell_summary );
			}
		}

		bool on_trail( const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view )
		{
			return on_trail_from_source( DifferentialHullSourceContext {}, trail_view );
		}

		DifferentialSourceHullSummary& ensure_source_registered( const DifferentialHullSourceContext& source_context )
		{
			auto& source = source_hulls[ DifferentialHullSourceKey { source_context.source_index } ];
			source.source_index = source_context.source_index;
			source.source_rounds = source_context.source_rounds;
			source.source_input_branch_a_difference = source_context.source_input_branch_a_difference;
			source.source_input_branch_b_difference = source_context.source_input_branch_b_difference;
			return source;
		}

		void annotate_source_runtime_result(
			const DifferentialHullSourceContext& source_context,
			const TwilightDream::auto_search_differential::DifferentialStrictHullRuntimeResult& runtime_result )
		{
			if ( !source_context.present )
				return;

			auto& source = ensure_source_registered( source_context );
			source.runtime_collected = runtime_result.collected;
			source.best_search_executed = runtime_result.best_search_executed;
			source.used_best_weight_reference = runtime_result.used_best_weight_reference;
			source.best_weight_certified = runtime_result.best_search_result.best_weight_certified;
			source.exact_within_collect_weight_cap =
				runtime_result.collected && runtime_result.aggregation_result.exact_within_collect_weight_cap;
			source.collect_weight_cap = runtime_result.collect_weight_cap;
			source.best_search_nodes_visited = runtime_result.best_search_result.nodes_visited;
			source.hull_nodes_visited = runtime_result.aggregation_result.nodes_visited;
			source.strict_runtime_rejection_reason = runtime_result.strict_runtime_rejection_reason;
			source.exactness_rejection_reason = runtime_result.aggregation_result.exactness_rejection_reason;
		}

		bool on_trail_from_source(
			const DifferentialHullSourceContext& source_context,
			const TwilightDream::auto_search_differential::DifferentialHullCollectedTrailView& trail_view )
		{
			if ( stop_requested )
				return false;

			found_any = true;
			++collected_trail_count;
			aggregate_probability += trail_view.exact_probability;

			const DifferentialHullEndpointKey endpoint_key { trail_view.output_branch_a_difference, trail_view.output_branch_b_difference };
			auto& endpoint = endpoint_hulls[ endpoint_key ];
			++endpoint.trail_count;
			endpoint.aggregate_probability += trail_view.exact_probability;
			auto& endpoint_shell = endpoint.shell_summaries[ trail_view.total_weight ];
			++endpoint_shell.trail_count;
			endpoint_shell.aggregate_probability += trail_view.exact_probability;
			if ( should_replace_strongest(
					 endpoint_shell.trail_count,
					 trail_view.exact_probability,
					 endpoint_shell.strongest_trail_probability,
					 endpoint_shell.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				endpoint_shell.strongest_trail_probability = trail_view.exact_probability;
				endpoint_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			if ( should_replace_strongest(
					 endpoint.trail_count,
					 trail_view.exact_probability,
					 endpoint.strongest_trail_probability,
					 endpoint.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				endpoint.strongest_trail_probability = trail_view.exact_probability;
				endpoint.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}

			if ( source_context.present )
			{
				auto& source = ensure_source_registered( source_context );
				++source.trail_count;
				source.aggregate_probability += trail_view.exact_probability;

				auto& source_endpoint = source.endpoint_hulls[ endpoint_key ];
				++source_endpoint.trail_count;
				source_endpoint.aggregate_probability += trail_view.exact_probability;
				auto& source_endpoint_shell = source_endpoint.shell_summaries[ trail_view.total_weight ];
				++source_endpoint_shell.trail_count;
				source_endpoint_shell.aggregate_probability += trail_view.exact_probability;
				if ( should_replace_strongest(
						 source_endpoint_shell.trail_count,
						 trail_view.exact_probability,
						 source_endpoint_shell.strongest_trail_probability,
						 source_endpoint_shell.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_endpoint_shell.strongest_trail_probability = trail_view.exact_probability;
					source_endpoint_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
				if ( should_replace_strongest(
						 source_endpoint.trail_count,
						 trail_view.exact_probability,
						 source_endpoint.strongest_trail_probability,
						 source_endpoint.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_endpoint.strongest_trail_probability = trail_view.exact_probability;
					source_endpoint.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}

				auto& source_shell = source.shell_summaries[ trail_view.total_weight ];
				++source_shell.trail_count;
				source_shell.aggregate_probability += trail_view.exact_probability;
				if ( should_replace_strongest(
						 source_shell.trail_count,
						 trail_view.exact_probability,
						 source_shell.strongest_trail_probability,
						 source_shell.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_shell.strongest_trail_probability = trail_view.exact_probability;
					source_shell.strongest_output_branch_a_difference = trail_view.output_branch_a_difference;
					source_shell.strongest_output_branch_b_difference = trail_view.output_branch_b_difference;
					source_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
				if ( should_replace_strongest(
						 source.trail_count,
						 trail_view.exact_probability,
						 source.strongest_trail_probability,
						 source.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source.strongest_trail_probability = trail_view.exact_probability;
					source.strongest_output_branch_a_difference = trail_view.output_branch_a_difference;
					source.strongest_output_branch_b_difference = trail_view.output_branch_b_difference;
					source.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
			}

			auto& shell = shell_summaries[ trail_view.total_weight ];
			++shell.trail_count;
			shell.aggregate_probability += trail_view.exact_probability;
			if ( should_replace_strongest(
					 shell.trail_count,
					 trail_view.exact_probability,
					 shell.strongest_trail_probability,
					 shell.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				shell.strongest_trail_probability = trail_view.exact_probability;
				shell.strongest_output_branch_a_difference = trail_view.output_branch_a_difference;
				shell.strongest_output_branch_b_difference = trail_view.output_branch_b_difference;
				shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			if ( maximum_stored_trails != 0 )
			{
				DifferentialCollectedTrailRecord record {};
				record.arrival_index = collected_trail_count - 1;
				record.total_weight = trail_view.total_weight;
				record.output_branch_a_difference = trail_view.output_branch_a_difference;
				record.output_branch_b_difference = trail_view.output_branch_b_difference;
				record.source_present = source_context.present;
				record.source_index = source_context.source_index;
				record.source_rounds = source_context.source_rounds;
				record.source_input_branch_a_difference = source_context.source_input_branch_a_difference;
				record.source_input_branch_b_difference = source_context.source_input_branch_b_difference;
				record.exact_probability = trail_view.exact_probability;
				record.trail = trail_view.trail ? *trail_view.trail : std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> {};
				store_trail_record( std::move( record ) );
			}
			if ( should_replace_strongest(
					 collected_trail_count,
					 trail_view.exact_probability,
					 strongest_trail_probability,
					 strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				strongest_trail_probability = trail_view.exact_probability;
				strongest_output_branch_a_difference = trail_view.output_branch_a_difference;
				strongest_output_branch_b_difference = trail_view.output_branch_b_difference;
				strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			return !stop_requested;
		}

		static bool shell_is_selected( int shell_weight, int shell_start, std::uint64_t shell_count ) noexcept
		{
			if ( shell_start < 0 )
				return true;
			if ( shell_weight < shell_start )
				return false;
			if ( shell_count == 0 )
				return true;
			const std::uint64_t offset = static_cast<std::uint64_t>( shell_weight - shell_start );
			return offset < shell_count;
		}

		static long double trail_strength( const DifferentialCollectedTrailRecord& record ) noexcept
		{
			return record.exact_probability;
		}

		static bool should_replace_strongest(
			std::uint64_t trail_count,
			long double candidate_probability,
			long double current_probability,
			bool current_trail_missing,
			bool auxiliary_storage_enabled ) noexcept
		{
			if ( trail_count == 1 || candidate_probability > current_probability )
				return true;
			return auxiliary_storage_enabled && current_trail_missing && candidate_probability == current_probability;
		}

		static void clear_trail_vector( std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord>& trail ) noexcept
		{
			std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> {}.swap( trail );
		}

		bool auxiliary_trail_storage_enabled() const noexcept
		{
			return maximum_stored_trails != 0;
		}

		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> copy_auxiliary_trail(
			const std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord>* trail ) const
		{
			if ( !auxiliary_trail_storage_enabled() || !trail )
				return {};
			return *trail;
		}

		void clear_auxiliary_trail_vectors() noexcept
		{
			clear_trail_vector( strongest_trail );
			for ( auto& [ shell_weight, shell_summary ] : shell_summaries )
			{
				(void)shell_weight;
				clear_trail_vector( shell_summary.strongest_trail );
			}
			for ( auto& [ endpoint_key, endpoint ] : endpoint_hulls )
			{
				(void)endpoint_key;
				clear_trail_vector( endpoint.strongest_trail );
				for ( auto& [ shell_weight, shell_summary ] : endpoint.shell_summaries )
				{
					(void)shell_weight;
					clear_trail_vector( shell_summary.strongest_trail );
				}
			}
			for ( auto& [ source_key, source ] : source_hulls )
			{
				(void)source_key;
				clear_trail_vector( source.strongest_trail );
				for ( auto& [ shell_weight, shell_summary ] : source.shell_summaries )
				{
					(void)shell_weight;
					clear_trail_vector( shell_summary.strongest_trail );
				}
				for ( auto& [ endpoint_key, endpoint ] : source.endpoint_hulls )
				{
					(void)endpoint_key;
					clear_trail_vector( endpoint.strongest_trail );
					for ( auto& [ shell_weight, shell_summary ] : endpoint.shell_summaries )
					{
						(void)shell_weight;
						clear_trail_vector( shell_summary.strongest_trail );
					}
				}
			}
		}

		static bool is_stronger_record( const DifferentialCollectedTrailRecord& lhs, const DifferentialCollectedTrailRecord& rhs ) noexcept
		{
			const long double lhs_strength = trail_strength( lhs );
			const long double rhs_strength = trail_strength( rhs );
			if ( lhs_strength != rhs_strength )
				return lhs_strength > rhs_strength;
			if ( lhs.total_weight != rhs.total_weight )
				return lhs.total_weight < rhs.total_weight;
			return lhs.arrival_index < rhs.arrival_index;
		}

		void sort_stored_trails_by_strength()
		{
			std::stable_sort(
				collected_trails.begin(),
				collected_trails.end(),
				[]( const DifferentialCollectedTrailRecord& lhs, const DifferentialCollectedTrailRecord& rhs ) {
					return is_stronger_record( lhs, rhs );
				} );
		}

		void sort_stored_trails_by_arrival()
		{
			std::stable_sort(
				collected_trails.begin(),
				collected_trails.end(),
				[]( const DifferentialCollectedTrailRecord& lhs, const DifferentialCollectedTrailRecord& rhs ) {
					return lhs.arrival_index < rhs.arrival_index;
				} );
		}

		void normalize_stored_trails_for_policy()
		{
			if ( maximum_stored_trails == 0 )
			{
				dropped_stored_trail_count += static_cast<std::uint64_t>( collected_trails.size() );
				collected_trails.clear();
				clear_auxiliary_trail_vectors();
				return;
			}

			if ( stored_trail_policy == StoredTrailPolicy::Strongest )
				sort_stored_trails_by_strength();
			else
				sort_stored_trails_by_arrival();

			if ( collected_trails.size() > maximum_stored_trails )
			{
				const std::size_t previous_size = collected_trails.size();
				collected_trails.resize( maximum_stored_trails );
				dropped_stored_trail_count += static_cast<std::uint64_t>( previous_size - collected_trails.size() );
			}
		}

		void store_trail_record( DifferentialCollectedTrailRecord&& record )
		{
			if ( maximum_stored_trails == 0 )
				return;

			if ( stored_trail_policy == StoredTrailPolicy::Strongest )
			{
				if ( collected_trails.size() < maximum_stored_trails )
				{
					collected_trails.push_back( std::move( record ) );
					sort_stored_trails_by_strength();
					return;
				}

				sort_stored_trails_by_strength();
				if ( is_stronger_record( record, collected_trails.back() ) )
				{
					collected_trails.back() = std::move( record );
					sort_stored_trails_by_strength();
					++dropped_stored_trail_count;
					return;
				}

				++dropped_stored_trail_count;
				return;
			}

			if ( collected_trails.size() < maximum_stored_trails )
			{
				collected_trails.push_back( std::move( record ) );
				return;
			}
			++dropped_stored_trail_count;
		}
	};

	struct DifferentialGrowthShellRow
	{
		int			  shell_weight = 0;
		std::uint64_t trail_count = 0;
		long double	  shell_probability = 0.0L;
		long double	  cumulative_probability = 0.0L;
		bool		  exact_within_collect_weight_cap = false;
		TwilightDream::auto_search_differential::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_differential::StrictCertificationFailureReason::None;
		bool		  hit_any_limit = false;
	};

	static inline bool differential_aggregation_hit_any_limit( const TwilightDream::auto_search_differential::DifferentialHullAggregationResult& aggregation_result ) noexcept
	{
		return
			aggregation_result.hit_maximum_search_nodes ||
			aggregation_result.hit_time_limit ||
			aggregation_result.hit_collection_limit ||
			aggregation_result.hit_callback_stop;
	}

	static inline DifferentialGrowthShellRow make_differential_growth_shell_row(
		int shell_weight,
		const DifferentialCallbackHullAggregator& callback_aggregator,
		const TwilightDream::auto_search_differential::DifferentialHullAggregationResult& aggregation_result )
	{
		DifferentialGrowthShellRow row {};
		row.shell_weight = shell_weight;
		row.cumulative_probability = callback_aggregator.aggregate_probability;
		row.exact_within_collect_weight_cap = aggregation_result.exact_within_collect_weight_cap;
		row.exactness_rejection_reason = aggregation_result.exactness_rejection_reason;
		row.hit_any_limit = differential_aggregation_hit_any_limit( aggregation_result );
		if ( const auto* shell_summary = callback_aggregator.find_shell_summary( shell_weight ); shell_summary )
		{
			row.trail_count = shell_summary->trail_count;
			row.shell_probability = shell_summary->aggregate_probability;
		}
		return row;
	}

	static inline bool write_differential_shell_summary_csv(
		const std::string& path,
		const DifferentialCallbackHullAggregator& callback_aggregator,
		const TwilightDream::auto_search_differential::DifferentialHullAggregationResult& aggregation_status,
		int shell_start,
		std::uint64_t shell_count,
		bool used_best_weight_reference,
		bool best_weight_certified )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "output_branch_a_difference,output_branch_b_difference,endpoint_trail_count,endpoint_probability_mass,shell_weight,shell_trail_count,shell_probability,cumulative_endpoint_probability,strongest_shell_probability,strongest_shell_step_count,best_weight_reference_used,best_weight_certified,strict_within_collect_weight_cap,exactness_rejection_reason\n";
		for ( const auto& [ endpoint_key, endpoint_summary ] : callback_aggregator.endpoint_hulls )
		{
			long double cumulative_probability = 0.0L;
			for ( const auto& [ weight, shell_summary ] : endpoint_summary.shell_summaries )
			{
				cumulative_probability += shell_summary.aggregate_probability;
				if ( !DifferentialCallbackHullAggregator::is_selected_shell( weight, shell_start, shell_count ) )
					continue;
				out << endpoint_key.output_branch_a_difference
					<< "," << endpoint_key.output_branch_b_difference
					<< "," << endpoint_summary.trail_count
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( endpoint_summary.aggregate_probability )
					<< "," << weight
					<< "," << shell_summary.trail_count
					<< "," << static_cast<double>( shell_summary.aggregate_probability )
					<< "," << static_cast<double>( cumulative_probability )
					<< "," << static_cast<double>( shell_summary.strongest_trail_probability )
					<< "," << shell_summary.strongest_trail.size()
					<< "," << ( used_best_weight_reference ? 1 : 0 )
					<< "," << ( best_weight_certified ? 1 : 0 )
					<< "," << ( aggregation_status.exact_within_collect_weight_cap ? 1 : 0 )
					<< "," << TwilightDream::auto_search_differential::strict_certification_failure_reason_to_string( aggregation_status.exactness_rejection_reason )
					<< "\n";
			}
		}
		return true;
	}

	static inline bool write_differential_growth_csv( const std::string& path, const std::vector<DifferentialGrowthShellRow>& rows )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "shell_weight,trail_count,shell_probability,cumulative_probability,exact_within_collect_weight_cap,exactness_rejection_reason,hit_any_limit\n";
		for ( const auto& row : rows )
		{
			out << row.shell_weight
				<< "," << row.trail_count
				<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( row.shell_probability )
				<< "," << static_cast<double>( row.cumulative_probability )
				<< "," << ( row.exact_within_collect_weight_cap ? 1 : 0 )
				<< "," << TwilightDream::auto_search_differential::strict_certification_failure_reason_to_string( row.exactness_rejection_reason )
				<< "," << ( row.hit_any_limit ? 1 : 0 )
				<< "\n";
		}
		return true;
	}

	static inline bool write_differential_collected_trails_csv(
		const std::string& path,
		const DifferentialCallbackHullAggregator& callback_aggregator )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "trail_index,arrival_index,stored_trail_policy,total_weight,output_branch_a_difference,output_branch_b_difference,exact_probability,trail_step_count,step_index,round_index,round_weight,input_branch_a_difference,input_branch_b_difference,output_branch_a_difference_step,output_branch_b_difference_step\n";
		for ( std::size_t trail_index = 0; trail_index < callback_aggregator.collected_trails.size(); ++trail_index )
		{
			const auto& record = callback_aggregator.collected_trails[ trail_index ];
			if ( record.trail.empty() )
			{
				out << trail_index
					<< "," << record.arrival_index
					<< "," << stored_trail_policy_to_string( callback_aggregator.stored_trail_policy )
					<< "," << record.total_weight
					<< "," << record.output_branch_a_difference
					<< "," << record.output_branch_b_difference
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( record.exact_probability )
					<< "," << 0
					<< "," << -1
					<< "," << -1
					<< "," << 0
					<< "," << 0
					<< "," << 0
					<< "," << 0
					<< "," << 0
					<< "\n";
				continue;
			}

			for ( std::size_t step_index = 0; step_index < record.trail.size(); ++step_index )
			{
				const auto& step = record.trail[ step_index ];
				out << trail_index
					<< "," << record.arrival_index
					<< "," << stored_trail_policy_to_string( callback_aggregator.stored_trail_policy )
					<< "," << record.total_weight
					<< "," << record.output_branch_a_difference
					<< "," << record.output_branch_b_difference
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( record.exact_probability )
					<< "," << record.trail.size()
					<< "," << step_index
					<< "," << step.round_index
					<< "," << step.round_weight
					<< "," << step.input_branch_a_difference
					<< "," << step.input_branch_b_difference
					<< "," << step.output_branch_a_difference
					<< "," << step.output_branch_b_difference
					<< "\n";
			}
		}
		return true;
	}

	static inline bool write_differential_shell_strongest_trails_csv(
		const std::string& path,
		const DifferentialCallbackHullAggregator& callback_aggregator,
		int shell_start,
		std::uint64_t shell_count )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "output_branch_a_difference,output_branch_b_difference,shell_weight,trail_count,strongest_trail_probability,strongest_trail_step_count,step_index,round_index,round_weight,input_branch_a_difference,input_branch_b_difference,output_branch_a_difference_step,output_branch_b_difference_step\n";
		for ( const auto& [ endpoint_key, endpoint_summary ] : callback_aggregator.endpoint_hulls )
		{
			for ( const auto& [ shell_weight, shell_summary ] : endpoint_summary.shell_summaries )
			{
				if ( !DifferentialCallbackHullAggregator::is_selected_shell( shell_weight, shell_start, shell_count ) )
					continue;

				if ( shell_summary.strongest_trail.empty() )
				{
					out << endpoint_key.output_branch_a_difference
						<< "," << endpoint_key.output_branch_b_difference
						<< "," << shell_weight
						<< "," << shell_summary.trail_count
						<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( shell_summary.strongest_trail_probability )
						<< "," << 0
						<< "," << -1
						<< "," << -1
						<< "," << 0
						<< "," << 0
						<< "," << 0
						<< "," << 0
						<< "," << 0
						<< "\n";
					continue;
				}

				for ( std::size_t step_index = 0; step_index < shell_summary.strongest_trail.size(); ++step_index )
				{
					const auto& step = shell_summary.strongest_trail[ step_index ];
					out << endpoint_key.output_branch_a_difference
						<< "," << endpoint_key.output_branch_b_difference
						<< "," << shell_weight
						<< "," << shell_summary.trail_count
						<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( shell_summary.strongest_trail_probability )
						<< "," << shell_summary.strongest_trail.size()
						<< "," << step_index
						<< "," << step.round_index
						<< "," << step.round_weight
						<< "," << step.input_branch_a_difference
						<< "," << step.input_branch_b_difference
						<< "," << step.output_branch_a_difference
						<< "," << step.output_branch_b_difference
						<< "\n";
				}
			}
		}
		return true;
	}
}

#include "auto_search_frame/differential_batch_breadth_deep.hpp"

#endif
