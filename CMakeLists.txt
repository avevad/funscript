cmake_minimum_required(VERSION 3.20)
project(funscript)

set(CMAKE_CXX_STANDARD 20)

add_executable(funscript src/tokenizer.cpp src/compiler.cpp src/vm.cpp src/main.cpp)
