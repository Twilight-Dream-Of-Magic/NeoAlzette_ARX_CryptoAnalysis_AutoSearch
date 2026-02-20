#include "auto_search_frame/detail/differential_best_search_math.hpp"
#include "auto_search_frame/detail/differential_best_search_types.hpp"

namespace TwilightDream::auto_search_differential
{
	namespace
	{
		enum class DifferentialWeightShellReadyStatus : std::uint8_t
		{
			Ready = 0,
			EmptyShell = 1,
			StopDueToLimits = 2
		};

		bool enumerate_add_shell_exact_collect(
			DifferentialBestSearchContext& context,
			std::uint32_t alpha,
			std::uint32_t beta,
			std::uint32_t output_hint,
			SearchWeight target_weight,
			int word_bits,
			std::vector<std::uint32_t>& out_shell,
			bool& stop_due_to_limits )
		{
			out_shell.clear();
			stop_due_to_limits = false;

			if ( word_bits < 1 )
				return true;
			if ( word_bits > 32 )
				word_bits = 32;
			const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
			alpha &= mask;
			beta &= mask;
			output_hint &= mask;

			struct Frame
			{
				int           bit_position = 0;
				std::uint32_t prefix = 0;
				std::uint32_t prefer = 0;
				std::uint8_t  state = 0;
			};

			static constexpr int MAX_STACK = 33;
			std::array<Frame, MAX_STACK> stack {};
			int                          stack_step = 0;
			stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };

			while ( stack_step > 0 )
			{
				Frame& frame = stack[ stack_step - 1 ];

				if ( frame.state == 0 )
				{
					if ( differential_note_runtime_node_visit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress( context, -1 );

					if ( frame.bit_position > 0 )
					{
						const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
						if ( w_prefix < 0 || static_cast<SearchWeight>( w_prefix ) > target_weight )
						{
							--stack_step;
							continue;
						}
						const int remaining_max = word_bits - frame.bit_position;
						if ( static_cast<SearchWeight>( w_prefix + remaining_max ) < target_weight )
						{
							--stack_step;
							continue;
						}
						if ( frame.bit_position == word_bits )
						{
							if ( static_cast<SearchWeight>( w_prefix ) == target_weight )
								out_shell.push_back( frame.prefix );
							--stack_step;
							continue;
						}
					}

					frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
					continue;
				}

				if ( frame.state == 1 )
				{
					if ( differential_runtime_node_limit_hit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}

					frame.state = 2;
					const std::uint32_t other = 1u - frame.prefer;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
					continue;
				}

				--stack_step;
			}

			return true;
		}

		std::uint64_t binom_u64( int n, int k ) noexcept
		{
			if ( k < 0 || k > n )
				return 0;
			k = std::min( k, n - k );
			long double acc = 1.0L;
			for ( int i = 1; i <= k; ++i )
			{
				acc = acc * static_cast<long double>( n - k + i ) / static_cast<long double>( i );
				if ( acc > static_cast<long double>( std::numeric_limits<std::uint64_t>::max() ) )
					return std::numeric_limits<std::uint64_t>::max();
			}
			const long double rounded = std::floor( acc + 0.5L );
			if ( rounded > static_cast<long double>( std::numeric_limits<std::uint64_t>::max() ) )
				return std::numeric_limits<std::uint64_t>::max();
			return static_cast<std::uint64_t>( rounded );
		}

		bool modular_addition_enumerator_target_weight_uses_weight_sliced_pddt(
			const DifferentialBestSearchConfiguration& configuration,
			const ModularAdditionEnumerator& enumerator ) noexcept
		{
			const auto contract = differential_varvar_add_q2_contract( configuration );
			return contract.accelerator_enabled &&
			       enumerator.target_weight <= configuration.weight_sliced_pddt_max_weight;
		}

