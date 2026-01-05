/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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

        {"bool", keyword::BOOL},
        {"byte", keyword::BYTE},
        {"char", keyword::CHAR},
        {"short", keyword::SHORT},
        {"int", keyword::INT},
        {"long", keyword::LONG},
        {"float", keyword::FLOAT},
        {"double", keyword::DOUBLE},

        {"unsigned", keyword::UNSIGNED}
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
        {"**", operator_::DOUBLE_STAR}
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

    lexer::lexer(k::log::logger& logger):
        _logger(logger)
    {
        init();
    }

    void lexer::parse(std::string_view src) {

        while (pos.pos <= src.size()) {
            char c = pos.pos == src.size() ? 0 : src[pos.pos];

            // This loop may let analyse the same char multiple time.
            // Let's go to the end of the loop block to go to the get char.
            // Immediately continue to analyse again with potentially another state.

            switch (lex_state) {
                case CR:
                    if (c == '\n') {
                        break;
                    } else {
                        // Not the CRLF continuation, consider as START
                        lex_state = START;
                        continue;
                    }
                case START:
                    if (is_whitespace(c)) {
                        break;
                    }
                    if (c == '\r') {
                        ++pos.line;
                        pos.col = 0;
                        lex_state = CR;
                        continue;
                    } else if (c == '\n') {
                        ++pos.line;
                        pos.col = 0;
                        break;
                    } else if (c >= 'A' && c <= 'Z'
                               || c >= 'a' && c <= 'z'
                               || c == '_'
                               || c == '$'
                        /* Todo handle unicode here ? */
                            ) {
                        begin = pos;
                        content = c;
                        lex_state = IDENTIFIER;
                    } else if (c == '0') {
                        begin = pos;
                        content = c;
                        lex_state = ZERO;
                    } else if (c >= '1' && c <= '9') {
                        begin = pos;
                        content = c;
                        num_content_size++;
                        base = numeric_base::DECIMAL;
                        lex_state = DECIMAL;
                    } else if (c == '\'') {
                        begin = pos;
                        content = c;
                        lex_state = CHAR;
                    } else if (c == '"') {
                        begin = pos;
                        content = c;
                        lex_state = STRING;
                    } else if (c == '/') {
                        begin = pos;
                        content = c;
                        lex_state = SLASH;
                    } else if (c == '.') {
                        begin = pos;
                        content = c;
                        lex_state = POINT;
                    } else if (is_operator_punctuator_char(c)) {
                        begin = pos;
                        content = c;
                        lex_state = OPERATOR;
                    } else {
                        /* TODO */
                    }
                    break;
                case POINT:
                    if (c >='0' && c <='9') {
                        content += c;
                        num_content_size = 2;
                        lex_state = FLOAT_POINT_DIGIT;
                    } else if (is_operator_punctuator_char(c)) {
                        content += c;
                        lex_state = OPERATOR;
                    } else {
                        lexemes.push_back(operator_(begin, pos, content, operator_::DOT));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case SLASH:
                    if (c == '/') {
                        content += c;
                        lex_state = COMMENT_SINGLE_LINE;
                    } else if (c == '*') {
                        content += c;
                        lex_state = COMMENT_MULTI_LINES;
                    } else {
                        // Consider '/' as the operator.
                        lex_state = OPERATOR;
                        continue;
                    }
                    break;
                case COMMENT_SINGLE_LINE:
                    if (c == '\r' || c == '\n' || c == 0) {
                        lexemes.push_back(comment(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        break;
                    } else {
                        content += c;
                    }
                    break;
                case COMMENT_MULTI_LINES:
                    if (c == '*') {
                        content += c;
                        lex_state = COMMENT_MULTI_LINES_END;
                    } else {
                        // TODO Handle EOL
                        // TODO Handle EOF
                        content += c;
                    }
                    break;
                case COMMENT_MULTI_LINES_END:
                    if (c == '/') {
                        content += c;
                        lexemes.push_back(comment(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else {
                        content += c;
                        lex_state = COMMENT_MULTI_LINES;
                    }
                    break;
                case OPERATOR:
                    // TODO Review and expand operator and punctuator parsing
                    //  - Make punctuator/operator parsing statefull to able to support >> as shift operator or two closing chevrons
                    if (is_operator_punctuator_char(c)) {
                        content += c;
                    } else {
                        while(!content.empty()) {
                            bool found = false;
                            for(auto& looked : puncts_or_ops) {
                                if(content.starts_with(looked.first)) {
                                    size_t sz = looked.first.size();
                                    if(std::holds_alternative<punctuator::type_t>(looked.second)) {
                                        lexemes.push_back(punctuator(begin, begin+sz, content.substr(0, sz), std::get<punctuator::type_t>(looked.second)));
                                    } else {
                                        lexemes.push_back(operator_(begin, begin+sz, content.substr(0, sz), std::get<operator_::type_t>(looked.second)));
                                    }
                                    begin += sz;
                                    content.erase(content.begin(), content.begin()+sz);
                                    found = true;
                                    break;
                                }
                            }
                            if(!found) {
                                /* Error, unknown punctuator nor operator. */
                                _logger.error(0x0001, begin, pos, "Unknown operator '{}'", {content});
                                // TODO throw exception
                            }
                        }
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case IDENTIFIER:
                    if (c >= 'A' && c <= 'Z'
                        || c >= 'a' && c <= 'z'
                        || c == '_'
                        || c >= '0' && c <= '9'
                        // || c == '$' No dollar at middle of identifier
                        /* Todo handle unicode here ? */
                            ) {
                        content += c;
                    } else {
                        if (content == "null") {
                            lexemes.push_back(null(begin, pos, content));
                        } else if (content == "true" || content == "false") {
                            lexemes.push_back(boolean(begin, pos, content));
                        } else if (auto kw = keywords.find(content); kw!=keywords.end()) {
                            lexemes.push_back(keyword(begin, pos, content, kw->second));
                        } else {
                            lexemes.push_back(identifier(begin, pos, content));
                        }
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case ZERO:
                    if (c == 'x' || c == 'X') {
                        content += c;
                        base = numeric_base::HEXADECIMAL;
                        num_prefix_size += 2;
                        lex_state = HEXA_PREFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        base = numeric_base::BINARY;
                        num_prefix_size += 2;
                        lex_state = BIN_PREFIX;
                    } else if (c == 'o' || c == 'O') {
                        content += c;
                        base = numeric_base::OCTAL;
                        num_prefix_size += 2;
                        lex_state = OCTAL_PREFIX;
                    } else if (c >= '0' && c <= '7') {
                        content += c;
                        base = numeric_base::OCTAL;
                        num_prefix_size = 1;
                        num_content_size = 1;
                        lex_state = OCTAL;
                    } else if (c >= '8' && c <= '9'
                               || c >= 'a' && c <= 'f'
                               || c >= 'A' && c <= 'F') {
                        /* Error : no Hexadec digit for octal number. */
                        _logger.error(0x0002, begin, pos, "Forbiden hexadigital character in octal number '{}'", {content + c});
                        // TODO throw exception
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        num_content_size = 1;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        saved_state = lex_state;
                        num_content_size = 1;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        saved_state = lex_state;
                        size = SHORT;
                        num_content_size = 1;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        num_content_size = 1;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == '.') {
                        content += c;
                        num_content_size = 2;
                        lex_state = FLOAT_DIGIT_POINT_DIGIT;
                    } else {
                        // TODO also add size suffix handling
                        // Emit "0" number
                        num_content_size = 1;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case HEXA_PREFIX:
                    if (c >= '0' && c <= '9'
                        || c >= 'a' && c <= 'f'
                        || c >= 'A' && c <= 'F') {
                        content += c;
                        num_content_size++;
                        lex_state = HEXADECIMAL;
                    } else if (c == 'u' || c == 'U') {
                        _logger.warning(0x0003, pos, "Hexadecimal number should have at least one digit before unsigned suffix '{}'", {content + c});
                        // WARN should have at least one digit after prefix
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else {
                        // TODO also add size suffix handling
                        _logger.warning(0x0004, pos, "Hexadecimal number should have at least one digit before size suffix '{}'", {content + c});
                        // WARN should have at least one digit after prefix
                    }
                    break;
                case BIN_PREFIX:
                    if (c == '0' || c == '1') {
                        content += c;
                        num_content_size++;
                        lex_state = BINARY;
                    } else {
                        // TODO also add unsigned suffix handling
                        // TODO also add size suffix handling
                        _logger.warning(0x0005 /* and 0x0006 */, pos, "Binary number should have at least one digit before suffix '{}'", {content + c});
                        // WARN should have at least one digit after prefix
                        /* Error, binary number must have at least one digit. */
                    }
                    break;
                case OCTAL_PREFIX:
                    if (c >= '0' && c <= '7') {
                        content += c;
                        num_content_size++;
                        lex_state = OCTAL;
                    } else {
                        // TODO also add unsigned suffix handling
                        // TODO also add size suffix handling
                        // WARN should have at least one digit after prefix
                        _logger.warning(0x0007 /* and 0x0008 */, pos, "Octal number should have at least one digit before suffix '{}'", {content + c});
                        /* Error, octal number must have at least one digit. */
                    }
                    break;
                case HEXADECIMAL:
                    if (c >= '0' && c <= '9'
                        || c >= 'a' && c <= 'f'
                        || c >= 'A' && c <= 'F'
                        || c == '_') {
                        num_content_size++;
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        size = SHORT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else {
                        // TODO add suffix handling
                        // Emit "0" number
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case DECIMAL:
                    if (c >= '0' && c <= '9'
                        || c == '_') {
                        num_content_size++;
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        size = SHORT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else if (c == '.') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_DIGIT_POINT_DIGIT;
                    } else if (c == 'e' || c == 'E') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_DIGIT_EXP;
                    } else if (c == 'f' || c == 'F' || c == 'd' || c == 'D') {
                        content += c;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add suffix handling
                        // Emit "0" number
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case OCTAL:
                    if (c >= '0' && c <= '7'
                        || c == '_') {
                        num_content_size++;
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        size = SHORT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else {
                        // TODO add suffix handling
                        // Emit "0" number
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case BINARY:
                    if (c >= '0' && c <= '1'
                        || c == '_') {
                        num_content_size++;
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        unsigned_num = true;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        size = SHORT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else {
                        // TODO add suffix handling
                        // Emit "0" number
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_DIGIT_POINT_DIGIT:
                    if(c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                    } else if(c == 'e' || c == 'E') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_DIGIT_POINT_DIGIT_EXP;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_DIGIT_POINT_DIGIT_EXP:
                    if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_DIGIT_POINT_DIGIT_EXP_DIGIT;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_DIGIT_POINT_DIGIT_EXP_DIGIT:
                    if(c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_POINT_DIGIT:
                    if(c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                    } else if(c == 'e' || c == 'E') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_POINT_DIGIT_EXP;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_POINT_DIGIT_EXP:
                    if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_POINT_DIGIT_EXP_DIGIT;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_POINT_DIGIT_EXP_DIGIT:
                    if(c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_DIGIT_EXP:
                    if(c == '+' || c == '-' || c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                        lex_state = FLOAT_DIGIT_EXP_DIGIT;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case FLOAT_DIGIT_EXP_DIGIT:
                    if(c >= '0' && c <= '9') {
                        content += c;
                        num_content_size++;
                    } else if (c == 'f' || c == 'F') {
                        content += c;
                        fsize = FLOAT;
                        push_float_and_reset();
                        lex_state = START;
                    } else if (c == 'd' || c == 'D') {
                        content += c;
                        fsize = DOUBLE;
                        push_float_and_reset();
                        lex_state = START;
                    } else {
                        // TODO add other fp suffix handling
                        push_float_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case CHAR:
                    if (c == '\'') {
                        // TODO Test for empty char sequence
                        content += c;
                        lexemes.push_back(character(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else if (c == '\\') {
                        content += c;
                        saved_state = CHAR;
                        lex_state = ESCAPE;
                    } else {
                        // TODO Test for EOL or not printable char.
                        content += c;
                    }
                    break;
                case STRING:
                    if (c == '"') {
                        content += c;
                        lexemes.push_back(string(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else if (c == '\\') {
                        content += c;
                        saved_state = STRING;
                        lex_state = ESCAPE;
                    } else {
                        // TODO Test for EOL or not printable char.
                        content += c;
                    }
                    break;
                case ESCAPE:
                    if (c == '\'' || c == '"' || c == '?' || c == '\\'
                        || c == 'b' || c == 'f' || c == 'n' || c == 'r'
                        || c == 't' || c == 'v') {
                        content += c;
                        /* TODO emmit escape. */
                        lex_state = saved_state;
                        saved_state = START;
                    } else if (c >= '0' && c <= '7') {
                        content += c;
                        lex_temp_count = 1;
                        lex_state = ESCAPE_OCTAL;
                    } else if (c == 'x') {
                        content += c;
                        lex_temp_count = 0;
                        lex_state = ESCAPE_HEXA;
                    } else if (c == 'u') {
                        content += c;
                        lex_temp_count = 0;
                        lex_state = ESCAPE_UNIVERSAL;
                    } else if (c == 'U') {
                        content += c;
                        lex_temp_count = 0;
                        lex_state = ESCAPE_UNIVERSAL_LONG;
                    } else {
                        _logger.error(0x0009, pos, "Bad escape sequence '{}'", {content + c});
                        /* error : bad escape sequence character. */
                        // TODO throw exception
                    }
                    break;
                case ESCAPE_OCTAL:
                    if (c >= '0' && c <= '7') {
                        content += c;
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
                        content += c;
                        lex_temp_count++;
                        if (lex_temp_count == 2) {
                            // Exhaust hexa escape, return to lex.
                            lex_temp_count = 0;
                            lex_state = saved_state;
                            saved_state = START;
                        }
                    } else {
                        _logger.warning(0x000A, pos, "Incomplete hexa escape sequence '{}'", {content + c});
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
                        content += c;
                        lex_temp_count++;
                        if (lex_temp_count == 4) {
                            // Exhaust universal escape, return to lex.
                            lex_temp_count = 0;
                            lex_state = saved_state;
                            saved_state = START;
                        }
                    } else {
                        _logger.warning(0x000B, pos, "Incomplete universal escape sequence '{}'", {content + c});
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
                        content += c;
                        lex_temp_count++;
                        if (lex_temp_count == 8) {
                            // Exhaust universal escape, return to lex.
                            lex_temp_count = 0;
                            lex_state = saved_state;
                            saved_state = START;
                        }
                    } else {
                        _logger.warning(0x000C, pos, "Incomplete long universal escape sequence '{}'", {content + c});
                        // WARN/TODO : not a complete long universal escape.
                        lex_state = saved_state;
                        saved_state = START;
                        continue;
                    }
                    break;
                case INT_UNSIGNED_SUFFIX:
                    if (c == 's' || c == 'S') {
                        content += c;
                        size = SHORT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'i' || c == 'I') {
                        content += c;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else {
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG_SUFFIX:
                    if (c == 'l' || c == 'L') {
                        content += c;
                        size = LONGLONG;
                        push_integer_and_reset();
                        lex_state = START;
                    } else if (c == '6') {
                        content += c;
                        lex_state = INT_LONG64_SUFFIX;
                    } else if (c == '1') {
                        content += c;
                        lex_state = INT_LONG128A_SUFFIX;
                    } else {
                        size = LONG;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG64_SUFFIX:
                    if (c == '4') {
                        content += c;
                        size = LONG;
                        push_integer_and_reset();
                        lex_state = START;
                    } else {
                        _logger.warning(0x000D, pos, "Bad integer suffix '{}', expect character '4'", {content + c});
                        // TODO/ERROR Bad integer suffix, expect character '4'.
                        size = LONG;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG128A_SUFFIX:
                    if (c == '2') {
                        content += c;
                        lex_state = INT_LONG128B_SUFFIX;
                    } else {
                        _logger.warning(0x000E, pos, "Bad integer suffix '{}', expect character '2'", {content + c});
                        // TODO/ERROR Bad integer suffix, expect character '2'.
                        size = LONGLONG;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG128B_SUFFIX:
                    if (c == '8') {
                        content += c;
                        size = LONGLONG;
                        push_integer_and_reset();
                        lex_state = START;
                    } else {
                        _logger.warning(0x000F, pos, "Bad integer suffix '{}', expect character '8'", {content + c});
                        // TODO/ERROR Bad integer suffix, expect character '8'.
                        size = LONGLONG;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_BIGINT_SUFFIX:
                    if (c == 'i' || c == 'I') {
                        content += c;
                        size = BIGINT;
                        push_integer_and_reset();
                        lex_state = START;
                    } else {
                        // TODO handle byte size here
                        _logger.warning(0x0010, pos, "Bad big integer suffix '{}', expect character 'B'", {content + c});
                        // TODO/ERROR Bad integer suffix, expect character 'B'.
                        size = LONGLONG;
                        push_integer_and_reset();
                        lex_state = START;
                        continue;
                    }
                    break;
            }

            // Let's increment
            ++pos.pos;
            ++pos.col;
        }
    }

    void lexer::push_integer_and_reset() {
        lexemes.push_back(integer(begin, pos, content, num_prefix_size, num_content_size, base, unsigned_num, size));
        base = numeric_base::DECIMAL;
        unsigned_num = false;
        size = INT;
        num_prefix_size = num_content_size = 0;
        content.clear();
        begin = {0, 0, 0};
    }

    void lexer::push_float_and_reset() {
        lexemes.push_back(float_num(begin, pos, content, num_content_size, fsize));
        fsize = FLOAT_DEFAULT;
        num_content_size = 0;
        content.clear();
        begin = {0, 0, 0};
    }

    std::vector<any_lexeme> lexer::parse_all(std::string_view src) {
        parse(src);
        return lexemes;
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

    opt_ref_any_lexeme lexer::pick() {
        if(!lexemes.empty() && index<lexemes.size()-1) {
            return std::ref(lexemes[index+1]);
        }  else {
            return {};
        }
    }

    char_coord lexer::end_coord() const {
        if(lexemes.empty()) {
            return {};
        } else {
            return std::visit([](auto&& arg)->char_coord{return arg.end;}, lexemes.back());
        }
    }

    bool lexer::eof() const {
        return lexemes.empty() || index>=lexemes.size();
    }

    //
    // Lexeme logger facility
    //
    void lexeme_logger::info(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        _logger.info(_error_class|code, lexeme.start, lexeme.end, message, args);
    }

    void lexeme_logger::warning(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        _logger.warning(_error_class|code, lexeme.start, lexeme.end, message, args);
    }

    void lexeme_logger::error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        _logger.error(_error_class|code, lexeme.start, lexeme.end, message, args);
    }

    void lexeme_logger::info(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        if(lexeme) {
            const auto& lex = as_lexeme(lexeme);
            _logger.info(_error_class|code, lex.start, lex.end, message, args);
        } else {
            _logger.info(_error_class|code, /*_lexer.end_coord()*/ {}, message, args);
        }
    }

    void lexeme_logger::warning(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        if(lexeme) {
            const auto& lex = as_lexeme(lexeme);
            _logger.warning(_error_class|code, lex.start, lex.end, message, args);
        } else {
            _logger.warning(_error_class|code, /*_lexer.end_coord()*/ {}, message, args);
        }
    }

    void lexeme_logger::error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args) {
        if(lexeme) {
            const auto& lex = as_lexeme(lexeme);
            _logger.error(_error_class|code, lex.start, lex.end, message, args);
        } else {
            _logger.error(_error_class|code, /*_lexer.end_coord()*/ {}, message, args);
        }
    }

} // k::lex
