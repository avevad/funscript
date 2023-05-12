cmake_minimum_required(VERSION 3.25)
project(funscript)

set(CMAKE_CXX_STANDARD 20)

IF (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined")
    add_definitions(-D_GLIBCXX_DEBUG)
ENDIF ()

add_executable(funscript src/tokenizer.cpp src/ast.cpp src/ast_parser.cpp src/ast_assembler.cpp src/vm.cpp src/main.cpp)
