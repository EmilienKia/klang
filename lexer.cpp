//
// Created by Emilien Kia <emilien.kia+dev@gmail.com> on 25/07/2021.
//

#include "lexer.hpp"

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

        {"byte", keyword::BYTE},
        {"char", keyword::CHAR},
        {"short", keyword::SHORT},
        {"int", keyword::INT},
        {"long", keyword::LONG},
        {"float", keyword::FLOAT},
        {"double", keyword::DOUBLE}
    };

    const std::map<std::string, punctuator::type_t> punctuators {
        {"(", punctuator::PARENTHESIS_OPEN},
        {")", punctuator::PARENTHESIS_CLOSE},
        {"{", punctuator::BRACE_OPEN},
        {"}", punctuator::BRACE_CLOSE},
        {"[", punctuator::BRACKET_OPEN},
        {"]", punctuator::BRAKET_CLOSE},
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

    const std::set<char> operator_punctuator_chars{
            '(', ')', '{', '}', '[', ']', ';', ',', '.', '@',
            '?', ':', '!', '~', '=',
            '+', '-', '*', '/', '&', '|', '^', '%', '<', '>'
    };

    inline bool is_operator_punctuator_char(char c) {
        return operator_punctuator_chars.contains(c);
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
                    } else if (is_operator_punctuator_char(c)) {
                        begin = pos;
                        content = c;
                        lex_state = OPERATOR;
                    } else {
                        /* TODO */
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
                    //  - Make operator parsing specific (not accept anything at first pass)
                    //  - Allow to parse_all many punctuators like two parenthesis (or operators) in a row without separators (like spaces)
                    //  - Make punctuator/operator parsing statefull to able to support >> as shift operator or two closing chevrons
                    if (is_operator_punctuator_char(c)) {
                        content += c;
                    } else {
                        if (auto it = operators.find(content); it != operators.end()) {
                            lexemes.push_back(operator_(begin, pos, content, it->second));
                            content.clear();
                            begin = {0, 0, 0};
                            lex_state = START;
                            continue;
                        } else if (auto it = punctuators.find(content); it != punctuators.end()) {
                            lexemes.push_back(punctuator(begin, pos, content, it->second));
                            content.clear();
                            begin = {0, 0, 0};
                            lex_state = START;
                            continue;
                        } else {
                            /* Error, unknown punctuator nor operator. */
                        }
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
                        lex_state = HEXA_PREFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = BIN_PREFIX;
                    } else if (c == 'o' || c == 'O') {
                        content += c;
                        lex_state = OCTAL_PREFIX;
                    } else if (c >= '0' && c <= '7') {
                        content += c;
                        lex_state = OCTAL;
                    } else if (c >= '8' && c <= '9'
                               || c >= 'a' && c <= 'f'
                               || c >= 'A' && c <= 'F') {
                        /* Error : no Hexadec digit for octal number. */
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else {
                        // Emit "0" number
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case HEXA_PREFIX:
                    if (c >= '0' && c <= '9'
                        || c >= 'a' && c <= 'f'
                        || c >= 'A' && c <= 'F') {
                        content += c;
                        lex_state = HEXADECIMAL;
                    } else if (c == 'u' || c == 'U') {
                        // WARN should have at least one digit after prefix
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else {
                        /* Error, hexa number must have at least one digit. */
                    }
                    break;
                case BIN_PREFIX:
                    if (c == '0' || c == '1') {
                        content += c;
                        lex_state = BINARY;
                    } else {
                        /* Error, binary number must have at least one digit. */
                    }
                    break;
                case OCTAL_PREFIX:
                    if (c >= '0' && c <= '7') {
                        content += c;
                        lex_state = OCTAL;
                    } else {
                        /* Error, octal number must have at least one digit. */
                    }
                    break;
                case HEXADECIMAL:
                    if (c >= '0' && c <= '9'
                        || c >= 'a' && c <= 'f'
                        || c >= 'A' && c <= 'F'
                        || c == '_') {
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case DECIMAL:
                    if (c >= '0' && c <= '9'
                        || c == '_') {
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case OCTAL:
                    if (c >= '0' && c <= '7'
                        || c == '_') {
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case BINARY:
                    if (c >= '0' && c <= '1'
                        || c == '_') {
                        content += c;
                    } else if (c == 'u' || c == 'U') {
                        content += c;
                        saved_state = lex_state;
                        lex_state = INT_UNSIGNED_SUFFIX;
                    } else if (c == 's' || c == 'S') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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
                        /* error : bad escape sequence character. */
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
                        // WARN/TODO : not a complete long universal escape.
                        lex_state = saved_state;
                        saved_state = START;
                        continue;
                    }
                    break;
                case INT_UNSIGNED_SUFFIX:
                    if (c == 's' || c == 'S') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else if (c == 'l' || c == 'L') {
                        content += c;
                        lex_state = INT_LONG_SUFFIX;
                    } else if (c == 'b' || c == 'B') {
                        content += c;
                        lex_state = INT_BIGINT_SUFFIX;
                    } else {
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG_SUFFIX:
                    if (c == 'l' || c == 'L') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else if (c == '6') {
                        content += c;
                        lex_state = INT_LONG64_SUFFIX;
                    } else if (c == '1') {
                        content += c;
                        lex_state = INT_LONG128A_SUFFIX;
                    } else {
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG64_SUFFIX:
                    if (c == '4') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else {
                        // TODO/ERROR Bad integer suffix, expect character '4'.
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG128A_SUFFIX:
                    if (c == '2') {
                        content += c;
                        lex_state = INT_LONG128B_SUFFIX;
                    } else {
                        // TODO/ERROR Bad integer suffix, expect character '2'.
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                        continue;
                    }
                    break;
                case INT_LONG128B_SUFFIX:
                    if (c == '8') {
                        content += c;
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
                        lex_state = START;
                    } else {
                        // TODO/ERROR Bad integer suffix, expect character '8'.
                        lexemes.push_back(integer(begin, pos, content));
                        content.clear();
                        begin = {0, 0, 0};
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

    bool lexer::eof() const {
        return lexemes.empty() || index>=lexemes.size();
    }


} // k::lex
