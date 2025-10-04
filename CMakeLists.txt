cmake_minimum_required(VERSION 3.16)
project(neoalzette_search CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native")
endif()

# Find OpenMP for parallel optimizations
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found, enabling parallel optimizations")
else()
    message(STATUS "OpenMP not found, building without parallel optimizations")
endif()
add_library(neoalzette STATIC
    src/neoalzette_core.cpp
    src/medcp_analyzer.cpp
    src/melcc_analyzer.cpp
    src/threshold_search_framework.cpp
    src/utility_tools.cpp
    src/matsui_algorithm2_complete.cpp
    src/pddt_algorithm1_complete.cpp
    # NeoAlzette專門模型
    src/neoalzette_differential_model.cpp
    src/neoalzette_linear_model.cpp
    src/neoalzette_medcp_analyzer.cpp
    src/neoalzette_melcc_analyzer.cpp
)
target_include_directories(neoalzette PUBLIC include)

add_executable(pddt_demo src/main_pddt.cpp src/pddt.cpp)
target_include_directories(pddt_demo PUBLIC include)
target_link_libraries(pddt_demo PRIVATE neoalzette)

add_executable(highway_table_build src/highway_table_build.cpp)
target_include_directories(highway_table_build PUBLIC include)
target_link_libraries(highway_table_build PRIVATE neoalzette)

add_executable(highway_table_build_lin src/highway_table_build_lin.cpp)
target_include_directories(highway_table_build_lin PUBLIC include)
target_link_libraries(highway_table_build_lin PRIVATE neoalzette)

# Complete Matsui algorithms implementation
add_executable(complete_matsui_demo src/complete_matsui_demo.cpp)
target_include_directories(complete_matsui_demo PUBLIC include)
target_link_libraries(complete_matsui_demo PRIVATE neoalzette)

# Paper algorithms demo (Algorithm 1 & 2 complete implementation)

# NeoAlzette MEDCP/MELCC analysis demo

# Test for Wallén 2003 exact linear correlation (add/sub const)

# Test for Theorem 2 correctness (differential add/sub const)

if(NOT NA_BUILD_DEMOS)
  set_target_properties(pddt_demo PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  set_target_properties(highway_table_build PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  set_target_properties(highway_table_build_lin PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  set_target_properties(complete_matsui_demo PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()

