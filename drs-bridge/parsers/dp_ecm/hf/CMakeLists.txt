cmake_minimum_required(VERSION 3.20)
project(dp_ecm_hf CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- HF DLL (1-30 MHz) ----
add_library(dp_ecm_hf SHARED
    ../src/sdfc_frame.cpp
    ../src/dp_ecm_hf_parser.cpp
)
target_include_directories(dp_ecm_hf PRIVATE ../include)

if(MSVC)
    target_compile_options(dp_ecm_hf PRIVATE /W4 /permissive-)
    set_target_properties(dp_ecm_hf PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/../dp_ecm_hf.def")
else()
    target_compile_options(dp_ecm_hf PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- HF unit tests ----
enable_testing()

add_executable(test_dp_ecm_hf
    ../tests/test_frames.cpp
    ../src/sdfc_frame.cpp
    ../src/dp_ecm_hf_parser.cpp
)
target_include_directories(test_dp_ecm_hf PRIVATE ../include)
if(NOT MSVC)
    target_compile_options(test_dp_ecm_hf PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_hf_frames COMMAND test_dp_ecm_hf)
