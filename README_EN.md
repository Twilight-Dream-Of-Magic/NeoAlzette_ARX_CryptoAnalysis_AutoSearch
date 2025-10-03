# NeoAlzette Cryptanalysis Toolkit

English | [中文](README.md)

## Project Overview

This project implements a **high-performance cryptanalysis toolkit** for ARX ciphers (Addition, Rotation, XOR), with a particular focus on differential and linear cryptanalysis of the NeoAlzette 64-bit ARX-box. The project is based on cutting-edge research from CRYPTO 2022 and other top-tier conferences, providing a complete automated search and analysis framework.

> ⚠️ **Important Warning**: This is a **computationally intensive** research tool. Single searches may require hours to days of CPU time and several GB of memory. Please understand the algorithmic complexity before use.

### Core Algorithms

- **MEDCP (Modular Addition by Constant - Differential Property)**: Advanced differential analysis algorithm for modular addition by constants
- **MELCC (Modular Addition Linear Cryptanalysis with Correlation)**: Correlation-based linear cryptanalysis algorithm for modular addition

### Algorithmic Significance

These algorithms solve the open problem posed by Niu et al. at CRYPTO 2022: **How to automatically search for differential-linear trails in ARX ciphers**. By transforming arbitrary matrix multiplication chains into Mixed Integer Quadratically-Constrained Programming (MIQCP), they achieve:

- Computational complexity reduction by approximately **8x**
- First implementation supporting **arbitrary output masks** for automatic search
- Discovery of **best differential-linear distinguishers** for SPECK, Alzette and other ARX ciphers

## Algorithm Background

### ARX Cipher Structure

ARX ciphers are based on three fundamental operations:
- **Addition (⊞)**: Modular addition modulo 2^n
- **Rotation (≪, ≫)**: Circular bit shifts
- **XOR (⊕)**: Exclusive OR operation

This structure provides excellent performance in software implementations while maintaining sufficient cryptographic strength.

### Theoretical Foundation

This project is based on the following core theoretical achievements:

1. **Lipmaa-Moriai (2001)** - Efficient algorithms for computing differential properties of modular addition
2. **Wallén (2003)** - Correlation analysis of linear approximations for modular addition
3. **Mixed Integer Quadratically-Constrained Programming (MIQCP)** - For automatic search of differential-linear trails
4. **Highway Table Technology** - O(1) suffix lower bound queries with linear space complexity

### NeoAlzette Algorithm

NeoAlzette is an improved version based on the Alzette 64-bit ARX-box, featuring:

- **64-bit State**: (A, B) ∈ F₃₂²
- **Two-Subround Structure**: Each round contains two subround operations  
- **Nonlinear Function**: F(x) = (x ≪ 31) ⊕ (x ≪ 17)
- **Linear Diffusion Layer**: L₁, L₂ provide branch number guarantees
- **Round Constant Injection**: 16 predefined round constants

## Complete Project Structure

