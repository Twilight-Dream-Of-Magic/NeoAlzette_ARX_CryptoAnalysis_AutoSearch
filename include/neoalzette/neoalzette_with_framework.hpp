/**
 * @file neoalzette_with_framework.hpp
 * @brief NeoAlzette與ARX框架的完整集成
 *
 * 本文件展示如何將NeoAlzette應用到完整的ARX自動化搜索框架：
 *
 * 【底層ARX算子】→【NeoAlzette模型】→【搜索框架】→【MEDCP/MELCC】
 *
 */

#pragma once

#include <queue>
#include <vector>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <tuple>
#include "neoalzette/neoalzette_core.hpp"
#include "neoalzette/neoalzette_differential_step.hpp"	// diff_one_round_analysis
#include "neoalzette/neoalzette_linear_step.hpp"		//linear_one_round_backward_analysis

//NeoAlzette算法黑盒 自带ARX分析的线性和差分
using TwilightDream::diff_one_round_analysis;
using TwilightDream::linear_one_round_backward_analysis;

// 下列兩個頭僅在示例中提及，實際構建未必需要
// #include "neoalzette/neoalzette_medcp.hpp"
// #include "neoalzette/neoalzette_melcc.hpp"

// 底層ARX算子
#include "arx_analysis_operators/differential_xdp_add.hpp"
#include "arx_analysis_operators/differential_addconst.hpp"
#include "arx_analysis_operators/linear_correlation_add_logn.hpp"
#include "arx_analysis_operators/linear_correlation_addconst.hpp"

// 搜索框架
#include "arx_search_framework/pddt/pddt_algorithm1.hpp"
#include "arx_search_framework/matsui/matsui_algorithm2.hpp"
#include "arx_search_framework/clat/clat_builder.hpp"

namespace TwilightDream
{
	/**
	 * @file neoalzette_medcp_analyzer.hpp
	 * @brief NeoAlzette專用的MEDCP（最大期望差分特征概率）分析器
	 * Maximum Expected Differential Characteristic Probability
	 *
	 * 本分析器：
	 * 1. 使用NeoAlzetteDifferentialModel的精確單輪模型
	 * 2. 應用論文的Highway/Country Roads搜索策略
	 * 3. 計算多輪NeoAlzette的MEDCP
	 *
	 * 區別於通用MEDCPAnalyzer：
	 * - 專門處理NeoAlzette的複雜操作（模加變量XOR、模減常量、線性層、交叉分支）
	 * - 使用bit-vector論文的模加常量模型
	 * - 考慮NeoAlzette的SPN結構（不是Feistel）
	 */


	/**
	  * @file neoalzette_melcc_analyzer.hpp
	  * @brief NeoAlzette專用的MELCC（最大期望線性特征相關性）分析器
	  * Maximum Expected Linear Characteristic Correlation
	  *
	  * 本分析器：
	  * 1. 使用NeoAlzetteLinearModel的精確線性模型
	  * 2. 應用Wallén論文的線性枚舉方法
	  * 3. 使用矩陣乘法鏈精確計算MELCC（基於MIQCP論文）
	  *
	  * 區別於通用線性分析：
	  * - 專門處理NeoAlzette的線性層和交叉分支
	  * - 使用2×2相關性矩陣表示
	  * - 精確的矩陣乘法鏈計算
	  */


	/**
	   * @brief NeoAlzette完整分析管線
	   *
	   * 展示如何將所有組件連接到一起
	   */
	struct DiffStep
	{
		std::uint32_t dA_out {};
		std::uint32_t dB_out {};
		int			  weight { -1 };  // if <0 → invalid
	};

	class NeoAlzetteFullPipeline
	{
	public:
		/**
		 * @brief 差分分析完整流程：底層算子 → 模型 → 搜索 → MEDCP
		 * 
		 * 流程：
		 * 1. 底層差分算子計算單步差分權重
		 * 2. NeoAlzetteDifferentialModel組合成單輪模型(边运行边ARX 算子分析的Black box function)
		 * 3. pDDT構建預計算表
		 * 4. Matsui Algorithm 2搜索最優軌道
		 * 5. 計算MEDCP
		 */
		struct DifferentialPipeline
		{
			// pDDT entry (Highways)
			struct PDDTEntry
			{
				std::uint32_t dA_in;
				std::uint32_t dB_in;
				std::uint32_t dA_out;
				std::uint32_t dB_out;
				int			  weight;  // one-round weight
			};
			using PDDTBucket = std::vector<PDDTEntry>;
			using PDDTMap = std::unordered_map<std::uint64_t, PDDTBucket>;

			struct Config
			{
				int			  rounds = 4;		// 搜索轮数 / number of rounds
				int			  weight_cap = 30;	// 权重剪枝 / prune when ≥ cap
				std::uint32_t start_dA = 0x00000001u;
				std::uint32_t start_dB = 0x00000000u;

				// Step3: pDDT construction policy (阈值化构建)
				bool precompute_pddt = true;  // 预构建 / build upfront
				int	 pddt_seed_stride = 8;	  // 单比特种子步长 / seed granularity
				// 优先级：先 weight_threshold，后 prob_threshold；都未设则全收
				int	   pddt_weight_threshold = -1;	 // ≤ w 进入 pDDT；-1 关闭
				double pddt_prob_threshold = 0.0;	 // 2^{-w} ≥ τ 进入 pDDT；0 关闭
				int	   pddt_capacity_per_input = 8;	 // 每输入桶容量上限 / per-input cap
				bool   pddt_enrich_on_miss = true;	 // 查询 miss 时动态补表

