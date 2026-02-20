#pragma once
#if !defined( AUTO_SEARCH_FRAME_DETAIL_BEST_SEARCH_SHARED_CORE_HPP )
#define AUTO_SEARCH_FRAME_DETAIL_BEST_SEARCH_SHARED_CORE_HPP

#include "common/runtime_component.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace TwilightDream::best_search_shared_core
{
	enum class FinalCheckpointAction : std::uint8_t
	{
		WriteSnapshot = 0,
		PreserveExisting = 1
	};

	struct FinalCheckpointDecision
	{
		FinalCheckpointAction action = FinalCheckpointAction::WriteSnapshot;
		const char*			  reason = "final_snapshot";
	};

	struct StoredRuntimeMetadata
	{
		std::uint64_t run_nodes_visited = 0;
		std::uint64_t maximum_search_nodes = 0;
		std::uint64_t maximum_search_seconds = 0;
		std::uint64_t progress_every_seconds = 0;
		std::uint64_t checkpoint_every_seconds = 0;
		std::uint64_t progress_node_mask = 0;
		bool		  last_run_hit_node_limit = false;
		bool		  last_run_hit_time_limit = false;
	};

	/// Values taken from a loaded single-run checkpoint for \ref resolve_resume_runtime_controls /
	/// \ref build_resume_runtime_plan only consult \p maximum_search_nodes, \p maximum_search_seconds,
	/// \p progress_every_seconds, and \p checkpoint_every_seconds. Other \ref StoredRuntimeMetadata
	/// members are ignored by the current resume merge (checkpoint files still store them for
	/// diagnostics / auto-pipeline snapshots / forward compatibility).
	inline StoredRuntimeMetadata stored_runtime_metadata_for_resume_control_merge(
		std::uint64_t checkpoint_maximum_search_nodes,
		std::uint64_t checkpoint_maximum_search_seconds,
		std::uint64_t checkpoint_progress_every_seconds,
		std::uint64_t checkpoint_checkpoint_every_seconds ) noexcept
	{
		StoredRuntimeMetadata m {};
		m.maximum_search_nodes = checkpoint_maximum_search_nodes;
		m.maximum_search_seconds = checkpoint_maximum_search_seconds;
		m.progress_every_seconds = checkpoint_progress_every_seconds;
		m.checkpoint_every_seconds = checkpoint_checkpoint_every_seconds;
		return m;
	}

	/// CLI-style banner after a single-run binary checkpoint has been parsed (\p best_metric_name e.g. \c "best_weight" or \c "best_total_weight").
	inline void print_resume_checkpoint_load_summary(
		std::ostream& os,
		const std::string& checkpoint_path,
		int			  round_count,
		std::uint64_t total_nodes_visited,
		std::uint64_t run_nodes_visited,
		const char*	  best_metric_name,
		int			  best_metric_value,
		std::uint64_t stored_runtime_maximum_search_nodes,
		std::uint64_t stored_runtime_maximum_search_seconds,
		std::uint64_t stored_runtime_checkpoint_every_seconds,
		std::uint64_t accumulated_elapsed_usec )
	{
		os << "[Resume] checkpoint_path=" << checkpoint_path << "\n";
		os << "[Resume] round_count=" << round_count << "  total_nodes_visited=" << total_nodes_visited << "  run_nodes_visited=" << run_nodes_visited << "  " << best_metric_name << "=" << best_metric_value << "\n";
		os << "[Resume] stored_runtime_maximum_search_nodes=" << stored_runtime_maximum_search_nodes << "  stored_runtime_maximum_search_seconds=" << stored_runtime_maximum_search_seconds << "  stored_runtime_checkpoint_every_seconds=" << stored_runtime_checkpoint_every_seconds << "\n";
		{
			TwilightDream::runtime_component::IosStateGuard g( os );
			const double elapsed_seconds = static_cast<double>( accumulated_elapsed_usec ) / 1e6;
			os << "[Resume] elapsed_seconds_recorded_in_checkpoint=" << std::fixed << std::setprecision( 3 ) << elapsed_seconds << "\n";
		}
	}

	struct RuntimeControlOverrideMask
	{
		bool maximum_search_nodes = false;
		bool maximum_search_seconds = false;
		bool progress_every_seconds = false;
		bool checkpoint_every_seconds = false;
	};

	/// Optional resume-only behavior for timed \c [Progress] lines (CLI test harness vs API defaults).
	struct ResumeProgressReportingOptions
	{
		/// When true, disable timed progress lines even if merged runtime controls use a positive interval.
		bool			force_disabled = false;
		/// When true and \p cli_progress_every_seconds is non-zero, use it instead of merged \c progress_every_seconds.
		bool			prefer_cli_interval = false;
		std::uint64_t cli_progress_every_seconds = 0;
	};

	template <class ContextT>
	inline std::uint64_t effective_resume_progress_interval_seconds(
		const ContextT& context,
		const ResumeProgressReportingOptions* opt ) noexcept
	{
		if ( opt && opt->force_disabled )
			return 0;
		if ( opt && opt->prefer_cli_interval && opt->cli_progress_every_seconds != 0 )
			return opt->cli_progress_every_seconds;
		return context.runtime_controls.progress_every_seconds;
	}

	enum class StartupMemoryGatePolicy : std::uint8_t
	{
		EnforceReject = 0,
		AdvisoryOnly = 1
	};

	struct StartupMemoryGateDecision
	{
		StartupMemoryGatePolicy policy = StartupMemoryGatePolicy::EnforceReject;
		bool					allow_start = true;
		bool					override_used = false;
	};

	struct ResumeRuntimePlan
	{
		TwilightDream::runtime_component::SearchRuntimeControls runtime_controls {};
		std::uint64_t										  total_nodes_visited = 0;
		std::uint64_t										  accumulated_elapsed_usec = 0;
	};

	struct ResolvedLogArtifactPaths
	{
		std::string history_log_path {};
		std::string runtime_log_path {};
	};

	class CheckpointFingerprintBuilder final
	{
	public:
		static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
		static constexpr std::uint64_t kPrime = 1099511628211ull;

		void mix_bytes( const void* data, std::size_t size ) noexcept
		{
			const auto* bytes = static_cast<const std::uint8_t*>( data );
			for ( std::size_t i = 0; i < size; ++i )
			{
				state_ ^= std::uint64_t( bytes[ i ] );
				state_ *= kPrime;
			}
		}

		void mix_u8( std::uint8_t value ) noexcept
		{
			mix_bytes( &value, sizeof( value ) );
		}

		void mix_u32( std::uint32_t value ) noexcept
		{
			mix_bytes( &value, sizeof( value ) );
		}

		void mix_u64( std::uint64_t value ) noexcept
		{
			mix_bytes( &value, sizeof( value ) );
		}

		void mix_i32( std::int32_t value ) noexcept
		{
			mix_bytes( &value, sizeof( value ) );
		}

		void mix_i64( std::int64_t value ) noexcept
		{
			mix_bytes( &value, sizeof( value ) );
		}

		void mix_bool( bool value ) noexcept
		{
			const std::uint8_t raw = value ? 1u : 0u;
			mix_u8( raw );
		}

		template <class EnumT, std::enable_if_t<std::is_enum_v<EnumT>, int> = 0>
		void mix_enum( EnumT value ) noexcept
		{
			using UnderlyingT = std::underlying_type_t<EnumT>;
			if constexpr ( std::is_signed_v<UnderlyingT> )
				mix_i64( static_cast<std::int64_t>( value ) );
			else
				mix_u64( static_cast<std::uint64_t>( value ) );
		}

		void mix_string( std::string_view value ) noexcept
		{
			mix_u64( static_cast<std::uint64_t>( value.size() ) );
			if ( !value.empty() )
				mix_bytes( value.data(), value.size() );
		}

		std::uint64_t finish() const noexcept
		{
			return state_;
		}

	private:
		std::uint64_t state_ = kOffsetBasis;
	};

	std::string checkpoint_fingerprint_hex( std::uint64_t value );

	static constexpr std::uint64_t kLatestCheckpointSafetyIntervalSeconds = 60ull;

	class RuntimeWatchdog final
	{
	public:
		RuntimeWatchdog() = default;
		RuntimeWatchdog( const RuntimeWatchdog& ) = delete;
		RuntimeWatchdog& operator=( const RuntimeWatchdog& ) = delete;
		~RuntimeWatchdog();

		void start(
			const TwilightDream::runtime_component::SearchRuntimeControls& runtime_controls,
			TwilightDream::runtime_component::RuntimeInvocationState& runtime_state,
			bool checkpoint_enabled );

		void stop( TwilightDream::runtime_component::RuntimeInvocationState* runtime_state = nullptr );

	private:
		void run() noexcept;

		TwilightDream::runtime_component::RuntimeWatchdogControl control_ {};
		TwilightDream::runtime_component::SearchRuntimeControls controls_ {};
		bool													   checkpoint_enabled_ = false;
		std::chrono::steady_clock::time_point					   run_start_time_ {};
		std::chrono::steady_clock::time_point					   next_latest_checkpoint_due_time_ {};
		std::chrono::steady_clock::time_point					   next_archive_checkpoint_due_time_ {};
		TwilightDream::runtime_component::NamedServiceThread	   service_thread_ {};
		TwilightDream::runtime_component::RuntimeInvocationState* attached_runtime_state_ = nullptr;
	};

	bool should_poll_binary_checkpoint(
		bool pending_best_change,
		bool pending_watchdog_request,
		std::uint64_t visited_nodes,
		std::uint64_t progress_node_mask ) noexcept;

	FinalCheckpointDecision decide_final_checkpoint_action(
		bool cursor_empty,
		bool runtime_budget_hit,
		bool has_previous_write,
		bool pending_best_change ) noexcept;

	const char* startup_memory_gate_policy_name( StartupMemoryGatePolicy policy ) noexcept;

	StartupMemoryGatePolicy startup_memory_gate_policy_for_strict_search( bool strict_search_mode ) noexcept;

	StartupMemoryGateDecision decide_startup_memory_gate(
		const TwilightDream::runtime_component::MemoryGateEvaluation& evaluation,
		bool														 allow_high_memory_usage,
		bool														 strict_search_mode ) noexcept;

	bool print_and_enforce_startup_memory_gate(
		const char*													 prefix,
		const TwilightDream::runtime_component::MemoryGateEvaluation& evaluation,
		bool														 allow_high_memory_usage,
		bool														 strict_search_mode );

	TwilightDream::runtime_component::SearchRuntimeControls resolve_resume_runtime_controls(
		const TwilightDream::runtime_component::SearchRuntimeControls& requested,
		const StoredRuntimeMetadata& stored,
		const RuntimeControlOverrideMask& overrides ) noexcept;

	const char* resume_runtime_budget_scope_name() noexcept;

	ResumeRuntimePlan build_resume_runtime_plan(
		const TwilightDream::runtime_component::SearchRuntimeControls& requested,
		const StoredRuntimeMetadata& stored,
		const RuntimeControlOverrideMask& overrides,
		std::uint64_t total_nodes_visited,
		std::uint64_t accumulated_elapsed_usec ) noexcept;

	template <class ContextT>
	inline void inherit_runtime_artifact_paths( ContextT& context )
	{
		if ( context.checkpoint != nullptr && context.history_log_output_path.empty() )
			context.history_log_output_path = context.checkpoint->path;
		if ( context.runtime_event_log != nullptr && context.runtime_log_output_path.empty() )
			context.runtime_log_output_path = context.runtime_event_log->path;
	}

	template <class ContextT>
	inline std::uint64_t accumulated_elapsed_microseconds( const ContextT& context ) noexcept
	{
		return context.accumulated_elapsed_usec + TwilightDream::runtime_component::runtime_elapsed_microseconds( context.run_start_time );
	}

	template <class ContextT>
	inline double accumulated_elapsed_seconds( const ContextT& context ) noexcept
	{
		return static_cast<double>( accumulated_elapsed_microseconds( context ) ) / 1e6;
	}

	template <class DefaultHistoryPathFn, class DefaultRuntimePathFn>
	inline ResolvedLogArtifactPaths resolve_log_artifact_paths(
		const std::string& runtime_log_override_path,
		const std::string& inherited_history_log_path,
		const std::string& inherited_runtime_log_path,
		DefaultHistoryPathFn&& default_history_log_path_fn,
		DefaultRuntimePathFn&& default_runtime_log_path_fn )
	{
		ResolvedLogArtifactPaths paths {};
		paths.history_log_path =
			inherited_history_log_path.empty() ?
				std::forward<DefaultHistoryPathFn>( default_history_log_path_fn )() :
				inherited_history_log_path;
		if ( !runtime_log_override_path.empty() )
			paths.runtime_log_path = runtime_log_override_path;
		else if ( !inherited_runtime_log_path.empty() )
			paths.runtime_log_path = inherited_runtime_log_path;
		else
			paths.runtime_log_path = std::forward<DefaultRuntimePathFn>( default_runtime_log_path_fn )();
		return paths;
	}

	/// Open a log file for append and mirror the chosen path to \p os (used by CLI / harness).
	/// When \p warn_on_open_failure is false, failed opens are silent (some auto-pipeline stages only report success).
	template <class LogT>
	inline bool open_append_log_and_report(
		std::ostream& os,
		LogT& log,
		const std::string& path,
		std::string_view console_tag,
		std::string_view path_key,
		std::string_view human_kind_for_warning,
		bool warn_on_open_failure = true,
		std::string_view success_suffix = "\n",
		std::string_view failure_suffix = "\n" )
	{
		const bool ok = log.open_append( path );
		if ( ok )
			os << console_tag << ' ' << path_key << '=' << path << success_suffix;
		else if ( warn_on_open_failure )
			os << console_tag << " WARNING: cannot open " << human_kind_for_warning << " file for writing: " << path << failure_suffix;
		return ok;
	}

	/// Open for append; on failure emit a single line to \p err_os (\p error_line_prefix is printed immediately before \p path).
	template <class LogT>
	inline bool open_append_log_or_emit_error(
		std::ostream& err_os,
		LogT& log,
		const std::string& path,
		std::string_view error_line_prefix )
	{
		const bool ok = log.open_append( path );
		if ( !ok )
			err_os << error_line_prefix << path << "\n";
		return ok;
	}

	template <class ContextT>
	inline bool context_has_enabled_binary_checkpoint( const ContextT& context ) noexcept
	{
		return context.binary_checkpoint != nullptr && context.binary_checkpoint->enabled();
	}

	template <class ContextT>
	inline void apply_resume_runtime_plan( ContextT& context, const ResumeRuntimePlan& plan ) noexcept
	{
		context.runtime_controls = plan.runtime_controls;
		context.visited_node_count = plan.total_nodes_visited;
		context.run_visited_node_count = 0;
		context.accumulated_elapsed_usec = plan.accumulated_elapsed_usec;
	}

	template <class BinaryCheckpointManagerT>
	inline void prepare_binary_checkpoint(
		BinaryCheckpointManagerT* binary_checkpoint,
		std::uint64_t checkpoint_every_seconds,
		bool pending_best )
	{
		if ( !binary_checkpoint )
			return;
		binary_checkpoint->pending_best = pending_best;
			if ( checkpoint_every_seconds != 0 )
				binary_checkpoint->every_seconds = checkpoint_every_seconds;
	}

	template <class BinaryCheckpointManagerT, class PathStringT>
	inline void prepare_binary_checkpoint(
		BinaryCheckpointManagerT* binary_checkpoint,
		std::uint64_t checkpoint_every_seconds,
		bool pending_best,
		const PathStringT& checkpoint_path )
	{
		if ( !binary_checkpoint )
			return;
		if ( binary_checkpoint->path.empty() )
			binary_checkpoint->path = checkpoint_path;
		binary_checkpoint->pending_best = pending_best;
			if ( checkpoint_every_seconds != 0 )
				binary_checkpoint->every_seconds = checkpoint_every_seconds;
	}

	template <class ContextT>
	inline bool initialize_progress_tracking(
		ContextT& context,
		std::uint64_t progress_every_seconds )
	{
		if ( progress_every_seconds == 0 )
			return false;
		context.progress_every_seconds = progress_every_seconds;
		context.progress_start_time = context.run_start_time;
		context.progress_last_print_time = {};
		context.progress_last_print_nodes = 0;
		return true;
	}

	template <class BinaryCheckpointManagerT, class WriteNowFn, class LogPreservedFn>
	inline void finalize_binary_checkpoint(
		BinaryCheckpointManagerT* binary_checkpoint,
		bool cursor_empty,
		bool runtime_budget_hit,
		WriteNowFn&& write_now,
		LogPreservedFn&& log_preserved )
	{
		if ( !binary_checkpoint )
			return;

		const auto final_checkpoint_decision =
			decide_final_checkpoint_action(
				cursor_empty,
				runtime_budget_hit,
				binary_checkpoint->last_write_time.time_since_epoch().count() != 0,
				binary_checkpoint->pending_best_change() );
		if ( final_checkpoint_decision.action == FinalCheckpointAction::PreserveExisting )
		{
			std::forward<LogPreservedFn>( log_preserved )( final_checkpoint_decision.reason );
			return;
		}
		if ( std::forward<WriteNowFn>( write_now )( final_checkpoint_decision.reason ) )
		{
			binary_checkpoint->pending_best = false;
			binary_checkpoint->last_write_time = std::chrono::steady_clock::now();
		}
	}

	template <class ContextT>
	class SearchControlSession final
	{
	public:
		explicit SearchControlSession( ContextT& context ) noexcept
			: context_( context )
		{
		}

		SearchControlSession( const SearchControlSession& ) = delete;
		SearchControlSession& operator=( const SearchControlSession& ) = delete;

		~SearchControlSession()
		{
			stop();
		}

		void begin()
		{
			if ( started_ )
				return;
			inherit_runtime_artifact_paths( context_ );
			watchdog_.start(
				context_.runtime_controls,
				context_.runtime_state,
				context_has_enabled_binary_checkpoint( context_ ) );
			started_ = true;
		}

		void stop()
		{
			if ( !started_ )
				return;
			watchdog_.stop( &context_.runtime_state );
			started_ = false;
		}

		template <class BinaryCheckpointManagerT, class WriteNowFn, class LogPreservedFn>
		void finalize(
			BinaryCheckpointManagerT* binary_checkpoint,
			bool cursor_empty,
			bool runtime_budget_hit,
			WriteNowFn&& write_now,
			LogPreservedFn&& log_preserved )
		{
			stop();
			finalize_binary_checkpoint(
				binary_checkpoint,
				cursor_empty,
				runtime_budget_hit,
				std::forward<WriteNowFn>( write_now ),
				std::forward<LogPreservedFn>( log_preserved ) );
		}

	private:
		ContextT&		context_;
		RuntimeWatchdog watchdog_ {};
		bool			started_ = false;
	};

	/// After \c begin_*_runtime_invocation and any resume text logs / \c maybe_write / \c write_*_resume_snapshot /
	/// \ref prepare_binary_checkpoint, runs \p continue_fn under \ref SearchControlSession (watchdog) and then
	/// \ref finalize_binary_checkpoint (same contract as \ref SearchControlSession::finalize).
	template <class ContextT, class CursorT, class BinaryCheckpointManagerT, class ContinueFn, class BudgetHitFn, class WriteNowFn, class LogPreservedFn>
	inline void control_session_run_then_finalize(
		ContextT& context,
		CursorT& cursor,
		BinaryCheckpointManagerT* binary_checkpoint,
		ContinueFn&& continue_fn,
		BudgetHitFn&& budget_hit,
		WriteNowFn&& write_now,
		LogPreservedFn&& log_preserved )
	{
		SearchControlSession<ContextT> control_session( context );
		control_session.begin();
		std::forward<ContinueFn>( continue_fn )();
		control_session.finalize(
			binary_checkpoint,
			cursor.stack.empty(),
			budget_hit( context ),
			std::forward<WriteNowFn>( write_now ),
			std::forward<LogPreservedFn>( log_preserved ) );
	}

	/// Shared resume-control skeleton for linear/differential search frontends:
	/// prepare checkpoint wiring, emit resume-start artifacts, run under watchdog, then log the final stop reason.
	template <
		class ContextT,
		class CursorT,
		class PrepareCheckpointFn,
		class BeginRuntimeFn,
		class LogResumeFn,
		class MaybeWriteResumeCheckpointFn,
		class WriteResumeSnapshotFn,
		class ContinueFn,
		class BudgetHitFn,
		class LogPreservedFn,
		class LogStopFn>
	inline void run_resume_control_session(
		ContextT& context,
		CursorT& cursor,
		PrepareCheckpointFn&& prepare_checkpoint_fn,
		BeginRuntimeFn&& begin_runtime_fn,
		LogResumeFn&& log_resume_fn,
		MaybeWriteResumeCheckpointFn&& maybe_write_resume_checkpoint_fn,
		WriteResumeSnapshotFn&& write_resume_snapshot_fn,
		ContinueFn&& continue_fn,
		BudgetHitFn&& budget_hit_fn,
		LogPreservedFn&& log_preserved_fn,
		LogStopFn&& log_stop_fn )
	{
		std::forward<BeginRuntimeFn>( begin_runtime_fn )( context );
		std::forward<LogResumeFn>( log_resume_fn )( context, cursor );
		std::forward<MaybeWriteResumeCheckpointFn>( maybe_write_resume_checkpoint_fn )( context, cursor );
		std::forward<WriteResumeSnapshotFn>( write_resume_snapshot_fn )( context, cursor );
		std::forward<PrepareCheckpointFn>( prepare_checkpoint_fn )( context );
		control_session_run_then_finalize(
			context,
			cursor,
			context.binary_checkpoint,
			[ & ]() {
				std::forward<ContinueFn>( continue_fn )( context, cursor );
			},
			[ & ]( ContextT& ctx ) {
				return std::forward<BudgetHitFn>( budget_hit_fn )( ctx );
			},
			[ & ]( const char* reason ) -> bool {
				return context.binary_checkpoint != nullptr &&
					   context.binary_checkpoint->write_now( context, cursor, reason );
			},
			[ & ]( const char* reason ) {
				std::forward<LogPreservedFn>( log_preserved_fn )( context, reason );
			} );
		std::forward<LogStopFn>( log_stop_fn )( context );
	}
}

#endif
