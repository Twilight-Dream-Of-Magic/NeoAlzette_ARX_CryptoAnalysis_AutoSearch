#pragma once


#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <memory>
#include <memory_resource>
#include <new>
#include <limits>
#include <optional>
#include <source_location>
#include <bit>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <ctime>

#include "auto_search_frame/detail/linear_best_search_primitives.hpp"
#include "neoalzette/neoalzette_core.hpp"
#include "injection_analysis/linear_rank.hpp"
#include "arx_analysis_operators/linear_correlation/constant_weight_evaluation.hpp"
// Keep the generic Wallen logn route nearby as a fallback / regression backend.
// The default fast path below still uses the Schulte-Geers / CCZ weight backend.
#include "arx_analysis_operators/linear_correlation/weight_evaluation.hpp"
#include "arx_analysis_operators/linear_correlation/weight_evaluation_ccz.hpp"

// self-test harness is compiled from test_arx_operator_self_test.cpp
int run_arx_operator_self_test();
int run_linear_search_self_test( std::uint64_t seed, std::size_t extra_cases );

namespace TwilightDream::auto_search_linear
{
	using ::TwilightDream::NeoAlzetteCore;

	struct LinearBestSearchConfiguration;

	struct AddCandidate
	{
		// Input masks for: s = x ⊞ y  (v, w in φ₂ notation when u is fixed on s).
		std::uint32_t input_mask_x = 0;
		std::uint32_t input_mask_y = 0;

		// Exact linear weight: w_lin(u,v,w) = |z| under Schulte–Geers Eq.(1); z is the shell label.
		// See `linear_best_search_math.hpp` (z-shell / weight-shell).
		SearchWeight linear_weight = 0;
	};

	/// Fixed (v,w) on the add inputs: exact / strict column-optimal output mask `u*` on the sum wire
	/// + exact `w_lin(u*,v,w)` (see `find_optimal_output_u_ccz`).
	struct VarVarAddColumnOptimalOnSumWire
	{
		std::uint32_t output_mask_u = 0;
		SearchWeight  linear_weight = 0;
	};

	struct SubConstCandidate
	{
		// Input mask on x for: y = x ⊟ C
		std::uint32_t input_mask_on_x = 0;

		// Linear weight contribution (exact, via carry transfer matrix operator).
		SearchWeight linear_weight = 0;
	};

	struct FixedAlphaSubConstCandidate
	{
		// Output mask on y for: y = x ⊟ C with input alpha on x fixed.
		std::uint32_t output_mask_beta = 0;

		// Linear weight contribution (exact, via carry transfer matrix operator).
		SearchWeight linear_weight = 0;
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
		SearchWeight  weight_cap = 0;
		int			  nbits = 32;
		std::uint64_t min_abs = 1;
		std::array<SubConstBitMatrix, 32> mats_alpha0 {};
		std::array<SubConstBitMatrix, 32> mats_alpha1 {};
		std::array<std::uint64_t, 33> max_gain_suffix {};
		std::array<SubConstStreamingCursorFrame, 64> stack {};
		int										 stack_step = 0;
		std::uint64_t emitted_candidate_count = 0;
		struct BestFirstNode
		{
			int			 bit_index = 0;
			std::uint32_t prefix = 0;
			TwilightDream::arx_operators::detail_varconst_carry_dp::DirectionKey dir {};
			TwilightDream::arx_operators::detail_varconst_carry_dp::uint128_t scale = 0;
			std::uint64_t optimistic_abs_weight_packed = 0;
			bool		 optimistic_abs_weight_is_exact_2pow64 = false;
			std::uint64_t optimistic_full_mask = 0;
		};
		std::array<TwilightDream::arx_operators::detail_varconst_carry_dp::BitMatrix, 64> ordered_mats_alpha0 {};
		std::array<TwilightDream::arx_operators::detail_varconst_carry_dp::BitMatrix, 64> ordered_mats_alpha1 {};
		TwilightDream::arx_operators::detail_varconst_carry_dp::ProjectiveBellmanMemo ordered_memo {};
		std::vector<BestFirstNode> heap {};
	};