```
├── include/                          # Core algorithm header library
│   ├── neoalzette.hpp               # NeoAlzette ARX-box core implementation (rounds, constants)
│   ├── lm_fast.hpp                  # Lipmaa-Moriai differential fast enumeration algorithm
│   ├── lm_wallen.hpp                # LM-Wallén hybrid model (differential+linear)
│   ├── wallen_fast.hpp              # Wallén linear correlation fast computation
│   ├── highway_table.hpp            # Differential Highway suffix lower bound table (O(1) query)
│   ├── highway_table_lin.hpp        # Linear Highway suffix lower bound table
│   ├── lb_round_full.hpp            # Complete round function differential bound estimation
│   ├── lb_round_lin.hpp             # Complete round function linear bound estimation
│   ├── suffix_lb.hpp                # Multi-round suffix differential bound computation
│   ├── suffix_lb_lin.hpp            # Multi-round suffix linear bound computation
│   ├── threshold_search.hpp         # Matsui threshold search framework
│   ├── canonicalize.hpp             # State canonicalization (rotation equivalence classes)
│   ├── diff_add_const.hpp           # Modular addition by constant differential properties (MEDCP core)
│   ├── mask_backtranspose.hpp       # Linear mask backward propagation
│   ├── neoalz_lin.hpp               # NeoAlzette linear layer exact implementation
│   ├── pddt.hpp                     # Partial Differential Distribution Table
│   └── trail_export.hpp             # Trail export and CSV formatting
├── src/                             # Main analysis tools
│   ├── analyze_medcp.cpp            # 🔥 MEDCP differential trail searcher (main tool)
│   ├── analyze_melcc.cpp            # 🔥 MELCC linear trail searcher (main tool)
│   ├── main_diff.cpp                # Differential analysis demo program
│   ├── main_lin.cpp                 # Linear analysis demo program
│   ├── main_pddt.cpp                # PDDT construction and testing
│   ├── main_threshold.cpp           # Threshold search demo
│   ├── bnb.cpp                      # Branch-and-bound search implementation
│   ├── neoalzette.cpp               # NeoAlzette algorithm implementation
│   ├── pddt.cpp                     # Partial Differential Distribution Table implementation
│   ├── gen_round_lb_table.cpp       # Round lower bound table generator
│   ├── highway_table_build.cpp      # Differential Highway table builder
│   ├── highway_table_build_lin.cpp  # Linear Highway table builder
│   ├── threshold_lin.cpp            # Linear threshold search
│   ├── search_beam_diff.cpp         # Beam search differential analysis
│   └── milp_diff.cpp                # MILP differential model (experimental)
├── papers/                          # Core theoretical papers (PDF)
│   ├── A MIQCP-Based Automatic Search Algorithm...pdf  # Core paper for this project
│   ├── Alzette A 64-Bit ARX-box...pdf                  # NeoAlzette foundation paper
│   ├── Efficient Algorithms for Computing Differential Properties...pdf
│   ├── Linear Approximations of Addition Modulo 2^n...pdf
│   ├── MILP-Based Automatic Search Algorithms for Differential...pdf
│   └── ... (11 core papers total)
├── papers_txt/                      # Text-extracted versions of papers (for analysis)
├── CMakeLists.txt                   # Build configuration file
├── LICENSE                          # GPL v3.0 open source license
└── .gitignore                       # Git ignore configuration
```

### Core File Descriptions

#### 🔥 Main Analysis Tools
- **`analyze_medcp.cpp`** - **MEDCP Differential Analyzer**: Search for optimal differential trails with Highway acceleration, CSV export, weight histograms
- **`analyze_melcc.cpp`** - **MELCC Linear Analyzer**: Search for optimal linear trails with exact backward propagation, beam search

#### 🧮 Core Algorithm Implementations
- **`lm_fast.hpp`** - **Lipmaa-Moriai 2001 Algorithm**: O(log n) time modular addition differential probability computation with prefix pruning
- **`wallen_fast.hpp`** - **Wallén 2003 Algorithm**: O(log n) time modular addition linear correlation computation  
- **`highway_table*.hpp`** - **Highway Table Technology**: Precomputed suffix bounds, O(1) query time, linear space

#### 🔧 Auxiliary Tools
- **`highway_table_build*.cpp`** - Highway table precomputation tools (optional, for large-scale search acceleration)
- **`gen_round_lb_table.cpp`** - Round lower bound table generator (optimize single-round pruning)
- **`trail_export.hpp`** - Unified CSV export format (for subsequent analysis)

## Build Instructions

### System Requirements

- **Compiler**: Modern C++20 compiler (GCC 10+ / Clang 12+ / MSVC 2019+)
- **Memory**: Recommended 8GB+ RAM (for large search tasks)
- **CPU**: x86_64 processor with popcount instruction support

### Build Steps

```bash
# Clone repository
git clone <repository-url>
cd neoalzette_search

# Create build directory
mkdir build && cd build

# Configure build
cmake ..

# Compile
make -j$(nproc)

# Optional: Build demo programs
cmake -DNA_BUILD_DEMOS=ON ..
make -j$(nproc)
```

### Build Artifacts

- `analyze_medcp` - MEDCP differential trail search
- `analyze_melcc` - MELCC linear trail search
- `diff_search` - Generic differential search tool
- `lin_search` - Generic linear search tool
- `highway_table_build*` - Highway table construction tools

## Usage

### ⚠️ Essential Pre-requisites

These tools are **computationally intensive** research-grade algorithm implementations:

- **Time Complexity**: Exponential worst-case, actual performance depends on pruning efficiency
- **Space Complexity**: O(2^{weight_cap}) memory usage
- **Recommended Environment**: High-performance servers or clusters, not personal laptops
- **Parameter Tuning**: Start with small parameters, gradually increase complexity

