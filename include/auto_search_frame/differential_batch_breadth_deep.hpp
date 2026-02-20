#pragma once
#if !defined( TWILIGHTDREAM_DIFFERENTIAL_BATCH_BREADTH_DEEP_HPP )
#define TWILIGHTDREAM_DIFFERENTIAL_BATCH_BREADTH_DEEP_HPP

// Breadth -> deep batch orchestration for differential best-search (shared by test_neoalzette_differential_best_search
// and hull-level drivers via differential_hull_callback_aggregator.hpp).

#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint_state.hpp"
#include "auto_search_frame/detail/hull_batch_checkpoint_shared.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"
#include "common/runtime_component.hpp"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace TwilightDream::hull_callback_aggregator
{
	struct DifferentialBatchJob
	{
		int			  rounds = 1;
		std::uint32_t initial_branch_a_difference = 0;
		std::uint32_t initial_branch_b_difference = 0;
	};

	enum class DifferentialBatchJobLineParseResult
	{
		Ignore,
		Ok,
		Error
	};

	namespace differential_batch_detail
	{
		inline bool parse_signed_integer_32( const char* text, int& value_out )
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

		inline bool parse_unsigned_integer_32( const char* text, std::uint32_t& value_out )
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
	}  // namespace differential_batch_detail

	inline DifferentialBatchJobLineParseResult parse_differential_batch_job_file_line(
		const std::string&	 raw_line,
		int					 line_no,
		int					 default_round_count,
		DifferentialBatchJob& out )
	{
		std::string line = raw_line;
		if ( const std::size_t p = line.find( '#' ); p != std::string::npos )
			line.resize( p );
		for ( char& ch : line )
			if ( ch == ',' )
				ch = ' ';

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
			return DifferentialBatchJobLineParseResult::Ignore;

		std::istringstream iss( line );
		std::string		   token0, token1, token2;
		if ( !( iss >> token0 >> token1 ) )
		{
			std::cerr << "[Batch] ERROR: invalid line " << line_no << " in batch file (need at least 2 tokens): " << raw_line << "\n";
			return DifferentialBatchJobLineParseResult::Error;
		}
		if ( iss >> token2 )
		{
			int			  round_count = 0;
			std::uint32_t da = 0, db = 0;
			if ( !differential_batch_detail::parse_signed_integer_32( token0.c_str(), round_count ) || round_count <= 0 )
			{
				std::cerr << "[Batch] ERROR: invalid rounds on line " << line_no << ": " << raw_line << "\n";
				return DifferentialBatchJobLineParseResult::Error;
			}
			if ( !differential_batch_detail::parse_unsigned_integer_32( token1.c_str(), da ) || !differential_batch_detail::parse_unsigned_integer_32( token2.c_str(), db ) )
			{
				std::cerr << "[Batch] ERROR: invalid initial branch differences on line " << line_no << ": " << raw_line << "\n";
				return DifferentialBatchJobLineParseResult::Error;
			}
			if ( da == 0u && db == 0u )
			{
				std::cerr << "[Batch] ERROR: (initial_branch_a_difference, initial_branch_b_difference)=(0,0) is not allowed (line " << line_no << ")\n";
				return DifferentialBatchJobLineParseResult::Error;
			}
			out = DifferentialBatchJob { round_count, da, db };
			return DifferentialBatchJobLineParseResult::Ok;
		}

		std::uint32_t da = 0, db = 0;
		if ( !differential_batch_detail::parse_unsigned_integer_32( token0.c_str(), da ) || !differential_batch_detail::parse_unsigned_integer_32( token1.c_str(), db ) )
		{
			std::cerr << "[Batch] ERROR: invalid initial branch differences on line " << line_no << ": " << raw_line << "\n";
			return DifferentialBatchJobLineParseResult::Error;
		}
		if ( da == 0u && db == 0u )
		{
			std::cerr << "[Batch] ERROR: (initial_branch_a_difference, initial_branch_b_difference)=(0,0) is not allowed (line " << line_no << ")\n";
			return DifferentialBatchJobLineParseResult::Error;
		}
		out = DifferentialBatchJob { default_round_count, da, db };
		return DifferentialBatchJobLineParseResult::Ok;
	}

	inline int load_differential_batch_jobs_from_file(
		const std::string&				 path,
		std::size_t						 max_jobs,
		int								 default_round_count,
		std::vector<DifferentialBatchJob>& jobs_out )
	{
		std::ifstream f( path );
		if ( !f )
		{
			std::cerr << "[Batch] ERROR: cannot open batch file: " << path << "\n";
			return 1;
		}
		const std::size_t limit = ( max_jobs > 0 ) ? max_jobs : std::numeric_limits<std::size_t>::max();
		jobs_out.reserve( std::min<std::size_t>( limit, 1024 ) );
		std::string line;
		int			line_no = 0;
		while ( std::getline( f, line ) )
		{
			++line_no;
			DifferentialBatchJob job {};
			const auto			 pr = parse_differential_batch_job_file_line( line, line_no, default_round_count, job );
			if ( pr == DifferentialBatchJobLineParseResult::Error )
				return 1;
			if ( pr == DifferentialBatchJobLineParseResult::Ok )
			{
				jobs_out.push_back( job );
				if ( jobs_out.size() >= limit )
					break;
			}
		}
		if ( jobs_out.empty() )
		{
			std::cerr << "[Batch] ERROR: batch file contains no jobs: " << path << "\n";
			return 1;
		}
		return 0;
	}

	inline void generate_differential_batch_jobs_from_seed(
		std::size_t						 count,
		int								 round_count,
		std::uint64_t					 seed,
		std::vector<DifferentialBatchJob>& jobs_out )
	{
		jobs_out.clear();
		jobs_out.reserve( count );
		std::mt19937_64 rng( seed );
		for ( std::size_t job_index = 0; job_index < count; ++job_index )
		{
			std::uint32_t da = 0, db = 0;
			do
			{
				da = static_cast<std::uint32_t>( rng() );
				db = static_cast<std::uint32_t>( rng() );
			} while ( da == 0u && db == 0u );
			jobs_out.push_back( DifferentialBatchJob { round_count, da, db } );
		}
	}

	struct DifferentialBatchBreadthDeepOrchestratorConfig
	{
		TwilightDream::auto_search_differential::DifferentialBestSearchConfiguration		 breadth_configuration {};
		TwilightDream::auto_search_differential::DifferentialBestSearchRuntimeControls	 breadth_runtime {};
		TwilightDream::auto_search_differential::DifferentialBestSearchConfiguration		 deep_configuration {};
		TwilightDream::auto_search_differential::DifferentialBestSearchRuntimeControls	 deep_runtime {};
	};

	struct DifferentialBatchTopCandidate
	{
		bool									 found = false;
		std::size_t								 job_index = 0;
		std::uint32_t							 start_delta_a = 0;
		std::uint32_t							 start_delta_b = 0;
		std::uint32_t							 entry_delta_a = 0;
		std::uint32_t							 entry_delta_b = 0;
		int										 entry_round1_weight = TwilightDream::auto_search_differential::INFINITE_WEIGHT;
		int										 best_weight = TwilightDream::auto_search_differential::INFINITE_WEIGHT;
		std::uint64_t							 nodes = 0;
		bool									 hit_maximum_search_nodes_limit = false;
		std::vector<TwilightDream::auto_search_differential::DifferentialTrailStepRecord> trail {};
	};

	struct DifferentialBatchBreadthDeepOrchestratorResult
	{
		bool		  breadth_had_any_candidate = false;
		std::uint64_t breadth_total_nodes = 0;
		std::uint64_t deep_total_nodes = 0;
		bool		  deep_success = false;
		std::size_t	  best_job_index = 0;
		std::vector<DifferentialBatchTopCandidate> top_candidates {};
		TwilightDream::auto_search_differential::MatsuiSearchRunDifferentialResult global_best {};
	};

	enum class DifferentialBatchSelectionCheckpointStage : std::uint8_t
	{
		Breadth = 1,
		DeepReady = 2
	};

	struct DifferentialBatchSourceSelectionCheckpointState
	{
		DifferentialBatchBreadthDeepOrchestratorConfig config {};
		std::vector<DifferentialBatchJob> jobs {};
		std::vector<std::uint8_t> completed_job_flags {};
		std::uint64_t breadth_total_nodes = 0;
		std::vector<DifferentialBatchTopCandidate> top_candidates {};
		DifferentialBatchSelectionCheckpointStage stage = DifferentialBatchSelectionCheckpointStage::Breadth;
	};

	static inline bool differential_batch_top_candidate_better( const DifferentialBatchTopCandidate& a, const DifferentialBatchTopCandidate& b ) noexcept
	{
		if ( a.best_weight != b.best_weight )
			return a.best_weight < b.best_weight;
		if ( a.entry_round1_weight != b.entry_round1_weight )
			return a.entry_round1_weight < b.entry_round1_weight;
		if ( a.job_index != b.job_index )
			return a.job_index < b.job_index;
		if ( a.start_delta_a != b.start_delta_a )
			return a.start_delta_a < b.start_delta_a;
		if ( a.start_delta_b != b.start_delta_b )
			return a.start_delta_b < b.start_delta_b;
		return a.nodes < b.nodes;
	}

	static inline void differential_sort_batch_top_candidates( std::vector<DifferentialBatchTopCandidate>& top_candidates )
	{
		std::sort(
			top_candidates.begin(),
			top_candidates.end(),
			[]( const DifferentialBatchTopCandidate& lhs, const DifferentialBatchTopCandidate& rhs ) {
				return differential_batch_top_candidate_better( lhs, rhs );
			} );
	}

	static inline void differential_try_update_batch_top_candidates(
		std::vector<DifferentialBatchTopCandidate>& top_candidates,
		std::size_t top_k,
		DifferentialBatchTopCandidate&& candidate )
	{
		if ( !candidate.found || top_k == 0 )
			return;
		if ( top_candidates.size() < top_k )
		{
			top_candidates.push_back( std::move( candidate ) );
			differential_sort_batch_top_candidates( top_candidates );
			return;
		}
		std::size_t worst = 0;
		for ( std::size_t index = 1; index < top_candidates.size(); ++index )
		{
			if ( !differential_batch_top_candidate_better( top_candidates[ index ], top_candidates[ worst ] ) )
				worst = index;
		}
		if ( differential_batch_top_candidate_better( candidate, top_candidates[ worst ] ) )
		{
			top_candidates[ worst ] = std::move( candidate );
			differential_sort_batch_top_candidates( top_candidates );
		}
	}

	static inline void write_differential_batch_top_candidate(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialBatchTopCandidate& candidate )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_bool( w, candidate.found );
		write_size( w, candidate.job_index );
		w.write_u32( candidate.start_delta_a );
		w.write_u32( candidate.start_delta_b );
		w.write_u32( candidate.entry_delta_a );
		w.write_u32( candidate.entry_delta_b );
		w.write_i32( candidate.entry_round1_weight );
		w.write_i32( candidate.best_weight );
		w.write_u64( candidate.nodes );
		write_bool( w, candidate.hit_maximum_search_nodes_limit );
		write_counted_container(
			w,
			candidate.trail,
			[&]( const TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
				TwilightDream::auto_search_differential::write_trail_step( w, step );
			} );
	}

	static inline bool read_differential_batch_top_candidate(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialBatchTopCandidate& candidate )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_bool( r, candidate.found ) &&
			read_size( r, candidate.job_index ) &&
			r.read_u32( candidate.start_delta_a ) &&
			r.read_u32( candidate.start_delta_b ) &&
			r.read_u32( candidate.entry_delta_a ) &&
			r.read_u32( candidate.entry_delta_b ) &&
			r.read_i32( candidate.entry_round1_weight ) &&
			r.read_i32( candidate.best_weight ) &&
			r.read_u64( candidate.nodes ) &&
			read_bool( r, candidate.hit_maximum_search_nodes_limit ) &&
			read_counted_container(
				r,
				candidate.trail,
				[&]( TwilightDream::auto_search_differential::DifferentialTrailStepRecord& step ) {
					return TwilightDream::auto_search_differential::read_trail_step( r, step );
				} );
	}

	static inline DifferentialBatchSourceSelectionCheckpointState make_initial_differential_batch_source_selection_checkpoint_state(
		const std::vector<DifferentialBatchJob>& jobs,
		const DifferentialBatchBreadthDeepOrchestratorConfig& config )
	{
		DifferentialBatchSourceSelectionCheckpointState state {};
		state.config = config;
		state.jobs = jobs;
		state.completed_job_flags.assign( jobs.size(), 0u );
		return state;
	}

	static inline bool write_differential_batch_source_selection_checkpoint(
		const std::string& path,
		const DifferentialBatchSourceSelectionCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[&]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullBatchSelection ) )
					return false;
				TwilightDream::auto_search_differential::write_config( w, state.config.breadth_configuration );
				write_runtime_controls( w, state.config.breadth_runtime );
				TwilightDream::auto_search_differential::write_config( w, state.config.deep_configuration );
				write_runtime_controls( w, state.config.deep_runtime );
				write_enum_u8( w, state.stage );
				write_counted_container(
					w,
					state.jobs,
					[&]( const DifferentialBatchJob& job ) {
						w.write_i32( job.rounds );
						w.write_u32( job.initial_branch_a_difference );
						w.write_u32( job.initial_branch_b_difference );
					} );
				write_u8_vector( w, state.completed_job_flags, [&]( std::uint8_t flag ) { w.write_u8( flag ); } );
				w.write_u64( state.breadth_total_nodes );
				write_counted_container(
					w,
					state.top_candidates,
					[&]( const DifferentialBatchTopCandidate& candidate ) { write_differential_batch_top_candidate( w, candidate ); } );
				return w.ok();
			} );
	}

	static inline bool read_differential_batch_source_selection_checkpoint(
		const std::string& path,
		DifferentialBatchSourceSelectionCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::DifferentialHullBatchSelection )
			return false;
		if ( !TwilightDream::auto_search_differential::read_config( r, state.config.breadth_configuration ) )
			return false;
		if ( !read_runtime_controls( r, state.config.breadth_runtime ) )
			return false;
		if ( !TwilightDream::auto_search_differential::read_config( r, state.config.deep_configuration ) )
			return false;
		if ( !read_runtime_controls( r, state.config.deep_runtime ) )
			return false;
		if ( !read_enum_u8( r, state.stage ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.jobs,
				 [&]( DifferentialBatchJob& job ) {
					 return
						 r.read_i32( job.rounds ) &&
						 r.read_u32( job.initial_branch_a_difference ) &&
						 r.read_u32( job.initial_branch_b_difference );
				 } ) )
			return false;
		if ( !read_u8_vector( r, state.completed_job_flags, [&]( std::uint8_t& flag ) { return r.read_u8( flag ); } ) )
			return false;
		if ( !r.read_u64( state.breadth_total_nodes ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.top_candidates,
				 [&]( DifferentialBatchTopCandidate& candidate ) { return read_differential_batch_top_candidate( r, candidate ); } ) )
			return false;
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		differential_sort_batch_top_candidates( state.top_candidates );
		return true;
	}

	inline void run_differential_batch_breadth_then_deep(
		const std::vector<DifferentialBatchJob>&			 jobs,
		int													 worker_thread_count,
		const DifferentialBatchBreadthDeepOrchestratorConfig& cfg,
		DifferentialBatchBreadthDeepOrchestratorResult&		out )
	{
		using namespace TwilightDream::auto_search_differential;
		using TwilightDream::runtime_component::IosStateGuard;
		using TwilightDream::runtime_component::memory_governor_poll_if_needed;
		using TwilightDream::runtime_component::pmr_report_oom_once;
		using TwilightDream::runtime_component::print_word32_hex;
		using TwilightDream::runtime_component::run_worker_threads;
		using TwilightDream::runtime_component::run_worker_threads_with_monitor;

		out = DifferentialBatchBreadthDeepOrchestratorResult {};

		const int		  breadth_threads = std::max( 1, worker_thread_count );
		const std::size_t deep_top_k = std::min<std::size_t>( jobs.size(), std::size_t( breadth_threads ) );

		using BreadthCandidate = DifferentialBatchTopCandidate;

		auto candidate_key_better = []( const BreadthCandidate& a, const BreadthCandidate& b ) -> bool {
			if ( a.best_weight != b.best_weight )
				return a.best_weight < b.best_weight;
			if ( a.entry_round1_weight != b.entry_round1_weight )
				return a.entry_round1_weight < b.entry_round1_weight;
			if ( a.job_index != b.job_index )
				return a.job_index < b.job_index;
			if ( a.start_delta_a != b.start_delta_a )
				return a.start_delta_a < b.start_delta_a;
			if ( a.start_delta_b != b.start_delta_b )
				return a.start_delta_b < b.start_delta_b;
			return a.nodes < b.nodes;
		};

		std::atomic<std::size_t>   next_index { 0 };
		std::atomic<std::size_t>   completed { 0 };
		std::atomic<std::uint64_t> total_nodes_breadth { 0 };

		std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( breadth_threads ) );
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
				// Evict the pool's worst candidate = last in ascending sort order (not the best slot).
				std::size_t worst = 0;
				for ( std::size_t i = 1; i < top_candidates.size(); ++i )
				{
					if ( !candidate_key_better( top_candidates[ i ], top_candidates[ worst ] ) )
						worst = i;
				}
				if ( candidate_key_better( c, top_candidates[ worst ] ) )
					top_candidates[ worst ] = std::move( c );
				else
					return;
			}
			std::sort( top_candidates.begin(), top_candidates.end(), candidate_key_better );
		};

		const auto start_time_breadth = std::chrono::steady_clock::now();

		auto worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
				if ( job_index >= jobs.size() )
					break;

				active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_index + 1, std::memory_order_relaxed );
				const DifferentialBatchJob job = jobs[ job_index ];

				MatsuiSearchRunDifferentialResult result {};
				try
				{
					result = run_matsui_best_search_with_injection_internal(
						job.rounds,
						job.initial_branch_a_difference,
						job.initial_branch_b_difference,
						cfg.breadth_configuration,
						cfg.breadth_runtime,
						false,
						false );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "batch.breadth" );
					result = MatsuiSearchRunDifferentialResult {};
				}

				total_nodes_breadth.fetch_add( result.nodes_visited, std::memory_order_relaxed );

				BreadthCandidate c {};
				c.job_index = job_index;
				c.start_delta_a = job.initial_branch_a_difference;
				c.start_delta_b = job.initial_branch_b_difference;
				c.best_weight = result.best_weight;
				c.nodes = result.nodes_visited;
				c.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
				c.trail = std::move( result.best_trail );
				c.found = result.found && !c.trail.empty();
				if ( c.found )
				{
					c.entry_delta_a = c.trail.front().input_branch_a_difference;
					c.entry_delta_b = c.trail.front().input_branch_b_difference;
					c.entry_round1_weight = c.trail.front().round_weight;
					try_update_top_candidates( std::move( c ) );
				}

				active_job_id_by_thread[ std::size_t( thread_id ) ].store( 0, std::memory_order_relaxed );
				completed.fetch_add( 1, std::memory_order_relaxed );
			}
		};

		const std::uint64_t breadth_progress_sec = ( cfg.breadth_runtime.progress_every_seconds == 0 ) ? 1 : cfg.breadth_runtime.progress_every_seconds;

		auto progress_monitor = [ & ]() {
			const std::size_t total = jobs.size();
			if ( total == 0 )
				return;

			std::size_t last_done = 0;
			auto		last_time = start_time_breadth;

			for ( ;; )
			{
				const std::size_t done = completed.load( std::memory_order_relaxed );
				const auto		  now_for_governor = std::chrono::steady_clock::now();
				memory_governor_poll_if_needed( now_for_governor );

				const auto	 now = std::chrono::steady_clock::now();
				const double since_last = std::chrono::duration<double>( now - last_time ).count();
				if ( since_last >= double( breadth_progress_sec ) || done >= total )
				{
					const double	  elapsed = std::chrono::duration<double>( now - start_time_breadth ).count();
					const double	  window = std::max( 1e-9, std::chrono::duration<double>( now - last_time ).count() );
					const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
					const double	  rate = double( delta ) / window;

					BreadthCandidate best_snapshot {};
					best_snapshot.best_weight = INFINITE_WEIGHT;
					{
						std::scoped_lock lk( top_mutex );
						if ( !top_candidates.empty() )
							best_snapshot = top_candidates.front();
					}

					struct ActiveBatchJob
					{
						std::size_t	 thread_id = 0;
						std::size_t	 job_id_one_based = 0;
						std::uint32_t delta_a = 0;
						std::uint32_t delta_b = 0;
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
						const DifferentialBatchJob& j = jobs[ job_index ];
						active.push_back( ActiveBatchJob { i, id, j.initial_branch_a_difference, j.initial_branch_b_difference } );
					}

					IosStateGuard g( std::cout );
					std::cout << "[Batch][Breadth] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
							  << " jobs_per_second=" << std::setprecision( 2 ) << rate << " elapsed_seconds=" << std::setprecision( 2 ) << elapsed << " total_nodes=" << total_nodes_breadth.load( std::memory_order_relaxed );
					if ( best_snapshot.best_weight < INFINITE_WEIGHT )
						std::cout << " best_weight=" << best_snapshot.best_weight << " best_job=#" << ( best_snapshot.job_index + 1 );
					if ( !active.empty() )
					{
						std::cout << " active={";
						const std::size_t show = std::min<std::size_t>( active.size(), 16 );
						for ( std::size_t i = 0; i < show; ++i )
						{
							if ( i )
								std::cout << ",";
							std::cout << "[Job#" << active[ i ].job_id_one_based << "@" << active[ i ].thread_id << "] ";
							print_word32_hex( "delta_a=", active[ i ].delta_a );
							std::cout << " ";
							print_word32_hex( "delta_b=", active[ i ].delta_b );
						}
						if ( active.size() > show )
							std::cout << ",...";
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

		run_worker_threads_with_monitor( breadth_threads, [ & ]( int t ) { worker( t ); }, progress_monitor );

		const auto	 end_time_breadth = std::chrono::steady_clock::now();
		const double elapsed_breadth = std::chrono::duration<double>( end_time_breadth - start_time_breadth ).count();

		{
			std::scoped_lock lk( top_mutex );
			std::sort( top_candidates.begin(), top_candidates.end(), candidate_key_better );
		}

		out.breadth_total_nodes = total_nodes_breadth.load( std::memory_order_relaxed );
		out.top_candidates = top_candidates;
		{
			IosStateGuard g( std::cout );
			std::cout << "\n[Batch][Breadth] done. elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed_breadth << " total_nodes_visited=" << out.breadth_total_nodes << " top_k_found=" << top_candidates.size() << "\n";
		}

		if ( top_candidates.empty() )
		{
			std::cout << "[Batch] FAIL: breadth found no trail in any job (within breadth limits).\n";
			return;
		}
		out.breadth_had_any_candidate = true;

		std::cout << "[Batch][Breadth] TOP-" << top_candidates.size() << " candidates:\n";
		for ( std::size_t i = 0; i < top_candidates.size(); ++i )
		{
			const auto&	  current_candidate = top_candidates[ i ];
			const auto&	  current_job = jobs[ current_candidate.job_index ];
			IosStateGuard g( std::cout );
			std::cout << "  #" << ( i + 1 ) << "  job=#" << ( current_candidate.job_index + 1 ) << "  rounds=" << current_job.rounds << "  best_weight=" << current_candidate.best_weight << "  nodes=" << current_candidate.nodes << ( current_candidate.hit_maximum_search_nodes_limit ? " [HIT maximum_search_nodes limit]" : "" ) << "\n";
		}
		std::cout << "\n";

		const std::uint64_t deep_progress_sec = ( cfg.deep_runtime.progress_every_seconds == 0 ) ? 1 : cfg.deep_runtime.progress_every_seconds;
		std::cout << "[Batch][Deep] start: inputs=" << top_candidates.size() << " deep_threads=" << top_candidates.size() << " progress_every_seconds=" << deep_progress_sec << "\n";
		std::cout << "  deep_runtime_maximum_search_nodes=" << cfg.deep_runtime.maximum_search_nodes << " deep_runtime_maximum_search_seconds=" << cfg.deep_runtime.maximum_search_seconds << " deep_target_best_weight=" << cfg.deep_configuration.target_best_weight << "\n\n";

		struct DeepResult
		{
			std::size_t							job_index = 0;
			MatsuiSearchRunDifferentialResult result {};
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

		const auto start_time_deep = std::chrono::steady_clock::now();

		auto deep_worker = [ & ]( int thread_id ) {
			const BreadthCandidate&	current_candidate = top_candidates[ static_cast<std::size_t>( thread_id ) ];
			const DifferentialBatchJob	current_job = jobs[ current_candidate.job_index ];
			const std::size_t			job_id = current_candidate.job_index + 1;

			const std::string prefix = "[Batch][Deep][Job#" + std::to_string( job_id ) + "@" + std::to_string( thread_id ) + "] ";
			TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );

			BestWeightHistory checkpoint {};
			const std::string checkpoint_path_base = BestWeightHistory::default_path( current_job.rounds, current_candidate.start_delta_a, current_candidate.start_delta_b );
			const std::string checkpoint_path = add_job_suffix_to_checkpoint_path( checkpoint_path_base, job_id );
			const bool checkpoint_ok = checkpoint.open_append( checkpoint_path );
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				if ( checkpoint_ok )
				{
					std::cout << "[DeepSearch] checkpoint_log_path=" << checkpoint_path << "\n";
					std::cout << "[DeepSearch] checkpoint_log_write_mode=append_on_best_weight_changes\n";
				}
				else
					std::cout << "[DeepSearch] WARNING: cannot open checkpoint log file for writing: " << checkpoint_path << "\n";
			}

			const int												 seed_weight = current_candidate.best_weight;
			const std::vector<DifferentialTrailStepRecord>* seed_trail = current_candidate.trail.empty() ? nullptr : &current_candidate.trail;

			MatsuiSearchRunDifferentialResult result {};
			try
			{
				result = run_matsui_best_search_with_injection_internal(
					current_job.rounds,
					current_candidate.start_delta_a,
					current_candidate.start_delta_b,
					cfg.deep_configuration,
					cfg.deep_runtime,
					true,
					true,
					seed_weight,
					seed_trail,
					checkpoint_ok ? &checkpoint : nullptr );
			}
			catch ( const std::bad_alloc& )
			{
				pmr_report_oom_once( "batch.deep" );
				result = MatsuiSearchRunDifferentialResult {};
			}

			deep_results[ static_cast<std::size_t>( thread_id ) ].job_index = current_candidate.job_index;
			deep_results[ static_cast<std::size_t>( thread_id ) ].result = std::move( result );
		};

		run_worker_threads( int( top_candidates.size() ), deep_worker );

		const auto	 end_time_deep = std::chrono::steady_clock::now();
		const double elapsed_deep = std::chrono::duration<double>( end_time_deep - start_time_deep ).count();

		std::uint64_t				 total_nodes_deep = 0;
		bool						 global_best_found = false;
		std::size_t					 global_best_job_index = 0;
		MatsuiSearchRunDifferentialResult global_best_result {};

		for ( const auto& dr : deep_results )
		{
			total_nodes_deep += dr.result.nodes_visited;
			if ( dr.result.found && !dr.result.best_trail.empty() && ( !global_best_found || dr.result.best_weight < global_best_result.best_weight ) )
			{
				global_best_found = true;
				global_best_job_index = dr.job_index;
				global_best_result = dr.result;
			}
		}

		out.deep_total_nodes = total_nodes_deep;
		for ( std::size_t candidate_index = 0; candidate_index < deep_results.size(); ++candidate_index )
		{
			const auto& deep_result = deep_results[ candidate_index ].result;
			if ( !deep_result.found || deep_result.best_trail.empty() )
				continue;

			auto& candidate = top_candidates[ candidate_index ];
			candidate.found = true;
			candidate.best_weight = deep_result.best_weight;
			candidate.nodes = deep_result.nodes_visited;
			candidate.hit_maximum_search_nodes_limit = deep_result.hit_maximum_search_nodes;
			candidate.trail = deep_result.best_trail;
			candidate.entry_round1_weight = deep_result.best_trail.front().round_weight;
			candidate.entry_delta_a = deep_result.best_trail.front().input_branch_a_difference;
			candidate.entry_delta_b = deep_result.best_trail.front().input_branch_b_difference;
		}
		std::sort( top_candidates.begin(), top_candidates.end(), candidate_key_better );
		out.top_candidates = top_candidates;

		{
			IosStateGuard g( std::cout );
			std::cout << "\n[Batch][Deep] done. elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed_deep << " total_nodes_visited=" << total_nodes_deep << "\n";
		}

		if ( !global_best_found || !global_best_result.found || global_best_result.best_weight >= INFINITE_WEIGHT || global_best_result.best_trail.empty() )
		{
			std::cout << "[Batch][Deep] no trail found in TOP-K deep runs; retaining breadth TOP-K seeds for downstream stages.\n";
			return;
		}

		out.deep_success = true;
		out.best_job_index = global_best_job_index;
		out.global_best = std::move( global_best_result );

		const DifferentialBatchJob& best_job = jobs[ global_best_job_index ];
		{
			IosStateGuard g( std::cout );
			const double  approx_dp = std::pow( 2.0, -double( out.global_best.best_weight ) );
			std::cout << "[Batch] BEST (from deep TOP-K): weight=" << out.global_best.best_weight << "  approx_DP=" << std::scientific << std::setprecision( 10 ) << approx_dp << std::defaultfloat << "\n";
		}
		std::cout << "  best_job=#" << ( global_best_job_index + 1 ) << "  best_rounds=" << best_job.rounds << "\n";
		print_word32_hex( "  best_initial_branch_a_difference=", best_job.initial_branch_a_difference );
		std::cout << "\n";
		print_word32_hex( "  best_initial_branch_b_difference=", best_job.initial_branch_b_difference );
		std::cout << "\n\n";
	}

	inline void run_differential_batch_breadth_then_deep_from_checkpoint_state(
		DifferentialBatchSourceSelectionCheckpointState& state,
		int worker_thread_count,
		const std::string& checkpoint_path,
		const std::function<void( const DifferentialBatchSourceSelectionCheckpointState&, const char* )>& on_checkpoint_written,
		DifferentialBatchBreadthDeepOrchestratorResult& out )
	{
		using namespace TwilightDream::auto_search_differential;
		using TwilightDream::runtime_component::IosStateGuard;
		using TwilightDream::runtime_component::memory_governor_poll_if_needed;
		using TwilightDream::runtime_component::pmr_report_oom_once;
		using TwilightDream::runtime_component::print_word32_hex;
		using TwilightDream::runtime_component::run_worker_threads;
		using TwilightDream::runtime_component::run_worker_threads_with_monitor;

		out = DifferentialBatchBreadthDeepOrchestratorResult {};

		const int breadth_threads = std::max( 1, worker_thread_count );
		const std::size_t deep_top_k = std::min<std::size_t>( state.jobs.size(), std::size_t( breadth_threads ) );

		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		differential_sort_batch_top_candidates( state.top_candidates );

		if ( state.stage == DifferentialBatchSelectionCheckpointStage::Breadth )
		{
			std::atomic<std::size_t> next_index { 0 };
			std::atomic<std::size_t> completed {
				static_cast<std::size_t>( std::count_if(
					state.completed_job_flags.begin(),
					state.completed_job_flags.end(),
					[]( std::uint8_t flag ) { return flag != 0u; } ) ) };
			std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( breadth_threads ) );
			for ( auto& x : active_job_id_by_thread )
				x.store( 0, std::memory_order_relaxed );

			std::mutex state_mutex;
			const auto start_time_breadth = std::chrono::steady_clock::now();

			auto breadth_worker = [ & ]( int thread_id ) {
				for ( ;; )
				{
					std::size_t job_index = state.jobs.size();
					for ( ;; )
					{
						job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
						if ( job_index >= state.jobs.size() )
							break;
						if ( state.completed_job_flags[ job_index ] == 0u )
							break;
					}
					if ( job_index >= state.jobs.size() )
						break;

					active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_index + 1, std::memory_order_relaxed );
					const DifferentialBatchJob job = state.jobs[ job_index ];

					MatsuiSearchRunDifferentialResult result {};
					try
					{
						result = run_matsui_best_search_with_injection_internal(
							job.rounds,
							job.initial_branch_a_difference,
							job.initial_branch_b_difference,
							state.config.breadth_configuration,
							state.config.breadth_runtime,
							false,
							false );
					}
					catch ( const std::bad_alloc& )
					{
						pmr_report_oom_once( "batch.breadth.resume" );
						result = MatsuiSearchRunDifferentialResult {};
					}

					DifferentialBatchTopCandidate candidate {};
					candidate.job_index = job_index;
					candidate.start_delta_a = job.initial_branch_a_difference;
					candidate.start_delta_b = job.initial_branch_b_difference;
					candidate.best_weight = result.best_weight;
					candidate.nodes = result.nodes_visited;
					candidate.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
					candidate.trail = std::move( result.best_trail );
					candidate.found = result.found && !candidate.trail.empty();
					if ( candidate.found )
					{
						candidate.entry_delta_a = candidate.trail.front().input_branch_a_difference;
						candidate.entry_delta_b = candidate.trail.front().input_branch_b_difference;
						candidate.entry_round1_weight = candidate.trail.front().round_weight;
					}

					{
						std::scoped_lock lk( state_mutex );
						state.breadth_total_nodes += result.nodes_visited;
						state.completed_job_flags[ job_index ] = 1u;
						if ( candidate.found )
							differential_try_update_batch_top_candidates( state.top_candidates, deep_top_k, std::move( candidate ) );
						if ( !checkpoint_path.empty() )
						{
							if ( write_differential_batch_source_selection_checkpoint( checkpoint_path, state ) && on_checkpoint_written )
								on_checkpoint_written( state, "breadth_job_completed" );
						}
					}

					active_job_id_by_thread[ std::size_t( thread_id ) ].store( 0, std::memory_order_relaxed );
					completed.fetch_add( 1, std::memory_order_relaxed );
				}
			};

			const std::uint64_t breadth_progress_sec =
				( state.config.breadth_runtime.progress_every_seconds == 0 ) ?
					1 :
					state.config.breadth_runtime.progress_every_seconds;

			auto progress_monitor = [ & ]() {
				const std::size_t total = state.jobs.size();
				if ( total == 0 )
					return;

				std::size_t last_done = completed.load( std::memory_order_relaxed );
				auto		last_time = start_time_breadth;

				for ( ;; )
				{
					const std::size_t done = completed.load( std::memory_order_relaxed );
					const auto now_for_governor = std::chrono::steady_clock::now();
					memory_governor_poll_if_needed( now_for_governor );

					const auto now = std::chrono::steady_clock::now();
					const double since_last = std::chrono::duration<double>( now - last_time ).count();
					if ( since_last >= double( breadth_progress_sec ) || done >= total )
					{
						const double elapsed = std::chrono::duration<double>( now - start_time_breadth ).count();
						const double window = std::max( 1e-9, std::chrono::duration<double>( now - last_time ).count() );
						const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
						const double rate = double( delta ) / window;

						DifferentialBatchTopCandidate best_snapshot {};
						bool has_best = false;
						std::uint64_t total_nodes_snapshot = 0;
						{
							std::scoped_lock lk( state_mutex );
							total_nodes_snapshot = state.breadth_total_nodes;
							if ( !state.top_candidates.empty() )
							{
								best_snapshot = state.top_candidates.front();
								has_best = true;
							}
						}

						IosStateGuard g( std::cout );
						std::cout << "[Batch][Breadth][Resume] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
								  << " jobs_per_second=" << std::setprecision( 2 ) << rate << " elapsed_seconds=" << std::setprecision( 2 ) << elapsed << " total_nodes=" << total_nodes_snapshot;
						if ( has_best && best_snapshot.best_weight < INFINITE_WEIGHT )
							std::cout << " best_weight=" << best_snapshot.best_weight << " best_job=#" << ( best_snapshot.job_index + 1 );
						std::cout << "\n";

						last_done = done;
						last_time = now;
					}
					if ( done >= total )
						break;
					std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
				}
			};

			run_worker_threads_with_monitor( breadth_threads, [ & ]( int t ) { breadth_worker( t ); }, progress_monitor );
			differential_sort_batch_top_candidates( state.top_candidates );
			state.stage = DifferentialBatchSelectionCheckpointStage::DeepReady;
			if ( !checkpoint_path.empty() )
			{
				if ( write_differential_batch_source_selection_checkpoint( checkpoint_path, state ) && on_checkpoint_written )
					on_checkpoint_written( state, "selection_deep_ready" );
			}
		}

		out.breadth_total_nodes = state.breadth_total_nodes;
		out.top_candidates = state.top_candidates;
		out.breadth_had_any_candidate = !state.top_candidates.empty();

		{
			IosStateGuard g( std::cout );
			std::cout << "\n[Batch][Breadth] done. total_nodes_visited=" << out.breadth_total_nodes << " top_k_found=" << out.top_candidates.size() << "\n";
		}
		if ( state.top_candidates.empty() )
		{
			std::cout << "[Batch] FAIL: breadth found no trail in any job (within breadth limits).\n";
			return;
		}

		std::cout << "[Batch][Breadth] TOP-" << state.top_candidates.size() << " candidates:\n";
		for ( std::size_t index = 0; index < state.top_candidates.size(); ++index )
		{
			const auto& candidate = state.top_candidates[ index ];
			const auto& job = state.jobs[ candidate.job_index ];
			IosStateGuard g( std::cout );
			std::cout << "  #" << ( index + 1 ) << "  job=#" << ( candidate.job_index + 1 ) << "  rounds=" << job.rounds << "  best_weight=" << candidate.best_weight << "  nodes=" << candidate.nodes << ( candidate.hit_maximum_search_nodes_limit ? " [HIT maximum_search_nodes limit]" : "" ) << "\n";
		}
		std::cout << "\n";

		const std::uint64_t deep_progress_sec =
			( state.config.deep_runtime.progress_every_seconds == 0 ) ?
				1 :
				state.config.deep_runtime.progress_every_seconds;
		std::cout << "[Batch][Deep] start: inputs=" << state.top_candidates.size() << " deep_threads=" << state.top_candidates.size() << " progress_every_seconds=" << deep_progress_sec << "\n";
		std::cout << "  deep_runtime_maximum_search_nodes=" << state.config.deep_runtime.maximum_search_nodes << " deep_runtime_maximum_search_seconds=" << state.config.deep_runtime.maximum_search_seconds << " deep_target_best_weight=" << state.config.deep_configuration.target_best_weight << "\n\n";

		struct DeepResult
		{
			std::size_t job_index = 0;
			MatsuiSearchRunDifferentialResult result {};
		};
		std::vector<DeepResult> deep_results( state.top_candidates.size() );

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
			const DifferentialBatchTopCandidate& current_candidate = state.top_candidates[ static_cast<std::size_t>( thread_id ) ];
			const DifferentialBatchJob current_job = state.jobs[ current_candidate.job_index ];
			const std::size_t job_id = current_candidate.job_index + 1;

			const std::string prefix = "[Batch][Deep][Job#" + std::to_string( job_id ) + "@" + std::to_string( thread_id ) + "] ";
			TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );

			BestWeightHistory checkpoint {};
			const std::string checkpoint_path_base = BestWeightHistory::default_path( current_job.rounds, current_candidate.start_delta_a, current_candidate.start_delta_b );
			const std::string checkpoint_log_path = add_job_suffix_to_checkpoint_path( checkpoint_path_base, job_id );
			const bool checkpoint_ok = checkpoint.open_append( checkpoint_log_path );
			{
				std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
				TwilightDream::runtime_component::print_progress_prefix( std::cout );
				if ( checkpoint_ok )
				{
					std::cout << "[DeepSearch] checkpoint_log_path=" << checkpoint_log_path << "\n";
					std::cout << "[DeepSearch] checkpoint_log_write_mode=append_on_best_weight_changes\n";
				}
				else
					std::cout << "[DeepSearch] WARNING: cannot open checkpoint log file for writing: " << checkpoint_log_path << "\n";
			}

			const int seed_weight = current_candidate.best_weight;
			const std::vector<DifferentialTrailStepRecord>* seed_trail = current_candidate.trail.empty() ? nullptr : &current_candidate.trail;

			MatsuiSearchRunDifferentialResult result {};
			try
			{
				result = run_matsui_best_search_with_injection_internal(
					current_job.rounds,
					current_candidate.start_delta_a,
					current_candidate.start_delta_b,
					state.config.deep_configuration,
					state.config.deep_runtime,
					true,
					true,
					seed_weight,
					seed_trail,
					checkpoint_ok ? &checkpoint : nullptr );
			}
			catch ( const std::bad_alloc& )
			{
				pmr_report_oom_once( "batch.deep.resume" );
				result = MatsuiSearchRunDifferentialResult {};
			}

			deep_results[ static_cast<std::size_t>( thread_id ) ] = DeepResult { current_candidate.job_index, std::move( result ) };
		};

		run_worker_threads( int( state.top_candidates.size() ), deep_worker );

		std::uint64_t total_nodes_deep = 0;
		bool global_best_found = false;
		std::size_t global_best_job_index = 0;
		MatsuiSearchRunDifferentialResult global_best_result {};

		for ( const auto& dr : deep_results )
		{
			total_nodes_deep += dr.result.nodes_visited;
			if ( dr.result.found && !dr.result.best_trail.empty() && ( !global_best_found || dr.result.best_weight < global_best_result.best_weight ) )
			{
				global_best_found = true;
				global_best_job_index = dr.job_index;
				global_best_result = dr.result;
			}
		}

		out.deep_total_nodes = total_nodes_deep;
		for ( std::size_t candidate_index = 0; candidate_index < deep_results.size(); ++candidate_index )
		{
			const auto& deep_result = deep_results[ candidate_index ].result;
			if ( !deep_result.found || deep_result.best_trail.empty() )
				continue;

			auto& candidate = out.top_candidates[ candidate_index ];
			candidate.found = true;
			candidate.best_weight = deep_result.best_weight;
			candidate.nodes = deep_result.nodes_visited;
			candidate.hit_maximum_search_nodes_limit = deep_result.hit_maximum_search_nodes;
			candidate.trail = deep_result.best_trail;
			candidate.entry_round1_weight = deep_result.best_trail.front().round_weight;
			candidate.entry_delta_a = deep_result.best_trail.front().input_branch_a_difference;
			candidate.entry_delta_b = deep_result.best_trail.front().input_branch_b_difference;
		}
		differential_sort_batch_top_candidates( out.top_candidates );

		{
			IosStateGuard g( std::cout );
			std::cout << "\n[Batch][Deep] done. total_nodes_visited=" << total_nodes_deep << "\n";
		}

		if ( !global_best_found || !global_best_result.found || global_best_result.best_weight >= INFINITE_WEIGHT || global_best_result.best_trail.empty() )
		{
			std::cout << "[Batch][Deep] no trail found in TOP-K deep runs; retaining breadth TOP-K seeds for downstream stages.\n";
			return;
		}

		out.deep_success = true;
		out.best_job_index = global_best_job_index;
		out.global_best = std::move( global_best_result );

		const DifferentialBatchJob& best_job = state.jobs[ global_best_job_index ];
		{
			IosStateGuard g( std::cout );
			const double approx_dp = std::pow( 2.0, -double( out.global_best.best_weight ) );
			std::cout << "[Batch] BEST (from deep TOP-K): weight=" << out.global_best.best_weight << "  approx_DP=" << std::scientific << std::setprecision( 10 ) << approx_dp << std::defaultfloat << "\n";
		}
		std::cout << "  best_job=#" << ( global_best_job_index + 1 ) << "  best_rounds=" << best_job.rounds << "\n";
		print_word32_hex( "  best_initial_branch_a_difference=", best_job.initial_branch_a_difference );
		std::cout << "\n";
		print_word32_hex( "  best_initial_branch_b_difference=", best_job.initial_branch_b_difference );
		std::cout << "\n\n";
	}

}  // namespace TwilightDream::hull_callback_aggregator

#endif
