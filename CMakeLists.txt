cmake_minimum_required(VERSION 3.17)
project(klang)

set(CMAKE_CXX_STANDARD 20)

find_package(Catch2 REQUIRED)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs core irreader native orcjit support)

add_executable(klang lexer.cpp parser.cpp ast.cpp main.cpp unit.cpp common.cpp common.hpp ast_unit_visitor.cpp ast_unit_visitor.hpp unit_dump.hpp unit_var_resolver.cpp unit_var_resolver.hpp unit_llvm_ir_gen.cpp unit_llvm_ir_gen.hpp)
target_link_libraries(klang ${llvm_libs})

add_executable(tests lexer.cpp parser.cpp ast.cpp unit.cpp test-lexer.cpp test-parser.cpp tests.cpp common.cpp common.hpp ast_unit_visitor.cpp ast_unit_visitor.hpp test-unit-visitor.cpp)
