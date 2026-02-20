#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "auto_search_frame/detail/polarity/linear/linear_bnb_q2_q1_facade.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_hot_path.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_strict_witness.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_alpha_theorem.hpp"
#include "arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp"

namespace TwilightDream::auto_search_linear
{
	FixedAlphaSubColumnRootTheoremRequest make_fixed_alpha_sub_column_root_theorem_request(
		std::uint32_t input_mask_alpha,
		std::uint32_t sub_constant,
		int n_bits ) noexcept
	{
		return FixedAlphaSubColumnRootTheoremRequest {
			input_mask_alpha,
			sub_constant,
			n_bits };
	}

	FixedAlphaSubColumnRootTheoremAnswer solve_fixed_alpha_sub_column_root_theorem_u32(
		FixedAlphaSubColumnRootTheoremRequest request ) noexcept
	{
		using ::TwilightDream::arx_operators::find_optimal_beta_varconst_mod_sub;

		const auto result =
			find_optimal_beta_varconst_mod_sub(
				static_cast<std::uint64_t>( request.input_mask_alpha ),
				static_cast<std::uint64_t>( request.sub_constant ),
				request.n_bits );
		const std::uint64_t mask =
			( request.n_bits >= 64 ) ? std::uint64_t( ~0ull ) :
			( request.n_bits <= 0 ) ? 0ull :
			( ( std::uint64_t( 1 ) << request.n_bits ) - 1ull );
		return FixedAlphaSubColumnRootTheoremAnswer {
			static_cast<std::uint32_t>( result.beta & mask ),
			result.abs_weight,
			result.abs_weight_is_exact_2pow64,
			result.ceil_linear_weight_int };
	}

