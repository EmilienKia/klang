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
        src/gen/gen_constructor.cpp
        src/gen/gen_variable_definition.cpp
        src/gen/gen_adapt_type.cpp
        src/compiler.hpp
        src/compiler.cpp
        src/common/process.cpp
        src/common/file_resolver.cpp
        src/common/path_lookup_file_resolver.cpp
        src/model/tools/kdi_exporter.hpp
        src/model/tools/kdi_exporter.cpp
        src/model/tools/k_source_emitter.hpp
        src/model/tools/k_source_emitter.cpp
        src/model/tools/kdi_importer.hpp
        src/model/tools/kdi_importer.cpp
        src/model/tools/kdi_type_converter.hpp
        src/model/tools/kdi_type_converter.cpp
        src/model/template_instantiator.cpp
        src/model/template.cpp
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

add_library(klang-test-helpers STATIC
        tests/helpers.hpp
        tests/helpers.cpp
)
target_link_libraries(klang-test-helpers PUBLIC klangcomp ${LLVM_LIBRARIES} fmt::fmt kdi dl)
target_include_directories(klang-test-helpers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/libkdi/src
)
target_compile_definitions(klang-test-helpers PUBLIC
    KLANG_STDLIB_LIB_DIR="${KLANG_STDLIB_LIB_DIR}"
)

add_executable(klang-tests
        tests/test-lexer.cpp
        tests/test-parser.cpp
        tests/test-parser-templates.cpp
        tests/test-gen-arithmetic.cpp
        tests/test-gen-statements.cpp
        tests/test-gen.cpp
        tests/test-gen-resolution.cpp
        tests/test-gen-static-ctor-dtor.cpp
        tests/test-gen-init-order.cpp
        tests/test-gen-nested-structs.cpp
        tests/test-gen-visibility.cpp
        tests/test-gen-inheritance.cpp
        tests/test-gen-indirection.cpp
        tests/test-gen-const.cpp
        tests/test-gen-class-basic.cpp
        tests/test-gen-virtual-inheritance.cpp
        tests/test-gen-class-virtuality.cpp
        tests/test-gen-class-abstract.cpp
        tests/test-gen-override.cpp
        tests/test-gen-interface.cpp
        tests/test-model-materializer.cpp
        tests/test-model-annotation.cpp
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
        tests/test-gen-owner.cpp
        tests/test-gen-null-semantics.cpp
        tests/test-gen-array-init.cpp
        tests/test-gen-new-array.cpp
        tests/test-gen-uniform-array-init.cpp
        tests/test-gen-redirector.cpp
        tests/test-gen-array-subscript-indirection.cpp
        tests/test-gen-array-size.cpp
        tests/test-gen-designated-init.cpp
        tests/test-multi-source.cpp
        tests/test-gen-operator-overload.cpp
        tests/test-gen-assign-operator.cpp
        tests/test-gen-cast.cpp
        tests/test-gen-lifecycle.cpp
        tests/test-gen-rvo.cpp
        tests/test-gen-named-return.cpp
        tests/test-gen-string-literal.cpp
        tests/test-gen-drain.cpp
        tests/test-gen-using.cpp
        tests/test-gen-friend.cpp
        tests/test-gen-extern.cpp
        tests/test-gen-annotation-members.cpp
        tests/test-gen-annotation-meta.cpp
        tests/test-model-constructor-rtti.cpp
        tests/test-gen-static-local.cpp
        tests/test-gen-temporary-construction.cpp
        tests/test-gen-temporary-brace-init.cpp
        tests/test-gen-template-aggregates.cpp
        tests/test-gen-template-functions.cpp
        tests/test-gen-template-instantiation.cpp
        tests/test-gen-template-mangling.cpp
        tests/test-gen-template-func-calls.cpp
        tests/test-gen-template-constraints.cpp
        tests/test-gen-template-value-params.cpp
        tests/test-gen-template-comprehensive.cpp
        tests/test-template-instantiator.cpp
        tests/test-gen-if-link-softfail.cpp
        tests/test-gen-if-local-var.cpp
        tests/test-gen-if-cond-var-test.cpp
)
target_link_libraries(klang-tests klang-test-helpers Catch2::Catch2WithMain)
# Export symbols from the test executable so that the JIT's
# DynamicLibrarySearchGenerator (dlsym) can resolve extern "C" functions
# defined in test source files (e.g. test-gen-extern.cpp).
target_link_options(klang-tests PRIVATE -rdynamic)
add_dependencies(klang-tests libk_stdlib)

add_test(NAME klang-tests COMMAND klang-tests)

#
# klangc executable integration tests
# These tests invoke the klangc binary and verify its CLI behaviour.
#

add_executable(klangc-tests
        tests/test-klangc-run.cpp
        tests/test-klangc-stdin.cpp
        tests/test-klangc-jit-exec.cpp
        tests/test-klangc-object-input.cpp
)
target_link_libraries(klangc-tests klang-test-helpers Catch2::Catch2WithMain)
# Ensure klangc and libk are built before running integration tests
add_dependencies(klangc-tests klangc libk_stdlib)

add_test(NAME klangc-tests COMMAND klangc-tests)

#
# Dev and test tools
#

add_executable(klang
        src/main.cpp
)
target_link_libraries(klang klangcomp ${LLVM_LIBRARIES} fmt::fmt)
