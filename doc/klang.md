K programming language specification
====================================
* Document id: K00001-WD
* Status: working draft
* Editor: Emilien KIA, emilien.kia+dev@gmail.com, https://github.com/EmilienKia/klang
* Abstract: K programming language specification

Introduction {intro}
====================

K language introduction.

Lexical structure {lex}
=======================

This section presents the K programming language lexical structure.

Programs are writen in ASCII.

Line terminators are defined to support different conventions of existing platforms while maintaining constant line numbers.

The tokens are the identifiers, keywords, literals, separators, and operators of the syntactic grammar.

Line terminators {lex.lines}
----------------------------

K language compiler split file entries in lines by recognising line terminators.

Line Terminator
: A Line terminators is a character or sequence of characters separating two lines.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn >LineTerminator</dfn>:
        the ASCII LF character, also known as "newline"
        the ASCII CR character, also known as "return"
        the ASCII CR character followed by the ASCII LF character

    <dfn>InputCharacter</dfn>:
        ASCIIInputCharacter but not CR or LF
</pre>

White spaces {lex.whitespaces}
------------------------------
White space is defined as the ASCII space character, horizontal tab character, form feed character, and line terminator characters.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>WhiteSpace</dfn>:
        the ASCII SP character, also known as "space"
        the ASCII HT character, also known as "horizontal tab"
        the ASCII FF character, also known as "form feed"
        <a>LineTerminator</a>
</pre>

Input elements and tokens {lex.tokens}
--------------------------------------
The input characters are processed in a sequence of input elements.
Elements that are not whitespaces nor comments are tokens.
White spaces and comments can serve to separate tokens that, if adjacent can be tokenized in another manner.
Input elements are processed in an ordered way.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Input</dfn>:
        *<a>InputElement</a>

    <dfn>InputElement</dfn>:
        <a>WhiteSpace</a>
        <a>Comment</a>
        <a>Token</a>

    <dfn>Token</dfn>:
        <a>Identifier</a>
        <a>Keyword</a>
        <a>Literal</a>
        <a>Punctuator</a>
        <a>Operator</a>

</pre>

Comments {lex.comments}
-----------------------
There are two form of comments:
* End-of-line comment. As in C, C++ or Java, comment that start at double-slash pair of characters and end at the end of line (or end of file, if no end of line).
  For example: ``some code; // end of line comment``
* Multiline comment. As in C++ or Java, comment that start at slash-star pair of characters and end at star-slash par of character, whatever the number of lines. For example:
    ```
    Some code /* multi
    line
    comment */ other code;
    ```

Comment text is ignored.

Following rules apply:
* Comments are not nested
* `/*` or `*/` have no special meaning in end-of-line comment.
* `//` have no special meaning in multi-line comment.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Comment</dfn>:
        <a>MultiLineComment</a>
        <a>EndOfLineComment</a>

    <dfn>MultiLineComment</dfn>:
        '/' '*' <a>CommentTail</a>

    <dfn>CommentTail</dfn>:
        '*' <a>CommentTailStar</a>
        <a>NotStar</a> <a>CommentTail</a>

    <dfn>CommentTailStar</dfn>:
        '/'
        '*' <a>CommentTailStar</a>
        <a>NotStarNotSlash</a> <a>CommentTail</a>

    <dfn>NotStar</dfn>:
        <a>InputCharacter</a> but not '*'
        <a>LineTerminator</a>

    <dfn>NotStarNotSlash</dfn>:
        <a>InputCharacter</a> but not '*' or '/'
        <a>LineTerminator</a>

    <dfn>EndOfLineComment</dfn>:
        '/' '/' *<a>InputCharacter</a>
</pre>

Identifiers {lex.identifiers}
-----------------------------
Identifiers are unlimited-length sequence of identifier-letter and digits. The first one must be an identifier letter.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Identifier</dfn>:
        <a>IdentifierChars</a> but not a <a>Keyword</a> nor any sequence of underscore '_'

    <dfn>IdentifierChars</dfn>:
        <a>IdentifierLetter</a> *<a>IdentifierLetterOrDigit</a>

    <dfn>IdentifierLetter</dfn>:
        'A' to 'Z', 'a' to 'z', '_'

    <dfn>IdentifierLetterOrDigit</dfn>:
        'A' to 'Z', 'a' to 'z', '0' to '9', '_'
