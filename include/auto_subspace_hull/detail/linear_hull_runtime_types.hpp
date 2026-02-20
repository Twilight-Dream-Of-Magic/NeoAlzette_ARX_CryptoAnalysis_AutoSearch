#pragma once
#ifndef TWILIGHTDREAM_LINEAR_HULL_RUNTIME_TYPES_HPP
#define TWILIGHTDREAM_LINEAR_HULL_RUNTIME_TYPES_HPP

struct LinearHullCollectedTrailView
{
	SearchWeight total_weight = 0;
	std::uint32_t input_branch_a_mask = 0;
	std::uint32_t input_branch_b_mask = 0;
	long double exact_signed_correlation = 0.0L;
	const std::vector<LinearTrailStepRecord>* trail = nullptr;
};

using LinearHullTrailCallback = std::function<bool(const LinearHullCollectedTrailView&)>;

struct LinearHullShellSummary
{
	std::uint64_t trail_count = 0;
	long double aggregate_signed_correlation = 0.0L;
	long double aggregate_abs_correlation_mass = 0.0L;
	long double strongest_trail_signed_correlation = 0.0L;
	std::uint32_t strongest_input_branch_a_mask = 0;
	std::uint32_t strongest_input_branch_b_mask = 0;
	std::vector<LinearTrailStepRecord> strongest_trail{};
};

struct LinearHullAggregationResult
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
	long double aggregate_signed_correlation = 0.0L;
	long double aggregate_abs_correlation_mass = 0.0L;
	long double strongest_trail_signed_correlation = 0.0L;
	std::uint32_t strongest_input_branch_a_mask = 0;
	std::uint32_t strongest_input_branch_b_mask = 0;
	std::map<SearchWeight, LinearHullShellSummary> shell_summaries{};
	std::vector<LinearTrailStepRecord> strongest_trail{};
};

enum class HullCollectCapMode : std::uint8_t
{
	BestWeightPlusWindow = 0,
	ExplicitCap = 1
};

enum class LinearCollectorResidualBoundaryMode : std::uint8_t
{
	ContinueResidualSearch = 0,
	PruneAtResidualBoundary = 1
};

struct BestWeightHistory;
struct BinaryCheckpointManager;

struct LinearHullCollectionOptions
{
	SearchWeight collect_weight_cap = 0;
	std::uint64_t maximum_collected_trails = 0;
	LinearBestSearchRuntimeControls runtime_controls{};
	LinearCollectorResidualBoundaryMode residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
	LinearHullTrailCallback on_trail{};
};

struct LinearStrictHullRuntimeOptions
{
	HullCollectCapMode collect_cap_mode = HullCollectCapMode::BestWeightPlusWindow;
	SearchWeight explicit_collect_weight_cap = 0;
	SearchWeight collect_weight_window = 0;
	std::uint64_t maximum_collected_trails = 0;
	LinearBestSearchRuntimeControls runtime_controls{};
	LinearCollectorResidualBoundaryMode residual_boundary_mode = LinearCollectorResidualBoundaryMode::ContinueResidualSearch;
	LinearHullTrailCallback on_trail{};
	bool best_search_seed_present = false;
	SearchWeight best_search_seeded_upper_bound_weight = INFINITE_WEIGHT;
	std::vector<LinearTrailStepRecord> best_search_seeded_upper_bound_trail{};
	std::string best_search_resume_checkpoint_path{};
	BestWeightHistory* best_search_history_log = nullptr;
	BinaryCheckpointManager* best_search_binary_checkpoint = nullptr;
	RuntimeEventLog* best_search_runtime_log = nullptr;
};

struct LinearStrictHullRuntimeResult
{
	bool best_search_executed = false;
	bool used_best_weight_reference = false;
	bool collected = false;
	SearchWeight collect_weight_cap = 0;
	MatsuiSearchRunLinearResult best_search_result{};
	LinearHullAggregationResult aggregation_result{};
	StrictCertificationFailureReason strict_runtime_rejection_reason = StrictCertificationFailureReason::None;
};

#endif
