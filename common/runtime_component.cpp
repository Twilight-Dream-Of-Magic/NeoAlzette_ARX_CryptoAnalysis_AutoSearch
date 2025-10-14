#include "runtime_component.hpp"

#include <algorithm>
#include <thread>

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined( __linux__ )
#include <sys/sysinfo.h>
#endif

namespace TwilightDream::runtime_component
{
	namespace
	{
		std::atomic<bool>		   g_pmr_oom_seen { false };
		std::atomic<bool>		   g_pmr_default_resource_installed { false };
		std::atomic<std::uint64_t> g_pmr_run_epoch { 0 };

		// Memory governor state (closed-loop stability for time-first runs).
		std::atomic<bool>		   g_memory_governor_enabled { false };
		std::atomic<bool>		   g_memory_pressure { false };
		std::atomic<std::uint64_t> g_memory_governor_headroom_bytes { 0 };
		std::atomic<std::uint64_t> g_memory_governor_hysteresis_bytes { 256ull * 1024ull * 1024ull };	// 256 MiB
		std::atomic<std::uint64_t> g_memory_governor_tight_remaining_threshold_bytes { 0 };				// derived from headroom
		std::atomic<std::uint64_t> g_memory_governor_limit_slack_bytes { 128ull * 1024ull * 1024ull };	// 128 MiB

		std::atomic<MemoryGovernorPollFn> g_memory_governor_poll_fn { nullptr };
		std::atomic<std::uint64_t>		  g_memory_governor_last_poll_ns { 0 };
		constexpr std::uint64_t			  g_memory_governor_poll_interval_ns = 5ull * 1000ull * 1000ull * 1000ull;	// 5 seconds

		thread_local const char* g_progress_prefix = nullptr;
	}  // namespace

	BoundedMemoryResource::BoundedMemoryResource( std::pmr::memory_resource* upstream ) : upstream_( upstream ? upstream : std::pmr::new_delete_resource() ) {}

	void BoundedMemoryResource::set_limit_bytes( std::uint64_t new_limit_bytes )
	{
		if ( new_limit_bytes == 0 )
		{
			limit_bytes_.store( 0, std::memory_order_relaxed );	 // 0 = unlimited
			return;
		}
		// Never set below already-allocated bytes (would cause immediate failures).
		const std::uint64_t used = allocated_bytes_.load( std::memory_order_relaxed );
		if ( new_limit_bytes < used )
			new_limit_bytes = used;
		limit_bytes_.store( new_limit_bytes, std::memory_order_relaxed );
	}

	std::uint64_t BoundedMemoryResource::limit_bytes() const
	{
		return limit_bytes_.load( std::memory_order_relaxed );
	}

	std::uint64_t BoundedMemoryResource::allocated_bytes() const
	{
		return allocated_bytes_.load( std::memory_order_relaxed );
	}

	std::uint64_t BoundedMemoryResource::remaining_bytes() const
	{
		const std::uint64_t lim = limit_bytes_.load( std::memory_order_relaxed );
		if ( lim == 0 )
			return std::numeric_limits<std::uint64_t>::max();  // unlimited
		const std::uint64_t used = allocated_bytes_.load( std::memory_order_relaxed );
		return ( used >= lim ) ? 0 : ( lim - used );
	}

	void* BoundedMemoryResource::do_allocate( std::size_t bytes, std::size_t alignment )
	{
		const std::uint64_t b = static_cast<std::uint64_t>( bytes );
		const std::uint64_t lim = limit_bytes_.load( std::memory_order_relaxed );
		if ( lim != 0 )
		{
			const std::uint64_t old = allocated_bytes_.fetch_add( b, std::memory_order_relaxed );
			const std::uint64_t now = old + b;
			if ( now > lim )
			{
				allocated_bytes_.fetch_sub( b, std::memory_order_relaxed );
				throw std::bad_alloc();
			}
		}
		else
		{
			allocated_bytes_.fetch_add( b, std::memory_order_relaxed );
		}

		try
		{
			return upstream_->allocate( bytes, alignment );
		}
		catch ( ... )
		{
			allocated_bytes_.fetch_sub( b, std::memory_order_relaxed );
			throw;
		}
	}

