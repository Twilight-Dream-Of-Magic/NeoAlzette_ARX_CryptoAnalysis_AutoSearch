#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"

namespace TwilightDream::auto_search_differential
{
	namespace
	{
		inline std::uint32_t differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
			std::uint32_t branch_b_difference_before_prewhitening ) noexcept
		{
			// Explicit split step:
			//   B_pre = B_raw xor RC[4]
			// XOR-difference is invariant under xor-by-constant.
			return branch_b_difference_before_prewhitening;
		}

		inline std::uint32_t differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
			std::uint32_t branch_a_difference_before_prewhitening ) noexcept
		{
			// Explicit split step:
			//   A_pre = A_raw xor RC[9]
			// XOR-difference is invariant under xor-by-constant.
			return branch_a_difference_before_prewhitening;
		}

		inline std::uint32_t differential_apply_cross_xor_rot_r0_on_branch_a(
			std::uint32_t branch_a_difference_before_xor,
			std::uint32_t branch_b_difference_current ) noexcept
		{
			return branch_a_difference_before_xor ^
				NeoAlzetteCore::rotl<std::uint32_t>(
					branch_b_difference_current,
					NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		}

		inline std::uint32_t differential_apply_cross_xor_rot_r1_on_branch_b(
			std::uint32_t branch_b_difference_before_xor,
			std::uint32_t branch_a_difference_current ) noexcept
		{
			return branch_b_difference_before_xor ^
				NeoAlzetteCore::rotl<std::uint32_t>(
					branch_a_difference_current,
					NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		}

		inline void differential_apply_first_subround_cross_xor_bridge(
			std::uint32_t output_branch_a_difference_after_first_constant_subtraction,
			std::uint32_t output_branch_b_difference_after_first_addition,
			std::uint32_t& branch_a_difference_after_first_xor_with_rotated_branch_b,
			std::uint32_t& branch_b_difference_after_first_xor_with_rotated_branch_a ) noexcept
		{
			branch_a_difference_after_first_xor_with_rotated_branch_b =
				differential_apply_cross_xor_rot_r0_on_branch_a(
					output_branch_a_difference_after_first_constant_subtraction,
					output_branch_b_difference_after_first_addition );
			branch_b_difference_after_first_xor_with_rotated_branch_a =
				differential_apply_cross_xor_rot_r1_on_branch_b(
					output_branch_b_difference_after_first_addition,
					branch_a_difference_after_first_xor_with_rotated_branch_b );
		}

		inline void differential_apply_second_subround_cross_xor_bridge(
			std::uint32_t output_branch_a_difference_after_second_addition,
			std::uint32_t output_branch_b_difference_after_second_constant_subtraction,
			std::uint32_t& branch_b_difference_after_second_xor_with_rotated_branch_a,
			std::uint32_t& branch_a_difference_after_second_xor_with_rotated_branch_b ) noexcept
		{
			branch_b_difference_after_second_xor_with_rotated_branch_a =
				differential_apply_cross_xor_rot_r0_on_branch_a(
					output_branch_b_difference_after_second_constant_subtraction,
					output_branch_a_difference_after_second_addition );
			branch_a_difference_after_second_xor_with_rotated_branch_b =
				differential_apply_cross_xor_rot_r1_on_branch_b(
					output_branch_a_difference_after_second_addition,
					branch_b_difference_after_second_xor_with_rotated_branch_a );
		}
	}

	std::array<std::uint64_t, 4> compute_next_prefix_counts_for_addition_by_constant_at_bit(
		const std::array<std::uint64_t, 4>& prefix_counts_by_carry_pair_state,
		std::uint32_t input_difference_bit,
		std::uint32_t additive_constant_bit,
		std::uint32_t output_difference_bit )
	{
		auto carry_out_bit = []( std::uint32_t x_bit, std::uint32_t k_bit, std::uint32_t c_in ) -> std::uint32_t {
			return ( x_bit & k_bit ) | ( x_bit & c_in ) | ( k_bit & c_in );
		};

		std::array<std::uint64_t, 4> next_prefix_counts_by_carry_pair_state {};
		for ( int carry_pair_state_index = 0; carry_pair_state_index < 4; ++carry_pair_state_index )
		{
			const std::uint64_t prefix_count = prefix_counts_by_carry_pair_state[ static_cast<std::size_t>( carry_pair_state_index ) ];
			if ( prefix_count == 0 )
				continue;

			const std::uint32_t carry_bit = ( std::uint32_t( carry_pair_state_index ) >> 1 ) & 1u;
			const std::uint32_t carry_bit_prime = std::uint32_t( carry_pair_state_index ) & 1u;
			const std::uint32_t required_output_difference_bit = input_difference_bit ^ carry_bit ^ carry_bit_prime;
			if ( required_output_difference_bit != output_difference_bit )
				continue;

			for ( std::uint32_t input_bit = 0; input_bit <= 1; ++input_bit )
			{
				const std::uint32_t input_bit_prime = input_bit ^ input_difference_bit;
				const std::uint32_t next_carry_bit = carry_out_bit( input_bit, additive_constant_bit, carry_bit );
				const std::uint32_t next_carry_bit_prime = carry_out_bit( input_bit_prime, additive_constant_bit, carry_bit_prime );
				const int next_carry_pair_state_index = int( ( next_carry_bit << 1 ) | next_carry_bit_prime );
				next_prefix_counts_by_carry_pair_state[ static_cast<std::size_t>( next_carry_pair_state_index ) ] += prefix_count;
			}
		}
		return next_prefix_counts_by_carry_pair_state;
	}

	std::uint32_t compute_greedy_output_difference_for_addition_by_constant(
		std::uint32_t input_difference,
		std::uint32_t additive_constant )
	{
		std::array<std::uint64_t, 4> prefix_counts_by_carry_pair_state {};
		prefix_counts_by_carry_pair_state[ 0 ] = 1;

		std::uint32_t output_difference = 0;
		for ( int bit_position = 0; bit_position < 32; ++bit_position )
		{
			const std::uint32_t input_difference_bit = ( input_difference >> bit_position ) & 1u;
			const std::uint32_t additive_constant_bit = ( additive_constant >> bit_position ) & 1u;
			const auto next_prefix_counts_when_output_bit_is_zero =
				compute_next_prefix_counts_for_addition_by_constant_at_bit(
					prefix_counts_by_carry_pair_state,
					input_difference_bit,
					additive_constant_bit,
					0u );
			const auto next_prefix_counts_when_output_bit_is_one =
				compute_next_prefix_counts_for_addition_by_constant_at_bit(
					prefix_counts_by_carry_pair_state,
					input_difference_bit,
					additive_constant_bit,
					1u );
			const std::uint64_t total_prefix_count_when_output_bit_is_zero =
				next_prefix_counts_when_output_bit_is_zero[ 0 ] +
				next_prefix_counts_when_output_bit_is_zero[ 1 ] +
				next_prefix_counts_when_output_bit_is_zero[ 2 ] +
				next_prefix_counts_when_output_bit_is_zero[ 3 ];
			const std::uint64_t total_prefix_count_when_output_bit_is_one =
				next_prefix_counts_when_output_bit_is_one[ 0 ] +
				next_prefix_counts_when_output_bit_is_one[ 1 ] +
				next_prefix_counts_when_output_bit_is_one[ 2 ] +
				next_prefix_counts_when_output_bit_is_one[ 3 ];
			const std::uint32_t chosen_output_difference_bit =
				( total_prefix_count_when_output_bit_is_one > total_prefix_count_when_output_bit_is_zero ) ? 1u : 0u;
			output_difference |= ( chosen_output_difference_bit << bit_position );
			prefix_counts_by_carry_pair_state =
				chosen_output_difference_bit ? next_prefix_counts_when_output_bit_is_one : next_prefix_counts_when_output_bit_is_zero;
		}
		return output_difference;
	}

	SearchWeight compute_greedy_upper_bound_weight(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference )
	{
		// Greedy initializer: pick per-addition optimal gamma (LM2001 Algorithm 4) and use
		// an identity choice for constants to get a cheap finite upper bound.
		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		SearchWeight total_weight = 0;

		for ( int round_index = 0; round_index < search_configuration.round_count; ++round_index )
		{
			const std::uint32_t first_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 17 );
			auto [ output_branch_b_difference_after_first_addition, weight_first_addition ] =
				find_optimal_gamma_with_weight( current_branch_b_difference, first_addition_term_difference, 32 );
			if ( weight_first_addition >= INFINITE_WEIGHT )
				return INFINITE_WEIGHT;
			current_branch_b_difference = output_branch_b_difference_after_first_addition;
			total_weight = saturating_add_search_weight( total_weight, weight_first_addition );

			{
				const SearchWeight weight_constant_subtraction_identity_choice =
					diff_subconst_exact_weight_ceil_int( current_branch_a_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], current_branch_a_difference );
				if ( weight_constant_subtraction_identity_choice >= INFINITE_WEIGHT )
					return INFINITE_WEIGHT;
				total_weight = saturating_add_search_weight( total_weight, weight_constant_subtraction_identity_choice );
			}

			{
				const std::uint32_t branch_a_after_first_cross_xor =
					differential_apply_cross_xor_rot_r0_on_branch_a(
						current_branch_a_difference,
						current_branch_b_difference );
				const std::uint32_t branch_b_after_second_cross_xor =
					differential_apply_cross_xor_rot_r1_on_branch_b(
						current_branch_b_difference,
						branch_a_after_first_cross_xor );
				current_branch_a_difference = branch_a_after_first_cross_xor;
				current_branch_b_difference = branch_b_after_second_cross_xor;
			}

			{
				const std::uint32_t branch_b_difference_after_explicit_prewhitening_before_injection =
					differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
						current_branch_b_difference );
				const InjectionAffineTransition injection_transition =
					compute_injection_transition_from_branch_b(
						branch_b_difference_after_explicit_prewhitening_before_injection );
				total_weight = saturating_add_search_weight( total_weight, injection_transition.rank_weight );
				current_branch_a_difference ^= injection_transition.offset;
			}

			const std::uint32_t second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] =
				find_optimal_gamma_with_weight( current_branch_a_difference, second_addition_term_difference, 32 );
			if ( weight_second_addition >= INFINITE_WEIGHT )
				return INFINITE_WEIGHT;
			current_branch_a_difference = output_branch_a_difference_after_second_addition;
			total_weight = saturating_add_search_weight( total_weight, weight_second_addition );

			{
				const SearchWeight weight_constant_subtraction_identity_choice =
					diff_subconst_exact_weight_ceil_int( current_branch_b_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], current_branch_b_difference );
				if ( weight_constant_subtraction_identity_choice >= INFINITE_WEIGHT )
					return INFINITE_WEIGHT;
				total_weight = saturating_add_search_weight( total_weight, weight_constant_subtraction_identity_choice );
			}

			{
				const std::uint32_t branch_b_after_first_cross_xor =
					differential_apply_cross_xor_rot_r0_on_branch_a(
						current_branch_b_difference,
						current_branch_a_difference );
				const std::uint32_t branch_a_after_second_cross_xor =
					differential_apply_cross_xor_rot_r1_on_branch_b(
						current_branch_a_difference,
						branch_b_after_first_cross_xor );
				current_branch_b_difference = branch_b_after_first_cross_xor;
				current_branch_a_difference = branch_a_after_second_cross_xor;
			}

			{
				const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
					differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
						current_branch_a_difference );
				const InjectionAffineTransition injection_transition =
					compute_injection_transition_from_branch_a(
						branch_a_difference_after_explicit_prewhitening_before_injection );
				total_weight = saturating_add_search_weight( total_weight, injection_transition.rank_weight );
				current_branch_b_difference ^= injection_transition.offset;
			}
		}

		return total_weight;
	}

	std::vector<DifferentialTrailStepRecord> construct_greedy_initial_differential_trail(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		SearchWeight& output_total_weight )
	{
		// Construct an explicit greedy trail so we always have a baseline solution to print,
		// even if the main search hits the maximum node budget before reaching any leaf.
		std::vector<DifferentialTrailStepRecord> trail;
		trail.reserve( std::max( 1, search_configuration.round_count ) );

		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		SearchWeight total_weight = 0;

		for ( int round_index = 0; round_index < search_configuration.round_count; ++round_index )
		{
			DifferentialTrailStepRecord step_record {};
			step_record.round_index = round_index + 1;
			step_record.input_branch_a_difference = current_branch_a_difference;
			step_record.input_branch_b_difference = current_branch_b_difference;

			step_record.first_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 17 );
			auto [ output_branch_b_difference_after_first_addition, weight_first_addition ] =
				find_optimal_gamma_with_weight( current_branch_b_difference, step_record.first_addition_term_difference, 32 );
			if ( weight_first_addition >= INFINITE_WEIGHT )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
			step_record.weight_first_addition = weight_first_addition;
			total_weight = saturating_add_search_weight( total_weight, weight_first_addition );

			step_record.output_branch_a_difference_after_first_constant_subtraction = current_branch_a_difference;
			step_record.weight_first_constant_subtraction =
				diff_subconst_exact_weight_ceil_int(
					current_branch_a_difference,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					step_record.output_branch_a_difference_after_first_constant_subtraction );
			if ( step_record.weight_first_constant_subtraction >= INFINITE_WEIGHT )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight = saturating_add_search_weight( total_weight, step_record.weight_first_constant_subtraction );

			differential_apply_first_subround_cross_xor_bridge(
				step_record.output_branch_a_difference_after_first_constant_subtraction,
				step_record.output_branch_b_difference_after_first_addition,
				step_record.branch_a_difference_after_first_xor_with_rotated_branch_b,
				step_record.branch_b_difference_after_first_xor_with_rotated_branch_a );
			const std::uint32_t branch_b_difference_after_explicit_prewhitening_before_injection =
				differential_difference_after_explicit_prewhitening_before_injection_from_branch_b(
					step_record.branch_b_difference_after_first_xor_with_rotated_branch_a );

			const InjectionAffineTransition injection_transition_from_branch_b =
				compute_injection_transition_from_branch_b(
					branch_b_difference_after_explicit_prewhitening_before_injection );
			step_record.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
			step_record.injection_from_branch_b_xor_difference = injection_transition_from_branch_b.offset;
			step_record.branch_a_difference_after_injection_from_branch_b =
				step_record.branch_a_difference_after_first_xor_with_rotated_branch_b ^ step_record.injection_from_branch_b_xor_difference;
			total_weight = saturating_add_search_weight( total_weight, step_record.weight_injection_from_branch_b );

			step_record.branch_b_difference_after_first_bridge = step_record.branch_b_difference_after_first_xor_with_rotated_branch_a;

			step_record.second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_first_bridge, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_first_bridge, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] =
				find_optimal_gamma_with_weight( step_record.branch_a_difference_after_injection_from_branch_b, step_record.second_addition_term_difference, 32 );
			if ( weight_second_addition >= INFINITE_WEIGHT )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
			step_record.weight_second_addition = weight_second_addition;
			total_weight = saturating_add_search_weight( total_weight, weight_second_addition );

			step_record.output_branch_b_difference_after_second_constant_subtraction = step_record.branch_b_difference_after_first_bridge;
			step_record.weight_second_constant_subtraction =
				diff_subconst_exact_weight_ceil_int(
					step_record.branch_b_difference_after_first_bridge,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					step_record.output_branch_b_difference_after_second_constant_subtraction );
			if ( step_record.weight_second_constant_subtraction >= INFINITE_WEIGHT )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight = saturating_add_search_weight( total_weight, step_record.weight_second_constant_subtraction );

			differential_apply_second_subround_cross_xor_bridge(
				step_record.output_branch_a_difference_after_second_addition,
				step_record.output_branch_b_difference_after_second_constant_subtraction,
				step_record.branch_b_difference_after_second_xor_with_rotated_branch_a,
				step_record.branch_a_difference_after_second_xor_with_rotated_branch_b );
			const std::uint32_t branch_a_difference_after_explicit_prewhitening_before_injection =
				differential_difference_after_explicit_prewhitening_before_injection_from_branch_a(
					step_record.branch_a_difference_after_second_xor_with_rotated_branch_b );

			const InjectionAffineTransition injection_transition_from_branch_a =
				compute_injection_transition_from_branch_a(
					branch_a_difference_after_explicit_prewhitening_before_injection );
			step_record.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
			step_record.injection_from_branch_a_xor_difference = injection_transition_from_branch_a.offset;
			total_weight = saturating_add_search_weight( total_weight, step_record.weight_injection_from_branch_a );

			step_record.output_branch_b_difference =
				step_record.branch_b_difference_after_second_xor_with_rotated_branch_a ^ step_record.injection_from_branch_a_xor_difference;
			step_record.output_branch_a_difference = step_record.branch_a_difference_after_second_xor_with_rotated_branch_b;

			step_record.round_weight =
				step_record.weight_first_addition +
				step_record.weight_first_constant_subtraction +
				step_record.weight_injection_from_branch_b +
				step_record.weight_second_addition +
				step_record.weight_second_constant_subtraction +
				step_record.weight_injection_from_branch_a;

			trail.push_back( step_record );

			current_branch_a_difference = step_record.output_branch_a_difference;
			current_branch_b_difference = step_record.output_branch_b_difference;
		}

		output_total_weight = total_weight;
		return trail;
	}

	void SubConstEnumerator::reset( std::uint32_t dx, std::uint32_t sub_const, SearchWeight bvweight_cap )
	{
		initialized = true;
		stop_due_to_limits = false;
		input_difference = dx;
		subtractive_constant = sub_const;
		cap_bitvector = std::min<SearchWeight>( bvweight_cap, SearchWeight( 32 ) );
		cap_dynamic_planning = std::min<SearchWeight>( SearchWeight( 32 ), cap_bitvector + SearchWeight( SLACK ) );
		additive_constant = std::uint32_t( 0u ) - subtractive_constant;
		output_hint = compute_greedy_output_difference_for_addition_by_constant( input_difference, additive_constant );
		stack_step = 0;
		std::array<std::uint64_t, 4> prefix_counts {};
		prefix_counts[ 0 ] = 1;
		stack[ stack_step++ ] = Frame { 0, 0u, prefix_counts, 0u, 0 };
	}

	bool SubConstEnumerator::next( DifferentialBestSearchContext& context, std::uint32_t& out_difference, SearchWeight& out_weight )
	{
		if ( !initialized || stop_due_to_limits )
			return false;

		while ( stack_step > 0 )
		{
			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				if ( differential_note_runtime_node_visit( context ) )
				{
					stop_due_to_limits = true;
					return false;
				}
				maybe_print_single_run_progress( context, -1 );

				if ( frame.bit_position > 0 )
				{
					const std::uint64_t total_prefix_count = frame.prefix_counts[ 0 ] + frame.prefix_counts[ 1 ] + frame.prefix_counts[ 2 ] + frame.prefix_counts[ 3 ];
					if ( total_prefix_count == 0 )
					{
						--stack_step;
						continue;
					}
					const int log2_total_prefix_count = floor_log2_uint64( total_prefix_count );
					const int prefix_weight_estimate = frame.bit_position - log2_total_prefix_count;
					if ( static_cast<SearchWeight>( prefix_weight_estimate ) > cap_dynamic_planning )
					{
						--stack_step;
						continue;
					}
				}

				if ( frame.bit_position == 32 )
				{
					const SearchWeight weight = diff_subconst_exact_weight_ceil_int( input_difference, subtractive_constant, frame.prefix );
					if ( weight < INFINITE_WEIGHT && weight <= cap_bitvector )
					{
						out_difference = frame.prefix;
						out_weight = weight;
						--stack_step;
						return true;
					}
					--stack_step;
					continue;
				}

				const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
				const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
				frame.preferred_bit = ( output_hint >> frame.bit_position ) & 1u;

				const auto next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, frame.preferred_bit );
				frame.state = 1;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.preferred_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
				continue;
			}

			if ( frame.state == 1 )
			{
				if ( differential_runtime_node_limit_hit( context ) )
				{
					stop_due_to_limits = true;
					return false;
				}

				const std::uint32_t input_difference_bit = ( input_difference >> frame.bit_position ) & 1u;
				const std::uint32_t additive_constant_bit = ( additive_constant >> frame.bit_position ) & 1u;
				const std::uint32_t other_bit = 1u - frame.preferred_bit;
				const auto			next_prefix_counts = compute_next_prefix_counts_for_addition_by_constant_at_bit( frame.prefix_counts, input_difference_bit, additive_constant_bit, other_bit );

				frame.state = 2;
				stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other_bit << frame.bit_position ), next_prefix_counts, 0u, 0 };
				continue;
			}

			--stack_step;
		}

		return false;
	}

	void AffineSubspaceEnumerator::reset( const InjectionAffineTransition& t, std::size_t max_outputs )
	{
		initialized = true;
		stop_due_to_limits = false;
		transition = t;
		maximum_output_difference_count = max_outputs;
		produced_output_difference_count = 0;
		stack_step = 0;
		stack[ stack_step++ ] = Frame { 0, transition.offset, 0 };
	}

	bool AffineSubspaceEnumerator::next( DifferentialBestSearchContext& context, std::uint32_t& out_difference )
	{
		if ( !initialized || stop_due_to_limits )
			return false;

		while ( stack_step > 0 )
		{
			if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
				return false;

			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				if ( differential_note_runtime_node_visit( context ) )
				{
					stop_due_to_limits = true;
					return false;
				}
				maybe_print_single_run_progress( context, -1 );

				if ( static_cast<std::uint64_t>( frame.basis_index ) >= transition.rank_weight )
				{
					out_difference = frame.current_difference;
					++produced_output_difference_count;
					--stack_step;
					return true;
				}

				frame.state = 1;
				stack[ stack_step++ ] = Frame { frame.basis_index + 1, frame.current_difference, 0 };
				continue;
			}

			if ( differential_runtime_node_limit_hit( context ) )
			{
				stop_due_to_limits = true;
				return false;
			}
			if ( maximum_output_difference_count != 0 && produced_output_difference_count >= maximum_output_difference_count )
				return false;

			frame = Frame { frame.basis_index + 1, frame.current_difference ^ transition.basis_vectors[ std::size_t( frame.basis_index ) ], 0 };
		}

		return false;
	}

}  // namespace TwilightDream::auto_search_differential
