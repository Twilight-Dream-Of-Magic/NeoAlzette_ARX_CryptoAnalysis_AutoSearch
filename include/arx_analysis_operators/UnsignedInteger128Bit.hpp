#pragma once

#include <bit>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

namespace TwilightDream
{
	namespace arx_operators
	{
		class UnsignedInteger128Bit
		{
		public:
			constexpr UnsignedInteger128Bit() noexcept = default;

			template<typename Int,
				typename = std::enable_if_t<
					std::is_integral_v<std::remove_cv_t<Int>> &&
					( sizeof( std::remove_cv_t<Int> ) <= sizeof( std::uint64_t ) )>>
			constexpr UnsignedInteger128Bit( Int value ) noexcept
			{
				assign_small_integral( value );
			}

			[[nodiscard]] static constexpr UnsignedInteger128Bit from_high_low(
				std::uint64_t high,
				std::uint64_t low ) noexcept
			{
				UnsignedInteger128Bit out;
				out.high_ = high;
				out.low_ = low;
				return out;
			}

			[[nodiscard]] static constexpr UnsignedInteger128Bit max_value() noexcept
			{
				return from_high_low( ~std::uint64_t( 0 ), ~std::uint64_t( 0 ) );
			}

			[[nodiscard]] constexpr std::uint64_t high64() const noexcept
			{
				return high_;
			}

			[[nodiscard]] constexpr std::uint64_t low64() const noexcept
			{
				return low_;
			}

			[[nodiscard]] constexpr bool is_zero() const noexcept
			{
				return high_ == 0 && low_ == 0;
			}

			[[nodiscard]] constexpr bool test_bit( unsigned bit_index ) const noexcept
			{
				if ( bit_index >= 128u )
					return false;
				if ( bit_index >= 64u )
					return ( ( high_ >> ( bit_index - 64u ) ) & 1ull ) != 0ull;
				return ( ( low_ >> bit_index ) & 1ull ) != 0ull;
			}

			constexpr void set_bit( unsigned bit_index, bool value = true ) noexcept
			{
				if ( bit_index >= 128u )
					return;

				if ( bit_index >= 64u )
				{
					const std::uint64_t mask = std::uint64_t( 1 ) << ( bit_index - 64u );
					if ( value )
						high_ |= mask;
					else
						high_ &= ~mask;
					return;
				}

				const std::uint64_t mask = std::uint64_t( 1 ) << bit_index;
				if ( value )
					low_ |= mask;
				else
					low_ &= ~mask;
			}

			[[nodiscard]] constexpr int bit_width() const noexcept
			{
				if ( high_ != 0 )
					return 64 + static_cast<int>( std::bit_width( high_ ) );
				if ( low_ != 0 )
					return static_cast<int>( std::bit_width( low_ ) );
				return 0;
			}

			[[nodiscard]] constexpr explicit operator bool() const noexcept
			{
				return !is_zero();
			}

			[[nodiscard]] constexpr explicit operator std::uint64_t() const noexcept
			{
				return low_;
			}

