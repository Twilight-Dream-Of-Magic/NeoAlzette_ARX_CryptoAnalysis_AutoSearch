#include "auto_search_frame/detail/linear_best_search_math.hpp"

namespace TwilightDream::auto_search_linear
{
	namespace
	{
		inline void finalize_varvar_add_candidate_ccz(
			std::uint32_t output_mask_u,
			AddCandidate& candidate ) noexcept
		{
			using TwilightDream::arx_operators::linear_correlation_add_ccz_weight;

			const std::uint64_t vx = static_cast<std::uint64_t>( candidate.input_mask_x );
			const std::uint64_t wy = static_cast<std::uint64_t>( candidate.input_mask_y );
			const auto weight =
				linear_correlation_add_ccz_weight(
					static_cast<std::uint64_t>( output_mask_u ),
					vx,
					wy,
					32 );
			if ( weight.has_value() )
				candidate.linear_weight = weight.value();
		}
	}  // namespace

	std::atomic<bool> g_disable_linear_tls_caches { false };

	void AddVarVarSplit8Enumerator32::reset_streaming_cursor(
		StreamingCursor& cursor,
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.output_mask_u = output_mask_u;
		cursor.weight_cap = std::min<SearchWeight>( weight_cap_requested, SearchWeight( 31 ) );
		cursor.output_mask_bytes = {
			std::uint8_t( output_mask_u >> 24 ),
			std::uint8_t( output_mask_u >> 16 ),
			std::uint8_t( output_mask_u >> 8 ),
			std::uint8_t( output_mask_u )
		};
		cursor.min_remaining_weight[ 4 ][ 0 ] = 0;
		cursor.min_remaining_weight[ 4 ][ 1 ] = 0;

		for ( int block_index = 3; block_index >= 0; --block_index )
		{
			for ( int connection_in = 0; connection_in <= 1; ++connection_in )
			{
				if ( runtime_time_limit_reached() )
				{
					cursor.stop_due_to_limits = true;
					return;
				}
				SearchWeight best = INFINITE_WEIGHT;
				const bool top_block = ( block_index == 0 );
				const auto& block_options =
					get_split8_block_options_for_u_byte(
						cursor.output_mask_bytes[ std::size_t( block_index ) ],
						connection_in,
						top_block );
				for ( const auto& opt : block_options )
				{
					if ( runtime_time_limit_reached() )
					{
						cursor.stop_due_to_limits = true;
						return;
					}
					const SearchWeight tail =
						( block_index == 3 )
							? SearchWeight( 0 )
							: cursor.min_remaining_weight[ std::size_t( block_index + 1 ) ][ std::size_t( opt.next_connection_bit & 1u ) ];
					best = std::min( best, tail + static_cast<SearchWeight>( opt.block_weight ) );
					if ( best == SearchWeight( 0 ) )
						break;
				}
				cursor.min_remaining_weight[ std::size_t( block_index ) ][ std::size_t( connection_in ) ] = best;
			}
		}
	}

