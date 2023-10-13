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

    inline bool is_whitespace(char c) {
        return c == ' ' || c == '\t' || c== '\f';
    }

    enum numeric_base {
        DECIMAL = 10,
        HEXADECIMAL = 16,
        OCTAL = 8,
        BINARY = 2
    };

    enum integer_size {
        BYTE = 8, // 8 bits
        SHORT = 16, // 16 bits
        INT = 32, // 32 bits
        LONG = 64, // 64 bits
        LONGLONG = 128, // 128 bits
        BIGINT = 256 // Big integer
    };

    enum float_size {
        FLOAT = 32, // 32 bits
        DOUBLE = 64, // 64 bits

        FLOAT_DEFAULT =  FLOAT
    };

    struct char_coord
    {
        std::size_t pos = 0;
        std::size_t line = 0;
        std::size_t col = 0;
    };

    /**
     * Trivial offset for char coordinate.
     * Assume this offset doesn't change the column.
     * @param coord Initial coordinates
     * @param offset Offset to apply.
     * @return New coordinates
     */
    inline char_coord operator+(const char_coord& coord, size_t offset) {
        return {.pos = coord.pos + offset, .line = coord.line, .col = coord.line+offset};
    }

    /**
     * Trivial offset for char coordinate.
     * Assume this offset doesn't change the column.
     * @param coord Initial coordinates
     * @param offset Offset to apply.
     * @return Updated this coordinates
     */
    inline char_coord& operator+=(char_coord& coord, size_t offset) {
        coord.pos += offset;
        coord.col += offset;
        return coord;
    }

    struct lexeme
    {
        char_coord start;
        char_coord end;
        std::string content;

        lexeme(const lexeme& other) : start(other.start), end(other.end), content(other.content) {}
        lexeme(lexeme&& other) : start(std::move(other.start)), end(std::move(other.end)), content(std::move(other.content)) {}
        lexeme& operator=(const lexeme& other) = default;
        lexeme& operator=(lexeme&& other) = default;
        lexeme(char_coord start, char_coord end, const std::string& content) : start(start), end(end), content(content) {}

        // For testing only
        lexeme(const std::string& content) : content(content) {}

    protected:
        lexeme() = default;
    };

    /**
     * Trivial lexeme/string comparison test.
     * @param lex Lexeme to look for
     * @param val String value to look for.
     * @return True if lexeme's content is equal to the expected val.
     */
    inline bool operator==(const lexeme& lex, const std::string_view& val) {
        return lex.content == val;
    }

    struct identifier : public lexeme {
        identifier(const identifier& other) = default;
        identifier(identifier&& other) = default;
        identifier& operator=(const identifier& other) = default;
        identifier& operator=(identifier&& other) = default;
        identifier(char_coord start, char_coord end, const std::string& content) : lexeme(start, end, content) {}

        // For testing only
        identifier(const std::string& content) : lexeme(content) {}

        bool operator == (const identifier& other) const {
            return this->content == other.content;
        }

    protected:
        identifier() : lexeme() {}; // Let accessible to enable optional default constructible
    };

    struct keyword : public lexeme {

        enum type_t {
            MODULE,
            IMPORT,
            NAMESPACE,
            PUBLIC,
            PROTECTED,
            PRIVATE,
            STATIC,
            CONST,
            ABSTRACT,
            FINAL,
            THIS,
            RETURN,
            BOOL,
            BYTE,
            CHAR,
            SHORT,
            INT,
            LONG,
            FLOAT,
            DOUBLE,
            UNSIGNED,
            IF,
            ELSE,
            WHILE,
            FOR
            /* TODO add new lexeme definition here. */
        };

        type_t type;

        keyword(const keyword& other) = default;
        keyword(keyword&& other) = default;
        keyword& operator=(const keyword& other) = default;
        keyword& operator=(keyword&& other) = default;
        keyword(char_coord start, char_coord end, const std::string& content, type_t type) : lexeme(start, end, content), type(type) {}
    protected:
        keyword() = default;
    };

    inline bool operator==(const keyword& obj1, const keyword& obj2) {
        return obj1.type == obj2.type;
    }

    struct literal : public lexeme {
        using lexeme::lexeme;

        virtual k::value_type value()const = 0;
    };

    struct integer : public literal {
        using literal::literal;

        size_t num_prefix_size = 0;
        size_t num_content_size = 0;

        numeric_base base = DECIMAL;
        bool unsigned_num = false;
        integer_size size = INT;

        integer(char_coord start, char_coord end, const std::string& content,
                size_t num_prefix_size = 0, size_t num_content_size = 0,
                numeric_base base = DECIMAL, bool unsigned_num = false, integer_size size = INT):
                literal(start, end, content),
                num_prefix_size(num_prefix_size), num_content_size(num_content_size),
                base(base), unsigned_num(unsigned_num), size(size) {}

        std::string_view int_content() const {
            return {content.begin()+num_prefix_size, content.begin()+num_prefix_size+num_content_size};
        }

        k::value_type value()const override;
    };

    struct float_num : public literal {
        using literal::literal;

        size_t num_content_size = 0;
        float_size size = FLOAT;

        float_num(char_coord start, char_coord end, const std::string& content,
                  size_t num_content_size = 0, float_size size = FLOAT):
                literal(start, end, content),
                num_content_size(num_content_size),
                size(size) {}

        std::string_view float_content() const {
            return {content.begin(), content.begin()+num_content_size};
        }

        // TODO add all content and accessors
        k::value_type value()const override;
    };

    struct character : public literal {
        using literal::literal;

        k::value_type value()const override;
    };

    struct string : public literal {
        using literal::literal;

        k::value_type value()const override;
    };

    struct boolean : public literal {
        using literal::literal;

        k::value_type value()const override;
    };

    struct null : public literal {
        using literal::literal;

        k::value_type value()const override;
    };


    struct comment : public lexeme {
        using lexeme::lexeme;
    };

    struct punctuator : public lexeme {
        enum type_t {
            PARENTHESIS_OPEN,
            PARENTHESIS_CLOSE,
            BRACE_OPEN,
            BRACE_CLOSE,
            BRACKET_OPEN,
            BRAKET_CLOSE,
            SEMICOLON,
            COMMA,
            DOUBLE_COLON,
            ELLIPSIS,
            AT_SIGN
        };

        type_t type;

        punctuator(const punctuator& other) = default;
        punctuator(punctuator&& other) = default;
        punctuator& operator=(const punctuator& other) = default;
        punctuator& operator=(punctuator&& other) = default;
        punctuator(char_coord start, char_coord end, const std::string& content, type_t type) : lexeme(start, end, content), type(type) {}
    protected:
        punctuator() = default;
    };

    inline bool operator==(const punctuator& obj1, const punctuator& obj2) {
        return obj1.type == obj2.type;
    }

    struct operator_ : public lexeme {
        enum type_t {
            DOT,
            ARROW,
            DOT_STAR,
            ARROW_STAR,
            QUESTION_MARK,
            COLON,
            EXCLAMATION_MARK,
            TILDE,
            EQUAL,
            PLUS,
            MINUS,
            STAR,
            SLASH,
            AMPERSAND,
            PIPE,
            CARET,
            PERCENT,
            DOUBLE_CHEVRON_OPEN,
            DOUBLE_CHEVRON_CLOSE,
            PLUS_EQUAL,
            MINUS_EQUAL,
            STAR_EQUAL,
            SLASH_EQUAL,
            AMPERSAND_EQUAL,
            PIPE_EQUAL,
            CARET_EQUAL,
            PERCENT_EQUAL,
            DOUBLE_CHEVRON_OPEN_EQUAL,
            DOUBLE_CHEVRON_CLOSE_EQUAL,
            DOUBLE_EQUAL,
            EXCLAMATION_MARK_EQUAL,
            CHEVRON_OPEN,
            CHEVRON_CLOSE,
            CHEVRON_OPEN_EQUAL,
            CHEVRON_CLOSE_EQUAL,
            CHEVRON_OPEN_EQUAL_CHEVRON_CLOSE,
            DOUBLE_AMPERSAND,
            DOUBLE_PIPE,
            DOUBLE_PLUS,
            DOUBLE_MINUS,
            DOUBLE_STAR
        };

        type_t type;

        operator_(const operator_& other) = default;
        operator_(operator_&& other) = default;
        operator_& operator=(const operator_& other) = default;
        operator_& operator=(operator_&& other) = default;
        operator_(char_coord start, char_coord end, const std::string& content, type_t type) : lexeme(start, end, content), type(type) {}
    protected:
        operator_() = default;
    };

    inline bool operator==(const operator_& obj1, const operator_& obj2) {
        return obj1.type == obj2.type;
    }


    extern const std::set<std::string> keyword_set;

    typedef std::variant<keyword, identifier, character, string, integer, float_num, boolean, null, comment, punctuator, operator_> any_lexeme;
