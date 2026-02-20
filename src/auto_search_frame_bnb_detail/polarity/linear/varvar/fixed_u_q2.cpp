#include <cstdint>
#include <vector>

#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "arx_analysis_operators/linear_correlation/weight_evaluation_ccz.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		struct VarVarStepStageBinding
		{
			std::uint32_t fixed_output_mask_u = 0;
			SearchWeight weight_cap = 0;
			AddVarVarSplit8Enumerator32::StreamingCursor* split8_stream_cursor = nullptr;
			WeightSlicedClatStreamingCursor* weight_sliced_stream_cursor = nullptr;
			std::vector<AddCandidate>* materialized_candidates = nullptr;
			std::size_t* candidate_index = nullptr;
		};

		[[nodiscard]] VarVarStepStageBinding bind_varvar_step_stage(
			LinearRoundSearchState& state,
			LinearVarVarStageSlot slot ) noexcept
		{
			switch ( slot )
			{
			case LinearVarVarStageSlot::FirstAdd:
				return VarVarStepStageBinding {
					state.output_branch_b_mask_after_first_addition,
					state.first_add_weight_cap,
					&state.first_addition_stream_cursor,
					&state.first_addition_weight_sliced_clat_stream_cursor,
					&state.first_addition_candidates_for_branch_b,
					&state.first_addition_candidate_index };
			case LinearVarVarStageSlot::SecondAdd:
			default:
				return VarVarStepStageBinding {
					state.output_branch_a_mask_after_second_addition,
					state.second_add_weight_cap,
					&state.second_addition_stream_cursor,
					&state.second_addition_weight_sliced_clat_stream_cursor,
					&state.second_addition_candidates_storage,
					&state.second_addition_candidate_index };
			}
		}

		[[nodiscard]] bool use_split8_streaming_add_cursor(
			const LinearBestSearchConfiguration& configuration ) noexcept
		{
			const auto contract = linear_varvar_row_q2_contract( configuration );
			return configuration.search_mode == SearchMode::Strict &&
				   contract.kind == LinearVarVarRowQ2ContractKind::Split8ExactPath;
		}

		[[nodiscard]] bool use_weight_sliced_clat_streaming_add_cursor(
			const LinearBestSearchConfiguration& configuration ) noexcept
		{
			const auto contract = linear_varvar_row_q2_contract( configuration );
			return configuration.search_mode == SearchMode::Strict &&
				   contract.kind == LinearVarVarRowQ2ContractKind::WeightSlicedClatExactShellIndex;
		}
	}  // namespace

	AddCandidate add_candidate_phi2_row_max( std::uint32_t output_mask_u, int n ) noexcept
	{
		using ::TwilightDream::arx_operators::linear_correlation_add_phi2_row_max;
		using ::TwilightDream::arx_operators::mask_n;

		const std::uint64_t mask = mask_n( n );
		const auto row =
			linear_correlation_add_phi2_row_max(
				static_cast<std::uint64_t>( output_mask_u ),
				n );
		return AddCandidate {
			static_cast<std::uint32_t>( row.v & mask ),
			static_cast<std::uint32_t>( row.w & mask ),
			row.z_weight };
	}

	void reset_weight_sliced_clat_streaming_cursor(
		WeightSlicedClatStreamingCursor& cursor,
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested )
	{
		AddVarVarSplit8Enumerator32::reset_streaming_cursor(
			cursor,
			output_mask_u,
			weight_cap_requested );
	}

	bool next_weight_sliced_clat_streaming_candidate(
		WeightSlicedClatStreamingCursor& cursor,
		AddCandidate& out_candidate )
	{
		return AddVarVarSplit8Enumerator32::next_streaming_candidate(
			cursor,
			out_candidate );
	}

	void materialize_varvar_row_q2_candidates_for_output_mask_u(
		std::vector<AddCandidate>& out_candidates,
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested,
		const LinearBestSearchConfiguration& configuration )
	{
		const LinearVarVarRowQ2Contract contract =
			linear_varvar_row_q2_contract( configuration );
		if ( configuration.search_mode == SearchMode::Strict &&
			 contract.materialized_list_is_truncated )
		{
			out_candidates =
				AddVarVarSplit8Enumerator32::generate_add_candidates_split8_slr(
					output_mask_u,
					weight_cap_requested );
			return;
		}

		const auto& candidates =
			AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
				output_mask_u,
				weight_cap_requested,
				configuration.search_mode,
				configuration.enable_weight_sliced_clat,
				configuration.weight_sliced_clat_max_candidates );
		out_candidates.assign( candidates.begin(), candidates.end() );
		if ( contract.materialized_list_is_truncated &&
			 configuration.weight_sliced_clat_max_candidates != 0 &&
			 out_candidates.size() > configuration.weight_sliced_clat_max_candidates )
		{
			out_candidates.resize( configuration.weight_sliced_clat_max_candidates );
		}
	}

	bool prepare_varvar_step_q2(
		LinearRoundSearchState& state,
		LinearVarVarStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		const std::function<bool()>& sync_and_check_runtime_stop )
	{
		auto binding = bind_varvar_step_stage( state, slot );
		*binding.candidate_index = 0;

		if ( use_split8_streaming_add_cursor( configuration ) )
		{
			AddVarVarSplit8Enumerator32::reset_streaming_cursor(
				*binding.split8_stream_cursor,
				binding.fixed_output_mask_u,
				binding.weight_cap );
			if ( sync_and_check_runtime_stop() )
				return false;
			binding.materialized_candidates->clear();
			return true;
		}

		if ( use_weight_sliced_clat_streaming_add_cursor( configuration ) )
		{
			reset_weight_sliced_clat_streaming_cursor(
				*binding.weight_sliced_stream_cursor,
				binding.fixed_output_mask_u,
				binding.weight_cap );
			if ( sync_and_check_runtime_stop() )
				return false;
			binding.materialized_candidates->clear();
			return true;
		}

		materialize_varvar_row_q2_candidates_for_output_mask_u(
			*binding.materialized_candidates,
			binding.fixed_output_mask_u,
			binding.weight_cap,
			configuration );
		if ( sync_and_check_runtime_stop() )
			return false;
		return true;
	}

	bool next_varvar_step_q2_candidate(
		LinearRoundSearchState& state,
		LinearVarVarStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearVarVarQ2Candidate& out_candidate )
	{
		const auto binding = bind_varvar_step_stage( state, slot );
		AddCandidate raw_candidate {};

		while ( true )
		{
			bool has_candidate = false;
			if ( use_split8_streaming_add_cursor( configuration ) )
			{
				has_candidate =
					AddVarVarSplit8Enumerator32::next_streaming_candidate(
						*binding.split8_stream_cursor,
						raw_candidate );
			}
			else if ( use_weight_sliced_clat_streaming_add_cursor( configuration ) )
			{
				has_candidate =
					next_weight_sliced_clat_streaming_candidate(
						*binding.weight_sliced_stream_cursor,
						raw_candidate );
			}
			else
			{
				if ( *binding.candidate_index < binding.materialized_candidates->size() )
				{
					raw_candidate =
						( *binding.materialized_candidates )[ ( *binding.candidate_index )++ ];
					has_candidate = true;
				}
			}

			if ( !has_candidate )
				return false;

			if ( resolve_varvar_q2_candidate(
					configuration,
					binding.fixed_output_mask_u,
					binding.weight_cap,
					raw_candidate,
					out_candidate ) )
			{
				return true;
			}
		}
	}

	bool resolve_varvar_q2_candidate(
		const LinearBestSearchConfiguration& configuration,
		std::uint32_t fixed_output_mask_u,
		SearchWeight weight_cap,
		const AddCandidate& raw_candidate,
		LinearVarVarQ2Candidate& out_candidate ) noexcept
	{
		out_candidate.q1_input = raw_candidate;
		out_candidate.ordered_stream =
			linear_varvar_q2_ordered_stream_contract( configuration );
		if ( configuration.varvar_modular_add_bnb_mode ==
			 LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU )
		{
			const auto column_optimal =
				try_varvar_add_column_optimal_u_within_cap_u32(
					raw_candidate.input_mask_x,
					raw_candidate.input_mask_y,
					weight_cap,
					32 );
			if ( !column_optimal.has_value() )
				return false;
			out_candidate.sum_wire_u = column_optimal->output_mask_u;
			out_candidate.exact_weight_hint = column_optimal->linear_weight;
			return true;
		}

		out_candidate.sum_wire_u = fixed_output_mask_u;
		out_candidate.exact_weight_hint = raw_candidate.linear_weight;
		return true;
	}
}  // namespace TwilightDream::auto_search_linear