</pre>

An identifier letter is any letter from a to z, lowercase or uppercase, or the underscore ( '_' ).

An identifier letter or digit is any letter from a to z, lowercase or uppercase, any digit from 0 to 9 or the underscore ( '_' ).

An identifier cannot have the same spelling of any language keyword nor be a sequence of underscore ( '_' ).

Keywords {lex.keywords}
-----------------------
Keywords are reserved word of the language and cannot be used as identifier.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Keyword</dfn>: (one of)
        byte        char        short       int         long
        float       double
</pre>

Note: This section is **work in progress** and may be adjusted in the future.

<pre>
        abstract   continue   for          new         switch
        assert     default    if           package     synchronized
        boolean    do         goto         private     this
        break      double     implements   protected   throw
        byte       else       import       public      throws
        case       enum       instanceof   return      transient
        catch      extends    int          short       try
        char       final      interface    static      void
        class      finally    long         strictfp    volatile
        const      float      native       super       while
</pre>


Literals {lex.literals}
-----------------------
Literals
: Literals are the source code representation of values of primitive types, string type_t, or null type_t.

Note: This section is **work in progress** and may be adjusted in the future.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Literal</dfn>:
        <a>IntegerLiteral</a>
        <a>FloatingPointLiteral</a>
        <a>BooleanLiteral</a>
        <a>CharacterLiteral</a>
        <a>StringLiteral</a>
        <a>NullLiteral</a>
</pre>

### Integer literals {lex.literals.integers}
An integer literal may be expressed in decimal (base 10), hexadecimal (base 16), octal (base 8), or binary (base 2).

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>IntegerLiteral</dfn>:
        <a>DecimalIntegerLiteral</a>
        <a>HexIntegerLiteral</a>
        <a>OctalIntegerLiteral</a>
        <a>BinaryIntegerLiteral</a>

    <dfn>DecimalIntegerLiteral</dfn>:
        <a>DecimalNumeral</a> +<a>IntegerSuffix</a>

    <dfn>HexIntegerLiteral</dfn>:
        <a>HexNumeral</a> +<a>IntegerSuffix</a>

    <dfn>OctalIntegerLiteral</dfn>:
        <a>OctalNumeral</a> +<a>IntegerSuffix</a>

    <dfn>BinaryIntegerLiteral</dfn>:
        <a>BinaryNumeral</a> +<a>IntegerSuffix</a>

    <dfn>IntegerSuffix</dfn>:
        +<a>IntegerUnsignedSuffix</a> +<a>IntegerTypeSuffix</a>
        +<a>IntegerCustomSuffix</a>

    <dfn>IntegerUnsignedSuffix</dfn>: (one of)
        'u' 'U'

    <dfn>IntegerTypeSuffix</dfn>: (one of)
        's' 'S' 'i' 'I' 'l' 'L' 'll' 'LL' 'l64' 'L64' 'l128' 'L128' 'bi' 'BI'

    <dfn>IntegerCustomSuffix</dfn>: (to be defined)
</pre>

The unsigned suffix specify the integer is not signed. By default, an integer is signed.

The type_t suffix specify the size type_t of the integer.
's' or 'S' for short (16-bits).
'i' or 'I' for integer (32-bits).
'l', 'L', 'l64' or 'L64' for long (64-bits).
'll', 'LL', 'l128' or 'L128' for 128-bits long.

Note: 'bi' and 'BI' are reserved suffices for future big integer.

Underscores are allowed as separators between digits of integers. These underscores are not allowed
in front or rear of the integer.

Note: Custom suffices are not specified yet.

