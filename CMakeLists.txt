cmake_minimum_required(VERSION 3.20)
project(minisearch CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # /utf-8: sources contain UTF-8 Chinese literals. /Zc:__cplusplus: correct C++20 detection.
  add_compile_options(/utf-8 /Zc:__cplusplus /W3 /EHsc /MP)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(MS_CORE_SOURCES
  src/json.cpp
  src/text.cpp
  src/index.cpp
  src/seed.cpp
  src/http.cpp
)

add_executable(minisearch src/main.cpp ${MS_CORE_SOURCES})
target_include_directories(minisearch PRIVATE src)
if(WIN32)
  target_link_libraries(minisearch PRIVATE ws2_32)
endif()
target_compile_features(minisearch PRIVATE cxx_std_20)

# Self tests for JSON / VByte / tokenizer / highlights / index (no sockets).
add_executable(selftest tests/selftest.cpp src/json.cpp src/text.cpp src/index.cpp src/seed.cpp)
target_include_directories(selftest PRIVATE src)
target_compile_features(selftest PRIVATE cxx_std_20)

# Building the demo corpus + benchmark live in bench/ (Python), no CMake target needed.
