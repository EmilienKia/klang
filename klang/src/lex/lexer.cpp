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
//
// Note: Last lexical log number: 0x0010
//

#include "lexer.hpp"
#include "../common/logger.hpp"

#include <map>

namespace k::lex {

    const std::map<std::string, keyword::type_t> keywords {
        {"module", keyword::MODULE},
        {"import", keyword::IMPORT},
        {"namespace", keyword::NAMESPACE},
        {"public", keyword::PUBLIC},
        {"protected", keyword::PROTECTED},
        {"private", keyword::PRIVATE},
        {"static", keyword::STATIC},
        {"const", keyword::CONST},
        {"abstract", keyword::ABSTRACT},
        {"final", keyword::FINAL},
        {"return", keyword::RETURN},
        {"this", keyword::THIS},

        {"if", keyword::IF},
        {"else", keyword::ELSE},
        {"while", keyword::WHILE},
        {"for", keyword::FOR},

        {"struct", keyword::STRUCT},
        {"class", keyword::CLASS},
        {"interface", keyword::INTERFACE},
        {"default", keyword::DEFAULT},
        {"delete", keyword::DELETE},
        {"new", keyword::NEW},
        {"enum", keyword::ENUM},
        {"operator", keyword::OPERATOR},

        {"bool", keyword::BOOL},
        {"byte", keyword::BYTE},
        {"char", keyword::CHAR},
        {"short", keyword::SHORT},
        {"int", keyword::INT},
        {"long", keyword::LONG},
        {"float", keyword::FLOAT},
        {"double", keyword::DOUBLE},

        {"unsigned", keyword::UNSIGNED},

        {"using", keyword::USING},
        {"friend", keyword::FRIEND},
        {"extern", keyword::EXTERN}
    };

    const std::map<std::string, punctuator::type_t> punctuators {
        {"(", punctuator::PARENTHESIS_OPEN},
        {")", punctuator::PARENTHESIS_CLOSE},
        {"{", punctuator::BRACE_OPEN},
        {"}", punctuator::BRACE_CLOSE},
        {"[", punctuator::BRACKET_OPEN},
        {"]", punctuator::BRACKET_CLOSE},
        {";", punctuator::SEMICOLON},
        {",", punctuator::COMMA},
        {"::", punctuator::DOUBLE_COLON},
        {"...", punctuator::ELLIPSIS},
        {"@", punctuator::AT_SIGN}
    };

    const std::map<std::string, operator_::type_t> operators {
        {".", operator_::DOT},
        {"->", operator_::ARROW},
        {".*", operator_::DOT_STAR},
        {"->*", operator_::ARROW_STAR},
        {"?", operator_::QUESTION_MARK},
        {":", operator_::COLON},
        {"!", operator_::EXCLAMATION_MARK},
        {"~", operator_::TILDE},
        {"=", operator_::EQUAL},
        {"+", operator_::PLUS},
        {"-", operator_::MINUS},
        {"*", operator_::STAR},
        {"/", operator_::SLASH},
        {"&", operator_::AMPERSAND},
        {"|", operator_::PIPE},
        {"^", operator_::CARET},
        {"%", operator_::PERCENT},
        {"<<", operator_::DOUBLE_CHEVRON_OPEN},
        {">>", operator_::DOUBLE_CHEVRON_CLOSE},
        {"+=", operator_::PLUS_EQUAL},
        {"-=", operator_::MINUS_EQUAL},
        {"*=", operator_::STAR_EQUAL},
        {"/=", operator_::SLASH_EQUAL},
        {"&=", operator_::AMPERSAND_EQUAL},
        {"|=", operator_::PIPE_EQUAL},
        {"^=", operator_::CARET_EQUAL},
        {"%=", operator_::PERCENT_EQUAL},
        {"<<=", operator_::DOUBLE_CHEVRON_OPEN_EQUAL},
        {">>=", operator_::DOUBLE_CHEVRON_CLOSE_EQUAL},
        {"==", operator_::DOUBLE_EQUAL},
        {"!=", operator_::EXCLAMATION_MARK_EQUAL},
        {">", operator_::CHEVRON_CLOSE},
        {"<", operator_::CHEVRON_OPEN},
        {">=", operator_::CHEVRON_CLOSE_EQUAL},
        {"<=", operator_::CHEVRON_OPEN_EQUAL},
        {"<=>", operator_::CHEVRON_OPEN_EQUAL_CHEVRON_CLOSE},
        {"&&", operator_::DOUBLE_AMPERSAND},
        {"||", operator_::DOUBLE_PIPE},
        {"++", operator_::DOUBLE_PLUS},
        {"--", operator_::DOUBLE_MINUS},
        {"**", operator_::DOUBLE_STAR},
        {"#", operator_::HASH}
    };

