#include "auto_search_frame/detail/best_search_shared_core.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace TwilightDream::best_search_shared_core
{
	std::string checkpoint_fingerprint_hex( std::uint64_t value )
	{
		std::ostringstream oss;
		oss << "0x" << std::hex << std::uppercase << std::setw( 16 ) << std::setfill( '0' ) << value;
		return oss.str();
	}

	RuntimeWatchdog::~RuntimeWatchdog()
	{
		stop();
	}

	void RuntimeWatchdog::start(
		const TwilightDream::runtime_component::SearchRuntimeControls& runtime_controls,
		TwilightDream::runtime_component::RuntimeInvocationState& runtime_state,
		bool checkpoint_enabled )
	{
		stop( &runtime_state );
		controls_ = runtime_controls;
		checkpoint_enabled_ = checkpoint_enabled;
		run_start_time_ = runtime_state.run_start_time;
		control_.total_nodes_visited.store( runtime_state.total_nodes_visited, std::memory_order_relaxed );
		control_.run_nodes_visited.store( runtime_state.run_nodes_visited, std::memory_order_relaxed );
		control_.stop_due_to_time_limit.store( runtime_state.stop_due_to_time_limit, std::memory_order_relaxed );
		control_.stop_due_to_node_limit.store( runtime_state.stop_due_to_node_limit, std::memory_order_relaxed );
		control_.checkpoint_latest_due.store( false, std::memory_order_relaxed );
		control_.checkpoint_archive_due.store( false, std::memory_order_relaxed );
		runtime_state.watchdog_control = &control_;
		TwilightDream::runtime_component::runtime_sync_watchdog_control( runtime_state );
		attached_runtime_state_ = &runtime_state;
		stop_requested_.store( false, std::memory_order_relaxed );
		next_latest_checkpoint_due_time_ = checkpoint_enabled_ ? ( run_start_time_ + std::chrono::seconds( kLatestCheckpointSafetyIntervalSeconds ) ) : std::chrono::steady_clock::time_point {};
		next_archive_checkpoint_due_time_ =
			( checkpoint_enabled_ && controls_.checkpoint_every_seconds != 0 ) ?
				( run_start_time_ + std::chrono::seconds( controls_.checkpoint_every_seconds ) ) :
				std::chrono::steady_clock::time_point {};

		const bool need_thread =
			TwilightDream::runtime_component::runtime_effective_maximum_search_nodes( controls_ ) != 0 ||
			controls_.maximum_search_seconds != 0 ||
			checkpoint_enabled_;
		if ( need_thread )
			worker_ = std::thread( [ this ] { run(); } );
	}

	void RuntimeWatchdog::stop( TwilightDream::runtime_component::RuntimeInvocationState* runtime_state )
	{
		stop_requested_.store( true, std::memory_order_relaxed );
		if ( worker_.joinable() )
			worker_.join();

		TwilightDream::runtime_component::RuntimeInvocationState* state_to_detach =
			( runtime_state != nullptr ) ? runtime_state : attached_runtime_state_;
		if ( state_to_detach != nullptr && state_to_detach->watchdog_control == &control_ )
		{
			TwilightDream::runtime_component::runtime_pull_watchdog_stop_flags( *state_to_detach );
			state_to_detach->watchdog_control = nullptr;
		}
		attached_runtime_state_ = nullptr;
	}

	void RuntimeWatchdog::run() noexcept
	{
		const std::uint64_t effective_maximum_search_nodes =
			TwilightDream::runtime_component::runtime_effective_maximum_search_nodes( controls_ );
		for ( ;; )
		{
			if ( stop_requested_.load( std::memory_order_relaxed ) )
				break;

			const auto now = std::chrono::steady_clock::now();
			TwilightDream::runtime_component::memory_governor_poll_if_needed( now );

			if ( effective_maximum_search_nodes != 0 &&
				 control_.run_nodes_visited.load( std::memory_order_relaxed ) >= effective_maximum_search_nodes )
			{
				control_.stop_due_to_node_limit.store( true, std::memory_order_relaxed );
			}
			if ( TwilightDream::runtime_component::runtime_time_limit_reached_at( controls_.maximum_search_seconds, run_start_time_, now ) )
			{
				control_.stop_due_to_time_limit.store( true, std::memory_order_relaxed );
			}

			if ( checkpoint_enabled_ )
			{
				if ( next_latest_checkpoint_due_time_.time_since_epoch().count() != 0 && now >= next_latest_checkpoint_due_time_ )
				{
					control_.checkpoint_latest_due.store( true, std::memory_order_relaxed );
					next_latest_checkpoint_due_time_ = now + std::chrono::seconds( kLatestCheckpointSafetyIntervalSeconds );
				}
				if ( next_archive_checkpoint_due_time_.time_since_epoch().count() != 0 && now >= next_archive_checkpoint_due_time_ )
				{
					control_.checkpoint_archive_due.store( true, std::memory_order_relaxed );
					next_archive_checkpoint_due_time_ = now + std::chrono::seconds( controls_.checkpoint_every_seconds );
				}
			}

			if ( control_.stop_due_to_node_limit.load( std::memory_order_relaxed ) ||
				 control_.stop_due_to_time_limit.load( std::memory_order_relaxed ) )
			{
				break;
			}

			std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		}
	}

	bool should_poll_binary_checkpoint(
		bool pending_best_change,
		bool pending_watchdog_request,
		std::uint64_t visited_nodes,
		std::uint64_t progress_node_mask ) noexcept
	{
		return
			pending_best_change ||
			pending_watchdog_request ||
			( ( visited_nodes & progress_node_mask ) == 0 );
	}

	FinalCheckpointDecision decide_final_checkpoint_action(
		bool cursor_empty,
		bool runtime_budget_hit,
		bool has_previous_write,
		bool pending_best_change ) noexcept
	{
		if ( cursor_empty && runtime_budget_hit && has_previous_write )
		{
			return {
				FinalCheckpointAction::PreserveExisting,
				"preserve_last_resumable_snapshot"
			};
		}

		if ( pending_best_change )
		{
			return {
				FinalCheckpointAction::WriteSnapshot,
				"final_pending_best"
			};
		}

		if ( runtime_budget_hit )
		{
			return {
				FinalCheckpointAction::WriteSnapshot,
				"runtime_limit_snapshot"
			};
		}

		return {
			FinalCheckpointAction::WriteSnapshot,
			"final_snapshot"
		};
	}

	const char* startup_memory_gate_policy_name( StartupMemoryGatePolicy policy ) noexcept
	{
		switch ( policy )
		{
		case StartupMemoryGatePolicy::AdvisoryOnly:
			return "advisory_only";
		case StartupMemoryGatePolicy::EnforceReject:
		default:
			return "enforce_reject";
		}
	}

	StartupMemoryGatePolicy startup_memory_gate_policy_for_strict_search( bool strict_search_mode ) noexcept
	{
		return strict_search_mode ? StartupMemoryGatePolicy::AdvisoryOnly : StartupMemoryGatePolicy::EnforceReject;
	}

	StartupMemoryGateDecision decide_startup_memory_gate(
		const TwilightDream::runtime_component::MemoryGateEvaluation& evaluation,
		bool														 allow_high_memory_usage,
		bool														 strict_search_mode ) noexcept
	{
		StartupMemoryGateDecision decision {};
		decision.policy = startup_memory_gate_policy_for_strict_search( strict_search_mode );
		decision.override_used =
			decision.policy == StartupMemoryGatePolicy::EnforceReject &&
			evaluation.status == TwilightDream::runtime_component::MemoryGateStatus::Reject &&
			allow_high_memory_usage;
		if ( decision.policy == StartupMemoryGatePolicy::AdvisoryOnly )
		{
			decision.allow_start = true;
			return decision;
		}
		decision.allow_start =
			evaluation.status != TwilightDream::runtime_component::MemoryGateStatus::Reject ||
			allow_high_memory_usage;
		return decision;
	}

	bool print_and_enforce_startup_memory_gate(
		const char*													 prefix,
		const TwilightDream::runtime_component::MemoryGateEvaluation& evaluation,
		bool														 allow_high_memory_usage,
		bool														 strict_search_mode )
	{
		const StartupMemoryGateDecision decision =
			decide_startup_memory_gate( evaluation, allow_high_memory_usage, strict_search_mode );

		{
			TwilightDream::runtime_component::IosStateGuard g( std::cout );
			std::cout << prefix
					  << "physical_available_gib=" << std::fixed << std::setprecision( 2 ) << TwilightDream::runtime_component::bytes_to_gibibytes( evaluation.physical_available_bytes )
					  << "  estimated_must_live_gib=" << TwilightDream::runtime_component::bytes_to_gibibytes( evaluation.must_live_bytes )
					  << "  estimated_optional_rebuildable_gib=" << TwilightDream::runtime_component::bytes_to_gibibytes( evaluation.optional_rebuildable_bytes )
					  << "  memory_gate=" << TwilightDream::runtime_component::memory_gate_status_name( evaluation.status )
					  << "  startup_memory_gate_policy=" << startup_memory_gate_policy_name( decision.policy );
			if ( decision.override_used )
				std::cout << "  [override=allow_high_memory_usage]";
			std::cout << "\n";
		}

		if ( evaluation.status == TwilightDream::runtime_component::MemoryGateStatus::Warn )
		{
			std::cout << prefix << "WARNING: estimated must-live memory is above 80% of available physical RAM.\n";
		}

		if ( evaluation.status == TwilightDream::runtime_component::MemoryGateStatus::Reject &&
			 decision.policy == StartupMemoryGatePolicy::AdvisoryOnly )
		{
			std::cout << prefix << "WARNING: estimated must-live memory is above 95% of available physical RAM.\n";
			std::cout << prefix << "WARNING: strict search keeps the startup gate advisory-only; the runtime physical-memory guard will block further growth allocations if real usage reaches the danger threshold.\n";
			return true;
		}

		if ( evaluation.status == TwilightDream::runtime_component::MemoryGateStatus::Reject && !decision.allow_start )
		{
			std::cerr << prefix << "ERROR: estimated must-live memory is above 95% of available physical RAM.\n";
			std::cerr << prefix << "ERROR: rerun with --allow-high-memory-usage if you want to override this safety gate.\n";
			return false;
		}

		return true;
	}

	TwilightDream::runtime_component::SearchRuntimeControls resolve_resume_runtime_controls(
		const TwilightDream::runtime_component::SearchRuntimeControls& requested,
		const StoredRuntimeMetadata& stored,
		const RuntimeControlOverrideMask& overrides ) noexcept
	{
		TwilightDream::runtime_component::SearchRuntimeControls resolved = requested;
		if ( !overrides.maximum_search_nodes )
			resolved.maximum_search_nodes = stored.maximum_search_nodes;
		if ( !overrides.maximum_search_seconds )
			resolved.maximum_search_seconds = stored.maximum_search_seconds;
		if ( !overrides.progress_every_seconds )
			resolved.progress_every_seconds = stored.progress_every_seconds;
		if ( !overrides.checkpoint_every_seconds )
			resolved.checkpoint_every_seconds = stored.checkpoint_every_seconds;
		return resolved;
	}

	const char* resume_runtime_budget_scope_name() noexcept
	{
		return TwilightDream::runtime_component::runtime_time_limit_scope_name(
			TwilightDream::runtime_component::runtime_time_limit_scope() );
	}

	ResumeRuntimePlan build_resume_runtime_plan(
		const TwilightDream::runtime_component::SearchRuntimeControls& requested,
		const StoredRuntimeMetadata& stored,
		const RuntimeControlOverrideMask& overrides,
		std::uint64_t total_nodes_visited,
		std::uint64_t accumulated_elapsed_usec ) noexcept
	{
		ResumeRuntimePlan plan {};
		plan.runtime_controls = resolve_resume_runtime_controls( requested, stored, overrides );
		plan.total_nodes_visited = total_nodes_visited;
		plan.accumulated_elapsed_usec = accumulated_elapsed_usec;
		return plan;
	}
}
