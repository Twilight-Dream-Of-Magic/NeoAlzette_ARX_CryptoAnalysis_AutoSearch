# NeoAlzette Cryptanalysis Toolkit

English | [中文](README.md)

## Project Overview

This project implements a **high-performance cryptanalysis toolkit** for ARX ciphers (Addition, Rotation, XOR), with a particular focus on differential and linear cryptanalysis of the NeoAlzette 64-bit ARX-box. The project is based on cutting-edge research from CRYPTO 2022 and other top-tier conferences, providing a complete automated search and analysis framework.

> ⚠️ **Important Warning**: This is a **computationally intensive** research tool. Single searches may require hours to days of CPU time and several GB of memory. Please understand the algorithmic complexity before use.

### Core Algorithms

- **MEDCP (Maximum Expected Differential Characteristic Probability)**: Algorithm for maximum expected differential characteristic probability of modular addition/subtraction
- **MELCC (Maximum Expected Linear Characteristic Correlation)**: Algorithm for maximum expected linear characteristic correlation of modular addition/subtraction

### Algorithmic Significance

These algorithms solve the open problem posed by Niu et al. at CRYPTO 2022: **How to automatically search for differential-linear trails in ARX ciphers**. By transforming arbitrary matrix multiplication chains into Mixed Integer Quadratically-Constrained Programming (MIQCP), they achieve:

- Computational complexity reduction by approximately **8x**
- First implementation supporting **arbitrary output masks** for automatic search
- Discovery of **best differential-linear distinguishers** for SPECK, Alzette and other ARX ciphers

## Algorithm Background

### ARX Cipher Structure

ARX ciphers are based on three fundamental operations:
- **Addition (⊞)**: Modular addition modulo 2^n
- **Rotation (<<<, >>>)**: Circular bit shifts
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
- **Nonlinear Functions**: F(A) = B = B ⊞ (A<<< 31) ⊕ (A <<< 17) ⊕ RC[0], F(B) = A = A ⊞ (B<<< 31) ⊕ (B <<< 17) ⊕ RC[5]
- **Linear Diffusion Layer**: L₁, L₂ provide branch number guarantees
- **Round Constant Injection**: 16 predefined round constants

## Complete Project Structure

