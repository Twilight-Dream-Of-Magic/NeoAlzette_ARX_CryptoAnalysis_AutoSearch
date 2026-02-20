#pragma once
#ifndef TWILIGHTDREAM_DIFFERENTIAL_HULL_RUNTIME_TYPES_HPP
#define TWILIGHTDREAM_DIFFERENTIAL_HULL_RUNTIME_TYPES_HPP

struct DifferentialHullCollectedTrailView
{
	SearchWeight total_weight = 0;
	std::uint32_t output_branch_a_difference = 0;
	std::uint32_t output_branch_b_difference = 0;
	long double exact_probability = 0.0L;
	const std::vector<DifferentialTrailStepRecord>* trail = nullptr;
};

using DifferentialHullTrailCallback = std::function<bool(const DifferentialHullCollectedTrailView&)>;

struct DifferentialHullShellSummary
{
	std::uint64_t trail_count = 0;
	long double aggregate_probability = 0.0L;
	long double strongest_trail_probability = 0.0L;
	std::uint32_t strongest_output_branch_a_difference = 0;
	std::uint32_t strongest_output_branch_b_difference = 0;
	std::vector<DifferentialTrailStepRecord> strongest_trail{};
};

struct DifferentialHullAggregationResult
{
	bool found_any = false;
	std::uint64_t nodes_visited = 0;
	std::uint64_t collected_trail_count = 0;
	bool hit_maximum_search_nodes = false;
	bool hit_time_limit = false;
	bool hit_collection_limit = false;
	bool hit_callback_stop = false;
	bool used_non_strict_branch_cap = false;
	bool exact_within_collect_weight_cap = false;
	bool best_weight_certified = false;
	StrictCertificationFailureReason exactness_rejection_reason = StrictCertificationFailureReason::None;
	SearchWeight collect_weight_cap = 0;
	long double aggregate_probability = 0.0L;
	long double strongest_trail_probability = 0.0L;
	std::map<SearchWeight, DifferentialHullShellSummary> shell_summaries{};
	std::vector<DifferentialTrailStepRecord> strongest_trail{};
};

enum class HullCollectCapMode : std::uint8_t
{
	BestWeightPlusWindow = 0,
	ExplicitCap = 1
};

enum class DifferentialCollectorResidualBoundaryMode : std::uint8_t
{
	ContinueResidualSearch = 0,
	PruneAtResidualBoundary = 1
};

struct BestWeightHistory;
struct BinaryCheckpointManager;

struct DifferentialHullCollectionOptions
{
	SearchWeight collect_weight_cap = 0;
	std::uint64_t maximum_collected_trails = 0;
	DifferentialBestSearchRuntimeControls runtime_controls{};
	DifferentialCollectorResidualBoundaryMode residual_boundary_mode = DifferentialCollectorResidualBoundaryMode::ContinueResidualSearch;
	DifferentialHullTrailCallback on_trail{};
};

struct DifferentialStrictHullRuntimeOptions
{
	HullCollectCapMode collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
	SearchWeight explicit_collect_weight_cap = 0;
	SearchWeight collect_weight_window = 0;
	std::uint64_t maximum_collected_trails = 0;
	DifferentialBestSearchRuntimeControls runtime_controls{};
	DifferentialCollectorResidualBoundaryMode residual_boundary_mode = DifferentialCollectorResidualBoundaryMode::ContinueResidualSearch;
	DifferentialHullTrailCallback on_trail{};
	bool best_search_seed_present = false;
	SearchWeight best_search_seeded_upper_bound_weight = INFINITE_WEIGHT;
	std::vector<DifferentialTrailStepRecord> best_search_seeded_upper_bound_trail{};
	std::string best_search_resume_checkpoint_path{};
	BestWeightHistory* best_search_history_log = nullptr;
	struct BinaryCheckpointManager* best_search_binary_checkpoint = nullptr;
	RuntimeEventLog* best_search_runtime_log = nullptr;
};

struct DifferentialStrictHullRuntimeResult
{
	bool best_search_executed = false;
	bool used_best_weight_reference = false;
	bool collected = false;
	SearchWeight collect_weight_cap = 0;
	MatsuiSearchRunDifferentialResult best_search_result{};
	DifferentialHullAggregationResult aggregation_result{};
	StrictCertificationFailureReason strict_runtime_rejection_reason = StrictCertificationFailureReason::None;
};

#endif
