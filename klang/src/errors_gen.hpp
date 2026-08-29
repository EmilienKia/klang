/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef KLANG_ERRORS_GEN_HPP
#define KLANG_ERRORS_GEN_HPP
#include "errors_model.hpp"

namespace k::diag {


// ────────────────────────────────────────────────────────────────────────────
// Operator diagnostics (arithmetic, comparison, assignment, overload resolution)
// ────────────────────────────────────────────────────────────────────────────
enum class operator_diag : unsigned int {
    ERR_ARITH_TYPE_MISMATCH                       = 0x0C01,
    ERR_ARITH_NO_COMMON_TYPE                      = 0x0C02,
    ERR_ARITH_MODULO_NOT_INT                      = 0x0C03,
    ERR_ASSIGN_INCOMPATIBLE                       = 0x0C04,
    ERR_ASSIGN_TO_CONST                           = 0x0C05,
    ERR_MAIN_WRONG_RETURN_TYPE                    = 0x0C06,
    ERR_MAIN_WRONG_PARAMS                         = 0x0C07,
    ERR_PREINC_NOT_REF                            = 0x0C08,
    ERR_PREINC_NOT_NUMERIC                        = 0x0C09,
    ERR_PREDEC_NOT_REF                            = 0x0C0A,
    ERR_POSTINC_NOT_REF                           = 0x0C0B,
    ERR_POSTINC_NOT_NUMERIC                       = 0x0C0C,
    ERR_SHIFT_NOT_INT                             = 0x0C0D,
    ERR_SHIFT_INCOMPATIBLE                        = 0x0C0E,
    ERR_LOGICAL_NOT_BOOL                          = 0x0C0F,
    ERR_LOGICAL_AND_INCOMPATIBLE                  = 0x0C10,
    WARN_IMPLICIT_LOSSY_CAST                      = 0x0C11,
    ERR_OVERLOAD_CALL_NO_MATCH                    = 0x0C12,
    ERR_OVERLOAD_ARG_TYPE_MISMATCH                = 0x0C13,
    ERR_OVERLOAD_RETURN_TYPE_MISMATCH             = 0x0C14,
    ERR_OVERLOAD_CONST_MISMATCH                   = 0x0C15,
    ERR_OVERLOAD_VISIBILITY_DENIED                = 0x0C16,
    ERR_OVERLOAD_NOT_FOUND                        = 0x0C17,
    ERR_BINARY_OVERLOAD_BAD_RECEIVER              = 0x0C18,
    ERR_UNARY_OVERLOAD_BAD_RECEIVER               = 0x0C19,
    ERR_PM_EXPR_BAD_TYPE                          = 0x0C1A,
    ERR_PM_EXPR_NOT_MEMBER_PTR                    = 0x0C1B,
    ERR_PM_EXPR_INCOMPATIBLE                      = 0x0C1C,
    ERR_SUBSCRIPT_OVERLOAD_NOT_FOUND              = 0x0C1D,
    ERR_SUBSCRIPT_OVERLOAD_CONST                  = 0x0C1E,
    ERR_SUBSCRIPT_OVERLOAD_BAD_RETURN             = 0x0C1F,
    ERR_BITWISE_AND_INCOMPATIBLE                  = 0x0C20,
    ERR_BITWISE_OR_INCOMPATIBLE                   = 0x0C21,
    ERR_BITWISE_XOR_INCOMPATIBLE                  = 0x0C22,
    ERR_SHIFT_ASSIGN_INCOMPATIBLE                 = 0x0C23,
    ERR_BITWISE_ASSIGN_INCOMPATIBLE               = 0x0C24,
    ERR_ADD_ASSIGN_INCOMPATIBLE                   = 0x0C25,
    ERR_MUL_ASSIGN_INCOMPATIBLE                   = 0x0C26,
    ERR_SUB_ASSIGN_INCOMPATIBLE                   = 0x0C27,
    ERR_DIV_ASSIGN_INCOMPATIBLE                   = 0x0C28,
    ERR_SPACESHIP_OVERLOAD_NOT_FOUND              = 0x0C29,
    ERR_SPACESHIP_BAD_RETURN_TYPE                 = 0x0C2A,
    ERR_SPACESHIP_NOT_PRIMITIVE                   = 0x0C2B,
};

// ────────────────────────────────────────────────────────────────────────────
// Variable definition diagnostics (initialization, type compatibility)
// ────────────────────────────────────────────────────────────────────────────
enum class variable_diag : unsigned int {
    ERR_VAR_TYPE_UNRESOLVED                       = 0x0D01,
    ERR_VAR_INIT_TYPE_MISMATCH                    = 0x0D02,
    ERR_VAR_INIT_ARG_MISMATCH                     = 0x0D03,
    ERR_STRUCT_VAR_NO_CTOR                        = 0x0D04,
    ERR_STRUCT_VAR_CTOR_AMBIGUOUS                 = 0x0D05,
    ERR_STRUCT_VAR_CTOR_ARG_MISMATCH              = 0x0D06,
    ERR_STRUCT_VAR_CTOR_NOT_FOUND                 = 0x0D07,
    ERR_REF_VAR_NEEDS_INIT                        = 0x0D08,
    ERR_REF_VAR_INCOMPATIBLE_INIT                 = 0x0D09,
    ERR_REF_VAR_WRONG_SUBTYPE                     = 0x0D0A,
    ERR_REF_VAR_CONST_MISMATCH                    = 0x0D0B,
    ERR_REF_VAR_MULTIPLE_INIT                     = 0x0D0C,
    ERR_SIZED_ARRAY_INIT_MISMATCH                 = 0x0D0D,
    ERR_LINK_VAR_NEEDS_INIT                       = 0x0D0E,
    ERR_LINK_VAR_INIT_MISMATCH                    = 0x0D0F,
    WARN_NARROWING_INIT                           = 0x0D10,
    ERR_LINK_VAR_WRONG_TYPE                       = 0x0D11,
    ERR_VIEW_VAR_NEEDS_INIT                       = 0x0D12,
    ERR_VIEW_VAR_INIT_MISMATCH                    = 0x0D13,
    ERR_VIEW_VAR_WRONG_TYPE                       = 0x0D14,
    ERR_POINTER_VAR_INIT_MISMATCH                 = 0x0D15,
    ERR_OWNER_VAR_NEEDS_INIT                      = 0x0D16,
    ERR_OWNER_VAR_INIT_MISMATCH                   = 0x0D17,
};

// ────────────────────────────────────────────────────────────────────────────
// Statement diagnostics (return, if, while, for, expression statements)
// ────────────────────────────────────────────────────────────────────────────
enum class statement_diag : unsigned int {
    ERR_RETURN_TYPE_MISMATCH                      = 0x0E01,
    ERR_IF_COND_NOT_BOOL                          = 0x0E02,
    ERR_WHILE_COND_NOT_BOOL                       = 0x0E03,
    ERR_FOR_COND_NOT_BOOL                         = 0x0E04,
    WARN_UNUSED_EXPR_RESULT                       = 0x0E05,
    WARN_UNREACHABLE_AFTER_RETURN                 = 0x0E06,
    ERR_LOCAL_VAR_TYPE_UNRESOLVED                 = 0x0E07,
    ERR_RETURN_TYPE_INCONSISTENT_DEDUCTION        = 0x0E08,
    ERR_RETURN_VOID_AND_NONVOID                   = 0x0E09,
    ERR_RETURN_DEDUCTION_CYCLE                    = 0x0E0A,
};

// ────────────────────────────────────────────────────────────────────────────
// Code generation diagnostics (LLVM IR, vtable, constructors, internal errors)
// ────────────────────────────────────────────────────────────────────────────
enum class codegen_diag : unsigned int {
    ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS               = 0x0F01,