### Decimal literals {lex.literals.decimals}
A decimal numeral is either the single ASCII digit 0, representing the integer zero,
or consists of an ASCII digit from 1 to 9 optionally followed by one or more ASCII digits from 0 to 9
interspersed with underscores, representing a positive integer.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>DecimalNumeral</dfn>:
        0
        <a>NonZeroDigit</a> *<a>Digits</a>
        <a>NonZeroDigit</a> <a>Underscores</a> <a>Digits</a>

    <dfn>NonZeroDigit</dfn>: (one of)
        1 2 3 4 5 6 7 8 9

    <dfn>Digits</dfn>:
        <a>Digit</a>
        <a>Digit</a> ?<a>DigitsAndUnderscores</a> <a>Digit</a>

    <dfn>Digit</dfn>:
        0
        <a>NonZeroDigit</a>

    <dfn>DigitsAndUnderscores</dfn>:
        <a>DigitOrUnderscore</a> *<a>DigitOrUnderscore</a>

    <dfn>DigitOrUnderscore</dfn>:
        <a>Digit</a>
        '_'

    <dfn>Underscores</dfn>:
        +'_'
</pre>

### Hexadecimal literals {lex.literals.hexadecimals}
A hexadecimal numeral consist of the leading **0x** or **0X** prefix followed by one or more hexadecimal digits,
eventually separated with underscores.

Hexadecimal digits with values 10 to 15 are represented by letters 'a' to 'f'.
Each used letter may be either lowercase or uppercase independently.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>HexNumeral</dfn>:
        0 x <a>HexDigits</a>
        0 X <a>HexDigits</a>

    <dfn>HexDigits</dfn>:
        <a>HexDigit</a>
        <a>HexDigit</a> ?<a>HexDigitsAndUnderscores</a> <a>HexDigit</a>

    <dfn>HexDigit</dfn>: (one of)
        0 1 2 3 4 5 6 7 8 9 a b c d e f A B C D E F

    <dfn>HexDigitsAndUnderscores</dfn>:
        <a>HexDigitOrUnderscore</a> *<a>HexDigitOrUnderscore</a>

    <dfn>HexDigitOrUnderscore</dfn>:
        <a>HexDigit</a>
        '_'
</pre>

### Octal literals {lex.literals.octals}
An octal numeral consist of the leading **0o**, **0O** or **0** digit prefix followed by one or more octal digits,
eventually separated with underscores.
Octal digits are gists '0' to '7'.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>OctalNumeral</dfn>:
        0 o <a>OctalDigits</a>
        0 O <a>OctalDigits</a>
        0 <a>OctalDigits</a>

    <dfn>OctalDigits</dfn>:
        <a>OctalDigit</a>
        <a>OctalDigit</a> ?<a>OctalDigitsAndUnderscores</a> <a>OctalDigit</a>

    <dfn>OctalDigit</dfn>: (one of)
        0 1 2 3 4 5 6 7

    <dfn>OctalDigitsAndUnderscores</dfn>:
        <a>OctalDigitOrUnderscore</a> *<a>OctalDigitOrUnderscore</a>

    <dfn>OctalDigitOrUnderscore</dfn>:
        <a>OctalDigit</a>
        '_'
</pre>

### Binary literals {lex.literals.binary}
A binary numeral consist of the leading **0b** or **0B** prefix followed by one or more '0' or '1' digits,
eventually separated with underscores.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>BinaryNumeral</dfn>:
        0 b <a>BinaryDigits</a>
        0 B <a>BinaryDigits</a>

    <dfn>BinaryDigits</dfn>:
        <a>BinaryDigit</a>
        <a>BinaryDigit</a> ?<a>BinaryDigitsAndUnderscores</a> <a>BinaryDigit</a>

    <dfn>BinaryDigit</dfn>:
        0
        1

    <dfn>BinaryDigitsAndUnderscores</dfn>:
        <a>BinaryDigitOrUnderscore</a> *<a>BinaryDigitOrUnderscore</a>

    <dfn>BinaryDigitOrUnderscore</dfn>:
        <a>BinaryDigit</a>
        '_'
</pre>

Note: Add examples, specifications of min and max values depending on type_t and unsigned suffices.

### Floating-point literals {lex.literals.floats}

Floating-point literal
: A floating-point literal is composed of a whole-number part, a decimal point represented by the period ',' character,
a fraction part, an exponent and a type_t suffix. The floating-point literal is expressed in decimal (base 10).

A floating-point literal, at least one digit (in either the whole number or the fraction part) and either a decimal point,
an exponent, or a float type_t suffix are required.
All other parts are optional.
The exponent, if present, is indicated by the ASCII letter e or E followed by an optionally signed integer.

