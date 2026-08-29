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
 * User-facing codes:  0x0101 - 0x15FF  (high byte = group ID, low byte = consecutive code in group)
 * Internal codes:     0xF001 - 0xF0FF  (high byte = 0xF0, low byte = error code)
 */

namespace k::diag {


// ────────────────────────────────────────────────────────────────────────────
// Compiler driver diagnostics (module resolution, imports, KDI loading)
// ────────────────────────────────────────────────────────────────────────────
enum class compiler_diag : unsigned int {
    ERR_CONFLICTING_MODULE_DECL                   = 0x0101,
    WARN_NO_MODULE_DECL                           = 0x0102,
    ERR_NS_ROOT_COLLISION                         = 0x0103,
    ERR_NS_COLLISION_ENFORCED                     = 0x0104,
    ERR_CIRCULAR_IMPORT                           = 0x0105,
    ERR_KDI_NOT_FOUND                             = 0x0106,
    ERR_KDI_PARSE_FAILED                          = 0x0107,
    WARN_UNUSED_IMPORT                            = 0x0108,
    // ── klangc CLI driver diagnostics (about the module being compiled) ────
    WARN_KDI_HEADER_READ_FAILED                   = 0x0109,
    ERR_JIT_NO_MAIN                               = 0x010A,
    ERR_JIT_INSTANTIATION_FAILED                  = 0x010B,
    ERR_JIT_MAIN_SYMBOL_NOT_FOUND                 = 0x010C,
    WARN_MAIN_IGNORED_DYN_LIB                     = 0x010D,
    WARN_MAIN_IGNORED_STATIC_LIB                  = 0x010E,
    WARN_MAIN_IGNORED_BOTH_LIBS                   = 0x010F,
    WARN_OUTPUT_OPTION_IGNORED                    = 0x0110,
    ERR_OBJECT_FILES_INCOMPATIBLE_MODE            = 0x0111,
    // ── Linker / library / KDI driver diagnostics ───────────────────────────
    ERR_NO_MAIN_FOR_EXECUTABLE                    = 0x0112,
    ERR_TOOL_NOT_FOUND_LINK_EXECUTABLE            = 0x0113,
    ERR_TOOL_NOT_FOUND_LINK_SHARED_LIB            = 0x0114,
    ERR_TOOL_NOT_FOUND_CREATE_STATIC_LIB          = 0x0115,
    ERR_GEN_KDI_NO_UNIT                           = 0x0116,
    ERR_GEN_KDI_WRITE_FAILED                      = 0x0117,
    ERR_GEN_KDI_JSON_WRITE_FAILED                 = 0x0118,
    ERR_GDWARF_OPTIONS_MUTUALLY_EXCLUSIVE         = 0x0119,
    WARN_UNKNOWN_LOG_LEVEL                        = 0x011A,
    WARN_UNKNOWN_DIAGNOSTIC_CODE                  = 0x011B,

