#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "auto_search_frame/detail/best_search_shared_core.hpp"
#include "auto_search_frame/test_neoalzette_linear_best_search.hpp"
#include "auto_search_frame/hull_growth_common.hpp"
#include "auto_search_frame/linear_hull_batch_export.hpp"

namespace
{
	using namespace TwilightDream::auto_search_linear;
	using TwilightDream::hull_callback_aggregator::GrowthStopPolicy;
	using TwilightDream::hull_callback_aggregator::LinearBatchBreadthDeepOrchestratorConfig;
	using TwilightDream::hull_callback_aggregator::LinearBatchBreadthDeepOrchestratorResult;
	using TwilightDream::hull_callback_aggregator::LinearBatchJob;
	using TwilightDream::hull_callback_aggregator::LinearBatchSelectionCheckpointStage;
	using TwilightDream::hull_callback_aggregator::LinearBatchTopCandidate;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullJobSummary;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineOptions;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineCheckpointState;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineResult;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullRunSummary;
	using TwilightDream::hull_callback_aggregator::LinearBatchSourceSelectionCheckpointState;
	using TwilightDream::hull_callback_aggregator::LinearCallbackHullAggregator;
	using TwilightDream::hull_callback_aggregator::LinearGrowthShellRow;
	using TwilightDream::hull_callback_aggregator::LinearHullSourceContext;
	using TwilightDream::hull_callback_aggregator::StoredTrailPolicy;
	using TwilightDream::hull_callback_aggregator::generate_linear_batch_jobs_from_seed;
	using TwilightDream::hull_callback_aggregator::load_linear_batch_jobs_from_file;
	using TwilightDream::hull_callback_aggregator::run_linear_batch_breadth_then_deep;
	using TwilightDream::hull_callback_aggregator::run_linear_batch_strict_hull_pipeline;
	using TwilightDream::hull_callback_aggregator::summarize_linear_batch_hull_job;
	using TwilightDream::hull_callback_aggregator::summarize_linear_batch_hull_run;
	using TwilightDream::runtime_component::append_timestamp_to_artifact_path;
	using TwilightDream::runtime_component::bytes_to_gibibytes;
	using TwilightDream::runtime_component::compute_memory_headroom_bytes;
	using TwilightDream::runtime_component::governor_poll_system_memory_once;
	using TwilightDream::runtime_component::IosStateGuard;
	using TwilightDream::runtime_component::MemoryBallast;
	using TwilightDream::runtime_component::memory_governor_disable_for_run;
	using TwilightDream::runtime_component::memory_governor_enable_for_run;
	using TwilightDream::runtime_component::memory_governor_set_poll_fn;
	using TwilightDream::runtime_component::pmr_configure_for_run;
	using TwilightDream::runtime_component::print_word32_hex;
	using TwilightDream::runtime_component::query_system_memory_info;
	using TwilightDream::runtime_component::SystemMemoryInfo;

	enum class SelfTestScope : std::uint8_t
	{
		All = 0,
		Synthetic = 1,
		Smoke = 2,
		Batch = 3,
		BatchExplicit = 4,
		BatchCombined = 5,
		BatchMultiTrajectory = 6,
		BatchReject = 7,
		BatchResume = 8
	};

	static const char* selftest_scope_to_string( SelfTestScope scope ) noexcept
	{
		switch ( scope )
		{
		case SelfTestScope::All: return "all";
		case SelfTestScope::Synthetic: return "synthetic";
		case SelfTestScope::Smoke: return "smoke";
		case SelfTestScope::Batch: return "batch";
		case SelfTestScope::BatchExplicit: return "batch-explicit";
		case SelfTestScope::BatchCombined: return "batch-combined";
		case SelfTestScope::BatchMultiTrajectory: return "batch-multitrajectory";
		case SelfTestScope::BatchReject: return "batch-reject";
		case SelfTestScope::BatchResume: return "batch-resume";
		default: return "unknown";
		}
	}

	static bool parse_selftest_scope_value( const char* text, SelfTestScope& out ) noexcept
	{
		if ( !text )
			return false;
		const std::string value( text );
		if ( value == "all" )
		{
			out = SelfTestScope::All;
			return true;
		}
		if ( value == "synthetic" || value == "aggregator" )
		{
			out = SelfTestScope::Synthetic;
			return true;
		}
		if ( value == "smoke" || value == "wrapper" )
		{
			out = SelfTestScope::Smoke;
			return true;
		}
		if ( value == "batch" )
		{
			out = SelfTestScope::Batch;
			return true;
		}
		if ( value == "batch-explicit" || value == "batch_explicit" )
		{
			out = SelfTestScope::BatchExplicit;
			return true;
		}
		if ( value == "batch-combined" || value == "batch_combined" )
		{
			out = SelfTestScope::BatchCombined;
			return true;
		}
		if ( value == "batch-multitrajectory" || value == "batch_multitrajectory" || value == "multitrajectory" )
		{
			out = SelfTestScope::BatchMultiTrajectory;
			return true;
		}
		if ( value == "batch-reject" || value == "batch_reject" || value == "reject" )
		{
			out = SelfTestScope::BatchReject;
			return true;
		}
		if ( value == "batch-resume" || value == "batch_resume" || value == "resume" )
		{
			out = SelfTestScope::BatchResume;
			return true;
		}
		return false;
	}

	struct CommandLineOptions
	{
		int			  round_count = 1;
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		bool		  masks_were_provided = false;
		std::uint64_t maximum_search_nodes = 0;
		std::uint64_t maximum_search_seconds = 0;
		int			  addition_weight_cap = 31;
		int			  constant_subtraction_weight_cap = 32;
		bool		  enable_weight_sliced_clat = true;
		std::size_t	  weight_sliced_clat_max_candidates = 0;
		std::size_t	  maximum_injection_input_masks = 0;
		std::size_t	  maximum_round_predecessors = 0;
		bool		  enable_state_memoization = true;
		int			  collect_weight_cap = -1;
		int			  collect_weight_window = 0;
		bool		  collect_weight_cap_was_provided = false;
		int			  shell_start = -1;
		std::uint64_t shell_count = 0;
		bool		  growth_mode = false;
		int			  growth_shell_start = -1;
		std::uint64_t growth_max_shells = 0;
		double		  growth_stop_relative_delta = -1.0;
		double		  growth_stop_absolute_delta = -1.0;
		std::uint64_t maximum_collected_trails = 0;
		std::uint64_t maximum_stored_trails = 0;
		StoredTrailPolicy stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		std::uint64_t progress_every_seconds = 0;
		// Batch breadth→deep (same orchestration as former test_neoalzette_linear_best_search --batch).
		std::size_t	  batch_job_count = 0;
		int			  batch_thread_count = 0;
		std::uint64_t batch_seed = 0;
		bool		  batch_seed_was_provided = false;
		std::string	  batch_job_file {};
		bool		  batch_job_file_was_provided = false;
		std::string	  batch_resume_checkpoint_path {};
		std::string	  batch_checkpoint_out_path {};
		std::string	  batch_runtime_log_path {};
		std::uint64_t auto_breadth_maximum_search_nodes = ( std::numeric_limits<std::uint32_t>::max() >> 12 );
		std::uint64_t auto_deep_maximum_search_nodes = 0;
		std::uint64_t auto_max_time_seconds = 0;
		int			  auto_target_best_weight = -1;
		bool		  memory_ballast_enabled = false;
		std::uint64_t memory_headroom_mib = 0;
		bool		  memory_headroom_mib_was_provided = false;
		std::uint64_t strategy_target_headroom_bytes = 0;
		std::string	  best_search_history_log_path {};
		std::string	  best_search_runtime_log_path {};
		std::string	  best_search_resume_checkpoint_path {};
		std::string	  best_search_checkpoint_out_path {};
		bool		  best_search_checkpoint_out_was_provided = false;
		std::uint64_t best_search_checkpoint_every_seconds = 1800;
		bool		  best_search_checkpoint_every_seconds_was_provided = false;
		bool		  selftest = false;
		SelfTestScope selftest_scope = SelfTestScope::All;
		bool		  verbose = false;
		bool		  show_help = false;
	};

	static int run_linear_batch_mode( const CommandLineOptions& options );

	static void print_usage( const char* executable_name )
	{
		if ( !executable_name )
			executable_name = "test_neoalzette_linear_hull_wrapper.exe";

		std::cout
			<< "Usage:\n"
			<< "  " << executable_name << " --round-count R --output-branch-a-mask MASK_A --output-branch-b-mask MASK_B [options]\n"
			<< "  " << executable_name << " R MASK_A MASK_B [options]\n"
			<< "\n"
			<< "Options:\n"
			<< "  --round-count R\n"
			<< "  --output-branch-a-mask MASK_A  Alias: --mask-a\n"
			<< "  --output-branch-b-mask MASK_B  Alias: --mask-b\n"
			<< "  --maximum-search-nodes N       Alias: --maxnodes\n"
			<< "  --maximum-search-seconds S     Alias: --maxsec\n"
			<< "  --addition-weight-cap N        Alias: --add\n"
			<< "  --constant-subtraction-weight-cap N Alias: --subtract\n"
			<< "  --enable-z-shell              Enable the z-shell based Weight-Sliced cLAT accelerator.\n"
			<< "  --disable-z-shell             Disable the z-shell based Weight-Sliced cLAT accelerator.\n"
			<< "  --z-shell-max-candidates N    Set the z-shell based Weight-Sliced cLAT candidate cap (0=unlimited).\n"
			<< "  --maximum-injection-input-masks N Alias: --injection-candidates\n"
			<< "  --maximum-round-predecessors N Alias: --max-round-predecessors\n"
			<< "  --collect-weight-cap W\n"
			<< "  --collect-weight-window D\n"
			<< "  --shell-start W\n"
			<< "  --shell-count N\n"
			<< "  --growth-mode\n"
			<< "  --growth-shell-start W\n"
			<< "  --growth-max-shells N\n"
			<< "  --growth-stop-relative-delta X\n"
			<< "  --growth-stop-absolute-delta X\n"
			<< "  --maximum-collected-trails N Alias: --maxcollected\n"
			<< "  --maximum-stored-trails N Alias: --store-trails\n"
			<< "  --stored-trail-policy arrival|strongest\n"
			<< "  --disable-state-memoization Alias: --nomemo\n"
			<< "  --progress-every-seconds S Alias: --progress-sec\n"
			<< "  --best-search-history-log PATH\n"
			<< "  --best-search-runtime-log PATH\n"
			<< "  --best-search-resume PATH\n"
			<< "  --best-search-checkpoint-out PATH\n"
			<< "  --best-search-checkpoint-every-seconds S\n"
			<< "      Archive interval for the embedded best-search checkpoint stream.\n"
			<< "      The latest resumable checkpoint is still maintained internally by the watchdog.\n"
			<< "  --selftest\n"
			<< "  --selftest-scope all|synthetic|smoke|batch|batch-explicit|batch-combined|batch-multitrajectory|batch-reject|batch-resume\n"
			<< "  --verbose\n"
			<< "  --help, -h\n"
			<< "\n"
			<< "Default flow:\n"
			<< "  1. In best_weight_plus_window mode, the strict runtime first requires a certified best-weight reference.\n"
			<< "  2. In explicit_cap mode, the strict runtime skips best-search and directly runs the collector.\n"
			<< "  3. The wrapper only formats the kernel status / reasons / shell summaries.\n"
			<< "\n"
			<< "If --collect-weight-cap is omitted, the wrapper uses certified best_weight + collect_weight_window.\n"
			<< "With the default window 0, this gives a strict best-weight-shell linear hull collector.\n"
			<< "\n"
			<< "Growth mode reruns the strict collector shell-by-shell and reports marginal/cumulative gain.\n"
			<< "If --growth-max-shells is omitted, growth mode defaults to 8 shells.\n"
			<< "\n"
			<< "Batch mode (breadth→deep over many jobs; ignores positional masks):\n"
			<< "  --batch-job-count N | --batch N   Random output-mask jobs (requires --seed)\n"
			<< "  --batch-file PATH                 Job list file: each non-empty line is \"mask_a mask_b\" or\n"
			<< "                                    \"round_count mask_a mask_b\" (hex/decimal; commas allowed; # starts comment)\n"
			<< "  --batch-resume PATH               Resume from a batch checkpoint (selection or selected-source strict-hull stage)\n"
			<< "  --batch-checkpoint-out PATH       Write selected-source strict-hull batch checkpoints after each completed job\n"
			<< "  --batch-runtime-log PATH          Write batch runtime event log (selection + strict-hull resume events)\n"
			<< "  --thread-count T | --threads T    Worker threads (0=auto)\n"
			<< "  --seed S                          RNG seed (required for --batch-job-count)\n"
			<< "  note=best_weight_plus_window now runs batch breadth->deep multi-trajectory best-search first,\n"
			<< "       then launches strict collector/hull only for the selected top sources.\n"
			<< "  note=explicit_cap still skips best-search and runs the strict collector directly per job.\n"
			<< "  note=batch checkpoint/resume works at completed-job granularity after source selection;\n"
			<< "       an in-flight strict-hull job may restart from its job boundary after resume.\n"
			<< "  --collect-weight-cap W            Optional explicit cap per job (otherwise uses strict best_weight + window)\n"
			<< "  --collect-weight-window D         Extra shells beyond strict best_weight per job\n"
			<< "  --auto-breadth-maxnodes N         Per-job breadth budget for the multi-trajectory front stage\n"
			<< "  --auto-deep-maxnodes N            Per-candidate deep budget for seed improvement (0=unlimited)\n"
			<< "  --auto-max-time-seconds S         Per-candidate deep time budget (0=unlimited)\n"
			<< "  --auto-target-best-weight W       Optional early-stop target for the deep stage\n"
			<< "  --progress-every-jobs N           Alias: --progress\n"
			<< "  --memory-ballast  --memory-headroom-mib M  --strategy-target-headroom-bytes B\n";
	}

	static bool parse_signed_integer_32( const char* text, int& value_out )
	{
		if ( !text )
			return false;
		char* end = nullptr;
		const long value = std::strtol( text, &end, 0 );
		if ( !end || *end != '\0' )
			return false;
		value_out = static_cast<int>( value );
		return true;
	}

	static bool parse_unsigned_integer_32( const char* text, std::uint32_t& value_out )
	{
		if ( !text )
			return false;
		char* end = nullptr;
		const unsigned long long value = std::strtoull( text, &end, 0 );
		if ( !end || *end != '\0' || value > 0xFFFFFFFFull )
			return false;
		value_out = static_cast<std::uint32_t>( value );
		return true;
	}

	static bool parse_unsigned_integer_64( const char* text, std::uint64_t& value_out )
	{
		if ( !text )
			return false;
		char* end = nullptr;
		const unsigned long long value = std::strtoull( text, &end, 0 );
		if ( !end || *end != '\0' )
			return false;
		value_out = static_cast<std::uint64_t>( value );
		return true;
	}

	static bool parse_double_value( const char* text, double& value_out )
	{
		if ( !text )
			return false;
		char* end = nullptr;
		const double value = std::strtod( text, &end );
		if ( !end || *end != '\0' || !std::isfinite( value ) )
			return false;
		value_out = value;
		return true;
	}

	static bool parse_stored_trail_policy_value( const char* text, StoredTrailPolicy& value_out )
	{
		if ( !text )
			return false;
		return TwilightDream::hull_callback_aggregator::parse_stored_trail_policy( text, value_out );
	}

