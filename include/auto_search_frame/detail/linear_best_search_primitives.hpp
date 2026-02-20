#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "common/runtime_component.hpp"

namespace TwilightDream::auto_search_linear
{
	constexpr int INFINITE_WEIGHT = 1'000'000;

	enum class SearchMode : std::uint8_t
	{
		Fast = 0,
		Strict = 1
	};

	static inline const char* search_mode_to_string( SearchMode m ) noexcept
	{
		return ( m == SearchMode::Strict ) ? "strict" : "fast";
	}

	using TwilightDream::runtime_component::format_local_time_now;
	using TwilightDream::runtime_component::hex8;
	using TwilightDream::runtime_component::RuntimeEventLog;
	using TwilightDream::runtime_component::RuntimeInvocationState;
	using TwilightDream::runtime_component::append_timestamp_to_artifact_path;
	using TwilightDream::runtime_component::bytes_to_gibibytes;
	using TwilightDream::runtime_component::make_unique_timestamped_artifact_path;
	using TwilightDream::runtime_component::memory_gate_status_name;
	using TwilightDream::runtime_component::memory_governor_enable_for_run;
	using TwilightDream::runtime_component::memory_governor_disable_for_run;
	using TwilightDream::runtime_component::memory_governor_in_pressure;
	using TwilightDream::runtime_component::memory_governor_poll_if_needed;
	using TwilightDream::runtime_component::memory_governor_set_poll_fn;
	using TwilightDream::runtime_component::memory_governor_update_from_system_sample;
	using TwilightDream::runtime_component::physical_memory_allocation_guard_active;
	using TwilightDream::runtime_component::pmr_bounded_resource;
	using TwilightDream::runtime_component::pmr_configure_for_run;
	using TwilightDream::runtime_component::pmr_report_oom_once;
	using TwilightDream::runtime_component::pmr_suggest_limit_bytes;
	using TwilightDream::runtime_component::print_word32_hex;
	using TwilightDream::runtime_component::runtime_elapsed_microseconds;
	using TwilightDream::runtime_component::runtime_elapsed_seconds;
	using TwilightDream::runtime_component::runtime_budget_mode_name;
	using TwilightDream::runtime_component::runtime_effective_maximum_search_nodes;
	using TwilightDream::runtime_component::runtime_nodes_ignored_due_to_time_limit;

	using LinearBestSearchRuntimeControls = TwilightDream::runtime_component::SearchRuntimeControls;
	using TwilightDream::runtime_component::compute_initial_memo_reserve_hint;
	using TwilightDream::runtime_component::MemoryGateStatus;
	using TwilightDream::runtime_component::SearchInvocationMetadata;
	using TwilightDream::runtime_component::ScopedRuntimeTimeLimitProbe;
	using TwilightDream::runtime_component::begin_runtime_invocation;
	using TwilightDream::runtime_component::recommended_progress_node_mask_for_time_limit;
	using TwilightDream::runtime_component::runtime_maximum_search_nodes_hit;
	using TwilightDream::runtime_component::runtime_note_node_visit;
	using TwilightDream::runtime_component::runtime_poll;
	using TwilightDream::runtime_component::runtime_time_limit_hit;
	using TwilightDream::runtime_component::runtime_time_limit_reached;

	struct InjectionCorrelationTransition
	{
		// correlated input masks v form an affine subspace:
		//   v ∈ offset_mask ⊕ span(basis_vectors[0..rank-1])
		std::uint32_t				  offset_mask = 0;
		std::array<std::uint32_t, 32> basis_vectors {};
		int							  rank = 0;	   // rank(S(u))
		int							  weight = 0;  // ceil(rank/2) for weight_int semantics
	};

	struct AddCandidate
	{
		// Input masks for: s = x ⊞ y
		// - input_mask_x: mask on x
		// - input_mask_y: mask on y
		std::uint32_t input_mask_x = 0;
		std::uint32_t input_mask_y = 0;

		// Linear weight contribution of this modular addition (weight = -log2(|cor|)).
		// For Schulte-Geers (Proposition 1), this equals |z|.
		int linear_weight = 0;
	};

	struct SubConstCandidate
	{
		// Input mask on x for: y = x ⊟ C
		std::uint32_t input_mask_on_x = 0;

		// Linear weight contribution (exact, via carry transfer matrix operator).
		int linear_weight = 0;
	};

	struct SubConstBitMatrix
	{
		int m00 = 0;
		int m01 = 0;
		int m10 = 0;
		int m11 = 0;
		std::uint8_t max_row_sum = 0;
	};