    /**
     * A model element that must be emitted (or exported through a KDI) produced an
     * empty mangled name. An empty name makes distinct entities indistinguishable at
     * link time, so it is always a compiler bug rather than a user error.
     */
    ERR_MANGLED_NAME_EMPTY                        = 0x0F02,

    /**
     * Two distinct model elements of the same unit produced the same mangled name.
     * Because template instantiations are emitted `linkonce_odr` in a `Comdat::Any`
     * group keyed by the mangled name, such a collision would silently make the linker
     * keep a single, arbitrarily chosen definition.
     */
    ERR_DUPLICATE_MANGLED_NAME                    = 0x0F03,

    /**
     * An LLVM type name reaching the KDI exporter contains a '.' — the marker of LLVM's
     * automatic uniquification of colliding type names. Such names depend on compilation
     * order and are therefore not stable across builds nor usable for cross-module type
     * identity.
     */
    ERR_LLVM_TYPE_NAME_NOT_CANONICAL              = 0x0F04,

    /**
     * Two imported KDI entries declare the same LLVM type name with different bodies.
     * Deduplicating them by name would give one of the two entities the other's layout.
     */
    ERR_KDI_TYPE_LAYOUT_CONFLICT                  = 0x0F05,

    /**
     * A non-trivial lvalue copy reached code generation without any available copy
     * constructor. Bytewise copying such a type would duplicate owned resources and
     * can cause double frees or use-after-free.
     */
    ERR_TYPE_NOT_COPYABLE                         = 0x0F06,

