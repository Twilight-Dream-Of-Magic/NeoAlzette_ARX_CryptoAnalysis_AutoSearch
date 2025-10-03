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

### NeoAlzette Algorithm

NeoAlzette is an improved version based on the Alzette 64-bit ARX-box, featuring:

- **64-bit State**: (A, B) ∈ F₃₂²
- **Two-Subround Structure**: Each round contains two subround operations  
- **Nonlinear Functions**: F(A) = B = B ⊞ (A<<< 31) ⊕ (A <<< 17) ⊕ RC[0], F(B) = A = A ⊞ (B<<< 31) ⊕ (B <<< 17) ⊕ RC[5]
- **Linear Diffusion Layer**: L₁, L₂ provide branch number guarantees
- **Round Constant Injection**: 16 predefined round constants

## Streamlined Project Structure

```
├── include/                          # Core algorithm headers (streamlined)
│   ├── neoalzette.hpp               # NeoAlzette ARX-box core implementation
│   ├── lm_fast.hpp                  # Lipmaa-Moriai differential fast enumeration
│   ├── wallen_fast.hpp              # Wallén linear correlation computation
│   ├── wallen_optimized.hpp         # Wallén optimized: precomputed automaton
│   ├── highway_table.hpp            # Differential Highway suffix lower bounds
│   ├── highway_table_lin.hpp        # Linear Highway suffix lower bounds
│   ├── threshold_search.hpp         # Matsui threshold search framework
│   ├── threshold_search_optimized.hpp # Parallelized threshold search
│   ├── matsui_complete.hpp          # Complete Matsui Algorithm 2 implementation
│   ├── pddt.hpp                     # Partial Differential Distribution Table
│   ├── pddt_optimized.hpp           # Optimized pDDT construction algorithms
│   ├── lb_round_full.hpp            # Complete round differential bounds
│   ├── lb_round_lin.hpp             # Complete round linear bounds
│   ├── suffix_lb.hpp                # Multi-round suffix differential bounds
│   ├── suffix_lb_lin.hpp            # Multi-round suffix linear bounds
│   ├── diff_add_const.hpp           # Modular addition by constant properties
│   ├── canonicalize.hpp             # State canonicalization
│   ├── mask_backtranspose.hpp       # Linear mask backward propagation
│   ├── neoalz_lin.hpp               # NeoAlzette linear layer implementation
│   └── trail_export.hpp             # Trail export and CSV formatting
├── src/                             # Core analysis tools (streamlined)
│   ├── analyze_medcp.cpp            # 🔥 MEDCP differential trail searcher
│   ├── analyze_medcp_optimized.cpp  # ⚡ Optimized MEDCP analyzer (recommended)
│   ├── analyze_melcc.cpp            # 🔥 MELCC linear trail searcher
│   ├── analyze_melcc_optimized.cpp  # ⚡ Optimized MELCC analyzer (recommended)
│   ├── complete_matsui_demo.cpp     # 📚 Complete Matsui Algorithms demonstration
│   ├── highway_table_build.cpp      # 🔧 Differential Highway table builder
│   ├── highway_table_build_lin.cpp  # 🔧 Linear Highway table builder
│   ├── threshold_lin.cpp            # Linear threshold search demo
│   ├── gen_round_lb_table.cpp       # Round lower bound table generator
│   ├── neoalzette.cpp               # NeoAlzette algorithm implementation
│   └── pddt.cpp                     # Partial DDT implementation
├── papers/                          # Core theoretical papers (PDF)
├── papers_txt/                      # Text-extracted paper versions
├── PAPERS_COMPLETE_ANALYSIS_CN.md   # 🔥 Complete 11-paper analysis guide (25,000+ words)
├── ALZETTE_VS_NEOALZETTE.md         # Alzette vs NeoAlzette design comparison
├── ALGORITHM_IMPLEMENTATION_STATUS.md # Paper algorithm implementation status
├── CMakeLists.txt                   # Build configuration
├── LICENSE                          # GPL v3.0 license
└── .gitignore                       # Git ignore configuration
```

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

# Compile core tools
make -j$(nproc)

