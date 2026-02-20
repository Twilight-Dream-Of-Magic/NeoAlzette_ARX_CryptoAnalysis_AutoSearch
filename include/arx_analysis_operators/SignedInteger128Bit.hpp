#pragma once

#include <bit>
#include <cmath>
#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>

#include "UnsignedInteger128Bit.hpp"

namespace TwilightDream::arx_operators
{
	struct SignedInteger128Bit
	{
		std::uint64_t lo { 0 };
		std::uint64_t hi { 0 };

		constexpr SignedInteger128Bit() noexcept = default;

		template<typename Int,
			typename = std::enable_if_t<
				std::is_integral_v<std::remove_cv_t<Int>> &&
				std::is_signed_v<std::remove_cv_t<Int>> &&
				( sizeof( std::remove_cv_t<Int> ) <= sizeof( std::int64_t ) )>>
		constexpr SignedInteger128Bit( Int value ) noexcept
			: lo( static_cast<std::uint64_t>( static_cast<std::int64_t>( value ) ) ),
			  hi( static_cast<std::int64_t>( value ) < 0 ? ~std::uint64_t( 0 ) : std::uint64_t( 0 ) )
		{
		}

		[[nodiscard]] static constexpr SignedInteger128Bit from_words(
			std::uint64_t high,
			std::uint64_t low ) noexcept
		{
			SignedInteger128Bit out;
			out.hi = high;
			out.lo = low;
			return out;
		}

		[[nodiscard]] constexpr bool is_zero() const noexcept
		{
			return lo == 0 && hi == 0;
		}

		[[nodiscard]] constexpr bool is_negative() const noexcept
		{
			return static_cast<std::int64_t>( hi ) < 0;
		}

		[[nodiscard]] constexpr UnsignedInteger128Bit bit_pattern() const noexcept
		{
			return UnsignedInteger128Bit::from_high_low( hi, lo );
		}

		[[nodiscard]] constexpr UnsignedInteger128Bit magnitude_bits() const noexcept
		{
			const UnsignedInteger128Bit bits = bit_pattern();
			return is_negative() ? ( UnsignedInteger128Bit {} - bits ) : bits;
		}

		[[nodiscard]] static constexpr SignedInteger128Bit neg_twos_complement( SignedInteger128Bit x ) noexcept
		{
			x.lo = ~x.lo + 1ull;
			x.hi = ~x.hi + ( ( x.lo == 0ull ) ? 1ull : 0ull );
			return x;
		}

		[[nodiscard]] friend constexpr SignedInteger128Bit operator-( SignedInteger128Bit x ) noexcept
		{
			return neg_twos_complement( x );
		}

		[[nodiscard]] friend constexpr SignedInteger128Bit operator+(
			SignedInteger128Bit a,
			const SignedInteger128Bit& b ) noexcept
		{
			const std::uint64_t sum_lo = a.lo + b.lo;
			const std::uint64_t carry = ( sum_lo < a.lo ) ? 1ull : 0ull;
			a.lo = sum_lo;
			a.hi = a.hi + b.hi + carry;
			return a;
		}

		[[nodiscard]] friend constexpr SignedInteger128Bit operator-(
			SignedInteger128Bit a,
			const SignedInteger128Bit& b ) noexcept
		{
			return a + ( -b );
		}

		friend constexpr SignedInteger128Bit& operator+=( SignedInteger128Bit& a, const SignedInteger128Bit& b ) noexcept
		{
			a = a + b;
			return a;
		}

		friend constexpr SignedInteger128Bit& operator-=( SignedInteger128Bit& a, const SignedInteger128Bit& b ) noexcept
		{
			a = a - b;
			return a;
		}

		[[nodiscard]] friend constexpr SignedInteger128Bit operator*(
			const SignedInteger128Bit& a,
			const SignedInteger128Bit& b ) noexcept
		{
			const bool neg = a.is_negative() != b.is_negative();
			const UnsignedInteger128Bit mag_a = a.magnitude_bits();
			const UnsignedInteger128Bit mag_b = b.magnitude_bits();
			const UnsignedInteger128Bit mag = mag_a * mag_b;
			SignedInteger128Bit out = from_words( mag.high64(), mag.low64() );
			return neg ? -out : out;
		}

		friend constexpr SignedInteger128Bit& operator*=( SignedInteger128Bit& a, const SignedInteger128Bit& b ) noexcept
		{
			a = a * b;
			return a;
		}

		[[nodiscard]] friend constexpr bool operator==( const SignedInteger128Bit& a, const SignedInteger128Bit& b ) noexcept
		{
			return a.lo == b.lo && a.hi == b.hi;
		}

		[[nodiscard]] friend constexpr std::strong_ordering operator<=>(
			const SignedInteger128Bit& a,
			const SignedInteger128Bit& b ) noexcept
		{
			const std::int64_t ahi = static_cast<std::int64_t>( a.hi );
			const std::int64_t bhi = static_cast<std::int64_t>( b.hi );
			if ( ahi < bhi )
				return std::strong_ordering::less;
			if ( ahi > bhi )
				return std::strong_ordering::greater;
			if ( a.lo < b.lo )
				return std::strong_ordering::less;
			if ( a.lo > b.lo )
				return std::strong_ordering::greater;
			return std::strong_ordering::equal;
		}

		[[nodiscard]] constexpr SignedInteger128Bit shl( int k ) const noexcept
		{
			if ( k <= 0 )
				return *this;
			const UnsignedInteger128Bit bits = bit_pattern() << k;
			return from_words( bits.high64(), bits.low64() );
		}

		[[nodiscard]] constexpr int bit_width_abs() const noexcept
		{
			return magnitude_bits().bit_width();
		}

		[[nodiscard]] inline long double to_long_double() const noexcept
		{
			const UnsignedInteger128Bit mag = magnitude_bits();
			long double v = std::scalbn( static_cast<long double>( mag.high64() ), 64 );
			v += static_cast<long double>( mag.low64() );
			return is_negative() ? -v : v;
		}

		[[nodiscard]] inline double to_double() const noexcept
		{
			const UnsignedInteger128Bit mag = magnitude_bits();
			double v = std::ldexp( static_cast<double>( mag.high64() ), 64 );
			v += static_cast<double>( mag.low64() );
			return is_negative() ? -v : v;
		}
	};
}  // namespace TwilightDream::arx_operators

namespace std
{
	template<>
	struct hash<TwilightDream::arx_operators::SignedInteger128Bit>
	{
		[[nodiscard]] size_t operator()( const TwilightDream::arx_operators::SignedInteger128Bit& value ) const noexcept
		{
			const size_t h0 = hash<std::uint64_t> {}( value.lo );
			const size_t h1 = hash<std::uint64_t> {}( value.hi );
			return h0 ^ ( h1 + 0x9e3779b97f4a7c15ULL + ( h0 << 6 ) + ( h0 >> 2 ) );
		}
	};
}  // namespace std
