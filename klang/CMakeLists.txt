
configure_file(src/config.h.in config.h)

set(CMAKE_CXX_STANDARD 20)

find_package(fmt)

find_package(Catch2 REQUIRED)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM all targets: ${LLVM_ALL_TARGETS}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(LLVM_LIBRARIES core irreader native orcjit support AArch64 AMDGPU ARM AVR BPF Hexagon Lanai M68k Mips MSP430 NVPTX PowerPC RISCV Sparc SystemZ VE WebAssembly X86 XCore)
message(STATUS "LLVM libs (by cmake): ${LLVM_LIBRARIES}")

find_package(Boost 1.74 REQUIRED
        COMPONENTS program_options
)
include_directories(${Boost_INCLUDE_DIRS})

configure_file(src/config.h.in config.h)
include_directories(${PROJECT_BINARY_DIR}/klang)

add_library(klangcomp
        src/lex/lexemes.cpp
        src/lex/lexer.cpp
        src/parse/parser.cpp
        src/parse/ast.cpp
        src/model/context.cpp
        src/model/context.hpp
        src/model/model.cpp
        src/model/model.hpp
        src/common/common.cpp
        src/model/type.cpp
        src/model/model_builder.cpp
        src/gen/resolvers.cpp
        src/gen/unit_llvm_ir_gen.cpp
        src/common/logger.cpp
        src/common/logger.hpp
        src/model/mangler.hpp
        src/model/mangler.cpp
        src/model/expressions.cpp
        src/model/expressions.hpp
        src/model/model_visitor.cpp
        src/model/model_visitor.hpp
        src/model/statements.cpp
        src/model/statements.hpp
        src/gen/gen_expressions.cpp
        src/gen/gen_statements.cpp
        src/gen/gen_unit.cpp
        src/compiler.hpp
        src/compiler.cpp
)

add_executable(klangc
        src/klang.cpp
)

target_include_directories(klangc PUBLIC ${PROJECT_BINARY_DIR})
target_link_libraries(klangc klangcomp ${LLVM_LIBRARIES} fmt::fmt Boost::program_options)

add_executable(tests
        tests/test-lexer.cpp
        tests/test-parser.cpp
        tests/test-gen.cpp
)
target_link_libraries(tests klangcomp ${LLVM_LIBRARIES} fmt::fmt Catch2::Catch2WithMain)


add_executable(klang
        src/main.cpp
)
target_link_libraries(klang klangcomp ${LLVM_LIBRARIES} fmt::fmt)
