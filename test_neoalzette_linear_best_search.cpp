#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined( __linux__ )
#include <sys/sysinfo.h>
#endif

#include "auto_search_frame/test_neoalzette_linear_best_search.hpp"

namespace
{
	using namespace TwilightDream::auto_search_linear;
	using ::TwilightDream::NeoAlzetteCore;
	using TwilightDream::runtime_component::bytes_to_gibibytes;
	using TwilightDream::runtime_component::compute_memory_headroom_bytes;
	using TwilightDream::runtime_component::governor_poll_system_memory_once;
	using TwilightDream::runtime_component::IosStateGuard;
	using TwilightDream::runtime_component::MemoryBallast;
	using TwilightDream::runtime_component::query_system_memory_info;
	using TwilightDream::runtime_component::resolve_worker_thread_count_for_command_line_interface;
	using TwilightDream::runtime_component::run_worker_threads_with_monitor;
	using TwilightDream::runtime_component::SystemMemoryInfo;

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
		bool show_help = false;

		// Memory (safety-first "near-limit" mode): keep some headroom, optionally allocate/release ballast to stay near the limit.
		bool memory_ballast_enabled = false;

		// Memory budgeting for PMR bounded resource
		std::uint64_t memory_headroom_mib = 0;	// 0 = default headroom rule
		bool		  memory_headroom_mib_was_provided = false;

		int			  round_count = 1;
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		bool		  output_masks_were_provided = false;
		bool		  output_masks_were_generated = false;

		// Auto mode (single-run only)
		std::size_t	  auto_breadth_candidate_count = 256;  // scan this many candidate output mask pairs (includes the provided start pair)
		std::size_t	  auto_breadth_top_k = 3;			   // keep/print top-K breadth candidates; deep stage selects the best one
		int			  auto_breadth_thread_count = 0;	   // 0=auto (hardware_concurrency)
		std::uint64_t auto_breadth_seed = 0;			   // RNG seed for breadth candidates (default: derived from start masks)
		bool		  auto_breadth_seed_was_provided = false;
		std::uint64_t auto_breadth_maximum_search_nodes = ( std::numeric_limits<uint32_t>::max() >> 12 );  // per candidate breadth budget
		std::size_t	  auto_breadth_maximum_round_predecessors = 512;									   // breadth branching limiter
		int			  auto_breadth_max_bit_flips = 4;													   // structured neighborhood: max random bit flips applied (1..this)
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
		std::uint64_t progress_every_seconds = 1;  // print progress every N seconds (0=disable)

