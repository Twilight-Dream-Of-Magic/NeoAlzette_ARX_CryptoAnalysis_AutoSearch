#pragma once

#include "auto_search_frame/search_checkpoint.hpp"
#include "auto_search_frame/detail/differential_best_search_checkpoint_state.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"

namespace TwilightDream::auto_search_differential
{

	std::string default_binary_checkpoint_path( int round_count, std::uint32_t da, std::uint32_t db );

	std::string default_runtime_log_path( int round_count, std::uint32_t da, std::uint32_t db );

	static inline void write_config( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialBestSearchConfiguration& c )
	{
		w.write_i32( c.round_count );
		w.write_u64( c.addition_weight_cap );
		w.write_u64( c.constant_subtraction_weight_cap );
		w.write_u64( static_cast<std::uint64_t>( c.maximum_transition_output_differences ) );
		w.write_u8( c.enable_state_memoization ? 1u : 0u );
		w.write_u8( c.enable_remaining_round_lower_bound ? 1u : 0u );
		w.write_u64( c.target_best_weight );
		w.write_u64( static_cast<std::uint64_t>( c.remaining_round_min_weight.size() ) );
		for ( const SearchWeight v : c.remaining_round_min_weight )
			w.write_u64( v );
		w.write_u8( c.strict_remaining_round_lower_bound ? 1u : 0u );
		w.write_u8( c.enable_verbose_output ? 1u : 0u );
		w.write_u8( c.enable_weight_sliced_pddt ? 1u : 0u );
		w.write_u64( c.weight_sliced_pddt_max_weight );
	}

	static inline bool read_config( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialBestSearchConfiguration& c )
	{
		std::int32_t round_count = 0;
		SearchWeight add_cap = 0;
		SearchWeight sub_cap = 0;
		std::uint64_t max_trans = 0;
		std::uint8_t memo = 0;
		std::uint8_t rem = 0;
		SearchWeight target = INFINITE_WEIGHT;
		std::uint64_t rem_size = 0;
		std::uint8_t strict_rem = 0;
		std::uint8_t verbose = 0;
		std::uint8_t weight_sliced_pddt = 0;
		SearchWeight weight_sliced_pddt_w = INFINITE_WEIGHT;
		if ( !r.read_i32( round_count ) ) return false;
		if ( !r.read_u64( add_cap ) ) return false;
		if ( !r.read_u64( sub_cap ) ) return false;
		if ( !r.read_u64( max_trans ) ) return false;
		if ( !r.read_u8( memo ) ) return false;
		if ( !r.read_u8( rem ) ) return false;
		if ( !r.read_u64( target ) ) return false;
		if ( !r.read_u64( rem_size ) ) return false;
		if ( !r.read_u8( strict_rem ) ) return false;
		if ( !r.read_u8( verbose ) ) return false;
		if ( !r.read_u8( weight_sliced_pddt ) ) return false;
		if ( !r.read_u64( weight_sliced_pddt_w ) ) return false;

		c.round_count = round_count;
		c.addition_weight_cap = add_cap;
		c.constant_subtraction_weight_cap = sub_cap;
		c.maximum_transition_output_differences = static_cast<std::size_t>( max_trans );
		c.enable_state_memoization = ( memo != 0 );
		c.enable_remaining_round_lower_bound = ( rem != 0 );
		c.target_best_weight = target;
		c.remaining_round_min_weight.clear();
		c.remaining_round_min_weight.resize( static_cast<std::size_t>( rem_size ) );
		for ( std::size_t i = 0; i < c.remaining_round_min_weight.size(); ++i )
		{
			SearchWeight v = 0;
			if ( !r.read_u64( v ) ) return false;
			c.remaining_round_min_weight[ i ] = v;
		}
		c.strict_remaining_round_lower_bound = ( strict_rem != 0 );
		c.enable_verbose_output = ( verbose != 0 );
		c.enable_weight_sliced_pddt = ( weight_sliced_pddt != 0 );
		c.weight_sliced_pddt_max_weight = weight_sliced_pddt_w;
		return true;
	}

