configure_file(src/config.h.in config.h)
include_directories(${PROJECT_BINARY_DIR}/klang)

#
# K lang compiler library and executable
#

add_library(klang
        src/lex/lexemes.cpp
        src/lex/lexer.cpp
        src/parse/parser_declarations.cpp
        src/parse/parser_statements.cpp
        src/parse/parser_expressions.cpp
        src/parse/doc_comment_parser.hpp
        src/parse/doc_comment_parser.cpp
        src/parse/ast.cpp
        src/model/context.cpp
        src/model/context.hpp
        src/model/model.cpp
        src/model/model.hpp
        src/model/documentation.hpp
        src/model/documentation.cpp
        src/model/imported.cpp
        src/model/imported.hpp
        src/common/common.cpp
        src/common/unicode.cpp
        src/model/type.cpp
        src/model/model_builder.cpp
        src/gen/resolvers_scope_lookup.cpp
        src/gen/resolvers_symbol.cpp
        src/gen/resolvers_aggregate.cpp
        src/gen/resolvers_materializer.cpp
        src/gen/resolvers_type_ref.cpp
        src/gen/resolvers_init_order.cpp
        src/gen/resolvers_generic.cpp
        src/gen/resolvers_constexpr.cpp
        src/gen/generators.cpp
        src/gen/debug_info.hpp
        src/gen/debug_info.cpp
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
        src/gen/gen_expr_unary.cpp
        src/gen/gen_expr_member.cpp
        src/gen/gen_expr_invocation.cpp
        src/gen/gen_expr_memory.cpp
        src/gen/gen_expr_construct.cpp
        src/gen/gen_expr_cast.cpp
        src/gen/gen_operators_overload.cpp
        src/gen/gen_operators_arithmetic.cpp
        src/gen/gen_operators_assign.cpp
        src/gen/gen_operators_unary.cpp
        src/gen/gen_operators_logical.cpp
        src/gen/gen_operators_spaceship.cpp
        src/gen/gen_statements.cpp
        src/gen/gen_unit.cpp
        src/gen/gen_struct.cpp
        src/gen/gen_class.cpp
        src/gen/gen_function.cpp
        src/gen/gen_constructor.cpp
        src/gen/gen_intrinsics.cpp
        src/gen/gen_variable_definition.cpp
        src/gen/gen_adapt_type.cpp
        src/gen/gen_callable.cpp
        src/gen/gen_callable_compat.cpp
        src/compiler.hpp
        src/compiler.cpp
        src/compiler_linker.cpp
        src/compiler_diagnostic.cpp
        src/common/process.cpp
        src/common/file_resolver.cpp
        src/common/path_lookup_file_resolver.cpp
        src/common/target_init.hpp
        src/model/tools/kdi_exporter.hpp
        src/model/tools/kdi_exporter.cpp
        src/model/tools/k_source_emitter.hpp
        src/model/tools/k_source_emitter.cpp
        src/model/tools/kdi_importer.hpp
        src/model/tools/kdi_importer.cpp
        src/model/tools/kdi_type_converter.hpp
        src/model/tools/kdi_type_converter.cpp
        src/model/template_instantiator.cpp
        src/model/template_deduction.cpp
        src/model/template.cpp
        src/model/aggregate_value.hpp
        src/model/aggregate_value.cpp
        src/model/constant_value.hpp
        src/model/constant_value.cpp
        src/model/constant_evaluator.hpp
        src/model/constant_evaluator.cpp
        src/plugin/plugin.hpp
        src/plugin/plugin_manager.hpp
        src/plugin/plugin_manager.cpp
        src/plugin/structural/structural_pass.hpp
        src/plugin/structural/structural_pass.cpp
        src/plugin/structural/structural_plugin.hpp
        src/plugin/structural/structural_plugin.cpp
)

target_include_directories(klang PRIVATE
    ${CMAKE_SOURCE_DIR}/libkdi/src
)
target_link_libraries(klang ${LLVM_LIBRARIES} ${Boost_LIBRARIES} kdi dl)

add_executable(klangc
        src/klang.cpp
)

target_include_directories(klangc PUBLIC ${PROJECT_BINARY_DIR})
target_link_libraries(klangc klang ${LLVM_LIBRARIES} fmt::fmt ${Boost_LIBRARIES} dl)
set_target_properties(klangc PROPERTIES ENABLE_EXPORTS ON)

