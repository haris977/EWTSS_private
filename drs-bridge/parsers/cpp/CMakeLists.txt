# parsers/cpp/CMakeLists.txt
#
# Builds one .dll per hardware variant and optionally the test suite.
#
# Prerequisites:
#   - Visual Studio 2022 (Developer Command Prompt, x64)
#   - CMake >= 3.20
#
# Build commands:
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64
#   cmake --build . --config Release
#
# Output DLLs land in:  build/Release/lib<variant>.dll
cmake_minimum_required(VERSION 3.20)
project(drs_parsers VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        /W4          # high warning level
        /WX          # warnings as errors — keep parser code clean
        /GS          # stack buffer overflow protection
        /sdl         # additional security development lifecycle checks
        /Gy          # function-level linking (smaller release binary)
    )
    add_compile_options(
        $<$<CONFIG:Release>:/O2>   # full optimisation in Release
        $<$<CONFIG:Release>:/GL>   # whole-program optimisation
        $<$<CONFIG:Debug>:/Od>     # no optimisation in Debug
        $<$<CONFIG:Debug>:/Zi>     # full debug info
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG> # link-time code generation
    )
endif()

# ── Hardware variant list ─────────────────────────────────────────────────────
# Add a new variant by:
#   1. Adding its name here
#   2. Creating cpp/<name>/<name>_frame_types.h
#   3. Creating cpp/<name>/<name>_parser.cpp
#   4. Creating cpp/<name>/<name>.def
set(HW_VARIANTS
    comm_df
    rdfs
    # jvuhf    # uncomment when ICD is received
    # jhf
    # sjrr
    # jlb
    # jmb
    # aus
    # pads
)

# ── One shared library (.dll) per variant ─────────────────────────────────────
foreach(HW IN LISTS HW_VARIANTS)
    add_library(${HW} SHARED
        ${HW}/${HW}_parser.cpp
    )

    target_include_directories(${HW} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/${HW}
    )

    # Explicit export list — only the 4 ABI symbols are visible
    target_link_options(${HW} PRIVATE
        /DEF:${CMAKE_CURRENT_SOURCE_DIR}/${HW}/${HW}.def
    )

    set_target_properties(${HW} PROPERTIES
        PREFIX          "lib"                 # output: libcomm_df.dll
        OUTPUT_NAME     "${HW}"
        # Static runtime — no MSVC runtime DLL dependency on customer machine
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        # DLLs land in build/Release/ (or build/Debug/)
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endforeach()

# ── Unit tests ────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build parser unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install rule (for CI artifact collection) ─────────────────────────────────
# Copies all built DLLs into packages/parsers/ for versioning and DVD install.
install(
    DIRECTORY   "${CMAKE_BINARY_DIR}/Release/"
    DESTINATION "${CMAKE_SOURCE_DIR}/../../../../packages/parsers"
    FILES_MATCHING PATTERN "lib*.dll"
)
