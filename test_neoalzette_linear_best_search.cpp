#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>
#include <array>
#include <filesystem>

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined( __linux__ )
#include <sys/sysinfo.h>
#endif

#include "auto_search_frame/test_neoalzette_linear_best_search.hpp"
#include "auto_search_frame/detail/auto_pipeline_shared.hpp"

namespace
{
	using namespace TwilightDream::auto_search_linear;
	using ::TwilightDream::NeoAlzetteCore;
	using TwilightDream::runtime_component::bytes_to_gibibytes;
	using TwilightDream::runtime_component::compute_memory_headroom_bytes;
	using TwilightDream::runtime_component::evaluate_memory_gate;
	using TwilightDream::runtime_component::governor_poll_system_memory_once;
	using TwilightDream::runtime_component::IosStateGuard;
	using TwilightDream::runtime_component::MemoryBallast;
	using TwilightDream::runtime_component::MemoryGateEvaluation;
	using TwilightDream::runtime_component::MemoryGateStatus;
	using TwilightDream::runtime_component::alloc_rebuildable;
	using TwilightDream::runtime_component::memory_gate_status_name;
	using TwilightDream::runtime_component::release_rebuildable_pool;
	using TwilightDream::runtime_component::print_system_memory_status_line;
	using TwilightDream::runtime_component::query_system_memory_info;
	using TwilightDream::runtime_component::resolve_worker_thread_count_for_command_line_interface;
	using TwilightDream::runtime_component::run_worker_threads_with_monitor;
	using TwilightDream::runtime_component::SystemMemoryInfo;
	using TwilightDream::auto_pipeline_shared::RebuildableReserveGuard;
	using BestSearchResult = MatsuiSearchRunLinearResult;

	struct CommandLineOptions
	{
		enum class FrontendMode
		{
			Strategy,
			Detail,
			Auto
		};
		enum class StrategyPreset
		{
			None,
			TimeFirst,
			Balanced,
			SpaceFirst
		};
		enum class AutoBreadthStrategy
		{
			FrontierBfs
		};

		FrontendMode   frontend_mode = FrontendMode::Detail;
		StrategyPreset strategy_preset = StrategyPreset::None;
		bool		   strategy_heuristics_enabled = false;
		int			   strategy_resolved_worker_threads = 1;
		std::uint64_t  strategy_total_physical_bytes = 0;
		std::uint64_t  strategy_available_physical_bytes = 0;
		std::uint64_t  strategy_target_headroom_bytes = 0;
		std::uint64_t  strategy_derived_budget_bytes = 0;
		std::uint64_t  strategy_total_work = 0;
		bool		   strategy_total_work_was_provided = false;
		bool		   strategy_batch_was_requested = false;

		bool selftest = false;
		std::uint64_t selftest_seed = 0;
		bool		  selftest_seed_was_provided = false;
		std::size_t	  selftest_case_count = 0;
		bool		  selftest_case_count_was_provided = false;
		bool show_help = false;
		bool resume_was_provided = false;
		std::string resume_path {};
		bool checkpoint_out_was_provided = false;
		std::string checkpoint_out_path {};
		std::string runtime_log_path {};
		bool runtime_maximum_search_nodes_was_provided = false;
		LinearBestSearchRuntimeControls runtime_controls = [] {
			LinearBestSearchRuntimeControls controls {};
			controls.maximum_search_nodes = 2000000;
			controls.progress_every_seconds = 1;
			controls.checkpoint_every_seconds = 1800;
			return controls;
		}();
		bool runtime_maximum_search_seconds_was_provided = false;
		bool checkpoint_every_seconds_was_provided = false;
		bool search_configuration_was_provided = false;
		bool search_mode_was_provided = false;
		bool round_count_was_provided = false;

		// Memory (safety-first "near-limit" mode): keep some headroom, optionally allocate/release ballast to stay near the limit.
		bool memory_ballast_enabled = false;
		bool allow_high_memory_usage = false;

		// Memory budgeting for PMR bounded resource
		std::uint64_t memory_headroom_mib = 0;	// 0 = default headroom rule
		bool		  memory_headroom_mib_was_provided = false;
		std::uint64_t rebuildable_reserve_mib = 0; // 0 = disabled
		bool		  rebuildable_reserve_mib_was_provided = false;

		int			  round_count = 1;
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		bool		  output_masks_were_provided = false;
		bool		  output_masks_were_generated = false;

		// Auto mode (single-run only)
		std::size_t	  auto_breadth_candidate_count = 256;  // scan this many candidate output mask pairs (includes the provided start pair)
		std::size_t	  auto_breadth_top_k = 3;			   // keep/print top-K breadth candidates; deep stage selects the best one
		int			  auto_breadth_thread_count = 0;	   // 0=auto (hardware_concurrency)
		AutoBreadthStrategy auto_breadth_strategy = AutoBreadthStrategy::FrontierBfs;
		std::size_t	  auto_breadth_frontier_width = 128;
		int			  auto_breadth_max_hops = 3;
		std::uint64_t auto_breadth_seed = 0;			   // RNG seed for breadth candidates (default: derived from start masks)
		bool		  auto_breadth_seed_was_provided = false;
		int			  auto_breadth_max_random_bitflips = 0;  // 0=off; else append RNG XOR masks (Hamming <= this) until job cap
		std::uint64_t auto_breadth_maximum_search_nodes = ( std::numeric_limits<uint32_t>::max() >> 12 );  // per candidate breadth budget
		std::size_t	  auto_breadth_maximum_round_predecessors = 512;									   // breadth branching limiter
		bool		  auto_print_breadth_candidates = false;											   // print ALL breadth candidates (can be large)
		std::uint64_t auto_deep_maximum_search_nodes = 0;												   // per candidate deep search (0=unlimited)
		std::uint64_t auto_max_time_seconds = 0;														   // deep-search time budget when deep maximum_search_nodes==0 (0=unlimited)
		int			  auto_target_best_weight = -1;													   // if >=0: stop early once best_weight <= target
		// Batch mode
		std::size_t	  batch_job_count = 0;	   // 0 = disabled
		int			  batch_thread_count = 0;  // 0 = auto (hardware_concurrency)
		std::uint64_t batch_seed = 0;
		bool		  batch_seed_was_provided = false;	// require explicit --seed when a random number generator is used
		std::string	  batch_job_file {};
		bool		  batch_job_file_was_provided = false;
		std::size_t	  progress_every_jobs = 10;	   // print progress every N jobs (0=disable)
		LinearBestSearchConfiguration search_configuration {};
	};

	static int run_resume_mode( const CommandLineOptions& command_line_options );

	struct LinearFastSearchCapDefaults
	{
		std::size_t maximum_injection_input_masks = 0;
		std::size_t maximum_round_predecessors = 0;
	};

	static bool apply_search_mode_overrides(
		LinearBestSearchConfiguration& c,
		std::size_t default_maximum_injection_input_masks = 0,
		std::size_t default_maximum_round_predecessors = 0 );
	static LinearFastSearchCapDefaults compute_default_fast_search_caps_for_cli( const CommandLineOptions& command_line_options );

	static std::string to_lowercase_ascii( std::string s )
	{
		for ( char& c : s )
		{
			if ( c >= 'A' && c <= 'Z' )
				c = char( c - 'A' + 'a' );
		}
		return s;
	}

	static BinaryCheckpointManager* g_pressure_binary_checkpoint = nullptr;
	static void ( *g_pressure_checkpoint_hook )() = nullptr;

	static void pressure_checkpoint_callback()
	{
		TwilightDream::auto_pipeline_shared::dispatch_pressure_checkpoint( g_pressure_binary_checkpoint, g_pressure_checkpoint_hook );
	}

	static void pressure_degrade_callback()
	{
		// Disable TLS caches (best-effort) to reduce memory pressure.
		g_disable_linear_tls_caches.store( true, std::memory_order_relaxed );
	}

	struct PressureCallbackGuard
	{
		bool previous_disable = false;
		TwilightDream::auto_pipeline_shared::MemoryPressureCallbackRegistrationGuard<BinaryCheckpointManager> registration;

		explicit PressureCallbackGuard( BinaryCheckpointManager* checkpoint, void ( *checkpoint_hook )() = nullptr )
			: previous_disable( g_disable_linear_tls_caches.load( std::memory_order_relaxed ) )
			, registration(
				  g_pressure_binary_checkpoint,
				  g_pressure_checkpoint_hook,
				  checkpoint,
				  checkpoint_hook,
				  &pressure_checkpoint_callback,
				  &pressure_degrade_callback )
		{
		}

		~PressureCallbackGuard()
		{
			g_disable_linear_tls_caches.store( previous_disable, std::memory_order_relaxed );
		}
	};

	static inline const char* to_string( CommandLineOptions::FrontendMode m )
	{
		switch ( m )
		{
		case CommandLineOptions::FrontendMode::Strategy:
			return "strategy";
		case CommandLineOptions::FrontendMode::Detail:
			return "detail";
		case CommandLineOptions::FrontendMode::Auto:
			return "auto";
		}
		return "unknown";
	}

	static inline const char* to_string( CommandLineOptions::StrategyPreset p )
	{
		switch ( p )
		{
		case CommandLineOptions::StrategyPreset::None:
			return "none";
		case CommandLineOptions::StrategyPreset::TimeFirst:
			return "time";
		case CommandLineOptions::StrategyPreset::Balanced:
			return "balanced";
		case CommandLineOptions::StrategyPreset::SpaceFirst:
			return "space";
		}
		return "unknown";
	}

	static constexpr std::uint64_t k_mib = 1024ull * 1024ull;
	static constexpr std::uint64_t k_gib = 1024ull * 1024ull * 1024ull;

	static std::uint64_t saturating_add_u64( std::uint64_t a, std::uint64_t b ) noexcept
	{
		const std::uint64_t maxv = std::numeric_limits<std::uint64_t>::max();
		return ( a > maxv - b ) ? maxv : ( a + b );
	}

	static std::uint64_t saturating_mul_u64( std::uint64_t a, std::uint64_t b ) noexcept;

	static std::uint64_t mebibytes_to_bytes( std::uint64_t mib ) noexcept
	{
		return saturating_mul_u64( mib, k_mib );
	}

	static std::uint64_t estimate_linear_optional_rebuildable_bytes( const CommandLineOptions& command_line_options ) noexcept
	{
		std::uint64_t estimate = mebibytes_to_bytes( command_line_options.rebuildable_reserve_mib );
		if ( command_line_options.search_configuration.enable_weight_sliced_clat )
			estimate = saturating_add_u64( estimate, 512ull * k_mib );
		return estimate;
	}

	static std::uint64_t estimate_linear_must_live_bytes(
		const CommandLineOptions& command_line_options,
		bool					  auto_mode,
		bool					  batch_mode,
		bool					  resume_mode ) noexcept
	{
		std::uint64_t estimate = 256ull * k_mib;
		estimate = saturating_add_u64( estimate, saturating_mul_u64( std::uint64_t( std::max( 1, command_line_options.round_count ) ), 96ull * k_mib ) );
		estimate = saturating_add_u64( estimate, command_line_options.search_configuration.enable_state_memoization ? ( 768ull * k_mib ) : ( 128ull * k_mib ) );
		estimate = saturating_add_u64( estimate, command_line_options.search_configuration.enable_weight_sliced_clat ? ( 384ull * k_mib ) : ( 3072ull * k_mib ) );
		if ( command_line_options.runtime_controls.maximum_search_seconds != 0 )
		{
			estimate = saturating_add_u64( estimate, 1024ull * k_mib );
		}
		else if ( command_line_options.runtime_controls.maximum_search_nodes != 0 )
		{
			const std::uint64_t node_signal =
				std::min<std::uint64_t>(
					8ull * k_gib,
					saturating_mul_u64(
						std::max<std::uint64_t>( 1ull, command_line_options.runtime_controls.maximum_search_nodes / 250000ull ),
						256ull * k_mib ) );
			estimate = saturating_add_u64( estimate, node_signal );
		}
		if ( auto_mode )
		{
			const unsigned hardware_thread_concurrency = std::thread::hardware_concurrency();
			const int	   auto_threads_raw =
				( command_line_options.auto_breadth_thread_count > 0 ) ?
					command_line_options.auto_breadth_thread_count :
					int( ( hardware_thread_concurrency == 0 ) ? 1 : hardware_thread_concurrency );
			const std::uint64_t auto_threads = std::uint64_t( std::max( 1, auto_threads_raw ) );
			estimate = saturating_add_u64( estimate, 768ull * k_mib );
			estimate = saturating_add_u64( estimate, saturating_mul_u64( auto_threads, 96ull * k_mib ) );
			estimate = saturating_add_u64(
				estimate,
				std::min<std::uint64_t>(
					2ull * k_gib,
					saturating_mul_u64( std::uint64_t( std::max<std::size_t>( 1, command_line_options.auto_breadth_candidate_count ) ), 64ull * 1024ull ) ) );
		}
		if ( batch_mode )
		{
			const int batch_threads = resolve_worker_thread_count_for_command_line_interface(
				command_line_options.batch_job_count,
				command_line_options.batch_job_file_was_provided,
				command_line_options.batch_thread_count );
			estimate = saturating_add_u64( estimate, saturating_mul_u64( std::uint64_t( std::max( 1, batch_threads ) ), 96ull * k_mib ) );
			estimate = saturating_add_u64(
				estimate,
				std::min<std::uint64_t>(
					1ull * k_gib,
					saturating_mul_u64( std::uint64_t( std::max<std::size_t>( 1, command_line_options.batch_job_count ) ), 16ull * 1024ull ) ) );
		}
		if ( resume_mode )
			estimate = saturating_add_u64( estimate, 256ull * k_mib );
		return estimate;
	}

	static MemoryGateEvaluation evaluate_linear_memory_gate(
		const CommandLineOptions& command_line_options,
		const SystemMemoryInfo&   system_memory_info,
		bool					  auto_mode,
		bool					  batch_mode,
		bool					  resume_mode ) noexcept
	{
		return evaluate_memory_gate(
			system_memory_info.available_physical_bytes,
			estimate_linear_must_live_bytes( command_line_options, auto_mode, batch_mode, resume_mode ),
			estimate_linear_optional_rebuildable_bytes( command_line_options ) );
	}

	static SearchInvocationMetadata build_invocation_metadata( const MemoryGateEvaluation& evaluation, bool startup_memory_gate_advisory_only ) noexcept
	{
		SearchInvocationMetadata metadata {};
		metadata.physical_available_bytes = evaluation.physical_available_bytes;
		metadata.estimated_must_live_bytes = evaluation.must_live_bytes;
		metadata.estimated_optional_rebuildable_bytes = evaluation.optional_rebuildable_bytes;
		metadata.memory_gate_status = evaluation.status;
		metadata.startup_memory_gate_advisory_only = startup_memory_gate_advisory_only;
		return metadata;
	}

	static bool print_and_enforce_memory_gate(
		const char*				 prefix,
		const MemoryGateEvaluation& evaluation,
		bool					 allow_high_memory_usage,
		bool					 strict_search_mode )
	{
		return TwilightDream::best_search_shared_core::print_and_enforce_startup_memory_gate(
			prefix,
			evaluation,
			allow_high_memory_usage,
			strict_search_mode );
	}

	static bool linear_result_is_resumable_pause( const BestSearchResult& result ) noexcept
	{
		return !result.found && ( result.hit_maximum_search_nodes || result.hit_time_limit );
	}

	static int linear_exit_code_from_result( const BestSearchResult& result ) noexcept
	{
		return ( result.found || linear_result_is_resumable_pause( result ) ) ? 0 : 2;
	}

	static bool parse_strategy_preset( const char* s, CommandLineOptions::StrategyPreset& out )
	{
		if ( !s )
			return false;
		const std::string v = to_lowercase_ascii( std::string( s ) );
		if ( v == "time" || v == "time-first" || v == "time_first" || v == "t" )
		{
			out = CommandLineOptions::StrategyPreset::TimeFirst;
			return true;
		}
		if ( v == "balanced" || v == "balance" || v == "b" )
		{
			out = CommandLineOptions::StrategyPreset::Balanced;
			return true;
		}
		if ( v == "space" || v == "space-first" || v == "space_first" || v == "s" )
		{
			out = CommandLineOptions::StrategyPreset::SpaceFirst;
			return true;
		}
		return false;
	}

	static inline const char* to_string( CommandLineOptions::AutoBreadthStrategy strategy )
	{
		switch ( strategy )
		{
		case CommandLineOptions::AutoBreadthStrategy::FrontierBfs:
			return "frontier-bfs";
		}
		return "unknown";
	}

	static bool parse_auto_breadth_strategy( const char* text, CommandLineOptions::AutoBreadthStrategy& out )
	{
		if ( !text )
			return false;
		const std::string value = to_lowercase_ascii( std::string( text ) );
		if ( value == "frontier-bfs" || value == "frontier_bfs" || value == "frontier" || value == "bfs" )
		{
			out = CommandLineOptions::AutoBreadthStrategy::FrontierBfs;
			return true;
		}
		return false;
	}

	static void print_usage_information( const char* executable_name )
	{
		if ( !executable_name )
			executable_name = "test_neoalzette_linear_best_search.exe";

		std::cout << "Usage:\n"
				  << "  " << executable_name << " strategy <time|balanced|space> [strategy_options]\n"
				  << "  " << executable_name << " detail   [detail_options]\n"
				  << "  " << executable_name << " auto     [auto_options]\n"
				  << "\n"
				  << "Strategy mode (recommended, fewer knobs):\n"
				  << "  " << executable_name << " strategy <preset> --round-count R [--output-branch-a-mask MASK_A --output-branch-b-mask MASK_B | --seed SEED]\n"
				  << "  presets:\n"
				  << "    time      Time-first: large budgets; memoization on; larger branching caps.\n"
				  << "    balanced  Balanced: moderate budgets; memoization on; heuristic caps.\n"
				  << "    space     Space-first: lower memory. Memoization off. Smaller caps.\n"
				  << "  strategy options:\n"
				  << "    --round-count R\n"
				  << "    --output-branch-a-mask MASK_A   Alias: --out-mask-a, --mask-a\n"
				  << "    --output-branch-b-mask MASK_B   Alias: --out-mask-b, --mask-b\n"
				  << "    --seed SEED                     Generate a single start mask pair when the masks are omitted.\n"
				  << "    --total-work N                  Auto-scale single-run budgets (maximum_search_nodes). Alias: --total.\n"
				  << "    --maximum-search-nodes N        Alias: --maxnodes\n"
				  << "    --target-best-weight W          Alias: --target-weight\n"
				  << "    --search-mode strict|fast\n"
				  << "    --progress-every-seconds S      Alias: --progress-sec\n"
				  << "    --memory-headroom-mib M  --memory-ballast  --allow-high-memory-usage  --rebuildable-reserve-mib M\n\n"
				  << "Detail mode (full knobs, long names; short-name aliases kept):\n"
				  << "  " << executable_name << " detail --round-count R --output-branch-a-mask MASK_A --output-branch-b-mask MASK_B [options]\n\n"
				  << "Auto mode (two-stage: breadth scan -> deep search, requires explicit output masks):\n"
				  << "  " << executable_name << " auto --round-count R --output-branch-a-mask MASK_A --output-branch-b-mask MASK_B [options]\n"
				  << "    (breadth) strict enumeration over many candidate output mask pairs.\n"
				  << "  auto options:\n"
				  << "    --auto-breadth-jobs N           Alias: --auto-breadth-max-runs. Candidate count (default=256).\n"
				  << "    --auto-breadth-top_candidates K Keep/print top K breadth candidates (default=3). Deep runs the best one.\n"
				  << "    --auto-breadth-threads T        Breadth threads (0=auto).\n"
				  << "    --auto-breadth-strategy frontier-bfs  Fixed breadth generator (the only supported strategy).\n"
				  << "    --auto-breadth-frontier-width N Frontier width for frontier-bfs generation (default=128).\n"
				  << "    --auto-breadth-max-hops H       Hop limit for frontier-bfs generation (default=3).\n"
				  << "    --auto-breadth-seed S           RNG seed (hex/dec) for pseudo-random XOR candidates after frontier-bfs.\n"
				  << "    --auto-breadth-max-bitflips F   After frontier-bfs, add random masks by XORing the start pair with up to F\n"
				  << "                                    bits total across both words (0=disable; default=0).\n"
				  << "    --auto-breadth-maxnodes N       Breadth per-candidate maximum_search_nodes.\n"
				  << "    --progress-every-seconds S      Alias: --progress-sec (breadth/deep runtime progress).\n"
				  << "    --auto-breadth-hcap N           Ignored (breadth is strict).\n"
				  << "    --auto-print-breadth-candidates  Print ALL breadth candidates (warning: verbose).\n"
				  << "    --auto-deep-maxnodes N          Deep per-candidate maximum_search_nodes (0=unlimited).\n"
				  << "    --auto-max-time T               Deep time budget when deep maximum_search_nodes==0. Examples: 3600, 30d, 4w.\n"
				  << "    --auto-target-best-weight W     Stop once best_weight <= W.\n"
				  << "                                    If hit under otherwise strict local settings, this reports threshold_target_certified,\n"
				  << "                                    not exact_best_certified.\n\n"
				  << "Common options:\n"
				  << "  --selftest                       Run ARX operator self-tests and exit.\n"
				  << "  --selftest-seed SEED             Seed for regression selftests (hex or decimal).\n"
				  << "  --selftest-cases N               Extra random selftest cases (default: 8).\n"
				  << "  --help, -h                       Show this help.\n"
				  << "  --mode strategy|detail|auto      Select CLI frontend (when no subcommand is used).\n\n"
				  << "Common parameters:\n"
				  << "  --round-count R\n"
				  << "  --output-branch-a-mask MASK_A    Alias: --out-mask-a, --mask-a\n"
				  << "  --output-branch-b-mask MASK_B    Alias: --out-mask-b, --mask-b\n\n"
				  << "Search options (detail):\n"
				  << "  --search-mode strict|fast       Candidate semantics (strict disables truncating caps; fast keeps heuristics).\n"
				  << "  --enable-z-shell                Enable the z-shell based Weight-Sliced cLAT accelerator for var-var addition.\n"
				  << "                                   In strict mode with --z-shell-max-candidates 0, this is the full exact z-shell streaming path.\n"
				  << "  --disable-z-shell               Disable the z-shell accelerator.\n"
				  << "                                   In strict mode, this selects the exact split-8 SLR streaming path for var-var addition.\n"
				  << "  --z-shell-max-candidates N\n"
				  << "                                   Candidate cap for z-shell generation (0=unlimited/full z-shell path, not disabled).\n"
				  << "                                   Nonzero truncates the z-shell helper; if that cap is hit, batch generation falls back to exact split-8 SLR.\n"
				  << "                                   Strict mode forces this back to 0 because nonzero caps are non-strict.\n"
				  << "  --maximum-search-nodes N         Alias: --maxnodes. Node budget (0=unlimited). Default: 2000000\n"
				  << "  --maximum-search-seconds T       Alias: --maxsec. Time budget (0=unlimited). Examples: 3600, 30d, 4w.\n"
				  << "  --target-best-weight W           Alias: --target-weight. Stop early once best_weight <= W.\n"
				  << "                                   If hit under otherwise strict settings, this reports threshold_target_certified,\n"
				  << "                                   not exact_best_certified.\n"
				  << "  --addition-weight-cap N     Alias: --add. Per modular-addition weight cap (0..31).\n"
				  << "  --constant-subtraction-weight-cap N\n"
				  << "                             Alias: --subtract. Per modular-subtraction-constant weight cap (0..32).\n"
				  << "  --maximum-injection-input-masks N\n"
				  << "                                   Alias: --injection-candidates, --maximum-injection-candidates.\n"
				  << "  --maximum-round-predecessors N   Alias: --max-round-predecessors, --heuristic-branch-cap, --hcap.\n"
				  << "  --disable-state-memoization      Alias: --nomemo. Disable memoization (less memory, usually slower).\n"
				  << "  --enable-verbose-output          Alias: --verbose. Verbose output.\n\n"
				  << "Memory options:\n"
				  << "  --memory-headroom-mib M          Keep ~M MiB of free RAM headroom (default: ~6-8GiB).\n"
				  << "  --memory-ballast                Adaptive ballast: allocate/release RAM to keep free RAM near headroom (not a cLAT preallocation).\n"
				  << "  --allow-high-memory-usage       Override the 95%-of-available startup must-live memory gate (strict mode keeps startup gate advisory-only; runtime 95% physical guard still applies).\n"
				  << "  --rebuildable-reserve-mib M      Reserve M MiB from rebuildable pool (touched, released on pressure; optional buffer, not a pDDT/cLAT upfront allocation).\n\n"
				  << "Checkpoint options:\n"
				  << "  --resume PATH                   Resume best-search or auto pipeline from binary checkpoint (single-run only).\n"
				  << "  --runtime-log PATH              Write runtime event log (default: auto_checkpoint_R..._YYYYMMDD_HHMMSS.runtime.log).\n"
				  << "  --checkpoint-out PATH           Write binary checkpoint (default: auto_checkpoint_R..._YYYYMMDD_HHMMSS.ckpt).\n"
				  << "  --checkpoint-every-seconds S    Periodic checkpoint ARCHIVE interval (default=1800). 0=disable archive timer.\n"
				  << "                                 The latest resumable checkpoint is still maintained internally by the watchdog.\n\n"
				  << "Batch breadth->deep note:\n"
				  << "  Multi-job breadth->deep runs now live in test_neoalzette_linear_hull_wrapper.exe.\n"
				  << "  Use the wrapper for --batch-job-count / --batch-file, --thread-count,\n"
				  << "  strict hull collection, and batch checkpoint/resume/runtime logs.\n\n"
				  << "Examples:\n"
				  << "  " << executable_name << " strategy balanced --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1\n"
				  << "  " << executable_name << " detail --round-count 4 --seed 0x1234 --maximum-search-nodes 5000000\n"
				  << "  " << executable_name << " auto --round-count 4 --output-branch-a-mask 0x0 --output-branch-b-mask 0x1 --auto-breadth-jobs 512 --auto-breadth-top_candidates 3 --auto-breadth-threads 0\n";
	}

