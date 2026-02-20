#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "common/runtime_component.hpp"
#include "arx_analysis_operators/DefineSearchWeight.hpp"

namespace TwilightDream::auto_search_differential
{
	using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;
	using TwilightDream::AutoSearchFrameDefine::display_search_weight;
	using TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;
	using TwilightDream::AutoSearchFrameDefine::MAX_FINITE_SEARCH_WEIGHT;
	using TwilightDream::AutoSearchFrameDefine::remaining_search_weight_budget;
	using TwilightDream::AutoSearchFrameDefine::saturating_add_search_weight;
	using TwilightDream::AutoSearchFrameDefine::successor_search_weight;

	using TwilightDream::runtime_component::budgeted_reserve_target;
	using TwilightDream::runtime_component::compute_initial_cache_reserve_hint;
	using TwilightDream::runtime_component::compute_initial_memo_reserve_hint;
	using TwilightDream::runtime_component::compute_next_cache_reserve_hint;
	using TwilightDream::runtime_component::format_local_time_now;
	using TwilightDream::runtime_component::hex8;
	using TwilightDream::runtime_component::RuntimeEventLog;
	using TwilightDream::runtime_component::RuntimeInvocationState;
	using TwilightDream::runtime_component::append_timestamp_to_artifact_path;
	using TwilightDream::runtime_component::bytes_to_gibibytes;
	using TwilightDream::runtime_component::make_unique_timestamped_artifact_path;
	using TwilightDream::runtime_component::memory_gate_status_name;
	using TwilightDream::runtime_component::memory_governor_enable_for_run;
	using TwilightDream::runtime_component::memory_governor_disable_for_run;
	using TwilightDream::runtime_component::memory_governor_in_pressure;
	using TwilightDream::runtime_component::memory_governor_poll_if_needed;
	using TwilightDream::runtime_component::memory_governor_set_poll_fn;
	using TwilightDream::runtime_component::memory_governor_update_from_system_sample;
	using TwilightDream::runtime_component::physical_memory_allocation_guard_active;
	using TwilightDream::runtime_component::pmr_bounded_resource;
	using TwilightDream::runtime_component::pmr_configure_for_run;
	using TwilightDream::runtime_component::pmr_report_oom_once;
	using TwilightDream::runtime_component::pmr_run_epoch;
	using TwilightDream::runtime_component::pmr_suggest_limit_bytes;
	using TwilightDream::runtime_component::print_word32_hex;
	using TwilightDream::runtime_component::round_up_power_of_two;
	using TwilightDream::runtime_component::runtime_elapsed_microseconds;
	using TwilightDream::runtime_component::runtime_elapsed_seconds;
	using TwilightDream::runtime_component::runtime_budget_mode_name;
	using TwilightDream::runtime_component::runtime_effective_maximum_search_nodes;
	using TwilightDream::runtime_component::runtime_nodes_ignored_due_to_time_limit;

	using DifferentialBestSearchRuntimeControls = TwilightDream::runtime_component::SearchRuntimeControls;
	using TwilightDream::runtime_component::MemoryGateStatus;
	using TwilightDream::runtime_component::SearchInvocationMetadata;
	using TwilightDream::runtime_component::ScopedRuntimeTimeLimitProbe;
	using TwilightDream::runtime_component::begin_runtime_invocation;
	using TwilightDream::runtime_component::poll_runtime_stop_after_node_visit;
	using TwilightDream::runtime_component::recommended_progress_node_mask_for_time_limit;
	using TwilightDream::runtime_component::runtime_maximum_search_nodes_hit;
	using TwilightDream::runtime_component::runtime_note_node_visit;
	using TwilightDream::runtime_component::runtime_poll;
	using TwilightDream::runtime_component::runtime_time_limit_hit;
}
