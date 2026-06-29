cmake_minimum_required(VERSION 3.20)
project(dp_ecm_parsers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

enable_testing()

# Each variant is fully self-contained — build independently if the other is absent.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/dp_ecm_hf_parser.cpp")
    add_subdirectory(hf)
else()
    message(STATUS "dp_ecm_hf: source not found, skipping")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/dp_ecm_vu_parser.cpp")
    add_subdirectory(vu)
else()
    message(STATUS "dp_ecm_vu: source not found, skipping")
endif()