	static void print_banner_and_mode( const CommandLineOptions& command_line_options )
	{
		std::cout << "============================================================\n";
		std::cout << "  Best Trail Search (Linear correlations, reverse masks)\n";
		std::cout << "  - Nonlinear weights: addition(mod 2^32) via CCZ operator, subconst via exact 2x2 carry transfer\n";
		std::cout << "  - Injection: exact quadratic rank/2 model + affine-subspace branching (capped)\n";
		std::cout << "  - Cipher cross-mixing rotations: R0=" << NeoAlzetteCore::CROSS_XOR_ROT_R0 << "  R1=" << NeoAlzetteCore::CROSS_XOR_ROT_R1 << "  (R0+R1 mod32=" << NeoAlzetteCore::CROSS_XOR_ROT_SUM << ")\n";
		std::cout << "  - Command-line interface frontend: " << to_string( command_line_options.frontend_mode );
		if ( command_line_options.strategy_preset != CommandLineOptions::StrategyPreset::None )
		{
			std::cout << " (preset=" << to_string( command_line_options.strategy_preset ) << ", heuristics=" << ( command_line_options.strategy_heuristics_enabled ? "on" : "off" ) << ")";
		}
		std::cout << "\n";
		std::cout << "============================================================\n";
	}

	static bool parse_signed_integer_32( const char* text, int& value_out )
	{
		if ( !text )
			return false;
		char*	   end = nullptr;
		const long parsed_value = std::strtol( text, &end, 0 );
		if ( !end || *end != '\0' )
			return false;
		value_out = static_cast<int>( parsed_value );
		return true;
	}

	static bool parse_unsigned_integer_32( const char* text, std::uint32_t& value_out )
	{
		if ( !text )
			return false;
		char*					 end = nullptr;
		const unsigned long long v = std::strtoull( text, &end, 0 );
		if ( !end || *end != '\0' )
			return false;
		if ( v > 0xFFFFFFFFull )
			return false;
		value_out = static_cast<std::uint32_t>( v );
		return true;
	}

	static bool parse_unsigned_integer_64( const char* text, std::uint64_t& value_out )
	{
		if ( !text )
			return false;
		char*					 end = nullptr;
		const unsigned long long v = std::strtoull( text, &end, 0 );
		if ( !end || *end != '\0' )
			return false;
		value_out = static_cast<std::uint64_t>( v );
		return true;
	}

	template <class WriteFieldsFn>
	static void write_runtime_event_if_open( RuntimeEventLog& runtime_log, const char* event_name, WriteFieldsFn&& write_fields )
	{
		if ( runtime_log.out )
			runtime_log.write_event( event_name, std::forward<WriteFieldsFn>( write_fields ) );
	}

	static bool read_checkpoint_kind_only( const std::string& path, TwilightDream::auto_search_checkpoint::SearchKind& kind_out )
	{
		TwilightDream::auto_search_checkpoint::BinaryReader reader( path );
		return reader.ok() && TwilightDream::auto_search_checkpoint::read_header( reader, kind_out );
	}

	static bool parse_search_mode( const char* text, SearchMode& out )
	{
		if ( !text )
			return false;
		const std::string mode = to_lowercase_ascii( std::string( text ) );
		if ( mode == "strict" )
		{
			out = SearchMode::Strict;
			return true;
		}
		if ( mode == "fast" )
		{
			out = SearchMode::Fast;
			return true;
		}
		return false;
	}

	static bool parse_duration_in_seconds( const char* text, std::uint64_t& seconds_out )
	{
		// Accept:
		// - plain integer: seconds
		// - suffixed: Ns, Nm, Nh, Nd, Nw  (seconds/minutes/hours/days/weeks)
		// Examples: 3600, 3600s, 60m, 24h, 30d, 4w
		if ( !text )
			return false;
		const std::string input( text );
		if ( input.empty() )
			return false;

		// Find numeric prefix.
		std::size_t numeric_prefix_length = 0;
		while ( numeric_prefix_length < input.size() && ( ( input[ numeric_prefix_length ] >= '0' && input[ numeric_prefix_length ] <= '9' ) || ( numeric_prefix_length == 0 && input[ numeric_prefix_length ] == '+' ) ) )
		{
			++numeric_prefix_length;
		}
		if ( numeric_prefix_length == 0 )
			return false;

		std::uint64_t numeric_value = 0;
		{
			const std::string	numeric_part = input.substr( 0, numeric_prefix_length );
			char*				end = nullptr;
			const std::uint64_t parsed_value = std::strtoull( numeric_part.c_str(), &end, 10 );
			if ( !end || *end != '\0' )
				return false;
			numeric_value = parsed_value;
		}

		std::uint64_t multiplier = 1;
		if ( numeric_prefix_length < input.size() )
		{
			const std::string suffix = to_lowercase_ascii( input.substr( numeric_prefix_length ) );
			if ( suffix == "s" || suffix == "sec" || suffix == "secs" || suffix == "second" || suffix == "seconds" )
				multiplier = 1;
			else if ( suffix == "m" || suffix == "min" || suffix == "mins" || suffix == "minute" || suffix == "minutes" )
				multiplier = 60ull;
			else if ( suffix == "h" || suffix == "hr" || suffix == "hrs" || suffix == "hour" || suffix == "hours" )
				multiplier = 3600ull;
			else if ( suffix == "d" || suffix == "day" || suffix == "days" )
				multiplier = 86400ull;
			else if ( suffix == "w" || suffix == "wk" || suffix == "wks" || suffix == "week" || suffix == "weeks" )
				multiplier = 7ull * 86400ull;
			else
				return false;
		}

		// Saturating multiply to uint64.
		if ( numeric_value != 0 && multiplier != 0 )
		{
			const std::uint64_t maximum_value = std::numeric_limits<std::uint64_t>::max();
			if ( numeric_value > maximum_value / multiplier )
				seconds_out = maximum_value;
			else
				seconds_out = numeric_value * multiplier;
		}
		else
		{
			seconds_out = 0;
		}
		return true;
	}

	static bool parse_unsigned_size( const char* text, std::size_t& value_out )
	{
		std::uint64_t tmp = 0;
		if ( !parse_unsigned_integer_64( text, tmp ) )
			return false;
		value_out = static_cast<std::size_t>( tmp );
		return true;
	}

	static int compute_strategy_scale_from_total_work( std::uint64_t total_work )
	{
		// total_work -> multiplier for per-job search budget. Keep growth gentle.
		// scale = 1 + floor(log10(total_work)), clamped.
		// examples: 1 ->1, 10->2, 1e3->4, 1e6->7 (clamped).
		if ( total_work <= 1 )
			return 1;
		int			  s = 1;
		std::uint64_t v = total_work;
		while ( v >= 10 )
		{
			v /= 10;
			++s;
		}
		return std::clamp( s, 1, 8 );
	}

	static std::uint64_t saturating_mul_u64( std::uint64_t a, std::uint64_t b ) noexcept
	{
		if ( a == 0 || b == 0 )
			return 0;
		const std::uint64_t maxv = std::numeric_limits<std::uint64_t>::max();
		if ( a > maxv / b )
			return maxv;
		return a * b;
	}

