cmake_minimum_required(VERSION 3.17)
project(klang)

set(CMAKE_CXX_STANDARD 20)

find_package(fmt)

find_package(Catch2 REQUIRED)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs core irreader native orcjit support)

add_library(klangc
        lexer.cpp
        parser.cpp
        ast.cpp
        unit.cpp
        common.cpp
        type.cpp
        ast_unit_visitor.cpp
        symbol_type_resolver.cpp
        unit_llvm_ir_gen.cpp
        logger.cpp
        logger.hpp
)

add_executable(klang main.cpp)
target_link_libraries(klang klangc ${llvm_libs} fmt::fmt)

add_executable(tests test-lexer.cpp test-parser.cpp tests.cpp test-gen.cpp)
target_link_libraries(tests klangc ${llvm_libs} fmt::fmt)