				// Step4: Search parameters
				int	 max_branch_per_node = 1;  // 每节点最大分支数 / fanout cap
				bool use_canonical = true;	   // 旋转正则化 / rotational canonicalization
				bool use_highway_lb = true;	   // 权重下界剪枝 / global one-round LB
				bool verbose = false;

				// Matsui-2 参数 / Matsui-2 params for probability-threshold search
				double				initial_estimate = 0.0;	 // 初始门槛 Bn；0=禁用阈值剪枝
				std::vector<double> best_probs;				 // 每轮最佳估计 B̂_i；若空则用单轮最佳填充
			};

			struct SearchStats
			{
				std::uint64_t nodes = 0, pruned = 0;
			};
			struct Result
			{
				int			best_weight = std::numeric_limits<int>::max();
				double		medcp = 0.0;  // MEDCP = 2^{-best_weight}
				SearchStats stats {};
				PDDTMap		pddt;  // 返回 pDDT 以便检查 / expose pDDT for inspection
			};

			// ────────── helpers ──────────
			static inline std::uint64_t key( std::uint32_t a, std::uint32_t b )
			{
				return ( std::uint64_t( a ) << 32 ) | b;
			}
			static inline std::pair<std::uint32_t, std::uint32_t> canonical_rotate_pair( std::uint32_t a, std::uint32_t b )
			{
				std::uint32_t best_a = a, best_b = b;
				for ( int r = 0; r < 32; ++r )
				{
					auto ra = NeoAlzetteCore::rotl<std::uint32_t>( a, r );
					auto rb = NeoAlzetteCore::rotl<std::uint32_t>( b, r );
					if ( std::make_pair( ra, rb ) < std::make_pair( best_a, best_b ) )
					{
						best_a = ra;
						best_b = rb;
					}
				}
				return { best_a, best_b };
			}
			static inline bool pass_threshold( const Config& cfg, int weight )
			{
				if ( cfg.pddt_weight_threshold >= 0 )
					return weight <= cfg.pddt_weight_threshold;
				if ( cfg.pddt_prob_threshold > 0.0 )
				{
					double w_max = -std::log2( cfg.pddt_prob_threshold );
					return double( weight ) <= w_max + 1e-12;
				}
				return true;
			}

			// ────────── Step 3: build pDDT (阈值 + 容量) ──────────
			static void build_pddt( const Config& cfg, PDDTMap& pddt )
			{
				pddt.clear();
				std::vector<std::pair<std::uint32_t, std::uint32_t>> seeds;
				const int											 stride = std::max( 1, cfg.pddt_seed_stride );
				for ( int i = 0; i < 32; i += stride )
				{
					seeds.emplace_back( 1u << i, 0u );
					seeds.emplace_back( 0u, 1u << i );
				}
				seeds.emplace_back( 0u, 0u );
				seeds.emplace_back( 1u, 1u );

				for ( auto [ dA, dB ] : seeds )
				{
					auto can = canonical_rotate_pair( dA, dB );

					TwilightDream::MatsuiSearchNeed need0 {};  // 默认不记录 trace（性能）
					TwilightDream::PDDTNeed			need1 {};
					need1.capture_edge = true;
					need1.weight_threshold = cfg.pddt_weight_threshold;	 // ≤w 才收
					need1.prob_threshold = cfg.pddt_prob_threshold;		 // 2^{-w} ≥ τ 才收

					auto r = diff_one_round_analysis( can.first, can.second, &need0, &need1 );
					if ( r.weight < 0 )
						continue;

					if ( need1.has_edge )
					{
						PDDTEntry e { need1.edge.inA, need1.edge.inB, need1.edge.outA, need1.edge.outB, need1.edge.weight };
						auto&	  bucket = pddt[ key( can.first, can.second ) ];
						bucket.push_back( e );
						std::sort( bucket.begin(), bucket.end(), []( auto& x, auto& y ) { return x.weight < y.weight; } );
						if ( cfg.pddt_capacity_per_input > 0 && ( int )bucket.size() > cfg.pddt_capacity_per_input )
							bucket.resize( cfg.pddt_capacity_per_input );
					}
				}
			}

			// Step 3.5: query + on-the-fly enrichment (miss 时补表)
			static PDDTBucket& query_or_enrich_pddt( PDDTMap& pddt, const Config& cfg, std::uint32_t dA, std::uint32_t dB )
			{
				static PDDTBucket kEmpty;
				auto			  can = canonical_rotate_pair( dA, dB );
				auto			  k = key( can.first, can.second );
				auto			  it = pddt.find( k );
				if ( it != pddt.end() )
					return it->second;

				if ( !cfg.pddt_enrich_on_miss )
					return kEmpty;

				TwilightDream::MatsuiSearchNeed need0 {};  // 不开 trace
				TwilightDream::PDDTNeed			need1 {};
				need1.capture_edge = true;
				need1.weight_threshold = cfg.pddt_weight_threshold;
				need1.prob_threshold = cfg.pddt_prob_threshold;

				auto r = diff_one_round_analysis( can.first, can.second, &need0, &need1 );
				if ( r.weight >= 0 && need1.has_edge )
				{
					PDDTEntry e { need1.edge.inA, need1.edge.inB, need1.edge.outA, need1.edge.outB, need1.edge.weight };
					auto&	  bucket = pddt[ k ];
					bucket.push_back( e );
					std::sort( bucket.begin(), bucket.end(), []( auto& x, auto& y ) { return x.weight < y.weight; } );
					if ( cfg.pddt_capacity_per_input > 0 && ( int )bucket.size() > cfg.pddt_capacity_per_input )
						bucket.resize( cfg.pddt_capacity_per_input );
					return bucket;
				}
				return kEmpty;
			}

