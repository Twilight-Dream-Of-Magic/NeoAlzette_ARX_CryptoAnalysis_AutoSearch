#pragma once
#ifndef TWILIGHTDREAM_LINEAR_HULL_RUNTIME_API_HPP
#define TWILIGHTDREAM_LINEAR_HULL_RUNTIME_API_HPP

struct LinearHullCollectorExecutionState
{
	LinearBestSearchContext context{};
	LinearHullAggregationResult aggregation_result{};
	LinearSearchCursor cursor{};
	SearchWeight collect_weight_cap = 0;
	std::uint64_t maximum_collected_trails = 0;
	LinearCollectorResidualBoundaryMode residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
};

void initialize_linear_hull_collection_state(
	std::uint32_t output_branch_a_mask,
	std::uint32_t output_branch_b_mask,
	const LinearBestSearchConfiguration& base_configuration,
	const LinearHullCollectionOptions& options,
	LinearHullCollectorExecutionState& state);

void continue_linear_hull_collection_from_state(
	LinearHullCollectorExecutionState& state,
	LinearHullTrailCallback on_trail = {},
	std::function<void()> checkpoint_hook = {});

LinearStrictHullRuntimeResult run_linear_strict_hull_runtime(
	std::uint32_t output_branch_a_mask,
	std::uint32_t output_branch_b_mask,
	const LinearBestSearchConfiguration& base_configuration,
	const LinearStrictHullRuntimeOptions& options = {},
	bool print_output = false,
	bool progress_print_masks = false);

#endif
