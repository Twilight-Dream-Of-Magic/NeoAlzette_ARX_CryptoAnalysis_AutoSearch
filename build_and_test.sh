#!/usr/bin/env bash
set -euo pipefail

# Build-only compile check for non-root AutoSearch modules.
# Root-level .cpp research programs are intentionally not built or run here.

do_clean=1
build_dir=""
generator=""

while (($# > 0)); do
    case "$1" in
        --no-clean)
            do_clean=0
            shift
            ;;
        --build-only|build-only)
            shift
            ;;
        --smoke)
            do_clean=0
            shift
            ;;
        --build-dir)
            if (($# < 2)); then
                echo "ERROR: --build-dir requires a path argument." >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --generator)
            if (($# < 2)); then
                echo "ERROR: --generator requires a generator name." >&2
                exit 2
            fi
            generator="$2"
            shift 2
            ;;
        --run-only)
            echo "ERROR: this script no longer runs programs." >&2
            exit 2
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

find_project_root() {
    local candidate="$1"
    if [[ -f "$candidate/CMakeLists.txt" ]]; then
        printf '%s\n' "$candidate"
        return 0
    fi
    return 1
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cwd="$(pwd -P)"
root=""

if root="$(find_project_root "$script_dir" 2>/dev/null)"; then
    :
elif root="$(find_project_root "$cwd" 2>/dev/null)"; then
    :
else
    echo "ERROR: cannot locate project root directory." >&2
    exit 1
fi

if [[ -z "$build_dir" ]]; then
    build_dir="$root/build/compile-check"
fi

cmake_bin="${CMAKE:-cmake}"
if ! command -v "$cmake_bin" >/dev/null 2>&1; then
    echo "ERROR: cmake not found. Set PATH or export CMAKE=/path/to/cmake." >&2
    exit 1
fi

echo "============================================================"
echo "Build - NeoAlzette AutoSearch compile checks"
echo "============================================================"
echo "ROOT      = $root"
echo "BUILD_DIR = $build_dir"
echo "CMAKE     = $(command -v "$cmake_bin")"
echo

if ((do_clean)); then
    echo "[1/3] Cleaning build directory..."
    rm -rf -- "$build_dir"
    echo
else
    echo "[1/3] Cleaning build directory... (skipped)"
    echo
fi

echo "[2/3] Configuring CMake..."
cmake_configure=(
    "$cmake_bin"
    -S "$root"
    -B "$build_dir"
    -DNEOALZETTE_BUILD_PROGRAMS=OFF
    -DCMAKE_BUILD_TYPE=Release
)
if [[ -n "$generator" ]]; then
    cmake_configure+=(-G "$generator")
fi
"${cmake_configure[@]}"
echo

echo "[3/3] Building non-root compile targets..."
"$cmake_bin" --build "$build_dir" --config Release --target \
    neoalzette_linear_best_search_compile \
    neoalzette_differential_best_search_compile \
    --parallel

echo
echo "OK: compile checks completed. No program was run."