			// ────────── Step 4: Matsui-2 B&B (probability threshold + 动态 Bn) ──────────
			struct BBNode
			{
				int			  round;
				std::uint32_t dA, dB;
				int			  wsum;
				double		  p;
			};

			static int matsui_search_best_weight( const Config& cfg, PDDTMap& pddt, SearchStats* out_stats = nullptr )
			{
				auto cmp = []( const BBNode& a, const BBNode& b ) {
					return a.wsum > b.wsum;
				};	// 小者优先 / min by weight
				std::priority_queue<BBNode, std::vector<BBNode>, decltype( cmp )> pq( cmp );
				pq.push( BBNode { 0, cfg.start_dA, cfg.start_dB, 0, 1.0 } );

				int			  best_weight = std::numeric_limits<int>::max();
				std::uint64_t nodes = 0, pruned = 0;

				// 权重下界（来自 pDDT 的全局最小一轮权重）/ global one-round LB
				int lb_one = 1;	 // conservative default
				if ( !pddt.empty() )
				{
					int mn = std::numeric_limits<int>::max();
					for ( const auto& kv : pddt )
						for ( const auto& e : kv.second )
							mn = std::min( mn, e.weight );
					if ( mn != std::numeric_limits<int>::max() )
						lb_one = std::max( 0, mn );
				}

				// 准备 Matsui-2 的 per-round 估计与门槛 / prepare best_probs and Bn
				double				single_round_best_p = std::exp2( -lb_one );  // conservative
				std::vector<double> best_probs = cfg.best_probs;
				if ( best_probs.empty() )
					best_probs.assign( cfg.rounds, single_round_best_p );
				auto remain_prod = [ & ]( int from_round ) {
					double r = 1.0;
					for ( int i = from_round; i < cfg.rounds; ++i )
						r *= best_probs[ i ];
					return r;
				};
				double Bn = ( cfg.initial_estimate > 0.0 ) ? cfg.initial_estimate : 0.0;  // 0 disables thresholding

				// 去重 / visited cache: (round, canonical(dA,dB)) → best wsum
				struct KeyHash
				{
					std::size_t operator()( const std::tuple<int, std::uint32_t, std::uint32_t>& t ) const noexcept
					{
						return std::hash<std::uint64_t> {}( ( std::uint64_t( std::get<0>( t ) ) << 48 ) | ( std::uint64_t( std::get<1>( t ) ) << 24 ) | std::get<2>( t ) );
					}
				};
				std::unordered_map<std::tuple<int, std::uint32_t, std::uint32_t>, int, KeyHash> seen;

				while ( !pq.empty() )
				{
					BBNode cur = pq.top();
					pq.pop();
					++nodes;

					if ( cur.wsum >= best_weight )
					{
						++pruned;
						continue;
					}
					if ( cur.wsum >= cfg.weight_cap )
					{
						++pruned;
						continue;
					}

					if ( cur.round >= cfg.rounds )
					{
						// 完成 n 轮：更新最优并**提升 Bn** / complete path: update best and RAISE Bn
						if ( cur.wsum < best_weight )
							best_weight = cur.wsum;
						if ( Bn > 0.0 )
							Bn = std::max( Bn, cur.p );
						else
							Bn = cur.p;	 // dynamic promotion
						continue;
					}

					auto can = canonical_rotate_pair( cur.dA, cur.dB );
					auto keyv = std::make_tuple( cur.round, can.first, can.second );
					auto itS = seen.find( keyv );
					if ( itS != seen.end() && itS->second <= cur.wsum )
					{
						++pruned;
						continue;
					}
					seen[ keyv ] = cur.wsum;

					// 权重下界剪枝 / weight-based LB
					if ( cfg.use_highway_lb )
					{
						int remain = cfg.rounds - cur.round;
						int lb = remain * lb_one;
						if ( cur.wsum + lb >= std::min( best_weight, cfg.weight_cap ) )
						{
							++pruned;
							continue;
						}
					}

					// Matsui-2 概率阈值剪枝 / probability-threshold pruning
					if ( Bn > 0.0 )
					{
						double est = cur.p * remain_prod( cur.round );
						if ( est < Bn )
						{
							++pruned;
							continue;
						}
					}

					// Prefer pDDT bucket; fallback to black-box
					auto& bucket = query_or_enrich_pddt( pddt, cfg, cur.dA, cur.dB );
					if ( !bucket.empty() )
					{
						int branched = 0;
						for ( const auto& e : bucket )
						{
							int next_w = cur.wsum + e.weight;
							if ( next_w >= cfg.weight_cap )
							{
								++pruned;
								continue;
							}
							double p_r = std::exp2( -e.weight );
							double p_next = cur.p * p_r;
							if ( Bn > 0.0 )
							{
								double est = p_next * remain_prod( cur.round + 1 );
								if ( est < Bn )
								{
									++pruned;
									continue;
								}
							}
							pq.push( BBNode { cur.round + 1, e.dA_out, e.dB_out, next_w, p_next } );
							if ( ++branched >= std::max( 1, cfg.max_branch_per_node ) )
								break;
						}
					}
					else
					{
						// —— 现在：用 need 收 & 塞 —— //
						TwilightDream::MatsuiSearchNeed need0 {};
						TwilightDream::PDDTNeed			need1 {};
						need1.capture_edge = cfg.pddt_enrich_on_miss;  // 与策略一致
						need1.weight_threshold = cfg.pddt_weight_threshold;
						need1.prob_threshold = cfg.pddt_prob_threshold;

						// 可选：只在 verbose 或“潜在成为最优”时开 trace
						need0.record_trace = cfg.verbose;

						auto r = diff_one_round_analysis( cur.dA, cur.dB, &need0, &need1 );
						if ( r.weight < 0 )
						{
							++pruned;
							continue;
						}

						// 即时塞回 pDDT（可选：与上面 enrich_on_miss 一致）
						if ( need1.has_edge )
						{
							auto&	  bucket = pddt[ key( canonical_rotate_pair( cur.dA, cur.dB ).first, canonical_rotate_pair( cur.dA, cur.dB ).second ) ];
							PDDTEntry e { need1.edge.inA, need1.edge.inB, need1.edge.outA, need1.edge.outB, need1.edge.weight };
							bucket.push_back( e );
							std::sort( bucket.begin(), bucket.end(), []( auto& x, auto& y ) { return x.weight < y.weight; } );
							if ( cfg.pddt_capacity_per_input > 0 && ( int )bucket.size() > cfg.pddt_capacity_per_input )
								bucket.resize( cfg.pddt_capacity_per_input );
						}

						int next_w = cur.wsum + r.weight;
						if ( next_w >= cfg.weight_cap )
						{
							++pruned;
							continue;
						}

						double p_r = std::exp2( -r.weight );
						double p_next = cur.p * p_r;

						if ( Bn > 0.0 )
						{
							double est = p_next * remain_prod( cur.round + 1 );
							if ( est < Bn )
							{
								++pruned;
								continue;
							}
						}
						pq.push( BBNode { cur.round + 1, r.dA_out, r.dB_out, next_w, p_next } );

						// （可选）如果 r 改写了最优，且 need0.record_trace=true，把 need0.steps[] 复制到“best-trace”里作为审计。
					}
				}

				if ( out_stats )
				{
					out_stats->nodes = nodes;
					out_stats->pruned = pruned;
				}
				return best_weight;
			}