	void BoundedMemoryResource::do_deallocate( void* p, std::size_t bytes, std::size_t alignment )
	{
		upstream_->deallocate( p, bytes, alignment );
		allocated_bytes_.fetch_sub( static_cast<std::uint64_t>( bytes ), std::memory_order_relaxed );
	}

	bool BoundedMemoryResource::do_is_equal( const std::pmr::memory_resource& other ) const noexcept
	{
		return this == &other;
	}

	BoundedMemoryResource& pmr_bounded_resource()
	{
		static BoundedMemoryResource* r = new BoundedMemoryResource( std::pmr::new_delete_resource() );
		return *r;
	}

	std::uint64_t pmr_run_epoch()
	{
		return g_pmr_run_epoch.load( std::memory_order_relaxed );
	}

	void pmr_report_oom_once( const char* where, const std::source_location& loc )
	{
		if ( !g_pmr_oom_seen.exchange( true, std::memory_order_relaxed ) )
		{
			std::cerr << "[PMR][OOM] memory budget exceeded";
			if ( where )
				std::cerr << " at " << where;
			std::cerr << " @" << loc.file_name() << ":" << loc.line() << " (" << loc.function_name() << ")";
#if !defined( NDEBUG )
			std::cerr << " [debug]";
#endif
			std::cerr << " (disabling some caches/memoization to continue)\n";
		}
	}

	std::uint64_t pmr_suggest_limit_bytes( std::uint64_t available_physical_bytes, std::uint64_t headroom_bytes )
	{
		if ( available_physical_bytes == 0 )
			return 0;  // unknown: unlimited (best-effort OOM handling still applies)
		const std::uint64_t budget = ( available_physical_bytes > headroom_bytes ) ? ( available_physical_bytes - headroom_bytes ) : 0;
		// Keep a little slack for non-PMR allocations and runtime overhead.
		return ( budget * 9ull ) / 10ull;
	}

	void pmr_configure_for_run( std::uint64_t available_physical_bytes, std::uint64_t headroom_bytes )
	{
		const std::uint64_t limit = pmr_suggest_limit_bytes( available_physical_bytes, headroom_bytes );
		pmr_bounded_resource().set_limit_bytes( limit );
		( void )std::pmr::set_default_resource( &pmr_bounded_resource() );
		g_pmr_default_resource_installed.store( true, std::memory_order_relaxed );
		g_pmr_run_epoch.fetch_add( 1, std::memory_order_relaxed );
	}

	void memory_governor_enable_for_run( std::uint64_t headroom_bytes )
	{
		g_memory_governor_enabled.store( true, std::memory_order_relaxed );
		g_memory_governor_headroom_bytes.store( headroom_bytes, std::memory_order_relaxed );
		// Start checking system memory once PMR remaining drops below max(headroom, 2 GiB).
		const std::uint64_t min_thresh = 2ull * 1024ull * 1024ull * 1024ull;
		const std::uint64_t thresh = std::max( headroom_bytes, min_thresh );
		g_memory_governor_tight_remaining_threshold_bytes.store( thresh, std::memory_order_relaxed );
		g_memory_pressure.store( false, std::memory_order_relaxed );
	}

	void memory_governor_disable_for_run()
	{
		g_memory_governor_enabled.store( false, std::memory_order_relaxed );
		g_memory_pressure.store( false, std::memory_order_relaxed );
		g_memory_governor_poll_fn.store( nullptr, std::memory_order_relaxed );
	}

	bool memory_governor_in_pressure()
	{
		return g_memory_governor_enabled.load( std::memory_order_relaxed ) && g_memory_pressure.load( std::memory_order_relaxed );
	}

	void memory_governor_set_poll_fn( MemoryGovernorPollFn fn )
	{
		g_memory_governor_poll_fn.store( fn, std::memory_order_relaxed );
		g_memory_governor_last_poll_ns.store( 0, std::memory_order_relaxed );
	}