Underscores are allowed as separators between digits that denote the whole-number part,
and between digits that denote the fraction part, and between digits that denote the exponent.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>FloatingPointLiteral</dfn>:
        <a>Digits</a> . ?<a>Digits</a> ?<a>ExponentPart</a> ?<a>FloatTypeSuffix</a>
        . <a>Digits</a> ?<a>ExponentPart</a> ?<a>FloatTypeSuffix</a>
        <a>Digits</a> <a>ExponentPart</a> ?<a>FloatTypeSuffix</a>
        <a>Digits</a> ?<a>ExponentPart</a> <a>FloatTypeSuffix</a>

    <dfn>ExponentIndicator</dfn>: (one of)
        e E

    <dfn>SignedInteger</dfn>:
        ?<a>Sign</a> <a>Digits</a>

    <dfn>Sign</dfn>: (one of)
        + -

    <dfn>FloatTypeSuffix</dfn>: (one of)
        f F d D
</pre>

A floating-point literal is of type_t float if it is suffixed with an ASCII letter F or f;
otherwise its type_t is double and it can optionally be suffixed with an ASCII letter D or d.

The elements of the types float and double are those values that can be represented using the IEEE 754 32-bit single-precision
and 64-bit double-precision binary floating-point formats, respectively.

Note: Add max range, infinity and not-a-number definitions.

### Boolean literals {lex.literals.booleans}
The boolean type_t has two values, represented by the boolean literals **true** and **false**, formed from ASCII letters.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>BooleanLiteral</dfn>: (one of)
        true false
</pre>

### Character literals {lex.literals.chars}
A character literal is expressed as a character or an escape sequence, enclosed in ASCII single quotes.
(The single-quote, or apostrophe, character is \u0027.)

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>CharacterLiteral</dfn>:
         ' <a>SingleCharacter</a> '
         ' <a>EscapeSequence</a> '

    <dfn>SingleCharacter</dfn>:
         <a>InputCharacter</a> but not ' or \
</pre>

### String literals {lex.literals.strings}
A string literal consists of zero or more characters enclosed in double quotes.
Characters may be represented by escape sequences.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>StringLiteral</dfn>:
        " *<a>StringCharacter</a> "

    <dfn>StringCharacter</dfn>:
        <a>InputCharacter</a> but not " or \
        <a>EscapeSequence</a>
</pre>

It is a compile-time error for a line terminator to appear after the opening " and before the closing matching ".