```
├── include/                          # Core algorithm header library
│   ├── neoalzette.hpp               # NeoAlzette ARX-box core implementation
│   ├── lm_fast.hpp                  # Lipmaa-Moriai differential fast enumeration
│   ├── lm_wallen.hpp                # LM-Wallén hybrid model (differential+linear)
│   ├── wallen_fast.hpp              # Wallén linear correlation fast computation
│   ├── wallen_optimized.hpp         # Wallén optimized: precomputed automaton
│   ├── highway_table.hpp            # Differential Highway suffix lower bound table
│   ├── highway_table_lin.hpp        # Linear Highway suffix lower bound table
│   ├── lb_round_full.hpp            # Complete round function differential bounds
│   ├── lb_round_lin.hpp             # Complete round function linear bounds
│   ├── suffix_lb.hpp                # Multi-round suffix differential bounds
│   ├── suffix_lb_lin.hpp            # Multi-round suffix linear bounds
│   ├── threshold_search.hpp         # Matsui threshold search framework
│   ├── threshold_search_optimized.hpp # Parallelized threshold search
│   ├── matsui_complete.hpp          # Complete Matsui Algorithm 2 implementation
│   ├── canonicalize.hpp             # State canonicalization (rotation equivalence)
│   ├── state_optimized.hpp          # Optimized state representation and caching
│   ├── diff_add_const.hpp           # Modular addition by constant differential properties
│   ├── mask_backtranspose.hpp       # Linear mask backward propagation
│   ├── neoalz_lin.hpp               # NeoAlzette linear layer exact implementation
│   ├── pddt.hpp                     # Partial Differential Distribution Table
│   ├── pddt_optimized.hpp           # Optimized pDDT construction algorithms
│   └── trail_export.hpp             # Trail export and CSV formatting
├── src/                             # Main analysis tools
│   ├── analyze_medcp.cpp            # 🔥 MEDCP trail searcher (main tool)
│   ├── analyze_medcp_optimized.cpp  # ⚡ Optimized MEDCP analyzer (recommended)
│   ├── analyze_melcc.cpp            # 🔥 MELCC trail searcher (main tool)
│   ├── analyze_melcc_optimized.cpp  # ⚡ Optimized MELCC analyzer (recommended)
│   ├── complete_matsui_demo.cpp     # Complete Matsui Algorithm 1&2 demonstration
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
├── papers_txt/                      # Text-extracted versions of papers
├── PAPERS_COMPLETE_ANALYSIS_CN.md   # 🔥 Complete analysis of 11 papers (25,000+ words)
├── ALZETTE_VS_NEOALZETTE.md         # Alzette vs NeoAlzette design comparison
├── ALGORITHM_IMPLEMENTATION_STATUS.md # Paper algorithm implementation status
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

#### 📚 Core Documentation
- **`PAPERS_COMPLETE_ANALYSIS_CN.md`** - **🔥 Complete analysis guide for 11 papers**
  - 25,000+ words of deep technical analysis
  - Complete chain from mathematical formulas to code implementation
  - Engineering art analysis of Alzette's three-step pipeline design
  - Layer-by-layer analysis of Lipmaa-Moriai, Wallén and other core algorithms
  - Clarification of common confusions and learning path guidance
  - **Essential reading when encountering algorithm understanding difficulties**

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

#### 🔥 Main Analysis Tools
- **`analyze_medcp`** - Standard MEDCP trail search
- **`analyze_medcp_optimized`** - **⚡ Optimized MEDCP Analyzer** (Recommended)
- **`analyze_melcc`** - Standard MELCC trail search  
- **`analyze_melcc_optimized`** - **⚡ Optimized MELCC Analyzer** (Recommended)

#### 🔧 Auxiliary Tools
- **`complete_matsui_demo`** - Complete Matsui Algorithm 1&2 demonstration
- `highway_table_build*` - Highway table construction tools

#### ⚡ Optimized Version Features
**New optimized versions include the following improvements**:
- **Wallén Algorithm Rewrite**: Precomputed automaton replaces runtime recursion, 2-5x performance improvement
- **Parallelized Search**: Multi-threaded work-stealing to fully utilize multi-core CPUs
- **Cache-Friendly Design**: 64-bit packed state representation reduces memory access overhead
- **Fast Canonicalization**: Optimized bit manipulation algorithms accelerate state equivalence checking  
- **Improved Pruning Strategy**: Better lower bound estimation and duplicate state detection

## Usage

### ⚠️ Essential Pre-requisites

These tools are **computationally intensive** research-grade algorithm implementations:

- **Time Complexity**: Exponential worst-case, actual performance depends on pruning efficiency
- **Space Complexity**: O(2^{weight_cap}) memory usage
- **Recommended Environment**: High-performance servers or clusters, not personal laptops
- **Parameter Tuning**: Start with small parameters, gradually increase complexity

### MEDCP Trail Search

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

#### Standard Version Examples
```bash
# Beginner example: 6-round search, weight cap 25 (~1 minute)
./analyze_medcp 6 25

# Standard search: 8 rounds, weight 35, custom starting difference
./analyze_medcp 8 35 --start-hex 0x1 0x0
```

#### ⚡ Optimized Version Examples (Recommended)
```bash
# Basic optimized search: auto-detect thread count
./analyze_medcp_optimized 6 25

# High-performance search: specify thread count, use Highway table
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 8 --k1 8 --k2 8

# Fast search: use fast canonicalization (suitable for large-scale search)
./analyze_medcp_optimized 10 40 --fast-canonical --threads 16

# Complete optimized analysis: export detailed results
./analyze_medcp_optimized 8 30 \
  --export-trace trail_opt.csv \
  --export-hist histogram_opt.csv \
  --export-topN 10 top10_opt.csv \
  --threads 8

# Performance comparison test
echo "Standard version:" && time ./analyze_medcp 6 25
echo "Optimized version:" && time ./analyze_medcp_optimized 6 25 --threads 4
```

#### Performance Improvement Description
- **2-5x Speed Improvement**: Optimized Wallén algorithm and parallelization
- **Lower Memory Usage**: Packed state representation and memory pool management
- **Better Scalability**: Multi-threading support fully utilizes modern multi-core CPUs

### MELCC Trail Search

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

#### Standard Version Examples
```bash
# Beginner example: 6-round linear search
./analyze_melcc 6 20

