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
    ERR_ARITH_TYPE_MISMATCH                       = 0x00DF,
    ERR_ARITH_NO_COMMON_TYPE                      = 0x00E0,
    ERR_ARITH_MODULO_NOT_INT                      = 0x00E1,
    ERR_ASSIGN_INCOMPATIBLE                       = 0x00E6,
    ERR_ASSIGN_TO_CONST                           = 0x00E7,
    ERR_MAIN_WRONG_RETURN_TYPE                    = 0x00E8,
    ERR_MAIN_WRONG_PARAMS                         = 0x00E9,
    ERR_PREINC_NOT_REF                            = 0x010B,
    ERR_PREINC_NOT_NUMERIC                        = 0x010C,
    ERR_PREDEC_NOT_REF                            = 0x010D,
    ERR_POSTINC_NOT_REF                           = 0x010E,
    ERR_POSTINC_NOT_NUMERIC                       = 0x010F,
    ERR_SHIFT_NOT_INT                             = 0x0118,
    ERR_SHIFT_INCOMPATIBLE                        = 0x0119,
    ERR_LOGICAL_NOT_BOOL                          = 0x011A,
    ERR_LOGICAL_AND_INCOMPATIBLE                  = 0x011B,
    WARN_IMPLICIT_LOSSY_CAST                      = 0x011C,
    ERR_OVERLOAD_CALL_NO_MATCH                    = 0x011D,
    ERR_OVERLOAD_ARG_TYPE_MISMATCH                = 0x011F,
    ERR_OVERLOAD_RETURN_TYPE_MISMATCH             = 0x0120,
    ERR_OVERLOAD_CONST_MISMATCH                   = 0x0121,
    ERR_OVERLOAD_VISIBILITY_DENIED                = 0x0122,
    ERR_OVERLOAD_NOT_FOUND                        = 0x0123,
    ERR_BINARY_OVERLOAD_BAD_RECEIVER              = 0x0124,
    ERR_UNARY_OVERLOAD_BAD_RECEIVER               = 0x0125,
    ERR_PM_EXPR_BAD_TYPE                          = 0x0126,
    ERR_PM_EXPR_NOT_MEMBER_PTR                    = 0x0127,
    ERR_PM_EXPR_INCOMPATIBLE                      = 0x0128,
    ERR_SUBSCRIPT_OVERLOAD_NOT_FOUND              = 0x012A,
    ERR_SUBSCRIPT_OVERLOAD_CONST                  = 0x012B,
    ERR_SUBSCRIPT_OVERLOAD_BAD_RETURN             = 0x012C,
    ERR_BITWISE_AND_INCOMPATIBLE                  = 0x0168,
    ERR_BITWISE_OR_INCOMPATIBLE                   = 0x0169,
    ERR_BITWISE_XOR_INCOMPATIBLE                  = 0x016A,
    ERR_SHIFT_ASSIGN_INCOMPATIBLE                 = 0x016B,
    ERR_BITWISE_ASSIGN_INCOMPATIBLE               = 0x016C,
    ERR_ADD_ASSIGN_INCOMPATIBLE                   = 0x016D,
    ERR_MUL_ASSIGN_INCOMPATIBLE                   = 0x016F,
    ERR_SUB_ASSIGN_INCOMPATIBLE                   = 0x0171,
    ERR_DIV_ASSIGN_INCOMPATIBLE                   = 0x0172,
};

