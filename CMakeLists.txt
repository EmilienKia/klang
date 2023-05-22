cmake_minimum_required(VERSION 3.17)
project(klang)

set(CMAKE_CXX_STANDARD 20)

find_package(Catch2 REQUIRED)

add_executable(klang lexer.cpp parser.cpp ast.cpp main.cpp unit.cpp common.cpp common.hpp ast_unit_visitor.cpp ast_unit_visitor.hpp unit_dump.hpp)

add_executable(tests lexer.cpp parser.cpp ast.cpp unit.cpp test-lexer.cpp test-parser.cpp tests.cpp common.cpp common.hpp ast_unit_visitor.cpp ast_unit_visitor.hpp test-unit-visitor.cpp)
