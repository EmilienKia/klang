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
    ERR_VISIBILITY_BAD_SCOPE                      = 0x007E,
    ERR_VISIBILITY_INVALID_KEYWORD                = 0x007F,
    ERR_STRUCT_BAD_SCOPE                          = 0x0080,
    ERR_VAR_BAD_SCOPE                             = 0x0081,
    ERR_FUNC_BAD_SCOPE                            = 0x0082,
    ERR_CTOR_HAS_RETURN_TYPE                      = 0x0083,
    ERR_DTOR_HAS_RETURN_TYPE                      = 0x0084,
    ERR_DTOR_HAS_PARAMS                           = 0x0085,
    ERR_FUNC_OPERATOR_BAD_SCOPE                   = 0x0086,
    ERR_FUNC_BLOCK_UNEXPECTED                     = 0x0087,
    ERR_FUNC_STATIC_CTOR_BAD_SCOPE                = 0x0088,
    ERR_IF_STMT_BAD_SCOPE                         = 0x0089,
    ERR_IF_STMT_NEEDS_CONDITION                   = 0x008A,
    ERR_IF_STMT_NEEDS_BODY                        = 0x008B,
    ERR_ELSE_CLAUSE_BAD_BODY                      = 0x008C,
    ERR_WHILE_STMT_BAD_SCOPE                      = 0x008D,
    ERR_WHILE_STMT_NEEDS_CONDITION                = 0x008E,
    ERR_WHILE_STMT_NEEDS_BODY                     = 0x008F,
    ERR_FOR_STMT_BAD_SCOPE                        = 0x0090,
    ERR_FOR_STMT_BAD_CONDITION                    = 0x0091,
    ERR_FOR_STMT_BAD_STEP                         = 0x0092,
    ERR_FOR_STMT_NEEDS_BODY                       = 0x0093,
    ERR_EXPR_STMT_BAD_SCOPE                       = 0x0094,
    ERR_UNSUPPORTED_BINARY_OP                     = 0x0095,
    ERR_UNSUPPORTED_UNARY_PREFIX_OP               = 0x0096,
    ERR_MEMBER_ACCESS_NOT_IDENTIFIER              = 0x0097,
    ERR_UNSUPPORTED_MEMBER_ACCESS_OP              = 0x0098,
    ERR_UNSUPPORTED_POSTFIX_OP                    = 0x0099,
    ERR_STATIC_DTOR_HAS_RETURN_TYPE               = 0x009A,
    ERR_STATIC_CTOR_HAS_PARAMS                    = 0x009B,
    ERR_STATIC_DTOR_HAS_PARAMS                    = 0x009C,
    ERR_RETURN_VAR_TYPE_MISMATCH                  = 0x009D,
    ERR_FUNC_VIRTUAL_ON_STRUCT                    = 0x009E,
    ERR_FUNC_DUPLICATE_DEFINITION                 = 0x009F,
    ERR_ABSTRACT_ON_STRUCT                        = 0x00A0,
    ERR_ABSTRACT_BAD_DECL_SCOPE                   = 0x00A1,
    ERR_ABSTRACT_ON_STATIC                        = 0x00A2,
    ERR_ABSTRACT_ON_FINAL                         = 0x00A3,
    ERR_ABSTRACT_WITH_BODY                        = 0x00A4,
    ERR_ABSTRACT_ON_PRIVATE                       = 0x00A5,
    ERR_FUNC_ABSTRACT_BAD_SCOPE                   = 0x00A6,
    WARN_ABSTRACT_REDUNDANT_ON_IFACE              = 0x00A7,
    WARN_ABSTRACT_REDUNDANT_ON_IFACE_METHOD       = 0x00A8,
    WARN_IFACE_NON_VIRTUAL_FUNC                   = 0x00A9,
    ERR_DEFAULT_PARAM_IN_BODY                     = 0x00AA,
    ERR_FUNC_NO_IMPL_NO_ABSTRACT                  = 0x00AB,
    ERR_USING_FILTER_INVALID                      = 0x00AC,
    ERR_USING_BAD_SCOPE                           = 0x00AD,
    ERR_BRACE_INIT_INTERNAL                       = 0x00AE,
    ERR_ENUM_BAD_SCOPE                            = 0x00AF,
    ERR_ENUM_DUPLICATE_DEFAULT                    = 0x00B0,
    ERR_ENUM_ENTRY_VALUE_NOT_INT                  = 0x00B1,
    ERR_RETURN_VAR_NAME_MISMATCH                  = 0x00B2,
    ERR_RETURN_VAR_NOT_IN_FUNC                    = 0x00B3,
    ERR_RETURN_VAR_TYPE_NOT_REF                   = 0x00B4,
    ERR_OVERRIDE_ON_STATIC                        = 0x0178,
    ERR_OVERRIDE_ON_ABSTRACT                      = 0x0179,
    ERR_OVERRIDE_ON_CTOR_DTOR                     = 0x017A,
    ERR_OVERRIDE_ON_STRUCT                        = 0x017B,
    ERR_BREAK_NOT_IN_LOOP                         = 0x017C,
    ERR_CONTINUE_NOT_IN_LOOP                      = 0x017D,
};

