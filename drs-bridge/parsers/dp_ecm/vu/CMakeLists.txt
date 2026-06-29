cmake_minimum_required(VERSION 3.20)
project(dp_ecm_vu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- VU DLL (30-6000 MHz) ----
add_library(dp_ecm_vu SHARED
    ../src/sdfc_frame.cpp
    ../src/dp_ecm_vu_parser.cpp
)
target_include_directories(dp_ecm_vu PRIVATE ../include)

if(MSVC)
    target_compile_options(dp_ecm_vu PRIVATE /W4 /permissive-)
    set_target_properties(dp_ecm_vu PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/../dp_ecm_vu.def")
else()
    target_compile_options(dp_ecm_vu PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- VU unit tests ----
enable_testing()

add_executable(test_dp_ecm_vu
    ../tests/test_frames_vu.cpp
    ../src/sdfc_frame.cpp
    ../src/dp_ecm_vu_parser.cpp
)
target_include_directories(test_dp_ecm_vu PRIVATE ../include)
if(NOT MSVC)
    target_compile_options(test_dp_ecm_vu PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_vu_frames COMMAND test_dp_ecm_vu)
