cmake_minimum_required(VERSION 3.16)
project(ozaki LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target is Sapphire Rapids (AMX). Turn off for a plain local build.
option(OZAKI_NATIVE_ARCH "Compile with -march=sapphirerapids" ON)

find_package(OpenMP)

add_library(ozaki src/ozaki_gemm.cpp)
target_include_directories(ozaki PUBLIC include)
if(OpenMP_CXX_FOUND)
  target_link_libraries(ozaki PUBLIC OpenMP::OpenMP_CXX)
endif()
if(OZAKI_NATIVE_ARCH AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ozaki PUBLIC -march=sapphirerapids)
endif()

enable_testing()
add_executable(ozaki_tests tests/test_main.cpp)
target_link_libraries(ozaki_tests PRIVATE ozaki)
add_test(NAME ozaki_tests COMMAND ozaki_tests)
