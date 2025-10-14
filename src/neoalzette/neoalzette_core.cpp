#include "neoalzette/neoalzette_core.hpp"

namespace TwilightDream
{
	// generate dynamic diffusion mask for NeoAlzette
	// 3 + 7 + 7 + 7 .... mod 32 generate 3,10,17,24,31,6,13,20,27,2,9,16

	std::uint32_t generate_dynamic_diffusion_mask0( std::uint32_t X ) noexcept
	{
		return NeoAlzetteCore::rotl( X, 2 ) ^ NeoAlzetteCore::rotl( X, 3 ) ^ NeoAlzetteCore::rotl( X, 6 ) ^ NeoAlzetteCore::rotl( X, 9 ) 
			^ NeoAlzetteCore::rotl( X, 10 ) ^ NeoAlzetteCore::rotl( X, 13 ) ^ NeoAlzetteCore::rotl( X, 16 ) ^ NeoAlzetteCore::rotl( X, 17 ) 
			^ NeoAlzetteCore::rotl( X, 20 ) ^ NeoAlzetteCore::rotl( X, 24 ) ^ NeoAlzetteCore::rotl( X, 27 ) ^ NeoAlzetteCore::rotl( X, 31 );
	}

	std::uint32_t generate_dynamic_diffusion_mask1( std::uint32_t X ) noexcept
	{
		return NeoAlzetteCore::rotr( X, 2 ) ^ NeoAlzetteCore::rotr( X, 3 ) ^ NeoAlzetteCore::rotr( X, 6 ) ^ NeoAlzetteCore::rotr( X, 9 ) 
			^ NeoAlzetteCore::rotr( X, 10 ) ^ NeoAlzetteCore::rotr( X, 13 ) ^ NeoAlzetteCore::rotr( X, 16 ) ^ NeoAlzetteCore::rotr( X, 17 ) 
			^ NeoAlzetteCore::rotr( X, 20 ) ^ NeoAlzetteCore::rotr( X, 24 ) ^ NeoAlzetteCore::rotr( X, 27 ) ^ NeoAlzetteCore::rotr( X, 31 );
	}

	// ============================================================================
	// Cross-branch injection (value domain with constants)
	// ============================================================================

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_B( std::uint32_t B, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		std::uint32_t s_box_in_B = ( B ^ RC[ 2 ] ) ^ ( ~( B & generate_dynamic_diffusion_mask0( B ) ) );

		std::uint32_t c = NeoAlzetteCore::l2_forward( B );
		std::uint32_t d = NeoAlzetteCore::l1_forward( B ) ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_B;
		d ^= NeoAlzetteCore::rotr( t, 16 ) ^ rc1;
		return { c, d };
	}

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::cd_injection_from_A( std::uint32_t A, std::uint32_t rc0, std::uint32_t rc1 ) noexcept
	{
		const auto& RC = ROUND_CONSTANTS;
		//XOR with NOT-AND and NOT-OR is balance of boolean logic
		std::uint32_t s_box_in_A = ( A ^ RC[ 7 ] ) ^ ( ~( A | generate_dynamic_diffusion_mask1( A ) ) );

		std::uint32_t c = NeoAlzetteCore::l1_forward( A );
		std::uint32_t d = NeoAlzetteCore::l2_forward( A ) ^ rc0;

		std::uint32_t t = c ^ d;
		c ^= d ^ s_box_in_A;
		d ^= NeoAlzetteCore::rotl( t, 16 ) ^ rc1;
		return { c, d };
	}

	// ============================================================================
	// Main ARX-box transformations
	// ============================================================================

	void NeoAlzetteCore::forward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// First subround
		B += ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );
		A -= RC[ 1 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
			B = NeoAlzetteCore::l1_backward( B );
		}

		// Second subround
		A += ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );
		B -= RC[ 6 ];  //This is hardcore! For current academic research papers on lightweight cryptography based on ARX, there's no good way to analyze it.
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		{
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
			A = NeoAlzetteCore::l2_backward( A );
		}

		// Final constant addition
		A ^= RC[ 10 ];
		B ^= RC[ 11 ];
		a = A;
		b = B;
	}

	void NeoAlzetteCore::backward( std::uint32_t& a, std::uint32_t& b ) noexcept
	{
		const auto&	  RC = ROUND_CONSTANTS;
		std::uint32_t A = a, B = b;

		// Reverse final constant addition
		B ^= RC[ 11 ];
		A ^= RC[ 10 ];

		// Reverse second subround
		{
			A = NeoAlzetteCore::l2_forward( A );
			//PRF A -> B
			auto [ C1, D1 ] = cd_injection_from_A( A, ( RC[ 7 ] & RC[ 8 ] ), RC[ 8 ] );
			B ^= ( NeoAlzetteCore::rotl( C1, 24 ) ^ NeoAlzetteCore::rotl( D1, 16 ) ^ RC[ 9 ] );
		}
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		B += RC[ 6 ];
		A -= ( NeoAlzetteCore::rotl( B, 31 ) ^ NeoAlzetteCore::rotl( B, 17 ) ^ RC[ 5 ] );

		// Reverse first subround
		{
			B = NeoAlzetteCore::l1_forward( B );
			//PRF B -> A
			auto [ C0, D0 ] = cd_injection_from_B( B, ( RC[ 2 ] | RC[ 3 ] ), RC[ 3 ] );
			A ^= ( NeoAlzetteCore::rotl( C0, 24 ) ^ NeoAlzetteCore::rotl( D0, 16 ) ^ RC[ 4 ] );
		}
		B ^= NeoAlzetteCore::rotl( A, NeoAlzetteCore::CROSS_XOR_ROT_R1 );
		A ^= NeoAlzetteCore::rotl( B, NeoAlzetteCore::CROSS_XOR_ROT_R0 );
		A += RC[ 1 ];
		B -= ( NeoAlzetteCore::rotl( A, 31 ) ^ NeoAlzetteCore::rotl( A, 17 ) ^ RC[ 0 ] );

		a = A;
		b = B;
	}

	// ============================================================================
	// Convenience methods
	// ============================================================================

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::encrypt( std::uint32_t a, std::uint32_t b ) noexcept
	{
		forward( a, b );
		return { a, b };
	}

	std::pair<std::uint32_t, std::uint32_t> NeoAlzetteCore::decrypt( std::uint32_t a, std::uint32_t b ) noexcept
	{
		backward( a, b );
		return { a, b };
	}

}  // namespace TwilightDream