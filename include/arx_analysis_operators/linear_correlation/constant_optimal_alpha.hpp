#pragma once
/**
 * @file linear_correlation/constant_optimal_alpha.hpp
 * @brief Fixed-beta public declarations for exact var-const Q2 linear correlation.
 *
 * All implementation lives in
 * `src/arx_analysis_operators/linear_correlation/constant_fixed_beta_core.cpp`.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "../SignedInteger128Bit.hpp"
#include "../UnsignedInteger128Bit.hpp"

namespace TwilightDream
{
	namespace arx_operators
	{
		using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;

#ifndef TWILIGHTDREAM_ARX_LINEAR_CONSTANT_Q2_SHARED_DECLS
#define TWILIGHTDREAM_ARX_LINEAR_CONSTANT_Q2_SHARED_DECLS
		enum class VarConstQ2Direction : std::uint8_t
		{
			fixed_alpha_to_beta,
			fixed_beta_to_alpha,
		};

		enum class VarConstQ2Operation : std::uint8_t
		{
			modular_add,
			modular_sub,
		};

		enum class VarConstQ2MainlineMethod : std::uint8_t
		{
			projective_support_summary,
		};

		struct VarConstQ2MainlineStats
		{
			std::size_t memo_states { 0 };
			std::size_t queue_pops { 0 };
			std::size_t queue_pushes { 0 };
			std::size_t segment_root_entries { 0 };
			std::size_t segment_max_entries { 0 };
			std::size_t segment_combine_pairs { 0 };
			std::size_t segment_raw_block_count { 0 };
			std::size_t segment_powered_leaf_count { 0 };
			std::size_t segment_power_runs { 0 };
			std::size_t segment_power_collapsed_blocks { 0 };
			std::size_t segment_descriptor_tree_leaf_classes { 0 };
			std::size_t segment_descriptor_tree_unique_leaf_nodes { 0 };
			std::size_t segment_descriptor_tree_unique_nodes { 0 };
			std::size_t segment_descriptor_tree_reuse_hits { 0 };
			std::size_t support_root_entries { 0 };
			std::size_t support_max_entries { 0 };
			std::size_t support_frontier_peak_entries { 0 };
			std::size_t support_frontier_transitions { 0 };
			std::size_t recursive_total_frontier_entries { 0 };
			std::size_t recursive_max_frontier_entries { 0 };
		};

		struct VarConstQ2MainlineRequest
		{
			std::uint64_t fixed_mask { 0 };
			std::uint64_t constant { 0 };
			int n { 0 };
			VarConstQ2Direction direction {};
			VarConstQ2Operation operation {};
		};

		struct VarConstQ2MainlineResult
		{
			std::uint64_t optimal_mask { 0 };
			std::uint64_t abs_weight { 0 };
			bool abs_weight_is_exact_2pow64 { false };
			double abs_correlation { 0.0 };
			SearchWeight ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

		struct VarConstOptimalInputMaskResult
		{
			std::uint64_t alpha { 0 };
			std::uint64_t abs_weight { 0 };
			bool abs_weight_is_exact_2pow64 { false };
			double abs_correlation { 0.0 };
			SearchWeight ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

		struct VarConstOptimalOutputMaskResult
		{
			std::uint64_t beta { 0 };
			std::uint64_t abs_weight { 0 };
			bool abs_weight_is_exact_2pow64 { false };
			double abs_correlation { 0.0 };
			SearchWeight ceil_linear_weight_int { TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT };
		};

		namespace detail_varconst_carry_dp
		{
			using uint128_t = UnsignedInteger128Bit;
			using CarryWide = SignedInteger128Bit;

			struct ExactAbsWeight
			{
				std::uint64_t packed_weight { 0 };
				bool is_exact_2pow64 { false };
			};

			struct VarConstLayerDpSummary
			{
				std::uint64_t optimal_mask { 0 };
				ExactAbsWeight abs_weight {};
			};

			struct BitMatrix
			{
				int m00 { 0 };
				int m01 { 0 };
				int m10 { 0 };
				int m11 { 0 };
			};

			struct VarConstQ2BitModel
			{
				std::array<BitMatrix, 64> choose_mask0 {};
				std::array<BitMatrix, 64> choose_mask1 {};
				int n { 0 };
			};

			struct DirectionKey
			{
				CarryWide x { 0 };
				CarryWide y { 0 };

				[[nodiscard]] bool operator==( const DirectionKey& other ) const noexcept
				{
					return x == other.x && y == other.y;
				}
			};

			struct DirectionKeyHash
			{
				[[nodiscard]] std::size_t operator()( const DirectionKey& key ) const noexcept;
			};

			struct ProjectiveMatrixKey
			{
				CarryWide m00 { 0 };
				CarryWide m01 { 0 };
				CarryWide m10 { 0 };
				CarryWide m11 { 0 };

				[[nodiscard]] bool operator==( const ProjectiveMatrixKey& other ) const noexcept
				{
					return m00 == other.m00 && m01 == other.m01 &&
						   m10 == other.m10 && m11 == other.m11;
				}
			};

			struct ProjectiveMatrixKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveMatrixKey& key ) const noexcept;
			};

			struct ProjectiveMatrixSummaryValue
			{
				uint128_t scale { 0 };
				std::uint64_t mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveMatrixSummaryValue& other ) const noexcept
				{
					return scale == other.scale && mask == other.mask;
				}
			};

			struct ProjectiveSegmentSummary
			{
				int bit_length { 0 };
				std::unordered_map<ProjectiveMatrixKey, ProjectiveMatrixSummaryValue, ProjectiveMatrixKeyHash> table {};
			};

			struct ProjectiveSegmentSummaryStats
			{
				std::size_t root_entries { 0 };
				std::size_t max_segment_entries { 0 };
				std::size_t combine_pairs { 0 };
			};

			struct ProjectiveRootQueryValue
			{
				uint128_t scale { 0 };
				std::uint64_t mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootQueryValue& other ) const noexcept
				{
					return scale == other.scale && mask == other.mask;
				}
			};

			struct ProjectiveRootQuerySummary
			{
				int bit_length { 0 };
				std::unordered_map<DirectionKey, ProjectiveRootQueryValue, DirectionKeyHash> table {};
			};

			enum class ProjectiveSlopeKind : std::uint8_t
			{
				finite,
				infinity,
			};

			struct ProjectiveSlopeKey
			{
				ProjectiveSlopeKind kind { ProjectiveSlopeKind::finite };
				CarryWide num { 0 };
				CarryWide den { 1 };

				[[nodiscard]] bool operator==( const ProjectiveSlopeKey& other ) const noexcept
				{
					return kind == other.kind && num == other.num && den == other.den;
				}
			};

			struct ProjectiveSlopeKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveSlopeKey& key ) const noexcept;
			};

			struct ProjectiveRootSlopeRelativeProfileValue
			{
				uint128_t relative_scale { 0 };
				std::uint64_t relative_mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootSlopeRelativeProfileValue& other ) const noexcept
				{
					return relative_scale == other.relative_scale &&
						   relative_mask == other.relative_mask;
				}
			};

			struct ProjectiveRootSlopeSummary
			{
				int bit_length { 0 };
				std::unordered_map<ProjectiveSlopeKey, ProjectiveRootQueryValue, ProjectiveSlopeKeyHash> table {};
			};

			struct ProjectiveRootSlopeRelativeProfileSummary
			{
				int bit_length { 0 };
				std::unordered_map<ProjectiveSlopeKey, ProjectiveRootSlopeRelativeProfileValue, ProjectiveSlopeKeyHash> table {};
			};

			struct ProjectiveRootSlopeSyncCompetitionKey
			{
				int run_bit { 0 };
				ProjectiveSlopeKey child_slope {};
				uint128_t relative_scale { 0 };
				std::uint64_t shifted_suffix_mask { 0 };

				[[nodiscard]] bool operator==( const ProjectiveRootSlopeSyncCompetitionKey& other ) const noexcept
				{
					return run_bit == other.run_bit &&
						   child_slope == other.child_slope &&
						   relative_scale == other.relative_scale &&
						   shifted_suffix_mask == other.shifted_suffix_mask;
				}
			};

			struct ProjectiveRootSlopeSyncCompetitionKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveRootSlopeSyncCompetitionKey& key ) const noexcept;
			};

			using ProjectiveRootSlopeSyncCompetitionGroupMap =
				std::unordered_map<ProjectiveRootSlopeSyncCompetitionKey, std::vector<std::uint64_t>, ProjectiveRootSlopeSyncCompetitionKeyHash>;

			struct ProjectiveRootSlopeSyncWinnerPathSummary
			{
				std::uint64_t winner_relative_mask { 0 };
				std::uint8_t query_depth { 0 };
				std::uint64_t winner_trace_word { 0 };
				std::uint64_t queried_bit_support { 0 };
			};

			struct FixedBetaFlatTheoremArtifacts
			{
				ProjectiveSegmentSummary summary {};
				ProjectiveSegmentSummaryStats segment_stats {};
				std::size_t raw_block_count { 0 };
				std::size_t powered_leaf_count { 0 };
				std::size_t power_runs { 0 };
				std::size_t power_collapsed_blocks { 0 };
				std::size_t descriptor_tree_leaf_classes { 0 };
				std::size_t descriptor_tree_unique_leaf_nodes { 0 };
				std::size_t descriptor_tree_unique_nodes { 0 };
				std::size_t descriptor_tree_reuse_hits { 0 };
			};

			struct ProjectiveSuffixChoice
			{
				ExactAbsWeight best_abs {};
				std::uint64_t best_suffix_mask { 0 };
			};

			struct ProjectiveState
			{
				DirectionKey dir {};
				uint128_t scale { 0 };
			};

			struct ProjectiveBellmanKey
			{
				int bit_index { 0 };
				DirectionKey dir {};

				[[nodiscard]] bool operator==( const ProjectiveBellmanKey& other ) const noexcept
				{
					return bit_index == other.bit_index && dir == other.dir;
				}
			};

			struct ProjectiveBellmanKeyHash
			{
				[[nodiscard]] std::size_t operator()( const ProjectiveBellmanKey& key ) const noexcept;
			};

			using ProjectiveBellmanMemo =
				std::unordered_map<ProjectiveBellmanKey, ProjectiveSuffixChoice, ProjectiveBellmanKeyHash>;

#ifndef TWILIGHTDREAM_ARX_LINEAR_CONSTANT_Q2_SKIP_DETAIL_FUNCTION_DECLS
			[[nodiscard]] std::uint64_t mask_n( int n ) noexcept;
			[[nodiscard]] std::uint64_t normalize_add_constant_for_q2(
				VarConstQ2Operation operation,
				std::uint64_t constant,
				int n ) noexcept;
			[[nodiscard]] ExactAbsWeight exact_abs_sum_v0_v1(
				CarryWide v0,
				CarryWide v1 ) noexcept;
			[[nodiscard]] std::pair<DirectionKey, uint128_t> normalize_projective_direction(
				CarryWide v0,
				CarryWide v1 ) noexcept;
			[[nodiscard]] std::uint64_t projective_root_slope_anchor_mask(
				const ProjectiveRootSlopeSummary& summary ) noexcept;
			[[nodiscard]] bool exact_abs_weight_is_zero( const ExactAbsWeight& value ) noexcept;
			[[nodiscard]] bool exact_abs_weight_equal( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept;
			[[nodiscard]] bool exact_abs_weight_less( const ExactAbsWeight& lhs, const ExactAbsWeight& rhs ) noexcept;
			[[nodiscard]] double exact_abs_weight_to_correlation( const ExactAbsWeight& value, int n ) noexcept;
			[[nodiscard]] SearchWeight exact_abs_weight_to_ceil_linear_weight_int( const ExactAbsWeight& value, int n ) noexcept;
			[[nodiscard]] BitMatrix build_matrix_for_beta_bit(
				int alpha_i,
				int constant_i,
				int beta_i ) noexcept;
			[[nodiscard]] ProjectiveState apply_projective_step(
				const BitMatrix& matrix,
				const DirectionKey& dir ) noexcept;
			[[nodiscard]] ExactAbsWeight scale_exact_abs_weight_projective(
				const ExactAbsWeight& weight,
				uint128_t scale ) noexcept;
			[[nodiscard]] ProjectiveSuffixChoice solve_projective_bellman_suffix_choice(
				int bit_index,
				const DirectionKey& dir,
				const std::array<BitMatrix, 64>& ordered_mats_alpha0,
				const std::array<BitMatrix, 64>& ordered_mats_alpha1,
				int nbits,
				ProjectiveBellmanMemo& memo ) noexcept;
			[[nodiscard]] VarConstQ2MainlineResult layer_dp_summary_to_q2_mainline(
				const VarConstLayerDpSummary& summary,
				int n ) noexcept;
			[[nodiscard]] VarConstOptimalInputMaskResult q2_mainline_to_alpha_result(
				const VarConstQ2MainlineResult& result ) noexcept;
			[[nodiscard]] VarConstOptimalOutputMaskResult q2_mainline_to_beta_result(
				const VarConstQ2MainlineResult& result ) noexcept;
			[[nodiscard]] ExactAbsWeight evaluate_fixed_beta_exact_abs_weight(
				std::uint64_t alpha,
				std::uint64_t add_constant,
				std::uint64_t beta,
				int n ) noexcept;
			[[nodiscard]] ExactAbsWeight evaluate_exact_abs_weight_for_alpha_beta(
				std::uint64_t alpha,
				std::uint64_t add_constant,
				std::uint64_t beta,
				int n ) noexcept;
			[[nodiscard]] VarConstQ2BitModel build_q2_bit_model_from_request(
				const VarConstQ2MainlineRequest& request ) noexcept;
			[[nodiscard]] VarConstLayerDpSummary run_raw_carry_layer_dp_from_model(
				const VarConstQ2BitModel& model ) noexcept;
			[[nodiscard]] VarConstLayerDpSummary summarize_projective_segment_summary_best(
				const ProjectiveSegmentSummary& summary ) noexcept;
			[[nodiscard]] VarConstLayerDpSummary run_q2_mainline_from_model(
				const VarConstQ2BitModel& model,
				VarConstQ2MainlineMethod method,
				VarConstQ2MainlineStats* stats = nullptr ) noexcept;
			[[nodiscard]] FixedBetaFlatTheoremArtifacts build_fixed_beta_flat_theorem_artifacts(
				std::uint64_t beta,
				std::uint64_t add_constant,
				int n ) noexcept;
			void populate_fixed_beta_flat_mainline_stats(
				const FixedBetaFlatTheoremArtifacts& artifacts,
				VarConstQ2MainlineStats& stats ) noexcept;
			[[nodiscard]] ProjectiveRootSlopeSummary build_projective_root_slope_summary_from_segment_summary(
				const ProjectiveSegmentSummary& summary ) noexcept;
			[[nodiscard]] ProjectiveRootSlopeRelativeProfileSummary build_projective_root_slope_relative_profile_summary(
				const ProjectiveRootSlopeSummary& summary ) noexcept;
			[[nodiscard]] ProjectiveRootSlopeSyncCompetitionGroupMap build_projective_root_slope_sync_competition_groups(
				const ProjectiveRootSlopeRelativeProfileSummary& profile ) noexcept;
			[[nodiscard]] ProjectiveRootSlopeSyncWinnerPathSummary build_projective_root_slope_sync_winner_path_summary(
				std::vector<std::uint64_t> rel_masks,
				std::uint64_t anchor_mask ) noexcept;
			[[nodiscard]] bool try_run_support_summary_request_fastpath(
				const VarConstQ2MainlineRequest& request,
				VarConstQ2MainlineResult& out,
				VarConstQ2MainlineStats* stats = nullptr ) noexcept;
#endif
		}  // namespace detail_varconst_carry_dp
#endif

		[[nodiscard]] VarConstQ2MainlineResult solve_varconst_q2_mainline(
			const VarConstQ2MainlineRequest& request,
			VarConstQ2MainlineMethod method =
				VarConstQ2MainlineMethod::projective_support_summary,
			VarConstQ2MainlineStats* stats = nullptr ) noexcept;

		[[nodiscard]] VarConstOptimalInputMaskResult find_optimal_alpha_varconst_mod_sub(
			std::uint64_t beta,
			std::uint64_t sub_constant_C,
			int n ) noexcept;

		[[nodiscard]] VarConstOptimalInputMaskResult find_optimal_alpha_varconst_mod_add(
			std::uint64_t beta,
			std::uint64_t add_constant_K,
			int n ) noexcept;
	}  // namespace arx_operators
}  // namespace TwilightDream
