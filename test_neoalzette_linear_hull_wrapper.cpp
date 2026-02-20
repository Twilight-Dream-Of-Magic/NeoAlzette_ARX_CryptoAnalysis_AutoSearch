#include <array>
#include <algorithm>
#include <atomic>
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
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "auto_search_frame/detail/best_search_shared_core.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_subspace_hull/detail/linear_hull_collector_checkpoint.hpp"
#include "auto_subspace_hull/hull_growth_common.hpp"
#include "auto_subspace_hull/linear_hull_batch_export.hpp"

namespace
{
	using namespace TwilightDream::auto_search_linear;
	using TwilightDream::hull_callback_aggregator::GrowthStopPolicy;
	using TwilightDream::hull_callback_aggregator::LinearBatchBreadthDeepOrchestratorConfig;
	using TwilightDream::hull_callback_aggregator::LinearBatchBreadthDeepOrchestratorResult;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullBestSearchSeed;
	using TwilightDream::hull_callback_aggregator::LinearBatchJob;
	using TwilightDream::hull_callback_aggregator::LinearBatchSelectionCheckpointStage;
	using TwilightDream::hull_callback_aggregator::LinearBatchTopCandidate;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullJobSummary;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineOptions;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineCheckpointState;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullPipelineResult;
	using TwilightDream::hull_callback_aggregator::LinearBatchHullRunSummary;
	using TwilightDream::hull_callback_aggregator::LinearBatchFullSourceSpaceSummary;
	using TwilightDream::hull_callback_aggregator::LinearBatchSourceSelectionCheckpointState;
	using TwilightDream::hull_callback_aggregator::LinearCallbackHullAggregator;
	using TwilightDream::hull_callback_aggregator::compute_linear_batch_full_source_space_summary;
	using TwilightDream::hull_callback_aggregator::compute_linear_callback_hull_source_union_endpoint_l2_mass;
	using TwilightDream::hull_callback_aggregator::compute_linear_callback_hull_source_union_total_signed_correlation_theorem;
	using TwilightDream::hull_callback_aggregator::compute_linear_endpoint_hull_l2_mass;
	using TwilightDream::hull_callback_aggregator::compute_linear_source_hull_endpoint_l2_mass;
	using TwilightDream::hull_callback_aggregator::compute_linear_source_total_signed_correlation_theorem;
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

	static const char* selftest_scope_to_string( SelfTestScope scope ) noexcept;
	static bool parse_selftest_scope_value( const char* text, SelfTestScope& out ) noexcept;

	struct SubspaceExclusionEvidenceRecord
	{
		std::uint64_t source_count = 0;
		SearchWeight min_best_weight = INFINITE_WEIGHT;
		std::string label {};
	};

	struct SubspaceCoverageSummary
	{
		std::uint64_t prescribed_source_count = 0;
		std::uint64_t active_partition_source_count = 0;
		std::uint64_t evidence_record_count = 0;
		std::uint64_t evidence_source_count = 0;
		std::uint64_t evidence_source_count_excluded_within_cap = 0;
		std::uint64_t full_space_source_count = 0;
		bool full_space_source_count_was_provided = false;
		bool all_evidence_excluded_within_cap = false;
		bool full_space_exact_within_collect_weight_cap = false;
	};

	enum class GeneratedEvidenceCertification : std::uint8_t
	{
		None = 0,
		LowerBoundOnly = 1,
		ExactBestCertified = 2
	};

	struct GeneratedSubspaceEvidenceSummary
	{
		bool available = false;
		GeneratedEvidenceCertification certification = GeneratedEvidenceCertification::None;
		SearchWeight min_best_weight = INFINITE_WEIGHT;
		std::uint64_t exact_best_job_count = 0;
		std::uint64_t lower_bound_only_job_count = 0;
		std::uint64_t unresolved_job_count = 0;
		std::uint64_t prepass_job_count = 0;
		std::uint64_t prepass_total_nodes = 0;
	};

	struct CommandLineOptions
	{
		int			  round_count = 1;
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
		bool		  masks_were_provided = false;
		std::uint64_t maximum_search_nodes = 0;
		std::uint64_t maximum_search_seconds = 0;
		SearchWeight	  addition_weight_cap = 31;
		SearchWeight	  constant_subtraction_weight_cap = 32;
		bool		  enable_weight_sliced_clat = true;
		std::size_t	  weight_sliced_clat_max_candidates = 0;
		LinearVarVarModularAddBnBMode varvar_modular_add_bnb_mode =
			LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW;
		LinearVarConstSubBnbMode varconst_sub_bnb_mode =
			LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha;
		bool enable_fixed_beta_outer_hot_path_gate = false;
		bool enable_fixed_alpha_outer_hot_path_gate = false;
		std::size_t	  maximum_injection_input_masks = 0;
		std::size_t	  maximum_round_predecessors = 0;
		bool		  enable_state_memoization = true;
		SearchWeight	  collect_weight_cap = 0;
		SearchWeight	  collect_weight_window = 0;
		bool		  collect_weight_cap_was_provided = false;
		SearchWeight	  shell_start = INFINITE_WEIGHT;
		std::uint64_t shell_count = 0;
		bool		  growth_mode = false;
		SearchWeight	  growth_shell_start = INFINITE_WEIGHT;
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
		bool		  subspace_hull_mode = false;
		bool		  subspace_evidence_only_mode = false;
		std::string	  subspace_job_file {};
		bool		  subspace_job_file_was_provided = false;
		std::string	  subspace_resume_checkpoint_path {};
		std::string	  subspace_checkpoint_out_path {};
		std::string	  subspace_runtime_log_path {};
		std::uint64_t subspace_partition_count = 1;
		std::uint64_t subspace_partition_index = 0;
		std::string	  subspace_exclusion_evidence_file {};
		bool		  subspace_exclusion_evidence_file_was_provided = false;
		std::string	  subspace_evidence_out_path {};
		std::string	  subspace_evidence_label {};
		std::string	  subspace_coverage_out_path {};
		bool		  auto_exclusion_evidence = false;
		bool		  campaign_mode = false;
		bool		  campaign_adaptive_mode = false;
		std::string	  campaign_source_file {};
		bool		  campaign_source_file_was_provided = false;
		std::uint64_t campaign_partition_count = 0;
		std::string	  campaign_output_dir {};
		std::string	  campaign_label_prefix {};
		std::uint64_t full_space_source_count = 0;
		bool		  full_space_source_count_was_provided = false;
		std::string	  batch_resume_checkpoint_path {};
		std::string	  batch_checkpoint_out_path {};
		std::string	  batch_runtime_log_path {};
		std::uint64_t auto_breadth_maximum_search_nodes = ( std::numeric_limits<std::uint32_t>::max() >> 12 );
		std::uint64_t auto_deep_maximum_search_nodes = 0;
		std::uint64_t auto_max_time_seconds = 0;
		SearchWeight	  auto_target_best_weight = INFINITE_WEIGHT;
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
		std::string	  collector_runtime_log_path {};
		std::string	  collector_resume_checkpoint_path {};
		std::string	  collector_checkpoint_out_path {};
		bool		  collector_checkpoint_out_was_provided = false;
		std::uint64_t collector_checkpoint_every_seconds = 1800;
		bool		  collector_checkpoint_every_seconds_was_provided = false;
		bool		  enable_bnb_residual = false;
		bool		  bnb_residual_was_provided = false;
		bool		  selftest = false;
		SelfTestScope selftest_scope = SelfTestScope::All;
		bool		  verbose = false;
		bool		  show_help = false;
	};

