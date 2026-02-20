#pragma once

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

namespace TwilightDream::auto_search_linear
{
	struct LinearBestSearchContext;
	struct LinearSearchCursor;

	struct BestWeightHistory
	{
		std::ofstream out {};
		std::string	  path {};
		SearchWeight  last_written_weight = INFINITE_WEIGHT;

		static std::string default_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b );

		bool open_append( const std::string& path );

		void maybe_write( const LinearBestSearchContext& context, const char* reason );
	};

	struct BinaryCheckpointManager
	{
		using WriteOverrideFn = bool ( * )( BinaryCheckpointManager&, const LinearBestSearchContext&, const LinearSearchCursor&, const char* );

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

		void poll( const LinearBestSearchContext& context, const LinearSearchCursor& cursor );
		bool write_now( const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason );
		bool write_archive_now( const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason );
	};
}
