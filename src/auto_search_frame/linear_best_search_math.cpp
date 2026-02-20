#include "auto_search_frame/detail/linear_best_search_math.hpp"

namespace TwilightDream::auto_search_linear
{
	std::atomic<bool> g_disable_linear_tls_caches { false };

	void reset_weight_sliced_clat_streaming_cursor(
		WeightSlicedClatStreamingCursor& cursor,
		std::uint32_t output_mask_u,
		int weight_cap_requested )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.output_mask_u = output_mask_u;
		cursor.weight_cap = std::clamp( weight_cap_requested, 0, 31 );

		const int u31 = int( ( output_mask_u >> 31 ) & 1u );
		cursor.input_mask_x_prefix = u31 ? ( 1u << 31 ) : 0u;
		cursor.input_mask_y_prefix = cursor.input_mask_x_prefix;
		cursor.z30 = u31;
		cursor.next_target_weight = cursor.z30;
		cursor.current_target_weight = -1;
	}

	bool next_weight_sliced_clat_streaming_candidate(
		WeightSlicedClatStreamingCursor& cursor,
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
				if ( cursor.next_target_weight > cursor.weight_cap )
					return false;

				cursor.current_target_weight = cursor.next_target_weight++;
				cursor.stack[ 0 ] = WeightSlicedClatStreamingCursorFrame {
					30,
					cursor.input_mask_x_prefix,
					cursor.input_mask_y_prefix,
					cursor.z30,
					0,
					0
				};
				cursor.stack_size = 1;
				continue;
			}

			WeightSlicedClatStreamingCursorFrame& frame = cursor.stack[ cursor.stack_size - 1 ];
			const int bit_index = frame.bit_index;
			const int z_i = frame.z_bit & 1;
			const int z_weight = frame.z_weight_so_far + z_i;

			if ( frame.branch_state == 0 )
			{
				const int remaining_z_bits = bit_index;
				if ( z_weight > cursor.current_target_weight ||
					 z_weight + remaining_z_bits < cursor.current_target_weight )
				{
					--cursor.stack_size;
					continue;
				}
			}

			const int u_i = int( ( cursor.output_mask_u >> bit_index ) & 1u );
			if ( z_i == 0 )
			{
				if ( frame.branch_state != 0 )
				{
					--cursor.stack_size;
					continue;
				}

				frame.branch_state = 1;
				const std::uint32_t next_input_mask_x = frame.input_mask_x | ( std::uint32_t( u_i & 1 ) << bit_index );
				const std::uint32_t next_input_mask_y = frame.input_mask_y | ( std::uint32_t( u_i & 1 ) << bit_index );
				if ( bit_index == 0 )
				{
					--cursor.stack_size;
					if ( z_weight == cursor.current_target_weight )
					{
						out_candidate = AddCandidate { next_input_mask_x, next_input_mask_y, z_weight };
						return true;
					}
					continue;
				}

				// With z_i == 0 and the forced assignment v_i = w_i = u_i, Eq.(1) reduces to z_{i-1} = u_i.
				const int z_prev = u_i;
				cursor.stack[ cursor.stack_size++ ] = WeightSlicedClatStreamingCursorFrame {
					bit_index - 1,
					next_input_mask_x,
					next_input_mask_y,
					z_prev,
					z_weight,
					0
				};
				continue;
			}

			if ( frame.branch_state >= 4 )
			{
				--cursor.stack_size;
				continue;
			}

			const std::uint8_t branch_code = static_cast<std::uint8_t>( 3u - frame.branch_state );
			++frame.branch_state;

			const int v_i = int( ( branch_code >> 1 ) & 1u );
			const int w_i = int( branch_code & 1u );
			const std::uint32_t next_input_mask_x = frame.input_mask_x | ( std::uint32_t( v_i & 1 ) << bit_index );
			const std::uint32_t next_input_mask_y = frame.input_mask_y | ( std::uint32_t( w_i & 1 ) << bit_index );
			if ( bit_index == 0 )
			{
				if ( z_weight == cursor.current_target_weight )
				{
					out_candidate = AddCandidate { next_input_mask_x, next_input_mask_y, z_weight };
					return true;
				}
				continue;
			}

			const int z_prev = z_i ^ u_i ^ v_i ^ w_i;
			cursor.stack[ cursor.stack_size++ ] = WeightSlicedClatStreamingCursorFrame {
				bit_index - 1,
				next_input_mask_x,
				next_input_mask_y,
				z_prev,
				z_weight,
				0
			};
		}
	}

	void AddVarVarSplit8Enumerator32::reset_streaming_cursor( StreamingCursor& cursor, std::uint32_t output_mask_u, int weight_cap_requested )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.output_mask_u = output_mask_u;
		cursor.weight_cap = std::clamp( weight_cap_requested, 0, 31 );
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
				int best = 1'000'000;
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
					const int tail =
						( block_index == 3 )
							? 0
							: cursor.min_remaining_weight[ std::size_t( block_index + 1 ) ][ std::size_t( opt.next_connection_bit & 1u ) ];
					best = std::min( best, int( opt.block_weight ) + tail );
					if ( best == 0 )
						break;
				}
				cursor.min_remaining_weight[ std::size_t( block_index ) ][ std::size_t( connection_in ) ] = best;
			}
		}
	}

	bool AddVarVarSplit8Enumerator32::next_streaming_candidate( StreamingCursor& cursor, AddCandidate& out_candidate )
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
				const int local_weight = int( opt.block_weight );
				if ( local_weight > frame.remaining_weight )
				{
					frame.option_index = block_options.size();
					break;
				}

				const int next_remaining = frame.remaining_weight - local_weight;
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
		int weight_cap_requested,
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
			// Generate candidates on demand for the current output mask only. The math
			// path does not preallocate a full-table cLAT buffer and leave it idle.
			if ( enable_weight_sliced_clat && !memory_governor_in_pressure() )
			{
				bool hit_cap = false;
				scratch = generate_add_candidates_for_fixed_u( output_mask_u, weight_cap_requested, weight_sliced_clat_max_candidates, &hit_cap );
				if ( runtime_time_limit_reached() )
					return release_scratch_after_runtime_stop();
				if ( !hit_cap )
					return scratch;
			}
			// Fallback: split-8 SLR generator (fast, compact).
			scratch = generate_add_candidates_split8_slr( output_mask_u, weight_cap_requested );
			if ( runtime_time_limit_reached() )
				return release_scratch_after_runtime_stop();
			return scratch;
		}

		if ( g_disable_linear_tls_caches.load( std::memory_order_relaxed ) )
		{
			cache.by_output_mask_u.clear();
			cache.scratch_index = ( cache.scratch_index + 1u ) & 3u;
			auto& scratch = cache.scratch_ring[ std::size_t( cache.scratch_index ) ];
			scratch = generate_add_candidates_for_fixed_u( output_mask_u, weight_cap_requested );
			return scratch;
		}

		if ( auto it = cache.by_output_mask_u.find( output_mask_u ); it != cache.by_output_mask_u.end() )
			return it->second;

		if ( cache.by_output_mask_u.size() >= kMaxCandidateCacheEntries )
			cache.by_output_mask_u.clear();

		std::vector<AddCandidate> generated;
		#if AUTO_SEARCH_LINEAR_ENABLE_VARVAR_ADD_SPLIT8_SLR
		generated = generate_add_candidates_split8_slr( output_mask_u, 31 );
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
		const std::uint16_t cache_key = std::uint16_t( std::uint16_t( u_byte ) << 2 ) | std::uint16_t( ( connection_bit_in & 1 ) << 1 ) | std::uint16_t( exclude_top_z31_weight ? 1 : 0 );
		static thread_local std::unordered_map<std::uint16_t, std::vector<Split8BlockOption>> cache;
		static thread_local std::vector<Split8BlockOption> scratch;

		if ( g_disable_linear_tls_caches.load( std::memory_order_relaxed ) )
		{
			scratch.clear();

			struct DfsState
			{
				int bit_index = 7;  // 7..0 (MSB->LSB within the byte)
				int z_bit = 0;
				std::uint8_t input_mask_x = 0;
				std::uint8_t input_mask_y = 0;
				int weight_sum = 0;
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

				// Do NOT count z31 in the global weight (z31 is fixed to 0).
				const int weight_add = ( exclude_top_z31_weight && bit_index == 7 ) ? 0 : z;
				const int next_weight_sum = st.weight_sum + weight_add;

				auto push_next = [ & ]( int v_i, int w_i ) {
					DfsState nx = st;
					nx.bit_index = bit_index - 1;
					nx.input_mask_x = std::uint8_t( nx.input_mask_x | ( std::uint8_t( ( v_i & 1 ) << bit_index ) ) );
					nx.input_mask_y = std::uint8_t( nx.input_mask_y | ( std::uint8_t( ( w_i & 1 ) << bit_index ) ) );
					// recursion: z_{i-1} = z_i ^ u_i ^ v_i ^ w_i
					nx.z_bit = z ^ u_i ^ ( v_i & 1 ) ^ ( w_i & 1 );
					nx.weight_sum = next_weight_sum;
					stack.push_back( nx );
				};

				if ( z == 0 )
				{
					// Forced (Schulte-Geers constraints): if z_i==0 then v_i=w_i=u_i.
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
			int bit_index = 7;  // 7..0 (MSB->LSB within the byte)
			int z_bit = 0;
			std::uint8_t input_mask_x = 0;
			std::uint8_t input_mask_y = 0;
			int weight_sum = 0;
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

			// Do NOT count z31 in the global weight (z31 is fixed to 0).
			const int weight_add = ( exclude_top_z31_weight && bit_index == 7 ) ? 0 : z;
			const int next_weight_sum = st.weight_sum + weight_add;

			auto push_next = [ & ]( int v_i, int w_i ) {
				DfsState nx = st;
				nx.bit_index = bit_index - 1;
				nx.input_mask_x = std::uint8_t( nx.input_mask_x | ( std::uint8_t( ( v_i & 1 ) << bit_index ) ) );
				nx.input_mask_y = std::uint8_t( nx.input_mask_y | ( std::uint8_t( ( w_i & 1 ) << bit_index ) ) );
				// recursion: z_{i-1} = z_i ⊕ u_i ⊕ v_i ⊕ w_i
				nx.z_bit = z ^ u_i ^ ( v_i & 1 ) ^ ( w_i & 1 );
				nx.weight_sum = next_weight_sum;
				stack.push_back( nx );
			};

			if ( z == 0 )
			{
				// Forced (Schulte-Geers constraints): if z_i==0 then v_i=w_i=u_i.
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

	std::vector<AddCandidate> AddVarVarSplit8Enumerator32::generate_add_candidates_split8_slr( std::uint32_t output_mask_u, int weight_cap )
	{
		std::vector<AddCandidate> candidates;
		if ( weight_cap < 0 )
			return candidates;
		weight_cap = std::clamp( weight_cap, 0, 31 );

		const std::uint8_t u_bytes[ 4 ] = { std::uint8_t( output_mask_u >> 24 ), std::uint8_t( output_mask_u >> 16 ), std::uint8_t( output_mask_u >> 8 ), std::uint8_t( output_mask_u ) };

		// Lower bound DP: minimum achievable remaining weight from (block_index, connection_bit_in).
		int min_remaining_weight[ 5 ][ 2 ];
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
				int best = 1'000'000;
				const bool top_block = ( block_index == 0 );
				const auto& block_options = get_split8_block_options_for_u_byte( u_bytes[ block_index ], connection_in, top_block );
				for ( const auto& opt : block_options )
				{
					if ( runtime_time_limit_reached() )
					{
						candidates.clear();
						return candidates;
					}
					const int tail = ( block_index == 3 ) ? 0 : min_remaining_weight[ block_index + 1 ][ int( opt.next_connection_bit & 1u ) ];
					best = std::min( best, int( opt.block_weight ) + tail );
					if ( best == 0 )
						break;
				}
				min_remaining_weight[ block_index ][ connection_in ] = best;
			}
		}

		const auto enumerate = [ & ]( auto&& self,
									  int target_weight,
									  int block_index,
									  int connection_in,
									  std::uint32_t input_mask_x_acc,
									  std::uint32_t input_mask_y_acc,
									  int remaining_weight ) -> void {
			if ( runtime_time_limit_reached() )
				return;
			if ( remaining_weight < 0 )
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
			const auto& block_options = get_split8_block_options_for_u_byte( u_bytes[ block_index ], connection_in, top_block );
			const int shift = ( 3 - block_index ) * 8;

			for ( const auto& opt : block_options )
			{
				if ( runtime_time_limit_reached() )
					return;
				const int local_w = int( opt.block_weight );
				if ( local_w > remaining_weight )
					break;  // sorted by weight
				const int next_remaining = remaining_weight - local_w;
				const int next_connection = int( opt.next_connection_bit & 1u );
				if ( block_index != 3 && min_remaining_weight[ block_index + 1 ][ next_connection ] > next_remaining )
					continue;

				const std::uint32_t x2 = input_mask_x_acc | ( std::uint32_t( opt.input_mask_x_byte ) << shift );
				const std::uint32_t y2 = input_mask_y_acc | ( std::uint32_t( opt.input_mask_y_byte ) << shift );
				self( self, target_weight, block_index + 1, next_connection, x2, y2, next_remaining );
			}
		};

		for ( int target = 0; target <= weight_cap; ++target )
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
		candidates.erase( std::unique( candidates.begin(), candidates.end(), []( const AddCandidate& a, const AddCandidate& b ) { return a.input_mask_x == b.input_mask_x && a.input_mask_y == b.input_mask_y; } ), candidates.end() );
		return candidates;
	}

	void reset_subconst_streaming_cursor( SubConstStreamingCursor& cursor, std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap, int nbits )
	{
		cursor = {};
		cursor.initialized = true;
		cursor.nbits = std::clamp( nbits, 1, 32 );
		cursor.weight_cap = std::clamp( weight_cap, 0, cursor.nbits );
		const std::uint32_t mask = ( cursor.nbits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << cursor.nbits ) - 1u );
		cursor.output_mask_beta = output_mask_beta & mask;
		cursor.constant = constant & mask;
		cursor.min_abs = ( cursor.weight_cap >= cursor.nbits ) ? 1ull : ( 1ull << ( cursor.nbits - cursor.weight_cap ) );

		auto build_matrix = [ & ]( int alpha_i, int constant_i, int beta_i ) -> SubConstBitMatrix {
			SubConstBitMatrix M {};
			for ( int cin = 0; cin <= 1; ++cin )
			{
				for ( int x = 0; x <= 1; ++x )
				{
					const int cout = TwilightDream::arx_operators::carry_out_bit( x, constant_i, cin );
					const int zi = x ^ constant_i ^ cin;
					const int exponent = ( alpha_i & x ) ^ ( beta_i & zi );
					const int s = exponent ? -1 : 1;
					if ( cin == 0 && cout == 0 )
						M.m00 += s;
					else if ( cin == 0 && cout == 1 )
						M.m01 += s;
					else if ( cin == 1 && cout == 0 )
						M.m10 += s;
					else
						M.m11 += s;
				}
			}
			const int row0 = std::abs( M.m00 ) + std::abs( M.m01 );
			const int row1 = std::abs( M.m10 ) + std::abs( M.m11 );
			M.max_row_sum = static_cast<std::uint8_t>( std::max( row0, row1 ) );
			return M;
		};

		std::array<std::uint8_t, 32> max_row_sum_bit {};
		for ( int bit = 0; bit < cursor.nbits; ++bit )
		{
			if ( runtime_time_limit_reached() )
			{
				cursor.stop_due_to_limits = true;
				return;
			}
			const int constant_i = int( ( cursor.constant >> bit ) & 1u );
			const int beta_i = int( ( cursor.output_mask_beta >> bit ) & 1u );
			cursor.mats_alpha0[ std::size_t( bit ) ] = build_matrix( 0, constant_i, beta_i );
			cursor.mats_alpha1[ std::size_t( bit ) ] = build_matrix( 1, constant_i, beta_i );
			max_row_sum_bit[ std::size_t( bit ) ] =
				std::max(
					cursor.mats_alpha0[ std::size_t( bit ) ].max_row_sum,
					cursor.mats_alpha1[ std::size_t( bit ) ].max_row_sum );
		}

		cursor.max_gain_suffix[ std::size_t( cursor.nbits ) ] = 1ull;
		for ( int bit = cursor.nbits - 1; bit >= 0; --bit )
		{
			cursor.max_gain_suffix[ std::size_t( bit ) ] =
				cursor.max_gain_suffix[ std::size_t( bit + 1 ) ] * std::uint64_t( max_row_sum_bit[ std::size_t( bit ) ] );
		}

		cursor.stack_step = 1;
		cursor.stack[ 0 ] = SubConstStreamingCursorFrame {};
	}

	bool next_subconst_streaming_candidate( SubConstStreamingCursor& cursor, SubConstCandidate& out_candidate )
	{
		if ( !cursor.initialized || cursor.stop_due_to_limits )
			return false;

		while ( cursor.stack_step > 0 )
		{
			if ( runtime_time_limit_reached() )
			{
				cursor.stop_due_to_limits = true;
				return false;
			}

			SubConstStreamingCursorFrame& frame = cursor.stack[ cursor.stack_step - 1 ];
			if ( frame.state == 0 )
			{
				const std::uint64_t abs_sum = abs_i64_to_u64( frame.v0 ) + abs_i64_to_u64( frame.v1 );
				const std::uint64_t ub = abs_sum * cursor.max_gain_suffix[ std::size_t( frame.bit_index ) ];
				if ( ub < cursor.min_abs )
				{
					--cursor.stack_step;
					continue;
				}

				if ( frame.bit_index >= cursor.nbits )
				{
					const std::int64_t W = frame.v0 + frame.v1;
					if ( W != 0 )
					{
						const std::uint64_t abs_w = abs_i64_to_u64( W );
						const int msb = floor_log2_u64( abs_w );
						const int weight = std::clamp( cursor.nbits - msb, 0, cursor.nbits );
						if ( weight <= cursor.weight_cap )
						{
							out_candidate = SubConstCandidate { frame.prefix, weight };
							--cursor.stack_step;
							return true;
						}
					}
					--cursor.stack_step;
					continue;
				}

				frame.state = 1;
				const auto& M = cursor.mats_alpha0[ std::size_t( frame.bit_index ) ];
				const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
				const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
				cursor.stack[ cursor.stack_step++ ] = SubConstStreamingCursorFrame { frame.bit_index + 1, frame.prefix, out0, out1, 0 };
				continue;
			}

			if ( frame.state == 1 )
			{
				frame.state = 2;
				const auto& M = cursor.mats_alpha1[ std::size_t( frame.bit_index ) ];
				const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
				const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
				cursor.stack[ cursor.stack_step++ ] =
					SubConstStreamingCursorFrame { frame.bit_index + 1, frame.prefix | ( 1u << frame.bit_index ), out0, out1, 0 };
				continue;
			}

			--cursor.stack_step;
		}

		return false;
	}

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta_exact( std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap, int nbits )
	{
		std::vector<SubConstCandidate> candidates;
		if ( nbits <= 0 )
			return candidates;
		if ( nbits > 32 )
			nbits = 32;

		weight_cap = std::clamp( weight_cap, 0, nbits );
		const std::uint32_t mask = ( nbits >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << nbits ) - 1u );
		output_mask_beta &= mask;
		constant &= mask;

		const std::uint64_t min_abs = ( weight_cap >= nbits ) ? 1ull : ( 1ull << ( nbits - weight_cap ) );

		struct BitMatrix
		{
			int m00 = 0;
			int m01 = 0;
			int m10 = 0;
			int m11 = 0;
			std::uint8_t max_row_sum = 0;
		};

		std::array<BitMatrix, 32> mats_alpha0 {};
		std::array<BitMatrix, 32> mats_alpha1 {};
		std::array<std::uint8_t, 32> max_row_sum_bit {};

		auto build_matrix = [ & ]( int alpha_i, int constant_i, int beta_i ) -> BitMatrix {
			BitMatrix M {};
			for ( int cin = 0; cin <= 1; ++cin )
			{
				for ( int x = 0; x <= 1; ++x )
				{
					const int cout = TwilightDream::arx_operators::carry_out_bit( x, constant_i, cin );
					const int zi = x ^ constant_i ^ cin;
					const int exponent = ( alpha_i & x ) ^ ( beta_i & zi );
					const int s = exponent ? -1 : 1;
					if ( cin == 0 && cout == 0 )
						M.m00 += s;
					else if ( cin == 0 && cout == 1 )
						M.m01 += s;
					else if ( cin == 1 && cout == 0 )
						M.m10 += s;
					else
						M.m11 += s;
				}
			}
			const int row0 = std::abs( M.m00 ) + std::abs( M.m01 );
			const int row1 = std::abs( M.m10 ) + std::abs( M.m11 );
			M.max_row_sum = static_cast<std::uint8_t>( std::max( row0, row1 ) );
			return M;
		};

		for ( int bit = 0; bit < nbits; ++bit )
		{
			const int constant_i = int( ( constant >> bit ) & 1u );
			const int beta_i = int( ( output_mask_beta >> bit ) & 1u );
			mats_alpha0[ std::size_t( bit ) ] = build_matrix( 0, constant_i, beta_i );
			mats_alpha1[ std::size_t( bit ) ] = build_matrix( 1, constant_i, beta_i );
			const std::uint8_t max_row =
				std::max( mats_alpha0[ std::size_t( bit ) ].max_row_sum, mats_alpha1[ std::size_t( bit ) ].max_row_sum );
			max_row_sum_bit[ std::size_t( bit ) ] = max_row;
		}

		std::array<std::uint64_t, 33> max_gain_suffix {};
		max_gain_suffix[ std::size_t( nbits ) ] = 1ull;
		for ( int bit = nbits - 1; bit >= 0; --bit )
		{
			max_gain_suffix[ std::size_t( bit ) ] =
				max_gain_suffix[ std::size_t( bit + 1 ) ] * std::uint64_t( max_row_sum_bit[ std::size_t( bit ) ] );
		}

		std::vector<std::vector<SubConstCandidate>> buckets( std::size_t( weight_cap ) + 1u );

		struct Frame
		{
			int bit_index = 0;
			std::uint32_t prefix = 0;
			std::int64_t v0 = 1;
			std::int64_t v1 = 0;
			std::uint8_t state = 0;  // 0=enter, 1=after alpha=0, 2=done
		};

		std::array<Frame, 64> stack {};
		int stack_step = 0;
		stack[ stack_step++ ] = Frame {};

		while ( stack_step > 0 )
		{
			if ( runtime_time_limit_reached() )
			{
				candidates.clear();
				return candidates;
			}
			Frame& frame = stack[ stack_step - 1 ];

			if ( frame.state == 0 )
			{
				const std::uint64_t abs_sum = abs_i64_to_u64( frame.v0 ) + abs_i64_to_u64( frame.v1 );
				const std::uint64_t ub = abs_sum * max_gain_suffix[ std::size_t( frame.bit_index ) ];
				if ( ub < min_abs )
				{
					--stack_step;
					continue;
				}

				if ( frame.bit_index >= nbits )
				{
					const std::int64_t W = frame.v0 + frame.v1;
					if ( W != 0 )
					{
						const std::uint64_t abs_w = abs_i64_to_u64( W );
						const int msb = floor_log2_u64( abs_w );
						const int weight = std::clamp( nbits - msb, 0, nbits );
						if ( weight <= weight_cap )
						{
							buckets[ std::size_t( weight ) ].push_back( SubConstCandidate { frame.prefix, weight } );
						}
					}
					--stack_step;
					continue;
				}

				frame.state = 1;
				{
					const auto& M = mats_alpha0[ std::size_t( frame.bit_index ) ];
					const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
					const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
					stack[ stack_step++ ] = Frame { frame.bit_index + 1, frame.prefix, out0, out1, 0 };
				}
				continue;
			}

			if ( frame.state == 1 )
			{
				frame.state = 2;
				{
					const auto& M = mats_alpha1[ std::size_t( frame.bit_index ) ];
					const std::int64_t out0 = frame.v0 * std::int64_t( M.m00 ) + frame.v1 * std::int64_t( M.m10 );
					const std::int64_t out1 = frame.v0 * std::int64_t( M.m01 ) + frame.v1 * std::int64_t( M.m11 );
					const std::uint32_t next_prefix = frame.prefix | ( 1u << frame.bit_index );
					stack[ stack_step++ ] = Frame { frame.bit_index + 1, next_prefix, out0, out1, 0 };
				}
				continue;
			}

			--stack_step;
		}

		if ( runtime_time_limit_reached() )
		{
			candidates.clear();
			return candidates;
		}

		std::size_t total_count = 0;
		for ( auto& b : buckets )
		{
			std::sort( b.begin(), b.end(), []( const SubConstCandidate& a, const SubConstCandidate& b ) {
				return a.input_mask_on_x < b.input_mask_on_x;
			} );
			total_count += b.size();
		}

		candidates.reserve( std::min<std::size_t>( total_count, 256u ) );
		for ( int w = 0; w <= weight_cap; ++w )
		{
			const auto& b = buckets[ std::size_t( w ) ];
			candidates.insert( candidates.end(), b.begin(), b.end() );
		}

		return candidates;
	}

	std::vector<SubConstCandidate> generate_subconst_candidates_for_fixed_beta( std::uint32_t output_mask_beta, std::uint32_t constant, int weight_cap )
	{
		return generate_subconst_candidates_for_fixed_beta_exact( output_mask_beta, constant, weight_cap );
	}
}  // namespace TwilightDream::auto_search_linear
