cmake_minimum_required(VERSION 3.20)
project(ewtss_parsers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Shared SDFC frame infrastructure (headers + frame-scanner source).
# Located in drs-bridge/parsers/dp_ecm — no source duplication.
set(DP_ECM_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../drs-bridge/parsers/dp_ecm")
set(DP_ECM_INC  "${DP_ECM_DIR}/include")
set(SDFC_CORE   "${DP_ECM_DIR}/src/sdfc_frame.cpp")

enable_testing()

add_subdirectory(hf_rpe)
add_subdirectory(vushf_rpe)
