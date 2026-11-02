cmake_minimum_required(VERSION 3.21)
project(ozaki LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Benchmarks on an unoptimised build are meaningless: default to Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Target is Sapphire Rapids (AMX). Turn off for a plain local build.
option(OZAKI_NATIVE_ARCH "Compile with -march=sapphirerapids" ON)
option(OZAKI_WERROR "Treat warnings as errors" OFF)

find_package(OpenMP)

# Warning flags, applied PRIVATE to our own targets only.
add_library(ozaki_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ozaki_warnings INTERFACE -Wall -Wextra -Wpedantic)
  if(OZAKI_WERROR)
    target_compile_options(ozaki_warnings INTERFACE -Werror)
  endif()
elseif(MSVC)
  target_compile_options(ozaki_warnings INTERFACE /W4)
  if(OZAKI_WERROR)
    target_compile_options(ozaki_warnings INTERFACE /WX)
  endif()
endif()

add_library(ozaki src/ozaki_gemm.cpp)
target_include_directories(ozaki PUBLIC include)
target_link_libraries(ozaki PRIVATE ozaki_warnings)
if(OpenMP_CXX_FOUND)
  target_link_libraries(ozaki PUBLIC OpenMP::OpenMP_CXX)
endif()
if(OZAKI_NATIVE_ARCH AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ozaki PUBLIC -march=sapphirerapids)
endif()

enable_testing()
add_executable(ozaki_tests tests/test_main.cpp)
target_link_libraries(ozaki_tests PRIVATE ozaki ozaki_warnings)
add_test(NAME ozaki_tests COMMAND ozaki_tests)
