cmake_minimum_required(VERSION 3.10)

project(rvdbt C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu11")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=gnu++20")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}") # TODO: -fno-rtti
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c99-designator")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-offsetof")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")

FIND_PACKAGE(Boost 1.71 COMPONENTS program_options REQUIRED)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR})

add_subdirectory(dbt)

file(GLOB_RECURSE CLANG_FORMAT_TARGETS dbt/*.h dbt/*.cpp)
list(FILTER CLANG_FORMAT_TARGETS EXCLUDE REGEX ".*third_party.*$")

add_custom_target(
    clang-format
    COMMAND /usr/bin/clang-format
    -i
    ${CLANG_FORMAT_TARGETS}
)
