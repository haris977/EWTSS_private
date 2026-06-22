cmake_minimum_required(VERSION 3.20)
project(esme_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- ESME MONUV (HF/VU) parser DLL ----
add_library(esme_monvuhf SHARED
    src/esme_parser_monvuhf.cpp
)
target_include_directories(esme_monvuhf PRIVATE ../dp_ecm/include)

if(MSVC)
    target_compile_options(esme_monvuhf PRIVATE /W4 /permissive-)
    set_target_properties(esme_monvuhf PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/esme_monvuhf.def")
else()
    target_compile_options(esme_monvuhf PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_esme_monvuhf
    tests/test_frames_esme.cpp
    src/esme_parser_monvuhf.cpp
)
target_include_directories(test_esme_monvuhf PRIVATE ../dp_ecm/include)
if(NOT MSVC)
    target_compile_options(test_esme_monvuhf PRIVATE -Wall -Wextra)
endif()
add_test(NAME esme_monvuhf_frames COMMAND test_esme_monvuhf)
