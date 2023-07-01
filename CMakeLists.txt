cmake_minimum_required(VERSION 3.0)
project(funscript LANGUAGES CXX)

# General compilation configuration

set(CMAKE_CXX_STANDARD 20)

IF (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined -fno-sanitize-recover")
    add_definitions(-D_GLIBCXX_DEBUG)
ENDIF ()

# Funscript libraries (static and dynamic)

add_library(funscript-static STATIC src/tokenizer.cpp src/ast.cpp src/ast_parser.cpp src/ast_assembler.cpp src/mm.cpp src/vm.cpp)
target_include_directories(funscript-static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(funscript-static PROPERTIES OUTPUT_NAME funscript)

add_library(funscript-shared SHARED src/tokenizer.cpp src/ast.cpp src/ast_parser.cpp src/ast_assembler.cpp src/mm.cpp src/vm.cpp)
target_include_directories(funscript-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(funscript-shared PROPERTIES OUTPUT_NAME funscript)

# Main executable

add_executable(funscript-bin src/main.cpp)
target_include_directories(funscript-bin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(funscript-bin PRIVATE funscript-static)
set_target_properties(funscript-bin PROPERTIES OUTPUT_NAME funscript)

# REPL executable (testing only)

add_executable(repl src/repl.cpp)
target_include_directories(repl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(repl PRIVATE funscript-static)

# Standard library (native Funscript module)

add_library(stdfs SHARED src/stdlib.cpp)
target_include_directories(stdfs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stdfs PRIVATE funscript-shared)

# Catch2 tests

find_package(Catch2 3 QUIET)

if (NOT Catch2_FOUND)
    Include(FetchContent)
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.3.2
    )
    FetchContent_MakeAvailable(Catch2)
endif ()

add_executable(tests-catch tests/tests.cpp)
target_include_directories(tests-catch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tests-catch PRIVATE funscript-static)
target_link_libraries(tests-catch PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(tests-catch)