// ────────────────────────────────────────────────────────────────────────────
// Symbol resolution diagnostics (name lookup, visibility, redirections, overloads)
// ────────────────────────────────────────────────────────────────────────────
enum class symbol_diag : unsigned int {
    ERR_SYMBOL_NOT_FOUND                          = 0x00B5,
    ERR_UNRESOLVED_IDENTIFIER                     = 0x00B6,
    ERR_STATIC_CTOR_INIT_FAILED                   = 0x00B7,
    ERR_VISIBILITY_ACCESS_DENIED                  = 0x00B8,
    ERR_AGGREGATE_VISIBILITY_DENIED               = 0x00B9,
    WARN_UNUSED_PRIVATE_CTOR                      = 0x00C3,
    ERR_FUNC_RETURN_UNRESOLVED                    = 0x00C4,
    ERR_REDIRECT_CHAIN_CYCLE                      = 0x00C5,
    ERR_REDIRECT_TARGET_NOT_FOUND                 = 0x00C6,
    ERR_REDIRECT_AMBIGUOUS                        = 0x00C7,
    ERR_REDIRECT_SELF_REF                         = 0x00C8,
    ERR_REDIRECT_INCOMPATIBLE_SIG                 = 0x00C9,
    ERR_DUPLICATE_BASE_CLASS                      = 0x00D7,
    ERR_INIT_ORDER_CYCLE                          = 0x00D8,
    ERR_OVERLOAD_AMBIGUOUS                        = 0x00D9,
    ERR_OVERLOAD_NO_MATCH                         = 0x00DA,
    ERR_FUNC_VISIBILITY_DENIED                    = 0x00DB,
    ERR_BINARY_OVERLOAD_NOT_FOUND                 = 0x00DC,
    ERR_UNARY_OVERLOAD_NOT_FOUND                  = 0x00DD,
};

// ────────────────────────────────────────────────────────────────────────────
// Aggregate type diagnostics (inheritance, enums, annotations, virtuality)
// ────────────────────────────────────────────────────────────────────────────
enum class structure_diag : unsigned int {
    ERR_BASE_NOT_FOUND                            = 0x00BA,
    ERR_STRUCT_SELF_INHERIT                       = 0x00BB,
    ERR_BASE_IS_FINAL                             = 0x00BC,
    ERR_CONST_STRUCT_MUTABLE_BASE                 = 0x00BD,
    ERR_CROSS_STRUCT_CLASS                        = 0x00BE,
    ERR_PRIVATE_OVERRIDE                          = 0x00BF,
    ERR_ANNOTATION_MISSING_TARGET                 = 0x00C0,
    ERR_ANNOTATION_BAD_TYPE                       = 0x00C1,
    ERR_ANNOTATION_TARGET_MISMATCH                = 0x00C2,
    ERR_ENUM_UNDERLYING_NOT_INT                   = 0x00CA,
    ERR_ENUM_BASE_NOT_ENUM                        = 0x00CB,
    ERR_ENUM_ENTRY_AMBIGUOUS                      = 0x00D4,
    ERR_ENUM_ENTRY_NOT_FOUND                      = 0x00D5,
    WARN_ENUM_ENTRY_SHADOW                        = 0x00D6,
    WARN_CONST_STRUCT_NON_CONST_BASE              = 0x012D,
    WARN_CONST_STRUCT_NON_CONST_MEMBER            = 0x012E,
    ERR_ABSTRACT_METHOD_IN_NON_ABSTRACT           = 0x0173,
    ERR_INHERITED_ABSTRACT_NOT_IMPL               = 0x0174,
    WARN_OVERRIDE_FINAL                           = 0x0175,
    WARN_MISSING_OVERRIDE                         = 0x0176,
    ERR_OVERRIDE_NOT_OVERRIDING                   = 0x0177,
    ERR_STRUCT_RECURSIVE_FORBIDDEN                = 0x017E,
};