		DifferentialWeightShellReadyStatus ensure_modular_addition_enumerator_weight_shell_ready(
			const DifferentialBestSearchConfiguration& configuration,
			ModularAdditionEnumerator& enumerator,
			DifferentialBestSearchContext* exact_context )
		{
			if ( !enumerator.shell_cache.empty() )
				return DifferentialWeightShellReadyStatus::Ready;

			const bool use_weight_sliced_pddt =
				modular_addition_enumerator_target_weight_uses_weight_sliced_pddt( configuration, enumerator );
			bool cache_hit = false;
			if ( use_weight_sliced_pddt &&
			     g_weight_shell_cache.try_get_shell(
				     enumerator.alpha,
				     enumerator.beta,
				     enumerator.output_hint,
				     enumerator.target_weight,
				     enumerator.word_bits,
				     enumerator.shell_cache ) )
			{
				cache_hit = true;
				return enumerator.shell_cache.empty()
					? DifferentialWeightShellReadyStatus::EmptyShell
					: DifferentialWeightShellReadyStatus::Ready;
			}

			DifferentialBestSearchContext rebuild_context {};
			DifferentialBestSearchContext& exact_run_context =
				exact_context ? *exact_context : rebuild_context;
			if ( !exact_context )
				rebuild_context.configuration = configuration;

			bool stop_due_to_limits = false;
			if ( !enumerate_add_shell_exact_collect(
				     exact_run_context,
				     enumerator.alpha,
				     enumerator.beta,
				     enumerator.output_hint,
				     enumerator.target_weight,
				     enumerator.word_bits,
				     enumerator.shell_cache,
				     stop_due_to_limits ) )
			{
				return DifferentialWeightShellReadyStatus::StopDueToLimits;
			}

			if ( stop_due_to_limits )
				return DifferentialWeightShellReadyStatus::StopDueToLimits;

			if ( use_weight_sliced_pddt && !cache_hit )
			{
				g_weight_shell_cache.maybe_put_shell(
					enumerator.alpha,
					enumerator.beta,
					enumerator.output_hint,
					enumerator.target_weight,
					enumerator.word_bits,
					enumerator.shell_cache );
			}

			return enumerator.shell_cache.empty()
				? DifferentialWeightShellReadyStatus::EmptyShell
				: DifferentialWeightShellReadyStatus::Ready;
		}
	}  // namespace

	WeightSlicedPddtCache g_weight_sliced_pddt_cache {};

	void WeightSlicedPddtCache::configure( bool enable, SearchWeight max_weight )
	{
		std::scoped_lock lk( mutex_ );
		enabled_ = enable;
		max_weight_ = std::min<SearchWeight>( max_weight, SearchWeight( 31 ) );
		if ( !enabled_ )
			map_.clear();
	}

	bool WeightSlicedPddtCache::enabled() const
	{
		std::scoped_lock lk( mutex_ );
		return enabled_;
	}

	SearchWeight WeightSlicedPddtCache::max_weight() const
	{
		std::scoped_lock lk( mutex_ );
		return max_weight_;
	}

	bool WeightSlicedPddtCache::try_get_shell(
		std::uint32_t alpha,
		std::uint32_t beta,
		std::uint32_t output_hint,
		SearchWeight weight,
		int word_bits,
		std::vector<std::uint32_t>& out )
	{
		if ( weight > SearchWeight( std::numeric_limits<std::uint8_t>::max() ) )
			return false;
		const WeightSlicedPddtKey key {
			alpha,
			beta,
			output_hint,
			static_cast<std::uint8_t>( weight ),
			static_cast<std::uint8_t>( word_bits )
		};
		std::scoped_lock lk( mutex_ );
		if ( !enabled_ )
			return false;
		auto it = map_.find( key );
		if ( it == map_.end() )
			return false;
		const WeightSlicedPddtShell& shell = it->second;
		if ( physical_memory_allocation_guard_active() )
			return false;
		out.clear();
		out.reserve( shell.size );
		for ( std::uint32_t i = 0; i < shell.size; ++i )
			out.push_back( shell.data[ i ] );
		return true;
	}

	void WeightSlicedPddtCache::maybe_put_shell(
		std::uint32_t alpha,
		std::uint32_t beta,
		std::uint32_t output_hint,
		SearchWeight weight,
		int word_bits,
		const std::vector<std::uint32_t>& shell )
	{
		if ( weight > SearchWeight( std::numeric_limits<std::uint8_t>::max() ) )
			return;
		if ( runtime_component::memory_governor_in_pressure() )
			return;
		const WeightSlicedPddtKey key {
			alpha,
			beta,
			output_hint,
			static_cast<std::uint8_t>( weight ),
			static_cast<std::uint8_t>( word_bits )
		};
		std::scoped_lock lk( mutex_ );
		if ( !enabled_ )
			return;
		if ( map_.find( key ) != map_.end() )
			return;

		if ( shell.empty() )
		{
			map_.emplace( key, WeightSlicedPddtShell { nullptr, 0 } );
			return;
		}

		const std::uint64_t bytes = static_cast<std::uint64_t>( shell.size() ) * sizeof( std::uint32_t );
		void* p = runtime_component::alloc_rebuildable( bytes, false );
		if ( !p )
			return;
		std::uint32_t* dst = static_cast<std::uint32_t*>( p );
		for ( std::size_t i = 0; i < shell.size(); ++i )
			dst[ i ] = shell[ i ];
		map_.emplace( key, WeightSlicedPddtShell { dst, static_cast<std::uint32_t>( shell.size() ) } );
	}

