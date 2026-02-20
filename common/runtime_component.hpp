#pragma once
#if !defined( TWILIGHTDREAM_RUNTIME_COMPONENT_HPP )
#define TWILIGHTDREAM_RUNTIME_COMPONENT_HPP

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <thread>
#include <ctime>

namespace TwilightDream::runtime_component
{
	// ============================================================================
	// PMR / bounded allocator infrastructure
	//
	// Goal:
	// - Make hot dynamic allocations (unordered_map nodes/buckets) come from a controllable resource,
	//   so we can enforce a hard cap and gracefully degrade when memory is tight.
	// - Avoid crashing with std::bad_alloc by turning caches/memoization into "best effort" components.
	// ============================================================================

	class BoundedMemoryResource final : public std::pmr::memory_resource
	{
	public:
		explicit BoundedMemoryResource( std::pmr::memory_resource* upstream );

		void		  set_limit_bytes( std::uint64_t new_limit_bytes );
		std::uint64_t limit_bytes() const;
		std::uint64_t allocated_bytes() const;
		std::uint64_t remaining_bytes() const;

	private:
		std::pmr::memory_resource* upstream_ = nullptr;
		std::atomic<std::uint64_t> allocated_bytes_ { 0 };
		std::atomic<std::uint64_t> limit_bytes_ { 0 };	// 0 = unlimited