			// ────────── Step 5: orchestrator ──────────
			static Result run( const Config& cfg_in )
			{
				Config cfg = cfg_in;
				Result out;
				if ( cfg.precompute_pddt )
					build_pddt( cfg, out.pddt );
				SearchStats stats {};
				int			wbest = matsui_search_best_weight( cfg, out.pddt, &stats );
				out.stats = stats;
				out.best_weight = wbest;
				if ( wbest != std::numeric_limits<int>::max() )
					out.medcp = std::exp2( -wbest );
				return out;
			}

			// Optional: diagnostic dump
			template <class Fn>
			static void foreach_pddt_edge( const PDDTMap& pddt, Fn&& fn )
			{
				for ( const auto& kv : pddt )
					for ( const auto& e : kv.second )
						fn( e );
			}
		};

		// ==================== Matsui-2 shell (kept) =============================
		class NeoAlzetteMatsuiAlgorithm2
		{
		public:
			struct HighwayEntry
			{
				std::uint32_t dA_in, dB_in, dA_out, dB_out;
				int			  weight;
				double		  prob;
			};
			class HighwayTable
			{
				friend class NeoAlzetteMatsuiAlgorithm2;  // 仅嵌套类互访 / nested friend only
			public:
				void clear()
				{
					table_.clear();
					min_weight_ = std::numeric_limits<int>::max();
				}
				void add( const HighwayEntry& e )
				{
					auto  k = key( canonical( e.dA_in, e.dB_in ) );
					auto& v = table_[ k ];
					v.push_back( e );
					std::sort( v.begin(), v.end(), []( auto& a, auto& b ) { return a.weight < b.weight; } );
					min_weight_ = std::min( min_weight_, e.weight );
				}
				const std::vector<HighwayEntry>& query( std::uint32_t dA, std::uint32_t dB ) const
				{
					static const std::vector<HighwayEntry> kEmpty;
					auto								   k = key( canonical( dA, dB ) );
					auto								   it = table_.find( k );
					return ( it == table_.end() ) ? kEmpty : it->second;
				}
				int global_min_weight() const
				{
					return ( min_weight_ == std::numeric_limits<int>::max() ) ? 1 : std::max( 0, min_weight_ );
				}

			private:
				static inline std::pair<std::uint32_t, std::uint32_t> canonical( std::uint32_t a, std::uint32_t b )
				{
					std::uint32_t A = a, B = b;
					for ( int r = 0; r < 32; ++r )
					{
						auto ra = NeoAlzetteCore::rotl<std::uint32_t>( a, r );
						auto rb = NeoAlzetteCore::rotl<std::uint32_t>( b, r );
						if ( std::make_pair( ra, rb ) < std::make_pair( A, B ) )
						{
							A = ra;
							B = rb;
						}
					}
					return { A, B };
				}
				static inline std::uint64_t key( std::pair<std::uint32_t, std::uint32_t> p )
				{
					return ( std::uint64_t( p.first ) << 32 ) | p.second;
				}
				std::unordered_map<std::uint64_t, std::vector<HighwayEntry>> table_ {};
				int															 min_weight_ = std::numeric_limits<int>::max();
			};

			struct Config
			{
				int			  rounds = 4;
				int			  weight_cap = 30;
				std::uint32_t start_dA = 0x00000001u;
				std::uint32_t start_dB = 0x00000000u;
				bool		  build_highways = true;
				int			  seed_stride = 8;
				// Highways thresholds & capacity
				int	   weight_threshold = -1;  // ≤ w
				double prob_threshold = 0.0;   // ≥ τ
				int	   capacity_per_input = 8;
				bool   enrich_on_miss = true;  // miss → enrich
				bool   use_canonical = true;
				bool   use_lb = true;
				int	   max_branch_per_node = 1;
				// Matsui-2 extras
				double				initial_estimate = 0.0;	 // Bn (0 disables)
				std::vector<double> best_probs;				 // per-round estimates
				bool				record_trace = false;
			};

