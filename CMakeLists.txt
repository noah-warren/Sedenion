cmake_minimum_required(VERSION 3.16)
project(Sedenion VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W3 /EHsc /bigobj /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES WIN32_LEAN_AND_MEAN)
  # The CAS and parser recurse deeply on large expressions; the 1 MB default stack is too small.
  add_link_options(/STACK:16777216)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

file(GLOB_RECURSE SEDENION_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM SEDENION_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_library(sedenion_core STATIC ${SEDENION_CORE_SOURCES})
target_include_directories(sedenion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(sedenion src/main.cpp)
target_link_libraries(sedenion PRIVATE sedenion_core)
if(WIN32)
  target_link_libraries(sedenion PRIVATE shell32)
endif()

enable_testing()

file(GLOB SEDENION_UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp)
add_executable(sedenion_unit_tests ${SEDENION_UNIT_TESTS})
target_link_libraries(sedenion_unit_tests PRIVATE sedenion_core)

add_executable(sedenion_script_tests tests/script_runner.cpp)
target_link_libraries(sedenion_script_tests PRIVATE sedenion_core)

add_test(NAME unit_tests COMMAND sedenion_unit_tests)
add_test(NAME script_tests COMMAND sedenion_script_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts)

file(GLOB SEDENION_EXAMPLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.sed)
foreach(example ${SEDENION_EXAMPLES})
  get_filename_component(example_name ${example} NAME_WE)
  add_test(NAME example_${example_name} COMMAND sedenion ${example})
endforeach()