	static bool parse_command_line( int argc, char** argv, CommandLineOptions& out )
	{
		int positional_index = 0;
		for ( int i = 1; i < argc; ++i )
		{
			const std::string arg = argv[ i ] ? std::string( argv[ i ] ) : std::string();
			if ( arg == "--help" || arg == "-h" )
			{
				out.show_help = true;
				return true;
			}
			if ( arg == "--selftest" )
			{
				out.selftest = true;
				continue;
			}
			if ( arg == "--selftest-scope" && i + 1 < argc )
			{
				if ( !parse_selftest_scope_value( argv[ ++i ], out.selftest_scope ) )
					return false;
				out.selftest = true;
				continue;
			}
			if ( arg == "--round-count" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.round_count ) )
					return false;
				continue;
			}
			if ( ( arg == "--output-branch-a-mask" || arg == "--mask-a" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_32( argv[ ++i ], out.output_branch_a_mask ) )
					return false;
				out.masks_were_provided = true;
				continue;
			}
			if ( ( arg == "--output-branch-b-mask" || arg == "--mask-b" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_32( argv[ ++i ], out.output_branch_b_mask ) )
					return false;
				out.masks_were_provided = true;
				continue;
			}
			if ( ( arg == "--maximum-search-nodes" || arg == "--maxnodes" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.maximum_search_nodes ) )
					return false;
				continue;
			}
			if ( ( arg == "--maximum-search-seconds" || arg == "--maxsec" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.maximum_search_seconds ) )
					return false;
				continue;
			}
			if ( ( arg == "--addition-weight-cap" || arg == "--add" ) && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.addition_weight_cap ) )
					return false;
				continue;
			}
			if ( ( arg == "--constant-subtraction-weight-cap" || arg == "--subtract" ) && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.constant_subtraction_weight_cap ) )
					return false;
				continue;
			}
			if ( arg == "--enable-z-shell" )
			{
				out.enable_weight_sliced_clat = true;
				continue;
			}
			if ( arg == "--disable-z-shell" )
			{
				out.enable_weight_sliced_clat = false;
				continue;
			}
			if ( arg == "--z-shell-max-candidates" && i + 1 < argc )
			{
				std::uint64_t value = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], value ) )
					return false;
				out.weight_sliced_clat_max_candidates = static_cast<std::size_t>( value );
				continue;
			}
			if ( ( arg == "--maximum-injection-input-masks" || arg == "--injection-candidates" ) && i + 1 < argc )
			{
				std::uint64_t value = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], value ) )
					return false;
				out.maximum_injection_input_masks = static_cast<std::size_t>( value );
				continue;
			}
			if ( ( arg == "--maximum-round-predecessors" || arg == "--max-round-predecessors" ) && i + 1 < argc )
			{
				std::uint64_t value = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], value ) )
					return false;
				out.maximum_round_predecessors = static_cast<std::size_t>( value );
				continue;
			}
			if ( arg == "--collect-weight-cap" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.collect_weight_cap ) )
					return false;
				out.collect_weight_cap_was_provided = true;
				continue;
			}
			if ( arg == "--collect-weight-window" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.collect_weight_window ) )
					return false;
				continue;
			}
			if ( arg == "--shell-start" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.shell_start ) )
					return false;
				continue;
			}
			if ( arg == "--shell-count" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.shell_count ) )
					return false;
				continue;
			}
			if ( arg == "--growth-mode" )
			{
				out.growth_mode = true;
				continue;
			}
			if ( arg == "--growth-shell-start" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.growth_shell_start ) )
					return false;
				continue;
			}
			if ( arg == "--growth-max-shells" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.growth_max_shells ) )
					return false;
				continue;
			}
			if ( arg == "--growth-stop-relative-delta" && i + 1 < argc )
			{
				if ( !parse_double_value( argv[ ++i ], out.growth_stop_relative_delta ) )
					return false;
				continue;
			}
			if ( arg == "--growth-stop-absolute-delta" && i + 1 < argc )
			{
				if ( !parse_double_value( argv[ ++i ], out.growth_stop_absolute_delta ) )
					return false;
				continue;
			}
			if ( ( arg == "--maximum-collected-trails" || arg == "--maxcollected" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.maximum_collected_trails ) )
					return false;
				continue;
			}
			if ( ( arg == "--maximum-stored-trails" || arg == "--store-trails" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.maximum_stored_trails ) )
					return false;
				continue;
			}
			if ( arg == "--stored-trail-policy" && i + 1 < argc )
			{
				if ( !parse_stored_trail_policy_value( argv[ ++i ], out.stored_trail_policy ) )
					return false;
				continue;
			}
			if ( arg == "--disable-state-memoization" || arg == "--nomemo" )
			{
				out.enable_state_memoization = false;
				continue;
			}
			if ( ( arg == "--progress-every-seconds" || arg == "--progress-sec" ) && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.progress_every_seconds ) )
					return false;
				continue;
			}
			if ( arg == "--best-search-history-log" && i + 1 < argc )
			{
				out.best_search_history_log_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--best-search-runtime-log" && i + 1 < argc )
			{
				out.best_search_runtime_log_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--best-search-resume" && i + 1 < argc )
			{
				out.best_search_resume_checkpoint_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--best-search-checkpoint-out" && i + 1 < argc )
			{
				out.best_search_checkpoint_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.best_search_checkpoint_out_was_provided = true;
				continue;
			}
			if ( arg == "--best-search-checkpoint-every-seconds" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.best_search_checkpoint_every_seconds ) )
					return false;
				out.best_search_checkpoint_every_seconds_was_provided = true;
				continue;
			}
			if ( ( arg == "--batch-job-count" || arg == "--batch" ) && i + 1 < argc )
			{
				std::uint64_t n = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], n ) )
					return false;
				out.batch_job_count = static_cast<std::size_t>( n );
				continue;
			}
			if ( arg == "--batch-file" && i + 1 < argc )
			{
				out.batch_job_file = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.batch_job_file_was_provided = true;
				continue;
			}
			if ( arg == "--batch-resume" && i + 1 < argc )
			{
				out.batch_resume_checkpoint_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--batch-checkpoint-out" && i + 1 < argc )
			{
				out.batch_checkpoint_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--batch-runtime-log" && i + 1 < argc )
			{
				out.batch_runtime_log_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( ( arg == "--thread-count" || arg == "--threads" ) && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.batch_thread_count ) )
					return false;
				continue;
			}
			if ( arg == "--seed" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.batch_seed ) )
					return false;
				out.batch_seed_was_provided = true;
				continue;
			}
			if ( arg == "--auto-breadth-maxnodes" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.auto_breadth_maximum_search_nodes ) )
					return false;
				continue;
			}
			if ( arg == "--auto-deep-maxnodes" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.auto_deep_maximum_search_nodes ) )
					return false;
				continue;
			}
			if ( arg == "--auto-max-time-seconds" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.auto_max_time_seconds ) )
					return false;
				continue;
			}
			if ( arg == "--auto-target-best-weight" && i + 1 < argc )
			{
				if ( !parse_signed_integer_32( argv[ ++i ], out.auto_target_best_weight ) )
					return false;
				continue;
			}
			if ( arg == "--memory-ballast" )
			{
				out.memory_ballast_enabled = true;
				continue;
			}
			if ( arg == "--memory-headroom-mib" && i + 1 < argc )
			{
				std::uint64_t v = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], v ) )
					return false;
				out.memory_headroom_mib = v;
				out.memory_headroom_mib_was_provided = true;
				continue;
			}
			if ( arg == "--strategy-target-headroom-bytes" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.strategy_target_headroom_bytes ) )
					return false;
				continue;
			}
			if ( arg == "--verbose" )
			{
				out.verbose = true;
				continue;
			}
			if ( !arg.empty() && arg[ 0 ] == '-' )
				return false;

			switch ( positional_index )
			{
			case 0:
				if ( !parse_signed_integer_32( argv[ i ], out.round_count ) )
					return false;
				break;
			case 1:
				if ( !parse_unsigned_integer_32( argv[ i ], out.output_branch_a_mask ) )
					return false;
				out.masks_were_provided = true;
				break;
			case 2:
				if ( !parse_unsigned_integer_32( argv[ i ], out.output_branch_b_mask ) )
					return false;
				out.masks_were_provided = true;
				break;
			default:
				return false;
			}
			++positional_index;
		}

		return true;
	}

	static long double correlation_to_weight( long double correlation )
	{
		return TwilightDream::hull_callback_aggregator::correlation_to_weight( correlation );
	}

	struct LinearBestSearchRuntimeArtifacts
	{
		BestWeightHistory	  history_log {};
		RuntimeEventLog	  runtime_log {};
		BinaryCheckpointManager binary_checkpoint {};
		bool				  history_log_enabled = false;
		bool				  runtime_log_enabled = false;
		bool				  binary_checkpoint_enabled = false;
	};

	struct LinearBatchResumeFingerprint
	{
		std::uint64_t hash = 0;
		std::uint64_t completed_jobs = 0;
		std::uint64_t payload_count = 0;
		std::uint64_t payload_digest = 0;
		std::uint64_t source_hull_count = 0;
		std::uint64_t endpoint_hull_count = 0;
		std::uint64_t collected_trail_count = 0;
	};

	static void mix_long_double_into_fingerprint(
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
		long double value ) noexcept
	{
		fp.mix_bytes( &value, sizeof( value ) );
	}

	static void mix_linear_batch_job_summary_into_fingerprint(
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
		const LinearBatchHullJobSummary& summary ) noexcept
	{
		fp.mix_u64( static_cast<std::uint64_t>( summary.job_index ) );
		fp.mix_i32( summary.job.rounds );
		fp.mix_u32( summary.job.output_branch_a_mask );
		fp.mix_u32( summary.job.output_branch_b_mask );
		fp.mix_bool( summary.collected );
		fp.mix_bool( summary.best_search_executed );
		fp.mix_bool( summary.best_weight_certified );
		fp.mix_i32( summary.best_weight );
		fp.mix_i32( summary.collect_weight_cap );
		fp.mix_bool( summary.exact_within_collect_weight_cap );
		fp.mix_enum( summary.strict_runtime_rejection_reason );
		fp.mix_enum( summary.exactness_rejection_reason );
		fp.mix_u64( summary.best_search_nodes_visited );
		fp.mix_u64( summary.hull_nodes_visited );
		fp.mix_u64( summary.collected_trails );
		fp.mix_u64( static_cast<std::uint64_t>( summary.endpoint_hull_count ) );
		mix_long_double_into_fingerprint( fp, summary.global_abs_correlation_mass );
		mix_long_double_into_fingerprint( fp, summary.strongest_trail_abs_correlation );
		fp.mix_bool( summary.hit_any_limit );
	}

	static void mix_linear_batch_top_candidate_into_fingerprint(
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
		const LinearBatchTopCandidate& candidate ) noexcept
	{
		fp.mix_u64( static_cast<std::uint64_t>( candidate.job_index ) );
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

	static LinearBatchResumeFingerprint compute_linear_selection_resume_fingerprint(
		const LinearBatchSourceSelectionCheckpointState& state ) noexcept
	{
		LinearBatchResumeFingerprint fingerprint {};
		fingerprint.completed_jobs = static_cast<std::uint64_t>(
			std::count_if( state.completed_job_flags.begin(), state.completed_job_flags.end(), []( std::uint8_t flag ) { return flag != 0u; } ) );
		fingerprint.payload_count = static_cast<std::uint64_t>( state.top_candidates.size() );

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder payload_fp {};
		for ( const auto& candidate : state.top_candidates )
			mix_linear_batch_top_candidate_into_fingerprint( payload_fp, candidate );
		fingerprint.payload_digest = payload_fp.finish();

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
		fp.mix_enum( state.stage );
		fp.mix_i32( state.config.breadth_configuration.round_count );
		fp.mix_u64( state.breadth_total_nodes );
		fp.mix_u64( static_cast<std::uint64_t>( state.jobs.size() ) );
		for ( const auto& job : state.jobs )
		{
			fp.mix_i32( job.rounds );
			fp.mix_u32( job.output_branch_a_mask );
			fp.mix_u32( job.output_branch_b_mask );
		}
		fp.mix_u64( static_cast<std::uint64_t>( state.completed_job_flags.size() ) );
		for ( const std::uint8_t flag : state.completed_job_flags )
			fp.mix_u8( flag );
		fp.mix_u64( fingerprint.payload_count );
		fp.mix_u64( fingerprint.payload_digest );
		fingerprint.hash = fp.finish();
		return fingerprint;
	}

	static LinearBatchResumeFingerprint compute_linear_hull_resume_fingerprint(
		const LinearBatchHullPipelineCheckpointState& state ) noexcept
	{
		LinearBatchResumeFingerprint fingerprint {};
		fingerprint.completed_jobs = static_cast<std::uint64_t>(
			std::count_if( state.completed_job_flags.begin(), state.completed_job_flags.end(), []( std::uint8_t flag ) { return flag != 0u; } ) );
		fingerprint.payload_count = static_cast<std::uint64_t>( state.summaries.size() );
		fingerprint.source_hull_count = static_cast<std::uint64_t>( state.combined_callback_aggregator.source_hulls.size() );
		fingerprint.endpoint_hull_count = static_cast<std::uint64_t>( state.combined_callback_aggregator.endpoint_hulls.size() );
		fingerprint.collected_trail_count = state.combined_callback_aggregator.collected_trail_count;

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder payload_fp {};
		for ( const auto& summary : state.summaries )
			mix_linear_batch_job_summary_into_fingerprint( payload_fp, summary );
		fingerprint.payload_digest = payload_fp.finish();

		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
		fp.mix_i32( state.base_search_configuration.round_count );
		fp.mix_enum( state.runtime_options_snapshot.collect_cap_mode );
		fp.mix_i32( state.runtime_options_snapshot.explicit_collect_weight_cap );
		fp.mix_i32( state.runtime_options_snapshot.collect_weight_window );
		fp.mix_u64( state.runtime_options_snapshot.maximum_collected_trails );
		fp.mix_bool( state.enable_combined_source_aggregator );
		fp.mix_enum( state.stored_trail_policy );
		fp.mix_u64( static_cast<std::uint64_t>( state.maximum_stored_trails ) );
		fp.mix_u64( static_cast<std::uint64_t>( state.jobs.size() ) );
		for ( const auto& job : state.jobs )
		{
			fp.mix_i32( job.rounds );
			fp.mix_u32( job.output_branch_a_mask );
			fp.mix_u32( job.output_branch_b_mask );
		}
		fp.mix_u64( static_cast<std::uint64_t>( state.completed_job_flags.size() ) );
		for ( const std::uint8_t flag : state.completed_job_flags )
			fp.mix_u8( flag );
		fp.mix_u64( fingerprint.payload_count );
		fp.mix_u64( fingerprint.payload_digest );
		fp.mix_u64( fingerprint.source_hull_count );
		fp.mix_u64( fingerprint.endpoint_hull_count );
		fp.mix_u64( fingerprint.collected_trail_count );
		fingerprint.hash = fp.finish();
		return fingerprint;
	}

	static void write_linear_batch_resume_fingerprint_fields(
		std::ostream& out,
		const LinearBatchResumeFingerprint& fingerprint,
		const char* prefix = "batch_resume_fingerprint_" )
	{
		const char* key_prefix = prefix ? prefix : "batch_resume_fingerprint_";
		out << key_prefix << "hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";
		out << key_prefix << "completed_jobs=" << fingerprint.completed_jobs << "\n";
		out << key_prefix << "payload_count=" << fingerprint.payload_count << "\n";
		out << key_prefix << "payload_digest=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.payload_digest ) << "\n";
		out << key_prefix << "source_hull_count=" << fingerprint.source_hull_count << "\n";
		out << key_prefix << "endpoint_hull_count=" << fingerprint.endpoint_hull_count << "\n";
		out << key_prefix << "collected_trail_count=" << fingerprint.collected_trail_count << "\n";
	}

	static std::string default_linear_batch_runtime_log_path( const std::string& checkpoint_path ) noexcept
	{
		if ( !checkpoint_path.empty() )
			return checkpoint_path + ".runtime.log";
		return RuntimeEventLog::default_path( "linear_hull_batch" );
	}

	static bool linear_best_search_phase_will_execute( const CommandLineOptions& options ) noexcept;

	static void timestamp_linear_best_search_output_paths( CommandLineOptions& options )
	{
		if ( !options.best_search_history_log_path.empty() )
			options.best_search_history_log_path = append_timestamp_to_artifact_path( options.best_search_history_log_path );
		if ( !options.best_search_runtime_log_path.empty() )
			options.best_search_runtime_log_path = append_timestamp_to_artifact_path( options.best_search_runtime_log_path );
		if ( options.best_search_checkpoint_out_was_provided && !options.best_search_checkpoint_out_path.empty() )
			options.best_search_checkpoint_out_path = append_timestamp_to_artifact_path( options.best_search_checkpoint_out_path );
	}

	static void ensure_default_linear_best_search_output_paths( CommandLineOptions& options )
	{
		if ( !linear_best_search_phase_will_execute( options ) )
			return;

		std::string resume_history_log_path {};
		std::string resume_runtime_log_path {};
		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			LinearBestSearchContext  load_context {};
			LinearCheckpointLoadResult load {};
			if ( read_linear_checkpoint( options.best_search_resume_checkpoint_path, load, load_context.memoization ) )
			{
				resume_history_log_path = load.history_log_path;
				resume_runtime_log_path = load.runtime_log_path;
				if ( options.maximum_search_nodes == 0 && options.maximum_search_seconds == 0 )
				{
					options.maximum_search_nodes = load.runtime_maximum_search_nodes;
					options.maximum_search_seconds = load.runtime_maximum_search_seconds;
				}
				if ( !options.best_search_checkpoint_every_seconds_was_provided )
					options.best_search_checkpoint_every_seconds = load.runtime_checkpoint_every_seconds;
			}
		}

		if ( options.best_search_history_log_path.empty() )
		{
			options.best_search_history_log_path =
				!resume_history_log_path.empty() ?
					resume_history_log_path :
					BestWeightHistory::default_path( options.round_count, options.output_branch_a_mask, options.output_branch_b_mask );
		}
		if ( options.best_search_runtime_log_path.empty() )
		{
			options.best_search_runtime_log_path =
				!resume_runtime_log_path.empty() ?
					resume_runtime_log_path :
					default_runtime_log_path( options.round_count, options.output_branch_a_mask, options.output_branch_b_mask );
		}
	}

	static bool linear_best_search_controls_requested( const CommandLineOptions& options ) noexcept
	{
		return
			!options.best_search_history_log_path.empty() ||
			!options.best_search_runtime_log_path.empty() ||
			!options.best_search_resume_checkpoint_path.empty() ||
			options.best_search_checkpoint_out_was_provided ||
			options.best_search_checkpoint_every_seconds_was_provided;
	}

	static bool linear_best_search_phase_will_execute( const CommandLineOptions& options ) noexcept
	{
		if ( options.growth_mode )
			return options.growth_shell_start < 0;
		return !options.collect_weight_cap_was_provided;
	}

	static bool validate_linear_best_search_runtime_controls( const CommandLineOptions& options )
	{
		if ( !linear_best_search_phase_will_execute( options ) && linear_best_search_controls_requested( options ) )
		{
			std::cerr << "[HullWrapper][Linear] ERROR: best-search-only options require a runtime path that actually executes best-search.\n";
			return false;
		}
		if ( options.best_search_checkpoint_every_seconds_was_provided &&
			 !options.best_search_checkpoint_out_was_provided &&
			 options.best_search_resume_checkpoint_path.empty() )
		{
			std::cerr << "[HullWrapper][Linear] ERROR: --best-search-checkpoint-every-seconds requires --best-search-checkpoint-out or --best-search-resume.\n";
			return false;
		}
		return true;
	}

	static bool configure_linear_best_search_runtime_artifacts( const CommandLineOptions& options, LinearBestSearchRuntimeArtifacts& artifacts )
	{
		if ( !options.best_search_history_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					artifacts.history_log,
					options.best_search_history_log_path,
					"[HullWrapper][Linear] ERROR: cannot open best-search history log: " ) )
				return false;
			artifacts.history_log_enabled = true;
		}
		if ( !options.best_search_runtime_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					artifacts.runtime_log,
					options.best_search_runtime_log_path,
					"[HullWrapper][Linear] ERROR: cannot open best-search runtime log: " ) )
				return false;
			artifacts.runtime_log_enabled = true;
		}

		if ( options.best_search_checkpoint_out_was_provided || !options.best_search_resume_checkpoint_path.empty() )
		{
			artifacts.binary_checkpoint.path =
				options.best_search_checkpoint_out_was_provided
					? options.best_search_checkpoint_out_path
					: options.best_search_resume_checkpoint_path;
			if ( artifacts.binary_checkpoint.path.empty() )
			{
				std::cerr << "[HullWrapper][Linear] ERROR: best-search checkpoint path must be non-empty.\n";
				return false;
			}
			artifacts.binary_checkpoint.every_seconds = options.best_search_checkpoint_every_seconds;
			artifacts.binary_checkpoint_enabled = true;
		}
		return true;
	}

	static bool linear_best_search_binary_checkpoint_requested( const CommandLineOptions& options ) noexcept
	{
		return options.best_search_checkpoint_out_was_provided || !options.best_search_resume_checkpoint_path.empty();
	}

	static const std::string& linear_effective_best_search_checkpoint_path( const CommandLineOptions& options ) noexcept
	{
		if ( options.best_search_checkpoint_out_was_provided && !options.best_search_checkpoint_out_path.empty() )
			return options.best_search_checkpoint_out_path;
		return options.best_search_resume_checkpoint_path;
	}

	static void print_linear_best_search_artifact_status( const CommandLineOptions& options, bool best_search_executed )
	{
		std::cout << "  best_search_executed=" << ( best_search_executed ? 1 : 0 ) << "\n";
		std::cout << "  best_search_history_log_enabled=" << ( options.best_search_history_log_path.empty() ? 0 : 1 ) << "\n";
		if ( !options.best_search_history_log_path.empty() )
			std::cout << "  best_search_history_log_path=" << options.best_search_history_log_path << "\n";
		std::cout << "  best_search_runtime_log_enabled=" << ( options.best_search_runtime_log_path.empty() ? 0 : 1 ) << "\n";
		if ( !options.best_search_runtime_log_path.empty() )
			std::cout << "  best_search_runtime_log_path=" << options.best_search_runtime_log_path << "\n";

		const bool binary_checkpoint_enabled = linear_best_search_binary_checkpoint_requested( options );
		std::cout << "  best_search_binary_checkpoint_enabled=" << ( binary_checkpoint_enabled ? 1 : 0 ) << "\n";
		if ( !options.best_search_resume_checkpoint_path.empty() )
			std::cout << "  best_search_resume_checkpoint_path=" << options.best_search_resume_checkpoint_path << "\n";
		if ( options.best_search_checkpoint_out_was_provided && !options.best_search_checkpoint_out_path.empty() )
			std::cout << "  best_search_checkpoint_out_path=" << options.best_search_checkpoint_out_path << "\n";
		if ( binary_checkpoint_enabled )
		{
			std::cout << "  best_search_checkpoint_effective_path=" << linear_effective_best_search_checkpoint_path( options ) << "\n";
			std::cout << "  best_search_checkpoint_every_seconds=" << options.best_search_checkpoint_every_seconds << "\n";
		}
	}

	static LinearStrictHullRuntimeOptions make_linear_runtime_options( const CommandLineOptions& options, LinearBestSearchRuntimeArtifacts* artifacts = nullptr, LinearCallbackHullAggregator* aggregator = nullptr )
	{
		LinearStrictHullRuntimeOptions runtime_options {};
		runtime_options.maximum_collected_trails = options.maximum_collected_trails;
		runtime_options.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
		runtime_options.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
		runtime_options.runtime_controls.progress_every_seconds = options.progress_every_seconds;
		runtime_options.runtime_controls.checkpoint_every_seconds = options.best_search_checkpoint_every_seconds;
			runtime_options.best_search_resume_checkpoint_path = options.best_search_resume_checkpoint_path;
			if ( artifacts )
			{
				runtime_options.best_search_history_log = artifacts->history_log_enabled ? &artifacts->history_log : nullptr;
				runtime_options.best_search_runtime_log = artifacts->runtime_log_enabled ? &artifacts->runtime_log : nullptr;
				runtime_options.best_search_binary_checkpoint = artifacts->binary_checkpoint_enabled ? &artifacts->binary_checkpoint : nullptr;
			}
		if ( aggregator )
			runtime_options.on_trail = aggregator->make_callback();
		if ( options.collect_weight_cap_was_provided )
		{
			runtime_options.collect_cap_mode = HullCollectCapMode::ExplicitCap;
			runtime_options.explicit_collect_weight_cap = std::max( 0, options.collect_weight_cap );
		}
		else
		{
			runtime_options.collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
			runtime_options.collect_weight_window = std::max( 0, options.collect_weight_window );
		}
		return runtime_options;
	}

	static LinearStrictHullRuntimeOptions make_linear_explicit_cap_runtime_options( const CommandLineOptions& options, LinearBestSearchRuntimeArtifacts* artifacts, int collect_weight_cap, LinearCallbackHullAggregator* aggregator = nullptr )
	{
		LinearStrictHullRuntimeOptions runtime_options {};
		runtime_options.collect_cap_mode = HullCollectCapMode::ExplicitCap;
		runtime_options.explicit_collect_weight_cap = std::max( 0, collect_weight_cap );
		runtime_options.maximum_collected_trails = options.maximum_collected_trails;
		runtime_options.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
		runtime_options.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
		runtime_options.runtime_controls.progress_every_seconds = options.progress_every_seconds;
		runtime_options.runtime_controls.checkpoint_every_seconds = options.best_search_checkpoint_every_seconds;
			runtime_options.best_search_resume_checkpoint_path = options.best_search_resume_checkpoint_path;
			if ( artifacts )
			{
				runtime_options.best_search_history_log = artifacts->history_log_enabled ? &artifacts->history_log : nullptr;
				runtime_options.best_search_runtime_log = artifacts->runtime_log_enabled ? &artifacts->runtime_log : nullptr;
				runtime_options.best_search_binary_checkpoint = artifacts->binary_checkpoint_enabled ? &artifacts->binary_checkpoint : nullptr;
			}
		if ( aggregator )
			runtime_options.on_trail = aggregator->make_callback();
		return runtime_options;
	}

	using LinearGrowthRunResult =
		TwilightDream::hull_callback_aggregator::OuterGrowthDriverResult<
			LinearGrowthShellRow,
			MatsuiSearchRunLinearResult,
			StrictCertificationFailureReason>;

	static GrowthStopPolicy make_growth_stop_policy( const CommandLineOptions& options ) noexcept
	{
		GrowthStopPolicy policy {};
		policy.absolute_delta = options.growth_stop_absolute_delta;
		policy.relative_delta = options.growth_stop_relative_delta;
		return policy;
	}

	static LinearGrowthRunResult run_linear_growth_mode(
		const CommandLineOptions& options,
		const LinearBestSearchConfiguration& config,
		LinearBestSearchRuntimeArtifacts* artifacts )
	{
		const GrowthStopPolicy growth_stop_policy = make_growth_stop_policy( options );
		struct LinearGrowthRuntimeState
		{
			LinearStrictHullRuntimeResult runtime_result {};
			LinearCallbackHullAggregator  callback_aggregator {};
		};

		return TwilightDream::hull_callback_aggregator::run_outer_growth_driver<
			LinearGrowthShellRow,
			MatsuiSearchRunLinearResult,
			StrictCertificationFailureReason,
			LinearGrowthRuntimeState>(
			options.growth_shell_start < 0,
			options.growth_shell_start,
			options.growth_max_shells,
			growth_stop_policy,
			[&]() {
				LinearGrowthRuntimeState state {};
				LinearStrictHullRuntimeOptions start_options = make_linear_runtime_options( options, artifacts, &state.callback_aggregator );
				start_options.collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
				start_options.collect_weight_window = 0;
				state.runtime_result =
					run_linear_strict_hull_runtime(
						options.output_branch_a_mask,
						options.output_branch_b_mask,
						config,
						start_options,
						options.verbose,
						false );
				return state;
			},
			[&]( int shell_weight ) {
				LinearGrowthRuntimeState state {};
				state.runtime_result =
					run_linear_strict_hull_runtime(
						options.output_branch_a_mask,
						options.output_branch_b_mask,
						config,
						make_linear_explicit_cap_runtime_options( options, artifacts, shell_weight, &state.callback_aggregator ),
						false,
						false );
				return state;
			},
			[]( const LinearGrowthRuntimeState& state ) {
				return state.runtime_result.collected;
			},
			[]( const LinearGrowthRuntimeState& state ) {
				return state.runtime_result.strict_runtime_rejection_reason;
			},
			[]( const LinearGrowthRuntimeState& state ) {
				return state.runtime_result.best_search_result;
			},
			[]( const LinearGrowthRuntimeState& state ) {
				return state.runtime_result.collect_weight_cap;
			},
			[]( int shell_weight, const LinearGrowthRuntimeState& state ) {
				return TwilightDream::hull_callback_aggregator::make_linear_growth_shell_row(
					shell_weight,
					state.callback_aggregator,
					state.runtime_result.aggregation_result );
			},
			[]( const LinearGrowthShellRow& row ) {
				return row.hit_any_limit;
			},
			[]( const LinearGrowthShellRow& row ) {
				return row.shell_abs_correlation_mass;
			},
			[]( const LinearGrowthShellRow& row ) {
				return row.cumulative_abs_correlation_mass;
			} );
	}

	static void print_linear_growth_result(
		const LinearGrowthRunResult& growth_result,
		const CommandLineOptions& options )
	{
		if ( growth_result.rows.empty() )
		{
			std::cout << "[HullWrapper][Linear][Growth] no shells were scanned.\n";
			return;
		}

		std::cout << "[HullWrapper][Linear][Growth]\n";
		std::cout << "  wrapper_mode=strict_shell_growth_driver\n";
		std::cout << "  best_weight_reference_used=" << ( growth_result.used_best_weight_reference ? 1 : 0 ) << "\n";
		print_linear_best_search_artifact_status( options, growth_result.used_best_weight_reference );
		if ( growth_result.best_result.found && growth_result.best_result.best_weight < INFINITE_WEIGHT )
			std::cout << "  best_search_best_weight=" << growth_result.best_result.best_weight << "\n";
		if ( growth_result.used_best_weight_reference )
		{
			std::cout << "  best_weight_certified=" << ( growth_result.best_result.best_weight_certified ? 1 : 0 ) << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( growth_result.best_result ) ) << "\n";
			std::cout << "  best_search_strict_rejection_reason=" << strict_certification_failure_reason_to_string( growth_result.best_result.strict_rejection_reason ) << "\n";
		}
		std::cout << "  scanned_shells=" << growth_result.rows.size() << "\n";
		if ( options.growth_stop_absolute_delta >= 0.0 )
			std::cout << "  growth_stop_absolute_delta=" << options.growth_stop_absolute_delta << "\n";
		if ( options.growth_stop_relative_delta >= 0.0 )
			std::cout << "  growth_stop_relative_delta=" << options.growth_stop_relative_delta << "\n";
		std::cout << "  note=each row is a strict collector run with collect_weight_cap = shell_weight\n";
		std::cout << "  note=linear growth rows below are global collected-trail diagnostics across all reached input-mask endpoints\n";
		std::cout << "  note=use non-growth mode to inspect strict source-hull and endpoint-hull summaries for fixed-endpoint runs\n";
		std::cout << "  growth_summary_begin\n";
		for ( const auto& row : growth_result.rows )
		{
			const long double relative_delta =
				TwilightDream::hull_callback_aggregator::compute_growth_relative_delta(
					row.shell_abs_correlation_mass,
					row.cumulative_abs_correlation_mass );
			std::cout << "    shell_weight=" << row.shell_weight
					  << "  trails=" << row.trail_count
					  << "  shell_subset_signed=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( row.shell_signed_correlation )
					  << "  shell_subset_abs_mass=" << static_cast<double>( row.shell_abs_correlation_mass )
					  << "  cumulative_subset_signed=" << static_cast<double>( row.cumulative_signed_correlation )
					  << "  cumulative_subset_abs_mass=" << static_cast<double>( row.cumulative_abs_correlation_mass )
					  << "  relative_delta=" << static_cast<double>( relative_delta )
					  << "  exact=" << ( row.exact_within_collect_weight_cap ? 1 : 0 )
					  << "  exactness_reason=" << strict_certification_failure_reason_to_string( row.exactness_rejection_reason )
					  << "  hit_limit=" << ( row.hit_any_limit ? 1 : 0 )
					  << std::defaultfloat << "\n";
		}
		std::cout << "  growth_summary_end\n";
	}

	static void print_linear_strict_runtime_rejection( const LinearStrictHullRuntimeResult& runtime_result, const CommandLineOptions& options, bool growth_mode )
	{
		std::cout << ( growth_mode ? "[HullWrapper][Linear][Growth] strict runtime rejected.\n" : "[HullWrapper][Linear] strict runtime rejected.\n" );
		std::cout << "  strict_runtime_rejection_reason=" << strict_certification_failure_reason_to_string( runtime_result.strict_runtime_rejection_reason ) << "\n";
		print_linear_best_search_artifact_status( options, runtime_result.best_search_executed );
		if ( runtime_result.best_search_executed )
		{
			if ( runtime_result.best_search_result.found && runtime_result.best_search_result.best_weight < INFINITE_WEIGHT )
				std::cout << "  best_search_best_weight=" << runtime_result.best_search_result.best_weight << "\n";
			std::cout << "  best_weight_certified=" << ( runtime_result.best_search_result.best_weight_certified ? 1 : 0 ) << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( runtime_result.best_search_result ) ) << "\n";
			std::cout << "  best_search_strict_rejection_reason=" << strict_certification_failure_reason_to_string( runtime_result.best_search_result.strict_rejection_reason ) << "\n";
		}
	}

	static void print_linear_growth_rejection( const LinearGrowthRunResult& growth_result, const CommandLineOptions& options )
	{
		std::cout << "[HullWrapper][Linear][Growth] strict runtime rejected.\n";
		std::cout << "  strict_runtime_rejection_reason=" << strict_certification_failure_reason_to_string( growth_result.rejection_reason ) << "\n";
		std::cout << "  best_weight_reference_used=" << ( growth_result.used_best_weight_reference ? 1 : 0 ) << "\n";
		print_linear_best_search_artifact_status( options, growth_result.used_best_weight_reference );
		if ( growth_result.best_result.found && growth_result.best_result.best_weight < INFINITE_WEIGHT )
			std::cout << "  best_search_best_weight=" << growth_result.best_result.best_weight << "\n";
		if ( growth_result.used_best_weight_reference )
		{
			std::cout << "  best_weight_certified=" << ( growth_result.best_result.best_weight_certified ? 1 : 0 ) << "\n";
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( growth_result.best_result ) ) << "\n";
			std::cout << "  best_search_strict_rejection_reason=" << strict_certification_failure_reason_to_string( growth_result.best_result.strict_rejection_reason ) << "\n";
		}
	}

	static void print_linear_source_hull_summary( const LinearCallbackHullAggregator& callback_aggregator )
	{
		if ( callback_aggregator.source_hulls.empty() )
			return;

		std::size_t source_order = 0;
		std::cout << "  source_hull_summary_begin\n";
		for ( const auto& [ source_key, source_summary ] : callback_aggregator.source_hulls )
		{
			const long double source_signed_weight = correlation_to_weight( source_summary.aggregate_signed_correlation );
			const long double source_abs_mass_weight = correlation_to_weight( source_summary.aggregate_abs_correlation_mass );
			const long double strongest_source_abs = std::fabsl( source_summary.strongest_trail_signed_correlation );
			const long double strongest_source_weight = correlation_to_weight( strongest_source_abs );
			std::cout << "    source_order=" << source_order
					  << "  source_index=" << source_key.source_index
					  << "  source_rounds=" << source_summary.source_rounds
					  << "  source_trails=" << source_summary.trail_count
					  << "  source_endpoint_hulls=" << source_summary.endpoint_hulls.size()
					  << "  source_collected=" << ( source_summary.runtime_collected ? 1 : 0 )
					  << "  source_exact=" << ( source_summary.exact_within_collect_weight_cap ? 1 : 0 )
					  << "  source_best_weight_certified=" << ( source_summary.best_weight_certified ? 1 : 0 )
					  << "  source_best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( source_summary.best_weight_certified, source_summary.strict_runtime_rejection_reason ) )
					  << "  source_collect_weight_cap=" << source_summary.collect_weight_cap
					  << "  source_best_search_nodes=" << source_summary.best_search_nodes_visited
					  << "  source_hull_nodes=" << source_summary.hull_nodes_visited
					  << "  source_exact_signed_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( source_summary.aggregate_signed_correlation )
					  << "  source_abs_correlation_mass=" << static_cast<double>( source_summary.aggregate_abs_correlation_mass )
					  << "  source_signed_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( source_signed_weight )
					  << "  source_abs_mass_weight_equivalent=" << static_cast<double>( source_abs_mass_weight )
					  << "  strongest_source_trail_signed_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( source_summary.strongest_trail_signed_correlation )
					  << "  strongest_source_trail_abs_correlation=" << static_cast<double>( strongest_source_abs )
					  << "  strongest_source_trail_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( strongest_source_weight )
					  << "  strongest_source_trail_steps=" << source_summary.strongest_trail.size()
					  << "  source_strict_rejection_reason=" << strict_certification_failure_reason_to_string( source_summary.strict_runtime_rejection_reason )
					  << "  source_exactness_reason=" << strict_certification_failure_reason_to_string( source_summary.exactness_rejection_reason )
					  << std::defaultfloat << "\n";
			std::cout << "      source_output_branch_a_mask=";
			print_word32_hex( "", source_summary.source_output_branch_a_mask );
			std::cout << "  source_output_branch_b_mask=";
			print_word32_hex( "", source_summary.source_output_branch_b_mask );
			std::cout << "  strongest_input_branch_a_mask=";
			print_word32_hex( "", source_summary.strongest_input_branch_a_mask );
			std::cout << "  strongest_input_branch_b_mask=";
			print_word32_hex( "", source_summary.strongest_input_branch_b_mask );
			std::cout << "\n";
			++source_order;
		}
		std::cout << "  source_hull_summary_end\n";
	}

	static void print_linear_endpoint_hull_summary( const LinearCallbackHullAggregator& callback_aggregator )
	{
		if ( callback_aggregator.endpoint_hulls.empty() )
			return;

		std::size_t endpoint_index = 0;
		std::cout << "  endpoint_hull_summary_begin\n";
		for ( const auto& [ endpoint_key, endpoint_summary ] : callback_aggregator.endpoint_hulls )
		{
			const long double endpoint_signed_weight = correlation_to_weight( endpoint_summary.aggregate_signed_correlation );
			const long double endpoint_abs_mass_weight = correlation_to_weight( endpoint_summary.aggregate_abs_correlation_mass );
			const long double endpoint_strongest_abs = std::fabsl( endpoint_summary.strongest_trail_signed_correlation );
			const long double endpoint_strongest_weight = correlation_to_weight( endpoint_strongest_abs );
			std::cout << "    endpoint_index=" << endpoint_index
					  << "  endpoint_trails=" << endpoint_summary.trail_count
					  << "  endpoint_exact_signed_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( endpoint_summary.aggregate_signed_correlation )
					  << "  endpoint_abs_correlation_mass=" << static_cast<double>( endpoint_summary.aggregate_abs_correlation_mass )
					  << "  endpoint_cancellation_ratio=" << static_cast<double>( TwilightDream::hull_callback_aggregator::linear_endpoint_cancellation_ratio( endpoint_summary ) )
					  << "  endpoint_signed_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( endpoint_signed_weight )
					  << "  endpoint_abs_mass_weight_equivalent=" << static_cast<double>( endpoint_abs_mass_weight )
					  << "  strongest_endpoint_trail_signed_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( endpoint_summary.strongest_trail_signed_correlation )
					  << "  strongest_endpoint_trail_abs_correlation=" << static_cast<double>( endpoint_strongest_abs )
					  << "  strongest_endpoint_trail_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( endpoint_strongest_weight )
					  << "  strongest_endpoint_trail_steps=" << endpoint_summary.strongest_trail.size()
					  << std::defaultfloat << "\n";
			std::cout << "      input_branch_a_mask=";
			print_word32_hex( "", endpoint_key.input_branch_a_mask );
			std::cout << "  input_branch_b_mask=";
			print_word32_hex( "", endpoint_key.input_branch_b_mask );
			std::cout << "\n";
			++endpoint_index;
		}
		std::cout << "  endpoint_hull_summary_end\n";
	}

	static void print_linear_global_shell_diagnostics(
		const LinearCallbackHullAggregator& callback_aggregator,
		const CommandLineOptions& options )
	{
		if ( callback_aggregator.shell_summaries.empty() )
			return;

		long double cumulative_signed_correlation = 0.0L;
		long double cumulative_abs_correlation_mass = 0.0L;
		std::cout << "  global_shell_diagnostic_begin\n";
		for ( const auto& [ shell_weight, shell_summary ] : callback_aggregator.shell_summaries )
		{
			cumulative_signed_correlation += shell_summary.aggregate_signed_correlation;
			cumulative_abs_correlation_mass += shell_summary.aggregate_abs_correlation_mass;
			if ( !LinearCallbackHullAggregator::is_selected_shell( shell_weight, options.shell_start, options.shell_count ) )
				continue;
			std::cout << "    shell_weight=" << shell_weight
					  << "  global_shell_trails=" << shell_summary.trail_count
					  << "  global_shell_signed_correlation_mass=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( shell_summary.aggregate_signed_correlation )
					  << "  global_shell_abs_correlation_mass=" << static_cast<double>( shell_summary.aggregate_abs_correlation_mass )
					  << "  global_cumulative_signed_correlation_mass=" << static_cast<double>( cumulative_signed_correlation )
					  << "  global_cumulative_abs_correlation_mass=" << static_cast<double>( cumulative_abs_correlation_mass )
					  << "  strongest_shell_signed_correlation=" << static_cast<double>( shell_summary.strongest_trail_signed_correlation )
					  << "  strongest_shell_abs_correlation=" << static_cast<double>( std::fabsl( shell_summary.strongest_trail_signed_correlation ) )
					  << "  strongest_shell_steps=" << shell_summary.strongest_trail.size()
					  << std::defaultfloat << "\n";
			std::cout << "      strongest_shell_input_branch_a_mask=";
			print_word32_hex( "", shell_summary.strongest_input_branch_a_mask );
			std::cout << "  strongest_shell_input_branch_b_mask=";
			print_word32_hex( "", shell_summary.strongest_input_branch_b_mask );
			std::cout << "\n";
		}
		std::cout << "  global_shell_diagnostic_end\n";
	}

	static void print_result(
		const LinearStrictHullRuntimeResult& runtime_result,
		const LinearCallbackHullAggregator& callback_aggregator,
		const CommandLineOptions& options )
	{
		const MatsuiSearchRunLinearResult& best_result = runtime_result.best_search_result;
		const LinearHullAggregationResult& aggregation_result = runtime_result.aggregation_result;
		if ( runtime_result.collect_weight_cap >= INFINITE_WEIGHT || !callback_aggregator.found_any )
		{
			std::cout << "[HullWrapper][Linear] no trail collected within limits.\n";
			print_linear_best_search_artifact_status( options, runtime_result.best_search_executed );
			std::cout << "  collect_weight_cap=" << runtime_result.collect_weight_cap << "\n";
			std::cout << "  exact_within_collect_weight_cap=" << ( aggregation_result.exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
			std::cout << "  exactness_rejection_reason=" << strict_certification_failure_reason_to_string( aggregation_result.exactness_rejection_reason ) << "\n";
			std::cout << "  nodes_visited=" << aggregation_result.nodes_visited;
			if ( aggregation_result.hit_maximum_search_nodes )
				std::cout << "  [HIT maximum_search_nodes]";
			if ( aggregation_result.hit_time_limit )
				std::cout << "  [HIT maximum_search_seconds]";
			if ( aggregation_result.hit_collection_limit )
				std::cout << "  [HIT maximum_collected_trails]";
			std::cout << "\n";
			return;
		}

		const long double strongest_abs_correlation = std::fabsl( callback_aggregator.strongest_trail_signed_correlation );
		const long double collected_subset_signed_weight = correlation_to_weight( callback_aggregator.aggregate_signed_correlation );
		const long double collected_subset_abs_mass_weight = correlation_to_weight( callback_aggregator.aggregate_abs_correlation_mass );
		const long double strongest_weight = correlation_to_weight( strongest_abs_correlation );

		std::cout << "[HullWrapper][Linear]\n";
		std::cout << "  wrapper_mode=strict_hull_runtime_formatter\n";
		std::cout << "  best_weight_reference_used=" << ( runtime_result.used_best_weight_reference ? 1 : 0 ) << "\n";
		print_linear_best_search_artifact_status( options, runtime_result.best_search_executed );
		if ( runtime_result.used_best_weight_reference )
			std::cout << "  collect_weight_window=" << std::max( 0, options.collect_weight_window ) << "\n";
		std::cout << "  collect_weight_cap=" << runtime_result.collect_weight_cap << "\n";
		if ( runtime_result.best_search_executed && best_result.found && best_result.best_weight < INFINITE_WEIGHT )
			std::cout << "  best_search_best_weight=" << best_result.best_weight << "\n";
		std::cout << "  best_weight_certified=" << ( aggregation_result.best_weight_certified ? 1 : 0 ) << "\n";
		if ( runtime_result.best_search_executed )
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( best_result ) ) << "\n";
		if ( runtime_result.best_search_executed )
			std::cout << "  best_search_strict_rejection_reason=" << strict_certification_failure_reason_to_string( best_result.strict_rejection_reason ) << "\n";
		std::cout << "  endpoint_hull_count=" << callback_aggregator.endpoint_hulls.size() << "\n";
		std::cout << "  global_collected_trail_mass_available=1\n";
		std::cout << "  strict_endpoint_hulls_within_collect_weight_cap=" << ( aggregation_result.exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
		std::cout << "  exact_within_collect_weight_cap=" << ( aggregation_result.exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
		std::cout << "  exactness_rejection_reason=" << strict_certification_failure_reason_to_string( aggregation_result.exactness_rejection_reason ) << "\n";
		std::cout << "  collected_trails=" << callback_aggregator.collected_trail_count << "\n";
		std::cout << "  stored_trails=" << callback_aggregator.collected_trails.size() << "\n";
		std::cout << "  dropped_stored_trails=" << callback_aggregator.dropped_stored_trail_count << "\n";
		std::cout << "  stored_trail_policy=" << TwilightDream::hull_callback_aggregator::stored_trail_policy_to_string( callback_aggregator.stored_trail_policy ) << "\n";
		std::cout << "  global_strongest_trail_signed_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( callback_aggregator.strongest_trail_signed_correlation ) << std::defaultfloat << "\n";
		std::cout << "  global_strongest_trail_abs_correlation=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( strongest_abs_correlation ) << std::defaultfloat << "\n";
		std::cout << "  global_strongest_trail_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( strongest_weight ) << std::defaultfloat << "\n";
		std::cout << "  global_collected_trail_signed_correlation_mass=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( callback_aggregator.aggregate_signed_correlation ) << std::defaultfloat << "\n";
		std::cout << "  global_collected_trail_abs_correlation_mass=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( callback_aggregator.aggregate_abs_correlation_mass ) << std::defaultfloat << "\n";
		std::cout << "  global_collected_trail_signed_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( collected_subset_signed_weight ) << std::defaultfloat << "\n";
		std::cout << "  global_collected_trail_abs_mass_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( collected_subset_abs_mass_weight ) << std::defaultfloat << "\n";
		std::cout << "  note=endpoint_hull_summary rows are the strict objects: fixed output mask -> fixed input mask signed hull sums within collect_weight_cap\n";
		std::cout << "  note=if strict_endpoint_hulls_within_collect_weight_cap=1, every endpoint row below is exact for all trails with total_weight <= collect_weight_cap\n";
		std::cout << "  note=global collected-trail masses mix multiple input-mask endpoints and are diagnostic only, not endpoint hull values\n";
		std::cout << "  note=missing linear trails may cancel or reinforce the final signed correlation, so any partial global sum is not a strict lower bound on |corr|\n";
		std::cout << "  note=exactness and rejection reasons are reported directly by the search kernel\n";
		std::cout << "  note=global per-shell diagnostics and strongest trails are tracked by the outer callback aggregator\n";
		if ( callback_aggregator.maximum_stored_trails != 0 )
			std::cout << "  note=outer aggregator stored up to " << callback_aggregator.maximum_stored_trails << " detailed trails from the callback stream\n";
		if ( !runtime_result.used_best_weight_reference )
			std::cout << "  note=explicit collect_weight_cap skips best-search by design\n";

		std::cout << "  global_strongest_input_branch_a_mask=";
		print_word32_hex( "", callback_aggregator.strongest_input_branch_a_mask );
		std::cout << "  global_strongest_input_branch_b_mask=";
		print_word32_hex( "", callback_aggregator.strongest_input_branch_b_mask );
		std::cout << "\n";

		std::cout << "  nodes_visited=" << aggregation_result.nodes_visited;
		if ( aggregation_result.hit_maximum_search_nodes )
			std::cout << "  [HIT maximum_search_nodes]";
		if ( aggregation_result.hit_time_limit )
			std::cout << "  [HIT maximum_search_seconds]";
		if ( aggregation_result.hit_collection_limit )
			std::cout << "  [HIT maximum_collected_trails]";
		std::cout << "\n";

		print_linear_source_hull_summary( callback_aggregator );
		print_linear_endpoint_hull_summary( callback_aggregator );
		print_linear_global_shell_diagnostics( callback_aggregator, options );

		for ( const auto& step : callback_aggregator.strongest_trail )
		{
			std::cout << "  R" << step.round_index << "  round_weight=" << step.round_weight << "\n";
			std::cout << "    ";
			print_word32_hex( "output_branch_a_mask=", step.output_branch_a_mask );
			std::cout << "  ";
			print_word32_hex( "output_branch_b_mask=", step.output_branch_b_mask );
			std::cout << "\n";
			std::cout << "    ";
			print_word32_hex( "input_branch_a_mask=", step.input_branch_a_mask );
			std::cout << "  ";
			print_word32_hex( "input_branch_b_mask=", step.input_branch_b_mask );
			std::cout << "\n";
		}
	}

	static void enforce_strict_linear_hull_branch_caps( const CommandLineOptions& options, LinearBestSearchConfiguration& config )
	{
		using TwilightDream::runtime_component::IosStateGuard;
		// This executable always runs SearchMode::Strict; branch / z-shell caps must be unlimited (0).
		if ( options.weight_sliced_clat_max_candidates != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[HullWrapper][Linear][SearchMode::Strict] weight_sliced_clat_max_candidates "
					  << options.weight_sliced_clat_max_candidates << " -> 0 (full z-shell / strict cLAT)\n";
		}
		if ( options.maximum_injection_input_masks != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[HullWrapper][Linear][SearchMode::Strict] maximum_injection_input_masks "
					  << options.maximum_injection_input_masks << " -> 0\n";
		}
		if ( options.maximum_round_predecessors != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[HullWrapper][Linear][SearchMode::Strict] maximum_round_predecessors "
					  << options.maximum_round_predecessors << " -> 0\n";
		}
		config.weight_sliced_clat_max_candidates = 0;
		config.maximum_injection_input_masks = 0;
		config.maximum_round_predecessors = 0;
	}

	static LinearBestSearchConfiguration make_linear_base_search_configuration( const CommandLineOptions& options )
	{
		LinearBestSearchConfiguration config {};
		config.search_mode = SearchMode::Strict;
		config.round_count = options.round_count;
		config.addition_weight_cap = options.addition_weight_cap;
		config.constant_subtraction_weight_cap = options.constant_subtraction_weight_cap;
		config.enable_weight_sliced_clat = options.enable_weight_sliced_clat;
		config.weight_sliced_clat_max_candidates = options.weight_sliced_clat_max_candidates;
		config.maximum_injection_input_masks = options.maximum_injection_input_masks;
		config.maximum_round_predecessors = options.maximum_round_predecessors;
		config.enable_state_memoization = options.enable_state_memoization;
		config.enable_verbose_output = options.verbose;
		enforce_strict_linear_hull_branch_caps( options, config );
		return config;
	}

	static bool linear_hull_selftest_check( bool condition, const std::string& message )
	{
		if ( !condition )
			std::cerr << "[SelfTest][LinearHull][FAIL] " << message << "\n";
		return condition;
	}

	static bool linear_hull_selftest_expect_contains( const std::string& text, const std::string& needle, const std::string& label )
	{
		return linear_hull_selftest_check( text.find( needle ) != std::string::npos, label + " missing `" + needle + "`" );
	}

	template <typename Fn>
	static std::string capture_linear_hull_selftest_stdout( Fn&& fn )
	{
		std::ostringstream buffer;
		std::streambuf* const previous = std::cout.rdbuf( buffer.rdbuf() );
		fn();
		std::cout.rdbuf( previous );
		return buffer.str();
	}

	static std::string create_linear_hull_batch_selftest_job_file()
	{
		namespace fs = std::filesystem;
		const fs::path path = fs::temp_directory_path() / "neoalzette_linear_hull_batch_selftest_jobs.txt";
		std::ofstream out( path.string(), std::ios::out | std::ios::trunc );
		out << "1 1 0\n";
		return path.string();
	}

	static bool run_linear_hull_batch_explicit_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions explicit_options {};
		explicit_options.round_count = 1;
		explicit_options.batch_job_file = job_file_path;
		explicit_options.batch_job_file_was_provided = true;
		explicit_options.batch_thread_count = 1;
		explicit_options.collect_weight_cap = 0;
		explicit_options.collect_weight_cap_was_provided = true;
		explicit_options.maximum_search_nodes = 65'536;
		explicit_options.maximum_search_seconds = 0;

		const std::string explicit_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( explicit_options ) == 0, "batch explicit-cap smoke should succeed" );
				} );
		ok = linear_hull_selftest_expect_contains( explicit_output, "[Batch][LinearHull][Job 1][OK]", "batch explicit-cap job status" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "collect_weight_cap=0", "batch explicit-cap collect cap" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "exact_jobs=1", "batch explicit-cap exact summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "[Batch][LinearHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "batch explicit-cap selection summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "[Batch][LinearHull][Combined] sources=1 endpoint_hulls=0 collected_trails=0 all_jobs_collected=1 all_jobs_exact=1", "batch explicit-cap combined summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "all_jobs_hard_limit_free=1", "batch explicit-cap hard-limit summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( explicit_output, "source_collected=1  source_exact=1", "batch explicit-cap source strictness summary" ) && ok;
		return ok;
	}

	static bool run_linear_hull_batch_combined_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions combined_options {};
		combined_options.round_count = 1;
		combined_options.collect_weight_cap = 0;
		combined_options.collect_weight_cap_was_provided = true;
		combined_options.maximum_search_nodes = 65'536;
		combined_options.maximum_search_seconds = 0;

		const std::vector<LinearBatchJob> combined_jobs {
			LinearBatchJob { 1, 0u, 0u },
			LinearBatchJob { 1, 0u, 0u }
		};
		LinearBatchHullPipelineOptions combined_pipeline_options {};
		combined_pipeline_options.worker_thread_count = 1;
		combined_pipeline_options.base_search_configuration = make_linear_base_search_configuration( combined_options );
		combined_pipeline_options.runtime_options_template = make_linear_runtime_options( combined_options, nullptr, nullptr );
		combined_pipeline_options.enable_combined_source_aggregator = true;
		combined_pipeline_options.stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		combined_pipeline_options.maximum_stored_trails = 4;
		const LinearBatchHullPipelineResult combined_pipeline_result =
			run_linear_batch_strict_hull_pipeline( combined_jobs, combined_pipeline_options );
		check( combined_pipeline_result.batch_summary.exact_jobs == combined_jobs.size(), "combined-source pipeline should keep all duplicate zero jobs exact" );
		check( combined_pipeline_result.batch_summary.partial_jobs == 0 && combined_pipeline_result.batch_summary.rejected_jobs == 0, "combined-source pipeline should avoid partial or rejected duplicate zero jobs" );
		check( combined_pipeline_result.batch_summary.jobs_hit_hard_limits == 0, "combined-source pipeline should stay free of hard limits for duplicate zero jobs" );
		check( combined_pipeline_result.combined_source_hull.enabled, "combined-source pipeline should enable the shared endpoint aggregator" );
		check( combined_pipeline_result.combined_source_hull.source_count == combined_jobs.size(), "combined-source pipeline should preserve both source jobs" );
		check( combined_pipeline_result.combined_source_hull.all_jobs_exact_within_collect_weight_cap, "combined-source pipeline should report exactness across all duplicate zero jobs" );
		check( combined_pipeline_result.combined_source_hull.all_jobs_hard_limit_free, "combined-source pipeline should report hard-limit freedom across all duplicate zero jobs" );
		const LinearCallbackHullAggregator& combined_aggregator = combined_pipeline_result.combined_source_hull.callback_aggregator;
		check( combined_aggregator.source_hulls.size() == combined_jobs.size(), "combined-source pipeline should expose one source hull per job" );
		check( combined_aggregator.endpoint_hulls.size() == 1, "combined-source pipeline should merge duplicate zero jobs into one shared endpoint hull" );
		check( combined_aggregator.collected_trail_count == 2, "combined-source pipeline should collect one zero trail from each source" );
		if ( const auto* source0 = combined_aggregator.find_source_hull( 0 ); source0 )
		{
			check( source0->trail_count == 1, "combined-source pipeline should preserve trail counts for source #0" );
		}
		else
		{
			check( false, "combined-source pipeline should expose source hull #0" );
		}
		if ( const auto* source1 = combined_aggregator.find_source_hull( 1 ); source1 )
		{
			check( source1->trail_count == 1, "combined-source pipeline should preserve trail counts for source #1" );
		}
		else
		{
			check( false, "combined-source pipeline should expose source hull #1" );
		}
		if ( combined_aggregator.endpoint_hulls.size() == 1 )
		{
			const auto& endpoint = combined_aggregator.endpoint_hulls.begin()->second;
			check( endpoint.trail_count == 2, "combined-source pipeline should accumulate both duplicate zero-job trails in the shared endpoint hull" );
		}
		if ( combined_aggregator.collected_trails.size() == 2 )
		{
			check( combined_aggregator.collected_trails[ 0 ].source_present && combined_aggregator.collected_trails[ 1 ].source_present, "combined-source stored trails should retain provenance for every duplicate zero job" );
			check( combined_aggregator.collected_trails[ 0 ].source_index == 0 && combined_aggregator.collected_trails[ 1 ].source_index == 1, "combined-source stored trails should retain source indices for duplicate zero jobs" );
		}
		if ( const auto* source0 = combined_aggregator.find_source_hull( 0 ); source0 )
		{
			check( source0->runtime_collected && source0->exact_within_collect_weight_cap, "combined-source pipeline should retain strict runtime metadata for source #0" );
			check( source0->best_weight_certified == false, "explicit-cap duplicate zero job should not pretend to certify a best weight" );
		}
		return ok;
	}

	static bool run_linear_hull_batch_multi_trajectory_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions multi_trajectory_options {};
		multi_trajectory_options.round_count = 1;
		multi_trajectory_options.batch_job_file = job_file_path;
		multi_trajectory_options.batch_job_file_was_provided = true;
		multi_trajectory_options.batch_thread_count = 1;
		multi_trajectory_options.maximum_search_nodes = 65'536;
		multi_trajectory_options.auto_breadth_maximum_search_nodes = 64;
		multi_trajectory_options.auto_deep_maximum_search_nodes = 65'536;
		multi_trajectory_options.auto_max_time_seconds = 0;
		multi_trajectory_options.collect_weight_window = 0;

		const std::string multi_trajectory_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( multi_trajectory_options ) == 0, "batch multi-trajectory strict-hull chain should succeed" );
				} );
		ok = linear_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][Breadth] TOP-1:", "batch multi-trajectory breadth summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][LinearHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "batch multi-trajectory selection summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][LinearHull][Combined] sources=1", "batch multi-trajectory combined source summary" ) && ok;
		return ok;
	}

	static bool run_linear_hull_batch_reject_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions reject_options {};
		reject_options.round_count = 1;
		reject_options.batch_job_file = job_file_path;
		reject_options.batch_job_file_was_provided = true;
		reject_options.batch_thread_count = 1;
		reject_options.collect_weight_cap = 0;
		reject_options.collect_weight_cap_was_provided = true;
		reject_options.maximum_search_nodes = 1;
		reject_options.maximum_search_seconds = 0;

		const std::string reject_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( reject_options ) == 0, "batch hard-limit partial smoke should still exit cleanly" );
				} );
		ok = linear_hull_selftest_expect_contains( reject_output, "[Batch][LinearHull][Job 1][PARTIAL]", "batch partial job status" ) && ok;
		ok = linear_hull_selftest_expect_contains( reject_output, "exactness_reason=hit_maximum_search_nodes", "batch partial exactness reason" ) && ok;
		ok = linear_hull_selftest_expect_contains( reject_output, "partial_jobs=1", "batch partial summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( reject_output, "all_jobs_hard_limit_free=0", "batch partial hard-limit summary" ) && ok;
		return ok;
	}

	static bool run_linear_hull_batch_resume_self_test( const std::string& job_file_path )
	{
		namespace fs = std::filesystem;

		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};
		auto read_text_file = [&]( const fs::path& path ) {
			std::ifstream in( path.string(), std::ios::in );
			std::ostringstream oss;
			oss << in.rdbuf();
			return oss.str();
		};

		const fs::path checkpoint_path = fs::temp_directory_path() / "neoalzette_linear_hull_batch_resume_selftest.ckpt";
		const fs::path selection_checkpoint_path = fs::temp_directory_path() / "neoalzette_linear_hull_batch_selection_resume_selftest.ckpt";
		const fs::path runtime_log_path = fs::temp_directory_path() / "neoalzette_linear_hull_batch_resume_selftest.runtime.log";
		const fs::path selection_runtime_log_path = fs::temp_directory_path() / "neoalzette_linear_hull_batch_selection_resume_selftest.runtime.log";
		{
			std::error_code ec {};
			fs::remove( checkpoint_path, ec );
			fs::remove( selection_checkpoint_path, ec );
			fs::remove( runtime_log_path, ec );
			fs::remove( selection_runtime_log_path, ec );
		}

		CommandLineOptions write_options {};
		write_options.round_count = 1;
		write_options.batch_job_file = job_file_path;
		write_options.batch_job_file_was_provided = true;
		write_options.batch_thread_count = 1;
		write_options.collect_weight_cap = 0;
		write_options.collect_weight_cap_was_provided = true;
		write_options.maximum_search_nodes = 65'536;
		write_options.maximum_search_seconds = 0;
		write_options.batch_checkpoint_out_path = checkpoint_path.string();
		write_options.batch_runtime_log_path = runtime_log_path.string();

		const std::string write_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( write_options ) == 0, "batch resume writer should succeed" );
				} );
		check( fs::exists( checkpoint_path ), "batch resume writer should create checkpoint file" );
		ok = linear_hull_selftest_expect_contains( write_output, "[Batch] checkpoint_resume=job_granularity", "batch resume writer checkpoint mode" ) && ok;

		LinearBatchHullPipelineCheckpointState checkpoint_state {};
		check( read_linear_batch_hull_pipeline_checkpoint( checkpoint_path.string(), checkpoint_state ), "batch resume reader should load checkpoint state" );
		check( checkpoint_state.jobs.size() == 1, "batch resume checkpoint should preserve job list" );
		check( checkpoint_state.completed_job_flags.size() == 1 && checkpoint_state.completed_job_flags[ 0 ] == 1u, "batch resume checkpoint should preserve completed job flags" );
		check( checkpoint_state.summaries.size() == 1 && checkpoint_state.summaries[ 0 ].collected, "batch resume checkpoint should preserve collected summary" );
		check( checkpoint_state.enable_combined_source_aggregator, "batch resume checkpoint should preserve combined aggregator enable flag" );
		check( checkpoint_state.combined_callback_aggregator.source_hulls.size() == 1, "batch resume checkpoint should preserve source hull aggregation" );

		CommandLineOptions resume_options {};
		resume_options.batch_resume_checkpoint_path = checkpoint_path.string();
		resume_options.batch_thread_count = 1;
		resume_options.batch_runtime_log_path = runtime_log_path.string();

		const std::string resume_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( resume_options ) == 0, "batch resume main path should succeed" );
				} );
		ok = linear_hull_selftest_expect_contains( resume_output, "resume_checkpoint=" + checkpoint_path.string(), "batch resume checkpoint banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( resume_output, "[Batch][LinearHull] jobs=1 exact_jobs=1 partial_jobs=0 rejected_jobs=0", "batch resume summary" ) && ok;
		ok = linear_hull_selftest_expect_contains( resume_output, "[Batch][LinearHull][Combined] sources=1 endpoint_hulls=0 collected_trails=0 all_jobs_collected=1 all_jobs_exact=1", "batch resume combined summary" ) && ok;
		const std::string runtime_log_text = read_text_file( runtime_log_path );
		ok = linear_hull_selftest_expect_contains( runtime_log_text, "event=batch_resume_start", "batch resume runtime event start" ) && ok;
		ok = linear_hull_selftest_expect_contains( runtime_log_text, "event=batch_checkpoint_write", "batch resume runtime event checkpoint write" ) && ok;
		ok = linear_hull_selftest_expect_contains( runtime_log_text, "event=batch_stop", "batch resume runtime event stop" ) && ok;
		ok = linear_hull_selftest_expect_contains( runtime_log_text, "batch_resume_fingerprint_hash=", "batch resume runtime fingerprint hash" ) && ok;

		CommandLineOptions selection_options {};
		selection_options.round_count = 1;
		selection_options.batch_job_file = job_file_path;
		selection_options.batch_job_file_was_provided = true;
		selection_options.batch_thread_count = 1;
		selection_options.maximum_search_nodes = 65'536;
		selection_options.auto_breadth_maximum_search_nodes = 64;
		selection_options.auto_deep_maximum_search_nodes = 65'536;
		selection_options.auto_max_time_seconds = 0;
		selection_options.collect_weight_window = 0;
		selection_options.batch_runtime_log_path = selection_runtime_log_path.string();

		std::vector<LinearBatchJob> selection_jobs {};
		check( load_linear_batch_jobs_from_file( job_file_path, 0, selection_options.round_count, selection_jobs ) == 0, "selection resume selftest should load batch jobs" );
		LinearBatchBreadthDeepOrchestratorConfig selection_cfg {};
		selection_cfg.breadth_configuration = make_linear_base_search_configuration( selection_options );
		selection_cfg.breadth_runtime.maximum_search_nodes = std::max<std::uint64_t>( 1, selection_options.auto_breadth_maximum_search_nodes );
		selection_cfg.breadth_runtime.maximum_search_seconds = 0;
		selection_cfg.breadth_runtime.progress_every_seconds = selection_options.progress_every_seconds;
		selection_cfg.deep_configuration = make_linear_base_search_configuration( selection_options );
		selection_cfg.deep_configuration.search_mode = SearchMode::Strict;
		selection_cfg.deep_configuration.maximum_round_predecessors = 0;
		selection_cfg.deep_configuration.enable_weight_sliced_clat = true;
		selection_cfg.deep_configuration.weight_sliced_clat_max_candidates = 0;
		selection_cfg.deep_runtime.maximum_search_nodes = selection_options.auto_deep_maximum_search_nodes;
		selection_cfg.deep_runtime.maximum_search_seconds = selection_options.auto_max_time_seconds;
		selection_cfg.deep_runtime.progress_every_seconds = selection_options.progress_every_seconds;
		LinearBatchSourceSelectionCheckpointState selection_state =
			make_initial_linear_batch_source_selection_checkpoint_state( selection_jobs, selection_cfg );
		check( write_linear_batch_source_selection_checkpoint( selection_checkpoint_path.string(), selection_state ), "selection resume selftest should write source-selection checkpoint" );

		CommandLineOptions selection_resume_options = selection_options;
		selection_resume_options.batch_resume_checkpoint_path = selection_checkpoint_path.string();
		selection_resume_options.batch_job_file.clear();
		selection_resume_options.batch_job_file_was_provided = false;
		selection_resume_options.batch_runtime_log_path = selection_runtime_log_path.string();

		const std::string selection_resume_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					check( run_linear_batch_mode( selection_resume_options ) == 0, "selection-stage batch resume main path should succeed" );
				} );
		ok = linear_hull_selftest_expect_contains( selection_resume_output, "resume_checkpoint=" + selection_checkpoint_path.string(), "selection resume checkpoint banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( selection_resume_output, "[Batch][Breadth]", "selection resume breadth banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( selection_resume_output, "[Batch][LinearHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "selection resume selection summary" ) && ok;
		const std::string selection_runtime_log_text = read_text_file( selection_runtime_log_path );
		ok = linear_hull_selftest_expect_contains( selection_runtime_log_text, "checkpoint_kind=linear_hull_batch_selection", "selection resume runtime checkpoint kind" ) && ok;
		ok = linear_hull_selftest_expect_contains( selection_runtime_log_text, "event=batch_resume_start", "selection resume runtime start" ) && ok;
		ok = linear_hull_selftest_expect_contains( selection_runtime_log_text, "batch_resume_fingerprint_hash=", "selection resume runtime fingerprint hash" ) && ok;

		std::error_code ec {};
		fs::remove( checkpoint_path, ec );
		fs::remove( selection_checkpoint_path, ec );
		fs::remove( runtime_log_path, ec );
		fs::remove( selection_runtime_log_path, ec );
		return ok;
	}

	static bool run_linear_hull_batch_mode_self_test( SelfTestScope scope )
	{
		bool ok = true;
		std::string job_file_path {};
		const bool needs_job_file =
			scope == SelfTestScope::All ||
			scope == SelfTestScope::Batch ||
			scope == SelfTestScope::BatchExplicit ||
			scope == SelfTestScope::BatchMultiTrajectory ||
			scope == SelfTestScope::BatchReject ||
			scope == SelfTestScope::BatchResume;
		if ( needs_job_file )
			job_file_path = create_linear_hull_batch_selftest_job_file();

		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchExplicit )
			ok = run_linear_hull_batch_explicit_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchCombined )
			ok = run_linear_hull_batch_combined_self_test() && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchMultiTrajectory )
			ok = run_linear_hull_batch_multi_trajectory_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchReject )
			ok = run_linear_hull_batch_reject_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchResume )
			ok = run_linear_hull_batch_resume_self_test( job_file_path ) && ok;

		std::error_code ec {};
		if ( !job_file_path.empty() )
			std::filesystem::remove( std::filesystem::path( job_file_path ), ec );
		return ok;
	}

	static bool run_linear_hull_aggregator_synthetic_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		const auto make_step =
			[]( int round_weight, std::uint32_t output_a, std::uint32_t output_b, std::uint32_t input_a, std::uint32_t input_b ) {
				LinearTrailStepRecord step {};
				step.round_index = 1;
				step.round_weight = round_weight;
				step.output_branch_a_mask = output_a;
				step.output_branch_b_mask = output_b;
				step.input_branch_a_mask = input_a;
				step.input_branch_b_mask = input_b;
				return step;
			};

		check( TwilightDream::hull_callback_aggregator::compute_growth_relative_delta( 0.0L, 0.0L ) == 0.0L, "compute_growth_relative_delta(0,0) should stay zero" );

		std::vector<LinearTrailStepRecord> trail_weight_2_a { make_step( 2, 0x10u, 0x20u, 0x11u, 0x22u ) };
		std::vector<LinearTrailStepRecord> trail_weight_2_b { make_step( 2, 0x30u, 0x40u, 0x33u, 0x44u ) };
		std::vector<LinearTrailStepRecord> trail_weight_1 { make_step( 1, 0x50u, 0x60u, 0x55u, 0x66u ) };

		LinearCallbackHullAggregator arrival_aggregator {};
		arrival_aggregator.set_maximum_stored_trails( 2 );

		LinearHullCollectedTrailView arrival_view_a {};
		arrival_view_a.total_weight = 2;
		arrival_view_a.input_branch_a_mask = 0x11u;
		arrival_view_a.input_branch_b_mask = 0x22u;
		arrival_view_a.exact_signed_correlation = 0.25L;
		arrival_view_a.trail = &trail_weight_2_a;

		LinearHullCollectedTrailView arrival_view_b {};
		arrival_view_b.total_weight = 2;
		arrival_view_b.input_branch_a_mask = 0x33u;
		arrival_view_b.input_branch_b_mask = 0x44u;
		arrival_view_b.exact_signed_correlation = -0.25L;
		arrival_view_b.trail = &trail_weight_2_b;

		LinearHullCollectedTrailView arrival_view_c {};
		arrival_view_c.total_weight = 1;
		arrival_view_c.input_branch_a_mask = 0x55u;
		arrival_view_c.input_branch_b_mask = 0x66u;
		arrival_view_c.exact_signed_correlation = 0.5L;
		arrival_view_c.trail = &trail_weight_1;

		check( arrival_aggregator.on_trail( arrival_view_a ), "arrival policy should accept first trail" );
		check( arrival_aggregator.on_trail( arrival_view_b ), "arrival policy should accept second trail" );
		check( arrival_aggregator.on_trail( arrival_view_c ), "arrival policy should accept callback even when storage fills" );
		check( arrival_aggregator.found_any, "arrival policy should mark found_any" );
		check( arrival_aggregator.collected_trail_count == 3, "arrival policy should count all collected trails" );
		check( arrival_aggregator.collected_trails.size() == 2, "arrival policy should keep only the first stored trails" );
		check( arrival_aggregator.dropped_stored_trail_count == 1, "arrival policy should count one dropped stored trail" );
		if ( arrival_aggregator.collected_trails.size() == 2 )
			check( arrival_aggregator.collected_trails[ 0 ].arrival_index == 0 && arrival_aggregator.collected_trails[ 1 ].arrival_index == 1, "arrival policy should keep arrival order" );
		check( arrival_aggregator.find_shell_summary( 2 ) && arrival_aggregator.find_shell_summary( 2 )->trail_count == 2, "arrival policy should accumulate shell trail counts" );
		check( arrival_aggregator.find_shell_summary( 1 ) && arrival_aggregator.find_shell_summary( 1 )->trail_count == 1, "arrival policy should track the strongest shell separately" );
		check( arrival_aggregator.aggregate_signed_correlation == 0.5L, "arrival policy should accumulate signed hull correlation exactly" );
		check( arrival_aggregator.aggregate_abs_correlation_mass == 1.0L, "arrival policy should accumulate absolute hull mass exactly" );
		check( arrival_aggregator.endpoint_hulls.size() == 3, "arrival policy should create one endpoint bucket per collected input mask" );
		if ( const auto* endpoint = arrival_aggregator.find_endpoint_hull( 0x55u, 0x66u ); endpoint )
		{
			check( endpoint->trail_count == 1, "arrival policy should track endpoint trail counts" );
			check( endpoint->aggregate_signed_correlation == 0.5L, "arrival policy should track endpoint signed correlation" );
			check( endpoint->aggregate_abs_correlation_mass == 0.5L, "arrival policy should track endpoint abs correlation mass" );
			check( endpoint->shell_summaries.size() == 1 && endpoint->shell_summaries.begin()->first == 1, "arrival policy should track endpoint shell summaries" );
		}
		else
		{
			check( false, "arrival policy should expose endpoint summary for the strongest input mask" );
		}
		check( arrival_aggregator.strongest_trail_signed_correlation == 0.5L, "arrival policy should track the strongest trail" );
		check( arrival_aggregator.strongest_input_branch_a_mask == 0x55u && arrival_aggregator.strongest_input_branch_b_mask == 0x66u, "arrival policy should retain strongest trail identity" );
		check( arrival_aggregator.strongest_trail.size() == 1 && arrival_aggregator.strongest_trail.front().round_weight == 1, "arrival policy should retain strongest trail steps" );
		arrival_aggregator.set_maximum_stored_trails( 1 );
		check( arrival_aggregator.collected_trails.size() == 1, "shrinking arrival-policy storage should resize stored trails" );
		check( arrival_aggregator.dropped_stored_trail_count == 2, "shrinking arrival-policy storage should count the extra drop" );
		arrival_aggregator.set_maximum_stored_trails( 0 );
		check( arrival_aggregator.collected_trails.empty(), "clearing arrival-policy storage should erase stored trails" );
		check( arrival_aggregator.dropped_stored_trail_count == 3, "clearing arrival-policy storage should count cleared trails" );
		check( arrival_aggregator.strongest_trail.empty(), "clearing arrival-policy storage should erase the global strongest trail steps" );
		if ( const auto* shell = arrival_aggregator.find_shell_summary( 1 ); shell )
			check( shell->strongest_trail.empty(), "clearing arrival-policy storage should erase shell strongest trail steps" );
		if ( const auto* endpoint = arrival_aggregator.find_endpoint_hull( 0x55u, 0x66u ); endpoint )
			check( endpoint->strongest_trail.empty(), "clearing arrival-policy storage should erase endpoint strongest trail steps" );

		LinearCallbackHullAggregator zero_storage_aggregator {};
		check( zero_storage_aggregator.on_trail( arrival_view_c ), "zero-storage policy should still accept collected trails" );
		check( zero_storage_aggregator.collected_trails.empty(), "zero-storage policy should store no trail records" );
		check( zero_storage_aggregator.strongest_trail_signed_correlation == 0.5L, "zero-storage policy should still track strongest trail strength" );
		check( zero_storage_aggregator.strongest_trail.empty(), "zero-storage policy should not retain the global strongest trail steps" );
		if ( const auto* shell = zero_storage_aggregator.find_shell_summary( 1 ); shell )
			check( shell->strongest_trail.empty(), "zero-storage policy should not retain shell strongest trail steps" );
		if ( const auto* endpoint = zero_storage_aggregator.find_endpoint_hull( 0x55u, 0x66u ); endpoint )
			check( endpoint->strongest_trail.empty(), "zero-storage policy should not retain endpoint strongest trail steps" );

		LinearCallbackHullAggregator strongest_aggregator {};
		strongest_aggregator.set_stored_trail_policy( StoredTrailPolicy::Strongest );
		strongest_aggregator.set_maximum_stored_trails( 2 );

		LinearHullCollectedTrailView strongest_view_weak {};
		strongest_view_weak.total_weight = 3;
		strongest_view_weak.input_branch_a_mask = 0x77u;
		strongest_view_weak.input_branch_b_mask = 0x88u;
		strongest_view_weak.exact_signed_correlation = 0.125L;

		LinearHullCollectedTrailView strongest_view_medium {};
		strongest_view_medium.total_weight = 2;
		strongest_view_medium.input_branch_a_mask = 0x99u;
		strongest_view_medium.input_branch_b_mask = 0xAAu;
		strongest_view_medium.exact_signed_correlation = -0.25L;

		LinearHullCollectedTrailView strongest_view_best {};
		strongest_view_best.total_weight = 1;
		strongest_view_best.input_branch_a_mask = 0xBBu;
		strongest_view_best.input_branch_b_mask = 0xCCu;
		strongest_view_best.exact_signed_correlation = 0.5L;

		check( strongest_aggregator.on_trail( strongest_view_weak ), "strongest policy should accept weak trail" );
		check( strongest_aggregator.on_trail( strongest_view_medium ), "strongest policy should accept medium trail" );
		check( strongest_aggregator.on_trail( strongest_view_best ), "strongest policy should accept strongest trail" );
		check( strongest_aggregator.collected_trails.size() == 2, "strongest policy should keep only top-k stored trails" );
		check( strongest_aggregator.dropped_stored_trail_count == 1, "strongest policy should count the replaced trail" );
		if ( strongest_aggregator.collected_trails.size() == 2 )
			check( strongest_aggregator.collected_trails[ 0 ].arrival_index == 2 && strongest_aggregator.collected_trails[ 1 ].arrival_index == 1, "strongest policy should keep the strongest stored trails sorted by strength" );
		strongest_aggregator.set_maximum_stored_trails( 1 );
		check( strongest_aggregator.collected_trails.size() == 1, "shrinking strongest-policy storage should preserve one strongest record" );
		if ( strongest_aggregator.collected_trails.size() == 1 )
			check( strongest_aggregator.collected_trails.front().arrival_index == 2, "shrinking strongest-policy storage should preserve the strongest record" );
		check( strongest_aggregator.dropped_stored_trail_count == 2, "shrinking strongest-policy storage should add one more drop" );

		LinearCallbackHullAggregator policy_switch_aggregator {};
		policy_switch_aggregator.set_maximum_stored_trails( 3 );
		check( policy_switch_aggregator.on_trail( strongest_view_weak ), "policy-switch storage should accept weak trail" );
		check( policy_switch_aggregator.on_trail( strongest_view_medium ), "policy-switch storage should accept medium trail" );
		check( policy_switch_aggregator.on_trail( strongest_view_best ), "policy-switch storage should accept strongest trail" );
		policy_switch_aggregator.set_stored_trail_policy( StoredTrailPolicy::Strongest );
		if ( policy_switch_aggregator.collected_trails.size() == 3 )
			check(
				policy_switch_aggregator.collected_trails[ 0 ].arrival_index == 2 &&
					policy_switch_aggregator.collected_trails[ 1 ].arrival_index == 1 &&
					policy_switch_aggregator.collected_trails[ 2 ].arrival_index == 0,
				"switching to strongest policy should reorder existing stored trails by strength" );
		policy_switch_aggregator.set_stored_trail_policy( StoredTrailPolicy::ArrivalOrder );
		if ( policy_switch_aggregator.collected_trails.size() == 3 )
			check(
				policy_switch_aggregator.collected_trails[ 0 ].arrival_index == 0 &&
					policy_switch_aggregator.collected_trails[ 1 ].arrival_index == 1 &&
					policy_switch_aggregator.collected_trails[ 2 ].arrival_index == 2,
				"switching back to arrival policy should reorder existing stored trails by arrival index" );

		LinearCallbackHullAggregator endpoint_aggregator {};
		LinearHullCollectedTrailView endpoint_view_a {};
		endpoint_view_a.total_weight = 2;
		endpoint_view_a.input_branch_a_mask = 0xAAu;
		endpoint_view_a.input_branch_b_mask = 0xBBu;
		endpoint_view_a.exact_signed_correlation = 0.25L;
		LinearHullCollectedTrailView endpoint_view_b {};
		endpoint_view_b.total_weight = 3;
		endpoint_view_b.input_branch_a_mask = 0xAAu;
		endpoint_view_b.input_branch_b_mask = 0xBBu;
		endpoint_view_b.exact_signed_correlation = -0.125L;
		check( endpoint_aggregator.on_trail( endpoint_view_a ), "endpoint aggregation should accept the first trail" );
		check( endpoint_aggregator.on_trail( endpoint_view_b ), "endpoint aggregation should accept a second trail in the same endpoint bucket" );
		if ( const auto* endpoint = endpoint_aggregator.find_endpoint_hull( 0xAAu, 0xBBu ); endpoint )
		{
			check( endpoint->trail_count == 2, "endpoint aggregation should keep both trails in the same bucket" );
			check( endpoint->aggregate_signed_correlation == 0.125L, "endpoint aggregation should sum signed correlation inside the bucket" );
			check( endpoint->aggregate_abs_correlation_mass == 0.375L, "endpoint aggregation should sum abs mass inside the bucket" );
			check( endpoint->shell_summaries.size() == 2, "endpoint aggregation should preserve shell splits inside the bucket" );
		}
		else
		{
			check( false, "endpoint aggregation should expose the shared endpoint bucket" );
		}

		LinearCallbackHullAggregator multi_source_aggregator {};
		multi_source_aggregator.set_maximum_stored_trails( 2 );
		std::vector<LinearTrailStepRecord> multi_source_trail_a { make_step( 1, 0x10u, 0x20u, 0xAAu, 0xBBu ) };
		std::vector<LinearTrailStepRecord> multi_source_trail_b { make_step( 2, 0x30u, 0x40u, 0xAAu, 0xBBu ) };
		LinearHullCollectedTrailView multi_source_view_a {};
		multi_source_view_a.total_weight = 1;
		multi_source_view_a.input_branch_a_mask = 0xAAu;
		multi_source_view_a.input_branch_b_mask = 0xBBu;
		multi_source_view_a.exact_signed_correlation = 0.25L;
		multi_source_view_a.trail = &multi_source_trail_a;
		LinearHullCollectedTrailView multi_source_view_b {};
		multi_source_view_b.total_weight = 2;
		multi_source_view_b.input_branch_a_mask = 0xAAu;
		multi_source_view_b.input_branch_b_mask = 0xBBu;
		multi_source_view_b.exact_signed_correlation = -0.125L;
		multi_source_view_b.trail = &multi_source_trail_b;
		check(
			multi_source_aggregator.make_callback_for_source( LinearHullSourceContext { true, 0, 1, 0x10u, 0x20u } )( multi_source_view_a ),
			"multi-source aggregation should accept source #0" );
		check(
			multi_source_aggregator.make_callback_for_source( LinearHullSourceContext { true, 1, 1, 0x30u, 0x40u } )( multi_source_view_b ),
			"multi-source aggregation should accept source #1" );
		check( multi_source_aggregator.source_hulls.size() == 2, "multi-source aggregation should track two distinct sources" );
		if ( const auto* source = multi_source_aggregator.find_source_hull( 1 ); source )
		{
			check( source->trail_count == 1, "multi-source aggregation should count trails per source" );
			check( source->source_output_branch_a_mask == 0x30u && source->source_output_branch_b_mask == 0x40u, "multi-source aggregation should preserve source identity" );
		}
		else
		{
			check( false, "multi-source aggregation should expose the second source bucket" );
		}
		if ( const auto* endpoint = multi_source_aggregator.find_endpoint_hull( 0xAAu, 0xBBu ); endpoint )
		{
			check( endpoint->trail_count == 2, "multi-source aggregation should merge sources into the shared endpoint hull" );
			check( endpoint->aggregate_signed_correlation == 0.125L, "multi-source aggregation should sum signed correlation across sources" );
		}
		else
		{
			check( false, "multi-source aggregation should expose the merged endpoint bucket" );
		}
		if ( multi_source_aggregator.collected_trails.size() == 2 )
		{
			check( multi_source_aggregator.collected_trails[ 0 ].source_present, "multi-source stored trails should retain provenance" );
			check( multi_source_aggregator.collected_trails[ 0 ].source_index == 0 && multi_source_aggregator.collected_trails[ 1 ].source_index == 1, "multi-source stored trails should retain source indices" );
		}

		LinearHullAggregationResult partial_aggregation {};
		partial_aggregation.exact_within_collect_weight_cap = false;
		partial_aggregation.used_non_strict_branch_cap = true;
		partial_aggregation.exactness_rejection_reason = StrictCertificationFailureReason::UsedBranchCap;
		check(
			!TwilightDream::hull_callback_aggregator::linear_aggregation_hit_any_limit( partial_aggregation ),
			"strictness-only exactness loss should not be classified as a hard limit" );
		const LinearGrowthShellRow partial_growth_row =
			TwilightDream::hull_callback_aggregator::make_linear_growth_shell_row( 1, zero_storage_aggregator, partial_aggregation );
		check( !partial_growth_row.hit_any_limit, "growth rows should keep strictness-only exactness loss separate from hard limits" );
		LinearStrictHullRuntimeResult partial_runtime_result {};
		partial_runtime_result.collected = true;
		partial_runtime_result.collect_weight_cap = 1;
		partial_runtime_result.aggregation_result = partial_aggregation;
		const LinearBatchHullJobSummary partial_job_summary =
			summarize_linear_batch_hull_job( 0, LinearBatchJob {}, partial_runtime_result, zero_storage_aggregator );
		check( !partial_job_summary.hit_any_limit, "batch summaries should not classify strictness-only exactness loss as a hard limit" );

		LinearCallbackHullAggregator stop_aggregator {};
		stop_aggregator.request_stop();
		check( !stop_aggregator.on_trail( arrival_view_a ), "stop policy should refuse callbacks after request_stop" );
		check( !stop_aggregator.found_any && stop_aggregator.collected_trail_count == 0, "stop policy should not mutate aggregator state after request_stop" );

		return ok;
	}

	static bool run_linear_hull_wrapper_smoke_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = linear_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions exact_options {};
		exact_options.round_count = 1;
		exact_options.collect_weight_cap = 0;
		exact_options.collect_weight_cap_was_provided = true;
		exact_options.maximum_search_nodes = 100000;
		exact_options.maximum_search_seconds = 1;

		const LinearBestSearchConfiguration exact_configuration = make_linear_base_search_configuration( exact_options );
		LinearCallbackHullAggregator exact_aggregator {};
		const LinearStrictHullRuntimeResult exact_result =
			run_linear_strict_hull_runtime(
				0u,
				0u,
				exact_configuration,
				make_linear_runtime_options( exact_options, nullptr, &exact_aggregator ),
				false,
				false );
		check( exact_result.collected, "exact smoke should complete the strict hull runtime" );
		check( !exact_result.best_search_executed && !exact_result.used_best_weight_reference, "exact smoke should stay in explicit-cap mode" );
		check( exact_result.collect_weight_cap == 0, "exact smoke should preserve collect_weight_cap=0" );
		check( exact_result.aggregation_result.exact_within_collect_weight_cap, "exact smoke should be exact within the collect cap" );
		check( exact_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::None, "exact smoke should report no exactness rejection" );
		check( exact_aggregator.collected_trail_count == 1 && exact_aggregator.found_any, "exact smoke should collect the single zero trail" );
		check( exact_aggregator.aggregate_signed_correlation == 1.0L, "exact smoke should aggregate to signed correlation 1" );
		check( exact_aggregator.aggregate_abs_correlation_mass == 1.0L, "exact smoke should aggregate to abs-mass 1" );
		check( exact_aggregator.endpoint_hulls.size() == 1, "exact smoke should expose exactly one endpoint hull" );
		if ( const auto* endpoint = exact_aggregator.find_endpoint_hull( 0u, 0u ); endpoint )
		{
			check( endpoint->trail_count == 1, "exact smoke should keep the zero input mask in one endpoint bucket" );
			check( endpoint->aggregate_signed_correlation == 1.0L, "exact smoke should keep exact signed correlation in the endpoint bucket" );
			check( endpoint->aggregate_abs_correlation_mass == 1.0L, "exact smoke should keep exact abs mass in the endpoint bucket" );
		}
		else
		{
			check( false, "exact smoke should expose the zero endpoint bucket" );
		}
		check( exact_aggregator.find_shell_summary( 0 ) && exact_aggregator.find_shell_summary( 0 )->trail_count == 1, "exact smoke should produce shell 0 only" );
		const std::string exact_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					print_result( exact_result, exact_aggregator, exact_options );
				} );
		ok = linear_hull_selftest_expect_contains( exact_output, "[HullWrapper][Linear]", "exact smoke output banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "endpoint_hull_count=1", "exact smoke endpoint count" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "strict_endpoint_hulls_within_collect_weight_cap=1", "exact smoke strict endpoint exactness flag" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "exact_within_collect_weight_cap=1", "exact smoke exactness flag" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "exactness_rejection_reason=none", "exact smoke exactness reason" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "endpoint_exact_signed_correlation=1.0000000000e+00", "exact smoke endpoint signed sum" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "endpoint_abs_correlation_mass=1.0000000000e+00", "exact smoke endpoint abs mass" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "input_branch_a_mask=0x00000000", "exact smoke endpoint input mask A" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "input_branch_b_mask=0x00000000", "exact smoke endpoint input mask B" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "global_collected_trail_signed_correlation_mass=1.0000000000e+00", "exact smoke global diagnostic signed mass" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "global_collected_trail_abs_correlation_mass=1.0000000000e+00", "exact smoke global diagnostic abs mass" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "collected_trails=1", "exact smoke collected trail count" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "global collected-trail masses mix multiple input-mask endpoints and are diagnostic only", "exact smoke diagnostic disclaimer" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "global_shell_diagnostic_begin", "exact smoke shell diagnostic banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( exact_output, "shell_weight=0", "exact smoke shell summary" ) && ok;

		CommandLineOptions limit_options = exact_options;
		limit_options.maximum_search_nodes = 1;
		limit_options.maximum_search_seconds = 0;

		const LinearBestSearchConfiguration limit_configuration = make_linear_base_search_configuration( limit_options );
		LinearCallbackHullAggregator limit_aggregator {};
		const LinearStrictHullRuntimeResult limit_result =
			run_linear_strict_hull_runtime(
				0u,
				0u,
				limit_configuration,
				make_linear_runtime_options( limit_options, nullptr, &limit_aggregator ),
				false,
				false );
		check( limit_result.collected, "limit smoke should still complete the explicit-cap runtime wrapper path" );
		check( !limit_aggregator.found_any && limit_aggregator.collected_trail_count == 0, "limit smoke should collect no trails under maxnodes=1" );
		check( limit_result.aggregation_result.hit_maximum_search_nodes, "limit smoke should report maximum_search_nodes" );
		check( !limit_result.aggregation_result.exact_within_collect_weight_cap, "limit smoke should not remain exact after hitting maxnodes" );
		check( limit_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::HitMaximumSearchNodes, "limit smoke should classify exactness loss as maxnodes" );
		const std::string limit_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					print_result( limit_result, limit_aggregator, limit_options );
				} );
		ok = linear_hull_selftest_expect_contains( limit_output, "no trail collected within limits.", "limit smoke banner" ) && ok;
		ok = linear_hull_selftest_expect_contains( limit_output, "exact_within_collect_weight_cap=0", "limit smoke exactness flag" ) && ok;
		ok = linear_hull_selftest_expect_contains( limit_output, "exactness_rejection_reason=hit_maximum_search_nodes", "limit smoke exactness reason" ) && ok;
		ok = linear_hull_selftest_expect_contains( limit_output, "nodes_visited=1", "limit smoke nodes_visited" ) && ok;

		CommandLineOptions collection_limit_options = exact_options;
		collection_limit_options.collect_weight_cap = 20;
		collection_limit_options.maximum_collected_trails = 1;
		collection_limit_options.maximum_search_seconds = 0;

		const LinearBestSearchConfiguration collection_limit_configuration = make_linear_base_search_configuration( collection_limit_options );
		LinearCallbackHullAggregator collection_limit_aggregator {};
		const LinearStrictHullRuntimeResult collection_limit_result =
			run_linear_strict_hull_runtime(
				0u,
				0u,
				collection_limit_configuration,
				make_linear_runtime_options( collection_limit_options, nullptr, &collection_limit_aggregator ),
				false,
				false );
		check( collection_limit_result.collected, "collection-limit smoke should still produce a runtime result" );
		check( collection_limit_result.aggregation_result.hit_collection_limit, "collection-limit smoke should report maximum_collected_trails" );
		check( !collection_limit_result.aggregation_result.exact_within_collect_weight_cap, "collection-limit smoke should lose exactness" );
		check(
			collection_limit_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::HitCollectionLimit,
			"collection-limit smoke should classify exactness loss as hit_collection_limit" );
		const std::string collection_limit_output =
			capture_linear_hull_selftest_stdout(
				[&]() {
					print_result( collection_limit_result, collection_limit_aggregator, collection_limit_options );
				} );
		ok = linear_hull_selftest_expect_contains( collection_limit_output, "exact_within_collect_weight_cap=0", "collection-limit smoke exactness flag" ) && ok;
		ok = linear_hull_selftest_expect_contains( collection_limit_output, "exactness_rejection_reason=hit_collection_limit", "collection-limit smoke exactness reason" ) && ok;
		ok = linear_hull_selftest_expect_contains( collection_limit_output, "[HIT maximum_collected_trails]", "collection-limit smoke limit banner" ) && ok;

		return ok;
	}

	static int run_linear_hull_wrapper_self_test( SelfTestScope scope )
	{
		std::cout << "[SelfTest][LinearHull] scope=" << selftest_scope_to_string( scope ) << "\n";
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Synthetic )
		{
			std::cout << "[SelfTest][LinearHull] synthetic aggregator checks\n";
			if ( !run_linear_hull_aggregator_synthetic_self_test() )
				return 1;
		}
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Smoke )
		{
			std::cout << "[SelfTest][LinearHull] deterministic wrapper smokes\n";
			if ( !run_linear_hull_wrapper_smoke_self_test() )
				return 1;
		}
		if ( scope == SelfTestScope::All ||
			 scope == SelfTestScope::Batch ||
			 scope == SelfTestScope::BatchExplicit ||
			 scope == SelfTestScope::BatchCombined ||
			 scope == SelfTestScope::BatchMultiTrajectory ||
			 scope == SelfTestScope::BatchReject ||
			 scope == SelfTestScope::BatchResume )
		{
			std::cout << "[SelfTest][LinearHull] batch pipeline checks\n";
			if ( !run_linear_hull_batch_mode_self_test( scope ) )
				return 1;
		}
		std::cout << "[SelfTest][LinearHull] OK\n";
		return 0;
	}

	static std::string format_word32_hex( std::uint32_t value )
	{
		return TwilightDream::hull_callback_aggregator::format_word32_hex_string( value );
	}

	static void print_linear_batch_hull_job_summary( const LinearBatchHullJobSummary& summary )
	{
		const char* status = "OK";
		if ( !summary.collected )
			status = "REJECTED";
		else if ( !summary.exact_within_collect_weight_cap )
			status = "PARTIAL";

		std::cout << "[Batch][LinearHull][Job " << ( summary.job_index + 1 ) << "][" << status << "]"
				  << " rounds=" << summary.job.rounds
				  << " output_branch_a_mask=" << format_word32_hex( summary.job.output_branch_a_mask )
				  << " output_branch_b_mask=" << format_word32_hex( summary.job.output_branch_b_mask );
		if ( summary.best_search_executed && summary.best_weight < INFINITE_WEIGHT )
			std::cout << " best_weight=" << summary.best_weight;
		std::cout << " collect_weight_cap=" << summary.collect_weight_cap;
		if ( !summary.collected )
		{
			std::cout << " rejection_reason=" << strict_certification_failure_reason_to_string( summary.strict_runtime_rejection_reason ) << "\n";
			return;
		}
		std::cout << " endpoint_hulls=" << summary.endpoint_hull_count
				  << " collected_trails=" << summary.collected_trails
				  << " exact=" << ( summary.exact_within_collect_weight_cap ? 1 : 0 )
				  << " exactness_reason=" << strict_certification_failure_reason_to_string( summary.exactness_rejection_reason )
				  << " best_search_nodes=" << summary.best_search_nodes_visited
				  << " hull_nodes=" << summary.hull_nodes_visited
				  << " global_abs_mass_weight_equivalent=" << std::fixed << std::setprecision( 6 ) << static_cast<double>( correlation_to_weight( summary.global_abs_correlation_mass ) )
				  << " strongest_trail_weight_equivalent=" << static_cast<double>( correlation_to_weight( summary.strongest_trail_abs_correlation ) )
				  << " hit_limit=" << ( summary.hit_any_limit ? 1 : 0 );
		std::cout << std::defaultfloat << "\n";
	}

	static void print_linear_combined_source_hull_summary(
		const LinearBatchHullPipelineResult& pipeline_result,
		std::size_t input_job_count,
		std::size_t selected_source_count )
	{
		if ( !pipeline_result.combined_source_hull.enabled )
			return;

		const LinearCallbackHullAggregator& combined_aggregator = pipeline_result.combined_source_hull.callback_aggregator;
		std::cout << "[Batch][LinearHull][Selection] input_jobs=" << input_job_count
				  << " selected_sources=" << selected_source_count
				  << " selection_complete=" << ( input_job_count == selected_source_count ? 1 : 0 )
				  << "\n";
		if ( selected_source_count < input_job_count )
			std::cout << "  note=combined endpoint hull is strict only across the selected sources chosen by the breadth->deep front stage\n";
		std::cout << "[Batch][LinearHull][Combined] sources=" << pipeline_result.combined_source_hull.source_count
				  << " endpoint_hulls=" << combined_aggregator.endpoint_hulls.size()
				  << " collected_trails=" << combined_aggregator.collected_trail_count
				  << " all_jobs_collected=" << ( pipeline_result.combined_source_hull.all_jobs_collected ? 1 : 0 )
				  << " all_jobs_exact=" << ( pipeline_result.combined_source_hull.all_jobs_exact_within_collect_weight_cap ? 1 : 0 )
				  << " all_jobs_hard_limit_free=" << ( pipeline_result.combined_source_hull.all_jobs_hard_limit_free ? 1 : 0 )
				  << "\n";
		std::cout << "  note=source_hull_summary rows are the strict per-source hulls kept before cross-source endpoint merging\n";
		std::cout << "  note=endpoint_hull_summary rows are the strict merged endpoint hulls across all collected sources\n";
		print_linear_source_hull_summary( combined_aggregator );
		print_linear_endpoint_hull_summary( combined_aggregator );
	}

	static const char* linear_batch_selection_stage_to_string( LinearBatchSelectionCheckpointStage stage ) noexcept
	{
		switch ( stage )
		{
		case LinearBatchSelectionCheckpointStage::Breadth:
			return "selection_breadth";
		case LinearBatchSelectionCheckpointStage::DeepReady:
			return "selection_deep_ready";
		default:
			return "selection_unknown";
		}
	}

	static void write_linear_batch_runtime_event(
		RuntimeEventLog& runtime_log,
		const char* event_name,
		const std::function<void( std::ostream& )>& write_fields )
	{
		runtime_log.write_event(
			event_name,
			[&]( std::ostream& out ) {
				if ( write_fields )
					write_fields( out );
			} );
	}

	static int run_linear_batch_mode( const CommandLineOptions& options )
	{
		const bool batch_resume_requested = !options.batch_resume_checkpoint_path.empty();
		const std::string batch_checkpoint_path =
			!options.batch_checkpoint_out_path.empty() ?
				options.batch_checkpoint_out_path :
				options.batch_resume_checkpoint_path;
		std::string batch_runtime_log_path = options.batch_runtime_log_path;
		LinearBatchHullPipelineCheckpointState resumed_batch_state {};
		bool resumed_batch_state_loaded = false;
		LinearBatchSourceSelectionCheckpointState resumed_selection_state {};
		bool resumed_selection_state_loaded = false;
		LinearBatchHullPipelineCheckpointState final_batch_state_for_log_storage {};
		const LinearBatchHullPipelineCheckpointState* final_batch_state_for_log = nullptr;
		RuntimeEventLog batch_runtime_log {};
		bool batch_runtime_log_enabled = false;

		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			std::cerr << "[Batch] ERROR: --best-search-resume is for single-run deep search only; use --batch-resume for batch strict-hull state.\n";
			return 1;
		}
		if ( options.best_search_checkpoint_out_was_provided || options.best_search_checkpoint_every_seconds_was_provided )
		{
			std::cerr << "[Batch] ERROR: binary checkpoints are supported only in single-run deep search.\n";
			return 1;
		}
		if ( !options.best_search_history_log_path.empty() || !options.best_search_runtime_log_path.empty() )
		{
			std::cerr << "[Batch] ERROR: per-job best-search logs are not supported in hull batch mode yet.\n";
			return 1;
		}
		if ( options.growth_mode )
		{
			std::cerr << "[Batch] ERROR: growth mode and batch mode are mutually exclusive.\n";
			return 1;
		}
		if ( !batch_resume_requested && options.batch_job_count == 0 && !options.batch_job_file_was_provided )
		{
			std::cerr << "[Batch] ERROR: batch mode requested but no jobs provided.\n";
			return 1;
		}
		if ( options.round_count <= 0 )
		{
			std::cerr << "[Batch] ERROR: --round-count must be > 0\n";
			return 1;
		}

		const unsigned		hw = std::thread::hardware_concurrency();
		const int			worker_thread_count = ( options.batch_thread_count > 0 ) ? options.batch_thread_count : int( ( hw == 0 ) ? 1 : hw );
		const int			worker_threads_clamped = std::max( 1, worker_thread_count );

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t	 avail_bytes = mem.available_physical_bytes;
		std::uint64_t		 headroom_bytes = ( options.strategy_target_headroom_bytes != 0 ) ? options.strategy_target_headroom_bytes : compute_memory_headroom_bytes( avail_bytes, options.memory_headroom_mib, options.memory_headroom_mib_was_provided );
		if ( avail_bytes != 0 && headroom_bytes > avail_bytes )
			headroom_bytes = avail_bytes;

		pmr_configure_for_run( avail_bytes, headroom_bytes );
		memory_governor_enable_for_run( headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );

		MemoryBallast memory_ballast( headroom_bytes );
		if ( options.memory_ballast_enabled && headroom_bytes != 0 )
		{
			IosStateGuard g( std::cout );
			std::cout << "[MemoryBallast] enabled  headroom_gibibytes=" << std::fixed << std::setprecision( 2 ) << bytes_to_gibibytes( headroom_bytes ) << "\n";
			memory_ballast.start();
		}

		if ( batch_runtime_log_path.empty() && !batch_checkpoint_path.empty() )
			batch_runtime_log_path = default_linear_batch_runtime_log_path( batch_checkpoint_path );
		if ( !batch_runtime_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					batch_runtime_log,
					batch_runtime_log_path,
					"[Batch] ERROR: cannot open batch runtime log: " ) )
			{
				memory_governor_disable_for_run();
				return 1;
			}
			batch_runtime_log_enabled = true;
		}

		if ( batch_resume_requested )
		{
			if ( read_linear_batch_hull_pipeline_checkpoint( options.batch_resume_checkpoint_path, resumed_batch_state ) )
			{
				resumed_batch_state_loaded = true;
			}
			else if ( read_linear_batch_source_selection_checkpoint( options.batch_resume_checkpoint_path, resumed_selection_state ) )
			{
				resumed_selection_state_loaded = true;
			}
			else
			{
				std::cerr << "[Batch] ERROR: cannot read batch checkpoint: " << options.batch_resume_checkpoint_path << "\n";
				memory_governor_disable_for_run();
				return 1;
			}
		}

		if ( batch_runtime_log_enabled )
		{
			write_linear_batch_runtime_event(
				batch_runtime_log,
				"batch_start",
				[&]( std::ostream& out ) {
					out << "batch_checkpoint_path=" << batch_checkpoint_path << "\n";
					out << "batch_runtime_log_path=" << batch_runtime_log_path << "\n";
					out << "batch_resume_requested=" << ( batch_resume_requested ? 1 : 0 ) << "\n";
					out << "batch_job_count_requested=" << options.batch_job_count << "\n";
					out << "batch_job_file_provided=" << ( options.batch_job_file_was_provided ? 1 : 0 ) << "\n";
				} );
			if ( resumed_selection_state_loaded )
			{
				const LinearBatchResumeFingerprint fingerprint =
					compute_linear_selection_resume_fingerprint( resumed_selection_state );
				write_linear_batch_runtime_event(
					batch_runtime_log,
					"batch_resume_start",
					[&]( std::ostream& out ) {
						out << "checkpoint_kind=linear_hull_batch_selection\n";
						out << "checkpoint_path=" << options.batch_resume_checkpoint_path << "\n";
						out << "stage=" << linear_batch_selection_stage_to_string( resumed_selection_state.stage ) << "\n";
						write_linear_batch_resume_fingerprint_fields( out, fingerprint );
					} );
			}
			else if ( resumed_batch_state_loaded )
			{
				const LinearBatchResumeFingerprint fingerprint =
					compute_linear_hull_resume_fingerprint( resumed_batch_state );
				write_linear_batch_runtime_event(
					batch_runtime_log,
					"batch_resume_start",
					[&]( std::ostream& out ) {
						out << "checkpoint_kind=linear_hull_batch\n";
						out << "checkpoint_path=" << options.batch_resume_checkpoint_path << "\n";
						out << "stage=strict_hull\n";
						write_linear_batch_resume_fingerprint_fields( out, fingerprint );
					} );
			}
		}

		std::vector<LinearBatchJob> jobs;
		if ( resumed_batch_state_loaded )
		{
			jobs = resumed_batch_state.jobs;
		}
		else if ( resumed_selection_state_loaded )
		{
			jobs = resumed_selection_state.jobs;
		}
		else if ( options.batch_job_file_was_provided )
		{
			if ( load_linear_batch_jobs_from_file( options.batch_job_file, options.batch_job_count, options.round_count, jobs ) != 0 )
			{
				memory_governor_disable_for_run();
				return 1;
			}
		}
		else
		{
			if ( !options.batch_seed_was_provided )
			{
				std::cerr << "[Batch] ERROR: batch RNG mode requires --seed.\n";
				memory_governor_disable_for_run();
				return 1;
			}
			generate_linear_batch_jobs_from_seed( options.batch_job_count, options.round_count, options.batch_seed, jobs );
		}

		int min_rounds = std::numeric_limits<int>::max();
		int max_rounds = 0;
		for ( const auto& j : jobs )
		{
			min_rounds = std::min( min_rounds, j.rounds );
			max_rounds = std::max( max_rounds, j.rounds );
		}

		LinearBestSearchConfiguration base_search_configuration = make_linear_base_search_configuration( options );
		if ( resumed_batch_state_loaded )
			base_search_configuration = resumed_batch_state.base_search_configuration;
		else if ( resumed_selection_state_loaded )
			base_search_configuration = resumed_selection_state.config.breadth_configuration;
		enforce_strict_linear_hull_branch_caps( options, base_search_configuration );

		const bool use_multi_trajectory_front_stage =
			resumed_batch_state_loaded ?
				( resumed_batch_state.runtime_options_snapshot.collect_cap_mode == HullCollectCapMode::BestWeightPlusWindow ) :
			resumed_selection_state_loaded ?
				true :
				!options.collect_weight_cap_was_provided;
		const auto display_collect_cap_mode =
			resumed_batch_state_loaded ?
				resumed_batch_state.runtime_options_snapshot.collect_cap_mode :
				( options.collect_weight_cap_was_provided ? HullCollectCapMode::ExplicitCap : HullCollectCapMode::BestWeightPlusWindow );
		const int display_collect_weight_cap =
			( display_collect_cap_mode == HullCollectCapMode::ExplicitCap ) ?
				( resumed_batch_state_loaded ? std::max( 0, resumed_batch_state.runtime_options_snapshot.explicit_collect_weight_cap ) : std::max( 0, options.collect_weight_cap ) ) :
				-1;
		const int display_collect_weight_window =
			( display_collect_cap_mode == HullCollectCapMode::BestWeightPlusWindow ) ?
				( resumed_batch_state_loaded ? std::max( 0, resumed_batch_state.runtime_options_snapshot.collect_weight_window ) : std::max( 0, options.collect_weight_window ) ) :
				-1;
		const std::uint64_t display_runtime_maxnodes =
			resumed_batch_state_loaded ?
				resumed_batch_state.runtime_options_snapshot.runtime_controls.maximum_search_nodes :
				options.maximum_search_nodes;
		const std::uint64_t display_runtime_maxseconds =
			resumed_batch_state_loaded ?
				resumed_batch_state.runtime_options_snapshot.runtime_controls.maximum_search_seconds :
				options.maximum_search_seconds;
		const std::uint64_t display_maximum_collected_trails =
			resumed_batch_state_loaded ?
				resumed_batch_state.runtime_options_snapshot.maximum_collected_trails :
				options.maximum_collected_trails;
		const std::uint64_t display_maximum_stored_trails =
			resumed_batch_state_loaded ?
				static_cast<std::uint64_t>( resumed_batch_state.maximum_stored_trails ) :
				options.maximum_stored_trails;
		const StoredTrailPolicy display_stored_trail_policy =
			resumed_batch_state_loaded ? resumed_batch_state.stored_trail_policy : options.stored_trail_policy;
		std::cout << "[Batch] mode=" << ( use_multi_trajectory_front_stage ? "linear_multi_trajectory_strict_hull_pipeline" : "linear_strict_hull_pipeline" ) << "\n";
		if ( min_rounds == max_rounds )
			std::cout << "  rounds=" << min_rounds;
		else
			std::cout << "  rounds_range=[" << min_rounds << "," << max_rounds << "]";
		std::cout << "  jobs=" << jobs.size() << "  threads=" << worker_threads_clamped;
		if ( batch_resume_requested )
			std::cout << "  resume_checkpoint=" << options.batch_resume_checkpoint_path << "\n";
		else if ( options.batch_job_file_was_provided )
			std::cout << "  batch_file=" << options.batch_job_file << "\n";
		else
			std::cout << "  seed=0x" << std::hex << options.batch_seed << std::dec << "\n";
		{
			IosStateGuard g( std::cout );
			std::cout << "  per_job: runtime_maximum_search_nodes=" << display_runtime_maxnodes
					  << "  runtime_maximum_search_seconds=" << display_runtime_maxseconds
					  << "  collect_mode=" << ( display_collect_cap_mode == HullCollectCapMode::ExplicitCap ? "explicit_cap" : "best_weight_plus_window" )
					  << "  collect_weight_cap=" << display_collect_weight_cap
					  << "  collect_weight_window=" << display_collect_weight_window
					  << "  maximum_collected_trails=" << display_maximum_collected_trails
					  << "  maximum_stored_trails=" << display_maximum_stored_trails
					  << "  stored_trail_policy=" << TwilightDream::hull_callback_aggregator::stored_trail_policy_to_string( display_stored_trail_policy )
					  << "\n";
		}
		if ( use_multi_trajectory_front_stage && !resumed_batch_state_loaded )
		{
			std::cout << "  breadth_stage_maxnodes=" << std::max<std::uint64_t>( 1, options.auto_breadth_maximum_search_nodes )
					  << "  deep_stage_maxnodes=" << options.auto_deep_maximum_search_nodes
					  << "  deep_stage_maxseconds=" << options.auto_max_time_seconds
					  << "  deep_target_best_weight=" << options.auto_target_best_weight
					  << "\n";
		}
		std::cout << "  [Batch] checkpoint_resume=" << ( batch_checkpoint_path.empty() ? "off" : "job_granularity" ) << "\n";
		if ( !batch_checkpoint_path.empty() )
		{
			std::cout << "  [Batch] checkpoint_path=" << batch_checkpoint_path << "\n";
			std::cout << "  [Batch] checkpoint_note=resume restarts any in-flight strict-hull job from its job boundary; completed jobs and combined hull aggregation are preserved.\n";
		}
		std::cout << "\n";

		std::vector<LinearBatchJob> selected_jobs = jobs;
		std::vector<TwilightDream::hull_callback_aggregator::LinearBatchHullBestSearchSeed> selected_job_seeds {};
		if ( !resumed_batch_state_loaded && use_multi_trajectory_front_stage )
		{
			LinearBatchBreadthDeepOrchestratorResult orchestrator_result {};
			if ( resumed_selection_state_loaded || !batch_checkpoint_path.empty() )
			{
				LinearBatchSourceSelectionCheckpointState selection_state =
					resumed_selection_state_loaded ?
						resumed_selection_state :
						make_initial_linear_batch_source_selection_checkpoint_state(
							jobs,
							[&]() {
								LinearBatchBreadthDeepOrchestratorConfig cfg {};
								cfg.breadth_configuration = base_search_configuration;
								cfg.breadth_runtime.maximum_search_nodes = std::max<std::uint64_t>( 1, options.auto_breadth_maximum_search_nodes );
								cfg.breadth_runtime.maximum_search_seconds = 0;
								cfg.breadth_runtime.progress_every_seconds = options.progress_every_seconds;
								cfg.deep_configuration = base_search_configuration;
								cfg.deep_configuration.search_mode = SearchMode::Strict;
								cfg.deep_configuration.maximum_round_predecessors = 0;
								cfg.deep_configuration.enable_weight_sliced_clat = true;
								cfg.deep_configuration.weight_sliced_clat_max_candidates = 0;
								if ( options.auto_target_best_weight >= 0 )
									cfg.deep_configuration.target_best_weight = options.auto_target_best_weight;
								cfg.deep_runtime.maximum_search_nodes = options.auto_deep_maximum_search_nodes;
								cfg.deep_runtime.maximum_search_seconds = options.auto_max_time_seconds;
								cfg.deep_runtime.progress_every_seconds = options.progress_every_seconds;
								return cfg;
							}() );
				if ( !resumed_selection_state_loaded && !batch_checkpoint_path.empty() )
				{
					if ( write_linear_batch_source_selection_checkpoint( batch_checkpoint_path, selection_state ) && batch_runtime_log_enabled )
					{
						const LinearBatchResumeFingerprint fingerprint =
							compute_linear_selection_resume_fingerprint( selection_state );
						write_linear_batch_runtime_event(
							batch_runtime_log,
							"batch_checkpoint_write",
							[&]( std::ostream& out ) {
								out << "checkpoint_kind=linear_hull_batch_selection\n";
								out << "checkpoint_path=" << batch_checkpoint_path << "\n";
								out << "stage=" << linear_batch_selection_stage_to_string( selection_state.stage ) << "\n";
								out << "checkpoint_reason=selection_stage_init\n";
								out << "checkpoint_write_result=success\n";
								write_linear_batch_resume_fingerprint_fields( out, fingerprint );
							} );
					}
				}
				run_linear_batch_breadth_then_deep_from_checkpoint_state(
					selection_state,
					worker_threads_clamped,
					batch_checkpoint_path,
					batch_runtime_log_enabled ?
						std::function<void( const LinearBatchSourceSelectionCheckpointState&, const char* )>(
							[&]( const LinearBatchSourceSelectionCheckpointState& checkpoint_state, const char* reason ) {
								const LinearBatchResumeFingerprint fingerprint =
									compute_linear_selection_resume_fingerprint( checkpoint_state );
								write_linear_batch_runtime_event(
									batch_runtime_log,
									"batch_checkpoint_write",
									[&]( std::ostream& out ) {
										out << "checkpoint_kind=linear_hull_batch_selection\n";
										out << "checkpoint_path=" << batch_checkpoint_path << "\n";
										out << "stage=" << linear_batch_selection_stage_to_string( checkpoint_state.stage ) << "\n";
										out << "checkpoint_reason=" << ( reason ? reason : "selection_checkpoint" ) << "\n";
										out << "checkpoint_write_result=success\n";
										write_linear_batch_resume_fingerprint_fields( out, fingerprint );
									} );
							} ) :
						std::function<void( const LinearBatchSourceSelectionCheckpointState&, const char* )> {},
					orchestrator_result );
			}
			else
			{
				LinearBatchBreadthDeepOrchestratorConfig orchestrator_config {};
				orchestrator_config.breadth_configuration = base_search_configuration;
				orchestrator_config.breadth_runtime.maximum_search_nodes = std::max<std::uint64_t>( 1, options.auto_breadth_maximum_search_nodes );
				orchestrator_config.breadth_runtime.maximum_search_seconds = 0;
				orchestrator_config.breadth_runtime.progress_every_seconds = options.progress_every_seconds;

				orchestrator_config.deep_configuration = base_search_configuration;
				orchestrator_config.deep_configuration.search_mode = SearchMode::Strict;
				orchestrator_config.deep_configuration.maximum_round_predecessors = 0;
				orchestrator_config.deep_configuration.enable_weight_sliced_clat = true;
				orchestrator_config.deep_configuration.weight_sliced_clat_max_candidates = 0;
				if ( options.auto_target_best_weight >= 0 )
					orchestrator_config.deep_configuration.target_best_weight = options.auto_target_best_weight;
				orchestrator_config.deep_runtime.maximum_search_nodes = options.auto_deep_maximum_search_nodes;
				orchestrator_config.deep_runtime.maximum_search_seconds = options.auto_max_time_seconds;
				orchestrator_config.deep_runtime.progress_every_seconds = options.progress_every_seconds;
				run_linear_batch_breadth_then_deep( jobs, worker_threads_clamped, orchestrator_config, orchestrator_result );
			}
			if ( orchestrator_result.top_candidates.empty() )
			{
				memory_governor_disable_for_run();
				return 1;
			}

			selected_jobs.clear();
			selected_job_seeds.reserve( orchestrator_result.top_candidates.size() );
			for ( const auto& candidate : orchestrator_result.top_candidates )
			{
				selected_jobs.push_back( jobs[ candidate.job_index ] );
				TwilightDream::hull_callback_aggregator::LinearBatchHullBestSearchSeed seed {};
				if ( candidate.found )
				{
					seed.present = true;
					seed.seeded_upper_bound_weight = candidate.best_weight;
					seed.seeded_upper_bound_trail = candidate.trail;
				}
				selected_job_seeds.push_back( std::move( seed ) );
			}
		}

		LinearBatchHullPipelineResult pipeline_result {};
		if ( resumed_batch_state_loaded )
		{
			pipeline_result =
				run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
					resumed_batch_state,
					worker_threads_clamped,
					TwilightDream::auto_search_linear::LinearHullTrailCallback {},
					[&]( const LinearBatchHullJobSummary& summary ) {
						print_linear_batch_hull_job_summary( summary );
					},
					batch_checkpoint_path,
					batch_runtime_log_enabled ?
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )>(
							[&]( const LinearBatchHullPipelineCheckpointState& checkpoint_state, const char* reason ) {
								const LinearBatchResumeFingerprint fingerprint =
									compute_linear_hull_resume_fingerprint( checkpoint_state );
								write_linear_batch_runtime_event(
									batch_runtime_log,
									"batch_checkpoint_write",
									[&]( std::ostream& out ) {
										out << "checkpoint_kind=linear_hull_batch\n";
										out << "checkpoint_path=" << batch_checkpoint_path << "\n";
										out << "stage=strict_hull\n";
										out << "checkpoint_reason=" << ( reason ? reason : "strict_hull_checkpoint" ) << "\n";
										out << "checkpoint_write_result=success\n";
										write_linear_batch_resume_fingerprint_fields( out, fingerprint );
									} );
							} ) :
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )> {} );
			selected_jobs = resumed_batch_state.jobs;
			final_batch_state_for_log = &resumed_batch_state;
		}
		else
		{
			LinearBatchHullPipelineOptions pipeline_options {};
			pipeline_options.worker_thread_count = worker_threads_clamped;
			pipeline_options.base_search_configuration = base_search_configuration;
			pipeline_options.runtime_options_template = make_linear_runtime_options( options, nullptr, nullptr );
			pipeline_options.best_search_seeds = std::move( selected_job_seeds );
			pipeline_options.enable_combined_source_aggregator = true;
			pipeline_options.stored_trail_policy = options.stored_trail_policy;
			pipeline_options.maximum_stored_trails = static_cast<std::size_t>( options.maximum_stored_trails );

			LinearBatchHullPipelineCheckpointState batch_state =
				make_initial_linear_batch_hull_pipeline_checkpoint_state( selected_jobs, pipeline_options );
			if ( !batch_checkpoint_path.empty() &&
				 !write_linear_batch_hull_pipeline_checkpoint( batch_checkpoint_path, batch_state ) )
			{
				std::cerr << "[Batch] ERROR: cannot write batch checkpoint: " << batch_checkpoint_path << "\n";
				memory_governor_disable_for_run();
				return 1;
			}
			if ( !batch_checkpoint_path.empty() && batch_runtime_log_enabled )
			{
				const LinearBatchResumeFingerprint fingerprint =
					compute_linear_hull_resume_fingerprint( batch_state );
				write_linear_batch_runtime_event(
					batch_runtime_log,
					"batch_checkpoint_write",
					[&]( std::ostream& out ) {
						out << "checkpoint_kind=linear_hull_batch\n";
						out << "checkpoint_path=" << batch_checkpoint_path << "\n";
						out << "stage=strict_hull\n";
						out << "checkpoint_reason=strict_hull_stage_init\n";
						out << "checkpoint_write_result=success\n";
						write_linear_batch_resume_fingerprint_fields( out, fingerprint );
					} );
			}

			pipeline_result =
				run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
					batch_state,
					worker_threads_clamped,
					pipeline_options.runtime_options_template.on_trail,
					[&]( const LinearBatchHullJobSummary& summary ) {
						print_linear_batch_hull_job_summary( summary );
					},
					batch_checkpoint_path,
					batch_runtime_log_enabled ?
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )>(
							[&]( const LinearBatchHullPipelineCheckpointState& checkpoint_state, const char* reason ) {
								const LinearBatchResumeFingerprint fingerprint =
									compute_linear_hull_resume_fingerprint( checkpoint_state );
								write_linear_batch_runtime_event(
									batch_runtime_log,
									"batch_checkpoint_write",
									[&]( std::ostream& out ) {
										out << "checkpoint_kind=linear_hull_batch\n";
										out << "checkpoint_path=" << batch_checkpoint_path << "\n";
										out << "stage=strict_hull\n";
										out << "checkpoint_reason=" << ( reason ? reason : "strict_hull_checkpoint" ) << "\n";
										out << "checkpoint_write_result=success\n";
										write_linear_batch_resume_fingerprint_fields( out, fingerprint );
									} );
							} ) :
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )> {} );
			final_batch_state_for_log_storage = batch_state;
			final_batch_state_for_log = &final_batch_state_for_log_storage;
		}
		const LinearBatchHullRunSummary& batch_summary = pipeline_result.batch_summary;

		std::cout << "\n[Batch][LinearHull] jobs=" << batch_summary.jobs.size()
				  << " exact_jobs=" << batch_summary.exact_jobs
				  << " partial_jobs=" << batch_summary.partial_jobs
				  << " rejected_jobs=" << batch_summary.rejected_jobs
				  << " total_best_search_nodes=" << batch_summary.total_best_search_nodes
				  << " total_hull_nodes=" << batch_summary.total_hull_nodes
				  << "\n";
		print_linear_combined_source_hull_summary( pipeline_result, jobs.size(), selected_jobs.size() );
		if ( batch_runtime_log_enabled )
		{
			write_linear_batch_runtime_event(
				batch_runtime_log,
				"batch_stop",
				[&]( std::ostream& out ) {
					out << "jobs=" << batch_summary.jobs.size() << "\n";
					out << "selected_sources=" << selected_jobs.size() << "\n";
					out << "exact_jobs=" << batch_summary.exact_jobs << "\n";
					out << "partial_jobs=" << batch_summary.partial_jobs << "\n";
					out << "rejected_jobs=" << batch_summary.rejected_jobs << "\n";
					out << "total_best_search_nodes=" << batch_summary.total_best_search_nodes << "\n";
					out << "total_hull_nodes=" << batch_summary.total_hull_nodes << "\n";
					if ( pipeline_result.combined_source_hull.enabled )
					{
						out << "combined_source_count=" << pipeline_result.combined_source_hull.source_count << "\n";
						out << "combined_endpoint_hull_count=" << pipeline_result.combined_source_hull.callback_aggregator.endpoint_hulls.size() << "\n";
						out << "combined_collected_trails=" << pipeline_result.combined_source_hull.callback_aggregator.collected_trail_count << "\n";
					}
					if ( final_batch_state_for_log != nullptr )
						write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( *final_batch_state_for_log ) );
				} );
		}
		memory_governor_disable_for_run();
		return 0;
	}

}  // namespace

