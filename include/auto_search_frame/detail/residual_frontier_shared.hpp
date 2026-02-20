#pragma once

#include "arx_analysis_operators/DefineSearchWeight.hpp"
#include "auto_search_frame/search_checkpoint.hpp"

#include <cstdint>
#include <ostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TwilightDream::residual_frontier_shared
{
	using SearchWeight = TwilightDream::AutoSearchFrameDefine::SearchWeight;
	using TwilightDream::AutoSearchFrameDefine::INFINITE_WEIGHT;

	enum class ResidualAnalysisDomain : std::uint8_t
	{
		Differential = 0,
		Linear = 1
	};

	enum class ResidualObjectiveKind : std::uint8_t
	{
		BestWeight = 0,
		HullCollect = 1
	};

	enum class ResidualPairEventKind : std::uint8_t
	{
		InterruptedSourceInputPair = 0,
		InterruptedOutputAsNextInputPair = 1,
		CompletedSourceInputPair = 2,
		CompletedOutputAsNextInputPair = 3,
		BestWeightImproved = 4
	};

	enum class ResidualSourceTag : std::uint8_t
	{
		SourceInputPair = 0,
		OutputAsNextInputPair = 1
	};

	struct ResidualProblemKey
	{
		ResidualAnalysisDomain domain = ResidualAnalysisDomain::Differential;
		ResidualObjectiveKind objective = ResidualObjectiveKind::BestWeight;
		std::int32_t rounds_remaining = 0;
		std::uint32_t absolute_round_index = 0;
		std::uint8_t stage_cursor = 0;
		std::uint8_t source_tag = 0;
		std::uint32_t pair_a = 0;
		std::uint32_t pair_b = 0;
		std::uint32_t pair_c = 0;
		std::uint64_t suffix_profile_id = 0;

		friend bool operator==( const ResidualProblemKey& lhs, const ResidualProblemKey& rhs ) noexcept
		{
			return lhs.domain == rhs.domain &&
				   lhs.objective == rhs.objective &&
				   lhs.rounds_remaining == rhs.rounds_remaining &&
				   lhs.stage_cursor == rhs.stage_cursor &&
				   lhs.pair_a == rhs.pair_a &&
				   lhs.pair_b == rhs.pair_b &&
				   lhs.pair_c == rhs.pair_c &&
				   lhs.suffix_profile_id == rhs.suffix_profile_id;
		}
	};

	struct ResidualProblemKeyHash
	{
		std::size_t operator()( const ResidualProblemKey& key ) const noexcept
		{
			std::uint64_t h = std::uint64_t( key.pair_a ) | ( std::uint64_t( key.pair_b ) << 32 );
			h ^= std::uint64_t( key.pair_c ) * 0x9e3779b97f4a7c15ULL;
			h ^= std::uint64_t( static_cast<std::uint8_t>( key.domain ) ) << 5;
			h ^= std::uint64_t( static_cast<std::uint8_t>( key.objective ) ) << 13;
			h ^= std::uint64_t( std::uint32_t( key.rounds_remaining ) ) << 21;
			h ^= std::uint64_t( key.stage_cursor ) << 29;
			h ^= key.suffix_profile_id;
			h ^= ( h >> 33 );
			h *= 0xff51afd7ed558ccdULL;
			h ^= ( h >> 33 );
			h *= 0xc4ceb9fe1a85ec53ULL;
			h ^= ( h >> 33 );
			return static_cast<std::size_t>( h );
		}
	};

	struct ResidualProblemRecord
	{
		ResidualProblemKey key {};
		SearchWeight best_prefix_weight = INFINITE_WEIGHT;
		std::uint64_t sequence_number = 0;
		std::uint64_t discovery_run_nodes = 0;
	};

	struct ResidualResultRecord
	{
		ResidualProblemKey key {};
		SearchWeight best_weight = INFINITE_WEIGHT;
		SearchWeight collect_weight_cap = 0;
		bool solved = false;
		bool certified = false;
		bool exact_within_collect_weight_cap = false;
	};

	struct ResidualCounters
	{
		std::uint64_t interrupted_source_input_pair_count = 0;
		std::uint64_t interrupted_output_as_next_input_pair_count = 0;
		std::uint64_t completed_source_input_pair_count = 0;
		std::uint64_t completed_output_as_next_input_pair_count = 0;
		std::uint64_t best_weight_improvement_count = 0;
		std::uint64_t repeated_or_dominated_residual_skip_count = 0;
	};

	struct LocalResidualStateKey
	{
		std::uint8_t stage_cursor = 0;
		std::uint32_t pair_a = 0;
		std::uint32_t pair_b = 0;
		std::uint32_t pair_c = 0;

		friend bool operator==( const LocalResidualStateKey& lhs, const LocalResidualStateKey& rhs ) noexcept
		{
			return lhs.stage_cursor == rhs.stage_cursor &&
				   lhs.pair_a == rhs.pair_a &&
				   lhs.pair_b == rhs.pair_b &&
				   lhs.pair_c == rhs.pair_c;
		}
	};

	struct LocalResidualStateKeyHash
	{
		std::size_t operator()( const LocalResidualStateKey& key ) const noexcept
		{
			std::uint64_t h = std::uint64_t( key.pair_a ) | ( std::uint64_t( key.pair_b ) << 32 );
			h ^= std::uint64_t( key.pair_c ) * 0x9e3779b97f4a7c15ULL;
			h ^= std::uint64_t( key.stage_cursor ) << 17;
			h ^= ( h >> 33 );
			h *= 0xff51afd7ed558ccdULL;
			h ^= ( h >> 33 );
			h *= 0xc4ceb9fe1a85ec53ULL;
			h ^= ( h >> 33 );
			return static_cast<std::size_t>( h );
		}
	};

	struct LocalResidualStateDominanceEntry
	{
		SearchWeight best_prefix_weight = INFINITE_WEIGHT;
		std::uint64_t last_touch_generation = 0;
	};

	struct LocalResidualStateDominanceTable
	{
		static constexpr std::size_t kDefaultCapacity = 4096u;

		std::size_t capacity = kDefaultCapacity;
		std::uint64_t touch_generation = 0;
		std::unordered_map<
			LocalResidualStateKey,
			LocalResidualStateDominanceEntry,
			LocalResidualStateKeyHash> table {};

		void clear()
		{
			touch_generation = 0;
			table.clear();
		}

		void set_capacity( std::size_t new_capacity )
		{
			capacity = new_capacity;
			if ( capacity == 0 )
			{
				clear();
				return;
			}
			if ( table.size() <= capacity )
				return;
			while ( table.size() > capacity )
				evict_one_oldest();
		}

		bool should_prune_or_update(
			std::uint8_t stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			std::uint32_t pair_c,
			SearchWeight prefix_weight )
		{
			if ( capacity == 0 )
				return false;

			const std::uint64_t generation = ++touch_generation;
			const LocalResidualStateKey key {
				stage_cursor,
				pair_a,
				pair_b,
				pair_c };

			if ( auto it = table.find( key ); it != table.end() )
			{
				it->second.last_touch_generation = generation;
				if ( it->second.best_prefix_weight <= prefix_weight )
					return true;
				it->second.best_prefix_weight = prefix_weight;
				return false;
			}

			table.emplace(
				key,
				LocalResidualStateDominanceEntry {
					prefix_weight,
					generation } );
			if ( table.size() > capacity )
				evict_one_oldest();
			return false;
		}

		bool should_prune_or_update(
			std::uint8_t stage_cursor,
			std::uint32_t pair_a,
			std::uint32_t pair_b,
			SearchWeight prefix_weight )
		{
			return should_prune_or_update(
				stage_cursor,
				pair_a,
				pair_b,
				0u,
				prefix_weight );
		}

	private:
		void evict_one_oldest()
		{
			if ( table.empty() )
				return;

			auto oldest_it = table.begin();
			for ( auto it = table.begin(); it != table.end(); ++it )
			{
				if ( it->second.last_touch_generation < oldest_it->second.last_touch_generation )
					oldest_it = it;
			}
			table.erase( oldest_it );
		}
	};

	inline const char* residual_analysis_domain_to_string( ResidualAnalysisDomain domain ) noexcept
	{
		switch ( domain )
		{
		case ResidualAnalysisDomain::Differential:
			return "differential";
		case ResidualAnalysisDomain::Linear:
			return "linear";
		default:
			return "unknown";
		}
	}

	inline const char* residual_objective_kind_to_string( ResidualObjectiveKind kind ) noexcept
	{
		switch ( kind )
		{
		case ResidualObjectiveKind::BestWeight:
			return "best_weight";
		case ResidualObjectiveKind::HullCollect:
			return "hull_collect";
		default:
			return "unknown";
		}
	}

	inline const char* residual_pair_event_kind_to_string( ResidualPairEventKind kind ) noexcept
	{
		switch ( kind )
		{
		case ResidualPairEventKind::InterruptedSourceInputPair:
			return "interrupted_source_input_pair";
		case ResidualPairEventKind::InterruptedOutputAsNextInputPair:
			return "interrupted_output_as_next_input_pair";
		case ResidualPairEventKind::CompletedSourceInputPair:
			return "completed_source_input_pair";
		case ResidualPairEventKind::CompletedOutputAsNextInputPair:
			return "completed_output_as_next_input_pair";
		case ResidualPairEventKind::BestWeightImproved:
			return "best_weight_improved";
		default:
			return "unknown";
		}
	}

	inline const char* residual_source_tag_to_string( ResidualSourceTag tag ) noexcept
	{
		switch ( tag )
		{
		case ResidualSourceTag::SourceInputPair:
			return "source_input_pair";
		case ResidualSourceTag::OutputAsNextInputPair:
			return "output_as_next_input_pair";
		default:
			return "unknown";
		}
	}

	inline ResidualSourceTag residual_source_tag_from_value( std::uint8_t source_tag ) noexcept
	{
		switch ( source_tag )
		{
		case static_cast<std::uint8_t>( ResidualSourceTag::SourceInputPair ):
			return ResidualSourceTag::SourceInputPair;
		case static_cast<std::uint8_t>( ResidualSourceTag::OutputAsNextInputPair ):
			return ResidualSourceTag::OutputAsNextInputPair;
		default:
			return static_cast<ResidualSourceTag>( 0xFFu );
		}
	}

	inline const char* residual_source_tag_to_string( std::uint8_t source_tag ) noexcept
	{
		return residual_source_tag_to_string( residual_source_tag_from_value( source_tag ) );
	}

	inline void write_residual_problem_key_debug_fields_inline(
		std::ostream& out,
		const ResidualProblemKey& key )
	{
		out << " absolute_round_index=" << key.absolute_round_index
			<< " source_tag=" << residual_source_tag_to_string( key.source_tag )
			<< " pair_c=" << key.pair_c
			<< " suffix_profile_id=" << key.suffix_profile_id;
	}

	inline void write_residual_problem_key_debug_fields_multiline(
		std::ostream& out,
		const ResidualProblemKey& key )
	{
		out << "absolute_round_index=" << key.absolute_round_index << "\n";
		out << "source_tag=" << residual_source_tag_to_string( key.source_tag ) << "\n";
		out << "pair_c=" << key.pair_c << "\n";
		out << "suffix_profile_id=" << key.suffix_profile_id << "\n";
	}

	inline ResidualProblemRecord make_residual_problem_record(
		ResidualAnalysisDomain domain,
		ResidualObjectiveKind objective,
		std::int32_t rounds_remaining,
		std::uint8_t stage_cursor,
		std::uint32_t pair_a,
		std::uint32_t pair_b,
		std::uint32_t pair_c,
		SearchWeight best_prefix_weight,
		std::uint64_t sequence_number,
		std::uint64_t discovery_run_nodes ) noexcept
	{
		ResidualProblemRecord record {};
		record.key.domain = domain;
		record.key.objective = objective;
		record.key.rounds_remaining = rounds_remaining;
		record.key.stage_cursor = stage_cursor;
		record.key.pair_a = pair_a;
		record.key.pair_b = pair_b;
		record.key.pair_c = pair_c;
		record.best_prefix_weight = best_prefix_weight;
		record.sequence_number = sequence_number;
		record.discovery_run_nodes = discovery_run_nodes;
		return record;
	}

	inline ResidualProblemRecord make_residual_problem_record(
		ResidualAnalysisDomain domain,
		ResidualObjectiveKind objective,
		std::int32_t rounds_remaining,
		std::uint8_t stage_cursor,
		std::uint32_t pair_a,
		std::uint32_t pair_b,
		SearchWeight best_prefix_weight,
		std::uint64_t sequence_number,
		std::uint64_t discovery_run_nodes ) noexcept
	{
		return make_residual_problem_record(
			domain,
			objective,
			rounds_remaining,
			stage_cursor,
			pair_a,
			pair_b,
			0u,
			best_prefix_weight,
			sequence_number,
			discovery_run_nodes );
	}

	inline void write_residual_problem_key(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const ResidualProblemKey& key )
	{
		w.write_u8( static_cast<std::uint8_t>( key.domain ) );
		w.write_u8( static_cast<std::uint8_t>( key.objective ) );
		w.write_i32( key.rounds_remaining );
		w.write_u32( key.absolute_round_index );
		w.write_u8( key.stage_cursor );
		w.write_u8( key.source_tag );
		w.write_u32( key.pair_a );
		w.write_u32( key.pair_b );
		w.write_u32( key.pair_c );
		w.write_u64( key.suffix_profile_id );
	}

	inline bool read_residual_problem_key(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		ResidualProblemKey& key )
	{
		std::uint8_t domain = 0;
		std::uint8_t objective = 0;
		if ( !r.read_u8( domain ) ) return false;
		if ( !r.read_u8( objective ) ) return false;
		if ( !r.read_i32( key.rounds_remaining ) ) return false;
		if ( !r.read_u32( key.absolute_round_index ) ) return false;
		if ( !r.read_u8( key.stage_cursor ) ) return false;
		if ( !r.read_u8( key.source_tag ) ) return false;
		if ( !r.read_u32( key.pair_a ) ) return false;
		if ( !r.read_u32( key.pair_b ) ) return false;
		if ( !r.read_u32( key.pair_c ) ) return false;
		if ( !r.read_u64( key.suffix_profile_id ) ) return false;
		key.domain = static_cast<ResidualAnalysisDomain>( domain );
		key.objective = static_cast<ResidualObjectiveKind>( objective );
		return true;
	}

	inline void write_residual_problem_record(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const ResidualProblemRecord& record )
	{
		write_residual_problem_key( w, record.key );
		w.write_u64( record.best_prefix_weight );
		w.write_u64( record.sequence_number );
		w.write_u64( record.discovery_run_nodes );
	}

	inline bool read_residual_problem_record(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		ResidualProblemRecord& record )
	{
		return
			read_residual_problem_key( r, record.key ) &&
			r.read_u64( record.best_prefix_weight ) &&
			r.read_u64( record.sequence_number ) &&
			r.read_u64( record.discovery_run_nodes );
	}

	inline void write_residual_result_record(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const ResidualResultRecord& record )
	{
		write_residual_problem_key( w, record.key );
		w.write_u64( record.best_weight );
		w.write_u64( record.collect_weight_cap );
		w.write_u8( record.solved ? 1u : 0u );
		w.write_u8( record.certified ? 1u : 0u );
		w.write_u8( record.exact_within_collect_weight_cap ? 1u : 0u );
	}

	inline bool read_residual_result_record(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		ResidualResultRecord& record )
	{
		std::uint8_t solved = 0;
		std::uint8_t certified = 0;
		std::uint8_t exact = 0;
		if ( !read_residual_problem_key( r, record.key ) ) return false;
		if ( !r.read_u64( record.best_weight ) ) return false;
		if ( !r.read_u64( record.collect_weight_cap ) ) return false;
		if ( !r.read_u8( solved ) ) return false;
		if ( !r.read_u8( certified ) ) return false;
		if ( !r.read_u8( exact ) ) return false;
		record.solved = ( solved != 0 );
		record.certified = ( certified != 0 );
		record.exact_within_collect_weight_cap = ( exact != 0 );
		return true;
	}

	inline void write_residual_counters(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const ResidualCounters& counters )
	{
		w.write_u64( counters.interrupted_source_input_pair_count );
		w.write_u64( counters.interrupted_output_as_next_input_pair_count );
		w.write_u64( counters.completed_source_input_pair_count );
		w.write_u64( counters.completed_output_as_next_input_pair_count );
		w.write_u64( counters.best_weight_improvement_count );
		w.write_u64( counters.repeated_or_dominated_residual_skip_count );
	}

	inline bool read_residual_counters(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		ResidualCounters& counters )
	{
		return
			r.read_u64( counters.interrupted_source_input_pair_count ) &&
			r.read_u64( counters.interrupted_output_as_next_input_pair_count ) &&
			r.read_u64( counters.completed_source_input_pair_count ) &&
			r.read_u64( counters.completed_output_as_next_input_pair_count ) &&
			r.read_u64( counters.best_weight_improvement_count ) &&
			r.read_u64( counters.repeated_or_dominated_residual_skip_count );
	}

	template <typename RecordT, typename WriteFn>
	inline void write_record_vector(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const std::vector<RecordT>& records,
		WriteFn&& write_fn )
	{
		w.write_u64( static_cast<std::uint64_t>( records.size() ) );
		for ( const auto& record : records )
			write_fn( w, record );
	}

	template <typename RecordT, typename ReadFn>
	inline bool read_record_vector(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		std::vector<RecordT>& records,
		ReadFn&& read_fn )
	{
		std::uint64_t size = 0;
		if ( !r.read_u64( size ) ) return false;
		records.clear();
		records.resize( static_cast<std::size_t>( size ) );
		for ( auto& record : records )
		{
			if ( !read_fn( r, record ) )
				return false;
		}
		return true;
	}

	inline void write_best_prefix_table(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const std::unordered_map<ResidualProblemKey, SearchWeight, ResidualProblemKeyHash>& table )
	{
		w.write_u64( static_cast<std::uint64_t>( table.size() ) );
		for ( const auto& entry : table )
		{
			write_residual_problem_key( w, entry.first );
			w.write_u64( entry.second );
		}
	}

	inline bool read_best_prefix_table(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		std::unordered_map<ResidualProblemKey, SearchWeight, ResidualProblemKeyHash>& table )
	{
		std::uint64_t size = 0;
		if ( !r.read_u64( size ) ) return false;
		table.clear();
		for ( std::uint64_t i = 0; i < size; ++i )
		{
			ResidualProblemKey key {};
			SearchWeight best_prefix_weight = INFINITE_WEIGHT;
			if ( !read_residual_problem_key( r, key ) ) return false;
			if ( !r.read_u64( best_prefix_weight ) ) return false;
			table.emplace( key, best_prefix_weight );
		}
		return true;
	}

	inline void write_completed_residual_set(
		TwilightDream::auto_search_checkpoint::BinaryWriter& w,
		const std::unordered_set<ResidualProblemKey, ResidualProblemKeyHash>& set )
	{
		w.write_u64( static_cast<std::uint64_t>( set.size() ) );
		for ( const auto& key : set )
			write_residual_problem_key( w, key );
	}

	inline bool read_completed_residual_set(
		TwilightDream::auto_search_checkpoint::BinaryReader& r,
		std::unordered_set<ResidualProblemKey, ResidualProblemKeyHash>& set )
	{
		std::uint64_t size = 0;
		if ( !r.read_u64( size ) ) return false;
		set.clear();
		for ( std::uint64_t i = 0; i < size; ++i )
		{
			ResidualProblemKey key {};
			if ( !read_residual_problem_key( r, key ) ) return false;
			set.emplace( key );
		}
		return true;
	}
}  // namespace TwilightDream::residual_frontier_shared