			struct Result
			{
				int			  best_weight = std::numeric_limits<int>::max();
				double		  best_probability = 0.0, medcp = 0.0;
				std::uint64_t nodes = 0, pruned = 0;
				bool		  complete = false;
				HighwayTable  H;
			};

			static Result run( const Config& cfg_in )
			{
				Config		 cfg = cfg_in;
				HighwayTable H;
				if ( cfg.build_highways )
					build_highways( cfg, H );
				double single_round_best_p = std::exp2( -H.global_min_weight() );
				if ( cfg.best_probs.empty() )
					cfg.best_probs = std::vector<double>( cfg.rounds, single_round_best_p );
				double Bn = ( cfg.initial_estimate > 0.0 ) ? cfg.initial_estimate : 0.0;

				struct Node
				{
					int			  round;
					std::uint32_t dA, dB;
					int			  weight;
					double		  probability;
				};
				auto cmp = []( const Node& a, const Node& b ) {
					return a.weight > b.weight;
				};
				std::priority_queue<Node, std::vector<Node>, decltype( cmp )> pq( cmp );
				pq.push( Node { 0, cfg.start_dA, cfg.start_dB, 0, 1.0 } );

				Result Result;
				Result.H = H;
				struct KeyHash
				{
					std::size_t operator()( const std::tuple<int, std::uint32_t, std::uint32_t>& t ) const noexcept
					{
						return std::hash<std::uint64_t> {}( ( std::uint64_t( std::get<0>( t ) ) << 48 ) | ( std::uint64_t( std::get<1>( t ) ) << 24 ) | std::get<2>( t ) );
					}
				};
				std::unordered_map<std::tuple<int, std::uint32_t, std::uint32_t>, int, KeyHash> seen;
				const int																		lb_one = H.global_min_weight();
				auto																			remain_prod = [ & ]( int from_round ) {
					   double r = 1.0;
					   for ( int i = from_round; i < cfg.rounds; ++i )
						   r *= cfg.best_probs[ i ];
					   return r;
				};

				while ( !pq.empty() )
				{
					Node cur = pq.top();
					pq.pop();
					++Result.nodes;
					if ( cur.weight >= Result.best_weight )
					{
						++Result.pruned;
						continue;
					}
					if ( cur.weight >= cfg.weight_cap )
					{
						++Result.pruned;
						continue;
					}
					if ( cur.round >= cfg.rounds )
					{
						if ( cur.weight < Result.best_weight )
						{
							Result.best_weight = cur.weight;
							Result.best_probability = cur.probability;
							Bn = ( cfg.initial_estimate > 0.0 ) ? std::max( Bn, Result.best_probability ) : Result.best_probability;  // 提升门槛
						}
						// 动态提升 Bn / promote Bn
						if ( Bn > 0.0 )
							Bn = std::max( Bn, cur.probability );
						else
							Bn = cur.probability;
						continue;
					}

					auto can = canonical( cur.dA, cur.dB );
					auto keyv = std::make_tuple( cur.round, can.first, can.second );
					auto itS = seen.find( keyv );
					if ( itS != seen.end() && itS->second <= cur.weight )
					{
						++Result.pruned;
						continue;
					}
					seen[ keyv ] = cur.weight;

					if ( cfg.use_lb )
					{
						int remain = cfg.rounds - cur.round;
						int lb = remain * lb_one;
						if ( cur.weight + lb >= std::min( Result.best_weight, cfg.weight_cap ) )
						{
							++Result.pruned;
							continue;
						}
					}
					if ( Bn > 0.0 )
					{
						double est = cur.probability * remain_prod( cur.round );
						if ( est < Bn )
						{
							++Result.pruned;
							continue;
						}
					}

					bool		last = ( cur.round + 1 == cfg.rounds );
					const auto& bucket = Result.H.query( cur.dA, cur.dB );
					if ( !bucket.empty() )
					{
						if ( last )
						{
							const auto& e = bucket.front();
							double		pr = std::exp2( -e.weight );
							double		pt = cur.probability * pr;
							int			nw = cur.weight + e.weight;
							if ( nw < Result.best_weight )
							{
								Result.best_weight = nw;
								Result.best_probability = pt;
							}
							// 提升 Bn / promote Bn
							if ( Bn > 0.0 )
								Bn = std::max( Bn, pt );
							else
								Bn = pt;
							continue;
						}
						int branched = 0;
						for ( const auto& e : bucket )
						{
							double pr = std::exp2( -e.weight );
							double pn = cur.probability * pr;
							int	   wn = cur.weight + e.weight;
							if ( wn >= cfg.weight_cap )
							{
								++Result.pruned;
								continue;
							}
							if ( Bn > 0.0 )
							{
								double est = pn * remain_prod( cur.round + 1 );
								if ( est < Bn )
								{
									++Result.pruned;
									continue;
								}
							}
							pq.push( Node { cur.round + 1, e.dA_out, e.dB_out, wn, pn } );
							if ( ++branched >= std::max( 1, cfg.max_branch_per_node ) )
								break;
						}
					}
					else
					{
						// can = canonical(cur.dA, cur.dB) 已在上面算好、且 seen 用它去重
						TwilightDream::MatsuiSearchNeed need0 {};
						TwilightDream::PDDTNeed			need1 {};

						// 只在 record_trace 或“潜在成为最优”时开启逐步轨迹，避免常态开销
						need0.record_trace = cfg.record_trace;

						// 是否把 miss 的结果在线塞回 Highway（与 cfg.enrich_on_miss 对齐）
						need1.capture_edge = cfg.enrich_on_miss;
						need1.weight_threshold = cfg.weight_threshold;
						need1.prob_threshold = cfg.prob_threshold;

						auto step = TwilightDream::diff_one_round_analysis( can.first, can.second, &need0, &need1 );
						if ( step.weight < 0 )
						{
							++Result.pruned;
							continue;
						}

						// 在线 enrich：把这条边塞回 H，后续命中会更快
						if ( need1.has_edge )
						{
							HighwayEntry e { need1.edge.inA, need1.edge.inB, need1.edge.outA, need1.edge.outB, step.weight, std::ldexp( 1.0, -step.weight ) };
							add_with_capacity( cfg, Result.H, e );
						}

						// —— 原有 Matsui-2 扩展逻辑，保持不变 —— //
						const bool last = ( cur.round + 1 == cfg.rounds );
						double	   pr = std::exp2( -step.weight );
						double	   pn = cur.probability * pr;
						int		   wn = cur.weight + step.weight;

						if ( last )
						{
							if ( wn < Result.best_weight )
							{
								Result.best_weight = wn;
								Result.best_probability = pn;
								Bn = ( cfg.initial_estimate > 0.0 ) ? std::max( Bn, Result.best_probability ) : Result.best_probability;
							}
							// 动态提升 Bn
							if ( Bn > 0.0 )
								Bn = std::max( Bn, pn );
							else
								Bn = pn;
						}
						else
						{
							if ( wn >= cfg.weight_cap )
							{
								++Result.pruned;
							}
							else
							{
								if ( Bn > 0.0 )
								{
									double est = pn * remain_prod( cur.round + 1 );
									if ( est < Bn )
									{
										++Result.pruned;
										continue;
									}
								}
								pq.push( Node { cur.round + 1, step.dA_out, step.dB_out, wn, pn } );
							}
						}
					}
				}

				if ( Result.best_weight != std::numeric_limits<int>::max() )
					Result.medcp = std::exp2( -Result.best_weight );
				Result.complete = true;
				return Result;
			}