# Optional: Build demonstration programs
cmake -DNA_BUILD_DEMOS=ON ..
make -j$(nproc)
```

### Build Artifacts

#### 🔥 Main Analysis Tools
- **`analyze_medcp`** - Standard MEDCP differential trail search
- **`analyze_medcp_optimized`** - **⚡ Optimized MEDCP Analyzer** (Recommended)
- **`analyze_melcc`** - Standard MELCC linear trail search  
- **`analyze_melcc_optimized`** - **⚡ Optimized MELCC Analyzer** (Recommended)

#### 🔧 Auxiliary Tools
- **`complete_matsui_demo`** - Complete Matsui Algorithm 1&2 demonstration
- **`highway_table_build`** - Differential Highway table builder
- **`highway_table_build_lin`** - Linear Highway table builder

## 🖥️ Personal Computer vs 🏢 Computing Cluster: Usage Scenarios

### ⚠️ **Critical Resource Requirements**

**Reality of Personal Computer Limitations**:
- 💻 **Memory Capacity**: Usually 8-16GB, severely insufficient for research-grade analysis
- ⏱️ **Computation Time**: Limited performance, multi-round searches may take days
- 🌡️ **Thermal Constraints**: Extended high-load may cause throttling
- 📊 **Parameter Limits**: Can only run very low parameter "validation" tests

**Computing Cluster Necessity**:
- 🎯 **Real Research Analysis** (8+ rounds) requires cluster resource allocation
- 📈 **Breakthrough Discoveries** (10+ rounds) mandate HPC environments
- 💾 **Memory Requirements**: 32GB+ RAM needed for complex search tasks
- 🔄 **Parallel Scaling**: Hundreds of cores needed for reasonable completion times

## 📋 **Complete CLI Usage Guide**

### 🔥 **analyze_medcp / analyze_medcp_optimized - MEDCP Differential Trail Search**

#### **Complete Syntax**
```bash
./analyze_medcp[_optimized] R Wcap [highway.bin] [options]
```

#### **Parameter Description**
- **`R`** - Search rounds (4-12, personal computer: 4-6)
- **`Wcap`** - Weight cap (15-50, personal computer: 15-25)
- **`highway.bin`** - Optional Highway table file

#### **Complete Options List**

| Option | Parameters | Description | Example |
|--------|-----------|-------------|---------|
| `--start-hex` | `dA dB` | Starting differential state (32-bit hex) | `--start-hex 0x1 0x0` |
| `--export` | `file.csv` | Export search summary | `--export results.csv` |
| `--export-trace` | `file.csv` | Export complete trail path | `--export-trace trail.csv` |
| `--export-hist` | `file.csv` | Export weight distribution histogram | `--export-hist histogram.csv` |
| `--export-topN` | `N file.csv` | Export top-N best results | `--export-topN 10 top10.csv` |
| `--k1` | `K` | Top-K candidates for var-var addition (1-16) | `--k1 8` |
| `--k2` | `K` | Top-K candidates for var-const addition (1-16) | `--k2 8` |
| `--threads` | `N` | Number of threads (optimized version only) | `--threads 8` |
| `--fast-canonical` | none | Fast canonicalization (optimized version only) | `--fast-canonical` |

#### **Usage Examples**

**🟢 Personal Computer Entry Level**:
```bash
# Quick verification (completes in 30 seconds)
./analyze_medcp_optimized 4 15

# Basic analysis (2-5 minutes)
./analyze_medcp_optimized 4 20 --start-hex 0x1 0x0 --export basic.csv

# Parameter understanding
for w in {15..25}; do
  echo "Testing weight $w:"
  time ./analyze_medcp_optimized 4 $w
done
```

**🟡 Personal Computer Challenge Level**:
```bash
# Multi-threaded search (5-15 minutes, close other programs)
./analyze_medcp_optimized 5 25 --threads 4

# Complete result export
./analyze_medcp_optimized 5 22 \
  --export summary.csv \
  --export-trace trail.csv \
  --export-hist histogram.csv
```

**🔴 Cluster Professional Level**:
```bash
# High-performance research analysis (requires cluster allocation)
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 16 --k1 8 --k2 8

# Large-scale parameter space search
for start in 0x1 0x8000 0x80000000; do
  sbatch --mem=16G --time=24:00:00 \
    run_medcp.sh 8 35 $start 0x0
