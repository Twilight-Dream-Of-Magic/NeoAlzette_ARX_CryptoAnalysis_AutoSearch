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
#include <source_location>
#include <bit>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <ctime>

#include "auto_search_frame/detail/differential_best_search_primitives.hpp"
#include "neoalzette/neoalzette_core.hpp"
#include "injection_analysis/differential_rank.hpp"
#include "arx_analysis_operators/differential_probability/weight_evaluation.hpp"
#include "arx_analysis_operators/differential_probability/optimal_gamma.hpp"
#include "arx_analysis_operators/differential_probability/constant_weight_evaluation.hpp"

using TwilightDream::arx_operators::diff_subconst_exact_weight_ceil_int;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::xdp_add_lm2001;
using TwilightDream::arx_operators::xdp_add_lm2001_n;

// Implemented in `test_arx_operator_self_test.cpp` (linked into this executable).
int run_arx_operator_self_test();
int run_differential_search_self_test( std::uint64_t seed, std::size_t extra_cases );

namespace TwilightDream::auto_search_differential
{
	struct DifferentialBestSearchConfiguration;
	struct ModularAdditionEnumerator;

	using ::TwilightDream::NeoAlzetteCore;

	struct DifferentialTrailStepRecord;
	struct DifferentialBestSearchConfiguration;

	static inline double weight_to_probability( SearchWeight weight )
	{
		return std::pow( 2.0, -double( weight ) );
	}

	static inline std::uint64_t pack_two_word32_differences( std::uint32_t first_difference, std::uint32_t second_difference )
	{
		return ( std::uint64_t( first_difference ) << 32 ) | std::uint64_t( second_difference );
	}

	static inline int floor_log2_uint64( std::uint64_t value )
	{
		// C++20 portable floor(log2(value)), returns -1 for value==0.
		return value ? ( static_cast<int>( std::bit_width( value ) ) - 1 ) : -1;
	}

	bool rebuild_modular_addition_enumerator_shell_cache(
		const DifferentialBestSearchConfiguration& configuration,
		ModularAdditionEnumerator& enumerator );

	// ============================================================================
	// Exact carry-DP helpers for y = x - constant (mod 2^32) under XOR differences.
	// These helpers are shared by the resumable sub-const enumerator.
	// ============================================================================

	std::array<std::uint64_t, 4> compute_next_prefix_counts_for_addition_by_constant_at_bit(
		const std::array<std::uint64_t, 4>& prefix_counts_by_carry_pair_state,
		std::uint32_t input_difference_bit,
		std::uint32_t additive_constant_bit,
		std::uint32_t output_difference_bit );

	std::uint32_t compute_greedy_output_difference_for_addition_by_constant(
		std::uint32_t input_difference,
		std::uint32_t additive_constant );

	SearchWeight compute_greedy_upper_bound_weight(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference );

	std::vector<DifferentialTrailStepRecord> construct_greedy_initial_differential_trail(
		const DifferentialBestSearchConfiguration& search_configuration,
		std::uint32_t initial_branch_a_difference,
		std::uint32_t initial_branch_b_difference,
		SearchWeight& output_total_weight );

	// ============================================================================
	// Matsui / best-first (round-level BnB + bit-level recursion)
	// ----------------------------------------------------------------------------
	// Notation (32-bit words):
	//   - ⊞ : addition modulo 2^32        (x ⊞ y) = (x + y) mod 2^32
	//   - ⊟ : subtraction modulo 2^32     (x ⊟ c) = (x - c) mod 2^32
	//   - ⊕ : bitwise XOR
	//   - ROTL_r(x), ROTR_r(x): rotations
	//   - The current V6 core has no standalone outer L1/L2 wrapper layers.
	//     Some state names below still retain older "linear_layer_*" wording for trace/checkpoint
	//     compatibility, but the carried value is an identity passthrough in the current core.
	//
	// Differential model conventions:
	//   - XOR-difference: Δx = x ⊕ x'
	//   - For modular add/sub, we DO NOT force identity transitions.
	//     We enumerate feasible output differences and score with exact integer-weight backends:
	//       - xdp_add_lm2001_n : (Δx, Δy) -> Δ(x ⊞ y)
	//       - diff_subconst_exact_weight_ceil_int(): Δx -> Δ(x ⊟ c)
	//   - Cross-branch injection cd_injection_from_* is kept exactly (structure preserved).
	//     We propagate differences through its internal ops, and additionally attach the
	//     exact InjectionAffineTransition rank weight from the affine-derivative model.
	//
	// IMPORTANT (must match the real cipher structure):
	//   - We DO NOT remove cd_injection_from_* from the cipher implementation.
	//   - We DO propagate differences through cd_injection_from_* (structure preserved),
	//     and we also include InjectionAffineTransition-based rank_weight.
	//
	// One forward round (as implemented):
	//   Input: (A0, B0)
	//   (1)  B1 = B0 ⊞ (ROTL_31(A0) ⊕ ROTL_17(A0) ⊕ RC[0])
	//        A1 = A0 ⊟ RC1
	//        A2 = A1 ⊕ ROTL_{R0}(B1)
	//        B2 = B1 ⊕ ROTL_{R1}(A2)
	//        (C0, D0) = cd_injection_from_B(B2; rc0, rc1)
	//        A3 = A2 ⊕ (ROTL_24(C0) ⊕ ROTL_16(D0) ⊕ ROTL_8((C0 >> 1) ⊕ (D0 << 1)) ⊕ RC4)
	//
	//   (2)  A4 = A3 ⊞ (ROTL_31(B2) ⊕ ROTL_17(B2) ⊕ RC5)
	//        B3 = B2 ⊟ RC6      // no extra standalone linear layer between the two subrounds in V6
	//        B4 = B3 ⊕ ROTL_{R0}(A4)
	//        A5 = A4 ⊕ ROTL_{R1}(B4)
	//        (C1, D1) = cd_injection_from_A(A5; rc0, rc1)
	//        B5 = B4 ⊕ (ROTR_24(C1) ⊕ ROTR_16(D1) ⊕ ROTR_8((C1 << 3) ⊕ (D1 >> 3)) ⊕ RC9)
	//
	//   (3)  A_out = A5 ⊕ RC10
	//        B_out = B5 ⊕ RC11
	//
	// Record fields below store Δ-values at each boundary and the per-step weight.
	// ============================================================================


}  // namespace TwilightDream::auto_search_differential
