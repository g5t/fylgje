cmake_minimum_required(VERSION 3.20)
project(fylgje LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -pedantic)
endif()

# wheels only need the executable
if(SKBUILD)
  set(fylgje_tests_default OFF)
else()
  set(fylgje_tests_default ON)
endif()
option(FYLGJE_BUILD_TESTS "Build the test suite" ${fylgje_tests_default})

add_subdirectory(src)

if(FYLGJE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
