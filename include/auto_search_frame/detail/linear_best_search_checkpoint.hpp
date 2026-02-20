#pragma once

#include "auto_search_frame/search_checkpoint.hpp"
#include "auto_search_frame/detail/linear_best_search_checkpoint_state.hpp"
#include "auto_search_frame/detail/linear_best_search_types.hpp"

namespace TwilightDream::auto_search_linear
{
	// Binary checkpoint serialization (linear)
	// ============================================================================

	std::string default_binary_checkpoint_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b );

	std::string default_runtime_log_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b );

	static inline void write_config( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearBestSearchConfiguration& c )
	{
		w.write_u8( static_cast<std::uint8_t>( c.search_mode ) );
		w.write_i32( c.round_count );
		w.write_i32( c.addition_weight_cap );
		w.write_i32( c.constant_subtraction_weight_cap );
		w.write_u8( c.enable_weight_sliced_clat ? 1u : 0u );
		w.write_u64( static_cast<std::uint64_t>( c.weight_sliced_clat_max_candidates ) );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_injection_input_masks ) );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_round_predecessors ) );
		w.write_i32( c.target_best_weight );
		w.write_u8( c.enable_state_memoization ? 1u : 0u );
		w.write_u8( c.enable_remaining_round_lower_bound ? 1u : 0u );
		w.write_u64( static_cast<std::uint64_t>( c.remaining_round_min_weight.size() ) );
		for ( const int v : c.remaining_round_min_weight )
			w.write_i32( v );
		w.write_u8( c.auto_generate_remaining_round_lower_bound ? 1u : 0u );
		w.write_u64( static_cast<std::uint64_t>( c.remaining_round_lower_bound_generation_nodes ) );
		w.write_u64( static_cast<std::uint64_t>( c.remaining_round_lower_bound_generation_seconds ) );
		w.write_u8( c.strict_remaining_round_lower_bound ? 1u : 0u );
		w.write_u8( c.enable_verbose_output ? 1u : 0u );
	}

	static inline bool read_config( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearBestSearchConfiguration& c )
	{
		std::uint8_t mode = 0;
		std::int32_t round_count = 0;
		std::int32_t add_cap = 0;
		std::int32_t sub_cap = 0;
		std::uint8_t weight_sliced_clat = 0;
		std::uint64_t weight_sliced_clat_max = 0;
		std::uint64_t max_inj = 0;
		std::uint64_t max_pred = 0;
		std::int32_t target = 0;
		std::uint8_t memo = 0;
		std::uint8_t rem = 0;
		std::uint64_t rem_size = 0;
		std::uint8_t auto_rem = 0;
		std::uint64_t rem_nodes = 0;
		std::uint64_t rem_secs = 0;
		std::uint8_t strict_rem = 0;
		std::uint8_t verbose = 0;
		if ( !r.read_u8( mode ) ) return false;
		if ( !r.read_i32( round_count ) ) return false;
		if ( !r.read_i32( add_cap ) ) return false;
		if ( !r.read_i32( sub_cap ) ) return false;
		if ( !r.read_u8( weight_sliced_clat ) ) return false;
		if ( !r.read_u64( weight_sliced_clat_max ) ) return false;
		if ( !r.read_u64( max_inj ) ) return false;
		if ( !r.read_u64( max_pred ) ) return false;
		if ( !r.read_i32( target ) ) return false;
		if ( !r.read_u8( memo ) ) return false;
		if ( !r.read_u8( rem ) ) return false;
		if ( !r.read_u64( rem_size ) ) return false;
		if ( !r.read_u8( auto_rem ) ) return false;
		if ( !r.read_u64( rem_nodes ) ) return false;
		if ( !r.read_u64( rem_secs ) ) return false;
		if ( !r.read_u8( strict_rem ) ) return false;
		if ( !r.read_u8( verbose ) ) return false;

		if ( mode > static_cast<std::uint8_t>( SearchMode::Strict ) )
			return false;
		c.search_mode = static_cast<SearchMode>( mode );
		c.round_count = round_count;
		c.addition_weight_cap = add_cap;
		c.constant_subtraction_weight_cap = sub_cap;
		c.enable_weight_sliced_clat = ( weight_sliced_clat != 0 );
		c.weight_sliced_clat_max_candidates = static_cast<std::size_t>( weight_sliced_clat_max );
		c.maximum_injection_input_masks = static_cast<std::size_t>( max_inj );
		c.maximum_round_predecessors = static_cast<std::size_t>( max_pred );
		c.target_best_weight = target;
		c.enable_state_memoization = ( memo != 0 );
		c.enable_remaining_round_lower_bound = ( rem != 0 );
		c.remaining_round_min_weight.clear();
		c.remaining_round_min_weight.resize( static_cast<std::size_t>( rem_size ) );
		for ( std::size_t i = 0; i < c.remaining_round_min_weight.size(); ++i )
		{
			std::int32_t v = 0;
			if ( !r.read_i32( v ) ) return false;
			c.remaining_round_min_weight[ i ] = v;
		}
		c.auto_generate_remaining_round_lower_bound = ( auto_rem != 0 );
		c.remaining_round_lower_bound_generation_nodes = rem_nodes;
		c.remaining_round_lower_bound_generation_seconds = rem_secs;
		c.strict_remaining_round_lower_bound = ( strict_rem != 0 );
		c.enable_verbose_output = ( verbose != 0 );
		return true;
	}

	static inline void write_trail_step( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearTrailStepRecord& s )
	{
		w.write_i32( s.round_index );
		w.write_u32( s.output_branch_a_mask );
		w.write_u32( s.output_branch_b_mask );
		w.write_u32( s.input_branch_a_mask );
		w.write_u32( s.input_branch_b_mask );
		w.write_u32( s.output_branch_b_mask_after_second_constant_subtraction );
		w.write_u32( s.input_branch_b_mask_before_second_constant_subtraction );
		w.write_i32( s.weight_second_constant_subtraction );
		w.write_u32( s.output_branch_a_mask_after_second_addition );
		w.write_u32( s.input_branch_a_mask_before_second_addition );
		w.write_u32( s.second_addition_term_mask_from_branch_b );
		w.write_i32( s.weight_second_addition );
		w.write_i32( s.weight_injection_from_branch_a );
		w.write_i32( s.weight_injection_from_branch_b );
		w.write_u32( s.chosen_correlated_input_mask_for_injection_from_branch_a );
		w.write_u32( s.chosen_correlated_input_mask_for_injection_from_branch_b );
		w.write_u32( s.output_branch_a_mask_after_first_constant_subtraction );
		w.write_u32( s.input_branch_a_mask_before_first_constant_subtraction );
		w.write_i32( s.weight_first_constant_subtraction );
		w.write_u32( s.output_branch_b_mask_after_first_addition );
		w.write_u32( s.input_branch_b_mask_before_first_addition );
		w.write_u32( s.first_addition_term_mask_from_branch_a );
		w.write_i32( s.weight_first_addition );
		w.write_i32( s.round_weight );
	}

	static inline bool read_trail_step( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearTrailStepRecord& s )
	{
		if ( !r.read_i32( s.round_index ) ) return false;
		if ( !r.read_u32( s.output_branch_a_mask ) ) return false;
		if ( !r.read_u32( s.output_branch_b_mask ) ) return false;
		if ( !r.read_u32( s.input_branch_a_mask ) ) return false;
		if ( !r.read_u32( s.input_branch_b_mask ) ) return false;
		if ( !r.read_u32( s.output_branch_b_mask_after_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.input_branch_b_mask_before_second_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.output_branch_a_mask_after_second_addition ) ) return false;
		if ( !r.read_u32( s.input_branch_a_mask_before_second_addition ) ) return false;
		if ( !r.read_u32( s.second_addition_term_mask_from_branch_b ) ) return false;
		if ( !r.read_i32( s.weight_second_addition ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.chosen_correlated_input_mask_for_injection_from_branch_a ) ) return false;
		if ( !r.read_u32( s.chosen_correlated_input_mask_for_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.output_branch_a_mask_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.input_branch_a_mask_before_first_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.output_branch_b_mask_after_first_addition ) ) return false;
		if ( !r.read_u32( s.input_branch_b_mask_before_first_addition ) ) return false;
		if ( !r.read_u32( s.first_addition_term_mask_from_branch_a ) ) return false;
		if ( !r.read_i32( s.weight_first_addition ) ) return false;
		if ( !r.read_i32( s.round_weight ) ) return false;
		return true;
	}

	static inline void write_subconst_candidate( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const SubConstCandidate& c )
	{
		w.write_u32( c.input_mask_on_x );
		w.write_i32( c.linear_weight );
	}

	static inline bool read_subconst_candidate( TwilightDream::auto_search_checkpoint::BinaryReader& r, SubConstCandidate& c )
	{
		if ( !r.read_u32( c.input_mask_on_x ) ) return false;
		if ( !r.read_i32( c.linear_weight ) ) return false;
		return true;
	}

	static inline void write_subconst_streaming_cursor( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const SubConstStreamingCursor& c )
	{
		w.write_u8( c.initialized ? 1u : 0u );
		w.write_u8( c.stop_due_to_limits ? 1u : 0u );
		w.write_u32( c.output_mask_beta );
		w.write_u32( c.constant );
		w.write_i32( c.weight_cap );
		w.write_i32( c.nbits );
		w.write_u64( c.min_abs );
		for ( const auto& m : c.mats_alpha0 )
		{
			w.write_i32( m.m00 ); w.write_i32( m.m01 ); w.write_i32( m.m10 ); w.write_i32( m.m11 ); w.write_u8( m.max_row_sum );
		}
		for ( const auto& m : c.mats_alpha1 )
		{
			w.write_i32( m.m00 ); w.write_i32( m.m01 ); w.write_i32( m.m10 ); w.write_i32( m.m11 ); w.write_u8( m.max_row_sum );
		}
		for ( const auto v : c.max_gain_suffix )
			w.write_u64( v );
		w.write_i32( c.stack_step );
		for ( const auto& frame : c.stack )
		{
			w.write_i32( frame.bit_index );
			w.write_u32( frame.prefix );
			w.write_i64( frame.v0 );
			w.write_i64( frame.v1 );
			w.write_u8( frame.state );
		}
	}

	static inline bool read_subconst_streaming_cursor( TwilightDream::auto_search_checkpoint::BinaryReader& r, SubConstStreamingCursor& c )
	{
		std::uint8_t initialized = 0;
		std::uint8_t stop_due_to_limits = 0;
		if ( !r.read_u8( initialized ) ) return false;
		if ( !r.read_u8( stop_due_to_limits ) ) return false;
		c.initialized = ( initialized != 0u );
		c.stop_due_to_limits = ( stop_due_to_limits != 0u );
		if ( !r.read_u32( c.output_mask_beta ) ) return false;
		if ( !r.read_u32( c.constant ) ) return false;
		if ( !r.read_i32( c.weight_cap ) ) return false;
		if ( !r.read_i32( c.nbits ) ) return false;
		if ( !r.read_u64( c.min_abs ) ) return false;
		for ( auto& m : c.mats_alpha0 )
		{
			if ( !r.read_i32( m.m00 ) ) return false;
			if ( !r.read_i32( m.m01 ) ) return false;
			if ( !r.read_i32( m.m10 ) ) return false;
			if ( !r.read_i32( m.m11 ) ) return false;
			if ( !r.read_u8( m.max_row_sum ) ) return false;
		}
		for ( auto& m : c.mats_alpha1 )
		{
			if ( !r.read_i32( m.m00 ) ) return false;
			if ( !r.read_i32( m.m01 ) ) return false;
			if ( !r.read_i32( m.m10 ) ) return false;
			if ( !r.read_i32( m.m11 ) ) return false;
			if ( !r.read_u8( m.max_row_sum ) ) return false;
		}
		for ( auto& v : c.max_gain_suffix )
		{
			if ( !r.read_u64( v ) ) return false;
		}
		if ( !r.read_i32( c.stack_step ) ) return false;
		c.stack_step = std::clamp( c.stack_step, 0, int( c.stack.size() ) );
		for ( auto& frame : c.stack )
		{
			if ( !r.read_i32( frame.bit_index ) ) return false;
			if ( !r.read_u32( frame.prefix ) ) return false;
			if ( !r.read_i64( frame.v0 ) ) return false;
			if ( !r.read_i64( frame.v1 ) ) return false;
			if ( !r.read_u8( frame.state ) ) return false;
		}
		return true;
	}

	static inline void write_add_candidate( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const AddCandidate& c )
	{
		w.write_u32( c.input_mask_x );
		w.write_u32( c.input_mask_y );
		w.write_i32( c.linear_weight );
	}

	static inline bool read_add_candidate( TwilightDream::auto_search_checkpoint::BinaryReader& r, AddCandidate& c )
	{
		if ( !r.read_u32( c.input_mask_x ) ) return false;
		if ( !r.read_u32( c.input_mask_y ) ) return false;
		if ( !r.read_i32( c.linear_weight ) ) return false;
		return true;
	}

	static inline void write_add_streaming_cursor( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const AddVarVarSplit8Enumerator32::StreamingCursor& c )
	{
		w.write_u8( c.initialized ? 1u : 0u );
		w.write_u8( c.stop_due_to_limits ? 1u : 0u );
		w.write_u32( c.output_mask_u );
		w.write_i32( c.weight_cap );
		w.write_i32( c.next_target_weight );
		for ( const auto b : c.output_mask_bytes )
			w.write_u8( b );
		for ( int i = 0; i < 5; ++i )
		{
			w.write_i32( c.min_remaining_weight[ std::size_t( i ) ][ 0 ] );
			w.write_i32( c.min_remaining_weight[ std::size_t( i ) ][ 1 ] );
		}
		w.write_i32( c.stack_size );
		for ( const auto& frame : c.stack )
		{
			w.write_i32( frame.block_index );
			w.write_i32( frame.connection_in );
			w.write_u32( frame.input_mask_x_acc );
			w.write_u32( frame.input_mask_y_acc );
			w.write_i32( frame.remaining_weight );
			w.write_u64( static_cast<std::uint64_t>( frame.option_index ) );
			w.write_i32( frame.target_weight );
		}
	}

	static inline bool read_add_streaming_cursor( TwilightDream::auto_search_checkpoint::BinaryReader& r, AddVarVarSplit8Enumerator32::StreamingCursor& c )
	{
		std::uint8_t initialized = 0;
		std::uint8_t stop_due_to_limits = 0;
		if ( !r.read_u8( initialized ) ) return false;
		if ( !r.read_u8( stop_due_to_limits ) ) return false;
		c.initialized = ( initialized != 0u );
		c.stop_due_to_limits = ( stop_due_to_limits != 0u );
		if ( !r.read_u32( c.output_mask_u ) ) return false;
		if ( !r.read_i32( c.weight_cap ) ) return false;
		if ( !r.read_i32( c.next_target_weight ) ) return false;
		for ( auto& b : c.output_mask_bytes )
		{
			if ( !r.read_u8( b ) ) return false;
		}
		for ( int i = 0; i < 5; ++i )
		{
			if ( !r.read_i32( c.min_remaining_weight[ std::size_t( i ) ][ 0 ] ) ) return false;
			if ( !r.read_i32( c.min_remaining_weight[ std::size_t( i ) ][ 1 ] ) ) return false;
		}
		if ( !r.read_i32( c.stack_size ) ) return false;
		c.stack_size = std::clamp( c.stack_size, 0, int( c.stack.size() ) );
		for ( auto& frame : c.stack )
		{
			std::uint64_t option_index = 0;
			if ( !r.read_i32( frame.block_index ) ) return false;
			if ( !r.read_i32( frame.connection_in ) ) return false;
			if ( !r.read_u32( frame.input_mask_x_acc ) ) return false;
			if ( !r.read_u32( frame.input_mask_y_acc ) ) return false;
			if ( !r.read_i32( frame.remaining_weight ) ) return false;
			if ( !r.read_u64( option_index ) ) return false;
			if ( !r.read_i32( frame.target_weight ) ) return false;
			frame.option_index = static_cast<std::size_t>( option_index );
		}
		return true;
	}

	static inline void write_weight_sliced_clat_streaming_cursor(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const WeightSlicedClatStreamingCursor& c )
	{
		w.write_u8( c.initialized ? 1u : 0u );
		w.write_u8( c.stop_due_to_limits ? 1u : 0u );
		w.write_u32( c.output_mask_u );
		w.write_i32( c.weight_cap );
		w.write_i32( c.next_target_weight );
		w.write_i32( c.current_target_weight );
		w.write_u32( c.input_mask_x_prefix );
		w.write_u32( c.input_mask_y_prefix );
		w.write_i32( c.z30 );
		w.write_i32( c.stack_size );
		for ( const auto& frame : c.stack )
		{
			w.write_i32( frame.bit_index );
			w.write_u32( frame.input_mask_x );
			w.write_u32( frame.input_mask_y );
			w.write_i32( frame.z_bit );
			w.write_i32( frame.z_weight_so_far );
			w.write_u8( frame.branch_state );
		}
	}

	static inline bool read_weight_sliced_clat_streaming_cursor(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		WeightSlicedClatStreamingCursor& c )
	{
		std::uint8_t initialized = 0;
		std::uint8_t stop_due_to_limits = 0;
		if ( !r.read_u8( initialized ) ) return false;
		if ( !r.read_u8( stop_due_to_limits ) ) return false;
		c.initialized = ( initialized != 0u );
		c.stop_due_to_limits = ( stop_due_to_limits != 0u );
		if ( !r.read_u32( c.output_mask_u ) ) return false;
		if ( !r.read_i32( c.weight_cap ) ) return false;
		if ( !r.read_i32( c.next_target_weight ) ) return false;
		if ( !r.read_i32( c.current_target_weight ) ) return false;
		if ( !r.read_u32( c.input_mask_x_prefix ) ) return false;
		if ( !r.read_u32( c.input_mask_y_prefix ) ) return false;
		if ( !r.read_i32( c.z30 ) ) return false;
		if ( !r.read_i32( c.stack_size ) ) return false;
		c.stack_size = std::clamp( c.stack_size, 0, int( c.stack.size() ) );
		for ( auto& frame : c.stack )
		{
			if ( !r.read_i32( frame.bit_index ) ) return false;
			if ( !r.read_u32( frame.input_mask_x ) ) return false;
			if ( !r.read_u32( frame.input_mask_y ) ) return false;
			if ( !r.read_i32( frame.z_bit ) ) return false;
			if ( !r.read_i32( frame.z_weight_so_far ) ) return false;
			if ( !r.read_u8( frame.branch_state ) ) return false;
		}
		return true;
	}

	static inline void write_injection_transition( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const InjectionCorrelationTransition& t )
	{
		w.write_u32( t.offset_mask );
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
			w.write_u32( t.basis_vectors[ i ] );
		w.write_i32( t.rank );
		w.write_i32( t.weight );
	}

	static inline bool read_injection_transition( TwilightDream::auto_search_checkpoint::BinaryReader& r, InjectionCorrelationTransition& t )
	{
		if ( !r.read_u32( t.offset_mask ) ) return false;
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
		{
			if ( !r.read_u32( t.basis_vectors[ i ] ) ) return false;
		}
		if ( !r.read_i32( t.rank ) ) return false;
		if ( !r.read_i32( t.weight ) ) return false;
		return true;
	}

	static inline void write_linear_affine_mask_enumerator( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearAffineMaskEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		w.write_u64( static_cast<std::uint64_t>( e.maximum_input_mask_count ) );
		w.write_u64( static_cast<std::uint64_t>( e.produced_count ) );
		w.write_i32( e.rank );
		w.write_i32( e.stack_step );
		for ( int i = 0; i < LinearAffineMaskEnumerator::MAX_STACK; ++i )
		{
			w.write_i32( e.stack[ i ].basis_index );
			w.write_u32( e.stack[ i ].current_mask );
			w.write_u8( e.stack[ i ].branch_state );
		}
	}

	static inline bool read_linear_affine_mask_enumerator( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearAffineMaskEnumerator& e )
	{
		std::uint8_t initialized = 0;
		std::uint8_t stop_due_to_limits = 0;
		std::uint64_t maximum_input_mask_count = 0;
		std::uint64_t produced_count = 0;
		int rank = 0;
		int stack_step = 0;
		if ( !r.read_u8( initialized ) ) return false;
		if ( !r.read_u8( stop_due_to_limits ) ) return false;
		if ( !r.read_u64( maximum_input_mask_count ) ) return false;
		if ( !r.read_u64( produced_count ) ) return false;
		if ( !r.read_i32( rank ) ) return false;
		if ( !r.read_i32( stack_step ) ) return false;
		e.initialized = ( initialized != 0u );
		e.stop_due_to_limits = ( stop_due_to_limits != 0u );
		e.maximum_input_mask_count = static_cast<std::size_t>( maximum_input_mask_count );
		e.produced_count = static_cast<std::size_t>( produced_count );
		e.rank = rank;
		e.stack_step = std::clamp( stack_step, 0, LinearAffineMaskEnumerator::MAX_STACK );
		for ( int i = 0; i < LinearAffineMaskEnumerator::MAX_STACK; ++i )
		{
			if ( !r.read_i32( e.stack[ i ].basis_index ) ) return false;
			if ( !r.read_u32( e.stack[ i ].current_mask ) ) return false;
			if ( !r.read_u8( e.stack[ i ].branch_state ) ) return false;
		}
		return true;
	}

	static inline void write_round_state( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearRoundSearchState& s )
	{
		w.write_i32( s.round_boundary_depth );
		w.write_i32( s.accumulated_weight_so_far );
		w.write_i32( s.round_index );
		w.write_i32( s.round_weight_cap );
		w.write_i32( s.remaining_round_weight_lower_bound_after_this_round );
		w.write_u32( s.round_output_branch_a_mask );
		w.write_u32( s.round_output_branch_b_mask );
		w.write_u32( s.branch_a_round_output_mask_before_inj_from_a );
		w.write_u32( s.branch_b_mask_before_injection_from_branch_a );
		write_injection_transition( w, s.injection_from_branch_a_transition );
		write_linear_affine_mask_enumerator( w, s.injection_from_branch_a_enumerator );
		w.write_i32( s.weight_injection_from_branch_a );
		w.write_i32( s.remaining_after_inj_a );
		w.write_i32( s.second_subconst_weight_cap );
		w.write_i32( s.second_add_weight_cap );
		w.write_u32( s.chosen_correlated_input_mask_for_injection_from_branch_a );
		w.write_u32( s.output_branch_a_mask_after_second_addition );
		w.write_u32( s.output_branch_b_mask_after_second_constant_subtraction );

		write_subconst_streaming_cursor( w, s.second_constant_subtraction_stream_cursor );
		write_weight_sliced_clat_streaming_cursor( w, s.second_addition_weight_sliced_clat_stream_cursor );
		w.write_u64( static_cast<std::uint64_t>( s.second_constant_subtraction_candidates_for_branch_b.size() ) );
		for ( const auto& c : s.second_constant_subtraction_candidates_for_branch_b )
			write_subconst_candidate( w, c );
		write_add_streaming_cursor( w, s.second_addition_stream_cursor );
		w.write_u64( static_cast<std::uint64_t>( s.second_addition_candidates_storage.size() ) );
		for ( const auto& c : s.second_addition_candidates_storage )
			write_add_candidate( w, c );
		w.write_u64( static_cast<std::uint64_t>( s.second_addition_candidate_index ) );

		w.write_u32( s.input_branch_a_mask_before_second_addition );
		w.write_u32( s.second_addition_term_mask_from_branch_b );
		w.write_i32( s.weight_second_addition );
		w.write_u32( s.branch_b_mask_contribution_from_second_addition_term );
		write_injection_transition( w, s.injection_from_branch_b_transition );
		write_linear_affine_mask_enumerator( w, s.injection_from_branch_b_enumerator );
		w.write_i32( s.weight_injection_from_branch_b );
		w.write_i32( s.base_weight_after_inj_b );
		w.write_u32( s.chosen_correlated_input_mask_for_injection_from_branch_b );
		w.write_u32( s.input_branch_b_mask_before_second_constant_subtraction );
		w.write_i32( s.weight_second_constant_subtraction );
		w.write_u32( s.branch_b_mask_after_second_add_term_removed );
		w.write_u32( s.branch_b_mask_after_first_xor_with_rotated_branch_a_base );
		w.write_u64( static_cast<std::uint64_t>( s.second_constant_subtraction_candidate_index ) );
		w.write_i32( s.base_weight_after_second_subconst );
		w.write_i32( s.first_subconst_weight_cap );
		w.write_i32( s.first_add_weight_cap );
		w.write_u32( s.output_branch_a_mask_after_first_constant_subtraction );
		w.write_u32( s.output_branch_b_mask_after_first_addition );
		write_subconst_streaming_cursor( w, s.first_constant_subtraction_stream_cursor );
		write_weight_sliced_clat_streaming_cursor( w, s.first_addition_weight_sliced_clat_stream_cursor );
		w.write_u64( static_cast<std::uint64_t>( s.first_constant_subtraction_candidates_for_branch_a.size() ) );
		for ( const auto& c : s.first_constant_subtraction_candidates_for_branch_a )
			write_subconst_candidate( w, c );
		write_add_streaming_cursor( w, s.first_addition_stream_cursor );
		w.write_u64( static_cast<std::uint64_t>( s.first_addition_candidates_for_branch_b.size() ) );
		for ( const auto& c : s.first_addition_candidates_for_branch_b )
			write_add_candidate( w, c );
		w.write_u64( static_cast<std::uint64_t>( s.first_constant_subtraction_candidate_index ) );
		w.write_u64( static_cast<std::uint64_t>( s.first_addition_candidate_index ) );
		w.write_u32( s.input_branch_a_mask_before_first_constant_subtraction_current );
		w.write_i32( s.weight_first_constant_subtraction_current );

		w.write_u64( static_cast<std::uint64_t>( s.round_predecessors.size() ) );
		for ( const auto& step : s.round_predecessors )
			write_trail_step( w, step );
	}

	static inline bool read_round_state( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearRoundSearchState& s )
	{
		if ( !r.read_i32( s.round_boundary_depth ) ) return false;
		if ( !r.read_i32( s.accumulated_weight_so_far ) ) return false;
		if ( !r.read_i32( s.round_index ) ) return false;
		if ( !r.read_i32( s.round_weight_cap ) ) return false;
		if ( !r.read_i32( s.remaining_round_weight_lower_bound_after_this_round ) ) return false;
		if ( !r.read_u32( s.round_output_branch_a_mask ) ) return false;
		if ( !r.read_u32( s.round_output_branch_b_mask ) ) return false;
		if ( !r.read_u32( s.branch_a_round_output_mask_before_inj_from_a ) ) return false;
		if ( !r.read_u32( s.branch_b_mask_before_injection_from_branch_a ) ) return false;
		if ( !read_injection_transition( r, s.injection_from_branch_a_transition ) ) return false;
		if ( !read_linear_affine_mask_enumerator( r, s.injection_from_branch_a_enumerator ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_i32( s.remaining_after_inj_a ) ) return false;
		if ( !r.read_i32( s.second_subconst_weight_cap ) ) return false;
		if ( !r.read_i32( s.second_add_weight_cap ) ) return false;
		if ( !r.read_u32( s.chosen_correlated_input_mask_for_injection_from_branch_a ) ) return false;
		if ( !r.read_u32( s.output_branch_a_mask_after_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_mask_after_second_constant_subtraction ) ) return false;

		if ( !read_subconst_streaming_cursor( r, s.second_constant_subtraction_stream_cursor ) ) return false;
		if ( !read_weight_sliced_clat_streaming_cursor( r, s.second_addition_weight_sliced_clat_stream_cursor ) ) return false;
		std::uint64_t subconst_count = 0;
		if ( !r.read_u64( subconst_count ) ) return false;
		s.second_constant_subtraction_candidates_for_branch_b.clear();
		s.second_constant_subtraction_candidates_for_branch_b.resize( static_cast<std::size_t>( subconst_count ) );
		for ( auto& c : s.second_constant_subtraction_candidates_for_branch_b )
		{
			if ( !read_subconst_candidate( r, c ) ) return false;
		}
		if ( !read_add_streaming_cursor( r, s.second_addition_stream_cursor ) ) return false;
		std::uint64_t second_add_count = 0;
		if ( !r.read_u64( second_add_count ) ) return false;
		s.second_addition_candidates_storage.clear();
		s.second_addition_candidates_storage.resize( static_cast<std::size_t>( second_add_count ) );
		for ( auto& c : s.second_addition_candidates_storage )
		{
			if ( !read_add_candidate( r, c ) ) return false;
		}
		std::uint64_t second_add_index = 0;
		if ( !r.read_u64( second_add_index ) ) return false;
		s.second_addition_candidate_index = static_cast<std::size_t>( second_add_index );

		if ( !r.read_u32( s.input_branch_a_mask_before_second_addition ) ) return false;
		if ( !r.read_u32( s.second_addition_term_mask_from_branch_b ) ) return false;
		if ( !r.read_i32( s.weight_second_addition ) ) return false;
		if ( !r.read_u32( s.branch_b_mask_contribution_from_second_addition_term ) ) return false;
		if ( !read_injection_transition( r, s.injection_from_branch_b_transition ) ) return false;
		if ( !read_linear_affine_mask_enumerator( r, s.injection_from_branch_b_enumerator ) ) return false;
		if ( !r.read_i32( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_i32( s.base_weight_after_inj_b ) ) return false;
		if ( !r.read_u32( s.chosen_correlated_input_mask_for_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.input_branch_b_mask_before_second_constant_subtraction ) ) return false;
		if ( !r.read_i32( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_b_mask_after_second_add_term_removed ) ) return false;
		if ( !r.read_u32( s.branch_b_mask_after_first_xor_with_rotated_branch_a_base ) ) return false;
		std::uint64_t second_subconst_index = 0;
		if ( !r.read_u64( second_subconst_index ) ) return false;
		s.second_constant_subtraction_candidate_index = static_cast<std::size_t>( second_subconst_index );
		if ( !r.read_i32( s.base_weight_after_second_subconst ) ) return false;
		if ( !r.read_i32( s.first_subconst_weight_cap ) ) return false;
		if ( !r.read_i32( s.first_add_weight_cap ) ) return false;
		if ( !r.read_u32( s.output_branch_a_mask_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.output_branch_b_mask_after_first_addition ) ) return false;
		if ( !read_subconst_streaming_cursor( r, s.first_constant_subtraction_stream_cursor ) ) return false;
		if ( !read_weight_sliced_clat_streaming_cursor( r, s.first_addition_weight_sliced_clat_stream_cursor ) ) return false;
		std::uint64_t first_subconst_count = 0;
		if ( !r.read_u64( first_subconst_count ) ) return false;
		s.first_constant_subtraction_candidates_for_branch_a.clear();
		s.first_constant_subtraction_candidates_for_branch_a.resize( static_cast<std::size_t>( first_subconst_count ) );
		for ( auto& c : s.first_constant_subtraction_candidates_for_branch_a )
		{
			if ( !read_subconst_candidate( r, c ) ) return false;
		}
		if ( !read_add_streaming_cursor( r, s.first_addition_stream_cursor ) ) return false;
		std::uint64_t first_add_count = 0;
		if ( !r.read_u64( first_add_count ) ) return false;
		s.first_addition_candidates_for_branch_b.clear();
		s.first_addition_candidates_for_branch_b.resize( static_cast<std::size_t>( first_add_count ) );
		for ( auto& c : s.first_addition_candidates_for_branch_b )
		{
			if ( !read_add_candidate( r, c ) ) return false;
		}
		std::uint64_t first_subconst_index = 0;
		std::uint64_t first_add_index = 0;
		if ( !r.read_u64( first_subconst_index ) ) return false;
		if ( !r.read_u64( first_add_index ) ) return false;
		s.first_constant_subtraction_candidate_index = static_cast<std::size_t>( first_subconst_index );
		s.first_addition_candidate_index = static_cast<std::size_t>( first_add_index );
		if ( !r.read_u32( s.input_branch_a_mask_before_first_constant_subtraction_current ) ) return false;
		if ( !r.read_i32( s.weight_first_constant_subtraction_current ) ) return false;

		std::uint64_t pred_count = 0;
		if ( !r.read_u64( pred_count ) ) return false;
		s.round_predecessors.clear();
		s.round_predecessors.resize( static_cast<std::size_t>( pred_count ) );
		for ( auto& step : s.round_predecessors )
		{
			if ( !read_trail_step( r, step ) ) return false;
		}

		s.second_addition_candidates_for_branch_a = nullptr;
		return true;
	}

	static inline void write_search_frame( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearSearchFrame& f )
	{
		w.write_u8( static_cast<std::uint8_t>( f.stage ) );
		w.write_u64( static_cast<std::uint64_t>( f.trail_size_at_entry ) );
		w.write_u64( static_cast<std::uint64_t>( f.predecessor_index ) );
		write_round_state( w, f.state );
	}

	static inline bool read_search_frame( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearSearchFrame& f )
	{
		std::uint8_t stage = 0;
		std::uint64_t trail_size = 0;
		std::uint64_t pred_index = 0;
		if ( !r.read_u8( stage ) ) return false;
		if ( !r.read_u64( trail_size ) ) return false;
		if ( !r.read_u64( pred_index ) ) return false;
		f.stage = static_cast<LinearSearchStage>( stage );
		f.trail_size_at_entry = static_cast<std::size_t>( trail_size );
		f.predecessor_index = static_cast<std::size_t>( pred_index );
		if ( !read_round_state( r, f.state ) ) return false;
		return true;
	}

	static inline void write_cursor( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearSearchCursor& cursor )
	{
		w.write_u64( static_cast<std::uint64_t>( cursor.stack.size() ) );
		for ( const auto& f : cursor.stack )
			write_search_frame( w, f );
	}

	static inline bool read_cursor( TwilightDream::auto_search_checkpoint::BinaryReader& r, LinearSearchCursor& cursor )
	{
		std::uint64_t count = 0;
		if ( !r.read_u64( count ) ) return false;
		cursor.stack.clear();
		cursor.stack.resize( static_cast<std::size_t>( count ) );
		for ( auto& f : cursor.stack )
		{
			if ( !read_search_frame( r, f ) ) return false;
		}
		return true;
	}

	static inline void write_trail_vector( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const std::vector<LinearTrailStepRecord>& v )
	{
		w.write_u64( static_cast<std::uint64_t>( v.size() ) );
		for ( const auto& s : v )
			write_trail_step( w, s );
	}

	static inline bool read_trail_vector( TwilightDream::auto_search_checkpoint::BinaryReader& r, std::vector<LinearTrailStepRecord>& v )
	{
		std::uint64_t count = 0;
		if ( !r.read_u64( count ) ) return false;
		v.clear();
		v.resize( static_cast<std::size_t>( count ) );
		for ( auto& s : v )
		{
			if ( !read_trail_step( r, s ) ) return false;
		}
		return true;
	}

	struct LinearCheckpointLoadResult
	{
		LinearBestSearchConfiguration configuration {};
		std::uint32_t start_mask_a = 0;
		std::uint32_t start_mask_b = 0;
		std::string	  history_log_path {};
		std::string	  runtime_log_path {};
		std::uint64_t accumulated_elapsed_usec = 0;
		std::uint64_t total_nodes_visited = 0;
		std::uint64_t run_nodes_visited = 0;
		std::uint64_t runtime_maximum_search_nodes = 0;
		std::uint64_t runtime_maximum_search_seconds = 0;
		std::uint64_t runtime_progress_every_seconds = 0;
		std::uint64_t runtime_checkpoint_every_seconds = 0;
		std::uint64_t runtime_progress_node_mask = 0;
		bool		  last_run_hit_node_limit = false;
		bool		  last_run_hit_time_limit = false;
		int best_weight = INFINITE_WEIGHT;
		std::uint32_t best_input_mask_a = 0;
		std::uint32_t best_input_mask_b = 0;
		std::vector<LinearTrailStepRecord> best_trail;
		std::vector<LinearTrailStepRecord> current_trail;
		LinearSearchCursor cursor;
	};

	struct LinearResumeFingerprint
	{
		std::uint64_t hash = 0;
		std::uint64_t cursor_stack_depth = 0;
		std::uint64_t current_trail_size = 0;
		std::uint64_t candidate_vector_size_total = 0;
		std::uint64_t candidate_index_total = 0;
		std::uint64_t streaming_cursor_stack_size_total = 0;
		std::uint64_t affine_outputs_produced_total = 0;
	};

	LinearResumeFingerprint compute_linear_resume_fingerprint( const LinearBestSearchContext& context, const LinearSearchCursor& cursor ) noexcept;

	LinearResumeFingerprint compute_linear_resume_fingerprint( const LinearCheckpointLoadResult& load ) noexcept;

	void write_linear_resume_fingerprint_fields(
		std::ostream& out,
		const LinearResumeFingerprint& fingerprint,
		const char* prefix = "resume_fingerprint_" );

	void linear_runtime_log_resume_event(
		const LinearBestSearchContext& context,
		const LinearSearchCursor& cursor,
		const char* event_name,
		const char* reason = "running" );

	bool write_linear_checkpoint_payload( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, std::uint64_t elapsed_usec );

	bool read_linear_checkpoint_payload(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearCheckpointLoadResult& out,
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int>& memo );

	bool write_linear_checkpoint( const std::string& path, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, std::uint64_t elapsed_usec );

	bool read_linear_checkpoint( const std::string& path, LinearCheckpointLoadResult& out, TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int>& memo );

	void write_linear_resume_snapshot(
		BestWeightHistory& history,
		const LinearBestSearchContext& context,
		const LinearSearchCursor& cursor,
		const char* reason );

}  // namespace TwilightDream::auto_search_linear
