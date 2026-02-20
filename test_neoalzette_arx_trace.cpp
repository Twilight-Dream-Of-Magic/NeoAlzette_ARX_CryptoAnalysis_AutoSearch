/*
 * test_neoalzette_arx_trace.cpp
 *
 * PURPOSE (why this file exists):
 *   This translation unit is a *trace / debug utility* generated for the
 *   project’s experimental framework. It prints an **interpretable ARX
 *   state/difference transition trace** (per step) so we can:
 *     - sanity-check bit-level propagation (carry / rotation / XOR),
 *     - validate intermediate operators behave as expected,
 *     - visualize a concrete “one-path” trail for debugging and reporting.
 *
 * WHAT THIS FILE IS NOT:
 *   - NOT the core cryptanalytic engine.
 *   - NOT the implementation of our main differential/linear search models.
 *   - NOT used to produce security claims, bounds, or “final results”.
 *
 * IMPORTANT CONTEXT:
 *   The core engineering-analysis models live elsewhere:
 *     - test_neoalzette_differential_best_search.hpp
 *     - test_neoalzette_linear_best_search.hpp
 *   Those modules implement the actual search / scoring / pruning / evaluation
 *   logic used in our experiments. In contrast, this file only *replays* a
 *   single traced path and logs intermediate states.
 *
 * NOTES ON METHODS USED HERE:
 *   - For modular addition under XOR-differences, we may call paper operators
 *     (e.g., LM-2001 optimal-γ construction / weight computation) to score
 *     local transitions, but this file still remains a **trace generator**.
 *   - Any “greedy” or “chosen” output-difference selection used here is for
 *     producing a readable trace and does not define the project’s main model.
 *
 * TL;DR:
 *   Treat this file as an auxiliary *framework-level tracing tool*.
 *   If you are looking for the actual analysis/search implementation, read:
 *     test_neoalzette_differential_best_search.hpp
 *     - class DifferentialBestTrailSearcher
 *     test_neoalzette_linear_best_search.hpp
 *     - class LinearBestTrailSearcher
 */


#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <array>
#include <unordered_map>

#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_injection_constexpr.hpp"
#include "arx_analysis_operators/differential_optimal_gamma.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"

using TwilightDream::NeoAlzetteCore;
using TwilightDream::arx_operators::find_optimal_gamma_with_weight;
using TwilightDream::arx_operators::diff_subconst_bvweight;