// ────────────────────────────────────────────────────────────────────────────
// Union type diagnostics (alternatives, construction, member access)
// ────────────────────────────────────────────────────────────────────────────
enum class union_diag : unsigned int {
    ERR_UNION_DRAIN_ADDRESSER                     = 0x0180,  ///< Drain '#' on union alternative
    ERR_UNION_MEMBER_NOT_FOUND                    = 0x0181,  ///< Unknown alternative name
    ERR_UNION_INVALID_MEMBER                      = 0x0182,  ///< Non-variable decl inside union body
    ERR_UNION_TYPE_MISMATCH                       = 0x0183,  ///< Wrong alternative access (compile-time)
    ERR_UNION_AMBIGUOUS_IMPLICIT_ASSIGN           = 0x0184,  ///< Multiple alternatives match RHS type
    ERR_UNION_NO_DEFAULT_CTOR                     = 0x0185,  ///< First alternative not default-constructible
};

// ────────────────────────────────────────────────────────────────────────────
// Function & parameter diagnostics (signatures, return types, abstract, access)
// ────────────────────────────────────────────────────────────────────────────
enum class function_diag : unsigned int {
    ERR_FUNC_ANNOTATION_MISMATCH                  = 0x00CC,
    WARN_FUNC_BODY_IGNORED                        = 0x00CD,
    ERR_FUNC_ABSTRACT_HAS_BODY                    = 0x00CE,
    ERR_PARAM_TYPE_UNRESOLVED                     = 0x00CF,
    WARN_PARAM_DRAIN_NON_STRUCT                   = 0x00D0,
    ERR_PARAM_DRAIN_MUST_BE_LAST                  = 0x00D1,
    ERR_PARAM_DEFAULT_TYPE_MISMATCH               = 0x00D2,
    WARN_PARAM_DEFAULT_NARROWING                  = 0x00D3,
    ERR_PARAM_VOID_NOT_ALLOWED                    = 0x00DE,
    ERR_FUNC_VISIBILITY_MISMATCH                  = 0x00E5,
    ERR_FUNC_ACCESS_DENIED                        = 0x0103,
    ERR_FUNC_CTOR_ACCESS_DENIED                   = 0x0104,
    ERR_FUNC_CTOR_VISIBILITY_MISMATCH             = 0x0105,
    ERR_FUNC_INTERFACE_NOT_IMPLEMENTED            = 0x0166,
};

