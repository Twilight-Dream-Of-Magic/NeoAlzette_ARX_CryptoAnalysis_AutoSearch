#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"

namespace TwilightDream::auto_search_differential
{
	namespace
	{
		int xor_basis_add( std::array<std::uint32_t, 32>& basis, std::uint32_t v )
		{
			// classic GF(2) linear basis insertion; returns 1 if v increased rank, 0 otherwise
			while ( v != 0u )
			{
				const unsigned bit = 31u - std::countl_zero( v );
				const std::uint32_t b = basis[ bit ];
				if ( b != 0u )
				{
					v ^= b;
				}
				else
				{
					basis[ bit ] = v;
					return 1;
				}
			}
			return 0;
		}

		std::uint64_t binom_u64( int n, int k ) noexcept
		{
			if ( k < 0 || k > n )
				return 0;
			k = std::min( k, n - k );
			long double acc = 1.0L;
			for ( int i = 1; i <= k; ++i )
			{
				acc = acc * static_cast<long double>( n - k + i ) / static_cast<long double>( i );
				if ( acc > static_cast<long double>( std::numeric_limits<std::uint64_t>::max() ) )
					return std::numeric_limits<std::uint64_t>::max();
			}
			const long double rounded = std::floor( acc + 0.5L );
			if ( rounded > static_cast<long double>( std::numeric_limits<std::uint64_t>::max() ) )
				return std::numeric_limits<std::uint64_t>::max();
			return static_cast<std::uint64_t>( rounded );
		}
	}  // namespace

	std::size_t g_injection_cache_max_entries_per_thread = 65536;  // default: 2^16
	WeightSlicedPddtCache g_weight_sliced_pddt_cache {};
	ShardedInjectionCache32 g_shared_injection_cache_branch_a {};
	ShardedInjectionCache32 g_shared_injection_cache_branch_b {};

	void WeightSlicedPddtCache::configure( bool enable, int max_weight )
	{
		std::scoped_lock lk( mutex_ );
		enabled_ = enable;
		max_weight_ = std::clamp( max_weight, 0, 31 );
		if ( !enabled_ )
			map_.clear();
	}

	bool WeightSlicedPddtCache::enabled() const
	{
		std::scoped_lock lk( mutex_ );
		return enabled_;
	}

	int WeightSlicedPddtCache::max_weight() const
	{
		std::scoped_lock lk( mutex_ );
		return max_weight_;
	}