    /**
     * `mangler::mangle_type()` was given a type it cannot encode. `mangle_type()` is
     * total by contract: returning an empty string here would silently collapse distinct
     * symbols.
     */
    INTERNAL_ERR_MANGLE_TYPE                      = 0xF001,

    /**
     * Null sub-expression in a unary expression reaching symbol resolution, a
     * root namespace with no name at codegen, or the top-level catch-all for
     * any unexpected non-compiler exception during compilation.
     */
    INTERNAL_ERR_F001                             = 0xF001,

    /**
     * A binary expression has a null left or right operand when
     * symbol_resolver visits it; indicates a malformed AST.
     */
    INTERNAL_ERR_F002                             = 0xF002,

    /**
     * A symbol reached the type-resolution phase without being resolved by
     * symbol_resolver first, or a constructor has no owner structure.
     */
    INTERNAL_ERR_F003                             = 0xF003,

    /**
     * A unary expression's sub-expression is null during type resolution, or
     * a destructor/variable definition is missing its owner structure or
     * function-reference type; indicates a bug in an earlier pass.
     */
    INTERNAL_ERR_F004                             = 0xF004,

    /**
     * The sub-expression of a unary operator could not be type-resolved
     * before the unary expression itself is typed.
     */
    INTERNAL_ERR_F005                             = 0xF005,

    /**
     * A binary expression has a null left or right operand when
     * type_reference_resolver visits it.
     */
    INTERNAL_ERR_F006                             = 0xF006,

    /// The left operand of a binary operator could not be type-resolved.
    INTERNAL_ERR_F007                             = 0xF007,

    /// The right operand of a binary operator could not be type-resolved.
    INTERNAL_ERR_F008                             = 0xF008,

    /**
     * A constructor invocation expression's constructed symbol does not
     * refer to a variable definition.
     */
    INTERNAL_ERR_F009                             = 0xF009,

    /// A constructor invocation refers to a variable with no resolved type.
    INTERNAL_ERR_F00A                             = 0xF00A,

    /// The '!' (logical NOT) operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F00B                             = 0xF00B,

    /// The '==' expression produced a null left or right LLVM value during codegen.
    INTERNAL_ERR_F00C                             = 0xF00C,

    /// The '==' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F00D                             = 0xF00D,

    /// The '!=' expression produced a null left or right LLVM value during codegen.
    INTERNAL_ERR_F00E                             = 0xF00E,

    /// The '!=' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F00F                             = 0xF00F,

    /// The '<' expression produced a null left or right LLVM value during codegen.
    INTERNAL_ERR_F010                             = 0xF010,

    /// The '<' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F011                             = 0xF011,

    /// The '>' expression produced a null left or right LLVM value during codegen.
    INTERNAL_ERR_F012                             = 0xF012,

    /// The '>' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F013                             = 0xF013,

    /// The '<=' expression produced a null left or right LLVM value during codegen.
    INTERNAL_ERR_F014                             = 0xF014,

    /// The '<=' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F015                             = 0xF015,

    /**
     * The '>=' expression produced a null left or right LLVM value, or has a
     * non-primitive operand, reaching code generation.
     */
    INTERNAL_ERR_F016                             = 0xF016,

    /// An enum_type used in a constructor invocation has no associated enumeration model object.
    INTERNAL_ERR_F017                             = 0xF017,