			constexpr UnsignedInteger128Bit& operator+=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				const std::uint64_t new_low = low_ + rhs.low_;
				const std::uint64_t carry = ( new_low < low_ ) ? 1ull : 0ull;
				low_ = new_low;
				high_ = high_ + rhs.high_ + carry;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator-=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				const std::uint64_t borrow = ( low_ < rhs.low_ ) ? 1ull : 0ull;
				low_ -= rhs.low_;
				high_ = high_ - rhs.high_ - borrow;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator*=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				std::uint64_t p0_hi = 0;
				std::uint64_t p0_lo = 0;
				std::uint64_t p1_hi = 0;
				std::uint64_t p1_lo = 0;
				std::uint64_t p2_hi = 0;
				std::uint64_t p2_lo = 0;
				multiply_u64( low_, rhs.low_, p0_hi, p0_lo );
				multiply_u64( low_, rhs.high_, p1_hi, p1_lo );
				multiply_u64( high_, rhs.low_, p2_hi, p2_lo );
				( void )p1_hi;
				( void )p2_hi;
				low_ = p0_lo;
				high_ = p0_hi + p1_lo + p2_lo;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator/=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				*this = divide_with_remainder( *this, rhs ).first;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator%=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				*this = divide_with_remainder( *this, rhs ).second;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator&=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				high_ &= rhs.high_;
				low_ &= rhs.low_;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator|=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				high_ |= rhs.high_;
				low_ |= rhs.low_;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator^=( const UnsignedInteger128Bit& rhs ) noexcept
			{
				high_ ^= rhs.high_;
				low_ ^= rhs.low_;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator<<=( int shift ) noexcept
			{
				if ( shift <= 0 )
					return *this;
				if ( shift >= 128 )
				{
					high_ = 0;
					low_ = 0;
					return *this;
				}
				if ( shift >= 64 )
				{
					high_ = low_ << ( shift - 64 );
					low_ = 0;
					return *this;
				}

				high_ = ( high_ << shift ) | ( low_ >> ( 64 - shift ) );
				low_ <<= shift;
				return *this;
			}

			constexpr UnsignedInteger128Bit& operator>>=( int shift ) noexcept
			{
				if ( shift <= 0 )
					return *this;
				if ( shift >= 128 )
				{
					high_ = 0;
					low_ = 0;
					return *this;
				}
				if ( shift >= 64 )
				{
					low_ = high_ >> ( shift - 64 );
					high_ = 0;
					return *this;
				}

				low_ = ( low_ >> shift ) | ( high_ << ( 64 - shift ) );
				high_ >>= shift;
				return *this;
			}

			[[nodiscard]] friend constexpr bool operator==(
				const UnsignedInteger128Bit& lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				return lhs.high_ == rhs.high_ && lhs.low_ == rhs.low_;
			}

			[[nodiscard]] friend constexpr std::strong_ordering operator<=>(
				const UnsignedInteger128Bit& lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				if ( lhs.high_ < rhs.high_ )
					return std::strong_ordering::less;
				if ( lhs.high_ > rhs.high_ )
					return std::strong_ordering::greater;
				if ( lhs.low_ < rhs.low_ )
					return std::strong_ordering::less;
				if ( lhs.low_ > rhs.low_ )
					return std::strong_ordering::greater;
				return std::strong_ordering::equal;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator+(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs += rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator-(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs -= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator*(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs *= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator/(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs /= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator%(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs %= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator&(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs &= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator|(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs |= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator^(
				UnsignedInteger128Bit lhs,
				const UnsignedInteger128Bit& rhs ) noexcept
			{
				lhs ^= rhs;
				return lhs;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator~(
				const UnsignedInteger128Bit& value ) noexcept
			{
				return from_high_low( ~value.high_, ~value.low_ );
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator<<(
				UnsignedInteger128Bit value,
				int shift ) noexcept
			{
				value <<= shift;
				return value;
			}

			[[nodiscard]] friend constexpr UnsignedInteger128Bit operator>>(
				UnsignedInteger128Bit value,
				int shift ) noexcept
			{
				value >>= shift;
				return value;
			}

		private:
			std::uint64_t high_ { 0 };
			std::uint64_t low_ { 0 };

			template<typename Int>
			constexpr void assign_small_integral( Int value ) noexcept
			{
				using Raw = std::remove_cv_t<Int>;
				if constexpr ( std::is_signed_v<Raw> )
				{
					const std::int64_t signed_value = static_cast<std::int64_t>( value );
					low_ = static_cast<std::uint64_t>( signed_value );
					high_ = ( signed_value < 0 ) ? ~std::uint64_t( 0 ) : std::uint64_t( 0 );
				}
				else
				{
					low_ = static_cast<std::uint64_t>( value );
					high_ = 0;
				}
			}

			[[nodiscard]] static constexpr std::pair<UnsignedInteger128Bit, UnsignedInteger128Bit> divide_with_remainder(
				UnsignedInteger128Bit numerator,
				UnsignedInteger128Bit denominator ) noexcept
			{
				if ( denominator.is_zero() )
					return { {}, {} };

				UnsignedInteger128Bit quotient {};
				UnsignedInteger128Bit remainder {};
				for ( int bit = 127; bit >= 0; --bit )
				{
					remainder <<= 1;
					if ( numerator.test_bit( static_cast<unsigned>( bit ) ) )
						remainder.low_ |= 1ull;
					if ( remainder >= denominator )
					{
						remainder -= denominator;
						quotient.set_bit( static_cast<unsigned>( bit ) );
					}
				}
				return { quotient, remainder };
			}

			[[nodiscard]] static constexpr std::uint64_t low32( std::uint64_t value ) noexcept
			{
				return value & 0xFFFFFFFFull;
			}

			[[nodiscard]] static constexpr std::uint64_t high32( std::uint64_t value ) noexcept
			{
				return value >> 32;
			}

			static constexpr void multiply_u64(
				std::uint64_t a,
				std::uint64_t b,
				std::uint64_t& high,
				std::uint64_t& low ) noexcept
			{
				const std::uint64_t a0 = low32( a );
				const std::uint64_t a1 = high32( a );
				const std::uint64_t b0 = low32( b );
				const std::uint64_t b1 = high32( b );

				const std::uint64_t p00 = a0 * b0;
				const std::uint64_t p01 = a0 * b1;
				const std::uint64_t p10 = a1 * b0;
				const std::uint64_t p11 = a1 * b1;

				const std::uint64_t carry_mid = high32( p00 ) + low32( p01 ) + low32( p10 );
				low = ( low32( carry_mid ) << 32 ) | low32( p00 );
				high = p11 + high32( p01 ) + high32( p10 ) + high32( carry_mid );
			}
		};
	}  // namespace arx_operators
}  // namespace TwilightDream

namespace std
{
	template<>
	struct hash<TwilightDream::arx_operators::UnsignedInteger128Bit>
	{
		[[nodiscard]] size_t operator()( const TwilightDream::arx_operators::UnsignedInteger128Bit& value ) const noexcept
		{
			const size_t h0 = hash<std::uint64_t> {}( value.low64() );
			const size_t h1 = hash<std::uint64_t> {}( value.high64() );
			return h0 ^ ( h1 + 0x9e3779b97f4a7c15ULL + ( h0 << 6 ) + ( h0 >> 2 ) );
		}
	};

	template<>
	class numeric_limits<TwilightDream::arx_operators::UnsignedInteger128Bit>
	{
	public:
		static constexpr bool is_specialized = true;
		static constexpr bool is_signed = false;
		static constexpr bool is_integer = true;
		static constexpr bool is_exact = true;
		static constexpr bool is_modulo = true;
		static constexpr int digits = 128;
		static constexpr int digits10 = 38;
		static constexpr int max_digits10 = 0;
		static constexpr int radix = 2;

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit min() noexcept
		{
			return TwilightDream::arx_operators::UnsignedInteger128Bit {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit lowest() noexcept
		{
			return TwilightDream::arx_operators::UnsignedInteger128Bit {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit max() noexcept
		{
			return TwilightDream::arx_operators::UnsignedInteger128Bit::max_value();
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit epsilon() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit round_error() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit infinity() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit quiet_NaN() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit signaling_NaN() noexcept
		{
			return {};
		}

		[[nodiscard]] static constexpr TwilightDream::arx_operators::UnsignedInteger128Bit denorm_min() noexcept
		{
			return {};
		}

		static constexpr bool has_infinity = false;
		static constexpr bool has_quiet_NaN = false;
		static constexpr bool has_signaling_NaN = false;
		static constexpr float_denorm_style has_denorm = denorm_absent;
		static constexpr bool has_denorm_loss = false;
		static constexpr bool traps = false;
		static constexpr bool tinyness_before = false;
		static constexpr float_round_style round_style = round_toward_zero;
	};
}  // namespace std