	void memory_governor_poll_if_needed( std::chrono::steady_clock::time_point now )
	{
		if ( !g_memory_governor_enabled.load( std::memory_order_relaxed ) )
			return;

		const std::uint64_t remain = pmr_bounded_resource().remaining_bytes();
		const std::uint64_t tight_threshold = g_memory_governor_tight_remaining_threshold_bytes.load( std::memory_order_relaxed );
		const bool			tight_by_budget = ( remain != std::numeric_limits<std::uint64_t>::max() ) && ( remain <= tight_threshold );
		const bool			pressure = g_memory_pressure.load( std::memory_order_relaxed );
		if ( !( tight_by_budget || pressure ) )
			return;

		const auto			ns = std::chrono::duration_cast<std::chrono::nanoseconds>( now.time_since_epoch() ).count();
		const std::uint64_t now_ns = ( ns <= 0 ) ? 0ull : static_cast<std::uint64_t>( ns );

		const std::uint64_t last = g_memory_governor_last_poll_ns.load( std::memory_order_relaxed );
		if ( last != 0 && now_ns > last && ( now_ns - last ) < g_memory_governor_poll_interval_ns )
			return;

		// Throttle across ALL threads: allow at most one poll per interval.
		std::uint64_t expected = last;
		if ( !g_memory_governor_last_poll_ns.compare_exchange_strong( expected, now_ns, std::memory_order_relaxed ) )
			return;

		const auto fn = g_memory_governor_poll_fn.load( std::memory_order_relaxed );
		if ( fn )
			fn();
	}

	void memory_governor_update_from_system_sample( std::uint64_t available_physical_bytes )
	{
		if ( !g_memory_governor_enabled.load( std::memory_order_relaxed ) )
			return;

		const std::uint64_t headroom = g_memory_governor_headroom_bytes.load( std::memory_order_relaxed );
		const std::uint64_t hyst = g_memory_governor_hysteresis_bytes.load( std::memory_order_relaxed );

		const bool was = g_memory_pressure.load( std::memory_order_relaxed );
		bool	   now = was;
		if ( available_physical_bytes != 0 && headroom != 0 )
		{
			if ( !was )
			{
				if ( available_physical_bytes < headroom )
					now = true;
			}
			else
			{
				if ( available_physical_bytes > headroom + hyst )
					now = false;
			}
		}

		if ( now != was )
		{
			g_memory_pressure.store( now, std::memory_order_relaxed );
			std::cout << "[Governor] memory_pressure=" << ( now ? "on" : "off" ) << " avail_bytes=" << available_physical_bytes << " headroom_bytes=" << headroom << "\n";
		}

		// Tighten/relax PMR budget based on current system available memory (best effort).
		if ( available_physical_bytes != 0 && headroom != 0 )
		{
			const std::uint64_t suggested = pmr_suggest_limit_bytes( available_physical_bytes, headroom );
			if ( now )
			{
				const std::uint64_t used = pmr_bounded_resource().allocated_bytes();
				const std::uint64_t slack = g_memory_governor_limit_slack_bytes.load( std::memory_order_relaxed );
				const std::uint64_t tight_limit = used + slack;
				pmr_bounded_resource().set_limit_bytes( std::min( suggested, tight_limit ) );
			}
			else
			{
				pmr_bounded_resource().set_limit_bytes( suggested );
			}
		}
	}

	void print_word32_hex( const char* label, std::uint32_t v )
	{
		// Preserve formatting state of std::cout (best-effort; printing is not performance critical).
		std::ios::fmtflags f( std::cout.flags() );
		std::streamsize	   p = std::cout.precision();
		char			   fill = std::cout.fill();

		std::cout << label << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << v << std::dec;

		std::cout.flags( f );
		std::cout.precision( p );
		std::cout.fill( fill );
	}

	std::string format_local_time_now()
	{
		const auto		  now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t( now );
		std::tm			  tm {};
#if defined( _WIN32 )
		localtime_s( &tm, &t );
#else
		localtime_r( &t, &tm );
#endif
		std::ostringstream oss;
		oss << std::put_time( &tm, "%Y-%m-%d %H:%M:%S" );
		return oss.str();
	}

	std::string hex8( std::uint32_t v )
	{
		std::ostringstream oss;
		oss << "0x" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << v << std::dec;
		return oss.str();
	}

	std::mutex& cout_mutex()
	{
		static std::mutex m;
		return m;
	}

	const char* progress_prefix()
	{
		return g_progress_prefix;
	}

	void set_progress_prefix( const char* prefix )
	{
		g_progress_prefix = prefix;
	}