    /// The single initialisation argument for a reference-kind ('&') variable is null.
    INTERNAL_ERR_F018                             = 0xF018,

    /// The single initialisation argument for a link-kind ('+') variable is null.
    INTERNAL_ERR_F019                             = 0xF019,

    /// The single initialisation argument for a view-kind ('?') variable is null.
    INTERNAL_ERR_F01A                             = 0xF01A,

    /**
     * The LLVM function declaration could not be materialised for a function
     * (missing this-parameter, or unresolved this/parameter/return LLVM
     * type), or no enclosing function context was found for a member access.
     */
    INTERNAL_ERR_F01B                             = 0xF01B,

    /**
     * No 'this' pointer found in the enclosing function for a member
     * variable access, or a global static-constructor function was not
     * found in the LLVM function table.
     */
    INTERNAL_ERR_F01C                             = 0xF01C,

    /// No struct context found on the code-generation stack when accessing a member variable.
    INTERNAL_ERR_F01D                             = 0xF01D,

    /**
     * Could not reach the struct owning a member variable via the
     * __parent__/__base__ chain, or the struct has no member with the
     * expected name.
     */
    INTERNAL_ERR_F01E                             = 0xF01E,

    /**
     * Could not find the struct owning a member variable in the __parent__
     * chain, or the struct has no LLVM type information for member access.
     */
    INTERNAL_ERR_F01F                             = 0xF01F,

    /**
     * An unsupported variable-definition kind (not parameter/global/local/
     * member) was encountered while generating a symbol, or its type could
     * not be mapped to an LLVM type.
     */
    INTERNAL_ERR_F020                             = 0xF020,

    /// No LLVM function declaration was found for a resolved function symbol.
    INTERNAL_ERR_F021                             = 0xF021,

    /// The LLVM function object registered for a resolved function is null.
    INTERNAL_ERR_F022                             = 0xF022,

    /// The sub-expression of an address-of ('&') operator produced no LLVM value.
    INTERNAL_ERR_F023                             = 0xF023,

    /**
     * A struct has no member with the expected name during codegen, or the
     * union definition/alternative for a member access could not be found.
     */
    INTERNAL_ERR_F024                             = 0xF024,

    /// The '.' operator is applied to a non-struct type during code generation.
    INTERNAL_ERR_F025                             = 0xF025,

    /**
     * An unsupported call-expression form (not direct/member/pointer-to-
     * member) reached code generation, or a sized/unsized array has no LLVM
     * struct type during subscript codegen.
     */
    INTERNAL_ERR_F026                             = 0xF026,

    /// A member function call has a non-symbol callee.
    INTERNAL_ERR_F027                             = 0xF027,

    /**
     * Failed to generate the 'this'/object argument for a member function
     * call, or the sub-expression of a drain ('#') operator produced no
     * LLVM value.
     */
    INTERNAL_ERR_F028                             = 0xF028,

    /// A call argument produced no LLVM value during code generation.
    INTERNAL_ERR_F029                             = 0xF029,

    /// No LLVM declaration was found for a (non-abstract, non-external-virtual) function.
    INTERNAL_ERR_F02A                             = 0xF02A,

    /// The LLVM function object is null for a resolved, non-abstract function.
    INTERNAL_ERR_F02B                             = 0xF02B,

    /**
     * A constructor-invocation/temporary-construction expression does not
     * refer to a resolved variable definition or struct type.
     */
    INTERNAL_ERR_F02C                             = 0xF02C,

    /// Failed to obtain an LLVM reference for the object being constructed.
    INTERNAL_ERR_F02D                             = 0xF02D,

    /**
     * Failed to generate an LLVM constant from a literal value expression
     * during primitive constructor invocation.
     */
    INTERNAL_ERR_F02E                             = 0xF02E,

    /**
     * Failed to generate an LLVM value for a primitive-variable
     * initialisation argument, or could not build a FunctionType for an
     * imported virtual dispatch call.
     */
    INTERNAL_ERR_F02F                             = 0xF02F,

    /**
     * A constructor argument produced no LLVM value, or an imported
     * aggregate has no LLVM struct type for virtual dispatch.
     */
    INTERNAL_ERR_F030                             = 0xF030,