#
# Compiler plugins
#

add_library(klang-plugin-structural SHARED
        src/plugin/structural/structural_pass.hpp
        src/plugin/structural/structural_pass.cpp
        src/plugin/structural/structural_plugin.hpp
        src/plugin/structural/structural_plugin.cpp
)
target_include_directories(klang-plugin-structural PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/libkdi/src
)

add_library(klang-plugin-naming SHARED
        src/plugin/naming/casing_utils.hpp
        src/plugin/naming/casing_utils.cpp
        src/plugin/naming/naming_pass.hpp
        src/plugin/naming/naming_pass.cpp
        src/plugin/naming/naming_plugin.hpp
        src/plugin/naming/naming_plugin.cpp
)
target_include_directories(klang-plugin-naming PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/libkdi/src
)

#
# Tests
#

add_library(klang-test-helpers STATIC
        tests/helpers.hpp
        tests/helpers.cpp
)
target_link_libraries(klang-test-helpers PUBLIC klang ${LLVM_LIBRARIES} fmt::fmt kdi dl)
target_include_directories(klang-test-helpers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/libkdi/src
)
target_compile_definitions(klang-test-helpers PUBLIC
    KLANG_STDLIB_LIB_DIR="${KLANG_STDLIB_LIB_DIR}"
)

# ---------------------------------------------------------------------------
# Helper macro: register a non-JIT frontend test executable.
# Usage: klang_add_test(<target> <timeout_s> <source1> [<source2> ...])
# ---------------------------------------------------------------------------
macro(klang_add_test TARGET TIMEOUT)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} klang-test-helpers Catch2::Catch2WithMain)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    klang_scale_ctest_timeout(_klang_test_timeout "${TIMEOUT}")
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT "${_klang_test_timeout}")
endmacro()

# ---------------------------------------------------------------------------
# Helper macro: register a standard codegen test executable.
# All JIT-based gen test executables share the same setup:
#   - link against klang-test-helpers and Catch2
#   - export symbols with -rdynamic (needed by test-gen-extern JIT dlsym)
#   - depend on libk_stdlib (K compilation auto-imports the stdlib)
# Usage: klang_gen_test(<target> <timeout_s> [SHARD_COUNT <count>] <source1> [<source2> ...])
# ---------------------------------------------------------------------------
macro(klang_gen_test TARGET TIMEOUT)
    cmake_parse_arguments(KLANG_GEN_TEST "" "SHARD_COUNT" "" ${ARGN})
    add_executable(${TARGET} ${KLANG_GEN_TEST_UNPARSED_ARGUMENTS})
    target_link_libraries(${TARGET} klang-test-helpers Catch2::Catch2WithMain)
    target_link_options(${TARGET} PRIVATE -rdynamic)
    add_dependencies(${TARGET} libk_stdlib)
    if(KLANG_GEN_TEST_SHARD_COUNT)
        math(EXPR KLANG_GEN_TEST_LAST_SHARD "${KLANG_GEN_TEST_SHARD_COUNT} - 1")
        foreach(KLANG_GEN_TEST_SHARD_INDEX RANGE ${KLANG_GEN_TEST_LAST_SHARD})
            klang_scale_ctest_timeout(_klang_test_timeout "${TIMEOUT}")
            add_test(NAME ${TARGET}-shard-${KLANG_GEN_TEST_SHARD_INDEX}
                COMMAND ${TARGET}
                    --shard-count ${KLANG_GEN_TEST_SHARD_COUNT}
                    --shard-index ${KLANG_GEN_TEST_SHARD_INDEX}
                    --order lex --rng-seed 42)
            set_tests_properties(${TARGET}-shard-${KLANG_GEN_TEST_SHARD_INDEX}
                PROPERTIES TIMEOUT "${_klang_test_timeout}" RUN_SERIAL TRUE)
        endforeach()
    else()
        add_test(NAME ${TARGET} COMMAND ${TARGET})
        klang_scale_ctest_timeout(_klang_test_timeout "${TIMEOUT}")
        set_tests_properties(${TARGET} PROPERTIES TIMEOUT "${_klang_test_timeout}")
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Group 1 — Frontend: lexer, parser, file-resolver, process
# No JIT, no libk dependency — runnable even before libk is built.
# ---------------------------------------------------------------------------
klang_add_test(klang-tests-frontend 30
        tests/test-lexer.cpp
        tests/test-unicode.cpp
        tests/test-parser.cpp
        tests/test-parser-templates.cpp
        tests/test-parse-alias.cpp
        tests/test-parse-lambda.cpp
        tests/test-file-resolver.cpp
        tests/test-process.cpp
)

