#include "auto_search_frame/detail/linear_best_search_math.hpp"
#include "auto_search_frame/detail/polarity/linear/varconst/fixed_beta_strict_witness.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		constexpr std::uint64_t kSubConstOrderedCursorCheckpointTag = 0x53434F52444C4741ull;

		namespace VarConstDetail = TwilightDream::arx_operators::detail_varconst_carry_dp;

		[[nodiscard]] inline VarConstDetail::ExactAbsWeight best_first_node_abs_weight(
			const SubConstStreamingCursor::BestFirstNode& node ) noexcept
		{
			return {
				node.optimistic_abs_weight_packed,
				node.optimistic_abs_weight_is_exact_2pow64 };
		}

		inline void set_best_first_node_abs_weight(
			SubConstStreamingCursor::BestFirstNode& node,
			const VarConstDetail::ExactAbsWeight& abs_weight ) noexcept
		{
			node.optimistic_abs_weight_packed = abs_weight.packed_weight;
			node.optimistic_abs_weight_is_exact_2pow64 = abs_weight.is_exact_2pow64;
		}

		[[nodiscard]] inline bool best_first_node_worse(
			const SubConstStreamingCursor::BestFirstNode& a,
			const SubConstStreamingCursor::BestFirstNode& b ) noexcept
		{
			const VarConstDetail::ExactAbsWeight abs_a = best_first_node_abs_weight( a );
			const VarConstDetail::ExactAbsWeight abs_b = best_first_node_abs_weight( b );
			if ( VarConstDetail::exact_abs_weight_less( abs_a, abs_b ) )
				return true;
			if ( VarConstDetail::exact_abs_weight_less( abs_b, abs_a ) )
				return false;
			return a.optimistic_full_mask > b.optimistic_full_mask;
		}

		inline void sync_subconst_best_first_debug_state( SubConstStreamingCursor& cursor ) noexcept
		{
			cursor.stack_step =
				std::clamp<int>(
					static_cast<int>( cursor.heap.size() ),
					0,
					static_cast<int>( cursor.stack.size() ) );
		}

		[[nodiscard]] inline VarConstDetail::ExactAbsWeight subconst_best_first_min_abs_weight(
			const SubConstStreamingCursor& cursor ) noexcept
		{
			return { cursor.min_abs, false };
		}

		[[nodiscard]] inline std::pair<VarConstDetail::DirectionKey, VarConstDetail::uint128_t>
		compute_subconst_best_first_prefix_state(
			const SubConstStreamingCursor& cursor,
			int bit_count,
			std::uint32_t prefix ) noexcept
		{
			VarConstDetail::DirectionKey dir { 1, 0 };
			VarConstDetail::uint128_t total_scale = 1;
			for ( int bit = 0; bit < bit_count; ++bit )
			{
				const auto& M =
					( ( prefix >> bit ) & 1u ) ?
						cursor.ordered_mats_alpha1[ std::size_t( bit ) ] :
						cursor.ordered_mats_alpha0[ std::size_t( bit ) ];
				const VarConstDetail::ProjectiveState child =
					VarConstDetail::apply_projective_step( M, dir );
				if ( child.scale == 0 || total_scale == 0 )
					return { {}, 0 };
				total_scale *= child.scale;
				dir = child.dir;
			}
			return { dir, total_scale };
		}

		inline void push_subconst_best_first_node(
			SubConstStreamingCursor& cursor,
			int bit_index,
			std::uint32_t prefix,
			const VarConstDetail::DirectionKey& dir,
			VarConstDetail::uint128_t total_scale ) noexcept
		{
			if ( total_scale == 0 )
				return;

			const VarConstDetail::ProjectiveSuffixChoice suffix =
				VarConstDetail::solve_projective_bellman_suffix_choice(
					bit_index,
					dir,
					cursor.ordered_mats_alpha0,
					cursor.ordered_mats_alpha1,
					cursor.nbits,
					cursor.ordered_memo );
			const VarConstDetail::ExactAbsWeight total_abs =
				VarConstDetail::scale_exact_abs_weight_projective(
					suffix.best_abs,
					total_scale );
			if ( VarConstDetail::exact_abs_weight_less(
					 total_abs,
					 subconst_best_first_min_abs_weight( cursor ) ) )
			{
				return;
			}

			SubConstStreamingCursor::BestFirstNode node {};
			node.bit_index = bit_index;
			node.prefix = prefix;
			node.dir = dir;
			node.scale = total_scale;
			node.optimistic_full_mask =
				static_cast<std::uint64_t>( prefix ) |
				suffix.best_suffix_mask;
			set_best_first_node_abs_weight( node, total_abs );
			cursor.heap.push_back( node );
			std::push_heap(
				cursor.heap.begin(),
				cursor.heap.end(),
				best_first_node_worse );
			sync_subconst_best_first_debug_state( cursor );
		}
	}

	void rebuild_fixed_beta_sub_column_strict_witness_cursor_runtime_state(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor )
	{
		if ( !cursor.initialized )
			return;

		const std::uint32_t beta = cursor.output_mask_beta;
		const std::uint32_t constant = cursor.constant;
		const int weight_cap = cursor.weight_cap;
		const int nbits = cursor.nbits;
		const std::uint64_t replay_target = cursor.emitted_candidate_count;

		reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
			cursor,
			make_fixed_beta_sub_column_strict_witness_request(
				beta,
				constant,
				weight_cap,
				nbits ) );

		FixedBetaSubColumnStrictWitness discard {};
		while ( cursor.emitted_candidate_count < replay_target )
		{
			if ( !next_fixed_beta_sub_column_strict_witness( cursor, discard ) )
			{
				cursor.stop_due_to_limits = true;
				break;
			}
		}
	}

	void restore_fixed_beta_sub_column_strict_witness_cursor_heap_snapshot(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		const std::vector<SubConstCandidate>& heap_snapshot )
	{
		if ( !cursor.initialized )
			return;

		const std::uint32_t beta = cursor.output_mask_beta;
		const std::uint32_t constant = cursor.constant;
		const int weight_cap = cursor.weight_cap;
		const int nbits = cursor.nbits;
		const std::uint64_t emitted_candidate_count = cursor.emitted_candidate_count;
		const bool saved_stop_due_to_limits = cursor.stop_due_to_limits;

		reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
			cursor,
			make_fixed_beta_sub_column_strict_witness_request(
				beta,
				constant,
				weight_cap,
				nbits ) );
		cursor.heap.clear();
		cursor.emitted_candidate_count = emitted_candidate_count;
		cursor.stop_due_to_limits = saved_stop_due_to_limits;

		for ( const auto& entry : heap_snapshot )
		{
			const int bit_index = -( entry.linear_weight + 1 );
			if ( bit_index < 0 || bit_index > cursor.nbits )
				continue;
			const auto [ dir, total_scale ] =
				compute_subconst_best_first_prefix_state(
					cursor,
					bit_index,
					entry.input_mask_on_x );
			if ( total_scale == 0 )
				continue;
			push_subconst_best_first_node(
				cursor,
				bit_index,
				entry.input_mask_on_x,
				dir,
				total_scale );
		}

		cursor.max_gain_suffix[ 0 ] = kSubConstOrderedCursorCheckpointTag;
		cursor.max_gain_suffix[ 1 ] = cursor.emitted_candidate_count;
		sync_subconst_best_first_debug_state( cursor );
	}

	void reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedBetaSubColumnStrictWitnessRequest request )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.nbits = std::clamp( request.n_bits, 1, 32 );
		cursor.weight_cap = std::min<SearchWeight>( request.weight_cap, static_cast<SearchWeight>( cursor.nbits ) );
		const std::uint32_t mask = ( cursor.nbits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << cursor.nbits ) - 1u );
		cursor.output_mask_beta = request.output_mask_beta & mask;
		cursor.constant = request.sub_constant & mask;
		cursor.min_abs = ( cursor.weight_cap >= static_cast<SearchWeight>( cursor.nbits ) ) ? 1ull : ( 1ull << ( cursor.nbits - static_cast<int>( cursor.weight_cap ) ) );
		cursor.max_gain_suffix.fill( 0ull );
		cursor.stack.fill( SubConstStreamingCursorFrame {} );
		cursor.ordered_memo.clear();
		cursor.ordered_memo.reserve( static_cast<std::size_t>( cursor.nbits ) * 16u + 8u );
		cursor.heap.clear();
		cursor.heap.reserve( 64u );

		auto build_matrix = [ & ]( int alpha_i, int constant_i, int beta_i ) -> SubConstBitMatrix {
			SubConstBitMatrix M {};
			for ( int cin = 0; cin <= 1; ++cin )
			{
				for ( int x = 0; x <= 1; ++x )
				{
					const int cout = TwilightDream::arx_operators::carry_out_bit( x, constant_i, cin );
					const int zi = x ^ constant_i ^ cin;
					const int exponent = ( alpha_i & x ) ^ ( beta_i & zi );
					const int s = exponent ? -1 : 1;
					if ( cin == 0 && cout == 0 )
						M.m00 += s;
					else if ( cin == 0 && cout == 1 )
						M.m01 += s;
					else if ( cin == 1 && cout == 0 )
						M.m10 += s;
					else
						M.m11 += s;
				}
			}
			const int row0 = std::abs( M.m00 ) + std::abs( M.m01 );
			const int row1 = std::abs( M.m10 ) + std::abs( M.m11 );
			M.max_row_sum = static_cast<std::uint8_t>( std::max( row0, row1 ) );
			return M;
		};

		for ( int bit = 0; bit < cursor.nbits; ++bit )
		{
			if ( runtime_time_limit_reached() )
			{
				cursor.stop_due_to_limits = true;
				return;
			}
			const int constant_i = int( ( cursor.constant >> bit ) & 1u );
			const int beta_i = int( ( cursor.output_mask_beta >> bit ) & 1u );
			cursor.mats_alpha0[ std::size_t( bit ) ] = build_matrix( 0, constant_i, beta_i );
			cursor.mats_alpha1[ std::size_t( bit ) ] = build_matrix( 1, constant_i, beta_i );
			cursor.ordered_mats_alpha0[ std::size_t( bit ) ] =
				VarConstDetail::build_matrix_for_beta_bit( 0, constant_i, beta_i );
			cursor.ordered_mats_alpha1[ std::size_t( bit ) ] =
				VarConstDetail::build_matrix_for_beta_bit( 1, constant_i, beta_i );
		}

		push_subconst_best_first_node(
			cursor,
			0,
			0u,
			VarConstDetail::DirectionKey { 1, 0 },
			1 );
		cursor.max_gain_suffix[ 0 ] = kSubConstOrderedCursorCheckpointTag;
		cursor.max_gain_suffix[ 1 ] = cursor.emitted_candidate_count;
		sync_subconst_best_first_debug_state( cursor );
	}

	bool next_fixed_beta_sub_column_strict_witness(
		FixedBetaSubColumnStrictWitnessStreamingCursor& cursor,
		FixedBetaSubColumnStrictWitness& out_witness )
	{
		if ( !cursor.initialized || cursor.stop_due_to_limits )
			return false;

		while ( !cursor.heap.empty() )
		{
			if ( runtime_time_limit_reached() )
			{
				cursor.stop_due_to_limits = true;
				return false;
			}

			std::pop_heap(
				cursor.heap.begin(),
				cursor.heap.end(),
				best_first_node_worse );
			const SubConstStreamingCursor::BestFirstNode node = cursor.heap.back();
			cursor.heap.pop_back();
			sync_subconst_best_first_debug_state( cursor );

			const VarConstDetail::ExactAbsWeight node_abs =
				best_first_node_abs_weight( node );
			if ( VarConstDetail::exact_abs_weight_less(
					 node_abs,
					 subconst_best_first_min_abs_weight( cursor ) ) )
			{
				cursor.heap.clear();
				sync_subconst_best_first_debug_state( cursor );
				return false;
			}

			if ( node.bit_index >= cursor.nbits )
			{
				const int weight =
					VarConstDetail::exact_abs_weight_to_ceil_linear_weight_int(
						node_abs,
						cursor.nbits );
				if ( static_cast<SearchWeight>( weight ) <= cursor.weight_cap )
				{
					out_witness =
						make_fixed_beta_sub_column_strict_witness(
							node.prefix,
							weight );
					++cursor.emitted_candidate_count;
					cursor.max_gain_suffix[ 0 ] = kSubConstOrderedCursorCheckpointTag;
					cursor.max_gain_suffix[ 1 ] = cursor.emitted_candidate_count;
					return true;
				}
				continue;
			}

			if ( node.scale == 0 )
				continue;

			const VarConstDetail::ProjectiveState child0 =
				VarConstDetail::apply_projective_step(
					cursor.ordered_mats_alpha0[ std::size_t( node.bit_index ) ],
					node.dir );
			if ( child0.scale != 0 )
			{
				push_subconst_best_first_node(
					cursor,
					node.bit_index + 1,
					node.prefix,
					child0.dir,
					node.scale * child0.scale );
			}

			const std::uint32_t bit_mask = std::uint32_t( 1u ) << node.bit_index;
			const VarConstDetail::ProjectiveState child1 =
				VarConstDetail::apply_projective_step(
					cursor.ordered_mats_alpha1[ std::size_t( node.bit_index ) ],
					node.dir );
			if ( child1.scale != 0 )
			{
				push_subconst_best_first_node(
					cursor,
					node.bit_index + 1,
					node.prefix | bit_mask,
					child1.dir,
					node.scale * child1.scale );
			}
		}

		return false;
	}

	std::vector<FixedBetaSubColumnStrictWitness> materialize_fixed_beta_sub_column_strict_witnesses_exact(
		FixedBetaSubColumnStrictWitnessRequest request )
	{
		std::vector<FixedBetaSubColumnStrictWitness> witnesses {};
		FixedBetaSubColumnStrictWitnessStreamingCursor cursor {};
		reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
			cursor,
			request );
		if ( cursor.stop_due_to_limits )
			return witnesses;

		FixedBetaSubColumnStrictWitness witness {};
		while ( next_fixed_beta_sub_column_strict_witness( cursor, witness ) )
			witnesses.push_back( witness );
		if ( cursor.stop_due_to_limits )
			witnesses.clear();
		return witnesses;
	}

	std::vector<FixedBetaSubColumnStrictWitness> materialize_fixed_beta_sub_column_strict_witnesses(
		FixedBetaSubColumnStrictWitnessRequest request )
	{
		return materialize_fixed_beta_sub_column_strict_witnesses_exact( request );
	}

	void rebuild_subconst_streaming_cursor_runtime_state( SubConstStreamingCursor& cursor )
	{
		rebuild_fixed_beta_sub_column_strict_witness_cursor_runtime_state( cursor );
	}

	void restore_subconst_streaming_cursor_heap_snapshot(
		SubConstStreamingCursor& cursor,
		const std::vector<SubConstCandidate>& heap_snapshot )
	{
		restore_fixed_beta_sub_column_strict_witness_cursor_heap_snapshot(
			cursor,
			heap_snapshot );
	}

	void reset_subconst_streaming_cursor( SubConstStreamingCursor& cursor, std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap, int nbits )
	{
		reset_fixed_beta_sub_column_strict_witness_streaming_cursor(
			cursor,
			make_fixed_beta_sub_column_strict_witness_request(
				output_mask_beta,
				constant,
				weight_cap,
				nbits ) );
	}

	bool next_subconst_streaming_candidate( SubConstStreamingCursor& cursor, SubConstCandidate& out_candidate )
	{
		FixedBetaSubColumnStrictWitness witness {};
		if ( !next_fixed_beta_sub_column_strict_witness( cursor, witness ) )
			return false;
		out_candidate =
			project_fixed_beta_sub_column_strict_witness_to_subconst_candidate(
				witness );
		return true;
	}

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta_exact( std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap, int nbits )
	{
		return project_fixed_beta_sub_column_strict_witnesses_to_subconst_candidates(
			materialize_fixed_beta_sub_column_strict_witnesses_exact(
				make_fixed_beta_sub_column_strict_witness_request(
					output_mask_beta,
					constant,
					weight_cap,
					nbits ) ) );
	}

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta( std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap )
	{
		return generate_subconst_candidates_for_fixed_beta_exact( output_mask_beta, constant, weight_cap );
	}
}  // namespace TwilightDream::auto_search_linear