	struct FixedAlphaSubConstStreamingCursor
	{
		bool initialized = false;
		bool stop_due_to_limits = false;
		std::uint32_t input_mask_alpha = 0;
		std::uint32_t constant = 0;
		SearchWeight  weight_cap = 0;
		int			  nbits = 32;
		std::uint64_t min_abs = 1;
		std::array<SubConstBitMatrix, 32> mats_beta0 {};
		std::array<SubConstBitMatrix, 32> mats_beta1 {};
		std::array<std::uint64_t, 33> max_gain_suffix {};
		std::array<SubConstStreamingCursorFrame, 64> stack {};
		int										 stack_step = 0;
		std::uint64_t emitted_candidate_count = 0;
		struct BestFirstNode
		{
			int			 bit_index = 0;
			std::uint32_t prefix = 0;
			TwilightDream::arx_operators::detail_varconst_carry_dp::DirectionKey dir {};
			TwilightDream::arx_operators::detail_varconst_carry_dp::uint128_t scale = 0;
			std::uint64_t optimistic_abs_weight_packed = 0;
			bool		 optimistic_abs_weight_is_exact_2pow64 = false;
			std::uint64_t optimistic_full_mask = 0;
		};
		std::array<TwilightDream::arx_operators::detail_varconst_carry_dp::BitMatrix, 64> ordered_mats_beta0 {};
		std::array<TwilightDream::arx_operators::detail_varconst_carry_dp::BitMatrix, 64> ordered_mats_beta1 {};
		TwilightDream::arx_operators::detail_varconst_carry_dp::ProjectiveBellmanMemo ordered_memo {};
		std::vector<BestFirstNode> heap {};
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
			SearchWeight remaining_weight = 0;
			std::size_t	 option_index = 0;
			SearchWeight target_weight = 0;
		};

		struct StreamingCursor
		{
			bool initialized = false;
			bool stop_due_to_limits = false;
			std::uint32_t output_mask_u = 0;
			SearchWeight  weight_cap = 0;
			SearchWeight  next_target_weight = 0;
			std::array<std::uint8_t, 4> output_mask_bytes {};
			std::array<std::array<SearchWeight, 2>, 5> min_remaining_weight {};
			std::array<StreamingCursorFrame, 8> stack {};
			int								  stack_size = 0;
		};

		static void reset_streaming_cursor( StreamingCursor& cursor, std::uint32_t output_mask_u, SearchWeight weight_cap_requested );

		static bool next_streaming_candidate( StreamingCursor& cursor, AddCandidate& out_candidate );

		static const std::vector<AddCandidate>& get_candidates_for_output_mask_u(
			std::uint32_t output_mask_u,
			SearchWeight weight_cap_requested,
			SearchMode search_mode,
			bool enable_weight_sliced_clat,
			std::size_t weight_sliced_clat_max_candidates );

		/// Same z-shell as `generate_add_candidates_for_fixed_u` (Schulte–Geers); exposed for audits.
		static std::vector<AddCandidate> generate_add_candidates_split8_slr( std::uint32_t output_mask_u, SearchWeight weight_cap );

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