	void WeightSlicedPddtCache::clear_and_disable( const char* /*reason*/ )
	{
		std::scoped_lock lk( mutex_ );
		map_.clear();
		enabled_ = false;
	}

	void WeightSlicedPddtCache::clear_keep_enabled( const char* /*reason*/ )
	{
		std::scoped_lock lk( mutex_ );
		map_.clear();
	}

	std::uint64_t estimate_weight_sliced_pddt_bytes( SearchWeight weight, int word_bits ) noexcept
	{
		if ( word_bits < 2 )
			return 0;
		const int n = word_bits - 1;
		const int bounded_weight = static_cast<int>( std::min<SearchWeight>( weight, static_cast<SearchWeight>( n ) ) );
		const std::uint64_t comb = binom_u64( n, bounded_weight );
		const std::uint64_t bytes = comb * sizeof( std::uint32_t );
		return bytes ? bytes : sizeof( std::uint32_t );
	}

	SearchWeight compute_weight_sliced_pddt_max_weight_from_budget( std::uint64_t budget_bytes, int word_bits ) noexcept
	{
		const int max_w = std::clamp( word_bits - 1, 0, 31 );
		if ( max_w <= 0 )
			return SearchWeight( 0 );
		if ( budget_bytes == 0 )
			return static_cast<SearchWeight>( std::min( 10, max_w ) );

		auto estimator = [ & ]( double threshold ) -> std::uint64_t {
			const int t = std::clamp( static_cast<int>( std::llround( threshold ) ), 0, max_w );
			const int w = max_w - t;
			return estimate_weight_sliced_pddt_bytes( w, word_bits );
		};

		const double threshold = TwilightDream::runtime_component::pddt_select_threshold_for_budget(
			estimator,
			0.0,
			double( max_w ),
			budget_bytes,
			24 );
		const int t = std::clamp( static_cast<int>( std::llround( threshold ) ), 0, max_w );
		return static_cast<SearchWeight>( std::clamp( max_w - t, 0, max_w ) );
	}

	void configure_weight_sliced_pddt_cache_for_run(
		DifferentialBestSearchConfiguration& configuration,
		std::uint64_t rebuildable_budget_bytes ) noexcept
	{
		if ( !configuration.enable_weight_sliced_pddt )
		{
			configuration.weight_sliced_pddt_max_weight = INFINITE_WEIGHT;
			g_weight_sliced_pddt_cache.configure( false, 0 );
			return;
		}

		SearchWeight max_weight = configuration.weight_sliced_pddt_max_weight;
		if ( max_weight >= INFINITE_WEIGHT )
			max_weight = compute_weight_sliced_pddt_max_weight_from_budget( rebuildable_budget_bytes, 32 );

		max_weight = std::min<SearchWeight>( max_weight, SearchWeight( 31 ) );
		configuration.weight_sliced_pddt_max_weight = max_weight;
		g_weight_sliced_pddt_cache.configure( true, max_weight );
	}

	bool rebuild_modular_addition_enumerator_shell_cache(
		const DifferentialBestSearchConfiguration& configuration,
		ModularAdditionEnumerator& enumerator )
	{
		if ( !enumerator.initialized || !enumerator.using_cached_shell )
			return true;
		if ( !enumerator.shell_cache.empty() )
			return enumerator.shell_index <= enumerator.shell_cache.size();
		if ( enumerator.target_weight >= INFINITE_WEIGHT || enumerator.target_weight > enumerator.weight_cap )
			return false;
		const auto shell_status =
			ensure_modular_addition_enumerator_weight_shell_ready(
				configuration,
				enumerator,
				nullptr );
		return shell_status == DifferentialWeightShellReadyStatus::Ready &&
		       enumerator.shell_index <= enumerator.shell_cache.size();
	}

