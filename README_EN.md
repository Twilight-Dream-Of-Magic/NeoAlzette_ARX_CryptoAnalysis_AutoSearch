# NeoAlzette Cryptanalysis Toolkit

English | [中文](README.md)

## Project Overview

This project implements an efficient cryptanalysis toolkit for ARX ciphers (Addition, Rotation, XOR), with a particular focus on differential and linear cryptanalysis of the NeoAlzette 64-bit ARX-box. The project is based on state-of-the-art cryptanalysis theories and algorithms, providing a complete automated search and analysis framework.

### Core Algorithms

- **MEDCP (Modular Addition by Constant - Differential Property)**: Analysis of differential properties in modular addition by constants
- **MELCC (Modular Addition Linear Cryptanalysis with Correlation)**: Correlation-based linear cryptanalysis for modular addition

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

## Project Structure

```
├── include/                 # Header files
│   ├── neoalzette.hpp      # NeoAlzette algorithm implementation
│   ├── lm_fast.hpp         # Lipmaa-Moriai fast enumeration
│   ├── wallen_fast.hpp     # Wallén linear analysis
│   ├── highway_table.hpp   # Highway suffix lower bound tables
│   └── ...
├── src/                    # Source files
│   ├── analyze_medcp.cpp   # MEDCP differential analyzer
│   ├── analyze_melcc.cpp   # MELCC linear analyzer
│   ├── main_diff.cpp       # Main differential cryptanalysis program
│   ├── main_lin.cpp        # Main linear cryptanalysis program
│   └── ...
├── papers/                 # Related papers
└── papers_txt/            # Text versions of papers
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

### MEDCP Differential Analysis

```bash
# Basic usage: Search R-round differential trails with weight cap Wcap
./analyze_medcp R Wcap [highway.bin] [options]

# Example: Search 8-round differential with weight cap 30
./analyze_medcp 8 30

# Use custom starting differences (hexadecimal)
./analyze_medcp 8 30 --start-hex 0x1 0x0

# Use Highway table for acceleration (precomputed suffix bounds)
./analyze_medcp 8 30 highway_diff.bin

# Export search results to CSV
./analyze_medcp 8 30 --export results.csv

# Adjust search parameters
./analyze_medcp 8 30 --k1 8 --k2 8  # Top-K candidates
```

### MELCC Linear Analysis

```bash
# Basic usage: Search R-round linear trails with weight cap Wcap
./analyze_melcc R Wcap [options]

# Example: Search 8-round linear approximation with weight cap 25
./analyze_melcc 8 25

# Use custom starting masks
./analyze_melcc 8 25 --start-hex 0x80000000 0x1

# Use linear Highway table
./analyze_melcc 8 25 --lin-highway highway_lin.bin

# Export analysis results
./analyze_melcc 8 25 --export linear_results.csv
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

## Performance Characteristics

### Computational Complexity

- **Differential Enumeration**: Average << 2^n (due to prefix infeasibility pruning)
- **Linear Correlation Computation**: O(log n) time complexity
- **Highway Queries**: O(1) time, O(n) space
- **Threshold Search**: Exponential worst-case, actual performance depends on pruning efficiency

### Resource Consumption

| Analysis Type | Memory | CPU Time | Applicable Rounds |
|--------------|---------|----------|-------------------|
| MEDCP 6-8 rounds | ~500MB | Minutes | Lightweight |
| MEDCP 9-12 rounds | ~2GB | Hours | Medium |
| MELCC 6-8 rounds | ~1GB | Minutes | Lightweight |
| MELCC 9-12 rounds | ~4GB | Hours | Heavy |

### Optimization Recommendations

1. **Precompute Highway tables** to reduce redundant calculations
2. **Adjust weight caps** to balance search depth with time
3. **Use clusters** for parallel processing of different parameter combinations
4. **Monitor memory usage** to avoid memory overflow in large search tasks

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