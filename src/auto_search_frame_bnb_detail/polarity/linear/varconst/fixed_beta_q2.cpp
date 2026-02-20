#include <cstdint>
#include <optional>
#include <vector>

#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_theorem.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_strict_witness.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_hot_path.hpp"
#include "arx_analysis_operators/linear_correlation/constant_optimal_alpha.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		struct VarConstStepStageBinding
		{
			std::uint32_t output_mask_beta = 0;
			std::uint32_t constant = 0;
			SearchWeight* weight_cap = nullptr;
			SearchWeight* weight_floor = nullptr;
			SubConstStreamingCursor* streaming_cursor = nullptr;
			std::vector<SubConstCandidate>* materialized_candidates = nullptr;
			std::size_t* candidate_index = nullptr;
		};

		[[nodiscard]] VarConstStepStageBinding bind_varconst_step_stage(
			LinearRoundSearchState& state,
			LinearVarConstStageSlot slot ) noexcept
		{
			switch ( slot )
			{
			case LinearVarConstStageSlot::FirstSubconst:
				return VarConstStepStageBinding {
					state.output_branch_a_mask_after_first_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					&state.first_subconst_weight_cap,
					&state.first_subconst_weight_floor,
					&state.first_constant_subtraction_stream_cursor,
					&state.first_constant_subtraction_candidates_for_branch_a,
					&state.first_constant_subtraction_candidate_index };
			case LinearVarConstStageSlot::SecondConst:
			default:
				return VarConstStepStageBinding {
					state.output_branch_b_mask_after_second_constant_subtraction,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					&state.second_subconst_weight_cap,
					&state.second_subconst_weight_floor,
					&state.second_constant_subtraction_stream_cursor,
					&state.second_constant_subtraction_candidates_for_branch_b,
					&state.second_constant_subtraction_candidate_index };
			}
		}

		[[nodiscard]] FixedBetaSubColumnRootTheoremRequest make_fixed_beta_outer_hot_path_root_theorem_request(
			VarConstStepStageBinding stage ) noexcept
		{
			return make_fixed_beta_sub_column_root_theorem_request(
				stage.output_mask_beta,
				stage.constant );
		}

		[[nodiscard]] std::optional<FixedBetaSubColumnRootTheoremAnswer> compute_fixed_beta_outer_hot_path_root_theorem_answer(
			VarConstStepStageBinding stage ) noexcept
		{
			return try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
				make_fixed_beta_outer_hot_path_root_theorem_request( stage ),
				*stage.weight_cap );
		}

		[[nodiscard]] FixedBetaSubColumnStrictWitnessRequest make_fixed_beta_outer_hot_path_strict_witness_request(
			VarConstStepStageBinding stage ) noexcept
		{
			return make_fixed_beta_sub_column_strict_witness_request(
				stage.output_mask_beta,
				stage.constant,
				*stage.weight_cap );
		}

		void clear_fixed_beta_hot_path_rebuildable_candidates_impl(
			std::vector<SubConstCandidate>& values,
			std::size_t keep_capacity = 4096u )
		{
			values.clear();
			if ( values.capacity() > keep_capacity &&
				 TwilightDream::runtime_component::memory_governor_in_pressure() )
			{
				std::vector<SubConstCandidate> empty;
				values.swap( empty );
			}
		}

		[[nodiscard]] std::optional<SubConstCandidate> try_materialize_fixed_beta_hot_path_column_candidate_impl(
			const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor ) noexcept
		{
			if ( !column_floor.has_value() )
				return std::nullopt;
			return SubConstCandidate {
				column_floor->input_mask_alpha,
				column_floor->linear_weight };
		}

		[[nodiscard]] LinearFixedBetaOuterHotPathGate fixed_beta_q2_gate(
			const LinearBestSearchConfiguration& configuration,
			LinearFixedBetaHotPathCallSite call_site ) noexcept
		{
			return linear_configuration_fixed_beta_outer_hot_path_gate_for_call_site(
				configuration,
				call_site );
		}

		[[nodiscard]] VarConstStepStageBinding adapt_public_fixed_beta_stage_binding(
			LinearFixedBetaOuterHotPathStageBinding stage ) noexcept
		{
			return VarConstStepStageBinding {
				stage.output_mask_beta,
				stage.constant,
				&stage.weight_cap,
				&stage.weight_floor,
				&stage.streaming_cursor,
				&stage.materialized_candidates,
				nullptr };
		}
	}  // namespace

	FixedBetaSubColumnRootTheoremRequest make_fixed_beta_sub_column_root_theorem_request(
		std::uint32_t output_mask_beta,
		std::uint32_t sub_constant,
		int n_bits ) noexcept
	{
		return FixedBetaSubColumnRootTheoremRequest {
			output_mask_beta,
			sub_constant,
			n_bits };
	}

	FixedBetaSubColumnRootTheoremAnswer solve_fixed_beta_sub_column_root_theorem_u32(
		FixedBetaSubColumnRootTheoremRequest request ) noexcept
	{
		using ::TwilightDream::arx_operators::find_optimal_alpha_varconst_mod_sub;

		const auto result =
			find_optimal_alpha_varconst_mod_sub(
				static_cast<std::uint64_t>( request.output_mask_beta ),
				static_cast<std::uint64_t>( request.sub_constant ),
				request.n_bits );
		const std::uint64_t mask =
			( request.n_bits >= 64 ) ? std::uint64_t( ~0ull ) :
			( request.n_bits <= 0 ) ? 0ull :
			( ( std::uint64_t( 1 ) << request.n_bits ) - 1ull );
		return FixedBetaSubColumnRootTheoremAnswer {
			static_cast<std::uint32_t>( result.alpha & mask ),
			result.abs_weight,
			result.abs_weight_is_exact_2pow64,
			result.ceil_linear_weight_int };
	}

	std::optional<FixedBetaSubColumnRootTheoremAnswer> try_solve_fixed_beta_sub_column_root_theorem_within_cap_u32(
		FixedBetaSubColumnRootTheoremRequest request,
		SearchWeight weight_cap ) noexcept
	{
		const auto answer =
			solve_fixed_beta_sub_column_root_theorem_u32( request );
		if ( !answer.fits_within_cap( weight_cap ) )
			return std::nullopt;
		return answer;
	}

	VarConstSubColumnOptimalOnOutputWire project_fixed_beta_sub_column_root_theorem_to_output_wire(
		const FixedBetaSubColumnRootTheoremAnswer& answer ) noexcept
	{
		return VarConstSubColumnOptimalOnOutputWire {
			answer.optimal_input_mask_alpha,
			answer.ceil_linear_weight };
	}

	std::optional<VarConstSubColumnOptimalOnOutputWire> try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
		const std::optional<FixedBetaSubColumnRootTheoremAnswer>& answer ) noexcept
	{
		if ( !answer.has_value() )
			return std::nullopt;
		return project_fixed_beta_sub_column_root_theorem_to_output_wire( *answer );
	}

	std::vector<SubConstCandidate> materialize_fixed_beta_subconst_hot_path_candidates(
		LinearFixedBetaHotPathSubconstMode hot_path_mode,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap )
	{
		switch ( hot_path_mode )
		{
		case LinearFixedBetaHotPathSubconstMode::ColumnOptimalCollapsed:
			if ( const auto candidate =
					try_materialize_fixed_beta_hot_path_column_candidate_impl( column_floor );
				 candidate.has_value() )
			{
				return { *candidate };
			}
			return {};
		case LinearFixedBetaHotPathSubconstMode::StrictMaterialized:
		case LinearFixedBetaHotPathSubconstMode::StrictStreaming:
		default:
			return project_fixed_beta_sub_column_strict_witnesses_to_subconst_candidates(
				materialize_fixed_beta_sub_column_strict_witnesses(
					make_fixed_beta_sub_column_strict_witness_request(
						output_mask_beta,
						constant,
						weight_cap ) ) );
		}
	}

	std::vector<SubConstCandidate> materialize_fixed_beta_subconst_hot_path_candidates(
		const LinearFixedBetaOuterHotPathGate& hot_path_gate,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap )
	{
		return materialize_fixed_beta_subconst_hot_path_candidates(
			hot_path_gate.effective_subconst_mode,
			column_floor,
			output_mask_beta,
			constant,
			weight_cap );
	}

	std::optional<VarConstSubColumnOptimalOnOutputWire> compute_fixed_beta_outer_hot_path_column_floor(
		LinearFixedBetaOuterHotPathStageBinding stage ) noexcept
	{
		const auto adapted_stage =
			adapt_public_fixed_beta_stage_binding( stage );
		const auto candidate =
			try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
				compute_fixed_beta_outer_hot_path_root_theorem_answer( adapted_stage ) );
		stage.weight_floor =
			candidate.has_value()
				? candidate->linear_weight
				: MAX_FINITE_SEARCH_WEIGHT;
		return candidate;
	}

	SearchWeight ensure_fixed_beta_outer_hot_path_weight_floor(
		LinearFixedBetaOuterHotPathStageBinding stage ) noexcept
	{
		if ( stage.weight_floor != INFINITE_WEIGHT )
			return stage.weight_floor;
		( void )compute_fixed_beta_outer_hot_path_column_floor( stage );
		return stage.weight_floor;
	}

	std::optional<VarConstSubColumnOptimalOnOutputWire> compute_varconst_step_q2_floor(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration ) noexcept
	{
		const auto contract = linear_varconst_stage_contract( slot, configuration );
		if ( contract.outer_floor_kind != LinearVarConstOuterFloorKind::FixedBetaColumnFloor )
			return std::nullopt;

		const auto stage = bind_varconst_step_stage( state, slot );
		const auto candidate =
			try_project_fixed_beta_sub_column_root_theorem_to_output_wire(
				compute_fixed_beta_outer_hot_path_root_theorem_answer( stage ) );
		*stage.weight_floor =
			candidate.has_value() ? candidate->linear_weight : MAX_FINITE_SEARCH_WEIGHT;
		return candidate;
	}

	SearchWeight ensure_varconst_step_q2_floor(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration ) noexcept
	{
		const auto stage = bind_varconst_step_stage( state, slot );
		if ( *stage.weight_floor != INFINITE_WEIGHT )
			return *stage.weight_floor;
		( void )compute_varconst_step_q2_floor( state, slot, configuration );
		return *stage.weight_floor;
	}

	LinearFixedBetaOuterHotPathEnterStatus enter_varconst_step_q2(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		const std::function<bool()>& sync_and_check_runtime_stop )
	{
		const auto contract = linear_varconst_stage_contract( slot, configuration );
		if ( contract.outer_stream_kind != LinearVarConstOuterStreamKind::FixedBetaStrictWitness )
			return LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate;

		const auto stage = bind_varconst_step_stage( state, slot );
		const auto column_floor = compute_varconst_step_q2_floor( state, slot, configuration );
		if ( !column_floor.has_value() )
			return LinearFixedBetaOuterHotPathEnterStatus::NoFeasibleCandidate;

		const auto gate = fixed_beta_q2_gate( configuration, call_site );
		*stage.candidate_index = 0;
		if ( linear_fixed_beta_outer_hot_path_gate_uses_streaming_cursor( gate ) )
		{
			reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
				*stage.streaming_cursor,
				make_fixed_beta_outer_hot_path_strict_witness_request( stage ) );
			if ( sync_and_check_runtime_stop() )
				return LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop;
			clear_fixed_beta_hot_path_rebuildable_candidates_impl( *stage.materialized_candidates );
			return LinearFixedBetaOuterHotPathEnterStatus::Prepared;
		}

		*stage.streaming_cursor = {};
		*stage.materialized_candidates =
			materialize_fixed_beta_subconst_hot_path_candidates(
				gate.effective_subconst_mode,
				column_floor,
				stage.output_mask_beta,
				stage.constant,
				*stage.weight_cap );
		if ( sync_and_check_runtime_stop() )
			return LinearFixedBetaOuterHotPathEnterStatus::RuntimeStop;
		return LinearFixedBetaOuterHotPathEnterStatus::Prepared;
	}

	bool next_varconst_step_q2_candidate(
		LinearRoundSearchState& state,
		LinearVarConstStageSlot slot,
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		LinearVarConstQ2Candidate& out_candidate )
	{
		const auto stage = bind_varconst_step_stage( state, slot );

		if ( linear_fixed_beta_outer_hot_path_gate_uses_streaming_cursor(
				fixed_beta_q2_gate( configuration, call_site ) ) )
		{
			FixedBetaSubColumnStrictWitness witness {};
			if ( !next_fixed_beta_sub_column_strict_witness(
					*stage.streaming_cursor,
					witness ) )
			{
				return false;
			}
			return resolve_varconst_q2_candidate(
				configuration,
				call_site,
				stage.output_mask_beta,
				SubConstCandidate {
					witness.input_mask_alpha,
					witness.linear_weight },
				out_candidate );
		}

		if ( *stage.candidate_index >= stage.materialized_candidates->size() )
			return false;

		const auto& candidate =
			( *stage.materialized_candidates )[ ( *stage.candidate_index )++ ];
		return resolve_varconst_q2_candidate(
			configuration,
			call_site,
			stage.output_mask_beta,
			candidate,
			out_candidate );
	}

	bool resolve_varconst_q2_candidate(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		std::uint32_t output_mask_beta,
		const SubConstCandidate& raw_candidate,
		LinearVarConstQ2Candidate& out_candidate ) noexcept
	{
		out_candidate.input_mask_alpha = raw_candidate.input_mask_on_x;
		out_candidate.output_mask_beta = output_mask_beta;
		out_candidate.exact_weight_hint = raw_candidate.linear_weight;
		out_candidate.ordered_stream =
			linear_varconst_fixed_beta_q2_ordered_stream_contract(
				fixed_beta_q2_gate( configuration, call_site ) );
		return true;
	}

	std::vector<LinearVarConstQ2Candidate> materialize_varconst_q2_candidates_for_call_site(
		const LinearBestSearchConfiguration& configuration,
		LinearFixedBetaHotPathCallSite call_site,
		const std::optional<VarConstSubColumnOptimalOnOutputWire>& column_floor,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap )
	{
		const auto gate = fixed_beta_q2_gate( configuration, call_site );
		const auto raw_candidates =
			materialize_fixed_beta_subconst_hot_path_candidates(
				gate.effective_subconst_mode,
				column_floor,
				output_mask_beta,
				constant,
				weight_cap );

		std::vector<LinearVarConstQ2Candidate> out;
		out.reserve( raw_candidates.size() );
		const auto ordered_stream =
			linear_varconst_fixed_beta_q2_ordered_stream_contract( gate );
		for ( const auto& candidate : raw_candidates )
		{
			out.push_back( LinearVarConstQ2Candidate {
				candidate.input_mask_on_x,
				output_mask_beta,
				candidate.linear_weight,
				ordered_stream } );
		}
		return out;
	}
}  // namespace TwilightDream::auto_search_linear
