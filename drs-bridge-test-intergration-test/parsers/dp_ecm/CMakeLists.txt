cmake_minimum_required(VERSION 3.20)
project(dp_ecm_parsers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)   # only .def / SDFC_EXPORT symbols are visible

# ---- Shared Family-A frame core (compiled into each variant DLL) ----
set(SDFC_CORE_SRC src/sdfc_frame.cpp)

# ---- DP-ECM-1071 HF parser DLL (1–30 MHz) ----
add_library(dp_ecm_hf SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    src/dp_ecm_hf_encoder.cpp
)
target_include_directories(dp_ecm_hf PRIVATE include)

if(MSVC)
    target_compile_options(dp_ecm_hf PRIVATE /W4 /permissive-)
    set_target_properties(dp_ecm_hf PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/dp_ecm_hf.def")
else()
    target_compile_options(dp_ecm_hf PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- DP-ECM-1074 VU parser DLL (30–6000 MHz) ----
add_library(dp_ecm_vu SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_vu_parser.cpp
)
target_include_directories(dp_ecm_vu PRIVATE include)

if(MSVC)
    target_compile_options(dp_ecm_vu PRIVATE /W4 /permissive-)
    set_target_properties(dp_ecm_vu PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/dp_ecm_vu.def")
else()
    target_compile_options(dp_ecm_vu PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_dp_ecm
    tests/test_frames.cpp
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
    src/dp_ecm_hf_encoder.cpp
)
target_include_directories(test_dp_ecm PRIVATE include)
if(NOT MSVC)
    target_compile_options(test_dp_ecm PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_frames COMMAND test_dp_ecm)

add_executable(test_dp_ecm_vu
    tests/test_frames_vu.cpp
    ${SDFC_CORE_SRC}
    src/dp_ecm_vu_parser.cpp
)
target_include_directories(test_dp_ecm_vu PRIVATE include)
if(NOT MSVC)
    target_compile_options(test_dp_ecm_vu PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_vu_frames COMMAND test_dp_ecm_vu)
