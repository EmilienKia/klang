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

#ifndef KLANG_ERRORS_LEX_PARSE_HPP
#define KLANG_ERRORS_LEX_PARSE_HPP

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
    // ── klangc CLI driver diagnostics (about the module being compiled) ────
    WARN_KDI_HEADER_READ_FAILED                   = 0x01E1,
    ERR_JIT_NO_MAIN                               = 0x01E2,
    ERR_JIT_INSTANTIATION_FAILED                  = 0x01E3,
    ERR_JIT_MAIN_SYMBOL_NOT_FOUND                 = 0x01E4,
    WARN_MAIN_IGNORED_DYN_LIB                     = 0x01E5,
    WARN_MAIN_IGNORED_STATIC_LIB                  = 0x01E6,
    WARN_MAIN_IGNORED_BOTH_LIBS                   = 0x01E7,
    WARN_OUTPUT_OPTION_IGNORED                    = 0x01E8,
    ERR_OBJECT_FILES_INCOMPATIBLE_MODE            = 0x01E9,
    // ── Linker / library / KDI driver diagnostics ───────────────────────────
    ERR_NO_MAIN_FOR_EXECUTABLE                    = 0x01EA,
    ERR_TOOL_NOT_FOUND_LINK_EXECUTABLE            = 0x01EB,
    ERR_TOOL_NOT_FOUND_LINK_SHARED_LIB            = 0x01EC,
    ERR_TOOL_NOT_FOUND_CREATE_STATIC_LIB          = 0x01ED,
    ERR_GEN_KDI_NO_UNIT                           = 0x01EE,
    ERR_GEN_KDI_WRITE_FAILED                      = 0x01EF,
    ERR_GEN_KDI_JSON_WRITE_FAILED                 = 0x01F0,
    ERR_GDWARF_OPTIONS_MUTUALLY_EXCLUSIVE          = 0x01F1,
    WARN_UNKNOWN_LOG_LEVEL                        = 0x01F2,
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
    WARN_INVALID_UTF8_COMMENT                     = 0x01E0,
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
    ERR_BREAK_MISSING_SEMICOLON                   = 0x0190,
    ERR_CONTINUE_MISSING_SEMICOLON                = 0x0191,
    ERR_ENUM_MISSING_OPEN_BRACE                   = 0x0192,
    ERR_ENUM_ENTRY_EXPECT_NAME                    = 0x0193,
    // Generic declaration errors (0x01A0–0x01AF)
    ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED           = 0x01A0,
    // Varargs parameter errors (0x01B0–0x01BF)
    ERR_VARARGS_NOT_LAST                          = 0x01B0,
    ERR_VARARGS_WITH_DEFAULT                      = 0x01B1,
    ERR_MULTIPLE_VARARGS                          = 0x01B2,
    // Warnings (0x01C0–0x01CF)
    WARN_SPURIOUS_FUN_PREFIX                      = 0x01C0,
    WARN_SPURIOUS_SEMICOLON                       = 0x01C1,
    // Exception-related parser errors (0x01D0–0x01DF)
    ERR_THROW_EXPECT_EXPRESSION                   = 0x01D0,
    ERR_THROW_MISSING_SEMICOLON                   = 0x01D1,
    ERR_TRY_EXPECT_BODY                           = 0x01D2,
    ERR_TRY_EXPECT_CATCH                          = 0x01D3,
    ERR_CATCH_EXPECT_OPEN_PAREN                   = 0x01D4,
    ERR_CATCH_EXPECT_IDENTIFIER                   = 0x01D5,
    ERR_CATCH_EXPECT_COLON                        = 0x01D6,
    ERR_CATCH_EXPECT_TYPE                         = 0x01D7,
    ERR_CATCH_EXPECT_CLOSE_PAREN                  = 0x01D8,
    ERR_CATCH_EXPECT_BODY                         = 0x01D9,
    ERR_THROWS_EXPECT_TYPE                        = 0x01DA,
    ERR_TRY_EXPECT_FINALLY_BODY                   = 0x01DB,
    ERR_SPACESHIP_EXPECT_SUBEXPR                  = 0x01DC,
};

} // namespace k::diag

#endif // KLANG_ERRORS_LEX_PARSE_HPP