# ---------------------------------------------------------------------------
# Group 2 — Model: model analysis, template instantiator, phase pipeline
# Compile-only tests — no heavy JIT execution.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-model 120
        tests/test-model-documentation.cpp
        tests/test-model-lexemes.cpp
        tests/test-model-annotation.cpp
        tests/test-model-constructor-rtti.cpp
        tests/test-model-materializer.cpp
        tests/test-model-constant-expressions.cpp
        tests/test-template-instantiator.cpp
        tests/test-phase3-dispatch.cpp
        tests/test-phase4-gen.cpp
        tests/test-gen-template-aggregates.cpp
        tests/test-gen-annotation-members.cpp
)

# ---------------------------------------------------------------------------
# Group 3a — Codegen core: basic codegen, string literals, variable init
# Catch2 shards keep this suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-core 120 SHARD_COUNT 3
        tests/test-gen.cpp
        tests/test-gen-string-literal.cpp
        tests/test-gen-variable-init.cpp
)

# ---------------------------------------------------------------------------
# Group 3b — Codegen resolution: symbol resolution, visibility, multi-source
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-resolution 120
        tests/test-gen-resolution.cpp
        tests/test-gen-visibility.cpp
        tests/test-multi-source.cpp
        tests/test-gen-diagnostic-suppression.cpp
)

# ---------------------------------------------------------------------------
# Group 3c — Codegen ABI & FFI: extern C, intrinsics, varargs
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-extern-intrinsics 120
        tests/test-gen-extern.cpp
        tests/test-gen-intrinsic.cpp
        tests/test-gen-varargs.cpp
)

# ---------------------------------------------------------------------------
# Group 3d — Codegen: functions, callables, lambdas, func-refs
# Keep the executable stable; serial execution shards fit the timeout budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-functions 120 SHARD_COUNT 3
        tests/test-gen-func-ref.cpp
        tests/test-gen-callable-basic.cpp
        tests/test-gen-callable-member.cpp
        tests/test-gen-callable-interface.cpp
        tests/test-gen-callable-variance.cpp
        tests/test-gen-lambda.cpp
        tests/test-gen-operator-call.cpp
)

# ---------------------------------------------------------------------------
# Group 3e — Codegen: statements, control flow, init order, static lifecycle
# Catch2 shards keep this broad suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-control 120 SHARD_COUNT 3
        tests/test-gen-statements.cpp
        tests/test-gen-if-link-softfail.cpp
        tests/test-gen-if-local-var.cpp
        tests/test-gen-if-cond-var-test.cpp
        tests/test-gen-if-multi-cond-var.cpp
        tests/test-gen-if-multi-softfail.cpp
        tests/test-gen-init-order.cpp
        tests/test-gen-static-ctor-dtor.cpp
        tests/test-gen-static-local.cpp
        tests/test-gen-thread-local.cpp
)

# ---------------------------------------------------------------------------
# Group 3f — Exception model and throw contracts
# Catch2 shards keep this suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-exceptions 120 SHARD_COUNT 2
        tests/test-gen-exceptions.cpp
        tests/test-gen-main-uncaught.cpp
)

# ---------------------------------------------------------------------------
# Group 3g — Ternary expression matrix
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-ternary 120
        tests/test-gen-ternary-comprehensive.cpp
)

# ---------------------------------------------------------------------------
# Group 4a — Arithmetic operators: 8/16-bit integers (byte, ubyte, short, ushort)
# Catch2 shards keep this single-source suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-arithmetic-int8-16 120 SHARD_COUNT 2
        tests/test-gen-arithmetic-int8-16.cpp
)

# ---------------------------------------------------------------------------
# Group 4b — Arithmetic operators: 32/64-bit integers (int, uint, long, ulong)
# Catch2 shards keep this suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-arithmetic-int32-64 120 SHARD_COUNT 2
        tests/test-gen-arithmetic-int32-64.cpp
)

# ---------------------------------------------------------------------------
# Group 4c — Arithmetic operators: floating-point, boolean, increment/decrement
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-arithmetic-float 120
        tests/test-gen-arithmetic-float.cpp
)