namespace {

static std::uint32_t rotate_left_word32(std::uint32_t word_value, int rotation_amount) {
    return NeoAlzetteCore::rotl<std::uint32_t>(word_value, rotation_amount);
}

static double weight_to_probability(int weight) {
    if (weight < 0) return 0.0;
    return std::pow(2.0, -double(weight));
}

static void print_differential_trace_step(
    const char* step_name,
    std::uint32_t branch_a_difference,
    std::uint32_t branch_b_difference,
    double current_probability
) {
    std::cout << std::left << std::setw(45) << step_name
              << " | dA: 0x" << std::hex << std::setw(8) << std::setfill('0') << branch_a_difference
              << " | dB: 0x" << std::setw(8) << branch_b_difference
              << " | log2(P): " << std::dec << std::log2(current_probability)
              << "\n";
}

// ============================================================================
// Injection differential model (affine derivative for quadratic functions)
// ============================================================================

static constexpr std::uint32_t compute_injected_xor_term_from_branch_b(std::uint32_t branch_b_value) noexcept {
    return TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_b(branch_b_value);
}

static constexpr std::uint32_t compute_injected_xor_term_from_branch_a(std::uint32_t branch_a_value) noexcept {
    return TwilightDream::neoalzette_constexpr::injected_xor_term_from_branch_a(branch_a_value);
}

// Exact speed-up (same idea as in best_search), with C++20 consteval precompute:
static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_b() {
    std::array<std::uint32_t, 32> out{};
    for (int i = 0; i < 32; ++i) out[static_cast<std::size_t>(i)] = compute_injected_xor_term_from_branch_b(1u << i);
    return out;
}
static consteval std::array<std::uint32_t, 32> make_injected_xor_term_basis_branch_a() {
    std::array<std::uint32_t, 32> out{};
    for (int i = 0; i < 32; ++i) out[static_cast<std::size_t>(i)] = compute_injected_xor_term_from_branch_a(1u << i);
    return out;
}

static constexpr std::uint32_t g_injected_xor_term_f0_branch_b = compute_injected_xor_term_from_branch_b(0u);
static constexpr std::uint32_t g_injected_xor_term_f0_branch_a = compute_injected_xor_term_from_branch_a(0u);
static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_b = make_injected_xor_term_basis_branch_b();
static constexpr std::array<std::uint32_t, 32> g_injected_xor_term_f_basis_branch_a = make_injected_xor_term_basis_branch_a();

static int insert_vector_into_xor_linear_basis(std::array<std::uint32_t, 32>& basis_by_most_significant_bit, std::uint32_t vector_value) {
    for (int bit_position = 31; bit_position >= 0; --bit_position) {
        if (((vector_value >> bit_position) & 1u) == 0u) continue;
        if (basis_by_most_significant_bit[size_t(bit_position)] != 0u) {
            vector_value ^= basis_by_most_significant_bit[size_t(bit_position)];
        } else {
            basis_by_most_significant_bit[size_t(bit_position)] = vector_value;
            return 1;
        }
    }
    return 0;
}

struct InjectionTransitionSummary {
    std::uint32_t offset_difference = 0;
    int rank_weight = 0;
};

static InjectionTransitionSummary compute_injection_transition_summary_from_branch_b(std::uint32_t branch_b_input_difference) {
    static std::unordered_map<std::uint32_t, InjectionTransitionSummary> transition_cache;
    auto cache_iterator = transition_cache.find(branch_b_input_difference);
    if (cache_iterator != transition_cache.end()) return cache_iterator->second;

    InjectionTransitionSummary transition{};
    const std::uint32_t f_delta = compute_injected_xor_term_from_branch_b(branch_b_input_difference);
    transition.offset_difference = g_injected_xor_term_f0_branch_b ^ f_delta; // D_Δ f(0)

    std::array<std::uint32_t, 32> basis_by_most_significant_bit{};
    int rank = 0;
    for (int bit_index = 0; bit_index < 32; ++bit_index) {
        const std::uint32_t basis_input_vector = (1u << bit_index);
        const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_b[size_t(bit_index)];
        const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_b(basis_input_vector ^ branch_b_input_difference);
        const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta; // D_Δ f(e_i)
        const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset_difference;
        rank += insert_vector_into_xor_linear_basis(basis_by_most_significant_bit, column_vector);
    }
    transition.rank_weight = rank;

    transition_cache.emplace(branch_b_input_difference, transition);
    return transition;
}

static InjectionTransitionSummary compute_injection_transition_summary_from_branch_a(std::uint32_t branch_a_input_difference) {
    static std::unordered_map<std::uint32_t, InjectionTransitionSummary> transition_cache;
    auto cache_iterator = transition_cache.find(branch_a_input_difference);
    if (cache_iterator != transition_cache.end()) return cache_iterator->second;

    InjectionTransitionSummary transition{};
    const std::uint32_t f_delta = compute_injected_xor_term_from_branch_a(branch_a_input_difference);
    transition.offset_difference = g_injected_xor_term_f0_branch_a ^ f_delta; // D_Δ f(0)

    std::array<std::uint32_t, 32> basis_by_most_significant_bit{};
    int rank = 0;
    for (int bit_index = 0; bit_index < 32; ++bit_index) {
        const std::uint32_t basis_input_vector = (1u << bit_index);
        const std::uint32_t f_ei = g_injected_xor_term_f_basis_branch_a[size_t(bit_index)];
        const std::uint32_t f_ei_delta = compute_injected_xor_term_from_branch_a(basis_input_vector ^ branch_a_input_difference);
        const std::uint32_t d_delta_f_ei = f_ei ^ f_ei_delta; // D_Δ f(e_i)
        const std::uint32_t column_vector = d_delta_f_ei ^ transition.offset_difference;
        rank += insert_vector_into_xor_linear_basis(basis_by_most_significant_bit, column_vector);
    }
    transition.rank_weight = rank;

    transition_cache.emplace(branch_a_input_difference, transition);
    return transition;
}

// ============================================================================
// Greedy constant-subtraction output selection (still scored by paper operator)
// ============================================================================

static std::uint32_t compute_carry_out_bit(std::uint32_t input_bit, std::uint32_t additive_constant_bit, std::uint32_t carry_in_bit) {
    return (input_bit & additive_constant_bit) | (input_bit & carry_in_bit) | (additive_constant_bit & carry_in_bit);
}

static std::array<std::uint64_t, 4> compute_next_prefix_counts_for_addition_by_constant_at_bit(
    const std::array<std::uint64_t, 4>& prefix_counts_by_carry_pair_state,
    std::uint32_t input_difference_bit,
    std::uint32_t additive_constant_bit,
    std::uint32_t output_difference_bit
) {
    std::array<std::uint64_t, 4> next_prefix_counts_by_carry_pair_state{};
    for (int carry_pair_state_index = 0; carry_pair_state_index < 4; ++carry_pair_state_index) {
        const std::uint64_t prefix_count = prefix_counts_by_carry_pair_state[size_t(carry_pair_state_index)];
        if (prefix_count == 0) continue;
        const std::uint32_t carry_bit = (std::uint32_t(carry_pair_state_index) >> 1) & 1u;
        const std::uint32_t carry_bit_prime = (std::uint32_t(carry_pair_state_index) >> 0) & 1u;
        const std::uint32_t required_output_difference_bit = input_difference_bit ^ carry_bit ^ carry_bit_prime;
        if (required_output_difference_bit != output_difference_bit) continue;

        for (std::uint32_t input_bit = 0; input_bit <= 1; ++input_bit) {
            const std::uint32_t input_bit_prime = input_bit ^ input_difference_bit;
            const std::uint32_t next_carry_bit = compute_carry_out_bit(input_bit, additive_constant_bit, carry_bit);
            const std::uint32_t next_carry_bit_prime = compute_carry_out_bit(input_bit_prime, additive_constant_bit, carry_bit_prime);
            const int next_carry_pair_state_index = int((next_carry_bit << 1) | next_carry_bit_prime);
            next_prefix_counts_by_carry_pair_state[size_t(next_carry_pair_state_index)] += prefix_count;
        }
    }
    return next_prefix_counts_by_carry_pair_state;
}

static std::uint32_t compute_trace_chosen_output_difference_for_addition_by_constant(std::uint32_t input_difference, std::uint32_t additive_constant) {
    std::array<std::uint64_t, 4> prefix_counts_by_carry_pair_state{};
    prefix_counts_by_carry_pair_state[0] = 1;
    std::uint32_t output_difference = 0;
    for (int bit_position = 0; bit_position < 32; ++bit_position) {
        const std::uint32_t input_difference_bit = (input_difference >> bit_position) & 1u;
        const std::uint32_t additive_constant_bit = (additive_constant >> bit_position) & 1u;
        const auto next_counts_if_output_bit_zero =
            compute_next_prefix_counts_for_addition_by_constant_at_bit(prefix_counts_by_carry_pair_state, input_difference_bit, additive_constant_bit, 0u);
        const auto next_counts_if_output_bit_one =
            compute_next_prefix_counts_for_addition_by_constant_at_bit(prefix_counts_by_carry_pair_state, input_difference_bit, additive_constant_bit, 1u);
        const std::uint64_t total_count_if_output_bit_zero =
            next_counts_if_output_bit_zero[0] + next_counts_if_output_bit_zero[1] + next_counts_if_output_bit_zero[2] + next_counts_if_output_bit_zero[3];
        const std::uint64_t total_count_if_output_bit_one =
            next_counts_if_output_bit_one[0] + next_counts_if_output_bit_one[1] + next_counts_if_output_bit_one[2] + next_counts_if_output_bit_one[3];

        const std::uint32_t chosen_output_difference_bit =
            (total_count_if_output_bit_one > total_count_if_output_bit_zero) ? 1u : 0u;
        output_difference |= (chosen_output_difference_bit << bit_position);
        prefix_counts_by_carry_pair_state = chosen_output_difference_bit ? next_counts_if_output_bit_one : next_counts_if_output_bit_zero;
    }
    return output_difference;
}

static std::pair<std::uint32_t, int> choose_subtraction_by_constant_output_difference_and_weight_trace_chosen(
    std::uint32_t input_difference,
    std::uint32_t subtractive_constant
) {
    const std::uint32_t additive_constant = std::uint32_t(0u) - subtractive_constant;
    const std::uint32_t greedy_output_difference =
        compute_trace_chosen_output_difference_for_addition_by_constant(input_difference, additive_constant);

    const int greedy_weight = diff_subconst_bvweight(input_difference, subtractive_constant, greedy_output_difference);
    const int identity_weight = diff_subconst_bvweight(input_difference, subtractive_constant, input_difference);

    if (greedy_weight >= 0 && (identity_weight < 0 || greedy_weight <= identity_weight)) {
        return {greedy_output_difference, greedy_weight};
    }
    if (identity_weight >= 0) {
        return {input_difference, identity_weight};
    }
    return {greedy_output_difference, greedy_weight};
}

} // namespace

