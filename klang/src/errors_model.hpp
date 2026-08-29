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

#ifndef KLANG_ERRORS_MODEL_HPP
#define KLANG_ERRORS_MODEL_HPP
#include "errors_lex_parse.hpp"

namespace k::diag {

// ────────────────────────────────────────────────────────────────────────────
// Model building diagnostics (scope validation, visibility, statement placement)
// ────────────────────────────────────────────────────────────────────────────
enum class model_diag : unsigned int {
    ERR_VISIBILITY_BAD_SCOPE                      = 0x0401,
    ERR_VISIBILITY_INVALID_KEYWORD                = 0x0402,
    ERR_STRUCT_BAD_SCOPE                          = 0x0403,
    ERR_VAR_BAD_SCOPE                             = 0x0404,
    ERR_FUNC_BAD_SCOPE                            = 0x0405,
    ERR_CTOR_HAS_RETURN_TYPE                      = 0x0406,
    ERR_DTOR_HAS_RETURN_TYPE                      = 0x0407,
    ERR_DTOR_HAS_PARAMS                           = 0x0408,
    ERR_FUNC_OPERATOR_BAD_SCOPE                   = 0x0409,
    ERR_FUNC_BLOCK_UNEXPECTED                     = 0x040A,
    ERR_FUNC_STATIC_CTOR_BAD_SCOPE                = 0x040B,
    ERR_IF_STMT_BAD_SCOPE                         = 0x040C,
    ERR_IF_STMT_NEEDS_CONDITION                   = 0x040D,
    ERR_IF_STMT_NEEDS_BODY                        = 0x040E,
    ERR_ELSE_CLAUSE_BAD_BODY                      = 0x040F,
    ERR_WHILE_STMT_BAD_SCOPE                      = 0x0410,
    ERR_WHILE_STMT_NEEDS_CONDITION                = 0x0411,
    ERR_WHILE_STMT_NEEDS_BODY                     = 0x0412,
    ERR_FOR_STMT_BAD_SCOPE                        = 0x0413,
    ERR_FOR_STMT_BAD_CONDITION                    = 0x0414,
    ERR_FOR_STMT_BAD_STEP                         = 0x0415,
    ERR_FOR_STMT_NEEDS_BODY                       = 0x0416,
    ERR_EXPR_STMT_BAD_SCOPE                       = 0x0417,
    ERR_UNSUPPORTED_BINARY_OP                     = 0x0418,
    ERR_UNSUPPORTED_UNARY_PREFIX_OP               = 0x0419,
    ERR_MEMBER_ACCESS_NOT_IDENTIFIER              = 0x041A,
    ERR_UNSUPPORTED_MEMBER_ACCESS_OP              = 0x041B,
    ERR_UNSUPPORTED_POSTFIX_OP                    = 0x041C,
    ERR_STATIC_DTOR_HAS_RETURN_TYPE               = 0x041D,
    ERR_STATIC_CTOR_HAS_PARAMS                    = 0x041E,
    ERR_STATIC_DTOR_HAS_PARAMS                    = 0x041F,
    ERR_RETURN_VAR_TYPE_MISMATCH                  = 0x0420,
    ERR_FUNC_VIRTUAL_ON_STRUCT                    = 0x0421,
    ERR_FUNC_DUPLICATE_DEFINITION                 = 0x0422,
    ERR_ABSTRACT_ON_STRUCT                        = 0x0423,
    ERR_ABSTRACT_BAD_DECL_SCOPE                   = 0x0424,
    ERR_ABSTRACT_ON_STATIC                        = 0x0425,
    ERR_ABSTRACT_ON_FINAL                         = 0x0426,
    ERR_ABSTRACT_WITH_BODY                        = 0x0427,
    ERR_ABSTRACT_ON_PRIVATE                       = 0x0428,
    ERR_FUNC_ABSTRACT_BAD_SCOPE                   = 0x0429,
    WARN_ABSTRACT_REDUNDANT_ON_IFACE              = 0x042A,
    WARN_ABSTRACT_REDUNDANT_ON_IFACE_METHOD       = 0x042B,
    WARN_IFACE_NON_VIRTUAL_FUNC                   = 0x042C,
    ERR_DEFAULT_PARAM_IN_BODY                     = 0x042D,
    ERR_FUNC_NO_IMPL_NO_ABSTRACT                  = 0x042E,
    ERR_USING_FILTER_INVALID                      = 0x042F,
    ERR_USING_BAD_SCOPE                           = 0x0430,
    ERR_BRACE_INIT_INTERNAL                       = 0x0431,
    ERR_ENUM_BAD_SCOPE                            = 0x0432,
    ERR_ENUM_DUPLICATE_DEFAULT                    = 0x0433,
    ERR_ENUM_ENTRY_VALUE_NOT_INT                  = 0x0434,
    ERR_RETURN_VAR_NAME_MISMATCH                  = 0x0435,
    ERR_RETURN_VAR_NOT_IN_FUNC                    = 0x0436,
    ERR_RETURN_VAR_TYPE_NOT_REF                   = 0x0437,
    ERR_FINAL_ON_CTOR_DTOR                        = 0x0438,
    ERR_OVERRIDE_ON_STATIC                        = 0x0439,
    ERR_OVERRIDE_ON_ABSTRACT                      = 0x043A,
    ERR_OVERRIDE_ON_CTOR_DTOR                     = 0x043B,
    ERR_OVERRIDE_ON_STRUCT                        = 0x043C,
    ERR_BREAK_NOT_IN_LOOP                         = 0x043D,
    ERR_CONTINUE_NOT_IN_LOOP                      = 0x043E,
    // Interface default methods ('default' prefix specifier)
    ERR_DEFAULT_OUTSIDE_INTERFACE                 = 0x043F,
    ERR_DEFAULT_REQUIRES_BODY                     = 0x0440,
    ERR_DEFAULT_INVALID_SPECIFIER                 = 0x0441,
    ERR_DEFAULT_ON_PRIVATE                        = 0x0442,
    ERR_DEFAULT_ON_CTOR_DTOR                      = 0x0443,
    // Foreach statement model-building errors (0x01F6-0x01FF)
    ERR_FOREACH_STMT_BAD_SCOPE                    = 0x0444,
    ERR_FOREACH_STMT_NEEDS_BODY                   = 0x0445,
};

// ────────────────────────────────────────────────────────────────────────────
// Symbol resolution diagnostics (name lookup, visibility, redirections, overloads)
// ────────────────────────────────────────────────────────────────────────────
enum class symbol_diag : unsigned int {
    ERR_SYMBOL_NOT_FOUND                          = 0x0501,
    ERR_UNRESOLVED_IDENTIFIER                     = 0x0502,
    ERR_STATIC_CTOR_INIT_FAILED                   = 0x0503,
    ERR_VISIBILITY_ACCESS_DENIED                  = 0x0504,
    ERR_AGGREGATE_VISIBILITY_DENIED               = 0x0505,
    WARN_UNUSED_PRIVATE_CTOR                      = 0x0506,
    ERR_FUNC_RETURN_UNRESOLVED                    = 0x0507,
    ERR_REDIRECT_CHAIN_CYCLE                      = 0x0508,
    ERR_REDIRECT_TARGET_NOT_FOUND                 = 0x0509,
    ERR_REDIRECT_AMBIGUOUS                        = 0x050A,
    ERR_REDIRECT_SELF_REF                         = 0x050B,
    ERR_REDIRECT_INCOMPATIBLE_SIG                 = 0x050C,
    ERR_DUPLICATE_BASE_CLASS                      = 0x050D,
    ERR_INIT_ORDER_CYCLE                          = 0x050E,
    ERR_OVERLOAD_AMBIGUOUS                        = 0x050F,
    ERR_OVERLOAD_NO_MATCH                         = 0x0510,
    ERR_FUNC_VISIBILITY_DENIED                    = 0x0511,
    ERR_BINARY_OVERLOAD_NOT_FOUND                 = 0x0512,
    ERR_UNARY_OVERLOAD_NOT_FOUND                  = 0x0513,
};

// ────────────────────────────────────────────────────────────────────────────
// Aggregate type diagnostics (inheritance, enums, annotations, virtuality)
// ────────────────────────────────────────────────────────────────────────────
enum class structure_diag : unsigned int {
    ERR_BASE_NOT_FOUND                            = 0x0601,
    ERR_STRUCT_SELF_INHERIT                       = 0x0602,
    ERR_BASE_IS_FINAL                             = 0x0603,
    ERR_CONST_STRUCT_MUTABLE_BASE                 = 0x0604,
    ERR_CROSS_STRUCT_CLASS                        = 0x0605,
    ERR_PRIVATE_OVERRIDE                          = 0x0606,
    ERR_ANNOTATION_MISSING_TARGET                 = 0x0607,
    ERR_ANNOTATION_BAD_TYPE                       = 0x0608,
    ERR_ANNOTATION_TARGET_MISMATCH                = 0x0609,
    ERR_ENUM_UNDERLYING_NOT_INT                   = 0x060A,
    ERR_ENUM_BASE_NOT_ENUM                        = 0x060B,
    ERR_ENUM_ENTRY_AMBIGUOUS                      = 0x060C,
    ERR_ENUM_ENTRY_NOT_FOUND                      = 0x060D,
    WARN_ENUM_ENTRY_SHADOW                        = 0x060E,
    WARN_CONST_STRUCT_NON_CONST_BASE              = 0x060F,
    WARN_CONST_STRUCT_NON_CONST_MEMBER            = 0x0610,
    ERR_ABSTRACT_METHOD_IN_NON_ABSTRACT           = 0x0611,
    ERR_INHERITED_ABSTRACT_NOT_IMPL               = 0x0612,
    WARN_OVERRIDE_FINAL                           = 0x0613,
    WARN_MISSING_OVERRIDE                         = 0x0614,
    ERR_OVERRIDE_NOT_OVERRIDING                   = 0x0615,
    ERR_STRUCT_RECURSIVE_FORBIDDEN                = 0x0616,
    WARN_REDUNDANT_INHERITED_REDECL               = 0x0617,
    WARN_HIDES_INHERITED_DEFAULT_METHOD           = 0x0618,
    WARN_NESTED_INHERITS_ENCLOSING                = 0x0619,  ///< Inner struct inherits from its enclosing struct
    WARN_NESTED_INHERITS_INNER                    = 0x061A,  ///< Struct inherits from one of its own inner structs
    WARN_IMPLICIT_COPY_CTOR_GENERATED             = 0x061B,  ///< Implicit copy constructor generated for a struct with bases/struct members
    ERR_VIRTUAL_DTOR_SECONDARY_BASE               = 0x061C,  ///< Virtual destructor reachable only via a secondary (non-primary) vtable base — needs a this-adjustment thunk, not yet supported (see TODO.md)
    ERR_ENUM_EXPLICIT_UNDERLYING_NOT_INTEGER      = 0x061D,  ///< 'enum X : <primitive>' names a non-integer primitive type (float/double/bool/char)
    ERR_ENUM_EXPLICIT_UNDERLYING_TOO_SMALL        = 0x061E,  ///< 'enum X : <primitive>' cannot hold the range of declared entry values
};

// ────────────────────────────────────────────────────────────────────────────
// Union type diagnostics (alternatives, construction, member access)
// ────────────────────────────────────────────────────────────────────────────
enum class union_diag : unsigned int {
    ERR_UNION_DRAIN_ADDRESSER                     = 0x0701,  ///< Drain '#' on union alternative
    ERR_UNION_MEMBER_NOT_FOUND                    = 0x0702,  ///< Unknown alternative name
    ERR_UNION_INVALID_MEMBER                      = 0x0703,  ///< Non-variable decl inside union body
    ERR_UNION_TYPE_MISMATCH                       = 0x0704,  ///< Wrong alternative access (compile-time)
    ERR_UNION_AMBIGUOUS_IMPLICIT_ASSIGN           = 0x0705,  ///< Multiple alternatives match RHS type
    ERR_UNION_NO_DEFAULT_CTOR                     = 0x0706,  ///< First alternative not default-constructible
    ERR_UNION_MULTIPLE_INHERITANCE                = 0x0707,  ///< More than one base union
    ERR_UNION_BASE_NOT_UNION                      = 0x0708,  ///< Base type is not a union
    ERR_UNION_TEMPLATE_INHERITANCE_NOT_SUPPORTED  = 0x0709,  ///< Template union with base not supported
    ERR_UNION_CIRCULAR_INHERITANCE                = 0x070A,  ///< Circular union inheritance chain
    ERR_UNION_ASSIGN_TYPE_MISMATCH                = 0x070B,  ///< Union assignment between unrelated types
    ERR_UNION_POLYMORPHIC_BASE_INVALID            = 0x070C,  ///< Base type in union declaration is neither a union nor a class/interface
    ERR_UNION_POLYMORPHIC_ALT_NOT_CLASS           = 0x070D,  ///< Alternative in polymorphic union is not a class type
    ERR_UNION_POLYMORPHIC_ALT_NOT_DERIVED         = 0x070E,  ///< Alternative in polymorphic union does not inherit from the polymorphic base
    ERR_UNION_POLYMORPHIC_ALT_ABSTRACT            = 0x070F,  ///< Alternative in polymorphic union cannot be an abstract class or interface
    ERR_UNION_CAST_AMBIGUOUS                      = 0x0710,  ///< Multiple alternatives match cast target type
    ERR_UNION_CAST_TYPE_NOT_FOUND                 = 0x0711,  ///< No alternative matches cast target type
};

// ────────────────────────────────────────────────────────────────────────────
// Function & parameter diagnostics (signatures, return types, abstract, access)
// ────────────────────────────────────────────────────────────────────────────
enum class function_diag : unsigned int {
    ERR_FUNC_ANNOTATION_MISMATCH                  = 0x0801,
    WARN_FUNC_BODY_IGNORED                        = 0x0802,
    ERR_FUNC_ABSTRACT_HAS_BODY                    = 0x0803,
    ERR_PARAM_TYPE_UNRESOLVED                     = 0x0804,
    WARN_PARAM_DRAIN_NON_STRUCT                   = 0x0805,
    ERR_PARAM_DRAIN_MUST_BE_LAST                  = 0x0806,
    ERR_PARAM_DEFAULT_TYPE_MISMATCH               = 0x0807,
    WARN_PARAM_DEFAULT_NARROWING                  = 0x0808,
    ERR_PARAM_VOID_NOT_ALLOWED                    = 0x0809,
    ERR_FUNC_ACCESS_DENIED                        = 0x080A,
    ERR_FUNC_CTOR_ACCESS_DENIED                   = 0x080B,
    ERR_FUNC_CTOR_VISIBILITY_MISMATCH             = 0x080C,
    ERR_FUNC_INTERFACE_NOT_IMPLEMENTED            = 0x080D,
};

// ────────────────────────────────────────────────────────────────────────────
// Type resolution & adaptation diagnostics (conversions, new/delete, casts, arrays)
// ────────────────────────────────────────────────────────────────────────────
enum class type_diag : unsigned int {
    ERR_UNRESOLVED_TYPE_EXPR                      = 0x0901,
    ERR_DEREF_NOT_POINTER                         = 0x0902,
    ERR_DEREF_VOID_POINTER                        = 0x0903,
    ERR_ADDRESS_OF_NOT_REF                        = 0x0904,
    ERR_MEMBER_NOT_FOUND_ON_OBJECT                = 0x0905,
    ERR_MEMBER_NOT_FOUND_ON_TYPE                  = 0x0906,
    ERR_MEMBER_ACCESS_ON_RVALUE                   = 0x0907,
    ERR_POINTER_MEMBER_NOT_FOUND                  = 0x0908,
    ERR_SUBSCRIPT_NOT_ARRAY                       = 0x0909,
    ERR_SUBSCRIPT_INDEX_TYPE                      = 0x090A,
    ERR_INVOKE_NOT_CALLABLE                       = 0x090B,
    ERR_INVOKE_ARG_TYPE_MISMATCH                  = 0x090C,
    ERR_INVOKE_TOO_MANY_ARGS                      = 0x090D,
    ERR_INVOKE_TOO_FEW_ARGS                       = 0x090E,
    ERR_INVOKE_NO_MATCHING_OVERLOAD               = 0x090F,
    ERR_INVOKE_AMBIGUOUS_OVERLOAD                 = 0x0910,
    ERR_INVOKE_ASSIGN_RESULT                      = 0x0911,
    ERR_INVOKE_MEMBER_NO_MATCH                    = 0x0912,
    ERR_INVOKE_VISIBILITY_DENIED                  = 0x0913,
    ERR_INVOKE_CTOR_RESULT                        = 0x0914,
    ERR_INVOKE_METHOD_ARG_MISMATCH                = 0x0915,
    ERR_MEMBER_DEREF_NOT_POINTER                  = 0x0916,
    ERR_MEMBER_FUNC_NO_MATCH                      = 0x0917,
    ERR_CAST_INCOMPATIBLE                         = 0x0918,
    ERR_CAST_UNSUPPORTED                          = 0x0919,
    ERR_CAST_OPERATOR_NOT_FOUND                   = 0x091A,
    ERR_NEW_EXPECT_STRUCT_OR_PRIM                 = 0x091B,
    ERR_NEW_CTOR_ARG_MISMATCH                     = 0x091C,
    ERR_NEW_TYPE_NOT_FOUND                        = 0x091D,
    ERR_NEW_INIT_TYPE_MISMATCH                    = 0x091E,
    ERR_NEW_ABSTRACT_CLASS                        = 0x091F,
    ERR_NEW_ARRAY_BAD_INIT                        = 0x0920,
    ERR_DELETE_EXPECT_EXPR                        = 0x0921,
    ERR_DELETE_NOT_OWNER                          = 0x0922,
    ERR_DYNAMIC_CAST_BAD_TYPE                     = 0x0923,
    ERR_SIGNATURE_STRUCT_NOT_FOUND                = 0x0924,
    ERR_SIGNATURE_STRUCT_WRONG_KIND               = 0x0925,
    ERR_SIGNATURE_ENUM_BAD_UNDERLYING             = 0x0926,
    ERR_DESIG_INIT_NOT_STRUCT                     = 0x0927,
    WARN_DESIG_INIT_PARTIAL                       = 0x0928,
    ERR_DESIG_INIT_MEMBER_NOT_FOUND               = 0x0929,
    ERR_DESIG_INIT_TYPE_MISMATCH                  = 0x092A,
    ERR_DESIG_INIT_CTOR_MISMATCH                  = 0x092B,
    ERR_ARRAY_SIZE_NOT_INT                        = 0x092C,
    ERR_ARRAY_INIT_NO_MATCH                       = 0x092D,
    WARN_ARRAY_INIT_EXTRA                         = 0x092E,
    ERR_ARRAY_ELEM_INIT_MISMATCH                  = 0x092F,
    ERR_ARRAY_ELEM_ABSTRACT                       = 0x0930,
    ERR_ARRAY_ELEM_NO_CTOR                        = 0x0931,
    ERR_ARRAY_CTOR_NO_SINGLE_PARAM                = 0x0932,
    ERR_ARRAY_CTOR_PARAM_MISMATCH                 = 0x0933,
    ERR_ARRAY_SUBSCRIPT_BAD_TYPE                  = 0x0934,
    ERR_ARRAY_BRACE_INIT_DYNAMIC                  = 0x0935,
    ERR_ARRAY_ALLOC_NOT_POINTER                   = 0x0936,
    ERR_ARRAY_ALLOC_NOT_ARRAY                     = 0x0937,
    ERR_ARRAY_ALLOC_TYPE_MISMATCH                 = 0x0938,
    ERR_ARRAY_ALLOC_SIZE_NOT_INT                  = 0x0939,
    ERR_DESIG_STRUCT_NOT_FOUND                    = 0x093A,
    ERR_DESIG_STRUCT_TOO_FEW_ARGS                 = 0x093B,
    ERR_DESIG_STRUCT_FIELD_NOT_FOUND              = 0x093C,
    ERR_DESIG_STRUCT_DUPLICATE_FIELD              = 0x093D,
    ERR_DESIG_STRUCT_TOO_MANY_ARGS                = 0x093E,
    ERR_DESIG_STRUCT_EXTRA_POSITIONAL             = 0x093F,
    ERR_DESIG_STRUCT_FIELD_TYPE_MISMATCH          = 0x0940,
    ERR_DESIG_STRUCT_CTOR_ARG_MISMATCH            = 0x0941,
    ERR_DESIG_STRUCT_CTOR_NOT_FOUND               = 0x0942,
    ERR_DESIG_STRUCT_CTOR_AMBIGUOUS               = 0x0943,
    ERR_DESIG_STRUCT_NO_DEFAULT_CTOR              = 0x0944,
    ERR_CAST_NOT_SUPPORTED                        = 0x0945,
    WARN_CAST_SIGN_CHANGE                         = 0x0946,
    WARN_CAST_UNSIGNED_TO_SIGNED                  = 0x0947,
    // Foreach statement type-resolution errors (0x01F8-0x01FF)
    ERR_FOREACH_SOURCE_NOT_ITERABLE               = 0x0948,
    ERR_FOREACH_VAR_OWNER_FORBIDDEN               = 0x0949,
    ERR_FOREACH_VAR_DRAIN_FORBIDDEN               = 0x094A,
};

/**
 * Diagnostics for the exported aliasing declarations ('alias' and 'typedef').
 *
 * Codes 0x0210-0x022F.
 */
enum class alias_diag : unsigned int {
    /** The aliased symbol or type could not be resolved. */
    ERR_ALIAS_TARGET_NOT_FOUND                    = 0x0A01,
    /** The alias chain loops back onto itself. */
    ERR_ALIAS_CYCLE                               = 0x0A02,
    /** Another entity with the same name already exists in this scope. */
    ERR_ALIAS_DUPLICATE_NAME                      = 0x0A03,
    /** An alias declaration appears in a scope that cannot hold one. */
    ERR_ALIAS_BAD_SCOPE                           = 0x0A04,
    /** 'alias' cannot target a namespace; 'using N = namespace X;' must be used. */
    ERR_ALIAS_NAMESPACE_TARGET                    = 0x0A05,
    /** An explicit visibility was given to a block-local alias. */
    ERR_ALIAS_VISIBILITY_IN_BLOCK                 = 0x0A06,
    /** 'typedef' was given a target that is not a type. */
    ERR_TYPEDEF_TARGET_NOT_A_TYPE                 = 0x0A07,
    /** An untainted underlying-typed expression was assigned to a typedef. */
    ERR_TYPEDEF_REQUIRES_EXPLICIT_CAST            = 0x0A08,
    /** Two overloads differ only by a typedef, which never distinguishes a signature. */
    ERR_TYPEDEF_OVERLOAD_FORBIDDEN                = 0x0A09,
    /** A typedef-typed parameter or return was given a plain underlying-typed value. */
    WARN_TYPEDEF_BASE_TYPE_ARGUMENT               = 0x0A0A,
    /** A parameterised alias was declared with a value (non-type) template parameter. */
    ERR_ALIAS_TEMPLATE_VALUE_PARAM                = 0x0A0B,
    /** A parameterised alias was declared with a template parameter pack. */
    ERR_ALIAS_TEMPLATE_PACK_PARAM                 = 0x0A0C,
    /** A parameterised alias was used without template arguments, or with a wrong count. */
    ERR_ALIAS_TEMPLATE_ARG_MISMATCH               = 0x0A0D,
    /** Template arguments were given to an alias that is not parameterised. */
    ERR_ALIAS_NOT_A_TEMPLATE                      = 0x0A0E,
    /** A parameterised alias target could not be resolved once its arguments were substituted. */
    ERR_ALIAS_TEMPLATE_TARGET_UNRESOLVED          = 0x0A0F,
};


/**
 * Model-level diagnostics for callables (function references, functors, lambdas).
 * Range 0x0250 — 0x025F.
 */
enum class callable_model_diag : unsigned int {
    /** A non-null callable (`+` or `&`) was declared without an initialiser. */
    ERR_CALLABLE_NONNULL_UNINITIALIZED            = 0x0B01,
    /** The source callable may throw exceptions that the target callable does not declare. */
    ERR_CALLABLE_THROWS_NOT_SUBSET                = 0x0B02,
    /** A member function was bound to a callable through a null receiver. */
    ERR_CALLABLE_NULL_RECEIVER_BIND               = 0x0B03,
    /** An addresser suffix was applied to a typedef naming a bare callable prototype. */
    ERR_CALLABLE_TYPEDEF_PROTOTYPE_READDRESS      = 0x0B04,
    /** A borrowed callable cannot be converted to an owned callable (`!`). */
    ERR_CALLABLE_OWNER_FROM_BORROW                = 0x0B05,
    /** An owned callable cannot bind a member function on a local/parameter receiver. */
    ERR_CALLABLE_OWNED_RECEIVER_LOCAL             = 0x0B06,
    /** An owned lambda cannot capture local variables by reference. */
    ERR_LAMBDA_OWNED_CAPTURE_LOCAL_REF            = 0x0B07,
    /** Implicit capture by reference of a local variable is forbidden in an owned lambda. */
    ERR_LAMBDA_OWNED_IMPLICIT_CAPTURE_LOCAL       = 0x0B08,
};

} // namespace k::diag

#endif // KLANG_ERRORS_MODEL_HPP
