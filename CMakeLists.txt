cmake_minimum_required(VERSION 3.0)
project(funscript LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

IF (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined")
    add_definitions(-D_GLIBCXX_DEBUG)
ENDIF ()

add_library(funscript src/tokenizer.cpp src/ast.cpp src/ast_parser.cpp src/ast_assembler.cpp src/mm.cpp src/vm.cpp)
target_include_directories(funscript PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
target_link_libraries(tests-catch PRIVATE funscript)
target_link_libraries(tests-catch PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(tests-catch)

add_executable(funscript-bin src/main.cpp)
target_include_directories(funscript-bin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(funscript-bin PRIVATE funscript)
set_target_properties(funscript-bin PROPERTIES OUTPUT_NAME funscript)