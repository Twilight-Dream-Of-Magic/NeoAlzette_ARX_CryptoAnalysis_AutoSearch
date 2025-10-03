cmake_minimum_required(VERSION 3.16)
project(neoalzette_search CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(neoalzette STATIC
    src/neoalzette.cpp
)
target_include_directories(neoalzette PUBLIC include)

add_executable(diff_search src/main_diff.cpp src/bnb.cpp src/lm_wallen.cpp src/neoalzette.cpp)
target_include_directories(diff_search PUBLIC include)
target_link_libraries(diff_search PRIVATE neoalzette)

add_executable(lin_search src/main_lin.cpp src/bnb.cpp src/lm_wallen.cpp src/neoalzette.cpp)
target_include_directories(lin_search PUBLIC include)
target_link_libraries(lin_search PRIVATE neoalzette)

add_executable(pddt_demo src/main_pddt.cpp src/pddt.cpp)
add_executable(threshold_demo src/main_threshold.cpp src/neoalzette.cpp)
target_include_directories(pddt_demo PUBLIC include)
target_include_directories(threshold_demo PUBLIC include)
target_link_libraries(threshold_demo PRIVATE neoalzette)

add_executable(gen_round_lb_table src/gen_round_lb_table.cpp)

add_executable(highway_table_build src/highway_table_build.cpp)

add_executable(threshold_lin src/threshold_lin.cpp)

add_executable(search_beam_diff src/search_beam_diff.cpp)

add_executable(milp_diff src/milp_diff.cpp)