static void trace_differential_trail_path(std::uint32_t initial_branch_a_difference, std::uint32_t initial_branch_b_difference) {
    std::uint32_t branch_a_difference = initial_branch_a_difference;
    std::uint32_t branch_b_difference = initial_branch_b_difference;
    double total_probability = 1.0;

    std::cout << ">>> Tracing Differential Trail (paper operators + injection model) <<<\n";
    std::cout << "Start Difference: (0x" << std::hex << branch_a_difference << ", 0x" << branch_b_difference << ")\n\n";

    // --- First subround ---
    // B += (rotl(A,31) ^ rotl(A,17) ^ ROUND_CONSTANTS[0])
    {
        const std::uint32_t first_addition_term_difference =
            rotate_left_word32(branch_a_difference, 31) ^ rotate_left_word32(branch_a_difference, 17);
        auto [output_branch_b_difference_after_first_addition, weight_first_addition] =
            find_optimal_gamma_with_weight(branch_b_difference, first_addition_term_difference);
        branch_b_difference = output_branch_b_difference_after_first_addition;
        total_probability *= weight_to_probability(weight_first_addition);
        print_differential_trace_step("B += (rotl(A,31) ^ rotl(A,17) ^ RC[0])", branch_a_difference, branch_b_difference, total_probability);
    }

    // A -= ROUND_CONSTANTS[1]
    {
        const auto [output_branch_a_difference_after_constant_subtraction, constant_subtraction_weight] =
            choose_subtraction_by_constant_output_difference_and_weight_trace_chosen(branch_a_difference, NeoAlzetteCore::ROUND_CONSTANTS[1]);
        branch_a_difference = output_branch_a_difference_after_constant_subtraction;
        total_probability *= weight_to_probability(constant_subtraction_weight);
        print_differential_trace_step("A -= RC[1] (greedy output difference)", branch_a_difference, branch_b_difference, total_probability);
    }

    // A ^= rotl(B,R0)
    {
        branch_a_difference ^= rotate_left_word32(branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0);
        print_differential_trace_step("A ^= rotl(B,R0)", branch_a_difference, branch_b_difference, total_probability);
    }

    // B ^= rotl(A,R1)
    {
        branch_b_difference ^= rotate_left_word32(branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1);
        print_differential_trace_step("B ^= rotl(A,R1)", branch_a_difference, branch_b_difference, total_probability);
    }

    // Injection from branch B into branch A
    {
        const InjectionTransitionSummary transition = compute_injection_transition_summary_from_branch_b(branch_b_difference);
        const std::uint32_t chosen_injected_xor_difference = transition.offset_difference;
        branch_a_difference ^= chosen_injected_xor_difference;
        total_probability *= weight_to_probability(transition.rank_weight);
        print_differential_trace_step("A ^= injection_from_B(B) (affine-derivative offset)", branch_a_difference, branch_b_difference, total_probability);
    }

    // B = l1_backward(B)
    {
        branch_b_difference = NeoAlzetteCore::l1_backward(branch_b_difference);
        print_differential_trace_step("B = l1_backward(B)", branch_a_difference, branch_b_difference, total_probability);
    }

    // --- Second subround ---
    // A += (rotl(B,31) ^ rotl(B,17) ^ ROUND_CONSTANTS[5])
    {
        const std::uint32_t second_addition_term_difference =
            rotate_left_word32(branch_b_difference, 31) ^ rotate_left_word32(branch_b_difference, 17);
        auto [output_branch_a_difference_after_second_addition, weight_second_addition] =
            find_optimal_gamma_with_weight(branch_a_difference, second_addition_term_difference);
        branch_a_difference = output_branch_a_difference_after_second_addition;
        total_probability *= weight_to_probability(weight_second_addition);
        print_differential_trace_step("A += (rotl(B,31) ^ rotl(B,17) ^ RC[5])", branch_a_difference, branch_b_difference, total_probability);
    }

    // B -= ROUND_CONSTANTS[6]
    {
        const auto [output_branch_b_difference_after_constant_subtraction, constant_subtraction_weight] =
            choose_subtraction_by_constant_output_difference_and_weight_trace_chosen(branch_b_difference, NeoAlzetteCore::ROUND_CONSTANTS[6]);
        branch_b_difference = output_branch_b_difference_after_constant_subtraction;
        total_probability *= weight_to_probability(constant_subtraction_weight);
        print_differential_trace_step("B -= RC[6] (greedy output difference)", branch_a_difference, branch_b_difference, total_probability);
    }

    // B ^= rotl(A,R0)
    {
        branch_b_difference ^= rotate_left_word32(branch_a_difference, NeoAlzetteCore::CROSS_XOR_ROT_R0);
        print_differential_trace_step("B ^= rotl(A,R0)", branch_a_difference, branch_b_difference, total_probability);
    }

    // A ^= rotl(B,R1)
    {
        branch_a_difference ^= rotate_left_word32(branch_b_difference, NeoAlzetteCore::CROSS_XOR_ROT_R1);
        print_differential_trace_step("A ^= rotl(B,R1)", branch_a_difference, branch_b_difference, total_probability);
    }

    // Injection from branch A into branch B
    {
        const InjectionTransitionSummary transition = compute_injection_transition_summary_from_branch_a(branch_a_difference);
        const std::uint32_t chosen_injected_xor_difference = transition.offset_difference;
        branch_b_difference ^= chosen_injected_xor_difference;
        total_probability *= weight_to_probability(transition.rank_weight);
        print_differential_trace_step("B ^= injection_from_A(A) (affine-derivative offset)", branch_a_difference, branch_b_difference, total_probability);
    }

    // A = l2_backward(A)
    {
        branch_a_difference = NeoAlzetteCore::l2_backward(branch_a_difference);
        print_differential_trace_step("A = l2_backward(A)", branch_a_difference, branch_b_difference, total_probability);
    }

    std::cout << "\nTrace score (this single replayed path only): log2P = "  << std::log2(total_probability) << "\n";
}

int main() {
    trace_differential_trail_path(1u, 0u);
    std::cout << "\n--------------------------------\n";
    trace_differential_trail_path(0u, 1u);
    return 0;
}
