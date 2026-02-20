#include "common/runtime_component.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace std::chrono_literals;
	using TwilightDream::runtime_component::NamedServiceThread;
	using TwilightDream::runtime_component::RuntimeAsyncJobStatus;
	using TwilightDream::runtime_component::RuntimeTaskContext;
	using TwilightDream::runtime_component::RuntimeTaskGroupHandle;
	using TwilightDream::runtime_component::RuntimeWorkerSnapshot;
	using TwilightDream::runtime_component::run_named_worker_group;
	using TwilightDream::runtime_component::snapshot_runtime_thread_pool_workers;
	using TwilightDream::runtime_component::start_named_worker_group;
	using TwilightDream::runtime_component::submit_named_async_job;

	[[noreturn]] void fail( const std::string& message )
	{
		throw std::runtime_error( message );
	}

	void require_true( bool condition, const std::string& message )
	{
		if ( !condition )
			fail( message );
	}

	bool starts_with( const std::string& value, const std::string& prefix )
	{
		return value.rfind( prefix, 0 ) == 0;
	}

	template <class Predicate>
	void wait_until( Predicate&& predicate, std::chrono::milliseconds timeout, const std::string& message )
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while ( std::chrono::steady_clock::now() < deadline )
		{
			if ( predicate() )
				return;
			std::this_thread::sleep_for( 10ms );
		}
		fail( message );
	}

	void test_dynamic_expansion()
	{
		const std::size_t before = snapshot_runtime_thread_pool_workers().size();

		auto first = start_named_worker_group(
			"runtime-selftest-expand-a",
			1,
			[]( RuntimeTaskContext& ) {
				std::this_thread::sleep_for( 30ms );
			} );
		first.wait();
		const std::size_t after_first = snapshot_runtime_thread_pool_workers().size();
		require_true( after_first >= std::max<std::size_t>( before, 1u ), "thread pool did not create the first worker" );

		const int target_workers = int( after_first + 2 );
		auto second = start_named_worker_group(
			"runtime-selftest-expand-b",
			target_workers,
			[]( RuntimeTaskContext& ) {
				std::this_thread::sleep_for( 30ms );
			} );
		second.wait();

		const std::size_t after_second = snapshot_runtime_thread_pool_workers().size();
		require_true(
			after_second >= std::size_t( target_workers ),
			"thread pool did not expand to satisfy a larger worker request" );
	}

	void test_basic_parallel_execution_and_idle_names()
	{
		std::atomic<int> started { 0 };
		std::mutex		name_mutex {};
		std::vector<std::string> running_names {};

		auto handle = start_named_worker_group(
			"runtime-selftest-basic",
			4,
			[ & ]( RuntimeTaskContext& context ) {
				require_true( starts_with( context.current_name(), "runtime-selftest-basic-g" ), "worker running name does not contain the group prefix" );
				{
					std::scoped_lock lk( name_mutex );
					running_names.push_back( context.current_name() );
				}
				started.fetch_add( 1, std::memory_order_relaxed );
				std::this_thread::sleep_for( 40ms );
			} );
		handle.wait();

		require_true( started.load( std::memory_order_relaxed ) == 4, "not all worker slots executed" );
		require_true( handle.completed_count() == 4, "completed_count() did not reach the worker count" );
		require_true( running_names.size() == 4, "running worker names were not captured for every slot" );

		const auto snapshots = handle.snapshot_workers();
		require_true( snapshots.size() == 4, "snapshot_workers() did not return one snapshot per worker slot" );
		for ( const RuntimeWorkerSnapshot& snapshot : snapshots )
		{
			require_true( snapshot.thread_id != std::thread::id {}, "worker snapshot is missing a thread id" );
			require_true( starts_with( snapshot.current_name, "runtime-idle-t" ), "worker did not return to an idle runtime name after completion" );
		}
	}

	void test_stop_by_name_replaces_worker()
	{
		std::atomic<bool> slot0_saw_stop { false };
		std::atomic<bool> release_slot1 { false };
		RuntimeWorkerSnapshot slot0_running {};

		auto handle = start_named_worker_group(
			"runtime-selftest-stop",
			2,
			[ & ]( RuntimeTaskContext& context ) {
				if ( context.slot_index == 0 )
				{
					while ( !context.stop_requested() )
						std::this_thread::sleep_for( 10ms );
					slot0_saw_stop.store( true, std::memory_order_relaxed );
					return;
				}

				while ( !release_slot1.load( std::memory_order_relaxed ) )
					std::this_thread::sleep_for( 10ms );
			} );

		wait_until(
			[ & ]() {
				const auto snapshots = handle.snapshot_workers();
				for ( const RuntimeWorkerSnapshot& snapshot : snapshots )
				{
					if ( snapshot.slot_index == 0 && snapshot.busy && snapshot.thread_id != std::thread::id {} &&
						 starts_with( snapshot.current_name, "runtime-selftest-stop-g" ) )
					{
						slot0_running = snapshot;
						return true;
					}
				}
				return false;
			},
			1500ms,
			"timed out waiting for the named stop test worker to start" );

		require_true(
			handle.request_stop_worker_by_name( slot0_running.current_name ),
			"request_stop_worker_by_name() failed to find the running worker" );

		wait_until(
			[ & ]() { return slot0_saw_stop.load( std::memory_order_relaxed ); },
			5000ms,
			"stopped worker never observed the stop request" );

		wait_until(
			[ & ]() {
				const auto snapshots = handle.snapshot_workers();
				for ( const RuntimeWorkerSnapshot& snapshot : snapshots )
				{
					if ( snapshot.slot_index == 0 )
					{
						return
							snapshot.generation > slot0_running.generation &&
							snapshot.alive &&
							starts_with( snapshot.current_name, "runtime-idle-t" );
					}
				}
				return false;
			},
			5000ms,
			"worker generation was not incremented after stop-by-name replacement" );

		release_slot1.store( true, std::memory_order_relaxed );
		handle.wait();
	}

	void test_exception_propagation_requests_group_stop()
	{
		std::atomic<bool> slot1_saw_stop { false };

		auto handle = start_named_worker_group(
			"runtime-selftest-exception",
			2,
			[ & ]( RuntimeTaskContext& context ) {
				if ( context.slot_index == 0 )
				{
					std::this_thread::sleep_for( 50ms );
					throw std::runtime_error( "runtime-selftest-boom" );
				}

				while ( !context.stop_requested() )
					std::this_thread::sleep_for( 10ms );
				slot1_saw_stop.store( true, std::memory_order_relaxed );
			} );

		bool threw_expected = false;
		try
		{
			handle.wait();
		}
		catch ( const std::runtime_error& ex )
		{
			threw_expected = std::string( ex.what() ) == "runtime-selftest-boom";
		}

		require_true( threw_expected, "worker exception was not rethrown by RuntimeTaskGroupHandle::wait()" );
		require_true( slot1_saw_stop.load( std::memory_order_relaxed ), "group stop was not requested after a worker exception" );
	}

	void test_named_service_thread()
	{
		NamedServiceThread service {};
		std::atomic<int>   ticks { 0 };

		service.start(
			"runtime-selftest-service",
			[ & ]() {
				while ( !service.stop_requested() )
				{
					ticks.fetch_add( 1, std::memory_order_relaxed );
					std::this_thread::sleep_for( 5ms );
				}
			} );

		wait_until(
			[ & ]() {
				return service.alive() && ticks.load( std::memory_order_relaxed ) > 0;
			},
			1000ms,
			"NamedServiceThread did not become alive" );

		require_true( service.name() == "runtime-selftest-service", "NamedServiceThread::name() returned an unexpected value" );
		service.stop();
		require_true( !service.alive(), "NamedServiceThread remained alive after stop()" );
	}

	void test_async_job_result_and_status()
	{
		std::atomic<bool> observed_running { false };

		auto handle = submit_named_async_job(
			"runtime-selftest-async-success",
			[ & ]( RuntimeTaskContext& context ) -> int {
				observed_running.store( true, std::memory_order_relaxed );
				require_true( starts_with( context.current_name(), "runtime-selftest-async-success-g" ), "async job worker name did not use the expected group prefix" );
				std::this_thread::sleep_for( 40ms );
				return 42;
			} );

		wait_until(
			[ & ]() {
				const RuntimeAsyncJobStatus status = handle.status();
				return status == RuntimeAsyncJobStatus::Running || observed_running.load( std::memory_order_relaxed );
			},
			1000ms,
			"async job never transitioned to running" );

		require_true( handle.result() == 42, "async job returned an unexpected result" );
		require_true( handle.status() == RuntimeAsyncJobStatus::Completed, "async job did not finish in completed state" );
	}

	void test_async_job_cancellation()
	{
		std::atomic<bool> saw_stop_request { false };

		auto handle = submit_named_async_job(
			"runtime-selftest-async-cancel",
			[ & ]( RuntimeTaskContext& context ) {
				while ( !context.stop_requested() )
					std::this_thread::sleep_for( 10ms );
				saw_stop_request.store( true, std::memory_order_relaxed );
			} );

		wait_until(
			[ & ]() {
				return handle.status() == RuntimeAsyncJobStatus::Running;
			},
			1000ms,
			"async cancellation test job never started" );

		handle.request_stop();
		handle.wait();
		require_true( saw_stop_request.load( std::memory_order_relaxed ), "async cancellation test job never observed the stop request" );
		require_true( handle.status() == RuntimeAsyncJobStatus::Cancelled, "async cancellation test job did not end in cancelled state" );
	}

	void test_async_job_failure()
	{
		auto handle = submit_named_async_job(
			"runtime-selftest-async-failure",
			[]() -> int {
				std::this_thread::sleep_for( 20ms );
				throw std::runtime_error( "runtime-selftest-async-failure" );
			} );

		bool threw_expected = false;
		try
		{
			( void )handle.result();
		}
		catch ( const std::runtime_error& ex )
		{
			threw_expected = std::string( ex.what() ) == "runtime-selftest-async-failure";
		}

		require_true( threw_expected, "async job failure was not propagated to the handle" );
		require_true( handle.status() == RuntimeAsyncJobStatus::Failed, "async job failure did not finish in failed state" );
	}

	void run_all_tests()
	{
		test_dynamic_expansion();
		test_basic_parallel_execution_and_idle_names();
		test_stop_by_name_replaces_worker();
		test_exception_propagation_requests_group_stop();
		test_named_service_thread();
		test_async_job_result_and_status();
		test_async_job_cancellation();
		test_async_job_failure();
	}
}  // namespace

int main()
{
	try
	{
		run_all_tests();
		std::cout << "[runtime-thread-pool] OK\n";
		return 0;
	}
	catch ( const std::exception& ex )
	{
		std::cerr << "[runtime-thread-pool] FAIL: " << ex.what() << "\n";
		return 1;
	}
}
