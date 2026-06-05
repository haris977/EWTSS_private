# parsers/cpp/tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# ── GoogleTest ────────────────────────────────────────────────────────────────
# Vendored under packages/cpp/googletest/ for air-gap install.
# No FetchContent — internet-required downloads are forbidden.
set(GTEST_ROOT "${CMAKE_SOURCE_DIR}/../../../packages/cpp/googletest")

add_subdirectory(${GTEST_ROOT} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)

# ── comm_df test binary ───────────────────────────────────────────────────────
add_executable(test_comm_df
    comm_df/test_comm_df_parser.cpp
)

target_include_directories(test_comm_df PRIVATE
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/comm_df
    ${GTEST_ROOT}/googletest/include
)

target_link_libraries(test_comm_df PRIVATE
    gtest
    gtest_main
)

set_target_properties(test_comm_df PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Register with CTest
include(GoogleTest)
gtest_discover_tests(test_comm_df)
