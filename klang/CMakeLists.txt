configure_file(src/config.h.in config.h)
include_directories(${PROJECT_BINARY_DIR}/klang)

#
# K lang compiler library and executable
#

add_library(klangcomp
        src/lex/lexemes.cpp
        src/lex/lexer.cpp
        src/parse/parser.cpp
        src/parse/ast.cpp
        src/model/context.cpp
        src/model/context.hpp
        src/model/model.cpp
        src/model/model.hpp
        src/model/imported.cpp
        src/model/imported.hpp
        src/common/common.cpp
        src/model/type.cpp
        src/model/model_builder.cpp
        src/gen/resolvers.cpp
        src/gen/generators.cpp
        src/common/logger.cpp
        src/common/logger.hpp
        src/model/mangler.hpp
        src/model/mangler.cpp
        src/model/expressions.cpp
        src/model/expressions.hpp
        src/model/operators.cpp
        src/model/operators.hpp
        src/model/model_visitor.cpp
        src/model/model_visitor.hpp
        src/model/statements.cpp
        src/model/statements.hpp
        src/gen/gen_expressions.cpp
        src/gen/gen_operators.cpp
        src/gen/gen_statements.cpp
        src/gen/gen_unit.cpp
        src/gen/gen_struct.cpp
        src/gen/gen_class.cpp
        src/gen/gen_function.cpp
        src/compiler.hpp
        src/compiler.cpp
        src/common/process.cpp
        src/common/file_resolver.cpp
        src/common/path_lookup_file_resolver.cpp
        src/model/tools/kdi_exporter.hpp
        src/model/tools/kdi_exporter.cpp
        src/model/tools/kdi_importer.hpp
        src/model/tools/kdi_importer.cpp
        src/model/tools/kdi_type_converter.hpp
        src/model/tools/kdi_type_converter.cpp
)

target_include_directories(klangcomp PRIVATE
    ${CMAKE_SOURCE_DIR}/libkdi/src
)
target_link_libraries(klangcomp ${LLVM_LIBRARIES} ${Boost_LIBRARIES} kdi)

add_executable(klangc
        src/klang.cpp
)

target_include_directories(klangc PUBLIC ${PROJECT_BINARY_DIR})
target_link_libraries(klangc klangcomp ${LLVM_LIBRARIES} fmt::fmt ${Boost_LIBRARIES})

#
# Tests
#

add_executable(klang-tests
        tests/helpers.hpp
        tests/helpers.cpp
        tests/test-lexer.cpp
        tests/test-parser.cpp
        tests/test-gen-arithmetic.cpp
        tests/test-gen-statements.cpp
        tests/test-gen.cpp
        tests/test-gen-resolution.cpp
        tests/test-gen-static-ctor-dtor.cpp
        tests/test-gen-init-order.cpp
        tests/test-gen-nested-structs.cpp
        tests/test-gen-visibility.cpp
        tests/test-gen-inheritance.cpp
        tests/test-run.cpp
        tests/test-gen-indirection.cpp
        tests/test-gen-const.cpp
        tests/test-gen-class-basic.cpp
        tests/test-gen-virtual-inheritance.cpp
        tests/test-gen-class-virtuality.cpp
        tests/test-gen-class-abstract.cpp
        tests/test-gen-interface.cpp
        tests/test-model-materializer.cpp
        tests/test-phase3-dispatch.cpp
        tests/test-phase4-gen.cpp
        tests/test-gen-virtuality-coverage.cpp
        tests/test-gen-multi-diamond.cpp
        tests/test-gen-indirection-downcast.cpp
        tests/test-gen-class-upcast.cpp
        tests/test-gen-upcast.cpp
        tests/test-gen-cast-indirection.cpp
        tests/test-prod-lib.cpp
        tests/test-process.cpp
        tests/test-file-resolver.cpp
        tests/test-import.cpp
        tests/test-gen-func-ref.cpp
)
target_link_libraries(klang-tests klangcomp ${LLVM_LIBRARIES} fmt::fmt Catch2::Catch2WithMain kdi)
target_include_directories(klang-tests PRIVATE ${CMAKE_SOURCE_DIR}/libkdi/src)

add_test(NAME klang-tests COMMAND klang-tests)

#
# Dev and test tools
#

add_executable(klang
        src/main.cpp
)
target_link_libraries(klang klangcomp ${LLVM_LIBRARIES} fmt::fmt)
