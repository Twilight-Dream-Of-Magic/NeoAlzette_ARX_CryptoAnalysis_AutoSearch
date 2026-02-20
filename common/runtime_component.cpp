#include "runtime_component.hpp"

#include <algorithm>
#include <thread>
#include <fstream>
#include <string>
#include <cctype>

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <psapi.h>
#elif defined( __linux__ )
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <unistd.h>
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

		std::atomic<MemoryPressureCallback> g_memory_pressure_checkpoint_fn { nullptr };
		std::atomic<MemoryPressureCallback> g_memory_pressure_must_live_degrade_fn { nullptr };
		std::atomic<bool>				   g_memory_pressure_handler_active { false };
		std::atomic<RebuildableCleanupCallback> g_rebuildable_cleanup_fn { nullptr };

		BudgetedMemoryPool g_must_live_pool( "must_live" );
		BudgetedMemoryPool g_rebuildable_pool( "rebuildable" );

		static std::uint64_t os_page_size_bytes()
		{
#if defined( _WIN32 )
			static std::uint64_t page = []() -> std::uint64_t {
				SYSTEM_INFO si {};
				GetSystemInfo( &si );
				return si.dwPageSize ? static_cast<std::uint64_t>( si.dwPageSize ) : 4096ull;
			}();
			return page;
#elif defined( __linux__ )
			static std::uint64_t page = []() -> std::uint64_t {
				const long v = ::sysconf( _SC_PAGESIZE );
				return ( v > 0 ) ? static_cast<std::uint64_t>( v ) : 4096ull;
			}();
			return page;
#else
			return 4096ull;
#endif
		}

		static std::uint64_t align_up_u64( std::uint64_t v, std::uint64_t align )
		{
			if ( align == 0 )
				return v;
			const std::uint64_t rem = v % align;
			return rem ? ( v + ( align - rem ) ) : v;
		}

		static void touch_pages( void* p, std::uint64_t size )
		{
			if ( !p || size == 0 )
				return;
			const std::uint64_t page = os_page_size_bytes();
			volatile std::uint8_t* b = reinterpret_cast<volatile std::uint8_t*>( p );
			for ( std::uint64_t i = 0; i < size; i += page )
				b[ i ] = 0;
		}

		static void* os_alloc_bytes( std::uint64_t size )
		{
			if ( size == 0 )
				return nullptr;
#if defined( _WIN32 )
			return VirtualAlloc( nullptr, SIZE_T( size ), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
#elif defined( __linux__ )
			void* p = ::mmap( nullptr, static_cast<size_t>( size ), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
			return ( p == MAP_FAILED ) ? nullptr : p;
#else
			return ::operator new( static_cast<size_t>( size ), std::nothrow );
#endif
		}

		static void os_free_bytes( void* p, std::uint64_t size )
		{
			if ( !p )
				return;
#if defined( _WIN32 )
			( void )size;
			( void )VirtualFree( p, 0, MEM_RELEASE );
#elif defined( __linux__ )
			if ( size )
				( void )::munmap( p, static_cast<size_t>( size ) );
#else
			( void )size;
			::operator delete( p );
#endif
		}

#if defined( __linux__ )
		static std::uint64_t parse_kb_line_value( const std::string& line, const char* key )
		{
			if ( line.rfind( key, 0 ) != 0 )
				return 0;
			std::size_t i = std::char_traits<char>::length( key );
			while ( i < line.size() && !std::isdigit( static_cast<unsigned char>( line[ i ] ) ) )
				++i;
			std::uint64_t v = 0;
			while ( i < line.size() && std::isdigit( static_cast<unsigned char>( line[ i ] ) ) )
			{
				v = v * 10ull + static_cast<std::uint64_t>( line[ i ] - '0' );
				++i;
			}
			return v;
		}

		static bool read_proc_meminfo( SystemMemoryInfo& out )
		{
			std::ifstream f( "/proc/meminfo" );
			if ( !f )
				return false;

			std::uint64_t mem_total_kb = 0;
			std::uint64_t mem_available_kb = 0;
			std::uint64_t mem_free_kb = 0;
			std::uint64_t buffers_kb = 0;
			std::uint64_t cached_kb = 0;
			std::uint64_t s_reclaimable_kb = 0;
			std::uint64_t shmem_kb = 0;
			std::uint64_t commit_limit_kb = 0;
			std::uint64_t committed_as_kb = 0;

			std::string line;
			while ( std::getline( f, line ) )
			{
				if ( mem_total_kb == 0 )
					mem_total_kb = parse_kb_line_value( line, "MemTotal:" );
				if ( mem_available_kb == 0 )
					mem_available_kb = parse_kb_line_value( line, "MemAvailable:" );
				if ( mem_free_kb == 0 )
					mem_free_kb = parse_kb_line_value( line, "MemFree:" );
				if ( buffers_kb == 0 )
					buffers_kb = parse_kb_line_value( line, "Buffers:" );
				if ( cached_kb == 0 )
					cached_kb = parse_kb_line_value( line, "Cached:" );
				if ( s_reclaimable_kb == 0 )
					s_reclaimable_kb = parse_kb_line_value( line, "SReclaimable:" );
				if ( shmem_kb == 0 )
					shmem_kb = parse_kb_line_value( line, "Shmem:" );
				if ( commit_limit_kb == 0 )
					commit_limit_kb = parse_kb_line_value( line, "CommitLimit:" );
				if ( committed_as_kb == 0 )
					committed_as_kb = parse_kb_line_value( line, "Committed_AS:" );
			}

			if ( mem_available_kb == 0 )
			{
				// Fallback approximation for older kernels without MemAvailable.
				std::uint64_t approx = mem_free_kb + buffers_kb + cached_kb + s_reclaimable_kb;
				if ( approx > shmem_kb )
					approx -= shmem_kb;
				mem_available_kb = approx;
			}

			out.total_physical_bytes = mem_total_kb * 1024ull;
			out.available_physical_bytes = mem_available_kb * 1024ull;
			out.commit_limit_bytes = commit_limit_kb * 1024ull;
			out.committed_as_bytes = committed_as_kb * 1024ull;
			return true;
		}

		static std::uint64_t read_proc_status_kb( const char* key )
		{
			std::ifstream f( "/proc/self/status" );
			if ( !f )
				return 0;
			std::string line;
			while ( std::getline( f, line ) )
			{
				const std::uint64_t v = parse_kb_line_value( line, key );
				if ( v != 0 )
					return v;
			}
			return 0;
		}
#endif
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

		SystemMemoryInfo info {};
		info.available_physical_bytes = available_physical_bytes;
		const MemoryBudget budget = compute_workstation_greedy_budget( info, headroom_bytes, 0.35 );
		configure_memory_pools( budget );
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
			if ( now && !was )
				on_memory_pressure();
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

	MemoryBudget compute_workstation_greedy_budget( const SystemMemoryInfo& info, std::uint64_t headroom_bytes, double must_live_fraction )
	{
		MemoryBudget b {};
		b.available_physical_bytes = info.available_physical_bytes;
		b.headroom_bytes = headroom_bytes;
		if ( info.available_physical_bytes == 0 )
			return b;
		if ( info.available_physical_bytes <= headroom_bytes )
			return b;
		b.total_budget_bytes = info.available_physical_bytes - headroom_bytes;
		if ( must_live_fraction < 0.05 )
			must_live_fraction = 0.05;
		if ( must_live_fraction > 0.95 )
			must_live_fraction = 0.95;
		b.must_live_budget_bytes = static_cast<std::uint64_t>( double( b.total_budget_bytes ) * must_live_fraction );
		b.rebuildable_budget_bytes = b.total_budget_bytes - b.must_live_budget_bytes;
		return b;
	}

	BudgetedMemoryPool::BudgetedMemoryPool( const char* label ) : label_( label ) {}

	void BudgetedMemoryPool::set_budget_bytes( std::uint64_t bytes )
	{
		std::scoped_lock lk( mutex_ );
		if ( bytes != 0 && allocated_bytes_ > bytes )
			bytes = allocated_bytes_;
		budget_bytes_ = bytes;
	}

	std::uint64_t BudgetedMemoryPool::budget_bytes() const
	{
		std::scoped_lock lk( mutex_ );
		return budget_bytes_;
	}

	std::uint64_t BudgetedMemoryPool::allocated_bytes() const
	{
		std::scoped_lock lk( mutex_ );
		return allocated_bytes_;
	}

	const char* BudgetedMemoryPool::label() const
	{
		return label_ ? label_ : "";
	}

	void* BudgetedMemoryPool::allocate( std::uint64_t bytes, bool touch )
	{
		if ( bytes == 0 )
			return nullptr;

		const std::uint64_t page = os_page_size_bytes();
		const std::uint64_t size = align_up_u64( bytes, page );

		std::scoped_lock lk( mutex_ );
		if ( budget_bytes_ != 0 && ( allocated_bytes_ + size ) > budget_bytes_ )
			return nullptr;

		void* p = os_alloc_bytes( size );
		if ( !p )
			return nullptr;

		if ( touch )
			touch_pages( p, size );
		blocks_.push_back( Block { p, size } );
		allocated_bytes_ += size;
		return p;
	}

	void BudgetedMemoryPool::release_all()
	{
		std::scoped_lock lk( mutex_ );
		for ( const auto& b : blocks_ )
			os_free_bytes( b.p, b.size );
		blocks_.clear();
		allocated_bytes_ = 0;
	}

	BudgetedMemoryPool& must_live_pool()
	{
		return g_must_live_pool;
	}

	BudgetedMemoryPool& rebuildable_pool()
	{
		return g_rebuildable_pool;
	}

	void configure_memory_pools( const MemoryBudget& budget )
	{
		g_must_live_pool.set_budget_bytes( budget.must_live_budget_bytes );
		g_rebuildable_pool.set_budget_bytes( budget.rebuildable_budget_bytes );
	}

	void release_rebuildable_pool()
	{
		const auto cleanup = g_rebuildable_cleanup_fn.load( std::memory_order_relaxed );
		if ( cleanup )
			cleanup();
		g_rebuildable_pool.release_all();
	}

	void* alloc_must_live( std::uint64_t bytes, bool touch_pages_flag )
	{
		void* p = g_must_live_pool.allocate( bytes, touch_pages_flag );
		if ( !p )
			pmr_report_oom_once( "alloc_must_live" );
		return p;
	}

	void* alloc_rebuildable( std::uint64_t bytes, bool touch_pages_flag )
	{
		void* p = g_rebuildable_pool.allocate( bytes, touch_pages_flag );
		if ( !p )
			pmr_report_oom_once( "alloc_rebuildable" );
		return p;
	}

	void memory_pressure_set_checkpoint_fn( MemoryPressureCallback fn )
	{
		g_memory_pressure_checkpoint_fn.store( fn, std::memory_order_relaxed );
	}

	void memory_pressure_set_must_live_degrade_fn( MemoryPressureCallback fn )
	{
		g_memory_pressure_must_live_degrade_fn.store( fn, std::memory_order_relaxed );
	}

	void rebuildable_set_cleanup_fn( RebuildableCleanupCallback fn )
	{
		g_rebuildable_cleanup_fn.store( fn, std::memory_order_relaxed );
	}

	void on_memory_pressure()
	{
		if ( g_memory_pressure_handler_active.exchange( true, std::memory_order_relaxed ) )
			return;
		try
		{
			const auto checkpoint = g_memory_pressure_checkpoint_fn.load( std::memory_order_relaxed );
			if ( checkpoint )
				checkpoint();

			release_rebuildable_pool();

			const auto degrade = g_memory_pressure_must_live_degrade_fn.load( std::memory_order_relaxed );
			if ( degrade )
				degrade();
		}
		catch ( ... )
		{
			g_memory_pressure_handler_active.store( false, std::memory_order_relaxed );
			throw;
		}
		g_memory_pressure_handler_active.store( false, std::memory_order_relaxed );
	}

	std::uint64_t clat_estimated_bytes_for_m( unsigned m )
	{
		if ( m < 8 )
			return 0;
		const std::uint64_t base = 1288490189ull;  // 1.2 GiB in bytes (approx)
		const unsigned		 shift = 3u * ( m - 8u );
		if ( shift >= 63u )
			return std::numeric_limits<std::uint64_t>::max();
		return base << shift;
	}

	unsigned clat_select_m_for_budget( std::uint64_t budget_bytes, unsigned min_m, unsigned max_m )
	{
		if ( min_m > max_m )
			std::swap( min_m, max_m );
		if ( budget_bytes == 0 )
			return min_m;
		unsigned best = min_m;
		for ( unsigned m = min_m; m <= max_m; ++m )
		{
			if ( clat_estimated_bytes_for_m( m ) <= budget_bytes )
				best = m;
			else
				break;
		}
		return best;
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
			info.commit_limit_bytes = static_cast<std::uint64_t>( s.ullTotalPageFile );
			const std::uint64_t avail_commit = static_cast<std::uint64_t>( s.ullAvailPageFile );
			info.committed_as_bytes = ( info.commit_limit_bytes > avail_commit ) ? ( info.commit_limit_bytes - avail_commit ) : 0;
		}
		PROCESS_MEMORY_COUNTERS_EX pmc {};
		if ( GetProcessMemoryInfo( GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>( &pmc ), sizeof( pmc ) ) )
			info.process_rss_bytes = static_cast<std::uint64_t>( pmc.WorkingSetSize );
#elif defined( __linux__ )
		if ( !read_proc_meminfo( info ) )
		{
			struct sysinfo s {};
			if ( sysinfo( &s ) == 0 )
			{
				const std::uint64_t unit = static_cast<std::uint64_t>( s.mem_unit );
				info.total_physical_bytes = static_cast<std::uint64_t>( s.totalram ) * unit;
				info.available_physical_bytes = static_cast<std::uint64_t>( s.freeram ) * unit;
			}
		}
		const std::uint64_t rss_kb = read_proc_status_kb( "VmRSS:" );
		if ( rss_kb != 0 )
			info.process_rss_bytes = rss_kb * 1024ull;
#endif
		return info;
	}

	void governor_poll_system_memory_once()
	{
		const SystemMemoryInfo mem = query_system_memory_info();
		memory_governor_update_from_system_sample( mem.available_physical_bytes );
	}

	void print_system_memory_status_line( std::ostream& os, const SystemMemoryInfo& info, const char* prefix )
	{
		IosStateGuard g( os );
		if ( prefix )
			os << prefix;

		bool printed = false;
		auto emit_gib = [ & ]( const char* label, std::uint64_t bytes ) {
			if ( bytes == 0 )
				return;
			if ( printed )
				os << "  ";
			os << label << "=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( bytes ) << "GiB";
			printed = true;
		};

		emit_gib( "MemTotal", info.total_physical_bytes );
		emit_gib( "VmRSS", info.process_rss_bytes );
		emit_gib( "MemAvailable", info.available_physical_bytes );
		emit_gib( "Committed_AS", info.committed_as_bytes );
		emit_gib( "CommitLimit", info.commit_limit_bytes );

		if ( !printed )
			os << "system_memory=unknown";
		os << "\n";
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
		touch_pages( p, step_bytes );
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
		touch_pages( p, step_bytes );
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