	bool AddVarVarSplit8Enumerator32::next_streaming_candidate(
		StreamingCursor& cursor,
		AddCandidate& out_candidate )
	{
		if ( !cursor.initialized || cursor.stop_due_to_limits )
			return false;

		while ( true )
		{
			if ( runtime_time_limit_reached() )
			{
				cursor.stop_due_to_limits = true;
				return false;
			}

			if ( cursor.stack_size == 0 )
			{
				while ( cursor.next_target_weight <= cursor.weight_cap &&
				        cursor.min_remaining_weight[ 0 ][ 0 ] > cursor.next_target_weight )
				{
					++cursor.next_target_weight;
				}
				if ( cursor.next_target_weight > cursor.weight_cap )
					return false;

				cursor.stack[ 0 ] = StreamingCursorFrame {
					0,
					0,
					0u,
					0u,
					cursor.next_target_weight,
					0u,
					cursor.next_target_weight
				};
				cursor.stack_size = 1;
				++cursor.next_target_weight;
				continue;
			}

			StreamingCursorFrame& frame = cursor.stack[ cursor.stack_size - 1 ];
			if ( frame.block_index == 4 )
			{
				const bool emit = ( frame.remaining_weight == 0 );
				const AddCandidate candidate { frame.input_mask_x_acc, frame.input_mask_y_acc, frame.target_weight };
				--cursor.stack_size;
				if ( emit )
				{
					out_candidate = candidate;
					finalize_varvar_add_candidate_ccz( cursor.output_mask_u, out_candidate );
					return true;
				}
				continue;
			}

			const bool top_block = ( frame.block_index == 0 );
			const auto& block_options =
				get_split8_block_options_for_u_byte(
					cursor.output_mask_bytes[ std::size_t( frame.block_index ) ],
					frame.connection_in,
					top_block );
			const int shift = ( 3 - frame.block_index ) * 8;
			bool descended = false;
			while ( frame.option_index < block_options.size() )
			{
				if ( runtime_time_limit_reached() )
				{
					cursor.stop_due_to_limits = true;
					return false;
				}

				const auto& opt = block_options[ frame.option_index++ ];
				const SearchWeight local_weight = static_cast<SearchWeight>( opt.block_weight );
				if ( local_weight > frame.remaining_weight )
				{
					frame.option_index = block_options.size();
					break;
				}

				const SearchWeight next_remaining = frame.remaining_weight - local_weight;
				const int next_connection = int( opt.next_connection_bit & 1u );
				if ( frame.block_index != 3 &&
				     cursor.min_remaining_weight[ std::size_t( frame.block_index + 1 ) ][ std::size_t( next_connection ) ] > next_remaining )
				{
					continue;
				}

				cursor.stack[ cursor.stack_size++ ] = StreamingCursorFrame {
					frame.block_index + 1,
					next_connection,
					frame.input_mask_x_acc | ( std::uint32_t( opt.input_mask_x_byte ) << shift ),
					frame.input_mask_y_acc | ( std::uint32_t( opt.input_mask_y_byte ) << shift ),
					next_remaining,
					0u,
					frame.target_weight
				};
				descended = true;
				break;
			}

			if ( descended )
				continue;

			--cursor.stack_size;
		}
	}

	const std::vector<AddCandidate>& AddVarVarSplit8Enumerator32::get_candidates_for_output_mask_u(
		std::uint32_t output_mask_u,
		SearchWeight weight_cap_requested,
		SearchMode search_mode,
		bool enable_weight_sliced_clat,
		std::size_t weight_sliced_clat_max_candidates )
	{
		static thread_local CandidateCache cache;

		if ( search_mode == SearchMode::Strict )
		{
			cache.scratch_index = ( cache.scratch_index + 1u ) & 3u;
			auto& scratch = cache.scratch_ring[ std::size_t( cache.scratch_index ) ];
			auto release_scratch_after_runtime_stop = [ & ]() -> const std::vector<AddCandidate>& {
				scratch.clear();
				if ( scratch.capacity() > 4096u )
				{
					std::vector<AddCandidate> empty;
					scratch.swap( empty );
				}
				return scratch;
			};
			scratch = generate_add_candidates_split8_slr( output_mask_u, weight_cap_requested );
			if ( runtime_time_limit_reached() )
				return release_scratch_after_runtime_stop();
			(void)enable_weight_sliced_clat;
			(void)weight_sliced_clat_max_candidates;
			return scratch;
		}

		if ( g_disable_linear_tls_caches.load( std::memory_order_relaxed ) )
		{
			cache.by_output_mask_u.clear();
			cache.scratch_index = ( cache.scratch_index + 1u ) & 3u;
			auto& scratch = cache.scratch_ring[ std::size_t( cache.scratch_index ) ];
			scratch = generate_add_candidates_split8_slr( output_mask_u, weight_cap_requested );
			return scratch;
		}

		if ( auto it = cache.by_output_mask_u.find( output_mask_u ); it != cache.by_output_mask_u.end() )
			return it->second;

		if ( cache.by_output_mask_u.size() >= kMaxCandidateCacheEntries )
			cache.by_output_mask_u.clear();

		std::vector<AddCandidate> generated;
		#if AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR
		generated = generate_add_candidates_split8_slr( output_mask_u, SearchWeight( 31 ) );
		#else
		generated = generate_add_candidates_for_fixed_u( output_mask_u, 31 );
		#endif

		if ( generated.size() <= kMaxCachedCandidates )
		{
			auto [ ins_it, _ ] = cache.by_output_mask_u.emplace( output_mask_u, std::move( generated ) );
			return ins_it->second;
		}

		cache.scratch_index = ( cache.scratch_index + 1u ) & 3u;
		auto& scratch = cache.scratch_ring[ std::size_t( cache.scratch_index ) ];
		scratch = std::move( generated );
		return scratch;
	}

