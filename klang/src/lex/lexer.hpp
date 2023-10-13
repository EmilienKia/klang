/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#ifndef KLANG_LEXER_HPP
#define KLANG_LEXER_HPP

#include "../common/any_of.hpp"
#include "../common/common.hpp"
#include "lexemes.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace k::log {
class logger;
}

namespace k::lex {

    class lexer {
    public:
        enum LEX_STATE {
            START,
            CR,
            IDENTIFIER,
            ZERO,
            BIN_PREFIX,
            OCTAL_PREFIX,
            HEXA_PREFIX,
            BINARY,
            OCTAL,
            DECIMAL,
            HEXADECIMAL,
            SLASH,
            POINT,
            COMMENT_SINGLE_LINE,
            COMMENT_MULTI_LINES,
            COMMENT_MULTI_LINES_END,
            CHAR,
            STRING,
            ESCAPE,
            ESCAPE_OCTAL,
            ESCAPE_HEXA,
            ESCAPE_UNIVERSAL,
            ESCAPE_UNIVERSAL_LONG,
            INT_UNSIGNED_SUFFIX,
            INT_LONG_SUFFIX,
            INT_LONG64_SUFFIX,
            INT_LONG128A_SUFFIX,
            INT_LONG128B_SUFFIX,
            INT_BIGINT_SUFFIX,
            FLOAT_DIGIT_POINT_DIGIT,
            FLOAT_DIGIT_POINT_DIGIT_EXP,
            FLOAT_DIGIT_POINT_DIGIT_EXP_DIGIT,
            FLOAT_POINT_DIGIT,
            FLOAT_POINT_DIGIT_EXP,
            FLOAT_POINT_DIGIT_EXP_DIGIT,
            FLOAT_DIGIT_EXP,
            FLOAT_DIGIT_EXP_DIGIT,
            OPERATOR
        };
    protected:

        k::log::logger& _logger;

        std::vector<any_lexeme> lexemes;

        /** Current lexer state. */
        LEX_STATE lex_state = START;
        /** Saved lexer state. Used to process escape sequence in chars and strings.*/
        LEX_STATE saved_state = START;

        /** Temporary counter, used for repeatable states like integer escaping.*/
        size_t lex_temp_count = 0;

        std::string content;

        char_coord pos {0, 0, 0};
        char_coord begin;

        size_t index = 0;

        numeric_base base = numeric_base::DECIMAL;
        bool unsigned_num = false;
        integer_size size = INT;
        size_t num_prefix_size = 0;
        size_t num_content_size = 0;

        float_size fsize = FLOAT;

    private:
        /// List of all chars used in operators or punctuators
        static std::set<char> operator_punctuator_chars;
        /// Test if a char can be part of an operator or a punctuator
        inline static  bool is_operator_punctuator_char(char c) {
            return operator_punctuator_chars.contains(c);
        }

        /// Type of operator or punctuator
        typedef std::variant<punctuator::type_t, operator_::type_t> punct_or_op_type_t;
        /// Sorting operator for longer tokens come before their prefix (<<= come before <<, << come before <)
        struct less_order_op_punct_for_lookup {
            bool operator()(const std::string &a, const std::string &b) const;
        };
        /// Specific map of punctuator or operator type indexed by their token string
        /// This map is ordered to ensure, when it is iterated, longer tokens come before their prefix (<<= come before <<, << come before <)
        /// Enable faster chained operator-punctuator parsing
        static std::map<std::string, punct_or_op_type_t, lexer::less_order_op_punct_for_lookup> puncts_or_ops;

        void push_integer_and_reset();
        void push_float_and_reset();

        static void init();

    public:
        lexer(k::log::logger& logger);

        void parse(std::string_view src);

        std::vector<any_lexeme> parse_all(std::string_view src);

        opt_ref_any_lexeme get();
        void unget(size_t count = 1);

        size_t tell() const;
        void seek(size_t pos);

        opt_ref_any_lexeme pick();

        char_coord end_coord() const;

        bool eof() const;

    };

    class lex_holder {
    protected:
        lexer& _lexer;
        size_t _index;

    public:
        lex_holder(lexer& lex):_lexer(lex),_index(lex.tell()) {}
        void sync() { _index = _lexer.tell(); }
        void rollback() { _lexer.seek(_index); }
    };

    class lexeme_logger {
    protected:
        k::log::logger& _logger;
        unsigned long _error_class;

        lexeme_logger(k::log::logger& logger,unsigned long error_class) : _logger(logger), _error_class(error_class) {}

        // TODO Link it to source container to get lines and end-of-file marker.

        void info(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});
        void warning(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});
        void error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});

        void info(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});
        void warning(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});
        void error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {});
    };


} // k::lex
#endif //KLANG_LEXER_HPP