		void* do_allocate( std::size_t bytes, std::size_t alignment ) override;
		void  do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) override;
		bool  do_is_equal( const std::pmr::memory_resource& other ) const noexcept override;
	};

	// IMPORTANT: leak-on-exit to avoid static/thread_local destruction order issues.
	BoundedMemoryResource& pmr_bounded_resource();

	// Run epoch increments on each `pmr_configure_for_run()`. Used by thread_local caches to reset between stages/runs.
	std::uint64_t pmr_run_epoch();

	void		  pmr_report_oom_once( const char* where, const std::source_location& loc = std::source_location::current() );
	std::uint64_t pmr_suggest_limit_bytes( std::uint64_t available_physical_bytes, std::uint64_t headroom_bytes );
	void		  pmr_configure_for_run( std::uint64_t available_physical_bytes, std::uint64_t headroom_bytes );

	// ============================================================================
	// Memory governor (optional)
	//
	// A light "pressure" signal used to stop growing caches and (best-effort) tighten PMR budgets.
	// The actual system memory sampling is provided by the caller via `memory_governor_set_poll_fn()`.
	// ============================================================================

	using MemoryGovernorPollFn = void ( * )();

	void memory_governor_enable_for_run( std::uint64_t headroom_bytes );
	void memory_governor_disable_for_run();
	bool memory_governor_in_pressure();
	void memory_governor_set_poll_fn( MemoryGovernorPollFn fn );
	void memory_governor_poll_if_needed( std::chrono::steady_clock::time_point now );

	// Called by the governor poll function when it samples system memory.
	void memory_governor_update_from_system_sample( std::uint64_t available_physical_bytes );

	// ============================================================================
	// Small output / formatting helpers (shared)
	// ============================================================================

	void		print_word32_hex( const char* label, std::uint32_t v );
	std::string format_local_time_now();
	std::string format_local_timestamp_for_filename_now();
	std::string make_unique_timestamped_artifact_path( const std::string& stem_path, const std::string& extension );
	std::string append_timestamp_to_artifact_path( const std::string& path );
	std::string hex8( std::uint32_t v );

	struct RuntimeEventLog
	{
		std::ofstream out {};
		std::string	  path {};

		static std::string default_path( const std::string& stem_path )
		{
			return make_unique_timestamped_artifact_path( stem_path, ".runtime.log" );
		}

		bool open_append( const std::string& path )
		{
			out.open( path, std::ios::out | std::ios::app );
			if ( out )
				this->path = path;
			return bool( out );
		}

		template <class WriteFieldsFn>
		void write_event( const char* event_name, WriteFieldsFn&& write_fields )
		{
			if ( !out )
				return;
			out << "=== runtime_event ===\n";
			out << "timestamp_local=" << format_local_time_now() << "\n";
			out << "event=" << ( event_name ? event_name : "unknown" ) << "\n";
			write_fields( out );
			out << "\n";
			out.flush();
		}
	};

	// ============================================================================
	// Binary file I/O helpers
	// ============================================================================

	struct BinaryWriter
	{
		std::ofstream out;

		explicit BinaryWriter( const std::string& path );

		bool ok() const;

		void write_bytes( const void* data, std::size_t size );
		void write_u8( std::uint8_t v );
		void write_u16( std::uint16_t v );
		void write_u32( std::uint32_t v );
		void write_u64( std::uint64_t v );
		void write_i32( std::int32_t v );
		void write_i64( std::int64_t v );
		void write_string( const std::string& s );
	};

	struct BinaryReader
	{
		std::ifstream in;

		explicit BinaryReader( const std::string& path );

		bool ok() const;

		bool read_bytes( void* data, std::size_t size );
		bool read_u8( std::uint8_t& out );
		bool read_u16( std::uint16_t& out );
		bool read_u32( std::uint32_t& out );
		bool read_u64( std::uint64_t& out );
		bool read_i32( std::int32_t& out );
		bool read_i64( std::int64_t& out );
		bool read_string( std::string& out );
	};

	void discard_atomic_binary_write( const std::string& tmp_path ) noexcept;
	bool commit_atomic_binary_write( const std::string& tmp_path, const std::string& path ) noexcept;

	template <class Fn>
	bool write_atomic_binary_file( const std::string& path, Fn&& fn )
	{
		const std::string tmp = path + ".tmp";
		BinaryWriter		  writer( tmp );
		if ( !writer.ok() )
			return false;
		if ( !std::forward<Fn>( fn )( writer ) )
		{
			writer.out.close();
			discard_atomic_binary_write( tmp );
			return false;
		}
		writer.out.flush();
		writer.out.close();
		return commit_atomic_binary_write( tmp, path );
	}

	// ============================================================================
	// Shared runtime budget / limiter helpers
	// ============================================================================

	struct SearchRuntimeControls
	{
		std::uint64_t maximum_search_nodes = 0;		// per-invocation node budget
		std::uint64_t maximum_search_seconds = 0;	// per-invocation wall-clock budget; resume starts a fresh timer
		std::uint64_t progress_every_seconds = 0;
		std::uint64_t checkpoint_every_seconds = 0;
	};

	struct RuntimeWatchdogControl
	{
		std::atomic<std::uint64_t> total_nodes_visited { 0 };
		std::atomic<std::uint64_t> run_nodes_visited { 0 };
		std::atomic<bool>			 stop_due_to_time_limit { false };
		std::atomic<bool>			 stop_due_to_node_limit { false };
		std::atomic<bool>			 checkpoint_latest_due { false };
		std::atomic<bool>			 checkpoint_archive_due { false };
	};

	struct RuntimeCheckpointWatchdogRequests
	{
		bool latest_due = false;
		bool archive_due = false;
	};

	enum class RuntimeTimeLimitScope : std::uint8_t
	{
		PerInvocationWallClock = 0
	};

	inline RuntimeTimeLimitScope runtime_time_limit_scope() noexcept
	{
		return RuntimeTimeLimitScope::PerInvocationWallClock;
	}

	inline const char* runtime_time_limit_scope_name( RuntimeTimeLimitScope scope ) noexcept
	{
		switch ( scope )
		{
		case RuntimeTimeLimitScope::PerInvocationWallClock:
		default:
			return "per_invocation_wall_clock";
		}
	}

	inline bool runtime_time_limit_reached_at(
		std::uint64_t maximum_search_seconds,
		const std::chrono::steady_clock::time_point& run_start_time,
		const std::chrono::steady_clock::time_point& now ) noexcept
	{
		if ( maximum_search_seconds == 0 || run_start_time.time_since_epoch().count() == 0 )
			return false;
		return std::chrono::duration<double>( now - run_start_time ).count() >= double( maximum_search_seconds );
	}

	inline bool runtime_time_limit_reached_now(
		std::uint64_t maximum_search_seconds,
		const std::chrono::steady_clock::time_point& run_start_time ) noexcept
	{
		return runtime_time_limit_reached_at( maximum_search_seconds, run_start_time, std::chrono::steady_clock::now() );
	}

	enum class RuntimeBudgetMode : std::uint8_t
	{
		Unlimited = 0,
		NodeOnly = 1,
		TimeOnly = 2
	};

	inline RuntimeBudgetMode runtime_budget_mode( const SearchRuntimeControls& runtime_controls ) noexcept
	{
		if ( runtime_controls.maximum_search_seconds != 0 )
			return RuntimeBudgetMode::TimeOnly;
		if ( runtime_controls.maximum_search_nodes != 0 )
			return RuntimeBudgetMode::NodeOnly;
		return RuntimeBudgetMode::Unlimited;
	}

	inline const char* runtime_budget_mode_name( const SearchRuntimeControls& runtime_controls ) noexcept
	{
		switch ( runtime_budget_mode( runtime_controls ) )
		{
		case RuntimeBudgetMode::TimeOnly:
			return "time_only";
		case RuntimeBudgetMode::NodeOnly:
			return "node_only";
		case RuntimeBudgetMode::Unlimited:
		default:
			return "unlimited";
		}
	}

	inline bool runtime_nodes_ignored_due_to_time_limit( const SearchRuntimeControls& runtime_controls ) noexcept
	{
		return runtime_controls.maximum_search_seconds != 0 && runtime_controls.maximum_search_nodes != 0;
	}

	inline std::uint64_t runtime_effective_maximum_search_nodes( const SearchRuntimeControls& runtime_controls ) noexcept
	{
		return ( runtime_budget_mode( runtime_controls ) == RuntimeBudgetMode::TimeOnly ) ? 0ull : runtime_controls.maximum_search_nodes;
	}

	struct RuntimeInvocationState
	{
		std::chrono::steady_clock::time_point run_start_time {};
		std::uint64_t						  total_nodes_visited = 0;
		std::uint64_t						  run_nodes_visited = 0;
		std::uint64_t						  progress_node_mask = ( 1ull << 18 ) - 1;
		bool								  stop_due_to_time_limit = false;
		bool								  stop_due_to_node_limit = false;
		RuntimeWatchdogControl*			  watchdog_control = nullptr;
	};

	inline void runtime_sync_watchdog_control( RuntimeInvocationState& runtime_state ) noexcept
	{
		if ( runtime_state.watchdog_control == nullptr )
			return;
		runtime_state.watchdog_control->total_nodes_visited.store( runtime_state.total_nodes_visited, std::memory_order_relaxed );
		runtime_state.watchdog_control->run_nodes_visited.store( runtime_state.run_nodes_visited, std::memory_order_relaxed );
		if ( runtime_state.stop_due_to_time_limit )
			runtime_state.watchdog_control->stop_due_to_time_limit.store( true, std::memory_order_relaxed );
		if ( runtime_state.stop_due_to_node_limit )
			runtime_state.watchdog_control->stop_due_to_node_limit.store( true, std::memory_order_relaxed );
	}

	inline void runtime_pull_watchdog_stop_flags( RuntimeInvocationState& runtime_state ) noexcept
	{
		if ( runtime_state.watchdog_control == nullptr )
			return;
		if ( runtime_state.watchdog_control->stop_due_to_time_limit.load( std::memory_order_relaxed ) )
			runtime_state.stop_due_to_time_limit = true;
		if ( runtime_state.watchdog_control->stop_due_to_node_limit.load( std::memory_order_relaxed ) )
			runtime_state.stop_due_to_node_limit = true;
	}

	inline bool runtime_watchdog_checkpoint_request_pending( const RuntimeInvocationState& runtime_state ) noexcept
	{
		return
			runtime_state.watchdog_control != nullptr &&
			( runtime_state.watchdog_control->checkpoint_latest_due.load( std::memory_order_relaxed ) ||
			  runtime_state.watchdog_control->checkpoint_archive_due.load( std::memory_order_relaxed ) );
	}

	inline RuntimeCheckpointWatchdogRequests runtime_take_watchdog_checkpoint_requests( RuntimeInvocationState& runtime_state ) noexcept
	{
		RuntimeCheckpointWatchdogRequests requests {};
		if ( runtime_state.watchdog_control == nullptr )
			return requests;
		requests.latest_due = runtime_state.watchdog_control->checkpoint_latest_due.exchange( false, std::memory_order_relaxed );
		requests.archive_due = runtime_state.watchdog_control->checkpoint_archive_due.exchange( false, std::memory_order_relaxed );
		return requests;
	}

	inline std::uint64_t recommended_progress_node_mask_for_time_limit( std::uint64_t maximum_search_seconds ) noexcept
	{
		if ( maximum_search_seconds == 0 )
			return ( 1ull << 18 ) - 1;
		if ( maximum_search_seconds <= 2 )
			return ( 1ull << 8 ) - 1;
		if ( maximum_search_seconds <= 10 )
			return ( 1ull << 10 ) - 1;
		return ( 1ull << 12 ) - 1;
	}

	inline std::chrono::steady_clock::time_point begin_runtime_invocation(
		const SearchRuntimeControls& runtime_controls,
		std::uint64_t& progress_node_mask,
		bool& stop_due_to_time_limit ) noexcept
	{
		stop_due_to_time_limit = false;
		progress_node_mask = ( 1ull << 18 ) - 1;
		if ( runtime_controls.maximum_search_seconds != 0 )
		{
			progress_node_mask =
				std::min<std::uint64_t>(
					progress_node_mask,
					recommended_progress_node_mask_for_time_limit( runtime_controls.maximum_search_seconds ) );
		}
		return std::chrono::steady_clock::now();
	}

	inline void begin_runtime_invocation( const SearchRuntimeControls& runtime_controls, RuntimeInvocationState& runtime_state ) noexcept
	{
		runtime_state.run_nodes_visited = 0;
		runtime_state.stop_due_to_time_limit = false;
		runtime_state.stop_due_to_node_limit = false;
		runtime_state.run_start_time =
			begin_runtime_invocation(
				runtime_controls,
				runtime_state.progress_node_mask,
				runtime_state.stop_due_to_time_limit );
		runtime_sync_watchdog_control( runtime_state );
	}

	inline bool poll_runtime_stop_after_node_visit(
		const SearchRuntimeControls& runtime_controls,
		std::uint64_t run_nodes_visited,
		std::uint64_t progress_node_mask,
		const std::chrono::steady_clock::time_point& run_start_time,
		bool& stop_due_to_time_limit ) noexcept
	{
		if ( stop_due_to_time_limit )
			return true;
		const std::uint64_t effective_maximum_search_nodes = runtime_effective_maximum_search_nodes( runtime_controls );
		if ( effective_maximum_search_nodes != 0 && run_nodes_visited >= effective_maximum_search_nodes )
			return true;
		if ( ( run_nodes_visited & progress_node_mask ) == 0 )
		{
			const auto now = std::chrono::steady_clock::now();
			memory_governor_poll_if_needed( now );
			if ( runtime_time_limit_reached_at( runtime_controls.maximum_search_seconds, run_start_time, now ) )
			{
				stop_due_to_time_limit = true;
				return true;
			}
		}
		return false;
	}

	inline bool runtime_poll( const SearchRuntimeControls& runtime_controls, RuntimeInvocationState& runtime_state ) noexcept
	{
		runtime_pull_watchdog_stop_flags( runtime_state );
		if ( runtime_state.stop_due_to_time_limit || runtime_state.stop_due_to_node_limit )
			return true;
		const std::uint64_t effective_maximum_search_nodes = runtime_effective_maximum_search_nodes( runtime_controls );
		if ( effective_maximum_search_nodes != 0 && runtime_state.run_nodes_visited >= effective_maximum_search_nodes )
		{
			runtime_state.stop_due_to_node_limit = true;
			runtime_sync_watchdog_control( runtime_state );
			return true;
		}
		if ( ( runtime_state.run_nodes_visited & runtime_state.progress_node_mask ) == 0 )
		{
			const auto now = std::chrono::steady_clock::now();
			memory_governor_poll_if_needed( now );
			if ( runtime_time_limit_reached_at( runtime_controls.maximum_search_seconds, runtime_state.run_start_time, now ) )
			{
				runtime_state.stop_due_to_time_limit = true;
				runtime_sync_watchdog_control( runtime_state );
				return true;
			}
		}
		return false;
	}

	inline bool runtime_note_node_visit( const SearchRuntimeControls& runtime_controls, RuntimeInvocationState& runtime_state ) noexcept
	{
		++runtime_state.total_nodes_visited;
		++runtime_state.run_nodes_visited;
		runtime_sync_watchdog_control( runtime_state );
		return runtime_poll( runtime_controls, runtime_state );
	}

	inline bool runtime_maximum_search_nodes_hit( const SearchRuntimeControls& runtime_controls, std::uint64_t run_nodes_visited ) noexcept
	{
		const std::uint64_t effective_maximum_search_nodes = runtime_effective_maximum_search_nodes( runtime_controls );
		return effective_maximum_search_nodes != 0 && run_nodes_visited >= effective_maximum_search_nodes;
	}

	inline bool runtime_maximum_search_nodes_hit( const SearchRuntimeControls& runtime_controls, const RuntimeInvocationState& runtime_state ) noexcept
	{
		const std::uint64_t effective_maximum_search_nodes = runtime_effective_maximum_search_nodes( runtime_controls );
		const bool watchdog_stop =
			runtime_state.watchdog_control != nullptr &&
			runtime_state.watchdog_control->stop_due_to_node_limit.load( std::memory_order_relaxed );
		return effective_maximum_search_nodes != 0 && ( runtime_state.stop_due_to_node_limit || watchdog_stop || runtime_state.run_nodes_visited >= effective_maximum_search_nodes );
	}

	inline bool runtime_time_limit_hit( const SearchRuntimeControls& runtime_controls, bool stop_due_to_time_limit ) noexcept
	{
		return runtime_controls.maximum_search_seconds != 0 && stop_due_to_time_limit;
	}

	inline bool runtime_time_limit_hit( const SearchRuntimeControls& runtime_controls, const RuntimeInvocationState& runtime_state ) noexcept
	{
		const bool watchdog_stop =
			runtime_state.watchdog_control != nullptr &&
			runtime_state.watchdog_control->stop_due_to_time_limit.load( std::memory_order_relaxed );
		return runtime_controls.maximum_search_seconds != 0 && ( runtime_state.stop_due_to_time_limit || watchdog_stop );
	}

	inline double runtime_elapsed_seconds( const std::chrono::steady_clock::time_point& run_start_time ) noexcept
	{
		if ( run_start_time.time_since_epoch().count() == 0 )
			return 0.0;
		return std::chrono::duration<double>( std::chrono::steady_clock::now() - run_start_time ).count();
	}

	inline std::uint64_t runtime_elapsed_microseconds( const std::chrono::steady_clock::time_point& run_start_time ) noexcept
	{
		if ( run_start_time.time_since_epoch().count() == 0 )
			return 0;
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - run_start_time ).count();
		return ( elapsed <= 0 ) ? 0ull : static_cast<std::uint64_t>( elapsed );
	}

	inline double runtime_elapsed_seconds( const RuntimeInvocationState& runtime_state ) noexcept
	{
		return runtime_elapsed_seconds( runtime_state.run_start_time );
	}

	inline std::uint64_t runtime_elapsed_microseconds( const RuntimeInvocationState& runtime_state ) noexcept
	{
		return runtime_elapsed_microseconds( runtime_state.run_start_time );
	}

	struct RuntimeTimeLimitProbeState
	{
		const SearchRuntimeControls* runtime_controls = nullptr;
		RuntimeInvocationState*		runtime_state = nullptr;
		const std::chrono::steady_clock::time_point* run_start_time = nullptr;
		std::uint64_t maximum_search_seconds = 0;
		bool* stop_due_to_time_limit = nullptr;
	};

	inline RuntimeTimeLimitProbeState& runtime_time_limit_probe_state() noexcept
	{
		static thread_local RuntimeTimeLimitProbeState state {};
		return state;
	}

	inline bool runtime_time_limit_reached() noexcept
	{
		RuntimeTimeLimitProbeState& state = runtime_time_limit_probe_state();
		if ( state.runtime_controls != nullptr && state.runtime_state != nullptr )
		{
			runtime_pull_watchdog_stop_flags( *state.runtime_state );
			if ( state.runtime_state->stop_due_to_time_limit || state.runtime_state->stop_due_to_node_limit )
				return true;
			const std::uint64_t effective_maximum_search_nodes = runtime_effective_maximum_search_nodes( *state.runtime_controls );
			if ( effective_maximum_search_nodes != 0 && state.runtime_state->run_nodes_visited >= effective_maximum_search_nodes )
			{
				state.runtime_state->stop_due_to_node_limit = true;
				runtime_sync_watchdog_control( *state.runtime_state );
				return true;
			}
			const auto now = std::chrono::steady_clock::now();
			memory_governor_poll_if_needed( now );
			if ( runtime_time_limit_reached_at( state.runtime_controls->maximum_search_seconds, state.runtime_state->run_start_time, now ) )
			{
				state.runtime_state->stop_due_to_time_limit = true;
				runtime_sync_watchdog_control( *state.runtime_state );
				return true;
			}
			return false;
		}
		if ( state.stop_due_to_time_limit && *state.stop_due_to_time_limit )
			return true;
		if ( state.run_start_time == nullptr || state.stop_due_to_time_limit == nullptr || state.maximum_search_seconds == 0 )
			return false;
		if ( runtime_time_limit_reached_now( state.maximum_search_seconds, *state.run_start_time ) )
		{
			*state.stop_due_to_time_limit = true;
			return true;
		}
		return false;
	}

	struct ScopedRuntimeTimeLimitProbe
	{
		RuntimeTimeLimitProbeState previous {};

		ScopedRuntimeTimeLimitProbe( const SearchRuntimeControls& runtime_controls, RuntimeInvocationState& runtime_state )
			: previous( runtime_time_limit_probe_state() )
		{
			RuntimeTimeLimitProbeState& state = runtime_time_limit_probe_state();
			state.runtime_controls = &runtime_controls;
			state.runtime_state = &runtime_state;
			state.run_start_time = &runtime_state.run_start_time;
			state.maximum_search_seconds = runtime_controls.maximum_search_seconds;
			state.stop_due_to_time_limit = &runtime_state.stop_due_to_time_limit;
		}

		ScopedRuntimeTimeLimitProbe( const std::chrono::steady_clock::time_point& run_start_time, std::uint64_t maximum_search_seconds, bool& stop_due_to_time_limit )
			: previous( runtime_time_limit_probe_state() )
		{
			RuntimeTimeLimitProbeState& state = runtime_time_limit_probe_state();
			state.runtime_controls = nullptr;
			state.runtime_state = nullptr;
			state.run_start_time = &run_start_time;
			state.maximum_search_seconds = maximum_search_seconds;
			state.stop_due_to_time_limit = &stop_due_to_time_limit;
		}

		~ScopedRuntimeTimeLimitProbe()
		{
			runtime_time_limit_probe_state() = previous;
		}
	};

	// ============================================================================
	// Thread-safe output + per-thread progress prefix (shared)
	// ============================================================================

	// Global mutex to avoid interleaved multi-thread `std::cout` lines (best-effort).
	std::mutex& cout_mutex();

	// A per-thread prefix printed before "[Progress]" lines (e.g. "[Batch][Deep][job#12] ").
	// Use `ProgressPrefixGuard` to set/restore.
	const char* progress_prefix();
	void		set_progress_prefix( const char* prefix );
	void		print_progress_prefix( std::ostream& os );

	struct ProgressPrefixGuard
	{
		const char* previous = nullptr;
		explicit ProgressPrefixGuard( const char* prefix ) : previous( progress_prefix() )
		{
			set_progress_prefix( prefix );
		}
		~ProgressPrefixGuard()
		{
			set_progress_prefix( previous );
		}
	};

	// ============================================================================
	// iostream formatting guard (shared)
	// ============================================================================

	struct IosStateGuard
	{
		std::ostream&	   os;
		std::ios::fmtflags flags;
		std::streamsize	   precision;
		char			   fill;
		explicit IosStateGuard( std::ostream& o ) : os( o ), flags( o.flags() ), precision( o.precision() ), fill( o.fill() ) {}
		~IosStateGuard()
		{
			os.flags( flags );
			os.precision( precision );
			os.fill( fill );
		}
	};

	// ============================================================================
	// System memory info + utilities (shared)
	// ============================================================================

	struct SystemMemoryInfo
	{
		std::uint64_t total_physical_bytes = 0;
		std::uint64_t available_physical_bytes = 0;
		std::uint64_t process_rss_bytes = 0;	   // VmRSS / working set (best-effort)
		std::uint64_t committed_as_bytes = 0;	   // Linux: Committed_AS; Windows: CommitTotal
		std::uint64_t commit_limit_bytes = 0;	   // Linux: CommitLimit; Windows: CommitLimit
	};

	// Best-effort query; returns {0,0} if unsupported.
	SystemMemoryInfo query_system_memory_info();
	bool			 physical_memory_allocation_guard_active() noexcept;

	// Default poll function for `memory_governor_set_poll_fn()`.
	void governor_poll_system_memory_once();

	// Print a compact status line with VmRSS / MemAvailable / Committed_AS (best-effort).
	void print_system_memory_status_line( std::ostream& os, const SystemMemoryInfo& info, const char* prefix = nullptr );

	// ============================================================================
	// Workstation-greedy memory budgeting (must-live vs rebuildable pools)
	// ============================================================================

	struct MemoryBudget
	{
		std::uint64_t available_physical_bytes = 0;
		std::uint64_t headroom_bytes = 0;
		std::uint64_t total_budget_bytes = 0;
		std::uint64_t must_live_budget_bytes = 0;
		std::uint64_t rebuildable_budget_bytes = 0;
	};

	// Split `available - headroom` into MUST-LIVE and REBUILDABLE budgets.
	MemoryBudget compute_workstation_greedy_budget( const SystemMemoryInfo& info, std::uint64_t headroom_bytes, double must_live_fraction = 0.35 );

	class BudgetedMemoryPool
	{
	public:
		explicit BudgetedMemoryPool( const char* label );
		BudgetedMemoryPool( const BudgetedMemoryPool& ) = delete;
		BudgetedMemoryPool& operator=( const BudgetedMemoryPool& ) = delete;

		void		  set_budget_bytes( std::uint64_t bytes );
		std::uint64_t budget_bytes() const;
		std::uint64_t allocated_bytes() const;
		const char*	  label() const;

		// Returns nullptr on failure or budget exceeded.
		void* allocate( std::uint64_t bytes, bool touch_pages );
		void  release_all();

	private:
		struct Block
		{
			void*		  p = nullptr;
			std::uint64_t size = 0;
		};

		const char*			  label_ = nullptr;
		mutable std::mutex	  mutex_ {};
		std::vector<Block>	  blocks_ {};
		std::uint64_t		  budget_bytes_ = 0;	 // 0 = unlimited
		std::uint64_t		  allocated_bytes_ = 0;
	};

	BudgetedMemoryPool& must_live_pool();
	BudgetedMemoryPool& rebuildable_pool();
	void			   configure_memory_pools( const MemoryBudget& budget );
	void			   release_rebuildable_pool();

	void* alloc_must_live( std::uint64_t bytes, bool touch_pages = false );
	void* alloc_rebuildable( std::uint64_t bytes, bool touch_pages = true );

	// Optional hook called right before `release_rebuildable_pool()` frees memory.
	// Use this to drop/clear any metadata that may point into the rebuildable pool.
	using RebuildableCleanupCallback = void ( * )();
	void rebuildable_set_cleanup_fn( RebuildableCleanupCallback fn );

	// Memory pressure hooks: checkpoint -> release rebuildable -> degrade must-live (optional).
	using MemoryPressureCallback = void ( * )();
	void memory_pressure_set_checkpoint_fn( MemoryPressureCallback fn );
	void memory_pressure_set_must_live_degrade_fn( MemoryPressureCallback fn );
	void on_memory_pressure();

	// ============================================================================
	// Table budget helpers (cLAT / pDDT)
	// ============================================================================

	// cLAT memory estimate from paper: 2^{3(m-8)} * 1.2 GB (approx).
	std::uint64_t clat_estimated_bytes_for_m( unsigned m );
	unsigned	  clat_select_m_for_budget( std::uint64_t budget_bytes, unsigned min_m = 8, unsigned max_m = 16 );

	// Generic threshold chooser: `estimate_bytes(threshold)` must be monotonic decreasing w.r.t. threshold.
	template <class Estimator>
	inline double pddt_select_threshold_for_budget( Estimator&& estimate_bytes, double min_threshold, double max_threshold, std::uint64_t budget_bytes, int iterations = 32 )
	{
		if ( budget_bytes == 0 )
			return max_threshold;
		double lo = min_threshold;
		double hi = max_threshold;
		double best = max_threshold;
		for ( int i = 0; i < iterations; ++i )
		{
			const double mid = ( lo + hi ) * 0.5;
			const std::uint64_t est = static_cast<std::uint64_t>( estimate_bytes( mid ) );
			if ( est > budget_bytes )
			{
				lo = mid;  // threshold too low (table too big)
			}
			else
			{
				best = mid;
				hi = mid;  // can lower threshold
			}
		}
		return best;
	}

	inline double bytes_to_gibibytes( std::uint64_t bytes )
	{
		return double( bytes ) / double( 1024.0 * 1024.0 * 1024.0 );
	}

	enum class MemoryGateStatus : std::uint8_t
	{
		Ok = 0,
		Warn = 1,
		Reject = 2
	};

	struct MemoryGateEvaluation
	{
		std::uint64_t physical_available_bytes = 0;
		std::uint64_t must_live_bytes = 0;
		std::uint64_t optional_rebuildable_bytes = 0;
		double		  warn_fraction = 0.80;
		double		  reject_fraction = 0.95;
		double		  must_live_fraction_of_available = 0.0;
		MemoryGateStatus status = MemoryGateStatus::Ok;
	};

	const char*		  memory_gate_status_name( MemoryGateStatus status ) noexcept;
	MemoryGateEvaluation evaluate_memory_gate(
		std::uint64_t physical_available_bytes,
		std::uint64_t must_live_bytes,
		std::uint64_t optional_rebuildable_bytes,
		double warn_fraction = 0.80,
		double reject_fraction = 0.95 );

	struct SearchInvocationMetadata
	{
		std::uint64_t physical_available_bytes = 0;
		std::uint64_t estimated_must_live_bytes = 0;
		std::uint64_t estimated_optional_rebuildable_bytes = 0;
		MemoryGateStatus memory_gate_status = MemoryGateStatus::Ok;
		bool startup_memory_gate_advisory_only = false;
	};

	inline std::uint64_t compute_memory_headroom_bytes( std::uint64_t available_physical_bytes, std::uint64_t memory_headroom_mib, bool memory_headroom_mib_was_provided )
	{
		if ( available_physical_bytes == 0 )
			return 0;

		const std::uint64_t mebibyte_in_bytes = 1024ull * 1024ull;
		const std::uint64_t gibibyte_in_bytes = 1024ull * 1024ull * 1024ull;

		// Default rule (workstation-greedy): keep ~6-8 GiB headroom.
		// Rationale: time-first modes can be extremely memory-hungry; keep OS responsive while enabling large tables/caches.
		std::uint64_t headroom_bytes = std::min<std::uint64_t>( 8ull * gibibyte_in_bytes, available_physical_bytes / 8ull );
		headroom_bytes = std::max<std::uint64_t>( headroom_bytes, 6ull * gibibyte_in_bytes );

		// Optional override (still clamped to a small hard minimum to avoid "0 headroom => OS thrash").
		if ( memory_headroom_mib_was_provided )
		{
			headroom_bytes = memory_headroom_mib * mebibyte_in_bytes;
			headroom_bytes = std::max<std::uint64_t>( headroom_bytes, 256ull * mebibyte_in_bytes );
		}

		if ( available_physical_bytes < headroom_bytes )
			headroom_bytes = available_physical_bytes;
		return headroom_bytes;
	}

	class MemoryBallast
	{
	public:
		explicit MemoryBallast( std::uint64_t headroom_bytes );
		MemoryBallast( const MemoryBallast& ) = delete;
		MemoryBallast& operator=( const MemoryBallast& ) = delete;
		~MemoryBallast();

		void start();
		void stop();

		std::uint64_t headroom_bytes() const;
		std::uint64_t allocated_bytes() const;

	private:
		static constexpr std::uint64_t mebibyte_in_bytes = 1024ull * 1024ull;
		static constexpr std::uint64_t step_bytes = 64ull * mebibyte_in_bytes;		   // allocate/release in 64 MiB blocks
		static constexpr std::uint64_t hysteresis_bytes = 256ull * mebibyte_in_bytes;  // avoid oscillation

		std::uint64_t						 headroom_bytes_ = 0;
		std::unique_ptr<class NamedServiceThread> service_thread_ {};
		std::vector<void*>					 blocks_ {};
		std::uint64_t						 allocated_bytes_ = 0;

		void run();
		bool try_allocate_one_block();
		void try_free_one_block();
		void clear();
	};

	// ============================================================================
	// Shared runtime thread-pool + named service threads
	// ============================================================================

	class RuntimeTaskGroupSharedState;

	enum class RuntimeAsyncJobStatus : std::uint8_t
	{
		Queued = 0,
		Running = 1,
		StopRequested = 2,
		Completed = 3,
		Cancelled = 4,
		Failed = 5
	};

	inline const char* runtime_async_job_status_name( RuntimeAsyncJobStatus status ) noexcept
	{
		switch ( status )
		{
		case RuntimeAsyncJobStatus::Running:
			return "running";
		case RuntimeAsyncJobStatus::StopRequested:
			return "stop_requested";
		case RuntimeAsyncJobStatus::Completed:
			return "completed";
		case RuntimeAsyncJobStatus::Cancelled:
			return "cancelled";
		case RuntimeAsyncJobStatus::Failed:
			return "failed";
		case RuntimeAsyncJobStatus::Queued:
		default:
			return "queued";
		}
	}

	struct RuntimeTaskContext
	{
		std::size_t				 slot_index = 0;
		const std::string*		 current_name_ptr = nullptr;
		const std::atomic<bool>* stop_requested_ptr = nullptr;

		bool stop_requested() const noexcept
		{
			return stop_requested_ptr != nullptr && stop_requested_ptr->load( std::memory_order_relaxed );
		}

		const std::string& current_name() const noexcept
		{
			static const std::string empty {};
			return ( current_name_ptr != nullptr ) ? *current_name_ptr : empty;
		}
	};

	struct RuntimeWorkerSnapshot
	{
		std::size_t	   slot_index = 0;
		std::uint64_t generation = 0;
		bool		   alive = false;
		bool		   busy = false;
		bool		   stop_requested = false;
		std::string	   current_name {};
		std::thread::id thread_id {};
	};

	template <class JobFn, bool TakesContext = std::is_invocable_v<std::decay_t<JobFn>&, RuntimeTaskContext&>, bool TakesNoArgs = std::is_invocable_v<std::decay_t<JobFn>&>>
	struct RuntimeAsyncJobResultHelper;

	template <class JobFn, bool TakesNoArgs>
	struct RuntimeAsyncJobResultHelper<JobFn, true, TakesNoArgs>
	{
		using type = std::invoke_result_t<std::decay_t<JobFn>&, RuntimeTaskContext&>;
	};

	template <class JobFn>
	struct RuntimeAsyncJobResultHelper<JobFn, false, true>
	{
		using type = std::invoke_result_t<std::decay_t<JobFn>&>;
	};

	template <class JobFn>
	using RuntimeAsyncJobResultT = typename RuntimeAsyncJobResultHelper<JobFn>::type;

	class RuntimeTaskGroupHandle
	{
	public:
		RuntimeTaskGroupHandle() = default;

		bool valid() const noexcept
		{
			return bool( state_ );
		}

		bool done() const;
		void wait() const;
		void request_stop_all() const;
		bool request_stop_worker_by_name( const std::string& name ) const;
		std::vector<RuntimeWorkerSnapshot> snapshot_workers() const;
		std::size_t						 completed_count() const;

	private:
		friend RuntimeTaskGroupHandle start_named_worker_group( const std::string&, int, std::function<void( RuntimeTaskContext& )> );

		explicit RuntimeTaskGroupHandle( std::shared_ptr<RuntimeTaskGroupSharedState> state )
			: state_( std::move( state ) )
		{
		}

		std::shared_ptr<RuntimeTaskGroupSharedState> state_ {};
	};

	template <class ResultT>
	struct RuntimeAsyncJobSharedState
	{
		std::string						  job_name {};
		RuntimeTaskGroupHandle			  group {};
		std::atomic<RuntimeAsyncJobStatus> status { RuntimeAsyncJobStatus::Queued };
		mutable std::mutex				  mutex {};
		std::optional<ResultT>			  result {};
		std::exception_ptr				  exception {};
	};

	template <>
	struct RuntimeAsyncJobSharedState<void>
	{
		std::string						  job_name {};
		RuntimeTaskGroupHandle			  group {};
		std::atomic<RuntimeAsyncJobStatus> status { RuntimeAsyncJobStatus::Queued };
		mutable std::mutex				  mutex {};
		std::exception_ptr				  exception {};
	};

	template <class ResultT>
	class RuntimeAsyncJobHandle
	{
	public:
		RuntimeAsyncJobHandle() = default;

		bool valid() const noexcept
		{
			return bool( state_ );
		}

		bool done() const
		{
			return !state_ || state_->group.done();
		}

		RuntimeAsyncJobStatus status() const noexcept
		{
			return state_ ? state_->status.load( std::memory_order_relaxed ) : RuntimeAsyncJobStatus::Completed;
		}

		void wait() const
		{
			if ( !state_ )
				return;
			try
			{
				state_->group.wait();
			}
			catch ( ... )
			{
			}
		}

		RuntimeAsyncJobStatus wait_status() const
		{
			wait();
			return status();
		}

		void request_stop() const
		{
			if ( !state_ )
				return;
			const RuntimeAsyncJobStatus current = status();
			if ( current == RuntimeAsyncJobStatus::Queued || current == RuntimeAsyncJobStatus::Running )
				state_->status.store( RuntimeAsyncJobStatus::StopRequested, std::memory_order_relaxed );
			state_->group.request_stop_all();
		}

		std::vector<RuntimeWorkerSnapshot> snapshot_workers() const
		{
			return state_ ? state_->group.snapshot_workers() : std::vector<RuntimeWorkerSnapshot> {};
		}

		std::size_t completed_count() const
		{
			return state_ ? state_->group.completed_count() : 0u;
		}

		std::string job_name() const
		{
			return state_ ? state_->job_name : std::string {};
		}

		const ResultT& result() const
		{
			if ( !state_ )
				throw std::runtime_error( "async job handle is empty" );
			wait();
			std::exception_ptr ex {};
			{
				std::scoped_lock lk( state_->mutex );
				ex = state_->exception;
			}
			if ( ex )
				std::rethrow_exception( ex );
			const RuntimeAsyncJobStatus final_status = status();
			if ( final_status == RuntimeAsyncJobStatus::Cancelled || final_status == RuntimeAsyncJobStatus::StopRequested )
				throw std::runtime_error( "async job was cancelled before producing a result" );
			if ( final_status != RuntimeAsyncJobStatus::Completed )
				throw std::runtime_error( std::string( "async job did not complete successfully: " ) + runtime_async_job_status_name( final_status ) );
			std::scoped_lock lk( state_->mutex );
			if ( !state_->result.has_value() )
				throw std::runtime_error( "async job completed without storing a result" );
			return *state_->result;
		}

	private:
		template <class JobFn>
		friend auto submit_named_async_job( const std::string&, JobFn&& ) -> RuntimeAsyncJobHandle<RuntimeAsyncJobResultT<JobFn>>;

		explicit RuntimeAsyncJobHandle( std::shared_ptr<RuntimeAsyncJobSharedState<ResultT>> state )
			: state_( std::move( state ) )
		{
		}

		std::shared_ptr<RuntimeAsyncJobSharedState<ResultT>> state_ {};
	};

	template <>
	class RuntimeAsyncJobHandle<void>
	{
	public:
		RuntimeAsyncJobHandle() = default;

		bool valid() const noexcept
		{
			return bool( state_ );
		}

		bool done() const
		{
			return !state_ || state_->group.done();
		}

		RuntimeAsyncJobStatus status() const noexcept
		{
			return state_ ? state_->status.load( std::memory_order_relaxed ) : RuntimeAsyncJobStatus::Completed;
		}

		void wait() const
		{
			if ( !state_ )
				return;
			try
			{
				state_->group.wait();
			}
			catch ( ... )
			{
			}
		}

		RuntimeAsyncJobStatus wait_status() const
		{
			wait();
			return status();
		}

		void request_stop() const
		{
			if ( !state_ )
				return;
			const RuntimeAsyncJobStatus current = status();
			if ( current == RuntimeAsyncJobStatus::Queued || current == RuntimeAsyncJobStatus::Running )
				state_->status.store( RuntimeAsyncJobStatus::StopRequested, std::memory_order_relaxed );
			state_->group.request_stop_all();
		}

		std::vector<RuntimeWorkerSnapshot> snapshot_workers() const
		{
			return state_ ? state_->group.snapshot_workers() : std::vector<RuntimeWorkerSnapshot> {};
		}

		std::size_t completed_count() const
		{
			return state_ ? state_->group.completed_count() : 0u;
		}

		std::string job_name() const
		{
			return state_ ? state_->job_name : std::string {};
		}

		void get() const
		{
			if ( !state_ )
				throw std::runtime_error( "async job handle is empty" );
			wait();
			std::exception_ptr ex {};
			{
				std::scoped_lock lk( state_->mutex );
				ex = state_->exception;
			}
			if ( ex )
				std::rethrow_exception( ex );
			const RuntimeAsyncJobStatus final_status = status();
			if ( final_status == RuntimeAsyncJobStatus::Cancelled || final_status == RuntimeAsyncJobStatus::StopRequested )
				throw std::runtime_error( "async job was cancelled before completion" );
			if ( final_status != RuntimeAsyncJobStatus::Completed )
				throw std::runtime_error( std::string( "async job did not complete successfully: " ) + runtime_async_job_status_name( final_status ) );
		}

	private:
		template <class JobFn>
		friend auto submit_named_async_job( const std::string&, JobFn&& ) -> RuntimeAsyncJobHandle<RuntimeAsyncJobResultT<JobFn>>;

		explicit RuntimeAsyncJobHandle( std::shared_ptr<RuntimeAsyncJobSharedState<void>> state )
			: state_( std::move( state ) )
		{
		}

		std::shared_ptr<RuntimeAsyncJobSharedState<void>> state_ {};
	};

	std::vector<RuntimeWorkerSnapshot> snapshot_runtime_thread_pool_workers();

	RuntimeTaskGroupHandle start_named_worker_group(
		const std::string&						  group_name,
		int										  worker_thread_count,
		std::function<void( RuntimeTaskContext& )> worker_fn );

	inline void run_named_worker_group(
		const std::string&						  group_name,
		int										  worker_thread_count,
		std::function<void( RuntimeTaskContext& )> worker_fn )
	{
		start_named_worker_group( group_name, worker_thread_count, std::move( worker_fn ) ).wait();
	}

	void run_named_worker_group_with_monitor(
		const std::string&						  group_name,
		int										  worker_thread_count,
		std::function<void( RuntimeTaskContext& )> worker_fn,
		std::function<void()>					  monitor_fn );

	template <class JobFn>
	auto submit_named_async_job( const std::string& job_name, JobFn&& job_fn ) -> RuntimeAsyncJobHandle<RuntimeAsyncJobResultT<JobFn>>
	{
		using JobFnDecayed = std::decay_t<JobFn>;
		constexpr bool takes_context = std::is_invocable_v<JobFnDecayed&, RuntimeTaskContext&>;
		static_assert( takes_context || std::is_invocable_v<JobFnDecayed&>, "async job must be invocable with RuntimeTaskContext& or with no arguments" );

		using ResultT = RuntimeAsyncJobResultT<JobFn>;

		auto state = std::make_shared<RuntimeAsyncJobSharedState<ResultT>>();
		state->job_name = job_name.empty() ? std::string( "runtime-async-job" ) : job_name;

		auto wrapper =
			[ state, fn = JobFnDecayed( std::forward<JobFn>( job_fn ) ) ]( RuntimeTaskContext& context ) mutable {
				if ( context.stop_requested() )
				{
					state->status.store( RuntimeAsyncJobStatus::Cancelled, std::memory_order_relaxed );
					return;
				}

				state->status.store( RuntimeAsyncJobStatus::Running, std::memory_order_relaxed );
				try
				{
					if constexpr ( std::is_void_v<ResultT> )
					{
						if constexpr ( takes_context )
							std::invoke( fn, context );
						else
							std::invoke( fn );
					}
					else
					{
						ResultT value =
							[ & ]() -> ResultT {
								if constexpr ( takes_context )
									return std::invoke( fn, context );
								else
									return std::invoke( fn );
							}();
						std::scoped_lock lk( state->mutex );
						state->result.emplace( std::move( value ) );
					}

					state->status.store(
						context.stop_requested() ? RuntimeAsyncJobStatus::Cancelled : RuntimeAsyncJobStatus::Completed,
						std::memory_order_relaxed );
				}
				catch ( ... )
				{
					{
						std::scoped_lock lk( state->mutex );
						state->exception = std::current_exception();
					}
					state->status.store( RuntimeAsyncJobStatus::Failed, std::memory_order_relaxed );
					throw;
				}
			};

		state->group = start_named_worker_group( state->job_name, 1, std::move( wrapper ) );
		return RuntimeAsyncJobHandle<ResultT>( std::move( state ) );
	}

	class NamedServiceThread
	{
	public:
		NamedServiceThread() = default;
		NamedServiceThread( const NamedServiceThread& ) = delete;
		NamedServiceThread& operator=( const NamedServiceThread& ) = delete;
		~NamedServiceThread();

		void start( const std::string& name, std::function<void()> fn );
		void stop();

		bool stop_requested() const noexcept
		{
			return stop_requested_.load( std::memory_order_relaxed );
		}

		bool alive() const noexcept
		{
			return alive_.load( std::memory_order_relaxed );
		}

		std::string name() const;

	private:
		mutable std::mutex mutex_ {};
		std::string		   name_ {};
		std::thread		   worker_ {};
		std::atomic<bool>  stop_requested_ { false };
		std::atomic<bool>  alive_ { false };
	};

	inline int resolve_worker_thread_count_for_command_line_interface( std::size_t batch_job_count, bool batch_job_file_was_provided, int batch_thread_count )
	{
		if ( batch_job_count == 0 && !batch_job_file_was_provided )
			return 1;
		const unsigned hardware_thread_concurrency = std::thread::hardware_concurrency();
		const int	   automatic_thread_count = int( ( hardware_thread_concurrency == 0 ) ? 1 : hardware_thread_concurrency );
		return ( batch_thread_count > 0 ) ? batch_thread_count : automatic_thread_count;
	}

	inline int clamp_worker_thread_count( int thread_count )
	{
		return std::max( 1, thread_count );
	}

	template <class WorkerFn>
	inline void run_worker_threads( int worker_thread_count, WorkerFn&& worker_fn )
	{
		run_named_worker_group(
			"runtime-worker",
			worker_thread_count,
			[ fn = std::forward<WorkerFn>( worker_fn ) ]( RuntimeTaskContext& context ) mutable {
				fn( int( context.slot_index ) );
			} );
	}

	template <class WorkerFn, class MonitorFn>
	inline void run_worker_threads_with_monitor( int worker_thread_count, WorkerFn&& worker_fn, MonitorFn&& monitor_fn )
	{
		run_named_worker_group_with_monitor(
			"runtime-worker",
			worker_thread_count,
			[ fn = std::forward<WorkerFn>( worker_fn ) ]( RuntimeTaskContext& context ) mutable {
				fn( int( context.slot_index ) );
			},
			std::function<void()>( std::forward<MonitorFn>( monitor_fn ) ) );
	}

	// ============================================================================
	// Hash-table reserve helpers (shared)
	// ============================================================================

	inline std::size_t round_up_power_of_two( std::size_t v )
	{
		if ( v <= 1 )
			return 1;
		// Round up to power-of-two using bit hacks on size_t.
		v--;
		for ( std::size_t shift = 1; shift < ( sizeof( std::size_t ) * 8 ); shift <<= 1 )
			v |= ( v >> shift );
		v++;
		return v;
	}

	// Reserve policy helpers (avoid huge upfront bucket allocations).
	inline std::size_t compute_initial_cache_reserve_hint( std::size_t cap )
	{
		if ( cap == 0 )
			return 0;
		if ( cap <= 64u )
			return cap;
		if ( cap <= 256u )
			return 64u;
		if ( cap <= 4096u )
			return 128u;
		if ( cap <= 65536u )
			return 256u;
		if ( cap <= 1048576u )
			return 512u;
		return 1024u;
	}

	inline std::size_t compute_next_cache_reserve_hint( std::size_t current_hint, std::size_t cap )
	{
		if ( cap == 0 )
			return 0;
		const std::size_t base = compute_initial_cache_reserve_hint( cap );
		if ( current_hint == 0 )
			return base;
		const std::size_t next =
			( current_hint < 4096u ) ?
				( current_hint * 2u ) :
				( current_hint + std::max<std::size_t>( 1024u, current_hint / 2u ) );
		return std::min<std::size_t>( cap, next );
	}

	inline std::size_t budgeted_reserve_target( std::size_t current_size, std::size_t desired_target, std::uint64_t estimated_bytes_per_entry )
	{
		// Under memory pressure, do not grow containers at all (stability-first).
		if ( memory_governor_in_pressure() )
			return current_size;

		// `reserve(n)` takes an element-count target, not bytes. We map remaining PMR budget to a conservative
		// max additional element count, so we avoid throwing in the common "not enough left" case.
		if ( desired_target <= current_size )
			return current_size;
		if ( estimated_bytes_per_entry == 0 )
			return desired_target;

		const std::uint64_t remain = pmr_bounded_resource().remaining_bytes();
		if ( remain == std::numeric_limits<std::uint64_t>::max() )
			return desired_target;	// unlimited budget

		const std::uint64_t max_extra_u64 = remain / estimated_bytes_per_entry;
		const std::size_t	max_extra = ( max_extra_u64 > std::uint64_t( std::numeric_limits<std::size_t>::max() ) ) ? std::numeric_limits<std::size_t>::max() : std::size_t( max_extra_u64 );
		const std::size_t	budgeted = current_size + max_extra;
		return std::min( desired_target, budgeted );
	}

	inline std::size_t compute_initial_memo_reserve_hint( std::size_t hint )
	{
		if ( hint == 0 )
			return 0;
		if ( hint <= 64u )
			return hint;
		if ( hint <= 512u )
			return 64u;
		if ( hint <= 8192u )
			return 128u;
		if ( hint <= 131072u )
			return 256u;
		return 512u;
	}

	// ============================================================================
	// Sharded shared cache (cross-thread, best-effort)
	// ============================================================================

	template <class Key, class Value, class Hash = std::hash<Key>>
	class ShardedSharedCache
	{
	public:
		ShardedSharedCache() = default;
		ShardedSharedCache( const ShardedSharedCache& ) = delete;
		ShardedSharedCache& operator=( const ShardedSharedCache& ) = delete;

		void configure( std::size_t total_entries, std::size_t shard_count )
		{
			disabled_due_to_oom_.store( false, std::memory_order_relaxed );
			// Releasing here is safe because configure is called before worker threads start.
			pool_.release();
			if ( total_entries == 0 )
			{
				shards_.clear();
				per_shard_cap_ = 0;
				shard_mask_ = 0;
				return;
			}
			const std::size_t sc = round_up_power_of_two( std::max<std::size_t>( 1, shard_count ) );
			shard_mask_ = sc - 1;
			per_shard_cap_ = ( total_entries + sc - 1 ) / sc;  // ceil
			shards_.clear();
			for ( std::size_t i = 0; i < sc; ++i )
			{
				auto shard = std::make_unique<Shard>( &pool_ );
				shard->map.max_load_factor( 0.7f );
				shards_.push_back( std::move( shard ) );
			}
		}

		bool enabled() const
		{
			return !disabled_due_to_oom_.load( std::memory_order_relaxed ) && !shards_.empty() && per_shard_cap_ != 0;
		}
		std::size_t shard_count() const
		{
			return shards_.size();
		}
		std::size_t per_shard_cap() const
		{
			return per_shard_cap_;
		}

		bool try_get( const Key& key, Value& out ) const
		{
			if ( !enabled() )
				return false;
			const std::size_t idx = Hash {}( key )&shard_mask_;
			const Shard&	  shard = *shards_[ idx ];
			std::scoped_lock  lk( shard.m );
			auto			  it = shard.map.find( key );
			if ( it == shard.map.end() )
				return false;
			out = it->second;
			return true;
		}

		void try_put( const Key& key, const Value& value )
		{
			if ( !enabled() )
				return;
			if ( memory_governor_in_pressure() )
				return;
			const std::size_t idx = Hash {}( key )&shard_mask_;
			Shard&			  shard = *shards_[ idx ];
			std::scoped_lock  lk( shard.m );
			if ( shard.map.size() >= per_shard_cap_ )
				return;
			try
			{
				shard.map.emplace( key, value );
			}
			catch ( const std::bad_alloc& )
			{
				// Disable further shared-cache use to avoid repeated allocations.
				disabled_due_to_oom_.store( true, std::memory_order_relaxed );
				pmr_report_oom_once( "shared_cache.emplace" );
			}
		}

		// Explicit teardown with progress printing (to avoid "looks hung" shutdown when maps are huge).
		// Safe to call only when no other threads are using the cache (e.g., after worker threads join).
		void clear_and_release_with_progress( const char* label )
		{
			const std::size_t n = shards_.size();
			if ( label )
			{
				std::cout << "[Cleanup] " << label << " shards=" << n;
				if ( disabled_due_to_oom_.load( std::memory_order_relaxed ) )
					std::cout << " (disabled_due_to_oom)";
				std::cout << "\n";
			}

			for ( std::size_t i = 0; i < n; ++i )
			{
				if ( label && ( ( i % 16 ) == 0 || i + 1 == n ) )
				{
					std::cout << "[Cleanup] " << label << " " << ( i + 1 ) << "/" << n << " (" << std::fixed << std::setprecision( 1 ) << ( n ? ( 100.0 * double( i + 1 ) / double( n ) ) : 100.0 ) << "%)\n" << std::defaultfloat;
				}
				Shard&			 shard = *shards_[ i ];
				std::scoped_lock lk( shard.m );
				shard.map.clear();
				shard.map.rehash( 0 );
			}

			shards_.clear();
			per_shard_cap_ = 0;
			shard_mask_ = 0;
			disabled_due_to_oom_.store( false, std::memory_order_relaxed );
			pool_.release();
		}

	private:
		struct Shard
		{
			mutable std::mutex						  m;
			std::pmr::unordered_map<Key, Value, Hash> map;
			explicit Shard( std::pmr::memory_resource* r ) : map( r ) {}
		};
		std::vector<std::unique_ptr<Shard>>	 shards_ {};
		std::size_t							 per_shard_cap_ = 0;
		std::size_t							 shard_mask_ = 0;
		std::pmr::synchronized_pool_resource pool_ { &pmr_bounded_resource() };
		std::atomic<bool>					 disabled_due_to_oom_ { false };
	};

	// ============================================================================
	// Best-weight memoization (by depth, best-effort)
	// ============================================================================

	template <class Key, class Weight, class Hash = std::hash<Key>, class KeyEq = std::equal_to<Key>>
	class BestWeightMemoizationByDepth
	{
	public:
		BestWeightMemoizationByDepth() : pool_( &pmr_bounded_resource() ) {}
		BestWeightMemoizationByDepth( const BestWeightMemoizationByDepth& ) = delete;
		BestWeightMemoizationByDepth& operator=( const BestWeightMemoizationByDepth& ) = delete;

		void initialize( std::size_t depth_count, bool enable, const char* oom_tag_init = nullptr )
		{
			enabled_ = enable;
			disabled_due_to_oom_ = false;
			maps_.clear();
			pool_.release();
			if ( !enable || depth_count == 0 )
				return;

			try
			{
				for ( std::size_t i = 0; i < depth_count; ++i )
				{
					maps_.emplace_back( &pool_ );
					maps_.back().max_load_factor( 0.7f );
				}
			}
			catch ( const std::bad_alloc& )
			{
				disable_and_release_( oom_tag_init ? oom_tag_init : "memoization.init" );
			}
		}

		bool enabled() const
		{
			return enabled_ && !disabled_due_to_oom_;
		}

		template <class Writer>
		bool serialize( Writer& w ) const
		{
			static_assert( std::is_integral_v<Key> && std::is_integral_v<Weight>, "Memoization serialization requires integral key/weight." );
			const bool on = enabled();
			w.write_u8( on ? 1u : 0u );
			w.write_u64( static_cast<std::uint64_t>( maps_.size() ) );
			if ( !on )
				return w.ok();

			for ( const auto& mp : maps_ )
			{
				w.write_u64( static_cast<std::uint64_t>( mp.size() ) );
				for ( const auto& kv : mp )
				{
					write_integral_( w, kv.first );
					write_integral_( w, kv.second );
				}
			}
			return w.ok();
		}

		template <class Reader>
		bool deserialize( Reader& r )
		{
			static_assert( std::is_integral_v<Key> && std::is_integral_v<Weight>, "Memoization serialization requires integral key/weight." );
			std::uint8_t enabled_flag = 0;
			std::uint64_t depth_count = 0;
			if ( !r.read_u8( enabled_flag ) )
				return false;
			if ( !r.read_u64( depth_count ) )
				return false;

			initialize( static_cast<std::size_t>( depth_count ), enabled_flag != 0u, "memoization.deserialize.init" );
			if ( !( enabled_flag != 0u ) )
				return true;

			bool store = enabled();
			for ( std::size_t depth = 0; depth < depth_count; ++depth )
			{
				std::uint64_t count = 0;
				if ( !r.read_u64( count ) )
					return false;

				for ( std::uint64_t i = 0; i < count; ++i )
				{
					Key key {};
					Weight weight {};
					if ( !read_integral_( r, key ) )
						return false;
					if ( !read_integral_( r, weight ) )
						return false;
					if ( store )
					{
						try
						{
							maps_[ depth ].try_emplace( key, weight );
						}
						catch ( const std::bad_alloc& )
						{
							disable_and_release_( "memoization.deserialize.emplace" );
							store = false;
						}
					}
				}
			}
			return true;
		}

		void clone_from( const BestWeightMemoizationByDepth& other, const char* oom_tag_init = nullptr, const char* oom_tag_reserve = nullptr, const char* oom_tag_emplace = nullptr )
		{
			( void )oom_tag_reserve;
			initialize(
				other.maps_.size(),
				other.enabled(),
				oom_tag_init ? oom_tag_init : "memoization.clone.init" );
			if ( !enabled() || !other.enabled() )
				return;

			for ( std::size_t depth = 0; depth < other.maps_.size(); ++depth )
			{
				const auto& src = other.maps_[ depth ];
				for ( const auto& kv : src )
				{
					try
					{
						maps_[ depth ].try_emplace( kv.first, kv.second );
					}
					catch ( const std::bad_alloc& )
					{
						disable_and_release_( oom_tag_emplace ? oom_tag_emplace : "memoization.clone.emplace" );
						return;
					}
				}
			}
		}

		// Returns true if the caller should PRUNE this node (already seen <= weight).
		bool should_prune_and_update( std::size_t depth, const Key& key, const Weight& weight, bool disable_when_memory_pressure, bool reserve_on_first_use, std::size_t reserve_hint, std::uint64_t estimated_bytes_per_entry, const char* oom_tag_reserve, const char* oom_tag_emplace )
		{
			( void )reserve_on_first_use;
			( void )reserve_hint;
			( void )estimated_bytes_per_entry;
			( void )oom_tag_reserve;
			if ( !enabled() )
				return false;
			if ( disable_when_memory_pressure && memory_governor_in_pressure() )
				return false;
			if ( depth >= maps_.size() )
				return false;

			auto& mp = maps_[ depth ];

			try
			{
				auto [ it, inserted ] = mp.try_emplace( key, weight );
				if ( !inserted )
				{
					if ( it->second <= weight )
						return true;
					it->second = weight;
				}
			}
			catch ( const std::bad_alloc& )
			{
				disable_and_release_( oom_tag_emplace ? oom_tag_emplace : "memoization.emplace" );
				return false;
			}

			return false;
		}

	private:
		void disable_and_release_( const char* where )
		{
			disabled_due_to_oom_ = true;
			pmr_report_oom_once( where ? where : "memoization.oom" );
			maps_.clear();
			pool_.release();
		}

		template <class Writer, class T>
		static void write_integral_( Writer& w, T value )
		{
			using U = std::make_unsigned_t<T>;
			const U u = static_cast<U>( value );
			if constexpr ( sizeof( U ) == 1 )
				w.write_u8( static_cast<std::uint8_t>( u ) );
			else if constexpr ( sizeof( U ) == 2 )
				w.write_u16( static_cast<std::uint16_t>( u ) );
			else if constexpr ( sizeof( U ) == 4 )
				w.write_u32( static_cast<std::uint32_t>( u ) );
			else if constexpr ( sizeof( U ) == 8 )
				w.write_u64( static_cast<std::uint64_t>( u ) );
		}

		template <class Reader, class T>
		static bool read_integral_( Reader& r, T& value )
		{
			using U = std::make_unsigned_t<T>;
			U u {};
			bool ok = false;
			if constexpr ( sizeof( U ) == 1 )
			{
				std::uint8_t tmp = 0;
				ok = r.read_u8( tmp );
				u = static_cast<U>( tmp );
			}
			else if constexpr ( sizeof( U ) == 2 )
			{
				std::uint16_t tmp = 0;
				ok = r.read_u16( tmp );
				u = static_cast<U>( tmp );
			}
			else if constexpr ( sizeof( U ) == 4 )
			{
				std::uint32_t tmp = 0;
				ok = r.read_u32( tmp );
				u = static_cast<U>( tmp );
			}
			else if constexpr ( sizeof( U ) == 8 )
			{
				std::uint64_t tmp = 0;
				ok = r.read_u64( tmp );
				u = static_cast<U>( tmp );
			}
			if ( !ok )
				return false;
			value = static_cast<T>( u );
			return true;
		}

		bool														   enabled_ = false;
		bool														   disabled_due_to_oom_ = false;
		std::pmr::unsynchronized_pool_resource						   pool_;
		std::vector<std::pmr::unordered_map<Key, Weight, Hash, KeyEq>> maps_;
	};

}  // namespace TwilightDream::runtime_component

#endif