    std::set<char> lexer::operator_punctuator_chars;

    std::map<std::string, lexer::punct_or_op_type_t, lexer::less_order_op_punct_for_lookup> lexer::puncts_or_ops;

    bool lexer::less_order_op_punct_for_lookup::operator()(const std::string &a, const std::string &b) const {
        // Force bigger tokens to be places before smallers
        // Else sort them alphabetically
        // TODO optimize non-prefix placement to optimize lookup.
        if(a.size() > b.size()) {
            return true;
        } else if(a.size() < b.size()) {
            return false;
        } else {
            return a < b;
        }
    }

    lexer::lexer(k::log::logger& logger):
    logger_relay(logger)
    {
        init();
    }

    void lexer::init() {
        // Register all chars of puctuators and operators in a set to look for them conveniently
        if(operator_punctuator_chars.empty()) {
            for(auto& punct : punctuators) {
                for(char c : punct.first) {
                    operator_punctuator_chars.insert(c);
                }
            }
            for(auto& op : operators) {
                for(char c : op.first) {
                    operator_punctuator_chars.insert(c);
                }
            }
        }
        // Register all punctuators and operators in lookup map
        if(puncts_or_ops.empty()) {
            for(auto& punct : punctuators) {
                puncts_or_ops.insert(punct);
            }
            for(auto& op : operators) {
                puncts_or_ops.insert(op);
            }
        }
    }

