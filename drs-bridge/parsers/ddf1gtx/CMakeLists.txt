cmake_minimum_required(VERSION 3.20)
project(ddf1gtx_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- DDF-1GTX parser DLL ----
add_library(ddf1gtx SHARED
    src/ddf1gtx_parser.cpp
)
target_include_directories(ddf1gtx PRIVATE ../dp_ecm/include)

if(MSVC)
    target_compile_options(ddf1gtx PRIVATE /W4 /permissive-)
    set_target_properties(ddf1gtx PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ddf1gtx.def")
else()
    target_compile_options(ddf1gtx PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ddf1gtx
    tests/test_frames_ddf1gtx.cpp
    src/ddf1gtx_parser.cpp
)
target_include_directories(test_ddf1gtx PRIVATE ../dp_ecm/include)
if(NOT MSVC)
    target_compile_options(test_ddf1gtx PRIVATE -Wall -Wextra)
endif()
add_test(NAME ddf1gtx_frames COMMAND test_ddf1gtx)