	struct SubConstStreamingCursorFrame
	{
		int			 bit_index = 0;
		std::uint32_t prefix = 0;
		std::int64_t v0 = 1;
		std::int64_t v1 = 0;
		std::uint8_t state = 0;
	};

	struct SubConstStreamingCursor
	{
		bool initialized = false;
		bool stop_due_to_limits = false;
		std::uint32_t output_mask_beta = 0;
		std::uint32_t constant = 0;
		int			  weight_cap = 0;
		int			  nbits = 32;
		std::uint64_t min_abs = 1;
		std::array<SubConstBitMatrix, 32> mats_alpha0 {};
		std::array<SubConstBitMatrix, 32> mats_alpha1 {};
		std::array<std::uint64_t, 33> max_gain_suffix {};
		std::array<SubConstStreamingCursorFrame, 64> stack {};
		int										 stack_step = 0;
	};

	struct WeightSlicedClatStreamingCursorFrame
	{
		int			  bit_index = 0;
		std::uint32_t input_mask_x = 0;
		std::uint32_t input_mask_y = 0;
		int			  z_bit = 0;
		int			  z_weight_so_far = 0;
		std::uint8_t  branch_state = 0;
	};

	struct WeightSlicedClatStreamingCursor
	{
		bool initialized = false;
		bool stop_due_to_limits = false;
		std::uint32_t output_mask_u = 0;
		int			  weight_cap = 0;
		int			  next_target_weight = 0;
		int			  current_target_weight = -1;
		std::uint32_t input_mask_x_prefix = 0;
		std::uint32_t input_mask_y_prefix = 0;
		int			  z30 = 0;
		std::array<WeightSlicedClatStreamingCursorFrame, 64> stack {};
		int													stack_size = 0;
	};

	#ifndef AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR
	#define AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR 1
	#endif

	class AddVarVarSplit8Enumerator32
	{
	public:
		struct StreamingCursorFrame
		{
			int			 block_index = 0;
			int			 connection_in = 0;
			std::uint32_t input_mask_x_acc = 0;
			std::uint32_t input_mask_y_acc = 0;
			int			 remaining_weight = 0;
			std::size_t	 option_index = 0;
			int			 target_weight = 0;
		};

		struct StreamingCursor
		{
			bool initialized = false;
			bool stop_due_to_limits = false;
			std::uint32_t output_mask_u = 0;
			int			  weight_cap = 0;
			int			  next_target_weight = 0;
			std::array<std::uint8_t, 4> output_mask_bytes {};
			std::array<std::array<int, 2>, 5> min_remaining_weight {};
			std::array<StreamingCursorFrame, 8> stack {};
			int								  stack_size = 0;
		};

		static void reset_streaming_cursor( StreamingCursor& cursor, std::uint32_t output_mask_u, int weight_cap_requested );

		static bool next_streaming_candidate( StreamingCursor& cursor, AddCandidate& out_candidate );

		static const std::vector<AddCandidate>& get_candidates_for_output_mask_u(
			std::uint32_t output_mask_u,
			int weight_cap_requested,
			SearchMode search_mode,
			bool enable_weight_sliced_clat,
			std::size_t weight_sliced_clat_max_candidates );

	private:
		static constexpr bool kEnableSplit8Slr = ( AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR != 0 );
		static constexpr std::size_t kMaxCachedCandidates = 4096;  // cache only when candidate set is small
		static constexpr std::size_t kMaxCandidateCacheEntries = 256;
		static constexpr std::size_t kMaxBlockOptionCacheEntries = 512;

		struct Split8BlockOption
		{
			// Local (8-bit) input masks for: s = x ⊞ y, with fixed 8-bit output mask u_byte.
			std::uint8_t input_mask_x_byte = 0;
			std::uint8_t input_mask_y_byte = 0;
			// Connection status passed to the next (less significant) block.
			std::uint8_t next_connection_bit = 0;	// 0/1
			// Local weight contribution (sum of z bits inside this block).
			std::uint8_t block_weight = 0;  // 0..8
		};

		struct CandidateCache
		{
			std::unordered_map<std::uint32_t, std::vector<AddCandidate>> by_output_mask_u;
			std::array<std::vector<AddCandidate>, 4>					 scratch_ring {};
			std::uint32_t												 scratch_index = 0;
		};

		static const std::vector<Split8BlockOption>& get_split8_block_options_for_u_byte( std::uint8_t u_byte, int connection_bit_in, bool exclude_top_z31_weight );

		static std::vector<AddCandidate> generate_add_candidates_split8_slr( std::uint32_t output_mask_u, int weight_cap );
	};
}
