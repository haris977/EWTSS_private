cmake_minimum_required(VERSION 3.20)
project(ca120_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- CA120 parser DLL ----
add_library(ca120 SHARED
    src/ca120_parser.cpp
)
target_include_directories(ca120 PRIVATE ../dp_ecm/include)

if(MSVC)
    target_compile_options(ca120 PRIVATE /W4 /permissive-)
    set_target_properties(ca120 PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ca120.def")
else()
    target_compile_options(ca120 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ca120
    tests/test_frames_ca120.cpp
    src/ca120_parser.cpp
)
target_include_directories(test_ca120 PRIVATE ../dp_ecm/include)
if(NOT MSVC)
    target_compile_options(test_ca120 PRIVATE -Wall -Wextra)
endif()
add_test(NAME ca120_frames COMMAND test_ca120)