		private:
			static inline std::pair<std::uint32_t, std::uint32_t> canonical( std::uint32_t a, std::uint32_t b )
			{
				std::uint32_t A = a, B = b;
				for ( int r = 0; r < 32; ++r )
				{
					auto ra = NeoAlzetteCore::rotl<std::uint32_t>( a, r );
					auto rb = NeoAlzetteCore::rotl<std::uint32_t>( b, r );
					if ( std::make_pair( ra, rb ) < std::make_pair( A, B ) )
					{
						A = ra;
						B = rb;
					}
				}
				return { A, B };
			}
			static inline bool pass( const Config& cfg, int w )
			{
				if ( cfg.weight_threshold >= 0 )
					return w <= cfg.weight_threshold;
				if ( cfg.prob_threshold > 0.0 )
				{
					double wmax = -std::log2( cfg.prob_threshold );
					return double( w ) <= wmax + 1e-12;
				}
				return true;
			}
			struct HighwayTableKey
			{
				std::uint64_t v;
				HighwayTableKey( std::pair<std::uint32_t, std::uint32_t> p ) : v( ( std::uint64_t( p.first ) << 32 ) | p.second ) {}
				operator std::uint64_t() const
				{
					return v;
				}
			};
			static void add_with_capacity( const Config& cfg, HighwayTable& H, const HighwayEntry& e )
			{
				auto  k = HighwayTableKey( canonical( e.dA_in, e.dB_in ) );
				auto& vec = H.table_[ k ];
				vec.push_back( e );
				std::sort( vec.begin(), vec.end(), []( auto& x, auto& y ) { return x.weight < y.weight; } );
				if ( cfg.capacity_per_input > 0 && ( int )vec.size() > cfg.capacity_per_input )
					vec.resize( cfg.capacity_per_input );
				H.min_weight_ = std::min( H.min_weight_, e.weight );
			}
			static void build_highways( const Config& cfg, HighwayTable& H )
			{
				H.clear();

				std::vector<std::pair<std::uint32_t, std::uint32_t>> seeds;
				int													 stride = std::max( 1, cfg.seed_stride );
				for ( int i = 0; i < 32; i += stride )
				{
					seeds.emplace_back( 1u << i, 0u );
					seeds.emplace_back( 0u, 1u << i );
				}
				seeds.emplace_back( 0u, 0u );
				seeds.emplace_back( 1u, 1u );

				for ( auto [ dA, dB ] : seeds )
				{
					auto can = canonical( dA, dB );

					TwilightDream::MatsuiSearchNeed need0 {};  // 不记录 trace（预建阶段讲究速度）
					TwilightDream::PDDTNeed			need1 {};
					need1.capture_edge = true;						// 让黑盒回填一条边
					need1.weight_threshold = cfg.weight_threshold;	// ≤ w 才收
					need1.prob_threshold = cfg.prob_threshold;		// 2^{-w} ≥ τ 才收

					auto r = TwilightDream::diff_one_round_analysis( can.first, can.second, &need0, &need1 );

					if ( r.weight < 0 )
						continue;
					if ( !need1.has_edge )
						continue;  // 未满足阈值，不收

					HighwayEntry e { need1.edge.inA, need1.edge.inB, need1.edge.outA, need1.edge.outB, r.weight, std::exp2( -r.weight ) };
					add_with_capacity( cfg, H, e );	 // 原有容量裁剪逻辑
				}
			}
		};