    /// No LLVM declaration was found for a constructor of a given struct type.
    INTERNAL_ERR_F031                             = 0xF031,

    /// The LLVM constructor function object is null for a given struct type.
    INTERNAL_ERR_F032                             = 0xF032,

    /// A cast expression has an unresolved source or target type reaching code generation.
    INTERNAL_ERR_F033                             = 0xF033,

    /**
     * The expression being cast, or a reference/drain variable's storage
     * location, could not be resolved to an LLVM value/location.
     */
    INTERNAL_ERR_F034                             = 0xF034,

    /**
     * An assignment expression, or a reference-variable initialisation
     * argument, produced a null LLVM value.
     */
    INTERNAL_ERR_F035                             = 0xF035,

    /**
     * The '+=' expression produced a null left or right LLVM value, or a
     * reference variable has no initialisation argument.
     */
    INTERNAL_ERR_F036                             = 0xF036,

    /**
     * The '-=' expression produced a null left or right LLVM value, or a
     * sized array variable has no LLVM struct type.
     */
    INTERNAL_ERR_F037                             = 0xF037,

    /// The '*=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F038                             = 0xF038,

    /// The '/=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F039                             = 0xF039,

    /// The '%=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03A                             = 0xF03A,

    /// The '&=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03B                             = 0xF03B,

    /// The '|=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03C                             = 0xF03C,

    /// The '^=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03D                             = 0xF03D,

    /// The '<<=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03E                             = 0xF03E,

    /// The '>>=' expression produced a null left or right LLVM value.
    INTERNAL_ERR_F03F                             = 0xF03F,

    /// Dynamic cast: source or target is not a class/interface/annotation aggregate with RTTI.
    INTERNAL_ERR_F040                             = 0xF040,

    /// Dynamic cast: the RTTI global for the target class could not be found in the module.
    INTERNAL_ERR_F041                             = 0xF041,

    /// Dynamic cast: the source aggregate has no vtable/vptr.
    INTERNAL_ERR_F042                             = 0xF042,

    /// Dynamic cast: the source class's LLVM type was not built.
    INTERNAL_ERR_F043                             = 0xF043,

    /**
     * An indirect call through a function-reference variable produced no
     * LLVM value for the callee address.
     */
    INTERNAL_ERR_F044                             = 0xF044,

    /// An indirect call lacks a callable_type annotation on its callee.
    INTERNAL_ERR_F045                             = 0xF045,

    /// Could not map a K parameter type to its LLVM type for an indirect call.
    INTERNAL_ERR_F046                             = 0xF046,

    /// An argument for an indirect call produced no LLVM value.
    INTERNAL_ERR_F047                             = 0xF047,

    /// An INDIRECT_MEMBER (pointer-to-member) dispatch is missing its pm_expression callee.
    INTERNAL_ERR_F048                             = 0xF048,

    /// An INDIRECT_MEMBER call could not obtain the member function pointer.
    INTERNAL_ERR_F049                             = 0xF049,

    /**
     * A binary operator-overload function has no LLVM definition, whether
     * checked before evaluating operands or after failing virtual dispatch.
     */
    INTERNAL_ERR_F04A                             = 0xF04A,

    /// The left operand for a member binary operator overload produced no LLVM value.
    INTERNAL_ERR_F04B                             = 0xF04B,

    /// The right operand for a member binary operator overload produced no LLVM value.
    INTERNAL_ERR_F04C                             = 0xF04C,

    /// The left operand for a non-member binary operator overload produced no LLVM value.
    INTERNAL_ERR_F04D                             = 0xF04D,

    /// The right operand for a non-member binary operator overload produced no LLVM value.
    INTERNAL_ERR_F04E                             = 0xF04E,

    /**
     * A unary operator-overload function has no LLVM definition, whether
     * checked before evaluating the operand or after failing virtual dispatch.
     */
    INTERNAL_ERR_F04F                             = 0xF04F,

    /// The operand for a member unary operator overload produced no LLVM value.
    INTERNAL_ERR_F050                             = 0xF050,

    /// The operand for a non-member unary operator overload produced no LLVM value.
    INTERNAL_ERR_F051                             = 0xF051,

