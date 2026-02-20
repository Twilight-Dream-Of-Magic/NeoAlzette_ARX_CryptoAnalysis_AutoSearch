cmake_minimum_required(VERSION 3.15) # for CMAKE_MSVC_RUNTIME_LIBRARY

set(_neoalzette_visual_studio_generator OFF)
if(DEFINED CMAKE_GENERATOR AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set(_neoalzette_visual_studio_generator ON)
endif()

if(WIN32 AND NOT _neoalzette_visual_studio_generator AND NOT DEFINED CMAKE_CXX_COMPILER AND DEFINED ENV{CLANG64})
    file(TO_CMAKE_PATH "$ENV{CLANG64}" _neoalzette_clang64_bin)
    if(EXISTS "${_neoalzette_clang64_bin}/clang++.exe")
        set(CMAKE_CXX_COMPILER "${_neoalzette_clang64_bin}/clang++.exe" CACHE FILEPATH "Preferred C++ compiler" FORCE)
    endif()
elseif(WIN32 AND _neoalzette_visual_studio_generator AND DEFINED ENV{CLANG64})
    message(STATUS "Skipping CLANG64 auto-compiler selection for Visual Studio generator; use -G Ninja to build with MinGW clang++.")
endif()

project(NeoAlzette_ARX_CryptoAnalysis_AutoSearch VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Some MinGW/clang setups in this repo are reported by CMake as "compiler
# identification is unknown", so keep the explicit standard flag as a fallback.
if(MSVC)
    add_compile_options(/std:c++20 /utf-8)
else()
    add_compile_options(-std=c++20)
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Release, Debug, RelWithDebInfo, MinSizeRel)")
endif()

option(NEOALZETTE_BUILD_PROGRAMS "Build research programs (test_*.cpp)" ON)

function(neoalzette_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
    endif()
endfunction()

function(neoalzette_apply_windows_system_libs target_name)
    if(WIN32)
        if(MSVC)
            target_link_libraries(${target_name} PRIVATE psapi)
        else()
            target_link_options(${target_name} PRIVATE -lpsapi)
        endif()
    endif()
endfunction()

option(NEOALZETTE_MSVC_STATIC_RUNTIME "Use static MSVC runtime (/MT, /MTd)" ON)
if(MSVC AND NEOALZETTE_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(_NEOALZETTE_DEFAULT_MINGW_STATIC OFF)
if(WIN32 AND NOT MSVC)
    set(_NEOALZETTE_DEFAULT_MINGW_STATIC ON)
endif()
option(NEOALZETTE_MINGW_STATIC "Link executables statically on MinGW/Clang (adds -static flags)" ${_NEOALZETTE_DEFAULT_MINGW_STATIC})

function(neoalzette_apply_static_linking target_name)
    if(WIN32 AND NOT MSVC AND NEOALZETTE_MINGW_STATIC)
        target_link_options(${target_name} PRIVATE -static)
    endif()
endfunction()

add_library(neoalzette STATIC
    src/neoalzette/neoalzette_core.cpp
)
set_target_properties(neoalzette PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(neoalzette PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
neoalzette_apply_warnings(neoalzette)

if(NEOALZETTE_BUILD_PROGRAMS)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    set(NEOALZETTE_BEST_SEARCH_SHARED_SOURCES
        src/auto_search_frame/best_search_shared_core.cpp
    )

    set(NEOALZETTE_LINEAR_BEST_SEARCH_SOURCES
        ${NEOALZETTE_BEST_SEARCH_SHARED_SOURCES}
        src/auto_search_frame/linear_best_search_math.cpp
        src/auto_search_frame/linear_best_search_checkpoint.cpp
        src/auto_search_frame/linear_best_search_engine.cpp
        src/auto_search_frame/linear_best_search_collector.cpp
    )

    set(NEOALZETTE_DIFFERENTIAL_BEST_SEARCH_SOURCES
        ${NEOALZETTE_BEST_SEARCH_SHARED_SOURCES}
        src/auto_search_frame/differential_best_search_math.cpp
        src/auto_search_frame/differential_best_search_checkpoint.cpp
        src/auto_search_frame/differential_best_search_engine.cpp
        src/auto_search_frame/differential_best_search_collector.cpp
    )

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
        src/auto_search_frame/linear_best_search_math.cpp
        ${NEOALZETTE_DIFFERENTIAL_BEST_SEARCH_SOURCES}
    )
    target_link_libraries(test_neoalzette_differential_best_search PRIVATE neoalzette Threads::Threads)
    neoalzette_apply_windows_system_libs(test_neoalzette_differential_best_search)
    target_include_directories(test_neoalzette_differential_best_search PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_differential_best_search)
    neoalzette_apply_static_linking(test_neoalzette_differential_best_search)

    add_executable(test_neoalzette_linear_best_search
        test_neoalzette_linear_best_search.cpp
        test_arx_operator_self_test.cpp
        common/runtime_component.cpp
        src/auto_search_frame/differential_best_search_math.cpp
        ${NEOALZETTE_LINEAR_BEST_SEARCH_SOURCES}
    )
    target_link_libraries(test_neoalzette_linear_best_search PRIVATE neoalzette Threads::Threads)
    neoalzette_apply_windows_system_libs(test_neoalzette_linear_best_search)
    target_include_directories(test_neoalzette_linear_best_search PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_linear_best_search)
    neoalzette_apply_static_linking(test_neoalzette_linear_best_search)

    add_executable(test_neoalzette_differential_hull_wrapper
        test_neoalzette_differential_hull_wrapper.cpp
        common/runtime_component.cpp
        ${NEOALZETTE_DIFFERENTIAL_BEST_SEARCH_SOURCES}
    )
    target_link_libraries(test_neoalzette_differential_hull_wrapper PRIVATE neoalzette Threads::Threads)
    neoalzette_apply_windows_system_libs(test_neoalzette_differential_hull_wrapper)
    target_include_directories(test_neoalzette_differential_hull_wrapper PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_differential_hull_wrapper)
    neoalzette_apply_static_linking(test_neoalzette_differential_hull_wrapper)

    add_executable(test_neoalzette_linear_hull_wrapper
        test_neoalzette_linear_hull_wrapper.cpp
        common/runtime_component.cpp
        ${NEOALZETTE_LINEAR_BEST_SEARCH_SOURCES}
    )
    target_link_libraries(test_neoalzette_linear_hull_wrapper PRIVATE neoalzette Threads::Threads)
    neoalzette_apply_windows_system_libs(test_neoalzette_linear_hull_wrapper)
    target_include_directories(test_neoalzette_linear_hull_wrapper PRIVATE
        ${PROJECT_SOURCE_DIR}
    )
    neoalzette_apply_warnings(test_neoalzette_linear_hull_wrapper)
    neoalzette_apply_static_linking(test_neoalzette_linear_hull_wrapper)

endif()

install(TARGETS neoalzette
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h"
)

message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "NeoAlzette ARX CryptoAnalysis AutoSearch")
message(STATUS "=======================================================")
message(STATUS "")
message(STATUS "Project layout:")
message(STATUS "  include/arx_analysis_operators/  - Header-only ARX analysis operators")
message(STATUS "  include/neoalzette/              - NeoAlzette interfaces")
message(STATUS "  include/auto_search_frame/       - best-search / hull / auto-pipeline framework")
message(STATUS "")
message(STATUS "Key operator headers:")
message(STATUS "  differential_xdp_add.hpp          - LM-2001 Algorithm 2 (O(1))")
message(STATUS "  differential_optimal_gamma.hpp    - LM-2001 Algorithm 4 (O(log n))")
message(STATUS "  differential_addconst.hpp         - exact count/weight + Qk BvWeight approx (O(n))")
message(STATUS "  linear_correlation_add_logn.hpp   - Wallen CPM/logn reference + regression path")
message(STATUS "  linear_correlation_addconst.hpp   - carry-matrix chain (O(n))")
message(STATUS "")
message(STATUS "Build targets:")
message(STATUS "  neoalzette                         - static library")
if(NEOALZETTE_BUILD_PROGRAMS)
    message(STATUS "  test_neoalzette_arx_trace          - trace exploration")
    message(STATUS "  test_neoalzette_differential_best_search - differential best-search / auto pipeline")
    message(STATUS "  test_neoalzette_linear_best_search - linear best-search / auto pipeline")
    message(STATUS "  test_neoalzette_differential_hull_wrapper - strict differential hull wrapper")
    message(STATUS "  test_neoalzette_linear_hull_wrapper - strict linear hull wrapper")
else()
    message(STATUS "  (programs disabled) set -DNEOALZETTE_BUILD_PROGRAMS=ON to build test_*.cpp")
endif()
message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "")