	void print_progress_prefix( std::ostream& os )
	{
		const char* p = g_progress_prefix;
		if ( p && *p )
			os << p;
	}

	SystemMemoryInfo query_system_memory_info()
	{
		SystemMemoryInfo info {};
#if defined( _WIN32 )
		MEMORYSTATUSEX s {};
		s.dwLength = sizeof( s );
		if ( GlobalMemoryStatusEx( &s ) )
		{
			info.total_physical_bytes = static_cast<std::uint64_t>( s.ullTotalPhys );
			info.available_physical_bytes = static_cast<std::uint64_t>( s.ullAvailPhys );
		}
#elif defined( __linux__ )
		struct sysinfo s {};
		if ( sysinfo( &s ) == 0 )
		{
			const std::uint64_t unit = static_cast<std::uint64_t>( s.mem_unit );
			info.total_physical_bytes = static_cast<std::uint64_t>( s.totalram ) * unit;
			info.available_physical_bytes = static_cast<std::uint64_t>( s.freeram ) * unit;
		}
#endif
		return info;
	}

	void governor_poll_system_memory_once()
	{
		const SystemMemoryInfo mem = query_system_memory_info();
		memory_governor_update_from_system_sample( mem.available_physical_bytes );
	}

	MemoryBallast::MemoryBallast( std::uint64_t headroom_bytes ) : headroom_bytes_( headroom_bytes ) {}

	MemoryBallast::~MemoryBallast()
	{
		stop();
		clear();
	}

	void MemoryBallast::start()
	{
		if ( running_.exchange( true ) )
			return;
		worker_ = std::thread( [ this ]() { run(); } );
	}

	void MemoryBallast::stop()
	{
		if ( !running_.exchange( false ) )
			return;
		if ( worker_.joinable() )
			worker_.join();
	}

	std::uint64_t MemoryBallast::headroom_bytes() const
	{
		return headroom_bytes_;
	}

	std::uint64_t MemoryBallast::allocated_bytes() const
	{
		return allocated_bytes_;
	}

	void MemoryBallast::run()
	{
		// Control loop: keep available physical memory hovering near headroom_bytes_.
		while ( running_.load() )
		{
			const SystemMemoryInfo mem = query_system_memory_info();
			const std::uint64_t	   avail = mem.available_physical_bytes;
			if ( avail != 0 && headroom_bytes_ != 0 )
			{
				if ( avail > headroom_bytes_ + hysteresis_bytes )
				{
					( void )try_allocate_one_block();
				}
				else if ( avail + hysteresis_bytes < headroom_bytes_ )
				{
					try_free_one_block();
				}
			}
			std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
		}
	}

	bool MemoryBallast::try_allocate_one_block()
	{
#if defined( _WIN32 )
		void* p = VirtualAlloc( nullptr, SIZE_T( step_bytes ), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
		if ( !p )
			return false;
		// Touch each page to make it real in the working set (best-effort).
		volatile std::uint8_t* b = reinterpret_cast<volatile std::uint8_t*>( p );
		for ( std::uint64_t i = 0; i < step_bytes; i += 4096 )
			b[ i ] = 0;
		blocks_.push_back( p );
		allocated_bytes_ += step_bytes;
		return true;
#else
		std::uint8_t* p = nullptr;
		try
		{
			p = new std::uint8_t[ size_t( step_bytes ) ];
		}
		catch ( ... )
		{
			return false;
		}
		for ( std::uint64_t i = 0; i < step_bytes; i += 4096 )
			p[ i ] = 0;
		blocks_.push_back( p );
		allocated_bytes_ += step_bytes;
		return true;
#endif
	}

	void MemoryBallast::try_free_one_block()
	{
		if ( blocks_.empty() )
			return;
		void* p = blocks_.back();
		blocks_.pop_back();
#if defined( _WIN32 )
		( void )VirtualFree( p, 0, MEM_RELEASE );
#else
		delete[] reinterpret_cast<std::uint8_t*>( p );
#endif
		if ( allocated_bytes_ >= step_bytes )
			allocated_bytes_ -= step_bytes;
		else
			allocated_bytes_ = 0;
	}

	void MemoryBallast::clear()
	{
		while ( !blocks_.empty() )
			try_free_one_block();
	}

}  // namespace TwilightDream::runtime_component
