#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace TwilightDream::AutoSearchFrameDefine
{
	using SearchWeight = std::uint64_t;
	constexpr SearchWeight INFINITE_WEIGHT = std::numeric_limits<SearchWeight>::max();
	constexpr SearchWeight MAX_FINITE_SEARCH_WEIGHT = INFINITE_WEIGHT - 1;

	constexpr bool search_weight_is_infinite( SearchWeight weight ) noexcept
	{
		return weight == INFINITE_WEIGHT;
	}

	constexpr SearchWeight add_weight( SearchWeight lhs, SearchWeight rhs ) noexcept
	{
		if ( search_weight_is_infinite( lhs ) || search_weight_is_infinite( rhs ) )
			return INFINITE_WEIGHT;
		if ( lhs > ( MAX_FINITE_SEARCH_WEIGHT - rhs ) )
			return INFINITE_WEIGHT;
		return lhs + rhs;
	}

	constexpr SearchWeight saturating_add_search_weight( SearchWeight weight, SearchWeight delta ) noexcept
	{
		return add_weight( weight, delta );
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
			return std::numeric_limits<std::int64_t>::max();
		constexpr SearchWeight kMaxDisplay = static_cast<SearchWeight>( std::numeric_limits<std::int64_t>::max() );
		return static_cast<std::int64_t>( ( weight > kMaxDisplay ) ? kMaxDisplay : weight );
	}

	inline std::string format_search_weight( SearchWeight weight )
	{
		if ( weight >= INFINITE_WEIGHT )
			return "inf";
		return std::to_string( weight );
	}
}
