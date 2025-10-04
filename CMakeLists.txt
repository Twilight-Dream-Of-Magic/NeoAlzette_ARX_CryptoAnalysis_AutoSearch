cmake_minimum_required(VERSION 3.14)
project(NeoAlzette_ARX_CryptoAnalysis VERSION 1.0.0 LANGUAGES CXX)

# C++20標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 編譯選項
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# 包含目錄
include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# 底層ARX分析算子（Header-only）
# ============================================================================
# arx_analysis_operators/ 文件夾下的所有算子都是header-only
# 無需編譯，直接include即可使用

# ============================================================================
# NeoAlzette核心庫
# ============================================================================
set(NEOALZETTE_SOURCES
    src/neoalzette/neoalzette_core.cpp
    src/neoalzette/neoalzette_differential.cpp
    src/neoalzette/neoalzette_linear.cpp
    src/neoalzette/neoalzette_medcp.cpp
    src/neoalzette/neoalzette_melcc.cpp
)

add_library(neoalzette STATIC ${NEOALZETTE_SOURCES})
target_include_directories(neoalzette PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# ARX搜索框架庫
# ============================================================================
set(ARX_FRAMEWORK_SOURCES
    src/arx_search_framework/pddt_algorithm1_complete.cpp
    src/arx_search_framework/matsui_algorithm2_complete.cpp
    src/arx_search_framework/medcp_analyzer.cpp
    src/arx_search_framework/melcc_analyzer.cpp
    src/arx_search_framework/threshold_search_framework.cpp
    src/utility_tools.cpp
)

add_library(arx_framework STATIC ${ARX_FRAMEWORK_SOURCES})
target_include_directories(arx_framework PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(arx_framework PUBLIC neoalzette)

# ============================================================================
# 工具程序
# ============================================================================

# Highway表構建工具（差分）
add_executable(highway_table_build src/highway_table_build.cpp)
target_link_libraries(highway_table_build PRIVATE arx_framework neoalzette)

# Highway表構建工具（線性）
add_executable(highway_table_build_lin src/highway_table_build_lin.cpp)
target_link_libraries(highway_table_build_lin PRIVATE arx_framework neoalzette)

# ============================================================================
# 安裝配置
# ============================================================================
install(TARGETS neoalzette arx_framework
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)

install(DIRECTORY include/
        DESTINATION include
        FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h")

# ============================================================================
# 測試（可選）
# ============================================================================
option(BUILD_TESTS "Build test programs" OFF)
if(BUILD_TESTS)
    enable_testing()
    # 添加測試程序
endif()

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
message(STATUS "  include/arx_search_framework/    - 自動化搜索框架")
message(STATUS "  include/neoalzette/              - NeoAlzette專用")
message(STATUS "")
message(STATUS "📚 底層算子：")
message(STATUS "  • differential_xdp_add.hpp       - LM-2001 (O(1))")
message(STATUS "  • differential_addconst.hpp      - BvWeight (O(log²n))")
message(STATUS "  • linear_cor_add.hpp             - Wallén M_n^T (O(n))")
message(STATUS "  • linear_cor_addconst.hpp        - Wallén DP (O(n))")
message(STATUS "")
message(STATUS "🔍 搜索框架：")
message(STATUS "  • pddt/                          - pDDT構建")
message(STATUS "  • clat/                          - cLAT構建（Algorithm 1/2/3）")
message(STATUS "  • matsui/                        - Matsui閾值搜索")
message(STATUS "  • medcp_analyzer.hpp             - MEDCP分析")
message(STATUS "  • melcc_analyzer.hpp             - MELCC分析")
message(STATUS "")
message(STATUS "🎯 構建目標：")
message(STATUS "  • libneoalzette.a                - NeoAlzette庫")
message(STATUS "  • libarx_framework.a             - 搜索框架庫")
message(STATUS "  • highway_table_build            - Highway表工具（差分）")
message(STATUS "  • highway_table_build_lin        - Highway表工具（線性）")
message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "")
