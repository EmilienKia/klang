# Keywords

[← Index](../index.md) · [Lexical Conventions](lexical.md)

Keywords are reserved identifiers with special syntactic meaning.
They may **not** be used as ordinary identifiers.

---

## Complete keyword list

| Keyword     | Description |
|-------------|-------------|
| `bool`      | Boolean primitive type |
| `byte`      | 8-bit signed integer type |
| `char`      | 8-bit signed character type (alias of `byte`) |
| `short`     | 16-bit signed integer type |
| `int`       | 32-bit signed integer type |
| `long`      | 64-bit signed integer type |
| `float`     | 32-bit IEEE 754 floating-point type |
| `double`    | 64-bit IEEE 754 floating-point type |
| `unsigned`  | Unsigned modifier for integer types |
| `struct`    | Structure type declaration |
| `namespace` | Namespace declaration |
| `module`    | Module (compilation unit) name declaration |
| `import`    | Import declaration |
| `static`    | Static storage / static member modifier |
| `const`     | Constant qualifier: marks a variable or parameter as immutable after construction |
| `public`    | Public visibility modifier |
| `protected` | Protected visibility modifier |
| `private`   | Private visibility modifier |
| `abstract`  | Abstract modifier (reserved) |
| `final`     | Final modifier (reserved) |
| `this`      | Reference to the current object inside a member function |
| `return`    | Return statement |
| `if`        | Conditional statement |
| `else`      | Alternative branch of an `if` statement |
| `while`     | Condition-controlled loop |
| `for`       | Counter-controlled loop |

### Grammar

```
Keyword: (one of)
    bool     byte     char     short    int      long
    float    double   unsigned
    struct   namespace   module   import
    static   const    abstract   final
    public   protected   private
    this     return
    if       else     while    for
```

---

## Notes

- `abstract` and `final` are reserved keywords but their full semantics are not yet defined.
- `const` marks a variable or parameter as immutable. See [Const-ness](types.md#12-const-ness) for the full specification.
- `public`, `protected`, and `private` are parsed as visibility declarations inside namespace and struct bodies; they end with a colon (e.g., `public:`).
- `unsigned` is used as a type modifier: `unsigned int`, `unsigned short`, etc. It is not a standalone type.
- All other keywords are used in declarations or statements as described in the relevant reference sections.

---

*See also:* [Lexical Conventions](lexical.md) · [Types](types.md) · [Statements Overview](../statements/statements.md)