// ────────────────────────────────────────────────────────────────────────────
// Type resolution & adaptation diagnostics (conversions, new/delete, casts, arrays)
// ────────────────────────────────────────────────────────────────────────────
enum class type_diag : unsigned int {
    ERR_UNRESOLVED_TYPE_EXPR                      = 0x00EE,
    ERR_DEREF_NOT_POINTER                         = 0x00EF,
    ERR_DEREF_VOID_POINTER                        = 0x00F0,
    ERR_ADDRESS_OF_NOT_REF                        = 0x00F1,
    ERR_MEMBER_NOT_FOUND_ON_OBJECT                = 0x00F2,
    ERR_MEMBER_NOT_FOUND_ON_TYPE                  = 0x00F3,
    ERR_MEMBER_ACCESS_ON_RVALUE                   = 0x00F4,
    ERR_POINTER_MEMBER_NOT_FOUND                  = 0x00F5,
    ERR_SUBSCRIPT_NOT_ARRAY                       = 0x00F6,
    ERR_SUBSCRIPT_INDEX_TYPE                      = 0x00F7,
    ERR_INVOKE_NOT_CALLABLE                       = 0x00F8,
    ERR_INVOKE_ARG_TYPE_MISMATCH                  = 0x00F9,
    ERR_INVOKE_TOO_MANY_ARGS                      = 0x00FA,
    ERR_INVOKE_TOO_FEW_ARGS                       = 0x00FB,
    ERR_INVOKE_NO_MATCHING_OVERLOAD               = 0x00FC,
    ERR_INVOKE_AMBIGUOUS_OVERLOAD                 = 0x00FD,
    ERR_INVOKE_ASSIGN_RESULT                      = 0x00FE,
    ERR_INVOKE_MEMBER_NO_MATCH                    = 0x00FF,
    ERR_INVOKE_VISIBILITY_DENIED                  = 0x0100,
    ERR_INVOKE_CTOR_RESULT                        = 0x0101,
    ERR_INVOKE_METHOD_ARG_MISMATCH                = 0x0102,
    ERR_MEMBER_DEREF_NOT_POINTER                  = 0x0106,
    ERR_MEMBER_FUNC_NO_MATCH                      = 0x0107,
    ERR_CAST_INCOMPATIBLE                         = 0x0108,
    ERR_CAST_UNSUPPORTED                          = 0x0109,
    ERR_CAST_OPERATOR_NOT_FOUND                   = 0x010A,
    ERR_NEW_EXPECT_STRUCT_OR_PRIM                 = 0x0110,
    ERR_NEW_CTOR_ARG_MISMATCH                     = 0x0111,
    ERR_NEW_TYPE_NOT_FOUND                        = 0x0112,
    ERR_NEW_INIT_TYPE_MISMATCH                    = 0x0113,
    ERR_NEW_ABSTRACT_CLASS                        = 0x0114,
    ERR_NEW_ARRAY_BAD_INIT                        = 0x0115,
    ERR_DELETE_EXPECT_EXPR                        = 0x0116,
    ERR_DELETE_NOT_OWNER                          = 0x0117,
    ERR_DYNAMIC_CAST_BAD_TYPE                     = 0x0129,
    ERR_SIGNATURE_STRUCT_NOT_FOUND                = 0x0133,
    ERR_SIGNATURE_STRUCT_WRONG_KIND               = 0x0134,
    ERR_SIGNATURE_ENUM_BAD_UNDERLYING             = 0x0135,
    ERR_DESIG_INIT_NOT_STRUCT                     = 0x013C,
    WARN_DESIG_INIT_PARTIAL                       = 0x013D,
    ERR_DESIG_INIT_MEMBER_NOT_FOUND               = 0x013E,
    ERR_DESIG_INIT_TYPE_MISMATCH                  = 0x013F,
    ERR_DESIG_INIT_CTOR_MISMATCH                  = 0x0140,
    ERR_ARRAY_SIZE_NOT_INT                        = 0x0141,
    ERR_ARRAY_INIT_NO_MATCH                       = 0x0142,
    WARN_ARRAY_INIT_EXTRA                         = 0x0143,
    ERR_ARRAY_ELEM_INIT_MISMATCH                  = 0x0144,
    ERR_ARRAY_ELEM_ABSTRACT                       = 0x0145,
    ERR_ARRAY_ELEM_NO_CTOR                        = 0x0146,
    ERR_ARRAY_CTOR_NO_SINGLE_PARAM                = 0x0147,
    ERR_ARRAY_CTOR_PARAM_MISMATCH                 = 0x0148,
    ERR_ARRAY_SUBSCRIPT_BAD_TYPE                  = 0x0149,
    ERR_ARRAY_BRACE_INIT_DYNAMIC                  = 0x014A,
    ERR_ARRAY_ALLOC_NOT_POINTER                   = 0x014B,
    ERR_ARRAY_ALLOC_NOT_ARRAY                     = 0x014C,
    ERR_ARRAY_ALLOC_TYPE_MISMATCH                 = 0x014D,
    ERR_ARRAY_ALLOC_SIZE_NOT_INT                  = 0x014E,
    ERR_DESIG_STRUCT_NOT_FOUND                    = 0x014F,
    ERR_DESIG_STRUCT_TOO_FEW_ARGS                 = 0x0150,
    ERR_DESIG_STRUCT_FIELD_NOT_FOUND              = 0x0151,
    ERR_DESIG_STRUCT_DUPLICATE_FIELD              = 0x0152,
    ERR_DESIG_STRUCT_TOO_MANY_ARGS                = 0x0153,
    ERR_DESIG_STRUCT_EXTRA_POSITIONAL             = 0x0154,
    ERR_DESIG_STRUCT_FIELD_TYPE_MISMATCH          = 0x0155,
    ERR_DESIG_STRUCT_CTOR_ARG_MISMATCH            = 0x0156,
    ERR_DESIG_STRUCT_CTOR_NOT_FOUND               = 0x0157,
    ERR_DESIG_STRUCT_CTOR_AMBIGUOUS               = 0x0158,
    ERR_DESIG_STRUCT_NO_DEFAULT_CTOR              = 0x0159,
    ERR_CAST_NOT_SUPPORTED                        = 0x016E,
    WARN_CAST_SIGN_CHANGE                         = 0x0170,
};

} // namespace k::diag

#endif // KLANG_ERRORS_MODEL_HPP