### Escape sequences {lex.literals.escape}
The character and string escape sequences allow for the representation of some nongraphic characters without using
Unicode escapes, as well as the single quote, double quote, and backslash characters,
in character literals and string literals.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>EscapeSequence</dfn>:
         \ ' (single quote ', Unicode \u0027)
         \ " (double quote ", Unicode \u0022)
         \ ? (question mark ", Unicode \u003f)
         \ \ (backslash \, Unicode \u005c)
         \ b (backspace BS, Unicode \u0008)
         \ f (form feed FF, Unicode \u000c)
         \ n (linefeed LF, Unicode \u000a)
         \ r (carriage return CR, Unicode \u000d)
         \ t (horizontal tab HT, Unicode \u0009)
         \ v (vertical tab HT, Unicode \u000b)
         <a>OctalEscape</a>
         <a>HexaEscape</a>
         <a>UniversalEscape</a>

    <dfn>OctalEscape</dfn>:
        \ 3*<a>OctalDigit</a>""

    <dfn>HexaEscape</dfn>:
        \ x 2*<a>HexDigit</a>

    <dfn>UniversalEscape</dfn>:
        \ u 4*<a>HexDigit</a>
        \ U 8*<a>HexDigit</a>
</pre>

### Null literal {lex.literals.null}
Nil reference is represented by the null literal.

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>NullLiteral</dfn>: null
</pre>

Punctuators {lex.punctuators}
-----------------------------

Punctuators (or separators) are specific ASCII symbols:

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Punctuator</dfn>: one of
    (   )   {   }   [   ]   ;   ,   ...   @
</pre>

Operators {lex.operators}
-------------------------

Operators are specific ASCII symbols:

<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>
    <dfn>Operator</dfn>: one of
    . ->   ?   :   !   ~  =
    +   -   *   /   &   |   ^   %   <<   >>
    +=  -=  *=  /=  &=  |=  ^=  %=  <<=  >>=
    ==  !=  >   <   >=  <=   <=>
    &&  ||  ++  -- ::
</pre>


Declarations {decl}
===================
TODO !!

Statements {stmt}
=================
TODO !!


Expressions {expr}
==================


<pre dfn-type_t=grammar link-type_t=grammar class=grammar highlight=abnf>

    <dfn>Expression</dfn>:
        <a>AssignmentExpression</a> *[ ',' <a>AssignmentExpression</a> ]

    <dfn>AssignmentExpression</dfn>:
        <a>ConditionalExpression</a> ?[ <a>AssignmentOperator</a> <a>AssignmentExpression</a>  ]

    <dfn>AssignmentOperator</dfn>: one of
        = *= /= %= += -= >>= <<= &= ^= |=

    <dfn>ConditionalExpression</dfn>:
        <a>LogicalOrExpression</a> ?[ '?' <a>ConditionalExpression</a> ':' <a>ConditionalExpression</a> ]

    <dfn>LogicalOrExpression</dfn>:
        <a>LogicalAndExpression</a> *[ '||' <a>LogicalAndExpression</a> ]

    <dfn>LogicalAndExpression</dfn>:
        <a>InclusiveBitwiseOrExpression</a> *[ '&&' <a>InclusiveBitwiseOrExpression</a> ]

    <dfn>InclusiveBitwiseOrExpression</dfn>:
        <a>ExclusiveBitwiseOrExpression</a> *[ '|' <a>ExclusiveBitwiseOrExpression</a> ]
    
    <dfn>ExclusiveBitwiseOrExpression</dfn>:
        <a>BitwiseAndExpression</a> *[ '^' <a>BitwiseAndExpression</a> ]

    <dfn>BitwiseAndExpression</dfn>:
        <a>EqualityExpression</a> *[ '&' <a>EqualityExpression</a> ]

    <dfn>EqualityExpression</dfn>:
        <a>RelationalExpression</a> *[ ('=='|'!=') <a>RelationalExpression</a> ]

    <dfn>RelationalExpression</dfn>:
        <a>ShiftingExpression</a> *[ ('<'|'>'|'<='|'>=') <a>ShiftingExpression</a> ]

    <dfn>ShiftingExpression</dfn>:
        <a>AdditiveExpression</a> *[ ('<<'|'>>') <a>AdditiveExpression</a> ]

    <dfn>AdditiveExpression</dfn>:
        <a>MultiplicativeExpression</a> *[ ('+'|'-') <a>MultiplicativeExpression</a> ]

    <dfn>MultiplicativeExpression</dfn>:
        <a>PointerToMemberExpression</a> *[ ('*'|'/'|'%') <a>PointerToMemberExpression</a> ]

    <dfn>PointerToMemberExpression</dfn>:
        <a>CastExpression</a> *[ ('.*'|'->*') <a>CastExpression</a> ]

    <dfn>CastExpression</dfn>:
        '(' <a>TypeSpecifier</a> ')' <a>CastExpression</a>
      | <a>UnaryExpression</a>

    <dfn>UnaryExpression</dfn>:
        ('++'|'--'|'*'|'&'|'+'|'-'|'!'|'~') <a>CastExpression</a>
      | <a>PostfixExpression</a>

    <dfn>PostfixExpression</dfn>:
        <a>PrimaryExpression</a> *[ '++' | '--'
                                    | [ '[' <a>Expression</a> ']' ]
                                    | [ '(' ?(<a>ExpressionList</a>) ')' ]
                                    | [ ('.'|'->') <a>IdentifierExpression</a> ]
                                  ]

    <dfn>ExpressionList</dfn>:
      <a>AssignmentExpression</a> *[ ',' <a>AssignmentExpression</a> ]

    <dfn>PrimaryExpression</dfn>:
        <a>Literal</a>
      | 'this'
      | '(' <a>Expression</a> ')'
      | <a>IdentifierExpression</a>
      
    <dfn>IdentifierExpression</dfn>:
        ?'::' <a>Identifier</a> *[ '::' <a>Identifier</a> ]

</pre>

