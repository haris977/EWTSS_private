cmake_minimum_required(VERSION 3.20)
project(gnss_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# No external JSON library needed: input is plain NMEA-0183 ASCII text (no
# JSON to parse), output is built with the header-only JsonWriter. If a
# later pass implements format_response() with structured kwargs input,
# add nlohmann::json via FetchContent then (see aus/CMakeLists.txt).

add_library(gnss SHARED
    src/gnss_parser.cpp
)
target_include_directories(gnss PRIVATE ../dp_ecm/include)

if(MSVC)
    target_compile_options(gnss PRIVATE /W4 /permissive-)
    set_target_properties(gnss PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/gnss.def")
else()
    target_compile_options(gnss PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(test_gnss
    tests/test_gnss_parser.cpp
    src/gnss_parser.cpp
)
target_include_directories(test_gnss PRIVATE ../dp_ecm/include)
if(NOT MSVC)
    target_compile_options(test_gnss PRIVATE -Wall -Wextra)
endif()
add_test(NAME gnss_frames COMMAND test_gnss)
