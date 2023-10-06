
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
        src/klang.cpp
)

target_include_directories(klangc PUBLIC ${PROJECT_BINARY_DIR})
target_link_libraries(klangc klangcomp ${LLVM_LIBRARIES} fmt::fmt Boost::program_options)

add_executable(tests
        tests/test-lexer.cpp
        tests/test-parser.cpp
        tests/tests.cpp
        tests/test-gen.cpp
)
target_link_libraries(tests klangcomp ${LLVM_LIBRARIES} fmt::fmt)


add_executable(klang
        src/main.cpp
)
target_link_libraries(klang klangcomp ${LLVM_LIBRARIES} fmt::fmt)
