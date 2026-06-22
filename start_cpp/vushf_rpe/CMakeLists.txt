cmake_minimum_required(VERSION 3.20)
project(vushf_rpe_parser CXX)

# DP-ECM-1074  VHF/UHF/SHF Receiver/Processor/Exciter  30 – 6000 MHz
add_library(vushf_rpe_parser SHARED
    ${SDFC_CORE}
    src/vushf_rpe_parser.cpp
)
target_include_directories(vushf_rpe_parser PRIVATE ${DP_ECM_INC})
set_target_properties(vushf_rpe_parser PROPERTIES PREFIX "")  # vushf_rpe_parser.dll / .so

if(MSVC)
    target_compile_options(vushf_rpe_parser PRIVATE /W4 /permissive-)
    set_target_properties(vushf_rpe_parser PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/vushf_rpe_parser.def")
else()
    target_compile_options(vushf_rpe_parser PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Tests ----
enable_testing()
add_executable(test_vushf_rpe tests/test_vushf_rpe.cpp ${SDFC_CORE} src/vushf_rpe_parser.cpp)
target_include_directories(test_vushf_rpe PRIVATE ${DP_ECM_INC})
if(NOT MSVC)
    target_compile_options(test_vushf_rpe PRIVATE -Wall -Wextra)
endif()
add_test(NAME vushf_rpe_frames COMMAND test_vushf_rpe)
