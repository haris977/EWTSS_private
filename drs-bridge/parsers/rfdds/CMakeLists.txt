cmake_minimum_required(VERSION 3.20)
project(rfdds_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(rfdds SHARED
    src/rfdds_parser.cpp
)
target_include_directories(rfdds PRIVATE ../dp_ecm/include)
target_link_libraries(rfdds PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(rfdds PRIVATE /W4 /permissive-)
    set_target_properties(rfdds PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/rfdds.def")
else()
    target_compile_options(rfdds PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(test_rfdds
    tests/test_rfdds_parser.cpp
    src/rfdds_parser.cpp
)
target_include_directories(test_rfdds PRIVATE ../dp_ecm/include)
target_link_libraries(test_rfdds PRIVATE nlohmann_json::nlohmann_json)
if(NOT MSVC)
    target_compile_options(test_rfdds PRIVATE -Wall -Wextra)
endif()
add_test(NAME rfdds_frames COMMAND test_rfdds)