# ---------------------------------------------------------------------------
# Group 4d — Operator overloads, comparison fallback, spaceship, assignment
# Catch2 shards keep this broad suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-operators 120 SHARD_COUNT 3
        tests/test-gen-operator-overload.cpp
        tests/test-gen-comparison-fallback.cpp
        tests/test-gen-spaceship.cpp
        tests/test-gen-assign-operator.cpp
)

# ---------------------------------------------------------------------------
# Group 4e — Casts and conversions: scalar casts, indirection casts
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-casts 120
        tests/test-gen-cast.cpp
        tests/test-gen-cast-indirection.cpp
)

# ---------------------------------------------------------------------------
# Group 5a — Types: enums/unions, const, null, metadata/designated init
# Split into enums/unions, const, null and metadata/designated-init executables.
# Catch2 shards keep the enum/union suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-types-enums-unions 120 SHARD_COUNT 3
        tests/test-gen-enum.cpp
        tests/test-gen-union.cpp
        tests/test-gen-union-template.cpp
        tests/test-gen-union-inheritance.cpp
        tests/test-gen-union-polymorphic.cpp
        tests/test-gen-union-cast.cpp
)
klang_gen_test(klang-tests-gen-types-const 120
        tests/test-gen-const.cpp
)
klang_gen_test(klang-tests-gen-types-null 120
        tests/test-gen-null-semantics.cpp
)
klang_gen_test(klang-tests-gen-types-meta-designated 120
        tests/test-gen-annotation-meta.cpp
        tests/test-gen-designated-init.cpp
)

# ---------------------------------------------------------------------------
# Group 5b — Alias, typedef, redirector, callable-alias
# Estimated wall-clock: ~40s on a low-spec VM.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-aliases 120
        tests/test-gen-redirector.cpp
        tests/test-gen-alias-soft.cpp
        tests/test-gen-typedef.cpp
        tests/test-gen-alias-template.cpp
        tests/test-gen-callable-alias.cpp
)

# ---------------------------------------------------------------------------
# Group 6a — Structs, inheritance, basic classes
# Estimated wall-clock: ~50s on a low-spec VM.
# Catch2 shards bound the import-heavy class matrix.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-classes 120 SHARD_COUNT 3
        tests/test-gen-nested-structs.cpp
        tests/test-gen-inheritance.cpp
        tests/test-gen-class-basic.cpp
        tests/test-gen-class-abstract.cpp
        tests/test-gen-class-upcast.cpp
        tests/test-gen-upcast.cpp
)

# ---------------------------------------------------------------------------
# Group 6b — Virtuality, interfaces, override, diamond inheritance
# Estimated wall-clock: ~50s on a low-spec VM.
# Catch2 shards bound the virtual inheritance matrix.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-virtuality 120 SHARD_COUNT 3
        tests/test-gen-class-virtuality.cpp
        tests/test-gen-virtual-inheritance.cpp
        tests/test-gen-virtuality-coverage.cpp
        tests/test-gen-multi-diamond.cpp
        tests/test-gen-override.cpp
        tests/test-gen-interface.cpp
        tests/test-gen-interface-default.cpp
        tests/test-gen-virtual-destructor.cpp
)

# ---------------------------------------------------------------------------
# Group 7a — Memory model: ownership, drain, and indirection
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-memory-ownership 120
        tests/test-gen-owner.cpp
        tests/test-gen-drain.cpp
        tests/test-gen-indirection.cpp
        tests/test-gen-indirection-downcast.cpp
        tests/test-gen-recursive-indirection.cpp
)

# ---------------------------------------------------------------------------
# Group 7b — Memory model: fixed/dynamic arrays and foreach iteration
# Catch2 shards keep this broad suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-arrays 120 SHARD_COUNT 3
        tests/test-gen-array-init.cpp
        tests/test-gen-new-array.cpp
        tests/test-gen-uniform-array-init.cpp
        tests/test-gen-array-subscript-indirection.cpp
        tests/test-gen-array-size.cpp
        tests/test-gen-array-unsized-conv.cpp
        tests/test-gen-foreach.cpp
)

# ---------------------------------------------------------------------------
# Group 8a — OOP: lifecycle and RVO
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-oop-lifecycle 120
        tests/test-gen-lifecycle.cpp
        tests/test-gen-rvo.cpp
)