	void ModularAdditionEnumerator::reset(
		std::uint32_t a,
		std::uint32_t b,
		std::uint32_t hint,
		SearchWeight cap,
		int bit_position,
		std::uint32_t prefix,
		int bits )
	{
		initialized = true;
		stop_due_to_limits = false;
		dfs_active = false;
		using_cached_shell = false;
		alpha = a;
		beta = b;
		word_bits = std::clamp( bits, 1, 32 );
		const std::uint32_t mask = ( word_bits == 32 ) ? 0xFFFFFFFFu : ( ( 1u << word_bits ) - 1u );
		alpha &= mask;
		beta &= mask;
		output_hint = hint & mask;
		weight_cap = std::min<SearchWeight>( cap, static_cast<SearchWeight>( word_bits - 1 ) );
		target_weight = 0;
		shell_index = 0;
		shell_cache.clear();
		stack_step = 0;
		stack[ stack_step++ ] = Frame { bit_position, prefix, 0u, 0 };
	}

	bool ModularAdditionEnumerator::next(
		DifferentialBestSearchContext& context,
		std::uint32_t& out_gamma,
		SearchWeight& out_weight )
	{
		if ( !initialized || stop_due_to_limits )
			return false;

		while ( true )
		{
			if ( target_weight > weight_cap )
				return false;

			if ( using_cached_shell ||
			     modular_addition_enumerator_target_weight_uses_weight_sliced_pddt( context.configuration, *this ) )
			{
				const auto shell_status =
					ensure_modular_addition_enumerator_weight_shell_ready(
						context.configuration,
						*this,
						&context );
				if ( shell_status == DifferentialWeightShellReadyStatus::StopDueToLimits )
				{
					stop_due_to_limits = true;
					return false;
				}
				if ( shell_status == DifferentialWeightShellReadyStatus::EmptyShell )
				{
					using_cached_shell = false;
					shell_cache.clear();
					shell_index = 0;
					++target_weight;
					continue;
				}

				using_cached_shell = true;
				if ( shell_index < shell_cache.size() )
				{
					out_gamma = shell_cache[ shell_index++ ];
					out_weight = target_weight;
					return true;
				}

				using_cached_shell = false;
				shell_cache.clear();
				shell_index = 0;
				++target_weight;
				continue;
			}

			if ( !dfs_active )
			{
				dfs_active = true;
				stack_step = 0;
				stack[ stack_step++ ] = Frame { 0, 0u, 0u, 0 };
			}

			while ( stack_step > 0 )
			{
				Frame& frame = stack[ stack_step - 1 ];

				if ( frame.state == 0 )
				{
					if ( differential_note_runtime_node_visit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}
					maybe_print_single_run_progress( context, -1 );

					if ( frame.bit_position > 0 )
					{
						const int w_prefix = xdp_add_lm2001_n( alpha, beta, frame.prefix, frame.bit_position );
						if ( w_prefix < 0 || static_cast<SearchWeight>( w_prefix ) > target_weight )
						{
							--stack_step;
							continue;
						}
						const int remaining_max = word_bits - frame.bit_position;
						if ( static_cast<SearchWeight>( w_prefix + remaining_max ) < target_weight )
						{
							--stack_step;
							continue;
						}
						if ( frame.bit_position == word_bits )
						{
							if ( static_cast<SearchWeight>( w_prefix ) == target_weight )
							{
								out_gamma = frame.prefix;
								out_weight = static_cast<SearchWeight>( w_prefix );
								--stack_step;
								return true;
							}
							--stack_step;
							continue;
						}
					}

					frame.prefer = ( output_hint >> frame.bit_position ) & 1u;
					frame.state = 1;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( frame.prefer << frame.bit_position ), 0u, 0 };
					continue;
				}

				if ( frame.state == 1 )
				{
					if ( differential_runtime_node_limit_hit( context ) )
					{
						stop_due_to_limits = true;
						return false;
					}

					frame.state = 2;
					const std::uint32_t other = 1u - frame.prefer;
					stack[ stack_step++ ] = Frame { frame.bit_position + 1, frame.prefix | ( other << frame.bit_position ), 0u, 0 };
					continue;
				}

				--stack_step;
			}

			dfs_active = false;
			++target_weight;
		}
	}
}  // namespace TwilightDream::auto_search_differential
