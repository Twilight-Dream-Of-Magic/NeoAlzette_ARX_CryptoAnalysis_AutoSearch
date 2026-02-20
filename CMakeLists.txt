cmake_minimum_required(VERSION 3.15) # for CMAKE_MSVC_RUNTIME_LIBRARY
project(NeoAlzette_ARX_CryptoAnalysis_AutoSearch VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

# Default to Release for single-config generators (matches build_cmake.bat behavior).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Release, Debug, RelWithDebInfo, MinSizeRel)" )
endif()

# ============================================================================
# Build switches (match current repo layout)
# ============================================================================
# This repo primarily provides a small core library + several research programs
# under `test_*.cpp` (they are not unit tests in the strict sense).
option(NEOALZETTE_BUILD_PROGRAMS "Build research programs (test_*.cpp)" ON)
# Backward-compatible alias (deprecated).
option(BUILD_TESTS "Deprecated: use NEOALZETTE_BUILD_PROGRAMS" OFF)
if(BUILD_TESTS)
    set(NEOALZETTE_BUILD_PROGRAMS ON)
endif()

# ============================================================================
# Common target helpers
# ============================================================================
function(neoalzette_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ============================================================================
# Static linking / runtime options
# ============================================================================
# - MSVC: use static CRT (/MT, /MTd) so produced EXEs don't depend on vcruntime DLLs.
# - MinGW/Clang: optionally add -static flags for fully static binaries (where available).
option(NEOALZETTE_MSVC_STATIC_RUNTIME "Use static MSVC runtime (/MT, /MTd)" ON)
if (MSVC AND NEOALZETTE_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(_NEOALZETTE_DEFAULT_MINGW_STATIC OFF)
if(WIN32 AND NOT MSVC)
    # Align with build_and_test.bat (clang++ -static on Windows/MinGW toolchains).
    set(_NEOALZETTE_DEFAULT_MINGW_STATIC ON)
endif()
option(NEOALZETTE_MINGW_STATIC "Link executables statically on MinGW/Clang (adds -static flags)" ${_NEOALZETTE_DEFAULT_MINGW_STATIC})
function(neoalzette_apply_static_linking target_name)
    # Note: CMake's `MINGW` is not always set when using Clang targeting MinGW,
    # so we use a more robust predicate here.
    if (WIN32 AND NOT MSVC AND NEOALZETTE_MINGW_STATIC)
        # Match build_and_test.bat (clang++ ... -static).
        target_link_options(${target_name} PRIVATE -static)
    endif()
endfunction()

# ============================================================================
# 底層ARX分析算子（Header-only）
# ============================================================================
# 所有算子都是header-only，包括Algorithm 4（全inline實現）
# 無需編譯，直接include即可使用

# ============================================================================
# NeoAlzette 核心庫（目前 repo 只保留 core）
# ============================================================================
add_library(neoalzette STATIC
    src/neoalzette/neoalzette_core.cpp
)
target_compile_features(neoalzette PUBLIC cxx_std_20)
set_target_properties(neoalzette PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(neoalzette PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
neoalzette_apply_warnings(neoalzette)

# ============================================================================
# 可執行檔（目前 repo 的入口程式 / 研究程式）
# ============================================================================
if(NEOALZETTE_BUILD_PROGRAMS)
    find_package(Threads REQUIRED)

    add_executable(test_neoalzette_arx_trace
        test_neoalzette_arx_trace.cpp
    )
    target_link_libraries(test_neoalzette_arx_trace PRIVATE neoalzette Threads::Threads)
    target_include_directories(test_neoalzette_arx_trace PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_arx_trace)
    neoalzette_apply_static_linking(test_neoalzette_arx_trace)

    add_executable(test_neoalzette_differential_best_search
        test_neoalzette_differential_best_search.cpp
        test_arx_operator_self_test.cpp
        common/runtime_component.cpp
    )
    target_link_libraries(test_neoalzette_differential_best_search PRIVATE neoalzette Threads::Threads)
    target_include_directories(test_neoalzette_differential_best_search PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_differential_best_search)
    neoalzette_apply_static_linking(test_neoalzette_differential_best_search)

    add_executable(test_neoalzette_linear_best_search
        test_neoalzette_linear_best_search.cpp
        test_arx_operator_self_test.cpp
        common/runtime_component.cpp
    )
    target_link_libraries(test_neoalzette_linear_best_search PRIVATE neoalzette Threads::Threads)
    target_include_directories(test_neoalzette_linear_best_search PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_linear_best_search)
    neoalzette_apply_static_linking(test_neoalzette_linear_best_search)

    add_executable(test_neoalzette_arx_probabilistic_neutral_bits
        test_neoalzette_arx_probabilistic_neutral_bits.cpp
    )
    target_link_libraries(test_neoalzette_arx_probabilistic_neutral_bits PRIVATE neoalzette Threads::Threads)
    target_include_directories(test_neoalzette_arx_probabilistic_neutral_bits PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_arx_probabilistic_neutral_bits)
    neoalzette_apply_static_linking(test_neoalzette_arx_probabilistic_neutral_bits)

    add_executable(test_neoalzette_arx_probabilistic_neutral_bits_average
        test_neoalzette_arx_probabilistic_neutral_bits_average.cpp
    )
    target_link_libraries(test_neoalzette_arx_probabilistic_neutral_bits_average PRIVATE neoalzette Threads::Threads)
    target_include_directories(test_neoalzette_arx_probabilistic_neutral_bits_average PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_arx_probabilistic_neutral_bits_average)
    neoalzette_apply_static_linking(test_neoalzette_arx_probabilistic_neutral_bits_average)
endif()

# ============================================================================
# 安裝配置
# ============================================================================
install(TARGETS neoalzette
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h")

# ============================================================================
# 文檔
# ============================================================================
message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "NeoAlzette ARX密碼分析與自動化搜索")
message(STATUS "=======================================================")
message(STATUS "")
message(STATUS "📁 項目結構：")
message(STATUS "  include/arx_analysis_operators/  - 底層ARX算子（最優化）")
message(STATUS "  include/neoalzette/              - NeoAlzette專用")
message(STATUS "")
message(STATUS "📚 底層算子：")
message(STATUS "  • differential_xdp_add.hpp          - LM-2001 Alg 2 (O(1))")
message(STATUS "  • differential_optimal_gamma.hpp    - LM-2001 Alg 4 (Θ(log n)) 🆕")
message(STATUS "  • differential_addconst.hpp         - BvWeight (O(log²n))")
message(STATUS "  • linear_correlation_add_logn.hpp   - Wallén Θ(log n)")
message(STATUS "  • linear_correlation_addconst.hpp   - carry-matrix chain (O(n))")
message(STATUS "")
message(STATUS "🎯 構建目標：")
message(STATUS "  • neoalzette (static library)    - NeoAlzette庫")
if(NEOALZETTE_BUILD_PROGRAMS)
    message(STATUS "  • test_neoalzette_arx_trace      - 差分 trace")
    message(STATUS "  • test_neoalzette_differential_best_search - 最佳差分 trail 搜尋")
    message(STATUS "  • test_neoalzette_linear_best_search - 最佳線性 trail 搜尋")
    message(STATUS "  • test_neoalzette_arx_probabilistic_neutral_bits - PNB-style + heatmap（CSV）")
    message(STATUS "  • test_neoalzette_arx_probabilistic_neutral_bits_average - average PNB（原始碼設定）")
else()
    message(STATUS "  • (programs disabled) set -DNEOALZETTE_BUILD_PROGRAMS=ON to build test_*.cpp")
endif()
message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "")
