#pragma once
#ifndef TWILIGHTDREAM_AUTO_SEARCH_CHECKPOINT_HPP
#define TWILIGHTDREAM_AUTO_SEARCH_CHECKPOINT_HPP

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

namespace TwilightDream::auto_search_checkpoint
{
	static constexpr std::uint32_t kMagic = 0x4B435A4Eu; // 'NZCK' little-endian
	static constexpr std::uint16_t kVersion = 1;
	static constexpr std::uint32_t kEndianTag = 0x01020304u;

	enum class SearchKind : std::uint16_t
	{
		Differential = 1,
		Linear = 2
	};

	struct BinaryWriter
	{
		std::ofstream out;

		explicit BinaryWriter( const std::string& path )
			: out( path, std::ios::binary | std::ios::out | std::ios::trunc )
		{
		}

		bool ok() const { return bool( out ); }

		void write_bytes( const void* data, std::size_t size )
		{
			out.write( static_cast<const char*>( data ), static_cast<std::streamsize>( size ) );
		}

		void write_u8( std::uint8_t v ) { write_bytes( &v, sizeof( v ) ); }
		void write_u16( std::uint16_t v )
		{
			std::uint8_t b[ 2 ] = { std::uint8_t( v & 0xFFu ), std::uint8_t( ( v >> 8 ) & 0xFFu ) };
			write_bytes( b, sizeof( b ) );
		}
		void write_u32( std::uint32_t v )
		{
			std::uint8_t b[ 4 ] = {
				std::uint8_t( v & 0xFFu ),
				std::uint8_t( ( v >> 8 ) & 0xFFu ),
				std::uint8_t( ( v >> 16 ) & 0xFFu ),
				std::uint8_t( ( v >> 24 ) & 0xFFu )
			};
			write_bytes( b, sizeof( b ) );
		}
		void write_u64( std::uint64_t v )
		{
			std::uint8_t b[ 8 ] = {
				std::uint8_t( v & 0xFFu ),
				std::uint8_t( ( v >> 8 ) & 0xFFu ),
				std::uint8_t( ( v >> 16 ) & 0xFFu ),
				std::uint8_t( ( v >> 24 ) & 0xFFu ),
				std::uint8_t( ( v >> 32 ) & 0xFFu ),
				std::uint8_t( ( v >> 40 ) & 0xFFu ),
				std::uint8_t( ( v >> 48 ) & 0xFFu ),
				std::uint8_t( ( v >> 56 ) & 0xFFu )
			};
			write_bytes( b, sizeof( b ) );
		}
		void write_i32( std::int32_t v ) { write_u32( static_cast<std::uint32_t>( v ) ); }
		void write_i64( std::int64_t v ) { write_u64( static_cast<std::uint64_t>( v ) ); }

		void write_string( const std::string& s )
		{
			write_u32( static_cast<std::uint32_t>( s.size() ) );
			if ( !s.empty() )
				write_bytes( s.data(), s.size() );
		}
	};

	struct BinaryReader
	{
		std::ifstream in;

		explicit BinaryReader( const std::string& path )
			: in( path, std::ios::binary | std::ios::in )
		{
		}

		bool ok() const { return bool( in ); }

		bool read_bytes( void* data, std::size_t size )
		{
			in.read( static_cast<char*>( data ), static_cast<std::streamsize>( size ) );
			return bool( in );
		}

		bool read_u8( std::uint8_t& out ) { return read_bytes( &out, sizeof( out ) ); }
		bool read_u16( std::uint16_t& out )
		{
			std::uint8_t b[ 2 ] = {};
			if ( !read_bytes( b, sizeof( b ) ) )
				return false;
			out = std::uint16_t( b[ 0 ] ) | ( std::uint16_t( b[ 1 ] ) << 8 );
			return true;
		}
		bool read_u32( std::uint32_t& out )
		{
			std::uint8_t b[ 4 ] = {};
			if ( !read_bytes( b, sizeof( b ) ) )
				return false;
			out = std::uint32_t( b[ 0 ] ) | ( std::uint32_t( b[ 1 ] ) << 8 ) | ( std::uint32_t( b[ 2 ] ) << 16 ) | ( std::uint32_t( b[ 3 ] ) << 24 );
			return true;
		}
		bool read_u64( std::uint64_t& out )
		{
			std::uint8_t b[ 8 ] = {};
			if ( !read_bytes( b, sizeof( b ) ) )
				return false;
			out = std::uint64_t( b[ 0 ] ) |
				( std::uint64_t( b[ 1 ] ) << 8 ) |
				( std::uint64_t( b[ 2 ] ) << 16 ) |
				( std::uint64_t( b[ 3 ] ) << 24 ) |
				( std::uint64_t( b[ 4 ] ) << 32 ) |
				( std::uint64_t( b[ 5 ] ) << 40 ) |
				( std::uint64_t( b[ 6 ] ) << 48 ) |
				( std::uint64_t( b[ 7 ] ) << 56 );
			return true;
		}
		bool read_i32( std::int32_t& out )
		{
			std::uint32_t v = 0;
			if ( !read_u32( v ) )
				return false;
			out = static_cast<std::int32_t>( v );
			return true;
		}
		bool read_i64( std::int64_t& out )
		{
			std::uint64_t v = 0;
			if ( !read_u64( v ) )
				return false;
			out = static_cast<std::int64_t>( v );
			return true;
		}

		bool read_string( std::string& out )
		{
			std::uint32_t size = 0;
			if ( !read_u32( size ) )
				return false;
			out.clear();
			if ( size == 0 )
				return true;
			out.resize( size );
			return read_bytes( out.data(), size );
		}
	};

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
		// Write to temp + rename for best-effort atomic replace.
		const std::string tmp = path + ".tmp";
		BinaryWriter w( tmp );
		if ( !w.ok() )
			return false;
		if ( !fn( w ) )
		{
			w.out.close();
			std::error_code ec;
			std::filesystem::remove( tmp, ec );
			return false;
		}
		w.out.flush();
		w.out.close();

		std::error_code ec;
		std::filesystem::remove( path, ec );
		std::filesystem::rename( tmp, path, ec );
		if ( ec )
		{
			std::error_code ec2;
			std::filesystem::remove( tmp, ec2 );
			return false;
		}
		return true;
	}

} // namespace TwilightDream::auto_search_checkpoint

#endif
