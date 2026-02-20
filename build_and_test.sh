#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# build_and_test.sh
#
# Goals:
#   - Linux / bash counterpart to build_and_test.bat
#   - Use the repo CMakeLists.txt so Windows-only system libs are not forced on Linux
#   - Build research programs and run the same selftests as the Windows script
#
# Options:
#   --no-clean           Do not remove the build directory before configuring
#   --build-only         Build but do not run selftests
#   --run-only           Run selftests without rebuilding
#   --smoke              Configure/build only; implies --build-only --no-clean
#   --build-dir PATH     Override the CMake build directory
#   --generator NAME     Forward an explicit CMake generator
#
# Environment:
#   CMAKE                Override the cmake executable path
#   CXX                  Preferred compiler executable path or bare command name for CMake
#   GXX_BIN_DIR          Optional bin dir containing g++ / clang++
#   MINGW64              Optional bin dir containing g++ / clang++
#   CLANG64              Optional bin dir containing clang++ / g++
#   If none of the compiler vars are set, the script falls back to `g++`
# ============================================================================

do_clean=1
do_build=1
do_run=1
smoke=0
build_dir=""
generator=""

while (($# > 0)); do
    case "$1" in
        --no-clean)
            do_clean=0
            shift
            ;;
        --build-only|build-only)
            do_run=0
            shift
            ;;
        --run-only)
            do_build=0
            do_clean=0
            shift
            ;;
        --smoke)
            smoke=1
            do_run=0
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
    echo "  Tried:" >&2
    echo "    script dir = $script_dir" >&2
    echo "    current dir = $cwd" >&2
    echo "  Expected file:" >&2
    echo "    CMakeLists.txt" >&2
    exit 1
fi

if [[ -z "$build_dir" ]]; then
    build_dir="$root/build/linux-release"
fi

cmake_bin="${CMAKE:-cmake}"
if ! command -v "$cmake_bin" >/dev/null 2>&1; then
    echo "ERROR: cmake not found. Set PATH or export CMAKE=/path/to/cmake." >&2
    exit 1
fi

resolve_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        printf '%s\n' "$CXX"
        return 0
    fi

    local dir=""
    for dir_var in GXX_BIN_DIR MINGW64 CLANG64; do
        dir="${!dir_var:-}"
        [[ -z "$dir" ]] && continue
        if [[ -x "$dir/g++" ]]; then
            printf '%s\n' "$dir/g++"
            return 0
        fi
        if [[ -x "$dir/clang++" ]]; then
            printf '%s\n' "$dir/clang++"
            return 0
        fi
    done

    printf '%s\n' g++
}

resolved_cxx="$(resolve_cxx)"
if [[ "$resolved_cxx" == */* ]]; then
    if [[ ! -x "$resolved_cxx" ]]; then
        echo "ERROR: compiler not found: $resolved_cxx" >&2
        exit 1
    fi
elif ! command -v "$resolved_cxx" >/dev/null 2>&1; then
    echo "ERROR: compiler not found on PATH: $resolved_cxx" >&2
    exit 1
fi
export CXX="$resolved_cxx"

echo "============================================================"
echo "Build and Test - NeoAlzette AutoSearch (Linux / CMake / C++20)"
echo "============================================================"
echo
echo "ROOT      = $root"
echo "BUILD_DIR = $build_dir"
echo "CMAKE     = $(command -v "$cmake_bin")"
echo "CXX       = $CXX"
if ((smoke)); then
    echo "MODE      = smoke (build-only, no-clean)"
fi
echo

if ((do_clean && do_build)); then
    echo "[1/3] Cleaning build directory..."
    rm -rf -- "$build_dir"
    echo
else
    echo "[1/3] Cleaning build directory... (skipped)"
    echo
fi

if ((do_build)); then
    echo "[2/3] Configuring and building with CMake..."
    echo

    cmake_configure=(
        "$cmake_bin"
        -S "$root"
        -B "$build_dir"
        -DNEOALZETTE_BUILD_PROGRAMS=ON
        -DCMAKE_BUILD_TYPE=Release
    )
    if [[ -n "$generator" ]]; then
        cmake_configure+=(-G "$generator")
    fi

    "${cmake_configure[@]}"
    "$cmake_bin" --build "$build_dir" --config Release --parallel

    echo
    echo "OK: build successful."
    echo
else
    echo "[2/3] Build... (skipped: --run-only)"
    echo
fi

run_selftest() {
    local exe="$1"
    if [[ ! -x "$exe" ]]; then
        echo "ERROR: missing executable: $exe" >&2
        exit 1
    fi
    "$exe" --selftest
}

if ((do_run)); then
    echo "[3/3] Running selftests..."
    echo "============================================================"
    run_selftest "$build_dir/test_neoalzette_differential_best_search"
    echo
    echo "============================================================"
    run_selftest "$build_dir/test_neoalzette_linear_best_search"
    echo
    echo "============================================================"
    run_selftest "$build_dir/test_neoalzette_linear_hull_wrapper"
    echo
    echo "============================================================"
    run_selftest "$build_dir/test_neoalzette_differential_hull_wrapper"
    echo
    echo "============================================================"
    echo
    echo "All selected steps completed successfully."
else
    echo "[3/3] Selftests... (skipped)"
    echo
    echo "Build step completed successfully."
fi