//    typedef k::helpers::any_of<lexeme, keyword, identifier, character, string, integer, boolean, null, comment, punctuator, operator_> any_lexeme;

    typedef anyof::any_of<literal, integer, float_num, character, string, boolean, null> any_literal;

    enum any_literal_type_index {
        INTEGER = 0,
        FLOAT_NUM = 1,
        CHARACTER = 2,
        STRING = 3,
        BOOLEAN = 4,
        NUL = 5,
        NOT_DEFINED = any_literal::npos
    };

    typedef std::optional<lex::any_lexeme> opt_any_lexeme;

    typedef std::reference_wrapper<lex::any_lexeme> ref_any_lexeme;

    typedef std::optional<std::reference_wrapper<lex::any_lexeme>> opt_ref_any_lexeme;


    inline bool operator==(const any_lexeme& lex, keyword::type_t type) {
        return std::holds_alternative<keyword>(lex) && std::get<keyword>(lex).type==type;
    }

    inline bool operator!=(const any_lexeme& lex, keyword::type_t type) {
        return !std::holds_alternative<keyword>(lex) || std::get<keyword>(lex).type!=type;
    }

    inline bool operator==(const any_lexeme& lex, punctuator::type_t type) {
        return std::holds_alternative<punctuator>(lex) && std::get<punctuator>(lex).type==type;
    }

    inline bool operator!=(const any_lexeme& lex, punctuator::type_t type) {
        return !std::holds_alternative<punctuator>(lex) || std::get<punctuator>(lex).type!=type;
    }

    inline bool operator==(const any_lexeme& lex, operator_::type_t type) {
        return std::holds_alternative<operator_>(lex) && std::get<operator_>(lex).type==type;
    }

    inline bool operator!=(const any_lexeme& lex, operator_::type_t type) {
        return !std::holds_alternative<operator_>(lex) || std::get<operator_>(lex).type!=type;
    }

    inline bool operator==(const opt_ref_any_lexeme& lex, keyword::type_t type) {
        return lex.has_value() && std::holds_alternative<keyword>(lex->get()) && std::get<keyword>(lex->get()).type==type;
    }

    inline bool operator!=(const opt_ref_any_lexeme& lex, keyword::type_t type) {
        return !lex.has_value() || !std::holds_alternative<keyword>(lex->get()) || std::get<keyword>(lex->get()).type!=type;
    }

    template<keyword::type_t...Keywords>
    inline bool is_one_of(const opt_ref_any_lexeme& lex) {
        return lex.has_value() && std::holds_alternative<keyword>(lex->get()) && ( (std::get<keyword>(lex->get()).type==Keywords) ||...);
    }

    inline bool operator==(const opt_ref_any_lexeme& lex, punctuator::type_t type) {
        return lex.has_value() && std::holds_alternative<punctuator>(lex->get()) && std::get<punctuator>(lex->get()).type==type;
    }

    inline bool operator!=(const opt_ref_any_lexeme& lex, punctuator::type_t type) {
        return !lex.has_value() || !std::holds_alternative<punctuator>(lex->get()) || std::get<punctuator>(lex->get()).type!=type;
    }

    template<punctuator::type_t...Punctuators>
    inline bool is_one_of(const opt_ref_any_lexeme& lex) {
        return lex.has_value() && std::holds_alternative<punctuator>(lex->get()) && ( (std::get<punctuator>(lex->get()).type==Punctuators) ||...);
    }

    inline bool operator==(const opt_ref_any_lexeme& lex, operator_::type_t type) {
        return lex.has_value() && std::holds_alternative<operator_>(lex->get()) && std::get<operator_>(lex->get()).type==type;
    }

    inline bool operator!=(const opt_ref_any_lexeme& lex, operator_::type_t type) {
        return !lex.has_value() || !std::holds_alternative<operator_>(lex->get()) || std::get<operator_>(lex->get()).type!=type;
    }

    template<operator_::type_t...Operators>
    inline bool is_one_of(const opt_ref_any_lexeme& lex) {
        return lex.has_value() && std::holds_alternative<operator_>(lex->get()) && ( (std::get<operator_>(lex->get()).type==Operators) ||...);
    }

    template<operator_::type_t...Operators>
    inline bool is_none_of(const opt_ref_any_lexeme& lex) {
        return !lex.has_value() || !std::holds_alternative<operator_>(lex->get()) || ( (std::get<operator_>(lex->get()).type!=Operators) && ...);
    }

    template<class Type>
    inline bool is(const any_lexeme& lex) {
        return std::holds_alternative<Type>(lex);
    }

    template<class Type>
    inline bool is_not(const any_lexeme& lex) {
        return !std::holds_alternative<Type>(lex);
    }

    template<class Type>
    inline bool is(const ref_any_lexeme& lexref) {
        return std::holds_alternative<Type>(lexref.get());
    }

    template<class Type>
    inline bool is_not(const ref_any_lexeme& lexref) {
        return !std::holds_alternative<Type>(lexref.get());
    }

    template<class Type>
    inline bool is(const opt_ref_any_lexeme& optlexref) {
        return optlexref.has_value() && std::holds_alternative<Type>(optlexref.value().get());
    }

    template<class Type>
    inline bool is_not(const opt_ref_any_lexeme& optlexref) {
        return !optlexref.has_value() || !std::holds_alternative<Type>(optlexref.value().get());
    }

    template<class Type>
    inline bool is(const opt_any_lexeme& optlex) {
        return optlex.has_value() && std::holds_alternative<Type>(optlex.value());
    }

    template<class Type>
    inline bool is_not(const opt_any_lexeme& optlex) {
        return !optlex.has_value() || !std::holds_alternative<Type>(optlex.value());
    }

    template<class Type>
    inline const Type& as(const any_lexeme& lex) {
        return std::get<Type>(lex);
    }

    template<class Type>
    inline const Type& as(const ref_any_lexeme& lexref) {
        return std::get<Type>(lexref.get());
    }

    template<class Type>
    inline const Type& as(const opt_ref_any_lexeme& optlexref) {
        return std::get<Type>(optlexref.value().get());
    }

    template<class Type>
    inline const Type& as(const opt_any_lexeme& optlex) {
        return std::get<Type>(optlex.value());
    }

    // Abstract literal-specific
    template<>
    inline bool is<literal>(const any_lexeme& lex) {
        return std::holds_alternative<integer>(lex)
               || std::holds_alternative<float_num>(lex)
               || std::holds_alternative<character>(lex)
               || std::holds_alternative<string>(lex)
               || std::holds_alternative<boolean>(lex)
               || std::holds_alternative<null>(lex)
                ;
    }
    template<>
    inline bool is<literal>(const ref_any_lexeme& lexref) {
        return is<literal>(lexref.get());
    }
    template<>
    inline bool is<literal>(const opt_ref_any_lexeme& optlexref) {
        return optlexref.has_value() && is<literal>(optlexref.value().get());
    }
    template<>
    inline bool is<literal>(const opt_any_lexeme& optlex) {
        return optlex.has_value() && is<literal>(optlex.value());
    }

    inline lex::any_literal as_any_literal(const lex::opt_ref_any_lexeme& optlexref) {
        any_lexeme& reflex = optlexref->get();
        if(std::holds_alternative<integer>(reflex)) {
            return lex::any_literal{std::get<integer>(reflex)};
        } else if(std::holds_alternative<float_num>(reflex)) {
            return lex::any_literal{std::get<float_num>(reflex)};
        } else if(std::holds_alternative<character>(reflex)) {
            return lex::any_literal{std::get<character>(reflex)};
        } else if(std::holds_alternative<string>(reflex)) {
            return lex::any_literal{std::get<string>(reflex)};
        } else if(std::holds_alternative<boolean>(reflex)) {
            return lex::any_literal{std::get<boolean>(reflex)};
        } else if(std::holds_alternative<null>(reflex)) {
            return lex::any_literal{std::get<null>(reflex)};
        } else {
            return {};
        }
    }


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


    inline const lex::lexeme& as_lexeme(const lex::opt_ref_any_lexeme& lexeme) {
        const auto& lex = lexeme.value().get();
        return std::visit([](auto&& arg)->const lex::lexeme&{return (const lex::lexeme&)arg;}, lex);
    }

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
