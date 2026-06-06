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

#ifndef KLANG_LEXEMES_HPP
#define KLANG_LEXEMES_HPP

#include "../common/any_of.hpp"
#include "../common/common.hpp"

#include <algorithm>
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


struct lexeme
{
    std::string_view content;

    lexeme() = default;
    lexeme(const lexeme&) = default;
    lexeme(lexeme&&) = default;
    lexeme& operator=(const lexeme& other) = default;
    lexeme& operator=(lexeme&& other) = default;
    explicit lexeme(const std::string_view& str)  : content(str) {}

protected:
    friend class k::log::logger;
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
    explicit identifier(const std::string_view& str)  : lexeme(str) {}

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
        FOR,
        BREAK,
        CONTINUE,
        STRUCT,
        CLASS,
        INTERFACE,
        DEFAULT,
        DELETE,
        NEW,
        ENUM,
        OPERATOR,
        USING,
        FRIEND,
        ANNOTATION,
        OVERRIDE,
        TEMPLATE,
        TYPENAME,
        GENERIC,
        UNION,
        THROW,
        TRY,
        CATCH,
        THROWS,
        FINALLY
    };

    type_t type;

    keyword(const keyword& other) = default;
    keyword(keyword&& other) = default;
    keyword& operator=(const keyword& other) = default;
    keyword& operator=(keyword&& other) = default;
    keyword(const std::string_view& str, type_t type)  : lexeme(str), type(type) {}

    template<typename Coll>
    static bool has(const Coll& coll, type_t kw) {
        return std::find(coll.begin(), coll.end(), kw) != coll.end();
    }
protected:
    keyword() = default;
};

inline bool operator==(const keyword& obj1, const keyword& obj2) {
    return obj1.type == obj2.type;
}

/**
 * Encoding prefix of a character or string literal.
 *
 *   unspecified  no prefix — element type is determined from context
 *   utf8         'u8'  prefix → array of unsigned byte
 *   utf16        'u' / 'u16' prefix → array of unsigned short
 *   utf32        'U' / 'u32' prefix → array of char (Unicode code points)
 */
enum class literal_encoding {
    unspecified,
    utf8,
    utf16,
    utf32
};

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

    integer(const std::string_view& content,
        size_t num_prefix_size = 0, size_t num_content_size = 0,
        numeric_base base = DECIMAL, bool unsigned_num = false, integer_size size = INT):
        literal(content),
        num_prefix_size(num_prefix_size), num_content_size(num_content_size),
        base(base), unsigned_num(unsigned_num), size(size) {}

    std::string_view int_content() const {
        return {content.begin()+num_prefix_size, content.begin()+num_prefix_size+num_content_size};
    }

    k::value_type value()const override;

    unsigned int to_unsigned_int() const;
};

struct float_num : public literal {
    using literal::literal;

    size_t num_content_size = 0;
    float_size size = FLOAT;

    float_num(const std::string_view& content,
              size_t num_content_size = 0, float_size size = FLOAT):
            literal(content),
            num_content_size(num_content_size),
            size(size) {}

    std::string_view float_content() const {
        return {content.begin(), content.begin()+num_content_size};
    }

    // TODO add all content and accessors
    k::value_type value()const override;
};

struct character : public literal {
    /** Encoding prefix (unspecified, utf8, utf16, utf32). */
    literal_encoding enc = literal_encoding::unspecified;

    character(const std::string_view& content, literal_encoding enc = literal_encoding::unspecified)
        : literal(content), enc(enc) {}

    k::value_type value()const override;

    /**
     * Decode the literal body (a single character, after stripping the quotes
     * and the optional prefix) into a Unicode code point, interpreting escape
     * sequences and multi-byte UTF-8. Returns U+FFFD on malformed input.
     */
    char32_t code_point() const;
};

struct string : public literal {
    /** Encoding prefix (unspecified, utf8, utf16, utf32). */
    literal_encoding enc = literal_encoding::unspecified;

    string(const std::string_view& content, literal_encoding enc = literal_encoding::unspecified)
        : literal(content), enc(enc) {}

    k::value_type value()const override;

    /**
     * Decode the literal body (after stripping the quotes and the optional
     * prefix) into a sequence of Unicode code points, interpreting escape
     * sequences and multi-byte UTF-8.
     */
    std::vector<char32_t> code_points() const;
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

/**
 * Documentation comment lexeme.
 *
 * Captures one of the four doc-comment forms emitted by the lexer:
 *   - LINE_FWD  ///  — single-line, documents the next element
 *   - LINE_BWD  //!  — single-line, documents the previous element
 *   - BLOCK_FWD /** — block, documents the next element
 *   - BLOCK_BWD /*! — block, documents the previous element
 *
 * content holds the raw source text including the opening marker
 * (and closing * / for block forms).
 */
struct doc_comment : public lexeme {
    enum class doc_type {
        LINE_FWD,   ///< /// form
        LINE_BWD,   ///< //! form
        BLOCK_FWD,  ///< /** form
        BLOCK_BWD   ///< /*! form
    };

    doc_type type;

    doc_comment(const doc_comment&) = default;
    doc_comment(doc_comment&&) = default;
    doc_comment& operator=(const doc_comment&) = default;
    doc_comment& operator=(doc_comment&&) = default;

    doc_comment(const std::string_view& content, doc_type type)
        : lexeme(content), type(type) {}
};

struct punctuator : public lexeme {
    enum type_t {
        PARENTHESIS_OPEN,
        PARENTHESIS_CLOSE,
        BRACE_OPEN,
        BRACE_CLOSE,
        BRACKET_OPEN,
        BRACKET_CLOSE,
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
    punctuator(const std::string_view& content, type_t type) : lexeme(content), type(type) {}
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
        DOUBLE_STAR,
        HASH
    };

    type_t type;

    operator_(const operator_& other) = default;
    operator_(operator_&& other) = default;
    operator_& operator=(const operator_& other) = default;
    operator_& operator=(operator_&& other) = default;
    operator_(const std::string_view& content, type_t type) : lexeme(content), type(type) {}
protected:
    operator_() = default;
};

inline bool operator==(const operator_& obj1, const operator_& obj2) {
    return obj1.type == obj2.type;
}


extern const std::set<std::string> keyword_set;

typedef std::variant<keyword, identifier, character, string, integer, float_num, boolean, null, comment, doc_comment, punctuator, operator_> any_lexeme;
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

inline const lex::lexeme& as_lexeme(const lex::opt_ref_any_lexeme& lexeme) {
    const auto& lex = lexeme.value().get();
    return std::visit([](auto&& arg)->const lex::lexeme&{return (const lex::lexeme&)arg;}, lex);
}


} // k::lex
#endif //KLANG_LEXEMES_HPP
