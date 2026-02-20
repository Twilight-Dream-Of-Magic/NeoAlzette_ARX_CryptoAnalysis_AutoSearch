#include "auto_search_frame/detail/linear_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

namespace TwilightDream::auto_search_linear
{
	std::string default_binary_checkpoint_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_MaskA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_a << "_MaskB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_b << std::dec;
		return make_unique_timestamped_artifact_path( oss.str(), ".ckpt" );
	}

	std::string default_runtime_log_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_MaskA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_a << "_MaskB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_b << std::dec;
		return RuntimeEventLog::default_path( oss.str() );
	}

	std::string BestWeightHistory::default_path( int round_count, std::uint32_t mask_a, std::uint32_t mask_b )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_MaskA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_a << "_MaskB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << mask_b << std::dec << ".log";
		return oss.str();
	}

	bool BestWeightHistory::open_append( const std::string& path )
	{
		out.open( path, std::ios::out | std::ios::app );
		if ( out )
			this->path = path;
		return bool( out );
	}

	namespace
	{
		const char* linear_stage_to_string( LinearSearchStage stage ) noexcept
		{
			switch ( stage )
			{
			case LinearSearchStage::Enter:
				return "Enter";
			case LinearSearchStage::Enumerate:
				return "Enumerate";
			case LinearSearchStage::InjA:
				return "InjA";
			case LinearSearchStage::SecondAdd:
				return "SecondAdd";
			case LinearSearchStage::InjB:
				return "InjB";
			case LinearSearchStage::SecondConst:
				return "SecondConst";
			case LinearSearchStage::FirstSubconst:
				return "FirstSubconst";
			case LinearSearchStage::FirstAdd:
				return "FirstAdd";
			case LinearSearchStage::Recurse:
				return "Recurse";
			default:
				return "Unknown";
			}
		}

		// Text history log: one block per trail. (Former "detailed" lines duplicated fields that lead each "full" line.)

		void write_linear_trail_full_block(
			std::ostream& out,
			const std::vector<LinearTrailStepRecord>& trail,
			const char* begin_label,
			const char* end_label )
		{
			out << begin_label << "\n";
			for ( const auto& s : trail )
			{
				out << "round_index=" << s.round_index
					<< " round_weight=" << s.round_weight
					<< " output_mask_branch_a=" << hex8( s.output_branch_a_mask )
					<< " output_mask_branch_b=" << hex8( s.output_branch_b_mask )
					<< " input_mask_branch_a=" << hex8( s.input_branch_a_mask )
					<< " input_mask_branch_b=" << hex8( s.input_branch_b_mask )
					<< " output_branch_b_mask_after_second_constant_subtraction=" << hex8( s.output_branch_b_mask_after_second_constant_subtraction )
					<< " input_branch_b_mask_before_second_constant_subtraction=" << hex8( s.input_branch_b_mask_before_second_constant_subtraction )
					<< " weight_second_constant_subtraction=" << s.weight_second_constant_subtraction
					<< " output_branch_a_mask_after_second_addition=" << hex8( s.output_branch_a_mask_after_second_addition )
					<< " input_branch_a_mask_before_second_addition=" << hex8( s.input_branch_a_mask_before_second_addition )
					<< " second_addition_term_mask_from_branch_b=" << hex8( s.second_addition_term_mask_from_branch_b )
					<< " weight_second_addition=" << s.weight_second_addition
					<< " weight_injection_from_branch_a=" << s.weight_injection_from_branch_a
					<< " weight_injection_from_branch_b=" << s.weight_injection_from_branch_b
					<< " chosen_correlated_input_mask_for_injection_from_branch_a=" << hex8( s.chosen_correlated_input_mask_for_injection_from_branch_a )
					<< " chosen_correlated_input_mask_for_injection_from_branch_b=" << hex8( s.chosen_correlated_input_mask_for_injection_from_branch_b )
					<< " output_branch_a_mask_after_first_constant_subtraction=" << hex8( s.output_branch_a_mask_after_first_constant_subtraction )
					<< " input_branch_a_mask_before_first_constant_subtraction=" << hex8( s.input_branch_a_mask_before_first_constant_subtraction )
					<< " weight_first_constant_subtraction=" << s.weight_first_constant_subtraction
					<< " output_branch_b_mask_after_first_addition=" << hex8( s.output_branch_b_mask_after_first_addition )
					<< " input_branch_b_mask_before_first_addition=" << hex8( s.input_branch_b_mask_before_first_addition )
					<< " first_addition_term_mask_from_branch_a=" << hex8( s.first_addition_term_mask_from_branch_a )
					<< " weight_first_addition=" << s.weight_first_addition << "\n";
			}
			out << end_label << "\n";
		}

		void write_linear_cursor_snapshot( std::ostream& out, const LinearSearchCursor& cursor )
		{
			out << "cursor_stack_begin\n";
			for ( std::size_t i = 0; i < cursor.stack.size(); ++i )
			{
				const auto& frame = cursor.stack[ i ];
				const auto& state = frame.state;
				out << "frame_index=" << i
					<< " stage=" << linear_stage_to_string( frame.stage )
					<< " trail_size_at_entry=" << frame.trail_size_at_entry
					<< " predecessor_index=" << frame.predecessor_index << "\n";
				out << "state.round_boundary_depth=" << state.round_boundary_depth
					<< " accumulated_weight_so_far=" << state.accumulated_weight_so_far
					<< " round_index=" << state.round_index
					<< " round_weight_cap=" << state.round_weight_cap
					<< " remaining_round_weight_lower_bound_after_this_round=" << state.remaining_round_weight_lower_bound_after_this_round
					<< " round_output_branch_a_mask=" << hex8( state.round_output_branch_a_mask )
					<< " round_output_branch_b_mask=" << hex8( state.round_output_branch_b_mask )
					<< " output_branch_a_mask_after_second_addition=" << hex8( state.output_branch_a_mask_after_second_addition )
					<< " output_branch_b_mask_after_second_constant_subtraction=" << hex8( state.output_branch_b_mask_after_second_constant_subtraction )
					<< " input_branch_a_mask_before_second_addition=" << hex8( state.input_branch_a_mask_before_second_addition )
					<< " input_branch_b_mask_before_second_constant_subtraction=" << hex8( state.input_branch_b_mask_before_second_constant_subtraction )
					<< " output_branch_a_mask_after_first_constant_subtraction=" << hex8( state.output_branch_a_mask_after_first_constant_subtraction )
					<< " output_branch_b_mask_after_first_addition=" << hex8( state.output_branch_b_mask_after_first_addition )
					<< " round_predecessor_count=" << state.round_predecessors.size()
					<< " second_addition_candidate_index=" << state.second_addition_candidate_index
					<< " second_constant_subtraction_candidate_index=" << state.second_constant_subtraction_candidate_index
					<< " first_constant_subtraction_candidate_index=" << state.first_constant_subtraction_candidate_index
					<< " first_addition_candidate_index=" << state.first_addition_candidate_index << "\n";
				out << "state.second_addition_candidates_storage_size=" << state.second_addition_candidates_storage.size()
					<< " second_constant_subtraction_candidates_for_branch_b_size=" << state.second_constant_subtraction_candidates_for_branch_b.size()
					<< " first_constant_subtraction_candidates_for_branch_a_size=" << state.first_constant_subtraction_candidates_for_branch_a.size()
					<< " first_addition_candidates_for_branch_b_size=" << state.first_addition_candidates_for_branch_b.size()
					<< " inj_a_enumerator_stack_step=" << state.injection_from_branch_a_enumerator.stack_step
					<< " inj_a_enumerator_produced_count=" << state.injection_from_branch_a_enumerator.produced_count
					<< " inj_b_enumerator_stack_step=" << state.injection_from_branch_b_enumerator.stack_step
					<< " inj_b_enumerator_produced_count=" << state.injection_from_branch_b_enumerator.produced_count
					<< " second_add_stream_stack_size=" << state.second_addition_stream_cursor.stack_size
					<< " second_add_weight_sliced_stack_size=" << state.second_addition_weight_sliced_clat_stream_cursor.stack_size
					<< " second_subconst_stack_step=" << state.second_constant_subtraction_stream_cursor.stack_step
					<< " first_add_stream_stack_size=" << state.first_addition_stream_cursor.stack_size
					<< " first_add_weight_sliced_stack_size=" << state.first_addition_weight_sliced_clat_stream_cursor.stack_size
					<< " first_subconst_stack_step=" << state.first_constant_subtraction_stream_cursor.stack_step << "\n";
			}
			out << "cursor_stack_end\n";
		}

		void mix_linear_trail_step( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const LinearTrailStepRecord& step ) noexcept
		{
			fp.mix_i32( step.round_index );
			fp.mix_u32( step.output_branch_a_mask );
			fp.mix_u32( step.output_branch_b_mask );
			fp.mix_u32( step.input_branch_a_mask );
			fp.mix_u32( step.input_branch_b_mask );
			fp.mix_u32( step.output_branch_b_mask_after_second_constant_subtraction );
			fp.mix_u32( step.input_branch_b_mask_before_second_constant_subtraction );
			fp.mix_i32( step.weight_second_constant_subtraction );
			fp.mix_u32( step.output_branch_a_mask_after_second_addition );
			fp.mix_u32( step.input_branch_a_mask_before_second_addition );
			fp.mix_u32( step.second_addition_term_mask_from_branch_b );
			fp.mix_i32( step.weight_second_addition );
			fp.mix_i32( step.weight_injection_from_branch_a );
			fp.mix_i32( step.weight_injection_from_branch_b );
			fp.mix_u32( step.chosen_correlated_input_mask_for_injection_from_branch_a );
			fp.mix_u32( step.chosen_correlated_input_mask_for_injection_from_branch_b );
			fp.mix_u32( step.output_branch_a_mask_after_first_constant_subtraction );
			fp.mix_u32( step.input_branch_a_mask_before_first_constant_subtraction );
			fp.mix_i32( step.weight_first_constant_subtraction );
			fp.mix_u32( step.output_branch_b_mask_after_first_addition );
			fp.mix_u32( step.input_branch_b_mask_before_first_addition );
			fp.mix_u32( step.first_addition_term_mask_from_branch_a );
			fp.mix_i32( step.weight_first_addition );
			fp.mix_i32( step.round_weight );
		}

		void mix_subconst_candidate( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const SubConstCandidate& candidate ) noexcept
		{
			fp.mix_u32( candidate.input_mask_on_x );
			fp.mix_i32( candidate.linear_weight );
		}

		void mix_add_candidate( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const AddCandidate& candidate ) noexcept
		{
			fp.mix_u32( candidate.input_mask_x );
			fp.mix_u32( candidate.input_mask_y );
			fp.mix_i32( candidate.linear_weight );
		}

		void mix_subconst_streaming_cursor( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const SubConstStreamingCursor& cursor ) noexcept
		{
			fp.mix_bool( cursor.initialized );
			fp.mix_bool( cursor.stop_due_to_limits );
			fp.mix_u32( cursor.output_mask_beta );
			fp.mix_u32( cursor.constant );
			fp.mix_i32( cursor.weight_cap );
			fp.mix_i32( cursor.nbits );
			fp.mix_u64( cursor.min_abs );
			for ( const auto& matrix : cursor.mats_alpha0 )
			{
				fp.mix_i32( matrix.m00 );
				fp.mix_i32( matrix.m01 );
				fp.mix_i32( matrix.m10 );
				fp.mix_i32( matrix.m11 );
				fp.mix_u8( matrix.max_row_sum );
			}
			for ( const auto& matrix : cursor.mats_alpha1 )
			{
				fp.mix_i32( matrix.m00 );
				fp.mix_i32( matrix.m01 );
				fp.mix_i32( matrix.m10 );
				fp.mix_i32( matrix.m11 );
				fp.mix_u8( matrix.max_row_sum );
			}
			for ( const std::uint64_t gain : cursor.max_gain_suffix )
				fp.mix_u64( gain );
			fp.mix_i32( cursor.stack_step );
			for ( const auto& frame : cursor.stack )
			{
				fp.mix_i32( frame.bit_index );
				fp.mix_u32( frame.prefix );
				fp.mix_i64( frame.v0 );
				fp.mix_i64( frame.v1 );
				fp.mix_u8( frame.state );
			}
		}

		void mix_add_streaming_cursor( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const AddVarVarSplit8Enumerator32::StreamingCursor& cursor ) noexcept
		{
			fp.mix_bool( cursor.initialized );
			fp.mix_bool( cursor.stop_due_to_limits );
			fp.mix_u32( cursor.output_mask_u );
			fp.mix_i32( cursor.weight_cap );
			fp.mix_i32( cursor.next_target_weight );
			for ( const std::uint8_t byte : cursor.output_mask_bytes )
				fp.mix_u8( byte );
			for ( const auto& row : cursor.min_remaining_weight )
			{
				fp.mix_i32( row[ 0 ] );
				fp.mix_i32( row[ 1 ] );
			}
			fp.mix_i32( cursor.stack_size );
			for ( const auto& frame : cursor.stack )
			{
				fp.mix_i32( frame.block_index );
				fp.mix_i32( frame.connection_in );
				fp.mix_u32( frame.input_mask_x_acc );
				fp.mix_u32( frame.input_mask_y_acc );
				fp.mix_i32( frame.remaining_weight );
				fp.mix_u64( static_cast<std::uint64_t>( frame.option_index ) );
				fp.mix_i32( frame.target_weight );
			}
		}

		void mix_weight_sliced_cursor( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const WeightSlicedClatStreamingCursor& cursor ) noexcept
		{
			fp.mix_bool( cursor.initialized );
			fp.mix_bool( cursor.stop_due_to_limits );
			fp.mix_u32( cursor.output_mask_u );
			fp.mix_i32( cursor.weight_cap );
			fp.mix_i32( cursor.next_target_weight );
			fp.mix_i32( cursor.current_target_weight );
			fp.mix_u32( cursor.input_mask_x_prefix );
			fp.mix_u32( cursor.input_mask_y_prefix );
			fp.mix_i32( cursor.z30 );
			fp.mix_i32( cursor.stack_size );
			for ( const auto& frame : cursor.stack )
			{
				fp.mix_i32( frame.bit_index );
				fp.mix_u32( frame.input_mask_x );
				fp.mix_u32( frame.input_mask_y );
				fp.mix_i32( frame.z_bit );
				fp.mix_i32( frame.z_weight_so_far );
				fp.mix_u8( frame.branch_state );
			}
		}

		void mix_injection_transition( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const InjectionCorrelationTransition& transition ) noexcept
		{
			fp.mix_u32( transition.offset_mask );
			for ( const std::uint32_t basis : transition.basis_vectors )
				fp.mix_u32( basis );
			fp.mix_i32( transition.rank );
			fp.mix_i32( transition.weight );
		}

		void mix_affine_enumerator( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const LinearAffineMaskEnumerator& enumerator ) noexcept
		{
			fp.mix_bool( enumerator.initialized );
			fp.mix_bool( enumerator.stop_due_to_limits );
			fp.mix_u64( static_cast<std::uint64_t>( enumerator.maximum_input_mask_count ) );
			fp.mix_u64( static_cast<std::uint64_t>( enumerator.produced_count ) );
			fp.mix_i32( enumerator.rank );
			fp.mix_i32( enumerator.stack_step );
			for ( const auto& frame : enumerator.stack )
			{
				fp.mix_i32( frame.basis_index );
				fp.mix_u32( frame.current_mask );
				fp.mix_u8( frame.branch_state );
			}
		}

		void mix_linear_round_state(
			TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
			LinearResumeFingerprint& summary,
			const LinearRoundSearchState& state ) noexcept
		{
			summary.candidate_vector_size_total += static_cast<std::uint64_t>( state.second_constant_subtraction_candidates_for_branch_b.size() );
			summary.candidate_vector_size_total += static_cast<std::uint64_t>( state.second_addition_candidates_storage.size() );
			summary.candidate_vector_size_total += static_cast<std::uint64_t>( state.first_constant_subtraction_candidates_for_branch_a.size() );
			summary.candidate_vector_size_total += static_cast<std::uint64_t>( state.first_addition_candidates_for_branch_b.size() );
			summary.candidate_index_total += static_cast<std::uint64_t>( state.second_addition_candidate_index );
			summary.candidate_index_total += static_cast<std::uint64_t>( state.second_constant_subtraction_candidate_index );
			summary.candidate_index_total += static_cast<std::uint64_t>( state.first_constant_subtraction_candidate_index );
			summary.candidate_index_total += static_cast<std::uint64_t>( state.first_addition_candidate_index );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.second_constant_subtraction_stream_cursor.stack_step );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.second_addition_weight_sliced_clat_stream_cursor.stack_size );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.second_addition_stream_cursor.stack_size );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.first_constant_subtraction_stream_cursor.stack_step );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.first_addition_weight_sliced_clat_stream_cursor.stack_size );
			summary.streaming_cursor_stack_size_total += static_cast<std::uint64_t>( state.first_addition_stream_cursor.stack_size );
			summary.affine_outputs_produced_total += static_cast<std::uint64_t>( state.injection_from_branch_a_enumerator.produced_count );
			summary.affine_outputs_produced_total += static_cast<std::uint64_t>( state.injection_from_branch_b_enumerator.produced_count );

			fp.mix_i32( state.round_boundary_depth );
			fp.mix_i32( state.accumulated_weight_so_far );
			fp.mix_i32( state.round_index );
			fp.mix_i32( state.round_weight_cap );
			fp.mix_i32( state.remaining_round_weight_lower_bound_after_this_round );
			fp.mix_u32( state.round_output_branch_a_mask );
			fp.mix_u32( state.round_output_branch_b_mask );
			fp.mix_u32( state.branch_a_round_output_mask_before_inj_from_a );
			fp.mix_u32( state.branch_b_mask_before_injection_from_branch_a );
			mix_injection_transition( fp, state.injection_from_branch_a_transition );
			mix_affine_enumerator( fp, state.injection_from_branch_a_enumerator );
			fp.mix_i32( state.weight_injection_from_branch_a );
			fp.mix_i32( state.remaining_after_inj_a );
			fp.mix_i32( state.second_subconst_weight_cap );
			fp.mix_i32( state.second_add_weight_cap );
			fp.mix_u32( state.chosen_correlated_input_mask_for_injection_from_branch_a );
			fp.mix_u32( state.output_branch_a_mask_after_second_addition );
			fp.mix_u32( state.output_branch_b_mask_after_second_constant_subtraction );
			mix_subconst_streaming_cursor( fp, state.second_constant_subtraction_stream_cursor );
			mix_weight_sliced_cursor( fp, state.second_addition_weight_sliced_clat_stream_cursor );
			fp.mix_u64( static_cast<std::uint64_t>( state.second_constant_subtraction_candidates_for_branch_b.size() ) );
			for ( const auto& candidate : state.second_constant_subtraction_candidates_for_branch_b )
				mix_subconst_candidate( fp, candidate );
			mix_add_streaming_cursor( fp, state.second_addition_stream_cursor );
			fp.mix_u64( static_cast<std::uint64_t>( state.second_addition_candidates_storage.size() ) );
			for ( const auto& candidate : state.second_addition_candidates_storage )
				mix_add_candidate( fp, candidate );
			fp.mix_u64( static_cast<std::uint64_t>( state.second_addition_candidate_index ) );
			fp.mix_u32( state.input_branch_a_mask_before_second_addition );
			fp.mix_u32( state.second_addition_term_mask_from_branch_b );
			fp.mix_i32( state.weight_second_addition );
			fp.mix_u32( state.branch_b_mask_contribution_from_second_addition_term );
			mix_injection_transition( fp, state.injection_from_branch_b_transition );
			mix_affine_enumerator( fp, state.injection_from_branch_b_enumerator );
			fp.mix_i32( state.weight_injection_from_branch_b );
			fp.mix_i32( state.base_weight_after_inj_b );
			fp.mix_u32( state.chosen_correlated_input_mask_for_injection_from_branch_b );
			fp.mix_u32( state.input_branch_b_mask_before_second_constant_subtraction );
			fp.mix_i32( state.weight_second_constant_subtraction );
			fp.mix_u32( state.branch_b_mask_after_second_add_term_removed );
			fp.mix_u32( state.branch_b_mask_after_first_xor_with_rotated_branch_a_base );
			fp.mix_u64( static_cast<std::uint64_t>( state.second_constant_subtraction_candidate_index ) );
			fp.mix_i32( state.base_weight_after_second_subconst );
			fp.mix_i32( state.first_subconst_weight_cap );
			fp.mix_i32( state.first_add_weight_cap );
			fp.mix_u32( state.output_branch_a_mask_after_first_constant_subtraction );
			fp.mix_u32( state.output_branch_b_mask_after_first_addition );
			mix_subconst_streaming_cursor( fp, state.first_constant_subtraction_stream_cursor );
			mix_weight_sliced_cursor( fp, state.first_addition_weight_sliced_clat_stream_cursor );
			fp.mix_u64( static_cast<std::uint64_t>( state.first_constant_subtraction_candidates_for_branch_a.size() ) );
			for ( const auto& candidate : state.first_constant_subtraction_candidates_for_branch_a )
				mix_subconst_candidate( fp, candidate );
			mix_add_streaming_cursor( fp, state.first_addition_stream_cursor );
			fp.mix_u64( static_cast<std::uint64_t>( state.first_addition_candidates_for_branch_b.size() ) );
			for ( const auto& candidate : state.first_addition_candidates_for_branch_b )
				mix_add_candidate( fp, candidate );
			fp.mix_u64( static_cast<std::uint64_t>( state.first_constant_subtraction_candidate_index ) );
			fp.mix_u64( static_cast<std::uint64_t>( state.first_addition_candidate_index ) );
			fp.mix_u32( state.input_branch_a_mask_before_first_constant_subtraction_current );
			fp.mix_i32( state.weight_first_constant_subtraction_current );
			fp.mix_u64( static_cast<std::uint64_t>( state.round_predecessors.size() ) );
			for ( const auto& predecessor : state.round_predecessors )
				mix_linear_trail_step( fp, predecessor );
		}

		LinearResumeFingerprint compute_linear_resume_fingerprint_impl(
			const std::vector<LinearTrailStepRecord>& current_trail,
			const LinearSearchCursor& cursor ) noexcept
		{
			LinearResumeFingerprint fingerprint {};
			TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
			fingerprint.cursor_stack_depth = static_cast<std::uint64_t>( cursor.stack.size() );
			fingerprint.current_trail_size = static_cast<std::uint64_t>( current_trail.size() );
			fp.mix_u64( fingerprint.current_trail_size );
			for ( const auto& step : current_trail )
				mix_linear_trail_step( fp, step );
			fp.mix_u64( fingerprint.cursor_stack_depth );
			for ( const auto& frame : cursor.stack )
			{
				fp.mix_enum( frame.stage );
				fp.mix_u64( static_cast<std::uint64_t>( frame.trail_size_at_entry ) );
				fp.mix_u64( static_cast<std::uint64_t>( frame.predecessor_index ) );
				mix_linear_round_state( fp, fingerprint, frame.state );
			}
			fingerprint.hash = fp.finish();
			return fingerprint;
		}

		void write_linear_runtime_event_common_fields( std::ostream& out, const LinearBestSearchContext& context, const char* reason )
		{
			out << "round_count=" << context.configuration.round_count << "\n";
			out << "start_output_mask_branch_a=" << hex8( context.start_output_branch_a_mask ) << "\n";
			out << "start_output_mask_branch_b=" << hex8( context.start_output_branch_b_mask ) << "\n";
			out << "best_weight=" << ( ( context.best_weight >= INFINITE_WEIGHT ) ? -1 : context.best_weight ) << "\n";
			out << "run_nodes_visited=" << context.run_visited_node_count << "\n";
			out << "total_nodes_visited=" << context.visited_node_count << "\n";
			out << "elapsed_seconds=" << std::fixed << std::setprecision( 3 ) << TwilightDream::best_search_shared_core::accumulated_elapsed_seconds( context ) << "\n";
			out << "runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "\n";
			out << "runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "\n";
			out << "runtime_progress_every_seconds=" << context.runtime_controls.progress_every_seconds << "\n";
			out << "runtime_checkpoint_every_seconds=" << context.runtime_controls.checkpoint_every_seconds << "\n";
			out << "runtime_progress_node_mask=" << context.runtime_state.progress_node_mask << "\n";
			out << "runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() ) << "\n";
			out << "runtime_budget_mode=" << runtime_budget_mode_name( context.runtime_controls ) << "\n";
			out << "maxnodes_ignored_due_to_time_limit=" << ( runtime_nodes_ignored_due_to_time_limit( context.runtime_controls ) ? 1 : 0 ) << "\n";
			out << "stop_reason=" << ( reason ? reason : "running" ) << "\n";
			out << "physical_available_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.physical_available_bytes ) << "\n";
			out << "estimated_must_live_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.estimated_must_live_bytes ) << "\n";
			out << "estimated_optional_rebuildable_gib=" << std::fixed << std::setprecision( 3 ) << bytes_to_gibibytes( context.invocation_metadata.estimated_optional_rebuildable_bytes ) << "\n";
			out << "memory_gate=" << memory_gate_status_name( context.invocation_metadata.memory_gate_status ) << "\n";
			out << "startup_memory_gate_policy=" << ( context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
			out << "reason=" << ( reason ? reason : "running" ) << "\n";
		}
	}

	void BestWeightHistory::maybe_write( const LinearBestSearchContext& context, const char* reason )
	{
		if ( !out )
			return;
		if ( context.best_weight >= INFINITE_WEIGHT )
			return;
		if ( last_written_weight < INFINITE_WEIGHT && context.best_weight == last_written_weight )
			return;

		const double elapsed_seconds = TwilightDream::best_search_shared_core::accumulated_elapsed_seconds( context );
		const auto&	 config = context.configuration;

		out << "=== checkpoint ===\n";
		out << "timestamp_local=" << format_local_time_now() << "\n";
		out << "checkpoint_reason=" << ( reason ? reason : "best_weight_changed" ) << "\n";
		out << "round_count=" << config.round_count << "\n";
		out << "search_mode=" << search_mode_to_string( config.search_mode ) << "\n";
		out << "start_output_mask_branch_a=" << hex8( context.start_output_branch_a_mask ) << "\n";
		out << "start_output_mask_branch_b=" << hex8( context.start_output_branch_b_mask ) << "\n";
		out << "best_weight=" << context.best_weight << "\n";
		out << "run_nodes_visited=" << context.run_visited_node_count << "\n";
		out << "visited_node_count=" << context.visited_node_count << "\n";
		out << "elapsed_seconds=" << std::fixed << std::setprecision( 3 ) << elapsed_seconds << "\n";
		out << "runtime_maximum_search_nodes=" << context.runtime_controls.maximum_search_nodes << "\n";
		out << "runtime_maximum_search_seconds=" << context.runtime_controls.maximum_search_seconds << "\n";
		out << "runtime_time_limit_scope=" << TwilightDream::runtime_component::runtime_time_limit_scope_name( TwilightDream::runtime_component::runtime_time_limit_scope() ) << "\n";
		out << "startup_memory_gate_policy=" << ( context.invocation_metadata.startup_memory_gate_advisory_only ? "advisory_only" : "enforce_reject" ) << "\n";
		out << "runtime_progress_every_seconds=" << context.runtime_controls.progress_every_seconds << "\n";
		out << "runtime_checkpoint_every_seconds=" << context.runtime_controls.checkpoint_every_seconds << "\n";
		out << "runtime_progress_node_mask=" << context.runtime_state.progress_node_mask << "\n";
		out << "target_best_weight=" << config.target_best_weight << "\n";
		out << "addition_weight_cap=" << config.addition_weight_cap << "\n";
		out << "constant_subtraction_weight_cap=" << config.constant_subtraction_weight_cap << "\n";
		out << "enable_weight_sliced_clat=" << ( config.enable_weight_sliced_clat ? 1 : 0 ) << "\n";
		out << "weight_sliced_clat_max_candidates=" << config.weight_sliced_clat_max_candidates << "\n";
		out << "maximum_injection_input_masks=" << config.maximum_injection_input_masks << "\n";
		out << "maximum_round_predecessors=" << config.maximum_round_predecessors << "\n";
		out << "enable_state_memoization=" << ( config.enable_state_memoization ? 1 : 0 ) << "\n";
		out << "enable_remaining_round_lower_bound=" << ( config.enable_remaining_round_lower_bound ? 1 : 0 ) << "\n";
		out << "remaining_round_min_weight_count=" << config.remaining_round_min_weight.size() << "\n";
		if ( !config.remaining_round_min_weight.empty() )
		{
			out << "remaining_round_min_weight_values=";
			for ( std::size_t i = 0; i < config.remaining_round_min_weight.size(); ++i )
			{
				if ( i != 0 )
					out << ",";
				out << config.remaining_round_min_weight[ i ];
			}
			out << "\n";
		}
		out << "auto_generate_remaining_round_lower_bound=" << ( config.auto_generate_remaining_round_lower_bound ? 1 : 0 ) << "\n";
		out << "remaining_round_lower_bound_generation_nodes=" << config.remaining_round_lower_bound_generation_nodes << "\n";
		out << "remaining_round_lower_bound_generation_seconds=" << config.remaining_round_lower_bound_generation_seconds << "\n";
		out << "strict_remaining_round_lower_bound=" << ( config.strict_remaining_round_lower_bound ? 1 : 0 ) << "\n";
		out << "enable_verbose_output=" << ( config.enable_verbose_output ? 1 : 0 ) << "\n";
		out << "best_input_mask_branch_a=" << hex8( context.best_input_branch_a_mask ) << "\n";
		out << "best_input_mask_branch_b=" << hex8( context.best_input_branch_b_mask ) << "\n";
		out << "trail_step_count=" << context.best_linear_trail.size() << "\n";
		write_linear_trail_full_block(
			out,
			context.best_linear_trail,
			"best_trail_full_begin",
			"best_trail_full_end" );
		out << "\n";
		out.flush();
		last_written_weight = context.best_weight;
	}

	// On-disk field order is frozen with auto_search_checkpoint::kVersion. Some trailing runtime fields
	// (run_visited_node_count snapshot, progress_node_mask, last-run limit flags) are not consumed when
	// merging CLI/runtime overrides on single-run resume â€” see stored_runtime_metadata_for_resume_control_merge.
	bool write_linear_checkpoint_payload( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, std::uint64_t elapsed_usec )
	{
		w.write_u64( elapsed_usec );
		write_config( w, context.configuration );
		w.write_u32( context.start_output_branch_a_mask );
		w.write_u32( context.start_output_branch_b_mask );
		w.write_string( context.history_log_output_path );
		w.write_string( context.runtime_log_output_path );
		w.write_u64( context.visited_node_count );
		w.write_u64( context.run_visited_node_count );
		w.write_i32( context.best_weight );
		w.write_u32( context.best_input_branch_a_mask );
		w.write_u32( context.best_input_branch_b_mask );
		write_trail_vector( w, context.best_linear_trail );
		write_trail_vector( w, context.current_linear_trail );
		write_cursor( w, cursor );
		w.write_u64( context.runtime_controls.maximum_search_nodes );
		w.write_u64( context.runtime_controls.maximum_search_seconds );
		w.write_u64( context.runtime_controls.progress_every_seconds );
		w.write_u64( context.runtime_controls.checkpoint_every_seconds );
		w.write_u64( context.runtime_state.progress_node_mask );
		w.write_u8( linear_runtime_node_limit_hit( context ) ? 1u : 0u );
		w.write_u8( runtime_time_limit_hit( context.runtime_controls, context.runtime_state ) ? 1u : 0u );
		context.memoization.serialize( w );
		return w.ok();
	}

	bool read_linear_checkpoint_payload(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		LinearCheckpointLoadResult& out,
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int>& memo )
	{
		if ( !r.read_u64( out.accumulated_elapsed_usec ) )
			return false;
		if ( !read_config( r, out.configuration ) )
			return false;
		if ( !r.read_u32( out.start_mask_a ) )
			return false;
		if ( !r.read_u32( out.start_mask_b ) )
			return false;
		if ( !r.read_string( out.history_log_path ) )
			return false;
		if ( !r.read_string( out.runtime_log_path ) )
			return false;
		if ( !r.read_u64( out.total_nodes_visited ) )
			return false;
		if ( !r.read_u64( out.run_nodes_visited ) )
			return false;
		if ( !r.read_i32( out.best_weight ) )
			return false;
		if ( !r.read_u32( out.best_input_mask_a ) )
			return false;
		if ( !r.read_u32( out.best_input_mask_b ) )
			return false;
		if ( !read_trail_vector( r, out.best_trail ) )
			return false;
		if ( !read_trail_vector( r, out.current_trail ) )
			return false;
		if ( !read_cursor( r, out.cursor ) )
			return false;
		if ( !r.read_u64( out.runtime_maximum_search_nodes ) )
			return false;
		if ( !r.read_u64( out.runtime_maximum_search_seconds ) )
			return false;
		if ( !r.read_u64( out.runtime_progress_every_seconds ) )
			return false;
		if ( !r.read_u64( out.runtime_checkpoint_every_seconds ) )
			return false;
		if ( !r.read_u64( out.runtime_progress_node_mask ) )
			return false;
		std::uint8_t last_run_hit_node_limit = 0;
		std::uint8_t last_run_hit_time_limit = 0;
		if ( !r.read_u8( last_run_hit_node_limit ) )
			return false;
		if ( !r.read_u8( last_run_hit_time_limit ) )
			return false;
		out.last_run_hit_node_limit = ( last_run_hit_node_limit != 0 );
		out.last_run_hit_time_limit = ( last_run_hit_time_limit != 0 );
		if ( !memo.deserialize( r ) )
			return false;
		return true;
	}

	bool write_linear_checkpoint( const std::string& path, const LinearBestSearchContext& context, const LinearSearchCursor& cursor, std::uint64_t elapsed_usec )
	{
		return TwilightDream::auto_search_checkpoint::write_atomic( path, [ & ]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
			if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::LinearBest ) )
				return false;
			return write_linear_checkpoint_payload( w, context, cursor, elapsed_usec );
		} );
	}

	bool read_linear_checkpoint( const std::string& path, LinearCheckpointLoadResult& out, TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, int>& memo )
	{
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::LinearBest )
			return false;
		return read_linear_checkpoint_payload( r, out, memo );
	}

	LinearResumeFingerprint compute_linear_resume_fingerprint( const LinearBestSearchContext& context, const LinearSearchCursor& cursor ) noexcept
	{
		return compute_linear_resume_fingerprint_impl( context.current_linear_trail, cursor );
	}

	LinearResumeFingerprint compute_linear_resume_fingerprint( const LinearCheckpointLoadResult& load ) noexcept
	{
		return compute_linear_resume_fingerprint_impl( load.current_trail, load.cursor );
	}

	void write_linear_resume_fingerprint_fields(
		std::ostream& out,
		const LinearResumeFingerprint& fingerprint,
		const char* prefix )
	{
		const char* key_prefix = ( prefix != nullptr ) ? prefix : "resume_fingerprint_";
		out << key_prefix << "hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";
		out << key_prefix << "cursor_stack_depth=" << fingerprint.cursor_stack_depth << "\n";
		out << key_prefix << "current_trail_size=" << fingerprint.current_trail_size << "\n";
		out << key_prefix << "candidate_vector_size_total=" << fingerprint.candidate_vector_size_total << "\n";
		out << key_prefix << "candidate_index_total=" << fingerprint.candidate_index_total << "\n";
		out << key_prefix << "streaming_cursor_stack_size_total=" << fingerprint.streaming_cursor_stack_size_total << "\n";
		out << key_prefix << "affine_outputs_produced_total=" << fingerprint.affine_outputs_produced_total << "\n";
	}

	void linear_runtime_log_resume_event(
		const LinearBestSearchContext& context,
		const LinearSearchCursor& cursor,
		const char* event_name,
		const char* reason )
	{
		if ( !context.runtime_event_log )
			return;
		const LinearResumeFingerprint fingerprint = compute_linear_resume_fingerprint( context, cursor );
		context.runtime_event_log->write_event(
			event_name,
			[&]( std::ostream& out ) {
				write_linear_runtime_event_common_fields( out, context, reason );
				write_linear_resume_fingerprint_fields( out, fingerprint );
			} );
	}

	void write_linear_resume_snapshot(
		BestWeightHistory& history,
		const LinearBestSearchContext& context,
		const LinearSearchCursor& cursor,
		const char* reason )
	{
		if ( !history.out )
			return;
		const LinearResumeFingerprint fingerprint = compute_linear_resume_fingerprint( context, cursor );

		history.out << "=== resume_snapshot ===\n";
		history.out << "timestamp_local=" << format_local_time_now() << "\n";
		history.out << "resume_reason=" << ( reason ? reason : "resume" ) << "\n";
		write_linear_resume_fingerprint_fields( history.out, fingerprint );
		history.out << "current_trail_step_count=" << context.current_linear_trail.size() << "\n";
		write_linear_trail_full_block(
			history.out,
			context.current_linear_trail,
			"current_trail_full_begin",
			"current_trail_full_end" );
		history.out << "cursor_stack_depth=" << cursor.stack.size() << "\n";
		write_linear_cursor_snapshot( history.out, cursor );
		history.out << "\n";
		history.out.flush();
	}

	void BinaryCheckpointManager::poll( const LinearBestSearchContext& context, const LinearSearchCursor& cursor )
	{
		if ( !enabled() )
			return;
		const TwilightDream::runtime_component::RuntimeCheckpointWatchdogRequests watchdog_requests =
			TwilightDream::runtime_component::runtime_take_watchdog_checkpoint_requests(
				const_cast<TwilightDream::runtime_component::RuntimeInvocationState&>( context.runtime_state ) );
		pending_watchdog_latest = pending_watchdog_latest || watchdog_requests.latest_due;
		pending_watchdog_archive = pending_watchdog_archive || watchdog_requests.archive_due;
		const auto now = std::chrono::steady_clock::now();
		if ( pending_best || pending_watchdog_latest )
		{
			if ( write_now( context, cursor, pending_best ? "best_weight_change" : "watchdog_timer" ) )
			{
				pending_best = false;
				pending_watchdog_latest = false;
				last_write_time = now;
			}
		}
		if ( pending_watchdog_archive )
		{
			if ( write_archive_now( context, cursor, "periodic_timer" ) )
			{
				pending_watchdog_archive = false;
				last_archive_write_time = now;
			}
		}
	}

	bool BinaryCheckpointManager::write_now( const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason )
	{
		const std::uint64_t elapsed_usec = TwilightDream::best_search_shared_core::accumulated_elapsed_microseconds( context );
		const LinearResumeFingerprint fingerprint = compute_linear_resume_fingerprint( context, cursor );
		const bool ok = ( write_override != nullptr ) ?
			write_override( *this, context, cursor, reason ) :
			write_linear_checkpoint( path, context, cursor, elapsed_usec );

		if ( context.runtime_event_log )
		{
			context.runtime_event_log->write_event(
				"checkpoint_write",
				[&]( std::ostream& out ) {
					write_linear_runtime_event_common_fields( out, context, reason );
					out << "checkpoint_path=" << path << "\n";
					out << "checkpoint_reason=" << ( reason ? reason : "unspecified" ) << "\n";
					out << "binary_checkpoint_write_result=" << ( ok ? "success" : "failure" ) << "\n";
					write_linear_resume_fingerprint_fields( out, fingerprint );
				} );
		}

		{
			std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
			const auto old_flags = std::cout.flags();
			const auto old_prec = std::cout.precision();
			const auto old_fill = std::cout.fill();

			const double elapsed_seconds = static_cast<double>( elapsed_usec ) / 1e6;
			const int best_weight_out = ( context.best_weight >= INFINITE_WEIGHT ) ? -1 : context.best_weight;

			TwilightDream::runtime_component::print_progress_prefix( std::cout );
			std::cout << "[Checkpoint] search_kind=linear"
					  << "  binary_checkpoint_write_result=" << ( ok ? "success" : "failure" )
					  << "  checkpoint_reason=" << ( reason ? reason : "unspecified" )
					  << "  checkpoint_path=" << path
					  << "  total_nodes_visited=" << context.visited_node_count
					  << "  run_nodes_visited=" << context.run_visited_node_count
					  << "  best_weight=" << best_weight_out
					  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed_seconds
					  << "  cursor_stack_depth=" << cursor.stack.size()
					  << "  resume_fingerprint_hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";

			std::cout.flags( old_flags );
			std::cout.precision( old_prec );
			std::cout.fill( old_fill );
		}

		return ok;
	}

	bool BinaryCheckpointManager::write_archive_now( const LinearBestSearchContext& context, const LinearSearchCursor& cursor, const char* reason )
	{
		if ( !enabled() )
			return false;
		const std::string base_path = path;
		const std::string archive_path = TwilightDream::runtime_component::append_timestamp_to_artifact_path( base_path );
		if ( archive_path.empty() )
			return false;
		path = archive_path;
		const bool ok = write_now( context, cursor, reason ? reason : "periodic_timer" );
		path = base_path;
		return ok;
	}
}  // namespace TwilightDream::auto_search_linear
