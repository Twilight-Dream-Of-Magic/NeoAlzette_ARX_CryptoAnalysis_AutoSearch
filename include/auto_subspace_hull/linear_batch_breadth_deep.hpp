#pragma once
#if !defined( TWILIGHTDREAM_LINEAR_BATCH_BREADTH_DEEP_HPP )
#define TWILIGHTDREAM_LINEAR_BATCH_BREADTH_DEEP_HPP

// Breadth -> deep batch orchestration for linear best-search (shared by test_neoalzette_linear_best_search
// and available to hull-level drivers via linear_hull_callback_aggregator.hpp).

#include "auto_subspace_hull/detail/hull_batch_checkpoint_shared.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint_state.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "common/runtime_component.hpp"

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
	struct LinearBatchJob
	{
		int			  rounds = 1;
		std::uint32_t output_branch_a_mask = 0;
		std::uint32_t output_branch_b_mask = 0;
	};

	enum class LinearBatchJobLineParseResult
	{
		Ignore,
		Ok,
		Error
	};

	namespace linear_batch_detail
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
	}  // namespace linear_batch_detail

	inline LinearBatchJobLineParseResult parse_linear_batch_job_file_line(
		const std::string& raw_line,
		int				   line_no,
		int				   default_round_count,
		LinearBatchJob&	   out )
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
			return LinearBatchJobLineParseResult::Ignore;

		std::istringstream iss( line );
		std::string		   token0, token1, token2;
		if ( !( iss >> token0 >> token1 ) )
		{
			std::cerr << "[Batch] ERROR: invalid line " << line_no << " in batch file (need at least 2 tokens): " << raw_line << "\n";
			return LinearBatchJobLineParseResult::Error;
		}
		if ( iss >> token2 )
		{
			int			  round_count = 0;
			std::uint32_t mask_a = 0, mask_b = 0;
			if ( !linear_batch_detail::parse_signed_integer_32( token0.c_str(), round_count ) || round_count <= 0 )
			{
				std::cerr << "[Batch] ERROR: invalid rounds on line " << line_no << ": " << raw_line << "\n";
				return LinearBatchJobLineParseResult::Error;
			}
			if ( !linear_batch_detail::parse_unsigned_integer_32( token1.c_str(), mask_a ) || !linear_batch_detail::parse_unsigned_integer_32( token2.c_str(), mask_b ) )
			{
				std::cerr << "[Batch] ERROR: invalid output masks on line " << line_no << ": " << raw_line << "\n";
				return LinearBatchJobLineParseResult::Error;
			}
			if ( mask_a == 0u && mask_b == 0u )
			{
				std::cerr << "[Batch] ERROR: (mask_a, mask_b)=(0,0) is not allowed (line " << line_no << ")\n";
				return LinearBatchJobLineParseResult::Error;
			}
			out = LinearBatchJob { round_count, mask_a, mask_b };
			return LinearBatchJobLineParseResult::Ok;
		}

		std::uint32_t mask_a = 0, mask_b = 0;
		if ( !linear_batch_detail::parse_unsigned_integer_32( token0.c_str(), mask_a ) || !linear_batch_detail::parse_unsigned_integer_32( token1.c_str(), mask_b ) )
		{
			std::cerr << "[Batch] ERROR: invalid output masks on line " << line_no << ": " << raw_line << "\n";
			return LinearBatchJobLineParseResult::Error;
		}
		if ( mask_a == 0u && mask_b == 0u )
		{
			std::cerr << "[Batch] ERROR: (mask_a, mask_b)=(0,0) is not allowed (line " << line_no << ")\n";
			return LinearBatchJobLineParseResult::Error;
		}
		out = LinearBatchJob { default_round_count, mask_a, mask_b };
		return LinearBatchJobLineParseResult::Ok;
	}

	// Returns 0 on success, 1 on error (messages to stderr).
	inline int load_linear_batch_jobs_from_file(
		const std::string&		 path,
		std::size_t				 max_jobs,
		int						 default_round_count,
		std::vector<LinearBatchJob>& jobs_out )
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
			LinearBatchJob job {};
			const auto	   pr = parse_linear_batch_job_file_line( line, line_no, default_round_count, job );
			if ( pr == LinearBatchJobLineParseResult::Error )
				return 1;
			if ( pr == LinearBatchJobLineParseResult::Ok )
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

	inline void generate_linear_batch_jobs_from_seed(
		std::size_t				 count,
		int						 round_count,
		std::uint64_t			 seed,
		std::vector<LinearBatchJob>& jobs_out )
	{
		jobs_out.clear();
		jobs_out.reserve( count );
		std::mt19937_64 rng( seed );
		for ( std::size_t job_index = 0; job_index < count; ++job_index )
		{
			std::uint32_t a = 0, b = 0;
			do
			{
				a = static_cast<std::uint32_t>( rng() );
				b = static_cast<std::uint32_t>( rng() );
			} while ( a == 0u && b == 0u );
			jobs_out.push_back( LinearBatchJob { round_count, a, b } );
		}
	}

	struct LinearBatchBreadthDeepOrchestratorConfig
	{
		TwilightDream::auto_search_linear::LinearBestSearchConfiguration		breadth_configuration {};
		TwilightDream::auto_search_linear::LinearBestSearchRuntimeControls	breadth_runtime {};
		TwilightDream::auto_search_linear::LinearBestSearchConfiguration		deep_configuration {};
		TwilightDream::auto_search_linear::LinearBestSearchRuntimeControls	deep_runtime {};
	};

	struct LinearBatchTopCandidate
	{
		std::size_t						   job_index = 0;
		std::uint32_t					   start_mask_a = 0;
		std::uint32_t					   start_mask_b = 0;
		bool							   found = false;
		TwilightDream::auto_search_linear::SearchWeight best_weight = TwilightDream::auto_search_linear::INFINITE_WEIGHT;
		std::uint64_t					   nodes = 0;
		bool							   hit_maximum_search_nodes_limit = false;
		std::uint32_t					   best_input_mask_a = 0;
		std::uint32_t					   best_input_mask_b = 0;
		std::vector<TwilightDream::auto_search_linear::LinearTrailStepRecord> trail {};
	};

	struct LinearBatchBreadthDeepOrchestratorResult
	{
		bool		  breadth_had_any_candidate = false;
		std::uint64_t breadth_total_nodes = 0;
		std::uint64_t deep_total_nodes = 0;
		bool		  deep_success = false;
		std::size_t	  best_job_index = 0;
		std::vector<LinearBatchTopCandidate> top_candidates {};
		TwilightDream::auto_search_linear::MatsuiSearchRunLinearResult global_best {};
	};

	enum class LinearBatchSelectionCheckpointStage : std::uint8_t
	{
		Breadth = 1,
		DeepReady = 2
	};

	struct LinearBatchSourceSelectionCheckpointState
	{
		LinearBatchBreadthDeepOrchestratorConfig config {};
		std::vector<LinearBatchJob> jobs {};
		std::vector<std::uint8_t> completed_job_flags {};
		std::uint64_t breadth_total_nodes = 0;
		std::vector<LinearBatchTopCandidate> top_candidates {};
		LinearBatchSelectionCheckpointStage stage = LinearBatchSelectionCheckpointStage::Breadth;
	};

	static inline bool linear_batch_top_candidate_better( const LinearBatchTopCandidate& a, const LinearBatchTopCandidate& b ) noexcept
	{
		if ( a.best_weight != b.best_weight )
			return a.best_weight < b.best_weight;
		if ( a.job_index != b.job_index )
			return a.job_index < b.job_index;
		if ( a.start_mask_a != b.start_mask_a )
			return a.start_mask_a < b.start_mask_a;
		if ( a.start_mask_b != b.start_mask_b )
			return a.start_mask_b < b.start_mask_b;
		return a.nodes < b.nodes;
	}

	static inline void linear_sort_batch_top_candidates( std::vector<LinearBatchTopCandidate>& top_candidates )
	{
		std::sort(
			top_candidates.begin(),
			top_candidates.end(),
			[]( const LinearBatchTopCandidate& lhs, const LinearBatchTopCandidate& rhs ) {
				return linear_batch_top_candidate_better( lhs, rhs );
			} );
	}

	static inline void linear_try_update_batch_top_candidates(
		std::vector<LinearBatchTopCandidate>& top_candidates,
		std::size_t top_k,
		LinearBatchTopCandidate&& candidate )
	{
		if ( !candidate.found || top_k == 0 )
			return;
		if ( top_candidates.size() < top_k )
		{
			top_candidates.push_back( std::move( candidate ) );
			linear_sort_batch_top_candidates( top_candidates );
			return;
		}
		std::size_t worst = 0;
		for ( std::size_t index = 1; index < top_candidates.size(); ++index )
		{
			if ( !linear_batch_top_candidate_better( top_candidates[ index ], top_candidates[ worst ] ) )
				worst = index;
		}
		if ( linear_batch_top_candidate_better( candidate, top_candidates[ worst ] ) )
		{
			top_candidates[ worst ] = std::move( candidate );
			linear_sort_batch_top_candidates( top_candidates );
		}
	}

	static inline std::size_t linear_resolve_batch_top_candidate_limit(
		std::size_t total_job_count,
		int worker_thread_count,
		std::size_t requested_top_k ) noexcept
	{
		if ( requested_top_k != 0 )
			return std::min<std::size_t>( total_job_count, requested_top_k );
		return std::min<std::size_t>( total_job_count, std::size_t( std::max( 1, worker_thread_count ) ) );
	}

	static inline void write_linear_batch_top_candidate(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const LinearBatchTopCandidate& candidate )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		write_size( w, candidate.job_index );
		w.write_u32( candidate.start_mask_a );
		w.write_u32( candidate.start_mask_b );
		write_bool( w, candidate.found );
		w.write_u64( candidate.best_weight );
		w.write_u64( candidate.nodes );
		write_bool( w, candidate.hit_maximum_search_nodes_limit );
		w.write_u32( candidate.best_input_mask_a );
		w.write_u32( candidate.best_input_mask_b );
		write_counted_container(
			w,
			candidate.trail,
			[&]( const TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
				TwilightDream::auto_search_linear::write_trail_step( w, step );
			} );
	}

	static inline bool read_linear_batch_top_candidate(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearBatchTopCandidate& candidate )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return
			read_size( r, candidate.job_index ) &&
			r.read_u32( candidate.start_mask_a ) &&
			r.read_u32( candidate.start_mask_b ) &&
			read_bool( r, candidate.found ) &&
			r.read_u64( candidate.best_weight ) &&
			r.read_u64( candidate.nodes ) &&
			read_bool( r, candidate.hit_maximum_search_nodes_limit ) &&
			r.read_u32( candidate.best_input_mask_a ) &&
			r.read_u32( candidate.best_input_mask_b ) &&
			read_counted_container(
				r,
				candidate.trail,
				[&]( TwilightDream::auto_search_linear::LinearTrailStepRecord& step ) {
					return TwilightDream::auto_search_linear::read_trail_step( r, step );
				} );
	}

	static inline LinearBatchSourceSelectionCheckpointState make_initial_linear_batch_source_selection_checkpoint_state(
		const std::vector<LinearBatchJob>& jobs,
		const LinearBatchBreadthDeepOrchestratorConfig& config )
	{
		LinearBatchSourceSelectionCheckpointState state {};
		state.config = config;
		state.jobs = jobs;
		state.completed_job_flags.assign( jobs.size(), 0u );
		return state;
	}

	static inline bool write_linear_batch_source_selection_checkpoint(
		const std::string& path,
		const LinearBatchSourceSelectionCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[&]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::LinearHullBatchSelection ) )
					return false;
				TwilightDream::auto_search_linear::write_config( w, state.config.breadth_configuration );
				write_runtime_controls( w, state.config.breadth_runtime );
				TwilightDream::auto_search_linear::write_config( w, state.config.deep_configuration );
				write_runtime_controls( w, state.config.deep_runtime );
				write_enum_u8( w, state.stage );
				write_counted_container(
					w,
					state.jobs,
					[&]( const LinearBatchJob& job ) {
						w.write_i32( job.rounds );
						w.write_u32( job.output_branch_a_mask );
						w.write_u32( job.output_branch_b_mask );
					} );
				write_u8_vector( w, state.completed_job_flags, [&]( std::uint8_t flag ) { w.write_u8( flag ); } );
				w.write_u64( state.breadth_total_nodes );
				write_counted_container(
					w,
					state.top_candidates,
					[&]( const LinearBatchTopCandidate& candidate ) { write_linear_batch_top_candidate( w, candidate ); } );
				return w.ok();
			} );
	}

	static inline bool read_linear_batch_source_selection_checkpoint(
		const std::string& path,
		LinearBatchSourceSelectionCheckpointState& state )
	{
		using namespace TwilightDream::hull_batch_checkpoint_shared;
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::LinearHullBatchSelection )
			return false;
		if ( !TwilightDream::auto_search_linear::read_config( r, state.config.breadth_configuration ) )
			return false;
		if ( !read_runtime_controls( r, state.config.breadth_runtime ) )
			return false;
		if ( !TwilightDream::auto_search_linear::read_config( r, state.config.deep_configuration ) )
			return false;
		if ( !read_runtime_controls( r, state.config.deep_runtime ) )
			return false;
		if ( !read_enum_u8( r, state.stage ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.jobs,
				 [&]( LinearBatchJob& job ) {
					 return
						 r.read_i32( job.rounds ) &&
						 r.read_u32( job.output_branch_a_mask ) &&
						 r.read_u32( job.output_branch_b_mask );
				 } ) )
			return false;
		if ( !read_u8_vector( r, state.completed_job_flags, [&]( std::uint8_t& flag ) { return r.read_u8( flag ); } ) )
			return false;
		if ( !r.read_u64( state.breadth_total_nodes ) )
			return false;
		if ( !read_counted_container(
				 r,
				 state.top_candidates,
				 [&]( LinearBatchTopCandidate& candidate ) { return read_linear_batch_top_candidate( r, candidate ); } ) )
			return false;
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		linear_sort_batch_top_candidates( state.top_candidates );
		return true;
	}

	// Runs stage-1 breadth over all jobs (parallel) keeping top-K candidates, then stage-2 deep with a fixed worker pool.
	// Caller must build breadth/deep configs (strict caps, auto_breadth_maxnodes baked into breadth_runtime, etc.).
	inline void run_linear_batch_breadth_then_deep(
		const std::vector<LinearBatchJob>&				 jobs,
		int												 worker_thread_count,
		std::size_t										 selection_top_k,
		const LinearBatchBreadthDeepOrchestratorConfig&	 cfg,
		LinearBatchBreadthDeepOrchestratorResult&		 out )
	{
		using namespace TwilightDream::auto_search_linear;
		using TwilightDream::runtime_component::IosStateGuard;
		using TwilightDream::runtime_component::memory_governor_poll_if_needed;
		using TwilightDream::runtime_component::pmr_report_oom_once;
		using TwilightDream::runtime_component::print_word32_hex;
		using TwilightDream::runtime_component::run_named_worker_group;
		using TwilightDream::runtime_component::run_named_worker_group_with_monitor;

		out = LinearBatchBreadthDeepOrchestratorResult {};

		const int		  threads = std::max( 1, worker_thread_count );
		const std::size_t deep_top_k =
			linear_resolve_batch_top_candidate_limit( jobs.size(), threads, selection_top_k );

		using BreadthCandidate = LinearBatchTopCandidate;

		auto candidate_key_better = []( const BreadthCandidate& a, const BreadthCandidate& b ) -> bool {
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

		std::vector<std::atomic<std::size_t>> active_job_id_by_thread( static_cast<std::size_t>( threads ) );
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
			std::sort( top_candidates.begin(), top_candidates.end(), [ & ]( const BreadthCandidate& x, const BreadthCandidate& y ) { return candidate_key_better( x, y ); } );
		};

		auto breadth_worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t job_index = next_index.fetch_add( 1, std::memory_order_relaxed );
				if ( job_index >= jobs.size() )
					break;

				active_job_id_by_thread[ std::size_t( thread_id ) ].store( job_index + 1, std::memory_order_relaxed );
				const LinearBatchJob job = jobs[ job_index ];

				LinearBestSearchConfiguration cfg_local = cfg.breadth_configuration;
				cfg_local.round_count = job.rounds;

				MatsuiSearchRunLinearResult result {};
				try
				{
					result = run_linear_best_search( job.output_branch_a_mask, job.output_branch_b_mask, cfg_local, cfg.breadth_runtime );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "batch.breadth.run" );
					result = MatsuiSearchRunLinearResult {};
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

		const std::uint64_t breadth_progress_sec = ( cfg.breadth_runtime.progress_every_seconds == 0 ) ? 1 : cfg.breadth_runtime.progress_every_seconds;
		const auto			breadth_start_time = std::chrono::steady_clock::now();

		auto breadth_progress_monitor = [ & ]() {
			const std::size_t total = jobs.size();
			if ( total == 0 || breadth_progress_sec == 0 )
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
						std::size_t	 thread_id = 0;
						std::size_t	 job_id_one_based = 0;
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
						const LinearBatchJob& j = jobs[ job_index ];
						active.push_back( ActiveBatchJob { i, id, j.output_branch_a_mask, j.output_branch_b_mask } );
					}

					IosStateGuard g( std::cout );
					std::cout << "[Batch][Breadth] progress " << done << "/" << total << " (" << std::fixed << std::setprecision( 2 ) << ( 100.0 * double( done ) / double( total ) ) << "%)"
							  << "  jobs_per_second=" << std::setprecision( 2 ) << rate << "  elapsed_seconds=" << std::setprecision( 2 ) << elapsed;
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

		run_named_worker_group_with_monitor( "linear-batch-breadth", threads, [ & ]( TwilightDream::runtime_component::RuntimeTaskContext& context ) { breadth_worker( int( context.slot_index ) ); }, breadth_progress_monitor );

		out.breadth_total_nodes = total_nodes.load( std::memory_order_relaxed );
		out.top_candidates = top_candidates;
		std::cout << "\n[Batch][Breadth] done. total_nodes_visited=" << out.breadth_total_nodes << "\n";

		if ( top_candidates.empty() )
		{
			std::cout << "[Batch][Breadth] FAIL: no trail found in any job (within breadth limits).\n";
			return;
		}
		out.breadth_had_any_candidate = true;

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

		std::cout << "[Batch][Deep] inputs (from breadth top candidates): count=" << top_candidates.size() << "\n";
		for ( std::size_t i = 0; i < top_candidates.size(); ++i )
		{
			const auto& current_candidate = top_candidates[ i ];
			const auto& current_job = jobs[ current_candidate.job_index ];
			std::cout << "  #" << ( i + 1 ) << "  job=#" << ( current_candidate.job_index + 1 ) << "  rounds=" << current_job.rounds << "  breadth_best_w=" << current_candidate.best_weight << "\n";
		}
		const std::uint64_t deep_progress_sec = ( cfg.deep_runtime.progress_every_seconds == 0 ) ? 1 : cfg.deep_runtime.progress_every_seconds;
		const std::size_t deep_worker_count = std::min<std::size_t>( top_candidates.size(), std::size_t( threads ) );
		std::cout << "  deep_threads=" << deep_worker_count << "  progress_every_seconds=" << deep_progress_sec << "\n\n";

		struct DeepResult
		{
			std::size_t					job_index = 0;
			MatsuiSearchRunLinearResult result {};
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

		std::atomic<std::size_t> next_deep_index { 0 };
		auto deep_worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t candidate_index = next_deep_index.fetch_add( 1, std::memory_order_relaxed );
				if ( candidate_index >= top_candidates.size() )
					break;

				const BreadthCandidate& current_candidate = top_candidates[ candidate_index ];
				const LinearBatchJob&	current_job = jobs[ current_candidate.job_index ];
				const std::size_t		job_id = current_candidate.job_index + 1;

				const std::string prefix = "[Job#" + std::to_string( job_id ) + "@" + std::to_string( thread_id ) + "] ";
				TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );

				BestWeightHistory checkpoint {};
				const std::string checkpoint_path_base = BestWeightHistory::default_path( current_job.rounds, current_candidate.start_mask_a, current_candidate.start_mask_b );
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

				LinearBestSearchConfiguration deep_cfg = cfg.deep_configuration;
				deep_cfg.round_count = current_job.rounds;

				const TwilightDream::auto_search_linear::SearchWeight seed_weight = current_candidate.best_weight;
				const std::vector<LinearTrailStepRecord>* seed_trail =
					current_candidate.trail.empty() ? nullptr : &current_candidate.trail;

				MatsuiSearchRunLinearResult result {};
				try
				{
					result = run_linear_best_search( current_job.output_branch_a_mask, current_job.output_branch_b_mask, deep_cfg, cfg.deep_runtime, true, true, seed_weight, seed_trail, checkpoint_ok ? &checkpoint : nullptr );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "batch.deep.run" );
					result = MatsuiSearchRunLinearResult {};
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

				deep_results[ candidate_index ] = DeepResult { current_candidate.job_index, std::move( result ) };
			}
		};

		run_named_worker_group( "linear-batch-deep", int( deep_worker_count ), [ & ]( TwilightDream::runtime_component::RuntimeTaskContext& context ) { deep_worker( int( context.slot_index ) ); } );

		bool			 global_best_found = false;
		std::size_t		 global_best_job_index = 0;
		MatsuiSearchRunLinearResult global_best_result {};
		std::uint64_t total_nodes_deep = 0;
		for ( const auto& dr : deep_results )
		{
			total_nodes_deep += dr.result.nodes_visited;
			if ( !dr.result.found )
				continue;
			if ( !global_best_found || dr.result.best_weight < global_best_result.best_weight )
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
			if ( !deep_result.found || deep_result.best_linear_trail.empty() )
				continue;

			auto& candidate = top_candidates[ candidate_index ];
			candidate.found = true;
			candidate.best_weight = deep_result.best_weight;
			candidate.nodes = deep_result.nodes_visited;
			candidate.hit_maximum_search_nodes_limit = deep_result.hit_maximum_search_nodes;
			candidate.best_input_mask_a = deep_result.best_input_branch_a_mask;
			candidate.best_input_mask_b = deep_result.best_input_branch_b_mask;
			candidate.trail = deep_result.best_linear_trail;
		}
		std::sort( top_candidates.begin(), top_candidates.end(), [ & ]( const BreadthCandidate& x, const BreadthCandidate& y ) { return candidate_key_better( x, y ); } );
		out.top_candidates = top_candidates;

		if ( !global_best_found || !global_best_result.found || global_best_result.best_linear_trail.empty() )
		{
			std::cout << "\n[Batch][Deep] no trail found in any deep job; retaining breadth TOP-K seeds for downstream stages.\n";
			return;
		}

		out.deep_success = true;
		out.best_job_index = global_best_job_index;
		out.global_best = std::move( global_best_result );

		const LinearBatchJob& best_job = jobs[ global_best_job_index ];
		std::cout << "\n[Batch] BEST (from deep): weight=" << out.global_best.best_weight << "  approx_abs_correlation=" << std::scientific << std::setprecision( 10 ) << std::pow( 2.0, -double( out.global_best.best_weight ) ) << std::defaultfloat << "\n";
		std::cout << "  best_job=#" << ( global_best_job_index + 1 ) << "  best_rounds=" << best_job.rounds << "\n";
		print_word32_hex( "  best_output_branch_a_mask=", best_job.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "  best_output_branch_b_mask=", best_job.output_branch_b_mask );
		std::cout << "\n\n";
	}

	inline void run_linear_batch_breadth_then_deep_from_checkpoint_state(
		LinearBatchSourceSelectionCheckpointState& state,
		int worker_thread_count,
		std::size_t selection_top_k,
		const std::string& checkpoint_path,
		const std::function<void( const LinearBatchSourceSelectionCheckpointState&, const char* )>& on_checkpoint_written,
		LinearBatchBreadthDeepOrchestratorResult& out )
	{
		using namespace TwilightDream::auto_search_linear;
		using TwilightDream::runtime_component::IosStateGuard;
		using TwilightDream::runtime_component::memory_governor_poll_if_needed;
		using TwilightDream::runtime_component::pmr_report_oom_once;
		using TwilightDream::runtime_component::print_word32_hex;
		using TwilightDream::runtime_component::run_named_worker_group;
		using TwilightDream::runtime_component::run_named_worker_group_with_monitor;

		out = LinearBatchBreadthDeepOrchestratorResult {};

		const int threads = std::max( 1, worker_thread_count );
		const std::size_t deep_top_k =
			linear_resolve_batch_top_candidate_limit( state.jobs.size(), threads, selection_top_k );
		if ( state.completed_job_flags.size() < state.jobs.size() )
			state.completed_job_flags.resize( state.jobs.size(), 0u );
		linear_sort_batch_top_candidates( state.top_candidates );
		if ( state.top_candidates.size() > deep_top_k )
			state.top_candidates.resize( deep_top_k );

		if ( state.stage == LinearBatchSelectionCheckpointStage::Breadth )
		{
			std::atomic<std::size_t> next_index { 0 };
			std::atomic<std::size_t> completed {
				static_cast<std::size_t>( std::count_if(
					state.completed_job_flags.begin(),
					state.completed_job_flags.end(),
					[]( std::uint8_t flag ) { return flag != 0u; } ) ) };

			std::mutex state_mutex;
			const auto breadth_start_time = std::chrono::steady_clock::now();

			auto breadth_worker = [ & ]( int ) {
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

					const LinearBatchJob job = state.jobs[ job_index ];
					LinearBestSearchConfiguration cfg_local = state.config.breadth_configuration;
					cfg_local.round_count = job.rounds;

					MatsuiSearchRunLinearResult result {};
					try
					{
						result = run_linear_best_search(
							job.output_branch_a_mask,
							job.output_branch_b_mask,
							cfg_local,
							state.config.breadth_runtime );
					}
					catch ( const std::bad_alloc& )
					{
						pmr_report_oom_once( "batch.breadth.resume.linear" );
						result = MatsuiSearchRunLinearResult {};
					}

					LinearBatchTopCandidate candidate {};
					candidate.job_index = job_index;
					candidate.start_mask_a = job.output_branch_a_mask;
					candidate.start_mask_b = job.output_branch_b_mask;
					candidate.found = result.found;
					candidate.best_weight = result.best_weight;
					candidate.nodes = result.nodes_visited;
					candidate.hit_maximum_search_nodes_limit = result.hit_maximum_search_nodes;
					candidate.best_input_mask_a = result.best_input_branch_a_mask;
					candidate.best_input_mask_b = result.best_input_branch_b_mask;
					candidate.trail = result.best_linear_trail;

					{
						std::scoped_lock lk( state_mutex );
						state.breadth_total_nodes += result.nodes_visited;
						state.completed_job_flags[ job_index ] = 1u;
						if ( candidate.found )
							linear_try_update_batch_top_candidates( state.top_candidates, deep_top_k, std::move( candidate ) );
						if ( !checkpoint_path.empty() )
						{
							if ( write_linear_batch_source_selection_checkpoint( checkpoint_path, state ) && on_checkpoint_written )
								on_checkpoint_written( state, "breadth_job_completed" );
						}
					}
					completed.fetch_add( 1, std::memory_order_relaxed );
				}
			};

			const std::uint64_t breadth_progress_sec =
				( state.config.breadth_runtime.progress_every_seconds == 0 ) ?
					1 :
					state.config.breadth_runtime.progress_every_seconds;

			auto breadth_progress_monitor = [ & ]() {
				const std::size_t total = state.jobs.size();
				if ( total == 0 )
					return;

				std::size_t last_done = completed.load( std::memory_order_relaxed );
				auto		last_time = breadth_start_time;
				for ( ;; )
				{
					const std::size_t done = completed.load( std::memory_order_relaxed );
					const auto now = std::chrono::steady_clock::now();
					memory_governor_poll_if_needed( now );

					const double since_last = std::chrono::duration<double>( now - last_time ).count();
					if ( since_last >= double( breadth_progress_sec ) || done == total )
					{
						const double elapsed = std::chrono::duration<double>( now - breadth_start_time ).count();
						const double window = std::max( 1e-9, since_last );
						const std::size_t delta = ( done >= last_done ) ? ( done - last_done ) : 0;
						const double rate = double( delta ) / window;

						LinearBatchTopCandidate best_snapshot {};
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
							std::cout << " best_w=" << best_snapshot.best_weight << " best_job=#" << ( best_snapshot.job_index + 1 );
						std::cout << "\n";

						last_done = done;
						last_time = now;
					}
					if ( done >= total )
						break;
					std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
				}
			};

			run_named_worker_group_with_monitor( "linear-batch-breadth", threads, [ & ]( TwilightDream::runtime_component::RuntimeTaskContext& context ) { breadth_worker( int( context.slot_index ) ); }, breadth_progress_monitor );
			linear_sort_batch_top_candidates( state.top_candidates );
			if ( state.top_candidates.size() > deep_top_k )
				state.top_candidates.resize( deep_top_k );
			state.stage = LinearBatchSelectionCheckpointStage::DeepReady;
			if ( !checkpoint_path.empty() )
			{
				if ( write_linear_batch_source_selection_checkpoint( checkpoint_path, state ) && on_checkpoint_written )
					on_checkpoint_written( state, "selection_deep_ready" );
			}
		}

		out.breadth_total_nodes = state.breadth_total_nodes;
		out.top_candidates = state.top_candidates;
		out.breadth_had_any_candidate = !state.top_candidates.empty();

		std::cout << "\n[Batch][Breadth] done. total_nodes_visited=" << out.breadth_total_nodes << "\n";
		if ( state.top_candidates.empty() )
		{
			std::cout << "[Batch][Breadth] FAIL: no trail found in any job (within breadth limits).\n";
			return;
		}

		std::cout << "[Batch][Breadth] TOP-" << state.top_candidates.size() << ":\n";
		for ( std::size_t index = 0; index < state.top_candidates.size(); ++index )
		{
			const auto& c = state.top_candidates[ index ];
			const auto& j = state.jobs[ c.job_index ];
			IosStateGuard g( std::cout );
			std::cout << "  #" << ( index + 1 ) << "  job=#" << ( c.job_index + 1 ) << "  rounds=" << j.rounds << "  best_weight=" << c.best_weight << "  nodes=" << c.nodes << ( c.hit_maximum_search_nodes_limit ? " [HIT maximum_search_nodes limit]" : "" ) << "\n";
		}
		std::cout << "\n";

		std::cout << "[Batch][Deep] inputs (from breadth top candidates): count=" << state.top_candidates.size() << "\n";
		for ( std::size_t index = 0; index < state.top_candidates.size(); ++index )
		{
			const auto& c = state.top_candidates[ index ];
			const auto& j = state.jobs[ c.job_index ];
			std::cout << "  #" << ( index + 1 ) << "  job=#" << ( c.job_index + 1 ) << "  rounds=" << j.rounds << "  breadth_best_w=" << c.best_weight << "\n";
		}
		const std::uint64_t deep_progress_sec =
			( state.config.deep_runtime.progress_every_seconds == 0 ) ?
				1 :
				state.config.deep_runtime.progress_every_seconds;
		const std::size_t deep_worker_count = std::min<std::size_t>( state.top_candidates.size(), std::size_t( threads ) );
		std::cout << "  deep_threads=" << deep_worker_count << "  progress_every_seconds=" << deep_progress_sec << "\n\n";

		struct DeepResult
		{
			std::size_t job_index = 0;
			MatsuiSearchRunLinearResult result {};
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

		std::atomic<std::size_t> next_deep_index { 0 };
		auto deep_worker = [ & ]( int thread_id ) {
			for ( ;; )
			{
				const std::size_t candidate_index = next_deep_index.fetch_add( 1, std::memory_order_relaxed );
				if ( candidate_index >= state.top_candidates.size() )
					break;

				const LinearBatchTopCandidate& current_candidate = state.top_candidates[ candidate_index ];
				const LinearBatchJob& current_job = state.jobs[ current_candidate.job_index ];
				const std::size_t job_id = current_candidate.job_index + 1;

				const std::string prefix = "[Job#" + std::to_string( job_id ) + "@" + std::to_string( thread_id ) + "] ";
				TwilightDream::runtime_component::ProgressPrefixGuard prefix_guard( prefix.c_str() );

				BestWeightHistory checkpoint {};
				const std::string checkpoint_path_base = BestWeightHistory::default_path( current_job.rounds, current_candidate.start_mask_a, current_candidate.start_mask_b );
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

				LinearBestSearchConfiguration deep_cfg = state.config.deep_configuration;
				deep_cfg.round_count = current_job.rounds;

				const TwilightDream::auto_search_linear::SearchWeight seed_weight = current_candidate.best_weight;
				const std::vector<LinearTrailStepRecord>* seed_trail =
					current_candidate.trail.empty() ? nullptr : &current_candidate.trail;

				MatsuiSearchRunLinearResult result {};
				try
				{
					result = run_linear_best_search( current_job.output_branch_a_mask, current_job.output_branch_b_mask, deep_cfg, state.config.deep_runtime, true, true, seed_weight, seed_trail, checkpoint_ok ? &checkpoint : nullptr );
				}
				catch ( const std::bad_alloc& )
				{
					pmr_report_oom_once( "batch.deep.resume.linear" );
					result = MatsuiSearchRunLinearResult {};
				}

				deep_results[ candidate_index ] = DeepResult { current_candidate.job_index, std::move( result ) };
			}
		};

		run_named_worker_group( "linear-batch-deep", int( deep_worker_count ), [ & ]( TwilightDream::runtime_component::RuntimeTaskContext& context ) { deep_worker( int( context.slot_index ) ); } );

		bool global_best_found = false;
		std::size_t global_best_job_index = 0;
		MatsuiSearchRunLinearResult global_best_result {};
		std::uint64_t total_nodes_deep = 0;
		for ( const auto& dr : deep_results )
		{
			total_nodes_deep += dr.result.nodes_visited;
			if ( !dr.result.found )
				continue;
			if ( !global_best_found || dr.result.best_weight < global_best_result.best_weight )
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
			if ( !deep_result.found || deep_result.best_linear_trail.empty() )
				continue;

			auto& candidate = out.top_candidates[ candidate_index ];
			candidate.found = true;
			candidate.best_weight = deep_result.best_weight;
			candidate.nodes = deep_result.nodes_visited;
			candidate.hit_maximum_search_nodes_limit = deep_result.hit_maximum_search_nodes;
			candidate.best_input_mask_a = deep_result.best_input_branch_a_mask;
			candidate.best_input_mask_b = deep_result.best_input_branch_b_mask;
			candidate.trail = deep_result.best_linear_trail;
		}
		linear_sort_batch_top_candidates( out.top_candidates );

		if ( !global_best_found || !global_best_result.found || global_best_result.best_linear_trail.empty() )
		{
			std::cout << "\n[Batch][Deep] no trail found in any deep job; retaining breadth TOP-K seeds for downstream stages.\n";
			return;
		}

		out.deep_success = true;
		out.best_job_index = global_best_job_index;
		out.global_best = std::move( global_best_result );

		const LinearBatchJob& best_job = state.jobs[ global_best_job_index ];
		std::cout << "\n[Batch] BEST (from deep): weight=" << out.global_best.best_weight << "  approx_abs_correlation=" << std::scientific << std::setprecision( 10 ) << std::pow( 2.0, -double( out.global_best.best_weight ) ) << std::defaultfloat << "\n";
		std::cout << "  best_job=#" << ( global_best_job_index + 1 ) << "  best_rounds=" << best_job.rounds << "\n";
		print_word32_hex( "  best_output_branch_a_mask=", best_job.output_branch_a_mask );
		std::cout << "\n";
		print_word32_hex( "  best_output_branch_b_mask=", best_job.output_branch_b_mask );
		std::cout << "\n\n";
	}

}  // namespace TwilightDream::hull_callback_aggregator

#endif
