# Lexical Conventions

[← Index](../index.md)

K source files are written in ASCII.  
The compiler transforms the character stream into a sequence of tokens, discarding white space and comments.  
Tokens are then consumed by the parser to build the program structure.

---

## Contents

1. [Source encoding](#1-source-encoding)
2. [Line terminators](#2-line-terminators)
3. [White space](#3-white-space)
4. [Comments](#4-comments)
5. [Tokens](#5-tokens)

---

## 1. Source encoding

K source files use the ASCII character set.

---

## 2. Line terminators

Line terminators are used to separate lines and are significant for error reporting.

### Grammar

```
LineTerminator:
    LF          (ASCII 0x0A, "newline")
    CR          (ASCII 0x0D, "return")
    CR LF

InputCharacter:
    any ASCII character except CR or LF
```

---

## 3. White space

White space consists of the space character, horizontal tab, form feed, and line terminators.  
White space separates adjacent tokens but is otherwise discarded.

### Grammar

```
WhiteSpace:
    SP          (ASCII 0x20, "space")
    HT          (ASCII 0x09, "horizontal tab")
    FF          (ASCII 0x0C, "form feed")
    LineTerminator
```

---

## 4. Comments

K supports two comment forms: end-of-line comments and block comments.  
Comment text is ignored by the compiler.  
Comments are **not** nestable.

### End-of-line comment

Starts with `//` and extends to the end of the current line (or end of file).

```k
x : int = 5;  // this is ignored
```

### Block comment

Starts with `/*` and ends with the first occurrence of `*/`, regardless of the number of lines.

```k
/*
 * Multi-line comment.
 * Everything here is ignored.
 */
a : int = 42;
```

**Constraints:**
- `/*` and `*/` have no special meaning inside an end-of-line comment.
- `//` has no special meaning inside a block comment.
- Block comments cannot be nested: a `/*` inside a block comment does not start a new one.

### Grammar

```
Comment:
    MultiLineComment
    EndOfLineComment

MultiLineComment:
    '/*' CommentTail

CommentTail:
    '*' CommentTailStar
    NotStar CommentTail

CommentTailStar:
    '/'
    '*' CommentTailStar
    NotStarNotSlash CommentTail

NotStar:
    InputCharacter but not '*'
    LineTerminator

NotStarNotSlash:
    InputCharacter but not '*' or '/'
    LineTerminator

EndOfLineComment:
    '//' { InputCharacter }
```

---

## 5. Tokens

After removing white space and comments, the remaining input forms a sequence of tokens.

### Grammar

```
Input:
    { InputElement }

InputElement:
    WhiteSpace
    Comment
    Token

Token:
    Identifier
    Keyword
    Literal
    Punctuator
    Operator
```

### Identifiers

An identifier is a non-empty sequence of identifier letters and digits, starting with a letter.

```
Identifier:
    IdentifierChars   (but not a Keyword, nor a sequence of underscores only)

IdentifierChars:
    IdentifierLetter { IdentifierLetterOrDigit }

IdentifierLetter:
    'A'-'Z'  |  'a'-'z'  |  '_'

IdentifierLetterOrDigit:
    'A'-'Z'  |  'a'-'z'  |  '0'-'9'  |  '_'
```

**Constraints:**
- An identifier must not have the same spelling as a [keyword](keywords.md).
- An identifier that consists solely of underscores (`_`, `__`, `___`, …) is reserved and cannot be used in user code.

**Examples:**

```k
x
counter
my_variable
_hidden        // valid identifier starting with underscore followed by letters
MyStruct
```

### Keywords

See the [Keywords](keywords.md) reference page for the complete list.

### Literals

See the [Literals](../expressions/literals.md) reference page.

### Punctuators

```
Punctuator: (one of)
    (   )   {   }   [   ]   ;   ,   ::   ...   @
```

| Token  | Name                |
|--------|---------------------|
| `(`    | Parenthesis open    |
| `)`    | Parenthesis close   |
| `{`    | Brace open          |
| `}`    | Brace close         |
| `[`    | Bracket open        |
| `]`    | Bracket close       |
| `;`    | Semicolon           |
| `,`    | Comma               |
| `::`   | Double colon (scope resolution) |
| `...`  | Ellipsis            |
| `@`    | At-sign             |

### Operators

```
Operator: (one of)
    .    ->   .*   ->*   ?    :    !    ~
    =    +    -    *    /    &    |    ?    %
    <<   >>
    +=   -=   *=   /=   &=   |=   ^=   %=   <<=   >>=
    ==   !=   <    >    <=   >=
    &&   ||   ++   --   **
```

---

*See also:* [Keywords](keywords.md) · [Literals](../expressions/literals.md) · [Names and Lookup](names.md)