    /**
     * A casting-operator overload function (operator_cv) has no LLVM
     * definition, whether checked before evaluating the source expression
     * or after failing virtual dispatch.
     */
    INTERNAL_ERR_F052                             = 0xF052,

    /// The source operand for a casting operator overload produced no LLVM value.
    INTERNAL_ERR_F053                             = 0xF053,

    /// Failed to generate an LLVM value for an enum-variable initialisation argument.
    INTERNAL_ERR_F054                             = 0xF054,

    /**
     * A comparison "source" operator used for fallback synthesis has no LLVM
     * definition, the left operand of a comparison-operator-overload call
     * produced no LLVM value, or the generated LLVM module failed verification.
     */
    INTERNAL_ERR_F055                             = 0xF055,

    /**
     * A comparison "source" operator has no LLVM definition and is not
     * dispatched virtually, or the right operand of a comparison-operator-
     * overload call produced no LLVM value.
     */
    INTERNAL_ERR_F056                             = 0xF056,

    /// The '<=>' (spaceship) expression produced a null left or right LLVM value.
    INTERNAL_ERR_F057                             = 0xF057,

    /// The builtin '<=>' operator has a non-primitive operand reaching code generation.
    INTERNAL_ERR_F058                             = 0xF058,

    /// Foreach codegen reached with an unsupported/unresolved iteration kind (only ARRAY, ITERATOR, SEQUENCE are supported).
    INTERNAL_ERR_F059                             = 0xF059,

    /// Foreach ITERATOR/SEQUENCE: expected method (next/iterator/constIterator) not found on the resolved aggregate.
    INTERNAL_ERR_F05A                             = 0xF05A,

    /// Callable binding: no LLVM declaration was emitted for the bound target function.
    INTERNAL_ERR_F05B                             = 0xF05B,

    /// Callable binding: the requested binding source is not supported by this compiler phase.
    INTERNAL_ERR_F05C                             = 0xF05C,

    /// Callable invocation reached code generation without a resolved callable type or value.
    INTERNAL_ERR_F05D                             = 0xF05D,

    /// Callable invocation: a parameter or return type could not be mapped to an LLVM type.
    INTERNAL_ERR_F05E                             = 0xF05E,

    /// Callable invocation: an argument expression produced no LLVM value.
    INTERNAL_ERR_F05F                             = 0xF05F,
};

// ────────────────────────────────────────────────────────────────────────────
// Template diagnostics (instantiation, constraints, argument validation)
// ────────────────────────────────────────────────────────────────────────────
enum class template_diag : unsigned int {
    ERR_TPL_TOO_MANY_ARGS                         = 0x1001,
    ERR_TPL_TOO_FEW_ARGS                          = 0x1002,
    ERR_TPL_ARG_WRONG_KIND                        = 0x1003,
    ERR_TPL_ARG_CONSTRAINT_VIOLATED               = 0x1004,
    ERR_TPL_ARG_NOT_AGGREGATE                     = 0x1005,
    ERR_TPL_VALUE_ARG_NOT_CONSTANT                = 0x1006,
    ERR_TPL_VALUE_ARG_TYPE_MISMATCH               = 0x1007,

    /**
     * Template aggregate instantiation recursed past the maximum allowed
     * depth (e.g. a template that (directly or transitively) instantiates
     * itself as a base or member with an ever-changing argument, such as
     * `template<typename T> class Node : public Node<T*> { }`). Raised
     * instead of letting the compiler overflow its own call stack.
     */
    ERR_TPL_INSTANTIATION_DEPTH_EXCEEDED          = 0x1008,
    ERR_CTAD_NO_MATCH                             = 0x1009,
    ERR_CTAD_AMBIGUOUS                            = 0x100A,
};

// ────────────────────────────────────────────────────────────────────────────
// Generic diagnostics (constraint violation on generic declarations/usage)
// ────────────────────────────────────────────────────────────────────────────
enum class generic_diag : unsigned int {
    /**
     * A generic type parameter is used directly (not through an addresser).
     * E.g., a member variable 'val: T' where T is a generic param is forbidden;
     * use 'val: T!' or 'val: T&' instead.
     */
    ERR_GENERIC_DIRECT_TYPE_USAGE                 = 0x1101,

