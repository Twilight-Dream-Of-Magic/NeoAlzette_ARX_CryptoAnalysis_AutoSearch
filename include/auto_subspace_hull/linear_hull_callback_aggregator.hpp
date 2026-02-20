#pragma once
#if !defined( TWILIGHTDREAM_LINEAR_HULL_CALLBACK_AGGREGATOR_HPP )
#define TWILIGHTDREAM_LINEAR_HULL_CALLBACK_AGGREGATOR_HPP

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include "auto_subspace_hull/hull_growth_common.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream::hull_callback_aggregator
{
	struct LinearCallbackShellSummary
	{
		std::uint64_t trail_count = 0;
		long double	  aggregate_signed_correlation = 0.0L;
		long double	  aggregate_abs_correlation_mass = 0.0L;
		long double	  strongest_trail_signed_correlation = 0.0L;
		std::uint32_t strongest_input_branch_a_mask = 0;
		std::uint32_t strongest_input_branch_b_mask = 0;
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> strongest_trail {};
	};

	struct LinearCollectedTrailRecord
	{
		std::uint64_t arrival_index = 0;
		TwilightDream::auto_search_linear::SearchWeight total_weight = 0;
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;
		bool		  source_present = false;
		std::size_t	  source_index = 0;
		OuterSourceNamespace source_namespace = OuterSourceNamespace::WrapperFixedSource;
		int			  source_rounds = 0;
		std::uint32_t source_output_branch_a_mask = 0;
		std::uint32_t source_output_branch_b_mask = 0;
		long double	  exact_signed_correlation = 0.0L;
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> trail {};
	};

	struct LinearHullEndpointKey
	{
		std::uint32_t input_branch_a_mask = 0;
		std::uint32_t input_branch_b_mask = 0;

		friend bool operator<( const LinearHullEndpointKey& lhs, const LinearHullEndpointKey& rhs ) noexcept
		{
			if ( lhs.input_branch_a_mask != rhs.input_branch_a_mask )
				return lhs.input_branch_a_mask < rhs.input_branch_a_mask;
			return lhs.input_branch_b_mask < rhs.input_branch_b_mask;
		}
	};

	struct LinearEndpointShellSummary
	{
		std::uint64_t trail_count = 0;
		long double	  aggregate_signed_correlation = 0.0L;
		long double	  aggregate_abs_correlation_mass = 0.0L;
		long double	  strongest_trail_signed_correlation = 0.0L;
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> strongest_trail {};
	};

	struct LinearEndpointHullSummary
	{
		std::uint64_t								 trail_count = 0;
		long double									 aggregate_signed_correlation = 0.0L;
		long double									 aggregate_abs_correlation_mass = 0.0L;
		long double									 strongest_trail_signed_correlation = 0.0L;
		std::map<TwilightDream::auto_search_linear::SearchWeight, LinearEndpointShellSummary> shell_summaries {};
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> strongest_trail {};
	};

	static inline long double linear_endpoint_cancellation_ratio( const LinearEndpointHullSummary& summary ) noexcept
	{
		if ( summary.aggregate_abs_correlation_mass == 0.0L )
			return 0.0L;
		return std::fabsl( summary.aggregate_signed_correlation ) / summary.aggregate_abs_correlation_mass;
	}

	static inline long double compute_linear_endpoint_hull_l2_mass( const LinearEndpointHullSummary& summary ) noexcept
	{
		return summary.aggregate_signed_correlation * summary.aggregate_signed_correlation;
	}

	struct LinearHullSourceContext
	{
		bool		  present = false;
		std::size_t	  source_index = 0;
		OuterSourceNamespace source_namespace = OuterSourceNamespace::WrapperFixedSource;
		int			  source_rounds = 0;
		std::uint32_t source_output_branch_a_mask = 0;
		std::uint32_t source_output_branch_b_mask = 0;
	};

	struct LinearHullSourceKey
	{
		std::size_t source_index = 0;
		OuterSourceNamespace source_namespace = OuterSourceNamespace::WrapperFixedSource;

		friend bool operator<( const LinearHullSourceKey& lhs, const LinearHullSourceKey& rhs ) noexcept
		{
			if ( lhs.source_namespace != rhs.source_namespace )
				return static_cast<std::uint8_t>( lhs.source_namespace ) < static_cast<std::uint8_t>( rhs.source_namespace );
			return lhs.source_index < rhs.source_index;
		}
	};

	struct LinearSourceHullSummary
	{
		std::size_t											 source_index = 0;
		OuterSourceNamespace								 source_namespace = OuterSourceNamespace::WrapperFixedSource;
		int													 source_rounds = 0;
		std::uint64_t										 trail_count = 0;
		std::uint32_t										 source_output_branch_a_mask = 0;
		std::uint32_t										 source_output_branch_b_mask = 0;
		long double											 aggregate_signed_correlation = 0.0L;
		long double											 aggregate_abs_correlation_mass = 0.0L;
		long double											 strongest_trail_signed_correlation = 0.0L;
		std::uint32_t										 strongest_input_branch_a_mask = 0;
		std::uint32_t										 strongest_input_branch_b_mask = 0;
		bool												 runtime_collected = false;
		bool												 best_search_executed = false;
		bool												 used_best_weight_reference = false;
		bool												 best_weight_certified = false;
		bool												 exact_within_collect_weight_cap = false;
		TwilightDream::auto_search_linear::SearchWeight		 collect_weight_cap = 0;
		std::uint64_t										 best_search_nodes_visited = 0;
		std::uint64_t										 hull_nodes_visited = 0;
		TwilightDream::auto_search_linear::StrictCertificationFailureReason strict_runtime_rejection_reason =
			TwilightDream::auto_search_linear::StrictCertificationFailureReason::None;
		TwilightDream::auto_search_linear::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_linear::StrictCertificationFailureReason::None;
		std::map<LinearHullEndpointKey, LinearEndpointHullSummary> endpoint_hulls {};
		std::map<TwilightDream::auto_search_linear::SearchWeight, LinearCallbackShellSummary> shell_summaries {};
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> strongest_trail {};
	};

	struct LinearCallbackHullAggregator
	{
		bool											found_any = false;
		bool											stop_requested = false;
		std::uint64_t								collected_trail_count = 0;
		std::uint64_t								dropped_stored_trail_count = 0;
		std::size_t									maximum_stored_trails = 0;
		StoredTrailPolicy							stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		long double									aggregate_signed_correlation = 0.0L;
		long double									aggregate_abs_correlation_mass = 0.0L;
		long double									strongest_trail_signed_correlation = 0.0L;
		std::uint32_t								strongest_input_branch_a_mask = 0;
		std::uint32_t								strongest_input_branch_b_mask = 0;
		std::map<LinearHullEndpointKey, LinearEndpointHullSummary> endpoint_hulls {};
		std::map<LinearHullSourceKey, LinearSourceHullSummary> source_hulls {};
		std::map<TwilightDream::auto_search_linear::SearchWeight, LinearCallbackShellSummary> shell_summaries {};
		std::vector<LinearCollectedTrailRecord>		collected_trails {};
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> strongest_trail {};

		void request_stop() noexcept
		{
			stop_requested = true;
		}

		const LinearCallbackShellSummary* find_shell_summary( TwilightDream::auto_search_linear::SearchWeight shell_weight ) const noexcept
		{
			const auto it = shell_summaries.find( shell_weight );
			return ( it == shell_summaries.end() ) ? nullptr : &it->second;
		}

		const LinearEndpointHullSummary* find_endpoint_hull( std::uint32_t input_branch_a_mask, std::uint32_t input_branch_b_mask ) const noexcept
		{
			const auto it = endpoint_hulls.find( LinearHullEndpointKey { input_branch_a_mask, input_branch_b_mask } );
			return ( it == endpoint_hulls.end() ) ? nullptr : &it->second;
		}

		const LinearSourceHullSummary* find_source_hull( std::size_t source_index ) const noexcept
		{
			for ( const auto& [ key, value ] : source_hulls )
			{
				if ( key.source_index == source_index )
					return &value;
			}
			return nullptr;
		}

		static bool is_selected_shell( TwilightDream::auto_search_linear::SearchWeight shell_weight, TwilightDream::auto_search_linear::SearchWeight shell_start, std::uint64_t shell_count ) noexcept
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

		TwilightDream::auto_search_linear::LinearHullTrailCallback make_callback()
		{
			return [this]( const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view ) {
				return this->on_trail( trail_view );
			};
		}

		TwilightDream::auto_search_linear::LinearHullTrailCallback make_callback_for_source( LinearHullSourceContext source_context )
		{
			return
				[this, source_context = std::move( source_context )]( const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view ) {
					return this->on_trail_from_source( source_context, trail_view );
				};
		}

		template <typename Fn>
		void for_each_selected_shell( TwilightDream::auto_search_linear::SearchWeight shell_start, std::uint64_t shell_count, Fn&& fn ) const
		{
			for ( const auto& [ shell_weight, shell_summary ] : shell_summaries )
			{
				if ( !shell_is_selected( shell_weight, shell_start, shell_count ) )
					continue;
				fn( shell_weight, shell_summary );
			}
		}

		bool on_trail( const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view )
		{
			return on_trail_from_source( LinearHullSourceContext {}, trail_view );
		}

		LinearSourceHullSummary& ensure_source_registered( const LinearHullSourceContext& source_context )
		{
			auto& source = source_hulls[ LinearHullSourceKey { source_context.source_index, source_context.source_namespace } ];
			source.source_index = source_context.source_index;
			source.source_namespace = source_context.source_namespace;
			source.source_rounds = source_context.source_rounds;
			source.source_output_branch_a_mask = source_context.source_output_branch_a_mask;
			source.source_output_branch_b_mask = source_context.source_output_branch_b_mask;
			return source;
		}

		void annotate_source_runtime_result(
			const LinearHullSourceContext& source_context,
			const TwilightDream::auto_search_linear::LinearStrictHullRuntimeResult& runtime_result )
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

		bool on_trail_from_source( const LinearHullSourceContext& source_context, const TwilightDream::auto_search_linear::LinearHullCollectedTrailView& trail_view )
		{
			if ( stop_requested )
				return false;

			found_any = true;
			++collected_trail_count;
			aggregate_signed_correlation += trail_view.exact_signed_correlation;
			aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );

			const LinearHullEndpointKey endpoint_key { trail_view.input_branch_a_mask, trail_view.input_branch_b_mask };
			auto& endpoint = endpoint_hulls[ endpoint_key ];
			++endpoint.trail_count;
			endpoint.aggregate_signed_correlation += trail_view.exact_signed_correlation;
			endpoint.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
			auto& endpoint_shell = endpoint.shell_summaries[ trail_view.total_weight ];
			++endpoint_shell.trail_count;
			endpoint_shell.aggregate_signed_correlation += trail_view.exact_signed_correlation;
			endpoint_shell.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
			if ( should_replace_strongest(
					 endpoint_shell.trail_count,
					 trail_view.exact_signed_correlation,
					 endpoint_shell.strongest_trail_signed_correlation,
					 endpoint_shell.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				endpoint_shell.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
				endpoint_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			if ( should_replace_strongest(
					 endpoint.trail_count,
					 trail_view.exact_signed_correlation,
					 endpoint.strongest_trail_signed_correlation,
					 endpoint.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				endpoint.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
				endpoint.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}

			if ( source_context.present )
			{
				auto& source = ensure_source_registered( source_context );
				++source.trail_count;
				source.aggregate_signed_correlation += trail_view.exact_signed_correlation;
				source.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );

				auto& source_endpoint = source.endpoint_hulls[ endpoint_key ];
				++source_endpoint.trail_count;
				source_endpoint.aggregate_signed_correlation += trail_view.exact_signed_correlation;
				source_endpoint.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
				auto& source_endpoint_shell = source_endpoint.shell_summaries[ trail_view.total_weight ];
				++source_endpoint_shell.trail_count;
				source_endpoint_shell.aggregate_signed_correlation += trail_view.exact_signed_correlation;
				source_endpoint_shell.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
				if ( should_replace_strongest(
						 source_endpoint_shell.trail_count,
						 trail_view.exact_signed_correlation,
						 source_endpoint_shell.strongest_trail_signed_correlation,
						 source_endpoint_shell.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_endpoint_shell.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
					source_endpoint_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
				if ( should_replace_strongest(
						 source_endpoint.trail_count,
						 trail_view.exact_signed_correlation,
						 source_endpoint.strongest_trail_signed_correlation,
						 source_endpoint.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_endpoint.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
					source_endpoint.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}

				auto& source_shell = source.shell_summaries[ trail_view.total_weight ];
				++source_shell.trail_count;
				source_shell.aggregate_signed_correlation += trail_view.exact_signed_correlation;
				source_shell.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
				if ( should_replace_strongest(
						 source_shell.trail_count,
						 trail_view.exact_signed_correlation,
						 source_shell.strongest_trail_signed_correlation,
						 source_shell.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source_shell.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
					source_shell.strongest_input_branch_a_mask = trail_view.input_branch_a_mask;
					source_shell.strongest_input_branch_b_mask = trail_view.input_branch_b_mask;
					source_shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
				if ( should_replace_strongest(
						 source.trail_count,
						 trail_view.exact_signed_correlation,
						 source.strongest_trail_signed_correlation,
						 source.strongest_trail.empty(),
						 auxiliary_trail_storage_enabled() ) )
				{
					source.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
					source.strongest_input_branch_a_mask = trail_view.input_branch_a_mask;
					source.strongest_input_branch_b_mask = trail_view.input_branch_b_mask;
					source.strongest_trail = copy_auxiliary_trail( trail_view.trail );
				}
			}

			auto& shell = shell_summaries[ trail_view.total_weight ];
			++shell.trail_count;
			shell.aggregate_signed_correlation += trail_view.exact_signed_correlation;
			shell.aggregate_abs_correlation_mass += std::fabsl( trail_view.exact_signed_correlation );
			if ( should_replace_strongest(
					 shell.trail_count,
					 trail_view.exact_signed_correlation,
					 shell.strongest_trail_signed_correlation,
					 shell.strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				shell.strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
				shell.strongest_input_branch_a_mask = trail_view.input_branch_a_mask;
				shell.strongest_input_branch_b_mask = trail_view.input_branch_b_mask;
				shell.strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			if ( maximum_stored_trails != 0 )
			{
				LinearCollectedTrailRecord record {};
				record.arrival_index = collected_trail_count - 1;
				record.total_weight = trail_view.total_weight;
				record.input_branch_a_mask = trail_view.input_branch_a_mask;
				record.input_branch_b_mask = trail_view.input_branch_b_mask;
				record.source_present = source_context.present;
				record.source_index = source_context.source_index;
				record.source_namespace = source_context.source_namespace;
				record.source_rounds = source_context.source_rounds;
				record.source_output_branch_a_mask = source_context.source_output_branch_a_mask;
				record.source_output_branch_b_mask = source_context.source_output_branch_b_mask;
				record.exact_signed_correlation = trail_view.exact_signed_correlation;
				record.trail = trail_view.trail ? *trail_view.trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
				store_trail_record( std::move( record ) );
			}
			if ( should_replace_strongest(
					 collected_trail_count,
					 trail_view.exact_signed_correlation,
					 strongest_trail_signed_correlation,
					 strongest_trail.empty(),
					 auxiliary_trail_storage_enabled() ) )
			{
				strongest_trail_signed_correlation = trail_view.exact_signed_correlation;
				strongest_input_branch_a_mask = trail_view.input_branch_a_mask;
				strongest_input_branch_b_mask = trail_view.input_branch_b_mask;
				strongest_trail = copy_auxiliary_trail( trail_view.trail );
			}
			return !stop_requested;
		}

		static bool shell_is_selected( TwilightDream::auto_search_linear::SearchWeight shell_weight, TwilightDream::auto_search_linear::SearchWeight shell_start, std::uint64_t shell_count ) noexcept
		{
			if ( shell_start >= TwilightDream::auto_search_linear::INFINITE_WEIGHT )
				return true;
			if ( shell_weight < shell_start )
				return false;
			if ( shell_count == 0 )
				return true;
			const std::uint64_t offset = static_cast<std::uint64_t>( shell_weight - shell_start );
			return offset < shell_count;
		}

		static long double trail_strength( const LinearCollectedTrailRecord& record ) noexcept
		{
			return std::fabsl( record.exact_signed_correlation );
		}

		static bool should_replace_strongest(
			std::uint64_t trail_count,
			long double candidate_signed_correlation,
			long double current_signed_correlation,
			bool current_trail_missing,
			bool auxiliary_storage_enabled ) noexcept
		{
			if ( trail_count == 1 || std::fabsl( candidate_signed_correlation ) > std::fabsl( current_signed_correlation ) )
				return true;
			return auxiliary_storage_enabled && current_trail_missing &&
				std::fabsl( candidate_signed_correlation ) == std::fabsl( current_signed_correlation );
		}

		static void clear_trail_vector( std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord>& trail ) noexcept
		{
			std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {}.swap( trail );
		}

		bool auxiliary_trail_storage_enabled() const noexcept
		{
			return maximum_stored_trails != 0;
		}

		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> copy_auxiliary_trail(
			const std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord>* trail ) const
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

		static bool is_stronger_record( const LinearCollectedTrailRecord& lhs, const LinearCollectedTrailRecord& rhs ) noexcept
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
				[]( const LinearCollectedTrailRecord& lhs, const LinearCollectedTrailRecord& rhs ) {
					return is_stronger_record( lhs, rhs );
				} );
		}

		void sort_stored_trails_by_arrival()
		{
			std::stable_sort(
				collected_trails.begin(),
				collected_trails.end(),
				[]( const LinearCollectedTrailRecord& lhs, const LinearCollectedTrailRecord& rhs ) {
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

		void store_trail_record( LinearCollectedTrailRecord&& record )
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

	static inline long double compute_linear_source_hull_endpoint_l2_mass( const LinearSourceHullSummary& source_summary ) noexcept
	{
		long double endpoint_l2_mass = 0.0L;
		for ( const auto& [ endpoint_key, endpoint_summary ] : source_summary.endpoint_hulls )
		{
			(void)endpoint_key;
			endpoint_l2_mass += compute_linear_endpoint_hull_l2_mass( endpoint_summary );
		}
		return endpoint_l2_mass;
	}

	static inline long double compute_linear_callback_hull_source_union_endpoint_l2_mass( const LinearCallbackHullAggregator& callback_aggregator ) noexcept
	{
		long double endpoint_l2_mass = 0.0L;
		for ( const auto& [ source_key, source_summary ] : callback_aggregator.source_hulls )
		{
			(void)source_key;
			endpoint_l2_mass += compute_linear_source_hull_endpoint_l2_mass( source_summary );
		}
		return endpoint_l2_mass;
	}

	static inline std::pair<std::uint32_t, std::uint32_t> compute_neoalzette_zero_state_after_rounds( int rounds ) noexcept
	{
		std::uint32_t branch_a = 0u;
		std::uint32_t branch_b = 0u;
		for ( int round_index = 0; round_index < std::max( 0, rounds ); ++round_index )
			TwilightDream::NeoAlzetteCore::forward( branch_a, branch_b );
		return { branch_a, branch_b };
	}

	static inline long double compute_linear_source_total_signed_correlation_theorem(
		int rounds,
		std::uint32_t output_branch_a_mask,
		std::uint32_t output_branch_b_mask ) noexcept
	{
		const auto [ zero_output_branch_a, zero_output_branch_b ] = compute_neoalzette_zero_state_after_rounds( rounds );
		const unsigned parity =
			static_cast<unsigned>(
				( std::popcount( output_branch_a_mask & zero_output_branch_a ) +
				  std::popcount( output_branch_b_mask & zero_output_branch_b ) ) &
				1u );
		return ( parity == 0u ) ? 1.0L : -1.0L;
	}

	static inline long double compute_linear_callback_hull_source_union_total_signed_correlation_theorem( const LinearCallbackHullAggregator& callback_aggregator ) noexcept
	{
		long double total_signed_correlation_theorem = 0.0L;
		for ( const auto& [ source_key, source_summary ] : callback_aggregator.source_hulls )
		{
			(void)source_key;
			total_signed_correlation_theorem +=
				compute_linear_source_total_signed_correlation_theorem(
					source_summary.source_rounds,
					source_summary.source_output_branch_a_mask,
					source_summary.source_output_branch_b_mask );
		}
		return total_signed_correlation_theorem;
	}

	static inline bool linear_strongest_signed_correlation_is_preferred(
		long double candidate_signed_correlation,
		long double current_signed_correlation,
		bool current_trail_missing,
		bool candidate_trail_present,
		bool auxiliary_storage_enabled ) noexcept
	{
		const long double candidate_strength = std::fabsl( candidate_signed_correlation );
		const long double current_strength = std::fabsl( current_signed_correlation );
		if ( candidate_strength != current_strength )
			return candidate_strength > current_strength;
		return auxiliary_storage_enabled && current_trail_missing && candidate_trail_present;
	}

	static inline void merge_linear_callback_shell_summary(
		LinearCallbackShellSummary& dst,
		const LinearCallbackShellSummary& src,
		bool auxiliary_storage_enabled )
	{
		if ( src.trail_count != 0 &&
			 linear_strongest_signed_correlation_is_preferred(
				 src.strongest_trail_signed_correlation,
				 dst.strongest_trail_signed_correlation,
				 dst.strongest_trail.empty(),
				 !src.strongest_trail.empty(),
				 auxiliary_storage_enabled ) )
		{
			dst.strongest_trail_signed_correlation = src.strongest_trail_signed_correlation;
			dst.strongest_input_branch_a_mask = src.strongest_input_branch_a_mask;
			dst.strongest_input_branch_b_mask = src.strongest_input_branch_b_mask;
			dst.strongest_trail = auxiliary_storage_enabled ? src.strongest_trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
		}
		dst.trail_count += src.trail_count;
		dst.aggregate_signed_correlation += src.aggregate_signed_correlation;
		dst.aggregate_abs_correlation_mass += src.aggregate_abs_correlation_mass;
	}

	static inline void merge_linear_endpoint_shell_summary(
		LinearEndpointShellSummary& dst,
		const LinearEndpointShellSummary& src,
		bool auxiliary_storage_enabled )
	{
		if ( src.trail_count != 0 &&
			 linear_strongest_signed_correlation_is_preferred(
				 src.strongest_trail_signed_correlation,
				 dst.strongest_trail_signed_correlation,
				 dst.strongest_trail.empty(),
				 !src.strongest_trail.empty(),
				 auxiliary_storage_enabled ) )
		{
			dst.strongest_trail_signed_correlation = src.strongest_trail_signed_correlation;
			dst.strongest_trail = auxiliary_storage_enabled ? src.strongest_trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
		}
		dst.trail_count += src.trail_count;
		dst.aggregate_signed_correlation += src.aggregate_signed_correlation;
		dst.aggregate_abs_correlation_mass += src.aggregate_abs_correlation_mass;
	}

	static inline void merge_linear_endpoint_hull_summary(
		LinearEndpointHullSummary& dst,
		const LinearEndpointHullSummary& src,
		bool auxiliary_storage_enabled )
	{
		for ( const auto& [ shell_weight, src_shell ] : src.shell_summaries )
			merge_linear_endpoint_shell_summary( dst.shell_summaries[ shell_weight ], src_shell, auxiliary_storage_enabled );

		if ( src.trail_count != 0 &&
			 linear_strongest_signed_correlation_is_preferred(
				 src.strongest_trail_signed_correlation,
				 dst.strongest_trail_signed_correlation,
				 dst.strongest_trail.empty(),
				 !src.strongest_trail.empty(),
				 auxiliary_storage_enabled ) )
		{
			dst.strongest_trail_signed_correlation = src.strongest_trail_signed_correlation;
			dst.strongest_trail = auxiliary_storage_enabled ? src.strongest_trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
		}
		dst.trail_count += src.trail_count;
		dst.aggregate_signed_correlation += src.aggregate_signed_correlation;
		dst.aggregate_abs_correlation_mass += src.aggregate_abs_correlation_mass;
	}

	static inline void merge_linear_source_hull_summary(
		LinearSourceHullSummary& dst,
		const LinearSourceHullSummary& src,
		std::size_t remapped_source_index,
		bool auxiliary_storage_enabled )
	{
		dst.source_index = remapped_source_index;
		dst.source_namespace = src.source_namespace;
		dst.source_rounds = src.source_rounds;
		dst.source_output_branch_a_mask = src.source_output_branch_a_mask;
		dst.source_output_branch_b_mask = src.source_output_branch_b_mask;
		dst.runtime_collected = dst.runtime_collected || src.runtime_collected;
		dst.best_search_executed = dst.best_search_executed || src.best_search_executed;
		dst.used_best_weight_reference = dst.used_best_weight_reference || src.used_best_weight_reference;
		dst.best_weight_certified = dst.best_weight_certified || src.best_weight_certified;
		dst.exact_within_collect_weight_cap = dst.exact_within_collect_weight_cap || src.exact_within_collect_weight_cap;
		if ( src.runtime_collected )
			dst.collect_weight_cap = src.collect_weight_cap;
		dst.best_search_nodes_visited += src.best_search_nodes_visited;
		dst.hull_nodes_visited += src.hull_nodes_visited;
		if ( dst.strict_runtime_rejection_reason == TwilightDream::auto_search_linear::StrictCertificationFailureReason::None )
			dst.strict_runtime_rejection_reason = src.strict_runtime_rejection_reason;
		if ( dst.exactness_rejection_reason == TwilightDream::auto_search_linear::StrictCertificationFailureReason::None )
			dst.exactness_rejection_reason = src.exactness_rejection_reason;

		for ( const auto& [ shell_weight, src_shell ] : src.shell_summaries )
			merge_linear_callback_shell_summary( dst.shell_summaries[ shell_weight ], src_shell, auxiliary_storage_enabled );
		for ( const auto& [ endpoint_key, src_endpoint ] : src.endpoint_hulls )
			merge_linear_endpoint_hull_summary( dst.endpoint_hulls[ endpoint_key ], src_endpoint, auxiliary_storage_enabled );

		if ( src.trail_count != 0 &&
			 linear_strongest_signed_correlation_is_preferred(
				 src.strongest_trail_signed_correlation,
				 dst.strongest_trail_signed_correlation,
				 dst.strongest_trail.empty(),
				 !src.strongest_trail.empty(),
				 auxiliary_storage_enabled ) )
		{
			dst.strongest_trail_signed_correlation = src.strongest_trail_signed_correlation;
			dst.strongest_input_branch_a_mask = src.strongest_input_branch_a_mask;
			dst.strongest_input_branch_b_mask = src.strongest_input_branch_b_mask;
			dst.strongest_trail = auxiliary_storage_enabled ? src.strongest_trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
		}
		dst.trail_count += src.trail_count;
		dst.aggregate_signed_correlation += src.aggregate_signed_correlation;
		dst.aggregate_abs_correlation_mass += src.aggregate_abs_correlation_mass;
	}

	template <typename RemapSourceIndexFn>
	static inline void merge_linear_callback_hull_aggregator(
		LinearCallbackHullAggregator& dst,
		const LinearCallbackHullAggregator& src,
		RemapSourceIndexFn&& remap_source_index )
	{
		const bool auxiliary_storage_enabled = dst.auxiliary_trail_storage_enabled();
		dst.found_any = dst.found_any || src.found_any;
		dst.stop_requested = dst.stop_requested || src.stop_requested;
		dst.dropped_stored_trail_count += src.dropped_stored_trail_count;

		for ( const auto& [ shell_weight, src_shell ] : src.shell_summaries )
			merge_linear_callback_shell_summary( dst.shell_summaries[ shell_weight ], src_shell, auxiliary_storage_enabled );
		for ( const auto& [ endpoint_key, src_endpoint ] : src.endpoint_hulls )
			merge_linear_endpoint_hull_summary( dst.endpoint_hulls[ endpoint_key ], src_endpoint, auxiliary_storage_enabled );
		for ( const auto& [ source_key, src_source ] : src.source_hulls )
		{
			const std::size_t remapped_index = remap_source_index( source_key.source_index );
			auto& dst_source = dst.source_hulls[ LinearHullSourceKey { remapped_index, source_key.source_namespace } ];
			merge_linear_source_hull_summary( dst_source, src_source, remapped_index, auxiliary_storage_enabled );
		}

		if ( src.collected_trail_count != 0 &&
			 linear_strongest_signed_correlation_is_preferred(
				 src.strongest_trail_signed_correlation,
				 dst.strongest_trail_signed_correlation,
				 dst.strongest_trail.empty(),
				 !src.strongest_trail.empty(),
				 auxiliary_storage_enabled ) )
		{
			dst.strongest_trail_signed_correlation = src.strongest_trail_signed_correlation;
			dst.strongest_input_branch_a_mask = src.strongest_input_branch_a_mask;
			dst.strongest_input_branch_b_mask = src.strongest_input_branch_b_mask;
			dst.strongest_trail = auxiliary_storage_enabled ? src.strongest_trail : std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> {};
		}

		const std::uint64_t arrival_index_base = dst.collected_trail_count;
		dst.collected_trail_count += src.collected_trail_count;
		dst.aggregate_signed_correlation += src.aggregate_signed_correlation;
		dst.aggregate_abs_correlation_mass += src.aggregate_abs_correlation_mass;

		for ( const auto& src_record : src.collected_trails )
		{
			LinearCollectedTrailRecord record = src_record;
			record.arrival_index += arrival_index_base;
			if ( record.source_present )
				record.source_index = remap_source_index( record.source_index );
			if ( !auxiliary_storage_enabled )
				record.trail.clear();
			dst.store_trail_record( std::move( record ) );
		}
	}

	static inline void merge_linear_callback_hull_aggregator(
		LinearCallbackHullAggregator& dst,
		const LinearCallbackHullAggregator& src )
	{
		merge_linear_callback_hull_aggregator(
			dst,
			src,
			[]( std::size_t source_index ) {
				return source_index;
			} );
	}

	struct LinearGrowthShellRow
	{
		TwilightDream::auto_search_linear::SearchWeight shell_weight = 0;
		std::uint64_t trail_count = 0;
		long double	  shell_signed_correlation = 0.0L;
		long double	  shell_abs_correlation_mass = 0.0L;
		long double	  cumulative_signed_correlation = 0.0L;
		long double	  cumulative_abs_correlation_mass = 0.0L;
		bool		  exact_within_collect_weight_cap = false;
		TwilightDream::auto_search_linear::StrictCertificationFailureReason exactness_rejection_reason =
			TwilightDream::auto_search_linear::StrictCertificationFailureReason::None;
		bool		  hit_any_limit = false;
	};

	static inline bool linear_aggregation_hit_any_limit( const TwilightDream::auto_search_linear::LinearHullAggregationResult& aggregation_result ) noexcept
	{
		return
			aggregation_result.hit_maximum_search_nodes ||
			aggregation_result.hit_time_limit ||
			aggregation_result.hit_collection_limit ||
			aggregation_result.hit_callback_stop;
	}

	static inline LinearGrowthShellRow make_linear_growth_shell_row(
		TwilightDream::auto_search_linear::SearchWeight shell_weight,
		const LinearCallbackHullAggregator& callback_aggregator,
		const TwilightDream::auto_search_linear::LinearHullAggregationResult& aggregation_result )
	{
		LinearGrowthShellRow row {};
		row.shell_weight = shell_weight;
		row.cumulative_signed_correlation = callback_aggregator.aggregate_signed_correlation;
		row.cumulative_abs_correlation_mass = callback_aggregator.aggregate_abs_correlation_mass;
		row.exact_within_collect_weight_cap = aggregation_result.exact_within_collect_weight_cap;
		row.exactness_rejection_reason = aggregation_result.exactness_rejection_reason;
		row.hit_any_limit = linear_aggregation_hit_any_limit( aggregation_result );
		if ( const auto* shell_summary = callback_aggregator.find_shell_summary( shell_weight ); shell_summary )
		{
			row.trail_count = shell_summary->trail_count;
			row.shell_signed_correlation = shell_summary->aggregate_signed_correlation;
			row.shell_abs_correlation_mass = shell_summary->aggregate_abs_correlation_mass;
		}
		return row;
	}

	static inline bool write_linear_shell_summary_csv(
		const std::string& path,
		const LinearCallbackHullAggregator& callback_aggregator,
		const TwilightDream::auto_search_linear::LinearHullAggregationResult& aggregation_status,
		TwilightDream::auto_search_linear::SearchWeight shell_start,
		std::uint64_t shell_count,
		bool used_best_weight_reference,
		bool best_weight_certified )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "input_branch_a_mask,input_branch_b_mask,endpoint_trail_count,endpoint_signed_correlation_sum,endpoint_abs_correlation_mass,endpoint_cancellation_ratio,shell_weight,shell_trail_count,shell_signed_correlation,shell_abs_correlation_mass,cumulative_endpoint_signed_correlation,cumulative_endpoint_abs_correlation_mass,strongest_shell_signed_correlation,strongest_shell_abs_correlation,strongest_shell_step_count,best_weight_reference_used,best_weight_certified,strict_within_collect_weight_cap,exactness_rejection_reason\n";
		for ( const auto& [ endpoint_key, endpoint_summary ] : callback_aggregator.endpoint_hulls )
		{
			long double cumulative_signed_correlation = 0.0L;
			long double cumulative_abs_correlation_mass = 0.0L;
			for ( const auto& [ weight, shell_summary ] : endpoint_summary.shell_summaries )
			{
				cumulative_signed_correlation += shell_summary.aggregate_signed_correlation;
				cumulative_abs_correlation_mass += shell_summary.aggregate_abs_correlation_mass;
				if ( !LinearCallbackHullAggregator::is_selected_shell( weight, shell_start, shell_count ) )
					continue;
				out << endpoint_key.input_branch_a_mask
					<< "," << endpoint_key.input_branch_b_mask
					<< "," << endpoint_summary.trail_count
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( endpoint_summary.aggregate_signed_correlation )
					<< "," << static_cast<double>( endpoint_summary.aggregate_abs_correlation_mass )
					<< "," << static_cast<double>( linear_endpoint_cancellation_ratio( endpoint_summary ) )
					<< "," << weight
					<< "," << shell_summary.trail_count
					<< "," << static_cast<double>( shell_summary.aggregate_signed_correlation )
					<< "," << static_cast<double>( shell_summary.aggregate_abs_correlation_mass )
					<< "," << static_cast<double>( cumulative_signed_correlation )
					<< "," << static_cast<double>( cumulative_abs_correlation_mass )
					<< "," << static_cast<double>( shell_summary.strongest_trail_signed_correlation )
					<< "," << static_cast<double>( std::fabsl( shell_summary.strongest_trail_signed_correlation ) )
					<< "," << shell_summary.strongest_trail.size()
					<< "," << ( used_best_weight_reference ? 1 : 0 )
					<< "," << ( best_weight_certified ? 1 : 0 )
					<< "," << ( aggregation_status.exact_within_collect_weight_cap ? 1 : 0 )
					<< "," << TwilightDream::auto_search_linear::strict_certification_failure_reason_to_string( aggregation_status.exactness_rejection_reason )
					<< "\n";
			}
		}
		return true;
	}

	static inline bool write_linear_growth_csv( const std::string& path, const std::vector<LinearGrowthShellRow>& rows )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "shell_weight,trail_count,shell_signed_correlation,shell_abs_correlation_mass,cumulative_signed_correlation,cumulative_abs_correlation_mass,exact_within_collect_weight_cap,exactness_rejection_reason,hit_any_limit\n";
		for ( const auto& row : rows )
		{
			out << row.shell_weight
				<< "," << row.trail_count
				<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( row.shell_signed_correlation )
				<< "," << static_cast<double>( row.shell_abs_correlation_mass )
				<< "," << static_cast<double>( row.cumulative_signed_correlation )
				<< "," << static_cast<double>( row.cumulative_abs_correlation_mass )
				<< "," << ( row.exact_within_collect_weight_cap ? 1 : 0 )
				<< "," << TwilightDream::auto_search_linear::strict_certification_failure_reason_to_string( row.exactness_rejection_reason )
				<< "," << ( row.hit_any_limit ? 1 : 0 )
				<< "\n";
		}
		return true;
	}

	static inline bool write_linear_collected_trails_csv(
		const std::string& path,
		const LinearCallbackHullAggregator& callback_aggregator )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "trail_index,arrival_index,stored_trail_policy,total_weight,input_branch_a_mask,input_branch_b_mask,exact_signed_correlation,trail_step_count,step_index,round_index,round_weight,output_branch_a_mask,output_branch_b_mask,input_branch_a_mask_step,input_branch_b_mask_step\n";
		for ( std::size_t trail_index = 0; trail_index < callback_aggregator.collected_trails.size(); ++trail_index )
		{
			const auto& record = callback_aggregator.collected_trails[ trail_index ];
			if ( record.trail.empty() )
			{
				out << trail_index
					<< "," << record.arrival_index
					<< "," << stored_trail_policy_to_string( callback_aggregator.stored_trail_policy )
					<< "," << record.total_weight
					<< "," << record.input_branch_a_mask
					<< "," << record.input_branch_b_mask
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( record.exact_signed_correlation )
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
					<< "," << record.input_branch_a_mask
					<< "," << record.input_branch_b_mask
					<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( record.exact_signed_correlation )
					<< "," << record.trail.size()
					<< "," << step_index
					<< "," << step.round_index
					<< "," << step.round_weight
					<< "," << step.output_branch_a_mask
					<< "," << step.output_branch_b_mask
					<< "," << step.input_branch_a_mask
					<< "," << step.input_branch_b_mask
					<< "\n";
			}
		}
		return true;
	}

	static inline bool write_linear_shell_strongest_trails_csv(
		const std::string& path,
		const LinearCallbackHullAggregator& callback_aggregator,
		TwilightDream::auto_search_linear::SearchWeight shell_start,
		std::uint64_t shell_count )
	{
		if ( path.empty() )
			return true;
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
			return false;

		out << "input_branch_a_mask,input_branch_b_mask,shell_weight,trail_count,strongest_trail_signed_correlation,strongest_trail_abs_correlation,strongest_trail_step_count,step_index,round_index,round_weight,output_branch_a_mask,output_branch_b_mask,input_branch_a_mask_step,input_branch_b_mask_step\n";
		for ( const auto& [ endpoint_key, endpoint_summary ] : callback_aggregator.endpoint_hulls )
		{
			for ( const auto& [ shell_weight, shell_summary ] : endpoint_summary.shell_summaries )
			{
				if ( !LinearCallbackHullAggregator::is_selected_shell( shell_weight, shell_start, shell_count ) )
					continue;

				if ( shell_summary.strongest_trail.empty() )
				{
					out << endpoint_key.input_branch_a_mask
						<< "," << endpoint_key.input_branch_b_mask
						<< "," << shell_weight
						<< "," << shell_summary.trail_count
						<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( shell_summary.strongest_trail_signed_correlation )
						<< "," << static_cast<double>( std::fabsl( shell_summary.strongest_trail_signed_correlation ) )
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
					out << endpoint_key.input_branch_a_mask
						<< "," << endpoint_key.input_branch_b_mask
						<< "," << shell_weight
						<< "," << shell_summary.trail_count
						<< "," << std::scientific << std::setprecision( 17 ) << static_cast<double>( shell_summary.strongest_trail_signed_correlation )
						<< "," << static_cast<double>( std::fabsl( shell_summary.strongest_trail_signed_correlation ) )
						<< "," << shell_summary.strongest_trail.size()
						<< "," << step_index
						<< "," << step.round_index
						<< "," << step.round_weight
						<< "," << step.output_branch_a_mask
						<< "," << step.output_branch_b_mask
						<< "," << step.input_branch_a_mask
						<< "," << step.input_branch_b_mask
						<< "\n";
				}
			}
		}
		return true;
	}
}

#include "auto_subspace_hull/linear_batch_breadth_deep.hpp"

#endif