	static void apply_strategy_defaults( CommandLineOptions& command_line_options, CommandLineOptions::StrategyPreset preset, int resolved_worker_threads )
	{
		// Baselines that should not surprise users.
		command_line_options.search_configuration.addition_weight_cap = 31;
		command_line_options.search_configuration.constant_subtraction_weight_cap = 32;
		command_line_options.search_configuration.enable_verbose_output = false;

		command_line_options.strategy_resolved_worker_threads = std::max( 1, resolved_worker_threads );
		{
			const SystemMemoryInfo mem = query_system_memory_info();
			command_line_options.strategy_total_physical_bytes = mem.total_physical_bytes;
			command_line_options.strategy_available_physical_bytes = mem.available_physical_bytes;
		}

		bool	  heuristics_enabled = true;
		const int scale = compute_strategy_scale_from_total_work( command_line_options.strategy_total_work_was_provided ? command_line_options.strategy_total_work : 1 );

		switch ( preset )
		{
		case CommandLineOptions::StrategyPreset::TimeFirst:
			heuristics_enabled = false;
			command_line_options.runtime_controls.maximum_search_nodes = saturating_mul_u64( 25'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 4096;
			command_line_options.search_configuration.maximum_injection_input_masks = 4096;
			break;
		case CommandLineOptions::StrategyPreset::Balanced:
			heuristics_enabled = true;
			command_line_options.runtime_controls.maximum_search_nodes = saturating_mul_u64( 5'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 512;
			command_line_options.search_configuration.maximum_injection_input_masks = 256;
			break;
		case CommandLineOptions::StrategyPreset::SpaceFirst:
			heuristics_enabled = true;
			command_line_options.runtime_controls.maximum_search_nodes = saturating_mul_u64( 1'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = false;
			command_line_options.search_configuration.maximum_round_predecessors = 256;
			command_line_options.search_configuration.maximum_injection_input_masks = 64;
			break;
		case CommandLineOptions::StrategyPreset::None:
		default:
			heuristics_enabled = true;
			command_line_options.runtime_controls.maximum_search_nodes = saturating_mul_u64( 5'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 512;
			command_line_options.search_configuration.maximum_injection_input_masks = 256;
			break;
		}

		// Time-first: compute and print memory-derived budget numbers (informational).
		if ( preset == CommandLineOptions::StrategyPreset::TimeFirst && command_line_options.strategy_available_physical_bytes != 0 )
		{
			const std::uint64_t headroom_bytes = compute_memory_headroom_bytes( command_line_options.strategy_available_physical_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
			command_line_options.strategy_target_headroom_bytes = headroom_bytes;
			const std::uint64_t budget_bytes = ( command_line_options.strategy_available_physical_bytes > headroom_bytes ) ? ( command_line_options.strategy_available_physical_bytes - headroom_bytes ) : 0;
			command_line_options.strategy_derived_budget_bytes = budget_bytes;
		}

		// Hard caps for maximum_search_nodes to avoid accidental "infinite" runs in presets.
		{
			const std::uint64_t cap = ( preset == CommandLineOptions::StrategyPreset::TimeFirst ) ? 250'000'000ull : ( preset == CommandLineOptions::StrategyPreset::Balanced ) ? 50'000'000ull : 10'000'000ull;
			command_line_options.runtime_controls.maximum_search_nodes = std::clamp<std::uint64_t>( command_line_options.runtime_controls.maximum_search_nodes, 1ull, cap );
		}

		command_line_options.strategy_preset = preset;
		command_line_options.strategy_heuristics_enabled = heuristics_enabled;
	}

	static bool parse_command_line_detail_mode( int argument_count, char** argument_values, int start_index, CommandLineOptions& command_line_options )
	{
		int argument_index = start_index;

		bool		  mask_a_option_was_provided = false;
		bool		  mask_b_option_was_provided = false;
		std::uint32_t mask_a_option_value = 0;
		std::uint32_t mask_b_option_value = 0;

		for ( ; argument_index < argument_count; ++argument_index )
		{
			const std::string argument = argument_values[ argument_index ] ? std::string( argument_values[ argument_index ] ) : std::string();
			if ( argument == "--selftest" || argument == "--help" || argument == "-h" )
				continue;
			if ( argument == "--mode" )
			{
				if ( argument_index + 1 < argument_count )
					++argument_index;
				continue;
			}

			if ( argument == "--resume" && argument_index + 1 < argument_count )
			{
				command_line_options.resume_path = argument_values[ ++argument_index ];
				command_line_options.resume_was_provided = true;
			}
			else if ( argument == "--runtime-log" && argument_index + 1 < argument_count )
			{
				command_line_options.runtime_log_path = argument_values[ ++argument_index ];
			}
			else if ( argument == "--checkpoint-out" && argument_index + 1 < argument_count )
			{
				command_line_options.checkpoint_out_path = argument_values[ ++argument_index ];
				command_line_options.checkpoint_out_was_provided = true;
			}
			else if ( ( argument == "--checkpoint-every-seconds" || argument == "--checkpoint-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.checkpoint_every_seconds = seconds;
				command_line_options.checkpoint_every_seconds_was_provided = true;
			}
			else if ( argument == "--selftest-seed" && argument_index + 1 < argument_count )
			{
				std::uint64_t seed = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seed ) )
					return false;
				command_line_options.selftest_seed = seed;
				command_line_options.selftest_seed_was_provided = true;
			}
			else if ( argument == "--selftest-cases" && argument_index + 1 < argument_count )
			{
				std::uint64_t count = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], count ) )
					return false;
				if ( count > std::numeric_limits<std::size_t>::max() )
					return false;
				command_line_options.selftest_case_count = static_cast<std::size_t>( count );
				command_line_options.selftest_case_count_was_provided = true;
			}
			else if ( argument == "--selftest-seed" && argument_index + 1 < argument_count )
			{
				std::uint64_t seed = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seed ) )
					return false;
				command_line_options.selftest_seed = seed;
				command_line_options.selftest_seed_was_provided = true;
			}
			else if ( argument == "--selftest-cases" && argument_index + 1 < argument_count )
			{
				std::uint64_t count = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], count ) )
					return false;
				if ( count > std::numeric_limits<std::size_t>::max() )
					return false;
				command_line_options.selftest_case_count = static_cast<std::size_t>( count );
				command_line_options.selftest_case_count_was_provided = true;
			}
			else if ( argument == "--selftest-seed" && argument_index + 1 < argument_count )
			{
				std::uint64_t seed = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seed ) )
					return false;
				command_line_options.selftest_seed = seed;
				command_line_options.selftest_seed_was_provided = true;
			}
			else if ( argument == "--selftest-cases" && argument_index + 1 < argument_count )
			{
				std::uint64_t count = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], count ) )
					return false;
				if ( count > std::numeric_limits<std::size_t>::max() )
					return false;
				command_line_options.selftest_case_count = static_cast<std::size_t>( count );
				command_line_options.selftest_case_count_was_provided = true;
			}

			else if ( ( argument == "--round-count" || argument == "--rounds" ) && argument_index + 1 < argument_count )
			{
				int round_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], round_count ) || round_count <= 0 )
					return false;
				command_line_options.round_count = round_count;
				command_line_options.round_count_was_provided = true;
			}
			else if ( ( argument == "--output-branch-a-mask" || argument == "--out-mask-a" || argument == "--mask-a" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_a_option_value ) )
					return false;
				mask_a_option_was_provided = true;
			}
			else if ( ( argument == "--output-branch-b-mask" || argument == "--out-mask-b" || argument == "--mask-b" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_b_option_value ) )
					return false;
				mask_b_option_was_provided = true;
			}
			else if ( ( argument == "--maximum-search-nodes" || argument == "--maxnodes" || argument == "--max-nodes" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t maximum_search_nodes = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], maximum_search_nodes ) )
					return false;
				command_line_options.runtime_controls.maximum_search_nodes = maximum_search_nodes;
				command_line_options.runtime_maximum_search_nodes_was_provided = true;
			}
			else if ( ( argument == "--maximum-search-seconds" || argument == "--maxsec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.maximum_search_seconds = seconds;
				command_line_options.runtime_maximum_search_seconds_was_provided = true;
			}
			else if ( ( argument == "--target-best-weight" || argument == "--target-weight" ) && argument_index + 1 < argument_count )
			{
				int w = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], w ) )
					return false;
				command_line_options.search_configuration.target_best_weight = w;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--search-mode" && argument_index + 1 < argument_count )
			{
				SearchMode mode = SearchMode::Fast;
				if ( !parse_search_mode( argument_values[ ++argument_index ], mode ) )
					return false;
				command_line_options.search_configuration.search_mode = mode;
				command_line_options.search_mode_was_provided = true;
			}
			else if ( argument == "--enable-z-shell" )
			{
				command_line_options.search_configuration.enable_weight_sliced_clat = true;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--disable-z-shell" )
			{
				command_line_options.search_configuration.enable_weight_sliced_clat = false;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--z-shell-max-candidates" && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.weight_sliced_clat_max_candidates = v;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--maximum-round-predecessors" || argument == "--max-round-predecessors" || argument == "--heuristic-branch-cap" || argument == "--hcap" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_round_predecessors = v;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--addition-weight-cap" || argument == "--add-weight-cap" || argument == "--add" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.addition_weight_cap = std::clamp( v, 0, 31 );
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--constant-subtraction-weight-cap" || argument == "--subconst-weight-cap" || argument == "--subconst-cap" || argument == "--subcap" || argument == "--subtract" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.constant_subtraction_weight_cap = std::clamp( v, 0, 32 );
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--maximum-injection-input-masks" || argument == "--maximum-injection-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_injection_input_masks = v;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--disable-state-memoization" || argument == "--nomemo" )
			{
				command_line_options.search_configuration.enable_state_memoization = false;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--enable-state-memoization" )
			{
				command_line_options.search_configuration.enable_state_memoization = true;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--enable-verbose-output" || argument == "--verbose" )
			{
				command_line_options.search_configuration.enable_verbose_output = true;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--batch-job-count" || argument == "--batch" ) && argument_index + 1 < argument_count )
			{
				// Detail: require an explicit job count.
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
					return false;
				command_line_options.batch_job_count = std::size_t( n );
			}
			else if ( argument == "--batch-file" && argument_index + 1 < argument_count )
			{
				command_line_options.batch_job_file = argument_values[ ++argument_index ];
				command_line_options.batch_job_file_was_provided = true;
			}
			else if ( ( argument == "--thread-count" || argument == "--threads" ) && argument_index + 1 < argument_count )
			{
				int thread_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], thread_count ) || thread_count < 0 )
					return false;
				command_line_options.batch_thread_count = thread_count;
			}
			else if ( argument == "--seed" && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], command_line_options.batch_seed ) )
					return false;
				command_line_options.batch_seed_was_provided = true;
			}
			else if ( ( argument == "--progress-every-jobs" || argument == "--progress" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t job_interval = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], job_interval ) )
					return false;
				command_line_options.progress_every_jobs = std::size_t( job_interval );
			}
			else if ( ( argument == "--progress-every-seconds" || argument == "--progress-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.progress_every_seconds = seconds;
			}
			else if ( argument == "--memory-headroom-mib" && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.memory_headroom_mib = mib;
				command_line_options.memory_headroom_mib_was_provided = true;
			}
			else if ( argument == "--memory-ballast" )
			{
				command_line_options.memory_ballast_enabled = true;
			}
			else if ( argument == "--allow-high-memory-usage" )
			{
				command_line_options.allow_high_memory_usage = true;
			}
			else if ( ( argument == "--rebuildable-reserve-mib" || argument == "--rebuildable-reserve" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.rebuildable_reserve_mib = mib;
				command_line_options.rebuildable_reserve_mib_was_provided = true;
			}
			else
			{
				std::cerr << "ERROR: unknown argument: " << argument << "\n";
				return false;
			}
		}

		// If masks were provided via options, they must be provided as a pair.
		if ( mask_a_option_was_provided || mask_b_option_was_provided )
		{
			if ( !( mask_a_option_was_provided && mask_b_option_was_provided ) )
				return false;
			command_line_options.output_branch_a_mask = mask_a_option_value;
			command_line_options.output_branch_b_mask = mask_b_option_value;
			command_line_options.output_masks_were_provided = true;
		}

		if ( command_line_options.round_count <= 0 )
			return false;
		if ( command_line_options.search_configuration.search_mode == SearchMode::Strict && !command_line_options.runtime_maximum_search_nodes_was_provided )
			command_line_options.runtime_controls.maximum_search_nodes = 0;

		// Selftest/help should be runnable without requiring masks.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		const bool batch_enabled = ( command_line_options.batch_job_count > 0 ) || command_line_options.batch_job_file_was_provided;

		if ( !batch_enabled && !command_line_options.resume_was_provided )
		{
			if ( !command_line_options.output_masks_were_provided )
				return false;
		}
		else
		{
			// Batch random-number-generator jobs require a seed; batch file does not.
			if ( command_line_options.batch_job_count > 0 && !command_line_options.batch_job_file_was_provided && !command_line_options.batch_seed_was_provided )
				return false;
		}
		if ( batch_enabled )
			return true;
		return command_line_options.output_masks_were_provided || command_line_options.resume_was_provided;
	}

	static bool parse_command_line_strategy_mode( int argument_count, char** argument_values, int start_index, CommandLineOptions& command_line_options )
	{
		CommandLineOptions::StrategyPreset preset = CommandLineOptions::StrategyPreset::Balanced;
		bool							   maximum_search_nodes_was_provided = false;
		std::uint64_t					   maximum_search_nodes_override = 0;

		// Allow a first positional token to be the preset (time/balanced/space).
		auto is_option = []( const char* s ) -> bool {
			return s && s[ 0 ] == '-';
		};
		if ( start_index < argument_count && !is_option( argument_values[ start_index ] ) )
		{
			if ( !parse_strategy_preset( argument_values[ start_index ], preset ) )
				return false;
			++start_index;
		}

		bool		  mask_a_option_was_provided = false;
		bool		  mask_b_option_was_provided = false;
		std::uint32_t mask_a_option_value = 0;
		std::uint32_t mask_b_option_value = 0;

		for ( int argument_index = start_index; argument_index < argument_count; ++argument_index )
		{
			const std::string argument = argument_values[ argument_index ] ? std::string( argument_values[ argument_index ] ) : std::string();
			if ( argument == "--selftest" || argument == "--help" || argument == "-h" )
				continue;
			if ( argument == "--mode" )
			{
				if ( argument_index + 1 < argument_count )
					++argument_index;
				continue;
			}

			if ( argument == "--resume" && argument_index + 1 < argument_count )
			{
				command_line_options.resume_path = argument_values[ ++argument_index ];
				command_line_options.resume_was_provided = true;
			}
			else if ( argument == "--runtime-log" && argument_index + 1 < argument_count )
			{
				command_line_options.runtime_log_path = argument_values[ ++argument_index ];
			}
			else if ( argument == "--checkpoint-out" && argument_index + 1 < argument_count )
			{
				command_line_options.checkpoint_out_path = argument_values[ ++argument_index ];
				command_line_options.checkpoint_out_was_provided = true;
			}
			else if ( ( argument == "--checkpoint-every-seconds" || argument == "--checkpoint-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.checkpoint_every_seconds = seconds;
				command_line_options.checkpoint_every_seconds_was_provided = true;
			}

			else if ( ( argument == "--preset" || argument == "--strategy" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_strategy_preset( argument_values[ ++argument_index ], preset ) )
					return false;
			}
			else if ( ( argument == "--round-count" || argument == "--rounds" ) && argument_index + 1 < argument_count )
			{
				int round_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], round_count ) || round_count <= 0 )
					return false;
				command_line_options.round_count = round_count;
				command_line_options.round_count_was_provided = true;
			}
			else if ( ( argument == "--output-branch-a-mask" || argument == "--out-mask-a" || argument == "--mask-a" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_a_option_value ) )
					return false;
				mask_a_option_was_provided = true;
			}
			else if ( ( argument == "--output-branch-b-mask" || argument == "--out-mask-b" || argument == "--mask-b" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_b_option_value ) )
					return false;
				mask_b_option_was_provided = true;
			}
			else if ( ( argument == "--batch-job-count" || argument == "--batch" ) && argument_index + 1 < argument_count )
			{
				// Strategy-only convenience:
				// - "--batch-job-count N" is explicit.
				// - "--batch N" is accepted (historical habit).
				// - "--batch" without a value means "enable batch", and total-work decides the count.
				const bool next_is_option = ( argument_index + 1 < argument_count ) && argument_values[ argument_index + 1 ] && argument_values[ argument_index + 1 ][ 0 ] == '-';
				if ( argument == "--batch" && next_is_option )
				{
					command_line_options.strategy_batch_was_requested = true;
				}
				else
				{
					command_line_options.strategy_batch_was_requested = true;
					std::uint64_t n = 0;
					if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
						return false;
					command_line_options.batch_job_count = std::size_t( n );
				}
			}
			else if ( argument == "--batch" )
			{
				command_line_options.strategy_batch_was_requested = true;
			}
			else if ( argument == "--batch-file" && argument_index + 1 < argument_count )
			{
				command_line_options.batch_job_file = argument_values[ ++argument_index ];
				command_line_options.batch_job_file_was_provided = true;
				command_line_options.strategy_batch_was_requested = true;
			}
			else if ( ( argument == "--thread-count" || argument == "--threads" ) && argument_index + 1 < argument_count )
			{
				int thread_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], thread_count ) || thread_count < 0 )
					return false;
				command_line_options.batch_thread_count = thread_count;
				if ( thread_count != 0 )
					command_line_options.strategy_batch_was_requested = true;
			}
			else if ( ( argument == "--total-work" || argument == "--total" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t total_work = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], total_work ) || total_work == 0 )
					return false;
				command_line_options.strategy_total_work = total_work;
				command_line_options.strategy_total_work_was_provided = true;
			}
			else if ( ( argument == "--maximum-search-nodes" || argument == "--maxnodes" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
					return false;
				maximum_search_nodes_override = n;
				maximum_search_nodes_was_provided = true;
			}
			else if ( ( argument == "--target-best-weight" || argument == "--target-weight" ) && argument_index + 1 < argument_count )
			{
				int w = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], w ) )
					return false;
				command_line_options.search_configuration.target_best_weight = w;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--search-mode" && argument_index + 1 < argument_count )
			{
				SearchMode mode = SearchMode::Fast;
				if ( !parse_search_mode( argument_values[ ++argument_index ], mode ) )
					return false;
				command_line_options.search_configuration.search_mode = mode;
				command_line_options.search_mode_was_provided = true;
			}
			else if ( argument == "--seed" && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], command_line_options.batch_seed ) )
					return false;
				command_line_options.batch_seed_was_provided = true;
			}
			else if ( ( argument == "--progress-every-jobs" || argument == "--progress" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t job_interval = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], job_interval ) )
					return false;
				command_line_options.progress_every_jobs = std::size_t( job_interval );
			}
			else if ( ( argument == "--progress-every-seconds" || argument == "--progress-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.progress_every_seconds = seconds;
			}
			else if ( argument == "--memory-headroom-mib" && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.memory_headroom_mib = mib;
				command_line_options.memory_headroom_mib_was_provided = true;
			}
			else if ( argument == "--memory-ballast" )
			{
				command_line_options.memory_ballast_enabled = true;
			}
			else if ( argument == "--allow-high-memory-usage" )
			{
				command_line_options.allow_high_memory_usage = true;
			}
			else if ( ( argument == "--rebuildable-reserve-mib" || argument == "--rebuildable-reserve" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.rebuildable_reserve_mib = mib;
				command_line_options.rebuildable_reserve_mib_was_provided = true;
			}
			else
			{
				// Strategy mode intentionally exposes fewer knobs.
				return false;
			}
		}

		// total-work decides BOTH batch-job-count and maximum_search_nodes:
		// - maximum_search_nodes is scaled regardless of single/batch.
		// - batch-job-count is derived from total-work ONLY when the user explicitly requested batch mode.
		if ( command_line_options.strategy_total_work_was_provided && command_line_options.strategy_batch_was_requested && command_line_options.batch_job_count == 0 )
		{
			command_line_options.batch_job_count = std::size_t( std::min<std::uint64_t>( command_line_options.strategy_total_work, std::uint64_t( std::numeric_limits<std::size_t>::max() ) ) );
		}
		if ( command_line_options.strategy_batch_was_requested && command_line_options.batch_job_count == 0 && !command_line_options.batch_job_file_was_provided )
			return false;

		// Apply the preset defaults.
		{
			const int resolved_threads = resolve_worker_thread_count_for_command_line_interface( command_line_options.batch_job_count, command_line_options.batch_job_file_was_provided, command_line_options.batch_thread_count );
			apply_strategy_defaults( command_line_options, preset, resolved_threads );
		}
		if ( maximum_search_nodes_was_provided )
		{
			// Allow 0=unlimited.
			command_line_options.runtime_controls.maximum_search_nodes = maximum_search_nodes_override;
			command_line_options.runtime_maximum_search_nodes_was_provided = true;
		}

		// If masks were provided via options, require both.
		if ( mask_a_option_was_provided || mask_b_option_was_provided )
		{
			if ( !( mask_a_option_was_provided && mask_b_option_was_provided ) )
				return false;
			command_line_options.output_branch_a_mask = mask_a_option_value;
			command_line_options.output_branch_b_mask = mask_b_option_value;
			command_line_options.output_masks_were_provided = true;
		}

		// Selftest/help should be runnable without requiring masks or --seed.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		const bool batch_enabled = ( command_line_options.batch_job_count > 0 ) || command_line_options.batch_job_file_was_provided;
		if ( !batch_enabled && !command_line_options.resume_was_provided )
		{
			if ( !command_line_options.output_masks_were_provided )
			{
				if ( !command_line_options.batch_seed_was_provided )
					return false;
				std::mt19937_64 rng( command_line_options.batch_seed );
				std::uint32_t	a = 0;
				std::uint32_t	b = 0;
				do
				{
					a = static_cast<std::uint32_t>( rng() );
					b = static_cast<std::uint32_t>( rng() );
				} while ( a == 0u && b == 0u );
				command_line_options.output_branch_a_mask = a;
				command_line_options.output_branch_b_mask = b;
				command_line_options.output_masks_were_generated = true;
				command_line_options.output_masks_were_provided = true;
			}
		}
		else
		{
			// Batch random-number-generator jobs require a seed; batch file does not.
			if ( command_line_options.batch_job_count > 0 && !command_line_options.batch_job_file_was_provided && !command_line_options.batch_seed_was_provided )
				return false;
		}
		return true;
	}

	static bool parse_command_line_auto_mode( int argument_count, char** argument_values, int start_index, CommandLineOptions& command_line_options )
	{
		// Auto mode: requires explicit (mask_a, mask_b); no random-number-generator seed fallback; single-run only.
		int	 argument_index = start_index;
		auto is_option = []( const char* s ) -> bool {
			return s && s[ 0 ] == '-';
		};
		if ( argument_index < argument_count && !is_option( argument_values[ argument_index ] ) )
		{
			int round_count = 0;
			if ( !parse_signed_integer_32( argument_values[ argument_index ], round_count ) || round_count <= 0 )
				return false;
			command_line_options.round_count = round_count;
			command_line_options.round_count_was_provided = true;
			++argument_index;
		}
		if ( argument_index < argument_count && !is_option( argument_values[ argument_index ] ) )
		{
			if ( !parse_unsigned_integer_32( argument_values[ argument_index ], command_line_options.output_branch_a_mask ) )
				return false;
			command_line_options.output_masks_were_provided = true;
			++argument_index;
		}
		if ( command_line_options.output_masks_were_provided )
		{
			if ( argument_index >= argument_count || is_option( argument_values[ argument_index ] ) )
				return false;
			if ( !parse_unsigned_integer_32( argument_values[ argument_index ], command_line_options.output_branch_b_mask ) )
				return false;
			++argument_index;
		}

		bool		  mask_a_option_was_provided = false;
		bool		  mask_b_option_was_provided = false;
		std::uint32_t mask_a_option_value = 0;
		std::uint32_t mask_b_option_value = 0;

		for ( ; argument_index < argument_count; ++argument_index )
		{
			const std::string argument = argument_values[ argument_index ] ? std::string( argument_values[ argument_index ] ) : std::string();
			if ( argument == "--selftest" || argument == "--help" || argument == "-h" )
				continue;
			if ( argument == "--mode" )
			{
				if ( argument_index + 1 < argument_count )
					++argument_index;
				continue;
			}

			if ( argument == "--resume" && argument_index + 1 < argument_count )
			{
				command_line_options.resume_path = argument_values[ ++argument_index ];
				command_line_options.resume_was_provided = true;
			}
			else if ( argument == "--runtime-log" && argument_index + 1 < argument_count )
			{
				command_line_options.runtime_log_path = argument_values[ ++argument_index ];
			}
			else if ( argument == "--checkpoint-out" && argument_index + 1 < argument_count )
			{
				command_line_options.checkpoint_out_path = argument_values[ ++argument_index ];
				command_line_options.checkpoint_out_was_provided = true;
			}
			else if ( ( argument == "--checkpoint-every-seconds" || argument == "--checkpoint-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.checkpoint_every_seconds = seconds;
				command_line_options.checkpoint_every_seconds_was_provided = true;
			}

			else if ( ( argument == "--round-count" || argument == "--rounds" ) && argument_index + 1 < argument_count )
			{
				int round_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], round_count ) || round_count <= 0 )
					return false;
				command_line_options.round_count = round_count;
				command_line_options.round_count_was_provided = true;
			}
			else if ( ( argument == "--output-branch-a-mask" || argument == "--out-mask-a" || argument == "--mask-a" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_a_option_value ) )
					return false;
				mask_a_option_was_provided = true;
			}
			else if ( ( argument == "--output-branch-b-mask" || argument == "--out-mask-b" || argument == "--mask-b" ) && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_32( argument_values[ ++argument_index ], mask_b_option_value ) )
					return false;
				mask_b_option_was_provided = true;
			}

			else if ( ( argument == "--addition-weight-cap" || argument == "--add-weight-cap" || argument == "--add" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.addition_weight_cap = std::clamp( v, 0, 31 );
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--constant-subtraction-weight-cap" || argument == "--subconst-weight-cap" || argument == "--subconst-cap" || argument == "--subcap" || argument == "--subtract" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.constant_subtraction_weight_cap = std::clamp( v, 0, 32 );
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--maximum-search-nodes" || argument == "--maxnodes" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t maximum_search_nodes = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], maximum_search_nodes ) )
					return false;
				command_line_options.runtime_controls.maximum_search_nodes = maximum_search_nodes;
				command_line_options.runtime_maximum_search_nodes_was_provided = true;
			}
			else if ( ( argument == "--maximum-search-seconds" || argument == "--maxsec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.maximum_search_seconds = seconds;
				command_line_options.runtime_maximum_search_seconds_was_provided = true;
			}
			else if ( ( argument == "--maximum-round-predecessors" || argument == "--max-round-predecessors" || argument == "--hcap" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_round_predecessors = v;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( ( argument == "--maximum-injection-input-masks" || argument == "--injection-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_injection_input_masks = v;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--disable-state-memoization" || argument == "--nomemo" )
			{
				command_line_options.search_configuration.enable_state_memoization = false;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--enable-state-memoization" )
			{
				command_line_options.search_configuration.enable_state_memoization = true;
				command_line_options.search_configuration_was_provided = true;
			}

			else if ( ( argument == "--auto-breadth-jobs" || argument == "--auto-breadth-max-runs" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
					return false;
				command_line_options.auto_breadth_candidate_count = std::max<std::size_t>( 1, std::size_t( n ) );
			}
			else if ( argument == "--auto-breadth-top_candidates" && argument_index + 1 < argument_count )
			{
				std::uint64_t k = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], k ) )
					return false;
				command_line_options.auto_breadth_top_k = std::max<std::size_t>( 1, std::size_t( k ) );
			}
			else if ( argument == "--auto-breadth-threads" && argument_index + 1 < argument_count )
			{
				int t = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], t ) || t < 0 )
					return false;
				command_line_options.auto_breadth_thread_count = t;
			}
			else if ( argument == "--auto-breadth-strategy" && argument_index + 1 < argument_count )
			{
				if ( !parse_auto_breadth_strategy( argument_values[ ++argument_index ], command_line_options.auto_breadth_strategy ) )
					return false;
			}
			else if ( argument == "--auto-breadth-frontier-width" && argument_index + 1 < argument_count )
			{
				std::uint64_t width = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], width ) )
					return false;
				command_line_options.auto_breadth_frontier_width = std::max<std::size_t>( 1, std::size_t( width ) );
			}
			else if ( argument == "--auto-breadth-max-hops" && argument_index + 1 < argument_count )
			{
				int hops = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], hops ) || hops < 0 )
					return false;
				command_line_options.auto_breadth_max_hops = hops;
			}
			else if ( ( argument == "--auto-breadth-seed" || argument == "--auto-breadth-rng-seed" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seed = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seed ) )
					return false;
				command_line_options.auto_breadth_seed = seed;
				command_line_options.auto_breadth_seed_was_provided = true;
			}
			else if ( ( argument == "--auto-breadth-max-bitflips" || argument == "--auto-breadth-max_bitflips" ) && argument_index + 1 < argument_count )
			{
				int max_flips = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], max_flips ) )
					return false;
				command_line_options.auto_breadth_max_random_bitflips = max_flips;
			}
			else if ( argument == "--auto-breadth-maxnodes" && argument_index + 1 < argument_count )
			{
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
					return false;
				command_line_options.auto_breadth_maximum_search_nodes = std::max<std::uint64_t>( 1, n );
			}
			else if ( ( argument == "--auto-breadth-heuristic-branch-cap" || argument == "--auto-breadth-hcap" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t v = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.auto_breadth_maximum_round_predecessors = std::size_t( v );
			}
			else if ( argument == "--auto-print-breadth-candidates" )
			{
				command_line_options.auto_print_breadth_candidates = true;
			}
			else if ( argument == "--auto-deep-maxnodes" && argument_index + 1 < argument_count )
			{
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], n ) )
					return false;
				command_line_options.auto_deep_maximum_search_nodes = n;  // 0 means unlimited (user explicitly requested this behavior)
			}
			else if ( argument == "--auto-max-time" && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.auto_max_time_seconds = seconds;
			}
			else if ( argument == "--auto-target-best-weight" && argument_index + 1 < argument_count )
			{
				int w = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], w ) )
					return false;
				command_line_options.auto_target_best_weight = w;
				command_line_options.search_configuration_was_provided = true;
			}
			else if ( argument == "--search-mode" && argument_index + 1 < argument_count )
			{
				SearchMode mode = SearchMode::Fast;
				if ( !parse_search_mode( argument_values[ ++argument_index ], mode ) )
					return false;
				command_line_options.search_configuration.search_mode = mode;
				command_line_options.search_mode_was_provided = true;
			}
			else if ( ( argument == "--progress-every-jobs" || argument == "--progress" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t job_interval = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], job_interval ) )
					return false;
				command_line_options.progress_every_jobs = std::size_t( job_interval );
			}
			else if ( ( argument == "--progress-every-seconds" || argument == "--progress-sec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.runtime_controls.progress_every_seconds = seconds;
			}
			else if ( argument == "--memory-headroom-mib" && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.memory_headroom_mib = mib;
				command_line_options.memory_headroom_mib_was_provided = true;
			}
			else if ( argument == "--memory-ballast" )
			{
				command_line_options.memory_ballast_enabled = true;
			}
			else if ( argument == "--allow-high-memory-usage" )
			{
				command_line_options.allow_high_memory_usage = true;
			}
			else if ( ( argument == "--rebuildable-reserve-mib" || argument == "--rebuildable-reserve" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t mib = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], mib ) )
					return false;
				command_line_options.rebuildable_reserve_mib = mib;
				command_line_options.rebuildable_reserve_mib_was_provided = true;
			}
			else
			{
				return false;
			}
		}

		// Apply mask options override.
		if ( mask_a_option_was_provided || mask_b_option_was_provided )
		{
			if ( !( mask_a_option_was_provided && mask_b_option_was_provided ) )
				return false;
			command_line_options.output_branch_a_mask = mask_a_option_value;
			command_line_options.output_branch_b_mask = mask_b_option_value;
			command_line_options.output_masks_were_provided = true;
		}

		if ( command_line_options.round_count <= 0 )
			command_line_options.round_count = 1;

		if ( command_line_options.auto_breadth_candidate_count == 0 )
			command_line_options.auto_breadth_candidate_count = 1;
		if ( command_line_options.auto_breadth_top_k == 0 )
			command_line_options.auto_breadth_top_k = 1;
		if ( command_line_options.auto_breadth_frontier_width == 0 )
			command_line_options.auto_breadth_frontier_width = 1;
		if ( command_line_options.auto_breadth_max_hops < 0 )
			command_line_options.auto_breadth_max_hops = 0;
		if ( command_line_options.auto_breadth_maximum_search_nodes == 0 )
			command_line_options.auto_breadth_maximum_search_nodes = 1;
		if ( command_line_options.auto_breadth_max_random_bitflips < 0 )
			command_line_options.auto_breadth_max_random_bitflips = 0;
		if ( command_line_options.auto_breadth_max_random_bitflips > 64 )
			command_line_options.auto_breadth_max_random_bitflips = 64;
		if ( command_line_options.auto_target_best_weight >= 0 )
			command_line_options.search_configuration.target_best_weight = command_line_options.auto_target_best_weight;

		// Selftest/help should be runnable without requiring masks.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		// Auto mode requires explicit masks (no --seed fallback).
		if ( !command_line_options.output_masks_were_provided && !command_line_options.resume_was_provided )
			return false;
		if ( !command_line_options.resume_was_provided && command_line_options.output_branch_a_mask == 0u && command_line_options.output_branch_b_mask == 0u )
			return false;
		return true;
	}

	static bool parse_command_line( int argument_count, char** argument_values, CommandLineOptions& command_line_options )
	{
		// Fast scan for help/selftest first (so we can exit early with help even on invalid args).
		for ( int i = 1; i < argument_count; ++i )
		{
			const std::string a = argument_values[ i ] ? std::string( argument_values[ i ] ) : std::string();
			if ( a == "--selftest" )
				command_line_options.selftest = true;
			if ( a == "--help" || a == "-h" )
				command_line_options.show_help = true;
		}

		// Selftest/help should be runnable without requiring masks.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		// Dispatcher: decide which frontend to use.
		CommandLineOptions::FrontendMode frontend = CommandLineOptions::FrontendMode::Detail;
		int								 start_index = 1;

		// Subcommand form: exe strategy|detail|auto ...
		if ( argument_count >= 2 )
		{
			const std::string sub = to_lowercase_ascii( std::string( argument_values[ 1 ] ) );
			if ( sub == "strategy" )
			{
				frontend = CommandLineOptions::FrontendMode::Strategy;
				start_index = 2;
			}
			else if ( sub == "detail" )
			{
				frontend = CommandLineOptions::FrontendMode::Detail;
				start_index = 2;
			}
			else if ( sub == "auto" )
			{
				frontend = CommandLineOptions::FrontendMode::Auto;
				start_index = 2;
			}
		}

		// Flag form: --mode strategy|detail|auto (if present, overrides default detail when no subcommand is used).
		if ( start_index == 1 )
		{
			for ( int i = 1; i + 1 < argument_count; ++i )
			{
				const std::string a = argument_values[ i ] ? std::string( argument_values[ i ] ) : std::string();
				if ( a != "--mode" )
					continue;
				const std::string m = to_lowercase_ascii( std::string( argument_values[ i + 1 ] ) );
				if ( m == "strategy" )
					frontend = CommandLineOptions::FrontendMode::Strategy;
				else if ( m == "detail" )
					frontend = CommandLineOptions::FrontendMode::Detail;
				else if ( m == "auto" )
					frontend = CommandLineOptions::FrontendMode::Auto;
			}
		}

		command_line_options.frontend_mode = frontend;
		bool ok = false;
		switch ( frontend )
		{
		case CommandLineOptions::FrontendMode::Strategy:
			ok = parse_command_line_strategy_mode( argument_count, argument_values, start_index, command_line_options );
			break;
		case CommandLineOptions::FrontendMode::Detail:
			ok = parse_command_line_detail_mode( argument_count, argument_values, start_index, command_line_options );
			break;
		case CommandLineOptions::FrontendMode::Auto:
			ok = parse_command_line_auto_mode( argument_count, argument_values, start_index, command_line_options );
			break;
		default:
			ok = parse_command_line_detail_mode( argument_count, argument_values, start_index, command_line_options );
			break;
		}
		if ( ok && command_line_options.search_mode_was_provided )
		{
			const LinearFastSearchCapDefaults defaults = compute_default_fast_search_caps_for_cli( command_line_options );
			apply_search_mode_overrides(
				command_line_options.search_configuration,
				defaults.maximum_injection_input_masks,
				defaults.maximum_round_predecessors );
			if ( command_line_options.frontend_mode == CommandLineOptions::FrontendMode::Strategy )
				command_line_options.strategy_heuristics_enabled = ( command_line_options.search_configuration.search_mode == SearchMode::Fast );
		}
		return ok;
	}

	static void print_result( const BestSearchResult& result );

	static inline double weight_to_abs_correlation( int weight )
	{
		if ( weight >= INFINITE_WEIGHT )
			return 0.0;
		return std::pow( 2.0, -double( weight ) );
	}

	static bool configs_equal( const LinearBestSearchConfiguration& a, const LinearBestSearchConfiguration& b )
	{
		return a.round_count == b.round_count &&
			a.addition_weight_cap == b.addition_weight_cap &&
			a.constant_subtraction_weight_cap == b.constant_subtraction_weight_cap &&
			a.enable_weight_sliced_clat == b.enable_weight_sliced_clat &&
			a.weight_sliced_clat_max_candidates == b.weight_sliced_clat_max_candidates &&
			a.maximum_injection_input_masks == b.maximum_injection_input_masks &&
			a.maximum_round_predecessors == b.maximum_round_predecessors &&
			a.target_best_weight == b.target_best_weight &&
			a.enable_state_memoization == b.enable_state_memoization &&
			a.enable_remaining_round_lower_bound == b.enable_remaining_round_lower_bound &&
			a.remaining_round_min_weight == b.remaining_round_min_weight &&
			a.auto_generate_remaining_round_lower_bound == b.auto_generate_remaining_round_lower_bound &&
			a.remaining_round_lower_bound_generation_nodes == b.remaining_round_lower_bound_generation_nodes &&
			a.remaining_round_lower_bound_generation_seconds == b.remaining_round_lower_bound_generation_seconds &&
			a.strict_remaining_round_lower_bound == b.strict_remaining_round_lower_bound &&
			a.enable_verbose_output == b.enable_verbose_output;
	}

	static SearchMode infer_search_mode_from_caps( const LinearBestSearchConfiguration& c )
	{
		if ( c.maximum_injection_input_masks == 0 &&
			 c.maximum_round_predecessors == 0 )
		{
			return SearchMode::Strict;
		}
		return SearchMode::Fast;
	}

	static bool apply_search_mode_overrides(
		LinearBestSearchConfiguration& c,
		std::size_t default_maximum_injection_input_masks,
		std::size_t default_maximum_round_predecessors )
	{
		bool changed = false;
		if ( c.search_mode == SearchMode::Strict )
		{
			if ( c.maximum_injection_input_masks != 0 )
			{
				const std::size_t prev = c.maximum_injection_input_masks;
				c.maximum_injection_input_masks = 0;
				changed = true;
				{
					IosStateGuard g( std::cout );
					std::cout << "[SearchMode::Strict] maximum_injection_input_masks " << prev
							  << " -> 0 (unlimited; required for strict branch enumeration)\n";
				}
			}
			if ( c.maximum_round_predecessors != 0 )
			{
				const std::size_t prev = c.maximum_round_predecessors;
				c.maximum_round_predecessors = 0;
				changed = true;
				{
					IosStateGuard g( std::cout );
					std::cout << "[SearchMode::Strict] maximum_round_predecessors " << prev
							  << " -> 0 (unlimited; required for strict branch enumeration)\n";
				}
			}
			// cLAT z-shell streaming strict path: 0 == no candidate cap (nonzero => truncated / non-strict).
			if ( c.weight_sliced_clat_max_candidates != 0 )
			{
				const std::size_t prev = c.weight_sliced_clat_max_candidates;
				c.weight_sliced_clat_max_candidates = 0;
				changed = true;
				{
					IosStateGuard g( std::cout );
					std::cout << "[SearchMode::Strict] weight_sliced_clat_max_candidates " << prev
							  << " -> 0 (unlimited; required when weight-sliced cLAT is enabled for full z-shell path)\n";
				}
			}
			return changed;
		}

		if ( c.maximum_injection_input_masks == 0 && default_maximum_injection_input_masks != 0 )
		{
			c.maximum_injection_input_masks = default_maximum_injection_input_masks;
			changed = true;
		}
		if ( c.maximum_round_predecessors == 0 && default_maximum_round_predecessors != 0 )
		{
			c.maximum_round_predecessors = default_maximum_round_predecessors;
			changed = true;
		}
		return changed;
	}

	static LinearFastSearchCapDefaults compute_default_fast_search_caps_for_cli( const CommandLineOptions& command_line_options )
	{
		LinearFastSearchCapDefaults defaults {};
		if ( command_line_options.frontend_mode == CommandLineOptions::FrontendMode::Strategy )
		{
			switch ( command_line_options.strategy_preset )
			{
			case CommandLineOptions::StrategyPreset::TimeFirst:
				defaults.maximum_round_predecessors = 4096;
				defaults.maximum_injection_input_masks = 4096;
				break;
			case CommandLineOptions::StrategyPreset::SpaceFirst:
				defaults.maximum_round_predecessors = 256;
				defaults.maximum_injection_input_masks = 64;
				break;
			case CommandLineOptions::StrategyPreset::Balanced:
			case CommandLineOptions::StrategyPreset::None:
			default:
				defaults.maximum_round_predecessors = 512;
				defaults.maximum_injection_input_masks = 256;
				break;
			}
			return defaults;
		}
		defaults.maximum_round_predecessors = 512;
		defaults.maximum_injection_input_masks = 256;
		return defaults;
	}

	static void normalize_config_for_compare( LinearBestSearchConfiguration& c )
	{
		if ( c.enable_remaining_round_lower_bound )
		{
			const bool generation_limited =
				c.auto_generate_remaining_round_lower_bound &&
				( c.remaining_round_lower_bound_generation_nodes != 0 || c.remaining_round_lower_bound_generation_seconds != 0 );
			if ( generation_limited && c.strict_remaining_round_lower_bound )
			{
				c.enable_remaining_round_lower_bound = false;
				c.remaining_round_min_weight.clear();
			}

			if ( c.enable_remaining_round_lower_bound )
			{
				auto& remaining_round_min_weight_table = c.remaining_round_min_weight;
				if ( !remaining_round_min_weight_table.empty() )
				{
					if ( remaining_round_min_weight_table.size() < 1u )
						remaining_round_min_weight_table.resize( 1u, 0 );
					remaining_round_min_weight_table[ 0 ] = 0;
					const std::size_t need = std::size_t( std::max( 0, c.round_count ) ) + 1u;
					if ( remaining_round_min_weight_table.size() < need )
						remaining_round_min_weight_table.resize( need, 0 );
					for ( int& round_min_weight : remaining_round_min_weight_table )
					{
						if ( round_min_weight < 0 )
							round_min_weight = 0;
					}
				}
			}
		}
	}

	static bool configs_compatible_for_resume( LinearBestSearchConfiguration a, LinearBestSearchConfiguration b )
	{
		normalize_config_for_compare( a );
		normalize_config_for_compare( b );

		const bool a_autogen_empty = a.auto_generate_remaining_round_lower_bound && a.remaining_round_min_weight.empty();
		if ( a_autogen_empty )
		{
			// Allow auto-generated table to differ; require all other fields to match.
			auto a_copy = a;
			auto b_copy = b;
			a_copy.remaining_round_min_weight.clear();
			b_copy.remaining_round_min_weight.clear();
			return configs_equal( a_copy, b_copy );
		}
		return configs_equal( a, b );
	}

	struct LinearAutoBreadthJob
	{
		std::uint32_t mask_a = 0;
		std::uint32_t mask_b = 0;
	};

	struct LinearAutoCandidate
	{
		std::uint32_t					   start_mask_a = 0;
		std::uint32_t					   start_mask_b = 0;
		bool							   found = false;
		int								   best_weight = INFINITE_WEIGHT;
		std::uint64_t					   nodes = 0;
		bool							   hit_maximum_search_nodes_limit = false;
		std::uint32_t					   best_input_mask_a = 0;
		std::uint32_t					   best_input_mask_b = 0;
		std::vector<LinearTrailStepRecord> trail {};
	};

	enum class LinearAutoPipelineStage : std::uint8_t
	{
		Breadth = 1,
		Deep = 2
	};

	struct LinearAutoPipelineState
	{
		LinearBestSearchConfiguration			   search_configuration {};
		int										   round_count = 1;
		std::uint32_t							   start_output_mask_branch_a = 0;
		std::uint32_t							   start_output_mask_branch_b = 0;
		CommandLineOptions::AutoBreadthStrategy	   auto_breadth_strategy = CommandLineOptions::AutoBreadthStrategy::FrontierBfs;
		std::uint64_t							   auto_breadth_seed = 0;
		bool									   auto_breadth_seed_was_provided = false;
		std::size_t								   auto_breadth_top_k = 1;
		LinearAutoPipelineStage					   stage = LinearAutoPipelineStage::Breadth;
		std::vector<LinearAutoBreadthJob>		   jobs {};
		std::vector<std::uint8_t>				   completed_job_flags {};
		std::uint64_t							   breadth_total_nodes_visited = 0;
		std::vector<LinearAutoCandidate>		   top_candidates {};
		bool									   has_selected_candidate = false;
		LinearAutoCandidate						   selected_candidate {};
		bool									   has_deep_snapshot = false;
		LinearCheckpointLoadResult				   deep_snapshot {};
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int> deep_memoization {};
		std::string								   runtime_log_path {};
		std::string								   history_log_path {};
		std::uint64_t							   last_runtime_maximum_search_nodes = 0;
		std::uint64_t							   last_runtime_maximum_search_seconds = 0;
		std::uint64_t							   last_runtime_progress_every_seconds = 0;
		std::uint64_t							   last_runtime_checkpoint_every_seconds = 0;
		std::uint64_t							   last_run_elapsed_usec = 0;
	};

	static bool linear_auto_candidate_better( const LinearAutoCandidate& a, const LinearAutoCandidate& b )
	{
		if ( a.best_weight != b.best_weight )
			return a.best_weight < b.best_weight;
		if ( a.start_mask_a != b.start_mask_a )
			return a.start_mask_a < b.start_mask_a;
		if ( a.start_mask_b != b.start_mask_b )
			return a.start_mask_b < b.start_mask_b;
		return a.nodes < b.nodes;
	}

	static void linear_sort_auto_top_candidates( std::vector<LinearAutoCandidate>& top_candidates )
	{
		std::sort( top_candidates.begin(), top_candidates.end(), []( const LinearAutoCandidate& x, const LinearAutoCandidate& y ) {
			return linear_auto_candidate_better( x, y );
		} );
	}

	static void linear_try_update_auto_top_candidates( std::vector<LinearAutoCandidate>& top_candidates, std::size_t top_k, LinearAutoCandidate&& candidate )
	{
		if ( !candidate.found || top_k == 0 )
			return;
		if ( top_candidates.size() < top_k )
		{
			top_candidates.push_back( std::move( candidate ) );
			linear_sort_auto_top_candidates( top_candidates );
			return;
		}

		std::size_t worst = 0;
		for ( std::size_t i = 1; i < top_candidates.size(); ++i )
		{
			if ( linear_auto_candidate_better( top_candidates[ worst ], top_candidates[ i ] ) )
				continue;
			worst = i;
		}
		if ( linear_auto_candidate_better( candidate, top_candidates[ worst ] ) )
		{
			top_candidates[ worst ] = std::move( candidate );
			linear_sort_auto_top_candidates( top_candidates );
		}
	}

	static void linear_update_auto_candidate_from_best_result( LinearAutoCandidate& candidate, const BestSearchResult& result )
	{
		candidate.found = result.found;
		candidate.nodes = result.nodes_visited;
		candidate.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
		if ( !result.found )
			return;
		candidate.best_weight = result.best_weight;
		candidate.best_input_mask_a = result.best_input_branch_a_mask;
		candidate.best_input_mask_b = result.best_input_branch_b_mask;
		candidate.trail = result.best_linear_trail;
	}

	static void linear_apply_deep_result_to_auto_candidates(
		std::vector<LinearAutoCandidate>& top_candidates,
		LinearAutoCandidate& selected_candidate,
		const BestSearchResult& result )
	{
		const std::uint32_t start_mask_a = selected_candidate.start_mask_a;
		const std::uint32_t start_mask_b = selected_candidate.start_mask_b;
		linear_update_auto_candidate_from_best_result( selected_candidate, result );

		bool updated_existing = false;
		for ( auto& candidate : top_candidates )
		{
			if ( candidate.start_mask_a != start_mask_a || candidate.start_mask_b != start_mask_b )
				continue;
			linear_update_auto_candidate_from_best_result( candidate, result );
			updated_existing = true;
			break;
		}
		if ( !updated_existing )
			top_candidates.push_back( selected_candidate );
		linear_sort_auto_top_candidates( top_candidates );
	}

	static std::size_t linear_count_completed_jobs( const std::vector<std::uint8_t>& completed_job_flags )
	{
		return TwilightDream::auto_pipeline_shared::count_completed_flags( completed_job_flags );
	}

	struct LinearAutoPipelineFingerprint
	{
		std::uint64_t hash = 0;
		std::uint64_t completed_jobs = 0;
		std::uint64_t completed_flags_digest = 0;
		std::uint64_t top_candidate_count = 0;
		std::uint64_t top_candidates_digest = 0;
		bool has_selected_candidate = false;
		std::uint64_t selected_candidate_digest = 0;
		bool has_deep_snapshot = false;
		std::uint64_t deep_resume_fingerprint_hash = 0;
	};

	static void mix_linear_auto_candidate_into_fingerprint(
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
		const LinearAutoCandidate& candidate )
	{
		fp.mix_u32( candidate.start_mask_a );
		fp.mix_u32( candidate.start_mask_b );
		fp.mix_bool( candidate.found );
		fp.mix_i32( candidate.best_weight );
		fp.mix_u64( candidate.nodes );
		fp.mix_bool( candidate.hit_maximum_search_nodes_limit );
		fp.mix_u32( candidate.best_input_mask_a );
		fp.mix_u32( candidate.best_input_mask_b );
		fp.mix_u64( static_cast<std::uint64_t>( candidate.trail.size() ) );
		for ( const auto& step : candidate.trail )
		{
			fp.mix_i32( step.round_index );
			fp.mix_i32( step.round_weight );
			fp.mix_u32( step.output_branch_a_mask );
			fp.mix_u32( step.output_branch_b_mask );
			fp.mix_u32( step.input_branch_a_mask );
			fp.mix_u32( step.input_branch_b_mask );
		}
	}

	static LinearAutoPipelineFingerprint compute_linear_auto_pipeline_fingerprint(
		const LinearAutoPipelineState& state,
		const LinearBestSearchContext* deep_context = nullptr,
		const LinearSearchCursor* deep_cursor = nullptr )
	{
		LinearAutoPipelineFingerprint fingerprint {};
		fingerprint.completed_jobs = static_cast<std::uint64_t>( linear_count_completed_jobs( state.completed_job_flags ) );
		fingerprint.top_candidate_count = static_cast<std::uint64_t>( state.top_candidates.size() );
		fingerprint.has_selected_candidate = state.has_selected_candidate;
		fingerprint.has_deep_snapshot = ( deep_context != nullptr && deep_cursor != nullptr ) || state.has_deep_snapshot;

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder completed_fp {};
		completed_fp.mix_u64( static_cast<std::uint64_t>( state.completed_job_flags.size() ) );
		for ( const std::uint8_t flag : state.completed_job_flags )
			completed_fp.mix_u8( flag );
		fingerprint.completed_flags_digest = completed_fp.finish();

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder top_fp {};
		top_fp.mix_u64( fingerprint.top_candidate_count );
		for ( const auto& candidate : state.top_candidates )
			mix_linear_auto_candidate_into_fingerprint( top_fp, candidate );
		fingerprint.top_candidates_digest = top_fp.finish();

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder selected_fp {};
		if ( state.has_selected_candidate )
			mix_linear_auto_candidate_into_fingerprint( selected_fp, state.selected_candidate );
		fingerprint.selected_candidate_digest = selected_fp.finish();

		if ( deep_context != nullptr && deep_cursor != nullptr )
			fingerprint.deep_resume_fingerprint_hash = compute_linear_resume_fingerprint( *deep_context, *deep_cursor ).hash;
		else if ( state.has_deep_snapshot )
			fingerprint.deep_resume_fingerprint_hash = compute_linear_resume_fingerprint( state.deep_snapshot ).hash;

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
		fp.mix_i32( state.round_count );
		fp.mix_u32( state.start_output_mask_branch_a );
		fp.mix_u32( state.start_output_mask_branch_b );
		fp.mix_enum( state.stage );
		fp.mix_u64( static_cast<std::uint64_t>( state.jobs.size() ) );
		for ( const auto& job : state.jobs )
		{
			fp.mix_u32( job.mask_a );
			fp.mix_u32( job.mask_b );
		}
		fp.mix_u64( state.breadth_total_nodes_visited );
		fp.mix_u64( fingerprint.completed_flags_digest );
		fp.mix_u64( fingerprint.top_candidates_digest );
		fp.mix_bool( fingerprint.has_selected_candidate );
		fp.mix_u64( fingerprint.selected_candidate_digest );
		fp.mix_bool( fingerprint.has_deep_snapshot );
		fp.mix_u64( fingerprint.deep_resume_fingerprint_hash );
		fingerprint.hash = fp.finish();
		return fingerprint;
	}

	static void write_linear_auto_pipeline_fingerprint_fields(
		std::ostream& out,
		const LinearAutoPipelineFingerprint& fingerprint,
		const char* prefix = "auto_pipeline_fingerprint_" )
	{
		const char* key_prefix = ( prefix != nullptr ) ? prefix : "auto_pipeline_fingerprint_";
		out << key_prefix << "hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";
		out << key_prefix << "completed_jobs=" << fingerprint.completed_jobs << "\n";
		out << key_prefix << "completed_flags_digest=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.completed_flags_digest ) << "\n";
		out << key_prefix << "top_candidate_count=" << fingerprint.top_candidate_count << "\n";
		out << key_prefix << "top_candidates_digest=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.top_candidates_digest ) << "\n";
		out << key_prefix << "has_selected_candidate=" << ( fingerprint.has_selected_candidate ? 1 : 0 ) << "\n";
		out << key_prefix << "selected_candidate_digest=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.selected_candidate_digest ) << "\n";
		out << key_prefix << "has_deep_snapshot=" << ( fingerprint.has_deep_snapshot ? 1 : 0 ) << "\n";
		out << key_prefix << "deep_resume_fingerprint_hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.deep_resume_fingerprint_hash ) << "\n";
	}

	template <class WriteFieldsFn>
	static void write_linear_auto_history_if_open( BestWeightHistory& history_log, const char* event_name, WriteFieldsFn&& write_fields )
	{
		if ( !history_log.out )
			return;
		history_log.out << "=== auto_pipeline ===\n";
		history_log.out << "timestamp_local=" << format_local_time_now() << "\n";
		history_log.out << "event=" << ( event_name ? event_name : "unknown" ) << "\n";
		write_fields( history_log.out );
		history_log.out << "\n";
		history_log.out.flush();
	}

	static void write_linear_auto_job( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearAutoBreadthJob& job )
	{
		w.write_u32( job.mask_a );
		w.write_u32( job.mask_b );
	}

	static bool read_linear_auto_job( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearAutoBreadthJob& job )
	{
		return r.read_u32( job.mask_a ) && r.read_u32( job.mask_b );
	}

	static void write_linear_auto_candidate( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearAutoCandidate& candidate )
	{
		w.write_u32( candidate.start_mask_a );
		w.write_u32( candidate.start_mask_b );
		w.write_u8( candidate.found ? 1u : 0u );
		w.write_i32( candidate.best_weight );
		w.write_u64( candidate.nodes );
		w.write_u8( candidate.hit_maximum_search_nodes_limit ? 1u : 0u );
		w.write_u32( candidate.best_input_mask_a );
		w.write_u32( candidate.best_input_mask_b );
		write_trail_vector( w, candidate.trail );
	}

	static bool read_linear_auto_candidate( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearAutoCandidate& candidate )
	{
		std::uint8_t found = 0;
		std::uint8_t hit_node_limit = 0;
		if ( !r.read_u32( candidate.start_mask_a ) ) return false;
		if ( !r.read_u32( candidate.start_mask_b ) ) return false;
		if ( !r.read_u8( found ) ) return false;
		if ( !r.read_i32( candidate.best_weight ) ) return false;
		if ( !r.read_u64( candidate.nodes ) ) return false;
		if ( !r.read_u8( hit_node_limit ) ) return false;
		if ( !r.read_u32( candidate.best_input_mask_a ) ) return false;
		if ( !r.read_u32( candidate.best_input_mask_b ) ) return false;
		if ( !read_trail_vector( r, candidate.trail ) ) return false;
		candidate.found = ( found != 0 );
		candidate.hit_maximum_search_nodes_limit = ( hit_node_limit != 0 );
		return true;
	}

	static bool write_linear_auto_pipeline_checkpoint_payload(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearAutoPipelineState& state,
		const LinearBestSearchContext* deep_context = nullptr,
		const LinearSearchCursor* deep_cursor = nullptr )
	{
		if ( !TwilightDream::auto_pipeline_shared::write_payload_header(
				 w,
				 state,
				 state.start_output_mask_branch_a,
				 state.start_output_mask_branch_b,
				 static_cast<std::uint8_t>( state.auto_breadth_strategy ),
				 static_cast<std::uint8_t>( state.stage ),
				 [ & ] { write_config( w, state.search_configuration ); } ) )
			return false;

		if ( !TwilightDream::auto_pipeline_shared::write_counted_container( w, state.jobs, [ & ]( const LinearAutoBreadthJob& job ) {
				 write_linear_auto_job( w, job );
			 } ) )
			return false;
		if ( !TwilightDream::auto_pipeline_shared::write_counted_container( w, state.completed_job_flags, [ & ]( std::uint8_t flag ) {
				 w.write_u8( flag );
			 } ) )
			return false;
		w.write_u64( state.breadth_total_nodes_visited );

		if ( !TwilightDream::auto_pipeline_shared::write_counted_container( w, state.top_candidates, [ & ]( const LinearAutoCandidate& candidate ) {
				 write_linear_auto_candidate( w, candidate );
			 } ) )
			return false;

		if ( !TwilightDream::auto_pipeline_shared::write_optional_section( w, state.has_selected_candidate, [ & ]() {
				 write_linear_auto_candidate( w, state.selected_candidate );
				 return w.ok();
			 } ) )
			return false;

		const bool write_live_deep_snapshot = ( deep_context != nullptr && deep_cursor != nullptr );
		return TwilightDream::auto_pipeline_shared::write_optional_section( w, write_live_deep_snapshot || state.has_deep_snapshot, [ & ]() {
			if ( write_live_deep_snapshot )
			{
				return write_linear_checkpoint_payload( w, *deep_context, *deep_cursor, TwilightDream::best_search_shared_core::accumulated_elapsed_microseconds( *deep_context ) );
			}
			LinearBestSearchContext snapshot_context {};
			snapshot_context.configuration = state.deep_snapshot.configuration;
			snapshot_context.start_output_branch_a_mask = state.deep_snapshot.start_mask_a;
			snapshot_context.start_output_branch_b_mask = state.deep_snapshot.start_mask_b;
			snapshot_context.history_log_output_path = state.deep_snapshot.history_log_path;
			snapshot_context.runtime_log_output_path = state.deep_snapshot.runtime_log_path;
			snapshot_context.accumulated_elapsed_usec = state.deep_snapshot.accumulated_elapsed_usec;
			snapshot_context.visited_node_count = state.deep_snapshot.total_nodes_visited;
			snapshot_context.run_visited_node_count = state.deep_snapshot.run_nodes_visited;
			snapshot_context.best_weight = state.deep_snapshot.best_weight;
			snapshot_context.best_input_branch_a_mask = state.deep_snapshot.best_input_mask_a;
			snapshot_context.best_input_branch_b_mask = state.deep_snapshot.best_input_mask_b;
			snapshot_context.best_linear_trail = state.deep_snapshot.best_trail;
			snapshot_context.current_linear_trail = state.deep_snapshot.current_trail;
			snapshot_context.runtime_controls.maximum_search_nodes = state.deep_snapshot.runtime_maximum_search_nodes;
			snapshot_context.runtime_controls.maximum_search_seconds = state.deep_snapshot.runtime_maximum_search_seconds;
			snapshot_context.runtime_controls.progress_every_seconds = state.deep_snapshot.runtime_progress_every_seconds;
			snapshot_context.runtime_controls.checkpoint_every_seconds = state.deep_snapshot.runtime_checkpoint_every_seconds;
			snapshot_context.runtime_state.total_nodes_visited = state.deep_snapshot.total_nodes_visited;
			snapshot_context.runtime_state.run_nodes_visited = state.deep_snapshot.run_nodes_visited;
			snapshot_context.runtime_state.progress_node_mask = state.deep_snapshot.runtime_progress_node_mask;
			snapshot_context.runtime_state.stop_due_to_node_limit = state.deep_snapshot.last_run_hit_node_limit;
			snapshot_context.runtime_state.stop_due_to_time_limit = state.deep_snapshot.last_run_hit_time_limit;
			return write_linear_checkpoint_payload( w, snapshot_context, state.deep_snapshot.cursor, state.deep_snapshot.accumulated_elapsed_usec );
		} );
	}

	static bool write_linear_auto_pipeline_checkpoint(
		const std::string& path,
		const LinearAutoPipelineState& state,
		const LinearBestSearchContext* deep_context = nullptr,
		const LinearSearchCursor* deep_cursor = nullptr )
	{
		return TwilightDream::auto_pipeline_shared::write_checkpoint_file(
			path,
			TwilightDream::auto_search_checkpoint::SearchKind::LinearAutoPipeline,
			[ & ]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				return write_linear_auto_pipeline_checkpoint_payload( w, state, deep_context, deep_cursor );
			} );
	}

	static bool read_linear_auto_pipeline_checkpoint( const std::string& path, LinearAutoPipelineState& state )
	{
		return TwilightDream::auto_pipeline_shared::read_checkpoint_file(
			path,
			TwilightDream::auto_search_checkpoint::SearchKind::LinearAutoPipeline,
			[ & ]( TwilightDream::auto_search_checkpoint::BinaryReader& r ) {
				TwilightDream::auto_pipeline_shared::AutoPipelinePayloadHeader header {};
				if ( !TwilightDream::auto_pipeline_shared::read_payload_header(
						 r,
						 state,
						 header,
						 [ & ]() { return read_config( r, state.search_configuration ); } ) )
					return false;
				if ( header.breadth_strategy != static_cast<std::uint8_t>( CommandLineOptions::AutoBreadthStrategy::FrontierBfs ) ) return false;
				state.start_output_mask_branch_a = header.start_word_a;
				state.start_output_mask_branch_b = header.start_word_b;
				state.auto_breadth_strategy = CommandLineOptions::AutoBreadthStrategy::FrontierBfs;
				state.stage = static_cast<LinearAutoPipelineStage>( header.stage );

				if ( !TwilightDream::auto_pipeline_shared::read_counted_container( r, state.jobs, [ & ]( LinearAutoBreadthJob& job ) {
						 return read_linear_auto_job( r, job );
					 } ) )
					return false;
				if ( !TwilightDream::auto_pipeline_shared::read_counted_container( r, state.completed_job_flags, [ & ]( std::uint8_t& flag ) {
						 return r.read_u8( flag );
					 } ) )
					return false;
				if ( !r.read_u64( state.breadth_total_nodes_visited ) ) return false;
				if ( !TwilightDream::auto_pipeline_shared::read_counted_container( r, state.top_candidates, [ & ]( LinearAutoCandidate& candidate ) {
						 return read_linear_auto_candidate( r, candidate );
					 } ) )
					return false;
				if ( !TwilightDream::auto_pipeline_shared::read_optional_section( r, state.has_selected_candidate, [ & ]() {
						 return read_linear_auto_candidate( r, state.selected_candidate );
					 } ) )
					return false;
				return TwilightDream::auto_pipeline_shared::read_optional_section( r, state.has_deep_snapshot, [ & ]() {
					return read_linear_checkpoint_payload( r, state.deep_snapshot, state.deep_memoization );
				} );
			} );
	}

	struct LinearAutoPipelineCheckpointSession : TwilightDream::auto_pipeline_shared::AutoPipelineCheckpointSessionCore
	{
		LinearAutoPipelineState*					   state = nullptr;
		std::mutex*									   state_mutex = nullptr;
		RuntimeEventLog*							   runtime_log = nullptr;

		bool enabled() const { return enabled_path() && state != nullptr; }

		void note_runtime_controls( const LinearBestSearchRuntimeControls& runtime_controls, std::uint64_t elapsed_usec )
		{
			if ( state == nullptr )
				return;
			TwilightDream::auto_pipeline_shared::note_runtime_controls( *state, runtime_controls, elapsed_usec );
		}

		void emit_checkpoint_event(
			const char* checkpoint_reason,
			const std::string& checkpoint_path,
			bool ok,
			std::uint64_t elapsed_usec,
			const LinearBestSearchContext* deep_context = nullptr,
			const LinearSearchCursor* deep_cursor = nullptr )
		{
			if ( runtime_log == nullptr || state == nullptr )
				return;
			const LinearAutoPipelineFingerprint fingerprint = compute_linear_auto_pipeline_fingerprint( *state, deep_context, deep_cursor );
			write_runtime_event_if_open( *runtime_log, "auto_checkpoint_write", [ & ]( std::ostream& out ) {
				out << "round_count=" << state->round_count << "\n";
				out << "stage=" << ( state->stage == LinearAutoPipelineStage::Breadth ? "breadth" : "deep" ) << "\n";
				out << "checkpoint_reason=" << ( checkpoint_reason ? checkpoint_reason : "stage_snapshot" ) << "\n";
				out << "checkpoint_path=" << checkpoint_path << "\n";
				out << "binary_checkpoint_write_result=" << ( ok ? "success" : "failure" ) << "\n";
				out << "runtime_checkpoint_every_seconds=" << state->last_runtime_checkpoint_every_seconds << "\n";
				out << "stage_elapsed_usec=" << elapsed_usec << "\n";
				out << "breadth_total_nodes_visited=" << state->breadth_total_nodes_visited << "\n";
				write_linear_auto_pipeline_fingerprint_fields( out, fingerprint );
			} );
		}

		bool write_stage_snapshot_to_path(
			const std::string& checkpoint_path,
			LinearAutoPipelineStage stage,
			const LinearBestSearchRuntimeControls& runtime_controls,
			std::uint64_t elapsed_usec,
			const char* checkpoint_reason = "stage_snapshot" )
		{
			return TwilightDream::auto_pipeline_shared::write_stage_snapshot_to_path(
				*this,
				checkpoint_path,
				stage,
				runtime_controls,
				elapsed_usec,
				checkpoint_reason,
				[ & ]( const LinearBestSearchRuntimeControls& controls, std::uint64_t usec ) {
					note_runtime_controls( controls, usec );
				},
				[ & ]( const std::string& path_to_write ) {
					return write_linear_auto_pipeline_checkpoint( path_to_write, *state );
				},
				[ & ]( const char* reason_to_emit, const std::string& path_to_emit, bool ok, std::uint64_t usec ) {
					emit_checkpoint_event( reason_to_emit, path_to_emit, ok, usec );
				} );
		}

		bool write_stage_snapshot(
			LinearAutoPipelineStage stage,
			const LinearBestSearchRuntimeControls& runtime_controls,
			std::uint64_t elapsed_usec,
			const char* checkpoint_reason = "stage_snapshot" )
		{
			return write_stage_snapshot_to_path( path, stage, runtime_controls, elapsed_usec, checkpoint_reason );
		}

		bool write_archive_stage_snapshot(
			LinearAutoPipelineStage stage,
			const LinearBestSearchRuntimeControls& runtime_controls,
			std::uint64_t elapsed_usec,
			const char* checkpoint_reason = "periodic_timer" )
		{
			return TwilightDream::auto_pipeline_shared::write_archive_stage_snapshot(
				*this,
				stage,
				runtime_controls,
				elapsed_usec,
				checkpoint_reason,
				[ & ]( const std::string& archive_path, LinearAutoPipelineStage archive_stage, const LinearBestSearchRuntimeControls& controls, std::uint64_t usec, const char* reason_to_emit ) {
					return write_stage_snapshot_to_path( archive_path, archive_stage, controls, usec, reason_to_emit );
				} );
		}

		bool maybe_write_breadth( const LinearBestSearchRuntimeControls& runtime_controls, const char* reason = nullptr )
		{
			return TwilightDream::auto_pipeline_shared::maybe_write_breadth(
				*this,
				runtime_controls,
				reason,
				[ & ]( const char* checkpoint_reason, std::uint64_t elapsed_usec ) {
					return write_stage_snapshot( LinearAutoPipelineStage::Breadth, runtime_controls, elapsed_usec, checkpoint_reason );
				},
				[ & ]( const char* checkpoint_reason, std::uint64_t elapsed_usec ) {
					return write_archive_stage_snapshot( LinearAutoPipelineStage::Breadth, runtime_controls, elapsed_usec, checkpoint_reason );
				} );
		}

		bool write_deep_snapshot( BinaryCheckpointManager& manager, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason )
		{
			return TwilightDream::auto_pipeline_shared::write_deep_snapshot(
				*this,
				manager,
				context,
				cursor,
				LinearAutoPipelineStage::Deep,
				reason,
				[ & ]( const LinearBestSearchRuntimeControls& controls, std::uint64_t usec ) {
					note_runtime_controls( controls, usec );
				},
				[ & ]( const std::string& path_to_write ) {
					return write_linear_auto_pipeline_checkpoint( path_to_write, *state, &context, &cursor );
				},
				[ & ]( const char* reason_to_emit, const std::string& path_to_emit, bool ok, std::uint64_t usec, const LinearBestSearchContext&, const LinearSearchCursor& ) {
					emit_checkpoint_event( reason_to_emit, path_to_emit, ok, usec, &context, &cursor );
				} );
		}
	};

	static LinearAutoPipelineCheckpointSession* g_linear_pressure_auto_pipeline_checkpoint = nullptr;

	static void linear_auto_pressure_checkpoint_callback()
	{
		TwilightDream::auto_pipeline_shared::trigger_auto_pipeline_pressure_checkpoint(
			g_pressure_binary_checkpoint,
			g_linear_pressure_auto_pipeline_checkpoint );
	}

	static bool linear_auto_pipeline_deep_checkpoint_override( BinaryCheckpointManager& manager, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason )
	{
		return TwilightDream::auto_pipeline_shared::deep_checkpoint_override<LinearAutoPipelineCheckpointSession>( manager, context, cursor, reason );
	}

	static std::uint64_t linear_auto_breadth_effective_rng_seed( const CommandLineOptions& o ) noexcept
	{
		if ( o.auto_breadth_seed_was_provided )
			return o.auto_breadth_seed;
		std::uint64_t x = ( std::uint64_t( o.output_branch_a_mask ) << 32 ) | std::uint64_t( o.output_branch_b_mask );
		x ^= 0xCAFEBABECAFEBABEULL;
		x ^= x >> 33;
		x *= 0xff51afd7ed558ccdULL;
		x ^= x >> 33;
		x *= 0xc4ceb9fe1a85ec53ULL;
		x ^= x >> 33;
		return x;
	}

	static std::vector<LinearAutoBreadthJob> build_linear_auto_breadth_jobs( const CommandLineOptions& command_line_options )
	{
		std::vector<LinearAutoBreadthJob> jobs;

		std::unordered_set<std::uint64_t> seen;
		auto make_key = []( std::uint32_t a, std::uint32_t b ) -> std::uint64_t {
			return ( std::uint64_t( a ) << 32 ) ^ std::uint64_t( b );
		};
		auto try_add_job = [ & ]( std::uint32_t a, std::uint32_t b ) {
			if ( a == 0u && b == 0u )
				return false;
			const std::uint64_t key = make_key( a, b );
			if ( !seen.insert( key ).second )
				return false;
			if ( jobs.size() >= command_line_options.auto_breadth_candidate_count )
				return false;
			if ( TwilightDream::runtime_component::physical_memory_allocation_guard_active() )
				return false;
			jobs.push_back( LinearAutoBreadthJob { a, b } );
			return true;
		};

		const std::uint32_t base_mask_a = command_line_options.output_branch_a_mask;
		const std::uint32_t base_mask_b = command_line_options.output_branch_b_mask;
		try_add_job( base_mask_a, base_mask_b );

		struct FrontierNode
		{
			std::uint32_t mask_a = 0;
			std::uint32_t mask_b = 0;
			int			  hop = 0;
		};

		std::vector<FrontierNode> frontier;
		frontier.push_back( FrontierNode { base_mask_a, base_mask_b, 0 } );

		for ( int hop = 0; hop < command_line_options.auto_breadth_max_hops && jobs.size() < command_line_options.auto_breadth_candidate_count && !frontier.empty(); ++hop )
		{
			std::vector<FrontierNode> next_frontier;
			const std::size_t expand_count = std::min<std::size_t>( frontier.size(), command_line_options.auto_breadth_frontier_width );
			for ( std::size_t i = 0; i < expand_count && jobs.size() < command_line_options.auto_breadth_candidate_count; ++i )
			{
				const FrontierNode node = frontier[ i ];
				for ( int bit = 0; bit < 32 && jobs.size() < command_line_options.auto_breadth_candidate_count; ++bit )
				{
					const std::uint32_t m = ( 1u << bit );
					if ( try_add_job( node.mask_a ^ m, node.mask_b ) &&
						 next_frontier.size() < command_line_options.auto_breadth_frontier_width &&
						 !TwilightDream::runtime_component::physical_memory_allocation_guard_active() )
						next_frontier.push_back( FrontierNode { node.mask_a ^ m, node.mask_b, hop + 1 } );
					if ( try_add_job( node.mask_a, node.mask_b ^ m ) &&
						 next_frontier.size() < command_line_options.auto_breadth_frontier_width &&
						 !TwilightDream::runtime_component::physical_memory_allocation_guard_active() )
						next_frontier.push_back( FrontierNode { node.mask_a, node.mask_b ^ m, hop + 1 } );
					if ( try_add_job( node.mask_a ^ m, node.mask_b ^ m ) &&
						 next_frontier.size() < command_line_options.auto_breadth_frontier_width &&
						 !TwilightDream::runtime_component::physical_memory_allocation_guard_active() )
						next_frontier.push_back( FrontierNode { node.mask_a ^ m, node.mask_b ^ m, hop + 1 } );
				}
			}
			frontier.swap( next_frontier );
		}

		if ( command_line_options.auto_breadth_max_random_bitflips > 0 &&
			 jobs.size() < command_line_options.auto_breadth_candidate_count )
		{
			const int						  f_cap = command_line_options.auto_breadth_max_random_bitflips;
			const std::uint64_t			  seed = linear_auto_breadth_effective_rng_seed( command_line_options );
			std::mt19937_64					  rng( seed );
			std::uniform_int_distribution<int> dist_t( 1, f_cap );
			std::array<int, 64>			  slots {};
			const std::size_t need = command_line_options.auto_breadth_candidate_count - jobs.size();
			const std::size_t max_attempts = std::max<std::size_t>( need * 128u, 4096u );
			for ( std::size_t attempt = 0; attempt < max_attempts && jobs.size() < command_line_options.auto_breadth_candidate_count; ++attempt )
			{
				if ( TwilightDream::runtime_component::physical_memory_allocation_guard_active() )
					break;
				const int t = dist_t( rng );
				for ( int i = 0; i < 64; ++i )
					slots[ static_cast<std::size_t>( i ) ] = i;
				for ( int i = 0; i < t; ++i )
				{
					const int j = i + static_cast<int>( rng() % std::uint64_t( 64 - i ) );
					std::swap( slots[ static_cast<std::size_t>( i ) ], slots[ static_cast<std::size_t>( j ) ] );
				}
				std::uint32_t xor_a = 0;
				std::uint32_t xor_b = 0;
				for ( int i = 0; i < t; ++i )
				{
					const int s = slots[ static_cast<std::size_t>( i ) ];
					if ( s < 32 )
						xor_a ^= ( 1u << s );
					else
						xor_b ^= ( 1u << ( s - 32 ) );
				}
				try_add_job( base_mask_a ^ xor_a, base_mask_b ^ xor_b );
			}
		}

		return jobs;
	}

	static bool linear_auto_pipeline_matches_resume_request( const CommandLineOptions& command_line_options, const LinearAutoPipelineState& state, std::string& error_message )
	{
		if ( command_line_options.round_count_was_provided && command_line_options.round_count != state.round_count )
		{
			error_message = "--round-count does not match auto pipeline checkpoint";
			return false;
		}
		if ( command_line_options.output_masks_were_provided &&
			 ( command_line_options.output_branch_a_mask != state.start_output_mask_branch_a ||
			   command_line_options.output_branch_b_mask != state.start_output_mask_branch_b ) )
		{
			error_message = "provided output masks do not match auto pipeline checkpoint";
			return false;
		}
		if ( command_line_options.search_configuration_was_provided )
		{
			LinearBestSearchConfiguration cli_config = command_line_options.search_configuration;
			cli_config.round_count =
				command_line_options.round_count_was_provided ?
					command_line_options.round_count :
					state.search_configuration.round_count;
			if ( !configs_compatible_for_resume( cli_config, state.search_configuration ) )
			{
				error_message = "search configuration does not match auto pipeline checkpoint";
				return false;
			}
		}
		return true;
	}

	static int run_auto_mode( const CommandLineOptions& command_line_options )
	{
		// Auto mode is single-run only (no batch).
		if ( command_line_options.resume_was_provided )
			return run_resume_mode( command_line_options );
		if ( command_line_options.batch_job_count > 0 || command_line_options.batch_job_file_was_provided )
		{
			std::cerr << "[Auto] ERROR: auto mode does not support batch mode.\n";
			return 1;
		}
		if ( !command_line_options.output_masks_were_provided )
		{
			std::cerr << "[Auto] ERROR: auto mode requires explicit output masks.\n";
			return 1;
		}
		if ( command_line_options.output_branch_a_mask == 0u && command_line_options.output_branch_b_mask == 0u )
		{
			std::cerr << "[Auto] ERROR: output masks are both zero; this is trivial.\n";
			return 1;
		}

		std::cout << "[Auto] round_count=" << command_line_options.round_count << "\n";
		print_word32_hex( "[Auto] start_output_branch_a_mask=", command_line_options.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "[Auto] start_output_branch_b_mask=", command_line_options.output_branch_b_mask );
		std::cout << "\n\n";

		LinearAutoPipelineState auto_pipeline_state {};
		auto_pipeline_state.search_configuration = command_line_options.search_configuration;
		auto_pipeline_state.round_count = command_line_options.round_count;
		auto_pipeline_state.start_output_mask_branch_a = command_line_options.output_branch_a_mask;
		auto_pipeline_state.start_output_mask_branch_b = command_line_options.output_branch_b_mask;
		auto_pipeline_state.auto_breadth_strategy = command_line_options.auto_breadth_strategy;
		auto_pipeline_state.auto_breadth_top_k = std::max<std::size_t>( 1, command_line_options.auto_breadth_top_k );
		auto_pipeline_state.stage = LinearAutoPipelineStage::Breadth;
		const auto log_artifact_paths =
			TwilightDream::best_search_shared_core::resolve_log_artifact_paths(
				command_line_options.runtime_log_path,
				std::string {},
				std::string {},
				[&]() {
					return BestWeightHistory::default_path(
						command_line_options.round_count,
						command_line_options.output_branch_a_mask,
						command_line_options.output_branch_b_mask );
				},
				[&]() {
					return default_runtime_log_path(
						command_line_options.round_count,
						command_line_options.output_branch_a_mask,
						command_line_options.output_branch_b_mask );
				} );
		auto_pipeline_state.history_log_path = log_artifact_paths.history_log_path;
		auto_pipeline_state.runtime_log_path = log_artifact_paths.runtime_log_path;

		BestWeightHistory history_log {};
		const bool history_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			history_log,
			auto_pipeline_state.history_log_path,
			"[Auto]",
			"history_log_output_path",
			"history log" );
		if ( history_log_ok )
		{
			write_linear_auto_history_if_open( history_log, "start", [ & ]( std::ostream& out ) {
				out << "round_count=" << command_line_options.round_count << "\n";
				out << "start_output_mask_branch_a=" << hex8( command_line_options.output_branch_a_mask ) << "\n";
				out << "start_output_mask_branch_b=" << hex8( command_line_options.output_branch_b_mask ) << "\n";
				out << "stage=breadth\n";
			} );
		}
		RuntimeEventLog runtime_log {};
		const bool runtime_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			runtime_log,
			auto_pipeline_state.runtime_log_path,
			"[Auto]",
			"runtime_log_output_path",
			"runtime log",
			true,
			"\n\n",
			"\n\n" );
		if ( runtime_log_ok )
		{
			write_runtime_event_if_open( runtime_log, "auto_start", [ & ]( std::ostream& out ) {
				out << "round_count=" << command_line_options.round_count << "\n";
				out << "start_output_mask_branch_a=" << hex8( command_line_options.output_branch_a_mask ) << "\n";
				out << "start_output_mask_branch_b=" << hex8( command_line_options.output_branch_b_mask ) << "\n";
				out << "stage=breadth\n";
				write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( auto_pipeline_state ) );
			} );
		}
		const unsigned hardware_thread_concurrency = std::thread::hardware_concurrency();
		const int	   breadth_threads = ( command_line_options.auto_breadth_thread_count > 0 ) ? command_line_options.auto_breadth_thread_count : int( ( hardware_thread_concurrency == 0 ) ? 1 : hardware_thread_concurrency );

		// Configure PMR for this run.
		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		const std::uint64_t	   auto_headroom_bytes = compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		const MemoryGateEvaluation memory_gate = evaluate_linear_memory_gate( command_line_options, mem, true, false, false );
		const bool strict_search_mode =
			( command_line_options.search_mode_was_provided ? command_line_options.search_configuration.search_mode : infer_search_mode_from_caps( command_line_options.search_configuration ) ) ==
			SearchMode::Strict;
		if ( !print_and_enforce_memory_gate( "[Auto][MemoryGate] ", memory_gate, command_line_options.allow_high_memory_usage, strict_search_mode ) )
			return 1;
		const SearchInvocationMetadata invocation_metadata = build_invocation_metadata( memory_gate, strict_search_mode );
		pmr_configure_for_run( avail_bytes, auto_headroom_bytes );
		memory_governor_enable_for_run( auto_headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );
		auto cleanup = [ & ]() {
			memory_governor_disable_for_run();
		};

		MemoryBallast memory_ballast( auto_headroom_bytes );
		if ( command_line_options.memory_ballast_enabled && auto_headroom_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[Auto] memory_ballast=on  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( auto_headroom_bytes ) << "\n";
			memory_ballast.start();
		}

		std::mutex auto_pipeline_mutex {};
		LinearAutoPipelineCheckpointSession auto_pipeline_checkpoint {};
		auto_pipeline_checkpoint.state = &auto_pipeline_state;
		auto_pipeline_checkpoint.state_mutex = &auto_pipeline_mutex;
		auto_pipeline_checkpoint.every_seconds = command_line_options.runtime_controls.checkpoint_every_seconds;
		auto_pipeline_checkpoint.runtime_log = runtime_log_ok ? &runtime_log : nullptr;
		if ( command_line_options.checkpoint_out_was_provided && command_line_options.checkpoint_out_path.empty() )
		{
			std::cerr << "[Auto] ERROR: --checkpoint-out requires a non-empty path.\n";
			cleanup();
			return 1;
		}
		if ( command_line_options.checkpoint_out_was_provided || command_line_options.checkpoint_every_seconds_was_provided )
		{
			auto_pipeline_checkpoint.path =
				command_line_options.checkpoint_out_was_provided ?
					command_line_options.checkpoint_out_path :
					default_binary_checkpoint_path(
						command_line_options.round_count,
						command_line_options.output_branch_a_mask,
						command_line_options.output_branch_b_mask );
			std::cout << "[Auto] pipeline_checkpoint_output_path=" << auto_pipeline_checkpoint.path << "\n";
			std::cout << "[Auto] pipeline_checkpoint_interval_seconds=" << auto_pipeline_checkpoint.every_seconds << "\n\n";
		}

		auto auto_pipeline_pressure_guard =
			TwilightDream::auto_pipeline_shared::ScopedValueRestore<LinearAutoPipelineCheckpointSession*>(
				g_linear_pressure_auto_pipeline_checkpoint,
				auto_pipeline_checkpoint.enabled() ? &auto_pipeline_checkpoint : nullptr );
		RebuildableReserveGuard rebuildable_reserve( command_line_options.rebuildable_reserve_mib );

		// ---------------------------------------------------------------------
		// Stage 1: "breadth" scan (small budget, many candidates)
		// ---------------------------------------------------------------------
		std::vector<LinearAutoBreadthJob> jobs = build_linear_auto_breadth_jobs( command_line_options );
		auto_pipeline_state.auto_breadth_seed = linear_auto_breadth_effective_rng_seed( command_line_options );
		auto_pipeline_state.auto_breadth_seed_was_provided = command_line_options.auto_breadth_seed_was_provided;
		auto_pipeline_state.jobs = jobs;
		auto_pipeline_state.completed_job_flags.assign( jobs.size(), 0u );

		if ( command_line_options.auto_print_breadth_candidates )
		{
			std::cout << "[Auto][Breadth] candidates:\n";
			for ( const auto& j : jobs )
			{
				print_word32_hex( "  mask_a=", j.mask_a );
				std::cout << "  ";
				print_word32_hex( "mask_b=", j.mask_b );
				std::cout << "\n";
			}
			std::cout << "\n";
		}

		LinearBestSearchConfiguration breadth_configuration = command_line_options.search_configuration;
		LinearBestSearchRuntimeControls breadth_runtime_controls = command_line_options.runtime_controls;
		breadth_configuration.search_mode = SearchMode::Strict;
		breadth_configuration.round_count = command_line_options.round_count;
		breadth_runtime_controls.maximum_search_nodes = std::max<std::uint64_t>( 1, command_line_options.auto_breadth_maximum_search_nodes );
		breadth_runtime_controls.maximum_search_seconds = command_line_options.runtime_controls.maximum_search_seconds;
		// Auto breadth policy: keep the legacy fast path for candidate generation.
		breadth_configuration.enable_weight_sliced_clat = false;
		breadth_configuration.weight_sliced_clat_max_candidates = 0;
		apply_search_mode_overrides( breadth_configuration );
		breadth_configuration.enable_verbose_output = false;

		std::cout << "[Auto][Breadth] jobs=" << jobs.size() << "  threads=" << breadth_threads << "  strategy=" << to_string( command_line_options.auto_breadth_strategy );
		std::cout << "  frontier_width=" << command_line_options.auto_breadth_frontier_width << "  max_hops=" << command_line_options.auto_breadth_max_hops;
		std::cout << "  max_random_bitflips=" << command_line_options.auto_breadth_max_random_bitflips;
		if ( command_line_options.auto_breadth_max_random_bitflips > 0 )
		{
			std::cout << "  breadth_rng_seed=0x" << std::hex << std::uppercase << linear_auto_breadth_effective_rng_seed( command_line_options ) << std::dec;
			std::cout << ( command_line_options.auto_breadth_seed_was_provided ? " (explicit --auto-breadth-seed)" : " (derived from start masks)" );
		}
		std::cout << "\n";
		std::cout << "  per_candidate: runtime_maximum_search_nodes=" << breadth_runtime_controls.maximum_search_nodes << "  maximum_round_predecessors=" << breadth_configuration.maximum_round_predecessors << "  maximum_injection_input_masks=" << breadth_configuration.maximum_injection_input_masks << "  state_memoization=" << ( breadth_configuration.enable_state_memoization ? "on" : "off" )
				  << "  weight_sliced_clat=" << ( breadth_configuration.enable_weight_sliced_clat ? "on" : "off" )
				  << "  weight_sliced_clat_max_candidates=" << breadth_configuration.weight_sliced_clat_max_candidates << "\n";
		if ( mem.available_physical_bytes != 0 )
		{
			print_system_memory_status_line( std::cout, mem, "  system_memory: " );
		}
		std::cout << "\n";

		const std::uint64_t breadth_progress_sec = ( command_line_options.runtime_controls.progress_every_seconds == 0 ) ? 1 : command_line_options.runtime_controls.progress_every_seconds;
		breadth_runtime_controls.progress_every_seconds = breadth_progress_sec;

		std::atomic<std::size_t>   next_index { 0 };
		std::atomic<std::size_t>   completed { 0 };
		std::atomic<std::uint64_t> total_nodes { 0 };

		const int breadth_threads_clamped = std::max( 1, breadth_threads );
		// Track active job id per worker thread so auto mode can print what's being worked on.
		// (0 means idle; job ids are 1-based.)
		std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( breadth_threads_clamped ) );
		for ( auto& x : active_job_id_by_thread )
			x.store( 0, std::memory_order_relaxed );

		std::vector<LinearAutoCandidate> top_candidates;
		top_candidates.reserve( std::min<std::size_t>( std::max<std::size_t>( 1, command_line_options.auto_breadth_top_k ), 8u ) );
		auto_pipeline_checkpoint.begin_breadth_stage();
		if ( auto_pipeline_checkpoint.enabled() )
			auto_pipeline_checkpoint.write_stage_snapshot( LinearAutoPipelineStage::Breadth, breadth_runtime_controls, 0, "stage_start" );
		PressureCallbackGuard breadth_pressure_guard( nullptr, auto_pipeline_checkpoint.enabled() ? &linear_auto_pressure_checkpoint_callback : nullptr );

		auto worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
				if ( job_index >= jobs.size() )
					break;

				const LinearAutoBreadthJob job = jobs[ job_index ];
				const std::size_t job_id_one_based = job_index + 1;
				active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_id_one_based, std::memory_order_relaxed );

				BestSearchResult result {};
				try
				{
					result = run_linear_best_search( job.mask_a, job.mask_b, breadth_configuration, breadth_runtime_controls, false, false, INFINITE_WEIGHT, nullptr, nullptr, nullptr, nullptr, &invocation_metadata );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "auto.breadth.run" );
					result = BestSearchResult {};
				}
				total_nodes.fetch_add( result.nodes_visited, std::memory_order_relaxed );

				LinearAutoCandidate c {};
				c.start_mask_a = job.mask_a;
				c.start_mask_b = job.mask_b;
				c.nodes = result.nodes_visited;
				c.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
				c.found = result.found;
				if ( c.found )
				{
					c.best_weight = result.best_weight;
					c.best_input_mask_a = result.best_input_branch_a_mask;
					c.best_input_mask_b = result.best_input_branch_b_mask;
					c.trail = std::move( result.best_linear_trail );
				}
				{
					std::scoped_lock lk( auto_pipeline_mutex );
					auto_pipeline_state.completed_job_flags[ job_index ] = 1u;
					auto_pipeline_state.breadth_total_nodes_visited += result.nodes_visited;
					linear_try_update_auto_top_candidates( top_candidates, auto_pipeline_state.auto_breadth_top_k, std::move( c ) );
					auto_pipeline_state.top_candidates = top_candidates;
				}
				completed.fetch_add( 1, std::memory_order_relaxed );
				if ( auto_pipeline_checkpoint.enabled() )
					auto_pipeline_checkpoint.maybe_write_breadth( breadth_runtime_controls, "breadth_candidate_completed" );
			}
			active_job_id_by_thread[ std::size_t( thread_id ) ].store( 0, std::memory_order_relaxed );
		};

		auto progress_monitor = [ & ]() {
			const std::size_t total = jobs.size();
			if ( total == 0 )
				return;
			if ( breadth_progress_sec == 0 )
				return;
			std::size_t last_done = 0;
			auto		start_time = std::chrono::steady_clock::now();
			auto		last_time = start_time;
			for ( ;; )
			{
				const std::size_t done = completed.load( std::memory_order_relaxed );
				const auto		  now = std::chrono::steady_clock::now();
				memory_governor_poll_if_needed( now );
				if ( auto_pipeline_checkpoint.enabled() )
					auto_pipeline_checkpoint.maybe_write_breadth( breadth_runtime_controls );
				const double since_last = std::chrono::duration<double>( now - last_time ).count();
				if ( since_last >= double( breadth_progress_sec ) || done == total )
				{
					const double	  elapsed = std::chrono::duration<double>( now - start_time ).count();
					const double	  window = std::max( 1e-9, since_last );
					const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
					const double	  rate = double( delta ) / window;

					LinearAutoCandidate best_snapshot {};
					bool		  has_best = false;
					{
						std::scoped_lock lk( auto_pipeline_mutex );
						if ( !top_candidates.empty() )
						{
							best_snapshot = top_candidates.front();
							has_best = true;
						}
					}

					// Snapshot active jobs (what breadth is currently working on).
					struct ActiveJob
					{
						std::size_t  thread_id = 0;
						std::size_t  job_id_one_based = 0;
						std::uint32_t mask_a = 0;
						std::uint32_t mask_b = 0;
					};
					std::vector<ActiveJob> active;
					active.reserve( active_job_id_by_thread.size() );
					for ( std::size_t i = 0; i < active_job_id_by_thread.size(); ++i )
					{
						const std::size_t id = active_job_id_by_thread[ i ].load( std::memory_order_relaxed );
						if ( id == 0 )
							continue;
						const std::size_t job_index = id - 1;
						if ( job_index >= jobs.size() )
							continue;
						const LinearAutoBreadthJob& j = jobs[ job_index ];
						active.push_back( ActiveJob { i, id, j.mask_a, j.mask_b } );
					}

					IosStateGuard g( std::cout );
					std::cout << "[Auto][Breadth] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
							  << "  jobs_per_second=" << std::setprecision( 2 ) << rate << "  elapsed_seconds=" << std::setprecision( 2 ) << elapsed;
					if ( has_best )
					{
						std::cout << "  best_w=" << best_snapshot.best_weight << "  best_start=";
						print_word32_hex( "mask_a=", best_snapshot.start_mask_a );
						std::cout << " ";
						print_word32_hex( "mask_b=", best_snapshot.start_mask_b );
					}
					if ( !active.empty() )
					{
						std::cout << "  active=" << active.size() << " {";
						const std::size_t show = std::min<std::size_t>( active.size(), 8 );
						for ( std::size_t i = 0; i < show; ++i )
						{
							if ( i )
								std::cout << ", ";
							std::cout << "[Job#" << active[ i ].job_id_one_based << "@" << active[ i ].thread_id << "] ";
							print_word32_hex( "mask_a=", active[ i ].mask_a );
							std::cout << " ";
							print_word32_hex( "mask_b=", active[ i ].mask_b );
						}
						if ( active.size() > show )
							std::cout << ", ...";
						std::cout << "}";
					}
					std::cout << "\n";

					last_done = done;
					last_time = now;
				}
				if ( done >= total )
					break;
				std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
			}
		};

		run_worker_threads_with_monitor( breadth_threads_clamped, [ & ]( int t ) { worker( t ); }, progress_monitor );
		if ( auto_pipeline_checkpoint.enabled() )
			auto_pipeline_checkpoint.write_stage_snapshot(
				LinearAutoPipelineStage::Breadth,
				breadth_runtime_controls,
				TwilightDream::runtime_component::runtime_elapsed_microseconds( auto_pipeline_checkpoint.breadth_stage_start_time ),
				"breadth_stage_completed" );

		std::cout << "\n[Auto][Breadth] done. total_nodes_visited=" << total_nodes.load() << "\n";
		write_runtime_event_if_open( runtime_log, "auto_breadth_stop", [ & ]( std::ostream& out ) {
			out << "round_count=" << command_line_options.round_count << "\n";
			out << "jobs=" << jobs.size() << "\n";
			out << "completed_jobs=" << completed.load( std::memory_order_relaxed ) << "\n";
			out << "total_nodes_visited=" << total_nodes.load( std::memory_order_relaxed ) << "\n";
			out << "top_candidate_count=" << top_candidates.size() << "\n";
			write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( auto_pipeline_state ) );
		} );
		if ( history_log_ok )
		{
			write_linear_auto_history_if_open( history_log, "breadth_stop", [ & ]( std::ostream& out ) {
				out << "jobs=" << jobs.size() << "\n";
				out << "completed_jobs=" << completed.load( std::memory_order_relaxed ) << "\n";
				out << "total_nodes_visited=" << total_nodes.load( std::memory_order_relaxed ) << "\n";
				out << "top_candidate_count=" << top_candidates.size() << "\n";
			} );
		}
		if ( top_candidates.empty() )
		{
			std::cout << "[Auto][Breadth] FAIL: no trail found in any candidate (within breadth limits).\n";
			cleanup();
			return 0;
		}

		std::cout << "[Auto][Breadth] TOP-" << top_candidates.size() << ":\n";
		for ( std::size_t i = 0; i < top_candidates.size(); ++i )
		{
			const auto&	  c = top_candidates[ i ];
			IosStateGuard g( std::cout );
			std::cout << "  #" << ( i + 1 ) << "  best_weight=" << c.best_weight << "  start=";
			print_word32_hex( "mask_a=", c.start_mask_a );
			std::cout << " ";
			print_word32_hex( "mask_b=", c.start_mask_b );
			std::cout << "  best_input=";
			print_word32_hex( "mask_a_in=", c.best_input_mask_a );
			std::cout << " ";
			print_word32_hex( "mask_b_in=", c.best_input_mask_b );
			std::cout << "  nodes=" << c.nodes << ( c.hit_maximum_search_nodes_limit ? " [HIT maximum_search_nodes limit]" : "" ) << "\n";
		}
		std::cout << "\n";

		// ---------------------------------------------------------------------
		// Stage 2: deep search on the best candidate (selected from the top-K pool)
		// ---------------------------------------------------------------------
		LinearBestSearchConfiguration deep_configuration = command_line_options.search_configuration;
		LinearBestSearchRuntimeControls deep_runtime_controls = command_line_options.runtime_controls;
		deep_configuration.search_mode = SearchMode::Strict;
		deep_configuration.round_count = command_line_options.round_count;
		// Deep stage: remove the breadth-only branching limiter.
		deep_configuration.maximum_round_predecessors = 0;
		deep_runtime_controls.maximum_search_nodes = command_line_options.auto_deep_maximum_search_nodes;	// 0=unlimited
		deep_runtime_controls.maximum_search_seconds = command_line_options.auto_max_time_seconds;
		deep_configuration.enable_verbose_output = true;
		// Auto deep policy: always use strict z-shell enumeration for var-var addition.
		deep_configuration.enable_weight_sliced_clat = true;
		deep_configuration.weight_sliced_clat_max_candidates = 0;
		if ( command_line_options.auto_target_best_weight >= 0 )
			deep_configuration.target_best_weight = command_line_options.auto_target_best_weight;
		apply_search_mode_overrides( deep_configuration );

		{
			std::scoped_lock lk( auto_pipeline_mutex );
			auto_pipeline_state.top_candidates = top_candidates;
			auto_pipeline_state.selected_candidate = top_candidates.front();
			auto_pipeline_state.has_selected_candidate = true;
			auto_pipeline_state.stage = LinearAutoPipelineStage::Deep;
		}
		const LinearAutoCandidate selected = auto_pipeline_state.selected_candidate;

		std::cout << "[Auto][Deep] selected_best #1/" << top_candidates.size() << "\n";
		print_word32_hex( "  mask_a=", selected.start_mask_a );
		std::cout << "  ";
		print_word32_hex( "mask_b=", selected.start_mask_b );
		std::cout << "\n";
		std::cout << "  search_mode=strict (breadth=strict)\n";
		std::cout << "  strict_caps=0 (max_injection_input_masks,max_round_predecessors)\n";
		std::cout << "  weight_sliced_clat=" << ( deep_configuration.enable_weight_sliced_clat ? "on" : "off" )
				  << "  weight_sliced_clat_max_candidates=" << deep_configuration.weight_sliced_clat_max_candidates << "\n";
		if ( deep_configuration.target_best_weight >= 0 )
		{
			std::cout << "  target_best_weight=" << deep_configuration.target_best_weight << "  (|corr| >= 2^-" << deep_configuration.target_best_weight << ")\n";
		}
		std::cout << "  deep_runtime_maximum_search_nodes=" << deep_runtime_controls.maximum_search_nodes << ( deep_runtime_controls.maximum_search_nodes == 0 ? " (unlimited)" : "" ) << "  deep_runtime_maximum_search_seconds=" << deep_runtime_controls.maximum_search_seconds << "  deep_maximum_round_predecessors=" << deep_configuration.maximum_round_predecessors << ( deep_configuration.maximum_round_predecessors == 0 ? " (unlimited)" : "" ) << "\n\n";
		write_runtime_event_if_open( runtime_log, "auto_deep_selected", [ & ]( std::ostream& out ) {
			out << "round_count=" << command_line_options.round_count << "\n";
			out << "selected_output_mask_branch_a=" << hex8( selected.start_mask_a ) << "\n";
			out << "selected_output_mask_branch_b=" << hex8( selected.start_mask_b ) << "\n";
			out << "seeded_upper_bound_weight=" << selected.best_weight << "\n";
			out << "runtime_maximum_search_nodes=" << deep_runtime_controls.maximum_search_nodes << "\n";
			out << "runtime_maximum_search_seconds=" << deep_runtime_controls.maximum_search_seconds << "\n";
			write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( auto_pipeline_state ) );
		} );
		if ( history_log_ok )
		{
			write_linear_auto_history_if_open( history_log, "deep_start", [ & ]( std::ostream& out ) {
				out << "selected_output_mask_branch_a=" << hex8( selected.start_mask_a ) << "\n";
				out << "selected_output_mask_branch_b=" << hex8( selected.start_mask_b ) << "\n";
				out << "seeded_upper_bound_weight=" << selected.best_weight << "\n";
				out << "runtime_maximum_search_nodes=" << deep_runtime_controls.maximum_search_nodes << "\n";
				out << "runtime_maximum_search_seconds=" << deep_runtime_controls.maximum_search_seconds << "\n";
			} );
		}

		// Force progress printing (needed to get data during long runs).
		// Keep behavior consistent with differential auto mode:
		// - Use --progress-every-seconds when provided
		// - Force non-zero interval in auto mode to avoid "silent" long runs
		const std::uint64_t deep_progress_sec = ( command_line_options.runtime_controls.progress_every_seconds == 0 ) ? 1 : command_line_options.runtime_controls.progress_every_seconds;
		deep_runtime_controls.progress_every_seconds = deep_progress_sec;

		BinaryCheckpointManager binary_checkpoint {};
		if ( auto_pipeline_checkpoint.enabled() )
		{
			binary_checkpoint.path = auto_pipeline_checkpoint.path;
			binary_checkpoint.every_seconds = command_line_options.runtime_controls.checkpoint_every_seconds;
			binary_checkpoint.write_override = &linear_auto_pipeline_deep_checkpoint_override;
			binary_checkpoint.write_override_user_data = &auto_pipeline_checkpoint;
			if ( auto_pipeline_checkpoint.enabled() )
				auto_pipeline_checkpoint.write_stage_snapshot( LinearAutoPipelineStage::Deep, deep_runtime_controls, 0, "deep_stage_selected" );
		}

		PressureCallbackGuard	 pressure_guard(
			binary_checkpoint.enabled() ? &binary_checkpoint : nullptr,
			auto_pipeline_checkpoint.enabled() ? &linear_auto_pressure_checkpoint_callback : nullptr );

		BestSearchResult best_result {};
		const std::string prefix = "[Job#1@0] ";
		TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );
		try
		{
			const int								  seed_weight = selected.best_weight;
			const std::vector<LinearTrailStepRecord>* seed_trail = selected.trail.empty() ? nullptr : &selected.trail;
			best_result = run_linear_best_search( selected.start_mask_a, selected.start_mask_b, deep_configuration, deep_runtime_controls, true, true, seed_weight, seed_trail, history_log_ok ? &history_log : nullptr, binary_checkpoint.enabled() ? &binary_checkpoint : nullptr, runtime_log_ok ? &runtime_log : nullptr, &invocation_metadata );
		}
		catch ( const std::bad_alloc& )
		{
			pmr_report_oom_once( "auto.deep.run" );
			best_result = BestSearchResult {};
		}

		if ( !best_result.found )
		{
			write_runtime_event_if_open( runtime_log, "auto_stop", [ & ]( std::ostream& out ) {
				out << "round_count=" << command_line_options.round_count << "\n";
				out << "reason=" << ( linear_result_is_resumable_pause( best_result ) ? "paused_runtime_budget" : "deep_no_trail" ) << "\n";
				write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( auto_pipeline_state ) );
			} );
			print_result( best_result );
			if ( !linear_result_is_resumable_pause( best_result ) )
				std::cout << "[Auto] FAIL: no trail found in deep stage.\n";
			cleanup();
			return linear_exit_code_from_result( best_result );
		}

		{
			std::scoped_lock lk( auto_pipeline_mutex );
			linear_apply_deep_result_to_auto_candidates( top_candidates, auto_pipeline_state.selected_candidate, best_result );
			auto_pipeline_state.top_candidates = top_candidates;
		}

		write_runtime_event_if_open( runtime_log, "auto_stop", [ & ]( std::ostream& out ) {
			out << "round_count=" << command_line_options.round_count << "\n";
			out << "reason=completed\n";
			out << "best_weight=" << best_result.best_weight << "\n";
			out << "nodes_visited=" << best_result.nodes_visited << "\n";
			write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( auto_pipeline_state ) );
		} );
		print_result( best_result );
		cleanup();
		return linear_exit_code_from_result( best_result );
	}

	static int run_linear_auto_resume_mode( const CommandLineOptions& command_line_options )
	{
		LinearAutoPipelineState state {};
		if ( !read_linear_auto_pipeline_checkpoint( command_line_options.resume_path, state ) )
		{
			std::cerr << "[Resume] ERROR: failed to read auto pipeline checkpoint: " << command_line_options.resume_path << "\n";
			return 1;
		}

		std::string error_message {};
		if ( !linear_auto_pipeline_matches_resume_request( command_line_options, state, error_message ) )
		{
			std::cerr << "[Resume] ERROR: " << error_message << ".\n";
			return 1;
		}

		TwilightDream::auto_pipeline_shared::print_resume_summary(
			std::cout,
			command_line_options.resume_path,
			state,
			[]( LinearAutoPipelineStage stage ) {
				return ( stage == LinearAutoPipelineStage::Breadth ) ? "breadth" : "deep";
			} );

		TwilightDream::auto_pipeline_shared::resolve_resume_log_artifact_paths(
			command_line_options.runtime_log_path,
			state,
			[&]() {
				return BestWeightHistory::default_path( state.round_count, state.start_output_mask_branch_a, state.start_output_mask_branch_b );
			},
			[&]() {
				return default_runtime_log_path( state.round_count, state.start_output_mask_branch_a, state.start_output_mask_branch_b );
			} );

		BestWeightHistory history_log {};
		const bool history_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			history_log,
			state.history_log_path,
			"[Auto][Resume]",
			"history_log_output_path",
			"history log",
			false );
		if ( history_log_ok )
		{
			write_linear_auto_history_if_open( history_log, "resume_start", [ & ]( std::ostream& out ) {
				out << "stage=" << ( state.stage == LinearAutoPipelineStage::Breadth ? "breadth" : "deep" ) << "\n";
				out << "jobs=" << state.jobs.size() << "\n";
				out << "completed_jobs=" << TwilightDream::auto_pipeline_shared::count_completed_flags( state.completed_job_flags ) << "\n";
			} );
		}

		RuntimeEventLog runtime_log {};
		const bool runtime_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			runtime_log,
			state.runtime_log_path,
			"[Auto][Resume]",
			"runtime_log_output_path",
			"runtime log",
			false );
		if ( runtime_log_ok )
		{
			write_runtime_event_if_open( runtime_log, "auto_resume_start", [ & ]( std::ostream& out ) {
				out << "round_count=" << state.round_count << "\n";
				out << "stage=" << ( state.stage == LinearAutoPipelineStage::Breadth ? "breadth" : "deep" ) << "\n";
				out << "jobs=" << state.jobs.size() << "\n";
				out << "completed_jobs=" << TwilightDream::auto_pipeline_shared::count_completed_flags( state.completed_job_flags ) << "\n";
				out << "runtime_checkpoint_every_seconds=" << command_line_options.runtime_controls.checkpoint_every_seconds << "\n";
				write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( state ) );
			} );
		}
		std::cout << "\n";

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		const std::uint64_t	   auto_headroom_bytes =
			compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		const MemoryGateEvaluation memory_gate = evaluate_linear_memory_gate( command_line_options, mem, true, false, true );
		const bool strict_search_mode =
			( command_line_options.search_mode_was_provided ? command_line_options.search_configuration.search_mode : infer_search_mode_from_caps( command_line_options.search_configuration ) ) ==
			SearchMode::Strict;
		if ( !print_and_enforce_memory_gate( "[Auto][Resume][MemoryGate] ", memory_gate, command_line_options.allow_high_memory_usage, strict_search_mode ) )
			return 1;
		const SearchInvocationMetadata invocation_metadata = build_invocation_metadata( memory_gate, strict_search_mode );
		pmr_configure_for_run( avail_bytes, auto_headroom_bytes );
		memory_governor_enable_for_run( auto_headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );
		auto cleanup = [ & ]() {
			memory_governor_disable_for_run();
		};

		MemoryBallast memory_ballast( auto_headroom_bytes );
		if ( command_line_options.memory_ballast_enabled && auto_headroom_bytes != 0 )
			memory_ballast.start();

		std::mutex auto_pipeline_mutex {};
		LinearAutoPipelineCheckpointSession auto_pipeline_checkpoint {};
		TwilightDream::auto_pipeline_shared::configure_resume_checkpoint_session(
			auto_pipeline_checkpoint,
			command_line_options.checkpoint_out_was_provided ? command_line_options.checkpoint_out_path : command_line_options.resume_path,
			command_line_options.runtime_controls.checkpoint_every_seconds,
			state,
			auto_pipeline_mutex,
			runtime_log_ok ? &runtime_log : nullptr );

		auto auto_pipeline_pressure_guard =
			TwilightDream::auto_pipeline_shared::ScopedValueRestore<LinearAutoPipelineCheckpointSession*>(
				g_linear_pressure_auto_pipeline_checkpoint,
				auto_pipeline_checkpoint.enabled() ? &auto_pipeline_checkpoint : nullptr );
		RebuildableReserveGuard rebuildable_reserve( command_line_options.rebuildable_reserve_mib );

		linear_sort_auto_top_candidates( state.top_candidates );
		LinearBestSearchRuntimeControls deep_runtime_controls = command_line_options.runtime_controls;
		deep_runtime_controls.maximum_search_nodes = command_line_options.auto_deep_maximum_search_nodes;
		deep_runtime_controls.maximum_search_seconds = command_line_options.auto_max_time_seconds;
		deep_runtime_controls.progress_every_seconds =
			( command_line_options.runtime_controls.progress_every_seconds == 0 ) ? 1 : command_line_options.runtime_controls.progress_every_seconds;
		LinearBestSearchConfiguration deep_configuration = state.search_configuration;
		deep_configuration.search_mode = SearchMode::Strict;
		deep_configuration.round_count = state.round_count;
		deep_configuration.maximum_round_predecessors = 0;
		deep_configuration.enable_verbose_output = true;
		deep_configuration.enable_weight_sliced_clat = true;
		deep_configuration.weight_sliced_clat_max_candidates = 0;
		if ( command_line_options.auto_target_best_weight >= 0 )
			deep_configuration.target_best_weight = command_line_options.auto_target_best_weight;
		apply_search_mode_overrides( deep_configuration );

		if ( state.stage == LinearAutoPipelineStage::Breadth )
		{
			PressureCallbackGuard breadth_pressure_guard( nullptr, auto_pipeline_checkpoint.enabled() ? &linear_auto_pressure_checkpoint_callback : nullptr );
			LinearBestSearchConfiguration breadth_configuration = state.search_configuration;
			LinearBestSearchRuntimeControls breadth_runtime_controls = command_line_options.runtime_controls;
			breadth_configuration.search_mode = SearchMode::Strict;
			breadth_configuration.round_count = state.round_count;
			breadth_configuration.enable_weight_sliced_clat = false;
			breadth_configuration.weight_sliced_clat_max_candidates = 0;
			breadth_configuration.enable_verbose_output = false;
			breadth_runtime_controls.maximum_search_nodes = std::max<std::uint64_t>( 1, command_line_options.auto_breadth_maximum_search_nodes );
			breadth_runtime_controls.maximum_search_seconds = command_line_options.runtime_controls.maximum_search_seconds;
			breadth_runtime_controls.progress_every_seconds = ( command_line_options.runtime_controls.progress_every_seconds == 0 ) ? 1 : command_line_options.runtime_controls.progress_every_seconds;
			apply_search_mode_overrides( breadth_configuration );

			if ( !TwilightDream::auto_pipeline_shared::resume_breadth_stage(
					 auto_pipeline_checkpoint,
					 state,
					 breadth_runtime_controls,
					 deep_runtime_controls,
					 LinearAutoPipelineStage::Breadth,
					 LinearAutoPipelineStage::Deep,
					 [ & ]( std::size_t job_index ) {
						 const LinearAutoBreadthJob job = state.jobs[ job_index ];
						 BestSearchResult result {};
						 try
						 {
							 result = run_linear_best_search( job.mask_a, job.mask_b, breadth_configuration, breadth_runtime_controls, false, false, INFINITE_WEIGHT, nullptr, nullptr, nullptr, nullptr, &invocation_metadata );
						 }
						 catch ( const std::bad_alloc& )
						 {
							 pmr_report_oom_once( "auto.resume.breadth.run" );
							 result = BestSearchResult {};
						 }

						 LinearAutoCandidate candidate {};
						 candidate.start_mask_a = job.mask_a;
						 candidate.start_mask_b = job.mask_b;
						 candidate.nodes = result.nodes_visited;
						 candidate.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
						 candidate.found = result.found;
						 if ( candidate.found )
						 {
							 candidate.best_weight = result.best_weight;
							 candidate.best_input_mask_a = result.best_input_branch_a_mask;
							 candidate.best_input_mask_b = result.best_input_branch_b_mask;
							 candidate.trail = std::move( result.best_linear_trail );
						 }

						 std::scoped_lock lk( auto_pipeline_mutex );
						 state.completed_job_flags[ job_index ] = 1u;
						 state.breadth_total_nodes_visited += result.nodes_visited;
						 linear_try_update_auto_top_candidates( state.top_candidates, state.auto_breadth_top_k, std::move( candidate ) );
					 },
					 [ & ]( std::size_t job_index ) {
						 std::cout << "[Auto][Resume][Breadth] completed_job=" << ( job_index + 1 ) << "/" << state.jobs.size()
								   << "  total_nodes_visited=" << state.breadth_total_nodes_visited << "\n";
					 } ) )
			{
				cleanup();
				std::cout << "[Auto][Resume][Breadth] FAIL: no trail found in any remaining candidate.\n";
				return 0;
			}
		}

		BinaryCheckpointManager binary_checkpoint {};
		binary_checkpoint.path = auto_pipeline_checkpoint.path;
		binary_checkpoint.every_seconds = auto_pipeline_checkpoint.every_seconds;
		binary_checkpoint.write_override = &linear_auto_pipeline_deep_checkpoint_override;
		binary_checkpoint.write_override_user_data = &auto_pipeline_checkpoint;
		PressureCallbackGuard deep_pressure_guard(
			auto_pipeline_checkpoint.enabled() ? &binary_checkpoint : nullptr,
			auto_pipeline_checkpoint.enabled() ? &linear_auto_pressure_checkpoint_callback : nullptr );

		if ( state.has_deep_snapshot )
		{
			LinearCheckpointLoadResult load = state.deep_snapshot;
			LinearBestSearchContext	context {};
			context.configuration = load.configuration;
			context.runtime_controls = deep_runtime_controls;
			context.start_output_branch_a_mask = load.start_mask_a;
			context.start_output_branch_b_mask = load.start_mask_b;
			context.visited_node_count = load.total_nodes_visited;
			context.run_visited_node_count = 0;
			context.best_weight = load.best_weight;
			context.best_input_branch_a_mask = load.best_input_mask_a;
			context.best_input_branch_b_mask = load.best_input_mask_b;
			context.best_linear_trail = load.best_trail;
			context.current_linear_trail = load.current_trail;
			context.memoization.clone_from(
				state.deep_memoization,
				"linear_memo.auto_resume.clone.init",
				"linear_memo.auto_resume.clone.reserve",
				"linear_memo.auto_resume.clone.emplace" );
			context.checkpoint = history_log_ok ? &history_log : nullptr;
			context.runtime_event_log = runtime_log_ok ? &runtime_log : nullptr;
			context.binary_checkpoint = auto_pipeline_checkpoint.enabled() ? &binary_checkpoint : nullptr;
			context.invocation_metadata = invocation_metadata;
			const LinearResumeFingerprint loaded_fingerprint = compute_linear_resume_fingerprint( load );
			LinearSearchCursor cursor = load.cursor;
			const LinearResumeFingerprint materialized_fingerprint = compute_linear_resume_fingerprint( context, cursor );
			if ( materialized_fingerprint.hash != loaded_fingerprint.hash )
			{
				cleanup();
				return 1;
			}
			TwilightDream::best_search_shared_core::run_resume_control_session(
				context,
				cursor,
				[]( LinearBestSearchContext& ctx ) {
					TwilightDream::best_search_shared_core::prepare_binary_checkpoint(
						ctx.binary_checkpoint,
						ctx.runtime_controls.checkpoint_every_seconds,
						true );
				},
				[]( LinearBestSearchContext& ctx ) {
					begin_linear_runtime_invocation( ctx );
				},
				[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
					linear_runtime_log_resume_event( ctx, resume_cursor, "resume_start" );
				},
				[]( LinearBestSearchContext& ctx, LinearSearchCursor& ) {
					if ( ctx.checkpoint )
						ctx.checkpoint->maybe_write( ctx, "resume_init" );
				},
				[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
					if ( ctx.checkpoint )
						write_linear_resume_snapshot( *ctx.checkpoint, ctx, resume_cursor, "resume_init" );
				},
				[]( LinearBestSearchContext& ctx, LinearSearchCursor& resume_cursor ) {
					ScopedRuntimeTimeLimitProbe time_probe( ctx.runtime_controls, ctx.runtime_state );
					linear_best_search_continue_from_cursor( ctx, resume_cursor );
				},
				[]( LinearBestSearchContext& ctx ) {
					return linear_runtime_budget_hit( ctx );
				},
				[]( LinearBestSearchContext& ctx, const char* reason ) {
					linear_runtime_log_basic_event( ctx, "checkpoint_preserved", reason );
				},
				[]( LinearBestSearchContext& ctx ) {
					if ( runtime_maximum_search_nodes_hit( ctx.runtime_controls, ctx.runtime_state ) )
						linear_runtime_log_basic_event( ctx, "resume_stop", "hit_maximum_search_nodes" );
					else if ( runtime_time_limit_hit( ctx.runtime_controls, ctx.runtime_state ) )
						linear_runtime_log_basic_event( ctx, "resume_stop", "hit_time_limit" );
					else if ( ctx.stop_due_to_target )
						linear_runtime_log_basic_event( ctx, "resume_stop", "hit_target_best_weight" );
					else
						linear_runtime_log_basic_event( ctx, "resume_stop", "completed" );
				} );

			MatsuiSearchRunLinearResult result {};
			result.nodes_visited = context.visited_node_count;
			result.hit_maximum_search_nodes = runtime_maximum_search_nodes_hit( context.runtime_controls, context.runtime_state );
			result.hit_time_limit = runtime_time_limit_hit( context.runtime_controls, context.runtime_state );
			result.hit_target_best_weight = context.stop_due_to_target;
			result.best_input_branch_a_mask = context.best_input_branch_a_mask;
			result.best_input_branch_b_mask = context.best_input_branch_b_mask;
			result.found = !context.best_linear_trail.empty();
			result.best_weight = result.found ? context.best_weight : INFINITE_WEIGHT;
			if ( result.found )
				result.best_linear_trail = std::move( context.best_linear_trail );
			if ( result.found )
				linear_apply_deep_result_to_auto_candidates( state.top_candidates, state.selected_candidate, result );
			write_runtime_event_if_open( runtime_log, "auto_stop", [ & ]( std::ostream& out ) {
				out << "round_count=" << state.round_count << "\n";
				out << "reason=" << ( result.found ? "completed" : ( linear_result_is_resumable_pause( result ) ? "paused_runtime_budget" : "deep_no_trail" ) ) << "\n";
				if ( result.found )
				{
					out << "best_weight=" << result.best_weight << "\n";
					out << "nodes_visited=" << result.nodes_visited << "\n";
				}
				write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( state ) );
			} );
			print_result( result );
			cleanup();
			return linear_exit_code_from_result( result );
		}

		if ( !TwilightDream::auto_pipeline_shared::ensure_selected_candidate( state ) )
		{
			std::cerr << "[Auto][Resume] ERROR: no selected candidate is available for deep stage.\n";
			cleanup();
			return 1;
		}

		const LinearAutoCandidate selected = state.selected_candidate;
		const int seed_weight = selected.best_weight;
		const std::vector<LinearTrailStepRecord>* seed_trail = selected.trail.empty() ? nullptr : &selected.trail;
		BestSearchResult best_result = run_linear_best_search(
			selected.start_mask_a,
			selected.start_mask_b,
			deep_configuration,
			deep_runtime_controls,
			true,
			true,
			seed_weight,
			seed_trail,
			history_log_ok ? &history_log : nullptr,
			auto_pipeline_checkpoint.enabled() ? &binary_checkpoint : nullptr,
			runtime_log_ok ? &runtime_log : nullptr,
			&invocation_metadata );
		if ( best_result.found )
			linear_apply_deep_result_to_auto_candidates( state.top_candidates, state.selected_candidate, best_result );
		write_runtime_event_if_open( runtime_log, "auto_stop", [ & ]( std::ostream& out ) {
			out << "round_count=" << state.round_count << "\n";
			out << "reason=" << ( best_result.found ? "completed" : ( linear_result_is_resumable_pause( best_result ) ? "paused_runtime_budget" : "deep_no_trail" ) ) << "\n";
			if ( best_result.found )
			{
				out << "best_weight=" << best_result.best_weight << "\n";
				out << "nodes_visited=" << best_result.nodes_visited << "\n";
			}
			write_linear_auto_pipeline_fingerprint_fields( out, compute_linear_auto_pipeline_fingerprint( state ) );
		} );
		print_result( best_result );
		cleanup();
		return linear_exit_code_from_result( best_result );
	}

	static int run_resume_mode( const CommandLineOptions& command_line_options )
	{
		if ( !command_line_options.resume_was_provided || command_line_options.resume_path.empty() )
		{
			std::cerr << "[Resume] ERROR: --resume PATH is required.\n";
			return 1;
		}
		if ( command_line_options.batch_job_count > 0 || command_line_options.batch_job_file_was_provided || command_line_options.strategy_batch_was_requested )
		{
			std::cerr << "[Resume] ERROR: resume does not support batch mode.\n";
			return 1;
		}
		if ( command_line_options.batch_seed_was_provided )
		{
			std::cerr << "[Resume] ERROR: --seed is not applicable when resuming.\n";
			return 1;
		}

		TwilightDream::auto_search_checkpoint::SearchKind checkpoint_kind {};
		if ( !read_checkpoint_kind_only( command_line_options.resume_path, checkpoint_kind ) )
		{
			std::cerr << "[Resume] ERROR: failed to read checkpoint header: " << command_line_options.resume_path << "\n";
			return 1;
		}
		if ( checkpoint_kind == TwilightDream::auto_search_checkpoint::SearchKind::LinearAutoPipeline )
			return run_linear_auto_resume_mode( command_line_options );
		if ( checkpoint_kind != TwilightDream::auto_search_checkpoint::SearchKind::LinearBest )
		{
			std::cerr << "[Resume] ERROR: checkpoint kind is not compatible with linear resume mode.\n";
			return 1;
		}
		if ( command_line_options.batch_thread_count > 1 )
		{
			std::cerr << "[Resume] ERROR: resume requires single-thread deep search.\n";
			return 1;
		}

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		std::uint64_t		   headroom_bytes = ( command_line_options.strategy_target_headroom_bytes != 0 ) ? command_line_options.strategy_target_headroom_bytes : compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		if ( avail_bytes != 0 && headroom_bytes > avail_bytes )
			headroom_bytes = avail_bytes;
		const MemoryGateEvaluation memory_gate = evaluate_linear_memory_gate( command_line_options, mem, false, false, true );
		const bool strict_search_mode =
			( command_line_options.search_mode_was_provided ? command_line_options.search_configuration.search_mode : infer_search_mode_from_caps( command_line_options.search_configuration ) ) ==
			SearchMode::Strict;
		if ( !print_and_enforce_memory_gate( "[Resume][MemoryGate] ", memory_gate, command_line_options.allow_high_memory_usage, strict_search_mode ) )
			return 1;
		const SearchInvocationMetadata invocation_metadata = build_invocation_metadata( memory_gate, strict_search_mode );

		pmr_configure_for_run( avail_bytes, headroom_bytes );
		memory_governor_enable_for_run( headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );

		MemoryBallast memory_ballast( headroom_bytes );
		if ( command_line_options.memory_ballast_enabled && headroom_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[MemoryBallast] enabled  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( headroom_bytes ) << "\n";
			memory_ballast.start();
		}

		auto cleanup = [ & ]() {
			memory_governor_disable_for_run();
		};

		LinearBestSearchContext  preload_memo_holder {};
		LinearCheckpointLoadResult load {};
		if ( !read_linear_checkpoint( command_line_options.resume_path, load, preload_memo_holder.memoization ) )
		{
			std::cerr << "[Resume] ERROR: failed to read binary checkpoint: " << command_line_options.resume_path << "\n";
			std::cerr << "[Resume] ERROR: binary checkpoint is unreadable or not in the current checkpoint format.\n";
			cleanup();
			return 1;
		}

		TwilightDream::best_search_shared_core::print_resume_checkpoint_load_summary(
			std::cout,
			command_line_options.resume_path,
			load.configuration.round_count,
			load.total_nodes_visited,
			load.run_nodes_visited,
			"best_weight",
			load.best_weight,
			load.runtime_maximum_search_nodes,
			load.runtime_maximum_search_seconds,
			load.runtime_checkpoint_every_seconds,
			load.accumulated_elapsed_usec );
		print_word32_hex( "[Resume] start_output_mask_branch_a=", load.start_mask_a );
		std::cout << " ";
		print_word32_hex( "start_output_mask_branch_b=", load.start_mask_b );
		std::cout << "\n";

		if ( command_line_options.round_count_was_provided && command_line_options.round_count != load.configuration.round_count )
		{
			std::cerr << "[Resume] ERROR: --round-count does not match checkpoint (checkpoint_rounds=" << load.configuration.round_count << ").\n";
			cleanup();
			return 1;
		}
		if ( command_line_options.output_masks_were_provided &&
			( command_line_options.output_branch_a_mask != load.start_mask_a || command_line_options.output_branch_b_mask != load.start_mask_b ) )
		{
			std::cerr << "[Resume] ERROR: provided output masks do not match checkpoint.\n";
			cleanup();
			return 1;
		}
		if ( command_line_options.search_configuration_was_provided )
		{
			LinearBestSearchConfiguration cli_config = command_line_options.search_configuration;
			cli_config.round_count = command_line_options.round_count_was_provided ? command_line_options.round_count : load.configuration.round_count;
			if ( !configs_compatible_for_resume( cli_config, load.configuration ) )
			{
				std::cerr << "[Resume] ERROR: search configuration does not match checkpoint.\n";
				cleanup();
				return 1;
			}
		}

		LinearBestSearchConfiguration expected_configuration = load.configuration;
		if ( command_line_options.search_configuration_was_provided )
		{
			expected_configuration = command_line_options.search_configuration;
			expected_configuration.round_count =
				command_line_options.round_count_was_provided ? command_line_options.round_count : load.configuration.round_count;
		}

		LinearBestSearchConfiguration execution_configuration = load.configuration;
		if ( command_line_options.search_mode_was_provided )
		{
			execution_configuration.search_mode = command_line_options.search_configuration.search_mode;
			const LinearFastSearchCapDefaults defaults = compute_default_fast_search_caps_for_cli( command_line_options );
			apply_search_mode_overrides(
				execution_configuration,
				defaults.maximum_injection_input_masks,
				defaults.maximum_round_predecessors );
			if ( execution_configuration.search_mode == SearchMode::Strict )
			{
				std::cout << "  search_mode=strict (override)\n";
				std::cout << "  strict_mode_candidate_limits=0 (maximum_injection_input_masks, maximum_round_predecessors)\n";
			}
			else
			{
				std::cout << "  search_mode=fast (override)\n";
			}
		}
		else
		{
			execution_configuration.search_mode = infer_search_mode_from_caps( execution_configuration );
			std::cout << "  search_mode=" << search_mode_to_string( execution_configuration.search_mode ) << " (inferred)\n";
		}
		std::cout << "  weight_sliced_clat=" << ( execution_configuration.enable_weight_sliced_clat ? "on" : "off" )
				  << "  weight_sliced_clat_max_candidates=" << execution_configuration.weight_sliced_clat_max_candidates << "\n";

		const auto log_artifact_paths =
			TwilightDream::best_search_shared_core::resolve_log_artifact_paths(
				command_line_options.runtime_log_path,
				load.history_log_path,
				load.runtime_log_path,
				[&]() {
					return BestWeightHistory::default_path( load.configuration.round_count, load.start_mask_a, load.start_mask_b );
				},
				[&]() {
					return default_runtime_log_path(
						load.configuration.round_count,
						load.start_mask_a,
						load.start_mask_b );
				} );

		BestWeightHistory history_log {};
		const std::string history_log_path = log_artifact_paths.history_log_path;
		const bool		  history_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			 std::cout,
			 history_log,
			 history_log_path,
			 "[Resume]",
			 "history_log_output_path",
			 "history log" );

		RuntimeEventLog runtime_log {};
		const std::string runtime_log_path = log_artifact_paths.runtime_log_path;
		const bool		   runtime_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			  std::cout,
			  runtime_log,
			  runtime_log_path,
			  "[Resume]",
			  "runtime_log_output_path",
			  "runtime log" );

		const TwilightDream::best_search_shared_core::RuntimeControlOverrideMask runtime_override_mask {
			command_line_options.runtime_maximum_search_nodes_was_provided,
			command_line_options.runtime_maximum_search_seconds_was_provided,
			false,
			command_line_options.checkpoint_every_seconds_was_provided
		};

		TwilightDream::best_search_shared_core::ResumeProgressReportingOptions progress_reporting {};
		if ( command_line_options.runtime_controls.progress_every_seconds == 0 )
			progress_reporting.force_disabled = true;
		else
		{
			progress_reporting.prefer_cli_interval = true;
			progress_reporting.cli_progress_every_seconds = command_line_options.runtime_controls.progress_every_seconds;
		}

		if ( command_line_options.checkpoint_out_was_provided && command_line_options.checkpoint_out_path.empty() )
		{
			std::cerr << "[Resume] ERROR: --checkpoint-out requires a non-empty path.\n";
			cleanup();
			return 1;
		}
		const std::string	   binary_checkpoint_path = command_line_options.checkpoint_out_was_provided ? command_line_options.checkpoint_out_path : command_line_options.resume_path;
		BinaryCheckpointManager binary_checkpoint {};
		BinaryCheckpointManager* binary_checkpoint_ptr = ( !binary_checkpoint_path.empty() ) ? &binary_checkpoint : nullptr;

		RebuildableReserveGuard rebuildable_reserve( command_line_options.rebuildable_reserve_mib );
		PressureCallbackGuard	 pressure_guard( binary_checkpoint_ptr );

		const BestSearchResult search_result = run_linear_best_search_resume(
			command_line_options.resume_path,
			load.start_mask_a,
			load.start_mask_b,
			expected_configuration,
			command_line_options.runtime_controls,
			false,
			false,
			history_log_ok ? &history_log : nullptr,
			binary_checkpoint_ptr,
			runtime_log_ok ? &runtime_log : nullptr,
			&invocation_metadata,
			&runtime_override_mask,
			&execution_configuration,
			&progress_reporting );

		if ( search_result.strict_rejection_reason == StrictCertificationFailureReason::CheckpointLoadFailed ||
			 search_result.strict_rejection_reason == StrictCertificationFailureReason::ResumeCheckpointMismatch )
		{
			if ( search_result.strict_rejection_reason == StrictCertificationFailureReason::CheckpointLoadFailed )
			{
				std::cerr << "[Resume] ERROR: failed to read binary checkpoint: " << command_line_options.resume_path << "\n";
				std::cerr << "[Resume] ERROR: binary checkpoint is unreadable or not in the current checkpoint format.\n";
			}
			else
				std::cerr << "[Resume] ERROR: resume checkpoint mismatch (" << strict_certification_failure_reason_to_string( search_result.strict_rejection_reason ) << ").\n";
			cleanup();
			return 1;
		}

		if ( binary_checkpoint_ptr )
		{
			std::cout << "[Resume] binary_checkpoint_output_path=" << binary_checkpoint.path << "\n";
			std::cout << "[Resume] binary_checkpoint_interval_seconds=" << binary_checkpoint.every_seconds << "\n";
			std::cout << "[Resume] binary_checkpoint_write_triggers=best_weight_change";
			if ( binary_checkpoint.every_seconds != 0 )
				std::cout << ",periodic_timer";
			std::cout << "\n";
		}

		print_result( search_result );

		cleanup();
		return linear_exit_code_from_result( search_result );
	}

	static int run_single_mode( const CommandLineOptions& command_line_options )
	{
		if ( command_line_options.round_count <= 0 )
		{
			std::cerr << "ERROR: --round-count must be > 0\n";
			return 1;
		}
		if ( !command_line_options.output_masks_were_provided )
		{
			std::cerr << "ERROR: output masks were not provided.\n";
			return 1;
		}
		if ( command_line_options.output_branch_a_mask == 0u && command_line_options.output_branch_b_mask == 0u )
		{
			std::cerr << "ERROR: output masks are both zero; this is trivial.\n";
			std::cerr << "  Provide at least one non-zero mask: --output-branch-a-mask / --output-branch-b-mask\n";
			return 1;
		}

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		std::uint64_t		   headroom_bytes = ( command_line_options.strategy_target_headroom_bytes != 0 ) ? command_line_options.strategy_target_headroom_bytes : compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		if ( avail_bytes != 0 && headroom_bytes > avail_bytes )
			headroom_bytes = avail_bytes;
		const MemoryGateEvaluation memory_gate = evaluate_linear_memory_gate( command_line_options, mem, false, false, false );
		const bool strict_search_mode =
			( command_line_options.search_mode_was_provided ? command_line_options.search_configuration.search_mode : infer_search_mode_from_caps( command_line_options.search_configuration ) ) ==
			SearchMode::Strict;
		if ( !print_and_enforce_memory_gate( "[MemoryGate] ", memory_gate, command_line_options.allow_high_memory_usage, strict_search_mode ) )
			return 1;
		const SearchInvocationMetadata invocation_metadata = build_invocation_metadata( memory_gate, strict_search_mode );

		// Configure PMR budget for this run.
		pmr_configure_for_run( avail_bytes, headroom_bytes );
		memory_governor_enable_for_run( headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );

		MemoryBallast memory_ballast( headroom_bytes );
		if ( command_line_options.memory_ballast_enabled && headroom_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[MemoryBallast] enabled  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( headroom_bytes ) << "\n";
			memory_ballast.start();
		}

		LinearBestSearchConfiguration search_configuration = command_line_options.search_configuration;
		search_configuration.round_count = command_line_options.round_count;
		const LinearFastSearchCapDefaults fast_defaults = compute_default_fast_search_caps_for_cli( command_line_options );
		const bool strict_overrides_applied =
			apply_search_mode_overrides(
				search_configuration,
				fast_defaults.maximum_injection_input_masks,
				fast_defaults.maximum_round_predecessors );

		std::cout << "round_count=" << search_configuration.round_count;
		if ( command_line_options.output_masks_were_generated && command_line_options.batch_seed_was_provided )
			std::cout << " (generated from --seed=0x" << std::hex << command_line_options.batch_seed << std::dec << ")";
		std::cout << "\n";
		print_word32_hex( "output_branch_a_mask=", command_line_options.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "output_branch_b_mask=", command_line_options.output_branch_b_mask );
		std::cout << "\n\n";

		if ( search_configuration.search_mode == SearchMode::Strict )
		{
			std::cout << "[Strict] enabled: candidate caps forced to 0 (max_injection_input_masks,max_round_predecessors)\n";
			if ( strict_overrides_applied )
				std::cout << "\n";
		}

		// Strategy mode is meant to be "lazy": show all auto-derived knobs up-front.
		if ( command_line_options.frontend_mode == CommandLineOptions::FrontendMode::Strategy )
		{
			std::cout << "[Strategy] resolved settings:\n";
			std::cout << "  preset=" << to_string( command_line_options.strategy_preset ) << "  heuristics=" << ( command_line_options.strategy_heuristics_enabled ? "on" : "off" ) << "\n";
			if ( command_line_options.strategy_total_work_was_provided )
			{
				std::cout << "  total_work=" << command_line_options.strategy_total_work << "\n";
			}
			std::cout << "  resolved_worker_threads=" << command_line_options.strategy_resolved_worker_threads << "\n";
			if ( command_line_options.strategy_available_physical_bytes != 0 )
			{
				SystemMemoryInfo mem_now = query_system_memory_info();
				if ( mem_now.total_physical_bytes == 0 )
					mem_now.total_physical_bytes = command_line_options.strategy_total_physical_bytes;
				if ( mem_now.available_physical_bytes == 0 )
					mem_now.available_physical_bytes = command_line_options.strategy_available_physical_bytes;
				print_system_memory_status_line( std::cout, mem_now, "  system_memory: " );
				if ( command_line_options.strategy_target_headroom_bytes != 0 )
				{
					IosStateGuard g( std::cout );
					std::cout << "  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_target_headroom_bytes );
				}
				if ( command_line_options.strategy_derived_budget_bytes != 0 )
				{
					IosStateGuard g( std::cout );
					std::cout << "  derived_budget_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_derived_budget_bytes );
				}
				if ( command_line_options.strategy_target_headroom_bytes != 0 || command_line_options.strategy_derived_budget_bytes != 0 )
					std::cout << "\n";
			}
			std::cout << "  addition_weight_cap=" << search_configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_configuration.constant_subtraction_weight_cap
					  << "  search_mode=" << search_mode_to_string( search_configuration.search_mode )
					  << "  weight_sliced_clat=" << ( search_configuration.enable_weight_sliced_clat ? "on" : "off" )
					  << "  weight_sliced_clat_max_candidates=" << search_configuration.weight_sliced_clat_max_candidates
					  << "  runtime_maximum_search_nodes=" << command_line_options.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << command_line_options.runtime_controls.maximum_search_seconds
					  << "  target_best_weight=" << search_configuration.target_best_weight << "  maximum_round_predecessors=" << search_configuration.maximum_round_predecessors
					  << "  maximum_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  enable_state_memoization=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n\n";
		}
		else
		{
			IosStateGuard g( std::cout );
			std::cout << "[Config] runtime_maximum_search_nodes=" << command_line_options.runtime_controls.maximum_search_nodes << "  runtime_maximum_search_seconds=" << command_line_options.runtime_controls.maximum_search_seconds
					  << "  target_best_weight=" << search_configuration.target_best_weight << "  search_mode=" << search_mode_to_string( search_configuration.search_mode )
					  << "  weight_sliced_clat=" << ( search_configuration.enable_weight_sliced_clat ? "on" : "off" )
					  << "  weight_sliced_clat_max_candidates=" << search_configuration.weight_sliced_clat_max_candidates
					  << "  maximum_round_predecessors=" << search_configuration.maximum_round_predecessors << "  addition_weight_cap=" << search_configuration.addition_weight_cap
					  << "  constant_subtraction_weight_cap=" << search_configuration.constant_subtraction_weight_cap
					  << "  maximum_injection_input_masks=" << search_configuration.maximum_injection_input_masks
					  << "  state_memoization=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n\n";
		}

		BinaryCheckpointManager binary_checkpoint {};
		if ( command_line_options.checkpoint_out_was_provided || command_line_options.checkpoint_every_seconds_was_provided )
		{
			if ( command_line_options.checkpoint_out_was_provided && command_line_options.checkpoint_out_path.empty() )
			{
				std::cerr << "[Checkpoint] ERROR: --checkpoint-out requires a non-empty path.\n";
				memory_governor_disable_for_run();
				return 1;
			}
			const std::string path = command_line_options.checkpoint_out_was_provided ? command_line_options.checkpoint_out_path :
				default_binary_checkpoint_path( search_configuration.round_count, command_line_options.output_branch_a_mask, command_line_options.output_branch_b_mask );
			binary_checkpoint.path = path;
			binary_checkpoint.every_seconds = command_line_options.runtime_controls.checkpoint_every_seconds;
			std::cout << "[Checkpoint] binary_checkpoint_output_path=" << binary_checkpoint.path << "\n";
			std::cout << "[Checkpoint] binary_checkpoint_interval_seconds=" << binary_checkpoint.every_seconds << "\n";
			std::cout << "[Checkpoint] binary_checkpoint_write_triggers=best_weight_change";
			if ( binary_checkpoint.every_seconds != 0 )
				std::cout << ",periodic_timer";
			std::cout << "\n\n";
		}

		const auto log_artifact_paths =
			TwilightDream::best_search_shared_core::resolve_log_artifact_paths(
				command_line_options.runtime_log_path,
				std::string {},
				std::string {},
				[&]() {
					return BestWeightHistory::default_path( search_configuration.round_count, command_line_options.output_branch_a_mask, command_line_options.output_branch_b_mask );
				},
				[&]() {
					return default_runtime_log_path(
						search_configuration.round_count,
						command_line_options.output_branch_a_mask,
						command_line_options.output_branch_b_mask );
				} );

		BestWeightHistory history_log {};
		const std::string history_log_path = log_artifact_paths.history_log_path;
		const bool history_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			history_log,
			history_log_path,
			"[Checkpoint]",
			"history_log_output_path",
			"history log",
			true,
			"\n\n",
			"\n\n" );

		RuntimeEventLog runtime_log {};
		const std::string runtime_log_path = log_artifact_paths.runtime_log_path;
		const bool runtime_log_ok = TwilightDream::best_search_shared_core::open_append_log_and_report(
			std::cout,
			runtime_log,
			runtime_log_path,
			"[RuntimeLog]",
			"runtime_log_output_path",
			"runtime log",
			true,
			"\n\n",
			"\n\n" );

		RebuildableReserveGuard rebuildable_reserve( command_line_options.rebuildable_reserve_mib );
		PressureCallbackGuard	 pressure_guard( binary_checkpoint.enabled() ? &binary_checkpoint : nullptr );

		BestSearchResult best_search_result {};
		try
		{
			best_search_result = run_linear_best_search( command_line_options.output_branch_a_mask, command_line_options.output_branch_b_mask, search_configuration, command_line_options.runtime_controls, true, false, INFINITE_WEIGHT, nullptr, history_log_ok ? &history_log : nullptr,
														 binary_checkpoint.enabled() ? &binary_checkpoint : nullptr, runtime_log_ok ? &runtime_log : nullptr, &invocation_metadata );
		}
		catch ( const std::bad_alloc& )
		{
			pmr_report_oom_once( "single.run" );
			std::cout << "[ERROR] out of memory.\n";
			memory_governor_disable_for_run();
			return 1;
		}
		memory_governor_disable_for_run();
		print_result( best_search_result );
		return linear_exit_code_from_result( best_search_result );
	}

	static void print_result( const BestSearchResult& result )
	{
		if ( !result.found )
		{
			if ( result.hit_maximum_search_nodes || result.hit_time_limit )
				std::cout << "[PAUSE] no trail found yet before the runtime budget expired; checkpoint/resume can continue.\n";
			else
				std::cout << "[FAIL] no trail found within limits.\n";
			std::cout << "  nodes_visited=" << result.nodes_visited;
			if ( result.hit_maximum_search_nodes )
				std::cout << "  [HIT maximum_search_nodes]";
			if ( result.hit_time_limit )
				std::cout << "  [HIT maximum_search_seconds]";
			std::cout << "\n";
			return;
		}

		std::cout << "[OK] best_weight=" << result.best_weight << "  (|corr| >= 2^-" << result.best_weight << ")\n";
		{
			IosStateGuard g( std::cout );
			std::cout << "  approx_abs_correlation=" << std::scientific << std::setprecision( 10 ) << weight_to_abs_correlation( result.best_weight ) << std::defaultfloat << "\n";
		}
		std::cout << "  nodes_visited=" << result.nodes_visited;
		if ( result.hit_maximum_search_nodes )
			std::cout << "  [HIT maximum_search_nodes]";
		if ( result.hit_time_limit )
			std::cout << "  [HIT maximum_search_seconds]";
		if ( result.hit_target_best_weight )
			std::cout << "  [HIT target_best_weight]";
		std::cout << "\n";
		TwilightDream::auto_search_linear::print_word32_hex( "  best_input_branch_a_mask=", result.best_input_branch_a_mask );
		std::cout << "  ";
		TwilightDream::auto_search_linear::print_word32_hex( "best_input_branch_b_mask=", result.best_input_branch_b_mask );
		std::cout << "\n\n";

		for ( const auto& step_record : result.best_linear_trail )
		{
			std::cout << "R" << step_record.round_index << "  round_weight=" << step_record.round_weight << "\n";
			TwilightDream::auto_search_linear::print_word32_hex( "  output_branch_a_mask=", step_record.output_branch_a_mask );
			std::cout << "  ";
			TwilightDream::auto_search_linear::print_word32_hex( "output_branch_b_mask=", step_record.output_branch_b_mask );
			std::cout << "\n";
			TwilightDream::auto_search_linear::print_word32_hex( "  input_branch_a_mask=", step_record.input_branch_a_mask );
			std::cout << "  ";
			TwilightDream::auto_search_linear::print_word32_hex( "input_branch_b_mask=", step_record.input_branch_b_mask );
			std::cout << "\n";
			TwilightDream::auto_search_linear::print_word32_hex( "  chosen_correlated_input_mask_for_injection_from_branch_a=", step_record.chosen_correlated_input_mask_for_injection_from_branch_a );
			std::cout << "  ";
			TwilightDream::auto_search_linear::print_word32_hex( "chosen_correlated_input_mask_for_injection_from_branch_b=", step_record.chosen_correlated_input_mask_for_injection_from_branch_b );
			std::cout << "\n";

			std::cout << "  weight_injection_from_branch_a=" << step_record.weight_injection_from_branch_a << "  weight_second_constant_subtraction=" << step_record.weight_second_constant_subtraction << "  weight_second_addition=" << step_record.weight_second_addition << "  weight_injection_from_branch_b=" << step_record.weight_injection_from_branch_b << "  weight_first_constant_subtraction=" << step_record.weight_first_constant_subtraction << "  weight_first_addition=" << step_record.weight_first_addition << "\n\n";
		}
	}

}  // namespace