# ---------------------------------------------------------------------------
# Group 8b — OOP: named return and temporaries
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-oop-temporary 120
        tests/test-gen-named-return.cpp
        tests/test-gen-temporary-construction.cpp
        tests/test-gen-temporary-brace-init.cpp
)

# ---------------------------------------------------------------------------
# Group 8c — Using declarations and friend: heavier because of import overhead
# Catch2 shards keep the import-heavy scope cases below the 120-second budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-scoping 120 SHARD_COUNT 2
        tests/test-gen-using.cpp
        tests/test-gen-friend.cpp
)

# ---------------------------------------------------------------------------
# Group 9a — Templates: functions, instantiation, mangling, member templates
# Catch2 shards keep this broad suite below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-templates-core 120 SHARD_COUNT 2
        tests/test-gen-template-functions.cpp
        tests/test-gen-template-instantiation.cpp
        tests/test-gen-template-mangling.cpp
        tests/test-gen-template-func-calls.cpp
        tests/test-gen-member-template.cpp
        tests/test-gen-template-recursion-guard.cpp
        tests/test-gen-template-comprehensive.cpp
)

# ---------------------------------------------------------------------------
# Group 9b — Templates: deduction, constraints, packs, value params
# Catch2 shards keep the template matrix below the 120-second CTest budget.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-templates-deduction 120 SHARD_COUNT 3
        tests/test-gen-template-constraints.cpp
        tests/test-gen-template-value-params.cpp
        tests/test-gen-template-aggregate-values.cpp
        tests/test-gen-template-deduction.cpp
        tests/test-gen-ctad.cpp
        tests/test-gen-template-packs.cpp
)

# ---------------------------------------------------------------------------
# Group 9c — Generics: type-erased generic declarations (single synthesis),
# restrictions and diagnostics, nested generics
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-gen-generics 120
        tests/test-gen-generic.cpp
        tests/test-gen-nested-generics.cpp
)

# ---------------------------------------------------------------------------
# Group 10 — (covered by dedicated groups above — no standalone group needed)
#
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Group 11 — KDI import, production libraries, multi-source
# Split into core import, templates cross-module, and interop test executables.
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-import-core 120
        tests/test-import-core.cpp
        tests/test-import-private-concrete.cpp
)
klang_gen_test(klang-tests-import-templates 120 SHARD_COUNT 15
        tests/test-import-templates.cpp
        tests/test-import-member-constructor.cpp
        tests/test-import-template-reparse-diagnostic.cpp
)
klang_gen_test(klang-tests-import-interop 120
        tests/test-import-interop.cpp
        tests/test-import-wrapped-alias.cpp
        tests/test-prod-lib.cpp
)

# ---------------------------------------------------------------------------
# Group 11 — Plugins: plugin system infrastructure, Structural, and Naming
# ---------------------------------------------------------------------------
klang_gen_test(klang-tests-plugins 120
        tests/test-plugin-structural.cpp
        tests/test-plugin-naming.cpp
)
target_link_libraries(klang-tests-plugins klang-plugin-structural klang-plugin-naming)
add_dependencies(klang-tests-plugins klang-plugin-structural klang-plugin-naming)

#
# klangc executable integration tests
# These tests invoke the klangc binary and verify its CLI behaviour.
#

add_executable(klangc-tests
        tests/test-compiler-fatal-diagnostics.cpp
        tests/test-klangc-run.cpp
        tests/test-klangc-stdin.cpp
        tests/test-klangc-jit-exec.cpp
        tests/test-klangc-main-abort.cpp
        tests/test-klangc-object-input.cpp
        tests/test-klangc-export-symbol.cpp
        tests/test-klangc-debug.cpp
        tests/test-klangc-mangling.cpp
        tests/test-klangc-static-diamond.cpp
)
target_link_libraries(klangc-tests klang-test-helpers Catch2::Catch2WithMain)
target_compile_definitions(klangc-tests PRIVATE
        KLANGC_PATH="$<TARGET_FILE:klangc>"
)
# Ensure klangc and libk are built before running integration tests
add_dependencies(klangc-tests klangc libk_stdlib)

add_test(NAME klangc-tests COMMAND klangc-tests)
klang_scale_ctest_timeout(_klangc_test_timeout 120)
set_tests_properties(klangc-tests PROPERTIES TIMEOUT "${_klangc_test_timeout}")