	bool WeightSlicedPddtCache::try_get_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, std::vector<std::uint32_t>& out )
	{
		if ( weight < 0 )
			return false;
		const WeightSlicedPddtKey key { alpha, beta, static_cast<std::uint8_t>( weight ), static_cast<std::uint8_t>( word_bits ) };
		std::scoped_lock lk( mutex_ );
		if ( !enabled_ )
			return false;
		auto it = map_.find( key );
		if ( it == map_.end() )
			return false;
		const WeightSlicedPddtShell& shell = it->second;
		if ( physical_memory_allocation_guard_active() )
			return false;
		out.clear();
		out.reserve( shell.size );
		for ( std::uint32_t i = 0; i < shell.size; ++i )
			out.push_back( shell.data[ i ] );
		return true;
	}

	void WeightSlicedPddtCache::maybe_put_shell( std::uint32_t alpha, std::uint32_t beta, int weight, int word_bits, const std::vector<std::uint32_t>& shell )
	{
		if ( weight < 0 )
			return;
		if ( runtime_component::memory_governor_in_pressure() )
			return;
		const WeightSlicedPddtKey key { alpha, beta, static_cast<std::uint8_t>( weight ), static_cast<std::uint8_t>( word_bits ) };
		std::scoped_lock lk( mutex_ );
		if ( !enabled_ )
			return;
		if ( map_.find( key ) != map_.end() )
			return;

		if ( shell.empty() )
		{
			map_.emplace( key, WeightSlicedPddtShell { nullptr, 0 } );
			return;
		}

		// Cache exactly the shell bytes that were just generated. We do not preallocate
		// to the full rebuildable budget and then fill it later.
		const std::uint64_t bytes = static_cast<std::uint64_t>( shell.size() ) * sizeof( std::uint32_t );
		void* p = runtime_component::alloc_rebuildable( bytes, false );
		if ( !p )
			return;
		std::uint32_t* dst = static_cast<std::uint32_t*>( p );
		for ( std::size_t i = 0; i < shell.size(); ++i )
			dst[ i ] = shell[ i ];
		map_.emplace( key, WeightSlicedPddtShell { dst, static_cast<std::uint32_t>( shell.size() ) } );
	}

	void WeightSlicedPddtCache::clear_and_disable( const char* /*reason*/ )
	{
		std::scoped_lock lk( mutex_ );
		map_.clear();
		enabled_ = false;
	}

	void WeightSlicedPddtCache::clear_keep_enabled( const char* /*reason*/ )
	{
		std::scoped_lock lk( mutex_ );
		map_.clear();
	}

	std::uint64_t estimate_weight_sliced_pddt_bytes( int weight, int word_bits ) noexcept
	{
		if ( word_bits < 2 )
			return 0;
		const int n = word_bits - 1;
		weight = std::clamp( weight, 0, n );
		const std::uint64_t comb = binom_u64( n, weight );
		const std::uint64_t bytes = comb * sizeof( std::uint32_t );
		return bytes ? bytes : sizeof( std::uint32_t );
	}

	int compute_weight_sliced_pddt_max_weight_from_budget( std::uint64_t budget_bytes, int word_bits ) noexcept
	{
		const int max_w = std::clamp( word_bits - 1, 0, 31 );
		if ( max_w <= 0 )
			return 0;
		if ( budget_bytes == 0 )
			return std::min( 10, max_w );

		auto estimator = [ & ]( double threshold ) -> std::uint64_t {
			const int t = std::clamp( static_cast<int>( std::llround( threshold ) ), 0, max_w );
			const int w = max_w - t;
			return estimate_weight_sliced_pddt_bytes( w, word_bits );
		};

		const double threshold = TwilightDream::runtime_component::pddt_select_threshold_for_budget( estimator, 0.0, double( max_w ), budget_bytes, 24 );
		const int	 t = std::clamp( static_cast<int>( std::llround( threshold ) ), 0, max_w );
		return std::clamp( max_w - t, 0, max_w );
	}

	void configure_weight_sliced_pddt_cache_for_run( DifferentialBestSearchConfiguration& configuration, std::uint64_t rebuildable_budget_bytes ) noexcept
	{
		if ( !configuration.enable_weight_sliced_pddt )
		{
			configuration.weight_sliced_pddt_max_weight = -1;
			g_weight_sliced_pddt_cache.configure( false, 0 );
			return;
		}

		int max_weight = configuration.weight_sliced_pddt_max_weight;
		if ( max_weight < 0 )
			max_weight = compute_weight_sliced_pddt_max_weight_from_budget( rebuildable_budget_bytes, 32 );

		max_weight = std::clamp( max_weight, 0, 31 );
		configuration.weight_sliced_pddt_max_weight = max_weight;
		g_weight_sliced_pddt_cache.configure( true, max_weight );
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

	int compute_greedy_upper_bound_weight(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference )
	{
		// Greedy initializer: pick per-addition optimal gamma (LM2001 Algorithm 4) and use
		// an identity choice for constants to get a cheap finite upper bound.
		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		long long	  total_weight = 0;

		for ( int round_index = 0; round_index < search_configuration.round_count; ++round_index )
		{
			const std::uint32_t first_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, 17 );
			auto [ output_branch_b_difference_after_first_addition, weight_first_addition ] =
				find_optimal_gamma_with_weight( current_branch_b_difference, first_addition_term_difference, 32 );
			if ( weight_first_addition < 0 )
				return INFINITE_WEIGHT;
			current_branch_b_difference = output_branch_b_difference_after_first_addition;
			total_weight += weight_first_addition;

			{
				const int weight_constant_subtraction_identity_choice =
					diff_subconst_exact_weight_ceil_int( current_branch_a_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 1 ], current_branch_a_difference );
				if ( weight_constant_subtraction_identity_choice < 0 )
					return INFINITE_WEIGHT;
				total_weight += weight_constant_subtraction_identity_choice;
			}

			current_branch_a_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			current_branch_b_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			{
				const InjectionAffineTransition injection_transition = compute_injection_transition_from_branch_b( current_branch_b_difference );
				total_weight += injection_transition.rank_weight;
				current_branch_a_difference ^= injection_transition.offset;
			}

			const std::uint32_t second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] =
				find_optimal_gamma_with_weight( current_branch_a_difference, second_addition_term_difference, 32 );
			if ( weight_second_addition < 0 )
				return INFINITE_WEIGHT;
			current_branch_a_difference = output_branch_a_difference_after_second_addition;
			total_weight += weight_second_addition;

			{
				const int weight_constant_subtraction_identity_choice =
					diff_subconst_exact_weight_ceil_int( current_branch_b_difference, NeoAlzetteCore::ROUND_CONSTANTS[ 6 ], current_branch_b_difference );
				if ( weight_constant_subtraction_identity_choice < 0 )
					return INFINITE_WEIGHT;
				total_weight += weight_constant_subtraction_identity_choice;
			}

			current_branch_b_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			current_branch_a_difference ^= NeoAlzetteCore::rotl<std::uint32_t>( current_branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			{
				const InjectionAffineTransition injection_transition = compute_injection_transition_from_branch_a( current_branch_a_difference );
				total_weight += injection_transition.rank_weight;
				current_branch_b_difference ^= injection_transition.offset;
			}
		}

		if ( total_weight > INFINITE_WEIGHT )
			return INFINITE_WEIGHT;
		return int( total_weight );
	}

	std::vector<DifferentialTrailStepRecord> construct_greedy_initial_differential_trail(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		int& output_total_weight )
	{
		// Construct an explicit greedy trail so we always have a baseline solution to print,
		// even if the main search hits the maximum node budget before reaching any leaf.
		std::vector<DifferentialTrailStepRecord> trail;
		trail.reserve( std::max( 1, search_configuration.round_count ) );

		std::uint32_t current_branch_a_difference = initial_branch_a_difference;
		std::uint32_t current_branch_b_difference = initial_branch_b_difference;
		long long	  total_weight = 0;

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
			if ( weight_first_addition < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_b_difference_after_first_addition = output_branch_b_difference_after_first_addition;
			step_record.weight_first_addition = weight_first_addition;
			total_weight += weight_first_addition;

			step_record.output_branch_a_difference_after_first_constant_subtraction = current_branch_a_difference;
			step_record.weight_first_constant_subtraction =
				diff_subconst_exact_weight_ceil_int(
					current_branch_a_difference,
					NeoAlzetteCore::ROUND_CONSTANTS[ 1 ],
					step_record.output_branch_a_difference_after_first_constant_subtraction );
			if ( step_record.weight_first_constant_subtraction < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight += step_record.weight_first_constant_subtraction;

			step_record.branch_a_difference_after_first_xor_with_rotated_branch_b =
				step_record.output_branch_a_difference_after_first_constant_subtraction ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.output_branch_b_difference_after_first_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			step_record.branch_b_difference_after_first_xor_with_rotated_branch_a =
				step_record.output_branch_b_difference_after_first_addition ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_a_difference_after_first_xor_with_rotated_branch_b, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			const InjectionAffineTransition injection_transition_from_branch_b =
				compute_injection_transition_from_branch_b( step_record.branch_b_difference_after_first_xor_with_rotated_branch_a );
			step_record.weight_injection_from_branch_b = injection_transition_from_branch_b.rank_weight;
			step_record.injection_from_branch_b_xor_difference = injection_transition_from_branch_b.offset;
			step_record.branch_a_difference_after_injection_from_branch_b =
				step_record.branch_a_difference_after_first_xor_with_rotated_branch_b ^ step_record.injection_from_branch_b_xor_difference;
			total_weight += step_record.weight_injection_from_branch_b;

			step_record.branch_b_difference_after_first_bridge = step_record.branch_b_difference_after_first_xor_with_rotated_branch_a;

			step_record.second_addition_term_difference =
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_first_bridge, 31 ) ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_first_bridge, 17 );
			auto [ output_branch_a_difference_after_second_addition, weight_second_addition ] =
				find_optimal_gamma_with_weight( step_record.branch_a_difference_after_injection_from_branch_b, step_record.second_addition_term_difference, 32 );
			if ( weight_second_addition < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			step_record.output_branch_a_difference_after_second_addition = output_branch_a_difference_after_second_addition;
			step_record.weight_second_addition = weight_second_addition;
			total_weight += weight_second_addition;

			step_record.output_branch_b_difference_after_second_constant_subtraction = step_record.branch_b_difference_after_first_bridge;
			step_record.weight_second_constant_subtraction =
				diff_subconst_exact_weight_ceil_int(
					step_record.branch_b_difference_after_first_bridge,
					NeoAlzetteCore::ROUND_CONSTANTS[ 6 ],
					step_record.output_branch_b_difference_after_second_constant_subtraction );
			if ( step_record.weight_second_constant_subtraction < 0 )
			{
				output_total_weight = INFINITE_WEIGHT;
				return {};
			}
			total_weight += step_record.weight_second_constant_subtraction;

			step_record.branch_b_difference_after_second_xor_with_rotated_branch_a =
				step_record.output_branch_b_difference_after_second_constant_subtraction ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.output_branch_a_difference_after_second_addition, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
			step_record.branch_a_difference_after_second_xor_with_rotated_branch_b =
				step_record.output_branch_a_difference_after_second_addition ^
				NeoAlzetteCore::rotl<std::uint32_t>( step_record.branch_b_difference_after_second_xor_with_rotated_branch_a, NeoAlzetteCore::CROSS_XOR_ROT_R1 );

			const InjectionAffineTransition injection_transition_from_branch_a =
				compute_injection_transition_from_branch_a( step_record.branch_a_difference_after_second_xor_with_rotated_branch_b );
			step_record.weight_injection_from_branch_a = injection_transition_from_branch_a.rank_weight;
			step_record.injection_from_branch_a_xor_difference = injection_transition_from_branch_a.offset;
			total_weight += step_record.weight_injection_from_branch_a;

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

		if ( total_weight > INFINITE_WEIGHT )
			total_weight = INFINITE_WEIGHT;
		output_total_weight = int( total_weight );
		return trail;
	}

	namespace
	{
		bool enumerate_add_shell_exact_collect(
			DifferentialBestSearchContext& context,
			std::uint32_t alpha,
			std::uint32_t beta,
			std::uint32_t output_hint,
			int target_weight,
			int word_bits,
			std::vector<std::uint32_t>& out_shell,
			bool& stop_due_to_limits )
		{
			out_shell.clear();
			stop_due_to_limits = false;
			if ( target_weight < 0 )
				return false;

			if ( word_bits < 1 )
				return true;
			if ( word_bits > 32 )
				word_bits = 32;
			const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			alpha &= mask;
			beta &= mask;
			output_hint &= mask;

			struct Frame
			{
				int			  bit_position = 0;
				std::uint32_t prefix = 0;
				std::uint32_t prefer = 0;
				std::uint8_t  state = 0;
			};

			static constexpr int MAX_STACK = 33;
			std::array<Frame, MAX_STACK> stack {};
			int							 stack_step = 0;
			stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };

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
						const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
						if ( w_prefix < 0 || w_prefix > target_weight )
						{
							--stack_step;
							continue;
						}
						const int remaining_max = word_bits - frame.bit_position;
						if ( w_prefix + remaining_max < target_weight )
						{
							--stack_step;
							continue;
						}
						if ( frame.bit_position == word_bits )
						{
							if ( w_prefix == target_weight )
								out_shell.push_back( frame.prefix );
							--stack_step;
							continue;
						}
					}

					frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
					continue;
				}

				if ( frame.state == 1 )
				{
					if ( differential_runtime_node_limit_hit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}

					frame.state = 2;
					const std::uint32_t other = 1u - frame.prefer;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
					continue;
				}

				--stack_step;
			}

			return true;
		}
	}  // namespace

	bool rebuild_modular_addition_enumerator_shell_cache(
		const DifferentialBestSearchConfiguration& configuration,
		ModularAdditionEnumerator& enumerator )
	{
		if ( !enumerator.initialized || !enumerator.using_cached_shell )
			return true;
		if ( !enumerator.shell_cache.empty() )
			return enumerator.shell_index <= enumerator.shell_cache.size();

		enumerator.shell_cache.clear();
		if ( enumerator.target_weight < 0 )
			return false;

		bool cache_hit = false;
		if ( configuration.enable_weight_sliced_pddt &&
			 enumerator.target_weight <= configuration.weight_sliced_pddt_max_weight )
		{
			if ( g_weight_shell_cache.try_get_shell(
					 enumerator.alpha,
					 enumerator.beta,
					 enumerator.target_weight,
					 enumerator.word_bits,
					 enumerator.shell_cache ) )
			{
				cache_hit = true;
			}
		}

		if ( enumerator.shell_cache.empty() )
		{
			DifferentialBestSearchContext rebuild_context {};
			rebuild_context.configuration = configuration;
			bool stop_due_to_limits = false;
			if ( !enumerate_add_shell_exact_collect(
					 rebuild_context,
					 enumerator.alpha,
					 enumerator.beta,
					 enumerator.output_hint,
					 enumerator.target_weight,
					 enumerator.word_bits,
					 enumerator.shell_cache,
					 stop_due_to_limits ) )
			{
				return false;
			}
			if ( stop_due_to_limits || enumerator.shell_cache.empty() )
				return false;
			if ( configuration.enable_weight_sliced_pddt &&
				 enumerator.target_weight <= configuration.weight_sliced_pddt_max_weight &&
				 !cache_hit )
			{
				g_weight_shell_cache.maybe_put_shell(
					enumerator.alpha,
					enumerator.beta,
					enumerator.target_weight,
					enumerator.word_bits,
					enumerator.shell_cache );
			}
		}

		return enumerator.shell_index <= enumerator.shell_cache.size();
	}

	void ModularAdditionEnumerator::reset( std::uint32_t a, std::uint32_t b, std::uint32_t hint, int cap, int bit_position, std::uint32_t prefix, int bits )
	{
		initialized = true;
		stop_due_to_limits = false;
		dfs_active = false;
		using_cached_shell = false;
		alpha = a;
		beta = b;
		word_bits = std::clamp( bits, 1, 32 );
		const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
		alpha &= mask;
		beta &= mask;
		output_hint = hint & mask;
		weight_cap = std::min( cap, word_bits - 1 );
		target_weight = 0;
		shell_index = 0;
		shell_cache.clear();
		stack_step = 0;
		stack[ stack_step++ ] = Frame { bit_position, prefix, 0u, 0 };
	}

	bool ModularAdditionEnumerator::next( DifferentialBestSearchContext& context, std::uint32_t& out_gamma, int& out_weight )
	{
		if ( !initialized || stop_due_to_limits )
			return false;

		while ( true )
		{
			if ( weight_cap < 0 || target_weight > weight_cap )
				return false;

			if ( using_cached_shell )
			{
				if ( shell_cache.empty() )
				{
					bool cache_hit = false;
					if ( context.configuration.enable_weight_sliced_pddt && target_weight <= context.configuration.weight_sliced_pddt_max_weight )
					{
						std::vector<std::uint32_t> tmp;
						if ( g_weight_shell_cache.try_get_shell( alpha, beta, target_weight, word_bits, tmp ) )
						{
							cache_hit = true;
							shell_cache = std::move( tmp );
						}
					}
					if ( cache_hit && shell_cache.empty() )
					{
						using_cached_shell = false;
						shell_cache.clear();
						shell_index = 0;
						++target_weight;
						continue;
					}
					if ( shell_cache.empty() )
					{
						bool stop = false;
						if ( !enumerate_add_shell_exact_collect( context, alpha, beta, output_hint, target_weight, word_bits, shell_cache, stop ) )
						{
							stop_due_to_limits = stop;
							return false;
						}
					}
				}

				if ( shell_index < shell_cache.size() )
				{
					out_gamma = shell_cache[ shell_index++ ];
					out_weight = target_weight;
					return true;
				}

				using_cached_shell = false;
				shell_cache.clear();
				shell_index = 0;
				++target_weight;
				continue;
			}

			if ( context.configuration.enable_weight_sliced_pddt && target_weight <= context.configuration.weight_sliced_pddt_max_weight )
			{
				std::vector<std::uint32_t> shell;
				if ( g_weight_shell_cache.try_get_shell( alpha, beta, target_weight, word_bits, shell ) )
				{
					if ( shell.empty() )
					{
						++target_weight;
						continue;
					}
					shell_cache = std::move( shell );
					using_cached_shell = true;
					shell_index = 0;
					continue;
				}

				bool stop = false;
				if ( !enumerate_add_shell_exact_collect( context, alpha, beta, output_hint, target_weight, word_bits, shell, stop ) )
				{
					stop_due_to_limits = stop;
					return false;
				}
				g_weight_shell_cache.maybe_put_shell( alpha, beta, target_weight, word_bits, shell );
				if ( shell.empty() )
				{
					++target_weight;
					continue;
				}
				shell_cache = std::move( shell );
				using_cached_shell = true;
				shell_index = 0;
				continue;
			}

			if ( !dfs_active )
			{
				dfs_active = true;
				stack_step = 0;
				stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };
			}

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
						const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
						if ( w_prefix < 0 || w_prefix > target_weight )
						{
							--stack_step;
							continue;
						}
						const int remaining_max = word_bits - frame.bit_position;
						if ( w_prefix + remaining_max < target_weight )
						{
							--stack_step;
							continue;
						}
						if ( frame.bit_position == word_bits )
						{
							if ( w_prefix == target_weight )
							{
								out_gamma = frame.prefix;
								out_weight = w_prefix;
								--stack_step;
								return true;
							}
							--stack_step;
							continue;
						}
					}

					frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
					continue;
				}

				if ( frame.state == 1 )
				{
					if ( differential_runtime_node_limit_hit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}

					frame.state = 2;
					const std::uint32_t other = 1u - frame.prefer;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
					continue;
				}

				--stack_step;
			}

			dfs_active = false;
			++target_weight;
		}
	}

	void SubConstEnumerator::reset( std::uint32_t dx, std::uint32_t sub_const, int bvweight_cap )
	{
		initialized = true;
		stop_due_to_limits = false;
		input_difference = dx;
		subtractive_constant = sub_const;
		cap_bitvector = std::clamp( bvweight_cap, 0, 32 );
		cap_dynamic_planning = std::min( 32, cap_bitvector + SLACK );
		additive_constant = std::uint32_t( 0u ) - subtractive_constant;
		output_hint = compute_greedy_output_difference_for_addition_by_constant( input_difference, additive_constant );
		stack_step = 0;
		std::array<std::uint64_t, 4> prefix_counts {};
		prefix_counts[ 0 ] = 1;
		stack[ stack_step++ ] = Frame { 0, 0u, prefix_counts, 0u, 0 };
	}

	bool SubConstEnumerator::next( DifferentialBestSearchContext& context, std::uint32_t& out_difference, int& out_weight )
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
					if ( prefix_weight_estimate > cap_dynamic_planning )
					{
						--stack_step;
						continue;
					}
				}

				if ( frame.bit_position == 32 )
				{
					const int weight = diff_subconst_exact_weight_ceil_int( input_difference, subtractive_constant, frame.prefix );
					if ( weight >= 0 && weight <= cap_bitvector )
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

				if ( frame.basis_index >= transition.rank_weight )
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

	InjectionAffineTransition compute_injection_transition_from_branch_b( std::uint32_t branch_b_input_difference )
	{
		// Thread-safe for batch search: each thread gets its own cache to avoid data races.
		static thread_local bool tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t tls_epoch = 0;
		static thread_local std::size_t cache_reserved_hint = 0;
		static thread_local std::size_t last_configured_cache_cap = std::size_t( -1 );
		// Reset thread-local state on each new "run" (so a prior OOM doesn't permanently disable caching).
		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				// Do not call tls_pool.release() while `cache` is still alive:
				// MSVC's pmr::unordered_map may still retain allocator-owned internal state
				// after rehash(0), and releasing the pool here can make the next find()/emplace()
				// observe freed storage.
				cache_reserved_hint = 0;
				last_configured_cache_cap = std::size_t( -1 );
			}
		}

		// If caching is disabled, bypass the thread-local map entirely (and avoid reusing stale entries).
		const std::size_t cap = g_injection_cache_max_entries_per_thread;
		if ( cap == 0 || tls_cache_disabled || memory_governor_in_pressure() )
		{
			if ( last_configured_cache_cap != 0 )
			{
				cache.clear();
				cache.rehash( 0 );
				cache_reserved_hint = 0;
				last_configured_cache_cap = 0;
			}
			// Optional shared cache is still valid even when per-thread caching is disabled.
			if ( g_shared_injection_cache_branch_b.enabled() )
			{
				InjectionAffineTransition cached {};
				if ( g_shared_injection_cache_branch_b.try_get( branch_b_input_difference, cached ) )
					return cached;
			}
			// Fall through to compute without caching.
		}
		else
		{
			// (Re)configure reserve if the cache cap changed between stages (e.g., auto breadth -> deep).
			if ( last_configured_cache_cap != cap )
			{
				cache.max_load_factor( 0.7f );
				cache_reserved_hint = 0;
				last_configured_cache_cap = cap;
			}
		}

		if ( cap != 0 && !tls_cache_disabled )
		{
			auto cache_iterator = cache.find( branch_b_input_difference );
			if ( cache_iterator != cache.end() )
				return cache_iterator->second;
		}

		// Optional shared cache (cross-thread). If hit, optionally populate thread-local (lock-free fast path).
		if ( g_shared_injection_cache_branch_b.enabled() )
		{
			InjectionAffineTransition cached {};
			if ( g_shared_injection_cache_branch_b.try_get( branch_b_input_difference, cached ) )
			{
				if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
				{
					// Grow TLS cache gradually (avoid huge upfront bucket allocation).
					if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
					{
						const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
						if ( next_hint > cache_reserved_hint )
						{
							try
							{
								cache.reserve( next_hint );
								cache_reserved_hint = next_hint;
							}
							catch ( const std::bad_alloc& )
							{
								tls_cache_disabled = true;
								pmr_report_oom_once( "tls_cache.reserve(branch_b)(grow)" );
							}
						}
					}
					try
					{
						cache.emplace( branch_b_input_difference, cached );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.emplace(branch_b)(shared_hit)" );
					}
				}
				return cached;
			}
		}

		InjectionAffineTransition transition {};
		const std::uint32_t f_delta = compute_injected_xor_term_from_branch_b( branch_b_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_b ^ f_delta;  // D_Δ f(0)

		// Build column space of M by evaluating D_Δ f(e_i) ⊕ D_Δ f(0) = column_i(M)
		int rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_b[ size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_b( basis_input_vector ^ branch_b_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;  // D_Δ f(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		// pack basis vectors deterministically (high-bit first)
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ size_t( packed_index++ ) ] = vector_value;
		}

		if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
		{
			if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
			{
				const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
				if ( next_hint > cache_reserved_hint )
				{
					try
					{
						cache.reserve( next_hint );
						cache_reserved_hint = next_hint;
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_b)(grow)" );
					}
				}
			}
			try
			{
				cache.emplace( branch_b_input_difference, transition );
			}
			catch ( const std::bad_alloc& )
			{
				tls_cache_disabled = true;
				pmr_report_oom_once( "tls_cache.emplace(branch_b)" );
			}
		}
		if ( g_shared_injection_cache_branch_b.enabled() )
		{
			g_shared_injection_cache_branch_b.try_put( branch_b_input_difference, transition );
		}
		return transition;
	}

	InjectionAffineTransition compute_injection_transition_from_branch_a( std::uint32_t branch_a_input_difference )
	{
		static thread_local bool tls_cache_disabled = false;
		static thread_local std::pmr::unsynchronized_pool_resource tls_pool( &pmr_bounded_resource() );
		static thread_local std::pmr::unordered_map<std::uint32_t, InjectionAffineTransition> cache( &tls_pool );
		static thread_local std::uint64_t tls_epoch = 0;
		static thread_local std::size_t cache_reserved_hint = 0;
		static thread_local std::size_t last_configured_cache_cap = std::size_t( -1 );

		{
			const std::uint64_t e = pmr_run_epoch();
			if ( tls_epoch != e )
			{
				tls_epoch = e;
				tls_cache_disabled = false;
				cache.clear();
				cache.rehash( 0 );
				// Same lifetime rule as branch-B cache above: keep the pool alive while the
				// container object exists, otherwise the next lookup can hit released storage.
				cache_reserved_hint = 0;
				last_configured_cache_cap = std::size_t( -1 );
			}
		}

		const std::size_t cap = g_injection_cache_max_entries_per_thread;
		if ( cap == 0 || tls_cache_disabled || memory_governor_in_pressure() )
		{
			if ( last_configured_cache_cap != 0 )
			{
				cache.clear();
				cache.rehash( 0 );
				cache_reserved_hint = 0;
				last_configured_cache_cap = 0;
			}
			if ( g_shared_injection_cache_branch_a.enabled() )
			{
				InjectionAffineTransition cached {};
				if ( g_shared_injection_cache_branch_a.try_get( branch_a_input_difference, cached ) )
					return cached;
			}
		}
		else
		{
			if ( last_configured_cache_cap != cap )
			{
				cache.max_load_factor( 0.7f );
				cache_reserved_hint = 0;
				last_configured_cache_cap = cap;
			}
		}

		if ( cap != 0 && !tls_cache_disabled )
		{
			auto it = cache.find( branch_a_input_difference );
			if ( it != cache.end() )
				return it->second;
		}

		if ( g_shared_injection_cache_branch_a.enabled() )
		{
			InjectionAffineTransition cached {};
			if ( g_shared_injection_cache_branch_a.try_get( branch_a_input_difference, cached ) )
			{
				if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
				{
					if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
					{
						const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
						if ( next_hint > cache_reserved_hint )
						{
							try
							{
								cache.reserve( next_hint );
								cache_reserved_hint = next_hint;
							}
							catch ( const std::bad_alloc& )
							{
								tls_cache_disabled = true;
								pmr_report_oom_once( "tls_cache.reserve(branch_a)(grow)" );
							}
						}
					}
					try
					{
						cache.emplace( branch_a_input_difference, cached );
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.emplace(branch_a)(shared_hit)" );
					}
				}
				return cached;
			}
		}

		InjectionAffineTransition transition {};
		const std::uint32_t f_delta = compute_injected_xor_term_from_branch_a( branch_a_input_difference );
		transition.offset = g_injected_xor_term_f0_branch_a ^ f_delta;  // D_Δ f(0)

		int rank = 0;
		std::array<std::uint32_t, 32> basis_by_bit {};
		for ( int i = 0; i < 32; ++i )
		{
			const std::uint32_t basis_input_vector = ( 1u << i );
			const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_a[ size_t( i ) ];
			const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_a( basis_input_vector ^ branch_a_input_difference );
			const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta;  // D_Δ f(e_i)
			const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset;
			if ( column_vector != 0u )
			{
				rank += xor_basis_add( basis_by_bit, column_vector );
			}
		}
		transition.rank_weight = rank;
		int packed_index = 0;
		for ( int bit = 31; bit >= 0; --bit )
		{
			const std::uint32_t vector_value = basis_by_bit[ size_t( bit ) ];
			if ( vector_value != 0u )
				transition.basis_vectors[ size_t( packed_index++ ) ] = vector_value;
		}

		if ( cap != 0 && !tls_cache_disabled && cache.size() < cap && !memory_governor_in_pressure() )
		{
			if ( cache_reserved_hint < cap && cache.size() + 1 > ( cache_reserved_hint * 8 ) / 10 )
			{
				const std::size_t next_hint = budgeted_reserve_target( cache.size(), compute_next_cache_reserve_hint( cache_reserved_hint, cap ), 256ull );
				if ( next_hint > cache_reserved_hint )
				{
					try
					{
						cache.reserve( next_hint );
						cache_reserved_hint = next_hint;
					}
					catch ( const std::bad_alloc& )
					{
						tls_cache_disabled = true;
						pmr_report_oom_once( "tls_cache.reserve(branch_a)(grow)" );
					}
				}
			}
			try
			{
				cache.emplace( branch_a_input_difference, transition );
			}
			catch ( const std::bad_alloc& )
			{
				tls_cache_disabled = true;
				pmr_report_oom_once( "tls_cache.emplace(branch_a)" );
			}
		}
		if ( g_shared_injection_cache_branch_a.enabled() )
		{
			g_shared_injection_cache_branch_a.try_put( branch_a_input_difference, transition );
		}
		return transition;
	}
}  // namespace TwilightDream::auto_search_differential
