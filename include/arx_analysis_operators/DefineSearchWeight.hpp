#pragma once

#include <cstdint>
#include <limits>

namespace TwilightDream::AutoSearchFrameDefine
{
	using SearchWeight = std::uint64_t;
	constexpr SearchWeight INFINITE_WEIGHT = std::numeric_limits<SearchWeight>::max();
	constexpr SearchWeight MAX_FINITE_SEARCH_WEIGHT = INFINITE_WEIGHT - 1;

	constexpr SearchWeight saturating_add_search_weight( SearchWeight weight, SearchWeight delta ) noexcept
	{
		const SearchWeight increment = delta;
		if ( weight >= INFINITE_WEIGHT )
			return INFINITE_WEIGHT;
		if ( weight >= MAX_FINITE_SEARCH_WEIGHT )
			return MAX_FINITE_SEARCH_WEIGHT;
		if ( increment >= MAX_FINITE_SEARCH_WEIGHT )
			return MAX_FINITE_SEARCH_WEIGHT;
		if ( weight > ( MAX_FINITE_SEARCH_WEIGHT - increment ) )
			return MAX_FINITE_SEARCH_WEIGHT;
		return weight + increment;
	}

	constexpr SearchWeight successor_search_weight( SearchWeight weight ) noexcept
	{
		return ( weight >= MAX_FINITE_SEARCH_WEIGHT ) ? INFINITE_WEIGHT : ( weight + 1 );
	}

	constexpr SearchWeight remaining_search_weight_budget( SearchWeight cap, SearchWeight used_weight ) noexcept
	{
		if ( cap >= INFINITE_WEIGHT )
			return INFINITE_WEIGHT;
		const SearchWeight used = used_weight;
		return ( used >= cap ) ? SearchWeight( 0 ) : ( cap - used );
	}

	constexpr std::int64_t display_search_weight( SearchWeight weight ) noexcept
	{
		if ( weight >= INFINITE_WEIGHT )
			return -1;
		constexpr SearchWeight kMaxDisplay = static_cast<SearchWeight>( std::numeric_limits<std::int64_t>::max() );
		return static_cast<std::int64_t>( ( weight > kMaxDisplay ) ? kMaxDisplay : weight );
	}
}
