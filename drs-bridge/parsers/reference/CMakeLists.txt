# drs-bridge/parsers/reference/CMakeLists.txt
#
# Builds the reference variant parser as a shared library:
#   • reference_parser.dll      on Windows (MSBuild / Ninja-Multi-Config)
#   • reference_parser.so       on Linux (Make / Ninja) — see PREFIX rule below
#
# Variant developers copy this file alongside the source, change the
# project name + target name, and otherwise leave it untouched.

cmake_minimum_required(VERSION 3.20)
project(reference_parser LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)  # only PARSER_EXPORT-tagged symbols

add_library(reference_parser SHARED
    src/reference_parser.cpp
)

target_include_directories(reference_parser PUBLIC include)

if(MSVC)
    target_compile_options(reference_parser PRIVATE /W4 /WX)
else()
    target_compile_options(reference_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# On Linux, drop the "lib" prefix so ctypes loads with the same name
# convention regardless of platform: reference_parser.dll vs
# reference_parser.so. Comment out if you prefer the standard libfoo.so.
if(NOT WIN32)
    set_target_properties(reference_parser PROPERTIES PREFIX "")
endif()