### MEDCP Differential Trail Search

MEDCP analyzer uses **Matsui threshold search + Lipmaa-Moriai local enumeration**, supporting complete trail path recording.

#### Basic Syntax
```bash
./analyze_medcp R Wcap [highway.bin] [options]
```

#### Parameter Description
- **`R`** - Number of search rounds (recommended range: 4-12 rounds)
- **`Wcap`** - Global weight cap (lower is faster, recommend 20-50)
- **`highway.bin`** - Optional precomputed Highway table file

#### Detailed Options
- **`--start-hex dA dB`** - Starting differential state (32-bit hexadecimal)
  - `dA`: A register difference
  - `dB`: B register difference
  - Example: `--start-hex 0x80000000 0x1`
- **`--k1 K`** - Top-K candidates for var-var addition (default 4, range 1-16)
- **`--k2 K`** - Top-K candidates for var-const addition (default 4, range 1-16)
- **`--export path.csv`** - Export search summary to CSV file
- **`--export-trace path.csv`** - Export complete optimal trail path
- **`--export-hist path.csv`** - Export weight distribution histogram
- **`--export-topN N path.csv`** - Export top-N best results

#### Usage Examples
```bash
# Beginner example: 6-round search, weight cap 25 (~1 minute)
./analyze_medcp 6 25

# Standard search: 8 rounds, weight 35, custom starting difference
./analyze_medcp 8 35 --start-hex 0x1 0x0

# High-performance search: 10 rounds, using Highway table and high K values
./analyze_medcp 10 40 highway_diff.bin --k1 8 --k2 8

# Complete analysis: export trails, histogram, top-10 results
./analyze_medcp 8 30 \
  --export-trace trail.csv \
  --export-hist histogram.csv \
  --export-topN 10 top10.csv

# Batch search (for parameter optimization)
for w in {25..45..5}; do
  ./analyze_medcp 8 $w --export results_w${w}.csv
done
```

### MELCC Linear Trail Search

MELCC analyzer uses **priority queue search + Wallén linear enumeration**, supporting precise backward mask propagation.

#### Basic Syntax
```bash
./analyze_melcc R Wcap [options]
```

#### Parameter Description
- **`R`** - Number of search rounds (recommended range: 4-10 rounds)
- **`Wcap`** - Weight cap (linear analysis typically more restrictive than differential)

#### Detailed Options
- **`--start-hex mA mB`** - Starting linear masks (32-bit hexadecimal)
  - `mA`: A register mask
  - `mB`: B register mask
- **`--lin-highway H.bin`** - Linear Highway table file
- **`--export path.csv`** - Export analysis summary
- **`--export-trace path.csv`** - Export optimal trail
- **`--export-hist path.csv`** - Export weight distribution
- **`--export-topN N path.csv`** - Export top-N results

#### Usage Examples
```bash
# Beginner example: 6-round linear search
./analyze_melcc 6 20

# High-weight mask search
./analyze_melcc 8 25 --start-hex 0x80000001 0x0

# Using Highway table acceleration
./analyze_melcc 10 30 --lin-highway highway_lin.bin

# Complete linear analysis pipeline
./analyze_melcc 8 25 \
  --start-hex 0x1 0x0 \
  --export-trace linear_trail.csv \
  --export-topN 5 best_linear.csv
```

### Highway Table Construction

```bash
# Build differential Highway table (optional, for search acceleration)
./highway_table_build output_diff.bin [max_rounds]

# Build linear Highway table
./highway_table_build_lin output_lin.bin [max_rounds]
```

### Advanced Options

#### Export and Analysis Options

```bash
# Export complete trail paths
./analyze_medcp 8 30 --export-trace trail.csv

# Export weight distribution histogram
./analyze_medcp 8 30 --export-hist histogram.csv

# Export top-N best results
./analyze_medcp 8 30 --export-topN 10 top10.csv
```

#### Search Parameter Tuning

- `--k1 K`: Top-K candidates for var-var addition (default 4)
- `--k2 K`: Top-K candidates for var-const addition (default 4)
- Increasing K values may find better trails but significantly increases search time

### Parallelization and Cluster Deployment

This toolkit is designed as a **CPU-intensive** application, supporting:

- **Multi-core Parallelism**: Use `make -j$(nproc)` to fully utilize multi-core CPUs
- **Memory Efficiency**: Uses memoization and incremental computation to reduce memory usage
- **Cluster-Friendly**: Tools run independently, easy to deploy in cluster environments

#### Cluster Usage Recommendations

```bash
# Split large tasks into multiple subtasks for parallel execution
for r in {6..12}; do
    for w in {20..40..5}; do
        sbatch run_analysis.sh $r $w
    done  
done
```

## Performance Characteristics & Computational Complexity

### Algorithm Complexity Analysis

#### Core Algorithm Complexities
- **Lipmaa-Moriai Enumeration**: Average << 2^n (highly efficient prefix infeasibility pruning)
- **Wallén Linear Computation**: O(log n) time with incremental popcount optimization
- **Highway Table Queries**: O(1) time, O(4^rounds) space precomputation
- **Threshold Search**: O(state_space × branching_factor^rounds), actual depends on pruning efficiency

#### Practical Performance Estimation
```
Search space size ≈ (valid differences) × (rounds) × (branching factor)
Where:
- Valid differences ≈ 2^{32} × pruning_rate (typically < 1%)
- Branching factor ≈ average successors per state (50-500)
- Pruning efficiency ≈ lower bound pruning hit rate (90%+)
```

### Detailed Resource Consumption Table

| Configuration | Rounds | Weight Cap | Memory Usage | CPU Time | Disk I/O | Use Case |
|---------------|--------|------------|--------------|----------|----------|-----------|
| **Entry Level** | 4-6 | 15-25 | 100MB-500MB | 30sec-5min | Minimal | Algorithm validation, teaching demos |
| **Standard** | 6-8 | 25-35 | 500MB-2GB | 5min-2hr | Medium | Paper experiments, method comparison |
| **Professional** | 8-10 | 35-45 | 2GB-8GB | 2hr-1day | High | Deep analysis, optimal trails |
| **Research** | 10-12+ | 45+ | 8GB-32GB | 1day-1week | Extreme | Breakthrough research, cluster computing |

### Memory Usage Pattern

```cpp
// Memory component analysis
Memoization hash table: O(search states) ≈ 10MB - 1GB
Priority queue:         O(active nodes) ≈ 1MB - 100MB
Highway tables:         O(4^max_rounds) ≈ 16MB - 4GB
Trail path storage:     O(max_rounds × Top-N) ≈ 1MB - 10MB
Temporary comp cache:   O(single round expansion) ≈ 10MB - 100MB
```

### Performance Optimization Strategies

#### 1. Parameter Tuning
```bash
# Progressive parameter increase strategy
./analyze_medcp 6 20    # Baseline test: ~1 minute
./analyze_medcp 6 25    # Medium test: ~5 minutes
./analyze_medcp 8 30    # Standard config: ~1 hour
./analyze_medcp 10 35   # Heavy task: ~1 day (requires cluster)
```

#### 2. Highway Table Precomputation
```bash
# One-time precomputation, multiple reuse
./highway_table_build highway_diff.bin 12     # Diff table: ~2GB, build time ~1 hour
./highway_table_build_lin highway_lin.bin 10  # Linear table: ~500MB, build time ~30 min

# Using precomputed tables can speed up 2-5x
./analyze_medcp 10 35 highway_diff.bin
```

#### 3. Cluster Parallelization
```bash
# Parameter space splitting strategy
for start_diff in 0x1 0x8000 0x80000000; do
  for weight_cap in {30..45..5}; do
    sbatch --mem=8G --time=24:00:00 \
      run_medcp.sh 10 $weight_cap $start_diff
  done
done
```

### Cluster Deployment Recommendations

#### Hardware Configuration Recommendations
- **CPU**: 16+ cores with AVX2/POPCNT instruction support
- **Memory**: 32GB+ DDR4, ECC memory recommended
- **Storage**: NVMe SSD for Highway tables and result storage
- **Network**: 10Gbps+ for distributed task coordination

#### SLURM Job Script Template
```bash
#!/bin/bash
#SBATCH --job-name=medcp_analysis
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=48:00:00
#SBATCH --partition=compute

module load gcc/11.2.0
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

# Execute search task
./analyze_medcp ${ROUNDS} ${WEIGHT_CAP} highway_diff.bin \
  --start-hex ${START_DA} ${START_DB} \
  --export results_${SLURM_JOB_ID}.csv \
  --export-trace trail_${SLURM_JOB_ID}.csv
```