		/**
		* @brief 線性分析完整流程：底層算子 → 模型 → cLAT → MELCC
		*
		* 流程：
		* 1. 底層線性算子計算單步相關度
		* 2. NeoAlzetteLinearModel組合成單輪模型(边运行边ARX 算子分析的Black box function)
		* 3. cLAT構建（Algorithm 2）
		* 4. SLR搜索（Algorithm 3）
		* 5. 計算MELCC
		*/
		struct LinearPipeline
		{
			struct Config
			{
				int			  rounds = 4;
				int			  weight_cap = 30;			   // 線性權重門檻（-log2 |corr|）
				std::uint32_t start_mask_A = 0x00000001u;  // 輸出端起始掩碼A（逆向）
				std::uint32_t start_mask_B = 0x00000000u;  // 輸出端起始掩碼B（逆向）
				bool		  precompute_clat = true;	   // Step3: 是否構建 cLAT
				int			  clat_m_bits = 8;			   // 分塊大小 m（默認8）
				bool		  verbose = false;
			};


			struct Node
			{
				int			  round;
				std::uint32_t mA;  // 當前輸出側掩碼（逆向步進）
				std::uint32_t mB;
				int			  accumulated_weight;  // 累計線性權重
				int			  parity;			   // 累計相位
				std::uint16_t used_rc_mask;		   // 已使用的輪常量集合
			};


			struct Result
			{
				int			  best_weight = std::numeric_limits<int>::max();
				double		  melcc = 0.0;			  // 2^{-best_weight}
				std::uint64_t nodes = 0, pruned = 0;  // 搜索統計
				bool		  complete = false;
				int			  parity = 0;		 // 最終相位（整條路徑）
				std::uint16_t used_rc_mask = 0;	 // 最終使用到的輪常量
				int			  used_count = 0;
				int			  missing_count = 0;  // 16 - used_count
			};


			static Result run( const Config& cfg )
			{
				// 固定 m=8 的 cLAT；若配置不同，仍退化使用 8（模板需編譯期常量）
                neoalz::cLAT<8> clat;
				if ( cfg.precompute_clat )
					( void )clat.build();

				// 以 cLAT 的最小塊權重推估一輪下界（作為剪枝輔助）
				int global_min_one_round = 1;  // 保守預設
				if ( cfg.precompute_clat )
				{
					int mn = std::numeric_limits<int>::max();
					for ( int v = 0; v < ( 1 << clat.m ); ++v )
						for ( int b = 0; b < 2; ++b )
							mn = std::min( mn, clat.get_min_weight( static_cast<std::uint32_t>( v ), b ) );
					if ( mn != std::numeric_limits<int>::max() )
						global_min_one_round = std::max( 0, mn );
				}

				auto cmp = []( const Node& a, const Node& b ) {
					return a.accumulated_weight > b.accumulated_weight;	 // 小權重優先
				};
				std::priority_queue<Node, std::vector<Node>, decltype( cmp )> pq( cmp );
				pq.push( Node { 0, cfg.start_mask_A, cfg.start_mask_B, 0, 0, 0 } );

				Result		  Result;
				int			  best_weight = std::numeric_limits<int>::max();
				int			  best_parity = 0;
				std::uint16_t best_used_mask = 0;

				// 去重：記錄在 (round, mA, mB) 下已見的最小權重
				struct KeyHash
				{
					std::size_t operator()( const std::tuple<int, std::uint32_t, std::uint32_t>& t ) const noexcept
					{
						return std::hash<std::uint64_t> {}( ( ( ( std::uint64_t )std::get<0>( t ) ) << 48 ) | ( ( ( std::uint64_t )std::get<1>( t ) ) << 24 ) | std::get<2>( t ) );
					}
				};
				std::unordered_map<std::tuple<int, std::uint32_t, std::uint32_t>, int, KeyHash> seen;

				while ( !pq.empty() )
				{
					Node current_node = pq.top();
					pq.pop();
					++Result.nodes;

					if ( current_node.accumulated_weight >= best_weight )
					{
						++Result.pruned;
						continue;
					}
					if ( current_node.accumulated_weight >= cfg.weight_cap )
					{
						++Result.pruned;
						continue;
					}

					if ( current_node.round >= cfg.rounds )
					{
						if ( current_node.accumulated_weight < best_weight )
						{
							best_weight = current_node.accumulated_weight;
							best_parity = current_node.parity;
							best_used_mask = current_node.used_rc_mask;
						}
						continue;
					}

					// 基於 cLAT 的簡單下界剪枝（SLR 的保守估計）
					{
						int remain = cfg.rounds - current_node.round;
						int lb = remain * global_min_one_round;
						if ( current_node.accumulated_weight + lb >= std::min( best_weight, cfg.weight_cap ) )
						{
							++Result.pruned;
							continue;
						}
					}

					// 已見檢查
					{
						auto keyv = std::make_tuple( current_node.round, current_node.mA, current_node.mB );
						auto it = seen.find( keyv );
						if ( it != seen.end() && it->second <= current_node.accumulated_weight )
						{
							++Result.pruned;
							continue;
						}
						seen[ keyv ] = current_node.accumulated_weight;
					}

					// Step4（嚴格）：cLAT 生成兩處模加的 β 候選，SLR 做乘積枚舉，黑盒用 BetaHints 精確驗證
					if ( cfg.precompute_clat )
					{
						const int t = 32 / clat.m;	// 32位 → 4塊（m=8）

						// ① 針對 B += (T(A) ^ R0) ：以 gamma = mB 做全寬重組，拿 β1 候選及其塊和權重 k1
						std::vector<std::pair<uint32_t, int>> C1;
						C1.reserve( 64 );
						clat.lookup_and_recombine(
							/*v_full=*/current_node.mB,
							/*t=*/t,
							/*weight_cap=*/cfg.weight_cap - current_node.accumulated_weight, [ & ]( uint32_t /*u*/, uint32_t beta, int k1 ) { C1.emplace_back( beta, k1 ); } );

						// ② 針對 A += (T(B) ^ R5) ：以 gamma = mA 做全寬重組，拿 β2 候選及其塊和權重 k2
						std::vector<std::pair<uint32_t, int>> C2;
						C2.reserve( 64 );
						clat.lookup_and_recombine(
							/*v_full=*/current_node.mA,
							/*t=*/t,
							/*weight_cap=*/cfg.weight_cap - current_node.accumulated_weight, [ & ]( uint32_t /*u*/, uint32_t beta, int k2 ) { C2.emplace_back( beta, k2 ); } );

						// 不可達：直接剪
						if ( C1.empty() || C2.empty() )
						{
							++Result.pruned;
							continue;
						}

						// ③ SLR：雙候選乘積，先用 k1+k2 做“本輪緊下界”剪枝，再喂黑盒
						for ( const auto& c1 : C1 )
						{
							const uint32_t beta1 = c1.first;
							const int	   k1 = c1.second;
							for ( const auto& c2 : C2 )
							{
								const uint32_t beta2 = c2.first;
								const int	   k2 = c2.second;

								// 本輪緊下界 + 余輪保守下界（仍用 global_min_one_round）
								const int		remain = cfg.rounds - current_node.round - 1;
								const long long tightened = ( long long )current_node.accumulated_weight + ( long long )( k1 + k2 ) + ( long long )remain * ( long long )global_min_one_round;

								if ( tightened >= std::min( best_weight, cfg.weight_cap ) )
								{
									++Result.pruned;
									continue;
								}

								// 用 cLAT 選出的 β1/β2 打包成 BetaHints，黑盒做一步精確回溯
								BetaHints hints {};
								hints.beta_for_B_plus_TA = beta1;  // 給 B += (T(A)^R0)
								hints.beta_for_A_plus_TB = beta2;  // 給 A += (T(B)^R5)

								auto step = linear_one_round_backward_analysis( current_node.mA, current_node.mB, &hints );
								if ( step.weight < 0 )
								{
									++Result.pruned;
									continue;
								}

								const int next_w = current_node.accumulated_weight + step.weight;
								if ( next_w >= cfg.weight_cap )
								{
									++Result.pruned;
									continue;
								}

								Node nxt { current_node.round + 1, step.a_in_mask, step.b_in_mask, next_w, current_node.parity ^ step.parity, static_cast<std::uint16_t>( current_node.used_rc_mask | step.used_rc_mask ) };

								// Matsui-style 去重
								auto keyn = std::make_tuple( nxt.round, nxt.mA, nxt.mB );
								auto itn = seen.find( keyn );
								if ( itn == seen.end() || next_w < itn->second )
								{
									pq.push( nxt );
								}
								else
								{
									++Result.pruned;
								}
							}
						}
						// 由候選集驅動，這一輪結束
						continue;
					}

					// 無 cLAT 時：给一个“退化 β”，常见做法是都取 γ；也可以全 0
					{
						BetaHints hints {};
						// 退化情況下，常見選擇：β = γ
						hints.beta_for_B_plus_TA = current_node.mB;	 // 對應 B += (T(A)^R0) 的右輸入β
						hints.beta_for_A_plus_TB = current_node.mA;	 // 對應 A += (T(B)^R5) 的右輸入β

						auto step = linear_one_round_backward_analysis( current_node.mA, current_node.mB, &hints );
						if ( step.weight < 0 )
						{
							++Result.pruned;
							continue;
						}

						int next_w = current_node.accumulated_weight + step.weight;
						if ( next_w >= cfg.weight_cap )
						{
							++Result.pruned;
							continue;
						}

						Node next_node { current_node.round + 1, step.a_in_mask, step.b_in_mask, next_w, current_node.parity ^ step.parity, static_cast<std::uint16_t>( current_node.used_rc_mask | step.used_rc_mask ) };
						pq.push( next_node );
					}
				}

				if ( best_weight != std::numeric_limits<int>::max() )
				{
					Result.best_weight = best_weight;
					Result.melcc = std::exp2( -best_weight );
					Result.parity = best_parity;
					Result.used_rc_mask = best_used_mask;
					Result.used_count = std::popcount( Result.used_rc_mask );
					Result.missing_count = 16 - Result.used_count;
				}
				Result.complete = true;
				return Result;
			}
		};

