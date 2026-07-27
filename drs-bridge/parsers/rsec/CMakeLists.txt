cmake_minimum_required(VERSION 3.20)
project(rsec_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- HIMSHAKTI RSEC parser DLL ----
add_library(rsec SHARED
    src/rsec_parser.cpp
)
target_include_directories(rsec PRIVATE ../dp_ecm/include)

if(MSVC)
    target_compile_options(rsec PRIVATE /W4 /permissive-)
    set_target_properties(rsec PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/rsec.def")
else()
    target_compile_options(rsec PRIVATE -Wall -Wextra -Wpedantic)
    # Static-link the MinGW runtime so ctypes.CDLL() can load this DLL
    # without libstdc++-6.dll/libgcc_s_seh-1.dll needing to be on PATH
    # (Python's ctypes uses the restricted DLL search path since 3.8+,
    # which does not consult PATH the way a normal EXE launch would).
    target_link_options(rsec PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_rsec
    tests/test_frames_rsec.cpp
    src/rsec_parser.cpp
)
target_include_directories(test_rsec PRIVATE ../dp_ecm/include)
if(NOT MSVC)
    target_compile_options(test_rsec PRIVATE -Wall -Wextra)
endif()
add_test(NAME rsec_frames COMMAND test_rsec)