int main( int argc, char** argv )
{
	CommandLineOptions options {};
	if ( !parse_command_line( argc, argv, options ) || options.show_help )
	{
		print_usage( ( argc > 0 && argv && argv[ 0 ] ) ? argv[ 0 ] : "test_neoalzette_linear_hull_wrapper.exe" );
		return ( options.show_help ? 0 : 1 );
	}
	if ( options.selftest )
		return run_linear_hull_wrapper_self_test( options.selftest_scope );
	const bool batch_requested =
		( options.batch_job_count > 0 ) ||
		options.batch_job_file_was_provided ||
		!options.batch_resume_checkpoint_path.empty();
	if ( batch_requested )
		return run_linear_batch_mode( options );
	if ( !options.masks_were_provided )
	{
		print_usage( ( argc > 0 && argv && argv[ 0 ] ) ? argv[ 0 ] : "test_neoalzette_linear_hull_wrapper.exe" );
		return 1;
	}
	timestamp_linear_best_search_output_paths( options );
	if ( !validate_linear_best_search_runtime_controls( options ) )
		return 1;
	ensure_default_linear_best_search_output_paths( options );

	LinearBestSearchRuntimeArtifacts best_search_artifacts {};
	if ( !configure_linear_best_search_runtime_artifacts( options, best_search_artifacts ) )
		return 1;

	LinearBestSearchConfiguration config {};
	config.search_mode = SearchMode::Strict;
	config.round_count = options.round_count;
	config.addition_weight_cap = options.addition_weight_cap;
	config.constant_subtraction_weight_cap = options.constant_subtraction_weight_cap;
	config.enable_weight_sliced_clat = options.enable_weight_sliced_clat;
	config.weight_sliced_clat_max_candidates = options.weight_sliced_clat_max_candidates;
	config.maximum_injection_input_masks = options.maximum_injection_input_masks;
	config.maximum_round_predecessors = options.maximum_round_predecessors;
	config.enable_state_memoization = options.enable_state_memoization;
	config.enable_verbose_output = options.verbose;
	enforce_strict_linear_hull_branch_caps( options, config );

	if ( options.growth_mode )
	{
		const LinearGrowthRunResult growth_result = run_linear_growth_mode( options, config, &best_search_artifacts );
		if ( growth_result.rejected )
		{
			print_linear_growth_rejection( growth_result, options );
			return 1;
		}
		print_linear_growth_result( growth_result, options );
		return 0;
	}

	LinearCallbackHullAggregator callback_aggregator {};
	callback_aggregator.set_stored_trail_policy( options.stored_trail_policy );
	callback_aggregator.set_maximum_stored_trails( static_cast<std::size_t>( options.maximum_stored_trails ) );
	const LinearStrictHullRuntimeResult runtime_result_with_callback =
		run_linear_strict_hull_runtime(
			options.output_branch_a_mask,
			options.output_branch_b_mask,
			config,
			make_linear_runtime_options( options, &best_search_artifacts, &callback_aggregator ),
			options.verbose,
			false );
	if ( !runtime_result_with_callback.collected )
	{
		print_linear_strict_runtime_rejection( runtime_result_with_callback, options, false );
		if ( runtime_result_with_callback.best_search_executed
			 && runtime_result_with_callback.strict_runtime_rejection_reason == StrictCertificationFailureReason::None
			 && runtime_result_with_callback.best_search_result.found
			 && runtime_result_with_callback.best_search_result.best_weight < INFINITE_WEIGHT )
			return 0;
		return 1;
	}

	print_result( runtime_result_with_callback, callback_aggregator, options );
	return 0;
}