    /**
     * An owner addresser ('!') is applied to a generic type parameter whose
     * constraint is not 'class' or 'interface'.
     * Owner of a generic type param requires a class/interface constraint so
     * that the virtual destructor is reachable from the unifom synthesised code.
     */
    ERR_GENERIC_OWNER_REQUIRES_CLASS              = 0x1102,

    /**
     * A generic aggregate or function was instantiated with a concrete type
     * argument that does not satisfy the declared constraint.
     */
    ERR_GENERIC_ARG_CONSTRAINT_VIOLATED           = 0x1103,

    /**
     * A generic aggregate or function was instantiated with too many type
     * arguments.
     */
    ERR_GENERIC_TOO_MANY_ARGS                     = 0x1104,

    /**
     * A generic aggregate or function was instantiated with too few type
     * arguments (and no defaults are available).
     */
    ERR_GENERIC_TOO_FEW_ARGS                      = 0x1105,
};

// ────────────────────────────────────────────────────────────────────────────
// Exception diagnostics (throw, try-catch, throws clause, contract verification)
// ────────────────────────────────────────────────────────────────────────────
enum class exception_diag : unsigned int {
    /**
     * The expression in a throw statement does not derive from ::k::Throwable.
     */
    ERR_THROW_NOT_EXCEPTION_TYPE                  = 0x1201,

    /**
     * A catch clause uses a type that does not derive from ::k::Throwable.
     */
    ERR_CATCH_NOT_EXCEPTION_TYPE                  = 0x1202,

    /**
     * A catch clause must catch by reference (&) addresser.
     */
    ERR_CATCH_MUST_BE_REFERENCE                   = 0x1203,

    /**
     * A type name in a throws clause could not be resolved to a known type.
     */
    ERR_THROWS_TYPE_NOT_FOUND                     = 0x1204,

    /**
     * A type in a throws clause does not derive from ::k::Throwable.
     */
    ERR_THROWS_NOT_EXCEPTION_TYPE                 = 0x1205,

    /**
     * A function throws an exception type not declared in its throws clause.
     */
    ERR_THROW_UNDECLARED_EXCEPTION                = 0x1206,

    /**
     * A call to a throwing function is not inside a try-catch block and the
     * calling function does not declare the exception in its own throws clause.
     */
    ERR_UNCAUGHT_EXCEPTION                        = 0x1207,

    /**
     * A catch clause catches a type that is a supertype of a previous catch
     * clause in the same try-catch — the later clause is unreachable.
     */
    WARN_CATCH_UNREACHABLE                        = 0x1208,

    /**
     * Duplicate exception type in a throws clause.
     */
    WARN_THROWS_DUPLICATE_TYPE                    = 0x1209,

