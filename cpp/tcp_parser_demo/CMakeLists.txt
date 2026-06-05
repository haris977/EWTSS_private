cmake_minimum_required(VERSION 3.20)
project(tcp_parser_demo LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tcp_parser_demo
    src/main.cpp
    src/binary_parser.cpp
    src/xml_parser.cpp
)

target_include_directories(tcp_parser_demo PRIVATE src)

# Winsock2 for TCP (Windows only — matches drs-bridge air-gapped Windows 11 target)
if(WIN32)
    target_link_libraries(tcp_parser_demo PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(tcp_parser_demo PRIVATE /W4 /wd4100)
else()
    target_compile_options(tcp_parser_demo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
