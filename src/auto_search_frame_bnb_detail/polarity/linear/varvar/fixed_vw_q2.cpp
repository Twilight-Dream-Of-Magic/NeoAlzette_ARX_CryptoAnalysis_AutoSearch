#include <cstdint>
#include <optional>

#include "auto_search_frame/detail/linear_best_search_types.hpp"
#include "auto_search_frame/detail/polarity/linear/linear_varvar_modular_add_bnb_mode.hpp"
#include "arx_analysis_operators/linear_correlation/weight_evaluation_ccz.hpp"

namespace TwilightDream::auto_search_linear
{
	std::optional<VarVarAddColumnOptimalOnSumWire> try_varvar_add_column_optimal_u_within_cap_u32(
		std::uint32_t input_mask_v,
		std::uint32_t input_mask_w,
		SearchWeight weight_cap,
		int n_bits ) noexcept
	{
		const auto column_max =
			TwilightDream::arx_operators::linear_correlation_add_phi2_column_max(
				static_cast<std::uint64_t>( input_mask_v ),
				static_cast<std::uint64_t>( input_mask_w ),
				n_bits );
		if ( column_max.z_weight > weight_cap )
			return std::nullopt;

		using ::TwilightDream::arx_operators::mask_n;
		const std::uint64_t mask = mask_n( n_bits );
		return VarVarAddColumnOptimalOnSumWire {
			static_cast<std::uint32_t>( column_max.u & mask ),
			column_max.z_weight };
	}

	LinearVarVarAddCandidateResolution resolve_varvar_add_candidate_for_mode(
		LinearVarVarModularAddBnBMode mode,
		std::uint32_t fixed_output_mask_u,
		const AddCandidate& candidate,
		SearchWeight weight_cap,
		int n_bits ) noexcept
	{
		switch ( mode )
		{
		case LinearVarVarModularAddBnBMode::FixedInputVW_ColumnOptimalOutputU:
		{
			const auto column_optimal =
				try_varvar_add_column_optimal_u_within_cap_u32(
					candidate.input_mask_x,
					candidate.input_mask_y,
					weight_cap,
					n_bits );
			if ( !column_optimal.has_value() )
				return {};
			return LinearVarVarAddCandidateResolution {
				LinearVarVarAddCandidateResolutionKind::accept_candidate,
				LinearResolvedVarVarAddCandidate {
					column_optimal->linear_weight,
					column_optimal->output_mask_u } };
		}
		case LinearVarVarModularAddBnBMode::FixedOutputMaskU_EnumerateInputVW:
		default:
			if ( candidate.linear_weight > weight_cap )
			{
				return LinearVarVarAddCandidateResolution {
					LinearVarVarAddCandidateResolutionKind::stop_enumerating,
					{} };
			}
			return LinearVarVarAddCandidateResolution {
				LinearVarVarAddCandidateResolutionKind::accept_candidate,
				LinearResolvedVarVarAddCandidate {
					candidate.linear_weight,
					fixed_output_mask_u } };
		}
	}
}  // namespace TwilightDream::auto_search_linear