		static const std::vector<Split8BlockOption>& get_split8_block_options_for_u_byte(
			std::uint8_t u_byte,
			int connection_bit_in,
			bool exclude_top_z31_weight );
	};

	/// Weight-sliced cLAT streaming uses the same split-8 SLR cursor (no separate 31-bit z DFS).
	using WeightSlicedClatStreamingCursor = AddVarVarSplit8Enumerator32::StreamingCursor;

	extern std::atomic<bool> g_disable_linear_tls_caches;

	// ============================================================================
	// Mask propagation for deterministic linear transport used in the cipher
	//
	// We traverse the cipher in reverse (output mask -> input mask).
	// For y = L(x), we need x_mask = L^T(y_mask).
	//
	// In the current V6 core there are no standalone outer l1/l2 wrapper layers anymore.
	// The relevant linear transport here is the fixed cross-branch XOR/rotation bridge, and
	// its transpose is obtained by flipping rotl <-> rotr with the same rotation constants.
	// ============================================================================

	/** Transpose transformations for linear cryptanalysis **/
	// Transpose: replace every rotl with rotr.
	// For the dynamic diffusion masks M0 / M1 used inside the injection gadget,
	// transpose is realized by reversing the rotation orientation, i.e.
	// NeoAlzetteCore::generate_dynamic_diffusion_mask0_transpose() /
	// NeoAlzetteCore::generate_dynamic_diffusion_mask1_transpose().

	// ============================================================================
	// Local cost evaluation helpers
	// ============================================================================


	// Exact Q1 for the var-const subtraction operator used by the linear search frame.
	// Q2 may organize fixed-beta / fixed-alpha candidates, shells, or floors, but the final
	// local score of a concrete `(alpha, C, beta)` tuple is this exact evaluator.
	// ============================================================================
	// z-shell / weight-shell (var-var modular add, Schulte–Geers φ₂)
	// ============================================================================
	//
	// **Object.**  The nontrivial quantity for linear correlation under φ₂ is the auxiliary bit vector z from
	// Liu/Wang/Rijmen ACNS 2016 Eq.(1) (Schulte–Geers explicit constraints).  When the constraints hold,
	//   |cor(u,v,w)| = 2^{-|z|},  hence  w_lin(u,v,w) = |z|.
	// So the **shell index** for a feasible triple is t = |z|, not an ad-hoc score on masks.
	//
	// **Weight sets.**  Write  Z_t = { z ∈ {0,1}^n | z_{n-1}=0, |z| = t }.
	//
	// **BnB polarity (see `linear_varvar_modular_add_bnb_mode.hpp`).**  NeoAlzette linear search now supports
	// both exact polarities:
	// - **fixed u → enumerate (v,w)** (z-shell L_t(u)) on the row-max side;
	// - **fixed (v,w) → u** / column-`u*`, where each fixed `(v,w)` takes the strict column maximum via
	//   `find_optimal_output_u_ccz` and then exact CCZ rescoring.
	// The resumable Matsui best-search cursor and the hull collector both honor the selected polarity.
	//
	// **Fixed-output formulation (NeoAlzette reverse search).**  The trail fixes the output mask u on the sum
	// wire s = x ⊞ y.  The **exact** weight-t shell is
	//   L_t(u) = { (v,w) | ∃ z, Φ(u,v,w,z) ∧ |z| = t },
	// with Φ the Schulte–Geers constraints.  Enumerating t = 0,1,…,T in order and, for each t, **all** pairs in
	// L_t(u), is **strictly complete** for all nonzero linear contributions with w_lin ≤ T at this add node.
	//
	// **Fixed-input variant (same Φ).**  If instead one fixes v and varies (u,w), the same definition with L_t(v)
	// is the complete shell; the implementation below follows the **fixed u** convention used by the engine.
	//
	// **Branching geometry.**  Where z_i = 0, Eq.(1) forces v_i = w_i = u_i (no branch).  Only positions with
	// z_i = 1 admit nontrivial (v_i,w_i); i.e. **supp(z)** is the combinatorial freedom — not “all bits naive DFS”.
	//
	// **Accelerators (must not replace mathematics).**
	// - **Weight-sliced cLAT** + **split-8 SLR** (`AddVarVarSplit8Enumerator32`): same z-shell set as bit-wise
	//   construction; they are **index / block-decomposition** layers (Huang–Wang style) to reach L_t(u) faster.
	// - **Gap A** (`modular_addition_ccz.hpp`): closed-form φ₂ weight / row–column tools; streaming emission calls
	//   `linear_correlation_add_ccz_weight` (and column u* where relevant) so rescoring stays exact.
	// A lookup miss or block-cache miss means “no **cached** fragment”, **not** “L_t(u) is empty” and **not**
	// “correlation is zero” unless Φ is actually infeasible.
	//
	// **Engineering caps** (`addition_weight_cap`, `weight_sliced_clat_max_candidates`, `max_candidates` on
	// `generate_add_candidates_for_fixed_u`, etc.) **truncate** exploration; they are **not** part of the φ₂
	// model.  Any “strict certification” that claims full shell coverage must set these to non-limiting values
	// (or record that a cap was hit).
	//
	// ============================================================================
	// Candidate generation (exact, deterministic; z-shell order by |z|)
	// ============================================================================

	/**
	 * @brief Enumerate feasible input masks for modular addition (var-var).
	 *
	 * We analyze the linear approximation of 32-bit modular addition:
	 *   s = x ⊞ y   (addition modulo 2^32)
	 *
	 * Notation (consistent with the paper and this project):
	 * - u: output mask on s
	 * - v: input mask on x
	 * - w: input mask on y
	 * - bit i: i=0 is LSB, i=31 is MSB
	 *
	 * Paper reference:
	 *   Liu/Wang/Rijmen, "Automatic Search of Linear Trails in ARX with Applications to SPECK and Chaskey"
	 *   (ACNS 2016) — Section 3.1, Proposition 1 / Eq.(1) (Schulte-Geers explicit formula)
	 *
	 * Schulte-Geers explicit constraints (Eq.(1)):
	 *   z_{n-1} = 0
	 *   z_{n-2} = u_{n-1} ⊕ v_{n-1} ⊕ w_{n-1}
	 *   z_j     = z_{j+1} ⊕ u_{j+1} ⊕ v_{j+1} ⊕ w_{j+1}     (0 ≤ j ≤ n-3)
	 *   z_i ≥ u_i ⊕ v_i,    z_i ≥ u_i ⊕ w_i                 (0 ≤ i ≤ n-1)
	 *
	 * Correlation magnitude (Proposition 1):
	 *   |cor(u,v,w)| = 2^{-|z|}    if the constraints hold, else 0
	 * Therefore the linear weight is:
	 *   weight = -log2(|cor|) = |z|
	 *
	 * Engineering interpretation of the inequalities:
	 * - If z_i == 0, then u_i ⊕ v_i must be 0 AND u_i ⊕ w_i must be 0  =>  v_i = w_i = u_i.
	 * - If z_i == 1, the inequalities impose no restriction on (v_i, w_i) (4 choices).
	 *
	 * This function enumerates candidate pairs (v,w) in nondecreasing weight |z| and returns up to
	 * max_candidates results (0 means "no cap"). If out_hit_cap is non-null, it is set to true when
	 * enumeration stops early due to max_candidates. Only the absolute correlation is used here; the
	 * sign term from Proposition 1 is irrelevant for weight-based best-trail search.
	 *
	 * **Complexity (strict).** Let N be the number of feasible (v,w) in the cap. Any algorithm that
	 * **materializes** the full multiset must spend Ω(N) time and Ω(N) output space — there is no
	 * O(log n) **total** bound for complete enumeration. What we optimize is the **per-shell
	 * construction**: default builds use `AddVarVarSplit8Enumerator32::generate_add_candidates_split8_slr`
	 * (split-8 SLR + TLS-cached 8-bit option tables + DP pruning + CCZ finalize), i.e. **O(1) word
	 * blocks** for n=32 instead of a 31-level bit DFS. Set `AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR`
	 * to 0 for the reference bit-wise stack DFS (audits / regressions).
	 */
	static inline std::vector<AddCandidate> generate_add_candidates_for_fixed_u(
		std::uint32_t output_mask_u,
		SearchWeight weight_cap,
		std::size_t max_candidates = 0,
		bool* out_hit_cap = nullptr )
	{
		std::vector<AddCandidate> candidates;
		if ( out_hit_cap )
			*out_hit_cap = false;
		weight_cap = std::min<SearchWeight>( weight_cap, 31 );
	
	#if AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR
		// Preserve the existing full-enumeration fast path unchanged.
		// The strict complexity win is for the capped case.
		if ( max_candidates == 0 )
		{
			candidates = AddVarVarSplit8Enumerator32::generate_add_candidates_split8_slr(
				output_mask_u, weight_cap );
	
			if ( runtime_time_limit_reached() )
			{
				candidates.clear();
				return candidates;
			}
			return candidates;
		}
	#endif
	
		// Exact capped enumerator:
		// We keep the Schulte-Geers / Liu-Wang-Rijmen feasibility model unchanged,
		// but replace repeated bitwise DFS by an exact suffix-count DP + shell-ordered enumeration.
		//
		// DP meaning:
		//   count_dp[bit_index][z_i][target_weight]
		// = number of exact completions for bits [bit_index .. 0],
		//   given current state z_i = z_i,
		//   whose total weight on these bits is exactly target_weight.
		//
		// Recurrence (exact):
		//   If z_i = 0:
		//     v_i = w_i = u_i, z_{i-1} = u_i, and current bit contributes 0 weight.
		//   If z_i = 1:
		//     current bit contributes 1 weight, and for each next state z_{i-1} in {0,1},
		//     there are exactly 2 local pairs (v_i,w_i) achieving it.
		//
		// For fixed 32-bit width and capped output K = max_candidates:
		//   DP build     : O(31 * weight_cap)
		//   Enumeration  : O(31 * K)
		// hence effectively O(weight_cap + K) up to a fixed machine-word constant.
	
		const std::size_t count_cap = ( max_candidates != 0 ) ? max_candidates : std::size_t( -1 );
	
		auto saturating_add_size_t = [ & ]( std::size_t a, std::size_t b ) noexcept -> std::size_t {
			if ( a >= count_cap )
				return count_cap;
			if ( b >= count_cap - a )
				return count_cap;
			return a + b;
		};
	
		auto saturating_mul2_size_t = [ & ]( std::size_t a ) noexcept -> std::size_t {
			if ( a >= count_cap )
				return count_cap;
			if ( a > count_cap / 2 )
				return count_cap;
			return a * 2;
		};
	
		// count_dp[bit_index][z_i][target_weight]
		std::size_t count_dp[31][2][32] = {};
	
		// Base layer: bit 0
		//
		// z_0 = 0:
		//   forced (v_0, w_0) = (u_0, u_0), exact shell weight = 0
		//
		// z_0 = 1:
		//   all 4 pairs are allowed, exact shell weight = 1
		count_dp[0][0][0] = 1;
		count_dp[0][1][1] = 4;
	
		for ( int bit_index = 1; bit_index <= 30; ++bit_index )
		{
			const int u_i = int( ( output_mask_u >> bit_index ) & 1u );
	
			for ( SearchWeight target_weight = 0; target_weight <= weight_cap; ++target_weight )
			{
				// z_i = 0 => forced next-state z_{i-1} = u_i, no extra weight
				count_dp[bit_index][0][ std::size_t( target_weight ) ] =
					count_dp[bit_index - 1][u_i][ std::size_t( target_weight ) ];
	
				// z_i = 1 => +1 weight, and 2 local pairs for each next-state
				std::size_t total = 0;
				if ( target_weight >= 1 )
				{
					const std::size_t from_z0 =
						saturating_mul2_size_t( count_dp[bit_index - 1][0][ std::size_t( target_weight - 1 ) ] );
					const std::size_t from_z1 =
						saturating_mul2_size_t( count_dp[bit_index - 1][1][ std::size_t( target_weight - 1 ) ] );
					total = saturating_add_size_t( from_z0, from_z1 );
				}
				count_dp[bit_index][1][ std::size_t( target_weight ) ] = total;
			}
		}
	
		const int         u31 = int( ( output_mask_u >> 31 ) & 1u );
		const std::uint32_t input_mask_x_prefix = u31 ? ( 1u << 31 ) : 0u;
		const std::uint32_t input_mask_y_prefix = input_mask_x_prefix;
		const int         z30 = u31;
	
		bool stop_due_to_cap = false;
		bool stop_due_to_memory_guard = false;
		bool stop_due_to_time_limit = false;
	
		auto emit_candidate = [ & ]( std::uint32_t input_mask_x,
									 std::uint32_t input_mask_y,
									 SearchWeight weight ) -> bool
		{
			if ( physical_memory_allocation_guard_active() )
			{
				stop_due_to_memory_guard = true;
				return false;
			}
	
			candidates.push_back( AddCandidate { input_mask_x, input_mask_y, weight } );
	
			if ( max_candidates != 0 && candidates.size() >= max_candidates )
			{
				if ( out_hit_cap )
					*out_hit_cap = true;
				stop_due_to_cap = true;
				return false;
			}
	
			return true;
		};
	
		auto enumerate_exact_shell =
			[ & ]( auto&& self,
				   int bit_index,
				   int z_i,
				   SearchWeight remaining_weight,
				   std::uint32_t input_mask_x,
				   std::uint32_t input_mask_y,
				   SearchWeight target_weight ) -> void
		{
			if ( stop_due_to_cap || stop_due_to_memory_guard || stop_due_to_time_limit )
				return;
	
			if ( runtime_time_limit_reached() )
			{
				stop_due_to_time_limit = true;
				return;
			}
	
			if ( bit_index == 0 )
			{
				const int u_0 = int( output_mask_u & 1u );
	
				if ( z_i == 0 )
				{
					if ( remaining_weight != 0 )
						return;
	
					const std::uint32_t next_input_mask_x =
						input_mask_x | ( std::uint32_t( u_0 ) << 0 );
					const std::uint32_t next_input_mask_y =
						input_mask_y | ( std::uint32_t( u_0 ) << 0 );
	
					(void) emit_candidate( next_input_mask_x, next_input_mask_y, target_weight );
					return;
				}
	
				// z_0 == 1: exact terminal shell weight is 1, and all 4 pairs are feasible.
				if ( remaining_weight != 1 )
					return;
	
				// Keep terminal order identical to the original bit_index==0 branch:
				// (0,0), (0,1), (1,0), (1,1)
				const int terminal_pairs[4][2] = {
					{ 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }
				};
	
				for ( int k = 0; k < 4; ++k )
				{
					const int v_0 = terminal_pairs[k][0];
					const int w_0 = terminal_pairs[k][1];
	
					const std::uint32_t next_input_mask_x =
						input_mask_x | ( std::uint32_t( v_0 ) << 0 );
					const std::uint32_t next_input_mask_y =
						input_mask_y | ( std::uint32_t( w_0 ) << 0 );
	
					if ( !emit_candidate( next_input_mask_x, next_input_mask_y, target_weight ) )
						return;
				}
				return;
			}
	
			const int u_i = int( ( output_mask_u >> bit_index ) & 1u );
	
			if ( z_i == 0 )
			{
				// Forced branch:
				//   v_i = w_i = u_i
				//   z_{i-1} = u_i
				//   no extra weight on this bit
				if ( count_dp[bit_index - 1][u_i][ std::size_t( remaining_weight ) ] == 0 )
					return;
	
				const std::uint32_t next_input_mask_x =
					input_mask_x | ( std::uint32_t( u_i ) << bit_index );
				const std::uint32_t next_input_mask_y =
					input_mask_y | ( std::uint32_t( u_i ) << bit_index );
	
				self( self,
					  bit_index - 1,
					  u_i,
					  remaining_weight,
					  next_input_mask_x,
					  next_input_mask_y,
					  target_weight );
				return;
			}
	
			// z_i == 1:
			//   z_{i-1} = 1 ^ u_i ^ v_i ^ w_i
			// We preserve the effective intra-shell traversal order of the original stack DFS.
			// Original push order: (0,0), (0,1), (1,0), (1,1)
			// Since it used LIFO stack, actual visit order was reversed:
			//   (1,1), (1,0), (0,1), (0,0)
			const int local_pairs[4][2] = {
				{ 1, 1 }, { 1, 0 }, { 0, 1 }, { 0, 0 }
			};
	
			for ( int k = 0; k < 4; ++k )
			{
				const int v_i = local_pairs[k][0];
				const int w_i = local_pairs[k][1];
				const int z_prev = 1 ^ u_i ^ v_i ^ w_i;
	
				if ( count_dp[bit_index - 1][z_prev][ std::size_t( remaining_weight - 1 ) ] == 0 )
					continue;
	
				const std::uint32_t next_input_mask_x =
					input_mask_x | ( std::uint32_t( v_i ) << bit_index );
				const std::uint32_t next_input_mask_y =
					input_mask_y | ( std::uint32_t( w_i ) << bit_index );
	
				self( self,
					  bit_index - 1,
					  z_prev,
					  remaining_weight - 1,
					  next_input_mask_x,
					  next_input_mask_y,
					  target_weight );
	
				if ( stop_due_to_cap || stop_due_to_memory_guard || stop_due_to_time_limit )
					return;
			}
		};
	
		for ( SearchWeight target_weight = 0; target_weight <= weight_cap; ++target_weight )
		{
			if ( stop_due_to_cap || stop_due_to_memory_guard || stop_due_to_time_limit )
				break;
	
			if ( count_dp[30][z30][ std::size_t( target_weight ) ] == 0 )
				continue;
	
			enumerate_exact_shell(
				enumerate_exact_shell,
				30,
				z30,
				target_weight,
				input_mask_x_prefix,
				input_mask_y_prefix,
				target_weight );
		}
	
		if ( stop_due_to_memory_guard || stop_due_to_time_limit )
		{
			candidates.clear();
			return candidates;
		}
	
		return candidates;
	}

	/**
	 * @brief Theorem 6 行最大单点，包装为搜索框架 `AddCandidate`。数学与实现见 `linear_correlation_add_phi2_row_max`。
	 */
	/**
	 * @brief **Fixed (v,w) → u\***（`find_optimal_output_u_ccz` + 精确权重）：若 w_lin > weight_cap 则无解。
	 *        这是 Gap A 的 exact / strict local column-max root operator，供
	 *        `FixedInputVW_ColumnOptimalOutputU` BnB 接线；当前由 collector 与 best-search engine 共同调用。
	 */

	// Mode adapter for the var-var modular-add local operator.
	//
	// Responsibility boundary:
	// - This function chooses the active Q2 polarity for the current BnB node object.
	// - For fixed-u mode, `candidate` already comes from the exact z-shell stream and carries
	//   its exact local Q1 weight after CCZ finalization.
	// - For fixed-(v,w) mode, this function switches to the exact column-optimal root oracle
	//   `(v,w) -> u*` and returns that strict local optimum with its exact weight.
	// - It does not decide global pruning, remaining-round bounds, or whether a caller may
	//   `break` vs `continue`; those stay in the engine / collector and must follow the
	//   ordered-stream contract of the chosen Q2 backend.
	// Fixed-beta theorem-facing root operator now lives in
	// `linear_varconst/fixed_beta_theorem.hpp`.
	//
	// This file keeps the strict witness / ordered candidate enumeration layer that
	// the search frame uses when collapsed single-witness semantics are not enough.

	// Weight-sliced cLAT **is** this split-8 SLR cursor type: mandatory fast path for the same z-shell as the
	// reference enumerator — cLAT/SLR is an **index** on (u_byte, connection) blocks, not a separate heuristic
	// judge.  Emission uses Gap A CCZ rescoring in linear_best_search_math.cpp.

	void reset_weight_sliced_clat_streaming_cursor(
		WeightSlicedClatStreamingCursor& cursor,
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested );

	bool next_weight_sliced_clat_streaming_candidate(
		WeightSlicedClatStreamingCursor& cursor,
		AddCandidate& out_candidate );

	// Contract-aware materialization helper for the var-var row-side Q2 candidate source.
	// All materialized add-candidate call sites in the search frame should go through this
	// helper instead of reading `get_candidates_for_output_mask_u()` directly.
	//
	// Semantics:
	// - `weight_sliced_clat_max_candidates == 0`: use the active exact shell index / cache path.
	// - `weight_sliced_clat_max_candidates != 0`: materialized helper list may be truncated.
	// - If a strict run somehow reaches this helper with a truncated z-shell helper config,
	//   we immediately fall back to the exact split-8 SLR materialization path instead of
	//   silently consuming a non-strict candidate list.
	void materialize_varvar_row_q2_candidates_for_output_mask_u(
		std::vector<AddCandidate>& out_candidates,
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested,
		const LinearBestSearchConfiguration& configuration );

	// ============================================================================
	// Split-Lookup-Recombine (SLR) + Weight-Sliced cLAT: **index** for z-shells (not a substitute Φ).
	//
	// Mathematical basis:
	//   Schulte–Geers: z from Eq.(1), |Cor| = 2^{-|z|}, w_lin = |z|.  Shell L_t(u) is the set of (v,w) with
	//   feasible Φ and |z| = t (see z-shell block above).
	//
	// Huang/Wang 2019 lineage: split / lookup / recombine accelerates **enumerating** mask pairs consistent
	// with a fixed correlation weight — here that weight is exactly |z|.
	//
	// Role in **strict** mode:
	// - cLAT / per-byte option tables are **only** faster ways to walk the same L_t(u); a “miss” is a cache
	//   miss on a block fragment, **not** evidence that L_t(u) is empty or that φ₂ = 0.
	// - Final weights for search are still exact Schulte–Geers / CCZ (`finalize_varvar_add_candidate_ccz`).
	//
	// This is not a full classical giant cLAT table; it is a compact split/lookup/recombine-8 realization of the z-shell
	// generator, shared with the Weight-Sliced cLAT streaming cursor type.
	// ============================================================================

	// ----------------------------------------------------------------------------
	// Exact sub-const enumeration (strict mode) — var-const subtraction in the SEARCH FRAME
	// ----------------------------------------------------------------------------
	//
	// Mathematical object (same Wallén / carry 2×2 chain as `linear_correlation_addconst.hpp`):
	//   y = x ⊟ C   (mod 2^nbits)
	// with **output mask beta on y fixed** and **input mask alpha on x enumerated**.
	//
	// This is the **transpose** of Gap B in
	// `arx_analysis_operators/linear_correlation/constant_optimal_alpha.hpp`, which fixes
	// (alpha, constant) and optimizes **beta***. There is **no** automatic substitution:
	// replacing one direction by the other requires a **proved reduction** between the two
	// search problems — see `LINEAR-PLAN.md` (M1 / Round 47).
	//
	// Gap B **column** on fixed output mask:
	// `find_optimal_alpha_varconst_mod_add` /
	// `find_optimal_alpha_varconst_mod_sub` in
	// `include/arx_analysis_operators/linear_correlation/constant_optimal_alpha.hpp`.
	// A concrete chained-two-stage counterexample now lives in
	// `research_gap_b_two_stage_subconst_reduction_audit.cpp`: exact local
	// `fixed beta -> alpha*` can still lose the global optimum once that chosen alpha
	// is fed into the next upstream var-const stage.
	// That is the operator-layer substitute for “scan every alpha”; it does **not** by itself
	// reproduce multi-candidate weight-cap enumeration — `LINEAR-PLAN.md`, Round 48.
	//
	// Implementation (`src/auto_search_frame/linear_best_search_math.cpp`):
	// - `reset_subconst_streaming_cursor` / `next_subconst_streaming_candidate` now use the
	//   fixed-beta projective Bellman witness as an exact **best-first ordered generator**:
	//   each queue node stores a prefix plus the Bellman-validated best completion bound, so
	//   the active strict path no longer walks a raw alpha-bit DFS stack.
	// - This keeps strict enumeration ordered by exact local weight while using the same fixed-beta
	//   theorem object as the root optimal-alpha operator.
	// - Strict mode may call `generate_subconst_candidates_for_fixed_beta_exact` and materialize
	//   the full candidate list (memory-heavy).
	//
	// NeoAlzette linear hull reverse search hits this path **twice per round** in the documented
	// wiring (e.g. A1 = A0 ⊟ RC[1], B3 = B2 ⊟ RC[6]), nested inside the outer trail DFS in
	// `linear_best_search_engine.cpp`. The remaining strict cost is now output-sensitive in the
	// number of surviving var-const candidates under the cap, rather than tied to replaying a raw
	// inner alpha DFS tree bit-by-bit.
	// ----------------------------------------------------------------------------

	static inline std::uint64_t abs_i64_to_u64( std::int64_t v ) noexcept
	{
		return ( v < 0 ) ? std::uint64_t( -v ) : std::uint64_t( v );
	}

	static inline int floor_log2_u64( std::uint64_t v ) noexcept
	{
		return v ? ( static_cast<int>( std::bit_width( v ) ) - 1 ) : -1;
	}

	// Compatibility forwards for the older generic subconst names.
	// The formal fixed-beta strict witness layer now lives in
	// `linear_varconst/fixed_beta_strict_witness.hpp`.
	void rebuild_subconst_streaming_cursor_runtime_state( SubConstStreamingCursor& cursor );

	void restore_subconst_streaming_cursor_heap_snapshot(
		SubConstStreamingCursor& cursor,
		const std::vector<SubConstCandidate>& heap_snapshot );

	void reset_subconst_streaming_cursor( SubConstStreamingCursor& cursor, std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap, int nbits = 32 );

	bool next_subconst_streaming_candidate( SubConstStreamingCursor& cursor, SubConstCandidate& out_candidate );

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta_exact( std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap, int nbits = 32 );

	/**
	 * @brief Exact enumeration of input masks for a fixed var-const subtraction mask.
	 *
	 * We analyze:
	 *   y = x ⊟ C   (mod 2^32)
	 * with:
	 *   beta  = output mask on y (given),
	 *   alpha = input  mask on x (to be enumerated).
	 *
	 * Weight is computed exactly from the integer carry-state transfer model; enumeration is strict
	 * and ordered by nondecreasing weight. No candidate cap is applied.
	 */
	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta( std::uint32_t output_mask_beta, std::uint32_t constant, SearchWeight weight_cap );

	// ============================================================================
	// One-round reverse transition enumeration for NeoAlzette (linear trails)
	// ============================================================================
	//
	// We search for linear trails (mask propagation) through one "round" of the NeoAlzette core.
	// This file traverses the cipher BACKWARDS (ciphertext-side masks -> plaintext-side masks).
	//
	// -----------------------------------------------------------------------------
	// Forward round structure (encryption direction) — simplified view
	// -----------------------------------------------------------------------------
	//
	// Let (A0,B0) be the round input (two 32-bit branches). One round in `NeoAlzetteCore::forward()`
	// can be summarized (naming matches the code in src/neoalzette/neoalzette_core.cpp):
	//
	//   // First subround (near round input):
	//   B1 = B0 ⊞ (rotl(A0,31) ⊕ rotl(A0,17) ⊕ RC[0])        (var-var addition; weight from |z|)
	//   A1 = A0 ⊟ RC[1]                                       (var-const subtraction; exact weight)
	//   A2 = A1 ⊕ rotl(B1, R0)                                (linear, weight 0)
	//   B2 = B1 ⊕ rotl(A2, R1)                                (linear, weight 0)
	//   A3 = A2 ⊕ f_B(B2) ⊕ RC[4]                             (injection, quadratic; weight = rank/2)
	//
	//   // Second subround (near round output):
	//   A4 = A3 ⊞ (rotl(B2,31) ⊕ rotl(B2,17) ⊕ RC[5])        (var-var addition; weight from |z|)
	//   B3 = B2 ⊟ RC[6]                                       (var-const subtraction; exact weight)
	//   B4 = B3 ⊕ rotl(A4, R0)                                (linear, weight 0)
	//   A5 = A4 ⊕ rotl(B4, R1)                                (linear, weight 0)
	//   B5 = B4 ⊕ f_A(A5) ⊕ RC[9]                             (injection, quadratic; weight = rank/2)
	//   A_out = A5 ⊕ RC[10],  B_out = B5 ⊕ RC[11]             (XOR with constants; weight 0)
	//
	// Notes about constants (engineering reality):
	// - XOR with a constant does NOT change |correlation|, it only flips the sign. Since this best-search
	//   optimizes weight = -log2(|cor|), we ignore those XOR constants in the reverse propagation.
	// - The constants in "termA/termB" appear as XOR on the second operand before addition; that also only
	//   flips correlation sign by (-1)^{<w,const>} and does not change weight, so it is ignored here too.
	//
	// Notes about injection modeling:
	// - For a fixed output mask u on the injected XOR term, the set of *correlated* input masks is an
	//   affine subspace  v ∈ l(u) ⊕ im(S(u))  (see injection section above). We enumerate masks in that
	//   affine subspace (capped by search_configuration.maximum_injection_input_masks) and charge weight = rank(S(u))/2.


}  // namespace TwilightDream::auto_search_linear
