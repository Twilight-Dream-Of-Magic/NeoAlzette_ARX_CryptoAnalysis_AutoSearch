#pragma once
#if !defined( TWILIGHTDREAM_HULL_GROWTH_COMMON_HPP )
#define TWILIGHTDREAM_HULL_GROWTH_COMMON_HPP

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace TwilightDream::hull_callback_aggregator
{
	enum class StoredTrailPolicy : std::uint8_t
	{
		ArrivalOrder = 0,
		Strongest = 1
	};

	static inline const char* stored_trail_policy_to_string( StoredTrailPolicy policy ) noexcept
	{
		switch ( policy )
		{
		case StoredTrailPolicy::ArrivalOrder: return "arrival";
		case StoredTrailPolicy::Strongest: return "strongest";
		default: return "unknown";
		}
	}

	static inline bool parse_stored_trail_policy( std::string_view text, StoredTrailPolicy& out_policy ) noexcept
	{
		if ( text == "arrival" || text == "arrival_order" || text == "first" )
		{
			out_policy = StoredTrailPolicy::ArrivalOrder;
			return true;
		}
		if ( text == "strongest" || text == "topk" || text == "top_k" )
		{
			out_policy = StoredTrailPolicy::Strongest;
			return true;
		}
		return false;
	}

	static inline std::string format_word32_hex_string( std::uint32_t value )
	{
		std::ostringstream out;
		out << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << value;
		return out.str();
	}

	static inline std::string append_suffix_before_extension( std::string_view path, std::string_view suffix )
	{
		if ( path.empty() || suffix.empty() )
			return std::string( path );

		const std::string owned_path( path );
		const std::size_t slash = owned_path.find_last_of( "/\\" );
		const std::size_t dot = owned_path.find_last_of( '.' );
		if ( dot == std::string::npos || ( slash != std::string::npos && dot < slash ) )
			return owned_path + std::string( suffix );
		return owned_path.substr( 0, dot ) + std::string( suffix ) + owned_path.substr( dot );
	}

	static inline long double correlation_to_weight( long double correlation ) noexcept
	{
		const long double absolute_correlation = std::fabsl( correlation );
		if ( !( absolute_correlation > 0.0L ) )
			return std::numeric_limits<long double>::infinity();
		return -std::log2( absolute_correlation );
	}

	static inline long double probability_to_weight( long double probability ) noexcept
	{
		if ( !( probability > 0.0L ) )
			return std::numeric_limits<long double>::infinity();
		return -std::log2( probability );
	}

	struct GrowthStopPolicy
	{
		double absolute_delta = -1.0;
		double relative_delta = -1.0;
	};

	template <typename Row, typename BestResult, typename RejectionReason>
	struct OuterGrowthDriverResult
	{
		std::vector<Row> rows {};
		BestResult	   best_result {};
		bool		   used_best_weight_reference = false;
		bool		   rejected = false;
		RejectionReason rejection_reason {};
	};

	static inline long double compute_growth_relative_delta( long double shell_delta_abs, long double cumulative_abs ) noexcept
	{
		if ( cumulative_abs <= 0.0L )
			return ( shell_delta_abs <= 0.0L ) ? 0.0L : std::numeric_limits<long double>::infinity();
		return
			( cumulative_abs > 0.0L )
				? ( shell_delta_abs / cumulative_abs )
				: std::numeric_limits<long double>::infinity();
	}

	static inline bool should_stop_growth(
		const GrowthStopPolicy& policy,
		bool hit_any_limit,
		long double shell_delta_abs,
		long double cumulative_abs ) noexcept
	{
		if ( hit_any_limit )
			return true;

		const long double relative_delta = compute_growth_relative_delta( shell_delta_abs, cumulative_abs );
		if ( policy.absolute_delta >= 0.0 && shell_delta_abs <= static_cast<long double>( policy.absolute_delta ) )
			return true;
		if ( policy.relative_delta >= 0.0 && relative_delta <= static_cast<long double>( policy.relative_delta ) )
			return true;
		return false;
	}

	template <
		typename Row,
		typename BestResult,
		typename RejectionReason,
		typename RuntimeState,
		typename StartRunFn,
		typename ShellRunFn,
		typename RuntimeCollectedFn,
		typename RuntimeRejectionFn,
		typename RuntimeBestResultFn,
		typename RuntimeCollectWeightCapFn,
		typename RowBuilderFn,
		typename RowHitAnyLimitFn,
		typename RowShellDeltaAbsFn,
		typename RowCumulativeAbsFn>
	static inline OuterGrowthDriverResult<Row, BestResult, RejectionReason> run_outer_growth_driver(
		bool used_best_weight_reference,
		int growth_shell_start,
		std::uint64_t growth_max_shells,
		const GrowthStopPolicy& growth_stop_policy,
		StartRunFn&& run_start,
		ShellRunFn&& run_shell,
		RuntimeCollectedFn&& runtime_collected,
		RuntimeRejectionFn&& runtime_rejection_reason,
		RuntimeBestResultFn&& runtime_best_result,
		RuntimeCollectWeightCapFn&& runtime_collect_weight_cap,
		RowBuilderFn&& make_row,
		RowHitAnyLimitFn&& row_hit_any_limit,
		RowShellDeltaAbsFn&& row_shell_delta_abs,
		RowCumulativeAbsFn&& row_cumulative_abs )
	{
		OuterGrowthDriverResult<Row, BestResult, RejectionReason> result {};
		result.used_best_weight_reference = used_best_weight_reference;

		const std::uint64_t max_shells = ( growth_max_shells == 0 ) ? 8ull : growth_max_shells;
		result.rows.reserve( static_cast<std::size_t>( std::min<std::uint64_t>( max_shells, 1024ull ) ) );

		int base_shell_weight = growth_shell_start;
		std::uint64_t next_shell_index = 0;
		if ( result.used_best_weight_reference )
		{
			RuntimeState start_state = run_start();
			result.best_result = runtime_best_result( start_state );
			if ( !runtime_collected( start_state ) )
			{
				result.rejected = true;
				result.rejection_reason = runtime_rejection_reason( start_state );
				return result;
			}

			base_shell_weight = runtime_collect_weight_cap( start_state );
			result.rows.push_back( make_row( base_shell_weight, start_state ) );
			next_shell_index = 1;
			if ( max_shells <= 1 ||
				 should_stop_growth(
					 growth_stop_policy,
					 row_hit_any_limit( result.rows.back() ),
					 row_shell_delta_abs( result.rows.back() ),
					 row_cumulative_abs( result.rows.back() ) ) )
				return result;
		}

		for ( std::uint64_t shell_index = next_shell_index; shell_index < max_shells; ++shell_index )
		{
			const int shell_weight = base_shell_weight + static_cast<int>( shell_index );
			RuntimeState shell_state = run_shell( shell_weight );
			if ( !runtime_collected( shell_state ) )
			{
				result.rejected = true;
				result.rejection_reason = runtime_rejection_reason( shell_state );
				return result;
			}

			result.rows.push_back( make_row( shell_weight, shell_state ) );
			if ( should_stop_growth(
					 growth_stop_policy,
					 row_hit_any_limit( result.rows.back() ),
					 row_shell_delta_abs( result.rows.back() ),
					 row_cumulative_abs( result.rows.back() ) ) )
				break;
		}

		return result;
	}
}

#endif