int main( int argument_count, char** argument_values )
{
	CommandLineOptions command_line_options {};
	if ( !parse_command_line( argument_count, argument_values, command_line_options ) || command_line_options.show_help )
	{
		print_usage_information( ( argument_count > 0 && argument_values && argument_values[ 0 ] ) ? argument_values[ 0 ] : "test_neoalzette_linear_best_search.exe" );
		return command_line_options.show_help ? 0 : 1;
	}
	if ( command_line_options.selftest )
	{
		constexpr std::uint64_t kDefaultSelftestSeed = 0xC0FFEE4321ull;
		constexpr std::size_t  kDefaultExtraCases = 8;
		const std::uint64_t seed = command_line_options.selftest_seed_was_provided ? command_line_options.selftest_seed : kDefaultSelftestSeed;
		const std::size_t extra_cases = command_line_options.selftest_case_count_was_provided ? command_line_options.selftest_case_count : kDefaultExtraCases;
		std::cout << "[SelfTest] operator correctness\n";
		const int arx_rc = run_arx_operator_self_test();
		if ( arx_rc != 0 )
			return arx_rc;
		std::cout << "[SelfTest] search/resume/cache correctness\n";
		return run_linear_search_self_test( seed, extra_cases );
	}

	print_banner_and_mode( command_line_options );

	if ( command_line_options.resume_was_provided )
	{
		return run_resume_mode( command_line_options );
	}

	if ( command_line_options.frontend_mode == CommandLineOptions::FrontendMode::Auto )
	{
		return run_auto_mode( command_line_options );
	}

	if ( command_line_options.batch_job_count > 0 || command_line_options.batch_job_file_was_provided )
	{
		std::cerr << "[Batch] Batch breadth→deep runs are implemented in test_neoalzette_linear_hull_wrapper (same flags: --batch-job-count / --batch-file, --seed, --thread-count, --auto-breadth-maxnodes, --auto-deep-maxnodes, …).\n";
		return 1;
	}
	return run_single_mode( command_line_options );
}
