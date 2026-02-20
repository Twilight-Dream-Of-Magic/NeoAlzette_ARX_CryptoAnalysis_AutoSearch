#pragma once
#ifndef TWILIGHTDREAM_DIFFERENTIAL_HULL_RUNTIME_API_HPP
#define TWILIGHTDREAM_DIFFERENTIAL_HULL_RUNTIME_API_HPP

struct DifferentialHullCollectorExecutionState
{
	DifferentialBestSearchContext context{};
	DifferentialHullAggregationResult aggregation_result{};
	DifferentialSearchCursor cursor{};
	SearchWeight collect_weight_cap = 0;
	std::uint64_t maximum_collected_trails = 0;
	DifferentialCollectorResidualBoundaryMode residual_boundary_mode = DifferentialCollectorResidualBoundaryMode::ContinueResidualSearch;
};

DifferentialHullAggregationResult collect_differential_hull_exact(
	std::uint32_t initial_branch_a_difference,
	std::uint32_t initial_branch_b_difference,
	const DifferentialBestSearchConfiguration& input_search_configuration,
	const DifferentialHullCollectionOptions& options = {});

void initialize_differential_hull_collection_state(
	std::uint32_t initial_branch_a_difference,
	std::uint32_t initial_branch_b_difference,
	const DifferentialBestSearchConfiguration& input_search_configuration,
	const DifferentialHullCollectionOptions& options,
	DifferentialHullCollectorExecutionState& state);

void continue_differential_hull_collection_from_state(
	DifferentialHullCollectorExecutionState& state,
	DifferentialHullTrailCallback on_trail = {},
	std::function<void()> checkpoint_hook = {});

DifferentialStrictHullRuntimeResult run_differential_strict_hull_runtime(
	std::uint32_t initial_branch_a_difference,
	std::uint32_t initial_branch_b_difference,
	const DifferentialBestSearchConfiguration& input_search_configuration,
	const DifferentialStrictHullRuntimeOptions& options = {},
	bool print_output = false,
	bool progress_print_differences = false);

#endif
