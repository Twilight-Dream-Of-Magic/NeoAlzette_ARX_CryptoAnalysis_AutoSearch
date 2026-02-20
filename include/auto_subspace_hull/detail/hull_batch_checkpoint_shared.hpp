#pragma once
#ifndef TWILIGHTDREAM_HULL_BATCH_CHECKPOINT_SHARED_HPP
#define TWILIGHTDREAM_HULL_BATCH_CHECKPOINT_SHARED_HPP

#include "auto_search_frame/search_checkpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <type_traits>
#include <vector>

namespace TwilightDream::hull_batch_checkpoint_shared
{
	using TwilightDream::auto_search_checkpoint::BinaryReader;
	using TwilightDream::auto_search_checkpoint::BinaryWriter;

	inline void write_bool( BinaryWriter& w, bool value )
	{
		w.write_u8( value ? 1u : 0u );
	}

	inline bool read_bool( BinaryReader& r, bool& out_value )
	{
		std::uint8_t raw = 0;
		if ( !r.read_u8( raw ) )
			return false;
		out_value = ( raw != 0u );
		return true;
	}

	template <class EnumT>
	inline void write_enum_u8( BinaryWriter& w, EnumT value )
	{
		static_assert( std::is_enum_v<EnumT>, "EnumT must be an enum type." );
		using Underlying = std::underlying_type_t<EnumT>;
		w.write_u8( static_cast<std::uint8_t>( static_cast<Underlying>( value ) ) );
	}

	template <class EnumT>
	inline bool read_enum_u8( BinaryReader& r, EnumT& out_value )
	{
		static_assert( std::is_enum_v<EnumT>, "EnumT must be an enum type." );
		using Underlying = std::underlying_type_t<EnumT>;
		std::uint8_t raw = 0;
		if ( !r.read_u8( raw ) )
			return false;
		out_value = static_cast<EnumT>( static_cast<Underlying>( raw ) );
		return true;
	}

	inline void write_size( BinaryWriter& w, std::size_t value )
	{
		w.write_u64( static_cast<std::uint64_t>( value ) );
	}

	inline bool read_size( BinaryReader& r, std::size_t& out_value )
	{
		std::uint64_t raw = 0;
		if ( !r.read_u64( raw ) )
			return false;
		if ( raw > static_cast<std::uint64_t>( std::numeric_limits<std::size_t>::max() ) )
			return false;
		out_value = static_cast<std::size_t>( raw );
		return true;
	}

	inline void write_long_double( BinaryWriter& w, long double value )
	{
		w.write_bytes( &value, sizeof( value ) );
	}

	inline bool read_long_double( BinaryReader& r, long double& out_value )
	{
		return r.read_bytes( &out_value, sizeof( out_value ) );
	}

	template <class RuntimeControlsT>
	inline void write_runtime_controls( BinaryWriter& w, const RuntimeControlsT& controls )
	{
		w.write_u64( controls.maximum_search_nodes );
		w.write_u64( controls.maximum_search_seconds );
		w.write_u64( controls.progress_every_seconds );
		w.write_u64( controls.checkpoint_every_seconds );
	}

	template <class RuntimeControlsT>
	inline bool read_runtime_controls( BinaryReader& r, RuntimeControlsT& controls )
	{
		return
			r.read_u64( controls.maximum_search_nodes ) &&
			r.read_u64( controls.maximum_search_seconds ) &&
			r.read_u64( controls.progress_every_seconds ) &&
			r.read_u64( controls.checkpoint_every_seconds );
	}

	template <class ContainerT, class WriteElementFn>
	inline void write_counted_container( BinaryWriter& w, const ContainerT& container, WriteElementFn&& write_element_fn )
	{
		write_size( w, container.size() );
		for ( const auto& value : container )
			std::forward<WriteElementFn>( write_element_fn )( value );
	}

	template <class ContainerT, class ReadElementFn>
	inline bool read_counted_container( BinaryReader& r, ContainerT& container, ReadElementFn&& read_element_fn )
	{
		std::size_t count = 0;
		if ( !read_size( r, count ) )
			return false;
		container.clear();
		container.resize( count );
		for ( auto& value : container )
		{
			if ( !std::forward<ReadElementFn>( read_element_fn )( value ) )
				return false;
		}
		return true;
	}

	template <class WriteElementFn>
	inline void write_u8_vector( BinaryWriter& w, const std::vector<std::uint8_t>& values, WriteElementFn&& write_element_fn )
	{
		write_size( w, values.size() );
		for ( const std::uint8_t value : values )
			std::forward<WriteElementFn>( write_element_fn )( value );
	}

	template <class ReadElementFn>
	inline bool read_u8_vector( BinaryReader& r, std::vector<std::uint8_t>& values, ReadElementFn&& read_element_fn )
	{
		std::size_t count = 0;
		if ( !read_size( r, count ) )
			return false;
		values.assign( count, 0u );
		for ( std::uint8_t& value : values )
		{
			if ( !std::forward<ReadElementFn>( read_element_fn )( value ) )
				return false;
		}
		return true;
	}

	template <class MapT, class WriteKeyFn, class WriteValueFn>
	inline void write_counted_map( BinaryWriter& w, const MapT& values, WriteKeyFn&& write_key_fn, WriteValueFn&& write_value_fn )
	{
		write_size( w, values.size() );
		for ( const auto& [ key, value ] : values )
		{
			std::forward<WriteKeyFn>( write_key_fn )( key );
			std::forward<WriteValueFn>( write_value_fn )( value );
		}
	}

	template <class MapT, class KeyT, class ValueT, class ReadKeyFn, class ReadValueFn>
	inline bool read_counted_map( BinaryReader& r, MapT& values, ReadKeyFn&& read_key_fn, ReadValueFn&& read_value_fn )
	{
		std::size_t count = 0;
		if ( !read_size( r, count ) )
			return false;
		values.clear();
		for ( std::size_t index = 0; index < count; ++index )
		{
			KeyT key {};
			ValueT value {};
			if ( !std::forward<ReadKeyFn>( read_key_fn )( key ) )
				return false;
			if ( !std::forward<ReadValueFn>( read_value_fn )( value ) )
				return false;
			values.emplace( std::move( key ), std::move( value ) );
		}
		return true;
	}
}  // namespace TwilightDream::hull_batch_checkpoint_shared

#endif
