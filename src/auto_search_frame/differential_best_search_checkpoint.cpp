#include "auto_search_frame/detail/differential_best_search_checkpoint.hpp"
#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/best_search_shared_core.hpp"

namespace TwilightDream::auto_search_differential
{
	std::string default_binary_checkpoint_path( int round_count, std::uint32_t da, std::uint32_t db )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << da << "_DiffB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << db << std::dec;
		return make_unique_timestamped_artifact_path( oss.str(), ".ckpt" );
	}

	std::string default_runtime_log_path( int round_count, std::uint32_t da, std::uint32_t db )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count << "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << da << "_DiffB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << db << std::dec;
		return RuntimeEventLog::default_path( oss.str() );
	}

	std::string BestWeightHistory::default_path( int round_count, std::uint32_t da, std::uint32_t db )
	{
		std::ostringstream oss;
		oss << "auto_checkpoint_R" << round_count
			<< "_DiffA" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << da
			<< "_DiffB" << std::hex << std::setw( 8 ) << std::setfill( '0' ) << db
			<< std::dec;
		return make_unique_timestamped_artifact_path( oss.str(), ".log" );
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
		const char* differential_stage_to_string( DifferentialSearchStage stage ) noexcept
		{
			switch ( stage )
			{
			case DifferentialSearchStage::Enter:
				return "Enter";
			case DifferentialSearchStage::FirstAdd:
				return "FirstAdd";
			case DifferentialSearchStage::FirstConst:
				return "FirstConst";
			case DifferentialSearchStage::InjB:
				return "InjB";
			case DifferentialSearchStage::SecondAdd:
				return "SecondAdd";
			case DifferentialSearchStage::SecondConst:
				return "SecondConst";
			case DifferentialSearchStage::InjA:
				return "InjA";
			default:
				return "Unknown";
			}
		}

		// Text history log: one block per trail. (Former "detailed" lines duplicated the in/out fields
		// that already appear at the start of each "full" line, so only full is written.)

		void write_differential_trail_full_block(
			std::ostream& out,
			const std::vector<DifferentialTrailStepRecord>& trail,
			const char* begin_label,
			const char* end_label )
		{
			out << begin_label << "\n";
			for ( const auto& s : trail )
			{
				out << "round_index=" << s.round_index
					<< " round_weight=" << s.round_weight
					<< " input_difference_branch_a=" << hex8( s.input_branch_a_difference )
					<< " input_difference_branch_b=" << hex8( s.input_branch_b_difference )
					<< " first_addition_term_difference=" << hex8( s.first_addition_term_difference )
					<< " output_branch_b_difference_after_first_addition=" << hex8( s.output_branch_b_difference_after_first_addition )
					<< " weight_first_addition=" << s.weight_first_addition
					<< " output_branch_a_difference_after_first_constant_subtraction=" << hex8( s.output_branch_a_difference_after_first_constant_subtraction )
					<< " weight_first_constant_subtraction=" << s.weight_first_constant_subtraction
					<< " branch_a_difference_after_first_xor_with_rotated_branch_b=" << hex8( s.branch_a_difference_after_first_xor_with_rotated_branch_b )
					<< " branch_b_difference_after_first_xor_with_rotated_branch_a=" << hex8( s.branch_b_difference_after_first_xor_with_rotated_branch_a )
					<< " injection_from_branch_b_xor_difference=" << hex8( s.injection_from_branch_b_xor_difference )
					<< " weight_injection_from_branch_b=" << s.weight_injection_from_branch_b
					<< " branch_a_difference_after_injection_from_branch_b=" << hex8( s.branch_a_difference_after_injection_from_branch_b )
					<< " branch_b_difference_after_first_bridge=" << hex8( s.branch_b_difference_after_first_bridge )
					<< " second_addition_term_difference=" << hex8( s.second_addition_term_difference )
					<< " output_branch_a_difference_after_second_addition=" << hex8( s.output_branch_a_difference_after_second_addition )
					<< " weight_second_addition=" << s.weight_second_addition
					<< " output_branch_b_difference_after_second_constant_subtraction=" << hex8( s.output_branch_b_difference_after_second_constant_subtraction )
					<< " weight_second_constant_subtraction=" << s.weight_second_constant_subtraction
					<< " branch_b_difference_after_second_xor_with_rotated_branch_a=" << hex8( s.branch_b_difference_after_second_xor_with_rotated_branch_a )
					<< " branch_a_difference_after_second_xor_with_rotated_branch_b=" << hex8( s.branch_a_difference_after_second_xor_with_rotated_branch_b )
					<< " injection_from_branch_a_xor_difference=" << hex8( s.injection_from_branch_a_xor_difference )
					<< " weight_injection_from_branch_a=" << s.weight_injection_from_branch_a
					<< " output_difference_branch_a=" << hex8( s.output_branch_a_difference )
					<< " output_difference_branch_b=" << hex8( s.output_branch_b_difference ) << "\n";
			}
			out << end_label << "\n";
		}

		void write_mod_add_enum_snapshot( std::ostream& out, const char* label, const ModularAdditionEnumerator& e )
		{
			out << label
				<< ".initialized=" << ( e.initialized ? 1 : 0 )
				<< " stop_due_to_limits=" << ( e.stop_due_to_limits ? 1 : 0 )
				<< " dfs_active=" << ( e.dfs_active ? 1 : 0 )
				<< " using_cached_shell=" << ( e.using_cached_shell ? 1 : 0 )
				<< " alpha=" << hex8( e.alpha )
				<< " beta=" << hex8( e.beta )
				<< " output_hint=" << hex8( e.output_hint )
				<< " weight_cap=" << e.weight_cap
				<< " target_weight=" << e.target_weight
				<< " word_bits=" << e.word_bits
				<< " shell_index=" << e.shell_index
				<< " shell_cache_size=" << e.shell_cache.size()
				<< " stack_step=" << e.stack_step << "\n";
		}

		void write_subconst_enum_snapshot( std::ostream& out, const char* label, const SubConstEnumerator& e )
		{
			out << label
				<< ".initialized=" << ( e.initialized ? 1 : 0 )
				<< " stop_due_to_limits=" << ( e.stop_due_to_limits ? 1 : 0 )
				<< " input_difference=" << hex8( e.input_difference )
				<< " subtractive_constant=" << hex8( e.subtractive_constant )
				<< " additive_constant=" << hex8( e.additive_constant )
				<< " output_hint=" << hex8( e.output_hint )
				<< " cap_bitvector=" << e.cap_bitvector
				<< " cap_dynamic_planning=" << e.cap_dynamic_planning
				<< " stack_step=" << e.stack_step << "\n";
		}

		void write_affine_enum_snapshot( std::ostream& out, const char* label, const AffineSubspaceEnumerator& e )
		{
			out << label
				<< ".initialized=" << ( e.initialized ? 1 : 0 )
				<< " stop_due_to_limits=" << ( e.stop_due_to_limits ? 1 : 0 )
				<< " transition_offset=" << hex8( e.transition.offset )
				<< " transition_rank_weight=" << e.transition.rank_weight
				<< " maximum_output_difference_count=" << e.maximum_output_difference_count
				<< " produced_output_difference_count=" << e.produced_output_difference_count
				<< " stack_step=" << e.stack_step << "\n";
		}

		void write_differential_cursor_snapshot( std::ostream& out, const DifferentialSearchCursor& cursor )
		{
			out << "cursor_stack_begin\n";
			for ( std::size_t i = 0; i < cursor.stack.size(); ++i )
			{
				const auto& frame = cursor.stack[ i ];
				const auto& state = frame.state;
				out << "frame_index=" << i
					<< " stage=" << differential_stage_to_string( frame.stage )
					<< " trail_size_at_entry=" << frame.trail_size_at_entry << "\n";
				out << "state.round_boundary_depth=" << state.round_boundary_depth
					<< " accumulated_weight_so_far=" << state.accumulated_weight_so_far
					<< " remaining_round_weight_lower_bound_after_this_round=" << state.remaining_round_weight_lower_bound_after_this_round
					<< " branch_a_input_difference=" << hex8( state.branch_a_input_difference )
					<< " branch_b_input_difference=" << hex8( state.branch_b_input_difference )
					<< " first_addition_term_difference=" << hex8( state.first_addition_term_difference )
					<< " output_branch_b_difference_after_first_addition=" << hex8( state.output_branch_b_difference_after_first_addition )
					<< " output_branch_a_difference_after_first_constant_subtraction=" << hex8( state.output_branch_a_difference_after_first_constant_subtraction )
					<< " branch_a_difference_after_injection_from_branch_b=" << hex8( state.branch_a_difference_after_injection_from_branch_b )
					<< " second_addition_term_difference=" << hex8( state.second_addition_term_difference )
					<< " output_branch_a_difference_after_second_addition=" << hex8( state.output_branch_a_difference_after_second_addition )
					<< " output_branch_b_difference_after_second_constant_subtraction=" << hex8( state.output_branch_b_difference_after_second_constant_subtraction )
					<< " output_difference_branch_a=" << hex8( state.output_branch_a_difference )
					<< " output_difference_branch_b=" << hex8( state.output_branch_b_difference ) << "\n";
				write_mod_add_enum_snapshot( out, "enum_first_add", frame.enum_first_add );
				write_subconst_enum_snapshot( out, "enum_first_const", frame.enum_first_const );
				write_affine_enum_snapshot( out, "enum_inj_b", frame.enum_inj_b );
				write_mod_add_enum_snapshot( out, "enum_second_add", frame.enum_second_add );
				write_subconst_enum_snapshot( out, "enum_second_const", frame.enum_second_const );
				write_affine_enum_snapshot( out, "enum_inj_a", frame.enum_inj_a );
			}
			out << "cursor_stack_end\n";
		}

		void mix_differential_trail_step( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const DifferentialTrailStepRecord& step ) noexcept
		{
			fp.mix_i32( step.round_index );
			fp.mix_u32( step.input_branch_a_difference );
			fp.mix_u32( step.input_branch_b_difference );
			fp.mix_u32( step.first_addition_term_difference );
			fp.mix_u32( step.output_branch_b_difference_after_first_addition );
			fp.mix_u64( step.weight_first_addition );
			fp.mix_u32( step.output_branch_a_difference_after_first_constant_subtraction );
			fp.mix_u64( step.weight_first_constant_subtraction );
			fp.mix_u32( step.branch_a_difference_after_first_xor_with_rotated_branch_b );
			fp.mix_u32( step.branch_b_difference_after_first_xor_with_rotated_branch_a );
			fp.mix_u32( step.injection_from_branch_b_xor_difference );
			fp.mix_u64( step.weight_injection_from_branch_b );
			fp.mix_u32( step.branch_a_difference_after_injection_from_branch_b );
			fp.mix_u32( step.branch_b_difference_after_first_bridge );
			fp.mix_u32( step.second_addition_term_difference );
			fp.mix_u32( step.output_branch_a_difference_after_second_addition );
			fp.mix_u64( step.weight_second_addition );
			fp.mix_u32( step.output_branch_b_difference_after_second_constant_subtraction );
			fp.mix_u64( step.weight_second_constant_subtraction );
			fp.mix_u32( step.branch_b_difference_after_second_xor_with_rotated_branch_a );
			fp.mix_u32( step.branch_a_difference_after_second_xor_with_rotated_branch_b );
			fp.mix_u32( step.injection_from_branch_a_xor_difference );
			fp.mix_u64( step.weight_injection_from_branch_a );
			fp.mix_u32( step.output_branch_a_difference );
			fp.mix_u32( step.output_branch_b_difference );
			fp.mix_u64( step.round_weight );
		}

		void mix_injection_transition( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const InjectionAffineTransition& transition ) noexcept
		{
			fp.mix_u32( transition.offset );
			for ( const std::uint32_t basis : transition.basis_vectors )
				fp.mix_u32( basis );
			fp.mix_u64( transition.rank_weight );
		}

		void mix_mod_add_enum(
			TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp,
			DifferentialResumeFingerprint& summary,
			const ModularAdditionEnumerator& enumerator ) noexcept
		{
			summary.modular_add_frame_count += 1;
			summary.modular_add_shell_index_total += static_cast<std::uint64_t>( enumerator.shell_index );

			fp.mix_bool( enumerator.initialized );
			fp.mix_bool( enumerator.stop_due_to_limits );
			fp.mix_bool( enumerator.dfs_active );
			fp.mix_bool( enumerator.using_cached_shell );
			fp.mix_u32( enumerator.alpha );
			fp.mix_u32( enumerator.beta );
			fp.mix_u32( enumerator.output_hint );
			fp.mix_u64( enumerator.weight_cap );
			fp.mix_u64( enumerator.target_weight );
			fp.mix_i32( enumerator.word_bits );
			fp.mix_u64( static_cast<std::uint64_t>( enumerator.shell_index ) );
			fp.mix_i32( enumerator.stack_step );
			for ( const auto& frame : enumerator.stack )
			{
				fp.mix_i32( frame.bit_position );
				fp.mix_u32( frame.prefix );
				fp.mix_u32( frame.prefer );
				fp.mix_u8( frame.state );
			}
		}

		void mix_subconst_enum( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const SubConstEnumerator& enumerator ) noexcept
		{
			fp.mix_bool( enumerator.initialized );
			fp.mix_bool( enumerator.stop_due_to_limits );
			fp.mix_u32( enumerator.input_difference );
			fp.mix_u32( enumerator.subtractive_constant );
			fp.mix_u32( enumerator.additive_constant );
			fp.mix_u32( enumerator.output_hint );
			fp.mix_i32( enumerator.cap_bitvector );
			fp.mix_i32( enumerator.cap_dynamic_planning );
			fp.mix_i32( enumerator.stack_step );
			for ( const auto& frame : enumerator.stack )
			{
				fp.mix_i32( frame.bit_position );
				fp.mix_u32( frame.prefix );
				for ( const std::uint64_t count : frame.prefix_counts )
					fp.mix_u64( count );
				fp.mix_u32( frame.preferred_bit );
				fp.mix_u8( frame.state );
			}
		}

		void mix_affine_enum( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const AffineSubspaceEnumerator& enumerator ) noexcept
		{
			fp.mix_bool( enumerator.initialized );
			fp.mix_bool( enumerator.stop_due_to_limits );
			mix_injection_transition( fp, enumerator.transition );
			fp.mix_u64( static_cast<std::uint64_t>( enumerator.maximum_output_difference_count ) );
			fp.mix_u64( static_cast<std::uint64_t>( enumerator.produced_output_difference_count ) );
			fp.mix_i32( enumerator.stack_step );
			for ( const auto& frame : enumerator.stack )
			{
				fp.mix_i32( frame.basis_index );
				fp.mix_u32( frame.current_difference );
				fp.mix_u8( frame.state );
			}
		}

		void mix_differential_round_state( TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder& fp, const DifferentialRoundSearchState& state ) noexcept
		{
			fp.mix_i32( state.round_boundary_depth );
			fp.mix_u64( state.accumulated_weight_so_far );
			fp.mix_u64( state.remaining_round_weight_lower_bound_after_this_round );
			fp.mix_u32( state.branch_a_input_difference );
			fp.mix_u32( state.branch_b_input_difference );
			mix_differential_trail_step( fp, state.base_step );
			fp.mix_u32( state.first_addition_term_difference );
			fp.mix_u32( state.optimal_output_branch_b_difference_after_first_addition );
			fp.mix_u64( state.optimal_weight_first_addition );
			fp.mix_u64( state.weight_cap_first_addition );
			fp.mix_u32( state.output_branch_b_difference_after_first_addition );
			fp.mix_u64( state.weight_first_addition );
			fp.mix_u64( state.accumulated_weight_after_first_addition );
			fp.mix_u32( state.output_branch_a_difference_after_first_constant_subtraction );
			fp.mix_u64( state.weight_first_constant_subtraction );
			fp.mix_u64( state.accumulated_weight_after_first_constant_subtraction );
			fp.mix_u32( state.branch_a_difference_after_first_xor_with_rotated_branch_b );
			fp.mix_u32( state.branch_b_difference_after_first_xor_with_rotated_branch_a );
			fp.mix_u32( state.branch_b_difference_after_first_bridge );
			fp.mix_u64( state.weight_injection_from_branch_b );
			fp.mix_u64( state.accumulated_weight_before_second_addition );
			fp.mix_u32( state.injection_from_branch_b_xor_difference );
			fp.mix_u32( state.branch_a_difference_after_injection_from_branch_b );
			fp.mix_u32( state.second_addition_term_difference );
			fp.mix_u32( state.optimal_output_branch_a_difference_after_second_addition );
			fp.mix_u64( state.optimal_weight_second_addition );
			fp.mix_u64( state.weight_cap_second_addition );
			fp.mix_u32( state.output_branch_a_difference_after_second_addition );
			fp.mix_u64( state.weight_second_addition );
			fp.mix_u64( state.accumulated_weight_after_second_addition );
			fp.mix_u32( state.output_branch_b_difference_after_second_constant_subtraction );
			fp.mix_u64( state.weight_second_constant_subtraction );
			fp.mix_u64( state.accumulated_weight_after_second_constant_subtraction );
			fp.mix_u32( state.branch_b_difference_after_second_xor_with_rotated_branch_a );
			fp.mix_u32( state.branch_a_difference_after_second_xor_with_rotated_branch_b );
			fp.mix_u64( state.weight_injection_from_branch_a );
			fp.mix_u64( state.accumulated_weight_at_round_end );
			fp.mix_u32( state.injection_from_branch_a_xor_difference );
			fp.mix_u32( state.output_branch_a_difference );
			fp.mix_u32( state.output_branch_b_difference );
		}

		DifferentialResumeFingerprint compute_differential_resume_fingerprint_impl(
			const std::vector<DifferentialTrailStepRecord>& current_trail,
			const DifferentialSearchCursor& cursor ) noexcept
		{
			DifferentialResumeFingerprint fingerprint {};
			TwilightDream::best_search_shared_core::CheckpointFingerprintBuilder fp {};
			fingerprint.cursor_stack_depth = static_cast<std::uint64_t>( cursor.stack.size() );
			fingerprint.current_trail_size = static_cast<std::uint64_t>( current_trail.size() );
			fp.mix_u64( fingerprint.current_trail_size );
			for ( const auto& step : current_trail )
				mix_differential_trail_step( fp, step );
			fp.mix_u64( fingerprint.cursor_stack_depth );
			for ( const auto& frame : cursor.stack )
			{
				fp.mix_enum( frame.stage );
				fp.mix_u64( static_cast<std::uint64_t>( frame.trail_size_at_entry ) );
				mix_differential_round_state( fp, frame.state );
				mix_mod_add_enum( fp, fingerprint, frame.enum_first_add );
				mix_subconst_enum( fp, frame.enum_first_const );
				mix_affine_enum( fp, frame.enum_inj_b );
				mix_mod_add_enum( fp, fingerprint, frame.enum_second_add );
				mix_subconst_enum( fp, frame.enum_second_const );
				mix_affine_enum( fp, frame.enum_inj_a );
			}
			fingerprint.hash = fp.finish();
			return fingerprint;
		}

		void write_differential_runtime_event_common_fields( std::ostream& out, const DifferentialBestSearchContext& context, const char* reason )
		{
			out << "round_count=" << context.configuration.round_count << "\n";
			out << "start_difference_branch_a=" << hex8( context.start_difference_a ) << "\n";
			out << "start_difference_branch_b=" << hex8( context.start_difference_b ) << "\n";
			out << "best_weight=" << ( ( context.best_total_weight >= INFINITE_WEIGHT ) ? -1 : context.best_total_weight ) << "\n";
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

	void BestWeightHistory::maybe_write( const DifferentialBestSearchContext& context, const char* reason )
	{
		if ( !out )
			return;
		if ( context.best_total_weight >= INFINITE_WEIGHT )
			return;
		if ( context.best_total_weight == last_written_weight )
			return;

		const double elapsed_seconds = TwilightDream::best_search_shared_core::accumulated_elapsed_seconds( context );
		const auto&	 config = context.configuration;

		out << "=== checkpoint ===\n";
		out << "timestamp_local=" << format_local_time_now() << "\n";
		out << "checkpoint_reason=" << ( reason ? reason : "best_weight_changed" ) << "\n";
		out << "round_count=" << config.round_count << "\n";
		out << "start_difference_branch_a=" << hex8( context.start_difference_a ) << "\n";
		out << "start_difference_branch_b=" << hex8( context.start_difference_b ) << "\n";
		out << "best_total_weight=" << context.best_total_weight << "\n";
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
		out << "maximum_transition_output_differences=" << config.maximum_transition_output_differences << "\n";
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
		out << "strict_remaining_round_lower_bound=" << ( config.strict_remaining_round_lower_bound ? 1 : 0 ) << "\n";
		out << "enable_weight_sliced_pddt=" << ( config.enable_weight_sliced_pddt ? 1 : 0 ) << "\n";
		out << "weight_sliced_pddt_max_weight=" << config.weight_sliced_pddt_max_weight << "\n";
		out << "trail_step_count=" << context.best_differential_trail.size() << "\n";
		write_differential_trail_full_block(
			out,
			context.best_differential_trail,
			"best_trail_full_begin",
			"best_trail_full_end" );
		out << "\n";
		out.flush();
		last_written_weight = context.best_total_weight;
	}

	// On-disk field order is frozen with auto_search_checkpoint::kVersion. Some trailing runtime fields
	// (run_visited_node_count snapshot, progress_node_mask, last-run limit flags) are not consumed when
	// merging CLI/runtime overrides on single-run resume â€” see stored_runtime_metadata_for_resume_control_merge.
	bool write_differential_checkpoint_payload( TwilightDream::auto_search_checkpoint::BinaryWriter& w, const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, std::uint64_t elapsed_usec )
	{
		w.write_u64( elapsed_usec );
		write_config( w, context.configuration );
		w.write_u32( context.start_difference_a );
		w.write_u32( context.start_difference_b );
		w.write_string( context.history_log_output_path );
		w.write_string( context.runtime_log_output_path );
		w.write_u64( context.visited_node_count );
		w.write_u64( context.run_visited_node_count );
		w.write_u64( context.best_total_weight );
		write_trail_vector( w, context.best_differential_trail );
		write_trail_vector( w, context.current_differential_trail );
		write_cursor( w, cursor );
		w.write_u8( context.active_problem_valid ? 1u : 0u );
		w.write_u8( context.active_problem_is_root ? 1u : 0u );
		TwilightDream::residual_frontier_shared::write_residual_problem_record( w, context.active_problem_record );
		w.write_u64( context.runtime_controls.maximum_search_nodes );
		w.write_u64( context.runtime_controls.maximum_search_seconds );
		w.write_u64( context.runtime_controls.progress_every_seconds );
		w.write_u64( context.runtime_controls.checkpoint_every_seconds );
		w.write_u64( context.runtime_state.progress_node_mask );
		w.write_u8( differential_runtime_node_limit_hit( context ) ? 1u : 0u );
		w.write_u8( runtime_time_limit_hit( context.runtime_controls, context.runtime_state ) ? 1u : 0u );
		context.memoization.serialize( w );
		TwilightDream::residual_frontier_shared::write_record_vector(
			w,
			context.pending_frontier,
			TwilightDream::residual_frontier_shared::write_residual_problem_record );
		TwilightDream::residual_frontier_shared::write_record_vector(
			w,
			context.pending_frontier_entries,
			write_residual_frontier_entry );
		TwilightDream::residual_frontier_shared::write_record_vector(
			w,
			context.completed_source_input_pairs,
			TwilightDream::residual_frontier_shared::write_residual_problem_record );
		TwilightDream::residual_frontier_shared::write_record_vector(
			w,
			context.completed_output_as_next_input_pairs,
			TwilightDream::residual_frontier_shared::write_residual_problem_record );
		TwilightDream::residual_frontier_shared::write_completed_residual_set(
			w,
			context.completed_residual_set );
		TwilightDream::residual_frontier_shared::write_best_prefix_table(
			w,
			context.best_prefix_by_residual_key );
		TwilightDream::residual_frontier_shared::write_record_vector(
			w,
			context.global_residual_result_table,
			TwilightDream::residual_frontier_shared::write_residual_result_record );
		TwilightDream::residual_frontier_shared::write_residual_counters(
			w,
			context.residual_counters );
		return w.ok();
	}

	bool read_differential_checkpoint_payload(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		DifferentialCheckpointLoadResult& out,
		TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, SearchWeight>& memo )
	{
		if ( !r.read_u64( out.accumulated_elapsed_usec ) )
			return false;
		if ( !read_config( r, out.configuration ) )
			return false;
		if ( !r.read_u32( out.start_difference_a ) )
			return false;
		if ( !r.read_u32( out.start_difference_b ) )
			return false;
		if ( !r.read_string( out.history_log_path ) )
			return false;
		if ( !r.read_string( out.runtime_log_path ) )
			return false;
		if ( !r.read_u64( out.total_nodes_visited ) )
			return false;
		if ( !r.read_u64( out.run_nodes_visited ) )
			return false;
		if ( !r.read_u64( out.best_total_weight ) )
			return false;
		if ( !read_trail_vector( r, out.best_trail ) )
			return false;
		if ( !read_trail_vector( r, out.current_trail ) )
			return false;
		if ( !read_cursor( r, out.cursor ) )
			return false;
		std::uint8_t active_problem_valid = 0;
		std::uint8_t active_problem_is_root = 0;
		if ( !r.read_u8( active_problem_valid ) )
			return false;
		if ( !r.read_u8( active_problem_is_root ) )
			return false;
		out.active_problem_valid = ( active_problem_valid != 0 );
		out.active_problem_is_root = ( active_problem_is_root != 0 );
		if ( !TwilightDream::residual_frontier_shared::read_residual_problem_record( r, out.active_problem_record ) )
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
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 out.pending_frontier,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 out.pending_frontier_entries,
				 read_residual_frontier_entry ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 out.completed_source_input_pairs,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 out.completed_output_as_next_input_pairs,
				 TwilightDream::residual_frontier_shared::read_residual_problem_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_completed_residual_set(
				 r,
				 out.completed_residual_set ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_best_prefix_table(
				 r,
				 out.best_prefix_by_residual_key ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_record_vector(
				 r,
				 out.global_residual_result_table,
				 TwilightDream::residual_frontier_shared::read_residual_result_record ) )
			return false;
		if ( !TwilightDream::residual_frontier_shared::read_residual_counters(
				 r,
				 out.residual_counters ) )
			return false;
		return true;
	}

	bool write_differential_checkpoint( const std::string& path, const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, std::uint64_t elapsed_usec )
	{
		return TwilightDream::auto_search_checkpoint::write_atomic( path, [ & ]( TwilightDream::auto_search_checkpoint::BinaryWriter& w ) {
			if ( !TwilightDream::auto_search_checkpoint::write_header( w, TwilightDream::auto_search_checkpoint::SearchKind::DifferentialResidualFrontierBest ) )
				return false;
			return write_differential_checkpoint_payload( w, context, cursor, elapsed_usec );
		} );
	}

	bool read_differential_checkpoint( const std::string& path, DifferentialCheckpointLoadResult& out, TwilightDream::runtime_component::BestWeightMemoizationByDepth<std::uint64_t, SearchWeight>& memo )
	{
		TwilightDream::auto_search_checkpoint::BinaryReader r( path );
		if ( !r.ok() )
			return false;
		TwilightDream::auto_search_checkpoint::SearchKind kind {};
		if ( !TwilightDream::auto_search_checkpoint::read_header( r, kind ) )
			return false;
		if ( kind != TwilightDream::auto_search_checkpoint::SearchKind::DifferentialResidualFrontierBest )
			return false;
		return read_differential_checkpoint_payload( r, out, memo );
	}

	DifferentialResumeFingerprint compute_differential_resume_fingerprint( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor ) noexcept
	{
		return compute_differential_resume_fingerprint_impl( context.current_differential_trail, cursor );
	}

	DifferentialResumeFingerprint compute_differential_resume_fingerprint( const DifferentialCheckpointLoadResult& load ) noexcept
	{
		return compute_differential_resume_fingerprint_impl( load.current_trail, load.cursor );
	}

	void write_differential_resume_fingerprint_fields(
		std::ostream& out,
		const DifferentialResumeFingerprint& fingerprint,
		const char* prefix )
	{
		const char* key_prefix = ( prefix != nullptr ) ? prefix : "resume_fingerprint_";
		out << key_prefix << "hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";
		out << key_prefix << "cursor_stack_depth=" << fingerprint.cursor_stack_depth << "\n";
		out << key_prefix << "current_trail_size=" << fingerprint.current_trail_size << "\n";
		out << key_prefix << "modular_add_frame_count=" << fingerprint.modular_add_frame_count << "\n";
		out << key_prefix << "modular_add_shell_index_total=" << fingerprint.modular_add_shell_index_total << "\n";
		out << key_prefix << "modular_add_shell_cache_entries=" << fingerprint.modular_add_shell_cache_entries << "\n";
		out << key_prefix << "modular_add_shell_cache_hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.modular_add_shell_cache_hash ) << "\n";
	}

	void differential_runtime_log_resume_event(
		const DifferentialBestSearchContext& context,
		const DifferentialSearchCursor& cursor,
		const char* event_name,
		const char* reason )
	{
		if ( !context.runtime_event_log )
			return;
		const DifferentialResumeFingerprint fingerprint = compute_differential_resume_fingerprint( context, cursor );
		context.runtime_event_log->write_event(
			event_name,
			[&]( std::ostream& out ) {
				write_differential_runtime_event_common_fields( out, context, reason );
				write_differential_resume_fingerprint_fields( out, fingerprint );
			} );
	}

	bool materialize_differential_resume_rebuildable_state(
		DifferentialBestSearchContext& context,
		DifferentialSearchCursor& cursor )
	{
		for ( auto& frame : cursor.stack )
		{
			if ( !rebuild_modular_addition_enumerator_shell_cache( context.configuration, frame.enum_first_add ) )
				return false;
			if ( !rebuild_modular_addition_enumerator_shell_cache( context.configuration, frame.enum_second_add ) )
				return false;
		}
		return true;
	}

	void write_differential_resume_snapshot(
		BestWeightHistory& history,
		const DifferentialBestSearchContext& context,
		const DifferentialSearchCursor& cursor,
		const char* reason )
	{
		if ( !history.out )
			return;
		const DifferentialResumeFingerprint fingerprint = compute_differential_resume_fingerprint( context, cursor );

		history.out << "=== resume_snapshot ===\n";
		history.out << "timestamp_local=" << format_local_time_now() << "\n";
		history.out << "resume_reason=" << ( reason ? reason : "resume" ) << "\n";
		write_differential_resume_fingerprint_fields( history.out, fingerprint );
		history.out << "current_trail_step_count=" << context.current_differential_trail.size() << "\n";
		write_differential_trail_full_block(
			history.out,
			context.current_differential_trail,
			"current_trail_full_begin",
			"current_trail_full_end" );
		history.out << "cursor_stack_depth=" << cursor.stack.size() << "\n";
		write_differential_cursor_snapshot( history.out, cursor );
		history.out << "\n";
		history.out.flush();
	}

	void BinaryCheckpointManager::poll( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor )
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

	bool BinaryCheckpointManager::write_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason )
	{
		const std::uint64_t elapsed_usec = TwilightDream::best_search_shared_core::accumulated_elapsed_microseconds( context );
		const DifferentialResumeFingerprint fingerprint = compute_differential_resume_fingerprint( context, cursor );
		const bool ok = ( write_override != nullptr ) ?
			write_override( *this, context, cursor, reason ) :
			write_differential_checkpoint( path, context, cursor, elapsed_usec );

		if ( context.runtime_event_log )
		{
			context.runtime_event_log->write_event(
				"checkpoint_write",
				[&]( std::ostream& out ) {
					write_differential_runtime_event_common_fields( out, context, reason );
					out << "checkpoint_path=" << path << "\n";
					out << "checkpoint_reason=" << ( reason ? reason : "unspecified" ) << "\n";
					out << "binary_checkpoint_write_result=" << ( ok ? "success" : "failure" ) << "\n";
					write_differential_resume_fingerprint_fields( out, fingerprint );
				} );
		}

		{
			std::scoped_lock lk( TwilightDream::runtime_component::cout_mutex() );
			const auto old_flags = std::cout.flags();
			const auto old_prec = std::cout.precision();
			const auto old_fill = std::cout.fill();

			const double elapsed_seconds = static_cast<double>( elapsed_usec ) / 1e6;
			const std::int64_t best_weight_out = display_search_weight( context.best_total_weight );

			TwilightDream::runtime_component::print_progress_prefix( std::cout );
			std::cout << "[Checkpoint] search_kind=differential"
					  << "  binary_checkpoint_write_result=" << ( ok ? "success" : "failure" )
					  << "  checkpoint_reason=" << ( reason ? reason : "unspecified" )
					  << "  checkpoint_path=" << path
					  << "  total_nodes_visited=" << context.visited_node_count
					  << "  run_nodes_visited=" << context.run_visited_node_count
					  << "  best_total_weight=" << best_weight_out
					  << "  elapsed_seconds=" << std::fixed << std::setprecision( 2 ) << elapsed_seconds
					  << "  cursor_stack_depth=" << cursor.stack.size()
					  << "  resume_fingerprint_hash=" << TwilightDream::best_search_shared_core::checkpoint_fingerprint_hex( fingerprint.hash ) << "\n";

			std::cout.flags( old_flags );
			std::cout.precision( old_prec );
			std::cout.fill( old_fill );
		}

		return ok;
	}

	bool BinaryCheckpointManager::write_archive_now( const DifferentialBestSearchContext& context, const DifferentialSearchCursor& cursor, const char* reason )
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
}  // namespace TwilightDream::auto_search_differential