done
```

### 🔥 **analyze_melcc / analyze_melcc_optimized - MELCC Linear Trail Search**

#### **Complete Syntax**
```bash
./analyze_melcc[_optimized] R Wcap [options]
```

#### **Parameter Description**
- **`R`** - Search rounds (4-10, linear more intensive than differential)
- **`Wcap`** - Weight cap (10-40, linear typically more restrictive)

#### **Complete Options List**

| Option | Parameters | Description | Example |
|--------|-----------|-------------|---------|
| `--start-hex` | `mA mB` | Starting linear masks (32-bit hex) | `--start-hex 0x80000000 0x1` |
| `--export` | `file.csv` | Export analysis summary | `--export linear_results.csv` |
| `--export-trace` | `file.csv` | Export optimal linear trail | `--export-trace linear_trail.csv` |
| `--export-hist` | `file.csv` | Export weight distribution | `--export-hist linear_hist.csv` |
| `--export-topN` | `N file.csv` | Export top-N best results | `--export-topN 5 top5.csv` |
| `--lin-highway` | `H.bin` | Linear Highway table file | `--lin-highway highway_lin.bin` |
| `--threads` | `N` | Number of threads (optimized version only) | `--threads 6` |
| `--fast-canonical` | none | Fast canonicalization (optimized version only) | `--fast-canonical` |

### 🔧 **Auxiliary Tools Usage**

#### **Highway Table Builders**
```bash
# Build differential Highway table (one-time, multiple reuse)
./highway_table_build highway_diff.bin 10
# Time: 1-3 hours, Size: 500MB-2GB

# Build linear Highway table
./highway_table_build_lin highway_lin.bin 8  
# Time: 30min-2hr, Size: 200MB-1GB

# Use Highway tables for accelerated search
./analyze_medcp_optimized 8 35 highway_diff.bin --threads 8
```

#### **Paper Algorithm Demonstration**
```bash
# Quick verification of Algorithm 1&2 correctness
./complete_matsui_demo --quick

# Complete demonstration of highways/country roads strategy
./complete_matsui_demo --full
```

## ⚠️ **Critical Usage Guidelines**

### **Personal Computer Limitations**

**✅ Recommended for personal computers**:
```bash
# Tool verification (guaranteed fast completion)
./analyze_medcp_optimized 4 15

# Parameter learning
./analyze_medcp_optimized 4 20 --start-hex 0x1 0x0

# Output format understanding
./analyze_medcp_optimized 4 18 --export test.csv --export-trace trail.csv
```

**❌ Do NOT attempt on personal computers**:
```bash
# These will cause out-of-memory or hours-long hangs:
./analyze_medcp_optimized 8 35     # ❌ 8 rounds require cluster
./analyze_medcp_optimized 6 45     # ❌ High weight requires cluster  
./analyze_melcc_optimized 7 30     # ❌ 7-round linear requires cluster
```

### **Cluster Resource Application Guide**

#### **SLURM Job Script Examples**

**Medium Research Tasks**:
```bash
#!/bin/bash
#SBATCH --job-name=neoalzette_analysis
#SBATCH --nodes=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=24:00:00

# Execute search
./analyze_medcp_optimized 8 35 highway_diff.bin \
  --threads 16 \
  --export results_${SLURM_JOB_ID}.csv
```

**Heavy Research Tasks**:
```bash
#!/bin/bash
#SBATCH --job-name=breakthrough_search
#SBATCH --nodes=4
#SBATCH --cpus-per-task=32  
#SBATCH --mem=128G
#SBATCH --time=168:00:00

# Distributed large-scale search
srun ./analyze_medcp_optimized 10 45 highway_diff.bin \
  --threads 32 \
  --export-trace breakthrough_trail.csv
```

### **Troubleshooting Guide**

**If search finds no results**:
```bash
# 1. Lower weight cap
./analyze_medcp_optimized 4 12 --start-hex 0x1 0x0

# 2. Try different starting states
./analyze_medcp_optimized 4 15 --start-hex 0x8000 0x0
```

**If search is too slow**:
```bash
# 1. Use fast mode
./analyze_medcp_optimized 4 20 --fast-canonical --threads 1

