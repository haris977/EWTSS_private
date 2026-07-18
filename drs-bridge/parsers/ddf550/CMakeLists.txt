cmake_minimum_required(VERSION 3.20)
project(ddf550_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- DDF-550 parser DLL ----
add_library(ddf550 SHARED
    src/ddf550_parser.cpp
    ../utils/pugixml_generic_mirror.cpp
    ../utils/pugixml-1.16/pugixml.cpp
    ../utils/json_kwargs.cpp
    ../utils/eb200_shared.cpp
)
target_include_directories(ddf550 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)

if(MSVC)
    target_compile_options(ddf550 PRIVATE /W4 /permissive-)
    set_target_properties(ddf550 PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ddf550.def")
else()
    target_compile_options(ddf550 PRIVATE -Wall -Wextra -Wpedantic)
    # Static-link the MinGW runtime so ctypes.CDLL() can load this DLL
    # without libstdc++-6.dll/libgcc_s_seh-1.dll needing to be on PATH
    # (Python's ctypes uses the restricted DLL search path since 3.8+,
    # which does not consult PATH the way a normal EXE launch would).
    target_link_options(ddf550 PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_ddf550
    tests/test_frames_ddf550.cpp
    src/ddf550_parser.cpp
    ../utils/pugixml_generic_mirror.cpp
    ../utils/pugixml-1.16/pugixml.cpp
    ../utils/json_kwargs.cpp
    ../utils/eb200_shared.cpp
)
target_include_directories(test_ddf550 PRIVATE ../dp_ecm/include ../utils/pugixml-1.16 ../utils)
if(NOT MSVC)
    target_compile_options(test_ddf550 PRIVATE -Wall -Wextra)
endif()
add_test(NAME ddf550_frames COMMAND test_ddf550)