	const std::vector<AddVarVarSplit8Enumerator32::Split8BlockOption>& AddVarVarSplit8Enumerator32::get_split8_block_options_for_u_byte(
		std::uint8_t u_byte,
		int connection_bit_in,
		bool exclude_top_z31_weight )
	{
		const std::uint16_t cache_key =
			std::uint16_t( std::uint16_t( u_byte ) << 2 ) |
			std::uint16_t( ( connection_bit_in & 1 ) << 1 ) |
			std::uint16_t( exclude_top_z31_weight ? 1 : 0 );
		static thread_local std::unordered_map<std::uint16_t, std::vector<Split8BlockOption>> cache;
		static thread_local std::vector<Split8BlockOption> scratch;

		if ( g_disable_linear_tls_caches.load( std::memory_order_relaxed ) )
		{
			scratch.clear();

			struct DfsState
			{
				int          bit_index = 7;
				int          z_bit = 0;
				std::uint8_t input_mask_x = 0;
				std::uint8_t input_mask_y = 0;
				int          weight_sum = 0;
			};

			static thread_local std::vector<DfsState> stack;
			stack.clear();
			stack.push_back( DfsState { 7, ( connection_bit_in & 1 ), 0u, 0u, 0 } );

			while ( !stack.empty() )
			{
				if ( runtime_time_limit_reached() )
				{
					scratch.clear();
					return scratch;
				}
				const DfsState st = stack.back();
				stack.pop_back();

				if ( st.bit_index < 0 )
				{
					if ( physical_memory_allocation_guard_active() )
					{
						scratch.clear();
						return scratch;
					}
					scratch.push_back( Split8BlockOption { st.input_mask_x, st.input_mask_y, std::uint8_t( st.z_bit & 1 ), std::uint8_t( st.weight_sum ) } );
					continue;
				}

				const int bit_index = st.bit_index;
				const int z = st.z_bit & 1;
				const int u_i = int( ( u_byte >> bit_index ) & 1u );

				const int weight_add = ( exclude_top_z31_weight && bit_index == 7 ) ? 0 : z;
				const int next_weight_sum = st.weight_sum + weight_add;

				auto push_next = [ & ]( int v_i, int w_i ) {
					DfsState nx = st;
					nx.bit_index = bit_index - 1;
					nx.input_mask_x = std::uint8_t( nx.input_mask_x | ( std::uint8_t( ( v_i & 1 ) << bit_index ) ) );
					nx.input_mask_y = std::uint8_t( nx.input_mask_y | ( std::uint8_t( ( w_i & 1 ) << bit_index ) ) );
					nx.z_bit = z ^ u_i ^ ( v_i & 1 ) ^ ( w_i & 1 );
					nx.weight_sum = next_weight_sum;
					stack.push_back( nx );
				};

				if ( z == 0 )
				{
					push_next( u_i, u_i );
				}
				else
				{
					push_next( 0, 0 );
					push_next( 0, 1 );
					push_next( 1, 0 );
					push_next( 1, 1 );
				}
			}

			if ( runtime_time_limit_reached() )
			{
				scratch.clear();
				return scratch;
			}

			std::sort( scratch.begin(), scratch.end(), []( const Split8BlockOption& a, const Split8BlockOption& b ) {
				if ( a.block_weight != b.block_weight )
					return a.block_weight < b.block_weight;
				if ( a.next_connection_bit != b.next_connection_bit )
					return a.next_connection_bit < b.next_connection_bit;
				if ( a.input_mask_x_byte != b.input_mask_x_byte )
					return a.input_mask_x_byte < b.input_mask_x_byte;
				return a.input_mask_y_byte < b.input_mask_y_byte;
			} );

			return scratch;
		}

		if ( auto it = cache.find( cache_key ); it != cache.end() )
			return it->second;

		if ( cache.size() >= kMaxBlockOptionCacheEntries )
			cache.clear();

		std::vector<Split8BlockOption> options;

		struct DfsState
		{
			int          bit_index = 7;
			int          z_bit = 0;
			std::uint8_t input_mask_x = 0;
			std::uint8_t input_mask_y = 0;
			int          weight_sum = 0;
		};

		static thread_local std::vector<DfsState> stack;
		stack.clear();
		stack.push_back( DfsState { 7, ( connection_bit_in & 1 ), 0u, 0u, 0 } );

		while ( !stack.empty() )
		{
			if ( runtime_time_limit_reached() )
			{
				scratch.clear();
				return scratch;
			}
			const DfsState st = stack.back();
			stack.pop_back();

			if ( st.bit_index < 0 )
			{
				if ( physical_memory_allocation_guard_active() )
				{
					options.clear();
					scratch.clear();
					return scratch;
				}
				options.push_back( Split8BlockOption { st.input_mask_x, st.input_mask_y, std::uint8_t( st.z_bit & 1 ), std::uint8_t( st.weight_sum ) } );
				continue;
			}

			const int bit_index = st.bit_index;
			const int z = st.z_bit & 1;
			const int u_i = int( ( u_byte >> bit_index ) & 1u );

			const int weight_add = ( exclude_top_z31_weight && bit_index == 7 ) ? 0 : z;
			const int next_weight_sum = st.weight_sum + weight_add;

			auto push_next = [ & ]( int v_i, int w_i ) {
				DfsState nx = st;
				nx.bit_index = bit_index - 1;
				nx.input_mask_x = std::uint8_t( nx.input_mask_x | ( std::uint8_t( ( v_i & 1 ) << bit_index ) ) );
				nx.input_mask_y = std::uint8_t( nx.input_mask_y | ( std::uint8_t( ( w_i & 1 ) << bit_index ) ) );
				nx.z_bit = z ^ u_i ^ ( v_i & 1 ) ^ ( w_i & 1 );
				nx.weight_sum = next_weight_sum;
				stack.push_back( nx );
			};

			if ( z == 0 )
			{
				push_next( u_i, u_i );
			}
			else
			{
				push_next( 0, 0 );
				push_next( 0, 1 );
				push_next( 1, 0 );
				push_next( 1, 1 );
			}
		}

		if ( runtime_time_limit_reached() )
		{
			scratch.clear();
			return scratch;
		}

		std::sort( options.begin(), options.end(), []( const Split8BlockOption& a, const Split8BlockOption& b ) {
			if ( a.block_weight != b.block_weight )
				return a.block_weight < b.block_weight;
			if ( a.next_connection_bit != b.next_connection_bit )
				return a.next_connection_bit < b.next_connection_bit;
			if ( a.input_mask_x_byte != b.input_mask_x_byte )
				return a.input_mask_x_byte < b.input_mask_x_byte;
			return a.input_mask_y_byte < b.input_mask_y_byte;
		} );

		auto [ ins_it, _ ] = cache.emplace( cache_key, std::move( options ) );
		return ins_it->second;
	}