// ────────────────────────────────────────────────────────────────────────────
// Variable definition diagnostics (initialization, type compatibility)
// ────────────────────────────────────────────────────────────────────────────
enum class variable_diag : unsigned int {
    ERR_VAR_TYPE_UNRESOLVED                       = 0x00E2,
    ERR_VAR_INIT_TYPE_MISMATCH                    = 0x00E3,
    ERR_VAR_INIT_ARG_MISMATCH                     = 0x00E4,
    ERR_STRUCT_VAR_NO_CTOR                        = 0x012F,
    ERR_STRUCT_VAR_CTOR_AMBIGUOUS                 = 0x0130,
    ERR_STRUCT_VAR_CTOR_ARG_MISMATCH              = 0x0131,
    ERR_STRUCT_VAR_CTOR_NOT_FOUND                 = 0x0132,
    ERR_REF_VAR_NEEDS_INIT                        = 0x0136,
    ERR_REF_VAR_INCOMPATIBLE_INIT                 = 0x0137,
    ERR_REF_VAR_WRONG_SUBTYPE                     = 0x0138,
    ERR_REF_VAR_CONST_MISMATCH                    = 0x0139,
    ERR_REF_VAR_MULTIPLE_INIT                     = 0x013A,
    ERR_SIZED_ARRAY_INIT_MISMATCH                 = 0x013B,
    ERR_LINK_VAR_NEEDS_INIT                       = 0x015A,
    ERR_LINK_VAR_INIT_MISMATCH                    = 0x015B,
    WARN_NARROWING_INIT                           = 0x015C,
    ERR_LINK_VAR_WRONG_TYPE                       = 0x015D,
    ERR_VIEW_VAR_NEEDS_INIT                       = 0x015E,
    ERR_VIEW_VAR_INIT_MISMATCH                    = 0x015F,
    ERR_VIEW_VAR_WRONG_TYPE                       = 0x0160,
    ERR_POINTER_VAR_INIT_MISMATCH                 = 0x0161,
    ERR_OWNER_VAR_NEEDS_INIT                      = 0x0162,
    ERR_OWNER_VAR_INIT_MISMATCH                   = 0x0163,
};

// ────────────────────────────────────────────────────────────────────────────
// Statement diagnostics (return, if, while, for, expression statements)
// ────────────────────────────────────────────────────────────────────────────
enum class statement_diag : unsigned int {
    ERR_RETURN_TYPE_MISMATCH                      = 0x00EA,
    ERR_IF_COND_NOT_BOOL                          = 0x00EB,
    ERR_WHILE_COND_NOT_BOOL                       = 0x00EC,
    ERR_FOR_COND_NOT_BOOL                         = 0x00ED,
    WARN_UNUSED_EXPR_RESULT                       = 0x0164,
    WARN_UNREACHABLE_AFTER_RETURN                 = 0x0165,
    ERR_LOCAL_VAR_TYPE_UNRESOLVED                 = 0x0167,
};