		// Keep the historical default from this executable (even if the header defaults differ).
		LinearBestSearchConfiguration search_configuration = [] {
			LinearBestSearchConfiguration c {};
			c.maximum_search_nodes = 2000000;
			return c;
		}();
	};

	static std::string to_lowercase_ascii( std::string s )
	{
		for ( char& c : s )
		{
			if ( c >= 'A' && c <= 'Z' )
				c = char( c - 'A' + 'a' );
		}
		return s;
	}

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
				  << "                         [--batch-job-count N --thread-count T --seed S]\n"
				  << "  presets:\n"
				  << "    time      Time-first: large budgets; memoization on; larger branching caps.\n"
				  << "    balanced  Balanced: moderate budgets; memoization on; heuristic caps.\n"
				  << "    space     Space-first: lower memory. Memoization off. Smaller caps.\n"
				  << "  strategy options:\n"
				  << "    --round-count R\n"
				  << "    --output-branch-a-mask MASK_A   Alias: --out-mask-a, --mask-a\n"
				  << "    --output-branch-b-mask MASK_B   Alias: --out-mask-b, --mask-b\n"
				  << "    --seed SEED\n"
				  << "    --total-work N                  Auto-scale budgets (maximum_search_nodes). Alias: --total. In batch: sets job count to N.\n"
				  << "    --batch                         Enable batch mode (job count comes from --total-work).\n"
				  << "    --batch-job-count N             Enable batch mode with explicit job count. (batch RNG requires --seed)\n"
				  << "    --batch-file PATH               Batch mode from file (see format below).\n"
				  << "    --thread-count T                Alias: --threads\n"
				  << "    --progress-every-jobs N         Alias: --progress\n"
				  << "    --progress-every-seconds S      Alias: --progress-sec\n"
				  << "    --maximum-search-nodes N        Alias: --maxnodes\n"
				  << "    --target-best-weight W          Alias: --target-weight\n\n"
				  << "Detail mode (full knobs, long names; short-name aliases kept):\n"
				  << "  " << executable_name << " detail --round-count R --output-branch-a-mask MASK_A --output-branch-b-mask MASK_B [options]\n\n"
				  << "Auto mode (two-stage: breadth scan -> deep search, requires explicit output masks):\n"
				  << "  " << executable_name << " auto --round-count R --output-branch-a-mask MASK_A --output-branch-b-mask MASK_B [options]\n"
				  << "  auto options:\n"
				  << "    --auto-breadth-jobs N           Alias: --auto-breadth-max-runs. Candidate count (default=256).\n"
				  << "    --auto-breadth-top_candidates K Keep/print top K breadth candidates (default=3). Deep runs the best one.\n"
				  << "    --auto-breadth-threads T        Breadth threads (0=auto).\n"
				  << "    --auto-breadth-seed S           RNG seed for breadth candidates (default: derived from start masks).\n"
				  << "    --auto-breadth-maxnodes N       Breadth per-candidate maximum_search_nodes.\n"
				  << "    --auto-breadth-hcap N           Breadth maximum_round_predecessors cap.\n"
				  << "    --auto-breadth-max-bitflips F   Structured neighborhood: max random bit flips (default=4).\n"
				  << "    --auto-print-breadth-candidates  Print ALL breadth candidates (warning: verbose).\n"
				  << "    --auto-deep-maxnodes N          Deep per-candidate maximum_search_nodes (0=unlimited).\n"
				  << "    --auto-max-time T               Deep time budget when deep maximum_search_nodes==0. Examples: 3600, 30d, 4w.\n"
				  << "    --auto-target-best-weight W     Stop once best_weight <= W.\n\n"
				  << "Common options:\n"
				  << "  --selftest                       Run ARX operator self-tests and exit.\n"
				  << "  --help, -h                       Show this help.\n"
				  << "  --mode strategy|detail|auto      Select CLI frontend (when no subcommand is used).\n\n"
				  << "Common parameters:\n"
				  << "  --round-count R\n"
				  << "  --output-branch-a-mask MASK_A    Alias: --out-mask-a, --mask-a\n"
				  << "  --output-branch-b-mask MASK_B    Alias: --out-mask-b, --mask-b\n\n"
				  << "Search options (detail):\n"
				  << "  --maximum-search-nodes N         Alias: --maxnodes. Node budget (0=unlimited). Default: 2000000\n"
				  << "  --maximum-search-seconds T       Alias: --maxsec. Time budget (0=unlimited). Examples: 3600, 30d, 4w.\n"
				  << "  --target-best-weight W           Alias: --target-weight. Stop early once best_weight <= W.\n"
				  << "  --addition-weight-cap N          Alias: --add-weight-cap, --add. Per modular-addition weight cap (0..31).\n"
				  << "  --maximum-addition-candidates N  Alias: --add-candidates. Cap enumerated (v,w) pairs per addition (0=no cap).\n"
				  << "  --maximum-constant-subtraction-candidates N\n"
				  << "                                   Alias: --sub-candidates, --maxconst. Per-subconst candidate input masks.\n"
				  << "  --maximum-injection-input-masks N\n"
				  << "                                   Alias: --injection-candidates, --maximum-injection-candidates.\n"
				  << "  --maximum-round-predecessors N   Alias: --max-round-predecessors, --heuristic-branch-cap, --hcap.\n"
				  << "  --disable-state-memoization      Alias: --nomemo. Disable memoization (less memory, usually slower).\n"
				  << "  --enable-verbose-output          Alias: --verbose. Verbose output.\n\n"
				  << "Memory options:\n"
				  << "  --memory-headroom-mib M          Keep ~M MiB of free RAM headroom for PMR budgeting.\n"
				  << "                                   Default: max(2GiB, min(4GiB, avail/10)).\n"
				  << "  --memory-ballast                Adaptive ballast: allocate/release RAM to keep free RAM near headroom.\n\n"
				  << "Batch options (detail/strategy):\n"
				  << "  --batch-job-count N              Alias: --batch. Enable batch mode with N random output masks.\n"
				  << "  --batch-file PATH                Batch mode from file. Each non-empty line is either:\n"
				  << "                                   - \"mask_a mask_b\" (use global --round-count)\n"
				  << "                                   - \"round_count mask_a mask_b\" (per-job rounds override global)\n"
				  << "                                   Numbers accept hex (0x...) or decimal; commas are allowed; lines starting with # are comments.\n"
				  << "  --thread-count THREADS           Alias: --threads. Thread count (0=auto).\n"
				  << "  --seed SEED                      RNG seed (hex or decimal). Required for random jobs.\n"
				  << "  --progress-every-jobs N          Alias: --progress. Print progress every N jobs (default=10, 0=disable).\n"
				  << "  --progress-every-seconds S       Alias: --progress-sec. Print progress every S seconds (default=1, 0=disable).\n";
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

	static std::uint64_t saturating_mul_u64( std::uint64_t a, std::uint64_t b )
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
			command_line_options.search_configuration.maximum_search_nodes = saturating_mul_u64( 25'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 4096;
			command_line_options.search_configuration.maximum_addition_candidates = 65'536;
			command_line_options.search_configuration.maximum_constant_subtraction_candidates = 64;
			command_line_options.search_configuration.maximum_injection_input_masks = 4096;
			break;
		case CommandLineOptions::StrategyPreset::Balanced:
			heuristics_enabled = true;
			command_line_options.search_configuration.maximum_search_nodes = saturating_mul_u64( 5'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 512;
			command_line_options.search_configuration.maximum_addition_candidates = 16'384;
			command_line_options.search_configuration.maximum_constant_subtraction_candidates = 16;
			command_line_options.search_configuration.maximum_injection_input_masks = 256;
			break;
		case CommandLineOptions::StrategyPreset::SpaceFirst:
			heuristics_enabled = true;
			command_line_options.search_configuration.maximum_search_nodes = saturating_mul_u64( 1'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = false;
			command_line_options.search_configuration.maximum_round_predecessors = 256;
			command_line_options.search_configuration.maximum_addition_candidates = 4096;
			command_line_options.search_configuration.maximum_constant_subtraction_candidates = 8;
			command_line_options.search_configuration.maximum_injection_input_masks = 64;
			break;
		case CommandLineOptions::StrategyPreset::None:
		default:
			heuristics_enabled = true;
			command_line_options.search_configuration.maximum_search_nodes = saturating_mul_u64( 5'000'000ull, std::uint64_t( scale ) );
			command_line_options.search_configuration.enable_state_memoization = true;
			command_line_options.search_configuration.maximum_round_predecessors = 512;
			command_line_options.search_configuration.maximum_addition_candidates = 16'384;
			command_line_options.search_configuration.maximum_constant_subtraction_candidates = 16;
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
			command_line_options.search_configuration.maximum_search_nodes = std::clamp<std::uint64_t>( command_line_options.search_configuration.maximum_search_nodes, 1ull, cap );
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

			if ( ( argument == "--round-count" || argument == "--rounds" ) && argument_index + 1 < argument_count )
			{
				int round_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], round_count ) || round_count <= 0 )
					return false;
				command_line_options.round_count = round_count;
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
				command_line_options.search_configuration.maximum_search_nodes = maximum_search_nodes;
			}
			else if ( ( argument == "--maximum-search-seconds" || argument == "--maxsec" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t seconds = 0;
				if ( !parse_duration_in_seconds( argument_values[ ++argument_index ], seconds ) )
					return false;
				command_line_options.search_configuration.maximum_search_seconds = seconds;
			}
			else if ( ( argument == "--target-best-weight" || argument == "--target-weight" ) && argument_index + 1 < argument_count )
			{
				int w = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], w ) )
					return false;
				command_line_options.search_configuration.target_best_weight = w;
			}
			else if ( ( argument == "--maximum-round-predecessors" || argument == "--max-round-predecessors" || argument == "--heuristic-branch-cap" || argument == "--hcap" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_round_predecessors = v;
			}
			else if ( ( argument == "--addition-weight-cap" || argument == "--add-weight-cap" || argument == "--add" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.addition_weight_cap = std::clamp( v, 0, 31 );
			}
			else if ( ( argument == "--constant-subtraction-weight-cap" || argument == "--subconst-weight-cap" || argument == "--subconst-cap" || argument == "--subcap" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.constant_subtraction_weight_cap = std::clamp( v, 0, 32 );
			}
			else if ( ( argument == "--maximum-addition-candidates" || argument == "--add-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_addition_candidates = v;
			}
			else if ( ( argument == "--maximum-constant-subtraction-candidates" || argument == "--sub-candidates" || argument == "--maxconst" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_constant_subtraction_candidates = v;
			}
			else if ( ( argument == "--maximum-injection-input-masks" || argument == "--maximum-injection-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_injection_input_masks = v;
			}
			else if ( argument == "--disable-state-memoization" || argument == "--nomemo" )
			{
				command_line_options.search_configuration.enable_state_memoization = false;
			}
			else if ( argument == "--enable-state-memoization" )
			{
				command_line_options.search_configuration.enable_state_memoization = true;
			}
			else if ( argument == "--enable-verbose-output" || argument == "--verbose" )
			{
				command_line_options.search_configuration.enable_verbose_output = true;
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
				command_line_options.progress_every_seconds = seconds;
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

		// Selftest/help should be runnable without requiring masks.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		const bool batch_enabled = ( command_line_options.batch_job_count > 0 ) || command_line_options.batch_job_file_was_provided;

		if ( !batch_enabled )
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
		return command_line_options.output_masks_were_provided;
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

			if ( ( argument == "--preset" || argument == "--strategy" ) && argument_index + 1 < argument_count )
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
				command_line_options.progress_every_seconds = seconds;
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
			command_line_options.search_configuration.maximum_search_nodes = maximum_search_nodes_override;
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
		if ( !batch_enabled )
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

			if ( ( argument == "--round-count" || argument == "--rounds" ) && argument_index + 1 < argument_count )
			{
				int round_count = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], round_count ) || round_count <= 0 )
					return false;
				command_line_options.round_count = round_count;
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
			}
			else if ( ( argument == "--constant-subtraction-weight-cap" || argument == "--subconst-weight-cap" || argument == "--subconst-cap" || argument == "--subcap" ) && argument_index + 1 < argument_count )
			{
				int v = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.constant_subtraction_weight_cap = std::clamp( v, 0, 32 );
			}
			else if ( ( argument == "--maximum-search-nodes" || argument == "--maxnodes" ) && argument_index + 1 < argument_count )
			{
				std::uint64_t maximum_search_nodes = 0;
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], maximum_search_nodes ) )
					return false;
				command_line_options.search_configuration.maximum_search_nodes = maximum_search_nodes;
			}
			else if ( ( argument == "--maximum-round-predecessors" || argument == "--max-round-predecessors" || argument == "--hcap" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_round_predecessors = v;
			}
			else if ( ( argument == "--maximum-addition-candidates" || argument == "--add-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_addition_candidates = v;
			}
			else if ( ( argument == "--maximum-constant-subtraction-candidates" || argument == "--sub-candidates" || argument == "--maxconst" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_constant_subtraction_candidates = v;
			}
			else if ( ( argument == "--maximum-injection-input-masks" || argument == "--injection-candidates" ) && argument_index + 1 < argument_count )
			{
				std::size_t v = 0;
				if ( !parse_unsigned_size( argument_values[ ++argument_index ], v ) )
					return false;
				command_line_options.search_configuration.maximum_injection_input_masks = v;
			}
			else if ( argument == "--disable-state-memoization" || argument == "--nomemo" )
			{
				command_line_options.search_configuration.enable_state_memoization = false;
			}
			else if ( argument == "--enable-state-memoization" )
			{
				command_line_options.search_configuration.enable_state_memoization = true;
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
			else if ( argument == "--auto-breadth-seed" && argument_index + 1 < argument_count )
			{
				if ( !parse_unsigned_integer_64( argument_values[ ++argument_index ], command_line_options.auto_breadth_seed ) )
					return false;
				command_line_options.auto_breadth_seed_was_provided = true;
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
			else if ( argument == "--auto-breadth-max-bitflips" && argument_index + 1 < argument_count )
			{
				int f = 0;
				if ( !parse_signed_integer_32( argument_values[ ++argument_index ], f ) )
					return false;
				command_line_options.auto_breadth_max_bit_flips = f;
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
		if ( command_line_options.auto_breadth_maximum_search_nodes == 0 )
			command_line_options.auto_breadth_maximum_search_nodes = 1;
		if ( command_line_options.auto_target_best_weight >= 0 )
			command_line_options.search_configuration.target_best_weight = command_line_options.auto_target_best_weight;

		// Selftest/help should be runnable without requiring masks.
		if ( command_line_options.selftest || command_line_options.show_help )
			return true;

		// Auto mode requires explicit masks (no --seed fallback).
		if ( !command_line_options.output_masks_were_provided )
			return false;
		if ( command_line_options.output_branch_a_mask == 0u && command_line_options.output_branch_b_mask == 0u )
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
		switch ( frontend )
		{
		case CommandLineOptions::FrontendMode::Strategy:
			return parse_command_line_strategy_mode( argument_count, argument_values, start_index, command_line_options );
		case CommandLineOptions::FrontendMode::Detail:
			return parse_command_line_detail_mode( argument_count, argument_values, start_index, command_line_options );
		case CommandLineOptions::FrontendMode::Auto:
			return parse_command_line_auto_mode( argument_count, argument_values, start_index, command_line_options );
		default:
			return parse_command_line_detail_mode( argument_count, argument_values, start_index, command_line_options );
		}
	}

	static void print_result( const BestSearchResult& result );

	static inline double weight_to_abs_correlation( int weight )
	{
		if ( weight >= INFINITE_WEIGHT )
			return 0.0;
		return std::pow( 2.0, -double( weight ) );
	}

	static int run_auto_mode( const CommandLineOptions& command_line_options )
	{
		// Auto mode is single-run only (no batch).
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

		const unsigned hardware_thread_concurrency = std::thread::hardware_concurrency();
		const int	   breadth_threads = ( command_line_options.auto_breadth_thread_count > 0 ) ? command_line_options.auto_breadth_thread_count : int( ( hardware_thread_concurrency == 0 ) ? 1 : hardware_thread_concurrency );

		// Configure PMR for this run.
		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		const std::uint64_t	   auto_headroom_bytes = compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		pmr_configure_for_run( avail_bytes, auto_headroom_bytes );
		memory_governor_enable_for_run( auto_headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );

		MemoryBallast memory_ballast( auto_headroom_bytes );
		if ( command_line_options.memory_ballast_enabled && auto_headroom_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[Auto] memory_ballast=on  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( auto_headroom_bytes ) << "\n";
			memory_ballast.start();
		}

		// ---------------------------------------------------------------------
		// Stage 1: "breadth" scan (small budget, many candidates)
		// ---------------------------------------------------------------------
		struct Job
		{
			std::uint32_t mask_a = 0;
			std::uint32_t mask_b = 0;
		};
		std::vector<Job> jobs;
		jobs.reserve( std::max<std::size_t>( 1, command_line_options.auto_breadth_candidate_count ) );

		std::unordered_set<std::uint64_t> seen;
		seen.reserve( std::max<std::size_t>( 64, command_line_options.auto_breadth_candidate_count * 2 ) );
		auto make_key = []( std::uint32_t a, std::uint32_t b ) -> std::uint64_t {
			return ( std::uint64_t( a ) << 32 ) ^ std::uint64_t( b );
		};
		auto try_add_job = [ & ]( std::uint32_t a, std::uint32_t b ) {
			if ( a == 0u && b == 0u )
				return;
			const std::uint64_t key = make_key( a, b );
			if ( !seen.insert( key ).second )
				return;
			if ( jobs.size() >= command_line_options.auto_breadth_candidate_count )
				return;
			jobs.push_back( Job { a, b } );
		};

		const std::uint32_t base_mask_a = command_line_options.output_branch_a_mask;
		const std::uint32_t base_mask_b = command_line_options.output_branch_b_mask;

		// Always include the user-provided start masks first.
		try_add_job( base_mask_a, base_mask_b );

		// Deterministic "close neighbors": single-bit flips in either lane.
		for ( int bit = 0; bit < 32 && jobs.size() < command_line_options.auto_breadth_candidate_count; ++bit )
		{
			const std::uint32_t m = ( 1u << bit );
			try_add_job( base_mask_a ^ m, base_mask_b );
			try_add_job( base_mask_a, base_mask_b ^ m );
			try_add_job( base_mask_a ^ m, base_mask_b ^ m );
		}

		// Byte-level toggles.
		for ( int byte = 0; byte < 4 && jobs.size() < command_line_options.auto_breadth_candidate_count; ++byte )
		{
			const std::uint32_t m = ( 0xFFu << ( 8 * byte ) );
			try_add_job( base_mask_a ^ m, base_mask_b );
			try_add_job( base_mask_a, base_mask_b ^ m );
		}

		// Nibble-level toggles (denser than single-bit).
		for ( int nib = 0; nib < 8 && jobs.size() < command_line_options.auto_breadth_candidate_count; ++nib )
		{
			const std::uint32_t m = ( 0xFu << ( 4 * nib ) );
			try_add_job( base_mask_a ^ m, base_mask_b );
			try_add_job( base_mask_a, base_mask_b ^ m );
		}

		// RNG seed for randomized neighborhood fill.
		const std::uint64_t derived_seed = ( std::uint64_t( base_mask_a ) << 32 ) ^ std::uint64_t( base_mask_b ) ^ 0x9e3779b97f4a7c15ull;
		const std::uint64_t seed = command_line_options.auto_breadth_seed_was_provided ? command_line_options.auto_breadth_seed : derived_seed;
		std::mt19937_64		rng( seed );

		// Randomized neighborhood fill: apply up to F random bit flips, biased toward small flip counts.
		const int max_flips = std::clamp( command_line_options.auto_breadth_max_bit_flips, 1, 32 );
		while ( jobs.size() < command_line_options.auto_breadth_candidate_count )
		{
			int flips = 1;
			while ( flips < max_flips && ( rng() & 3ull ) == 0ull )
				++flips;  // ~25% chance to increase each step

			std::uint32_t m_a = 0u;
			std::uint32_t m_b = 0u;
			for ( int i = 0; i < flips; ++i )
			{
				const int			which = int( rng() & 1ull );  // 0 -> A, 1 -> B
				const int			bit = int( rng() % 32ull );
				const std::uint32_t m = ( 1u << bit );
				if ( which == 0 )
					m_a ^= m;
				else
					m_b ^= m;
			}
			try_add_job( base_mask_a ^ m_a, base_mask_b ^ m_b );
		}

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
		breadth_configuration.round_count = command_line_options.round_count;
		breadth_configuration.maximum_search_nodes = std::max<std::uint64_t>( 1, command_line_options.auto_breadth_maximum_search_nodes );
		breadth_configuration.maximum_search_seconds = 0;
		breadth_configuration.maximum_round_predecessors = command_line_options.auto_breadth_maximum_round_predecessors;
		breadth_configuration.enable_verbose_output = false;

		std::cout << "[Auto][Breadth] jobs=" << jobs.size() << "  threads=" << breadth_threads << "  seed=0x" << std::hex << seed << std::dec << ( command_line_options.auto_breadth_seed_was_provided ? "" : " (derived)" ) << "\n";
		std::cout << "  per_candidate: maximum_search_nodes=" << breadth_configuration.maximum_search_nodes << "  maximum_round_predecessors=" << breadth_configuration.maximum_round_predecessors << "  maximum_addition_candidates=" << breadth_configuration.maximum_addition_candidates << "  maximum_constant_subtraction_candidates=" << breadth_configuration.maximum_constant_subtraction_candidates << "  maximum_injection_input_masks=" << breadth_configuration.maximum_injection_input_masks << "  maximum_bit_flips=" << command_line_options.auto_breadth_max_bit_flips << "  state_memoization=" << ( breadth_configuration.enable_state_memoization ? "on" : "off" ) << "\n";
		if ( mem.available_physical_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "  system_memory: available_physical_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( mem.available_physical_bytes ) << "\n";
		}
		std::cout << "\n";

		const std::uint64_t breadth_progress_sec = ( command_line_options.progress_every_seconds == 0 ) ? 1 : command_line_options.progress_every_seconds;

		std::atomic<std::size_t>   next_index { 0 };
		std::atomic<std::size_t>   completed { 0 };
		std::atomic<std::uint64_t> total_nodes { 0 };

		const int breadth_threads_clamped = std::max( 1, breadth_threads );
		// Track active job id per worker thread so auto mode can print what's being worked on.
		// (0 means idle; job ids are 1-based.)
		std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( breadth_threads_clamped ) );
		for ( auto& x : active_job_id_by_thread )
			x.store( 0, std::memory_order_relaxed );

		struct AutoCandidate
		{
			std::uint32_t					   start_mask_a = 0;
			std::uint32_t					   start_mask_b = 0;
			bool							   found = false;
			int								   best_weight = INFINITE_WEIGHT;
			std::uint64_t					   nodes = 0;
			bool							   hit_maximum_search_nodes_limit = false;
			std::uint32_t					   best_input_mask_a = 0;
			std::uint32_t					   best_input_mask_b = 0;
			std::vector<LinearTrailStepRecord> trail;  // kept only for top-K (seed deep)
		};

		auto candidate_key_better = [ & ]( const AutoCandidate& a, const AutoCandidate& b ) -> bool {
			if ( a.best_weight != b.best_weight )
				return a.best_weight < b.best_weight;
			if ( a.start_mask_a != b.start_mask_a )
				return a.start_mask_a < b.start_mask_a;
			if ( a.start_mask_b != b.start_mask_b )
				return a.start_mask_b < b.start_mask_b;
			return a.nodes < b.nodes;
		};

		std::mutex				   top_mutex;
		std::vector<AutoCandidate> top_candidates;
		top_candidates.reserve( std::max<std::size_t>( 1, command_line_options.auto_breadth_top_k ) );

		auto try_update_top_candidates = [ & ]( AutoCandidate&& c ) {
			if ( !c.found )
				return;
			std::scoped_lock lk( top_mutex );
			if ( top_candidates.size() < command_line_options.auto_breadth_top_k )
			{
				top_candidates.push_back( std::move( c ) );
			}
			else
			{
				// Find worst candidate in top_candidates.
				std::size_t worst = 0;
				for ( std::size_t i = 1; i < top_candidates.size(); ++i )
				{
					if ( candidate_key_better( top_candidates[ worst ], top_candidates[ i ] ) )
						continue;
					worst = i;
				}
				if ( candidate_key_better( c, top_candidates[ worst ] ) )
				{
					top_candidates[ worst ] = std::move( c );
				}
				else
				{
					return;
				}
			}
			// Keep top_candidates sorted for stable printing.
			std::sort( top_candidates.begin(), top_candidates.end(), [ & ]( const AutoCandidate& x, const AutoCandidate& y ) { return candidate_key_better( x, y ); } );
		};

		auto worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
				if ( job_index >= jobs.size() )
					break;

				const Job		   job = jobs[ job_index ];
				const std::size_t job_id_one_based = job_index + 1;
				active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_id_one_based, std::memory_order_relaxed );

				BestSearchResult result {};
				try
				{
					result = run_linear_best_search( job.mask_a, job.mask_b, breadth_configuration );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "auto.breadth.run" );
					result = BestSearchResult {};
				}
				total_nodes.fetch_add( result.nodes_visited, std::memory_order_relaxed );

				AutoCandidate c {};
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
					try_update_top_candidates( std::move( c ) );
				}
				completed.fetch_add( 1, std::memory_order_relaxed );
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
				const double since_last = std::chrono::duration<double>( now - last_time ).count();
				if ( since_last >= double( breadth_progress_sec ) || done == total )
				{
					const double	  elapsed = std::chrono::duration<double>( now - start_time ).count();
					const double	  window = std::max( 1e-9, since_last );
					const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
					const double	  rate = double( delta ) / window;

					AutoCandidate best_snapshot {};
					bool		  has_best = false;
					{
						std::scoped_lock lk( top_mutex );
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
						const Job& j = jobs[ job_index ];
						active.push_back( ActiveJob { i, id, j.mask_a, j.mask_b } );
					}

					IosStateGuard g( std::cout );
					std::cout << "[Auto][Breadth] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
							  << "  jobs_per_sec=" << std::setprecision( 2 ) << rate << "  elapsed_sec=" << std::setprecision( 2 ) << elapsed;
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

		std::cout << "\n[Auto][Breadth] done. total_nodes_visited=" << total_nodes.load() << "\n";
		if ( top_candidates.empty() )
		{
			std::cout << "[Auto][Breadth] FAIL: no trail found in any candidate (within breadth limits).\n";
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
		deep_configuration.round_count = command_line_options.round_count;
		// Deep stage: remove the breadth-only branching limiter.
		deep_configuration.maximum_round_predecessors = 0;
		deep_configuration.maximum_search_nodes = command_line_options.auto_deep_maximum_search_nodes;	// 0=unlimited
		deep_configuration.maximum_search_seconds = ( deep_configuration.maximum_search_nodes == 0 ) ? command_line_options.auto_max_time_seconds : 0;
		deep_configuration.enable_verbose_output = true;
		if ( command_line_options.auto_target_best_weight >= 0 )
			deep_configuration.target_best_weight = command_line_options.auto_target_best_weight;

		const AutoCandidate& selected = top_candidates.front();

		std::cout << "[Auto][Deep] selected_best #1/" << top_candidates.size() << "\n";
		print_word32_hex( "  mask_a=", selected.start_mask_a );
		std::cout << "  ";
		print_word32_hex( "mask_b=", selected.start_mask_b );
		std::cout << "\n";
		if ( deep_configuration.target_best_weight >= 0 )
		{
			std::cout << "  target_best_weight=" << deep_configuration.target_best_weight << "  (|corr| >= 2^-" << deep_configuration.target_best_weight << ")\n";
		}
		std::cout << "  deep_maximum_search_nodes=" << deep_configuration.maximum_search_nodes << ( deep_configuration.maximum_search_nodes == 0 ? " (unlimited)" : "" ) << "  deep_maximum_search_seconds=" << deep_configuration.maximum_search_seconds << "  deep_maximum_round_predecessors=" << deep_configuration.maximum_round_predecessors << ( deep_configuration.maximum_round_predecessors == 0 ? " (unlimited)" : "" ) << "\n\n";

		// Force progress printing (needed to get data during long runs).
		// Keep behavior consistent with differential auto mode:
		// - Use --progress-every-seconds when provided
		// - Force non-zero interval in auto mode to avoid "silent" long runs
		const std::uint64_t deep_progress_sec = ( command_line_options.progress_every_seconds == 0 ) ? 1 : command_line_options.progress_every_seconds;

		BestWeightCheckpointWriter checkpoint {};
		const std::string		   checkpoint_path = BestWeightCheckpointWriter::default_path( command_line_options.round_count, selected.start_mask_a, selected.start_mask_b );
		if ( checkpoint.open_append( checkpoint_path ) )
		{
			std::cout << "[Auto][Deep] checkpoint_file=" << checkpoint_path << " (append on best-weight changes)\n\n";
		}
		else
		{
			std::cout << "[Auto][Deep] WARNING: cannot open checkpoint file for writing: " << checkpoint_path << "\n\n";
		}

		BestSearchResult best_result {};
		const std::string prefix = "[Job#1@0] ";
		TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );
		try
		{
			const int								  seed_weight = selected.best_weight;
			const std::vector<LinearTrailStepRecord>* seed_trail = selected.trail.empty() ? nullptr : &selected.trail;
			best_result = run_linear_best_search( selected.start_mask_a, selected.start_mask_b, deep_configuration, true, deep_progress_sec, true, seed_weight, seed_trail, checkpoint.out ? &checkpoint : nullptr );
		}
		catch ( const std::bad_alloc& )
		{
			pmr_report_oom_once( "auto.deep.run" );
			best_result = BestSearchResult {};
		}

		if ( !best_result.found )
		{
			std::cout << "[Auto] FAIL: no trail found in deep stage.\n";
			return 2;
		}

		print_result( best_result );
		memory_governor_disable_for_run();
		return 0;
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

		std::cout << "round_count=" << search_configuration.round_count;
		if ( command_line_options.output_masks_were_generated && command_line_options.batch_seed_was_provided )
			std::cout << " (generated from --seed=0x" << std::hex << command_line_options.batch_seed << std::dec << ")";
		std::cout << "\n";
		print_word32_hex( "output_branch_a_mask=", command_line_options.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "output_branch_b_mask=", command_line_options.output_branch_b_mask );
		std::cout << "\n\n";

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
				IosStateGuard g( std::cout );
				std::cout << "  system_memory: total_physical_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_total_physical_bytes ) << "  available_physical_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_available_physical_bytes );
				if ( command_line_options.strategy_target_headroom_bytes != 0 )
				{
					std::cout << "  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_target_headroom_bytes );
				}
				if ( command_line_options.strategy_derived_budget_bytes != 0 )
				{
					std::cout << "  derived_budget_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( command_line_options.strategy_derived_budget_bytes );
				}
				std::cout << "\n";
			}
			std::cout << "  addition_weight_cap=" << search_configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_configuration.constant_subtraction_weight_cap << "  maximum_search_nodes=" << search_configuration.maximum_search_nodes << "  maximum_search_seconds=" << search_configuration.maximum_search_seconds << "  target_best_weight=" << search_configuration.target_best_weight << "  maximum_round_predecessors=" << search_configuration.maximum_round_predecessors << "  maximum_addition_candidates=" << search_configuration.maximum_addition_candidates << "  maximum_constant_subtraction_candidates=" << search_configuration.maximum_constant_subtraction_candidates << "  maximum_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  enable_state_memoization=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n\n";
		}
		else
		{
			IosStateGuard g( std::cout );
			std::cout << "[Config] maximum_search_nodes=" << search_configuration.maximum_search_nodes << "  maximum_search_seconds=" << search_configuration.maximum_search_seconds << "  target_best_weight=" << search_configuration.target_best_weight << "  maximum_round_predecessors=" << search_configuration.maximum_round_predecessors << "  addition_weight_cap=" << search_configuration.addition_weight_cap << "  constant_subtraction_weight_cap=" << search_configuration.constant_subtraction_weight_cap << "  maximum_addition_candidates=" << search_configuration.maximum_addition_candidates << "  maximum_constant_subtraction_candidates=" << search_configuration.maximum_constant_subtraction_candidates << "  maximum_injection_input_masks=" << search_configuration.maximum_injection_input_masks << "  state_memoization=" << ( search_configuration.enable_state_memoization ? "on" : "off" ) << "\n\n";
		}

		BestSearchResult best_search_result {};
		try
		{
			best_search_result = run_linear_best_search( command_line_options.output_branch_a_mask, command_line_options.output_branch_b_mask, search_configuration, true, command_line_options.progress_every_seconds );
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
		return best_search_result.found ? 0 : 2;
	}

	static int run_batch_mode( const CommandLineOptions& command_line_options )
	{
		if ( command_line_options.batch_job_count == 0 && !command_line_options.batch_job_file_was_provided )
		{
			std::cerr << "[Batch] ERROR: batch mode requested but no jobs provided.\n";
			return 1;
		}
		if ( command_line_options.round_count <= 0 )
		{
			std::cerr << "[Batch] ERROR: --round-count must be > 0\n";
			return 1;
		}

		const unsigned hw = std::thread::hardware_concurrency();
		const int	   worker_thread_count = ( command_line_options.batch_thread_count > 0 ) ? command_line_options.batch_thread_count : int( ( hw == 0 ) ? 1 : hw );
		const int	   worker_threads_clamped = std::max( 1, worker_thread_count );

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	   avail_bytes = mem.available_physical_bytes;
		std::uint64_t		   headroom_bytes = ( command_line_options.strategy_target_headroom_bytes != 0 ) ? command_line_options.strategy_target_headroom_bytes : compute_memory_headroom_bytes( avail_bytes, command_line_options.memory_headroom_mib, command_line_options.memory_headroom_mib_was_provided );
		if ( avail_bytes != 0 && headroom_bytes > avail_bytes )
			headroom_bytes = avail_bytes;

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

		struct BatchJob
		{
			int			  rounds;
			std::uint32_t output_branch_a_mask;
			std::uint32_t output_branch_b_mask;
		};
		std::vector<BatchJob> jobs;

		enum class BatchLineParseResult
		{
			Ignore,
			Ok,
			Error
		};
		auto parse_batch_job_line = [ & ]( const std::string& raw_line, int line_no, BatchJob& out ) -> BatchLineParseResult {
			std::string line = raw_line;
			// Strip comments starting with '#'
			if ( const std::size_t p = line.find( '#' ); p != std::string::npos )
				line.resize( p );
			// Normalize separators: commas -> spaces
			for ( char& ch : line )
				if ( ch == ',' )
					ch = ' ';

			// Skip empty/whitespace-only lines
			bool any_non_ws = false;
			for ( char ch : line )
			{
				if ( !( ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ) )
				{
					any_non_ws = true;
					break;
				}
			}
			if ( !any_non_ws )
				return BatchLineParseResult::Ignore;

			std::istringstream iss( line );
			std::string		   token0, token1, token2;
			if ( !( iss >> token0 >> token1 ) )
			{
				std::cerr << "[Batch] ERROR: invalid line " << line_no << " in batch file (need at least 2 tokens): " << raw_line << "\n";
				return BatchLineParseResult::Error;
			}
			if ( iss >> token2 )
			{
				// 3 tokens: rounds mask_a mask_b
				int			  round_count = 0;
				std::uint32_t mask_a = 0, mask_b = 0;
				if ( !parse_signed_integer_32( token0.c_str(), round_count ) || round_count <= 0 )
				{
					std::cerr << "[Batch] ERROR: invalid rounds on line " << line_no << ": " << raw_line << "\n";
					return BatchLineParseResult::Error;
				}
				if ( !parse_unsigned_integer_32( token1.c_str(), mask_a ) || !parse_unsigned_integer_32( token2.c_str(), mask_b ) )
				{
					std::cerr << "[Batch] ERROR: invalid output masks on line " << line_no << ": " << raw_line << "\n";
					return BatchLineParseResult::Error;
				}
				if ( mask_a == 0u && mask_b == 0u )
				{
					std::cerr << "[Batch] ERROR: (mask_a, mask_b)=(0,0) is not allowed (line " << line_no << ")\n";
					return BatchLineParseResult::Error;
				}
				out = BatchJob { round_count, mask_a, mask_b };
				return BatchLineParseResult::Ok;
			}

			// 2 tokens: mask_a mask_b (use global rounds)
			{
				std::uint32_t mask_a = 0, mask_b = 0;
				if ( !parse_unsigned_integer_32( token0.c_str(), mask_a ) || !parse_unsigned_integer_32( token1.c_str(), mask_b ) )
				{
					std::cerr << "[Batch] ERROR: invalid output masks on line " << line_no << ": " << raw_line << "\n";
					return BatchLineParseResult::Error;
				}
				if ( mask_a == 0u && mask_b == 0u )
				{
					std::cerr << "[Batch] ERROR: (mask_a, mask_b)=(0,0) is not allowed (line " << line_no << ")\n";
					return BatchLineParseResult::Error;
				}
				out = BatchJob { command_line_options.round_count, mask_a, mask_b };
				return BatchLineParseResult::Ok;
			}
		};

		if ( command_line_options.batch_job_file_was_provided )
		{
			std::ifstream f( command_line_options.batch_job_file );
			if ( !f )
			{
				std::cerr << "[Batch] ERROR: cannot open batch file: " << command_line_options.batch_job_file << "\n";
				return 1;
			}
			const std::size_t limit = ( command_line_options.batch_job_count > 0 ) ? command_line_options.batch_job_count : std::numeric_limits<std::size_t>::max();
			jobs.reserve( std::min<std::size_t>( limit, 1024 ) );
			std::string line;
			int			line_no = 0;
			while ( std::getline( f, line ) )
			{
				++line_no;
				BatchJob   job {};
				const auto parse_result = parse_batch_job_line( line, line_no, job );
				if ( parse_result == BatchLineParseResult::Error )
					return 1;
				if ( parse_result == BatchLineParseResult::Ok )
				{
					jobs.push_back( job );
					if ( jobs.size() >= limit )
						break;
				}
			}
			if ( jobs.empty() )
			{
				std::cerr << "[Batch] ERROR: batch file contains no jobs: " << command_line_options.batch_job_file << "\n";
				return 1;
			}
		}
		else
		{
			if ( !command_line_options.batch_seed_was_provided )
			{
				std::cerr << "[Batch] ERROR: batch RNG mode requires --seed.\n";
				return 1;
			}
			jobs.reserve( command_line_options.batch_job_count );
			std::mt19937_64 rng( command_line_options.batch_seed );
			for ( std::size_t job_index = 0; job_index < command_line_options.batch_job_count; ++job_index )
			{
				std::uint32_t a = 0, b = 0;
				do
				{
					a = static_cast<std::uint32_t>( rng() );
					b = static_cast<std::uint32_t>( rng() );
				} while ( a == 0u && b == 0u );
				jobs.push_back( { command_line_options.round_count, a, b } );
			}
		}

		int min_rounds = std::numeric_limits<int>::max();
		int max_rounds = 0;
		for ( const auto& j : jobs )
		{
			min_rounds = std::min( min_rounds, j.rounds );
			max_rounds = std::max( max_rounds, j.rounds );
		}

		std::cout << "[Batch] mode=linear_best_search (auto-style: breadth -> deep)\n";
		if ( min_rounds == max_rounds )
			std::cout << "  rounds=" << min_rounds;
		else
			std::cout << "  rounds_range=[" << min_rounds << "," << max_rounds << "]";
		std::cout << "  jobs=" << jobs.size() << "  threads=" << worker_threads_clamped;
		if ( command_line_options.batch_job_file_was_provided )
			std::cout << "  batch_file=" << command_line_options.batch_job_file << "\n";
		else
			std::cout << "  seed=0x" << std::hex << command_line_options.batch_seed << std::dec << "\n";

		const std::size_t deep_top_k = std::min<std::size_t>( jobs.size(), std::size_t( worker_threads_clamped ) );

		// ---------------------------------------------------------------------
		// Stage 1: breadth scan across ALL batch jobs (small budget, keep top-K)
		// ---------------------------------------------------------------------
		LinearBestSearchConfiguration breadth_configuration = command_line_options.search_configuration;
		// Breadth stage: enforce a small per-job node budget and a predecessor limiter.
		breadth_configuration.maximum_search_nodes = std::max<std::uint64_t>( 1, command_line_options.auto_breadth_maximum_search_nodes );
		breadth_configuration.maximum_search_seconds = 0;
		breadth_configuration.maximum_round_predecessors = command_line_options.auto_breadth_maximum_round_predecessors;
		breadth_configuration.enable_verbose_output = false;

		std::cout << "[Batch][Breadth] jobs=" << jobs.size() << "  threads=" << worker_threads_clamped << "  top_k=" << deep_top_k << "\n";
		{
			IosStateGuard g( std::cout );
			std::cout << "  per_job: maximum_search_nodes=" << breadth_configuration.maximum_search_nodes << "  maximum_round_predecessors=" << breadth_configuration.maximum_round_predecessors << "  maximum_addition_candidates=" << breadth_configuration.maximum_addition_candidates << "  maximum_constant_subtraction_candidates=" << breadth_configuration.maximum_constant_subtraction_candidates << "  maximum_injection_input_masks=" << breadth_configuration.maximum_injection_input_masks << "  state_memoization=" << ( breadth_configuration.enable_state_memoization ? "on" : "off" ) << "\n\n";
		}

		struct BreadthCandidate
		{
			std::size_t						   job_index = 0;  // 0-based
			std::uint32_t					   start_mask_a = 0;
			std::uint32_t					   start_mask_b = 0;
			bool							   found = false;
			int								   best_weight = INFINITE_WEIGHT;
			std::uint64_t					   nodes = 0;
			bool							   hit_maximum_search_nodes_limit = false;
			std::uint32_t					   best_input_mask_a = 0;
			std::uint32_t					   best_input_mask_b = 0;
			std::vector<LinearTrailStepRecord> trail;  // kept only for top-K
		};

		auto candidate_key_better = [ & ]( const BreadthCandidate& a, const BreadthCandidate& b ) -> bool {
			if ( a.best_weight != b.best_weight )
				return a.best_weight < b.best_weight;
			if ( a.job_index != b.job_index )
				return a.job_index < b.job_index;
			if ( a.start_mask_a != b.start_mask_a )
				return a.start_mask_a < b.start_mask_a;
			if ( a.start_mask_b != b.start_mask_b )
				return a.start_mask_b < b.start_mask_b;
			return a.nodes < b.nodes;
		};

		std::atomic<std::size_t>   next_index { 0 };
		std::atomic<std::size_t>   completed { 0 };
		std::atomic<std::uint64_t> total_nodes { 0 };

		// Track active job-id (1-based) per thread for progress printing.
		std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( worker_threads_clamped ) );
		for ( auto& x : active_job_id_by_thread )
			x.store( 0, std::memory_order_relaxed );

		std::mutex					  top_mutex;
		std::vector<BreadthCandidate> top_candidates;
		top_candidates.reserve( std::max<std::size_t>( 1, deep_top_k ) );

		auto try_update_top_candidates = [ & ]( BreadthCandidate&& c ) {
			if ( !c.found )
				return;
			std::scoped_lock lk( top_mutex );
			if ( top_candidates.size() < deep_top_k )
			{
				top_candidates.push_back( std::move( c ) );
			}
			else
			{
				std::size_t worst = 0;
				for ( std::size_t i = 1; i < top_candidates.size(); ++i )
				{
					if ( candidate_key_better( top_candidates[ worst ], top_candidates[ i ] ) )
						continue;
					worst = i;
				}
				if ( candidate_key_better( c, top_candidates[ worst ] ) )
				{
					top_candidates[ worst ] = std::move( c );
				}
				else
				{
					return;
				}
			}
			std::sort( top_candidates.begin(), top_candidates.end(), [ & ]( const BreadthCandidate& x, const BreadthCandidate& y ) { return candidate_key_better( x, y ); } );
		};

		auto breadth_worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
				if ( job_index >= jobs.size() )
					break;

				active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_index + 1, std::memory_order_relaxed );
				const BatchJob job = jobs[ job_index ];

				LinearBestSearchConfiguration cfg = breadth_configuration;
				cfg.round_count = job.rounds;

				BestSearchResult result {};
				try
				{
					result = run_linear_best_search( job.output_branch_a_mask, job.output_branch_b_mask, cfg );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "batch.breadth.run" );
					result = BestSearchResult {};
				}
				total_nodes.fetch_add( result.nodes_visited, std::memory_order_relaxed );

				if ( result.found )
				{
					BreadthCandidate c {};
					c.job_index = job_index;
					c.start_mask_a = job.output_branch_a_mask;
					c.start_mask_b = job.output_branch_b_mask;
					c.nodes = result.nodes_visited;
					c.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
					c.found = true;
					c.best_weight = result.best_weight;
					c.best_input_mask_a = result.best_input_branch_a_mask;
					c.best_input_mask_b = result.best_input_branch_b_mask;
					c.trail = std::move( result.best_linear_trail );
					try_update_top_candidates( std::move( c ) );
				}

				completed.fetch_add( 1, std::memory_order_relaxed );
			}
			active_job_id_by_thread[ std::size_t( thread_id ) ].store( 0, std::memory_order_relaxed );
		};

		const std::uint64_t breadth_progress_sec = ( command_line_options.progress_every_seconds == 0 ) ? 1 : command_line_options.progress_every_seconds;
		const auto			breadth_start_time = std::chrono::steady_clock::now();

		auto breadth_progress_monitor = [ & ]() {
			const std::size_t total = jobs.size();
			if ( total == 0 )
				return;
			if ( breadth_progress_sec == 0 )
				return;

			std::size_t last_done = 0;
			auto		last_time = breadth_start_time;
			for ( ;; )
			{
				const std::size_t done = completed.load( std::memory_order_relaxed );
				const auto		  now = std::chrono::steady_clock::now();
				memory_governor_poll_if_needed( now );

				const double since_last = std::chrono::duration<double>( now - last_time ).count();
				if ( since_last >= double( breadth_progress_sec ) || done == total )
				{
					const double	  elapsed = std::chrono::duration<double>( now - breadth_start_time ).count();
					const double	  window = std::max( 1e-9, since_last );
					const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
					const double	  rate = double( delta ) / window;

					BreadthCandidate best_snapshot {};
					bool			 has_best = false;
					{
						std::scoped_lock lk( top_mutex );
						if ( !top_candidates.empty() )
						{
							best_snapshot = top_candidates.front();
							has_best = true;
						}
					}

					struct ActiveBatchJob
					{
						std::size_t  thread_id = 0;
						std::size_t  job_id_one_based = 0;
						std::uint32_t mask_a = 0;
						std::uint32_t mask_b = 0;
					};
					std::vector<ActiveBatchJob> active;
					active.reserve( active_job_id_by_thread.size() );
					for ( std::size_t i = 0; i < active_job_id_by_thread.size(); ++i )
					{
						const std::size_t id = active_job_id_by_thread[ i ].load( std::memory_order_relaxed );
						if ( id == 0 )
							continue;
						const std::size_t job_index = id - 1;
						if ( job_index >= jobs.size() )
							continue;
						const BatchJob& j = jobs[ job_index ];
						active.push_back( ActiveBatchJob { i, id, j.output_branch_a_mask, j.output_branch_b_mask } );
					}

					IosStateGuard g( std::cout );
					std::cout << "[Batch][Breadth] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
							  << "  jobs_per_sec=" << std::setprecision( 2 ) << rate << "  elapsed_sec=" << std::setprecision( 2 ) << elapsed;
					if ( has_best )
					{
						std::cout << "  best_w=" << best_snapshot.best_weight << "  best_job=#" << ( best_snapshot.job_index + 1 );
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

		run_worker_threads_with_monitor( worker_threads_clamped, [ & ]( int t ) { breadth_worker( t ); }, breadth_progress_monitor );

		std::cout << "\n[Batch][Breadth] done. total_nodes_visited=" << total_nodes.load() << "\n";
		if ( top_candidates.empty() )
		{
			std::cout << "[Batch][Breadth] FAIL: no trail found in any job (within breadth limits).\n";
			memory_governor_disable_for_run();
			return 0;
		}

		std::cout << "[Batch][Breadth] TOP-" << top_candidates.size() << ":\n";
		for ( std::size_t i = 0; i < top_candidates.size(); ++i )
		{
			const auto&	  c = top_candidates[ i ];
			const auto&	  j = jobs[ c.job_index ];
			IosStateGuard g( std::cout );
			std::cout << "  #" << ( i + 1 ) << "  job=#" << ( c.job_index + 1 ) << "  rounds=" << j.rounds << "  best_weight=" << c.best_weight;
			std::cout << "  start=";
			print_word32_hex( "mask_a=", c.start_mask_a );
			std::cout << " ";
			print_word32_hex( "mask_b=", c.start_mask_b );
			std::cout << "  nodes=" << c.nodes << ( c.hit_maximum_search_nodes_limit ? " [HIT maximum_search_nodes limit]" : "" ) << "\n";
		}
		std::cout << "\n";

		// ---------------------------------------------------------------------
		// Stage 2: deep run on TOP-K jobs (parallel, one job per thread)
		// ---------------------------------------------------------------------
		LinearBestSearchConfiguration deep_configuration = command_line_options.search_configuration;
		deep_configuration.maximum_round_predecessors = 0;												// remove breadth-only branching limiter
		deep_configuration.maximum_search_nodes = command_line_options.auto_deep_maximum_search_nodes;	// 0=unlimited
		deep_configuration.maximum_search_seconds = ( deep_configuration.maximum_search_nodes == 0 ) ? command_line_options.auto_max_time_seconds : 0;
		deep_configuration.enable_verbose_output = true;
		if ( command_line_options.auto_target_best_weight >= 0 )
			deep_configuration.target_best_weight = command_line_options.auto_target_best_weight;

		const std::uint64_t deep_progress_sec = ( command_line_options.progress_every_seconds == 0 ) ? 1 : command_line_options.progress_every_seconds;

		std::cout << "[Batch][Deep] inputs (from breadth top candidates): count=" << top_candidates.size() << "\n";
		for ( std::size_t i = 0; i < top_candidates.size(); ++i )
		{
			const auto& current_candidate = top_candidates[ i ];
			const auto& current_job = jobs[ current_candidate.job_index ];
			std::cout << "  #" << ( i + 1 ) << "  job=#" << ( current_candidate.job_index + 1 ) << "  rounds=" << current_job.rounds << "  breadth_best_w=" << current_candidate.best_weight << "\n";
		}
		std::cout << "  deep_threads=" << top_candidates.size() << "  progress_every_seconds=" << deep_progress_sec << "\n\n";

		struct DeepResult
		{
			std::size_t		 job_index = 0;
			BestSearchResult result {};
		};
		std::vector<DeepResult> deep_results( top_candidates.size() );

		auto add_job_suffix_to_checkpoint_path = []( std::string path, std::size_t job_id_one_based ) -> std::string {
			const std::string suffix = "_job" + std::to_string( job_id_one_based );
			const std::string ext = ".log";
			if ( path.size() >= ext.size() && path.substr( path.size() - ext.size() ) == ext )
			{
				path.insert( path.size() - ext.size(), suffix );
				return path;
			}
			return path + suffix;
		};

		auto deep_worker = [ & ]( int thread_id ) {
			const BreadthCandidate& current_candidate = top_candidates[ static_cast<std::size_t>(thread_id) ];
			const BatchJob&			current_job = jobs[ current_candidate.job_index ];
			const std::size_t		job_id = current_candidate.job_index + 1;

			const std::string prefix = "[Job#" + std::to_string( job_id ) + "@" + std::to_string( thread_id ) + "] ";
			TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );

			BestWeightCheckpointWriter checkpoint {};
			const std::string		   checkpoint_path_base = BestWeightCheckpointWriter::default_path( current_job.rounds, current_candidate.start_mask_a, current_candidate.start_mask_b );
			const std::string		   checkpoint_path = add_job_suffix_to_checkpoint_path( checkpoint_path_base, job_id );
			const bool				   checkpoint_ok = checkpoint.open_append( checkpoint_path );
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				if ( checkpoint_ok )
					std::cout << "[Deep] checkpoint_file=" << checkpoint_path << " (append on best-weight changes)\n";
				else
					std::cout << "[Deep] WARNING: cannot open checkpoint file for writing: " << checkpoint_path << "\n";
			}

			deep_configuration.round_count = current_job.rounds;

			const int								  seed_weight = current_candidate.best_weight;
			const std::vector<LinearTrailStepRecord>* seed_trail = current_candidate.trail.empty() ? nullptr : &current_candidate.trail;

			BestSearchResult result {};
			try
			{
				result = run_linear_best_search( current_job.output_branch_a_mask, current_job.output_branch_b_mask, deep_configuration, true, deep_progress_sec, true, seed_weight, seed_trail, checkpoint_ok ? &checkpoint : nullptr );
			}
			catch ( const std::bad_alloc& )
			{
				pmr_report_oom_once( "batch.deep.run" );
				result = BestSearchResult {};
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				std::cout << "[Deep] ERROR: out of memory; aborted this job.\n";
			}

			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				if ( result.found )
				{
					std::cout << "[Deep] done. best_weight=" << result.best_weight << "  nodes=" << result.nodes_visited << "\n";
				}
				else
				{
					std::cout << "[Deep] done. found=0  nodes=" << result.nodes_visited;
					if ( result.hit_maximum_search_nodes )
						std::cout << " [HIT maximum_search_nodes]";
					if ( result.hit_time_limit )
						std::cout << " [HIT maximum_search_seconds]";
					std::cout << "\n";
				}
			}

			deep_results[ static_cast<std::size_t>(thread_id) ] = DeepResult { current_candidate.job_index, std::move( result ) };
		};

		TwilightDream::runtime_component::run_worker_threads( int( top_candidates.size() ), deep_worker );

		// Select global best from deep results.
		bool			 global_best_found = false;
		std::size_t		 global_best_job_index = 0;
		BestSearchResult global_best_result {};
		for ( const auto& dr : deep_results )
		{
			if ( !dr.result.found )
				continue;
			if ( !global_best_found || dr.result.best_weight < global_best_result.best_weight )
			{
				global_best_found = true;
				global_best_job_index = dr.job_index;
				global_best_result = dr.result;
			}
		}

		if ( !global_best_found || !global_best_result.found || global_best_result.best_linear_trail.empty() )
		{
			std::cout << "\n[Batch][Deep] FAIL: no trail found in any deep job.\n";
			memory_governor_disable_for_run();
			return 0;
		}

		const BatchJob& best_job = jobs[ global_best_job_index ];
		std::cout << "\n[Batch] BEST (from deep): weight=" << global_best_result.best_weight << "  approx_abs_correlation=" << std::scientific << std::setprecision( 10 ) << weight_to_abs_correlation( global_best_result.best_weight ) << std::defaultfloat << "\n";
		std::cout << "  best_job=#" << ( global_best_job_index + 1 ) << "  best_rounds=" << best_job.rounds << "\n";
		print_word32_hex( "  best_output_branch_a_mask=", best_job.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "  best_output_branch_b_mask=", best_job.output_branch_b_mask );
		std::cout << "\n\n";
		print_result( global_best_result );
		memory_governor_disable_for_run();
		return 0;
	}

	static void print_result( const BestSearchResult& result )
	{
		if ( !result.found )
		{
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
		return run_arx_operator_self_test();

	print_banner_and_mode( command_line_options );

	if ( command_line_options.frontend_mode == CommandLineOptions::FrontendMode::Auto )
	{
		return run_auto_mode( command_line_options );
	}

	if ( command_line_options.batch_job_count > 0 || command_line_options.batch_job_file_was_provided )
	{
		return run_batch_mode( command_line_options );
	}
	return run_single_mode( command_line_options );
}