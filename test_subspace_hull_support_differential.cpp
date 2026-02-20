// Split from test_subspace_hull_support.cpp.

using TwilightDream::hull_callback_aggregator::DifferentialBatchHullBestSearchSeed;
using TwilightDream::hull_callback_aggregator::DifferentialCallbackShellSummary;
using TwilightDream::hull_callback_aggregator::DifferentialCollectedTrailRecord;
using TwilightDream::hull_callback_aggregator::DifferentialEndpointHullSummary;
using TwilightDream::hull_callback_aggregator::DifferentialEndpointShellSummary;
using TwilightDream::hull_callback_aggregator::DifferentialHullSourceKey;
using TwilightDream::hull_callback_aggregator::DifferentialSourceHullSummary;

static bool write_differential_subspace_coverage_report(
		const std::string& path,
		const CommandLineOptions& options,
		const DifferentialBatchHullRunSummary& batch_summary,
		const SubspaceCoverageSummary& coverage_summary,
		const std::string& source_file_path,
		bool explicit_cap_mode,
		SearchWeight explicit_collect_weight_cap,
		const GeneratedSubspaceEvidenceSummary& generated_evidence )
	{
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
		{
			std::cerr << "[Subspace] ERROR: cannot open coverage summary output path: " << path << "\n";
			return false;
		}
		out << "kind=differential_subspace_coverage\n";
		out << "source_file=" << source_file_path << "\n";
		out << "subspace_partition_count=" << options.subspace_partition_count << "\n";
		out << "subspace_partition_index=" << options.subspace_partition_index << "\n";
		out << "collect_mode=" << ( explicit_cap_mode ? "explicit_cap" : "best_weight_plus_window" ) << "\n";
		out << "collect_weight_cap=" << ( explicit_cap_mode ? explicit_collect_weight_cap : -1 ) << "\n";
		out << "collect_weight_window=" << ( explicit_cap_mode ? -1 : display_search_weight( options.collect_weight_window ) ) << "\n";
		out << "prescribed_source_count=" << coverage_summary.prescribed_source_count << "\n";
		out << "active_partition_source_count=" << coverage_summary.active_partition_source_count << "\n";
		out << "exact_jobs=" << batch_summary.exact_jobs << "\n";
		out << "partial_jobs=" << batch_summary.partial_jobs << "\n";
		out << "rejected_jobs=" << batch_summary.rejected_jobs << "\n";
		out << "evidence_record_count=" << coverage_summary.evidence_record_count << "\n";
		out << "evidence_source_count=" << coverage_summary.evidence_source_count << "\n";
		out << "evidence_source_count_excluded_within_cap=" << coverage_summary.evidence_source_count_excluded_within_cap << "\n";
		out << "full_space_source_count=" << coverage_summary.full_space_source_count << "\n";
		out << "full_space_source_count_was_provided=" << ( coverage_summary.full_space_source_count_was_provided ? 1 : 0 ) << "\n";
		out << "full_space_exact_within_collect_weight_cap=" << ( coverage_summary.full_space_exact_within_collect_weight_cap ? 1 : 0 ) << "\n";
		out << "generated_evidence_available=" << ( generated_evidence.available ? 1 : 0 ) << "\n";
		out << "generated_evidence_certification=" << generated_evidence_certification_to_string( generated_evidence.certification ) << "\n";
		out << "generated_evidence_min_best_weight=" << generated_evidence.min_best_weight << "\n";
		out << "generated_evidence_exact_best_jobs=" << generated_evidence.exact_best_job_count << "\n";
		out << "generated_evidence_lower_bound_only_jobs=" << generated_evidence.lower_bound_only_job_count << "\n";
		out << "generated_evidence_unresolved_jobs=" << generated_evidence.unresolved_job_count << "\n";
		out << "generated_evidence_prepass_jobs=" << generated_evidence.prepass_job_count << "\n";
		out << "generated_evidence_prepass_total_nodes=" << generated_evidence.prepass_total_nodes << "\n";
		return true;
	}

	struct DifferentialEvidenceOnlyReportRecord
	{
		std::string source_file {};
		std::uint64_t subspace_partition_count = 0;
		std::uint64_t subspace_partition_index = 0;
		std::uint64_t prescribed_source_count = 0;
		std::uint64_t active_partition_source_count = 0;
		bool generated_evidence_available = false;
		std::string generated_evidence_certification {};
		SearchWeight generated_evidence_min_best_weight = INFINITE_WEIGHT;
	};

	static bool read_key_value_file( const std::string& path, std::map<std::string, std::string>& kv )
	{
		std::ifstream in( path );
		if ( !in )
		{
			std::cerr << "[CoverageMerge] ERROR: cannot open report file: " << path << "\n";
			return false;
		}
		std::string line {};
		while ( std::getline( in, line ) )
		{
			if ( line.empty() )
				continue;
			if ( const std::size_t eq = line.find( '=' ); eq != std::string::npos )
				kv[ line.substr( 0, eq ) ] = line.substr( eq + 1 );
		}
		return true;
	}

	static bool parse_u64_string( const std::string& text, std::uint64_t& value )
	{
		return parse_unsigned_integer_64( text.c_str(), value );
	}

	static bool parse_weight_string( const std::string& text, SearchWeight& value )
	{
		if ( text == "-1" )
		{
			value = INFINITE_WEIGHT;
			return true;
		}
		std::uint64_t raw = 0;
		if ( !parse_u64_string( text, raw ) )
			return false;
		value = static_cast<SearchWeight>( raw );
		return true;
	}

	static bool read_differential_evidence_only_report(
		const std::string& path,
		DifferentialEvidenceOnlyReportRecord& report )
	{
		std::map<std::string, std::string> kv {};
		if ( !read_key_value_file( path, kv ) )
			return false;
		if ( kv[ "kind" ] != "differential_subspace_evidence_only_report" )
		{
			std::cerr << "[AdaptiveCampaign] ERROR: unsupported evidence-only report kind in " << path << "\n";
			return false;
		}
		report.source_file = kv[ "source_file" ];
		report.generated_evidence_certification = kv[ "generated_evidence_certification" ];
		if ( !parse_u64_string( kv[ "subspace_partition_count" ], report.subspace_partition_count ) ) return false;
		if ( !parse_u64_string( kv[ "subspace_partition_index" ], report.subspace_partition_index ) ) return false;
		if ( !parse_u64_string( kv[ "prescribed_source_count" ], report.prescribed_source_count ) ) return false;
		if ( !parse_u64_string( kv[ "active_partition_source_count" ], report.active_partition_source_count ) ) return false;
		report.generated_evidence_available = ( kv[ "generated_evidence_available" ] == "1" );
		if ( !parse_weight_string( kv[ "generated_evidence_min_best_weight" ], report.generated_evidence_min_best_weight ) ) return false;
		return true;
	}


	static std::string campaign_shard_stem( const std::string& output_dir, std::uint64_t shard_index, std::uint64_t shard_count )
	{
		namespace fs = std::filesystem;
		const int width = std::max( 1, int( std::to_string( std::max<std::uint64_t>( 0, shard_count - 1 ) ).size() ) );
		std::ostringstream filename;
		filename << "shard_" << std::setw( width ) << std::setfill( '0' ) << shard_index;
		return ( fs::path( output_dir ) / filename.str() ).string();
	}

	static int run_differential_partition_campaign( const CommandLineOptions& options )
	{
		namespace fs = std::filesystem;

		if ( !options.campaign_source_file_was_provided || options.campaign_source_file.empty() )
		{
			std::cerr << "[Campaign] ERROR: --campaign-source-file PATH is required.\n";
			return 1;
		}
		if ( options.campaign_partition_count == 0 )
		{
			std::cerr << "[Campaign] ERROR: --campaign-partition-count must be >= 1.\n";
			return 1;
		}
		if ( options.campaign_output_dir.empty() )
		{
			std::cerr << "[Campaign] ERROR: --campaign-output-dir PATH is required.\n";
			return 1;
		}
		if ( options.campaign_adaptive_mode && options.subspace_evidence_only_mode )
		{
			std::cerr << "[Campaign] ERROR: --adaptive-campaign already mixes evidence-only and hull; do not combine it with --subspace-evidence-only.\n";
			return 1;
		}
		if ( options.campaign_adaptive_mode && !options.collect_weight_cap_was_provided )
		{
			std::cerr << "[Campaign] ERROR: --adaptive-campaign requires --collect-weight-cap W.\n";
			return 1;
		}

		std::error_code ec {};
		fs::create_directories( fs::path( options.campaign_output_dir ), ec );
		if ( ec )
		{
			std::cerr << "[Campaign] ERROR: cannot create output directory: " << options.campaign_output_dir << "\n";
			return 1;
		}

		std::uint64_t adaptive_hull_shards = 0;
		std::uint64_t adaptive_evidence_only_shards = 0;

		std::cout << "[Campaign] mode=differential_partition_campaign\n";
		std::cout << "  source_file=" << options.campaign_source_file
				  << "  partitions=" << options.campaign_partition_count
				  << "  output_dir=" << options.campaign_output_dir
				  << "  scheduler=" << ( options.campaign_adaptive_mode ? "adaptive_evidence_then_hull" : ( options.subspace_evidence_only_mode ? "evidence_only" : "uniform_hull" ) )
				  << "\n";

		for ( std::uint64_t shard_index = 0; shard_index < options.campaign_partition_count; ++shard_index )
		{
			CommandLineOptions shard_options = options;
			shard_options.campaign_mode = false;
			shard_options.campaign_source_file.clear();
			shard_options.campaign_source_file_was_provided = false;
			shard_options.campaign_partition_count = 0;
			shard_options.campaign_output_dir.clear();
			shard_options.campaign_label_prefix.clear();
			shard_options.subspace_hull_mode = true;
			shard_options.subspace_job_file = options.campaign_source_file;
			shard_options.subspace_job_file_was_provided = true;
			shard_options.batch_job_file.clear();
			shard_options.batch_job_file_was_provided = false;
			shard_options.subspace_partition_count = options.campaign_partition_count;
			shard_options.subspace_partition_index = shard_index;

			const std::string stem = campaign_shard_stem( options.campaign_output_dir, shard_index, options.campaign_partition_count );
			const std::string label_prefix =
				!options.campaign_label_prefix.empty() ?
					( options.campaign_label_prefix + "_" ) :
					std::string {};
			const std::string shard_label =
				label_prefix + "partition_" + std::to_string( shard_index ) + "_of_" + std::to_string( options.campaign_partition_count );

			if ( options.campaign_adaptive_mode )
			{
				CommandLineOptions probe_options = shard_options;
				probe_options.subspace_hull_mode = false;
				probe_options.subspace_evidence_only_mode = true;
				probe_options.subspace_coverage_out_path = stem + ".evidence_report.txt";
				probe_options.subspace_evidence_out_path.clear();
				probe_options.subspace_evidence_label = shard_label;
				probe_options.subspace_runtime_log_path = stem + ".evidence.runtime.log";
				probe_options.subspace_checkpoint_out_path.clear();
				probe_options.enable_bnb_residual = false;
				probe_options.bnb_residual_was_provided = false;

				std::cout << "[Campaign][Adaptive] shard=" << shard_index << "/" << options.campaign_partition_count
						  << "  probe_report_out=" << probe_options.subspace_coverage_out_path << "\n";
				const int probe_rc = run_differential_subspace_evidence_only_mode( probe_options );
				if ( probe_rc != 0 )
					return probe_rc;

				DifferentialEvidenceOnlyReportRecord probe_report {};
				if ( !read_differential_evidence_only_report( probe_options.subspace_coverage_out_path, probe_report ) )
					return 1;

				const bool shard_excluded_within_cap =
					probe_report.generated_evidence_available &&
					probe_report.generated_evidence_min_best_weight > options.collect_weight_cap;
				if ( shard_excluded_within_cap )
				{
					const std::string evidence_path = stem + ".evidence.txt";
					if ( !write_generated_subspace_evidence_record( evidence_path, probe_report.active_partition_source_count, probe_report.generated_evidence_min_best_weight, shard_label ) )
						return 1;
					++adaptive_evidence_only_shards;
					std::cout << "[Campaign][Adaptive] shard=" << shard_index
							  << " decision=evidence_only"
							  << " min_best_weight=" << probe_report.generated_evidence_min_best_weight
							  << " evidence_out=" << evidence_path << "\n";
					continue;
				}

				shard_options.subspace_hull_mode = true;
				shard_options.subspace_evidence_only_mode = false;
				shard_options.subspace_coverage_out_path = stem + ".coverage.txt";
				shard_options.subspace_checkpoint_out_path = stem + ".subspace.ckpt";
				shard_options.subspace_runtime_log_path = stem + ".runtime.log";
				shard_options.subspace_evidence_out_path.clear();
				shard_options.subspace_evidence_label.clear();
				std::cout << "[Campaign][Adaptive] shard=" << shard_index
						  << " decision=promote_to_hull"
						  << " probe_min_best_weight=" << probe_report.generated_evidence_min_best_weight
						  << " coverage_out=" << shard_options.subspace_coverage_out_path << "\n";
				const int hull_rc = run_differential_subspace_mode( shard_options );
				if ( hull_rc != 0 )
					return hull_rc;
				++adaptive_hull_shards;
				continue;
			}

			shard_options.subspace_coverage_out_path =
				options.subspace_evidence_only_mode ?
					( stem + ".evidence_report.txt" ) :
					( stem + ".coverage.txt" );
			shard_options.subspace_checkpoint_out_path =
				options.subspace_evidence_only_mode ?
					std::string {} :
					( stem + ".subspace.ckpt" );
			shard_options.subspace_runtime_log_path = stem + ".runtime.log";
			if ( options.subspace_evidence_only_mode || !options.collect_weight_cap_was_provided )
			{
				shard_options.subspace_evidence_out_path = stem + ".evidence.txt";
				shard_options.subspace_evidence_label = shard_label;
			}
			else
			{
				shard_options.subspace_evidence_out_path.clear();
				shard_options.subspace_evidence_label.clear();
			}

			std::cout << "[Campaign] shard=" << shard_index << "/" << options.campaign_partition_count
					  << "  report_out=" << shard_options.subspace_coverage_out_path << "\n";
			const int rc = run_differential_subspace_mode( shard_options );
			if ( rc != 0 )
				return rc;
		}
		if ( options.campaign_adaptive_mode )
		{
			std::cout << "[Campaign][Adaptive] hull_shards=" << adaptive_hull_shards
					  << " evidence_only_shards=" << adaptive_evidence_only_shards
					  << "\n";
		}
		return 0;
	}

	static const char* differential_batch_selection_stage_to_string( DifferentialBatchSelectionCheckpointStage stage ) noexcept
	{
		switch ( stage )
		{
		case DifferentialBatchSelectionCheckpointStage::Breadth:
			return "selection_breadth";
		case DifferentialBatchSelectionCheckpointStage::DeepReady:
			return "selection_deep_ready";
		default:
			return "selection_unknown";
		}
	}

	static void write_differential_batch_runtime_event(
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

	static bool write_differential_subspace_evidence_only_report(
		const std::string& path,
		const CommandLineOptions& options,
		const std::string& source_file_path,
		std::uint64_t prescribed_source_count,
		std::uint64_t active_partition_source_count,
		const GeneratedSubspaceEvidenceSummary& generated_evidence )
	{
		std::ofstream out( path, std::ios::out | std::ios::trunc );
		if ( !out )
		{
			std::cerr << "[SubspaceEvidence] ERROR: cannot open evidence-only report output path: " << path << "\n";
			return false;
		}
		out << "kind=differential_subspace_evidence_only_report\n";
		out << "source_file=" << source_file_path << "\n";
		out << "subspace_partition_count=" << options.subspace_partition_count << "\n";
		out << "subspace_partition_index=" << options.subspace_partition_index << "\n";
		out << "collect_weight_cap=" << options.collect_weight_cap << "\n";
		out << "prescribed_source_count=" << prescribed_source_count << "\n";
		out << "active_partition_source_count=" << active_partition_source_count << "\n";
		out << "generated_evidence_available=" << ( generated_evidence.available ? 1 : 0 ) << "\n";
		out << "generated_evidence_certification=" << generated_evidence_certification_to_string( generated_evidence.certification ) << "\n";
		out << "generated_evidence_min_best_weight=" << generated_evidence.min_best_weight << "\n";
		out << "generated_evidence_exact_best_jobs=" << generated_evidence.exact_best_job_count << "\n";
		out << "generated_evidence_lower_bound_only_jobs=" << generated_evidence.lower_bound_only_job_count << "\n";
		out << "generated_evidence_unresolved_jobs=" << generated_evidence.unresolved_job_count << "\n";
		out << "generated_evidence_prepass_jobs=" << generated_evidence.prepass_job_count << "\n";
		out << "generated_evidence_prepass_total_nodes=" << generated_evidence.prepass_total_nodes << "\n";
		return true;
	}

	static int run_differential_subspace_evidence_only_mode( const CommandLineOptions& options )
	{
		if ( options.bnb_residual_was_provided )
		{
			std::cerr << "[SubspaceEvidence] ERROR: --bnb-residual is collector-only and is not valid in evidence-only mode.\n";
			return 1;
		}
		if ( options.batch_job_count != 0 || options.batch_seed_was_provided || !options.batch_resume_checkpoint_path.empty() )
		{
			std::cerr << "[SubspaceEvidence] ERROR: RNG batch-job generation and --batch-resume are not valid in evidence-only mode.\n";
			return 1;
		}
		if ( !options.best_search_resume_checkpoint_path.empty() ||
			 !options.subspace_resume_checkpoint_path.empty() )
		{
			std::cerr << "[SubspaceEvidence] ERROR: resume/checkpoint is not supported yet in evidence-only mode.\n";
			return 1;
		}
		if ( options.best_search_checkpoint_out_was_provided || options.best_search_checkpoint_every_seconds_was_provided || !options.subspace_checkpoint_out_path.empty() )
		{
			std::cerr << "[SubspaceEvidence] ERROR: binary checkpoints are not supported in evidence-only mode.\n";
			return 1;
		}
		if ( !options.collect_weight_cap_was_provided )
		{
			std::cerr << "[SubspaceEvidence] ERROR: evidence-only mode requires --collect-weight-cap W so the exclusion target is explicit.\n";
			return 1;
		}
		if ( !options.best_search_history_log_path.empty() || !options.best_search_runtime_log_path.empty() )
		{
			std::cerr << "[SubspaceEvidence] ERROR: per-job best-search logs are not supported in evidence-only mode.\n";
			return 1;
		}

		const std::string source_file_path =
			options.subspace_job_file_was_provided ? options.subspace_job_file :
			( options.batch_job_file_was_provided ? options.batch_job_file : std::string {} );
		if ( source_file_path.empty() )
		{
			std::cerr << "[SubspaceEvidence] ERROR: evidence-only mode requires --subspace-file PATH or --batch-file PATH together with prescribed-subset semantics.\n";
			return 1;
		}

		std::vector<DifferentialBatchJob> jobs {};
		if ( load_differential_batch_jobs_from_file( source_file_path, 0, options.round_count, jobs ) != 0 )
			return 1;
		const std::uint64_t prescribed_source_count = static_cast<std::uint64_t>( jobs.size() );
		partition_differential_subspace_jobs( jobs, options.subspace_partition_count, options.subspace_partition_index );
		if ( jobs.empty() )
		{
			std::cout << "[SubspaceEvidence] mode=differential_prescribed_subset_certificate\n";
			std::cout << "  source_file=" << source_file_path << "\n";
			std::cout << "  note=active partition is empty; no sources selected for this shard\n";
			return 0;
		}

		RuntimeEventLog runtime_log {};
		bool runtime_log_enabled = false;
		if ( !options.subspace_runtime_log_path.empty() )
		{
			if ( !TwilightDream::best_search_shared_core::open_append_log_or_emit_error(
					std::cerr,
					runtime_log,
					options.subspace_runtime_log_path,
					"[SubspaceEvidence] ERROR: cannot open runtime log: " ) )
				return 1;
			runtime_log_enabled = true;
		}

		const SystemMemoryInfo mem = query_system_memory_info();
		const std::uint64_t avail_bytes = mem.available_physical_bytes;
		std::uint64_t headroom_bytes = ( options.strategy_target_headroom_bytes != 0 ) ? options.strategy_target_headroom_bytes : compute_memory_headroom_bytes( avail_bytes, options.memory_headroom_mib, options.memory_headroom_mib_was_provided );
		if ( avail_bytes != 0 && headroom_bytes > avail_bytes )
			headroom_bytes = avail_bytes;
		pmr_configure_for_run( avail_bytes, headroom_bytes );
		memory_governor_enable_for_run( headroom_bytes );
		memory_governor_set_poll_fn( &governor_poll_system_memory_once );
		MemoryBallast memory_ballast( headroom_bytes );
		if ( options.memory_ballast_enabled && headroom_bytes != 0 )
			memory_ballast.start();

		DifferentialBestSearchConfiguration run_configuration = make_differential_base_search_configuration( options );
		differential_heuristic_branch_cap( run_configuration ) = 0;
		configure_weight_sliced_pddt_for_run_wrapper( run_configuration, rebuildable_pool().budget_bytes() );

		std::cout << "[SubspaceEvidence] mode=differential_prescribed_subset_certificate\n";
		std::cout << "  rounds=" << options.round_count
				  << "  prescribed_source_count=" << prescribed_source_count
				  << "  active_partition_source_count=" << jobs.size()
				  << "  collect_weight_cap=" << options.collect_weight_cap
				  << "  subspace_partition_count=" << options.subspace_partition_count
				  << "  subspace_partition_index=" << options.subspace_partition_index
				  << "\n";
		std::cout << "  note=this mode does not build hulls; it only certifies per-source lower bounds / exact best weights over the prescribed subset S\n";

		if ( runtime_log_enabled )
		{
			write_differential_batch_runtime_event(
				runtime_log,
				"subspace_evidence_only_start",
				[&]( std::ostream& out ) {
					out << "source_file=" << source_file_path << "\n";
					out << "prescribed_source_count=" << prescribed_source_count << "\n";
					out << "active_partition_source_count=" << jobs.size() << "\n";
					out << "collect_weight_cap=" << options.collect_weight_cap << "\n";
				} );
		}

		DifferentialBatchHullRunSummary synthetic_batch_summary {};
		synthetic_batch_summary.jobs.resize( jobs.size() );
		for ( std::size_t i = 0; i < jobs.size(); ++i )
		{
			synthetic_batch_summary.jobs[ i ].job_index = i;
			synthetic_batch_summary.jobs[ i ].job = jobs[ i ];
		}

		CommandLineOptions evidence_options = options;
		evidence_options.auto_exclusion_evidence = true;
		const GeneratedSubspaceEvidenceSummary generated_evidence =
			compute_differential_generated_evidence_summary(
				jobs,
				synthetic_batch_summary,
				DifferentialBatchHullPipelineResult {},
				run_configuration,
				evidence_options,
				true,
				options.collect_weight_cap );
		if ( !generated_evidence.available )
		{
			memory_governor_disable_for_run();
			clear_shared_injection_caches_with_progress();
			std::cerr << "[SubspaceEvidence] ERROR: failed to certify the prescribed subset under the requested collect cap.\n";
			return 1;
		}

		std::string label = options.subspace_evidence_label;
		if ( label.empty() )
		{
			std::ostringstream oss;
			oss << "partition_" << options.subspace_partition_index << "_of_" << options.subspace_partition_count;
			label = oss.str();
		}
		if ( !options.subspace_evidence_out_path.empty() )
		{
			if ( !write_generated_subspace_evidence_record( options.subspace_evidence_out_path, static_cast<std::uint64_t>( jobs.size() ), generated_evidence.min_best_weight, label ) )
			{
				memory_governor_disable_for_run();
				clear_shared_injection_caches_with_progress();
				return 1;
			}
		}
		if ( !options.subspace_coverage_out_path.empty() )
		{
			if ( !write_differential_subspace_evidence_only_report( options.subspace_coverage_out_path, options, source_file_path, prescribed_source_count, static_cast<std::uint64_t>( jobs.size() ), generated_evidence ) )
			{
				memory_governor_disable_for_run();
				clear_shared_injection_caches_with_progress();
				return 1;
			}
		}

		std::cout << "[SubspaceEvidence][Differential] source_count=" << jobs.size()
				  << " min_best_weight=" << generated_evidence.min_best_weight
				  << " certification=" << generated_evidence_certification_to_string( generated_evidence.certification )
				  << " exact_best_jobs=" << generated_evidence.exact_best_job_count
				  << " lower_bound_only_jobs=" << generated_evidence.lower_bound_only_job_count
				  << " prepass_jobs=" << generated_evidence.prepass_job_count
				  << " prepass_total_nodes=" << generated_evidence.prepass_total_nodes
				  << "\n";
		if ( !options.subspace_evidence_out_path.empty() )
			std::cout << "  evidence_out=" << options.subspace_evidence_out_path << "  label=" << label << "\n";
		if ( !options.subspace_coverage_out_path.empty() )
			std::cout << "  evidence_report_out=" << options.subspace_coverage_out_path << "\n";

		if ( runtime_log_enabled )
		{
			write_differential_batch_runtime_event(
				runtime_log,
				"subspace_evidence_only_stop",
				[&]( std::ostream& out ) {
					out << "active_partition_source_count=" << jobs.size() << "\n";
					out << "generated_evidence_certification=" << generated_evidence_certification_to_string( generated_evidence.certification ) << "\n";
					out << "generated_evidence_min_best_weight=" << generated_evidence.min_best_weight << "\n";
					out << "generated_evidence_prepass_jobs=" << generated_evidence.prepass_job_count << "\n";
					out << "generated_evidence_prepass_total_nodes=" << generated_evidence.prepass_total_nodes << "\n";
				} );
		}

		memory_governor_disable_for_run();
		clear_shared_injection_caches_with_progress();
		return 0;
	}
