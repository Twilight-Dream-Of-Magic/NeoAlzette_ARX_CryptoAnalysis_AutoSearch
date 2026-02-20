#pragma once
#ifndef TWILIGHTDREAM_AUTO_SEARCH_CHECKPOINT_HPP
#define TWILIGHTDREAM_AUTO_SEARCH_CHECKPOINT_HPP

#include "common/runtime_component.hpp"

#include <cstdint>
#include <string>

namespace TwilightDream::auto_search_checkpoint
{
	static constexpr std::uint32_t kMagic = 0x4B435A4Eu; // 'NZCK' little-endian
	// MUST Frozen THIS version, DO NOT CHANGE IT (payload layout after the header is tied to kVersion;
	// shrinking fields requires bumping kVersion and keeping a reader for older versions).
	static constexpr std::uint16_t kVersion = 1;
	static constexpr std::uint32_t kEndianTag = 0x01020304u;

	enum class SearchKind : std::uint16_t
	{
		LinearBest = 1,
		DifferentialBest = 2,
		LinearAutoPipeline = 3,
		DifferentialAutoPipeline = 4,
		LinearHullBatch = 5,
		DifferentialHullBatch = 6,
		LinearHullBatchSelection = 7,
		DifferentialHullBatchSelection = 8,
		DifferentialHullCollector = 9,
		LinearHullCollector = 10,
		DifferentialHullSubspace = 11,
		LinearHullSubspace = 12,
		LinearResidualFrontierBest = 13,
		DifferentialResidualFrontierBest = 14,
		LinearResidualFrontierCollector = 15,
		DifferentialResidualFrontierCollector = 16
	};

	using BinaryWriter = TwilightDream::runtime_component::BinaryWriter;
	using BinaryReader = TwilightDream::runtime_component::BinaryReader;

	struct CheckpointHeader
	{
		std::uint32_t magic = kMagic;
		std::uint16_t version = kVersion;
		std::uint16_t kind = 0;
		std::uint32_t endian = kEndianTag;
	};

	inline bool write_header( BinaryWriter& w, SearchKind kind )
	{
		CheckpointHeader h {};
		h.kind = static_cast<std::uint16_t>( kind );
		w.write_u32( h.magic );
		w.write_u16( h.version );
		w.write_u16( h.kind );
		w.write_u32( h.endian );
		return w.ok();
	}

	inline bool read_header( BinaryReader& r, SearchKind& kind_out )
	{
		CheckpointHeader h {};
		if ( !r.read_u32( h.magic ) )
			return false;
		if ( !r.read_u16( h.version ) )
			return false;
		if ( !r.read_u16( h.kind ) )
			return false;
		if ( !r.read_u32( h.endian ) )
			return false;
		if ( h.magic != kMagic || h.version != kVersion || h.endian != kEndianTag )
			return false;
		kind_out = static_cast<SearchKind>( h.kind );
		return true;
	}

	template <class Fn>
	bool write_atomic( const std::string& path, Fn&& fn )
	{
		return TwilightDream::runtime_component::write_atomic_binary_file( path, std::forward<Fn>( fn ) );
	}

} // namespace TwilightDream::auto_search_checkpoint

#endif