    /**
     * Re-parsing the source text of an imported template definition (carried
     * verbatim inside the KDI) failed — either parsing/model-building threw,
     * or produced no usable declaration. The template becomes unavailable
     * for cross-module instantiation; this used to fail completely silently.
     */
    ERR_KDI_TEMPLATE_REPARSE_FAILED               = 0x011C,
};

// ────────────────────────────────────────────────────────────────────────────
// Lexer diagnostics (tokens, literals, escape sequences, suffixes)
// ────────────────────────────────────────────────────────────────────────────
enum class lexer_diag : unsigned int {
    WARN_HEX_MISSING_DIGIT_UNSIGNED               = 0x0201,
    WARN_HEX_MISSING_DIGIT_SIZE                   = 0x0202,
    WARN_BIN_MISSING_DIGIT_SUFFIX                 = 0x0203,
    WARN_OCT_MISSING_DIGIT_SUFFIX                 = 0x0204,
    ERR_BAD_ESCAPE_SEQ                            = 0x0205,
    WARN_INCOMPLETE_HEX_ESCAPE                    = 0x0206,
    WARN_INCOMPLETE_UNICODE_ESCAPE                = 0x0207,
    WARN_INCOMPLETE_LONG_UNICODE                  = 0x0208,
    WARN_BAD_INT_SUFFIX_4                         = 0x0209,
    WARN_BAD_INT_SUFFIX_2                         = 0x020A,
    WARN_BAD_INT_SUFFIX_8                         = 0x020B,
    WARN_BAD_BIGINT_SUFFIX                        = 0x020C,
    WARN_INVALID_UTF8_COMMENT                     = 0x020D,
};

// ────────────────────────────────────────────────────────────────────────────
// Parser diagnostics (syntax errors, missing delimiters, malformed declarations)
// ────────────────────────────────────────────────────────────────────────────
enum class parser_diag : unsigned int {
    ERR_MISSING_MODULE_NAME                       = 0x0301,
    ERR_MISSING_SEMICOLON_MODULE                  = 0x0302,
    ERR_MISSING_IMPORT_NAME                       = 0x0303,
    ERR_MISSING_SEMICOLON_IMPORT                  = 0x0304,
    ERR_MISSING_NS_OPEN_BRACE                     = 0x0305,
    ERR_MISSING_NS_CLOSE_BRACE                    = 0x0306,
    ERR_QNAME_AFTER_ROOT_SEP                      = 0x0307,
    ERR_QNAME_AFTER_INTERMEDIATE_SEP              = 0x0308,
    ERR_FUNC_EXPECT_FINALIZE                      = 0x0309,
    ERR_FUNC_EXPECT_FIRST_PARAM                   = 0x030A,
    ERR_FUNC_EXPECT_FINALIZE_2                    = 0x030B,
    ERR_FUNC_EXPECT_CLOSE_OR_COMMA                = 0x030C,
    ERR_FUNC_EXPECT_PARAM_SPEC                    = 0x030D,
    ERR_FUNC_EXPECT_BODY_BLOCK                    = 0x030E,
    ERR_BLOCK_MISSING_CLOSE_BRACE                 = 0x030F,
    ERR_RETURN_MISSING_SEMICOLON                  = 0x0310,
    ERR_VARDECL_EXPECT_TYPE                       = 0x0311,
    ERR_VARDECL_EXPECT_INIT_EXPR                  = 0x0312,
    ERR_VARDECL_MISSING_SEMICOLON                 = 0x0313,
    ERR_EXPRSTMT_MISSING_SEMICOLON                = 0x0314,
    ERR_EXPRLIST_EXPECT_SUBEXPR                   = 0x0315,
    ERR_ASSIGN_EXPECT_SUBEXPR                     = 0x0316,
    ERR_COND_EXPECT_THEN_EXPR                     = 0x0317,
    ERR_COND_EXPECT_COLON                         = 0x0318,
    ERR_COND_EXPECT_ELSE_EXPR                     = 0x0319,
    ERR_LOGOR_EXPECT_SUBEXPR                      = 0x031A,
    ERR_LOGAND_EXPECT_SUBEXPR                     = 0x031B,
    ERR_BITOR_EXPECT_SUBEXPR                      = 0x031C,
    ERR_BITXOR_EXPECT_SUBEXPR                     = 0x031D,
    ERR_BITAND_EXPECT_SUBEXPR                     = 0x031E,
    ERR_EQUALITY_EXPECT_SUBEXPR                   = 0x031F,
    ERR_RELATIONAL_EXPECT_SUBEXPR                 = 0x0320,
    ERR_SHIFT_EXPECT_SUBEXPR                      = 0x0321,
    ERR_ADDITIVE_EXPECT_SUBEXPR                   = 0x0322,
    ERR_MULTIPLICATIVE_EXPECT_SUBEXPR             = 0x0323,
    ERR_CAST_EXPECT_SUBEXPR                       = 0x0324,
    ERR_UNARY_EXPECT_SUBEXPR                      = 0x0325,
    ERR_BRACKET_EXPECT_SUBEXPR                    = 0x0326,
    ERR_BRACKET_EXPECT_CLOSE                      = 0x0327,
    ERR_PAREN_POSTFIX_EXPECT_CLOSE                = 0x0328,
    ERR_PAREN_EXPECT_SUBEXPR                      = 0x0329,
    ERR_PAREN_EXPECT_CLOSE                        = 0x032A,
    ERR_IF_EXPECT_OPEN_PAREN                      = 0x032B,
    ERR_IF_EXPECT_CONDITION                       = 0x032C,
    ERR_IF_EXPECT_CLOSE_PAREN                     = 0x032D,
    ERR_IF_EXPECT_BODY                            = 0x032E,
    ERR_IF_EXPECT_ELSE_BODY                       = 0x032F,
    ERR_WHILE_EXPECT_OPEN_PAREN                   = 0x0330,
    ERR_WHILE_EXPECT_CONDITION                    = 0x0331,
    ERR_WHILE_EXPECT_CLOSE_PAREN                  = 0x0332,
    ERR_WHILE_EXPECT_BODY                         = 0x0333,
    ERR_FOR_EXPECT_OPEN_PAREN                     = 0x0334,
    ERR_FOR_EXPECT_INIT_OR_SEMICOLON              = 0x0335,
    ERR_FOR_EXPECT_COND_OR_SEMICOLON              = 0x0336,
    ERR_FOR_EXPECT_CLOSE_PAREN                    = 0x0337,
    ERR_FOR_EXPECT_BODY                           = 0x0338,
    ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET           = 0x0339,
    ERR_STRUCT_MISSING_OPEN_BRACE                 = 0x033A,
    ERR_STRUCT_MISSING_CLOSE_BRACE                = 0x033B,
    ERR_DTOR_MUST_HAVE_NO_PARAMS                  = 0x033C,
    ERR_DTOR_MUST_HAVE_NO_RETURN                  = 0x033D,
    ERR_EXPECTED_BASE_CLASS_NAME                  = 0x033E,
    ERR_NO_DEFAULT_AFTER_NON_DEFAULT              = 0x033F,
    ERR_MEMINIT_EXPECT_OPEN_PAREN                 = 0x0340,
    ERR_MEMINIT_EXPECT_EXPR_OR_CLOSE              = 0x0341,
    ERR_MEMINIT_EXPECT_COMMA_OR_CLOSE             = 0x0342,
    ERR_MEMINIT_EXPECT_EXPR_AFTER_COMMA           = 0x0343,
    ERR_ALIAS_EXPECT_SEMICOLON                    = 0x0344,
    ERR_ALIAS_EXPECT_BODY_DEFAULT_DELETE          = 0x0345,
    ERR_ALIAS_INVALID_KEYWORD                     = 0x0346,
    ERR_REDIRECT_EXPECT_TARGET                    = 0x0347,
    ERR_REDIRECT_EXPECT_TYPE_OR_CLOSE             = 0x0348,
    ERR_REDIRECT_EXPECT_COMMA_OR_CLOSE            = 0x0349,
    ERR_REDIRECT_EXPECT_SEMICOLON                 = 0x034A,
    ERR_EXPECTED_OPERATOR_SYMBOL                  = 0x034B,
    ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE         = 0x034C,
    ERR_OPERATOR_PREDEC_EXPECT_UNDERSCORE         = 0x034D,
    ERR_UNSUPPORTED_OPERATOR_SYMBOL               = 0x034E,
    ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC           = 0x034F,
    ERR_INVALID_OPERATOR_AFTER_KEYWORD            = 0x0350,
    ERR_CAST_OPERATOR_EMPTY_PARAMS                = 0x0351,
    ERR_CAST_OPERATOR_EXPECT_RETURN_TYPE          = 0x0352,
    ERR_REDIRECT_ABSTRACT_INVALID                 = 0x0353,
    ERR_USING_EXPECT_QNAME                        = 0x0354,
    ERR_USING_MISSING_SEMICOLON                   = 0x0355,
    ERR_ENUM_ENTRY_EXPECT_VALUE                   = 0x0356,
    ERR_ENUM_ENTRY_MISSING_SEMICOLON              = 0x0357,
    ERR_ENUM_MISSING_CLOSE_BRACE                  = 0x0358,
    ERR_ENUM_MISSING_SEMICOLON                    = 0x0359,
    ERR_DESIGNATED_EXPECT_EQ_OR_PAREN             = 0x035A,
    ERR_MIXED_POSITIONAL_DESIGNATED               = 0x035B,
    ERR_DESIGNATED_CTOR_EXPECT_COMMA_CLOSE        = 0x035C,
    ERR_FRIEND_EXPECT_QNAME                       = 0x035D,
    ERR_FRIEND_MISSING_SEMICOLON                  = 0x035E,
    ERR_NAMED_RET_EXPECT_TYPE                     = 0x035F,
    ERR_NAMED_RET_EXPECT_INIT_EXPR                = 0x0360,
    ERR_NAMED_RET_CTOR_EXPECT_EXPR_CLOSE          = 0x0361,
    ERR_EXPECTED_BASE_ENUM_NAME                   = 0x0362,
    ERR_NAMED_RET_CTOR_EXPECT_EXPR_COMMA          = 0x0363,
    ERR_NAMED_RET_NO_ALIAS                        = 0x0364,
    ERR_ANNOTATION_EXPECT_NAME                    = 0x0365,
    ERR_ANNOTATION_EXPECT_CLOSE_PAREN             = 0x0366,
    ERR_ANNOTATION_EXPECT_NAME_EXPR               = 0x0367,
    ERR_BRACE_INIT_NESTED_ERROR                   = 0x0368,
    ERR_BRACE_INIT_SEP_ERROR                      = 0x0369,
    ERR_BREAK_MISSING_SEMICOLON                   = 0x036A,
    ERR_CONTINUE_MISSING_SEMICOLON                = 0x036B,
    ERR_ENUM_MISSING_OPEN_BRACE                   = 0x036C,
    ERR_ENUM_ENTRY_EXPECT_NAME                    = 0x036D,
    // Generic declaration errors (0x01A0–0x01AF)
    ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED           = 0x036E,
    // Varargs parameter errors (0x01B0–0x01BF)
    ERR_VARARGS_NOT_LAST                          = 0x036F,
    ERR_VARARGS_WITH_DEFAULT                      = 0x0370,
    ERR_MULTIPLE_VARARGS                          = 0x0371,
    // Warnings (0x01C0–0x01CF)
    WARN_SPURIOUS_FUN_PREFIX                      = 0x0372,
    WARN_SPURIOUS_SEMICOLON                       = 0x0373,
    // Exception-related parser errors (0x01D0–0x01DF)
    ERR_THROW_EXPECT_EXPRESSION                   = 0x0374,
    ERR_THROW_MISSING_SEMICOLON                   = 0x0375,
    ERR_TRY_EXPECT_BODY                           = 0x0376,
    ERR_TRY_EXPECT_CATCH                          = 0x0377,
    ERR_CATCH_EXPECT_OPEN_PAREN                   = 0x0378,
    ERR_CATCH_EXPECT_IDENTIFIER                   = 0x0379,
    ERR_CATCH_EXPECT_COLON                        = 0x037A,
    ERR_CATCH_EXPECT_TYPE                         = 0x037B,
    ERR_CATCH_EXPECT_CLOSE_PAREN                  = 0x037C,
    ERR_CATCH_EXPECT_BODY                         = 0x037D,
    ERR_THROWS_EXPECT_TYPE                        = 0x037E,
    ERR_TRY_EXPECT_FINALLY_BODY                   = 0x037F,
    ERR_SPACESHIP_EXPECT_SUBEXPR                  = 0x0380,
    ERR_THROWS_EXPECT_OPEN_PAREN                  = 0x0381,
    ERR_THROWS_EXPECT_CLOSE_PAREN                 = 0x0382,
    // Foreach statement parser errors (0x01F3–0x01FF)
    ERR_FOREACH_EXPECT_INIT_EXPR                  = 0x0383,
    ERR_FOREACH_EXPECT_CLOSE_OR_SEMICOLON         = 0x0384,
    ERR_FOREACH_EXPECT_BODY                       = 0x0385,
    // Alias / typedef declaration parser errors (0x0200–0x020F)
    ERR_ALIAS_EXPECT_NAME                         = 0x0386,
    ERR_ALIAS_EXPECT_COLON                        = 0x0387,
    ERR_ALIAS_EXPECT_QNAME                        = 0x0388,
    ERR_ALIAS_EXPECT_TYPE                         = 0x0389,
    ERR_ALIAS_MISSING_SEMICOLON                   = 0x038A,
    // Template & generic declaration parser errors
    ERR_TEMPLATE_EXPECT_OPEN_CHEVRON              = 0x038B,
    ERR_TEMPLATE_EXPECT_PARAM                     = 0x038C,
    ERR_TEMPLATE_EXPECT_PARAM_AFTER_COMMA         = 0x038D,
    ERR_TEMPLATE_EXPECT_CLOSE_CHEVRON             = 0x038E,
    ERR_GENERIC_EXPECT_OPEN_CHEVRON               = 0x038F,
    ERR_GENERIC_EXPECT_PARAM                      = 0x0390,
    ERR_GENERIC_EXPECT_PARAM_AFTER_COMMA          = 0x0391,
    ERR_GENERIC_EXPECT_CLOSE_CHEVRON              = 0x0392,
    ERR_TEMPLATE_PARAM_EXPECT_NAME                = 0x0393,
    ERR_TEMPLATE_PARAM_PACK_NO_CONSTRAINT         = 0x0394,
    ERR_TEMPLATE_PARAM_PACK_NO_DEFAULT            = 0x0395,
    ERR_TEMPLATE_PARAM_EXPECT_CONSTRAINT_TYPE     = 0x0396,
    ERR_TEMPLATE_PARAM_EXPECT_DEFAULT_TYPE        = 0x0397,
    ERR_TEMPLATE_VALUE_PARAM_EXPECT_DEFAULT_EXPR  = 0x0398,
};

} // namespace k::diag

#endif // KLANG_ERRORS_LEX_PARSE_HPP