	static int run_linear_batch_mode( const CommandLineOptions& options );
	static int run_linear_subspace_mode( const CommandLineOptions& options );
	static int run_linear_subspace_evidence_only_mode( const CommandLineOptions& options );
	static int run_linear_partition_campaign( const CommandLineOptions& options );

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
			<< "  --linear-varvar-bnb MODE       Var-var modular-add BnB polarity: fixed_u_enumerate_vw | fixed_vw_column_optimal_u\n"
			<< "                                fixed_vw_column_optimal_u is the strict Gap-A column-max line:\n"
			<< "                                (v,w) -> find_optimal_output_u_ccz -> u* -> linear_correlation_add_ccz_weight/value,\n"
			<< "                                with the same exact CCZ object and Θ(log n) root-depth contract as row max.\n"
			<< "  --linear-varconst-sub-bnb MODE Var-const subtraction polarity:\n"
			<< "                                fixed_beta_enumerate_alpha | fixed_beta_column_optimal_alpha |\n"
			<< "                                fixed_alpha_enumerate_beta | fixed_alpha_column_optimal_beta\n"
			<< "                                fixed_beta_* remains the current reverse-stage primary polarity.\n"
			<< "                                fixed_alpha_* uses the accepted fixed-alpha Q2 semantics where alpha is already fixed\n"
			<< "                                instead of silently treating the request as fixed_beta_*.\n"
			<< "                                In the current reverse-stage topology it applies where alpha is already fixed\n"
			<< "                                (or both sides are fixed).\n"
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
			<< "      Archive interval for the embedded residual-frontier best-search checkpoint stream.\n"
			<< "      The latest residual-frontier resumable checkpoint is still maintained internally by the watchdog.\n"
			<< "  --collector-runtime-log PATH\n"
			<< "  --collector-resume PATH\n"
			<< "  --collector-checkpoint-out PATH\n"
			<< "  --collector-checkpoint-every-seconds S\n"
			<< "      Independent residual-frontier collector checkpoint/runtime-log stream for the fixed-source hull phase.\n"
			<< "  --bnb-residual                Force collector residualization at residual boundaries.\n"
			<< "                                Default in subspace/batch parallel collector jobs is prune-at-boundary.\n"
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
			<< "  --subspace-hull                  Treat the provided source file as a prescribed strict subset S (no breadth/deep selection)\n"
			<< "  --subspace-evidence-only         Over the prescribed subset S, generate exclusion/lower-bound certificates only (no hull collector)\n"
			<< "  --subspace-file PATH             Prescribed source subset file; same line format as --batch-file\n"
			<< "  --subspace-resume PATH           Resume a prescribed-subset strict hull run at completed-job granularity\n"
			<< "  --subspace-checkpoint-out PATH   Write prescribed-subset strict hull checkpoints after each completed job\n"
			<< "  --subspace-runtime-log PATH      Write prescribed-subset runtime event log\n"
			<< "  --subspace-partition-count N     Deterministically partition the prescribed set S into N disjoint shards (default=1)\n"
			<< "  --subspace-partition-index I     Run only shard I of the partitioned prescribed set (0-based)\n"
			<< "  --subspace-evidence-file PATH    Optional exclusion/upper-bound evidence file for omitted source regions\n"
			<< "  --subspace-evidence-out PATH     Write an evidence record for the active prescribed shard after the run\n"
			<< "  --subspace-evidence-label TEXT   Optional label stored with the emitted evidence record\n"
			<< "  --subspace-coverage-out PATH     Write a machine-readable coverage summary for the active prescribed shard\n"
			<< "  --auto-exclusion-evidence        If the main strict run cannot already certify a shard lower bound, run a seeded/parallel evidence prepass only on unresolved shard jobs (explicit-cap uses a cap-bounded exclusion certificate search)\n"
			<< "  --campaign                       Run a partition campaign: enumerate shards and run strict subspace/evidence jobs per shard\n"
			<< "  --adaptive-campaign              In explicit-cap campaigns, probe each shard with evidence-only first and promote only non-excluded shards to full hull collection\n"
			<< "  --campaign-source-file PATH      Prescribed source set S for the campaign\n"
			<< "  --campaign-partition-count N     Number of deterministic shards to run in the campaign\n"
			<< "  --campaign-output-dir PATH       Output directory for shard coverage/evidence/checkpoints\n"
			<< "  --campaign-label-prefix TEXT     Optional prefix for generated shard evidence labels\n"
			<< "  --full-space-source-count N      Optional total source-space size used for full-space progress/exactness reporting\n"
			<< "  --batch-resume PATH               Resume from a batch checkpoint (selection or selected-source strict-hull stage)\n"
			<< "  --batch-checkpoint-out PATH       Write selected-source strict-hull batch checkpoints after each completed job\n"
			<< "  --batch-runtime-log PATH          Write batch runtime event log (selection + strict-hull resume events)\n"
			<< "  --thread-count T | --threads T    Worker threads (0=auto)\n"
			<< "  --seed S                          RNG seed (required for --batch-job-count)\n"
			<< "  note=best_weight_plus_window now runs a breadth proposal stage over all jobs,\n"
			<< "       then runs parallel deep best-search over every breadth-surviving source,\n"
			<< "       and finally launches strict collector/hull over that deep-certified source union.\n"
			<< "  note=Gap B exact optimal-mask operators now exist at the operator layer for fixed-mask columns,\n"
			<< "       but current linear hull strictness still relies on the transpose fixed-output subconst path inside the search frame.\n"
			<< "  note=explicit_cap still skips best-search and runs the strict collector directly per job.\n"
			<< "  note=batch checkpoint/resume works at completed-job granularity after source selection;\n"
			<< "       an in-flight strict-hull job may restart from its job boundary after resume.\n"
			<< "  --collect-weight-cap W            Optional explicit cap per job (otherwise uses strict best_weight + window)\n"
			<< "  --collect-weight-window D         Extra shells beyond strict best_weight per job\n"
			<< "  --auto-breadth-maxnodes N         Per-job breadth budget for the multi-trajectory front stage\n"
			<< "  --auto-deep-maxnodes N            Per-candidate deep budget for seed improvement (0=unlimited)\n"
			<< "  --auto-max-time-seconds S         Per-candidate deep time budget (0=unlimited)\n"
			<< "  --auto-target-best-weight W       Optional early-stop target for the deep stage\n"
			<< "                                    If hit, that deep run is threshold-certified rather than exact-best certified.\n"
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
				std::uint64_t value = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], value ) )
					return false;
				out.addition_weight_cap = static_cast<SearchWeight>( value );
				continue;
			}
			if ( ( arg == "--constant-subtraction-weight-cap" || arg == "--subtract" ) && i + 1 < argc )
			{
				std::uint64_t value = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], value ) )
					return false;
				out.constant_subtraction_weight_cap = static_cast<SearchWeight>( value );
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
			if ( arg == "--linear-varvar-bnb" && i + 1 < argc )
			{
				const std::string mode = argv[ ++i ];
				if ( mode == "fixed_u_enumerate_vw" )
					out.varvar_modular_add_bnb_mode = LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW;
				else if ( mode == "fixed_vw_column_optimal_u" )
					out.varvar_modular_add_bnb_mode = LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU;
				else
					return false;
				continue;
			}
			if ( arg == "--linear-varconst-sub-bnb" && i + 1 < argc )
			{
				const std::string mode = argv[ ++i ];
				if ( mode == "fixed_beta_enumerate_alpha" )
				{
					out.varconst_sub_bnb_mode = LinearVarConstSubBnbMode::FixedOutputMaskBeta_EnumerateInputAlpha;
					out.enable_fixed_beta_outer_hot_path_gate = false;
					out.enable_fixed_alpha_outer_hot_path_gate = false;
				}
				else if ( mode == "fixed_beta_column_optimal_alpha" )
				{
					out.varconst_sub_bnb_mode = LinearVarConstSubBnbMode::FixedOutputMaskBeta_ColumnOptimalInputAlpha;
					out.enable_fixed_beta_outer_hot_path_gate = true;
					out.enable_fixed_alpha_outer_hot_path_gate = false;
				}
				else if ( mode == "fixed_alpha_enumerate_beta" )
				{
					out.varconst_sub_bnb_mode = LinearVarConstSubBnbMode::FixedInputMaskAlpha_EnumerateOutputBeta;
					out.enable_fixed_beta_outer_hot_path_gate = false;
					out.enable_fixed_alpha_outer_hot_path_gate = false;
				}
				else if ( mode == "fixed_alpha_column_optimal_beta" )
				{
					out.varconst_sub_bnb_mode = LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
					out.enable_fixed_beta_outer_hot_path_gate = false;
					out.enable_fixed_alpha_outer_hot_path_gate = true;
				}
				else
					return false;
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
				std::uint64_t collect_weight_cap = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], collect_weight_cap ) )
					return false;
				out.collect_weight_cap = collect_weight_cap;
				out.collect_weight_cap_was_provided = true;
				continue;
			}
			if ( arg == "--collect-weight-window" && i + 1 < argc )
			{
				std::uint64_t collect_weight_window = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], collect_weight_window ) )
					return false;
				out.collect_weight_window = collect_weight_window;
				continue;
			}
			if ( arg == "--shell-start" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.shell_start ) )
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
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.growth_shell_start ) )
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
			if ( arg == "--collector-runtime-log" && i + 1 < argc )
			{
				out.collector_runtime_log_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--collector-resume" && i + 1 < argc )
			{
				out.collector_resume_checkpoint_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--collector-checkpoint-out" && i + 1 < argc )
			{
				out.collector_checkpoint_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.collector_checkpoint_out_was_provided = true;
				continue;
			}
			if ( arg == "--collector-checkpoint-every-seconds" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.collector_checkpoint_every_seconds ) )
					return false;
				out.collector_checkpoint_every_seconds_was_provided = true;
				continue;
			}
			if ( arg == "--bnb-residual" )
			{
				out.enable_bnb_residual = true;
				out.bnb_residual_was_provided = true;
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
			if ( arg == "--subspace-hull" )
			{
				out.subspace_hull_mode = true;
				continue;
			}
			if ( arg == "--subspace-file" && i + 1 < argc )
			{
				out.subspace_job_file = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.subspace_job_file_was_provided = true;
				continue;
			}
			if ( arg == "--subspace-evidence-only" )
			{
				out.subspace_evidence_only_mode = true;
				continue;
			}
			if ( arg == "--subspace-resume" && i + 1 < argc )
			{
				out.subspace_resume_checkpoint_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--subspace-checkpoint-out" && i + 1 < argc )
			{
				out.subspace_checkpoint_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--subspace-runtime-log" && i + 1 < argc )
			{
				out.subspace_runtime_log_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--subspace-partition-count" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.subspace_partition_count ) )
					return false;
				continue;
			}
			if ( arg == "--subspace-partition-index" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.subspace_partition_index ) )
					return false;
				continue;
			}
			if ( arg == "--subspace-evidence-file" && i + 1 < argc )
			{
				out.subspace_exclusion_evidence_file = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.subspace_exclusion_evidence_file_was_provided = true;
				continue;
			}
			if ( arg == "--subspace-evidence-out" && i + 1 < argc )
			{
				out.subspace_evidence_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--subspace-evidence-label" && i + 1 < argc )
			{
				out.subspace_evidence_label = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--subspace-coverage-out" && i + 1 < argc )
			{
				out.subspace_coverage_out_path = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--auto-exclusion-evidence" )
			{
				out.auto_exclusion_evidence = true;
				continue;
			}
			if ( arg == "--campaign" )
			{
				out.campaign_mode = true;
				continue;
			}
			if ( arg == "--adaptive-campaign" )
			{
				out.campaign_mode = true;
				out.campaign_adaptive_mode = true;
				continue;
			}
			if ( arg == "--campaign-source-file" && i + 1 < argc )
			{
				out.campaign_source_file = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				out.campaign_source_file_was_provided = true;
				continue;
			}
			if ( arg == "--campaign-partition-count" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.campaign_partition_count ) )
					return false;
				continue;
			}
			if ( arg == "--campaign-output-dir" && i + 1 < argc )
			{
				out.campaign_output_dir = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--campaign-label-prefix" && i + 1 < argc )
			{
				out.campaign_label_prefix = argv[ ++i ] ? std::string( argv[ i ] ) : std::string();
				continue;
			}
			if ( arg == "--full-space-source-count" && i + 1 < argc )
			{
				if ( !parse_unsigned_integer_64( argv[ ++i ], out.full_space_source_count ) )
					return false;
				out.full_space_source_count_was_provided = true;
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
				std::uint64_t auto_target_best_weight = 0;
				if ( !parse_unsigned_integer_64( argv[ ++i ], auto_target_best_weight ) )
					return false;
				out.auto_target_best_weight = auto_target_best_weight;
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

	struct LinearCollectorRuntimeArtifacts
	{
		RuntimeEventLog runtime_log {};
		bool runtime_log_enabled = false;
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
		fp.mix_u64( summary.best_weight );
		fp.mix_u64( summary.collect_weight_cap );
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
		fp.mix_u64( candidate.best_weight );
		fp.mix_u64( candidate.nodes );
		fp.mix_bool( candidate.hit_maximum_search_nodes_limit );
		fp.mix_u32( candidate.best_input_mask_a );
		fp.mix_u32( candidate.best_input_mask_b );
		fp.mix_u64( static_cast<std::uint64_t>( candidate.trail.size() ) );
		for ( const auto& step : candidate.trail )
		{
			fp.mix_i32( step.round_index );
			fp.mix_u64( step.round_weight );
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
		fp.mix_u64( state.runtime_options_snapshot.explicit_collect_weight_cap );
		fp.mix_u64( state.runtime_options_snapshot.collect_weight_window );
		fp.mix_u64( state.runtime_options_snapshot.maximum_collected_trails );
		fp.mix_enum( state.runtime_options_snapshot.residual_boundary_mode );
		fp.mix_bool( state.enable_combined_source_aggregator );
		fp.mix_enum( state.source_namespace );
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

	static std::string default_linear_subspace_runtime_log_path( const std::string& checkpoint_path ) noexcept
	{
		if ( !checkpoint_path.empty() )
			return checkpoint_path + ".runtime.log";
		return RuntimeEventLog::default_path( "linear_hull_subspace" );
	}

	static std::string default_linear_collector_runtime_log_path( const CommandLineOptions& options ) noexcept
	{
		return default_linear_hull_collector_runtime_log_path( options.round_count, options.output_branch_a_mask, options.output_branch_b_mask );
	}

	static bool linear_collector_controls_requested( const CommandLineOptions& options ) noexcept
	{
		return
			!options.collector_runtime_log_path.empty() ||
			!options.collector_resume_checkpoint_path.empty() ||
			options.collector_checkpoint_out_was_provided ||
			options.collector_checkpoint_every_seconds_was_provided;
	}

	static bool linear_collector_phase_will_execute( const CommandLineOptions& options ) noexcept
	{
		return !options.growth_mode && options.batch_job_count == 0 && !options.batch_job_file_was_provided && options.batch_resume_checkpoint_path.empty();
	}

	static const std::string& linear_effective_collector_checkpoint_path( const CommandLineOptions& options ) noexcept
	{
		if ( options.collector_checkpoint_out_was_provided && !options.collector_checkpoint_out_path.empty() )
			return options.collector_checkpoint_out_path;
		return options.collector_resume_checkpoint_path;
	}

	static void timestamp_linear_collector_output_paths( CommandLineOptions& options )
	{
		if ( !options.collector_runtime_log_path.empty() )
			options.collector_runtime_log_path = append_timestamp_to_artifact_path( options.collector_runtime_log_path );
		if ( options.collector_checkpoint_out_was_provided && !options.collector_checkpoint_out_path.empty() )
			options.collector_checkpoint_out_path = append_timestamp_to_artifact_path( options.collector_checkpoint_out_path );
	}

	static void ensure_default_linear_collector_output_paths( CommandLineOptions& options )
	{
		if ( !linear_collector_controls_requested( options ) )
			return;

		std::string resume_runtime_log_path {};
		if ( !options.collector_resume_checkpoint_path.empty() )
		{
			LinearHullCollectorCheckpointState load {};
			if ( read_linear_hull_collector_checkpoint( options.collector_resume_checkpoint_path, load ) )
				resume_runtime_log_path = load.runtime_log_path;
		}
		if ( options.collector_runtime_log_path.empty() )
		{
			options.collector_runtime_log_path =
				!resume_runtime_log_path.empty() ?
					resume_runtime_log_path :
					default_linear_collector_runtime_log_path( options );
		}
	}

	static bool validate_linear_collector_runtime_controls( const CommandLineOptions& options )
	{
		if ( !linear_collector_phase_will_execute( options ) && linear_collector_controls_requested( options ) )
		{
			std::cerr << "[HullWrapper][Linear] ERROR: collector-only options are supported only in fixed-source hull mode right now.\n";
			return false;
		}
		if ( !options.best_search_resume_checkpoint_path.empty() && !options.collector_resume_checkpoint_path.empty() )
		{
			std::cerr << "[HullWrapper][Linear] ERROR: --best-search-resume and --collector-resume are mutually exclusive.\n";
			return false;
		}
		if ( options.collector_checkpoint_every_seconds_was_provided &&
			 !options.collector_checkpoint_out_was_provided &&
			 options.collector_resume_checkpoint_path.empty() )
		{
			std::cerr << "[HullWrapper][Linear] ERROR: --collector-checkpoint-every-seconds requires --collector-checkpoint-out or --collector-resume.\n";
			return false;
		}
		return true;
	}

	static bool configure_linear_collector_runtime_artifacts( const CommandLineOptions& options, LinearCollectorRuntimeArtifacts& artifacts )
	{
		if ( !options.collector_runtime_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					artifacts.runtime_log,
					options.collector_runtime_log_path,
					"[HullWrapper][Linear] ERROR: cannot open collector runtime log: " ) )
				return false;
			artifacts.runtime_log_enabled = true;
		}
		return true;
	}

	static void write_linear_collector_runtime_event(
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
			return options.growth_shell_start >= INFINITE_WEIGHT;
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

	static void print_linear_collector_artifact_status( const CommandLineOptions& options )
	{
		std::cout << "  collector_runtime_log_enabled=" << ( options.collector_runtime_log_path.empty() ? 0 : 1 ) << "\n";
		if ( !options.collector_runtime_log_path.empty() )
			std::cout << "  collector_runtime_log_path=" << options.collector_runtime_log_path << "\n";
		const bool collector_checkpoint_enabled = options.collector_checkpoint_out_was_provided || !options.collector_resume_checkpoint_path.empty();
		std::cout << "  collector_checkpoint_enabled=" << ( collector_checkpoint_enabled ? 1 : 0 ) << "\n";
		if ( !options.collector_resume_checkpoint_path.empty() )
			std::cout << "  collector_resume_checkpoint_path=" << options.collector_resume_checkpoint_path << "\n";
		if ( options.collector_checkpoint_out_was_provided && !options.collector_checkpoint_out_path.empty() )
			std::cout << "  collector_checkpoint_out_path=" << options.collector_checkpoint_out_path << "\n";
		if ( collector_checkpoint_enabled )
		{
			std::cout << "  collector_checkpoint_effective_path=" << linear_effective_collector_checkpoint_path( options ) << "\n";
			std::cout << "  collector_checkpoint_every_seconds=" << options.collector_checkpoint_every_seconds << "\n";
		}
	}

	[[maybe_unused]] static const char* linear_collector_residual_boundary_mode_to_string( LinearCollectorResidualBoundaryMode mode ) noexcept
	{
		switch ( mode )
		{
		case LinearCollectorResidualBoundaryMode::ContinueResidualSearch:
			return "continue";
		case LinearCollectorResidualBoundaryMode::PruneAtResidualBoundary:
			return "prune";
		default:
			return "unknown";
		}
	}

	static LinearCollectorResidualBoundaryMode resolve_linear_collector_residual_boundary_mode(
		const CommandLineOptions& options,
		bool parallel_best_weight_jobs_for_hull ) noexcept
	{
		if ( options.enable_bnb_residual )
			return LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
		return
			parallel_best_weight_jobs_for_hull ?
				LinearCollectorResidualBoundaryMode::PruneAtResidualBoundary :
				LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
	}

	static LinearHullSourceContext make_fixed_linear_source_context( const CommandLineOptions& options ) noexcept
	{
		return LinearHullSourceContext {
			true,
			0,
			TwilightDream::hull_callback_aggregator::OuterSourceNamespace::WrapperFixedSource,
			options.round_count,
			options.output_branch_a_mask,
			options.output_branch_b_mask
		};
	}

	static LinearStrictHullRuntimeOptions make_linear_runtime_options(
		const CommandLineOptions& options,
		LinearBestSearchRuntimeArtifacts* artifacts = nullptr,
		LinearCallbackHullAggregator* aggregator = nullptr,
		bool parallel_best_weight_jobs_for_hull = false )
	{
		LinearStrictHullRuntimeOptions runtime_options {};
		runtime_options.maximum_collected_trails = options.maximum_collected_trails;
		runtime_options.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
		runtime_options.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
		runtime_options.runtime_controls.progress_every_seconds = options.progress_every_seconds;
		runtime_options.runtime_controls.checkpoint_every_seconds = options.best_search_checkpoint_every_seconds;
		runtime_options.residual_boundary_mode =
			resolve_linear_collector_residual_boundary_mode( options, parallel_best_weight_jobs_for_hull );
			runtime_options.best_search_resume_checkpoint_path = options.best_search_resume_checkpoint_path;
			if ( artifacts )
			{
				runtime_options.best_search_history_log = artifacts->history_log_enabled ? &artifacts->history_log : nullptr;
				runtime_options.best_search_runtime_log = artifacts->runtime_log_enabled ? &artifacts->runtime_log : nullptr;
				runtime_options.best_search_binary_checkpoint = artifacts->binary_checkpoint_enabled ? &artifacts->binary_checkpoint : nullptr;
			}
		if ( aggregator )
			runtime_options.on_trail = aggregator->make_callback_for_source( make_fixed_linear_source_context( options ) );
		if ( options.collect_weight_cap_was_provided )
		{
			runtime_options.collect_cap_mode = HullCollectCapMode::ExplicitCap;
			runtime_options.explicit_collect_weight_cap = options.collect_weight_cap;
		}
		else
		{
			runtime_options.collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
			runtime_options.collect_weight_window = options.collect_weight_window;
		}
		return runtime_options;
	}

static LinearStrictHullRuntimeOptions make_linear_explicit_cap_runtime_options(
		const CommandLineOptions& options,
		LinearBestSearchRuntimeArtifacts* artifacts,
		SearchWeight collect_weight_cap,
		LinearCallbackHullAggregator* aggregator = nullptr,
		bool parallel_best_weight_jobs_for_hull = false )
	{
		LinearStrictHullRuntimeOptions runtime_options {};
		runtime_options.collect_cap_mode = HullCollectCapMode::ExplicitCap;
		runtime_options.explicit_collect_weight_cap = collect_weight_cap;
		runtime_options.maximum_collected_trails = options.maximum_collected_trails;
		runtime_options.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
		runtime_options.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
		runtime_options.runtime_controls.progress_every_seconds = options.progress_every_seconds;
		runtime_options.runtime_controls.checkpoint_every_seconds = options.best_search_checkpoint_every_seconds;
		runtime_options.residual_boundary_mode =
			resolve_linear_collector_residual_boundary_mode( options, parallel_best_weight_jobs_for_hull );
			runtime_options.best_search_resume_checkpoint_path = options.best_search_resume_checkpoint_path;
			if ( artifacts )
			{
				runtime_options.best_search_history_log = artifacts->history_log_enabled ? &artifacts->history_log : nullptr;
				runtime_options.best_search_runtime_log = artifacts->runtime_log_enabled ? &artifacts->runtime_log : nullptr;
				runtime_options.best_search_binary_checkpoint = artifacts->binary_checkpoint_enabled ? &artifacts->binary_checkpoint : nullptr;
			}
		if ( aggregator )
			runtime_options.on_trail = aggregator->make_callback_for_source( make_fixed_linear_source_context( options ) );
		return runtime_options;
	}

	static void populate_linear_collector_checkpoint_state(
		LinearHullCollectorCheckpointState& checkpoint_state,
		const LinearBestSearchConfiguration& config,
		const LinearHullCollectorExecutionState& collector_state,
		const LinearCallbackHullAggregator& callback_aggregator,
		const LinearStrictHullRuntimeResult& runtime_result,
		const std::string& runtime_log_path )
	{
		checkpoint_state.~LinearHullCollectorCheckpointState();
		::new ( static_cast<void*>( &checkpoint_state ) ) LinearHullCollectorCheckpointState {};
		checkpoint_state.base_configuration = config;
		checkpoint_state.collector_state.collect_weight_cap = collector_state.collect_weight_cap;
		checkpoint_state.collector_state.maximum_collected_trails = collector_state.maximum_collected_trails;
		checkpoint_state.collector_state.residual_boundary_mode = collector_state.residual_boundary_mode;
		checkpoint_state.collector_state.aggregation_result = collector_state.aggregation_result;
		checkpoint_state.collector_state.cursor = collector_state.cursor;
		checkpoint_state.collector_state.context.configuration = collector_state.context.configuration;
		checkpoint_state.collector_state.context.runtime_controls = collector_state.context.runtime_controls;
		checkpoint_state.collector_state.context.start_output_branch_a_mask = collector_state.context.start_output_branch_a_mask;
		checkpoint_state.collector_state.context.start_output_branch_b_mask = collector_state.context.start_output_branch_b_mask;
		checkpoint_state.collector_state.context.visited_node_count = collector_state.context.visited_node_count;
		checkpoint_state.collector_state.context.accumulated_elapsed_usec = collector_state.context.accumulated_elapsed_usec;
		checkpoint_state.collector_state.context.current_linear_trail = collector_state.context.current_linear_trail;
		checkpoint_state.callback_aggregator = callback_aggregator;
		checkpoint_state.output_branch_a_mask = collector_state.context.start_output_branch_a_mask;
		checkpoint_state.output_branch_b_mask = collector_state.context.start_output_branch_b_mask;
		checkpoint_state.used_best_weight_reference = runtime_result.used_best_weight_reference;
		checkpoint_state.best_search_executed = runtime_result.best_search_executed;
		checkpoint_state.best_search_result = runtime_result.best_search_result;
		checkpoint_state.runtime_log_path = runtime_log_path;
	}

	static bool write_linear_collector_checkpoint_with_log(
		const std::string& path,
		const LinearHullCollectorCheckpointState& checkpoint_state,
		RuntimeEventLog* runtime_log,
		const char* reason )
	{
		const bool ok = write_linear_hull_collector_checkpoint( path, checkpoint_state );
		if ( runtime_log )
		{
			write_linear_collector_runtime_event(
				*runtime_log,
				"collector_checkpoint_write",
				[&]( std::ostream& out ) {
					out << "checkpoint_path=" << path << "\n";
					out << "checkpoint_reason=" << ( reason ? reason : "collector_checkpoint" ) << "\n";
					out << "checkpoint_write_result=" << ( ok ? "success" : "failure" ) << "\n";
					out << "round_count=" << checkpoint_state.base_configuration.round_count << "\n";
					out << "start_output_mask_branch_a=" << TwilightDream::runtime_component::hex8( checkpoint_state.output_branch_a_mask ) << "\n";
					out << "start_output_mask_branch_b=" << TwilightDream::runtime_component::hex8( checkpoint_state.output_branch_b_mask ) << "\n";
					out << "collect_weight_cap=" << checkpoint_state.collector_state.collect_weight_cap << "\n";
					out << "nodes_visited=" << checkpoint_state.collector_state.context.visited_node_count << "\n";
					out << "collected_trails=" << checkpoint_state.collector_state.aggregation_result.collected_trail_count << "\n";
					out << "cursor_stack_depth=" << checkpoint_state.collector_state.cursor.stack.size() << "\n";
				} );
		}
		return ok;
	}

	using LinearGrowthRunResult =
		TwilightDream::hull_callback_aggregator::OuterGrowthDriverResult<
			LinearGrowthShellRow,
			MatsuiSearchRunLinearResult,
			StrictCertificationFailureReason>;

	static LinearStrictHullRuntimeResult run_linear_fixed_source_hull_with_separate_collector_artifacts(
		const CommandLineOptions& options,
		const LinearBestSearchConfiguration& config,
		LinearBestSearchRuntimeArtifacts* best_search_artifacts,
		LinearCollectorRuntimeArtifacts* collector_artifacts,
		LinearCallbackHullAggregator& callback_aggregator )
	{
		LinearStrictHullRuntimeResult runtime_result {};
		const LinearHullSourceContext source_context = make_fixed_linear_source_context( options );

		auto write_checkpoint_snapshot =
			[&]( const LinearHullCollectorExecutionState& collector_state, const char* reason ) {
				if ( !options.collector_checkpoint_out_was_provided && options.collector_resume_checkpoint_path.empty() )
					return true;
				LinearHullCollectorCheckpointState checkpoint_state {};
				populate_linear_collector_checkpoint_state(
					checkpoint_state,
					config,
					collector_state,
					callback_aggregator,
					runtime_result,
					options.collector_runtime_log_path );
				return write_linear_collector_checkpoint_with_log(
					linear_effective_collector_checkpoint_path( options ),
					checkpoint_state,
					collector_artifacts && collector_artifacts->runtime_log_enabled ? &collector_artifacts->runtime_log : nullptr,
					reason );
			};

		auto run_collector_phase =
			[&]( LinearHullCollectorExecutionState& collector_state ) {
				LinearHullTrailCallback on_trail = callback_aggregator.make_callback_for_source( source_context );
				if ( collector_artifacts && collector_artifacts->runtime_log_enabled )
				{
					write_linear_collector_runtime_event(
						collector_artifacts->runtime_log,
						"collector_start",
						[&]( std::ostream& out ) {
							out << "round_count=" << config.round_count << "\n";
							out << "start_output_mask_branch_a=" << TwilightDream::runtime_component::hex8( options.output_branch_a_mask ) << "\n";
							out << "start_output_mask_branch_b=" << TwilightDream::runtime_component::hex8( options.output_branch_b_mask ) << "\n";
							out << "collect_weight_cap=" << collector_state.collect_weight_cap << "\n";
						} );
				}

				continue_linear_hull_collection_from_state(
					collector_state,
					std::move( on_trail ),
					[&]() { (void) write_checkpoint_snapshot( collector_state, "collector_periodic" ); } );

				if ( collector_artifacts && collector_artifacts->runtime_log_enabled )
				{
					write_linear_collector_runtime_event(
						collector_artifacts->runtime_log,
						"collector_stop",
						[&]( std::ostream& out ) {
							out << "collect_weight_cap=" << collector_state.collect_weight_cap << "\n";
							out << "nodes_visited=" << collector_state.context.visited_node_count << "\n";
							out << "collected_trails=" << collector_state.aggregation_result.collected_trail_count << "\n";
							out << "exact_within_collect_weight_cap=" << ( collector_state.aggregation_result.exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
							out << "cursor_stack_depth=" << collector_state.cursor.stack.size() << "\n";
						} );
				}
			};

		if ( !options.collector_resume_checkpoint_path.empty() )
		{
			LinearHullCollectorCheckpointState checkpoint_state {};
			if ( !read_linear_hull_collector_checkpoint( options.collector_resume_checkpoint_path, checkpoint_state ) )
			{
				runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::CheckpointLoadFailed;
				return runtime_result;
			}
			if ( checkpoint_state.output_branch_a_mask != options.output_branch_a_mask || checkpoint_state.output_branch_b_mask != options.output_branch_b_mask || checkpoint_state.base_configuration.round_count != options.round_count )
			{
				runtime_result.strict_runtime_rejection_reason = StrictCertificationFailureReason::ResumeCheckpointMismatch;
				return runtime_result;
			}

			runtime_result.used_best_weight_reference = checkpoint_state.used_best_weight_reference;
			runtime_result.best_search_executed = checkpoint_state.best_search_executed;
			runtime_result.best_search_result = checkpoint_state.best_search_result;
			callback_aggregator = checkpoint_state.callback_aggregator;
			callback_aggregator.set_stored_trail_policy( options.stored_trail_policy );
			callback_aggregator.set_maximum_stored_trails( static_cast<std::size_t>( options.maximum_stored_trails ) );
			if ( options.enable_bnb_residual )
				checkpoint_state.collector_state.residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;

			if ( options.maximum_search_nodes != 0 )
				checkpoint_state.collector_state.context.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
			if ( options.maximum_search_seconds != 0 )
				checkpoint_state.collector_state.context.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
			if ( options.progress_every_seconds != 0 )
				checkpoint_state.collector_state.context.runtime_controls.progress_every_seconds = options.progress_every_seconds;
			checkpoint_state.collector_state.context.runtime_controls.checkpoint_every_seconds =
				options.collector_checkpoint_every_seconds_was_provided ?
					options.collector_checkpoint_every_seconds :
					checkpoint_state.collector_state.context.runtime_controls.checkpoint_every_seconds;

			if ( collector_artifacts && collector_artifacts->runtime_log_enabled )
			{
				write_linear_collector_runtime_event(
					collector_artifacts->runtime_log,
					"collector_resume_start",
					[&]( std::ostream& out ) {
						out << "resume_checkpoint_path=" << options.collector_resume_checkpoint_path << "\n";
						out << "collect_weight_cap=" << checkpoint_state.collector_state.collect_weight_cap << "\n";
						out << "nodes_visited=" << checkpoint_state.collector_state.context.visited_node_count << "\n";
						out << "collected_trails=" << checkpoint_state.collector_state.aggregation_result.collected_trail_count << "\n";
						out << "cursor_stack_depth=" << checkpoint_state.collector_state.cursor.stack.size() << "\n";
					} );
			}

			run_collector_phase( checkpoint_state.collector_state );
			runtime_result.collect_weight_cap = checkpoint_state.collector_state.collect_weight_cap;
			runtime_result.aggregation_result = checkpoint_state.collector_state.aggregation_result;
			runtime_result.collected = true;
			(void) write_checkpoint_snapshot( checkpoint_state.collector_state, "collector_stop" );
			return runtime_result;
		}

		LinearHullCollectionOptions collection_options {};
		collection_options.maximum_collected_trails = options.maximum_collected_trails;
		collection_options.runtime_controls.maximum_search_nodes = options.maximum_search_nodes;
		collection_options.runtime_controls.maximum_search_seconds = options.maximum_search_seconds;
		collection_options.runtime_controls.progress_every_seconds = options.progress_every_seconds;
		collection_options.runtime_controls.checkpoint_every_seconds = options.collector_checkpoint_every_seconds;
		collection_options.residual_boundary_mode =
			resolve_linear_collector_residual_boundary_mode( options, false );

		if ( options.collect_weight_cap_was_provided )
		{
			LinearHullCollectorExecutionState collector_state {};
			runtime_result.collect_weight_cap = options.collect_weight_cap;
			collection_options.collect_weight_cap = runtime_result.collect_weight_cap;
			initialize_linear_hull_collection_state( options.output_branch_a_mask, options.output_branch_b_mask, config, collection_options, collector_state );
			run_collector_phase( collector_state );
			runtime_result.aggregation_result = collector_state.aggregation_result;
			runtime_result.aggregation_result.best_weight_certified = false;
			runtime_result.collected = true;
			(void) write_checkpoint_snapshot( collector_state, "collector_stop" );
			return runtime_result;
		}

		runtime_result.used_best_weight_reference = true;
		runtime_result.best_search_executed = true;
		const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail = nullptr;
		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			runtime_result.best_search_result =
				run_linear_best_search_resume(
					options.best_search_resume_checkpoint_path,
					options.output_branch_a_mask,
					options.output_branch_b_mask,
					config,
					LinearBestSearchRuntimeControls {
						options.maximum_search_nodes,
						options.maximum_search_seconds,
						options.progress_every_seconds,
						options.best_search_checkpoint_every_seconds },
					options.verbose,
					false,
					best_search_artifacts && best_search_artifacts->history_log_enabled ? &best_search_artifacts->history_log : nullptr,
					best_search_artifacts && best_search_artifacts->binary_checkpoint_enabled ? &best_search_artifacts->binary_checkpoint : nullptr,
					best_search_artifacts && best_search_artifacts->runtime_log_enabled ? &best_search_artifacts->runtime_log : nullptr,
					nullptr );
		}
		else
		{
			runtime_result.best_search_result =
				run_linear_best_search(
					options.output_branch_a_mask,
					options.output_branch_b_mask,
					config,
					LinearBestSearchRuntimeControls {
						options.maximum_search_nodes,
						options.maximum_search_seconds,
						options.progress_every_seconds,
						options.best_search_checkpoint_every_seconds },
					options.verbose,
					false,
					INFINITE_WEIGHT,
					seeded_upper_bound_trail,
					best_search_artifacts && best_search_artifacts->history_log_enabled ? &best_search_artifacts->history_log : nullptr,
					best_search_artifacts && best_search_artifacts->binary_checkpoint_enabled ? &best_search_artifacts->binary_checkpoint : nullptr,
					best_search_artifacts && best_search_artifacts->runtime_log_enabled ? &best_search_artifacts->runtime_log : nullptr,
					nullptr );
		}

		if ( !runtime_result.best_search_result.best_weight_certified )
		{
			runtime_result.strict_runtime_rejection_reason = runtime_result.best_search_result.strict_rejection_reason;
			return runtime_result;
		}

		runtime_result.collect_weight_cap =
			saturating_add_search_weight( runtime_result.best_search_result.best_weight, options.collect_weight_window );
		collection_options.collect_weight_cap = runtime_result.collect_weight_cap;
		LinearHullCollectorExecutionState collector_state {};
		initialize_linear_hull_collection_state( options.output_branch_a_mask, options.output_branch_b_mask, config, collection_options, collector_state );
		run_collector_phase( collector_state );
		runtime_result.aggregation_result = collector_state.aggregation_result;
		runtime_result.aggregation_result.best_weight_certified = runtime_result.best_search_result.best_weight_certified;
		runtime_result.collected = true;
		(void) write_checkpoint_snapshot( collector_state, "collector_stop" );
		return runtime_result;
	}

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
			options.growth_shell_start >= INFINITE_WEIGHT,
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
				state.callback_aggregator.annotate_source_runtime_result( make_fixed_linear_source_context( options ), state.runtime_result );
				return state;
			},
			[&]( SearchWeight shell_weight ) {
				LinearGrowthRuntimeState state {};
				state.runtime_result =
					run_linear_strict_hull_runtime(
						options.output_branch_a_mask,
						options.output_branch_b_mask,
						config,
						make_linear_explicit_cap_runtime_options( options, artifacts, shell_weight, &state.callback_aggregator ),
						false,
						false );
				state.callback_aggregator.annotate_source_runtime_result( make_fixed_linear_source_context( options ), state.runtime_result );
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
			[]( SearchWeight shell_weight, const LinearGrowthRuntimeState& state ) {
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
		print_linear_collector_artifact_status( options );
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
			const long double source_total_signed_correlation_theorem =
				compute_linear_source_total_signed_correlation_theorem(
					source_summary.source_rounds,
					source_summary.source_output_branch_a_mask,
					source_summary.source_output_branch_b_mask );
			const long double source_residual_signed_correlation_exact =
				source_total_signed_correlation_theorem - source_summary.aggregate_signed_correlation;
			const long double source_signed_weight = correlation_to_weight( source_summary.aggregate_signed_correlation );
			const long double source_abs_mass_weight = correlation_to_weight( source_summary.aggregate_abs_correlation_mass );
			const long double source_collected_endpoint_l2_mass = compute_linear_source_hull_endpoint_l2_mass( source_summary );
			const long double source_residual_endpoint_l2_mass_exact = 1.0L - source_collected_endpoint_l2_mass;
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
					  << "  source_total_signed_correlation_theorem=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( source_total_signed_correlation_theorem )
					  << "  source_residual_signed_correlation_exact=" << static_cast<double>( source_residual_signed_correlation_exact )
					  << "  source_total_endpoint_l2_mass_theorem=" << static_cast<double>( 1.0L )
					  << "  source_collected_endpoint_l2_mass=" << static_cast<double>( source_collected_endpoint_l2_mass )
					  << "  source_residual_endpoint_l2_mass_exact=" << static_cast<double>( source_residual_endpoint_l2_mass_exact )
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
			print_linear_collector_artifact_status( options );
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
		print_linear_collector_artifact_status( options );
		if ( runtime_result.used_best_weight_reference )
			std::cout << "  collect_weight_window=" << options.collect_weight_window << "\n";
		std::cout << "  collect_weight_cap=" << runtime_result.collect_weight_cap << "\n";
		if ( runtime_result.best_search_executed && best_result.found && best_result.best_weight < INFINITE_WEIGHT )
			std::cout << "  best_search_best_weight=" << best_result.best_weight << "\n";
		std::cout << "  best_weight_certified=" << ( aggregation_result.best_weight_certified ? 1 : 0 ) << "\n";
		if ( runtime_result.best_search_executed )
			std::cout << "  best_weight_certification=" << best_weight_certification_status_to_string( best_weight_certification_status( best_result ) ) << "\n";
		if ( runtime_result.best_search_executed )
			std::cout << "  best_search_strict_rejection_reason=" << strict_certification_failure_reason_to_string( best_result.strict_rejection_reason ) << "\n";
		std::cout << "  source_hull_count=" << callback_aggregator.source_hulls.size() << "\n";
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
		std::cout << "  note=source_hull_summary rows are equally strict per-source objects; for a fixed-source run there should be exactly one source hull mirroring the chosen output-mask pair\n";
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
		config.varvar_modular_add_bnb_mode = options.varvar_modular_add_bnb_mode;
		config.varconst_sub_bnb_mode = options.varconst_sub_bnb_mode;
		config.enable_fixed_beta_outer_hot_path_gate = options.enable_fixed_beta_outer_hot_path_gate;
		config.enable_fixed_alpha_outer_hot_path_gate = options.enable_fixed_alpha_outer_hot_path_gate;
		config.enable_fixed_alpha_outer_hot_path_gate =
			options.varconst_sub_bnb_mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
		config.maximum_injection_input_masks = options.maximum_injection_input_masks;
		config.maximum_round_predecessors = options.maximum_round_predecessors;
		config.enable_state_memoization = options.enable_state_memoization;
		config.enable_verbose_output = options.verbose;
		enforce_strict_linear_hull_branch_caps( options, config );
		return config;
	}

#include "test_subspace_hull_self_test_linear.cpp"
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

	static void print_linear_subspace_hull_job_summary( const LinearBatchHullJobSummary& summary )
	{
		const char* status = "OK";
		if ( !summary.collected )
			status = "REJECTED";
		else if ( !summary.exact_within_collect_weight_cap )
			status = "PARTIAL";

		std::cout << "[Subspace][LinearHull][Job " << ( summary.job_index + 1 ) << "][" << status << "]"
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
		const std::vector<LinearBatchJob>& full_source_jobs,
		std::size_t selected_source_count )
	{
		if ( !pipeline_result.combined_source_hull.enabled )
			return;

		const LinearCallbackHullAggregator& combined_aggregator = pipeline_result.combined_source_hull.callback_aggregator;
		const LinearBatchFullSourceSpaceSummary full_source_space_summary =
			compute_linear_batch_full_source_space_summary(
				full_source_jobs,
				pipeline_result.combined_source_hull );
		const std::size_t input_job_count = full_source_jobs.size();
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
		std::cout << "  source_union_total_signed_correlation_theorem=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( pipeline_result.combined_source_hull.source_union_total_signed_correlation_theorem )
				  << "  source_union_residual_signed_correlation_exact=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_residual_signed_correlation_exact )
				  << "  aggregate_endpoint_l2_mass=" << static_cast<double>( pipeline_result.combined_source_hull.aggregate_endpoint_l2_mass )
				  << "  source_union_total_endpoint_l2_mass_theorem=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_total_endpoint_l2_mass_theorem )
				  << "  source_union_residual_endpoint_l2_mass_exact=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_residual_endpoint_l2_mass_exact )
				  << "  source_union_residual_endpoint_l2_mass_upper_bound=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_residual_endpoint_l2_mass_upper_bound )
				  << "  source_union_endpoint_l2_mass_residual_certified_zero=" << ( pipeline_result.combined_source_hull.source_union_endpoint_l2_mass_residual_certified_zero ? 1 : 0 )
				  << std::defaultfloat << "\n";
		std::cout << "  batch_source_space_source_count=" << full_source_space_summary.full_source_count
				  << "  batch_source_space_total_signed_correlation_theorem=" << std::scientific << std::setprecision( 10 ) << static_cast<double>( full_source_space_summary.total_signed_correlation_theorem )
				  << "  batch_source_space_residual_signed_correlation_exact=" << static_cast<double>( full_source_space_summary.residual_signed_correlation_exact )
				  << "  batch_source_space_total_endpoint_l2_mass_theorem=" << static_cast<double>( full_source_space_summary.total_endpoint_l2_mass_theorem )
				  << "  batch_source_space_residual_endpoint_l2_mass_exact=" << static_cast<double>( full_source_space_summary.residual_endpoint_l2_mass_exact )
				  << "  batch_source_space_residual_endpoint_l2_mass_upper_bound=" << static_cast<double>( full_source_space_summary.residual_endpoint_l2_mass_upper_bound )
				  << "  batch_source_space_endpoint_l2_mass_residual_certified_zero=" << ( full_source_space_summary.residual_endpoint_l2_mass_certified_zero ? 1 : 0 )
				  << std::defaultfloat << "\n";
		std::cout << "  note=source_hull_summary rows are the strict per-source hulls kept before cross-source endpoint merging\n";
		std::cout << "  note=endpoint_hull_summary rows are the strict merged endpoint hulls across all collected sources\n";
		std::cout << "  note=source_union_total_endpoint_l2_mass_theorem uses Parseval row-norm over the selected deep-certified source union\n";
		std::cout << "  note=source_union_total_signed_correlation_theorem uses Walsh row-sum over the selected deep-certified source union\n";
		if ( selected_source_count < input_job_count )
			std::cout << "  note=batch_source_space_* fields treat the original batch candidate universe as Ω_batch, so their residual includes omitted sources outside the selected deep-certified sub-space S\n";
		print_linear_source_hull_summary( combined_aggregator );
		print_linear_endpoint_hull_summary( combined_aggregator );
	}

	static void print_linear_subspace_hull_summary(
		const LinearBatchHullPipelineResult& pipeline_result,
		std::size_t prescribed_source_count,
		std::size_t active_partition_source_count )
	{
		if ( !pipeline_result.combined_source_hull.enabled )
			return;

		const LinearCallbackHullAggregator& combined_aggregator = pipeline_result.combined_source_hull.callback_aggregator;
		std::cout << "[Subspace][LinearHull] prescribed_sources=" << prescribed_source_count
				  << " processed_sources=" << pipeline_result.combined_source_hull.source_count
				  << " active_partition_sources=" << active_partition_source_count
				  << " endpoint_hulls=" << combined_aggregator.endpoint_hulls.size()
				  << " collected_trails=" << combined_aggregator.collected_trail_count
				  << " all_jobs_collected=" << ( pipeline_result.combined_source_hull.all_jobs_collected ? 1 : 0 )
				  << " all_jobs_exact=" << ( pipeline_result.combined_source_hull.all_jobs_exact_within_collect_weight_cap ? 1 : 0 )
				  << " all_jobs_hard_limit_free=" << ( pipeline_result.combined_source_hull.all_jobs_hard_limit_free ? 1 : 0 )
				  << "\n";
		std::cout << "  note=combined endpoint hull is exact only over the prescribed source subset S loaded for this subspace run\n";
		std::cout << "  note=source_hull_summary rows are strict per-source hulls over S before endpoint merging\n";
		std::cout << "  note=endpoint_hull_summary rows are strict merged endpoint hulls over S\n";
		print_linear_source_hull_summary( combined_aggregator );
		print_linear_endpoint_hull_summary( combined_aggregator );
	}

	static std::uint64_t linear_subspace_job_partition_hash( const LinearBatchJob& job ) noexcept
	{
		TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
		fp.mix_i32( job.rounds );
		fp.mix_u32( job.output_branch_a_mask );
		fp.mix_u32( job.output_branch_b_mask );
		return fp.finish();
	}

	static void partition_linear_subspace_jobs(
		std::vector<LinearBatchJob>& jobs,
		std::uint64_t partition_count,
		std::uint64_t partition_index )
	{
		if ( partition_count <= 1 )
			return;
		std::vector<LinearBatchJob> filtered {};
		filtered.reserve( jobs.size() );
		for ( const auto& job : jobs )
		{
			if ( linear_subspace_job_partition_hash( job ) % partition_count == partition_index )
				filtered.push_back( job );
		}
		jobs.swap( filtered );
	}

	static bool load_subspace_exclusion_evidence_file(
		const std::string& path,
		std::vector<SubspaceExclusionEvidenceRecord>& records_out )
	{
		std::ifstream f( path );
		if ( !f )
		{
			std::cerr << "[Subspace] ERROR: cannot open exclusion evidence file: " << path << "\n";
			return false;
		}
		std::string line {};
		int line_no = 0;
		while ( std::getline( f, line ) )
		{
			++line_no;
			if ( const std::size_t p = line.find( '#' ); p != std::string::npos )
				line.resize( p );
			std::istringstream iss( line );
			std::string count_token {}, weight_token {};
			if ( !( iss >> count_token >> weight_token ) )
				continue;
			std::uint64_t source_count = 0;
			SearchWeight parsed_weight = INFINITE_WEIGHT;
			bool parsed_weight_ok = false;
			{
				if ( weight_token == "-1" )
				{
					parsed_weight = INFINITE_WEIGHT;
					parsed_weight_ok = true;
				}
				else
				{
					std::uint64_t raw_weight = 0;
					if ( parse_unsigned_integer_64( weight_token.c_str(), raw_weight ) )
					{
						parsed_weight = static_cast<SearchWeight>( raw_weight );
						parsed_weight_ok = true;
					}
				}
			}
			if ( !parse_unsigned_integer_64( count_token.c_str(), source_count ) || !parsed_weight_ok || source_count == 0 )
			{
				std::cerr << "[Subspace] ERROR: invalid exclusion evidence line " << line_no << ": " << line << "\n";
				return false;
			}
			std::string label {};
			std::getline( iss, label );
			if ( !label.empty() && label.front() == ' ' )
				label.erase( label.begin() );
			records_out.push_back( SubspaceExclusionEvidenceRecord { source_count, parsed_weight, label } );
		}
		return true;
	}

	static SubspaceCoverageSummary summarize_subspace_coverage(
		std::uint64_t prescribed_source_count,
		std::uint64_t active_partition_source_count,
		const std::vector<SubspaceExclusionEvidenceRecord>& evidence_records,
		bool full_space_source_count_was_provided,
		std::uint64_t full_space_source_count,
		bool exact_jobs_complete,
		bool explicit_cap_mode,
		SearchWeight explicit_collect_weight_cap ) noexcept
	{
		SubspaceCoverageSummary summary {};
		summary.prescribed_source_count = prescribed_source_count;
		summary.active_partition_source_count = active_partition_source_count;
		summary.evidence_record_count = static_cast<std::uint64_t>( evidence_records.size() );
		summary.full_space_source_count = full_space_source_count;
		summary.full_space_source_count_was_provided = full_space_source_count_was_provided;
		summary.all_evidence_excluded_within_cap = explicit_cap_mode;
		for ( const auto& record : evidence_records )
		{
			summary.evidence_source_count += record.source_count;
			if ( explicit_cap_mode && record.min_best_weight > explicit_collect_weight_cap )
				summary.evidence_source_count_excluded_within_cap += record.source_count;
			else
				summary.all_evidence_excluded_within_cap = false;
		}
		if ( summary.full_space_source_count_was_provided &&
			 exact_jobs_complete &&
			 explicit_cap_mode &&
			 summary.all_evidence_excluded_within_cap &&
			 summary.active_partition_source_count + summary.evidence_source_count == summary.full_space_source_count )
		{
			summary.full_space_exact_within_collect_weight_cap = true;
		}
		return summary;
	}

	static void print_subspace_coverage_summary(
		const SubspaceCoverageSummary& summary,
		bool explicit_cap_mode,
		SearchWeight explicit_collect_weight_cap )
	{
		std::cout << "[Subspace][Coverage] prescribed_sources=" << summary.prescribed_source_count
				  << " active_partition_sources=" << summary.active_partition_source_count
				  << " evidence_records=" << summary.evidence_record_count
				  << " evidence_sources=" << summary.evidence_source_count
				  << " evidence_sources_excluded_within_cap=" << summary.evidence_source_count_excluded_within_cap
				  << "\n";
		if ( summary.full_space_source_count_was_provided )
		{
			std::cout << "  full_space_source_count=" << summary.full_space_source_count
					  << " full_space_exact_within_collect_weight_cap=" << ( summary.full_space_exact_within_collect_weight_cap ? 1 : 0 )
					  << "\n";
		}
		if ( !explicit_cap_mode )
			std::cout << "  note=full-space exactness cannot be concluded from exclusion evidence under best_weight_plus_window because there is no single global collect cap\n";
		else
			std::cout << "  note=full-space exactness is only asserted when the active strict subset plus exclusion evidence covers the declared full-space source count and every evidence record proves min_best_weight > " << explicit_collect_weight_cap << "\n";
	}

	static bool write_generated_subspace_evidence_record(
		const std::string& path,
		std::uint64_t source_count,
		SearchWeight min_best_weight,
		const std::string& label )
	{
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
		{
			std::cerr << "[Subspace] ERROR: cannot open generated evidence output path: " << path << "\n";
			return false;
		}
		out << source_count << " " << min_best_weight;
		if ( !label.empty() )
			out << " " << label;
		out << "\n";
		return true;
	}

	static const char* generated_evidence_certification_to_string( GeneratedEvidenceCertification certification ) noexcept
	{
		switch ( certification )
		{
		case GeneratedEvidenceCertification::LowerBoundOnly:
			return "lower_bound_only";
		case GeneratedEvidenceCertification::ExactBestCertified:
			return "exact_best_certified";
		default:
			return "none";
		}
	}

	static SearchWeight linear_trail_total_weight( const std::vector<LinearTrailStepRecord>& trail ) noexcept
	{
		SearchWeight total_weight = 0;
		for ( const auto& step : trail )
			total_weight = saturating_add_search_weight( total_weight, step.round_weight );
		return total_weight;
	}

	static GeneratedSubspaceEvidenceSummary compute_generated_subspace_evidence_from_main_run(
		const LinearBatchHullRunSummary& batch_summary,
		bool explicit_cap_mode )
	{
		(void)explicit_cap_mode;
		GeneratedSubspaceEvidenceSummary evidence_summary {};
		SearchWeight min_best_weight = INFINITE_WEIGHT;
		for ( const auto& job_summary : batch_summary.jobs )
		{
			if ( job_summary.best_search_executed &&
				 job_summary.best_weight_certified &&
				 job_summary.best_weight < INFINITE_WEIGHT )
			{
				++evidence_summary.exact_best_job_count;
				min_best_weight = std::min( min_best_weight, job_summary.best_weight );
				continue;
			}
			if ( job_summary.exact_within_collect_weight_cap &&
				 job_summary.collected &&
				 job_summary.collected_trails == 0 &&
				 job_summary.collect_weight_cap < ( INFINITE_WEIGHT - 1 ) )
			{
				++evidence_summary.lower_bound_only_job_count;
				min_best_weight = std::min( min_best_weight, successor_search_weight( job_summary.collect_weight_cap ) );
				continue;
			}
			++evidence_summary.unresolved_job_count;
		}
		if ( evidence_summary.unresolved_job_count == 0 )
		{
			evidence_summary.available = true;
			evidence_summary.min_best_weight = min_best_weight;
			evidence_summary.certification =
				( evidence_summary.lower_bound_only_job_count == 0 ) ?
					GeneratedEvidenceCertification::ExactBestCertified :
					GeneratedEvidenceCertification::LowerBoundOnly;
		}
		return evidence_summary;
	}

	struct LinearEvidencePrepassSeed
	{
		bool present = false;
		SearchWeight upper_bound_weight = INFINITE_WEIGHT;
		std::vector<LinearTrailStepRecord> upper_bound_trail {};
	};

	static std::map<std::size_t, LinearEvidencePrepassSeed> build_linear_prepass_seed_map(
		const LinearBatchHullPipelineResult& pipeline_result )
	{
		std::map<std::size_t, LinearEvidencePrepassSeed> seeds {};
		if ( !pipeline_result.combined_source_hull.enabled )
			return seeds;
		for ( const auto& [ source_key, source_summary ] : pipeline_result.combined_source_hull.callback_aggregator.source_hulls )
		{
			if ( source_summary.strongest_trail.empty() )
				continue;
			LinearEvidencePrepassSeed seed {};
			seed.present = true;
			seed.upper_bound_trail = source_summary.strongest_trail;
			seed.upper_bound_weight = linear_trail_total_weight( seed.upper_bound_trail );
			seeds[ source_key.source_index ] = std::move( seed );
		}
		return seeds;
	}

	static bool compute_linear_min_best_weight_prepass(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchHullRunSummary& batch_summary,
		const LinearBestSearchConfiguration& base_search_configuration,
		const CommandLineOptions& options,
		GeneratedSubspaceEvidenceSummary& evidence_summary,
		const std::map<std::size_t, LinearEvidencePrepassSeed>& seed_map,
		bool explicit_cap_mode,
		SearchWeight explicit_collect_weight_cap )
	{
		if ( evidence_summary.unresolved_job_count == 0 )
			return evidence_summary.available;

		std::vector<std::size_t> unresolved_job_indices {};
		unresolved_job_indices.reserve( std::size_t( evidence_summary.unresolved_job_count ) );
		for ( std::size_t job_index = 0; job_index < jobs.size(); ++job_index )
		{
			const auto& job_summary = batch_summary.jobs[ job_index ];
			const bool already_exact =
				job_summary.best_search_executed &&
				job_summary.best_weight_certified &&
				job_summary.best_weight < INFINITE_WEIGHT;
			const bool already_has_safe_lower_bound =
				job_summary.exact_within_collect_weight_cap &&
				job_summary.collected &&
				job_summary.collected_trails == 0 &&
				job_summary.collect_weight_cap < ( INFINITE_WEIGHT - 1 );
			if ( !already_exact && !already_has_safe_lower_bound )
				unresolved_job_indices.push_back( job_index );
		}

		const unsigned hw = std::thread::hardware_concurrency();
		const int worker_thread_count = ( options.batch_thread_count > 0 ) ? options.batch_thread_count : int( ( hw == 0 ) ? 1 : hw );
		const int worker_threads_clamped = std::max( 1, std::min<int>( worker_thread_count, static_cast<int>( unresolved_job_indices.size() ) ) );
		std::mutex aggregate_mutex {};
		SearchWeight min_best_weight = evidence_summary.min_best_weight;
		std::uint64_t total_prepass_nodes = 0;
		std::uint64_t prepass_exact_jobs = 0;
		std::uint64_t prepass_lower_bound_only_jobs = 0;

		struct LinearEvidencePrepassOutcome
		{
			bool success = false;
			SearchWeight resolved_lower_bound = INFINITE_WEIGHT;
			std::uint64_t nodes_visited = 0;
			GeneratedEvidenceCertification certification = GeneratedEvidenceCertification::None;
		};

		struct ActiveLinearEvidenceJob
		{
			std::size_t job_index = 0;
			TwilightDream::runtime_component::RuntimeAsyncJobHandle<LinearEvidencePrepassOutcome> handle {};
		};

		auto submit_job =
			[&]( std::size_t job_index ) {
				return TwilightDream::runtime_component::submit_named_async_job(
					"linear-evidence-prepass-job#" + std::to_string( job_index + 1 ),
					[&, job_index]( TwilightDream::runtime_component::RuntimeTaskContext& ) -> LinearEvidencePrepassOutcome {
						const auto& job = jobs[ job_index ];
						LinearEvidencePrepassOutcome outcome {};
						LinearBestSearchConfiguration cfg = base_search_configuration;
						cfg.round_count = job.rounds;
						cfg.target_best_weight = INFINITE_WEIGHT;
						SearchWeight seeded_upper_bound_weight = INFINITE_WEIGHT;
						const std::vector<LinearTrailStepRecord>* seeded_upper_bound_trail = nullptr;
						const bool certify_against_collect_cap =
							explicit_cap_mode &&
							explicit_collect_weight_cap < INFINITE_WEIGHT;
						if ( certify_against_collect_cap )
							seeded_upper_bound_weight = successor_search_weight( explicit_collect_weight_cap );
						if ( const auto it = seed_map.find( job_index ); it != seed_map.end() && it->second.present )
						{
							if ( seeded_upper_bound_weight >= INFINITE_WEIGHT || it->second.upper_bound_weight < seeded_upper_bound_weight )
							{
								seeded_upper_bound_weight = it->second.upper_bound_weight;
								seeded_upper_bound_trail = &it->second.upper_bound_trail;
							}
						}

						const MatsuiSearchRunLinearResult result =
							run_linear_best_search(
								job.output_branch_a_mask,
								job.output_branch_b_mask,
								cfg,
								LinearBestSearchRuntimeControls {
									0,
									0,
									0,
									0 },
								false,
								false,
								seeded_upper_bound_weight,
								seeded_upper_bound_trail,
								nullptr,
								nullptr,
								nullptr,
								nullptr );

						outcome.nodes_visited = result.nodes_visited;
						if ( result.hit_maximum_search_nodes ||
							 result.hit_time_limit ||
							 result.strict_rejection_reason != StrictCertificationFailureReason::None )
							return outcome;

						if ( result.best_weight_certified && result.best_weight < INFINITE_WEIGHT )
						{
							outcome.success = true;
							outcome.resolved_lower_bound = result.best_weight;
							outcome.certification = GeneratedEvidenceCertification::ExactBestCertified;
							return outcome;
						}
						if ( result.exhaustive_completed && !result.found )
						{
							outcome.success = true;
							outcome.resolved_lower_bound = seeded_upper_bound_weight;
							outcome.certification =
								certify_against_collect_cap ?
									GeneratedEvidenceCertification::LowerBoundOnly :
									GeneratedEvidenceCertification::ExactBestCertified;
						}
						return outcome;
					} );
			};

		std::vector<ActiveLinearEvidenceJob> active_jobs {};
		active_jobs.reserve( std::size_t( worker_threads_clamped ) );
		std::size_t next_submit = 0;
		while ( next_submit < unresolved_job_indices.size() || !active_jobs.empty() )
		{
			while ( active_jobs.size() < std::size_t( worker_threads_clamped ) && next_submit < unresolved_job_indices.size() )
			{
				const std::size_t job_index = unresolved_job_indices[ next_submit++ ];
				active_jobs.push_back( ActiveLinearEvidenceJob { job_index, submit_job( job_index ) } );
			}

			bool progressed = false;
			for ( std::size_t i = 0; i < active_jobs.size(); )
			{
				if ( !active_jobs[ i ].handle.done() )
				{
					++i;
					continue;
				}

				LinearEvidencePrepassOutcome outcome = active_jobs[ i ].handle.result();
				if ( !outcome.success )
				{
					for ( auto& active_job : active_jobs )
						active_job.handle.request_stop();
					return false;
				}

				{
					std::scoped_lock lk( aggregate_mutex );
					min_best_weight = std::min( min_best_weight, outcome.resolved_lower_bound );
					total_prepass_nodes += outcome.nodes_visited;
					if ( outcome.certification == GeneratedEvidenceCertification::ExactBestCertified )
						++prepass_exact_jobs;
					else if ( outcome.certification == GeneratedEvidenceCertification::LowerBoundOnly )
						++prepass_lower_bound_only_jobs;
				}

				active_jobs.erase( active_jobs.begin() + static_cast<std::ptrdiff_t>( i ) );
				progressed = true;
			}

			if ( !progressed && !active_jobs.empty() )
				std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}

		evidence_summary.prepass_job_count = unresolved_job_indices.size();
		evidence_summary.prepass_total_nodes += total_prepass_nodes;
		evidence_summary.unresolved_job_count = 0;
		evidence_summary.available = true;
		evidence_summary.min_best_weight = min_best_weight;
		evidence_summary.exact_best_job_count += prepass_exact_jobs;
		evidence_summary.lower_bound_only_job_count += prepass_lower_bound_only_jobs;
		evidence_summary.certification =
			( evidence_summary.lower_bound_only_job_count == 0 ) ?
				GeneratedEvidenceCertification::ExactBestCertified :
				GeneratedEvidenceCertification::LowerBoundOnly;
		return evidence_summary.available;
	}

	static GeneratedSubspaceEvidenceSummary compute_linear_generated_evidence_summary(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchHullRunSummary& batch_summary,
		const LinearBatchHullPipelineResult& pipeline_result,
		const LinearBestSearchConfiguration& base_search_configuration,
		const CommandLineOptions& options,
		bool explicit_cap_mode,
		SearchWeight explicit_collect_weight_cap )
	{
		GeneratedSubspaceEvidenceSummary evidence_summary =
			compute_generated_subspace_evidence_from_main_run( batch_summary, explicit_cap_mode );
		if ( evidence_summary.available || !options.auto_exclusion_evidence )
			return evidence_summary;
		const auto seed_map = build_linear_prepass_seed_map( pipeline_result );
		if ( compute_linear_min_best_weight_prepass( jobs, batch_summary, base_search_configuration, options, evidence_summary, seed_map, explicit_cap_mode, explicit_collect_weight_cap ) )
			return evidence_summary;
		return GeneratedSubspaceEvidenceSummary {};
	}

#include "test_subspace_hull_support_linear.cpp"
	static int run_linear_subspace_mode( const CommandLineOptions& options )
	{
		if ( options.subspace_evidence_only_mode )
			return run_linear_subspace_evidence_only_mode( options );
		const bool subspace_resume_requested = !options.subspace_resume_checkpoint_path.empty();
		const std::string subspace_checkpoint_path =
			!options.subspace_checkpoint_out_path.empty() ?
				options.subspace_checkpoint_out_path :
				options.subspace_resume_checkpoint_path;
		std::string subspace_runtime_log_path = options.subspace_runtime_log_path;
		LinearBatchHullPipelineCheckpointState resumed_subspace_state {};
		bool resumed_subspace_state_loaded = false;
		LinearBatchHullPipelineCheckpointState final_subspace_state_for_log_storage {};
		const LinearBatchHullPipelineCheckpointState* final_subspace_state_for_log = nullptr;
		RuntimeEventLog subspace_runtime_log {};
		bool subspace_runtime_log_enabled = false;

		if ( options.batch_job_count != 0 || options.batch_seed_was_provided || !options.batch_resume_checkpoint_path.empty() )
		{
			std::cerr << "[Subspace] ERROR: RNG batch-job generation and --batch-resume are not valid in subspace mode.\n";
			return 1;
		}
		if ( !options.best_search_resume_checkpoint_path.empty() )
		{
			std::cerr << "[Subspace] ERROR: --best-search-resume is not supported in subspace mode.\n";
			return 1;
		}
		if ( options.best_search_checkpoint_out_was_provided || options.best_search_checkpoint_every_seconds_was_provided )
		{
			std::cerr << "[Subspace] ERROR: embedded best-search checkpoints are not supported in subspace mode.\n";
			return 1;
		}
		if ( !options.best_search_history_log_path.empty() || !options.best_search_runtime_log_path.empty() )
		{
			std::cerr << "[Subspace] ERROR: per-job best-search logs are not supported in subspace mode.\n";
			return 1;
		}
		if ( options.auto_deep_maximum_search_nodes != 0 ||
			 options.auto_max_time_seconds != 0 ||
			 options.auto_target_best_weight < INFINITE_WEIGHT )
		{
			std::cerr << "[Subspace] ERROR: heuristic breadth/deep selection options are not valid in subspace mode.\n";
			return 1;
		}
		if ( options.subspace_job_file_was_provided && options.batch_job_file_was_provided )
		{
			std::cerr << "[Subspace] ERROR: provide either --subspace-file or --batch-file --subspace-hull, not both.\n";
			return 1;
		}
		if ( options.subspace_partition_count == 0 )
		{
			std::cerr << "[Subspace] ERROR: --subspace-partition-count must be >= 1.\n";
			return 1;
		}
		if ( options.subspace_partition_index >= options.subspace_partition_count )
		{
			std::cerr << "[Subspace] ERROR: --subspace-partition-index must be in [0, subspace-partition-count).\n";
			return 1;
		}

		std::string source_file_path {};
		if ( options.subspace_job_file_was_provided )
			source_file_path = options.subspace_job_file;
		else if ( options.batch_job_file_was_provided )
			source_file_path = options.batch_job_file;
		if ( !subspace_resume_requested && source_file_path.empty() )
		{
			std::cerr << "[Subspace] ERROR: subspace mode requires --subspace-file PATH or --batch-file PATH --subspace-hull.\n";
			return 1;
		}

		if ( subspace_runtime_log_path.empty() && !subspace_checkpoint_path.empty() )
			subspace_runtime_log_path = default_linear_subspace_runtime_log_path( subspace_checkpoint_path );
		if ( !subspace_runtime_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					subspace_runtime_log,
					subspace_runtime_log_path,
					"[Subspace] ERROR: cannot open subspace runtime log: " ) )
				return 1;
			subspace_runtime_log_enabled = true;
		}

		if ( subspace_resume_requested )
		{
			if ( !read_linear_subspace_hull_pipeline_checkpoint( options.subspace_resume_checkpoint_path, resumed_subspace_state ) )
			{
				std::cerr << "[Subspace] ERROR: cannot read subspace checkpoint: " << options.subspace_resume_checkpoint_path << "\n";
				return 1;
			}
			if ( options.enable_bnb_residual )
				resumed_subspace_state.runtime_options_snapshot.residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
			resumed_subspace_state_loaded = true;
		}

		std::vector<LinearBatchJob> jobs {};
		std::uint64_t prescribed_source_count = 0;
		if ( resumed_subspace_state_loaded )
		{
			jobs = resumed_subspace_state.jobs;
			prescribed_source_count = static_cast<std::uint64_t>( jobs.size() );
			if ( !source_file_path.empty() )
			{
				std::vector<LinearBatchJob> all_jobs {};
				if ( load_linear_batch_jobs_from_file( source_file_path, 0, options.round_count, all_jobs ) != 0 )
					return 1;
				prescribed_source_count = static_cast<std::uint64_t>( all_jobs.size() );
			}
		}
		else
		{
			if ( load_linear_batch_jobs_from_file( source_file_path, 0, options.round_count, jobs ) != 0 )
				return 1;
			prescribed_source_count = static_cast<std::uint64_t>( jobs.size() );
			partition_linear_subspace_jobs( jobs, options.subspace_partition_count, options.subspace_partition_index );
			if ( jobs.empty() )
			{
				std::cout << "[Subspace] mode=linear_strict_subspace_hull\n";
				std::cout << "  source_file=" << source_file_path << "\n";
				std::cout << "  note=active partition is empty; no sources selected for this shard\n";
				return 0;
			}
		}

		std::vector<SubspaceExclusionEvidenceRecord> exclusion_evidence {};
		if ( options.subspace_exclusion_evidence_file_was_provided )
		{
			if ( !load_subspace_exclusion_evidence_file( options.subspace_exclusion_evidence_file, exclusion_evidence ) )
				return 1;
		}

		const HullCollectCapMode display_collect_cap_mode =
			resumed_subspace_state_loaded ?
				resumed_subspace_state.runtime_options_snapshot.collect_cap_mode :
				( options.collect_weight_cap_was_provided ? HullCollectCapMode::ExplicitCap : HullCollectCapMode::BestWeightPlusWindow );
		const SearchWeight display_collect_weight_cap =
			( display_collect_cap_mode == HullCollectCapMode::ExplicitCap ) ?
				( resumed_subspace_state_loaded ? resumed_subspace_state.runtime_options_snapshot.explicit_collect_weight_cap : options.collect_weight_cap ) :
				INFINITE_WEIGHT;
		const SearchWeight display_collect_weight_window =
			( display_collect_cap_mode == HullCollectCapMode::BestWeightPlusWindow ) ?
				( resumed_subspace_state_loaded ? resumed_subspace_state.runtime_options_snapshot.collect_weight_window : options.collect_weight_window ) :
				INFINITE_WEIGHT;

		std::cout << "[Subspace] mode=linear_strict_subspace_hull\n";
		std::cout << "  rounds=" << options.round_count << "  sources=" << jobs.size();
		if ( subspace_resume_requested )
			std::cout << "  resume_checkpoint=" << options.subspace_resume_checkpoint_path << "\n";
		else
			std::cout << "  source_file=" << source_file_path << "\n";
		if ( !subspace_resume_requested )
		{
			std::cout << "  prescribed_source_count=" << prescribed_source_count
					  << "  active_partition_source_count=" << jobs.size()
					  << "  subspace_partition_count=" << options.subspace_partition_count
					  << "  subspace_partition_index=" << options.subspace_partition_index
					  << "\n";
		}
		else if ( source_file_path.empty() )
		{
			std::cout << "  note=resume without re-supplying the original source file only preserves active shard metadata from the checkpoint\n";
		}
		std::cout << "  per_job: collect_mode=" << ( display_collect_cap_mode == HullCollectCapMode::ExplicitCap ? "explicit_cap" : "best_weight_plus_window" )
				  << " collect_weight_cap=" << display_search_weight( display_collect_weight_cap )
				  << " collect_weight_window=" << display_search_weight( display_collect_weight_window )
				  << "\n";
		if ( !subspace_checkpoint_path.empty() )
			std::cout << "  subspace_checkpoint=" << subspace_checkpoint_path << "\n";
		if ( !subspace_runtime_log_path.empty() )
			std::cout << "  subspace_runtime_log=" << subspace_runtime_log_path << "\n";
		std::cout << "  note=this mode is exact only over the prescribed source subset S loaded for this run\n\n";
		std::cout << "  note=the operator-layer Gap B optimal-mask oracle is not yet a formal reduction of the current search-frame subconst enumeration used by this hull path\n";

		LinearBestSearchConfiguration base_search_configuration = make_linear_base_search_configuration( options );
		LinearBatchHullPipelineResult pipeline_result {};
		if ( resumed_subspace_state_loaded )
		{
			if ( subspace_runtime_log_enabled )
			{
				write_linear_batch_runtime_event(
					subspace_runtime_log,
					"subspace_resume_start",
					[&]( std::ostream& out ) {
						out << "checkpoint_kind=linear_hull_subspace\n";
						out << "checkpoint_path=" << options.subspace_resume_checkpoint_path << "\n";
						write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( resumed_subspace_state ) );
					} );
			}
			pipeline_result =
				run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
					resumed_subspace_state,
					std::max( 1, options.batch_thread_count == 0 ? int( std::thread::hardware_concurrency() == 0 ? 1 : std::thread::hardware_concurrency() ) : options.batch_thread_count ),
					TwilightDream::auto_search_linear::LinearHullTrailCallback {},
					[&]( const LinearBatchHullJobSummary& summary ) { print_linear_subspace_hull_job_summary( summary ); },
					subspace_checkpoint_path,
					subspace_runtime_log_enabled ?
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )>(
							[&]( const LinearBatchHullPipelineCheckpointState& checkpoint_state, const char* reason ) {
								write_linear_batch_runtime_event(
									subspace_runtime_log,
									"subspace_checkpoint_write",
									[&]( std::ostream& out ) {
										out << "checkpoint_kind=linear_hull_subspace\n";
										out << "checkpoint_path=" << subspace_checkpoint_path << "\n";
										out << "checkpoint_reason=" << ( reason ? reason : "subspace_checkpoint" ) << "\n";
										write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( checkpoint_state ) );
									} );
							} ) :
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )> {},
					[&]( const std::string& path, const LinearBatchHullPipelineCheckpointState& checkpoint_state ) {
						return write_linear_subspace_hull_pipeline_checkpoint( path, checkpoint_state );
					} );
			final_subspace_state_for_log = &resumed_subspace_state;
		}
		else
		{
			LinearBatchHullPipelineOptions pipeline_options {};
			pipeline_options.worker_thread_count = std::max( 1, options.batch_thread_count == 0 ? int( std::thread::hardware_concurrency() == 0 ? 1 : std::thread::hardware_concurrency() ) : options.batch_thread_count );
			pipeline_options.base_search_configuration = base_search_configuration;
			pipeline_options.runtime_options_template = make_linear_runtime_options( options, nullptr, nullptr, true );
			pipeline_options.enable_combined_source_aggregator = true;
			pipeline_options.source_namespace = TwilightDream::hull_callback_aggregator::OuterSourceNamespace::SubspaceJob;
			pipeline_options.stored_trail_policy = options.stored_trail_policy;
			pipeline_options.maximum_stored_trails = static_cast<std::size_t>( options.maximum_stored_trails );

			LinearBatchHullPipelineCheckpointState subspace_state =
				make_initial_linear_batch_hull_pipeline_checkpoint_state( jobs, pipeline_options );
			if ( !subspace_checkpoint_path.empty() )
			{
				if ( !write_linear_subspace_hull_pipeline_checkpoint( subspace_checkpoint_path, subspace_state ) )
				{
					std::cerr << "[Subspace] ERROR: cannot write subspace checkpoint: " << subspace_checkpoint_path << "\n";
					return 1;
				}
				if ( subspace_runtime_log_enabled )
				{
					write_linear_batch_runtime_event(
						subspace_runtime_log,
						"subspace_checkpoint_write",
						[&]( std::ostream& out ) {
							out << "checkpoint_kind=linear_hull_subspace\n";
							out << "checkpoint_path=" << subspace_checkpoint_path << "\n";
							out << "checkpoint_reason=subspace_stage_init\n";
							write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( subspace_state ) );
						} );
				}
			}

			pipeline_result =
				run_linear_batch_strict_hull_pipeline_from_checkpoint_state(
					subspace_state,
					pipeline_options.worker_thread_count,
					TwilightDream::auto_search_linear::LinearHullTrailCallback {},
					[&]( const LinearBatchHullJobSummary& summary ) { print_linear_subspace_hull_job_summary( summary ); },
					subspace_checkpoint_path,
					subspace_runtime_log_enabled ?
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )>(
							[&]( const LinearBatchHullPipelineCheckpointState& checkpoint_state, const char* reason ) {
								write_linear_batch_runtime_event(
									subspace_runtime_log,
									"subspace_checkpoint_write",
									[&]( std::ostream& out ) {
										out << "checkpoint_kind=linear_hull_subspace\n";
										out << "checkpoint_path=" << subspace_checkpoint_path << "\n";
										out << "checkpoint_reason=" << ( reason ? reason : "subspace_checkpoint" ) << "\n";
										write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( checkpoint_state ) );
									} );
							} ) :
						std::function<void( const LinearBatchHullPipelineCheckpointState&, const char* )> {},
					[&]( const std::string& path, const LinearBatchHullPipelineCheckpointState& checkpoint_state ) {
						return write_linear_subspace_hull_pipeline_checkpoint( path, checkpoint_state );
					} );
			final_subspace_state_for_log_storage = subspace_state;
			final_subspace_state_for_log = &final_subspace_state_for_log_storage;
		}

		const LinearBatchHullRunSummary& batch_summary = pipeline_result.batch_summary;
		const SubspaceCoverageSummary coverage_summary =
			summarize_subspace_coverage(
				prescribed_source_count,
				static_cast<std::uint64_t>( jobs.size() ),
				exclusion_evidence,
				options.full_space_source_count_was_provided,
				options.full_space_source_count,
				pipeline_result.combined_source_hull.all_jobs_exact_within_collect_weight_cap,
				display_collect_cap_mode == HullCollectCapMode::ExplicitCap,
				display_collect_weight_cap );
		std::cout << "\n[Subspace][LinearHull] jobs=" << batch_summary.jobs.size()
				  << " exact_jobs=" << batch_summary.exact_jobs
				  << " partial_jobs=" << batch_summary.partial_jobs
				  << " rejected_jobs=" << batch_summary.rejected_jobs
				  << " total_best_search_nodes=" << batch_summary.total_best_search_nodes
				  << " total_hull_nodes=" << batch_summary.total_hull_nodes
				  << "\n";
		print_linear_subspace_hull_summary( pipeline_result, static_cast<std::size_t>( prescribed_source_count ), jobs.size() );
		print_subspace_coverage_summary(
			coverage_summary,
			display_collect_cap_mode == HullCollectCapMode::ExplicitCap,
			display_collect_weight_cap );
		const GeneratedSubspaceEvidenceSummary generated_evidence =
			compute_linear_generated_evidence_summary(
				jobs,
				batch_summary,
				pipeline_result,
				base_search_configuration,
				options,
				display_collect_cap_mode == HullCollectCapMode::ExplicitCap,
				display_collect_weight_cap );
		if ( !options.subspace_evidence_out_path.empty() )
		{
			if ( !generated_evidence.available )
			{
				std::cerr << "[Subspace] ERROR: cannot emit generated evidence because at least one active shard job still lacks a certified lower bound.\n";
				return 1;
			}
			std::string label = options.subspace_evidence_label;
			if ( label.empty() )
			{
				std::ostringstream oss;
				oss << "partition_" << options.subspace_partition_index << "_of_" << options.subspace_partition_count;
				label = oss.str();
			}
			if ( !write_generated_subspace_evidence_record( options.subspace_evidence_out_path, static_cast<std::uint64_t>( jobs.size() ), generated_evidence.min_best_weight, label ) )
				return 1;
			std::cout << "[Subspace][Evidence] generated path=" << options.subspace_evidence_out_path
					  << " source_count=" << jobs.size()
					  << " min_best_weight=" << generated_evidence.min_best_weight
					  << " certification=" << generated_evidence_certification_to_string( generated_evidence.certification )
					  << " prepass_jobs=" << generated_evidence.prepass_job_count
					  << " prepass_total_nodes=" << generated_evidence.prepass_total_nodes
					  << " label=" << label << "\n";
		}
		if ( !options.subspace_coverage_out_path.empty() )
		{
			if ( !write_linear_subspace_coverage_report(
					options.subspace_coverage_out_path,
					options,
					batch_summary,
					coverage_summary,
					source_file_path,
					display_collect_cap_mode == HullCollectCapMode::ExplicitCap,
					display_collect_weight_cap,
					generated_evidence ) )
				return 1;
			std::cout << "[Subspace][Coverage] report_path=" << options.subspace_coverage_out_path << "\n";
		}
		if ( subspace_runtime_log_enabled )
		{
			write_linear_batch_runtime_event(
				subspace_runtime_log,
				"subspace_stop",
				[&]( std::ostream& out ) {
					out << "jobs=" << batch_summary.jobs.size() << "\n";
					out << "exact_jobs=" << batch_summary.exact_jobs << "\n";
					out << "partial_jobs=" << batch_summary.partial_jobs << "\n";
					out << "rejected_jobs=" << batch_summary.rejected_jobs << "\n";
					out << "prescribed_source_count=" << prescribed_source_count << "\n";
					out << "active_partition_source_count=" << jobs.size() << "\n";
					out << "subspace_partition_count=" << options.subspace_partition_count << "\n";
					out << "subspace_partition_index=" << options.subspace_partition_index << "\n";
					out << "evidence_record_count=" << coverage_summary.evidence_record_count << "\n";
					out << "evidence_source_count=" << coverage_summary.evidence_source_count << "\n";
					out << "evidence_source_count_excluded_within_cap=" << coverage_summary.evidence_source_count_excluded_within_cap << "\n";
					out << "full_space_source_count=" << coverage_summary.full_space_source_count << "\n";
					out << "full_space_source_count_was_provided=" << ( coverage_summary.full_space_source_count_was_provided ? 1 : 0 ) << "\n";
					out << "full_space_exact_within_collect_weight_cap=" << ( coverage_summary.full_space_exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
					out << "total_best_search_nodes=" << batch_summary.total_best_search_nodes << "\n";
					out << "total_hull_nodes=" << batch_summary.total_hull_nodes << "\n";
					if ( pipeline_result.combined_source_hull.enabled )
					{
						out << "combined_source_count=" << pipeline_result.combined_source_hull.source_count << "\n";
						out << "combined_endpoint_hull_count=" << pipeline_result.combined_source_hull.callback_aggregator.endpoint_hulls.size() << "\n";
						out << "combined_collected_trails=" << pipeline_result.combined_source_hull.callback_aggregator.collected_trail_count << "\n";
						out << "combined_source_union_total_signed_correlation_theorem=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_total_signed_correlation_theorem ) << "\n";
						out << "combined_source_union_residual_signed_correlation_exact=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_residual_signed_correlation_exact ) << "\n";
						out << "combined_aggregate_endpoint_l2_mass=" << static_cast<double>( pipeline_result.combined_source_hull.aggregate_endpoint_l2_mass ) << "\n";
						out << "combined_source_union_total_endpoint_l2_mass_theorem=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_total_endpoint_l2_mass_theorem ) << "\n";
						out << "combined_source_union_residual_endpoint_l2_mass_exact=" << static_cast<double>( pipeline_result.combined_source_hull.source_union_residual_endpoint_l2_mass_exact ) << "\n";
					}
					if ( final_subspace_state_for_log != nullptr )
						write_linear_batch_resume_fingerprint_fields( out, compute_linear_hull_resume_fingerprint( *final_subspace_state_for_log ) );
				} );
		}
		return 0;
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
				if ( options.enable_bnb_residual )
					resumed_batch_state.runtime_options_snapshot.residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
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
		const SearchWeight display_collect_weight_cap =
			( display_collect_cap_mode == HullCollectCapMode::ExplicitCap ) ?
				( resumed_batch_state_loaded ? resumed_batch_state.runtime_options_snapshot.explicit_collect_weight_cap : options.collect_weight_cap ) :
				INFINITE_WEIGHT;
		const SearchWeight display_collect_weight_window =
			( display_collect_cap_mode == HullCollectCapMode::BestWeightPlusWindow ) ?
				( resumed_batch_state_loaded ? resumed_batch_state.runtime_options_snapshot.collect_weight_window : options.collect_weight_window ) :
				INFINITE_WEIGHT;
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
		const std::size_t selection_top_source_limit =
			TwilightDream::hull_callback_aggregator::linear_resolve_batch_top_candidate_limit(
				jobs.size(),
				worker_threads_clamped,
				jobs.size() );
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
					  << "  collect_weight_cap=" << display_search_weight( display_collect_weight_cap )
					  << "  collect_weight_window=" << display_search_weight( display_collect_weight_window )
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
								if ( options.auto_target_best_weight < INFINITE_WEIGHT )
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
					selection_top_source_limit,
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
				if ( options.auto_target_best_weight < INFINITE_WEIGHT )
					orchestrator_config.deep_configuration.target_best_weight = options.auto_target_best_weight;
				orchestrator_config.deep_runtime.maximum_search_nodes = options.auto_deep_maximum_search_nodes;
				orchestrator_config.deep_runtime.maximum_search_seconds = options.auto_max_time_seconds;
				orchestrator_config.deep_runtime.progress_every_seconds = options.progress_every_seconds;
				run_linear_batch_breadth_then_deep( jobs, worker_threads_clamped, selection_top_source_limit, orchestrator_config, orchestrator_result );
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
			pipeline_options.runtime_options_template = make_linear_runtime_options( options, nullptr, nullptr, true );
			pipeline_options.best_search_seeds = std::move( selected_job_seeds );
			pipeline_options.enable_combined_source_aggregator = true;
			pipeline_options.source_namespace = TwilightDream::hull_callback_aggregator::OuterSourceNamespace::BatchJob;
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
		const LinearBatchFullSourceSpaceSummary batch_full_source_space_summary =
			compute_linear_batch_full_source_space_summary( jobs, pipeline_result.combined_source_hull );

		std::cout << "\n[Batch][LinearHull] jobs=" << batch_summary.jobs.size()
				  << " exact_jobs=" << batch_summary.exact_jobs
				  << " partial_jobs=" << batch_summary.partial_jobs
				  << " rejected_jobs=" << batch_summary.rejected_jobs
				  << " total_best_search_nodes=" << batch_summary.total_best_search_nodes
				  << " total_hull_nodes=" << batch_summary.total_hull_nodes
				  << "\n";
		print_linear_combined_source_hull_summary( pipeline_result, jobs, selected_jobs.size() );
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
						out << "batch_source_space_source_count=" << batch_full_source_space_summary.full_source_count << "\n";
						out << "batch_source_space_total_signed_correlation_theorem=" << static_cast<double>( batch_full_source_space_summary.total_signed_correlation_theorem ) << "\n";
						out << "batch_source_space_residual_signed_correlation_exact=" << static_cast<double>( batch_full_source_space_summary.residual_signed_correlation_exact ) << "\n";
						out << "batch_source_space_total_endpoint_l2_mass_theorem=" << static_cast<double>( batch_full_source_space_summary.total_endpoint_l2_mass_theorem ) << "\n";
						out << "batch_source_space_residual_endpoint_l2_mass_exact=" << static_cast<double>( batch_full_source_space_summary.residual_endpoint_l2_mass_exact ) << "\n";
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
	if ( options.campaign_mode )
		return run_linear_partition_campaign( options );
	if ( options.selftest )
		return run_linear_hull_wrapper_self_test( options.selftest_scope );
	const bool subspace_requested =
		options.subspace_hull_mode ||
		options.subspace_job_file_was_provided ||
		!options.subspace_resume_checkpoint_path.empty();
	const bool batch_requested =
		( options.batch_job_count > 0 ) ||
		options.batch_job_file_was_provided ||
		!options.batch_resume_checkpoint_path.empty();
	if ( !validate_linear_collector_runtime_controls( options ) )
		return 1;
	if ( subspace_requested )
		return run_linear_subspace_mode( options );
	if ( batch_requested )
		return run_linear_batch_mode( options );
	if ( !options.masks_were_provided )
	{
		print_usage( ( argc > 0 && argv && argv[ 0 ] ) ? argv[ 0 ] : "test_neoalzette_linear_hull_wrapper.exe" );
		return 1;
	}
	timestamp_linear_best_search_output_paths( options );
	timestamp_linear_collector_output_paths( options );
	if ( !validate_linear_best_search_runtime_controls( options ) )
		return 1;
	ensure_default_linear_best_search_output_paths( options );
	ensure_default_linear_collector_output_paths( options );

	LinearBestSearchRuntimeArtifacts best_search_artifacts {};
	if ( !configure_linear_best_search_runtime_artifacts( options, best_search_artifacts ) )
		return 1;
	LinearCollectorRuntimeArtifacts collector_artifacts {};
	if ( !configure_linear_collector_runtime_artifacts( options, collector_artifacts ) )
		return 1;

	LinearBestSearchConfiguration config {};
	config.search_mode = SearchMode::Strict;
	config.round_count = options.round_count;
	config.addition_weight_cap = options.addition_weight_cap;
		config.constant_subtraction_weight_cap = options.constant_subtraction_weight_cap;
		config.enable_weight_sliced_clat = options.enable_weight_sliced_clat;
		config.weight_sliced_clat_max_candidates = options.weight_sliced_clat_max_candidates;
		config.varvar_modular_add_bnb_mode = options.varvar_modular_add_bnb_mode;
		config.varconst_sub_bnb_mode = options.varconst_sub_bnb_mode;
		config.enable_fixed_beta_outer_hot_path_gate = options.enable_fixed_beta_outer_hot_path_gate;
		config.enable_fixed_alpha_outer_hot_path_gate = options.enable_fixed_alpha_outer_hot_path_gate;
		config.enable_fixed_alpha_outer_hot_path_gate =
			options.varconst_sub_bnb_mode == LinearVarConstSubBnbMode::FixedInputMaskAlpha_ColumnOptimalOutputBeta;
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
		run_linear_fixed_source_hull_with_separate_collector_artifacts(
			options,
			config,
			&best_search_artifacts,
			&collector_artifacts,
			callback_aggregator );
	callback_aggregator.annotate_source_runtime_result( make_fixed_linear_source_context( options ), runtime_result_with_callback );
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
