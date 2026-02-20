#pragma once
#if !defined( AUTO_SEARCH_FRAME_DETAIL_AUTO_PIPELINE_SHARED_HPP )
#define AUTO_SEARCH_FRAME_DETAIL_AUTO_PIPELINE_SHARED_HPP

#include "auto_search_frame/detail/best_search_shared_core.hpp"
#include "auto_search_frame/search_checkpoint.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace TwilightDream::auto_pipeline_shared
{
	struct RebuildableReserveGuard
	{
		void*		  ptr = nullptr;
		std::uint64_t bytes = 0;

		explicit RebuildableReserveGuard( std::uint64_t reserve_mib )
		{
			if ( reserve_mib == 0 )
				return;
			const std::uint64_t mib = 1024ull * 1024ull;
			bytes = reserve_mib * mib;
			ptr = TwilightDream::runtime_component::alloc_rebuildable( bytes, true );
			TwilightDream::runtime_component::IosStateGuard g( std::cout );
			if ( ptr )
			{
				std::cout << "[Rebuildable] reserved_gibibytes=" << std::fixed << std::setprecision( 2 )
						  << TwilightDream::runtime_component::bytes_to_gibibytes( bytes ) << "\n";
			}
			else
			{
				std::cout << "[Rebuildable] reserve_failed_mib=" << reserve_mib << "\n";
			}
		}

		~RebuildableReserveGuard()
		{
			if ( ptr )
				TwilightDream::runtime_component::release_rebuildable_pool();
		}
	};

	template <class BinaryCheckpointManagerT>
	inline void dispatch_pressure_checkpoint( BinaryCheckpointManagerT* checkpoint, void ( *checkpoint_hook )() )
	{
		if ( checkpoint_hook != nullptr )
		{
			checkpoint_hook();
			return;
		}
		if ( checkpoint != nullptr )
			checkpoint->mark_best_changed();
	}

	template <class BinaryCheckpointManagerT>
	class MemoryPressureCallbackRegistrationGuard final
	{
	public:
		MemoryPressureCallbackRegistrationGuard(
			BinaryCheckpointManagerT*& checkpoint_slot,
			void ( *&checkpoint_hook_slot )(),
			BinaryCheckpointManagerT* checkpoint,
			void ( *checkpoint_hook )(),
			void ( *checkpoint_callback )(),
			void ( *degrade_callback )() )
			: checkpoint_slot_( &checkpoint_slot )
			, checkpoint_hook_slot_( &checkpoint_hook_slot )
			, previous_checkpoint_( checkpoint_slot )
			, previous_checkpoint_hook_( checkpoint_hook_slot )
		{
			checkpoint_slot = checkpoint;
			checkpoint_hook_slot = checkpoint_hook;
			TwilightDream::runtime_component::memory_pressure_set_checkpoint_fn( checkpoint_callback );
			TwilightDream::runtime_component::memory_pressure_set_must_live_degrade_fn( degrade_callback );
		}

		MemoryPressureCallbackRegistrationGuard( const MemoryPressureCallbackRegistrationGuard& ) = delete;
		MemoryPressureCallbackRegistrationGuard& operator=( const MemoryPressureCallbackRegistrationGuard& ) = delete;

		~MemoryPressureCallbackRegistrationGuard()
		{
			TwilightDream::runtime_component::memory_pressure_set_checkpoint_fn( nullptr );
			TwilightDream::runtime_component::memory_pressure_set_must_live_degrade_fn( nullptr );
			if ( checkpoint_slot_ != nullptr )
				*checkpoint_slot_ = previous_checkpoint_;
			if ( checkpoint_hook_slot_ != nullptr )
				*checkpoint_hook_slot_ = previous_checkpoint_hook_;
		}

	private:
		BinaryCheckpointManagerT** checkpoint_slot_ = nullptr;
		void ( **checkpoint_hook_slot_ )() = nullptr;
		BinaryCheckpointManagerT* previous_checkpoint_ = nullptr;
		void ( *previous_checkpoint_hook_ )() = nullptr;
	};

	template <class ValueT>
	class ScopedValueRestore final
	{
	public:
		ScopedValueRestore( ValueT& target, const ValueT& replacement )
			: target_( &target )
			, previous_( target )
		{
			target = replacement;
		}

		ScopedValueRestore( const ScopedValueRestore& ) = delete;
		ScopedValueRestore& operator=( const ScopedValueRestore& ) = delete;

		~ScopedValueRestore()
		{
			if ( target_ != nullptr )
				*target_ = previous_;
		}

	private:
		ValueT* target_ = nullptr;
		ValueT  previous_ {};
	};

	template <class StateT, class RuntimeControlsT>
	inline void note_runtime_controls( StateT& state, const RuntimeControlsT& runtime_controls, std::uint64_t elapsed_usec ) noexcept
	{
		state.last_runtime_maximum_search_nodes = runtime_controls.maximum_search_nodes;
		state.last_runtime_maximum_search_seconds = runtime_controls.maximum_search_seconds;
		state.last_runtime_progress_every_seconds = runtime_controls.progress_every_seconds;
		state.last_runtime_checkpoint_every_seconds = runtime_controls.checkpoint_every_seconds;
		state.last_run_elapsed_usec = elapsed_usec;
	}

	template <class BinaryWriterT, class StateT>
	inline void write_runtime_metadata( BinaryWriterT& w, const StateT& state )
	{
		w.write_u64( state.last_runtime_maximum_search_nodes );
		w.write_u64( state.last_runtime_maximum_search_seconds );
		w.write_u64( state.last_runtime_progress_every_seconds );
		w.write_u64( state.last_runtime_checkpoint_every_seconds );
		w.write_u64( state.last_run_elapsed_usec );
	}

	template <class BinaryReaderT, class StateT>
	inline bool read_runtime_metadata( BinaryReaderT& r, StateT& state )
	{
		return
			r.read_u64( state.last_runtime_maximum_search_nodes ) &&
			r.read_u64( state.last_runtime_maximum_search_seconds ) &&
			r.read_u64( state.last_runtime_progress_every_seconds ) &&
			r.read_u64( state.last_runtime_checkpoint_every_seconds ) &&
			r.read_u64( state.last_run_elapsed_usec );
	}

	template <class FlagContainerT>
	inline std::size_t count_completed_flags( const FlagContainerT& flags )
	{
		return std::count_if( flags.begin(), flags.end(), []( const auto& value ) { return value != 0; } );
	}

	template <class StateT, class DefaultHistoryPathFn, class DefaultRuntimePathFn>
	inline void resolve_resume_log_artifact_paths(
		const std::string& runtime_log_override,
		StateT& state,
		DefaultHistoryPathFn&& default_history_path_fn,
		DefaultRuntimePathFn&& default_runtime_path_fn )
	{
		const auto log_artifact_paths =
			TwilightDream::best_search_shared_core::resolve_log_artifact_paths(
				runtime_log_override,
				state.history_log_path,
				state.runtime_log_path,
				std::forward<DefaultHistoryPathFn>( default_history_path_fn ),
				std::forward<DefaultRuntimePathFn>( default_runtime_path_fn ) );
		state.history_log_path = log_artifact_paths.history_log_path;
		state.runtime_log_path = log_artifact_paths.runtime_log_path;
	}

	template <class StateT, class StageNameFn>
	inline void print_resume_summary(
		std::ostream& out,
		const std::string& checkpoint_path,
		const StateT& state,
		StageNameFn&& stage_name_fn )
	{
		out << "[Auto][Resume] checkpoint_path=" << checkpoint_path << "\n";
		out << "[Auto][Resume] stage=" << std::forward<StageNameFn>( stage_name_fn )( state.stage )
			<< "  jobs=" << state.jobs.size()
			<< "  completed_jobs=" << count_completed_flags( state.completed_job_flags )
			<< "  top_candidate_count=" << state.top_candidates.size() << "\n";
	}

	struct AutoPipelinePayloadHeader
	{
		std::int32_t   round_count = 0;
		std::uint32_t  start_word_a = 0;
		std::uint32_t  start_word_b = 0;
		std::uint8_t   breadth_strategy = 0;
		std::uint64_t  auto_breadth_seed = 0;
		bool		   auto_breadth_seed_was_provided = false;
		std::uint64_t  auto_breadth_top_k = 0;
		std::uint8_t   stage = 0;
	};

	template <class BinaryWriterT, class StateT, class WriteConfigFn>
	inline bool write_payload_header(
		BinaryWriterT& w,
		const StateT& state,
		std::uint32_t start_word_a,
		std::uint32_t start_word_b,
		std::uint8_t breadth_strategy,
		std::uint8_t stage,
		WriteConfigFn&& write_config_fn )
	{
		std::forward<WriteConfigFn>( write_config_fn )();
		w.write_i32( state.round_count );
		w.write_u32( start_word_a );
		w.write_u32( start_word_b );
		w.write_u8( breadth_strategy );
		w.write_u64( state.auto_breadth_seed );
		w.write_u8( state.auto_breadth_seed_was_provided ? 1u : 0u );
		w.write_u64( static_cast<std::uint64_t>( state.auto_breadth_top_k ) );
		w.write_u8( stage );
		w.write_string( state.runtime_log_path );
		w.write_string( state.history_log_path );
		write_runtime_metadata( w, state );
		return w.ok();
	}

	template <class BinaryReaderT, class StateT, class ReadConfigFn>
	inline bool read_payload_header(
		BinaryReaderT& r,
		StateT& state,
		AutoPipelinePayloadHeader& header,
		ReadConfigFn&& read_config_fn )
	{
		std::uint8_t breadth_seed_was_provided = 0;
		if ( !std::forward<ReadConfigFn>( read_config_fn )() ) return false;
		if ( !r.read_i32( header.round_count ) ) return false;
		if ( !r.read_u32( header.start_word_a ) ) return false;
		if ( !r.read_u32( header.start_word_b ) ) return false;
		if ( !r.read_u8( header.breadth_strategy ) ) return false;
		if ( !r.read_u64( header.auto_breadth_seed ) ) return false;
		if ( !r.read_u8( breadth_seed_was_provided ) ) return false;
		if ( !r.read_u64( header.auto_breadth_top_k ) ) return false;
		if ( !r.read_u8( header.stage ) ) return false;
		if ( !r.read_string( state.runtime_log_path ) ) return false;
		if ( !r.read_string( state.history_log_path ) ) return false;
		if ( !read_runtime_metadata( r, state ) ) return false;
		header.auto_breadth_seed_was_provided = ( breadth_seed_was_provided != 0 );
		state.round_count = header.round_count;
		state.auto_breadth_seed = header.auto_breadth_seed;
		state.auto_breadth_seed_was_provided = header.auto_breadth_seed_was_provided;
		state.auto_breadth_top_k = static_cast<std::size_t>( header.auto_breadth_top_k );
		return true;
	}

	template <class BinaryWriterT, class ContainerT, class WriteElementFn>
	inline bool write_counted_container(
		BinaryWriterT& w,
		const ContainerT& container,
		WriteElementFn&& write_element_fn )
	{
		w.write_u64( static_cast<std::uint64_t>( container.size() ) );
		for ( const auto& value : container )
			std::forward<WriteElementFn>( write_element_fn )( value );
		return w.ok();
	}

	template <class BinaryReaderT, class ContainerT, class ReadElementFn>
	inline bool read_counted_container(
		BinaryReaderT& r,
		ContainerT& container,
		ReadElementFn&& read_element_fn )
	{
		std::uint64_t count = 0;
		if ( !r.read_u64( count ) )
			return false;
		container.clear();
		container.resize( static_cast<std::size_t>( count ) );
		for ( auto& value : container )
		{
			if ( !std::forward<ReadElementFn>( read_element_fn )( value ) )
				return false;
		}
		return true;
	}

	template <class BinaryWriterT, class WritePayloadFn>
	inline bool write_optional_section(
		BinaryWriterT& w,
		bool present,
		WritePayloadFn&& write_payload_fn )
	{
		w.write_u8( present ? 1u : 0u );
		if ( !present )
			return w.ok();
		return std::forward<WritePayloadFn>( write_payload_fn )();
	}

	template <class BinaryReaderT, class ReadPayloadFn>
	inline bool read_optional_section(
		BinaryReaderT& r,
		bool& present,
		ReadPayloadFn&& read_payload_fn )
	{
		std::uint8_t flag = 0;
		if ( !r.read_u8( flag ) )
			return false;
		present = ( flag != 0 );
		if ( !present )
			return true;
		return std::forward<ReadPayloadFn>( read_payload_fn )();
	}

	template <class WritePayloadFn>
	inline bool write_checkpoint_file(
		const std::string& path,
		TwilightDream::auto_search_checkpoint::SearchKind kind,
		WritePayloadFn&& write_payload_fn )
	{
		return TwilightDream::auto_search_checkpoint::write_atomic(
			path,
			[ & ]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
				if ( !TwilightDream::auto_search_checkpoint::write_header( w, kind ) )
					return false;
				return std::forward<WritePayloadFn>( write_payload_fn )( w );
			} );
	}

	template <class ReadPayloadFn>
	inline bool read_checkpoint_file(
		const std::string& path,
		TwilightDream::auto_search_checkpoint::SearchKind expected_kind,
		ReadPayloadFn&& read_payload_fn )
	{
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != expected_kind )
			return false;
		return std::forward<ReadPayloadFn>( read_payload_fn )( r );
	}

	struct AutoPipelineCheckpointSessionCore
	{
		std::string								   path {};
		std::uint64_t							   every_seconds = 0;
		std::chrono::steady_clock::time_point	   breadth_stage_start_time {};
		std::chrono::steady_clock::time_point	   last_write_time {};
		std::chrono::steady_clock::time_point	   last_periodic_write_time {};
		std::atomic<bool>						   flush_requested { false };

		bool enabled_path() const noexcept { return !path.empty(); }

		void request_flush() noexcept
		{
			flush_requested.store( true, std::memory_order_relaxed );
		}

		void begin_breadth_stage() noexcept
		{
			const auto now = std::chrono::steady_clock::now();
			breadth_stage_start_time = now;
			last_periodic_write_time = now;
			flush_requested.store( false, std::memory_order_relaxed );
		}
	};

	template <class SessionT, class StateT, class RuntimeLogPtrT>
	inline void configure_resume_checkpoint_session(
		SessionT& session,
		const std::string& checkpoint_path,
		std::uint64_t checkpoint_every_seconds,
		StateT& state,
		std::mutex& state_mutex,
		RuntimeLogPtrT runtime_log_ptr )
	{
		session.path = checkpoint_path;
		session.every_seconds = checkpoint_every_seconds;
		session.state = &state;
		session.state_mutex = &state_mutex;
		session.runtime_log = runtime_log_ptr;
	}

	template <class StateT>
	inline bool ensure_selected_candidate( StateT& state )
	{
		if ( !state.has_selected_candidate && !state.top_candidates.empty() )
		{
			state.selected_candidate = state.top_candidates.front();
			state.has_selected_candidate = true;
		}
		return state.has_selected_candidate;
	}

	template <class SessionT, class StageT, class RuntimeControlsT, class NoteRuntimeControlsFn, class WriteCheckpointFn, class EmitEventFn>
	inline bool write_stage_snapshot_to_path(
		SessionT& session,
		const std::string& checkpoint_path,
		StageT stage,
		const RuntimeControlsT& runtime_controls,
		std::uint64_t elapsed_usec,
		const char* checkpoint_reason,
		NoteRuntimeControlsFn&& note_runtime_controls,
		WriteCheckpointFn&& write_checkpoint,
		EmitEventFn&& emit_event )
	{
		if ( !session.enabled() || session.state_mutex == nullptr || checkpoint_path.empty() )
			return false;
		const auto now = std::chrono::steady_clock::now();
		std::scoped_lock lk( *session.state_mutex );
		session.state->stage = stage;
		std::forward<NoteRuntimeControlsFn>( note_runtime_controls )( runtime_controls, elapsed_usec );
		const bool ok = std::forward<WriteCheckpointFn>( write_checkpoint )( checkpoint_path );
		if ( ok )
			session.last_write_time = now;
		std::forward<EmitEventFn>( emit_event )( checkpoint_reason, checkpoint_path, ok, elapsed_usec );
		return ok;
	}

	template <class SessionT, class StageT, class RuntimeControlsT, class WriteStageSnapshotToPathFn>
	inline bool write_archive_stage_snapshot(
		SessionT& session,
		StageT stage,
		const RuntimeControlsT& runtime_controls,
		std::uint64_t elapsed_usec,
		const char* checkpoint_reason,
		WriteStageSnapshotToPathFn&& write_stage_snapshot_to_path_fn )
	{
		if ( !session.enabled() )
			return false;
		const std::string archive_path = TwilightDream::runtime_component::append_timestamp_to_artifact_path( session.path );
		if ( archive_path.empty() )
			return false;
		return std::forward<WriteStageSnapshotToPathFn>( write_stage_snapshot_to_path_fn )(
			archive_path,
			stage,
			runtime_controls,
			elapsed_usec,
			checkpoint_reason );
	}

	template <class SessionT, class RuntimeControlsT, class WriteStageSnapshotFn, class WriteArchiveStageSnapshotFn>
	inline bool maybe_write_breadth(
		SessionT& session,
		const RuntimeControlsT& runtime_controls,
		const char* reason,
		WriteStageSnapshotFn&& write_stage_snapshot_fn,
		WriteArchiveStageSnapshotFn&& write_archive_stage_snapshot_fn )
	{
		( void )runtime_controls;
		if ( !session.enabled() || session.state_mutex == nullptr )
			return false;
		const auto now = std::chrono::steady_clock::now();
		const bool due_time =
			session.every_seconds != 0 &&
			( session.last_periodic_write_time.time_since_epoch().count() == 0 ||
			  std::chrono::duration<double>( now - session.last_periodic_write_time ).count() >= double( session.every_seconds ) );
		const bool force_write = session.flush_requested.exchange( false, std::memory_order_relaxed );
		if ( reason == nullptr && !due_time && !force_write )
			return false;
		const char* checkpoint_reason = reason;
		if ( checkpoint_reason == nullptr )
			checkpoint_reason = force_write ? "pressure_flush" : "periodic_timer";
		const std::uint64_t elapsed_usec =
			TwilightDream::runtime_component::runtime_elapsed_microseconds( session.breadth_stage_start_time );
		const bool use_archive_path = ( reason == nullptr && !force_write && due_time );
		const bool ok = use_archive_path ?
			std::forward<WriteArchiveStageSnapshotFn>( write_archive_stage_snapshot_fn )( checkpoint_reason, elapsed_usec ) :
			std::forward<WriteStageSnapshotFn>( write_stage_snapshot_fn )( checkpoint_reason, elapsed_usec );
		if ( ok && use_archive_path )
			session.last_periodic_write_time = now;
		return ok;
	}

	template <class SessionT, class BinaryCheckpointManagerT, class ContextT, class CursorT, class StageT, class NoteRuntimeControlsFn, class WriteCheckpointFn, class EmitEventFn>
	inline bool write_deep_snapshot(
		SessionT& session,
		BinaryCheckpointManagerT& manager,
		const ContextT& context,
		const CursorT& cursor,
		StageT stage,
		const char* reason,
		NoteRuntimeControlsFn&& note_runtime_controls,
		WriteCheckpointFn&& write_checkpoint,
		EmitEventFn&& emit_event )
	{
		if ( !session.enabled() || session.state_mutex == nullptr )
			return false;
		const auto now = std::chrono::steady_clock::now();
		const std::uint64_t elapsed_usec = TwilightDream::best_search_shared_core::accumulated_elapsed_microseconds( context );
		const std::string checkpoint_path = manager.path.empty() ? session.path : manager.path;
		std::scoped_lock lk( *session.state_mutex );
		session.state->stage = stage;
		std::forward<NoteRuntimeControlsFn>( note_runtime_controls )( context.runtime_controls, elapsed_usec );
		const bool ok = std::forward<WriteCheckpointFn>( write_checkpoint )( checkpoint_path );
		if ( ok )
			session.last_write_time = now;
		session.flush_requested.store( false, std::memory_order_relaxed );
		std::forward<EmitEventFn>( emit_event )( reason ? reason : "deep_snapshot", checkpoint_path, ok, elapsed_usec, context, cursor );
		return ok;
	}

	template <class SessionT, class RuntimeControlsT>
	class AutoPipelineCheckpointThread final
	{
	public:
		AutoPipelineCheckpointThread() = default;
		AutoPipelineCheckpointThread( const AutoPipelineCheckpointThread& ) = delete;
		AutoPipelineCheckpointThread& operator=( const AutoPipelineCheckpointThread& ) = delete;

		~AutoPipelineCheckpointThread()
		{
			stop();
		}

		void start( SessionT* session, const RuntimeControlsT* runtime_controls )
		{
			stop();
			if ( session == nullptr || runtime_controls == nullptr || !session->enabled() )
				return;
			session_ = session;
			runtime_controls_ = runtime_controls;
			stop_requested_.store( false, std::memory_order_relaxed );
			worker_ = std::thread( [ this ] {
				while ( !stop_requested_.load( std::memory_order_relaxed ) )
				{
					session_->maybe_write_breadth( *runtime_controls_ );
					std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
				}
			} );
		}

		void stop()
		{
			stop_requested_.store( true, std::memory_order_relaxed );
			if ( worker_.joinable() )
				worker_.join();
			session_ = nullptr;
			runtime_controls_ = nullptr;
		}

	private:
		SessionT*				 session_ = nullptr;
		const RuntimeControlsT* runtime_controls_ = nullptr;
		std::atomic<bool>		 stop_requested_ { false };
		std::thread				 worker_ {};
	};

	template <class SessionT, class StateT, class RuntimeControlsT, class StageT, class ProcessJobFn, class OnJobCompletedFn>
	inline bool resume_breadth_stage(
		SessionT& session,
		StateT& state,
		const RuntimeControlsT& breadth_runtime_controls,
		const RuntimeControlsT& deep_runtime_controls,
		StageT breadth_stage,
		StageT deep_stage,
		ProcessJobFn&& process_job_fn,
		OnJobCompletedFn&& on_job_completed_fn )
	{
		session.begin_breadth_stage();
		if ( session.enabled() )
			session.write_stage_snapshot( breadth_stage, breadth_runtime_controls, 0, "resume_stage_start" );

		AutoPipelineCheckpointThread<SessionT, RuntimeControlsT> breadth_checkpoint_thread {};
		breadth_checkpoint_thread.start(
			session.enabled() ? &session : nullptr,
			session.enabled() ? &breadth_runtime_controls : nullptr );

		for ( std::size_t job_index = 0; job_index < state.jobs.size(); ++job_index )
		{
			if ( state.completed_job_flags.size() <= job_index )
				state.completed_job_flags.resize( state.jobs.size(), 0u );
			if ( state.completed_job_flags[ job_index ] != 0 )
				continue;

			process_job_fn( job_index );
			if ( session.enabled() )
				session.maybe_write_breadth( breadth_runtime_controls, "breadth_candidate_completed" );
			on_job_completed_fn( job_index );
		}

		breadth_checkpoint_thread.stop();
		if ( state.top_candidates.empty() )
			return false;

		state.selected_candidate = state.top_candidates.front();
		state.has_selected_candidate = true;
		state.stage = deep_stage;
		if ( session.enabled() )
			session.write_stage_snapshot( deep_stage, deep_runtime_controls, 0, "resume_deep_stage_selected" );
		return true;
	}

	template <class SessionT, class BinaryCheckpointManagerT, class ContextT, class CursorT>
	inline bool deep_checkpoint_override(
		BinaryCheckpointManagerT& manager,
		const ContextT& context,
		const CursorT& cursor,
		const char* reason )
	{
		auto* session = static_cast<SessionT*>( manager.write_override_user_data );
		return ( session != nullptr ) ? session->write_deep_snapshot( manager, context, cursor, reason ) : false;
	}

	template <class BinaryCheckpointManagerT, class SessionT>
	inline void trigger_auto_pipeline_pressure_checkpoint(
		BinaryCheckpointManagerT* binary_checkpoint,
		SessionT* session ) noexcept
	{
		if ( binary_checkpoint != nullptr )
			binary_checkpoint->mark_best_changed();
		if ( session != nullptr )
			session->request_flush();
	}
}

#endif
