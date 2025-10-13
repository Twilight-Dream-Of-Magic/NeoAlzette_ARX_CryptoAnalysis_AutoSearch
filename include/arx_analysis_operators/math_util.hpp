#pragma once
#include <cstdint>
#include <type_traits>

namespace neoalz {
namespace arx_operators {

// Two's complement negation modulo 2^n for unsigned integers.
// - T must be an unsigned integer type (e.g., uint32_t/uint64_t)
// - n is the active word size in bits (1..bitwidth(T))
//   Returns: (-k) mod 2^n
//   This function masks inputs to n bits and computes two's complement.
template <class T>
static constexpr T neg_mod_2n(T k, int n) noexcept {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    const int widthBits = int(sizeof(T) * 8);
    const T mask = (n >= widthBits) ? T(~T(0)) : (T(1) << n) - 1;
    return (T(0) - (k & mask)) & mask;
}

} // namespace arx_operators
} // namespace neoalz
