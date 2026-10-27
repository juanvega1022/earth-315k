cmake_minimum_required(VERSION 3.16)
project(Earth315kSimulation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_library(planet_core INTERFACE)
target_include_directories(planet_core INTERFACE include)
target_link_libraries(planet_core INTERFACE Threads::Threads)
# Unreal treats shadowed names as errors, so the core is kept clean of them here too.
if(MSVC)
  target_compile_options(planet_core INTERFACE /W3)
else()
  target_compile_options(planet_core INTERFACE -Wall -Wextra -Wshadow -Werror)
endif()

add_executable(earth315k_sim apps/run_sim.cpp)
target_link_libraries(earth315k_sim PRIVATE planet_core)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE planet_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(ecosystem_tests tests/ecosystem_tests.cpp)
target_link_libraries(ecosystem_tests PRIVATE planet_core)
add_test(NAME ecosystem_tests COMMAND ecosystem_tests)

add_executable(people_tests tests/people_tests.cpp)
target_link_libraries(people_tests PRIVATE planet_core)
add_test(NAME people_tests COMMAND people_tests)