# High-weight mask search
./analyze_melcc 8 25 --start-hex 0x80000001 0x0
```

#### ⚡ Optimized Version Examples (Recommended)
```bash
# Basic optimized linear search
./analyze_melcc_optimized 6 20

# High-performance linear analysis: multi-threading + Highway table
./analyze_melcc_optimized 8 25 --lin-highway highway_lin.bin --threads 6

# Fast linear search: use fast canonicalization
./analyze_melcc_optimized 10 30 --fast-canonical --threads 8

# Complete optimized linear analysis pipeline
./analyze_melcc_optimized 8 25 \
  --start-hex 0x1 0x0 \
  --export-trace linear_trail_opt.csv \
  --export-topN 5 best_linear_opt.csv \
  --threads 6

# Performance comparison: standard vs optimized
time ./analyze_melcc 6 20
time ./analyze_melcc_optimized 6 20 --threads 4
```

#### Linear Analysis Specific Optimizations
- **Precomputed Wallén Automaton**: Avoids runtime recursion, significantly improves enumeration speed
- **Optimized Backward Propagation**: Precise (L^{-1})^T computation reduces unnecessary state generation
- **Improved Priority Queue**: Uses packed states to reduce memory footprint and improve cache hit rates

### Highway Table Construction

```bash
# Build differential Highway table (optional, for search acceleration)
./highway_table_build output_diff.bin [max_rounds]

# Build linear Highway table
./highway_table_build_lin output_lin.bin [max_rounds]
```

### Paper Algorithm Demonstration

```bash
# Demonstrate complete Matsui Algorithm 1&2 implementation
./complete_matsui_demo --full

# Quick algorithm correctness verification
./complete_matsui_demo --quick
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

## 🖥️ Personal Computer vs 🏢 Computing Cluster: Usage Scenarios

### ⚠️ **Critical Resource Requirements**

**Reality of Personal Computer Limitations**:
- 💻 **Memory Capacity**: Usually 8-16GB, severely insufficient for research-grade analysis
- ⏱️ **Computation Time**: Limited single-core performance, multi-round searches may take days
- 🌡️ **Thermal Constraints**: Extended high-load may cause throttling or overheating
- 📊 **Parameter Limits**: Can only run very low parameter "validation" tests

**Computing Cluster Necessity**:
- 🎯 **Real Research Analysis** (8+ rounds) requires cluster resource allocation
- 📈 **Breakthrough Discoveries** (10+ rounds) mandate high-performance computing environments
- 💾 **Memory Requirements**: 32GB+ RAM needed to handle complex search tasks
- 🔄 **Parallel Scaling**: Hundreds of cores working simultaneously for reasonable completion times

### Detailed Usage Scenario Comparison

#### 👨‍💻 **Personal Computer Applicable Scenarios**

| Parameter Config | Rounds | Weight Cap | Memory Required | Time Range | Purpose |
|------------------|--------|------------|----------------|-------------|---------|
| **Validation** | 4 | 15-20 | 50-200MB | 10sec-2min | Algorithm validation, tool understanding |
| **Learning** | 4-5 | 20-25 | 200-500MB | 2-10min | Learning cryptanalysis, parameter understanding |
| **Testing** | 6 | 25-30 | 500MB-1GB | 10-60min | Tool testing, method verification |

**Personal Computer Practical Limitations**:
```bash
# ✅ Feasible: Verify tools work correctly
./analyze_medcp_optimized 4 15    # ~30 seconds

# 🟡 Barely manageable: Need to close other programs
./analyze_medcp_optimized 4 25    # ~5-15 minutes, memory pressure

# ❌ Not recommended: May cause system freeze or out-of-memory
./analyze_medcp_optimized 6 30    # May take hours, likely insufficient memory
```

#### 🏢 **Computing Cluster Required Scenarios**

| Research Type | Rounds | Weight Cap | Memory Required | Compute Nodes | Resource Application |
|---------------|--------|------------|----------------|---------------|---------------------|
| **Paper Experiments** | 6-8 | 25-35 | 4-8GB | 1-2 nodes | Medium compute resources |
| **Deep Analysis** | 8-10 | 35-45 | 8-16GB | 2-4 nodes | High-performance resources |
| **Breakthrough Research** | 10-12+ | 45+ | 16-64GB | 4-16 nodes | Supercomputing center resources |