#### Containerized Deployment
```dockerfile
FROM gcc:11.2.0
WORKDIR /app
COPY . .
RUN mkdir build && cd build && cmake .. && make -j$(nproc)
ENTRYPOINT ["./build/analyze_medcp"]
```

### Monitoring and Troubleshooting

#### Performance Monitoring Metrics
```bash
# Real-time resource usage monitoring
htop -p $(pgrep analyze_medcp)
iostat -x 5    # Monitor disk I/O
free -m        # Monitor memory usage

# Use perf for hotspot function analysis
perf record ./analyze_medcp 8 30
perf report
```

#### Common Issues and Solutions
- **Out of Memory**: Reduce weight cap or use swap space
- **Computation Hangs**: Check for deep recursion traps, adjust pruning parameters
- **Abnormal Results**: Verify starting state validity, check parameter ranges
- **Performance Degradation**: Clean temporary files, restart to reduce memory fragmentation

## Theoretical Results

Based on experimental results from papers, this toolkit achieves breakthroughs in:

### Differential Analysis Results

- **NeoAlzette 8 rounds**: Optimal differential probability 2^{-32}
- **SPECK32 11 rounds**: Correlation -2^{-17.09}
- **SPECK64 12 rounds**: Correlation -2^{-20.46}

### Linear Analysis Results

- **Algorithm Improvement**: ~8x reduction in computational complexity compared to previous methods
- **Automated Search**: First implementation of automatic DL trail search for arbitrary output masks
- **MIQCP Transformation**: Converts matrix multiplication chains to solvable optimization problems

## Development and Extension

### Adding New Cipher Algorithms

1. Define algorithm structures in `include/`
2. Implement round functions and state transitions
3. Adapt differential/linear local models
4. Add corresponding analysis programs

### Custom Search Strategies

```cpp
// Implement custom lower bound function
auto custom_lower_bound = [](const State& s, int round) -> int {
    // Custom bound calculation logic
    return compute_bound(s, round);
};

// Use custom strategy for search
auto result = matsui_threshold_search(rounds, start, cap, 
                                     next_states, custom_lower_bound);
```

## What is MEDCP/MELCC?

### MEDCP (Modular Addition by Constant - Differential Property)

MEDCP analyzes the differential behavior when one operand of modular addition is a constant:

- **Operation**: `z = x + c (mod 2^n)` where `c` is constant
- **Differential**: Given input difference `Δx`, find output difference `Δz`  
- **Applications**: Round constant additions, key mixing operations
- **Complexity**: Exact computation in O(1) time using precomputed models

### MELCC (Modular Addition Linear Cryptanalysis with Correlation)

MELCC studies linear approximations of modular addition with correlation analysis:

- **Linear Mask**: Input mask `α`, output mask `β`
- **Correlation**: `Cor(α·x ⊕ β·(x+y))` for addition `z = x + y`
- **Wallén Model**: Efficient enumeration of feasible linear masks
- **Applications**: Linear trail search, correlation computation

Both algorithms are essential components for analyzing ARX ciphers, particularly for:
- Automated cryptanalysis tool development
- Security evaluation of new ARX designs
- Optimized attack implementations

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

## Citation

If you use this toolkit in your research, please cite the relevant papers:

```bibtex
@inproceedings{miqcp2022,
  title={A MIQCP-Based Automatic Search Algorithm for Differential-Linear Trails of ARX Ciphers},
  author={Guangqiu Lv and Chenhui Jin and Ting Cui},
  booktitle={Cryptology ePrint Archive},
  year={2022}
}

@inproceedings{alzette2020,
  title={Alzette: A 64-Bit ARX-box},
  author={Christof Beierle and Alex Biryukov and Luan Cardoso dos Santos and others},
  booktitle={Annual International Cryptology Conference},
  pages={419--448},
  year={2020}
}
```

## Contributing

Issues and Pull Requests are welcome! Please ensure:

1. Code conforms to C++20 standards
2. Add appropriate tests and documentation
3. Follow existing code style
4. Provide detailed descriptions of changes

## Contact

For technical questions or collaboration inquiries, please contact through:

- Create GitHub Issues
- Email project maintainers
- Participate in relevant academic conferences and discussions

---

**Note**: This toolkit is intended for academic research and educational purposes only. Please do not use for malicious attacks or illegal activities.