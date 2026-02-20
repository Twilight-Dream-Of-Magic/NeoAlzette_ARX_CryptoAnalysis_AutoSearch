// Split from test_subspace_hull_self_test.cpp.

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

static bool differential_hull_selftest_check( bool condition, const std::string& message )
	{
		if ( !condition )
			std::cerr << "[SelfTest][DifferentialHull][FAIL] " << message << "\n";
		return condition;
	}

	static bool differential_hull_selftest_expect_contains( const std::string& text, const std::string& needle, const std::string& label )
	{
		return differential_hull_selftest_check( text.find( needle ) != std::string::npos, label + " missing `" + needle + "`" );
	}

	static bool differential_hull_selftest_expect_probability_near( long double lhs, long double rhs, long double tolerance, const std::string& label )
	{
		return differential_hull_selftest_check( std::fabsl( lhs - rhs ) <= tolerance, label );
	}

	template <typename Fn>
	static std::string capture_differential_hull_selftest_stdout( Fn&& fn )
	{
		std::ostringstream buffer;
		std::streambuf* const previous = std::cout.rdbuf( buffer.rdbuf() );
		fn();
		std::cout.rdbuf( previous );
		return buffer.str();
	}

	static std::string create_differential_hull_batch_selftest_job_file()
	{
		namespace fs = std::filesystem;
		const fs::path path = fs::temp_directory_path() / "neoalzette_differential_hull_batch_selftest_jobs.txt";
		std::ofstream out( path.string(), std::ios::out | std::ios::trunc );
		out << "1 1 0\n";
		return path.string();
	}

	static bool run_differential_hull_batch_explicit_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
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
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( explicit_options ) == 0, "batch explicit-cap smoke should succeed" );
				} );
		ok = differential_hull_selftest_expect_contains( explicit_output, "[Batch][DifferentialHull][Job 1][OK]", "batch explicit-cap job status" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "collect_weight_cap=0", "batch explicit-cap collect cap" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "exact_jobs=1", "batch explicit-cap exact summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "[Batch][DifferentialHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "batch explicit-cap selection summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "[Batch][DifferentialHull][Combined] sources=1 endpoint_hulls=0 collected_trails=0 all_jobs_collected=1 all_jobs_exact=1", "batch explicit-cap combined summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "all_jobs_hard_limit_free=1", "batch explicit-cap hard-limit summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( explicit_output, "source_collected=1  source_exact=1", "batch explicit-cap source strictness summary" ) && ok;
		return ok;
	}

	static bool run_differential_hull_batch_combined_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};

		CommandLineOptions combined_options {};
		combined_options.round_count = 1;
		combined_options.collect_weight_cap = 0;
		combined_options.collect_weight_cap_was_provided = true;
		combined_options.maximum_search_nodes = 65'536;
		combined_options.maximum_search_seconds = 0;

		const std::vector<DifferentialBatchJob> combined_jobs {
			DifferentialBatchJob { 1, 0u, 0u },
			DifferentialBatchJob { 1, 0u, 0u }
		};
		DifferentialBatchHullPipelineOptions combined_pipeline_options {};
		combined_pipeline_options.worker_thread_count = 1;
		combined_pipeline_options.base_search_configuration = make_differential_base_search_configuration( combined_options );
		combined_pipeline_options.runtime_options_template = make_differential_runtime_options( combined_options, nullptr, nullptr );
		combined_pipeline_options.enable_combined_source_aggregator = true;
		combined_pipeline_options.source_namespace = TwilightDream::hull_callback_aggregator::OuterSourceNamespace::BatchJob;
		combined_pipeline_options.stored_trail_policy = StoredTrailPolicy::ArrivalOrder;
		combined_pipeline_options.maximum_stored_trails = 4;
		const DifferentialBatchHullPipelineResult combined_pipeline_result =
			run_differential_batch_strict_hull_pipeline( combined_jobs, combined_pipeline_options );
		check( combined_pipeline_result.batch_summary.exact_jobs == combined_jobs.size(), "combined-source pipeline should keep all duplicate zero jobs exact" );
		check( combined_pipeline_result.batch_summary.partial_jobs == 0 && combined_pipeline_result.batch_summary.rejected_jobs == 0, "combined-source pipeline should avoid partial or rejected duplicate zero jobs" );
		check( combined_pipeline_result.batch_summary.jobs_hit_hard_limits == 0, "combined-source pipeline should stay free of hard limits for duplicate zero jobs" );
		check( combined_pipeline_result.combined_source_hull.enabled, "combined-source pipeline should enable the shared endpoint aggregator" );
		check( combined_pipeline_result.combined_source_hull.source_count == combined_jobs.size(), "combined-source pipeline should preserve both source jobs" );
		check( combined_pipeline_result.combined_source_hull.all_jobs_exact_within_collect_weight_cap, "combined-source pipeline should report exactness across all duplicate zero jobs" );
		check( combined_pipeline_result.combined_source_hull.all_jobs_hard_limit_free, "combined-source pipeline should report hard-limit freedom across all duplicate zero jobs" );
		check( differential_hull_selftest_expect_probability_near( combined_pipeline_result.combined_source_hull.aggregate_probability_mass, 2.0L, 1e-18L, "combined-source probability mass should add over the source union" ), "combined-source probability mass should add over the source union" );
		check( differential_hull_selftest_expect_probability_near( combined_pipeline_result.combined_source_hull.source_union_total_probability_theorem, 2.0L, 1e-18L, "combined-source probability theorem total should equal source count" ), "combined-source probability theorem total should equal source count" );
		check( differential_hull_selftest_expect_probability_near( combined_pipeline_result.combined_source_hull.source_union_residual_probability_exact, 0.0L, 1e-18L, "combined-source probability residual should close to zero for duplicate zero jobs" ), "combined-source probability residual should close to zero for duplicate zero jobs" );
		check( combined_pipeline_result.combined_source_hull.source_union_probability_residual_certified_zero, "combined-source probability residual should certify zero for duplicate zero jobs" );
		const DifferentialCallbackHullAggregator& combined_aggregator = combined_pipeline_result.combined_source_hull.callback_aggregator;
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

	static bool run_differential_hull_batch_multi_trajectory_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
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
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( multi_trajectory_options ) == 0, "batch multi-trajectory strict-hull chain should succeed" );
				} );
		ok = differential_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][Breadth] TOP-1 candidates:", "batch multi-trajectory breadth summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][DifferentialHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "batch multi-trajectory selection summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( multi_trajectory_output, "[Batch][DifferentialHull][Combined] sources=1", "batch multi-trajectory combined source summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( multi_trajectory_output, "source_union_total_probability_theorem=", "batch multi-trajectory probability theorem summary" ) && ok;
		return ok;
	}

	static bool run_differential_hull_batch_reject_self_test( const std::string& job_file_path )
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
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
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( reject_options ) == 0, "batch hard-limit partial smoke should still exit cleanly" );
				} );
		ok = differential_hull_selftest_expect_contains( reject_output, "[Batch][DifferentialHull][Job 1][PARTIAL]", "batch partial job status" ) && ok;
		ok = differential_hull_selftest_expect_contains( reject_output, "exactness_reason=hit_maximum_search_nodes", "batch partial exactness reason" ) && ok;
		ok = differential_hull_selftest_expect_contains( reject_output, "partial_jobs=1", "batch partial summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( reject_output, "all_jobs_hard_limit_free=0", "batch partial hard-limit summary" ) && ok;
		return ok;
	}

	static bool run_differential_hull_batch_resume_self_test( const std::string& job_file_path )
	{
		namespace fs = std::filesystem;

		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};
		auto read_text_file = [&]( const fs::path& path ) {
			std::ifstream in( path.string(), std::ios::in );
			std::ostringstream oss;
			oss << in.rdbuf();
			return oss.str();
		};

		const fs::path checkpoint_path = fs::temp_directory_path() / "neoalzette_differential_hull_batch_resume_selftest.ckpt";
		const fs::path selection_checkpoint_path = fs::temp_directory_path() / "neoalzette_differential_hull_batch_selection_resume_selftest.ckpt";
		const fs::path runtime_log_path = fs::temp_directory_path() / "neoalzette_differential_hull_batch_resume_selftest.runtime.log";
		const fs::path selection_runtime_log_path = fs::temp_directory_path() / "neoalzette_differential_hull_batch_selection_resume_selftest.runtime.log";
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
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( write_options ) == 0, "batch resume writer should succeed" );
				} );
		check( fs::exists( checkpoint_path ), "batch resume writer should create checkpoint file" );
		ok = differential_hull_selftest_expect_contains( write_output, "[Batch] checkpoint_resume=job_granularity", "batch resume writer checkpoint mode" ) && ok;

		DifferentialBatchHullPipelineCheckpointState checkpoint_state {};
		check( read_differential_batch_hull_pipeline_checkpoint( checkpoint_path.string(), checkpoint_state ), "batch resume reader should load checkpoint state" );
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
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( resume_options ) == 0, "batch resume main path should succeed" );
				} );
		ok = differential_hull_selftest_expect_contains( resume_output, "resume_checkpoint=" + checkpoint_path.string(), "batch resume checkpoint banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( resume_output, "[Batch][DifferentialHull] jobs=1 exact_jobs=1 partial_jobs=0 rejected_jobs=0", "batch resume summary" ) && ok;
		ok = differential_hull_selftest_expect_contains( resume_output, "[Batch][DifferentialHull][Combined] sources=1 endpoint_hulls=0 collected_trails=0 all_jobs_collected=1 all_jobs_exact=1", "batch resume combined summary" ) && ok;
		const std::string runtime_log_text = read_text_file( runtime_log_path );
		ok = differential_hull_selftest_expect_contains( runtime_log_text, "event=batch_resume_start", "batch resume runtime event start" ) && ok;
		ok = differential_hull_selftest_expect_contains( runtime_log_text, "event=batch_checkpoint_write", "batch resume runtime event checkpoint write" ) && ok;
		ok = differential_hull_selftest_expect_contains( runtime_log_text, "event=batch_stop", "batch resume runtime event stop" ) && ok;
		ok = differential_hull_selftest_expect_contains( runtime_log_text, "batch_resume_fingerprint_hash=", "batch resume runtime fingerprint hash" ) && ok;

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

		std::vector<DifferentialBatchJob> selection_jobs {};
		check( load_differential_batch_jobs_from_file( job_file_path, 0, selection_options.round_count, selection_jobs ) == 0, "selection resume selftest should load batch jobs" );
		DifferentialBatchBreadthDeepOrchestratorConfig selection_cfg {};
		selection_cfg.breadth_configuration = make_differential_base_search_configuration( selection_options );
		selection_cfg.breadth_runtime.maximum_search_nodes = std::max<std::uint64_t>( 1, selection_options.auto_breadth_maximum_search_nodes );
		selection_cfg.breadth_runtime.maximum_search_seconds = 0;
		selection_cfg.breadth_runtime.progress_every_seconds = selection_options.progress_every_seconds;
		selection_cfg.deep_configuration = make_differential_base_search_configuration( selection_options );
		differential_heuristic_branch_cap( selection_cfg.deep_configuration ) = 0;
		selection_cfg.deep_runtime.maximum_search_nodes = selection_options.auto_deep_maximum_search_nodes;
		selection_cfg.deep_runtime.maximum_search_seconds = selection_options.auto_max_time_seconds;
		selection_cfg.deep_runtime.progress_every_seconds = selection_options.progress_every_seconds;
		DifferentialBatchSourceSelectionCheckpointState selection_state =
			make_initial_differential_batch_source_selection_checkpoint_state( selection_jobs, selection_cfg );
		check( write_differential_batch_source_selection_checkpoint( selection_checkpoint_path.string(), selection_state ), "selection resume selftest should write source-selection checkpoint" );

		CommandLineOptions selection_resume_options = selection_options;
		selection_resume_options.batch_resume_checkpoint_path = selection_checkpoint_path.string();
		selection_resume_options.batch_job_file.clear();
		selection_resume_options.batch_job_file_was_provided = false;
		selection_resume_options.batch_runtime_log_path = selection_runtime_log_path.string();

		const std::string selection_resume_output =
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_batch_mode( selection_resume_options ) == 0, "selection-stage batch resume main path should succeed" );
				} );
		ok = differential_hull_selftest_expect_contains( selection_resume_output, "resume_checkpoint=" + selection_checkpoint_path.string(), "selection resume checkpoint banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( selection_resume_output, "[Batch][Breadth]", "selection resume breadth banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( selection_resume_output, "[Batch][DifferentialHull][Selection] input_jobs=1 selected_sources=1 selection_complete=1", "selection resume selection summary" ) && ok;
		const std::string selection_runtime_log_text = read_text_file( selection_runtime_log_path );
		ok = differential_hull_selftest_expect_contains( selection_runtime_log_text, "checkpoint_kind=differential_hull_batch_selection", "selection resume runtime checkpoint kind" ) && ok;
		ok = differential_hull_selftest_expect_contains( selection_runtime_log_text, "event=batch_resume_start", "selection resume runtime start" ) && ok;
		ok = differential_hull_selftest_expect_contains( selection_runtime_log_text, "batch_resume_fingerprint_hash=", "selection resume runtime fingerprint hash" ) && ok;

		std::error_code ec {};
		fs::remove( checkpoint_path, ec );
		fs::remove( selection_checkpoint_path, ec );
		fs::remove( runtime_log_path, ec );
		fs::remove( selection_runtime_log_path, ec );
		return ok;
	}

	static bool run_differential_subspace_campaign_automation_self_test()
	{
		namespace fs = std::filesystem;

		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};
		const fs::path source_file = fs::temp_directory_path() / "neoalzette_differential_subspace_campaign_selftest_jobs.txt";
		const fs::path output_dir = fs::temp_directory_path() / "neoalzette_differential_subspace_campaign_selftest";
		{
			std::error_code ec {};
			fs::remove( source_file, ec );
			fs::remove_all( output_dir, ec );
		}
		{
			std::ofstream out( source_file.string(), std::ios::out | std::ios::trunc );
			out << "1 1 0\n";
		}

		CommandLineOptions campaign_options {};
		campaign_options.round_count = 1;
		campaign_options.batch_thread_count = 1;
		campaign_options.collect_weight_cap = 0;
		campaign_options.collect_weight_cap_was_provided = true;
		campaign_options.maximum_search_nodes = 65'536;
		campaign_options.maximum_search_seconds = 0;
		campaign_options.campaign_source_file = source_file.string();
		campaign_options.campaign_source_file_was_provided = true;
		campaign_options.campaign_partition_count = 1;
		campaign_options.campaign_output_dir = output_dir.string();
		campaign_options.full_space_source_count = 1;
		campaign_options.full_space_source_count_was_provided = true;

		const fs::path shard_coverage_path = output_dir / "shard_0.coverage.txt";
		const fs::path shard_checkpoint_path = output_dir / "shard_0.subspace.ckpt";

		const std::string campaign_output =
			capture_differential_hull_selftest_stdout(
				[&]() {
					check( run_differential_partition_campaign( campaign_options ) == 0, "subspace campaign automation selftest should succeed" );
				} );
		ok = differential_hull_selftest_expect_contains( campaign_output, "[Campaign] shard=0/1", "campaign should run shard 0" ) && ok;
		check( fs::exists( shard_coverage_path ), "campaign should emit shard coverage report" );
		check( fs::exists( shard_checkpoint_path ), "campaign should emit shard checkpoint" );

		std::error_code ec {};
		fs::remove( source_file, ec );
		fs::remove_all( output_dir, ec );
		return ok;
	}

	static bool run_differential_hull_batch_mode_self_test( SelfTestScope scope )
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
			job_file_path = create_differential_hull_batch_selftest_job_file();

		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchExplicit )
			ok = run_differential_hull_batch_explicit_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchCombined )
			ok = run_differential_hull_batch_combined_self_test() && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchMultiTrajectory )
			ok = run_differential_hull_batch_multi_trajectory_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchReject )
			ok = run_differential_hull_batch_reject_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch || scope == SelfTestScope::BatchResume )
			ok = run_differential_hull_batch_resume_self_test( job_file_path ) && ok;
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Batch )
			ok = run_differential_subspace_campaign_automation_self_test() && ok;

		std::error_code ec {};
		if ( !job_file_path.empty() )
			std::filesystem::remove( std::filesystem::path( job_file_path ), ec );
		return ok;
	}

	static bool run_differential_hull_aggregator_synthetic_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};

		const auto make_step =
			[]( SearchWeight round_weight, std::uint32_t input_a, std::uint32_t input_b, std::uint32_t output_a, std::uint32_t output_b ) {
				DifferentialTrailStepRecord step {};
				step.round_index = 1;
				step.round_weight = round_weight;
				step.input_branch_a_difference = input_a;
				step.input_branch_b_difference = input_b;
				step.output_branch_a_difference = output_a;
				step.output_branch_b_difference = output_b;
				return step;
			};

		check( TwilightDream::hull_callback_aggregator::compute_growth_relative_delta( 0.0L, 0.0L ) == 0.0L, "compute_growth_relative_delta(0,0) should stay zero" );

		std::vector<DifferentialTrailStepRecord> trail_weight_2_a { make_step( 2, 0x01u, 0x02u, 0x10u, 0x20u ) };
		std::vector<DifferentialTrailStepRecord> trail_weight_2_b { make_step( 2, 0x03u, 0x04u, 0x30u, 0x40u ) };
		std::vector<DifferentialTrailStepRecord> trail_weight_1 { make_step( 1, 0x05u, 0x06u, 0x50u, 0x60u ) };

		DifferentialCallbackHullAggregator arrival_aggregator {};
		arrival_aggregator.set_maximum_stored_trails( 2 );

		DifferentialHullCollectedTrailView arrival_view_a {};
		arrival_view_a.total_weight = 2;
		arrival_view_a.output_branch_a_difference = 0x10u;
		arrival_view_a.output_branch_b_difference = 0x20u;
		arrival_view_a.exact_probability = 0.25L;
		arrival_view_a.trail = &trail_weight_2_a;

		DifferentialHullCollectedTrailView arrival_view_b {};
		arrival_view_b.total_weight = 2;
		arrival_view_b.output_branch_a_difference = 0x30u;
		arrival_view_b.output_branch_b_difference = 0x40u;
		arrival_view_b.exact_probability = 0.25L;
		arrival_view_b.trail = &trail_weight_2_b;

		DifferentialHullCollectedTrailView arrival_view_c {};
		arrival_view_c.total_weight = 1;
		arrival_view_c.output_branch_a_difference = 0x50u;
		arrival_view_c.output_branch_b_difference = 0x60u;
		arrival_view_c.exact_probability = 0.5L;
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
		check( arrival_aggregator.aggregate_probability == 1.0L, "arrival policy should accumulate total probability exactly" );
		check( arrival_aggregator.endpoint_hulls.size() == 3, "arrival policy should create one endpoint bucket per output difference" );
		if ( const auto* endpoint = arrival_aggregator.find_endpoint_hull( 0x50u, 0x60u ); endpoint )
		{
			check( endpoint->trail_count == 1, "arrival policy should track endpoint trail counts" );
			check( endpoint->aggregate_probability == 0.5L, "arrival policy should track endpoint probability" );
			check( endpoint->shell_summaries.size() == 1 && endpoint->shell_summaries.begin()->first == 1, "arrival policy should track endpoint shell summaries" );
		}
		else
		{
			check( false, "arrival policy should expose endpoint summary for the strongest output difference" );
		}
		check( arrival_aggregator.strongest_trail_probability == 0.5L, "arrival policy should track the strongest trail probability" );
		check( arrival_aggregator.strongest_output_branch_a_difference == 0x50u && arrival_aggregator.strongest_output_branch_b_difference == 0x60u, "arrival policy should retain strongest trail identity" );
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
		if ( const auto* endpoint = arrival_aggregator.find_endpoint_hull( 0x50u, 0x60u ); endpoint )
			check( endpoint->strongest_trail.empty(), "clearing arrival-policy storage should erase endpoint strongest trail steps" );

		DifferentialCallbackHullAggregator zero_storage_aggregator {};
		check( zero_storage_aggregator.on_trail( arrival_view_c ), "zero-storage policy should still accept collected trails" );
		check( zero_storage_aggregator.collected_trails.empty(), "zero-storage policy should store no trail records" );
		check( zero_storage_aggregator.strongest_trail_probability == 0.5L, "zero-storage policy should still track strongest trail probability" );
		check( zero_storage_aggregator.strongest_trail.empty(), "zero-storage policy should not retain the global strongest trail steps" );
		if ( const auto* shell = zero_storage_aggregator.find_shell_summary( 1 ); shell )
			check( shell->strongest_trail.empty(), "zero-storage policy should not retain shell strongest trail steps" );
		if ( const auto* endpoint = zero_storage_aggregator.find_endpoint_hull( 0x50u, 0x60u ); endpoint )
			check( endpoint->strongest_trail.empty(), "zero-storage policy should not retain endpoint strongest trail steps" );

		DifferentialCallbackHullAggregator strongest_aggregator {};
		strongest_aggregator.set_stored_trail_policy( StoredTrailPolicy::Strongest );
		strongest_aggregator.set_maximum_stored_trails( 2 );

		DifferentialHullCollectedTrailView strongest_view_weak {};
		strongest_view_weak.total_weight = 3;
		strongest_view_weak.output_branch_a_difference = 0x70u;
		strongest_view_weak.output_branch_b_difference = 0x80u;
		strongest_view_weak.exact_probability = 0.125L;

		DifferentialHullCollectedTrailView strongest_view_medium {};
		strongest_view_medium.total_weight = 2;
		strongest_view_medium.output_branch_a_difference = 0x90u;
		strongest_view_medium.output_branch_b_difference = 0xA0u;
		strongest_view_medium.exact_probability = 0.25L;

		DifferentialHullCollectedTrailView strongest_view_best {};
		strongest_view_best.total_weight = 1;
		strongest_view_best.output_branch_a_difference = 0xB0u;
		strongest_view_best.output_branch_b_difference = 0xC0u;
		strongest_view_best.exact_probability = 0.5L;

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

		DifferentialCallbackHullAggregator policy_switch_aggregator {};
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

		DifferentialCallbackHullAggregator endpoint_aggregator {};
		DifferentialHullCollectedTrailView endpoint_view_a {};
		endpoint_view_a.total_weight = 2;
		endpoint_view_a.output_branch_a_difference = 0xAAu;
		endpoint_view_a.output_branch_b_difference = 0xBBu;
		endpoint_view_a.exact_probability = 0.25L;
		DifferentialHullCollectedTrailView endpoint_view_b {};
		endpoint_view_b.total_weight = 3;
		endpoint_view_b.output_branch_a_difference = 0xAAu;
		endpoint_view_b.output_branch_b_difference = 0xBBu;
		endpoint_view_b.exact_probability = 0.125L;
		check( endpoint_aggregator.on_trail( endpoint_view_a ), "endpoint aggregation should accept the first trail" );
		check( endpoint_aggregator.on_trail( endpoint_view_b ), "endpoint aggregation should accept a second trail in the same endpoint bucket" );
		if ( const auto* endpoint = endpoint_aggregator.find_endpoint_hull( 0xAAu, 0xBBu ); endpoint )
		{
			check( endpoint->trail_count == 2, "endpoint aggregation should keep both trails in the same bucket" );
			check( endpoint->aggregate_probability == 0.375L, "endpoint aggregation should sum probability inside the bucket" );
			check( endpoint->shell_summaries.size() == 2, "endpoint aggregation should preserve shell splits inside the bucket" );
		}
		else
		{
			check( false, "endpoint aggregation should expose the shared output-difference bucket" );
		}

		DifferentialCallbackHullAggregator multi_source_aggregator {};
		multi_source_aggregator.set_maximum_stored_trails( 2 );
		std::vector<DifferentialTrailStepRecord> multi_source_trail_a { make_step( 1, 0x10u, 0x20u, 0xAAu, 0xBBu ) };
		std::vector<DifferentialTrailStepRecord> multi_source_trail_b { make_step( 2, 0x30u, 0x40u, 0xAAu, 0xBBu ) };
		DifferentialHullCollectedTrailView multi_source_view_a {};
		multi_source_view_a.total_weight = 1;
		multi_source_view_a.output_branch_a_difference = 0xAAu;
		multi_source_view_a.output_branch_b_difference = 0xBBu;
		multi_source_view_a.exact_probability = 0.25L;
		multi_source_view_a.trail = &multi_source_trail_a;
		DifferentialHullCollectedTrailView multi_source_view_b {};
		multi_source_view_b.total_weight = 2;
		multi_source_view_b.output_branch_a_difference = 0xAAu;
		multi_source_view_b.output_branch_b_difference = 0xBBu;
		multi_source_view_b.exact_probability = 0.125L;
		multi_source_view_b.trail = &multi_source_trail_b;
		check(
			multi_source_aggregator.make_callback_for_source( TwilightDream::hull_callback_aggregator::DifferentialHullSourceContext { true, 0, TwilightDream::hull_callback_aggregator::OuterSourceNamespace::WrapperFixedSource, 1, 0x10u, 0x20u } )( multi_source_view_a ),
			"multi-source aggregation should accept source #0" );
		check(
			multi_source_aggregator.make_callback_for_source( TwilightDream::hull_callback_aggregator::DifferentialHullSourceContext { true, 1, TwilightDream::hull_callback_aggregator::OuterSourceNamespace::WrapperFixedSource, 1, 0x30u, 0x40u } )( multi_source_view_b ),
			"multi-source aggregation should accept source #1" );
		check( multi_source_aggregator.source_hulls.size() == 2, "multi-source aggregation should track two distinct sources" );
		if ( const auto* source = multi_source_aggregator.find_source_hull( 1 ); source )
		{
			check( source->trail_count == 1, "multi-source aggregation should count trails per source" );
			check( source->source_input_branch_a_difference == 0x30u && source->source_input_branch_b_difference == 0x40u, "multi-source aggregation should preserve source identity" );
		}
		else
		{
			check( false, "multi-source aggregation should expose the second source bucket" );
		}
		if ( const auto* endpoint = multi_source_aggregator.find_endpoint_hull( 0xAAu, 0xBBu ); endpoint )
		{
			check( endpoint->trail_count == 2, "multi-source aggregation should merge sources into the shared endpoint hull" );
			check( endpoint->aggregate_probability == 0.375L, "multi-source aggregation should sum probability across sources" );
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

		DifferentialHullAggregationResult partial_aggregation {};
		partial_aggregation.exact_within_collect_weight_cap = false;
		partial_aggregation.used_non_strict_branch_cap = true;
		partial_aggregation.exactness_rejection_reason = StrictCertificationFailureReason::UsedBranchCap;
		check(
			!TwilightDream::hull_callback_aggregator::differential_aggregation_hit_any_limit( partial_aggregation ),
			"strictness-only exactness loss should not be classified as a hard limit" );
		const DifferentialGrowthShellRow partial_growth_row =
			TwilightDream::hull_callback_aggregator::make_differential_growth_shell_row( 1, zero_storage_aggregator, partial_aggregation );
		check( !partial_growth_row.hit_any_limit, "growth rows should keep strictness-only exactness loss separate from hard limits" );
		DifferentialStrictHullRuntimeResult partial_runtime_result {};
		partial_runtime_result.collected = true;
		partial_runtime_result.collect_weight_cap = 1;
		partial_runtime_result.aggregation_result = partial_aggregation;
		const DifferentialBatchHullJobSummary partial_job_summary =
			summarize_differential_batch_hull_job( 0, DifferentialBatchJob {}, partial_runtime_result, zero_storage_aggregator );
		check( !partial_job_summary.hit_any_limit, "batch summaries should not classify strictness-only exactness loss as a hard limit" );

		DifferentialHullCollectionOptions prune_mode_options {};
		prune_mode_options.residual_boundary_mode = DifferentialCollectorResidualBoundaryMode::PruneAtResidualBoundary;
		DifferentialHullCollectorExecutionState prune_mode_state {};
		DifferentialBestSearchConfiguration prune_mode_configuration {};
		prune_mode_configuration.round_count = 1;
		initialize_differential_hull_collection_state( 0u, 1u, prune_mode_configuration, prune_mode_options, prune_mode_state );
		check( prune_mode_state.residual_boundary_mode == DifferentialCollectorResidualBoundaryMode::PruneAtResidualBoundary, "differential collector state should preserve prune residual-boundary mode" );

		DifferentialStrictHullRuntimeOptions prune_runtime_options {};
		prune_runtime_options.residual_boundary_mode = DifferentialCollectorResidualBoundaryMode::PruneAtResidualBoundary;
		check( prune_runtime_options.residual_boundary_mode == DifferentialCollectorResidualBoundaryMode::PruneAtResidualBoundary, "differential strict hull runtime options should expose residual-boundary mode" );

		DifferentialCallbackHullAggregator stop_aggregator {};
		stop_aggregator.request_stop();
		check( !stop_aggregator.on_trail( arrival_view_a ), "stop policy should refuse callbacks after request_stop" );
		check( !stop_aggregator.found_any && stop_aggregator.collected_trail_count == 0, "stop policy should not mutate aggregator state after request_stop" );

		return ok;
	}

	static bool run_differential_hull_remaining_round_lower_bound_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};

		DifferentialBestSearchConfiguration custom_table_configuration {};
		custom_table_configuration.round_count = 2;
		custom_table_configuration.enable_remaining_round_lower_bound = true;
		custom_table_configuration.remaining_round_min_weight = { 777u, 3u, 8u };

		DifferentialHullCollectionOptions custom_table_options {};
		DifferentialHullCollectorExecutionState custom_table_state {};
		initialize_differential_hull_collection_state( 0u, 1u, custom_table_configuration, custom_table_options, custom_table_state );
		const auto& custom_table = custom_table_state.context.configuration.remaining_round_min_weight;
		check( custom_table.size() == 3u, "differential collector custom table should preserve round_count+1 entries" );
		check( custom_table[ 0 ] == 0u && custom_table[ 1 ] == 3u && custom_table[ 2 ] == 8u, "differential collector should preserve caller-provided remaining-round entries after normalization" );

		DifferentialHullCollectionOptions pruning_options {};
		pruning_options.collect_weight_cap = 79;
		pruning_options.runtime_controls.maximum_search_nodes = 4096;

		DifferentialBestSearchConfiguration pruning_without_bound_configuration {};
		pruning_without_bound_configuration.round_count = 1;
		pruning_without_bound_configuration.enable_remaining_round_lower_bound = false;
		DifferentialHullCollectorExecutionState pruning_without_bound_state {};
		initialize_differential_hull_collection_state( 0u, 1u, pruning_without_bound_configuration, pruning_options, pruning_without_bound_state );
		continue_differential_hull_collection_from_state( pruning_without_bound_state );

		DifferentialBestSearchConfiguration pruning_with_bound_configuration = pruning_without_bound_configuration;
		pruning_with_bound_configuration.enable_remaining_round_lower_bound = true;
		pruning_with_bound_configuration.remaining_round_min_weight = { 0u, 80u };
		DifferentialHullCollectorExecutionState pruning_with_bound_state {};
		initialize_differential_hull_collection_state( 0u, 1u, pruning_with_bound_configuration, pruning_options, pruning_with_bound_state );
		continue_differential_hull_collection_from_state( pruning_with_bound_state );

		const DifferentialHullAggregationResult& pruning_without_bound = pruning_without_bound_state.aggregation_result;
		const DifferentialHullAggregationResult& pruning_with_bound = pruning_with_bound_state.aggregation_result;
		const bool pruning_outputs_match =
			pruning_without_bound.found_any == pruning_with_bound.found_any &&
			pruning_without_bound.collected_trail_count == pruning_with_bound.collected_trail_count &&
			pruning_without_bound.aggregate_probability == pruning_with_bound.aggregate_probability &&
			pruning_without_bound.shell_summaries.empty() == pruning_with_bound.shell_summaries.empty() &&
			pruning_without_bound.strongest_trail.empty() == pruning_with_bound.strongest_trail.empty();
		check( pruning_outputs_match, "differential collector remaining-round pruning should preserve collection outputs" );
		check( pruning_with_bound.nodes_visited < pruning_without_bound.nodes_visited, "differential collector remaining-round lower bound should reduce visited nodes on an infeasible cap" );

		return ok;
	}

	static bool collect_differential_mod_add_sequence_for_self_test(
		std::uint32_t alpha,
		std::uint32_t beta,
		std::uint32_t output_hint,
		SearchWeight weight_cap,
		int word_bits,
		bool enable_weight_sliced_pddt,
		SearchWeight weight_sliced_pddt_max_weight,
		std::vector<std::pair<std::uint32_t, SearchWeight>>& out )
	{
		out.clear();
		DifferentialBestSearchContext context {};
		context.configuration.enable_weight_sliced_pddt = enable_weight_sliced_pddt;
		context.configuration.weight_sliced_pddt_max_weight = weight_sliced_pddt_max_weight;
		context.runtime_controls.maximum_search_nodes = 0;
		context.runtime_controls.maximum_search_seconds = 0;

		ModularAdditionEnumerator enumerator {};
		enumerator.reset( alpha, beta, output_hint, weight_cap, 0, 0u, word_bits );

		std::uint32_t gamma = 0;
		SearchWeight  weight = 0;
		while ( enumerator.next( context, gamma, weight ) )
		{
			out.emplace_back( gamma, weight );
		}
		return !enumerator.stop_due_to_limits;
	}

	static bool differential_mod_add_pair_multiset_equal(
		const std::vector<std::pair<std::uint32_t, SearchWeight>>& lhs,
		const std::vector<std::pair<std::uint32_t, SearchWeight>>& rhs )
	{
		auto lhs_sorted = lhs;
		auto rhs_sorted = rhs;
		std::sort( lhs_sorted.begin(), lhs_sorted.end() );
		std::sort( rhs_sorted.begin(), rhs_sorted.end() );
		return lhs_sorted == rhs_sorted;
	}

	static bool run_differential_weight_sliced_pddt_strictness_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};

		const std::uint32_t alpha = 0x00000001u;
		const std::uint32_t beta = 0x00000001u;
		const std::uint32_t output_hint = 0x00000000u;
		const SearchWeight weight_cap = 5;
		const int word_bits = 8;

		std::vector<std::pair<std::uint32_t, SearchWeight>> baseline_sequence {};
		g_weight_sliced_pddt_cache.clear_keep_enabled();
		g_weight_sliced_pddt_cache.configure( false, 0 );
		check(
			collect_differential_mod_add_sequence_for_self_test(
				alpha,
				beta,
				output_hint,
				weight_cap,
				word_bits,
				false,
				0,
				baseline_sequence ),
			"differential w-pDDT baseline collection should succeed" );
		check( !baseline_sequence.empty(), "differential w-pDDT baseline should produce at least one candidate" );

		std::vector<std::pair<std::uint32_t, SearchWeight>> cached_sequence {};
		g_weight_sliced_pddt_cache.clear_keep_enabled();
		g_weight_sliced_pddt_cache.configure( true, weight_cap );
		check(
			collect_differential_mod_add_sequence_for_self_test(
				alpha,
				beta,
				output_hint,
				weight_cap,
				word_bits,
				true,
				weight_cap,
				cached_sequence ),
			"differential w-pDDT cached collection should succeed" );
		check( baseline_sequence == cached_sequence, "differential w-pDDT cache hit path should preserve the exact shell order for the same hint" );

		std::vector<std::pair<std::uint32_t, SearchWeight>> fallback_sequence {};
		g_weight_sliced_pddt_cache.clear_keep_enabled();
		g_weight_sliced_pddt_cache.configure( true, 0 );
		check(
			collect_differential_mod_add_sequence_for_self_test(
				alpha,
				beta,
				output_hint,
				weight_cap,
				word_bits,
				true,
				0,
				fallback_sequence ),
			"differential w-pDDT fallback collection should succeed" );
		check( baseline_sequence == fallback_sequence, "differential w-pDDT cache miss path should fall back to the exact shell order" );

		bool found_hint_sensitive_case = false;
		for ( std::uint32_t probe_alpha = 1u; probe_alpha <= 15u && !found_hint_sensitive_case; ++probe_alpha )
		{
			for ( std::uint32_t probe_beta = 1u; probe_beta <= 15u && !found_hint_sensitive_case; ++probe_beta )
			{
				std::vector<std::pair<std::uint32_t, SearchWeight>> baseline_a {};
				std::vector<std::pair<std::uint32_t, SearchWeight>> baseline_b {};
				if ( !collect_differential_mod_add_sequence_for_self_test( probe_alpha, probe_beta, 0x00u, weight_cap, word_bits, false, 0, baseline_a ) )
					continue;
				if ( !collect_differential_mod_add_sequence_for_self_test( probe_alpha, probe_beta, 0xFFu, weight_cap, word_bits, false, 0, baseline_b ) )
					continue;
				if ( baseline_a.empty() || baseline_b.empty() )
					continue;
				if ( baseline_a == baseline_b )
					continue;
				if ( !differential_mod_add_pair_multiset_equal( baseline_a, baseline_b ) )
					continue;

				std::vector<std::pair<std::uint32_t, SearchWeight>> warmed_sequence_a {};
				g_weight_sliced_pddt_cache.clear_keep_enabled();
				g_weight_sliced_pddt_cache.configure( true, weight_cap );
				std::vector<std::pair<std::uint32_t, SearchWeight>> warmup_sequence {};
				if ( !collect_differential_mod_add_sequence_for_self_test( probe_alpha, probe_beta, 0xFFu, weight_cap, word_bits, true, weight_cap, warmup_sequence ) )
					continue;
				if ( !collect_differential_mod_add_sequence_for_self_test( probe_alpha, probe_beta, 0x00u, weight_cap, word_bits, true, weight_cap, warmed_sequence_a ) )
					continue;
				check( baseline_a == warmed_sequence_a, "differential w-pDDT cache key should preserve hint-biased exact shell order across warmups" );
				found_hint_sensitive_case = true;
			}
		}
		check( found_hint_sensitive_case, "differential w-pDDT strictness test should find at least one hint-sensitive shell-order case" );

		g_weight_sliced_pddt_cache.clear_keep_enabled();
		g_weight_sliced_pddt_cache.configure( false, 0 );
		return ok;
	}

	static bool run_differential_hull_wrapper_smoke_self_test()
	{
		bool ok = true;
		auto check = [&]( bool condition, const std::string& message ) {
			ok = differential_hull_selftest_check( condition, message ) && ok;
		};

		check( run_differential_hull_remaining_round_lower_bound_self_test(), "differential collector remaining-round lower-bound checks should pass" );
		check( run_differential_weight_sliced_pddt_strictness_self_test(), "differential w-pDDT strictness checks should pass" );

		CommandLineOptions exact_options {};
		exact_options.round_count = 1;
		exact_options.collect_weight_cap = 0;
		exact_options.collect_weight_cap_was_provided = true;
		exact_options.maximum_search_nodes = 100000;
		exact_options.maximum_search_seconds = 1;

		const DifferentialBestSearchConfiguration exact_configuration = make_differential_base_search_configuration( exact_options );
		DifferentialCallbackHullAggregator exact_aggregator {};
		const DifferentialStrictHullRuntimeResult exact_result =
			run_differential_strict_hull_runtime(
				0u,
				0u,
				exact_configuration,
				make_differential_runtime_options( exact_options, nullptr, &exact_aggregator ),
				false,
				false );
		check( exact_result.collected, "exact smoke should complete the strict hull runtime" );
		check( !exact_result.best_search_executed && !exact_result.used_best_weight_reference, "exact smoke should stay in explicit-cap mode" );
		check( exact_result.collect_weight_cap == 0, "exact smoke should preserve collect_weight_cap=0" );
		check( exact_result.aggregation_result.exact_within_collect_weight_cap, "exact smoke should be exact within the collect cap" );
		check( exact_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::None, "exact smoke should report no exactness rejection" );
		check( exact_aggregator.collected_trail_count == 1 && exact_aggregator.found_any, "exact smoke should collect the single zero trail" );
		check( exact_aggregator.aggregate_probability == 1.0L, "exact smoke should aggregate to probability 1" );
		check( exact_aggregator.endpoint_hulls.size() == 1, "exact smoke should expose exactly one endpoint hull" );
		if ( const auto* endpoint = exact_aggregator.find_endpoint_hull( 0u, 0u ); endpoint )
		{
			check( endpoint->trail_count == 1, "exact smoke should keep the zero output difference in one endpoint bucket" );
			check( endpoint->aggregate_probability == 1.0L, "exact smoke should keep exact probability in the endpoint bucket" );
		}
		else
		{
			check( false, "exact smoke should expose the zero endpoint bucket" );
		}
		check( exact_aggregator.find_shell_summary( 0 ) && exact_aggregator.find_shell_summary( 0 )->trail_count == 1, "exact smoke should produce shell 0 only" );
		const std::string exact_output =
			capture_differential_hull_selftest_stdout(
				[&]() {
					print_result( exact_result, exact_aggregator, exact_options );
				} );
		ok = differential_hull_selftest_expect_contains( exact_output, "[HullWrapper][Differential]", "exact smoke output banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "endpoint_hull_count=1", "exact smoke endpoint count" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "strict_endpoint_hulls_within_collect_weight_cap=1", "exact smoke strict endpoint exactness flag" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "exact_within_collect_weight_cap=1", "exact smoke exactness flag" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "exactness_rejection_reason=none", "exact smoke exactness reason" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "endpoint_exact_probability=1.0000000000e+00", "exact smoke endpoint probability" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "output_branch_a_difference=0x00000000", "exact smoke endpoint output difference A" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "output_branch_b_difference=0x00000000", "exact smoke endpoint output difference B" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "global_collected_trail_probability_mass=1.0000000000e+00", "exact smoke global diagnostic probability mass" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "collected_trails=1", "exact smoke collected trail count" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "global collected-trail probability mass mixes multiple output-difference endpoints and is diagnostic only", "exact smoke diagnostic disclaimer" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "global_shell_diagnostic_begin", "exact smoke shell diagnostic banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( exact_output, "shell_weight=0", "exact smoke shell summary" ) && ok;

		CommandLineOptions limit_options = exact_options;
		limit_options.maximum_search_nodes = 1;
		limit_options.maximum_search_seconds = 0;

		const DifferentialBestSearchConfiguration limit_configuration = make_differential_base_search_configuration( limit_options );
		DifferentialCallbackHullAggregator limit_aggregator {};
		const DifferentialStrictHullRuntimeResult limit_result =
			run_differential_strict_hull_runtime(
				0u,
				0u,
				limit_configuration,
				make_differential_runtime_options( limit_options, nullptr, &limit_aggregator ),
				false,
				false );
		check( limit_result.collected, "limit smoke should still complete the explicit-cap runtime wrapper path" );
		check( !limit_aggregator.found_any && limit_aggregator.collected_trail_count == 0, "limit smoke should collect no trails under maxnodes=1" );
		check( limit_result.aggregation_result.hit_maximum_search_nodes, "limit smoke should report maximum_search_nodes" );
		check( !limit_result.aggregation_result.exact_within_collect_weight_cap, "limit smoke should not remain exact after hitting maxnodes" );
		check( limit_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::HitMaximumSearchNodes, "limit smoke should classify exactness loss as maxnodes" );
		const std::string limit_output =
			capture_differential_hull_selftest_stdout(
				[&]() {
					print_result( limit_result, limit_aggregator, limit_options );
				} );
		ok = differential_hull_selftest_expect_contains( limit_output, "no trail collected within limits.", "limit smoke banner" ) && ok;
		ok = differential_hull_selftest_expect_contains( limit_output, "exact_within_collect_weight_cap=0", "limit smoke exactness flag" ) && ok;
		ok = differential_hull_selftest_expect_contains( limit_output, "exactness_rejection_reason=hit_maximum_search_nodes", "limit smoke exactness reason" ) && ok;
		ok = differential_hull_selftest_expect_contains( limit_output, "nodes_visited=1", "limit smoke nodes_visited" ) && ok;

		CommandLineOptions collection_limit_options = exact_options;
		collection_limit_options.collect_weight_cap = 20;
		collection_limit_options.maximum_collected_trails = 1;
		collection_limit_options.maximum_search_seconds = 0;

		const DifferentialBestSearchConfiguration collection_limit_configuration = make_differential_base_search_configuration( collection_limit_options );
		DifferentialCallbackHullAggregator collection_limit_aggregator {};
		const DifferentialStrictHullRuntimeResult collection_limit_result =
			run_differential_strict_hull_runtime(
				0u,
				0u,
				collection_limit_configuration,
				make_differential_runtime_options( collection_limit_options, nullptr, &collection_limit_aggregator ),
				false,
				false );
		check( collection_limit_result.collected, "collection-limit smoke should still produce a runtime result" );
		check( collection_limit_result.aggregation_result.hit_collection_limit, "collection-limit smoke should report maximum_collected_trails" );
		check( !collection_limit_result.aggregation_result.exact_within_collect_weight_cap, "collection-limit smoke should lose exactness" );
		check(
			collection_limit_result.aggregation_result.exactness_rejection_reason == StrictCertificationFailureReason::HitCollectionLimit,
			"collection-limit smoke should classify exactness loss as hit_collection_limit" );
		const std::string collection_limit_output =
			capture_differential_hull_selftest_stdout(
				[&]() {
					print_result( collection_limit_result, collection_limit_aggregator, collection_limit_options );
				} );
		ok = differential_hull_selftest_expect_contains( collection_limit_output, "exact_within_collect_weight_cap=0", "collection-limit smoke exactness flag" ) && ok;
		ok = differential_hull_selftest_expect_contains( collection_limit_output, "exactness_rejection_reason=hit_collection_limit", "collection-limit smoke exactness reason" ) && ok;
		ok = differential_hull_selftest_expect_contains( collection_limit_output, "[HIT maximum_collected_trails]", "collection-limit smoke limit banner" ) && ok;

		return ok;
	}

	static int run_differential_hull_wrapper_self_test( SelfTestScope scope )
	{
		std::cout << "[SelfTest][DifferentialHull] scope=" << selftest_scope_to_string( scope ) << "\n";
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Synthetic )
		{
			std::cout << "[SelfTest][DifferentialHull] synthetic aggregator checks\n";
			if ( !run_differential_hull_aggregator_synthetic_self_test() )
				return 1;
		}
		if ( scope == SelfTestScope::All || scope == SelfTestScope::Smoke )
		{
			std::cout << "[SelfTest][DifferentialHull] deterministic wrapper smokes\n";
			if ( !run_differential_hull_wrapper_smoke_self_test() )
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
			std::cout << "[SelfTest][DifferentialHull] batch pipeline checks\n";
			if ( !run_differential_hull_batch_mode_self_test( scope ) )
				return 1;
		}
		std::cout << "[SelfTest][DifferentialHull] OK\n";
		return 0;
	}