		// ======= 外殼：保持接口一致 =======
		static double run_linear_analysis( int num_rounds )
		{
			LinearPipeline::Config cfg;
			cfg.rounds = num_rounds;
			auto result = LinearPipeline::run( cfg );
			return result.melcc;
		}
		static double run_linear_analysis( const LinearPipeline::Config& cfg )
		{
			auto result = LinearPipeline::run( cfg );
			return result.melcc;
		}

		static double run_differential_analysis( int num_rounds )
		{
			DifferentialPipeline::Config cfg;
			cfg.rounds = num_rounds;
			auto result = DifferentialPipeline::run( cfg );
			return result.medcp;
		}
		static double run_differential_analysis( const DifferentialPipeline::Config& cfg )
		{
			auto result = DifferentialPipeline::run( cfg );
			return result.medcp;
		}

		/**
		 * @brief 完整分析：差分 + 線性
		 */
		static void run_full_analysis( int num_rounds )
		{
			DifferentialPipeline diff_pipeline;
			LinearPipeline		 lin_pipeline;

			// 差分分析
			double medcp = run_differential_analysis( num_rounds );

			// 線性分析
			double melcc = run_linear_analysis( num_rounds );

			// 輸出結果
			printf( "=== NeoAlzette %d輪分析結果 ===\n", num_rounds );
			printf( "MEDCP: 2^%.2f\n", -std::log2( medcp ) );
			printf( "MELCC: 2^%.2f\n", -std::log2( melcc ) );
		}
	};

}  // namespace TwilightDream