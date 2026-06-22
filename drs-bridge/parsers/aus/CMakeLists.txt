cmake_minimum_required(VERSION 3.20)
project(aus_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# ---- JSON parsing dependency ----
# Option A (default): FetchContent — pulls nlohmann/json v3.11.3 on first configure.
# Option B (offline): vendor the single header. Download
#   https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
#   to vendor/nlohmann/json.hpp, then:
#   1. Comment out the FetchContent block below.
#   2. Add: target_include_directories(aus PRIVATE vendor)
#      (and same for test_aus)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---- AUS Protocol Analyser parser DLL ----
add_library(aus SHARED
    src/aus_parser.cpp
)
target_include_directories(aus PRIVATE ../dp_ecm/include)
target_link_libraries(aus PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(aus PRIVATE /W4 /permissive-)
    set_target_properties(aus PROPERTIES LINK_FLAGS
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/aus.def")
else()
    target_compile_options(aus PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit tests (no external framework; plain asserts) ----
enable_testing()

add_executable(test_aus
    tests/test_aus_parser.cpp
    src/aus_parser.cpp
)
target_include_directories(test_aus PRIVATE ../dp_ecm/include)
target_link_libraries(test_aus PRIVATE nlohmann_json::nlohmann_json)
if(NOT MSVC)
    target_compile_options(test_aus PRIVATE -Wall -Wextra)
endif()
add_test(NAME aus_frames COMMAND test_aus)
