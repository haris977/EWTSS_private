cmake_minimum_required(VERSION 3.20)
project(dp_ecm_parsers CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)   # only .def / SDFC_EXPORT symbols are visible

# ---- Shared Family-A frame core (compiled into each variant DLL) ----
set(SDFC_CORE_SRC src/sdfc_frame.cpp)

# ---- DP-ECM-1071 HF parser DLL ----
add_library(dp_ecm_hf SHARED
    ${SDFC_CORE_SRC}
    src/dp_ecm_hf_parser.cpp
)
target_include_directories(dp_ecm_hf PRIVATE include)

if(MSVC)
    target_compile_options(dp_ecm_hf PRIVATE /W4 /permissive-)
    # Restrict exports to the 4 ABI symbols via the module-definition file.
    set_target_properties(dp_ecm_hf PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/dp_ecm_hf.def")
else()
    target_compile_options(dp_ecm_hf PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()
add_executable(test_dp_ecm tests/test_frames.cpp ${SDFC_CORE_SRC} src/dp_ecm_hf_parser.cpp)
target_include_directories(test_dp_ecm PRIVATE include)
# Tests link the parser sources directly so they can call the ABI symbols in-process.
if(NOT MSVC)
    target_compile_options(test_dp_ecm PRIVATE -Wall -Wextra)
endif()
add_test(NAME dp_ecm_frames COMMAND test_dp_ecm)