	std::vector<AddCandidate> AddVarVarSplit8Enumerator32::generate_add_candidates_split8_slr(
		std::uint32_t output_mask_u,
		SearchWeight weight_cap )
	{
		std::vector<AddCandidate> candidates;
		weight_cap = std::min<SearchWeight>( weight_cap, SearchWeight( 31 ) );

		const std::uint8_t u_bytes[ 4 ] = {
			std::uint8_t( output_mask_u >> 24 ),
			std::uint8_t( output_mask_u >> 16 ),
			std::uint8_t( output_mask_u >> 8 ),
			std::uint8_t( output_mask_u )
		};

		SearchWeight min_remaining_weight[ 5 ][ 2 ];
		min_remaining_weight[ 4 ][ 0 ] = 0;
		min_remaining_weight[ 4 ][ 1 ] = 0;

		for ( int block_index = 3; block_index >= 0; --block_index )
		{
			for ( int connection_in = 0; connection_in <= 1; ++connection_in )
			{
				if ( runtime_time_limit_reached() )
				{
					candidates.clear();
					return candidates;
				}
				SearchWeight best = INFINITE_WEIGHT;
				const bool top_block = ( block_index == 0 );
				const auto& block_options =
					get_split8_block_options_for_u_byte(
						u_bytes[ block_index ],
						connection_in,
						top_block );
				for ( const auto& opt : block_options )
				{
					if ( runtime_time_limit_reached() )
					{
						candidates.clear();
						return candidates;
					}
					const SearchWeight tail =
						( block_index == 3 )
							? SearchWeight( 0 )
							: min_remaining_weight[ block_index + 1 ][ int( opt.next_connection_bit & 1u ) ];
					best = std::min( best, tail + static_cast<SearchWeight>( opt.block_weight ) );
					if ( best == SearchWeight( 0 ) )
						break;
				}
				min_remaining_weight[ block_index ][ connection_in ] = best;
			}
		}

		const auto enumerate = [ & ](
			                       auto&& self,
			                       SearchWeight target_weight,
			                       int block_index,
			                       int connection_in,
			                       std::uint32_t input_mask_x_acc,
			                       std::uint32_t input_mask_y_acc,
			                       SearchWeight remaining_weight ) -> void {
			if ( runtime_time_limit_reached() )
				return;
			if ( min_remaining_weight[ block_index ][ connection_in ] > remaining_weight )
				return;

			if ( block_index == 4 )
			{
				if ( remaining_weight == 0 )
				{
					if ( physical_memory_allocation_guard_active() )
					{
						candidates.clear();
						return;
					}
					candidates.push_back( AddCandidate { input_mask_x_acc, input_mask_y_acc, target_weight } );
				}
				return;
			}

			const bool top_block = ( block_index == 0 );
			const auto& block_options =
				get_split8_block_options_for_u_byte(
					u_bytes[ block_index ],
					connection_in,
					top_block );
			const int shift = ( 3 - block_index ) * 8;

			for ( const auto& opt : block_options )
			{
				if ( runtime_time_limit_reached() )
					return;
				const SearchWeight local_w = static_cast<SearchWeight>( opt.block_weight );
				if ( local_w > remaining_weight )
					break;
				const SearchWeight next_remaining = remaining_weight - local_w;
				const int next_connection = int( opt.next_connection_bit & 1u );
				if ( block_index != 3 && min_remaining_weight[ block_index + 1 ][ next_connection ] > next_remaining )
					continue;

				const std::uint32_t x2 =
					input_mask_x_acc | ( std::uint32_t( opt.input_mask_x_byte ) << shift );
				const std::uint32_t y2 =
					input_mask_y_acc | ( std::uint32_t( opt.input_mask_y_byte ) << shift );
				self( self, target_weight, block_index + 1, next_connection, x2, y2, next_remaining );
			}
		};

		for ( SearchWeight target = 0; target <= weight_cap; ++target )
		{
			if ( runtime_time_limit_reached() )
			{
				candidates.clear();
				return candidates;
			}
			if ( min_remaining_weight[ 0 ][ 0 ] > target )
				continue;
			enumerate( enumerate, target, 0, 0, 0u, 0u, target );
		}

		if ( runtime_time_limit_reached() )
		{
			candidates.clear();
			return candidates;
		}

		std::sort( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) {
			if ( a.linear_weight != b.linear_weight )
				return a.linear_weight < b.linear_weight;
			if ( a.input_mask_x != b.input_mask_x )
				return a.input_mask_x < b.input_mask_x;
			return a.input_mask_y < b.input_mask_y;
		} );
		candidates.erase(
			std::unique( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) {
				return a.input_mask_x == b.input_mask_x && a.input_mask_y == b.input_mask_y;
			} ),
			candidates.end() );
		for ( auto& c : candidates )
			finalize_varvar_add_candidate_ccz( output_mask_u, c );
		std::sort( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) {
			if ( a.linear_weight != b.linear_weight )
				return a.linear_weight < b.linear_weight;
			if ( a.input_mask_x != b.input_mask_x )
				return a.input_mask_x < b.input_mask_x;
			return a.input_mask_y < b.input_mask_y;
		} );
		return candidates;
	}
}  // namespace TwilightDream::auto_search_linear
