#pragma once

#include "auto_search_frame/detail/differential_best_search_primitives.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

namespace TwilightDream::auto_search_differential
{
	struct DifferentialBestSearchContext;
	struct DifferentialSearchCursor;

	struct BestWeightHistory
	{
		std::ofstream out {};
		std::string	  path {};
		SearchWeight  last_written_weight = INFINITE_WEIGHT;

		static std::string default_path( int round_count, std::uint32_t da, std::uint32_t db );

		bool open_append( const std::string& path );

		void maybe_write( const DifferentialBestSearchContext& context, const char* reason );
	};

	struct BinaryCheckpointManager
	{
		using WriteOverrideFn = bool ( * )( BinaryCheckpointManager&, const DifferentialBestSearchContext&, const DifferentialSearchCursor&, const char* );

		std::string path {};
		std::uint64_t every_seconds = 0;
		bool pending_best = false;
		bool pending_watchdog_latest = false;
		bool pending_watchdog_archive = false;
		std::chrono::steady_clock::time_point last_write_time {};
		std::chrono::steady_clock::time_point last_archive_write_time {};
		WriteOverrideFn write_override = nullptr;
		void* write_override_user_data = nullptr;

		bool enabled() const { return !path.empty(); }
		bool pending_best_change() const { return pending_best; }
		bool pending_runtime_request() const { return pending_watchdog_latest || pending_watchdog_archive; }
		void mark_best_changed() { pending_best = true; }

		void poll( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor );
		bool write_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason );
		bool write_archive_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason );
	};
}