    std::vector<any_lexeme> lexer::parse(k::source& source) {
        this->source = &source;

        const std::string& src = this->source->content;
        std::vector<unsigned int>& lines = this->source->lines;

        pos = begin = 0;
        source.lines.push_back(0);

        try {
            while (pos <= src.size()) {
                char c = pos == src.size() ? 0 : src[pos];

                // This loop may let analyze the same char multiple time.
                // Let's go to the end of the loop block to go to the get char.
                // Immediately continue to analyze again with potentially another state.

                switch (lex_state) {
                    case CR:
                        if (c == '\n') {
                            source.lines.push_back(pos+1);
                            break;
                        } else {
                            source.lines.push_back(pos);
                            // Not the CRLF continuation, consider as START
                            lex_state = START;
                            continue;
                        }
                    case START:
                        if (is_whitespace(c)) {
                            break;
                        }
                        if (c == '\r') {
                            lex_state = CR;
                            continue;
                        } else if (c == '\n') {
                            source.lines.push_back(pos+1);
                            break;
                        } else if (c >= 'A' && c <= 'Z'
                                   || c >= 'a' && c <= 'z'
                                   || c == '_'
                                   || c == '$'
                            /* Todo handle unicode here ? */
                                ) {
                            begin = pos;
                            lex_state = IDENTIFIER;
                                } else if (c == '0') {
                                    begin = pos;
                                    lex_state = ZERO;
                                } else if (c >= '1' && c <= '9') {
                                    begin = pos;
                                    num_content_size++;
                                    base = numeric_base::DECIMAL;
                                    lex_state = DECIMAL;
                                } else if (c == '\'') {
                                    begin = pos;
                                    lex_state = CHAR;
                                } else if (c == '"') {
                                    begin = pos;
                                    lex_state = STRING;
                                } else if (c == '/') {
                                    begin = pos;
                                    lex_state = SLASH;
                                } else if (c == '.') {
                                    begin = pos;
                                    lex_state = POINT;
                                } else if (is_operator_punctuator_char(c)) {
                                    begin = pos;
                                    lex_state = OPERATOR;
                                } else {
                                    /* TODO */
                                }
                        break;
                    case POINT:
                        if (c >='0' && c <='9') {
                            num_content_size = 2;
                            lex_state = FLOAT_POINT_DIGIT;
                        } else if (is_operator_punctuator_char(c)) {
                            lex_state = OPERATOR;
                        } else {
                            lexemes.push_back(operator_(get_content(), operator_::DOT));
                            begin = 0;
                            lex_state = START;
                            continue;
                        }
                        break;
                    case SLASH:
                        if (c == '/') {
                            lex_state = COMMENT_SINGLE_LINE;
                        } else if (c == '*') {
                            lex_state = COMMENT_MULTI_LINES;
                        } else {
                            // Consider '/' as the operator.
                            lex_state = OPERATOR;
                            continue;
                        }
                        break;
                    case COMMENT_SINGLE_LINE:
                        if (c == '\r' || c == '\n' || c == 0) {
                            lexemes.push_back(comment(get_content()));
                            if (c!=0) {
                                source.lines.push_back(pos+1);
                            }
                            begin = 0;
                            if (c == '\r') {
                                lex_state = CR;
                            } else {
                                lex_state = START;
                            }
                            break;
                        }
                        break;
                    case COMMENT_MULTI_LINES:
                        if (c == '*') {
                            lex_state = COMMENT_MULTI_LINES_END;
                        } else if (c == '\n') {
                            source.lines.push_back(pos+1);
                        } else if (c =='\r') {
                            lex_state = COMMENT_MULTI_LINES_CR;
                        } else {
                            // TODO Handle EOF
                        }
                        break;
                    case COMMENT_MULTI_LINES_CR:
                        if (c == '\n') {
                            source.lines.push_back(pos+1);
                            lex_state = COMMENT_MULTI_LINES;
                            break;
                        } else {
                            source.lines.push_back(pos);
                            // Not the CRLF continuation, consider as COMMENT_MULTI_LINES
                            lex_state = COMMENT_MULTI_LINES;
                            continue;
                        }
                    case COMMENT_MULTI_LINES_END:
                        if (c == '/') {
                            lexemes.push_back(comment(get_content(get_current_size()+1)));
                            begin = 0;
                            lex_state = START;
                        } else {
                            lex_state = COMMENT_MULTI_LINES;
                        }
                        break;
                    case OPERATOR:
                        // TODO Review and expand operator and punctuator parsing
                        //  - Make punctuator/operator parsing statefull to able to support >> as shift operator or two closing chevrons
                        if (!is_operator_punctuator_char(c)) {
                            while(!get_content().empty()) {
                                bool found = false;
                                for(auto& looked : puncts_or_ops) {
                                    if(get_content().starts_with(looked.first)) {
                                        size_t sz = looked.first.size();
                                        if(std::holds_alternative<punctuator::type_t>(looked.second)) {
                                            lexemes.push_back(punctuator(get_content(sz), std::get<punctuator::type_t>(looked.second)));
                                        } else {
                                            lexemes.push_back(operator_(get_content(sz), std::get<operator_::type_t>(looked.second)));
                                        }
                                        begin += sz;
                                        found = true;
                                        break;
                                    }
                                }
                                if(!found) {
                                    /* Error, unknown punctuator nor operator. */
                                    error(0x0001, "Unknown operator '{}'", {std::string{get_content()}}, begin, pos);
                                    // TODO throw exception
                                }
                            }
                            begin = 0;
                            lex_state = START;
                            continue;
                        }
                        break;
                    case IDENTIFIER:
                        if (! (c >= 'A' && c <= 'Z'
                            || c >= 'a' && c <= 'z'
                            || c == '_'
                            || c >= '0' && c <= '9'
                            // || c == '$' No dollar at middle of identifier
                            /* Todo handle unicode here ? */
                                )) {
                            if (get_content() == "null") {
                                lexemes.push_back(null(get_content()));
                            } else if (get_content() == "true" || get_content() == "false") {
                                lexemes.push_back(boolean(get_content()));
                            } else if (auto kw = keywords.find(std::string(get_content())); kw!=keywords.end()) {
                                lexemes.push_back(keyword(get_content(), kw->second));
                            } else {
                                lexemes.push_back(identifier(get_content()));
                            }
                            begin = 0;
                            lex_state = START;
                            continue;
                                }
                        break;
                    case ZERO:
                        if (c == 'x' || c == 'X') {
                            base = numeric_base::HEXADECIMAL;
                            num_prefix_size += 2;
                            lex_state = HEXA_PREFIX;
                        } else if (c == 'b' || c == 'B') {
                            base = numeric_base::BINARY;
                            num_prefix_size += 2;
                            lex_state = BIN_PREFIX;
                        } else if (c == 'o' || c == 'O') {
                            base = numeric_base::OCTAL;
                            num_prefix_size += 2;
                            lex_state = OCTAL_PREFIX;
                        } else if (c >= '0' && c <= '7') {
                            base = numeric_base::OCTAL;
                            num_prefix_size = 1;
                            num_content_size = 1;
                            lex_state = OCTAL;
                        } else if (c >= '8' && c <= '9'
                                   || c >= 'a' && c <= 'f'
                                   || c >= 'A' && c <= 'F') {
                            /* Error : no Hexadec digit for octal number. */
                            error(0x0002, "Forbiden hexadigital character in octal number '{}'", {std::string{get_content()} + c}, begin, pos);
                            // TODO throw exception
                                   } else if (c == 'u' || c == 'U') {
                                       saved_state = lex_state;
                                       unsigned_num = true;
                                       num_content_size = 1;
                                       lex_state = INT_UNSIGNED_SUFFIX;
                                   } else if (c == 'i' || c == 'I') {
                                       saved_state = lex_state;
                                       num_content_size = 1;
                                       push_integer_and_reset();
                                       lex_state = START;
                                   } else if (c == 's' || c == 'S') {
                                       saved_state = lex_state;
                                       size = SHORT;
                                       num_content_size = 1;
                                       push_integer_and_reset();
                                       lex_state = START;
                                   } else if (c == 'l' || c == 'L') {
                                       num_content_size = 1;
                                       lex_state = INT_LONG_SUFFIX;
                                   } else if (c == '.') {
                                       num_content_size = 2;
                                       lex_state = FLOAT_DIGIT_POINT_DIGIT;
                                   } else {
                                       // TODO also add size suffix handling
                                       // Emit "0" number
                                       num_content_size = 1;
                                       push_integer_and_reset(false);
                                       lex_state = START;
                                       continue;
                                   }
                        break;
                    case HEXA_PREFIX:
                        if (c >= '0' && c <= '9'
                            || c >= 'a' && c <= 'f'
                            || c >= 'A' && c <= 'F') {
                            num_content_size++;
                            lex_state = HEXADECIMAL;
                            } else if (c == 'u' || c == 'U') {
                                warn(0x0003, "Hexadecimal number should have at least one digit before unsigned suffix '{}'", {std::string{get_content()} + c}, pos);
                                // WARN should have at least one digit after prefix
                                saved_state = lex_state;
                                unsigned_num = true;
                                lex_state = INT_UNSIGNED_SUFFIX;
                            } else {
                                // TODO also add size suffix handling
                                warn(0x0004, "Hexadecimal number should have at least one digit before size suffix '{}'", {std::string{get_content()} + c}, pos);
                                // WARN should have at least one digit after prefix
                            }
                        break;
                    case BIN_PREFIX:
                        if (c == '0' || c == '1') {
                            num_content_size++;
                            lex_state = BINARY;
                        } else {
                            // TODO also add unsigned suffix handling
                            // TODO also add size suffix handling
                            warn(0x0005 /* and 0x0006 */, "Binary number should have at least one digit before suffix '{}'", {std::string{get_content()} + c}, pos);
                            // WARN should have at least one digit after prefix
                            /* Error, binary number must have at least one digit. */
                        }
                        break;
                    case OCTAL_PREFIX:
                        if (c >= '0' && c <= '7') {
                            num_content_size++;
                            lex_state = OCTAL;
                        } else {
                            // TODO also add unsigned suffix handling
                            // TODO also add size suffix handling
                            // WARN should have at least one digit after prefix
                            warn(0x0007 /* and 0x0008 */, "Octal number should have at least one digit before suffix '{}'", {std::string{get_content()} + c}, pos);
                            /* Error, octal number must have at least one digit. */
                        }
                        break;
                    case HEXADECIMAL:
                        if (c >= '0' && c <= '9'
                            || c >= 'a' && c <= 'f'
                            || c >= 'A' && c <= 'F'
                            || c == '_') {
                            num_content_size++;
                            } else if (c == 'u' || c == 'U') {
                                saved_state = lex_state;
                                unsigned_num = true;
                                lex_state = INT_UNSIGNED_SUFFIX;
                            } else if (c == 'i' || c == 'I') {
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 's' || c == 'S') {
                                size = SHORT;
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 'l' || c == 'L') {
                                lex_state = INT_LONG_SUFFIX;
                            } else if (c == 'b' || c == 'B') {
                                lex_state = INT_BIGINT_SUFFIX;
                            } else {
                                // TODO add suffix handling
                                // Emit "0" number
                                push_integer_and_reset(false);
                                lex_state = START;
                                continue;
                            }
                        break;
                    case DECIMAL:
                        if (c >= '0' && c <= '9'
                            || c == '_') {
                            num_content_size++;
                            } else if (c == 'u' || c == 'U') {
                                saved_state = lex_state;
                                unsigned_num = true;
                                lex_state = INT_UNSIGNED_SUFFIX;
                            } else if (c == 'i' || c == 'I') {
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 's' || c == 'S') {
                                size = SHORT;
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 'l' || c == 'L') {
                                lex_state = INT_LONG_SUFFIX;
                            } else if (c == 'b' || c == 'B') {
                                lex_state = INT_BIGINT_SUFFIX;
                            } else if (c == '.') {
                                num_content_size++;
                                lex_state = FLOAT_DIGIT_POINT_DIGIT;
                            } else if (c == 'e' || c == 'E') {
                                num_content_size++;
                                lex_state = FLOAT_DIGIT_EXP;
                            } else if (c == 'f' || c == 'F') {
                                fsize = FLOAT;
                                push_float_and_reset();
                                lex_state = START;
                            } else if (c == 'd' || c == 'D') {
                                fsize = DOUBLE;
                                push_float_and_reset();
                                lex_state = START;
                            } else {
                                // TODO add suffix handling
                                // Emit "0" number
                                push_integer_and_reset(false);
                                lex_state = START;
                                continue;
                            }
                        break;
                    case OCTAL:
                        if (c >= '0' && c <= '7'
                            || c == '_') {
                            num_content_size++;
                            } else if (c == 'u' || c == 'U') {
                                saved_state = lex_state;
                                unsigned_num = true;
                                lex_state = INT_UNSIGNED_SUFFIX;
                            } else if (c == 'i' || c == 'I') {
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 's' || c == 'S') {
                                size = SHORT;
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 'l' || c == 'L') {
                                lex_state = INT_LONG_SUFFIX;
                            } else if (c == 'b' || c == 'B') {
                                lex_state = INT_BIGINT_SUFFIX;
                            } else {
                                // TODO add suffix handling
                                // Emit "0" number
                                push_integer_and_reset(false);
                                lex_state = START;
                                continue;
                            }
                        break;
                    case BINARY:
                        if (c >= '0' && c <= '1'
                            || c == '_') {
                            num_content_size++;
                            } else if (c == 'u' || c == 'U') {
                                saved_state = lex_state;
                                unsigned_num = true;
                                lex_state = INT_UNSIGNED_SUFFIX;
                            } else if (c == 'i' || c == 'I') {
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 's' || c == 'S') {
                                size = SHORT;
                                push_integer_and_reset();
                                lex_state = START;
                            } else if (c == 'l' || c == 'L') {
                                lex_state = INT_LONG_SUFFIX;
                            } else if (c == 'b' || c == 'B') {
                                lex_state = INT_BIGINT_SUFFIX;
                            } else {
                                // TODO add suffix handling
                                // Emit "0" number
                                push_integer_and_reset(false);
                                lex_state = START;
                                continue;
                            }
                        break;
                    case FLOAT_DIGIT_POINT_DIGIT:
                        if(c >= '0' && c <= '9') {
                            num_content_size++;
                        } else if(c == 'e' || c == 'E') {
                            num_content_size++;
                            lex_state = FLOAT_DIGIT_POINT_DIGIT_EXP;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_DIGIT_POINT_DIGIT_EXP:
                        if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                            num_content_size++;
                            lex_state = FLOAT_DIGIT_POINT_DIGIT_EXP_DIGIT;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_DIGIT_POINT_DIGIT_EXP_DIGIT:
                        if(c >= '0' && c <= '9') {
                            num_content_size++;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_POINT_DIGIT:
                        if(c >= '0' && c <= '9') {
                            num_content_size++;
                        } else if(c == 'e' || c == 'E') {
                            num_content_size++;
                            lex_state = FLOAT_POINT_DIGIT_EXP;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_POINT_DIGIT_EXP:
                        if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                            num_content_size++;
                            lex_state = FLOAT_POINT_DIGIT_EXP_DIGIT;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_POINT_DIGIT_EXP_DIGIT:
                        if(c >= '0' && c <= '9') {
                            num_content_size++;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_DIGIT_EXP:
                        if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                            num_content_size++;
                            lex_state = FLOAT_DIGIT_EXP_DIGIT;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case FLOAT_DIGIT_EXP_DIGIT:
                        if(c >= '0' && c <= '9') {
                            num_content_size++;
                        } else if (c == 'f' || c == 'F') {
                            fsize = FLOAT;
                            push_float_and_reset();
                            lex_state = START;
                        } else if (c == 'd' || c == 'D') {
                            fsize = DOUBLE;
                            push_float_and_reset();
                            lex_state = START;
                        } else {
                            // TODO add other fp suffix handling
                            push_float_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case CHAR:
                        if (c == '\'') {
                            // TODO Test for empty char sequence
                            lexemes.push_back(character(get_content(get_current_size()+1)));
                            begin = 0;
                            lex_state = START;
                        } else if (c == '\\') {
                            saved_state = CHAR;
                            lex_state = ESCAPE;
                        } else {
                            // TODO Test for EOL or not printable char.
                        }
                        break;
                    case STRING:
                        if (c == '"') {
                            lexemes.push_back(string(get_content(get_current_size()+1)));
                            begin = 0;
                            lex_state = START;
                        } else if (c == '\\') {
                            saved_state = STRING;
                            lex_state = ESCAPE;
                        } else {
                            // TODO Test for EOL or not printable char.
                        }
                        break;
                    case ESCAPE:
                        if (c == '\'' || c == '"' || c == '?' || c == '\\'
                            || c == 'b' || c == 'f' || c == 'n' || c == 'r'
                            || c == 't' || c == 'v') {
                            /* TODO emmit escape. */
                            lex_state = saved_state;
                            saved_state = START;
                            } else if (c >= '0' && c <= '7') {
                                lex_temp_count = 1;
                                lex_state = ESCAPE_OCTAL;
                            } else if (c == 'x') {
                                lex_temp_count = 0;
                                lex_state = ESCAPE_HEXA;
                            } else if (c == 'u') {
                                lex_temp_count = 0;
                                lex_state = ESCAPE_UNIVERSAL;
                            } else if (c == 'U') {
                                lex_temp_count = 0;
                                lex_state = ESCAPE_UNIVERSAL_LONG;
                            } else {
                                error(0x0009, "Bad escape sequence '{}'", {std::string{get_content()} + c}, pos);
                                /* error : bad escape sequence character. */
                                // TODO throw exception
                            }
                        break;
                    case ESCAPE_OCTAL:
                        if (c >= '0' && c <= '7') {
                            lex_temp_count++;
                            if (lex_temp_count == 3) {
                                // Exhaust octal escape, return to lex.
                                lex_temp_count = 0;
                                lex_state = saved_state;
                                saved_state = START;
                            }
                        } else {
                            // WARN/TODO : not a complete octal escape.
                            lex_state = saved_state;
                            saved_state = START;
                            continue;
                        }
                        break;
                    case ESCAPE_HEXA:
                        if (c >= '0' && c <= '9'
                            || c >= 'A' && c <= 'F'
                            || c >= 'a' && c <= 'f') {
                            lex_temp_count++;
                            if (lex_temp_count == 2) {
                                // Exhaust hexa escape, return to lex.
                                lex_temp_count = 0;
                                lex_state = saved_state;
                                saved_state = START;
                            }
                            } else {
                                warn(0x000A, "Incomplete hexa escape sequence '{}'", {std::string{get_content()} + c}, pos);
                                // WARN/TODO : not a complete hexa escape.
                                lex_state = saved_state;
                                saved_state = START;
                                continue;
                            }
                        break;
                    case ESCAPE_UNIVERSAL:
                        if (c >= '0' && c <= '9'
                            || c >= 'A' && c <= 'F'
                            || c >= 'a' && c <= 'f') {
                            lex_temp_count++;
                            if (lex_temp_count == 4) {
                                // Exhaust universal escape, return to lex.
                                lex_temp_count = 0;
                                lex_state = saved_state;
                                saved_state = START;
                            }
                            } else {
                                warn(0x000B, "Incomplete universal escape sequence '{}'", {std::string{get_content()} + c}, pos);
                                // WARN/TODO : not a complete universal escape.
                                lex_state = saved_state;
                                saved_state = START;
                                continue;
                            }
                        break;
                    case ESCAPE_UNIVERSAL_LONG:
                        if (c >= '0' && c <= '9'
                            || c >= 'A' && c <= 'F'
                            || c >= 'a' && c <= 'f') {
                            lex_temp_count++;
                            if (lex_temp_count == 8) {
                                // Exhaust universal escape, return to lex.
                                lex_temp_count = 0;
                                lex_state = saved_state;
                                saved_state = START;
                            }
                            } else {
                                warn(0x000C, "Incomplete long universal escape sequence '{}'", {std::string{get_content()} + c}, pos);
                                // WARN/TODO : not a complete long universal escape.
                                lex_state = saved_state;
                                saved_state = START;
                                continue;
                            }
                        break;
                    case INT_UNSIGNED_SUFFIX:
                        if (c == 's' || c == 'S') {
                            size = SHORT;
                            push_integer_and_reset();
                            lex_state = START;
                        } else if (c == 'i' || c == 'I') {
                            push_integer_and_reset();
                            lex_state = START;
                        } else if (c == 'l' || c == 'L') {
                            lex_state = INT_LONG_SUFFIX;
                        } else if (c == 'b' || c == 'B') {
                            lex_state = INT_BIGINT_SUFFIX;
                        } else {
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case INT_LONG_SUFFIX:
                        if (c == 'l' || c == 'L') {
                            size = LONGLONG;
                            push_integer_and_reset();
                            lex_state = START;
                        } else if (c == '6') {
                            lex_state = INT_LONG64_SUFFIX;
                        } else if (c == '1') {
                            lex_state = INT_LONG128A_SUFFIX;
                        } else {
                            size = LONG;
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case INT_LONG64_SUFFIX:
                        if (c == '4') {
                            size = LONG;
                            push_integer_and_reset();
                            lex_state = START;
                        } else {
                            warn(0x000D, "Bad integer suffix '{}', expect character '4'", {std::string{get_content()} + c}, pos);
                            // TODO/ERROR Bad integer suffix, expect character '4'.
                            size = LONG;
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case INT_LONG128A_SUFFIX:
                        if (c == '2') {
                            lex_state = INT_LONG128B_SUFFIX;
                        } else {
                            warn(0x000E, "Bad integer suffix '{}', expect character '2'", {std::string{get_content()} + c}, pos);
                            // TODO/ERROR Bad integer suffix, expect character '2'.
                            size = LONGLONG;
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case INT_LONG128B_SUFFIX:
                        if (c == '8') {
                            size = LONGLONG;
                            push_integer_and_reset();
                            lex_state = START;
                        } else {
                            warn(0x000F, "Bad integer suffix '{}', expect character '8'", {std::string{get_content()} + c}, pos);
                            // TODO/ERROR Bad integer suffix, expect character '8'.
                            size = LONGLONG;
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                    case INT_BIGINT_SUFFIX:
                        if (c == 'i' || c == 'I') {
                            size = BIGINT;
                            push_integer_and_reset();
                            lex_state = START;
                        } else {
                            // TODO handle byte size here
                            warn(0x0010, "Bad big integer suffix '{}', expect character 'B'", {std::string{get_content()} + c}, pos);
                            // TODO/ERROR Bad integer suffix, expect character 'B'.
                            size = LONGLONG;
                            push_integer_and_reset(false);
                            lex_state = START;
                            continue;
                        }
                        break;
                }

                // Let's increment
                pos++;
            }
        } catch (std::exception& ex) {
            while (pos <= src.size()) {
                char c = pos == src.size() ? 0 : src[pos];
                if (c == '\r' || c == '\n') {
                    source.lines.push_back(pos+1);
                    break;
                }
            }
        }
        return lexemes;
    }

    void lexer::push_integer_and_reset(bool include_last_char) {
        lexemes.push_back(integer(get_content(get_current_size() + (include_last_char ? 1 : 0)), num_prefix_size, num_content_size, base, unsigned_num, size));
        base = numeric_base::DECIMAL;
        unsigned_num = false;
        size = INT;
        num_prefix_size = num_content_size = 0;
        begin = 0;
    }

    void lexer::push_float_and_reset(bool include_last_char) {
        lexemes.push_back(float_num(get_content(get_current_size() + (include_last_char ? 1 : 0)), num_content_size, fsize));
        fsize = FLOAT_DEFAULT;
        num_content_size = 0;
        begin = 0;
    }

    opt_ref_any_lexeme lexer::get()
    {
        while(!eof()) {
            lex::any_lexeme & lex = lexemes[index++];
            if(!std::holds_alternative<lex::comment>(lex)){
                return std::ref(lex);
            }
        }
        return {};
    }

    void lexer::unget(size_t count) {
        while(index>0 && count>0) {
            const lex::any_lexeme & lex = lexemes[--index];
            if(!std::holds_alternative<lex::comment>(lex)){
                count--;
            }
        }
    }

    size_t lexer::tell() const {
        return index;
    }

    void lexer::seek(size_t index) {
        this->index = index;
    }

    opt_ref_any_lexeme lexer::pick_next() {
        if(!lexemes.empty() && index<lexemes.size()-1) {
            return std::ref(lexemes[index+1]);
        }  else {
            return {};
        }
    }

    opt_ref_any_lexeme lexer::pick_current() {
        if(!lexemes.empty() && index<lexemes.size()) {
            return std::ref(lexemes[index]);
        }  else {
            return {};
        }
    }

    opt_ref_any_lexeme lexer::pick_previous() {
        if(!lexemes.empty() && index<lexemes.size() && index > 0) {
            return std::ref(lexemes[index-1]);
        }  else {
            return {};
        }
    }

    bool lexer::eof() const {
        return lexemes.empty() || index>=lexemes.size();
    }

    void lexer::warn(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, size_t pos) {
        k::log::logger_relay::warn(code, std::string(msg), args);
    }

    void lexer::error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, size_t pos) {
        k::log::logger_relay::error(code, std::string(msg), args);
    }

    void lexer::error(unsigned int code, const std::string_view& msg, const std::vector<std::string>& args, size_t start, size_t end) {
        k::log::logger_relay::error(code, std::string(msg), args);
    }



} // k::lex
