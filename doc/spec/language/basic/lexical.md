# Lexical Conventions

[← Index](../index.md)

K source files are written in ASCII.  
The compiler transforms the character stream into a sequence of tokens, discarding white space and ordinary comments.
Documentation comments are retained as declaration documentation.
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
Ordinary comment text is ignored by the compiler; documentation comments are described below.
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

### Documentation comments

The documentation forms use the existing comment syntax:

| Form | Attachment |
|------|------------|
| `/// text` | Next declaration |
| `/** text */` | Next declaration |
| `//! text` | Previous declaration |
| `/*! text */` | Previous declaration |

A one-line documentation comment may provide an inline brief. Consecutive
documentation comments are merged in source order. The first free-text paragraph
is the **brief**; subsequent free-text paragraphs form the **description**.
Outside Markdown code, a line starting with `@tag` or `\tag` introduces a tagged
section, whose content continues until the next tag. Tag names are
case-insensitive; examples include `@param`, `@return` and `@throws`.

Extraction must preserve Markdown layout rather than render Markdown:

- A line comment loses its three-character marker and at most one following
  padding space. Further indentation and all trailing spaces belong to its text.
- A block comment loses its opening and closing delimiters, at most one padding
  space after the opening delimiter, and at most one padding space immediately
  before an inline closing delimiter. For example, `/** Brief. */` stores
  `Brief.` without delimiter padding; two Markdown hard-break spaces before
  that padding must be written as three spaces before `*/`.
- Multiline block content loses its common leading indentation. Inline text
  after the opening marker establishes the baseline of an undecorated block,
  so an indented code-only continuation must not lose its code indentation. Aligned
  conventional `*` or `!` decoration and one following padding space are removed
  when used throughout the block. A decoration marker must be followed by
  whitespace or the end of its line; Markdown emphasis and image markers are
  not decoration. Indentation after decoration is retained.
  Paired matching `===`-style framing banners are removed. Markdown rules,
  heading underlines and code fences are not general-purpose border decoration.
- Relative indentation, internal blank lines (including consecutive blanks)
  and trailing spaces on content lines must be retained in the brief,
  description and tagged-section continuations. Blank lines framing a section
  are not part of that section.
- Backtick and tilde code fences, and code indented by at least four columns
  (a tab advances to the next four-column stop), protect tag-like text from
  directive recognition. Fences may follow list or blockquote container prefixes;
  their content indentation remains intact. A closing fence must use the opening
  character and contain at least as many markers. Blank lines inside a fence do not split
  the brief from the description. Leaving a list or blockquote container ends
  its unclosed fence; an unclosed top-level fence extends to the end of the
  documentation.

For named tags such as `@param value`, `@throws Error` and `@tparam T`, the
description may begin on the next line. Mapping the tag's name and description
consumes the name-line separator without removing subsequent blank lines,
indentation or trailing spaces.

For example, the block below contains a brief, a Markdown description and a
parameter section. The `@param` inside the fence is literal example text, not
another documentation directive.

````k
/**
 * Compute a value.
 *
 * - First step
 *   - Nested step
 *
 * ```k
 * @param literal_example
 *     nested_call();
 * ```
 *
 * @param value input value
 *
 *     indented_example();
 */
compute(value: int) : int;
````

These extraction rules do not change the language's comment delimiters or
declaration grammar.

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
    .    ->   .*   ->*   ?    :    !    ~    #
    =    +    -    *    /    &    |    ?    %
    <<   >>
    +=   -=   *=   /=   &=   |=   ^=   %=   <<=   >>=
    ==   !=   <    >    <=   >=
    &&   ||   ++   --   **
```

---

*See also:* [Keywords](keywords.md) · [Literals](../expressions/literals.md) · [Names and Lookup](names.md)