// ────────────────────────────────────────────────────────────────────────────
// Code generation diagnostics (LLVM IR, vtable, constructors, internal errors)
// ────────────────────────────────────────────────────────────────────────────
enum class codegen_diag : unsigned int {
    ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS               = 0x011E,
    INTERNAL_ERR_F001                             = 0xF001,
    INTERNAL_ERR_F002                             = 0xF002,
    INTERNAL_ERR_F003                             = 0xF003,
    INTERNAL_ERR_F004                             = 0xF004,
    INTERNAL_ERR_F005                             = 0xF005,
    INTERNAL_ERR_F006                             = 0xF006,
    INTERNAL_ERR_F007                             = 0xF007,
    INTERNAL_ERR_F008                             = 0xF008,
    INTERNAL_ERR_F009                             = 0xF009,
    INTERNAL_ERR_F00A                             = 0xF00A,
    INTERNAL_ERR_F00B                             = 0xF00B,
    INTERNAL_ERR_F00C                             = 0xF00C,
    INTERNAL_ERR_F00D                             = 0xF00D,
    INTERNAL_ERR_F00E                             = 0xF00E,
    INTERNAL_ERR_F00F                             = 0xF00F,
    INTERNAL_ERR_F010                             = 0xF010,
    INTERNAL_ERR_F011                             = 0xF011,
    INTERNAL_ERR_F012                             = 0xF012,
    INTERNAL_ERR_F013                             = 0xF013,
    INTERNAL_ERR_F014                             = 0xF014,
    INTERNAL_ERR_F015                             = 0xF015,
    INTERNAL_ERR_F016                             = 0xF016,
    INTERNAL_ERR_F017                             = 0xF017,
    INTERNAL_ERR_F018                             = 0xF018,
    INTERNAL_ERR_F019                             = 0xF019,
    INTERNAL_ERR_F01A                             = 0xF01A,
    INTERNAL_ERR_F01B                             = 0xF01B,
    INTERNAL_ERR_F01C                             = 0xF01C,
    INTERNAL_ERR_F01D                             = 0xF01D,
    INTERNAL_ERR_F01E                             = 0xF01E,
    INTERNAL_ERR_F01F                             = 0xF01F,
    INTERNAL_ERR_F020                             = 0xF020,
    INTERNAL_ERR_F021                             = 0xF021,
    INTERNAL_ERR_F022                             = 0xF022,
    INTERNAL_ERR_F023                             = 0xF023,
    INTERNAL_ERR_F024                             = 0xF024,
    INTERNAL_ERR_F025                             = 0xF025,
    INTERNAL_ERR_F026                             = 0xF026,
    INTERNAL_ERR_F027                             = 0xF027,
    INTERNAL_ERR_F028                             = 0xF028,
    INTERNAL_ERR_F029                             = 0xF029,
    INTERNAL_ERR_F02A                             = 0xF02A,
    INTERNAL_ERR_F02B                             = 0xF02B,
    INTERNAL_ERR_F02C                             = 0xF02C,
    INTERNAL_ERR_F02D                             = 0xF02D,
    INTERNAL_ERR_F02E                             = 0xF02E,
    INTERNAL_ERR_F02F                             = 0xF02F,
    INTERNAL_ERR_F030                             = 0xF030,
    INTERNAL_ERR_F031                             = 0xF031,
    INTERNAL_ERR_F032                             = 0xF032,
    INTERNAL_ERR_F033                             = 0xF033,
    INTERNAL_ERR_F034                             = 0xF034,
    INTERNAL_ERR_F035                             = 0xF035,
    INTERNAL_ERR_F036                             = 0xF036,
    INTERNAL_ERR_F037                             = 0xF037,
    INTERNAL_ERR_F038                             = 0xF038,
    INTERNAL_ERR_F039                             = 0xF039,
    INTERNAL_ERR_F03A                             = 0xF03A,
    INTERNAL_ERR_F03B                             = 0xF03B,
    INTERNAL_ERR_F03C                             = 0xF03C,
    INTERNAL_ERR_F03D                             = 0xF03D,
    INTERNAL_ERR_F03E                             = 0xF03E,
    INTERNAL_ERR_F03F                             = 0xF03F,
    INTERNAL_ERR_F040                             = 0xF040,
    INTERNAL_ERR_F041                             = 0xF041,
    INTERNAL_ERR_F042                             = 0xF042,
    INTERNAL_ERR_F043                             = 0xF043,
    INTERNAL_ERR_F044                             = 0xF044,
    INTERNAL_ERR_F045                             = 0xF045,
    INTERNAL_ERR_F046                             = 0xF046,
    INTERNAL_ERR_F047                             = 0xF047,
    INTERNAL_ERR_F048                             = 0xF048,
    INTERNAL_ERR_F049                             = 0xF049,
    INTERNAL_ERR_F04A                             = 0xF04A,
    INTERNAL_ERR_F04B                             = 0xF04B,
    INTERNAL_ERR_F04C                             = 0xF04C,
    INTERNAL_ERR_F04D                             = 0xF04D,
    INTERNAL_ERR_F04E                             = 0xF04E,
    INTERNAL_ERR_F04F                             = 0xF04F,
    INTERNAL_ERR_F050                             = 0xF050,
    INTERNAL_ERR_F051                             = 0xF051,
    INTERNAL_ERR_F052                             = 0xF052,
    INTERNAL_ERR_F053                             = 0xF053,
    INTERNAL_ERR_F054                             = 0xF054,
    INTERNAL_ERR_F055                             = 0xF055,
};

