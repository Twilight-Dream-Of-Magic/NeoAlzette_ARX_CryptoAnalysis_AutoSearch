#pragma once
#if !defined( TWILIGHTDREAM_RUNTIME_COMPONENT_HPP )
#define TWILIGHTDREAM_RUNTIME_COMPONENT_HPP

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <source_location>
#include <sstream>
#include <string>
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
	std::string hex8( std::uint32_t v );

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
	};

	// Best-effort query; returns {0,0} if unsupported.
	SystemMemoryInfo query_system_memory_info();

	// Default poll function for `memory_governor_set_poll_fn()`.
	void governor_poll_system_memory_once();

	inline double bytes_to_gibibytes( std::uint64_t bytes )
	{
		return double( bytes ) / double( 1024.0 * 1024.0 * 1024.0 );
	}

	inline std::uint64_t compute_memory_headroom_bytes( std::uint64_t available_physical_bytes, std::uint64_t memory_headroom_mib, bool memory_headroom_mib_was_provided )
	{
		if ( available_physical_bytes == 0 )
			return 0;

		const std::uint64_t mebibyte_in_bytes = 1024ull * 1024ull;
		const std::uint64_t gibibyte_in_bytes = 1024ull * 1024ull * 1024ull;

		// Default rule: keep max(2 GiB, min(4 GiB, available/10)).
		// Rationale: time-first modes can be extremely memory-hungry; 512 MiB headroom is too risky on Windows.
		std::uint64_t headroom_bytes = std::min<std::uint64_t>( 4ull * gibibyte_in_bytes, available_physical_bytes / 10ull );
		headroom_bytes = std::max<std::uint64_t>( headroom_bytes, 2ull * gibibyte_in_bytes );

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

		std::uint64_t	   headroom_bytes_ = 0;
		std::atomic<bool>  running_ { false };
		std::thread		   worker_ {};
		std::vector<void*> blocks_ {};
		std::uint64_t	   allocated_bytes_ = 0;

		void run();
		bool try_allocate_one_block();
		void try_free_one_block();
		void clear();
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
		const int				 n = clamp_worker_thread_count( worker_thread_count );
		std::vector<std::thread> threads;
		threads.reserve( std::size_t( n ) );
		for ( int t = 0; t < n; ++t )
			threads.emplace_back( [ &, t ]() { worker_fn( t ); } );
		for ( auto& th : threads )
			th.join();
	}

	template <class WorkerFn, class MonitorFn>
	inline void run_worker_threads_with_monitor( int worker_thread_count, WorkerFn&& worker_fn, MonitorFn&& monitor_fn )
	{
		const int				 n = clamp_worker_thread_count( worker_thread_count );
		std::vector<std::thread> threads;
		threads.reserve( std::size_t( n ) );
		std::thread monitor_thread( std::forward<MonitorFn>( monitor_fn ) );
		for ( int t = 0; t < n; ++t )
			threads.emplace_back( [ &, t ]() { worker_fn( t ); } );
		for ( auto& th : threads )
			th.join();
		if ( monitor_thread.joinable() )
			monitor_thread.join();
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
		// For small caps, reserving the full cap is fine and avoids rehash overhead.
		if ( cap <= 16384u )
			return cap;
		// For larger caps, reserve a fraction to avoid committing too much memory upfront.
		// Clamp to a reasonable range to keep hash tables stable.
		std::size_t h = cap / 8;  // 12.5% of cap
		h = std::clamp<std::size_t>( h, 16384u, 262144u );
		return std::min( h, cap );
	}

	inline std::size_t compute_next_cache_reserve_hint( std::size_t current_hint, std::size_t cap )
	{
		if ( cap == 0 )
			return 0;
		const std::size_t base = compute_initial_cache_reserve_hint( cap );
		const std::size_t next = ( current_hint != 0 ) ? ( current_hint * 2 ) : base;
		// Keep a safety ceiling; beyond this, unordered_map bucket counts can become huge and memory-hungry.
		return std::min<std::size_t>( cap, std::min<std::size_t>( 1'000'000u, next ) );
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
		// hint is already clamped by caller to [4096..1'000'000].
		if ( hint <= 16384u )
			return hint;
		std::size_t h = hint / 16;	// 6.25% of hint
		h = std::clamp<std::size_t>( h, 16384u, 131072u );
		return std::min( h, hint );
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
			// Small reserve to reduce early rehashing without committing full memory upfront.
			const std::size_t reserve_hint = std::min<std::size_t>( per_shard_cap_, 16384 );
			shards_.clear();
			shards_.reserve( sc );
			for ( std::size_t i = 0; i < sc; ++i )
			{
				auto shard = std::make_unique<Shard>( &pool_ );
				try
				{
					shard->map.reserve( reserve_hint );
				}
				catch ( const std::bad_alloc& )
				{
					// Shared cache is optional. If we can't reserve it, disable it for this run.
					disabled_due_to_oom_.store( true, std::memory_order_relaxed );
					pmr_report_oom_once( "shared_cache.reserve" );
					shards_.clear();
					per_shard_cap_ = 0;
					shard_mask_ = 0;
					return;
				}
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
				maps_.reserve( depth_count );
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

		// Returns true if the caller should PRUNE this node (already seen <= weight).
		bool should_prune_and_update( std::size_t depth, const Key& key, const Weight& weight, bool disable_when_memory_pressure, bool reserve_on_first_use, std::size_t reserve_hint, std::uint64_t estimated_bytes_per_entry, const char* oom_tag_reserve, const char* oom_tag_emplace )
		{
			if ( !enabled() )
				return false;
			if ( disable_when_memory_pressure && memory_governor_in_pressure() )
				return false;
			if ( depth >= maps_.size() )
				return false;

			auto& mp = maps_[ depth ];
			if ( reserve_on_first_use && mp.empty() && reserve_hint != 0 )
			{
				try
				{
					const std::size_t target = budgeted_reserve_target( mp.size(), compute_initial_memo_reserve_hint( reserve_hint ), estimated_bytes_per_entry );
					mp.reserve( target );
				}
				catch ( const std::bad_alloc& )
				{
					disable_and_release_( oom_tag_reserve ? oom_tag_reserve : "memoization.reserve" );
					return false;
				}
			}

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

		bool														   enabled_ = false;
		bool														   disabled_due_to_oom_ = false;
		std::pmr::unsynchronized_pool_resource						   pool_;
		std::vector<std::pmr::unordered_map<Key, Weight, Hash, KeyEq>> maps_;
	};

}  // namespace TwilightDream::runtime_component

#endif