	static inline void write_trail_step( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialTrailStepRecord& s )
	{
		w.write_i32( s.round_index );
		w.write_u32( s.input_branch_a_difference );
		w.write_u32( s.input_branch_b_difference );
		w.write_u32( s.first_addition_term_difference );
		w.write_u32( s.output_branch_b_difference_after_first_addition );
		w.write_u64( s.weight_first_addition );
		w.write_u32( s.output_branch_a_difference_after_first_constant_subtraction );
		w.write_u64( s.weight_first_constant_subtraction );
		w.write_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b );
		w.write_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a );
		w.write_u32( s.injection_from_branch_b_xor_difference );
		w.write_u64( s.weight_injection_from_branch_b );
		w.write_u32( s.branch_a_difference_after_injection_from_branch_b );
		w.write_u32( s.branch_b_difference_after_first_bridge );
		w.write_u32( s.second_addition_term_difference );
		w.write_u32( s.output_branch_a_difference_after_second_addition );
		w.write_u64( s.weight_second_addition );
		w.write_u32( s.output_branch_b_difference_after_second_constant_subtraction );
		w.write_u64( s.weight_second_constant_subtraction );
		w.write_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a );
		w.write_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b );
		w.write_u32( s.injection_from_branch_a_xor_difference );
		w.write_u64( s.weight_injection_from_branch_a );
		w.write_u32( s.output_branch_a_difference );
		w.write_u32( s.output_branch_b_difference );
		w.write_u64( s.round_weight );
	}

	static inline bool read_trail_step( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialTrailStepRecord& s )
	{
		if ( !r.read_i32( s.round_index ) ) return false;
		if ( !r.read_u32( s.input_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.input_branch_b_difference ) ) return false;
		if ( !r.read_u32( s.first_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_u64( s.weight_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.weight_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_b_xor_difference ) ) return false;
		if ( !r.read_u64( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_bridge ) ) return false;
		if ( !r.read_u32( s.second_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_u64( s.weight_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_second_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_a_xor_difference ) ) return false;
		if ( !r.read_u64( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference ) ) return false;
		if ( !r.read_u64( s.round_weight ) ) return false;
		return true;
	}

	static inline void write_round_state( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialRoundSearchState& s )
	{
		w.write_i32( s.round_boundary_depth );
		w.write_u64( s.accumulated_weight_so_far );
		w.write_u64( s.remaining_round_weight_lower_bound_after_this_round );
		w.write_u32( s.branch_a_input_difference );
		w.write_u32( s.branch_b_input_difference );
		write_trail_step( w, s.base_step );
		w.write_u32( s.first_addition_term_difference );
		w.write_u32( s.optimal_output_branch_b_difference_after_first_addition );
		w.write_u64( s.optimal_weight_first_addition );
		w.write_u64( s.weight_cap_first_addition );
		w.write_u32( s.output_branch_b_difference_after_first_addition );
		w.write_u64( s.weight_first_addition );
		w.write_u64( s.accumulated_weight_after_first_addition );
		w.write_u32( s.output_branch_a_difference_after_first_constant_subtraction );
		w.write_u64( s.weight_first_constant_subtraction );
		w.write_u64( s.accumulated_weight_after_first_constant_subtraction );
		w.write_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b );
		w.write_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a );
		w.write_u32( s.branch_b_difference_after_first_bridge );
		w.write_u64( s.weight_injection_from_branch_b );
		w.write_u64( s.accumulated_weight_before_second_addition );
		w.write_u32( s.injection_from_branch_b_xor_difference );
		w.write_u32( s.branch_a_difference_after_injection_from_branch_b );
		w.write_u32( s.second_addition_term_difference );
		w.write_u32( s.optimal_output_branch_a_difference_after_second_addition );
		w.write_u64( s.optimal_weight_second_addition );
		w.write_u64( s.weight_cap_second_addition );
		w.write_u32( s.output_branch_a_difference_after_second_addition );
		w.write_u64( s.weight_second_addition );
		w.write_u64( s.accumulated_weight_after_second_addition );
		w.write_u32( s.output_branch_b_difference_after_second_constant_subtraction );
		w.write_u64( s.weight_second_constant_subtraction );
		w.write_u64( s.accumulated_weight_after_second_constant_subtraction );
		w.write_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a );
		w.write_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b );
		w.write_u64( s.weight_injection_from_branch_a );
		w.write_u64( s.accumulated_weight_at_round_end );
		w.write_u32( s.injection_from_branch_a_xor_difference );
		w.write_u32( s.output_branch_a_difference );
		w.write_u32( s.output_branch_b_difference );
	}

	static inline bool read_round_state( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialRoundSearchState& s )
	{
		if ( !r.read_i32( s.round_boundary_depth ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_so_far ) ) return false;
		if ( !r.read_u64( s.remaining_round_weight_lower_bound_after_this_round ) ) return false;
		if ( !r.read_u32( s.branch_a_input_difference ) ) return false;
		if ( !r.read_u32( s.branch_b_input_difference ) ) return false;
		if ( !read_trail_step( r, s.base_step ) ) return false;
		if ( !r.read_u32( s.first_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.optimal_output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_u64( s.optimal_weight_first_addition ) ) return false;
		if ( !r.read_u64( s.weight_cap_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_first_addition ) ) return false;
		if ( !r.read_u64( s.weight_first_addition ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_after_first_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.weight_first_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_after_first_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_first_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_first_bridge ) ) return false;
		if ( !r.read_u64( s.weight_injection_from_branch_b ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_before_second_addition ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_b_xor_difference ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_injection_from_branch_b ) ) return false;
		if ( !r.read_u32( s.second_addition_term_difference ) ) return false;
		if ( !r.read_u32( s.optimal_output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_u64( s.optimal_weight_second_addition ) ) return false;
		if ( !r.read_u64( s.weight_cap_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference_after_second_addition ) ) return false;
		if ( !r.read_u64( s.weight_second_addition ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_after_second_addition ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference_after_second_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.weight_second_constant_subtraction ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_after_second_constant_subtraction ) ) return false;
		if ( !r.read_u32( s.branch_b_difference_after_second_xor_with_rotated_branch_a ) ) return false;
		if ( !r.read_u32( s.branch_a_difference_after_second_xor_with_rotated_branch_b ) ) return false;
		if ( !r.read_u64( s.weight_injection_from_branch_a ) ) return false;
		if ( !r.read_u64( s.accumulated_weight_at_round_end ) ) return false;
		if ( !r.read_u32( s.injection_from_branch_a_xor_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_a_difference ) ) return false;
		if ( !r.read_u32( s.output_branch_b_difference ) ) return false;
		return true;
	}

	static inline void write_injection_transition( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const InjectionAffineTransition& t )
	{
		w.write_u32( t.offset );
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
			w.write_u32( t.basis_vectors[ i ] );
		w.write_u64( t.rank_weight );
	}

	static inline bool read_injection_transition( TwilightDream::auto_search_checkpoint::BinaryReader& r, InjectionAffineTransition& t )
	{
		if ( !r.read_u32( t.offset ) ) return false;
		for ( std::size_t i = 0; i < t.basis_vectors.size(); ++i )
		{
			if ( !r.read_u32( t.basis_vectors[ i ] ) ) return false;
		}
		if ( !r.read_u64( t.rank_weight ) ) return false;
		return true;
	}

	static inline void write_mod_add_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const ModularAdditionEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		w.write_u32( e.alpha );
		w.write_u32( e.beta );
		w.write_u32( e.output_hint );
		w.write_u64( e.weight_cap );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.bit_position );
			w.write_u32( f.prefix );
			w.write_u32( f.prefer );
			w.write_u8( f.state );
		}
		// Extension block (required).
		static constexpr std::uint32_t kModAddEnumExtTag = 0x584D4144u; // 'DAMX'
		w.write_u32( kModAddEnumExtTag );
		w.write_u8( e.dfs_active ? 1u : 0u );
		w.write_u8( e.using_cached_shell ? 1u : 0u );
		w.write_u64( e.target_weight );
		w.write_i32( e.word_bits );
		w.write_u64( static_cast<std::uint64_t>( e.shell_index ) );
	}

	static inline bool read_mod_add_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, ModularAdditionEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !r.read_u32( e.alpha ) ) return false;
		if ( !r.read_u32( e.beta ) ) return false;
		if ( !r.read_u32( e.output_hint ) ) return false;
		if ( !r.read_u64( e.weight_cap ) ) return false;
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.bit_position ) ) return false;
			if ( !r.read_u32( f.prefix ) ) return false;
			if ( !r.read_u32( f.prefer ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		// Extension block (required).
		{
			std::uint32_t tag = 0;
			if ( !r.read_u32( tag ) ) return false;
			if ( tag != 0x584D4144u ) return false;
			std::uint8_t dfs = 0;
			std::uint8_t cached = 0;
			SearchWeight target = 0;
			std::int32_t bits = 32;
			std::uint64_t index = 0;
			if ( !r.read_u8( dfs ) ) return false;
			if ( !r.read_u8( cached ) ) return false;
			if ( !r.read_u64( target ) ) return false;
			if ( !r.read_i32( bits ) ) return false;
			if ( !r.read_u64( index ) ) return false;
			e.dfs_active = ( dfs != 0u );
			e.using_cached_shell = ( cached != 0u );
			e.target_weight = target;
			e.word_bits = std::clamp( bits, 1, 32 );
			e.shell_index = static_cast<std::size_t>( index );
		}

		e.shell_cache.clear();
		return true;
	}

	static inline void write_subconst_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const SubConstEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		w.write_u32( e.input_difference );
		w.write_u32( e.subtractive_constant );
		w.write_u32( e.additive_constant );
		w.write_u32( e.output_hint );
		w.write_u64( e.cap_bitvector );
		w.write_u64( e.cap_dynamic_planning );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.bit_position );
			w.write_u32( f.prefix );
			for ( std::uint64_t v : f.prefix_counts )
				w.write_u64( v );
			w.write_u32( f.preferred_bit );
			w.write_u8( f.state );
		}
	}

	static inline bool read_subconst_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, SubConstEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !r.read_u32( e.input_difference ) ) return false;
		if ( !r.read_u32( e.subtractive_constant ) ) return false;
		if ( !r.read_u32( e.additive_constant ) ) return false;
		if ( !r.read_u32( e.output_hint ) ) return false;
		if ( !r.read_u64( e.cap_bitvector ) ) return false;
		if ( !r.read_u64( e.cap_dynamic_planning ) ) return false;
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.bit_position ) ) return false;
			if ( !r.read_u32( f.prefix ) ) return false;
			for ( std::uint64_t& v : f.prefix_counts )
			{
				if ( !r.read_u64( v ) ) return false;
			}
			if ( !r.read_u32( f.preferred_bit ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		return true;
	}

	static inline void write_affine_enum( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const AffineSubspaceEnumerator& e )
	{
		w.write_u8( e.initialized ? 1u : 0u );
		w.write_u8( e.stop_due_to_limits ? 1u : 0u );
		write_injection_transition( w, e.transition );
		w.write_u64( static_cast<std::uint64_t>( e.maximum_output_difference_count ) );
		w.write_u64( static_cast<std::uint64_t>( e.produced_output_difference_count ) );
		w.write_i32( e.stack_step );
		for ( const auto& f : e.stack )
		{
			w.write_i32( f.basis_index );
			w.write_u32( f.current_difference );
			w.write_u8( f.state );
		}
	}

	static inline bool read_affine_enum( TwilightDream::auto_search_checkpoint::BinaryReader& r, AffineSubspaceEnumerator& e )
	{
		std::uint8_t init = 0;
		std::uint8_t stop = 0;
		if ( !r.read_u8( init ) ) return false;
		if ( !r.read_u8( stop ) ) return false;
		if ( !read_injection_transition( r, e.transition ) ) return false;
		std::uint64_t max_out = 0;
		std::uint64_t prod = 0;
		if ( !r.read_u64( max_out ) ) return false;
		if ( !r.read_u64( prod ) ) return false;
		e.maximum_output_difference_count = static_cast<std::size_t>( max_out );
		e.produced_output_difference_count = static_cast<std::size_t>( prod );
		if ( !r.read_i32( e.stack_step ) ) return false;
		for ( auto& f : e.stack )
		{
			if ( !r.read_i32( f.basis_index ) ) return false;
			if ( !r.read_u32( f.current_difference ) ) return false;
			if ( !r.read_u8( f.state ) ) return false;
		}
		e.initialized = ( init != 0u );
		e.stop_due_to_limits = ( stop != 0u );
		return true;
	}

	static inline void write_search_frame( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialSearchFrame& f )
	{
		w.write_u8( static_cast<std::uint8_t>( f.stage ) );
		w.write_u64( static_cast<std::uint64_t>( f.trail_size_at_entry ) );
		write_round_state( w, f.state );
		write_mod_add_enum( w, f.enum_first_add );
		write_subconst_enum( w, f.enum_first_const );
		write_affine_enum( w, f.enum_inj_b );
		write_mod_add_enum( w, f.enum_second_add );
		write_subconst_enum( w, f.enum_second_const );
		write_affine_enum( w, f.enum_inj_a );
	}

	static inline bool read_search_frame( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialSearchFrame& f )
	{
		std::uint8_t stage = 0;
		std::uint64_t trail_size = 0;
		if ( !r.read_u8( stage ) ) return false;
		if ( !r.read_u64( trail_size ) ) return false;
		f.stage = static_cast<DifferentialSearchStage>( stage );
		f.trail_size_at_entry = static_cast<std::size_t>( trail_size );
		if ( !read_round_state( r, f.state ) ) return false;
		if ( !read_mod_add_enum( r, f.enum_first_add ) ) return false;
		if ( !read_subconst_enum( r, f.enum_first_const ) ) return false;
		if ( !read_affine_enum( r, f.enum_inj_b ) ) return false;
		if ( !read_mod_add_enum( r, f.enum_second_add ) ) return false;
		if ( !read_subconst_enum( r, f.enum_second_const ) ) return false;
		if ( !read_affine_enum( r, f.enum_inj_a ) ) return false;
		return true;
	}

	static inline void write_cursor( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialSearchCursor& cursor )
	{
		w.write_u64( static_cast<std::uint64_t>( cursor.stack.size() ) );
		for ( const auto& f : cursor.stack )
			write_search_frame( w, f );
	}

	static inline bool read_cursor( TwilightDream::auto_search_checkpoint::BinaryReader& r, DifferentialSearchCursor& cursor )
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

	static inline void write_trail_vector( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const std::vector<DifferentialTrailStepRecord>& v )
	{
		w.write_u64( static_cast<std::uint64_t>( v.size() ) );
		for ( const auto& s : v )
			write_trail_step( w, s );
	}

	static inline bool read_trail_vector( TwilightDream::auto_search_checkpoint::BinaryReader& r, std::vector<DifferentialTrailStepRecord>& v )
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

	static inline void write_residual_frontier_entry(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const DifferentialResidualFrontierEntry& entry )
	{
		TwilightDream::residual_frontier_shared::write_residual_problem_record( w, entry.record );
		const bool has_snapshot = static_cast<bool>( entry.frame_snapshot );
		w.write_u8( has_snapshot ? 1u : 0u );
		if ( has_snapshot )
			write_search_frame( w, *entry.frame_snapshot );
		write_trail_vector( w, entry.current_trail_snapshot );
		w.write_u64( entry.prefix_weight_offset );
	}

	static inline bool read_residual_frontier_entry(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialResidualFrontierEntry& entry )
	{
		std::uint8_t has_snapshot = 0;
		if ( !TwilightDream::residual_frontier_shared::read_residual_problem_record( r, entry.record ) )
			return false;
		if ( !r.read_u8( has_snapshot ) )
			return false;
		if ( has_snapshot != 0 )
		{
			entry.frame_snapshot = std::make_shared<DifferentialSearchFrame>();
			if ( !read_search_frame( r, *entry.frame_snapshot ) )
				return false;
		}
		else
		{
			entry.frame_snapshot.reset();
		}
		return
			read_trail_vector( r, entry.current_trail_snapshot ) &&
			r.read_u64( entry.prefix_weight_offset );
	}

	struct DifferentialCheckpointLoadResult
	{
		DifferentialBestSearchConfiguration configuration {};
		std::uint32_t start_difference_a = 0;
		std::uint32_t start_difference_b = 0;
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
		SearchWeight best_total_weight = INFINITE_WEIGHT;
		std::vector<DifferentialTrailStepRecord> best_trail;
		std::vector<DifferentialTrailStepRecord> current_trail;
		DifferentialSearchCursor cursor;
		bool active_problem_valid = false;
		bool active_problem_is_root = false;
		TwilightDream::residual_frontier_shared::ResidualProblemRecord active_problem_record{};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> pending_frontier {};
		std::vector<DifferentialResidualFrontierEntry> pending_frontier_entries {};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> completed_source_input_pairs {};
		std::vector<TwilightDream::residual_frontier_shared::ResidualProblemRecord> completed_output_as_next_input_pairs {};
		std::unordered_set<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> completed_residual_set {};
		std::unordered_map<
			TwilightDream::residual_frontier_shared::ResidualProblemKey,
			SearchWeight,
			TwilightDream::residual_frontier_shared::ResidualProblemKeyHash> best_prefix_by_residual_key {};
		std::vector<TwilightDream::residual_frontier_shared::ResidualResultRecord> global_residual_result_table {};
		TwilightDream::residual_frontier_shared::ResidualCounters residual_counters {};
	};

	struct DifferentialResumeFingerprint
	{
		std::uint64_t hash = 0;
		std::uint64_t cursor_stack_depth = 0;
		std::uint64_t current_trail_size = 0;
		std::uint64_t modular_add_frame_count = 0;
		std::uint64_t modular_add_shell_index_total = 0;
		std::uint64_t modular_add_shell_cache_entries = 0;
		std::uint64_t modular_add_shell_cache_hash = 0;
	};

	DifferentialResumeFingerprint compute_differential_resume_fingerprint( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor ) noexcept;

	DifferentialResumeFingerprint compute_differential_resume_fingerprint( const DifferentialCheckpointLoadResult& load ) noexcept;

	void write_differential_resume_fingerprint_fields(
		std::ostream& out,
		const DifferentialResumeFingerprint& fingerprint,
		const char* prefix = "resume_fingerprint_" );

	void differential_runtime_log_resume_event(
		const DifferentialBestSearchContext& context,
		const DifferentialSearchCursor& cursor,
		const char* event_name,
		const char* reason = "running" );

	bool write_differential_checkpoint_payload( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, std::uint64_t elapsed_usec );

	bool read_differential_checkpoint_payload(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialCheckpointLoadResult& out,
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, SearchWeight>& memo );

	bool write_differential_checkpoint( const std::string& path, const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, std::uint64_t elapsed_usec );

	bool read_differential_checkpoint( const std::string& path, DifferentialCheckpointLoadResult& out, TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, SearchWeight>& memo );

	bool materialize_differential_resume_rebuildable_state(
		DifferentialBestSearchContext& context,
		DifferentialSearchCursor& cursor );

	void write_differential_resume_snapshot(
		BestWeightHistory& history,
		const DifferentialBestSearchContext& context,
		const DifferentialSearchCursor& cursor,
		const char* reason );

}  // namespace TwilightDream::auto_search_differential