// ────────────────────────────────────────────────────────────────────────────
// Template diagnostics (instantiation, constraints, argument validation)
// ────────────────────────────────────────────────────────────────────────────
enum class template_diag : unsigned int {
    ERR_TPL_TOO_MANY_ARGS                         = 0x0180,
    ERR_TPL_TOO_FEW_ARGS                          = 0x0181,
    ERR_TPL_ARG_WRONG_KIND                        = 0x0182,
    ERR_TPL_ARG_CONSTRAINT_VIOLATED               = 0x0183,
    ERR_TPL_ARG_NOT_AGGREGATE                     = 0x0184,
    ERR_TPL_VALUE_ARG_NOT_CONSTANT                = 0x0185,
    ERR_TPL_VALUE_ARG_TYPE_MISMATCH               = 0x0186,
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
    ERR_GENERIC_DIRECT_TYPE_USAGE                 = 0x01B0,

    /**
     * An owner addresser ('!') is applied to a generic type parameter whose
     * constraint is not 'class' or 'interface'.
     * Owner of a generic type param requires a class/interface constraint so
     * that the virtual destructor is reachable from the unifom synthesised code.
     */
    ERR_GENERIC_OWNER_REQUIRES_CLASS              = 0x01B1,

    /**
     * A generic aggregate or function was instantiated with a concrete type
     * argument that does not satisfy the declared constraint.
     */
    ERR_GENERIC_ARG_CONSTRAINT_VIOLATED           = 0x01B2,

    /**
     * A generic aggregate or function was instantiated with too many type
     * arguments.
     */
    ERR_GENERIC_TOO_MANY_ARGS                     = 0x01B3,

    /**
     * A generic aggregate or function was instantiated with too few type
     * arguments (and no defaults are available).
     */
    ERR_GENERIC_TOO_FEW_ARGS                      = 0x01B4,
};

// ────────────────────────────────────────────────────────────────────────────
// Exception diagnostics (throw, try-catch, throws clause, contract verification)
// ────────────────────────────────────────────────────────────────────────────
enum class exception_diag : unsigned int {
    /**
     * The expression in a throw statement does not derive from ::k::Throwable.
     */
    ERR_THROW_NOT_EXCEPTION_TYPE                  = 0x01C0,

    /**
     * A catch clause uses a type that does not derive from ::k::Throwable.
     */
    ERR_CATCH_NOT_EXCEPTION_TYPE                  = 0x01C1,

    /**
     * A catch clause must catch by reference (&) addresser.
     */
    ERR_CATCH_MUST_BE_REFERENCE                   = 0x01C2,

    /**
     * A type name in a throws clause could not be resolved to a known type.
     */
    ERR_THROWS_TYPE_NOT_FOUND                     = 0x01C3,

    /**
     * A type in a throws clause does not derive from ::k::Throwable.
     */
    ERR_THROWS_NOT_EXCEPTION_TYPE                 = 0x01C4,

    /**
     * A function throws an exception type not declared in its throws clause.
     */
    ERR_THROW_UNDECLARED_EXCEPTION                = 0x01C5,

    /**
     * A call to a throwing function is not inside a try-catch block and the
     * calling function does not declare the exception in its own throws clause.
     */
    ERR_UNCAUGHT_EXCEPTION                        = 0x01C6,

    /**
     * A catch clause catches a type that is a supertype of a previous catch
     * clause in the same try-catch — the later clause is unreachable.
     */
    WARN_CATCH_UNREACHABLE                        = 0x01C7,

    /**
     * Duplicate exception type in a throws clause.
     */
    WARN_THROWS_DUPLICATE_TYPE                    = 0x01C8,

    /**
     * A bare 'throw;' (rethrow) statement appears outside a catch block.
     */
    ERR_RETHROW_OUTSIDE_CATCH                     = 0x01C9,
};


} // namespace k::diag

#endif // KLANG_ERRORS_GEN_HPP
