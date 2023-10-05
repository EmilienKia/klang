
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

add_library(klangcomp
        src/lexer.cpp
        src/parser.cpp
        src/ast.cpp
        src/unit.cpp
        src/common.cpp
        src/type.cpp
        src/ast_unit_visitor.cpp
        src/symbol_type_resolver.cpp
        src/unit_llvm_ir_gen.cpp
        src/logger.cpp
        src/logger.hpp
)

add_executable(klangc
        src/main.cpp
)
target_link_libraries(klangc klangcomp ${llvm_libs} fmt::fmt)

add_executable(tests
        tests/test-lexer.cpp
        tests/test-parser.cpp
        tests/tests.cpp
        tests/test-gen.cpp
)
target_link_libraries(tests klangcomp ${llvm_libs} fmt::fmt)
