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

#ifndef KLANG_ERRORS_HPP
#define KLANG_ERRORS_HPP

/**
 * @file errors.hpp
 * @brief Centralized diagnostic code definitions for the K compiler.
 *
 * Each enum groups diagnostics by semantic category (independent of severity
 * and compilation phase). Entries follow the naming convention:
 *
 *   [INTERNAL_]SEVERITY_SHORT_DESCRIPTION
 *
 * Where:
 *   - INTERNAL_ is present only for compiler-internal errors (bugs)
 *   - SEVERITY is ERR_, WARN_, or INFO_
 *   - SHORT_DESCRIPTION is a concise, unique, human-readable identifier
 *
 * User-facing codes:  0x0001 - 0x0FFF
 * Internal codes:     0xF001 - 0xFFFF
 */

namespace k::diag {


// ────────────────────────────────────────────────────────────────────────────
// Compiler driver diagnostics (module resolution, imports, KDI loading)
// ────────────────────────────────────────────────────────────────────────────
enum class compiler_diag : unsigned int {
    ERR_CONFLICTING_MODULE_DECL                   = 0x0001,
    WARN_NO_MODULE_DECL                           = 0x0002,
    ERR_NS_ROOT_COLLISION                         = 0x0003,
    ERR_NS_COLLISION_ENFORCED                     = 0x0004,
    ERR_CIRCULAR_IMPORT                           = 0x0005,
    ERR_KDI_NOT_FOUND                             = 0x0006,
    ERR_KDI_PARSE_FAILED                          = 0x0007,
    WARN_UNUSED_IMPORT                            = 0x0008,
};

// ────────────────────────────────────────────────────────────────────────────
// Lexer diagnostics (tokens, literals, escape sequences, suffixes)
// ────────────────────────────────────────────────────────────────────────────
enum class lexer_diag : unsigned int {
    WARN_HEX_MISSING_DIGIT_UNSIGNED               = 0x0009,
    WARN_HEX_MISSING_DIGIT_SIZE                   = 0x000A,
    WARN_BIN_MISSING_DIGIT_SUFFIX                 = 0x000B,
    WARN_OCT_MISSING_DIGIT_SUFFIX                 = 0x000C,
    ERR_BAD_ESCAPE_SEQ                            = 0x000D,
    WARN_INCOMPLETE_HEX_ESCAPE                    = 0x000E,
    WARN_INCOMPLETE_UNICODE_ESCAPE                = 0x000F,
    WARN_INCOMPLETE_LONG_UNICODE                  = 0x0010,
    WARN_BAD_INT_SUFFIX_4                         = 0x0011,
    WARN_BAD_INT_SUFFIX_2                         = 0x0012,
    WARN_BAD_INT_SUFFIX_8                         = 0x0013,
    WARN_BAD_BIGINT_SUFFIX                        = 0x0014,
};

// ────────────────────────────────────────────────────────────────────────────
// Parser diagnostics (syntax errors, missing delimiters, malformed declarations)
// ────────────────────────────────────────────────────────────────────────────
enum class parser_diag : unsigned int {
    ERR_MISSING_MODULE_NAME                       = 0x0015,
    ERR_MISSING_SEMICOLON_MODULE                  = 0x0016,
    ERR_MISSING_IMPORT_NAME                       = 0x0017,
    ERR_MISSING_SEMICOLON_IMPORT                  = 0x0018,
    ERR_MISSING_NS_OPEN_BRACE                     = 0x0019,
    ERR_MISSING_NS_CLOSE_BRACE                    = 0x001A,
    ERR_QNAME_AFTER_ROOT_SEP                      = 0x001B,
    ERR_QNAME_AFTER_INTERMEDIATE_SEP              = 0x001C,
    ERR_FUNC_EXPECT_FINALIZE                      = 0x001D,
    ERR_FUNC_EXPECT_FIRST_PARAM                   = 0x001E,
    ERR_FUNC_EXPECT_FINALIZE_2                    = 0x001F,
    ERR_FUNC_EXPECT_CLOSE_OR_COMMA                = 0x0020,
    ERR_FUNC_EXPECT_PARAM_SPEC                    = 0x0021,
    ERR_FUNC_EXPECT_BODY_BLOCK                    = 0x0022,
    ERR_BLOCK_MISSING_CLOSE_BRACE                 = 0x0023,
    ERR_RETURN_MISSING_SEMICOLON                  = 0x0024,
    ERR_VARDECL_EXPECT_TYPE                       = 0x0025,
    ERR_VARDECL_EXPECT_INIT_EXPR                  = 0x0026,
    ERR_VARDECL_MISSING_SEMICOLON                 = 0x0027,
    ERR_EXPRSTMT_MISSING_SEMICOLON                = 0x0028,
    ERR_EXPRLIST_EXPECT_SUBEXPR                   = 0x0029,
    ERR_ASSIGN_EXPECT_SUBEXPR                     = 0x002A,
    ERR_COND_EXPECT_THEN_EXPR                     = 0x002B,
    ERR_COND_EXPECT_COLON                         = 0x002C,
    ERR_COND_EXPECT_ELSE_EXPR                     = 0x002D,
    ERR_LOGOR_EXPECT_SUBEXPR                      = 0x002E,
    ERR_LOGAND_EXPECT_SUBEXPR                     = 0x002F,
    ERR_BITOR_EXPECT_SUBEXPR                      = 0x0030,
    ERR_BITXOR_EXPECT_SUBEXPR                     = 0x0031,
    ERR_BITAND_EXPECT_SUBEXPR                     = 0x0032,
    ERR_EQUALITY_EXPECT_SUBEXPR                   = 0x0033,
    ERR_RELATIONAL_EXPECT_SUBEXPR                 = 0x0034,
    ERR_SHIFT_EXPECT_SUBEXPR                      = 0x0035,
    ERR_ADDITIVE_EXPECT_SUBEXPR                   = 0x0036,
    ERR_MULTIPLICATIVE_EXPECT_SUBEXPR             = 0x0037,
    ERR_CAST_EXPECT_SUBEXPR                       = 0x0038,
    ERR_UNARY_EXPECT_SUBEXPR                      = 0x0039,
    ERR_BRACKET_EXPECT_SUBEXPR                    = 0x003A,
    ERR_BRACKET_EXPECT_CLOSE                      = 0x003B,
    ERR_PAREN_POSTFIX_EXPECT_CLOSE                = 0x003C,
    ERR_PAREN_EXPECT_SUBEXPR                      = 0x003D,
    ERR_PAREN_EXPECT_CLOSE                        = 0x003E,
    ERR_IF_EXPECT_OPEN_PAREN                      = 0x003F,
    ERR_IF_EXPECT_CONDITION                       = 0x0040,
    ERR_IF_EXPECT_CLOSE_PAREN                     = 0x0041,
    ERR_IF_EXPECT_BODY                            = 0x0042,
    ERR_IF_EXPECT_ELSE_BODY                       = 0x0043,
    ERR_WHILE_EXPECT_OPEN_PAREN                   = 0x0044,
    ERR_WHILE_EXPECT_CONDITION                    = 0x0045,
    ERR_WHILE_EXPECT_CLOSE_PAREN                  = 0x0046,
    ERR_WHILE_EXPECT_BODY                         = 0x0047,
    ERR_FOR_EXPECT_OPEN_PAREN                     = 0x0048,
    ERR_FOR_EXPECT_INIT_OR_SEMICOLON              = 0x0049,
    ERR_FOR_EXPECT_COND_OR_SEMICOLON              = 0x004A,
    ERR_FOR_EXPECT_CLOSE_PAREN                    = 0x004B,
    ERR_FOR_EXPECT_BODY                           = 0x004C,
    ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET           = 0x004D,
    ERR_STRUCT_MISSING_OPEN_BRACE                 = 0x004E,
    ERR_STRUCT_MISSING_CLOSE_BRACE                = 0x004F,
    ERR_DTOR_MUST_HAVE_NO_PARAMS                  = 0x0050,
    ERR_DTOR_MUST_HAVE_NO_RETURN                  = 0x0051,
    ERR_EXPECTED_BASE_CLASS_NAME                  = 0x0052,
    ERR_NO_DEFAULT_AFTER_NON_DEFAULT              = 0x0053,
    ERR_MEMINIT_EXPECT_OPEN_PAREN                 = 0x0054,
    ERR_MEMINIT_EXPECT_EXPR_OR_CLOSE              = 0x0055,
    ERR_MEMINIT_EXPECT_COMMA_OR_CLOSE             = 0x0056,
    ERR_MEMINIT_EXPECT_EXPR_AFTER_COMMA           = 0x0057,
    ERR_ALIAS_EXPECT_SEMICOLON                    = 0x0058,
    ERR_ALIAS_EXPECT_BODY_DEFAULT_DELETE          = 0x0059,
    ERR_ALIAS_INVALID_KEYWORD                     = 0x005A,
    ERR_REDIRECT_EXPECT_TARGET                    = 0x005B,
    ERR_REDIRECT_EXPECT_TYPE_OR_CLOSE             = 0x005C,
    ERR_REDIRECT_EXPECT_COMMA_OR_CLOSE            = 0x005D,
    ERR_REDIRECT_EXPECT_SEMICOLON                 = 0x005E,
    ERR_EXPECTED_OPERATOR_SYMBOL                  = 0x005F,
    ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE         = 0x0060,
    ERR_OPERATOR_PREDEC_EXPECT_UNDERSCORE         = 0x0061,
    ERR_UNSUPPORTED_OPERATOR_SYMBOL               = 0x0062,
    ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC           = 0x0063,
    ERR_INVALID_OPERATOR_AFTER_KEYWORD            = 0x0064,
    ERR_CAST_OPERATOR_EMPTY_PARAMS                = 0x0065,
    ERR_CAST_OPERATOR_EXPECT_RETURN_TYPE          = 0x0066,
    ERR_REDIRECT_ABSTRACT_INVALID                 = 0x0067,
    ERR_USING_EXPECT_QNAME                        = 0x0068,
    ERR_USING_MISSING_SEMICOLON                   = 0x0069,
    ERR_ENUM_ENTRY_EXPECT_VALUE                   = 0x006A,
    ERR_ENUM_ENTRY_MISSING_SEMICOLON              = 0x006B,
    ERR_ENUM_MISSING_CLOSE_BRACE                  = 0x006C,
    ERR_ENUM_MISSING_SEMICOLON                    = 0x006D,
    ERR_DESIGNATED_EXPECT_EQ_OR_PAREN             = 0x006E,
    ERR_MIXED_POSITIONAL_DESIGNATED               = 0x006F,
    ERR_DESIGNATED_CTOR_EXPECT_COMMA_CLOSE        = 0x0070,
    ERR_FRIEND_EXPECT_QNAME                       = 0x0071,
    ERR_FRIEND_MISSING_SEMICOLON                  = 0x0072,
    ERR_NAMED_RET_EXPECT_TYPE                     = 0x0073,
    ERR_NAMED_RET_EXPECT_INIT_EXPR                = 0x0074,
    ERR_NAMED_RET_CTOR_EXPECT_EXPR_CLOSE          = 0x0075,
    ERR_EXPECTED_BASE_ENUM_NAME                   = 0x0076,
    ERR_NAMED_RET_CTOR_EXPECT_EXPR_COMMA          = 0x0077,
    ERR_NAMED_RET_NO_ALIAS                        = 0x0078,
    ERR_ANNOTATION_EXPECT_NAME                    = 0x0079,
    ERR_ANNOTATION_EXPECT_CLOSE_PAREN             = 0x007A,
    ERR_ANNOTATION_EXPECT_NAME_EXPR               = 0x007B,
    ERR_BRACE_INIT_NESTED_ERROR                   = 0x007C,
    ERR_BRACE_INIT_SEP_ERROR                      = 0x007D,
};

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

} // namespace k::diag

#endif // KLANG_ERRORS_HPP