**Cluster Resource Application Guide**:
```bash
# Medium research tasks
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1  
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=24:00:00

# Heavy research tasks  
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=32
#SBATCH --mem=128G
#SBATCH --time=168:00:00  # 1 week
```

### 📊 **Realistic Performance Expectation Management**

#### **What Personal Computers Can Do**:
```
✅ Algorithm validation: Confirm tools work correctly
✅ Parameter understanding: Learn effects of different parameters
✅ Method verification: Verify correctness of analysis methods
✅ Small-scale experiments: Obtain basic analysis data
✅ Tool familiarization: Master usage methods and interfaces

❌ Cannot do: Real cryptographic research-grade analysis
❌ Cannot do: Discover new cryptographic results
❌ Cannot do: Comparative verification with literature results
❌ Cannot do: Complete security assessments
```

#### **What Cluster Computing Can Do**:
```
🎯 Real Research:
- Discover new optimal differential/linear trails
- Verify or challenge existing security claims
- Develop new attack methods
- Publish breakthrough results at top-tier conferences

📈 Large-scale Analysis:
- Complete parameter space exploration
- Statistically significant experimental validation
- Comparative analysis with existing literature
- Security assessment of new ARX ciphers
```

### 💰 **Resource Cost Estimation**

#### **Personal Computer Costs**:
- 💻 **Hardware Cost**: 0 (use existing equipment)
- ⏱️ **Time Cost**: Low (short-term validation)
- 🎯 **Research Value**: Limited (learning and validation only)

#### **Cluster Resource Costs**:
- 💰 **Application Difficulty**: Requires formal research project support
- 📝 **Resource Application**: Detailed computational requirement documentation
- ⏱️ **Wait Time**: May require queuing for resources
- 🎯 **Research Value**: High (genuine breakthrough discoveries)

### 🎓 **Recommendations for Different Research Stages**

#### **Learning Stage (Personal Computer Sufficient)**:
```bash
# Understand basic tool functionality
./analyze_medcp_optimized 4 15 --export test1.csv
./analyze_melcc_optimized 4 15 --export test2.csv

# Understand parameter effects
for w in {15..25}; do 
  time ./analyze_medcp_optimized 4 $w
done

# Master export and analysis features
./analyze_medcp_optimized 4 20 \
  --export-trace trail.csv \
  --export-hist hist.csv
```

#### **Research Stage (Cluster Application Required)**:
```bash
# Actual research after cluster resource allocation
sbatch run_analysis.sbatch 8 35    # Discover new trails
sbatch run_analysis.sbatch 10 40   # Challenge security boundaries  
sbatch run_comparison.sbatch       # Compare with literature
```

### 🚨 **Critical Warnings**

**Do NOT attempt on personal computers**:
- ❌ 8+ round searches (may cause out-of-memory)
- ❌ Weight cap >30 tasks (may run for days)
- ❌ Large searches without Highway tables (extremely inefficient)
- ❌ Running multiple tools simultaneously (resource competition)

**Requirements for cluster application**:
- 📚 **Clear research objectives**: Explain what cryptographic problems to solve
- 📊 **Detailed resource requirements**: Reasonable estimates based on small-scale tests
- ⏱️ **Realistic time expectations**: Large searches may require weeks
- 💾 **Storage planning**: Result data storage and management schemes

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

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

## 📚 Learning Resource Guide

### **Documentation Usage Recommendations**
- **Want to get started quickly?** → Read the "Usage" section of this README
- **Encountering algorithm confusion?** → Refer to `PAPERS_COMPLETE_ANALYSIS_CN.md`
- **Need deep understanding?** → Start with MnT operator, understand algorithm encapsulation layers
- **Optimization bottlenecks?** → Consult "Performance Optimization" sections in paper analysis

### **Recommended Learning Path** 
```
Step 1: Familiarize with tools → Run 4-round small tests, understand basic concepts
Step 2: Understand algorithms → Read paper analysis, master mathematical principles
Step 3: Deep application → Try 8-round analysis, explore parameter optimization  
Step 4: Advanced research → Extend to other ARX ciphers, publish results
```

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