    /**
     * A bare 'throw;' (rethrow) statement appears outside a catch block.
     */
    ERR_RETHROW_OUTSIDE_CATCH                     = 0x120A,
};


/**
 * Code-generation diagnostics for callables (function references, functors, lambdas).
 * Range 0x01D0 — 0x01DF.
 */
enum class callable_diag : unsigned int {
    /** A bare prototype (no addresser) was used where a value type is required. */
    ERR_CALLABLE_PROTOTYPE_NOT_INSTANTIABLE       = 0x1301,
    /** An addresser that a callable does not support (`!`, `#`, `[]`) was applied. */
    ERR_CALLABLE_BAD_ADDRESSER                    = 0x1302,
    /** `null` was assigned to a non-null callable (`+` or `&`). */
    ERR_CALLABLE_NULL_TO_NONNULL                  = 0x1303,
    /** A non-rebindable callable (`?` or `&`) was assigned after initialisation. */
    ERR_CALLABLE_NOT_REBINDABLE                   = 0x1304,
    /** The bound target signature is not compatible with the callable prototype. */
    ERR_CALLABLE_INCOMPATIBLE_SIGNATURE           = 0x1305,
    /** No overload of the named function matches the callable prototype. */
    ERR_CALLABLE_NO_MATCHING_OVERLOAD             = 0x1306,
    /** Several overloads of the named function match the callable prototype. */
    ERR_CALLABLE_AMBIGUOUS_OVERLOAD               = 0x1307,
    /** The target type is not a functional interface (exactly one abstract slot). */
    ERR_CALLABLE_NOT_FUNCTIONAL_IFACE             = 0x1308,
    /** The functional interface method signature does not match the callable prototype. */
    ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH         = 0x1309,
    /** An operator that is not defined on callables was applied to one. */
    ERR_CALLABLE_OP_FORBIDDEN                     = 0x130A,
    /** Binding a non-static member function requires a receiver object. */
    ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT      = 0x130B,
    /** A co/contravariant binding would require a representation adjustment. */
    ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT      = 0x130C,
    /** Several `operator()` overloads match the call arguments. */
    ERR_CALLABLE_AMBIGUOUS_OPERATOR_CALL          = 0x130D,
    /** The callee expression is not invocable. */
    ERR_CALLABLE_NOT_INVOCABLE                    = 0x130E,
    /** The number of call arguments does not match the callable prototype. */
    ERR_CALLABLE_ARG_COUNT_MISMATCH               = 0x130F,
    /** A callable may outlive the context object it is bound to. */
    WARN_CALLABLE_DANGLING_CONTEXT                = 0x1310,
};

/**
 * Parser diagnostics for lambda expressions.
 * Range 0x01E0 — 0x01EF.
 */
enum class lambda_diag : unsigned int {
    /** A lambda capture list uses an unsupported or malformed spelling. */
    ERR_LAMBDA_BAD_CAPTURE_SYNTAX                 = 0x1401,
};

/**
 * Diagnostics for the Application entry-point mechanism (user-declared
 * `class Application` in an executable module).
 * Range 0x01F0 — 0x01FF.
 */
enum class application_diag : unsigned int {
    /** A user-declared `class Application` does not (directly or transitively)
     * extend `::k::Application`. */
    ERR_APPLICATION_MUST_EXTEND_K_APPLICATION     = 0x1501,
    /** A user-declared `class Application` is missing a usable (non-deleted,
     * non-abstract) `main` method. */
    ERR_APPLICATION_NO_USABLE_MAIN                = 0x1502,
    /** A user-declared `class Application` declares more than one usable
     * (non-deleted, non-abstract) `main` method. */
    ERR_APPLICATION_MULTIPLE_MAIN                 = 0x1503,
    /** A user-declared `class Application` is itself abstract; it must be
     * concrete/instantiable. */
    ERR_APPLICATION_MUST_NOT_BE_ABSTRACT          = 0x1504,

    // ── Phase 4: abstract Application-chain diagnostics ──────────────────────

    /** At a given level of the `::k::Application`-derived abstract class
     * chain, zero, or more than one, of the four standard `main` signatures
     * (`main()`, `main():int`, `main(args:const String[])`,
     * `main(args:const String[]):int`) is left non-deleted; exactly one must
     * remain active to decide (or continue) the entry-point chain. */
    ERR_APPLICATION_CHAIN_BAD_ACTIVE_MAIN_COUNT   = 0x1505,
    /** An implemented standard `main` that delegates to a custom abstract
     * `main` must be paired with exactly one custom (non-standard-shaped)
     * abstract `main` method declared in the same class. */
    ERR_APPLICATION_CHAIN_BAD_DELEGATE_COUNT      = 0x1506,
    /** The custom `main` used as a delegation target (or as a further
     * delegation point down the chain) must be abstract (no body). */
    ERR_APPLICATION_CHAIN_DELEGATE_NOT_ABSTRACT   = 0x1507,
    /** A class in the `::k::Application` chain declares a `main` overload
     * that does not match the signature currently required by an outer
     * (less-derived) class in the chain. */
    ERR_APPLICATION_CHAIN_UNEXPECTED_MAIN         = 0x1508,
    /** A class in the `::k::Application` chain marks the currently required
     * `main` override as deleted; the required entry-point method cannot be
     * deleted once selected by an outer class. */
    ERR_APPLICATION_CHAIN_REQUIRED_MAIN_DELETED   = 0x1509,
    /** The final, concrete `class Application` does not implement the `main`
     * signature required by the abstract `::k::Application` chain above it. */
    ERR_APPLICATION_CHAIN_FINAL_MAIN_NOT_IMPLEMENTED = 0x150A,
};

} // namespace k::diag

#endif // KLANG_ERRORS_GEN_HPP