# 2. Pre-build Highway table for acceleration
./highway_table_build highway.bin 6
./analyze_medcp_optimized 6 25 highway.bin
```

**If memory insufficient**:
```bash
# Reduce complexity parameters
./analyze_medcp_optimized 4 18 --fast-canonical
```

## 📊 **Realistic Performance Expectations**

### **What Personal Computers Can Do**:
- ✅ **Algorithm Validation**: Confirm tools work correctly
- ✅ **Parameter Learning**: Understand effects of different settings
- ✅ **Method Understanding**: Grasp basic cryptanalysis concepts
- ✅ **Small Experiments**: Obtain basic analysis data

### **What Personal Computers Cannot Do**:
- ❌ **Research-Grade Analysis**: Discover new cryptographic results
- ❌ **Literature Comparison**: Verification with published results
- ❌ **Complete Assessment**: Security evaluation of new algorithms
- ❌ **Breakthrough Discoveries**: Research for top-tier conference publication

### **Value of Cluster Computing**:
- 🎯 **Real Cryptographic Research**: Discover new trails, challenge security claims
- 📈 **Large-Scale Validation**: Statistically significant experimental results
- 🔬 **Method Innovation**: Develop new analysis techniques and attack methods

## Theoretical Results and Validation

### Algorithm-Theory Correspondence

This toolkit completely implements the following paper algorithms:

1. **Lipmaa-Moriai (2001)**: O(log n) differential probability computation
2. **Wallén (2003)**: O(log n) linear correlation computation
3. **Matsui Algorithm 1&2 (complete paper reproduction)**: pDDT construction + threshold search
4. **MIQCP Conversion Technology (2022)**: Mathematical foundation for 8x performance improvement
5. **Highway Table Technology**: O(1) suffix lower bound queries

### Experimental Validation Recommendations

```bash
# Step 1: Verify algorithm implementation correctness
./complete_matsui_demo --quick

# Step 2: Small-scale performance testing
time ./analyze_medcp_optimized 4 15
time ./analyze_melcc_optimized 4 15  

# Step 3: Understand result formats
./analyze_medcp_optimized 4 18 --export-trace trail.csv
# Examine trail.csv file format

# Step 4: Prepare for research applications
./analyze_medcp_optimized 5 25 --export estimate.csv
# Estimate cluster resource requirements based on results
```

## 📚 Learning Resource Guide

### **Recommended Learning Path**
```
Step 1: Tool Validation → ./analyze_medcp_optimized 4 15 (confirm environment)
Step 2: Theory Study → Read PAPERS_COMPLETE_ANALYSIS_CN.md  
Step 3: Parameter Practice → Test different Wcap and starting states
Step 4: Advanced Application → Apply for cluster resources for real research
```

### **Important Documentation Index**
- **Quick Start**: CLI usage guide in this README
- **Algorithm Understanding**: `PAPERS_COMPLETE_ANALYSIS_CN.md` (25,000+ word deep analysis)
- **Design Comparison**: `ALZETTE_VS_NEOALZETTE.md` (original vs extended design)
- **Implementation Status**: `ALGORITHM_IMPLEMENTATION_STATUS.md` (paper algorithm coverage)

## License and Citation

This project is licensed under **GNU General Public License v3.0**.

```bibtex
@inproceedings{miqcp2022,
  title={A MIQCP-Based Automatic Search Algorithm for Differential-Linear Trails of ARX Ciphers},
  author={Guangqiu Lv and Chenhui Jin and Ting Cui},
  year={2022}
}

@inproceedings{alzette2020,
  title={Alzette: A 64-Bit ARX-box},  
  author={Christof Beierle and Alex Biryukov and others},
  booktitle={Annual International Cryptology Conference},
  pages={419--448},
  year={2020}
}
```

## 🎯 **Summary**

The NeoAlzette Cryptanalysis Toolkit provides:
- ✅ **Complete ARX Analysis Capability**: Full toolchain from theory to implementation
- ✅ **Academic-Grade Accuracy**: Rigorous implementation based on 11 core papers
- ✅ **Engineering-Grade Optimization**: Significant performance improvements and parallelization
- ✅ **Practical Guidance**: Clear personal computer vs cluster usage guidelines

**Target Users**:
- 🎓 **Cryptography Researchers**: Conducting ARX cipher security analysis
- 👨‍🎓 **Graduate Students & PhDs**: Learning modern cryptanalysis techniques
- 🏢 **Industry Researchers**: Evaluating security of ARX designs  
- 📚 **Educators**: Teaching advanced cryptographic concepts

---

**Note**: This toolkit is intended for academic research and educational purposes only. Please do not use for malicious attacks or illegal activities.