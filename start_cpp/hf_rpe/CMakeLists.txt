cmake_minimum_required(VERSION 3.20)
project(hf_rpe_parser CXX)

# DP-ECM-1071  HF Receiver/Processor/Exciter  1 – 30 MHz
add_library(hf_rpe_parser SHARED
    ${SDFC_CORE}
    src/hf_rpe_parser.cpp
)
target_include_directories(hf_rpe_parser PRIVATE ${DP_ECM_INC})
set_target_properties(hf_rpe_parser PROPERTIES PREFIX "")   # hf_rpe_parser.dll / .so

if(MSVC)
    target_compile_options(hf_rpe_parser PRIVATE /W4 /permissive-)
    set_target_properties(hf_rpe_parser PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/hf_rpe_parser.def")
else()
    target_compile_options(hf_rpe_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Tests ----
enable_testing()
add_executable(test_hf_rpe tests/test_hf_rpe.cpp ${SDFC_CORE} src/hf_rpe_parser.cpp)
target_include_directories(test_hf_rpe PRIVATE ${DP_ECM_INC})
if(NOT MSVC)
    target_compile_options(test_hf_rpe PRIVATE -Wall -Wextra)
endif()
add_test(NAME hf_rpe_frames COMMAND test_hf_rpe)