	std::optional<FixedAlphaSubColumnRootTheoremAnswer> try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
		FixedAlphaSubColumnRootTheoremRequest request,
		SearchWeight weight_cap ) noexcept
	{
		const auto answer =
			solve_fixed_alpha_sub_column_root_theorem_u32( request );
		if ( answer.ceil_linear_weight > weight_cap )
			return std::nullopt;
		return answer;
	}

	VarConstSubColumnOptimalOnInputWire project_fixed_alpha_sub_column_root_theorem_to_input_wire(
		const FixedAlphaSubColumnRootTheoremAnswer& answer ) noexcept
	{
		return VarConstSubColumnOptimalOnInputWire {
			answer.optimal_output_mask_beta,
			answer.ceil_linear_weight };
	}

	std::optional<VarConstSubColumnOptimalOnInputWire> try_project_fixed_alpha_sub_column_root_theorem_to_input_wire(
		const std::optional<FixedAlphaSubColumnRootTheoremAnswer>& answer ) noexcept
	{
		if ( !answer.has_value() )
			return std::nullopt;
		return project_fixed_alpha_sub_column_root_theorem_to_input_wire( *answer );
	}

	FixedAlphaSubColumnStrictWitnessRequest make_fixed_alpha_sub_column_strict_witness_request(
		std::uint32_t input_mask_alpha,
		std::uint32_t sub_constant,
		SearchWeight weight_cap,
		int n_bits ) noexcept
	{
		return FixedAlphaSubColumnStrictWitnessRequest {
			input_mask_alpha,
			sub_constant,
			weight_cap,
			n_bits };
	}

	FixedAlphaSubColumnStrictWitness make_fixed_alpha_sub_column_strict_witness(
		std::uint32_t output_mask_beta,
		SearchWeight linear_weight ) noexcept
	{
		return FixedAlphaSubColumnStrictWitness {
			output_mask_beta,
			linear_weight };
	}

	FixedAlphaSubConstCandidate project_fixed_alpha_sub_column_strict_witness_to_candidate(
		const FixedAlphaSubColumnStrictWitness& witness ) noexcept
	{
		return FixedAlphaSubConstCandidate {
			witness.output_mask_beta,
			witness.linear_weight };
	}

	FixedAlphaSubColumnStrictWitness project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness(
		const FixedAlphaSubColumnRootTheoremAnswer& answer ) noexcept
	{
		return make_fixed_alpha_sub_column_strict_witness(
			answer.optimal_output_mask_beta,
			answer.ceil_linear_weight );
	}

	std::optional<FixedAlphaSubColumnStrictWitness> try_project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness(
		const std::optional<FixedAlphaSubColumnRootTheoremAnswer>& answer ) noexcept
	{
		if ( !answer.has_value() )
			return std::nullopt;
		return project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness( *answer );
	}

	std::vector<FixedAlphaSubConstCandidate> project_fixed_alpha_sub_column_strict_witnesses_to_candidates(
		const std::vector<FixedAlphaSubColumnStrictWitness>& witnesses )
	{
		std::vector<FixedAlphaSubConstCandidate> out;
		out.reserve( witnesses.size() );
		for ( const auto& witness : witnesses )
			out.push_back( project_fixed_alpha_sub_column_strict_witness_to_candidate( witness ) );
		return out;
	}

	void rebuild_fixed_alpha_sub_column_strict_witness_cursor_runtime_state(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& /*cursor*/ )
	{
	}

	void restore_fixed_alpha_sub_column_strict_witness_cursor_heap_snapshot(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		const std::vector<FixedAlphaSubConstCandidate>& /*heap_snapshot*/ )
	{
		rebuild_fixed_alpha_sub_column_strict_witness_cursor_runtime_state( cursor );
	}

	void reset_fixed_alpha_sub_column_strict_witness_streaming_cursor(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedAlphaSubColumnStrictWitnessRequest request )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.input_mask_alpha = request.input_mask_alpha;
		cursor.constant = request.sub_constant;
		cursor.weight_cap = request.weight_cap;
		cursor.nbits = std::clamp( request.n_bits, 1, 32 );
	}

	bool next_fixed_alpha_sub_column_strict_witness(
		FixedAlphaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedAlphaSubColumnStrictWitness& out_witness )
	{
		if ( !cursor.initialized || cursor.stop_due_to_limits )
			return false;
		if ( cursor.emitted_candidate_count != 0 )
			return false;

		const auto answer =
			try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
				make_fixed_alpha_sub_column_root_theorem_request(
					cursor.input_mask_alpha,
					cursor.constant,
					cursor.nbits ),
				cursor.weight_cap );
		if ( !answer.has_value() )
			return false;

		out_witness =
			project_fixed_alpha_sub_column_root_theorem_answer_to_strict_witness(
				*answer );
		++cursor.emitted_candidate_count;
		return true;
	}

	std::vector<FixedAlphaSubColumnStrictWitness> materialize_fixed_alpha_sub_column_strict_witnesses_exact(
		FixedAlphaSubColumnStrictWitnessRequest request )
	{
		std::vector<FixedAlphaSubColumnStrictWitness> witnesses {};
		FixedAlphaSubColumnStrictWitnessStreamingCursor cursor {};
		reset_fixed_alpha_sub_column_strict_witness_streaming_cursor(
			cursor,
			request );
		FixedAlphaSubColumnStrictWitness witness {};
		while ( next_fixed_alpha_sub_column_strict_witness( cursor, witness ) )
			witnesses.push_back( witness );
		return witnesses;
	}

	std::vector<FixedAlphaSubColumnStrictWitness> materialize_fixed_alpha_sub_column_strict_witnesses(
		FixedAlphaSubColumnStrictWitnessRequest request )
	{
		return materialize_fixed_alpha_sub_column_strict_witnesses_exact( request );
	}

	std::optional<VarConstSubColumnOptimalOnInputWire> compute_fixed_alpha_outer_hot_path_column_floor(
		LinearFixedAlphaOuterHotPathStageBinding stage ) noexcept
	{
		const auto candidate =
			try_project_fixed_alpha_sub_column_root_theorem_to_input_wire(
				try_solve_fixed_alpha_sub_column_root_theorem_within_cap_u32(
					make_fixed_alpha_sub_column_root_theorem_request(
						stage.input_mask_alpha,
						stage.constant,
						32 ),
					stage.weight_cap ? *stage.weight_cap : INFINITE_WEIGHT ) );
		if ( stage.weight_floor != nullptr )
		{
			*stage.weight_floor =
				candidate.has_value()
					? candidate->linear_weight
					: MAX_FINITE_SEARCH_WEIGHT;
		}
		return candidate;
	}

	SearchWeight ensure_fixed_alpha_outer_hot_path_weight_floor(
		LinearFixedAlphaOuterHotPathStageBinding stage ) noexcept
	{
		if ( stage.weight_floor != nullptr &&
			 *stage.weight_floor != INFINITE_WEIGHT )
		{
			return *stage.weight_floor;
		}
		( void )compute_fixed_alpha_outer_hot_path_column_floor( stage );
		return stage.weight_floor
			? *stage.weight_floor
			: MAX_FINITE_SEARCH_WEIGHT;
	}

	bool fixed_alpha_dual_fixed_pair_within_cap(
		std::uint32_t input_mask_alpha,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight weight_cap,
		int n_bits ) noexcept
	{
		const SearchWeight exact_weight =
			TwilightDream::arx_operators::correlation_sub_const_weight_ceil_int_logdepth(
				static_cast<std::uint64_t>( input_mask_alpha ),
				static_cast<std::uint64_t>( constant ),
				static_cast<std::uint64_t>( output_mask_beta ),
				n_bits );
		return exact_weight <= weight_cap;
	}

	std::optional<SearchWeight> resolve_varconst_sub_candidate_weight_for_runtime(
		const LinearBestSearchConfiguration& /*configuration*/,
		std::uint32_t input_mask_alpha,
		std::uint32_t output_mask_beta,
		std::uint32_t constant,
		SearchWeight fallback_exact_weight,
		SearchWeight weight_cap,
		SearchWeight* fixed_alpha_weight_floor,
		int n_bits ) noexcept
	{
		return evaluate_varconst_q1_exact_weight(
			LinearVarConstQ2Candidate {
				input_mask_alpha,
				output_mask_beta,
				fallback_exact_weight,
				LinearOrderedQ2StreamContract {} },
			constant,
			weight_cap,
			fixed_alpha_weight_floor,
			n_bits );
	}
}  // namespace TwilightDream::auto_search_linear
